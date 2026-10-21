#include "imagejxx/image.hpp"
#include "kernel_control.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <queue>
#include <stdexcept>

namespace imagejxx {
Image wand_mask(const Image &image, int x, int y, double tolerance,
                int connectivity) {
  require_gray(image);
  require_finite(tolerance, "Wand tolerance");
  if (tolerance < 0 || (connectivity != 4 && connectivity != 8))
    throw std::invalid_argument(
        "Wand requires nonnegative tolerance and 4/8 connectivity");
  const double target = image.at(x, y);
  return with_kernel_stage(
      "Wand validation", image.pixels.size(), [&](const auto &validation) {
        for (std::size_t i = 0; i < image.pixels.size(); ++i) {
          validation.periodic(i);
          require_finite(image.pixels[i], "Wand pixel");
        }
        validation.finish();
        const auto traversal =
            validation.child("Wand traversal", image.pixels.size());
        Image mask(image.width, image.height, 1, PixelType::UInt8);
        std::vector<std::size_t> queue{
            static_cast<std::size_t>(y) * image.width + x};
        mask.pixels[queue[0]] = 255;
        for (std::size_t index = 0; index < queue.size(); ++index) {
          traversal.periodic(index);
          int px = static_cast<int>(queue[index] % image.width),
              py = static_cast<int>(queue[index] / image.width);
          for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
              if ((dx == 0 && dy == 0) ||
                  (connectivity == 4 && dx != 0 && dy != 0))
                continue;
              int xx = px + dx, yy = py + dy;
              if (xx >= 0 && yy >= 0 && xx < image.width && yy < image.height &&
                  mask.at(xx, yy) == 0 &&
                  std::abs(image.at(xx, yy) - target) <= tolerance) {
                mask.at(xx, yy) = 255;
                queue.push_back(static_cast<std::size_t>(yy) * image.width +
                                xx);
              }
            }
        }
        traversal.checkpoint(queue.size());
        return mask;
      });
}
namespace {
std::vector<std::array<int, 2>> neighbors(int connectivity) {
  if (connectivity != 4 && connectivity != 8)
    throw std::invalid_argument("Connectivity must be 4 or 8");
  std::vector<std::array<int, 2>> out;
  for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
      if ((x || y) && (connectivity == 8 || x == 0 || y == 0))
        out.push_back({x, y});
  return out;
}
std::size_t index(int x, int y, int width) {
  return static_cast<std::size_t>(y) * width + x;
}
void check_labels(const Labels &labels) {
  if (labels.width <= 0 || labels.height <= 0 || labels.count < 0 ||
      labels.pixels.size() !=
          static_cast<std::size_t>(labels.width) * labels.height)
    throw std::invalid_argument("Invalid label image shape");
  with_kernel_stage(
      "Label validation", labels.pixels.size(), [&](const auto &stage) {
        for (std::size_t i = 0; i < labels.pixels.size(); ++i) {
          stage.periodic(i);
          if (labels.pixels[i] < 0)
            throw std::invalid_argument("Labels must be nonnegative");
        }
        stage.finish();
      });
}
struct DistanceWorkspace {
  std::vector<int> sites;
  std::vector<double> boundaries, output;
};
template <class Stage>
const std::vector<double> &
distance_pass(const std::vector<double> &input, double spacing,
              DistanceWorkspace &workspace, const Stage &parent) {
  int n = static_cast<int>(input.size()), k = -1;
  double a = spacing * spacing,
         infinity = std::numeric_limits<double>::infinity();
  const auto stage = parent.child("Distance sites", input.size());
  auto &sites = workspace.sites;
  auto &boundaries = workspace.boundaries;
  auto &out = workspace.output;
  sites.resize(input.size());
  boundaries.resize(input.size() + 1);
  out.resize(input.size());
  for (int q = 0; q < n; ++q) {
    stage.periodic(q);
    if (!std::isfinite(input[q]))
      continue;
    double boundary = -infinity;
    while (k >= 0) {
      stage.poll();
      int v = sites[k];
      boundary =
          (input[q] - input[v] +
           a * (static_cast<double>(q) * q - static_cast<double>(v) * v)) /
          (2 * a * (q - v));
      if (boundary > boundaries[k])
        break;
      --k;
    }
    ++k;
    sites[k] = q;
    boundaries[k] = k == 0 ? -infinity : boundary;
    boundaries[k + 1] = infinity;
  }
  stage.finish();
  if (k < 0) {
    const auto empty = stage.child("Distance empty output", input.size());
    for (std::size_t i = 0; i < out.size(); ++i) {
      empty.periodic(i);
      out[i] = infinity;
    }
    empty.finish();
    return out;
  }
  const auto evaluation = stage.child("Distance evaluation", input.size());
  int current = 0;
  for (int q = 0; q < n; ++q) {
    evaluation.periodic(q);
    while (current < k && boundaries[current + 1] < q) {
      evaluation.poll();
      ++current;
    }
    double delta = q - sites[current];
    out[q] = a * delta * delta + input[sites[current]];
  }
  evaluation.finish();
  return out;
}
} // namespace

