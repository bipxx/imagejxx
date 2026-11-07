#include "rendering.hpp"
#include "path_visual.hpp"
#include "point_visual.hpp"
#include "point_raster_font.hpp"
#include "point_raster_export.hpp"
#include "../native/point_presentation_internal.hpp"
#include "render_memory.hpp"
#include "../native/point_raster_internal.hpp"
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
#include <imagejxx/measurement_overlay.hpp>
#include <imagejxx/composite_path.hpp>
#include <imgui.h>
#include <limits>
#define STBTT_malloc(n,u) imagejxx::desktop::point_font_detail::allocate(n,u)
#define STBTT_free(p,u) imagejxx::desktop::point_font_detail::release(p,u)
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

namespace imagejxx::desktop {
const PlanePreviewDisplayEffects *planePreviewDisplayEffect(
    const std::vector<PlanePreviewDisplayEffects> &effects, PlanePosition position) {
  for (const auto &effect : effects)
    if (effect.position.t == position.t && effect.position.c == position.c &&
        effect.position.z == position.z)
      return &effect;
  return nullptr;
}

std::vector<PlanePreviewDisplayEffects> collectPlanePreviewDisplayEffects(
    const DocumentSnapshot &current, PlanePosition position,
    const std::optional<ChannelDisplayState> &channels,
    const ChannelDisplaySources *sources, bool active_preview,
    const PlanePreviewDisplayQuery &query, ChannelDisplayLimits limits) {
  std::vector<PlanePreviewDisplayEffects> result;
  if (!query) return result;
  const auto append = [&](PlanePosition candidate) {
    if (auto effect = query(current, candidate)) result.push_back(*effect);
  };
  if (channels && sources) {
    const auto estimate = estimate_channel_display(current.data().dataset,
        *channels, Plane::XY, position.z, position.t, position.c, limits);
    for (int c : estimate.contributing_channels)
      if (sources->usesPreview(c)) append({position.t, c, position.z});
  } else if (!channels && active_preview) append(position);
  return result;
}

void projectPlanePreviewDisplayEffects(
    Json &metadata, std::optional<ChannelDisplayState> &channels,
    PlanePosition position, const std::vector<PlanePreviewDisplayEffects> &effects) {
  if (channels) {
    for (const auto &effect : effects)
      if (effect.range && effect.position.t == position.t && effect.position.z == position.z &&
          effect.position.c >= 0 && std::size_t(effect.position.c) < channels->channels.size())
        channels->channels[std::size_t(effect.position.c)].range = *effect.range;
  } else if (const auto *effect = planePreviewDisplayEffect(effects, position)) {
    if (effect->range) metadata["display_range"] = *effect->range;
  }
}

Json planePreviewDisplayKey(
    const DocumentSnapshot &live, PlanePosition position, const Json &metadata,
    const std::optional<ChannelDisplayState> &channels,
    const std::vector<PlanePreviewDisplayEffects> &effects) {
  Json values = Json::array();
  for (const auto &effect : effects)
    values.push_back({{"position", {effect.position.t, effect.position.c, effect.position.z}},
      {"generation", effect.generation}, {"range", effect.range ? displayRangeValue(*effect.range) : Json()},
      {"suppress_threshold_overlay", effect.suppress_threshold_overlay}});
  const auto *active = planePreviewDisplayEffect(effects, position);
  const bool suppress = active && active->suppress_threshold_overlay;
  const auto threshold = live.display().properties.find(manual_threshold_property);
  return {{"effects", std::move(values)},
    {"range", channels ? displayRangeValue(channels->channels.at(position.c).range)
                        : get(metadata, "display_range")},
    {"threshold", !suppress && threshold != live.display().properties.end()
                       ? metadataJson(threshold->second) : Json()},
    {"contrast_source", active && active->range ? "top_hat_preview" : "live"},
    {"threshold_source", suppress ? "top_hat_preview" : "live"}};
}

Json channelDisplayDiagnostics(const ChannelDisplayState &state,
                               const ChannelDisplaySources &sources,
                               Plane plane, int coordinate, int t, int active_c,
                               bool include_z_strips,
                               ChannelDisplayLimits limits,
                               const std::vector<PlanePreviewDisplayEffects> &effects) {
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
    if (!effects.empty()) {
      const auto effect = std::find_if(effects.begin(), effects.end(), [&](const auto &value) {
        return preview && value.position.t == t && value.position.c == c &&
               (plane != Plane::XY || value.position.z == coordinate);
      });
      contributor["contrast_source"] = effect != effects.end() && effect->range
          ? "top_hat_preview" : "live";
      if (effect != effects.end())
        contributor["display_effect"] = {{"position", {effect->position.t, effect->position.c, effect->position.z}},
          {"generation", effect->generation}, {"range", effect->range ? displayRangeValue(*effect->range) : Json()},
          {"suppress_threshold_overlay", effect->suppress_threshold_overlay}};
    }
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
                    RoiColor fallback,std::size_t max_working_bytes) {
  const auto selection = selectionFromJson(json);
  selection.validate(width, height);
  if(selection.is_path())return pathVisual(selection,width,height,{1,1},128ULL*1024*1024,fallback);
  RoiVisual result;
  result.style = resolve_roi_style(selection, fallback);
  result.area = selection_is_area(selection);
  result.markers = selection.kind == "point" || selection.kind == "multipoint";
  result.markerWidth = selection.width;
  if(result.markers && selection.attributes.count(point_style_attribute)) {
    result.pointVisual=pointVisual(selection,result.style.stroke_color.value_or(fallback),max_working_bytes);
    return result;
  }
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
                   std::array<float, 2> origin, std::array<float, 2> scale,
                   std::optional<PlanePosition> position,Plane plane) {
  if(visual.pointVisual) {
    if(visual.style.stroke_color)drawPointVisual(*visual.pointVisual,draw,origin,scale,position,plane);
    return;
  }
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
  return {{"point_visual",visual.pointVisual?pointVisualJson(*visual.pointVisual):Json()},
          {"stroke_color", rgba(visual.style.stroke_color)},
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
  const auto font = annotationFontPath(string(get(o, "font_path")));
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
bool selectionProperty(const Json& selection,const char* key) {
  return selection.contains(key) || get(selection,"attributes").contains(key);
}
bool typedOverlay(const Json& overlay,const Json& selection) {
  return overlay.contains(measurement_overlay_row) || selectionProperty(selection,roi_style_attribute) ||
      selectionProperty(selection,point_style_attribute) || string(get(selection,"kind"))=="path" ||
      string(get(selection,"kind"))=="region";
}
MeasurementOverlayPresentation overlayPresentation(const Json& metadata) {
  const auto& properties=get(metadata,"native_properties");
  const auto it=properties.find(measurement_overlay_property);
  if(it==properties.end())return {};
  // Bound the only converted object before calling the authoritative native
  // decoder. Never convert/copy arbitrary image metadata just to read flags.
  const auto& value=*it;
  if(!value.is_object() || value.size()!=5 || !get(value,"version").is_number_integer() ||
      !get(value,"draw_labels").is_boolean() || !get(value,"draw_names").is_boolean() ||
      !get(value,"draw_backgrounds").is_boolean() || !get(value,"label_color").is_array() ||
      get(value,"label_color").size()!=4)
    throw std::invalid_argument("Invalid measurement overlay presentation");
  for(const auto& channel:value.at("label_color"))
    if(!channel.is_number_integer() || channel<0 || channel>255)
      throw std::invalid_argument("Invalid measurement overlay label color");
  return measurement_overlay_presentation({{measurement_overlay_property,metadataFromJson(value)}});
}
std::optional<OverlayVisual::Caption> overlayCaption(const Json& overlay,std::size_t index,
    const MeasurementOverlayPresentation& presentation) {
  const auto kind=string(get(overlay,"kind"));
  if(!presentation.present || !presentation.draw_labels || kind.empty() || kind=="text")return {};
  const auto& wire=kind=="roi"?get(overlay,"selection"):overlay;
  if(!wire.is_object())return {};
  OverlayVisual::Caption result;
  const auto& name=get(overlay,"name").is_string()?get(overlay,"name"):
      (get(wire,"imagej_roi_name").is_string()?get(wire,"imagej_roi_name"):get(get(wire,"attributes"),"imagej_roi_name"));
  if(presentation.draw_names) {
    if(!name.is_string())return {};
    const auto& text=name.get_ref<const std::string&>();
    if(text.size()>4096)throw std::length_error("Overlay label exceeds4096 bytes");
    result.text=text;
  } else result.text=std::to_string(index+1);
  if(result.text.empty())return {};
  const auto selection=selectionFromJson(wire);
  if(selection.is_path())result.bounds=selection.path_geometry().bounds();
  else if(selection.is_mask()) {
    const auto origin=selection.mask_origin();
    result.bounds={double(origin[0]),double(origin[1]),double(origin[0])+selection.mask_width(),double(origin[1])+selection.mask_height()};
  } else if(const auto frame=selection.box_frame())result.bounds=frame->bounds(selection.kind=="rotated_ellipse");
  else {
    if(selection.points.empty())return {};
    result.bounds={selection.points[0][0],selection.points[0][1],selection.points[0][0],selection.points[0][1]};
    for(const auto& point:selection.points)for(unsigned d=0;d<2;++d) {
      result.bounds[d]=std::min(result.bounds[d],point[d]);result.bounds[d+2]=std::max(result.bounds[d+2],point[d]);
    }
  }
  for(auto coordinate:result.bounds)if(!std::isfinite(coordinate))throw std::invalid_argument("Overlay label bounds must be finite");
  if(selection.kind=="point" || selection.kind=="multipoint") {
    const auto style=point_style(selection);result.point_size=int(style?style->size:1);
  }
  result.color=presentation.label_color;result.background=presentation.draw_backgrounds;return result;
}
struct CaptionPlacement {double x,y;int size;bool centered;};
CaptionPlacement captionPlacement(const OverlayVisual::Caption& caption,std::array<double,2> origin,
    std::array<double,2> scale,double dpi=1) {
  const auto& b=caption.bounds;
  int size=(b[2]-b[0])*scale[0]>40 || (b[3]-b[1])*scale[1]>40?12:9;
  if(caption.point_size>=0)size=caption.point_size<2?9:(caption.point_size<4?10:12);
  size=std::max(1,int(std::round(size*dpi)));
  const double x=origin[0]+(b[0]+b[2])*.5*scale[0],y=origin[1]+(b[1]+b[3])*.5*scale[1];
  const double baseline=y+std::max(size/2.,6*dpi)-2*dpi;
  if(caption.point_size>=0)return {x+(6+caption.point_size)*dpi,
      baseline+size+(caption.point_size-6)*dpi,size,true};
  return {x,baseline,size,true};
}
struct CaptionGlyph {std::shared_ptr<const PointRasterGlyph> glyph;double x,y;};
struct CaptionPixels {
  std::vector<CaptionGlyph> glyphs;
  std::array<double,4> bounds{};
};
CaptionPixels prepareCaptionPixels(const OverlayVisual::Caption& caption,PointRasterFontCache& font) {
  const auto placement=captionPlacement(caption,{0,0},{1,1});
  CaptionPixels out;out.glyphs.reserve(caption.text.size());
  double x=0,y=std::floor(placement.y),width=0;std::size_t lines=1;
  for(std::size_t i=0;i<caption.text.size();) {
    kernel_checkpoint_periodic();
    unsigned cp=static_cast<unsigned char>(caption.text[i++]);int continuation=0;
    if(cp>=0xf0){cp&=7;continuation=3;}else if(cp>=0xe0){cp&=15;continuation=2;}else if(cp>=0xc0){cp&=31;continuation=1;}
    for(int k=0;k<continuation && i<caption.text.size();++k)cp=(cp<<6)|(static_cast<unsigned char>(caption.text[i++])&63);
    if(cp=='\n'){width=std::max(width,x);x=0;y+=placement.size;++lines;continue;}
    const auto glyph=font.textGlyph(placement.size,cp);
    out.glyphs.push_back({glyph,std::trunc(x)+glyph->offset_x,y+glyph->offset_y});x+=float(glyph->advance);
  }
  width=std::max(width,x);
  const double left=placement.x-(placement.centered?width*.5:0);
  for(auto& glyph:out.glyphs)glyph.x=std::trunc(glyph.x+left);
  out.bounds={left-1,placement.y-font.textBaseline(placement.size)-1,left+width+1,
      placement.y-font.textBaseline(placement.size)+placement.size*lines+1};
  return out;
}
void paintCaptionPixels(Image& image,const OverlayVisual::Caption& caption,const CaptionPixels& pixels) {
  auto clipped=[](double value,int maximum){return int(std::clamp(value,0.,double(maximum)));};
  if(caption.background) {
    const auto& b=pixels.bounds;
    for(int y=clipped(std::floor(b[1]),image.height);y<clipped(std::ceil(b[3]),image.height);++y)
      for(int x=clipped(std::floor(b[0]),image.width);x<clipped(std::ceil(b[2]),image.width);++x) {
        kernel_checkpoint_periodic();blend(image,x,y,{0,0,0,255});
      }
  }
  for(const auto& placed:pixels.glyphs) {
    const auto& glyph=*placed.glyph;
    for(int y=clipped(placed.y,image.height);y<clipped(placed.y+glyph.height,image.height);++y)
      for(int x=clipped(placed.x,image.width);x<clipped(placed.x+glyph.width,image.width);++x) {
        kernel_checkpoint_periodic();const auto gx=std::size_t(x-placed.x),gy=std::size_t(y-placed.y);
        blend(image,x,y,caption.color,glyph.coverage[gy*glyph.width+gx]/255.);
      }
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
void paintOverlay(Image &result, const Json &o,
                  std::optional<PlanePosition> position=std::nullopt,
                  PointRasterFontCache* fonts=nullptr,bool show_all=false) {
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
  if (selectionProperty(selection,point_style_attribute)) {
    if(result.channels!=3)throw std::invalid_argument("Point flatten needs RGB display pixels");
    const auto typed=selectionFromJson(selection);
    const auto resolved=resolve_roi_style(typed,rgba);
    if(!resolved.stroke_color)return;
    PointPresentationOptions view;view.color=*resolved.stroke_color;
    view.position=position;view.show_all=show_all;
    PointRasterLimits limits;
    // Reserve font file/coverage/stb scratch alongside native layout/buffers.
    constexpr std::size_t font_allowance=8*1048576;
    limits.max_working_bytes-=font_allowance;
    const auto dense=RenderMemory::mul(RenderMemory::mul(std::size_t(result.width),std::size_t(result.height)),6*sizeof(double));
    if(dense>limits.max_working_bytes)throw std::length_error("Point flatten display buffer limit");
    limits.presentation.max_working_bytes=limits.max_working_bytes-dense;
    auto layout=point_presentation(typed,view,limits.presentation);
    const PointRasterFont* font=nullptr;
    std::unique_ptr<PointRasterFontCache> local;
    for(const auto& primitive:layout.primitives)if(primitive.kind==PointPrimitiveKind::Text) {
      if(!fonts){local=std::make_unique<PointRasterFontCache>(font_allowance);fonts=local.get();}
      font=&fonts->get(string(get(o,"font_path")),primitive.font_size);break;
    }
    const auto prepared=detail::prepare_point_raster(result.width,result.height,typed,view,font,limits,&layout);
    result=detail::raster_point_style(result,prepared,font);
    return;
  }
  if (typedOverlay(o,selection)) {
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
  const auto presentation=overlayPresentation(metadata);
  for (const auto &overlay : get(metadata, "overlays")) {
    if (!visibleOverlay(overlay, metadata, position) ||
        string(get(overlay, "kind")) == "text")
      continue;
    const auto &selection = string(get(overlay, "kind")) == "roi"
                                ? get(overlay, "selection")
                                : overlay;
    if (typedOverlay(overlay,selection) || (presentation.present && presentation.draw_labels))
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
  const auto presentation=overlayPresentation(metadata);
  std::size_t label_bytes=0,index = 0;
  for (const auto &overlay : get(metadata, "overlays")) {
    const auto originalIndex = index++;
    if (!visibleOverlay(overlay, metadata, position))
      continue;
    OverlayVisual layer;
    layer.index = originalIndex;
    layer.kind = string(get(overlay, "kind"));
    const auto &selection =
        layer.kind == "roi" ? get(overlay, "selection") : overlay;
    if (layer.kind != "text" && typedOverlay(overlay,selection)) {
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
    layer.caption=overlayCaption(overlay,originalIndex,presentation);
    if(layer.caption) {
      label_bytes=RenderMemory::add(label_bytes,layer.caption->text.size());
      if(label_bytes>1048576)throw std::length_error("Overlay labels exceed view text limit");
    }
    layers.push_back(std::move(layer));
  }
  return layers;
}
void drawOverlayVisual(const OverlayVisual &visual, ImDrawList *draw,
                       std::array<float, 2> origin,
                       std::array<float, 2> scale,std::optional<PlanePosition> current,Plane plane) {
  visual.caption_drawn=false;
  if (visual.styled)drawRoiVisual(*visual.styled, draw, origin, scale,current,plane);
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
  if(visual.caption && plane==Plane::XY) {
    const auto& caption=*visual.caption;const double dpi=visual.gui_scale;
    if(!std::isfinite(dpi) || dpi<=0 || dpi>16)throw std::invalid_argument("Unsupported overlay caption DPI");
    auto placed=captionPlacement(caption,{origin[0],origin[1]},{scale[0],scale[1]},dpi);
    auto* font=ImGui::GetFont();
    if(caption.point_size>=0)placed.y+=(font->Ascent-font->Descent)*placed.size/font->FontSize-placed.size;
    const auto extent=font->CalcTextSizeA(float(placed.size),FLT_MAX,0,caption.text.c_str());
    const double x=placed.x-(placed.centered?extent.x*.5:0),y=placed.y-font->Ascent*placed.size/font->FontSize;
    const std::array<double,4> bounds{x-dpi,y-dpi,x+extent.x+dpi,y+extent.y+dpi};
    for(auto coordinate:bounds)if(!std::isfinite(coordinate))return;
    visual.caption_font_size=float(placed.size);visual.caption_screen_bounds=bounds;
    if(bounds[2]>low.x && bounds[0]<high.x && bounds[3]>low.y && bounds[1]<high.y) {
      if(caption.background)draw->AddRectFilled({float(bounds[0]),float(bounds[1])},{float(bounds[2]),float(bounds[3])},IM_COL32(0,0,0,255));
      draw->AddText(font,float(placed.size),{float(x),float(y)},IM_COL32(caption.color[0],caption.color[1],caption.color[2],caption.color[3]),caption.text.c_str());
      visual.caption_drawn=true;
    }
  }
}
Json overlayVisualJson(const OverlayVisual &visual) {
  Json result = visual.styled ? roiVisualJson(*visual.styled) : Json::object();
  result["role"] = "overlay";
  result["index"] = visual.index;
  result["kind"] = visual.kind;
  result["styled"] = bool(visual.styled);
  result["raster_spans"] = visual.raster.size();
  if(visual.caption)result["caption"]={{"text",visual.caption->text},{"image_bounds",visual.caption->bounds},
      {"background",visual.caption->background},{"color",visual.caption->color},{"point_size",visual.caption->point_size},
      {"font_size_device",visual.caption_font_size},{"screen_bounds",visual.caption_screen_bounds},{"drawn",visual.caption_drawn}};
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
namespace {
void paintDisplayOverlays(Image& image,const Json& metadata,
    std::optional<PlanePosition> position,bool show_all) {
  // One lazy resource owner per render, reused across decimal labels/overlays.
  PointRasterFontCache fonts;
  if(!position && get(metadata,"slice_index").is_number_integer())
    position=PlanePosition{0,0,integer(get(metadata,"slice_index"))};
  const auto presentation=overlayPresentation(metadata);
  std::size_t index=0;
  for (const auto &overlay : get(metadata, "overlays")) {
    const auto original=index++;
    if (visibleOverlay(overlay, metadata, position)) {
      paintOverlay(image, overlay,position,&fonts,show_all);
      if(const auto caption=overlayCaption(overlay,original,presentation)) {
        fonts.open(annotationFontPath(string(get(overlay,"font_path"))));
        paintCaptionPixels(image,*caption,prepareCaptionPixels(*caption,fonts));
      }
    }
  }
}
}
void renderDisplayOverlays(Image &image, const Json &metadata) {
  renderDisplayOverlays(image, metadata, std::nullopt);
}
void renderDisplayOverlays(Image &image, const Json &metadata,
                           std::optional<PlanePosition> position) {
  paintDisplayOverlays(image,metadata,position,false);
}
namespace {
void exportCharge(std::size_t& used,std::size_t count,std::size_t width,std::size_t limit,
                  const ExportAdmission& admit={},
                  const char* error="Display export exceeds configured processing memory budget") {
  if(used>limit || (count && width>(limit-used)/count))
    throw std::length_error(error);
  const auto next=used+count*width;if(admit)admit(next);used=next;
}
constexpr std::size_t export_work_limit=512*1048576;
void exportWork(std::size_t& used,std::size_t count,std::size_t width=1) {
  exportCharge(used,count,width,export_work_limit,{},"Display export exceeds bounded work limit");
}
void exportMetadataCharge(const MetadataValue& value,std::size_t& used,std::size_t cap,std::size_t& work,
    const ExportAdmission& admit={}) {
  kernel_checkpoint_periodic();
  exportWork(work,1);
  // JSON staging, native selection decoding and retained render records. The
  // source's immutable metadata is borrowed, never cloned before admission.
  exportCharge(used,1,512,cap,admit);
  if(const auto* text=std::get_if<std::string>(&value))exportCharge(used,text->size(),4,cap,admit);
  else if(const auto* texts=std::get_if<std::vector<std::string>>(&value))
    for(const auto& text:*texts){exportCharge(used,1,512,cap,admit);exportCharge(used,text.size(),4,cap,admit);}
  else if(const auto* tree=std::get_if<std::shared_ptr<const MetadataTree>>(&value)) {
    if(!*tree)throw std::invalid_argument("Invalid display export metadata");
    if(const auto* array=std::get_if<MetadataArray>(&(*tree)->value))
      for(const auto& child:*array)exportMetadataCharge(child,used,cap,work,admit);
    else for(const auto& child:std::get<MetadataObject>((*tree)->value)) {
      exportCharge(used,child.first.size(),4,cap,admit);exportMetadataCharge(child.second,used,cap,work,admit);
    }
  }
}
bool exportVisible(const MetadataObject& row,PlanePosition position) {
  const auto kind=row.find("kind");
  if(kind==row.end() || std::holds_alternative<std::monostate>(kind->second) ||
      (std::holds_alternative<std::string>(kind->second) && std::get<std::string>(kind->second).empty()))return false;
  for(const auto& field:std::array<std::pair<const char*,int>,4>{{
      {"slice_index",position.z},{"t",position.t},{"c",position.c},{"z",position.z}}}) {
    const auto found=row.find(field.first);if(found==row.end() || std::holds_alternative<std::monostate>(found->second))continue;
    if(const auto* n=std::get_if<std::int64_t>(&found->second)){if(*n!=field.second)return false;}
    else if(const auto* n=std::get_if<std::uint64_t>(&found->second)){if(*n!=std::uint64_t(field.second))return false;}
    else return false;
  }
  return true;
}
std::size_t exportFontBytes(const PointRasterFont& font) {
  std::size_t bytes=sizeof(PointRasterFont)+256;
  for(const auto& glyph:font.digits)bytes=RenderMemory::add(bytes,glyph.coverage.size());
  return bytes;
}
}
struct PreparedExportImage::State {
  struct TextGlyph {
    std::shared_ptr<const PointRasterGlyph> glyph;
    double x,y;
  };
  struct Row {
    Json wire;
    std::vector<TextGlyph> text;
    std::optional<OverlayVisual::Caption> caption;
    CaptionPixels caption_pixels;
    RoiColor text_color{};
    bool text_row=false;
    std::optional<Selection> selection;
    PointPresentation layout;
    std::optional<imagejxx::detail::PreparedPointRaster> raster;
    std::shared_ptr<const PointRasterFont> font;
    std::string font_path;
    double font_size=0;
    bool skip=false;
  };
  ExportAdmission admit;
  DocumentSnapshot source;
  PlanePosition position;
  bool show_all;
  std::optional<DocumentMetadata> output_metadata;
  SpatialCalibration output_calibration;
  std::size_t limit,used=0,peak=0,work=0;
  Json metadata=Json::object();
  std::vector<Row> rows;
  State(DocumentSnapshot snapshot,bool all,std::size_t cap,bool show_rois,DisplayExportOutput output,ExportAdmission admission,
      std::optional<DisplayExportFontPolicy> font_policy)
      :admit(std::move(admission)),source(std::move(snapshot)),
       position{source.display().t,source.display().c,source.display().crosshair[2]},show_all(all),limit(cap) {
    kernel_checkpoint();const auto shape=source.data().dataset.shape();
    if((shape.s!=1 && shape.s!=3 && shape.s!=4) || shape.t!=1 || shape.c!=1)throw std::invalid_argument("Legacy display export requires one T/C source");
    const auto pixels=RenderMemory::mul(std::size_t(shape.x),std::size_t(shape.y));
    // Source-plane lease, display conversion/IEEE projection, RGB result,
    // sequential legacy masks, publication and codec/packing copies. Existing
    // document/history and arbitrary reader caches remain outside this budget.
    exportCharge(used,1,131072,limit,admit);
    exportCharge(used,pixels,RenderMemory::add(64*std::size_t(shape.s),272),limit,admit);
    constexpr auto work_limit=export_work_limit;
    exportWork(work,pixels,8*std::size_t(shape.s)+3);
    const auto labels=source.data().metadata.extensions.find("labels");
    if(labels!=source.data().metadata.extensions.end()) {
      exportMetadataCharge(labels->second,used,limit,work,admit);metadata["labels"]=metadataJson(labels->second);
    }
    for(const auto* key:{"display_range","lut","custom_lut"}) {
      const auto found=source.display().properties.find(key);if(found==source.display().properties.end())continue;
      exportMetadataCharge(found->second,used,limit,work,admit);metadata[key]=metadataJson(found->second);
    }
    metadata["slice_index"]=position.z;
    // Validate mapping metadata with a bounded carrier before any source reads.
    Image carrier(1,1,shape.s,source.data().dataset.pixel_type());
    (void)displayImage(carrier,metadata,source.data().dataset.sample_policy());
    MetadataObject provenance;
    if(output==DisplayExportOutput::Document) {
      output_metadata.emplace();
      // Admit source properties before copying any owned strings. Structural
      // extensions/codec layouts describe the source, not the detached RGB result.
      exportCharge(used,source.data().metadata.title.size(),4,limit,admit);
      for(const auto& field:source.data().metadata.properties) {
        exportCharge(used,field.first.size(),4,limit,admit);exportMetadataCharge(field.second,used,limit,work,admit);
      }
      provenance={{"version",std::int64_t(1)},
          {"source_document_id",source.id()}, {"source_title",source.data().metadata.title},
          {"source_position",metadata_array({std::int64_t(position.t),std::int64_t(position.c),std::int64_t(position.z)})},
          {"display_values",true}, {"manager_visible",show_rois}};
      if(source.data_identity())provenance["source_data_identity"]=*source.data_identity();
      const auto revisions=source.revisions();
      provenance["source_revisions"]=metadata_array({revisions.pixels,revisions.metadata,revisions.selection,revisions.display});
      output_metadata->title="Flat_"+source.data().metadata.title;
      output_metadata->properties=source.data().metadata.properties;
      const auto intensity=output_metadata->properties.find("imagejxx.intensity_calibration");
      if(intensity!=output_metadata->properties.end()) {
        provenance["source_intensity_calibration"]=intensity->second;
        output_metadata->properties.erase(intensity);
      }
      const auto& source_calibration=source.data().dataset.calibration();
      exportCharge(used,source_calibration.unit.size(),4,limit,admit);
      exportCharge(used,source_calibration.time_unit.size(),4,limit,admit);
      output_calibration=source_calibration;
      // Output z=0 denotes the rendered source plane in the native affine model.
      output_calibration.origin=output_calibration.voxel_to_world({0,0,double(position.z)});
      output_calibration.time_origin=output_calibration.time(position.t);
    }
    const auto overlay_presentation=measurement_overlay_presentation(source.data().metadata.properties);
    struct Input {const MetadataObject* overlay;const Selection* selection;const char* color;std::size_t overlay_index=0;};
    std::vector<Input> inputs;
    auto admitSelection=[&](const Selection& selection,const char* color) {
      exportCharge(used,1,sizeof(Input)+sizeof(Row)+1024,limit,admit);
      exportCharge(used,selection.points.size(),256,limit,admit);exportWork(work,selection.points.size());
      if(selection.is_path()){exportCharge(used,selection.path_commands().size(),64,limit,admit);exportWork(work,selection.path_commands().size());}
      if(selection.is_mask())exportCharge(used,selection.packed_mask().size(),16,limit,admit);
      for(const auto& field:selection.attributes){exportCharge(used,field.first.size(),4,limit,admit);exportMetadataCharge(field.second,used,limit,work,admit);}
      inputs.push_back({nullptr,&selection,color});
    };
    // ImageCanvas paints overlays, the visible manager list, then the active ROI.
    std::size_t overlay_index=0;
    for(const auto& row:source.data().overlays) {
      const auto original_index=overlay_index++;
      kernel_checkpoint_periodic();exportWork(work,1);
      if(!exportVisible(row,position))continue;
      exportCharge(used,1,sizeof(Input)+sizeof(Row)+256,limit,admit);
      for(const auto& field:row){exportCharge(used,field.first.size(),4,limit,admit);exportMetadataCharge(field.second,used,limit,work,admit);}
      inputs.push_back({&row,nullptr,nullptr,original_index});
    }
    const auto overlay_count=inputs.size();
    if(show_rois)for(const auto& roi:source.data().rois) {
      kernel_checkpoint_periodic();exportWork(work,1);
      if((!roi.t || *roi.t==position.t) && (!roi.c || *roi.c==position.c) && (!roi.z || *roi.z==position.z))
        admitSelection(roi.selection,"#40d2f0b4");
    }
    const auto manager_count=inputs.size()-overlay_count;
    const auto& active=source.data().selection;
    if(active && (source.data().overlays.empty() || active->kind=="point" || active->kind=="multipoint"))
      admitSelection(*active,"#ffd54bff");
    if(output_metadata) {
      provenance["overlay_rows"]=std::uint64_t(overlay_count);
      provenance["manager_rows"]=std::uint64_t(manager_count);
      provenance["active_rows"]=std::uint64_t(inputs.size()-overlay_count-manager_count);
      output_metadata->extensions["imagejxx.display_export"]=metadata_object(std::move(provenance));
    }
    rows.reserve(inputs.size());
    struct Fonts {
      std::map<double,std::shared_ptr<const PointRasterFont>> points;
      std::vector<std::size_t> text_rows,caption_rows;
    };
    std::map<std::string,Fonts> fonts;
    auto fontPath=[&](std::string configured) {
      auto path=annotationFontPath(configured.empty() && font_policy?font_policy->default_path:std::move(configured));
      if(font_policy) {
        auto resolved=std::filesystem::u8path(path);if(resolved.is_relative())resolved=font_policy->directory/resolved;
        path=resolved.lexically_normal().u8string();
        if(path.size()>131064)throw std::invalid_argument("Resolved font path exceeds limit");
      }
      return path;
    };
    std::optional<std::string> default_font;
    for(const auto& input:inputs) {
      kernel_checkpoint_periodic();
      Row row;
      if(input.overlay)row.wire=metadataJson(*input.overlay);
      else row.wire={{"kind","roi"},{"selection",selectionJson(*input.selection)},{"color",input.color}};
      const auto kind=string(get(row.wire,"kind"));
      if(kind=="text") {
        row.text_row=true;
        const auto& anchor=get(row.wire,"points");const auto content=string(get(row.wire,"text"));
        // Preserve malformed/absent legacy anchors as no-op geometry. Empty
        // rows acquire no otherwise-unused font resource.
        if(content.find_first_not_of('\n')==std::string::npos || !anchor.is_array() || anchor.empty() || anchor[0].size()!=2)row.skip=true;
        else {
          if(!std::isfinite(number(anchor[0][0])) || !std::isfinite(number(anchor[0][1])))
            throw std::invalid_argument("Text anchor must be finite");
          const int size=integer(get(row.wire,"font_size"),16);
          if(size<=0 || size>512)throw std::invalid_argument("Font size must be1..512");
          row.font_size=size;row.text_color=color(string(get(row.wire,"color"),"#ffcc00"));
          const auto configured=string(get(row.wire,"font_path"));
          if(configured.empty()){if(!default_font)default_font=fontPath({});row.font_path=*default_font;}
          else row.font_path=fontPath(configured);
          exportCharge(used,row.font_path.size(),3,limit,admit);
          exportCharge(used,content.size(),sizeof(TextGlyph)+8,limit,admit);
          exportWork(work,content.size(),8);
          row.text.reserve(content.size());
          fonts[row.font_path].text_rows.push_back(rows.size());
        }
      } else {
        const auto& wire=kind=="roi"?get(row.wire,"selection"):row.wire;
        if(selectionProperty(wire,point_style_attribute)) {
          row.selection=selectionFromJson(wire);
          const auto style=resolve_roi_style(*row.selection,color(string(get(row.wire,"color"),"#ffcc00")));
          PointPresentationOptions options;options.position=position;options.show_all=show_all;
          options.color=style.stroke_color.value_or(RoiColor{0,0,0,0});
          PointPresentationLimits bounds;bounds.max_working_bytes=std::min(bounds.max_working_bytes,limit-used);
          row.layout=imagejxx::detail::point_presentation_admitted(*row.selection,options,bounds,
              [&](std::size_t bytes){if(admit)admit(RenderMemory::add(used,bytes));});
          exportCharge(used,row.layout.working_bytes,1,limit,admit);
          for(const auto& primitive:row.layout.primitives)if(primitive.kind==PointPrimitiveKind::Text) {
            const auto configured=string(get(row.wire,"font_path"));
            if(configured.empty()){if(!default_font)default_font=fontPath({});row.font_path=*default_font;}
            else row.font_path=fontPath(configured);
            row.font_size=primitive.font_size;
            exportCharge(used,row.font_path.size(),3,limit,admit);
            fonts[row.font_path].points.emplace(row.font_size,nullptr);break;
          }
        }
      }
      if(input.overlay)row.caption=overlayCaption(row.wire,input.overlay_index,overlay_presentation);
      if(row.caption) {
        exportCharge(used,row.caption->text.size(),sizeof(CaptionGlyph)+8,limit,admit);
        exportWork(work,row.caption->text.size(),8);
        const auto configured=string(get(row.wire,"font_path"));
        std::string path;
        if(configured.empty()){if(!default_font)default_font=fontPath({});path=*default_font;}
        else path=fontPath(configured);
        exportCharge(used,path.size(),3,limit,admit);
        fonts[path].caption_rows.push_back(rows.size());
      }
      rows.push_back(std::move(row));
    }
    peak=used;
    for(auto& file:fonts) {
      const auto base=used;
      const auto allowance=std::min<std::size_t>((file.second.text_rows.empty() && file.second.caption_rows.empty()?8:64)*1048576,limit-used);
      if(allowance<65536)throw std::length_error("Annotation font exceeds configured processing memory budget");
      PointRasterFontCache owner(allowance,work_limit-work,[&](std::size_t bytes) {
        const auto total=RenderMemory::add(base,bytes);if(admit)admit(total);peak=std::max(peak,total);
      });owner.open(file.first);
      for(auto& entry:file.second.points) {
        auto font=owner.take(file.first,entry.first);
        exportCharge(used,exportFontBytes(font),1,limit,admit);
        entry.second=std::make_shared<const PointRasterFont>(std::move(font));
      }
      for(auto index:file.second.text_rows) {
        auto& row=rows[index];const int size=int(row.font_size);
        const auto& anchor=row.wire.at("points")[0];const auto content=string(get(row.wire,"text"));
        const double origin=number(anchor[0]);double x=origin,y=std::floor(number(anchor[1]))+owner.textBaseline(size);
        for(std::size_t i=0;i<content.size();) {
          kernel_checkpoint_periodic();
          unsigned cp=static_cast<unsigned char>(content[i++]);int continuation=0;
          if(cp>=0xf0){cp&=7;continuation=3;}else if(cp>=0xe0){cp&=15;continuation=2;}else if(cp>=0xc0){cp&=31;continuation=1;}
          for(int k=0;k<continuation && i<content.size();++k)cp=(cp<<6)|(static_cast<unsigned char>(content[i++])&63);
          if(cp=='\n'){x=origin;y+=size;continue;}
          std::shared_ptr<const PointRasterGlyph> glyph;
          const auto digits=file.second.points.find(row.font_size);
          if(cp>='0' && cp<='9' && digits!=file.second.points.end())
            glyph=std::shared_ptr<const PointRasterGlyph>(digits->second,&digits->second->digits[cp-'0']);
          else glyph=owner.textGlyph(size,cp);
          const double left=std::trunc(x)+glyph->offset_x,top=y+glyph->offset_y;
          const double l=std::max(0.,left),t=std::max(0.,top);
          const double r=std::min(double(shape.x),left+glyph->width),b=std::min(double(shape.y),top+glyph->height);
          if(l<r && t<b) {
            // Legacy text has integer bitmap anchors. Clip before casting huge
            // offscreen coordinates; coverage/alpha order remains per glyph.
            exportWork(work,std::size_t(r-l),std::size_t(b-t));
            row.text.push_back({glyph,left,top});
          }
          x+=float(glyph->advance);
        }
        row.wire=Json();
      }
      for(auto index:file.second.caption_rows) {
        auto& row=rows[index];row.caption_pixels=prepareCaptionPixels(*row.caption,owner);
        for(const auto& placed:row.caption_pixels.glyphs) {
          const auto& glyph=*placed.glyph;
          const auto width=std::max(0.,std::min(double(shape.x),placed.x+glyph.width)-std::max(0.,placed.x));
          const auto height=std::max(0.,std::min(double(shape.y),placed.y+glyph.height)-std::max(0.,placed.y));
          exportWork(work,std::size_t(std::ceil(width)),std::size_t(std::ceil(height)));
        }
        if(row.caption->background) {
          const auto& b=row.caption_pixels.bounds;
          const auto width=std::max(0.,std::min(double(shape.x),std::ceil(b[2]))-std::max(0.,std::floor(b[0])));
          const auto height=std::max(0.,std::min(double(shape.y),std::ceil(b[3]))-std::max(0.,std::floor(b[1])));
          exportWork(work,std::size_t(width),std::size_t(height));
        }
      }
      exportCharge(used,owner.text_bytes(),1,limit,admit);
      exportWork(work,owner.working_work());
      peak=std::max({peak,used,RenderMemory::add(base,owner.working_bytes())});
    }
    for(auto& row:rows)if(row.selection) {
      if(row.layout.primitives.empty()){row.skip=!row.caption;row.wire=Json();row.selection.reset();continue;}
      if(row.font_size)row.font=fonts.at(row.font_path).points.at(row.font_size);
      PointPresentationOptions options;options.position=position;options.show_all=show_all;
      // Layout already owns the exact resolved colors/options. Raster uses only
      // coordinates, relative mode, clip and emitted font size from that capture.
      PointRasterLimits bounds;bounds.max_working_bytes=limit-(used-row.layout.working_bytes);
      bounds.max_pixel_samples=std::min(bounds.max_pixel_samples,work_limit-work);
      row.raster=imagejxx::detail::prepare_point_raster(shape.x,shape.y,*row.selection,options,row.font.get(),bounds,&row.layout,true);
      exportWork(work,row.raster->pixel_samples);
      peak=std::max(peak,RenderMemory::add(used-row.raster->layout.working_bytes,row.raster->working_bytes));
      if(admit)admit(peak);
      row.selection.reset();
    }
    kernel_checkpoint();
    admit={}; // Do not retain the worker admission callback in a detached result.
  }
};
PreparedExportImage::PreparedExportImage(DocumentSnapshot source,bool all,std::size_t cap,bool show_rois,DisplayExportOutput output,ExportAdmission admit,
    std::optional<DisplayExportFontPolicy> font_policy) {
  // Admit the fixed owner/control allowance before constructing State. The
  // accepted snapshot is moved into it, never cloned by this preparation path.
  std::size_t initial=0;exportCharge(initial,1,131072,cap,admit);
  state_=std::make_unique<State>(std::move(source),all,cap,show_rois,output,std::move(admit),std::move(font_policy));
}
PreparedExportImage::~PreparedExportImage()=default;
std::size_t PreparedExportImage::working_bytes() const{return state_->peak;}
void PreparedExportImage::validate(const DocumentSnapshot& now,bool all,std::size_t cap) const {
  if(!state_->source.same_document(now) || !state_->source.shares_data(now) ||
      !now.revisions().matches(state_->source.revisions(),Change::All) ||
      all!=state_->show_all || cap!=state_->limit)throw RevisionConflict();
  const auto& a=now.display();const auto& b=state_->source.display();
  if(a.t!=b.t || a.c!=b.c || a.crosshair[2]!=b.crosshair[2])throw RevisionConflict();
  for(const auto* key:{"display_range","lut","custom_lut"}) {
    const auto left=a.properties.find(key),right=b.properties.find(key);
    if((left==a.properties.end())!=(right==b.properties.end()) ||
        (left!=a.properties.end() && !metadata_equal(left->second,right->second)))throw RevisionConflict();
  }
}
std::shared_ptr<imagejxx::Document> PreparedExportImage::document(Image image) const {
  if(!state_->output_metadata)throw std::logic_error("Pixel-only export has no document metadata");
  const auto shape=state_->source.data().dataset.shape();
  if(image.width!=shape.x || image.height!=shape.y || image.channels!=3 || image.type!=PixelType::UInt8)
    throw std::invalid_argument("Flatten result must be the captured RGB plane");
  Stack frames;frames.push_back(std::move(image));
  DocumentData data{Dataset::from_stack(std::move(frames),state_->output_calibration),*state_->output_metadata};
  return std::make_shared<imagejxx::Document>(std::move(data));
}
Image PreparedExportImage::render(const ExecutionControl& control) const {
  ExecutionScope scope(control);kernel_checkpoint();
  const auto input=state_->source.data().dataset.plane(state_->position);
  auto output=displayImage(*input,state_->metadata,state_->source.data().dataset.sample_policy());
  if(std::any_of(state_->rows.begin(),state_->rows.end(),[](const auto& row){return bool(row.raster);}))
    for(double value:output.pixels){kernel_checkpoint_periodic();if(!std::isfinite(value) || value<0 || value>255)throw std::invalid_argument("Invalid owned display RGB component");}
  for(const auto& row:state_->rows) {
    kernel_checkpoint();
    if(row.skip)continue;
    if(row.raster)imagejxx::detail::paint_owned_point_style(output,*row.raster,row.font.get());
    else if(row.text_row)for(const auto& placed:row.text) {
      const auto& glyph=*placed.glyph;
      const int left=int(std::max(0.,placed.x)),top=int(std::max(0.,placed.y));
      const int right=int(std::min(double(output.width),placed.x+glyph.width));
      const int bottom=int(std::min(double(output.height),placed.y+glyph.height));
      for(int y=top;y<bottom;++y)for(int x=left;x<right;++x) {
        kernel_checkpoint_periodic();
        const auto gx=std::size_t(x-placed.x),gy=std::size_t(y-placed.y);
        blend(output,x,y,row.text_color,glyph.coverage[gy*glyph.width+gx]/255.);
      }
    } else paintOverlay(output,row.wire,state_->position,nullptr,state_->show_all);
    if(row.caption)paintCaptionPixels(output,*row.caption,row.caption_pixels);
  }
  kernel_checkpoint();imagejxx::detail::without_execution_callbacks([]{kernel_checkpoint();});return output;
}
} // namespace imagejxx::desktop
