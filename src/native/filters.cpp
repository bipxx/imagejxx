#include "imagejxx/image.hpp"
#include "kernel_control.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace imagejxx {
namespace {
std::vector<double> gaussian_kernel(double sigma) {
  require_finite(sigma, "Sigma");
  if (sigma < 0 || sigma > 1024)
    throw std::invalid_argument("Sigma must be in [0, 1024]");
  if (sigma == 0)
    return {1};
  int r = static_cast<int>(std::ceil(4 * sigma));
  KernelStage stage("Gaussian kernel", static_cast<std::size_t>(2 * r + 1));
  std::vector<double> kernel(static_cast<std::size_t>(2 * r + 1));
  for (int x = -r; x <= r; ++x) {
    kernel[x + r] = std::exp(-0.5 * (x / sigma) * (x / sigma));
    stage.periodic(static_cast<std::size_t>(x + r + 1));
  }
  double sum = kernel_accumulate(kernel.begin(), kernel.end(), 0.0);
  kernel_blocks("Gaussian weights", kernel.size(),
                [&](std::size_t first, std::size_t last) {
                  for (auto i = first; i < last; ++i)
                    kernel[i] /= sum;
                });
  stage.finish();
  return kernel;
}
Image separable(const Image &image, const std::vector<double> &kernel,
                bool horizontal, Border border) {
  return with_kernel_stage(
      horizontal ? "Gaussian X" : "Gaussian Y", image.pixels.size(),
      [&](const auto &stage) {
        Image out = image;
        int radius = static_cast<int>(kernel.size() / 2);
        for (int y = 0; y < image.height; ++y)
          for (int x = 0; x < image.width; ++x)
            for (int c = 0; c < image.channels; ++c) {
              double sum = 0;
              for (int d = -radius; d <= radius; ++d)
                sum += kernel[d + radius] *
                       image.sample(x + (horizontal ? d : 0),
                                    y + (horizontal ? 0 : d), c, border);
              out.at(x, y, c) = sum;
              stage.periodic((static_cast<std::size_t>(y) * image.width + x) *
                                 image.channels +
                             c + 1);
            }
        stage.finish();
        return out;
      });
}
Image box_pass(const Image &image, int radius, bool horizontal, Border border) {
  return with_kernel_stage(
      horizontal ? "Box X" : "Box Y", image.pixels.size(),
      [&](const auto &stage) {
        Image out = image;
        int length = horizontal ? image.width : image.height,
            lines = horizontal ? image.height : image.width;
        std::vector<double> prefix(static_cast<std::size_t>(length) +
                                   2 * radius + 1);
        for (int line = 0; line < lines; ++line)
          for (int c = 0; c < image.channels; ++c) {
            prefix[0] = 0;
            for (int i = -radius; i < length + radius; ++i)
              prefix[i + radius + 1] =
                  prefix[i + radius] + image.sample(horizontal ? i : line,
                                                    horizontal ? line : i, c,
                                                    border);
            for (int i = 0; i < length; ++i) {
              out.at(horizontal ? i : line, horizontal ? line : i, c) =
                  (prefix[i + 2 * radius + 1] - prefix[i]) / (2 * radius + 1);
              stage.periodic(
                  (static_cast<std::size_t>(line) * image.channels + c) *
                      length +
                  i + 1);
            }
          }
        stage.finish();
        return out;
      });
}
} // namespace

