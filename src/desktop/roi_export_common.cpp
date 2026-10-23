#include "roi_export_common.hpp"
#include "../native/kernel_control.hpp"
#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <imagejxx/roi_style.hpp>
namespace imagejxx::desktop::roi_export_detail {
std::string export_error(const std::exception &error) {
  if (const auto file =
          dynamic_cast<const std::filesystem::filesystem_error *>(&error)) {
    auto text = "ROI file operation failed (code " +
                std::to_string(file->code().value()) +
                "): " + file->path1().u8string();
    if (!file->path2().empty())
      text += " -> " + file->path2().u8string();
    return text;
  }
  return error.what();
}
bool same_source(const DocumentSnapshot &source,
                 const DocumentSnapshot &current) {
  return source.same_document(current) && source.shares_data(current) &&
         source.revisions().matches(current.revisions(),
                                    Change::Pixels | Change::Metadata);
}
std::filesystem::path destination(const std::string &text, bool zip) {
  namespace fs = std::filesystem;
  if (text.empty() || text.find('\0') != std::string::npos)
    throw std::invalid_argument("Choose an output ROI or ZIP file");
  auto path = fs::absolute(fs::u8path(text));
  auto extension = path.extension().u8string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  if (extension != (zip ? ".zip" : ".roi") ||
      !fs::is_directory(path.parent_path()))
    throw std::invalid_argument(
        "Choose the expected ROI/ZIP extension in an existing folder");
  if (fs::exists(path) && !fs::is_regular_file(path))
    throw std::invalid_argument("Output must be a regular file");
  return path;
}
ImageJRoi fill_geometric_projection(ImageJRoi out, const PlanarAnnotation &row,
                                    bool reset) {
  if (reset) {
    out.source_type.reset();
    out.source_subtype = 0;
    out.ignored_rotated_parameters.reset();
    out.composite_path.clear();
    out.rounded_rectangle.reset();
    out.arrow.reset();
    out.spline.reset();
    out.point_counters.clear();
    out.point_type = out.point_size = 0;
    out.options = 8192;
  }
  auto selection = row.selection;
  for (auto it = selection.attributes.begin();
       it != selection.attributes.end();)
    if (it->first != roi_style_attribute)
      it = selection.attributes.erase(it);
    else
      ++it;
  out.roi = {row.name, std::move(selection), row.t, row.c, row.coordinate, {}};
  out.legacy_position = 0;
  return out;
}
namespace {
template <class T>
T template_value(std::optional<ImageJAnnotationProjection> value, bool reset) {
  if (!value)
    return T{};
  if (auto same = std::get_if<T>(&*value))
    return std::move(*same);
  if (!reset)
    throw std::invalid_argument(
        "Metadata template has a different representation; explicitly reset it "
        "or choose fresh metadata");
  T out;
  std::visit(
      [&](const auto &source) {
        out.group = source.group;
        out.properties = source.properties;
        out.overlay_label_color = source.overlay_label_color;
        out.overlay_font_size = source.overlay_font_size;
      },
      *value);
  return out;
}
} // namespace
ImageJAnnotationProjection
display_projection(const PlanarDisplayAnnotation &row,
                   std::optional<ImageJAnnotationProjection> source, bool reset,
                   bool regenerate,
                   const ImageJDisplayProjectionOptions &options) {
  if(row.has_transform())throw std::invalid_argument("Affine display annotations require explicit flattening before ImageJ export");
  if (const auto text = std::get_if<TextAnnotationPayload>(&row.payload)) {
    if (!options.round_font_size &&
        text->font_size != std::round(text->font_size))
      throw std::invalid_argument(
          "Confirm standard font-size rounding; the native size stays exact");
    const auto size = std::round(text->font_size);
    if (size < 1 || size > 16384)
      throw std::invalid_argument("Rounded standard font size must be1..16384");
    auto out = template_value<ImageJTextRoi>(std::move(source), reset);
    if (reset)
      out.options = 8192;
    out.name = row.name;
    out.bounds = text->bounds;
    out.text = text->text;
    out.font_name = text->font_name;
    out.font_size = int(size);
    out.angle = text->angle;
    out.font_style = text->font_style;
    out.justification = text->justification;
    out.draw_string_mode = text->draw_string_mode;
    out.stroke_color = text->color;
    out.fill_color = text->background.value_or(0);
    out.t = row.t;
    out.c = row.c;
    out.z = row.coordinate;
    out.legacy_position = 0;
    return out;
  }
  const auto &image = std::get<ImageAnnotationPayload>(row.payload);
  if (image.channels == 4 && !options.drop_image_alpha)
    throw std::invalid_argument("Confirm alpha omission in the standard image; "
                                "native RGBA stays exact");
  if ((image.bounds[2] != image.width || image.bounds[3] != image.height) &&
      !options.reset_image_scale)
    throw std::invalid_argument(
        "Confirm unscaled standard image bounds; native bounds stay exact");
  std::array<int, 2> origin{};
  for (int i = 0; i < 2; ++i) {
    const auto n = std::round(image.bounds[i]);
    if (!options.round_image_origin && n != image.bounds[i])
      throw std::invalid_argument(
          "Confirm standard image-origin rounding; native bounds stay exact");
    if (n < INT_MIN || n > INT_MAX)
      throw std::invalid_argument("Standard image origin out of range");
    origin[i] = int(n);
  }
  auto out = template_value<ImageJImageRoi>(std::move(source), reset);
  if (reset)
    out.options = 8192;
  if (regenerate)
    out.serialized_image.clear();
  out.pixels = Image(image.width, image.height, 3, PixelType::UInt8);
  for (std::size_t i = 0; i < std::size_t(image.width) * image.height; ++i) {
    kernel_checkpoint_periodic();
    for (int c = 0; c < 3; ++c)
      out.pixels.pixels[i * 3 + c] = image.pixels[i * image.channels + c];
  }
  out.name = row.name;
  out.origin = origin;
  out.opacity = image.opacity;
  out.zero_transparent = image.zero_transparent;
  out.t = row.t;
  out.c = row.c;
  out.z = row.coordinate;
  out.legacy_position = 0;
  return out;
}
} // namespace imagejxx::desktop::roi_export_detail
