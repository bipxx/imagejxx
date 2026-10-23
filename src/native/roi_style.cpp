#include <algorithm>
#include <cctype>
#include <cmath>
#include <imagejxx/operations.hpp>
#include <imagejxx/roi_style.hpp>

namespace imagejxx {
namespace {
const MetadataObject &object(const MetadataValue &value) {
  const auto tree = std::get_if<std::shared_ptr<const MetadataTree>>(&value);
  if (!tree || !*tree ||
      !std::holds_alternative<MetadataObject>((*tree)->value))
    throw std::invalid_argument("ROI style must be an object");
  return std::get<MetadataObject>((*tree)->value);
}
std::uint64_t integer(const MetadataValue &value, const char *what) {
  if (const auto i = std::get_if<std::int64_t>(&value)) {
    if (*i >= 0)
      return static_cast<std::uint64_t>(*i);
  } else if (const auto i = std::get_if<std::uint64_t>(&value))
    return *i;
  throw std::invalid_argument(what);
}
std::optional<RoiColor> rgba(const MetadataValue &value) {
  if (std::holds_alternative<std::monostate>(value))
    return {};
  const auto tree = std::get_if<std::shared_ptr<const MetadataTree>>(&value);
  if (!tree || !*tree || !std::holds_alternative<MetadataArray>((*tree)->value))
    throw std::invalid_argument(
        "ROI style color needs four RGBA bytes or null");
  const auto &array = std::get<MetadataArray>((*tree)->value);
  if (array.size() != 4)
    throw std::invalid_argument("ROI style color needs four RGBA bytes");
  RoiColor result{};
  for (std::size_t i = 0; i < 4; ++i) {
    const auto byte =
        integer(array[i], "ROI color components must be integers");
    if (byte > 255)
      throw std::invalid_argument("ROI color components exceed 255");
    result[i] = static_cast<unsigned char>(byte);
  }
  return result;
}
MetadataValue canonical_color(const MetadataValue &value) {
  const auto color = std::holds_alternative<std::string>(value)
                         ? parse_roi_color(std::get<std::string>(value))
                         : rgba(value);
  if (!color)
    return {};
  MetadataArray values;
  for (auto byte : *color)
    values.emplace_back(std::int64_t(byte));
  return metadata_array(std::move(values));
}
} // namespace
std::optional<RoiColor> parse_roi_color(const std::string &value) {
  std::string name;
  for (unsigned char c : value)
    if (!std::isspace(c))
      name.push_back(static_cast<char>(std::tolower(c)));
  if (name == "none" || name == "null")
    return {};
  static const std::map<std::string, RoiColor> names{
      {"red", {255, 0, 0, 255}},           {"green", {0, 255, 0, 255}},
      {"blue", {0, 0, 255, 255}},          {"magenta", {255, 0, 255, 255}},
      {"cyan", {0, 255, 255, 255}},        {"yellow", {255, 255, 0, 255}},
      {"orange", {255, 200, 0, 255}},      {"black", {0, 0, 0, 255}},
      {"white", {255, 255, 255, 255}},     {"gray", {128, 128, 128, 255}},
      {"grey", {128, 128, 128, 255}},      {"lightgray", {192, 192, 192, 255}},
      {"lightgrey", {192, 192, 192, 255}}, {"darkgray", {64, 64, 64, 255}},
      {"darkgrey", {64, 64, 64, 255}},     {"pink", {255, 175, 175, 255}}};
  if (const auto found = names.find(name); found != names.end())
    return found->second;
  if ((name.size() != 7 && name.size() != 9) || name[0] != '#')
    throw std::invalid_argument(
        "ROI color must be an ImageJ name, #RRGGBB, #AARRGGBB or none");
  auto byte = [&](std::size_t pos) {
    unsigned n = 0;
    for (std::size_t i = pos; i < pos + 2; ++i) {
      const char c = name[i];
      if (!std::isxdigit(static_cast<unsigned char>(c)))
        throw std::invalid_argument("Invalid ROI color hex digit");
      n = n * 16 + (c <= '9' ? c - '0' : c - 'a' + 10);
    }
    return static_cast<unsigned char>(n);
  };
  const std::size_t first = name.size() == 9 ? 3 : 1;
  return RoiColor{byte(first), byte(first + 2), byte(first + 4),
                  name.size() == 9 ? byte(1) : static_cast<unsigned char>(255)};
}
std::string roi_color_string(const std::optional<RoiColor> &value) {
  if (!value)
    return "none";
  static constexpr char hex[] = "0123456789abcdef";
  std::string result = "#";
  for (auto i : {3, 0, 1, 2}) {
    const auto byte = (*value)[i];
    result.push_back(hex[byte >> 4]);
    result.push_back(hex[byte & 15]);
  }
  return result;
}
bool selection_is_area(const Selection &selection) {
  const auto &kind = selection.kind;
  return kind == "rounded_rectangle" || kind == "rotated_rounded_rectangle" || kind == "rectangle" || kind == "ellipse" ||
         kind == "rotated_rectangle" || kind == "rotated_ellipse" ||
         kind == "polygon" || kind == "freehand" || kind == "mask" || kind == "region";
}
RoiStyle roi_style(const Selection &selection) {
  RoiStyle result;
  const auto found = selection.attributes.find(roi_style_attribute);
  if (found == selection.attributes.end())
    return result;
  validate_metadata(found->second);
  const auto &record = object(found->second);
  const auto version = record.find("schema_version");
  if (version == record.end() ||
      integer(version->second, "ROI style version must be an integer") != 1)
    throw std::invalid_argument("Unsupported ROI style version");
  result.present = true;
  if (const auto item = record.find("stroke_color"); item != record.end()) {
    result.has_stroke = true;
    result.stroke_color = rgba(item->second);
  }
  if (const auto item = record.find("fill_color"); item != record.end()) {
    result.has_fill = true;
    result.fill_color = rgba(item->second);
  }
  if (const auto item = record.find("stroke_width"); item != record.end()) {
    if (const auto number = std::get_if<double>(&item->second))
      result.stroke_width = *number;
    else if (const auto number = std::get_if<std::int64_t>(&item->second))
      result.stroke_width = static_cast<double>(*number);
    else if (const auto number = std::get_if<std::uint64_t>(&item->second))
      result.stroke_width = static_cast<double>(*number);
    else
      throw std::invalid_argument("ROI stroke width must be numeric");
    if (!std::isfinite(*result.stroke_width) || *result.stroke_width < 0)
      throw std::invalid_argument(
          "ROI stroke width must be finite and nonnegative");
  }
  return result;
}
Selection with_roi_style(const Selection &selection,
                         const MetadataObject &patch) {
  roi_style(selection);
  validate_metadata(patch);
  auto result = selection;
  MetadataObject record;
  if (const auto item = selection.attributes.find(roi_style_attribute);
      item != selection.attributes.end())
    record = object(item->second);
  record["schema_version"] = std::int64_t(1);
  for (const auto &item : patch)
    if (item.first != "stroke_color" && item.first != "fill_color")
      record[item.first] = item.second;
  if (const auto item = patch.find("fill_color"); item != patch.end()) {
    record["fill_color"] = canonical_color(item->second);
    if (selection_is_area(selection) &&
        !std::holds_alternative<std::monostate>(record["fill_color"]))
      record["stroke_color"] = std::monostate{};
  }
  if (const auto item = patch.find("stroke_color"); item != patch.end())
    record["stroke_color"] = canonical_color(item->second);
  result.attributes[roi_style_attribute] = metadata_object(std::move(record));
  roi_style(result);
  return result;
}
ResolvedRoiStyle resolve_roi_style(const Selection &selection,
                                   RoiColor fallback) {
  const auto style = roi_style(selection);
  ResolvedRoiStyle result;
  result.styled = style.present;
  result.stroke_width = style.stroke_width.value_or(selection.width);
  result.cosmetic = style.stroke_width && *style.stroke_width == 0;
  if (selection_is_area(selection))
    result.fill_color = style.fill_color;
  if (style.has_stroke && style.stroke_color)
    result.stroke_color = style.stroke_color;
  else if (!result.fill_color) {
    if (!style.has_stroke) {
      const auto legacy = selection.attributes.find("color");
      if (legacy != selection.attributes.end() &&
          std::holds_alternative<std::string>(legacy->second))
        fallback = parse_color(std::get<std::string>(legacy->second));
    }
    result.stroke_color = fallback;
  }
  return result;
}
double selection_processing_width(const Selection &selection) {
  const auto style = roi_style(selection);
  if (selection.kind == "line" || selection.kind == "polyline" ||
      selection.kind == "angle")
    return style.stroke_width
               ? (*style.stroke_width == 0 ? 1 : *style.stroke_width)
               : selection.width;
  return selection.width;
}
Image roi_stroke_mask(const Selection &selection, int width, int height) {
  auto mask = selection.mask(width, height);
  if (!selection_is_area(selection))
    return mask;
  auto edge = binary_outline(mask);
  const auto style = roi_style(selection);
  const double thickness =
      style.present ? resolve_roi_style(selection).stroke_width : 1;
  if (thickness <= 1 || std::none_of(edge.pixels.begin(), edge.pixels.end(),
                                     [](double v) { return v != 0; }))
    return edge;
  // Bound comparisons in floating point; never cast user widths to an int.
  const double radius =
      std::min(thickness / 2, std::hypot(double(width), double(height)));
  auto distance = distance_transform(point(edge, "invert"));
  for (std::size_t i = 0; i < edge.pixels.size(); ++i)
    edge.pixels[i] = distance.pixels[i] <= radius ? 255 : 0;
  return edge;
}
Image render_roi_style(const Image &rgb, const Selection &selection,
                       RoiColor fallback) {
  if (rgb.channels != 3 || rgb.type != PixelType::UInt8)
    throw std::invalid_argument(
        "ROI rendering needs a uint8 RGB display image");
  auto result = rgb;
  const auto style = resolve_roi_style(selection, fallback);
  auto paint = [&](const Image &mask, const RoiColor &color) {
    const double alpha = color[3] / 255.;
    for (std::size_t i = 0; i < mask.pixels.size(); ++i)
      if (mask.pixels[i])
        for (int c = 0; c < 3; ++c)
          result.pixels[i * 3 + c] = std::floor(
              result.pixels[i * 3 + c] * (1 - alpha) + color[c] * alpha + .5);
  };
  if (style.fill_color)
    paint(selection.mask(rgb.width, rgb.height), *style.fill_color);
  if (style.stroke_color)
    paint(roi_stroke_mask(selection, rgb.width, rgb.height),
          *style.stroke_color);
  return result;
}
} // namespace imagejxx
