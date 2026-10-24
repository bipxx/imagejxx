#include "rendering.hpp"
#include "path_visual.hpp"
#include "../native/kernel_control.hpp"
#include "../native/manual_threshold_display_internal.hpp"
#include "channel_display_sources.hpp"
#include <algorithm>
#include <climits>
#include <cstring>
#include <imagejxx/float32_display.hpp>
#include <cmath>
#include <cstdlib>
#include <imagejxx/manual_threshold.hpp>
#include <imgui.h>
#include <limits>
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

namespace imagejxx::desktop {
Json channelDisplayDiagnostics(const ChannelDisplayState &state,
                               const ChannelDisplaySources &sources,
                               Plane plane, int coordinate, int t, int active_c,
                               bool include_z_strips,
                               ChannelDisplayLimits limits) {
  const auto &dataset = sources.live().data().dataset;
  const auto estimate = estimate_channel_display(
      dataset, state, plane, coordinate, t, active_c, limits);
  Json contributors = Json::array();
  for (int c : estimate.contributing_channels) {
    const bool preview = sources.usesPreview(c);
    Json contributor = {
        {"c", c},
        {"preview", preview},
        {"generation", preview ? sources.generation() : 0},
        {"data_identity", sources.dataIdentity(c)},
        {"range", displayRangeValue(state.channels[static_cast<std::size_t>(c)].range)}};
    if (include_z_strips) {
      Json strips = Json::array();
      if (preview)
        for (int z = 0; z < dataset.shape().z; ++z)
          if (sources.usesPreview(c, z))
            strips.push_back(z);
      contributor["preview_z"] = std::move(strips);
    }
    contributors.push_back(std::move(contributor));
  }
  return {{"state", metadataJson(encode_channel_display(
                        state, dataset.shape(), dataset.pixel_type(),
                        dataset.sample_policy()))},
          {"contributors", std::move(contributors)}};
}

void renderThresholdOverlay(Image &rgb, const Image &original,
                            const DocumentSnapshot &source,
                            PlanePosition position) {
  const auto state =
      manual_threshold_state(source.display(), source.data().dataset.shape());
  if (state && manual_threshold_contains(*state,position))
    imagejxx::detail::blend_manual_threshold_source_overlay(original, rgb, state->range,
        source.data().dataset.sample_policy());
}
RoiVisual roiVisual(const Json &json, int width, int height,
                    RoiColor fallback) {
  const auto selection = selectionFromJson(json);
  selection.validate(width, height);
  if(selection.is_path())return pathVisual(selection,width,height,{1,1},128ULL*1024*1024,fallback);
  RoiVisual result;
  result.style = resolve_roi_style(selection, fallback);
  result.area = selection_is_area(selection);
  result.markers = selection.kind == "point" || selection.kind == "multipoint";
  result.markerWidth = selection.width;
  result.points = selection.points;
  if (result.area) {
    const auto mask = selection.mask(width, height);
    auto inside = [&](int x, int y) {
      return x >= 0 && y >= 0 && x < width && y < height && mask.at(x, y) != 0;
    };
    for (int y = 0; y < height; ++y) {
      kernel_checkpoint();
      if (result.style.fill_color) {
        int x = 0;
        while (x < width) {
          kernel_checkpoint_periodic();
          if (!inside(x, y)) {
            ++x;
            continue;
          }
          const int start = x++;
          while (x < width && inside(x, y)) {
            kernel_checkpoint_periodic();
            ++x;
          }
          result.fill.push_back({start, y, x - start, 1});
        }
      }
      if (result.style.stroke_color)
        for (int x = 0; x < width; ++x) {
          kernel_checkpoint_periodic();
          if (inside(x, y)) {
            if (!inside(x, y - 1))
              result.edges.push_back(
                  {double(x), double(y), double(x + 1), double(y)});
            if (!inside(x, y + 1))
              result.edges.push_back(
                  {double(x), double(y + 1), double(x + 1), double(y + 1)});
            if (!inside(x - 1, y))
              result.edges.push_back(
                  {double(x), double(y), double(x), double(y + 1)});
            if (!inside(x + 1, y))
              result.edges.push_back(
                  {double(x + 1), double(y), double(x + 1), double(y + 1)});
          }
        }
    }
  }
  return result;
}
void drawRoiVisual(const RoiVisual &visual, ImDrawList *draw,
                   std::array<float, 2> origin, std::array<float, 2> scale) {
  const auto low = draw->GetClipRectMin(), high = draw->GetClipRectMax();
  for (float value :
       {origin[0], origin[1], scale[0], scale[1], low.x, low.y, high.x, high.y})
    if (!std::isfinite(value))
      return;
  if (scale[0] <= 0 || scale[1] <= 0 || low.x >= high.x || low.y >= high.y)
    return;
  auto tint = [](const RoiColor &rgba) {
    return IM_COL32(rgba[0], rgba[1], rgba[2], rgba[3]);
  };
  auto x = [&](double value) { return double(origin[0]) + value * scale[0]; };
  auto y = [&](double value) { return double(origin[1]) + value * scale[1]; };
  for (const auto &span : visual.fill) {
    const double a = std::max(double(low.x), x(span.x)),
                 b = std::min(double(high.x), x(double(span.x) + span.width));
    const double c = std::max(double(low.y), y(span.y)),
                 d = std::min(double(high.y), y(double(span.y) + span.height));
    if (a < b && c < d)
      draw->AddRectFilled({float(a), float(c)}, {float(b), float(d)},
                          tint(*visual.style.fill_color));
  }
  if (!visual.style.stroke_color)
    return;
  const std::array<float, 8> key{origin[0], origin[1], scale[0], scale[1],
                                 low.x,     low.y,     high.x,   high.y};
  if (!visual.screenReady || visual.screenKey != key) {
    visual.screenReady = false;
    visual.screenStroke.clear();
    // All narrowing and storage depend on the viewport, never on a user width
    // or off-screen coordinate. Refuse unsupported device extents explicitly.
    if (low.x <= double(INT_MIN) + 1 || low.y <= double(INT_MIN) + 1 ||
        high.x >= double(INT_MAX) - 1 || high.y >= double(INT_MAX) - 1 ||
        double(high.x) - low.x > 32768 || double(high.y) - low.y > 32768)
      throw std::invalid_argument("ROI viewport extent is unsupported");
    const int firstX = int(std::floor(low.x)),
              lastX = int(std::ceil(high.x)) - 1;
    const int firstY = int(std::floor(low.y)),
              lastY = int(std::ceil(high.y)) - 1;
    using Interval = std::pair<int, int>;
    std::vector<std::vector<Interval>> rows(std::size_t(lastY - firstY) + 1);
    const double maximum = std::numeric_limits<double>::max();
    const double width =
        visual.markers ? 1.5 * std::min(visual.markerWidth,
                                        2 * std::max(double(high.x) - low.x,
                                                     double(high.y) - low.y))
        : visual.style.cosmetic
            ? 1.
            : (visual.style.stroke_width > maximum / scale[0]
                   ? maximum
                   : visual.style.stroke_width * scale[0]);
    const double radius = width / 2;
    visual.screenWidth = width;
    bool full = false;
    auto line = [&](double ax, double ay, double bx, double by) {
      if (full || !std::isfinite(ax) || !std::isfinite(ay) ||
          !std::isfinite(bx) || !std::isfinite(by) || radius <= 0)
        return;
      const double dx = bx - ax, dy = by - ay, length = std::hypot(dx, dy);
      if (!std::isfinite(length))
        return;
      const double ux = length == 0 ? 0 : dx / length;
      const double uy = length == 0 ? 0 : dy / length;
      const double cx = (double(low.x) + high.x) / 2,
                   cy = (double(low.y) + high.y) / 2;
      const double along =
          std::clamp((cx - ax) * ux + (cy - ay) * uy, 0., length);
      const double distance =
          std::hypot(cx - ax - along * ux, cy - ay - along * uy);
      const double extent =
          std::hypot(double(high.x) - low.x, double(high.y) - low.y) / 2;
      if (radius >= extent && distance <= radius - extent) {
        full = true;
        return;
      }
      const double minY =
          std::max(double(firstY), std::min(ay, by) - radius - .5);
      const double maxY =
          std::min(double(lastY) + 1, std::max(ay, by) + radius - .5);
      if (minY >= maxY)
        return;
      const int start = std::max(firstY, int(std::ceil(minY)));
      const int stop = std::min(lastY, int(std::ceil(maxY)) - 1);
      const std::array<std::array<double, 2>, 4> body{
          {{ax - uy * radius, ay + ux * radius},
           {bx - uy * radius, by + ux * radius},
           {bx + uy * radius, by - ux * radius},
           {ax + uy * radius, ay - ux * radius}}};
      for (int row = start; row <= stop; ++row) {
        const double sample = row + .5;
        double left = maximum, right = -maximum;
        auto circle = [&](double px, double py) {
          const double delta = std::abs(sample - py);
          if (delta >= radius)
            return;
          // Normalized circle extent avoids radius*radius overflow.
          const double ratio = delta / radius;
          const double half =
              radius * std::sqrt(std::max(0., 1 - ratio * ratio));
          left = std::min(left, px - half);
          right = std::max(right, px + half);
        };
        circle(ax, ay);
        circle(bx, by);
        if (length != 0)
          for (std::size_t i = 0; i < 4; ++i) {
            const auto &a = body[i], &b = body[(i + 1) % 4];
            if (!std::isfinite(a[0]) || !std::isfinite(a[1]) ||
                !std::isfinite(b[0]) || !std::isfinite(b[1]))
              continue;
            if (sample < std::min(a[1], b[1]) ||
                sample >= std::max(a[1], b[1]) || a[1] == b[1])
              continue;
            const double t = (sample - a[1]) / (b[1] - a[1]);
            const double hit = (1 - t) * a[0] + t * b[0];
            left = std::min(left, hit);
            right = std::max(right, hit);
          }
        left = std::max(double(firstX), left - .5);
        right = std::min(double(lastX) + 1, right - .5);
        if (left < right) {
          const int startX = int(std::ceil(left)),
                    stopX = int(std::ceil(right)) - 1;
          if (startX <= stopX)
            rows[std::size_t(row - firstY)].emplace_back(startX, stopX);
        }
      }
    };
    if (visual.area) {
      for (const auto &edge : visual.edges)
        line(x(edge[0]), y(edge[1]), x(edge[2]), y(edge[3]));
    } else if (visual.markers) {
      for (const auto &point : visual.points) {
        const double px = x(point[0]), py = y(point[1]);
        line(px - 5, py, px + 5, py);
        line(px, py - 5, px, py + 5);
      }
    } else
      for (std::size_t i = 1; i < visual.points.size(); ++i)
        line(x(visual.points[i - 1][0]), y(visual.points[i - 1][1]),
             x(visual.points[i][0]), y(visual.points[i][1]));
    for (int row = firstY; row <= lastY; ++row) {
      if (full) {
        visual.screenStroke.push_back({firstX, row, lastX - firstX + 1, 1});
        continue;
      }
      auto &intervals = rows[std::size_t(row - firstY)];
      if (intervals.empty())
        continue;
      std::sort(intervals.begin(), intervals.end());
      auto current = intervals.front();
      for (std::size_t i = 1; i < intervals.size(); ++i) {
        if (intervals[i].first <= current.second + 1)
          current.second = std::max(current.second, intervals[i].second);
        else {
          visual.screenStroke.push_back(
              {current.first, row, current.second - current.first + 1, 1});
          current = intervals[i];
        }
      }
      visual.screenStroke.push_back(
          {current.first, row, current.second - current.first + 1, 1});
    }
    visual.screenKey = key;
    visual.screenReady = true;
    ++visual.screenBuilds;
  }
  for (const auto &span : visual.screenStroke)
    draw->AddRectFilled(
        {std::max(low.x, float(span.x)), std::max(low.y, float(span.y))},
        {std::min(high.x, float(span.x + span.width)),
         std::min(high.y, float(span.y + 1))},
        tint(*visual.style.stroke_color));
}
Json roiVisualJson(const RoiVisual &visual) {
  auto rgba = [](const std::optional<RoiColor> &value) {
    return value ? Json(*value) : Json();
  };
  Json bounds;
  for (const auto &span : visual.screenStroke) {
    if (bounds.is_null())
      bounds = {span.x, span.y, span.x + span.width, span.y + span.height};
    else {
      bounds[0] = std::min(bounds[0].get<int>(), span.x);
      bounds[1] = std::min(bounds[1].get<int>(), span.y);
      bounds[2] = std::max(bounds[2].get<int>(), span.x + span.width);
      bounds[3] = std::max(bounds[3].get<int>(), span.y + span.height);
    }
  }
  return {{"stroke_color", rgba(visual.style.stroke_color)},
          {"fill_color", rgba(visual.style.fill_color)},
          {"stroke_width", visual.style.stroke_width},
          {"cosmetic", visual.style.cosmetic},
          {"area", visual.area},
          {"fill_spans", visual.fill.size()},
          {"contour_edges", visual.edges.size()},
          {"device_spans", visual.screenStroke.size()},
          {"device_builds", visual.screenBuilds},
          {"device_stroke_width", visual.screenWidth},
          {"device_bounds", bounds},
          {"device_key", visual.screenReady ? Json(visual.screenKey) : Json()}};
}
std::vector<Rect> selectionOutlineSpans(const Json &selection, int width,
                                        int height) {
  const auto mask = binary_outline(selectionMask(selection, width, height));
  std::vector<Rect> spans;
  for (int y = 0; y < height; ++y) {
    int x = 0;
    while (x < width) {
      if (mask.at(x, y) == 0) {
        ++x;
        continue;
      }
      int start = x++;
      while (x < width && mask.at(x, y) != 0)
        ++x;
      spans.push_back({start, y, x - start, 1});
    }
  }
  return spans;
}
namespace {
bool preservedDisplayPolicy(const Image &image,
                            FloatingSamplePolicy sample_policy) {
  if (sample_policy == FloatingSamplePolicy::FiniteOnly)
    return false;
  if (sample_policy != FloatingSamplePolicy::PreserveQuietNaNFloat32 &&
      sample_policy != FloatingSamplePolicy::PreserveIEEEFloat32)
    throw std::invalid_argument("Invalid floating display sample policy");
  if (image.type != PixelType::Float32 || image.channels != 1 ||
      image.width <= 0 || image.height <= 0 ||
      std::size_t(image.width) >
          std::numeric_limits<std::size_t>::max() / image.height ||
      image.pixels.size() != std::size_t(image.width) * image.height)
    throw std::invalid_argument(
        "Quiet-NaN display requires admitted grayscale Float32 storage");
  return true;
}
bool quietDisplayNaN(double sample, FloatingSamplePolicy sample_policy) {
  // Classify integer IEEE fields under the source policy before any numeric cast.
  const auto bits = float32_source_sample_bits(sample, sample_policy);
  return (bits & 0x7fffffffu) > 0x7f800000u;
}
} // namespace

std::array<double, 2> finiteDisplayRange(const Image &image,
                                         FloatingSamplePolicy sample_policy,
                                         bool rgb_unit_range) {
  if (sample_policy==FloatingSamplePolicy::PreserveIEEEFloat32)
    throw std::invalid_argument("IEEE automatic bounds require sourceDisplayRange");
  const bool preserve = preservedDisplayPolicy(image, sample_policy);
  if (image.pixels.empty())
    throw std::invalid_argument("Display range requires nonempty pixels");
  double low = 0, high = 0;
  bool haveFinite = false;
  if (!preserve) {
    // Keep the legacy first-minimum/last-maximum selection, including signed
    // zero ties, for every finite-only caller.
    const auto bounds =
        std::minmax_element(image.pixels.begin(), image.pixels.end());
    low = *bounds.first;
    high = *bounds.second;
    if (!std::isfinite(low) || !std::isfinite(high))
      throw std::invalid_argument("Finite-only display requires finite pixels");
    haveFinite = true;
  } else {
    for (const double sample : image.pixels) {
      if (quietDisplayNaN(sample, sample_policy))
        continue;
      if (!haveFinite) {
        low = high = sample;
        haveFinite = true;
      } else {
        if (sample < low)
          low = sample;
        if (!(sample < high))
          high = sample;
      }
    }
  }
  // This explicit owned display choice is not an upstream parity claim.
  if (!haveFinite)
    return {0, 1};
  if (rgb_unit_range && image.channels >= 3 && low >= 0 && high <= 1) {
    low = 0;
    high = 1;
  }
  if (high <= low)
    high = low + 1;
  return {low, high};
}

std::array<double,2> sourceDisplayRange(const Image &image, FloatingSamplePolicy policy,
                                      bool rgb_unit_range) {
  if (policy != FloatingSamplePolicy::PreserveIEEEFloat32)
    return finiteDisplayRange(image,policy,rgb_unit_range);
  const auto words=float32_display_bounds(image,policy);
  return {float32_source_sample(words[0],policy),float32_source_sample(words[1],policy)};
}
std::array<std::uint32_t,2> displayRangeBits(const std::array<double,2> &range) {
  std::array<std::uint32_t,2> result;
  for(unsigned i=0;i<2;++i) {
    double value=range[i];
    if (std::isfinite(value)) {
      if (std::abs(value)>double(std::numeric_limits<float>::max()))
        throw std::invalid_argument("Computed Float32 view bound exceeds binary32");
      value=double(static_cast<float>(value));
    }
    result[i]=float32_source_sample_bits(value,FloatingSamplePolicy::PreserveIEEEFloat32);
  }
  return result;
}
Json displayRangeValue(const std::array<double,2> &range) {
  if (std::isfinite(range[0]) && std::isfinite(range[1])) return Json(range);
  return {{"float32_bits",displayRangeBits(range)}};
}
bool sameDisplayRange(const std::array<double,2> &a,const std::array<double,2> &b) {
  if (a==b) return true;
  return std::memcmp(a.data(),b.data(),sizeof(double)*2)==0;
}

Image displayImage(const Image &image, const Json &meta) {
  return displayImage(image, meta, FloatingSamplePolicy::FiniteOnly);
}

Image displayImage(const Image &image, const Json &meta,
                   FloatingSamplePolicy sample_policy,
                   std::optional<std::array<std::uint32_t,2>> computed) {
  const bool preserve = preservedDisplayPolicy(image, sample_policy);
  const bool ieee = sample_policy==FloatingSamplePolicy::PreserveIEEEFloat32;
  if (computed && !ieee) throw std::invalid_argument("Computed Float32 view bounds require IEEE source policy");
  Float32DisplayOptions ieeeOptions;
  if (ieee) ieeeOptions.computed_range_bits=computed;
  double low = 0, high = type_max(image.type);
  const auto &range = get(meta, "display_range");
  if (range.is_array() && range.size() == 2) {
    low = number(range[0]);
    high = number(range[1]);
    if (ieee && !computed) {
      if (low==0 && high==0) ieeeOptions.computed_range_bits=displayRangeBits({low,high});
      else ieeeOptions.range=std::array<double,2>{low,high};
    }
  } else if (!ieee && (image.type == PixelType::Float32 ||
             image.type == PixelType::Float64)) {
    const auto bounds = finiteDisplayRange(image, sample_policy, true);
    low = bounds[0];
    high = bounds[1];
  }
  const bool equalScalarRange = high == low && image.channels == 1 &&
      (image.type == PixelType::UInt16 || image.type == PixelType::Float32);
  if (!ieee && ((!equalScalarRange && high <= low) || !std::isfinite(low) || !std::isfinite(high)))
    throw std::invalid_argument("Display maximum must exceed minimum");
  // Only temporary display pixels are altered. The original Image, immutable
  // Dataset and raw source readback retain their exact IEEE payloads.
  std::optional<Image> projection;
  if (preserve && !ieee)
    for (std::size_t i = 0; i < image.pixels.size(); ++i)
      if (quietDisplayNaN(image.pixels[i], sample_policy)) {
        if (!projection) {
          projection = image;
          // A finite display endpoint may lie outside the Float32 range. Its
          // temporary representation is double, as in the existing scale path.
          projection->type = PixelType::Float64;
        }
        projection->pixels[i] = low;
      }
  const Image &displayPixels = projection ? *projection : image;
  auto bytes = [&]() {
    if (ieee) {
      const auto rendered=render_float32_display(image,ieeeOptions,sample_policy).rgb;
      Image mapped(image.width,image.height,1,PixelType::UInt8);
      for(std::size_t i=0;i<mapped.pixels.size();++i) {
        kernel_checkpoint_periodic(); mapped.pixels[i]=rendered.pixels[i*3];
      }
      return mapped;
    }
    if (equalScalarRange) {
      // Retain the stored range. Typed processors display an equal range as a
      // step; no artificial high endpoint or pixel-domain rewrite is needed.
      double cutoff = image.type == PixelType::UInt16 ? std::floor(low + .5) : low;
      if (image.type == PixelType::Float32 && std::abs(low) <= double(std::numeric_limits<float>::max()))
        cutoff = double(static_cast<float>(low));
      Image bytes(image.width, image.height, 1, PixelType::UInt8);
      for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        const auto value = displayPixels.pixels[i];
        if (!std::isfinite(value))
          throw std::invalid_argument("Finite display requires finite pixels");
        bytes.pixels[i] = value <= cutoff ? 0.0 : 255.0;
      }
      return bytes;
    } else {
      return canonical(convert(linear(convert(displayPixels, PixelType::Float64),
                                      255 / (high - low), -low * 255 / (high - low)),
                                PixelType::UInt8));
    }
  }();
  Image out(image.width, image.height, 3, PixelType::UInt8);
  auto lut = string(get(meta, "lut"), "Grays");
  bool labels = boolean(get(meta, "labels"));
  const auto &custom = get(meta, "custom_lut");
  Image hsv(256, 1, 3, PixelType::Float64);
  for (int i = 0; i < 256; ++i) {
    hsv.at(i, 0, 0) = i / 256.0;
    hsv.at(i, 0, 1) = hsv.at(i, 0, 2) = 1;
  }
  auto spectrum = canonical(hsv_to_rgb(hsv, PixelType::UInt8));
  const std::array<std::array<double, 3>, 5> fire{
      {{0, 0, 0}, {80, 0, 120}, {220, 20, 20}, {255, 180, 0}, {255, 255, 255}}},
      ice{{{0, 0, 0},
           {0, 40, 120},
           {0, 160, 220},
           {160, 240, 255},
           {255, 255, 255}}};
  for (int y = 0; y < image.height; ++y)
    for (int x = 0; x < image.width; ++x) {
      int v = int(bytes.at(x, y));
      const bool nanSample =
          preserve && quietDisplayNaN(image.at(x, y), sample_policy);
      if (nanSample)
        v = 0;
      std::array<double, 3> rgb{double(v), double(v), double(v)};
      if (image.channels >= 3) {
        rgb = {bytes.at(x, y, 0), bytes.at(x, y, 1), bytes.at(x, y, 2)};
        if (image.channels == 4) {
          double alpha =
              std::clamp(image.at(x, y, 3) / type_max(image.type), 0.0, 1.0);
          int bg = ((x / 12 + y / 12) % 2) * 24 + 210;
          for (auto &c : rgb)
            c = std::floor(c * alpha + bg * (1 - alpha) + .5);
        }
      } else if (labels || lut == "Spectrum") {
        int index =
            nanSample ? 0
            : ieee && !std::isfinite(image.at(x,y)) ? v
            : labels  ? int(std::fmod(std::max(0.0, image.at(x, y)) * 37, 256))
                      : v;
        for (int c = 0; c < 3; ++c)
          rgb[c] = labels && !nanSample && image.at(x, y) == 0
                       ? 0
                       : spectrum.at(index, 0, c);
      } else if (custom.is_array() && custom.size() == 256) {
        if (!custom[v].is_array() || custom[v].size() != 3)
          throw std::invalid_argument("Invalid custom LUT");
        for (int c = 0; c < 3; ++c) {
          rgb[c] = number(custom[v][c]);
          if (rgb[c] < 0 || rgb[c] > 255)
            throw std::invalid_argument("LUT colors must be 0..255");
        }
      } else if (lut == "Inverted")
        rgb = {double(255 - v), double(255 - v), double(255 - v)};
      else if (lut == "Red")
        rgb = {double(v), 0, 0};
      else if (lut == "Green")
        rgb = {0, double(v), 0};
      else if (lut == "Blue")
        rgb = {0, 0, double(v)};
      else if (lut == "Fire" || lut == "Ice") {
        const auto &stops = lut == "Fire" ? fire : ice;
        double t = v / 255.0 * 4;
        int k = std::min(3, int(t));
        for (int c = 0; c < 3; ++c)
          rgb[c] = std::floor(stops[k][c] * (1 - (t - k)) +
                              stops[k + 1][c] * (t - k) + .5);
      }
      for (int c = 0; c < 3; ++c)
        out.at(x, y, c) = rgb[c];
    }
  return out;
}
namespace {
void blend(Image &image, int x, int y, const std::array<unsigned char, 4> &rgba,
           double coverage = 1) {
  if (x < 0 || y < 0 || x >= image.width || y >= image.height)
    return;
  double alpha = coverage * rgba[3] / 255.0;
  if (image.channels == 4) {
    // A legacy annotation is uniform in color. Accumulate overlapping glyph
    // coverage once into its owned raster rather than blending its joins twice
    // when the cached layer is displayed over another annotation.
    for (int c = 0; c < 3; ++c)
      image.at(x, y, c) = rgba[c];
    image.at(x, y, 3) =
        std::floor((image.at(x, y, 3) * (1 - alpha) + 255 * alpha) + .5);
    return;
  }
  for (int c = 0; c < 3; ++c)
    image.at(x, y, c) =
        std::floor(image.at(x, y, c) * (1 - alpha) + rgba[c] * alpha + .5);
}
void text(Image &image, const Json &o,
          const std::array<unsigned char, 4> &rgba) {
  std::string font = string(get(o, "font_path"));
  if (font.empty()) {
    if (const char *configured = std::getenv("IMAGEJXX_FONT"))
      font = configured;
  }
  if (font.empty()) {
#ifdef _WIN32
    const char *windows = std::getenv("WINDIR");
    font = std::string(windows ? windows : "C:/Windows") + "/Fonts/segoeui.ttf";
#elif defined(__APPLE__)
    font = "/System/Library/Fonts/Supplemental/Arial.ttf";
#else
    font = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
#endif
  }
  auto data = readBytes(font, 64ULL * 1024 * 1024);
  stbtt_fontinfo face{};
  if (!stbtt_InitFont(&face, data.data(),
                      stbtt_GetFontOffsetForIndex(data.data(), 0)))
    throw std::invalid_argument("Invalid annotation font");
  const auto &points = get(o, "points");
  if (!points.is_array() || points.empty() || points[0].size() != 2)
    return;
  int size = integer(get(o, "font_size"), 16);
  if (size <= 0 || size > 512)
    throw std::invalid_argument("Font size must be 1..512");
  float scale = stbtt_ScaleForPixelHeight(&face, float(size));
  int ascent, descent, gap;
  stbtt_GetFontVMetrics(&face, &ascent, &descent, &gap);
  double origin = number(points[0][0]), x = origin;
  double y = std::floor(number(points[0][1])) + int(ascent * scale);
  auto content = string(get(o, "text"));
  for (size_t i = 0; i < content.size();) {
    unsigned int cp = static_cast<unsigned char>(content[i++]);
    int continuation = 0;
    if (cp >= 0xf0) {
      cp &= 7;
      continuation = 3;
    } else if (cp >= 0xe0) {
      cp &= 15;
      continuation = 2;
    } else if (cp >= 0xc0) {
      cp &= 31;
      continuation = 1;
    }
    for (int k = 0; k < continuation && i < content.size(); ++k)
      cp = (cp << 6) | (static_cast<unsigned char>(content[i++]) & 63);
    if (cp == '\n') {
      x = origin;
      y += size;
      continue;
    }
    int w, h, dx, dy;
    auto bitmap = stbtt_GetCodepointBitmap(&face, scale, scale, int(cp), &w, &h,
                                           &dx, &dy);
    for (int yy = 0; yy < h; ++yy)
      for (int xx = 0; xx < w; ++xx) {
        const double px = std::trunc(x) + dx + xx, py = y + dy + yy;
        if (std::isfinite(px) && std::isfinite(py) && px >= 0 && py >= 0 &&
            px < image.width && py < image.height)
          blend(image, int(px), int(py), rgba, bitmap[yy * w + xx] / 255.0);
      }
    stbtt_FreeBitmap(bitmap, nullptr);
    int advance, bearing;
    stbtt_GetCodepointHMetrics(&face, int(cp), &advance, &bearing);
    x += advance * scale;
  }
}
bool visibleOverlay(const Json &overlay, const Json &metadata,
                    std::optional<PlanePosition> position) {
  const auto &slice = get(overlay, "slice_index");
  if (!position)
    return slice.is_null() || slice == get(metadata, "slice_index");
  const auto matches = [&](const char *name, int coordinate) {
    const auto &value = get(overlay, name);
    return value.is_null() ||
           (value.is_number_integer() && value == coordinate);
  };
  return matches("slice_index", position->z) && matches("t", position->t) &&
         matches("c", position->c) && matches("z", position->z);
}
void paintOverlay(Image &result, const Json &o) {
  auto rgba = color(string(get(o, "color"), "#ffcc00"));
  auto kind = string(get(o, "kind"));
  if (kind.empty())
    return; // Opaque native overlay metadata has no geometry to paint.
  if (kind == "text") {
    text(result, o, rgba);
    return;
  }
  auto selection = kind == "roi" ? get(o, "selection") : o;
  selection["width"] = number(get(selection, "width"), 1);
  kind = string(get(selection, "kind"));
  if (selection.contains(roi_style_attribute)) {
    result = render_roi_style(result, selectionFromJson(selection), rgba);
    return;
  }
  auto mask = selectionMask(selection, result.width, result.height);
  if (kind == "rounded_rectangle" || kind == "rotated_rounded_rectangle" ||
      kind == "rectangle" || kind == "ellipse" || kind == "polygon" ||
      kind == "freehand" || kind == "rotated_rectangle" ||
      kind == "rotated_ellipse")
    mask = binary_outline(mask);
  for (int y = 0; y < result.height; ++y)
    for (int x = 0; x < result.width; ++x)
      if (mask.at(x, y))
        blend(result, x, y, rgba);
}
} // namespace
bool hasStyledOverlays(const Json &metadata) {
  return hasStyledOverlays(metadata, std::nullopt);
}
bool hasStyledOverlays(const Json &metadata,
                       std::optional<PlanePosition> position) {
  for (const auto &overlay : get(metadata, "overlays")) {
    if (!visibleOverlay(overlay, metadata, position) ||
        string(get(overlay, "kind")) == "text")
      continue;
    const auto &selection = string(get(overlay, "kind")) == "roi"
                                ? get(overlay, "selection")
                                : overlay;
    if (selection.contains(roi_style_attribute))
      return true;
  }
  return false;
}
std::vector<OverlayVisual> overlayVisuals(const Json &metadata, int width,
                                          int height) {
  return overlayVisuals(metadata, width, height, std::nullopt);
}
std::vector<OverlayVisual>
overlayVisuals(const Json &metadata, int width, int height,
               std::optional<PlanePosition> position) {
  std::vector<OverlayVisual> layers;
  std::size_t index = 0;
  for (const auto &overlay : get(metadata, "overlays")) {
    const auto originalIndex = index++;
    if (!visibleOverlay(overlay, metadata, position))
      continue;
    OverlayVisual layer;
    layer.index = originalIndex;
    layer.kind = string(get(overlay, "kind"));
    const auto &selection =
        layer.kind == "roi" ? get(overlay, "selection") : overlay;
    if (layer.kind != "text" && selection.contains(roi_style_attribute)) {
      layer.styled = roiVisual(selection, width, height,
                               color(string(get(overlay, "color"), "#ffcc00")));
    } else {
      Image raster(width, height, 4, PixelType::UInt8);
      paintOverlay(raster, overlay);
      for (int y = 0; y < height; ++y) {
        int x = 0;
        while (x < width) {
          if (raster.at(x, y, 3) == 0) {
            ++x;
            continue;
          }
          const int start = x;
          RoiColor rgba{};
          for (int c = 0; c < 4; ++c)
            rgba[c] = static_cast<unsigned char>(raster.at(x, y, c));
          do {
            ++x;
          } while (x < width && raster.at(x, y, 0) == rgba[0] &&
                   raster.at(x, y, 1) == rgba[1] &&
                   raster.at(x, y, 2) == rgba[2] &&
                   raster.at(x, y, 3) == rgba[3]);
          layer.raster.push_back({{start, y, x - start, 1}, rgba});
        }
      }
    }
    layers.push_back(std::move(layer));
  }
  return layers;
}
void drawOverlayVisual(const OverlayVisual &visual, ImDrawList *draw,
                       std::array<float, 2> origin,
                       std::array<float, 2> scale) {
  if (visual.styled) {
    drawRoiVisual(*visual.styled, draw, origin, scale);
    return;
  }
  const auto low = draw->GetClipRectMin(), high = draw->GetClipRectMax();
  for (float value :
       {origin[0], origin[1], scale[0], scale[1], low.x, low.y, high.x, high.y})
    if (!std::isfinite(value))
      return;
  if (scale[0] <= 0 || scale[1] <= 0 || high.x <= low.x || high.y <= low.y)
    return;
  for (const auto &span : visual.raster) {
    const auto &rect = span.bounds;
    const double
        x0 = std::max(double(low.x), origin[0] + double(rect.x) * scale[0]),
        y0 = std::max(double(low.y), origin[1] + double(rect.y) * scale[1]),
        x1 = std::min(double(high.x),
                      origin[0] + double(rect.x + rect.width) * scale[0]),
        y1 = std::min(double(high.y),
                      origin[1] + double(rect.y + rect.height) * scale[1]);
    if (x0 < x1 && y0 < y1)
      draw->AddRectFilled(
          {float(x0), float(y0)}, {float(x1), float(y1)},
          IM_COL32(span.color[0], span.color[1], span.color[2], span.color[3]));
  }
}
Json overlayVisualJson(const OverlayVisual &visual) {
  Json result = visual.styled ? roiVisualJson(*visual.styled) : Json::object();
  result["role"] = "overlay";
  result["index"] = visual.index;
  result["kind"] = visual.kind;
  result["styled"] = bool(visual.styled);
  result["raster_spans"] = visual.raster.size();
  return result;
}
Image renderImage(const Image &image, const Json &meta, bool includeOverlays) {
  return renderImage(image, meta, includeOverlays,
                     FloatingSamplePolicy::FiniteOnly);
}
Image renderImage(const Image &image, const Json &meta, bool includeOverlays,
                  FloatingSamplePolicy sample_policy) {
  return renderImage(image, meta, includeOverlays, sample_policy, std::nullopt);
}
Image renderImage(const Image &image, const Json &meta, bool includeOverlays,
                  FloatingSamplePolicy sample_policy,
                  std::optional<PlanePosition> position,
                  std::optional<std::array<std::uint32_t,2>> computed) {
  auto result = displayImage(image, meta, sample_policy, computed);
  if (includeOverlays)
    renderDisplayOverlays(result, meta, position);
  return result;
}
void renderDisplayOverlays(Image &image, const Json &metadata) {
  renderDisplayOverlays(image, metadata, std::nullopt);
}
void renderDisplayOverlays(Image &image, const Json &metadata,
                           std::optional<PlanePosition> position) {
  for (const auto &overlay : get(metadata, "overlays"))
    if (visibleOverlay(overlay, metadata, position))
      paintOverlay(image, overlay);
}
} // namespace imagejxx::desktop
