#include "axis_command_record.hpp"
#include "display_annotation_internal.hpp"
#include "kernel_control.hpp"
#include "session_metadata.hpp"
#include <cmath>
#include <algorithm>
#include <imagejxx/planar_display_annotations.hpp>
#include <limits>

namespace imagejxx {
namespace {
using namespace detail::axis_command_record;
namespace codec = detail::session_metadata;
constexpr std::size_t max_bytes = 16 * 1024 * 1024, max_text = 1024 * 1024;
const MetadataArray &list_value(const MetadataValue &value) {
  // Python metadata transport represents an empty array as an empty string
  // list.
  if (const auto strings = std::get_if<std::vector<std::string>>(&value);
      strings && strings->empty()) {
    static const MetadataArray empty;
    return empty;
  }
  return array_value(value);
}
void require(bool value, const char *message) {
  if (!value)
    throw std::invalid_argument(message);
}
void add(std::size_t &bytes, std::size_t amount, std::size_t cap) {
  require(bytes <= cap && amount <= cap - bytes,
          "Display annotation metadata byte cap");
  bytes += amount;
}
void check_text(const std::string &value, std::size_t cap,
                bool display = true) {
  require(value.size() <= cap, "Display annotation text cap");
  require(!display || value.find('\0') == std::string::npos,
          "Display annotation text contains NUL");
  // The project JSON codec strictly rejects malformed UTF-8 and surrogates.
  try {
    (void)nlohmann::json(value).dump();
  } catch (const nlohmann::json::exception &) {
    throw std::invalid_argument(
        "Display annotation text requires strict UTF-8");
  }
}
void charge(const MetadataValue &v, std::size_t &bytes, std::size_t &nodes,
            std::size_t cap, unsigned depth) {
  require(++nodes <= 1024 * 1024 && depth <= 80,
          "Display annotation metadata node/depth cap");
  add(bytes, 32, cap);
  if (const auto s = std::get_if<std::string>(&v)) {
    add(bytes, s->size(), cap);
    check_text(*s, cap, false);
  } else if (const auto strings = std::get_if<std::vector<std::string>>(&v)) {
    require(strings->size() <= 1024 * 1024 - nodes,
            "Display annotation string-list cap");
    nodes += strings->size();
    for (const auto &s : *strings) {
      add(bytes, 32, cap);
      add(bytes, s.size(), cap);
      check_text(s, cap, false);
    }
  } else if (const auto tree =
                 std::get_if<std::shared_ptr<const MetadataTree>>(&v)) {
    require(bool(*tree), "Null display annotation metadata");
    if (const auto list = std::get_if<MetadataArray>(&(*tree)->value))
      for (const auto &item : *list)
        charge(item, bytes, nodes, cap, depth + 1);
    else
      for (const auto &[key, item] : std::get<MetadataObject>((*tree)->value)) {
        add(bytes, key.size(), cap);
        check_text(key, cap, false);
        charge(item, bytes, nodes, cap, depth + 1);
      }
  } else if (const auto number = std::get_if<double>(&v))
    require(std::isfinite(*number),
            "Display annotation metadata must be finite");
  kernel_checkpoint();
}
void bounds_ok(const std::array<double, 4> &b) {
  for (double n : b)
    require(std::isfinite(n) && std::abs(n) <= 1e9,
            "Display annotation bounds must be finite and bounded");
  require(b[2] > 0 && b[3] > 0, "Display annotation size must be positive");
}
MetadataValue bounds_record(const std::array<double, 4> &bounds) {
  MetadataArray values;
  for (double n : bounds)
    values.push_back(n);
  return metadata_array(std::move(values));
}
std::array<double, 4> read_bounds(const MetadataValue &value) {
  const auto &a = array_value(value);
  require(a.size() == 4, "Invalid display annotation bounds");
  std::array<double, 4> b;
  for (int i = 0; i < 4; ++i)
    b[i] = number_value(a[i]);
  bounds_ok(b);
  return b;
}
bool boolean(const MetadataValue &value) {
  if (const auto b = std::get_if<bool>(&value))
    return *b;
  throw std::invalid_argument("Display annotation flag requires bool");
}
std::uint32_t color(const MetadataValue &value) {
  auto n = unsigned_value(value);
  require(n <= std::numeric_limits<std::uint32_t>::max(),
          "Display annotation color overflow");
  return std::uint32_t(n);
}
unsigned char small(const MetadataValue &value, unsigned max) {
  auto n = unsigned_value(value);
  require(n <= max, "Display annotation integer out of range");
  return static_cast<unsigned char>(n);
}
MetadataObject payload_record(const DisplayAnnotationPayload &payload) {
  if (const auto p = std::get_if<TextAnnotationPayload>(&payload))
    return {{"kind", std::string("text")},
            {"bounds", bounds_record(p->bounds)},
            {"text", p->text},
            {"font_name", p->font_name},
            {"font_size", p->font_size},
            {"angle", p->angle},
            {"font_style", std::uint64_t(p->font_style)},
            {"justification", std::uint64_t(p->justification)},
            {"draw_string_mode", p->draw_string_mode},
            {"color", std::uint64_t(p->color)},
            {"background", p->background
                               ? MetadataValue(std::uint64_t(*p->background))
                               : MetadataValue{}}};
  const auto &p = std::get<ImageAnnotationPayload>(payload);
  return {{"kind", std::string("image")},
          {"bounds", bounds_record(p.bounds)},
          {"width", p.width},
          {"height", p.height},
          {"channels", p.channels},
          {"pixels", codec::encode_base64(p.pixels)},
          {"opacity", std::uint64_t(p.opacity)},
          {"zero_transparent", p.zero_transparent}};
}
DisplayAnnotationPayload read_payload(const MetadataValue &value) {
  const auto &p = object_value(value);
  const auto kind = text_value(p.at("kind"));
  if (kind == "text") {
    exact_fields(p, {"kind", "bounds", "text", "font_name", "font_size",
                     "angle", "font_style", "justification", "draw_string_mode",
                     "color", "background"});
    TextAnnotationPayload result;
    result.bounds = read_bounds(p.at("bounds"));
    result.text = text_value(p.at("text"));
    result.font_name = text_value(p.at("font_name"));
    result.font_size = number_value(p.at("font_size"));
    result.angle = number_value(p.at("angle"));
    result.font_style = small(p.at("font_style"), 3);
    result.justification = small(p.at("justification"), 2);
    result.draw_string_mode = boolean(p.at("draw_string_mode"));
    result.color = color(p.at("color"));
    if (!std::holds_alternative<std::monostate>(p.at("background")))
      result.background = color(p.at("background"));
    result.validate();
    return result;
  }
  require(kind == "image", "Unknown display annotation payload kind");
  exact_fields(p, {"kind", "bounds", "width", "height", "channels", "pixels",
                   "opacity", "zero_transparent"});
  ImageAnnotationPayload result;
  result.bounds = read_bounds(p.at("bounds"));
  result.width = index_value(p.at("width"), true);
  result.height = index_value(p.at("height"), true);
  result.channels = index_value(p.at("channels"), true);
  require(result.width <= 16384 && result.height <= 16384 &&
              std::size_t(result.width) * result.height <= max_text &&
              (result.channels == 3 || result.channels == 4),
          "Display image dimensions exceed limits");
  const auto size = std::size_t(result.width) * result.height * result.channels;
  const auto &encoded = text_value(p.at("pixels"));
  require(encoded.size() == (size + 2) / 3 * 4,
          "Display image encoded size differs");
  result.pixels = codec::decode_base64(encoded);
  require(result.pixels.size() == size &&
              codec::encode_base64(result.pixels) == encoded,
          "Noncanonical display image pixels");
  result.opacity = small(p.at("opacity"), 255);
  result.zero_transparent = boolean(p.at("zero_transparent"));
  result.validate();
  return result;
}
MetadataValue extent(DatasetShape shape) {
  shape.validate();
  MetadataArray a;
  for (int n : shape.dimensions())
    a.push_back(n);
  return metadata_array(std::move(a));
}
} // namespace
namespace detail {
void preflight_display_annotation_metadata(const MetadataValue &v,
                                           std::size_t cap) {
  std::size_t bytes = 0, nodes = 0;
  charge(v, bytes, nodes, cap, 0);
}
void preflight_display_annotation_record(const MetadataObject &v,
                                         std::size_t cap) {
  std::size_t bytes = 0, nodes = 0;
  for (const auto &[key, item] : v) {
    add(bytes, key.size(), cap);
    check_text(key, cap, false);
    charge(item, bytes, nodes, cap, 0);
  }
}
} // namespace detail
void TextAnnotationPayload::validate() const {
  bounds_ok(bounds);
  check_text(text, max_text);
  check_text(font_name, 4096);
  require(!font_name.empty(), "Display text font name is empty");
  require(std::isfinite(font_size) && font_size > 0 && font_size <= 16384,
          "Display text font size out of range");
  require(std::isfinite(angle) && std::abs(angle) <= 1e9,
          "Display text angle out of range");
  require(font_style <= 3 && justification <= 2,
          "Display text font style/justification out of range");
}
bool TextAnnotationPayload::operator==(const TextAnnotationPayload &v) const {
  return bounds == v.bounds && text == v.text && font_name == v.font_name &&
         font_size == v.font_size && angle == v.angle &&
         font_style == v.font_style && justification == v.justification &&
         draw_string_mode == v.draw_string_mode && color == v.color &&
         background == v.background;
}
void ImageAnnotationPayload::validate() const {
  bounds_ok(bounds);
  require(width > 0 && width <= 16384 && height > 0 && height <= 16384 &&
              (channels == 3 || channels == 4),
          "Display image dimensions/channels out of range");
  require(std::size_t(width) * height <= max_text &&
              pixels.size() == std::size_t(width) * height * channels,
          "Display image pixel count/size differs");
}
bool ImageAnnotationPayload::operator==(const ImageAnnotationPayload &v) const {
  return bounds == v.bounds && width == v.width && height == v.height &&
         channels == v.channels && pixels == v.pixels && opacity == v.opacity &&
         zero_transparent == v.zero_transparent;
}
bool PlanarDisplayAnnotation::has_transform() const {
  return transform!=std::array<double,6>{1,0,0,0,1,0};
}
void PlanarDisplayAnnotation::validate_transform() const {
  for(const auto n:transform)require(std::isfinite(n) && std::abs(n)<=1e9,
      "Display annotation transform must be finite and bounded");
  const auto determinant=transform[0]*transform[4]-transform[1]*transform[3];
  require(std::isfinite(determinant) && determinant!=0,
      "Display annotation transform must be invertible");
}
std::array<double,2> PlanarDisplayAnnotation::map_point(std::array<double,2> point) const {
  validate_transform();for(auto n:point)require(std::isfinite(n),"Display annotation point must be finite");
  const auto &a=transform;
  std::array<double,2> result{a[0]*point[0]+a[1]*point[1]+a[2],a[3]*point[0]+a[4]*point[1]+a[5]};
  for(auto n:result)require(std::isfinite(n),"Mapped display annotation point overflow");
  return result;
}
std::array<double,2> PlanarDisplayAnnotation::unmap_point(std::array<double,2> point) const {
  validate_transform();for(auto n:point)require(std::isfinite(n),"Display annotation point must be finite");
  const auto &a=transform;const auto x=point[0]-a[2],y=point[1]-a[5];
  const auto determinant=a[0]*a[4]-a[1]*a[3];
  std::array<double,2> result{(a[4]*x-a[1]*y)/determinant,(-a[3]*x+a[0]*y)/determinant};
  for(auto n:result)require(std::isfinite(n),"Inverse display annotation point overflow");
  return result;
}
void PlanarDisplayAnnotation::validate(DatasetShape shape) const {
  validate_transform();
  const auto axes = planar_annotation_geometry(shape, plane);
  const std::array<int, 3> dimensions{shape.x, shape.y, shape.z};
  require(coordinate >= 0 && coordinate < dimensions[axes.fixed_axis] &&
              (!t || (*t >= 0 && *t < shape.t)) &&
              (!c || (*c >= 0 && *c < shape.c)),
          "Display annotation plane/T/C outside source");
  check_text(name, 4096);
  std::visit([](const auto &p) { p.validate(); }, payload);
  detail::preflight_display_annotation_record(attributes, max_bytes);
}
bool PlanarDisplayAnnotation::operator==(
    const PlanarDisplayAnnotation &v) const {
  return name == v.name && payload == v.payload && plane == v.plane &&
         coordinate == v.coordinate && t == v.t && c == v.c &&
         metadata_equal(attributes, v.attributes) && transform==v.transform;
}
bool planar_display_annotation_visible(const PlanarDisplayAnnotation &row,
                                       Plane plane, int coordinate, int t,
                                       int c) {
  return row.plane == plane && row.coordinate == coordinate &&
         (!row.t || *row.t == t) && (!row.c || *row.c == c);
}
MetadataValue encode_planar_display_annotations(
    DatasetShape shape, const std::vector<PlanarDisplayAnnotation> &rows) {
  require(rows.size() <= 4096, "Display annotation row cap");
  std::size_t bytes = 1024, nodes = 0;
  // Reserve conservative container/field overhead before allocating
  // payload/base64 copies.
  for (const auto &row : rows) {
    row.validate(shape);
    add(bytes, 2048 + row.name.size(), max_bytes);
    if (const auto p = std::get_if<TextAnnotationPayload>(&row.payload))
      add(bytes, p->text.size() + p->font_name.size(), max_bytes);
    else {
      const auto n =
          std::get<ImageAnnotationPayload>(row.payload).pixels.size();
      add(bytes, (n + 2) / 3 * 4, max_bytes);
    }
    for (const auto &[key, item] : row.attributes) {
      add(bytes, key.size(), max_bytes);
      charge(item, bytes, nodes, max_bytes, 0);
    }
  }
  const bool affine=std::any_of(rows.begin(),rows.end(),[](const auto &row){return row.has_transform();});
  MetadataArray list;
  list.reserve(rows.size());
  for (const auto &row : rows) {
    kernel_checkpoint();
    MetadataObject encoded
        {{"name", row.name},
         {"payload", metadata_object(payload_record(row.payload))},
         {"plane", std::string(plane_name(row.plane))},
         {"coordinate", row.coordinate},
         {"t", row.t ? MetadataValue(*row.t) : MetadataValue{}},
         {"c", row.c ? MetadataValue(*row.c) : MetadataValue{}},
         {"attributes", metadata_object(row.attributes)}};
    if(affine) {
      MetadataArray matrix;for(auto n:row.transform)matrix.emplace_back(n);
      encoded["transform"]=metadata_array(std::move(matrix));
    }
    list.push_back(metadata_object(std::move(encoded)));
  }
  auto result = metadata_object(
      {{"schema", std::string("imagejxx.planar-display-annotations")},
       {"version", affine?2:1},
       {"extent", extent(shape)},
       {"rows", metadata_array(std::move(list))}});
  detail::preflight_display_annotation_metadata(result);
  return result;
}
std::vector<PlanarDisplayAnnotation>
decode_planar_display_annotations(DatasetShape shape,
                                  const MetadataValue &value) {
  detail::preflight_display_annotation_metadata(value);
  const auto &record = object_value(value);
  exact_fields(record, {"schema", "version", "extent", "rows"});
  const auto version=unsigned_value(record.at("version"));
  require(text_value(record.at("schema")) ==
                  "imagejxx.planar-display-annotations" &&
              (version==1 || version==2),
          "Unsupported display annotation schema");
  const auto &dimensions = array_value(record.at("extent"));
  const auto expected = shape.dimensions();
  require(dimensions.size() == expected.size(),
          "Invalid display annotation extent");
  for (std::size_t i = 0; i < expected.size(); ++i)
    require(index_value(dimensions[i], true) == expected[i],
            "Display annotations require explicit extent remapping");
  const auto &list = list_value(record.at("rows"));
  require(list.size() <= 4096, "Display annotation row cap");
  std::vector<PlanarDisplayAnnotation> result;
  result.reserve(list.size());
  for (const auto &item : list) {
    kernel_checkpoint();
    const auto &row = object_value(item);
    if(version==1)exact_fields(row, {"name", "payload", "plane", "coordinate", "t", "c", "attributes"});
    else exact_fields(row, {"name", "payload", "plane", "coordinate", "t", "c", "attributes", "transform"});
    auto optional_axis = [](const MetadataValue &v) -> std::optional<int> {
      return std::holds_alternative<std::monostate>(v) ? std::optional<int>{}
                                                       : index_value(v);
    };
    PlanarDisplayAnnotation p{
        text_value(row.at("name")),        read_payload(row.at("payload")),
        read_plane(row.at("plane")),       index_value(row.at("coordinate")),
        optional_axis(row.at("t")),        optional_axis(row.at("c")),
        object_value(row.at("attributes"))};
    if(version==2) {
      const auto &matrix=array_value(row.at("transform"));require(matrix.size()==6,"Display transform requires six numbers");
      for(std::size_t i=0;i<6;++i)p.transform[i]=number_value(matrix[i]);
    }
    p.validate(shape);
    result.push_back(std::move(p));
  }
  require((version==2)==std::any_of(result.begin(),result.end(),[](const auto &row){return row.has_transform();}),
      "Display annotation version must match affine geometry");
  // Apply the same conservative retained-input budget to fresh and decoded
  // sets.
  (void)encode_planar_display_annotations(shape, result);
  return result;
}
std::vector<PlanarDisplayAnnotation>
planar_display_annotations(const DocumentData &data) {
  const auto it =
      data.metadata.extensions.find(planar_display_annotations_extension);
  return it == data.metadata.extensions.end()
             ? std::vector<PlanarDisplayAnnotation>{}
             : decode_planar_display_annotations(data.dataset.shape(),
                                                 it->second);
}
void validate_planar_display_annotations(const DocumentData &data) {
  (void)planar_display_annotations(data);
}
DocumentTransaction
prepare_store_planar_display_annotation(const DocumentSnapshot &source,
                                        const PlanarDisplayAnnotation &row) {
  row.validate(source.data().dataset.shape());
  auto rows = planar_display_annotations(source.data());
  require(rows.size() < 4096, "Display annotation row cap");
  rows.push_back(row);
  auto encoded =
      encode_planar_display_annotations(source.data().dataset.shape(), rows);
  auto edit = source.begin_edit(Change::Pixels | Change::Metadata);
  edit.set_extension(planar_display_annotations_extension, std::move(encoded));
  return edit;
}
DocumentTransaction
prepare_update_planar_display_annotation(const DocumentSnapshot &source,
                                         std::size_t index,
                                         const PlanarDisplayAnnotation &row) {
  row.validate(source.data().dataset.shape());
  auto rows = planar_display_annotations(source.data());
  if (index >= rows.size())
    throw std::out_of_range("Select a display annotation");
  auto edit = source.begin_edit(Change::Pixels | Change::Metadata);
  if (!(rows[index] == row)) {
    rows[index] = row;
    edit.set_extension(
        planar_display_annotations_extension,
        encode_planar_display_annotations(source.data().dataset.shape(), rows));
  }
  return edit;
}
DocumentTransaction
prepare_delete_planar_display_annotation(const DocumentSnapshot &source,
                                         std::size_t index) {
  auto rows = planar_display_annotations(source.data());
  if (index >= rows.size())
    throw std::out_of_range("Select a display annotation");
  rows.erase(rows.begin() + index);
  auto edit = source.begin_edit(Change::Pixels | Change::Metadata);
  if (rows.empty())
    edit.remove_extension(planar_display_annotations_extension);
  else
    edit.set_extension(
        planar_display_annotations_extension,
        encode_planar_display_annotations(source.data().dataset.shape(), rows));
  return edit;
}
} // namespace imagejxx