Image convolve(const Image &image, const std::vector<double> &kernel, int kw,
               int kh, Border border, double constant, bool normalize_kernel) {
  if (kw <= 0 || kh <= 0 || kw % 2 == 0 || kh % 2 == 0 || kw > 8193 ||
      kh > 8193 || kernel.size() != static_cast<std::size_t>(kw) * kh)
    throw std::invalid_argument(
        "Kernel must have nonempty odd dimensions no larger than 8193");
  require_finite(constant, "Border value");
  kernel_blocks("Convolution weights", kernel.size(),
                [&](std::size_t first, std::size_t last) {
                  for (auto i = first; i < last; ++i)
                    require_finite(kernel[i], "Kernel entry");
                });
  double divisor = normalize_kernel
                       ? kernel_accumulate(kernel.begin(), kernel.end(), 0.0)
                       : 1;
  if (divisor == 0)
    throw std::invalid_argument("Cannot normalize a zero-sum kernel");
  return with_kernel_stage(
      "Convolution pixels", image.pixels.size(), [&](const auto &stage) {
        Image out = image;
        for (int y = 0; y < image.height; ++y)
          for (int x = 0; x < image.width; ++x)
            for (int c = 0; c < image.channels; ++c) {
              double value = 0;
              for (int ky = 0; ky < kh; ++ky)
                for (int kx = 0; kx < kw; ++kx)
                  value += kernel[static_cast<std::size_t>(ky) * kw + kx] *
                           image.sample(x + kw / 2 - kx, y + kh / 2 - ky, c,
                                        border, constant);
              out.at(x, y, c) = value / divisor;
              stage.periodic((static_cast<std::size_t>(y) * image.width + x) *
                                 image.channels +
                             c + 1);
            }
        stage.finish();
        return out;
      });
}
Image gaussian_blur(const Image &image, double sx, double sy, Border border) {
  return separable(separable(image, gaussian_kernel(sx), true, border),
                   gaussian_kernel(sy), false, border);
}
Image box_blur(const Image &image, int radius, Border border) {
  require_radius(radius);
  return box_pass(box_pass(image, radius, true, border), radius, false, border);
}
Image rank_filter(const Image &image, int radius, const std::string &op,
                  bool disk, Border border, double constant) {
  require_radius(radius);
  require_finite(constant, "Border value");
  if (op != "minimum" && op != "maximum" && op != "median" && op != "mean" &&
      op != "variance" && op != "stddev")
    throw std::invalid_argument("Unknown rank filter operation");
  Image out = image;
  if (op == "variance" || op == "stddev")
    out.type = PixelType::Float64;
  std::vector<std::array<int, 2>> offsets;
  const auto side = static_cast<std::size_t>(2 * radius + 1);
  KernelStage footprint("Rank footprint", side * side);
  std::size_t visited = 0;
  for (int dy = -radius; dy <= radius; ++dy)
    for (int dx = -radius; dx <= radius; ++dx) {
      if (!disk || dx * dx + dy * dy <= radius * radius)
        offsets.push_back({dx, dy});
      footprint.periodic(++visited);
    }
  footprint.finish();
  KernelStage stage("Rank pixels", image.pixels.size());
  std::vector<double> values(offsets.size());
  for (int y = 0; y < image.height; ++y)
    for (int x = 0; x < image.width; ++x)
      for (int c = 0; c < image.channels; ++c) {
        for (std::size_t i = 0; i < offsets.size(); ++i)
          values[i] = image.sample(x + offsets[i][0], y + offsets[i][1], c,
                                   border, constant);
        double value = 0;
        if (op == "minimum")
          value = *kernel_min_element(values.begin(), values.end());
        else if (op == "maximum")
          value = *kernel_max_element(values.begin(), values.end());
        else if (op == "median") {
          auto middle = values.begin() + values.size() / 2;
          kernel_nth_element(values.begin(), middle, values.end());
          value = *middle;
        } else {
          double mean = kernel_accumulate(values.begin(), values.end(), 0.0) /
                        values.size();
          if (op == "mean")
            value = mean;
          else {
            value = kernel_accumulate(values.begin(), values.end(), 0.,
                                      [mean](double sum, double v) {
                                        return sum + (v - mean) * (v - mean);
                                      });
            value /= values.size();
            if (op == "stddev")
              value = std::sqrt(value);
          }
        }
        out.at(x, y, c) = value;
        stage.periodic((static_cast<std::size_t>(y) * image.width + x) *
                           image.channels +
                       c + 1);
      }
  stage.finish();
  return out;
}
Image sobel(const Image &image, Border border) {
  auto gx = convolve(image, {-1, 0, 1, -2, 0, 2, -1, 0, 1}, 3, 3, border);
  auto gy = convolve(image, {-1, -2, -1, 0, 0, 0, 1, 2, 1}, 3, 3, border);
  Image out = image;
  out.type = PixelType::Float64;
  kernel_blocks("Sobel magnitude", out.pixels.size(),
                [&](std::size_t first, std::size_t last) {
                  for (auto i = first; i < last; ++i)
                    out.pixels[i] = std::hypot(gx.pixels[i], gy.pixels[i]);
                });
  return out;
}
Image unsharp_mask(const Image &image, double sigma, double amount) {
  require_finite(amount, "Amount");
  if (amount < 0)
    throw std::invalid_argument("Unsharp amount must be nonnegative");
  auto blurred = gaussian_blur(image, sigma, sigma);
  Image out = image;
  kernel_blocks("Unsharp pixels", out.pixels.size(),
                [&](std::size_t first, std::size_t last) {
                  for (auto i = first; i < last; ++i)
                    out.pixels[i] +=
                        amount * (image.pixels[i] - blurred.pixels[i]);
                });
  return out;
}
Image bilateral_filter(const Image &image, int radius, double spatial,
                       double range) {
  require_radius(radius);
  require_finite(spatial, "Spatial sigma");
  require_finite(range, "Range sigma");
  if (spatial <= 0 || range <= 0)
    throw std::invalid_argument("Bilateral sigmas must be positive");
  KernelStage stage("Bilateral pixels",
                    static_cast<std::size_t>(image.width) * image.height);
  Image out = image;
  std::vector<double> sums(static_cast<std::size_t>(image.channels));
  for (int y = 0; y < image.height; ++y)
    for (int x = 0; x < image.width; ++x) {
      std::fill(sums.begin(), sums.end(), 0);
      double weight_sum = 0;
      for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx) {
          double difference = 0;
          for (int c = 0; c < image.channels; ++c) {
            double d = (image.sample(x + dx, y + dy, c, Border::Reflect) -
                        image.at(x, y, c)) /
                       range;
            difference += d * d;
          }
          double weight =
              std::exp(-0.5 * ((dx / spatial) * (dx / spatial) +
                               (dy / spatial) * (dy / spatial) + difference));
          weight_sum += weight;
          for (int c = 0; c < image.channels; ++c)
            sums[c] +=
                weight * image.sample(x + dx, y + dy, c, Border::Reflect);
        }
      for (int c = 0; c < image.channels; ++c)
        out.at(x, y, c) = sums[c] / weight_sum;
      stage.periodic(static_cast<std::size_t>(y) * image.width + x + 1);
    }
  stage.finish();
  return out;
}
Image kuwahara_filter(const Image &image, int radius) {
  require_radius(radius);
  Image out = image;
  KernelStage stage("Kuwahara pixels",
                    static_cast<std::size_t>(image.width) * image.height);
  for (int y = 0; y < image.height; ++y)
    for (int x = 0; x < image.width; ++x) {
      double best = std::numeric_limits<double>::infinity();
      std::vector<double> means(static_cast<std::size_t>(image.channels));
      for (int quadrant = 0; quadrant < 4; ++quadrant) {
        std::fill(means.begin(), means.end(), 0);
        std::vector<double> squares(means.size(), 0);
        double n = (radius + 1.0) * (radius + 1.0), variance = 0;
        for (int dy = 0; dy <= radius; ++dy)
          for (int dx = 0; dx <= radius; ++dx)
            for (int c = 0; c < image.channels; ++c) {
              double v = image.sample(x + (quadrant & 1 ? dx : -dx),
                                      y + (quadrant & 2 ? dy : -dy), c,
                                      Border::Reflect);
              means[c] += v;
              squares[c] += v * v;
            }
        for (int c = 0; c < image.channels; ++c) {
          means[c] /= n;
          variance += std::max(0.0, squares[c] / n - means[c] * means[c]);
        }
        if (variance < best) {
          best = variance;
          for (int c = 0; c < image.channels; ++c)
            out.at(x, y, c) = means[c];
        }
      }
      stage.periodic(static_cast<std::size_t>(y) * image.width + x + 1);
    }
  stage.finish();
  return out;
}
Image morphology(const Image &image, const std::string &op, int radius,
                 bool disk, Border border, double constant) {
  auto erode = [&](const Image &i) {
    return rank_filter(i, radius, "minimum", disk, border, constant);
  };
  auto dilate = [&](const Image &i) {
    return rank_filter(i, radius, "maximum", disk, border, constant);
  };
  if (op == "erode")
    return erode(image);
  if (op == "dilate")
    return dilate(image);
  if (op == "open")
    return dilate(erode(image));
  if (op == "close")
    return erode(dilate(image));
  if (op == "gradient")
    return calculate(dilate(image), erode(image), "subtract");
  if (op == "top_hat")
    return calculate(image, dilate(erode(image)), "subtract");
  if (op == "black_hat")
    return calculate(erode(dilate(image)), image, "subtract");
  throw std::invalid_argument("Unknown morphology operation");
}
Image subtract_background(const Image &image, int radius, bool light) {
  auto background = morphology(image, light ? "close" : "open", radius, true);
  return light ? calculate(background, image, "subtract")
               : calculate(image, background, "subtract");
}
Image rolling_ball_background(const Image &image, double radius, bool light) {
  require_finite(radius, "Ball radius");
  if (radius < 0 || radius > 4096)
    throw std::invalid_argument("Ball radius must be in [0, 4096]");
  int r = static_cast<int>(std::ceil(radius));
  struct Offset {
    int x, y;
    double height;
  };
  std::vector<Offset> offsets;
  const auto side = static_cast<std::size_t>(2 * r + 1);
  KernelStage footprint("Rolling-ball footprint", side * side);
  std::size_t visited = 0;
  for (int y = -r; y <= r; ++y)
    for (int x = -r; x <= r; ++x) {
      double square = static_cast<double>(x) * x + static_cast<double>(y) * y;
      if (square <= radius * radius)
        offsets.push_back(
            {x, y, std::sqrt(std::max(0.0, radius * radius - square))});
      footprint.periodic(++visited);
    }
  footprint.finish();
  auto source = light ? point(image, "multiply", -1) : image;
  Image erosion = source, background = source;
  with_kernel_stage(
      "Rolling-ball erosion", image.pixels.size(),
      [&](const auto &erode_stage) {
        for (int y = 0; y < image.height; ++y)
          for (int x = 0; x < image.width; ++x)
            for (int c = 0; c < image.channels; ++c) {
              double v = std::numeric_limits<double>::infinity();
              for (const auto &d : offsets)
                v = std::min(
                    v, source.sample(x + d.x, y + d.y, c, Border::Reflect) -
                           d.height);
              erosion.at(x, y, c) = v;
              erode_stage.periodic(
                  (static_cast<std::size_t>(y) * image.width + x) *
                      image.channels +
                  c + 1);
            }
        erode_stage.finish();
      });
  with_kernel_stage(
      "Rolling-ball dilation", image.pixels.size(),
      [&](const auto &dilate_stage) {
        for (int y = 0; y < image.height; ++y)
          for (int x = 0; x < image.width; ++x)
            for (int c = 0; c < image.channels; ++c) {
              double v = -std::numeric_limits<double>::infinity();
              for (const auto &d : offsets)
                v = std::max(
                    v, erosion.sample(x - d.x, y - d.y, c, Border::Reflect) +
                           d.height);
              background.at(x, y, c) = light ? -v : v;
              dilate_stage.periodic(
                  (static_cast<std::size_t>(y) * image.width + x) *
                      image.channels +
                  c + 1);
            }
        dilate_stage.finish();
      });
  return background;
}
Image subtract_rolling_ball(const Image &image, double radius, bool light) {
  auto background = rolling_ball_background(image, radius, light);
  return light ? calculate(background, image, "subtract")
               : calculate(image, background, "subtract");
}
Image remove_outliers(const Image &image, int radius, double threshold,
                      bool bright) {
  require_finite(threshold, "Outlier threshold");
  if (threshold < 0)
    throw std::invalid_argument("Outlier threshold must be nonnegative");
  auto median = rank_filter(image, radius, "median", true);
  Image out = image;
  kernel_blocks("Outlier pixels", out.pixels.size(),
                [&](std::size_t first, std::size_t last) {
                  for (auto i = first; i < last; ++i) {
                    double delta = bright ? image.pixels[i] - median.pixels[i]
                                          : median.pixels[i] - image.pixels[i];
                    if (delta > threshold)
                      out.pixels[i] = median.pixels[i];
                  }
                });
  return out;
}
Image anisotropic_diffusion(const Image &image, int iterations, double kappa,
                            double step) {
  require_finite(kappa, "Kappa");
  require_finite(step, "Step");
  if (iterations < 0 || iterations > 10000 || kappa <= 0 || step <= 0 ||
      step > 0.25)
    throw std::invalid_argument(
        "Require iterations in [0, 10000], kappa > 0 and step in (0, 0.25]");
  Image current = image;
  current.type = PixelType::Float64;
  const std::array<std::array<int, 2>, 4> neighbors = {
      {{-1, 0}, {1, 0}, {0, -1}, {0, 1}}};
  KernelStage iterations_stage("Diffusion iterations",
                               static_cast<std::size_t>(iterations));
  for (int iteration = 0; iteration < iterations; ++iteration) {
    Image next = current;
    KernelStage stage("Diffusion pixels", image.pixels.size());
    for (int y = 0; y < image.height; ++y)
      for (int x = 0; x < image.width; ++x)
        for (int c = 0; c < image.channels; ++c) {
          double delta = 0, center = current.at(x, y, c);
          for (auto d : neighbors) {
            double gradient =
                current.sample(x + d[0], y + d[1], c, Border::Nearest) - center;
            delta +=
                gradient * std::exp(-(gradient / kappa) * (gradient / kappa));
          }
          next.at(x, y, c) += step * delta;
          stage.periodic((static_cast<std::size_t>(y) * image.width + x) *
                             image.channels +
                         c + 1);
        }
    stage.finish();
    current = std::move(next);
    iterations_stage.checkpoint(static_cast<std::size_t>(iteration + 1));
  }
  return current;
}