Labels connected_components(const Image &mask, int connectivity) {
  require_gray(mask);
  auto offsets = neighbors(connectivity);
  return with_kernel_stage(
      "Component scan", mask.pixels.size(), [&](const auto &scan) {
        const auto traversal =
            scan.child("Component traversal", mask.pixels.size());
        Labels out{mask.width, mask.height, 0,
                   std::vector<std::int32_t>(mask.pixels.size(), 0)};
        std::vector<std::size_t> queue;
        std::size_t visited = 0;
        for (std::size_t start = 0; start < mask.pixels.size(); ++start) {
          scan.periodic(start);
          if (mask.pixels[start] == 0 || out.pixels[start] != 0)
            continue;
          if (out.count == std::numeric_limits<std::int32_t>::max())
            throw std::invalid_argument("Too many connected components");
          ++out.count;
          out.pixels[start] = out.count;
          queue.clear();
          queue.push_back(start);
          for (std::size_t head = 0; head < queue.size(); ++head) {
            traversal.periodic(visited++);
            auto p = queue[head];
            int x = static_cast<int>(p % mask.width),
                y = static_cast<int>(p / mask.width);
            for (auto d : offsets) {
              int xx = x + d[0], yy = y + d[1];
              if (xx < 0 || yy < 0 || xx >= mask.width || yy >= mask.height)
                continue;
              auto q = index(xx, yy, mask.width);
              if (mask.pixels[q] != 0 && out.pixels[q] == 0) {
                out.pixels[q] = out.count;
                queue.push_back(q);
              }
            }
          }
        }
        traversal.checkpoint(visited);
        scan.finish();
        return out;
      });
}
std::vector<Particle> region_properties(const Labels &labels,
                                        const Image *intensity, double sx,
                                        double sy) {
  check_labels(labels);
  require_finite(sx, "X spacing");
  require_finite(sy, "Y spacing");
  if (sx <= 0 || sy <= 0)
    throw std::invalid_argument("Pixel spacing must be positive");
  if (intensity) {
    require_gray(*intensity);
    if (intensity->width != labels.width || intensity->height != labels.height)
      throw std::invalid_argument(
          "Intensity and labels must have matching shapes");
  }
  struct Accumulator {
    Particle particle;
    double x = 0, y = 0, xx = 0, yy = 0, xy = 0, sum = 0;
    int xmax = 0, ymax = 0;
  };
  return with_kernel_stage(
      "Region accumulation", labels.pixels.size(),
      [&](const auto &accumulation) {
        std::map<int, Accumulator> objects;
        for (int y = 0; y < labels.height; ++y)
          for (int x = 0; x < labels.width; ++x) {
            auto position = index(x, y, labels.width);
            int label = labels.pixels[position];
            accumulation.periodic(position);
            if (label == 0)
              continue;
            auto &accumulator = objects[label];
            auto &p = accumulator.particle;
            double value = intensity ? intensity->pixels[position] : 0;
            if (p.pixels == 0) {
              p.label = label;
              p.x = accumulator.xmax = x;
              p.y = accumulator.ymax = y;
              p.minimum = p.maximum = value;
            }
            ++p.pixels;
            p.x = std::min(p.x, x);
            p.y = std::min(p.y, y);
            accumulator.xmax = std::max(accumulator.xmax, x);
            accumulator.ymax = std::max(accumulator.ymax, y);
            accumulator.x += x;
            accumulator.y += y;
            accumulator.xx += static_cast<double>(x) * x;
            accumulator.yy += static_cast<double>(y) * y;
            accumulator.xy += static_cast<double>(x) * y;
            accumulator.sum += value;
            p.minimum = std::min(p.minimum, value);
            p.maximum = std::max(p.maximum, value);
            p.touches_border = p.touches_border || x == 0 || y == 0 ||
                               x == labels.width - 1 || y == labels.height - 1;
            if (x == 0 || labels.pixels[index(x - 1, y, labels.width)] != label)
              p.perimeter += sy;
            if (x == labels.width - 1 ||
                labels.pixels[index(x + 1, y, labels.width)] != label)
              p.perimeter += sy;
            if (y == 0 || labels.pixels[index(x, y - 1, labels.width)] != label)
              p.perimeter += sx;
            if (y == labels.height - 1 ||
                labels.pixels[index(x, y + 1, labels.width)] != label)
              p.perimeter += sx;
          }
        accumulation.finish();
        const auto measurements =
            accumulation.child("Region measurements", objects.size());
        std::vector<Particle> out;
        std::size_t measured = 0;
        for (auto &entry : objects) {
          measurements.periodic(measured++);
          auto &a = entry.second;
          auto &p = a.particle;
          double mx = a.x / p.pixels, my = a.y / p.pixels;
          p.width = a.xmax - p.x + 1;
          p.height = a.ymax - p.y + 1;
          p.area = p.pixels * sx * sy;
          p.centroid_x = mx * sx;
          p.centroid_y = my * sy;
          p.mean = a.sum / p.pixels;
          p.integrated_density = a.sum * sx * sy;
          double vx = std::max(0.0, a.xx / p.pixels - mx * mx) * sx * sx;
          double vy = std::max(0.0, a.yy / p.pixels - my * my) * sy * sy;
          double covariance = (a.xy / p.pixels - mx * my) * sx * sy;
          double difference = std::hypot(vx - vy, 2 * covariance);
          double major = (vx + vy + difference) / 2,
                 minor = std::max(0.0, (vx + vy - difference) / 2);
          p.eccentricity =
              major > 0 ? std::sqrt(std::max(0.0, 1 - minor / major)) : 0;
          p.circularity = p.perimeter > 0 ? 4 * std::acos(-1.0) * p.area /
                                                (p.perimeter * p.perimeter)
                                          : 0;
          out.push_back(p);
        }
        measurements.finish();
        return out;
      });
}
Image fill_holes(const Image &mask, int connectivity) {
  require_gray(mask);
  auto offsets = neighbors(connectivity);
  const auto border_units =
      2 * (static_cast<std::size_t>(mask.width) + mask.height);
  return with_kernel_stage(
      "Hole border seeds", border_units, [&](const auto &border) {
        std::vector<bool> visited(mask.pixels.size(), false);
        std::vector<std::size_t> queue;
        auto visit = [&](int x, int y) {
          auto p = index(x, y, mask.width);
          if (!visited[p] && mask.pixels[p] == 0) {
            visited[p] = true;
            queue.push_back(p);
          }
        };
        for (int x = 0; x < mask.width; ++x) {
          border.periodic(static_cast<std::size_t>(x) * 2);
          visit(x, 0);
          visit(x, mask.height - 1);
        }
        for (int y = 0; y < mask.height; ++y) {
          border.periodic((static_cast<std::size_t>(mask.width) + y) * 2);
          visit(0, y);
          visit(mask.width - 1, y);
        }
        border.finish();
        const auto traversal =
            border.child("Hole traversal", mask.pixels.size());
        for (std::size_t head = 0; head < queue.size(); ++head) {
          traversal.periodic(head);
          auto p = queue[head];
          int x = static_cast<int>(p % mask.width),
              y = static_cast<int>(p / mask.width);
          for (auto d : offsets) {
            int xx = x + d[0], yy = y + d[1];
            if (xx >= 0 && yy >= 0 && xx < mask.width && yy < mask.height)
              visit(xx, yy);
          }
        }
        traversal.checkpoint(queue.size());
        const auto output = border.child("Hole output", mask.pixels.size());
        Image out(mask.width, mask.height, 1, PixelType::UInt8);
        for (std::size_t i = 0; i < out.pixels.size(); ++i) {
          output.periodic(i);
          out.pixels[i] = mask.pixels[i] != 0 || !visited[i] ? 255 : 0;
        }
        output.finish();
        return out;
      });
}
Image skeletonize(const Image &mask) {
  require_gray(mask);
  return with_kernel_stage(
      "Skeleton input", mask.pixels.size(), [&](const auto &input) {
        Image out(mask.width, mask.height, 1, PixelType::UInt8);
        for (std::size_t i = 0; i < out.pixels.size(); ++i) {
          input.periodic(i);
          out.pixels[i] = mask.pixels[i] != 0 ? 1 : 0;
        }
        input.finish();
        const std::array<std::array<int, 2>, 8> offsets = {{{0, -1},
                                                            {1, -1},
                                                            {1, 0},
                                                            {1, 1},
                                                            {0, 1},
                                                            {-1, 1},
                                                            {-1, 0},
                                                            {-1, -1}}};
        bool changed;
        do {
          changed = false;
          for (int pass = 0; pass < 2; ++pass) {
            const auto thinning =
                input.child("Skeleton thinning", out.pixels.size());
            std::vector<std::size_t> remove;
            for (int y = 0; y < out.height; ++y)
              for (int x = 0; x < out.width; ++x) {
                thinning.periodic(index(x, y, out.width));
                if (out.at(x, y) == 0)
                  continue;
                std::array<int, 8> p{};
                int count = 0, transitions = 0;
                for (int j = 0; j < 8; ++j) {
                  p[j] = out.sample(x + offsets[j][0], y + offsets[j][1], 0,
                                    Border::Constant) != 0;
                  count += p[j];
                }
                for (int j = 0; j < 8; ++j)
                  if (!p[j] && p[(j + 1) % 8])
                    ++transitions;
                bool condition =
                    pass == 0
                        ? p[0] * p[2] * p[4] == 0 && p[2] * p[4] * p[6] == 0
                        : p[0] * p[2] * p[6] == 0 && p[0] * p[4] * p[6] == 0;
                if (count >= 2 && count <= 6 && transitions == 1 && condition)
                  remove.push_back(index(x, y, out.width));
              }
            thinning.finish();
            const auto removal = input.child("Skeleton removal", remove.size());
            for (std::size_t i = 0; i < remove.size(); ++i) {
              removal.periodic(i);
              out.pixels[remove[i]] = 0;
            }
            removal.finish();
            changed = changed || !remove.empty();
          }
        } while (changed);
        const auto output = input.child("Skeleton output", out.pixels.size());
        for (std::size_t i = 0; i < out.pixels.size(); ++i) {
          output.periodic(i);
          out.pixels[i] *= 255;
        }
        output.finish();
        return out;
      });
}
Image binary_outline(const Image &mask) {
  require_gray(mask);
  return with_kernel_stage(
      "Outline input", mask.pixels.size(), [&](const auto &stage) {
        Image binary(mask.width, mask.height, 1, PixelType::UInt8);
        for (std::size_t i = 0; i < binary.pixels.size(); ++i) {
          stage.periodic(i);
          binary.pixels[i] = mask.pixels[i] != 0 ? 255 : 0;
        }
        stage.finish();
        return calculate(
            binary, morphology(binary, "erode", 1, false, Border::Constant),
            "subtract");
      });
}
Image remove_small_objects(const Image &mask, std::uint64_t min_size,
                           int connectivity) {
  auto labels = connected_components(mask, connectivity);
  return with_kernel_stage(
      "Object sizes", labels.pixels.size(), [&](const auto &counting) {
        std::vector<std::uint64_t> sizes(
            static_cast<std::size_t>(labels.count) + 1, 0);
        for (std::size_t i = 0; i < labels.pixels.size(); ++i) {
          counting.periodic(i);
          ++sizes[labels.pixels[i]];
        }
        counting.finish();
        const auto output =
            counting.child("Object filtering", mask.pixels.size());
        Image out(mask.width, mask.height, 1, PixelType::UInt8);
        for (std::size_t i = 0; i < out.pixels.size(); ++i) {
          output.periodic(i);
          out.pixels[i] =
              labels.pixels[i] != 0 && sizes[labels.pixels[i]] >= min_size ? 255
                                                                           : 0;
        }
        output.finish();
        return out;
      });
}
Image clear_border(const Image &mask, int connectivity) {
  auto labels = connected_components(mask, connectivity);
  auto properties = region_properties(labels);
  return with_kernel_stage(
      "Border object selection", properties.size(), [&](const auto &selection) {
        std::vector<bool> remove(static_cast<std::size_t>(labels.count) + 1,
                                 false);
        for (std::size_t i = 0; i < properties.size(); ++i) {
          selection.periodic(i);
          remove[properties[i].label] = properties[i].touches_border;
        }
        selection.finish();
        const auto output =
            selection.child("Border object filtering", mask.pixels.size());
        Image out(mask.width, mask.height, 1, PixelType::UInt8);
        for (std::size_t i = 0; i < out.pixels.size(); ++i) {
          output.periodic(i);
          out.pixels[i] =
              labels.pixels[i] != 0 && !remove[labels.pixels[i]] ? 255 : 0;
        }
        output.finish();
        return out;
      });
}
Image distance_transform(const Image &mask, double sx, double sy) {
  require_gray(mask);
  require_finite(sx, "X spacing");
  require_finite(sy, "Y spacing");
  if (sx <= 0 || sy <= 0 || !std::isfinite(sx * sx) ||
      !std::isfinite(sy * sy) || sx * sx == 0 || sy * sy == 0)
    throw std::invalid_argument("Spacing must have finite positive squares");
  return with_kernel_stage(
      "Distance rows", static_cast<std::size_t>(mask.height),
      [&](const auto &rows) {
        Image temp(mask.width, mask.height, 1, PixelType::Float64), out = temp;
        std::vector<double> line(static_cast<std::size_t>(mask.width));
        DistanceWorkspace workspace;
        for (int y = 0; y < mask.height; ++y) {
          rows.periodic(y);
          const auto input = rows.child("Distance row input", line.size());
          for (int x = 0; x < mask.width; ++x) {
            input.periodic(x);
            line[x] = mask.at(x, y) == 0
                          ? 0
                          : std::numeric_limits<double>::infinity();
          }
          input.finish();
          const auto &transformed = distance_pass(line, sx, workspace, rows);
          const auto output = rows.child("Distance row output", line.size());
          for (int x = 0; x < mask.width; ++x) {
            output.periodic(x);
            temp.at(x, y) = transformed[x];
          }
          output.finish();
        }
        rows.finish();
        const auto columns = rows.child("Distance columns",
                                        static_cast<std::size_t>(mask.width));
        line.resize(static_cast<std::size_t>(mask.height));
        for (int x = 0; x < mask.width; ++x) {
          columns.periodic(x);
          const auto input = rows.child("Distance column input", line.size());
          for (int y = 0; y < mask.height; ++y) {
            input.periodic(y);
            line[y] = temp.at(x, y);
          }
          input.finish();
          const auto &transformed = distance_pass(line, sy, workspace, columns);
          const auto output = rows.child("Distance column output", line.size());
          for (int y = 0; y < mask.height; ++y) {
            output.periodic(y);
            out.at(x, y) = std::sqrt(transformed[y]);
          }
          output.finish();
        }
        columns.finish();
        return out;
      });
}
Image local_maxima(const Image &image, int radius) {
  require_gray(image);
  require_radius(radius);
  if (radius == 0)
    throw std::invalid_argument("Maxima radius must be positive");
  auto maximum = rank_filter(image, radius, "maximum", true),
       minimum = rank_filter(image, radius, "minimum", true);
  return with_kernel_stage(
      "Maxima output", image.pixels.size(), [&](const auto &stage) {
        Image out(image.width, image.height, 1, PixelType::UInt8);
        for (std::size_t i = 0; i < out.pixels.size(); ++i) {
          stage.periodic(i);
          out.pixels[i] = image.pixels[i] == maximum.pixels[i] &&
                                  image.pixels[i] > minimum.pixels[i]
                              ? 255
                              : 0;
        }
        stage.finish();
        return out;
      });
}
Labels watershed(const Image &height, const Labels &markers, const Image *mask,
                 int connectivity) {
  require_gray(height);
  check_labels(markers);
  auto offsets = neighbors(connectivity);
  if (markers.width != height.width || markers.height != height.height)
    throw std::invalid_argument("Markers and height must have matching shapes");
  if (mask) {
    require_gray(*mask);
    require_same_shape(height, *mask);
  }
  return with_kernel_stage(
      "Watershed seeds", markers.pixels.size(), [&](const auto &seeds) {
        Labels out = markers;
        struct Node {
          double cost;
          std::uint64_t order;
          std::size_t pixel;
          int label;
        };
        auto compare = [&](const Node &a, const Node &b) {
          seeds.poll();
          return a.cost == b.cost ? a.order > b.order : a.cost > b.cost;
        };
        std::priority_queue<Node, std::vector<Node>, decltype(compare)> queue(
            compare);
        std::vector<double> costs(height.pixels.size(),
                                  std::numeric_limits<double>::infinity());
        std::uint64_t order = 0;
        for (std::size_t p = 0; p < markers.pixels.size(); ++p) {
          seeds.periodic(p);
          if (markers.pixels[p] <= 0)
            continue;
          if (mask && mask->pixels[p] == 0)
            throw std::invalid_argument(
                "Watershed markers must lie inside the mask");
          costs[p] = height.pixels[p];
          queue.push({costs[p], order++, p, markers.pixels[p]});
        }
        seeds.finish();
        if (queue.empty())
          throw std::invalid_argument(
              "Watershed needs at least one positive marker");
        const auto traversal =
            seeds.child("Watershed traversal", height.pixels.size());
        std::size_t settled = 0;
        while (!queue.empty()) {
          traversal.poll();
          auto node = queue.top();
          queue.pop();
          if (node.cost != costs[node.pixel] ||
              node.label != out.pixels[node.pixel])
            continue;
          traversal.periodic(settled++);
          int x = static_cast<int>(node.pixel % height.width),
              y = static_cast<int>(node.pixel / height.width);
          for (auto d : offsets) {
            int xx = x + d[0], yy = y + d[1];
            if (xx < 0 || yy < 0 || xx >= height.width || yy >= height.height)
              continue;
            auto p = index(xx, yy, height.width);
            if (markers.pixels[p] > 0 || (mask && mask->pixels[p] == 0))
              continue;
            double cost = std::max(node.cost, height.pixels[p]);
            if (cost < costs[p]) {
              costs[p] = cost;
              out.pixels[p] = node.label;
              queue.push({cost, order++, p, node.label});
            }
          }
        }
        traversal.checkpoint(settled);
        return out;
      });
}
Labels binary_watershed(const Image &mask, int radius, int connectivity) {
  require_gray(mask);
  require_radius(radius);
  auto offsets = neighbors(connectivity);
  if (radius == 0)
    throw std::invalid_argument("Maxima radius must be positive");
  if (mask.width > std::numeric_limits<int>::max() - 2 ||
      mask.height > std::numeric_limits<int>::max() - 2)
    throw std::invalid_argument("Mask is too large to pad");
  const bool empty = with_kernel_stage(
      "Binary mask scan", mask.pixels.size(), [&](const auto &stage) {
        for (std::size_t i = 0; i < mask.pixels.size(); ++i) {
          stage.periodic(i);
          if (mask.pixels[i] != 0) {
            stage.checkpoint(i + 1);
            return false;
          }
        }
        stage.finish();
        return true;
      });
  if (empty)
    return {mask.width, mask.height, 0,
            std::vector<std::int32_t>(mask.pixels.size(), 0)};
  auto padded = resize_canvas(mask, mask.width + 2, mask.height + 2, 1, 1, 0);
  auto padded_distance = distance_transform(padded);
  auto distance = crop(padded_distance, 1, 1, mask.width, mask.height);
  auto peaks = crop(local_maxima(padded_distance, radius), 1, 1, mask.width,
                    mask.height);
  // A flat ridge can satisfy a pointwise maximum test even when its plateau
  // connects to a higher value. Only entire regional-maximum plateaus seed
  // the flood; otherwise touching objects can acquire a spurious third seed.
  return with_kernel_stage(
      "Watershed plateau scan", distance.pixels.size(), [&](const auto &scan) {
        const auto traversal =
            scan.child("Watershed plateau traversal", mask.pixels.size());
        std::vector<bool> visited(mask.pixels.size(), false);
        std::vector<std::size_t> plateau;
        std::size_t explored = 0;
        for (std::size_t start = 0; start < distance.pixels.size(); ++start) {
          scan.periodic(start);
          if (visited[start] || distance.pixels[start] == 0)
            continue;
          double level = distance.pixels[start];
          bool higher_neighbor = false;
          plateau.clear();
          plateau.push_back(start);
          visited[start] = true;
          for (std::size_t head = 0; head < plateau.size(); ++head) {
            traversal.periodic(explored++);
            auto p = plateau[head];
            int x = static_cast<int>(p % mask.width),
                y = static_cast<int>(p / mask.width);
            for (auto d : offsets) {
              int xx = x + d[0], yy = y + d[1];
              if (xx < 0 || yy < 0 || xx >= mask.width || yy >= mask.height)
                continue;
              auto q = index(xx, yy, mask.width);
              if (distance.pixels[q] > level)
                higher_neighbor = true;
              if (!visited[q] && distance.pixels[q] == level) {
                visited[q] = true;
                plateau.push_back(q);
              }
            }
          }
          if (higher_neighbor) {
            const auto removal =
                scan.child("Watershed plateau removal", plateau.size());
            for (std::size_t i = 0; i < plateau.size(); ++i) {
              removal.periodic(i);
              peaks.pixels[plateau[i]] = 0;
            }
            removal.finish();
          }
        }
        traversal.checkpoint(explored);
        scan.finish();
        auto seeds = connected_components(peaks, connectivity),
             components = connected_components(mask, connectivity);
        const auto fallback =
            scan.child("Watershed fallback scan", mask.pixels.size());
        std::vector<bool> has_seed(
            static_cast<std::size_t>(components.count) + 1, false);
        std::vector<std::size_t> best(
            static_cast<std::size_t>(components.count) + 1, mask.pixels.size());
        for (std::size_t p = 0; p < mask.pixels.size(); ++p) {
          fallback.periodic(p);
          auto component = components.pixels[p];
          if (component == 0)
            continue;
          if (seeds.pixels[p] != 0)
            has_seed[component] = true;
          if (best[component] == mask.pixels.size() ||
              distance.pixels[p] > distance.pixels[best[component]])
            best[component] = p;
        }
        fallback.finish();
        const auto additions =
            scan.child("Watershed fallback seeds",
                       static_cast<std::size_t>(components.count));
        for (std::size_t component = 1;
             component <= static_cast<std::size_t>(components.count);
             ++component) {
          additions.periodic(component - 1);
          if (has_seed[component])
            continue;
          if (seeds.count == std::numeric_limits<std::int32_t>::max())
            throw std::invalid_argument("Too many watershed markers");
          seeds.pixels[best[component]] = ++seeds.count;
        }
        additions.finish();
        return watershed(point(distance, "multiply", -1), seeds, &mask,
                         connectivity);
      });
}
} // namespace imagejxx