Image clahe(const Image &image, int tw, int th, double clip_limit) {
  require_gray(image);
  require_finite(clip_limit, "Clip limit");
  if (tw <= 0 || th <= 0 || clip_limit <= 0)
    throw std::invalid_argument(
        "Tile dimensions and clip limit must be positive");
  Image out = image;
  KernelStage extrema_stage("CLAHE extrema", image.pixels.size());
  auto extrema =
      kernel_minmax_element(image.pixels.begin(), image.pixels.end());
  extrema_stage.finish();
  if (*extrema.first == *extrema.second)
    return out;
  double low = image.type == PixelType::UInt8 || image.type == PixelType::UInt16
                   ? 0
                   : *extrema.first;
  double high =
      image.type == PixelType::UInt8 || image.type == PixelType::UInt16
          ? type_max(image.type)
          : *extrema.second;
  auto bin_index = [&](double v) {
    return std::clamp(
        static_cast<int>(std::floor((v - low) / (high - low) * 255)), 0, 255);
  };
  int nx = (image.width - 1) / tw + 1, ny = (image.height - 1) / th + 1;
  std::vector<std::array<double, 256>> maps(static_cast<std::size_t>(nx) * ny);
  std::vector<double> centers_x(static_cast<std::size_t>(nx)),
      centers_y(static_cast<std::size_t>(ny));
  kernel_blocks(
      "CLAHE centers X", centers_x.size(),
      [&](std::size_t first, std::size_t last) {
        for (auto tx = first; tx < last; ++tx)
          centers_x[tx] =
              (tx * tw +
               std::min(image.width, static_cast<int>(std::min<std::int64_t>(
                                         (tx + 1LL) * tw, image.width))) -
               1) /
              2.0;
      });
  kernel_blocks(
      "CLAHE centers Y", centers_y.size(),
      [&](std::size_t first, std::size_t last) {
        for (auto ty = first; ty < last; ++ty)
          centers_y[ty] =
              (ty * th +
               std::min(image.height, static_cast<int>(std::min<std::int64_t>(
                                          (ty + 1LL) * th, image.height))) -
               1) /
              2.0;
      });
  KernelStage maps_stage("CLAHE maps", maps.size());
  for (int ty = 0; ty < ny; ++ty)
    for (int tx = 0; tx < nx; ++tx) {
      std::array<std::uint64_t, 256> counts{};
      int x0 = tx * tw, y0 = ty * th;
      int x1 = static_cast<int>(std::min<std::int64_t>(
          image.width, x0 + static_cast<std::int64_t>(tw)));
      int y1 = static_cast<int>(std::min<std::int64_t>(
          image.height, y0 + static_cast<std::int64_t>(th)));
      std::uint64_t n = static_cast<std::uint64_t>(x1 - x0) * (y1 - y0),
                    excess = 0;
      auto limit = static_cast<std::uint64_t>(std::min(
          static_cast<double>(n), std::max(1.0, clip_limit * n / 256)));
      for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
          kernel_checkpoint_periodic();
          ++counts[bin_index(image.at(x, y))];
        }
      for (auto &count : counts)
        if (count > limit) {
          excess += count - limit;
          count = limit;
        }
      for (auto &count : counts)
        count += excess / 256;
      auto remainder = excess % 256;
      // Spread the remainder through the range instead of biasing dark bins.
      for (std::uint64_t i = 0; i < remainder; ++i)
        ++counts[i * 256 / remainder];
      auto &map = maps[static_cast<std::size_t>(ty) * nx + tx];
      std::uint64_t cumulative = 0;
      for (int b = 0; b < 256; ++b) {
        cumulative += counts[b];
        map[b] = low + (high - low) * cumulative / n;
      }
      kernel_checkpoint_periodic();
      maps_stage.periodic(static_cast<std::size_t>(ty) * nx + tx + 1);
    }
  maps_stage.finish();
  auto bracket = [](const std::vector<double> &centers, double coordinate) {
    auto next = std::upper_bound(centers.begin(), centers.end(), coordinate);
    int second = static_cast<int>(next - centers.begin());
    int first = std::clamp(second - 1, 0, static_cast<int>(centers.size()) - 1);
    second = std::clamp(second, 0, static_cast<int>(centers.size()) - 1);
    double fraction = first == second
                          ? 0
                          : std::clamp((coordinate - centers[first]) /
                                           (centers[second] - centers[first]),
                                       0.0, 1.0);
    return std::array<double, 3>{static_cast<double>(first),
                                 static_cast<double>(second), fraction};
  };
  KernelStage pixels_stage("CLAHE pixels", image.pixels.size());
  for (int y = 0; y < image.height; ++y)
    for (int x = 0; x < image.width; ++x) {
      auto bx = bracket(centers_x, x), by = bracket(centers_y, y);
      int x0 = static_cast<int>(bx[0]), x1 = static_cast<int>(bx[1]),
          y0 = static_cast<int>(by[0]), y1 = static_cast<int>(by[1]);
      int b = bin_index(image.at(x, y));
      double upper =
          (1 - bx[2]) * maps[static_cast<std::size_t>(y0) * nx + x0][b] +
          bx[2] * maps[static_cast<std::size_t>(y0) * nx + x1][b];
      double lower =
          (1 - bx[2]) * maps[static_cast<std::size_t>(y1) * nx + x0][b] +
          bx[2] * maps[static_cast<std::size_t>(y1) * nx + x1][b];
      out.at(x, y) = (1 - by[2]) * upper + by[2] * lower;
      pixels_stage.periodic(static_cast<std::size_t>(y) * image.width + x + 1);
    }
  pixels_stage.finish();
  return out;
}

Image local_threshold(const Image &image, const std::string &method, int radius,
                      double k, double offset, double range,
                      double contrast_threshold) {
  require_gray(image);
  require_radius(radius);
  require_finite(k, "K");
  require_finite(offset, "Offset");
  require_finite(range, "Dynamic range");
  require_finite(contrast_threshold, "Contrast threshold");
  if (range <= 0 || contrast_threshold < 0)
    throw std::invalid_argument(
        "Dynamic range must be positive and contrast threshold nonnegative");
  const std::vector<std::string> methods = {"mean",       "median",  "midgrey",
                                            "niblack",    "sauvola", "bernsen",
                                            "phansalkar", "contrast"};
  if (std::find(methods.begin(), methods.end(), method) == methods.end())
    throw std::invalid_argument("Unknown local threshold method");
  Image source = image;
  source.type = PixelType::Float64;
  auto mean = box_blur(source, radius);
  Image squared = source;
  kernel_blocks("Local squares", squared.pixels.size(),
                [&](std::size_t first, std::size_t last) {
                  for (auto i = first; i < last; ++i)
                    squared.pixels[i] *= squared.pixels[i];
                });
  auto mean_squared = box_blur(squared, radius);
  Image minimum = source, maximum = source, median = source;
  if (method == "midgrey" || method == "bernsen" || method == "contrast") {
    minimum = rank_filter(source, radius, "minimum", false);
    maximum = rank_filter(source, radius, "maximum", false);
  }
  if (method == "median")
    median = rank_filter(source, radius, "median", false);
  Image out(image.width, image.height, 1, PixelType::UInt8);
  kernel_blocks(
      "Local threshold pixels", out.pixels.size(),
      [&](std::size_t first, std::size_t last) {
        for (auto i = first; i < last; ++i) {
          double m = mean.pixels[i],
                 s = std::sqrt(std::max(0.0, mean_squared.pixels[i] - m * m));
          double t = m - offset;
          if (method == "median")
            t = median.pixels[i] - offset;
          else if (method == "midgrey" || method == "bernsen") {
            t = (minimum.pixels[i] + maximum.pixels[i]) / 2 - offset;
            if (method == "bernsen" &&
                maximum.pixels[i] - minimum.pixels[i] < contrast_threshold)
              t = t >= type_max(image.type) / 2
                      ? -std::numeric_limits<double>::infinity()
                      : std::numeric_limits<double>::infinity();
          } else if (method == "niblack")
            t = m + k * s - offset;
          else if (method == "sauvola")
            t = m * (1 + k * (s / range - 1)) - offset;
          else if (method == "phansalkar") {
            double scale = type_max(image.type);
            t = m * (1 + 2 * std::exp(-10 * m / scale) + k * (s / range - 1)) -
                offset;
          } else if (method == "contrast") {
            out.pixels[i] = maximum.pixels[i] - source.pixels[i] <
                                    source.pixels[i] - minimum.pixels[i]
                                ? 255
                                : 0;
            continue;
          }
          out.pixels[i] = source.pixels[i] > t ? 255 : 0;
        }
      });
  return out;
}
} // namespace imagejxx
