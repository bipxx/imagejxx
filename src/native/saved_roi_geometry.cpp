#include <imagejxx/saved_roi_geometry.hpp>
#include <imagejxx/selection_edit.hpp>
#include <imagejxx/composite_path.hpp>

#include "kernel_control.hpp"
#include "saved_roi_geometry_internal.hpp"
#include "selection_codec.hpp"
#include "session_metadata.hpp"
#include "regional_geometry.hpp"
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <utility>

namespace imagejxx {
struct ResolvedSavedRoiGeometryCommand::Data {
  DocumentSnapshot source;
  std::size_t row_index;
  Roi original;
  Selection replacement;
  PlanePosition position;
  MetadataObject record;
  SavedRoiGeometryEstimate estimate;
  SavedRoiGeometryLimits limits;
  bool replay;
};

namespace {
constexpr const char *schema = "imagejxx.saved-roi-geometry-command";
constexpr const char *operation = "replace_saved_roi_geometry";
constexpr std::size_t node_bytes = sizeof(MetadataObject::value_type) +
    sizeof(MetadataTree) + 16 * sizeof(void *);
// The second 16KiB covers opaque controller/capture/prepared/focus holders;
// Application statically verifies its corresponding holder sizes fit this.
constexpr std::size_t fixed_bytes = 16384 + sizeof(DocumentSnapshot) * 4 +
    sizeof(DocumentTransaction) * 2 + sizeof(SavedRoiGeometryRequest) * 2 +
    sizeof(SavedRoiGeometryLimits) * 3 + sizeof(ExecutionControl) * 2 +
    sizeof(ResolvedSavedRoiGeometryCommand) * 4 + sizeof(Roi) * 3 +
    sizeof(Selection) * 4 + sizeof(MetadataObject) * 4;

[[noreturn]] void invalid(const char *message) {
  throw std::invalid_argument(message);
}
std::size_t add(std::size_t a, std::size_t b) {
  if (b > std::numeric_limits<std::size_t>::max() - a)
    invalid("Saved ROI geometry size overflow");
  return a + b;
}
std::size_t mul(std::size_t a, std::size_t b) {
  if (a && b > std::numeric_limits<std::size_t>::max() / a)
    invalid("Saved ROI geometry size overflow");
  return a * b;
}
void within(std::size_t value, std::size_t bound) {
  if (value > bound) invalid("Saved ROI geometry resource limit exceeded");
}
void limits_valid(const SavedRoiGeometryLimits &limits) {
  if (!limits.max_xy_pixels || !limits.max_vertices ||
      !limits.max_working_bytes || !limits.max_record_bytes ||
      !limits.metadata_depth || limits.metadata_depth > 128)
    invalid("Saved ROI geometry limits must be positive; metadata depth <=128");
}
std::size_t xy_size(DatasetShape shape, const SavedRoiGeometryLimits &limits) {
  shape.validate();
  const auto count = mul(std::size_t(shape.x), std::size_t(shape.y));
  within(count, limits.max_xy_pixels);
  return count;
}
std::size_t base64_size(std::size_t bytes) { return mul(add(bytes, 2) / 3, 4); }
std::size_t string_wire(std::size_t bytes) { return add(2, mul(6, bytes)); }

// Borrowed, occurrence-based traversal. Cardinality is charged before descent;
// depth, finite scalars and every sum/product are checked before JSON/copies.
// No callbacks or source plane/region/strip acquisition occur in preflight.
struct Size {
  std::size_t native = 0, wire = 0, nodes = 0;
};
Size sum(Size a, Size b) {
  return {add(a.native, b.native), add(a.wire, b.wire), add(a.nodes, b.nodes)};
}
class MetadataSize {
public:
  explicit MetadataSize(const SavedRoiGeometryLimits &limits) : limits_(limits) {}
  Size object(const MetadataObject &object, std::size_t depth = 0) {
    container(depth);
    Size result{add(sizeof(MetadataObject), mul(object.size(), node_bytes)),
                add(2, mul(object.size(), 2)), add(1, object.size())};
    check(result);
    for (const auto &[key, value] : object) {
      result = sum(result, {key.size(), string_wire(key.size()), 0});
      result = sum(result, entry(value, depth + 1));
      check(result);
    }
    return result;
  }
private:
  const SavedRoiGeometryLimits &limits_;
  void container(std::size_t depth) {
    if (depth >= limits_.metadata_depth) invalid("Saved ROI metadata nesting limit exceeded");
  }
  void check(Size size) {
    within(size.native, limits_.max_working_bytes);
    within(size.wire, limits_.max_working_bytes);
  }
  Size entry(const MetadataValue &value, std::size_t depth) {
    Size result{sizeof(MetadataValue), 32, 1};
    if (const auto number = std::get_if<double>(&value)) {
      if (!std::isfinite(*number)) invalid("Saved ROI metadata must be finite");
    } else if (const auto text = std::get_if<std::string>(&value)) {
      result.native = add(result.native, text->size());
      result.wire = string_wire(text->size());
    } else if (const auto strings = std::get_if<std::vector<std::string>>(&value)) {
      container(depth);
      result.native = add(result.native, mul(strings->size(), sizeof(std::string)));
      result.wire = add(2, strings->size());
      result.nodes = add(1, strings->size());
      check(result);
      for (const auto &text : *strings) {
        result.native = add(result.native, text.size());
        result.wire = add(result.wire, string_wire(text.size()));
        check(result);
      }
    } else if (const auto tree = std::get_if<std::shared_ptr<const MetadataTree>>(&value)) {
      if (!*tree) invalid("Saved ROI metadata has a null container");
      container(depth);
      result.native = add(result.native, sizeof(MetadataTree));
      if (const auto object = std::get_if<MetadataObject>(&(*tree)->value)) {
        result = sum(result, this->object(*object, depth));
      } else {
        const auto &array = std::get<MetadataArray>((*tree)->value);
        result.native = add(result.native, add(sizeof(MetadataArray),
            mul(array.size(), sizeof(MetadataValue))));
        result.wire = add(2, array.size());
        check(result);
        for (const auto &item : array) {
          result = sum(result, entry(item, depth + 1));
          check(result);
        }
      }
    }
    check(result);
    return result;
  }
};

bool same_geometry_style(const Selection &a, const Selection &b) {
  if (a.width != b.width) return false;
  const bool rounded = a.is_rounded_box() && b.is_rounded_box();
  const auto count = [&](const Selection &v) {
    return v.attributes.size() - (rounded && v.attributes.count("corner_diameter") ? 1 : 0);
  };
  if (count(a) != count(b)) return false;
  for (const auto &[key, value] : a.attributes) {
    if (rounded && key == "corner_diameter") continue;
    const auto found = b.attributes.find(key);
    if (found == b.attributes.end() || !metadata_equal(value, found->second)) return false;
  }
  return true;
}
bool area_kind(const std::string &kind) {
  return kind == "rounded_rectangle" || kind == "rotated_rounded_rectangle" || kind == "rectangle" || kind == "ellipse" ||
      kind == "rotated_rectangle" || kind == "rotated_ellipse" ||
      kind == "polygon" || kind == "freehand" || kind == "mask" || kind == "region" || kind == "path";
}
std::size_t geometry_vertices(const Selection &selection) {
  return selection.is_path() ?
      (selection.path_commands().size()-selection.path_geometry().segment_count())/2 : selection.points.size();
}
Size selection_size(const Selection &selection, DatasetShape shape,
                    const SavedRoiGeometryLimits &limits, bool require_area,
                    bool validate_geometry = true) {

  if (require_area && !area_kind(selection.kind))
    invalid("Saved ROI geometry requires an area selection");
  within(geometry_vertices(selection), limits.max_vertices);
  const auto point_bytes = mul(selection.points.size(), sizeof(std::array<double, 2>));
  within(point_bytes, limits.max_working_bytes);
  auto attributes = MetadataSize(limits).object(selection.attributes);
  const auto packed = selection.packed_mask().size();
  within(packed, limits.max_working_bytes);
  if(selection.is_region()) {
    detail::validate_region_storage(selection);
    within(mul(std::size_t(selection.mask_width()),std::size_t(selection.mask_height())),limits.max_xy_pixels);
  }
  if (selection.kind == "mask" && validate_geometry) {
    const auto xy = xy_size(shape, limits);
    if (selection.mask_width() != shape.x || selection.mask_height() != shape.y ||
        packed != add(xy, 7) / 8)
      invalid("Saved ROI mask dimensions or packed length differ");
    if (xy % 8 && (selection.packed_mask().back() & ((1u << (8 - xy % 8)) - 1u)))
      invalid("Saved ROI mask has nonzero unused tail bits");
  }
  // Existing native validity/style/geometry rules, without rasterization.
  if (validate_geometry) selection.validate(shape.x, shape.y);
  Size result{add(sizeof(Selection), add(selection.kind.size(), add(point_bytes, packed))),
      add(256, add(string_wire(selection.kind.size()),
                   mul(selection.points.size(), 96))),
      add(16, mul(selection.points.size(), 3))};
  if(selection.is_path()) {
    const auto count=selection.path_commands().size();
    result.native=add(result.native,add(sizeof(ImageJCompositePath)+sizeof(std::vector<double>),mul(count,sizeof(double))));
    result.wire=add(result.wire,add(32,mul(count,32)));
    result.nodes=add(result.nodes,add(2,count));
  }
  if(selection.is_region()) {
    result.wire=add(result.wire,128);
    result.nodes=add(result.nodes,16);
  }
  if (selection.is_mask()) {
    result.wire = add(result.wire, add(2, base64_size(packed)));
    result.native = add(result.native, packed);
  }
  return sum(result, attributes);
}
Size roi_size(const Roi &roi, DatasetShape shape,
              const SavedRoiGeometryLimits &limits, bool require_area = false) {
  auto result = selection_size(roi.selection, shape, limits, require_area);
  result = sum(result, MetadataSize(limits).object(roi.attributes));
  roi.validate(shape);
  return sum(result, {add(sizeof(Roi), roi.name.size()),
                     add(256, string_wire(roi.name.size())), 16});
}
PlanePosition current_position(const DocumentSnapshot &source) {
  return {source.display().t, source.display().c, source.display().crosshair[2]};
}
void position_valid(PlanePosition p, DatasetShape shape) {
  if (p.t < 0 || p.c < 0 || p.z < 0 || p.t >= shape.t || p.c >= shape.c || p.z >= shape.z)
    invalid("Saved ROI geometry position is outside source");
}
bool visible(const Roi &roi, PlanePosition p) {
  return (!roi.t || *roi.t == p.t) && (!roi.c || *roi.c == p.c) &&
         (!roi.z || *roi.z == p.z);
}
const char *type_name(PixelType type) {
  switch (type) {
  case PixelType::UInt8: return "uint8";
  case PixelType::UInt16: return "uint16";
  case PixelType::Float32: return "float32";
  case PixelType::Float64: return "float64";
  }
  invalid("Unknown saved ROI source type");
}
const char *policy_name(FloatingSamplePolicy policy) {
  switch (policy) {
  case FloatingSamplePolicy::FiniteOnly: return "finite_only";
  case FloatingSamplePolicy::PreserveQuietNaNFloat32: return "preserve_quiet_nan_float32";
  case FloatingSamplePolicy::PreserveIEEEFloat32: return "preserve_ieee_float32";
  }
  invalid("Unknown saved ROI sample policy");
}
void validate_policy(PixelType type, FloatingSamplePolicy policy, DatasetShape shape) {
  (void)type_name(type);
  (void)policy_name(policy);
  if ((policy == FloatingSamplePolicy::PreserveQuietNaNFloat32 ||
       policy == FloatingSamplePolicy::PreserveIEEEFloat32) &&
      (type != PixelType::Float32 || shape.s != 1))
    invalid(policy == FloatingSamplePolicy::PreserveIEEEFloat32
        ? "Invalid IEEE saved ROI source policy" : "Invalid quiet-NaN saved ROI source policy");
}
void finish_estimate(SavedRoiGeometryEstimate &e, bool mask,
                     const SavedRoiGeometryLimits &limits) {
  within(e.record_wire_bytes, limits.max_record_bytes);
  e.fixed_bytes = fixed_bytes;
  auto total = add(16384, e.fixed_bytes);
  total = add(total, mul(4, e.source_data_bytes));
  total = add(total, mul(4, e.roi_bytes));
  total = add(total, mul(6, e.original_selection_bytes));
  total = add(total, mul(6, e.replacement_selection_bytes));
  total = add(total, mul(3, e.record_metadata_bytes));
  total = add(total, mul(3, e.record_wire_bytes));
  total = add(total, mul(mul(2, add(e.vertices, 16)), sizeof(SelectionHandle)));
  if (mask) total = add(total, mul(mul(2, e.xy_pixels), sizeof(double)));
  e.working_bytes = total;
  within(total, limits.max_working_bytes);
}

SavedRoiGeometryEstimate layout(const DocumentSnapshot &source,
    std::size_t row_index, const Selection &replacement, const SavedRoiGeometryLimits &limits,
    bool require_visibility) {
  limits_valid(limits);
  const auto &data = source.data();
  const auto shape = data.dataset.shape();
  SavedRoiGeometryEstimate e;
  e.xy_pixels = xy_size(shape, limits);
  validate_policy(data.dataset.pixel_type(), data.dataset.sample_policy(), shape);
  if (row_index >= data.rois.size()) invalid("Saved ROI row index is outside source");
  // Admit complete row storage before visiting/copying any collection.
  e.roi_bytes = add(sizeof(std::vector<Roi>), mul(data.rois.size(), sizeof(Roi)));
  within(mul(4, e.roi_bytes), limits.max_working_bytes);
  const auto &original = data.rois[row_index];
  const auto original_size = selection_size(original.selection, shape, limits, true);
  const auto replacement_size = selection_size(replacement, shape, limits, true);
  if (!same_geometry_style(original.selection, replacement))
    invalid("Saved ROI geometry cannot change selection width or attributes");
  // Reserve the normalized record's structural nesting in admission, so a
  // too-deep preserved attribute cannot fail only after the first callback.
  (void)MetadataSize(limits).object(original.selection.attributes, 4);
  (void)MetadataSize(limits).object(original.attributes, 3);
  (void)MetadataSize(limits).object(replacement.attributes, 2);
  const auto position = current_position(source);
  position_valid(position, shape);
  if (require_visibility && !visible(original, position))
    invalid("Saved ROI row is not visible at captured position");
  for (const auto &roi : data.rois) {
    e.roi_bytes = add(e.roi_bytes, roi_size(roi, shape, limits).native);
    within(mul(4, e.roi_bytes), limits.max_working_bytes);
  }
  e.source_data_bytes = sizeof(DocumentData) + sizeof(DisplayState) + sizeof(SpatialCalibration);
  e.source_data_bytes = add(e.source_data_bytes, data.metadata.title.size());
  for (const auto *object : {&data.metadata.properties, &data.metadata.extensions,
                             &data.metadata.record_layout, &source.display().properties})
    e.source_data_bytes = add(e.source_data_bytes, MetadataSize(limits).object(*object).native);
  if (data.selection) e.source_data_bytes = add(e.source_data_bytes,
      selection_size(*data.selection, shape, limits, false).native);
  // Previous selection may legitimately belong to an earlier canvas extent.
  // It is copied/accounted, never reselected or revalidated against this XY.
  if (data.previous_selection) e.source_data_bytes = add(e.source_data_bytes,
      selection_size(*data.previous_selection, shape, limits, false, false).native);
  e.source_data_bytes = add(e.source_data_bytes,
      mul(data.overlays.size(), sizeof(MetadataObject)));
  within(e.source_data_bytes, limits.max_working_bytes);
  for (const auto &overlay : data.overlays) {
    e.source_data_bytes = add(e.source_data_bytes, MetadataSize(limits).object(overlay).native);
    within(e.source_data_bytes, limits.max_working_bytes);
  }
  const auto &calibration = data.dataset.calibration();
  e.source_data_bytes = add(e.source_data_bytes,
      add(calibration.unit.size(), calibration.time_unit.size()));
  if (data.spectrum) e.source_data_bytes = add(e.source_data_bytes,
      add(sizeof(FourierState), mul(data.spectrum->spectrum().pixels.size(),
                                   sizeof(std::complex<double>))));
  e.original_selection_bytes = original_size.native;
  e.replacement_selection_bytes = replacement_size.native;
  e.vertices = add(geometry_vertices(original.selection), geometry_vertices(replacement));
  const auto original_roi = roi_size(original, shape, limits, true);
  // Structural upper bounds for the normalized seven-field record and its
  // canonical nativeV3 nested values; never discover size by formatting it.
  e.record_wire_bytes = add(4096, add(original_roi.wire, replacement_size.wire));
  e.record_metadata_bytes = add(e.record_wire_bytes,
      mul(add(80, add(original_roi.nodes, replacement_size.nodes)), node_bytes));
  finish_estimate(e, original.selection.is_mask() || replacement.is_mask(), limits);
  return e;
}

const MetadataObject &object_value(const MetadataValue &value) {
  const auto tree = std::get_if<std::shared_ptr<const MetadataTree>>(&value);
  if (tree && *tree)
    if (const auto object = std::get_if<MetadataObject>(&(*tree)->value)) return *object;
  invalid("Saved ROI record requires an object");
}
const MetadataArray &array_value(const MetadataValue &value) {
  // The canonical scalar codec preserves empty JSON arrays as string lists.
  if (const auto strings = std::get_if<std::vector<std::string>>(&value);
      strings && strings->empty()) {
    static const MetadataArray empty;
    return empty;
  }
  const auto tree = std::get_if<std::shared_ptr<const MetadataTree>>(&value);
  if (tree && *tree)
    if (const auto array = std::get_if<MetadataArray>(&(*tree)->value)) return *array;
  invalid("Saved ROI record requires an array");
}
const std::string &text_value(const MetadataValue &value) {
  if (const auto text = std::get_if<std::string>(&value)) return *text;
  invalid("Saved ROI record requires text");
}
std::uint64_t uint_value(const MetadataValue &value) {
  if (const auto number = std::get_if<std::uint64_t>(&value)) return *number;
  if (const auto number = std::get_if<std::int64_t>(&value); number && *number >= 0)
    return std::uint64_t(*number);
  invalid("Saved ROI record requires a nonnegative integer");
}
std::size_t size_value(const MetadataValue &value) {
  const auto number = uint_value(value);
  if (number > std::numeric_limits<std::size_t>::max()) invalid("Saved ROI record count overflow");
  return std::size_t(number);
}
int index_value(const MetadataValue &value) {
  const auto number = uint_value(value);
  if (number > std::uint64_t(std::numeric_limits<int>::max())) invalid("Saved ROI record index overflow");
  return int(number);
}
double number_value(const MetadataValue &value) {
  double result;
  if (const auto number = std::get_if<double>(&value)) result = *number;
  else if (const auto number = std::get_if<std::int64_t>(&value)) {
    result = static_cast<double>(*number);
    if (result >= std::ldexp(1.0, 63) || result < -std::ldexp(1.0, 63) ||
        static_cast<std::int64_t>(result) != *number)
      invalid("Saved ROI numeric integer loses precision");
  } else if (const auto number = std::get_if<std::uint64_t>(&value)) {
    result = static_cast<double>(*number);
    if (result >= std::ldexp(1.0, 64) || static_cast<std::uint64_t>(result) != *number)
      invalid("Saved ROI numeric integer loses precision");
  } else invalid("Saved ROI record requires a number");
  if (!std::isfinite(result)) invalid("Saved ROI record number must be finite");
  return result;
}
void fields(const MetadataObject &object, std::initializer_list<const char *> names) {
  if (object.size() != names.size()) invalid("Saved ROI record has missing or unknown fields");
  for (const auto name : names)
    if (!object.count(name)) invalid("Saved ROI record has missing or unknown fields");
}
void preflight_selection_wire(const MetadataObject &value, DatasetShape shape,
                             const SavedRoiGeometryLimits &limits,bool regional,bool paths) {
  const auto kind_entry = value.find("kind");
  if (kind_entry == value.end()) invalid("Missing saved ROI selection kind");
  const auto &kind = text_value(kind_entry->second);
  if (!area_kind(kind)) invalid("Saved ROI record requires an area");
  if(kind=="path") {
    if(!paths)invalid("Path saved ROI geometry requires command version 4");
    fields(value,{"kind","points","width","attributes","path"});
    const auto &path=array_value(value.at("path"));
    within(mul(path.size(),sizeof(double)),limits.max_working_bytes);
    within(mul(path.size(),sizeof(double)),ImageJRoiLimits{}.max_bytes);
    if(path.empty())invalid("Empty saved ROI path");
    bool active=false;std::size_t segments=0,vertices=0;
    for(std::size_t at=0;at<path.size();) {
      const double opcode=number_value(path[at++]);
      if(opcode<0 || opcode>4 || std::floor(opcode)!=opcode)invalid("Invalid saved ROI path opcode");
      const int op=int(opcode);const auto n=op==4?0u:op==3?6u:op==2?4u:2u;
      if(n>path.size()-at)invalid("Truncated saved ROI path segment");
      if(op!=0 && !active)invalid("Saved ROI path must begin with move");
      active=true;within(++segments,ImageJRoiLimits{}.max_vertices);
      vertices=add(vertices,n/2);within(vertices,limits.max_vertices);
      for(unsigned i=0;i<n;++i)if(std::abs(number_value(path[at+i]))>1e9)
        invalid("Saved ROI path coordinate range");
      at+=n;
    }
  } else if(kind=="region") {
    if(!regional)invalid("Regional saved ROI geometry requires command version 3");
    fields(value,{"kind","points","width","attributes","shape","mask","origin"});
    const auto &origin=array_value(value.at("origin"));
    if(origin.size()!=2)invalid("Regional saved ROI origin requires two integers");
    for(const auto &item:origin) {
      if(const auto n=std::get_if<std::int64_t>(&item)) {
        if(*n<std::numeric_limits<int>::min() || *n>std::numeric_limits<int>::max())invalid("Regional saved ROI origin exceeds int32");
      } else if(const auto positive=std::get_if<std::uint64_t>(&item)) {
        if(*positive>std::uint64_t(std::numeric_limits<int>::max()))invalid("Regional saved ROI origin exceeds int32");
      } else invalid("Regional saved ROI origin requires exact signed integers");
    }
  }
  else if (kind == "mask") fields(value, {"kind", "points", "width", "attributes", "shape", "mask"});
  else fields(value, {"kind", "points", "width", "attributes"});
  const auto &points = array_value(value.at("points"));
  within(points.size(), limits.max_vertices);
  within(mul(points.size(), sizeof(std::array<double, 2>)), limits.max_working_bytes);
  for (const auto &point : points) {
    const auto &pair = array_value(point);
    if (pair.size() != 2) invalid("Saved ROI point must contain two numbers");
    (void)number_value(pair[0]);
    (void)number_value(pair[1]);
  }
  if (number_value(value.at("width")) <= 0) invalid("Saved ROI width must be positive");
  (void)object_value(value.at("attributes"));
  if(kind=="region" && !points.empty())invalid("Regional saved ROI cannot contain points");
  if(kind=="path" && !points.empty())invalid("Path saved ROI cannot contain points");
  if (kind == "mask" || kind=="region") {
    const auto &mask_shape = array_value(value.at("shape"));
    if(mask_shape.size()!=2)invalid("Saved ROI mask requires two dimensions");
    const auto h=index_value(mask_shape[0]),w=index_value(mask_shape[1]);
    if(!h || !w || (kind=="mask" && (h!=shape.y || w!=shape.x)))
      invalid("Saved ROI record mask shape differs from source");
    const auto samples=mul(std::size_t(h),std::size_t(w));
    within(samples,limits.max_xy_pixels);
    const auto packed = add(samples, 7) / 8;
    if (text_value(value.at("mask")).size() != base64_size(packed))
      invalid("Saved ROI record mask length differs from source");
  }
}
struct RecordView {
  DatasetShape shape;
  PixelType type;
  FloatingSamplePolicy policy;
  std::uint64_t id;
  Revisions revisions;
  PlanePosition position;
  std::size_t row_count, index;
  const MetadataObject *roi;
  const MetadataObject *replacement;
  Size size;
  bool regional;
  bool paths;
};
RecordView inspect_record(const MetadataObject &record, const SavedRoiGeometryLimits &limits) {
  limits_valid(limits);
  const auto size = MetadataSize(limits).object(record);
  within(size.wire, limits.max_record_bytes);
  fields(record, {"schema", "version", "operation", "source", "position", "original", "replacement"});
  if (text_value(record.at("schema")) != schema || (uint_value(record.at("version")) != 1 && uint_value(record.at("version")) != 2 && uint_value(record.at("version")) != 3 && uint_value(record.at("version")) != 4) ||
      text_value(record.at("operation")) != operation)
    invalid("Unsupported saved ROI command schema/version/operation");
  const auto &source = object_value(record.at("source"));
  fields(source, {"id", "pixels_revision", "metadata_revision", "selection_revision",
                  "shape", "type", "sample_policy", "roi_count"});
  const auto &dimensions = array_value(source.at("shape"));
  if (dimensions.size() != 6) invalid("Saved ROI record needs TCZYXS shape");
  int d[6];
  for (int i = 0; i < 6; ++i) {
    d[i] = index_value(dimensions[std::size_t(i)]);
    if (!d[i]) invalid("Saved ROI shape must be positive");
  }
  DatasetShape shape{d[0], d[1], d[2], d[3], d[4], d[5]};
  (void)xy_size(shape, limits);
  const auto &type = text_value(source.at("type"));
  PixelType pixel_type;
  if (type == "uint8") pixel_type = PixelType::UInt8;
  else if (type == "uint16") pixel_type = PixelType::UInt16;
  else if (type == "float32") pixel_type = PixelType::Float32;
  else if (type == "float64") pixel_type = PixelType::Float64;
  else invalid("Unknown saved ROI record type");
  const auto &policy = text_value(source.at("sample_policy"));
  FloatingSamplePolicy sample_policy;
  if (policy == "finite_only") sample_policy = FloatingSamplePolicy::FiniteOnly;
  else if (policy == "preserve_quiet_nan_float32") sample_policy = FloatingSamplePolicy::PreserveQuietNaNFloat32;
  else if (policy == "preserve_ieee_float32") sample_policy = FloatingSamplePolicy::PreserveIEEEFloat32;
  else invalid("Unknown saved ROI record sample policy");
  validate_policy(pixel_type, sample_policy, shape);
  const auto count = size_value(source.at("roi_count"));
  if (!count) invalid("Saved ROI record needs at least one row");
  within(mul(mul(count, sizeof(Roi)), 4), limits.max_working_bytes);
  const auto &original = object_value(record.at("original"));
  fields(original, {"index", "roi"});
  const auto index = size_value(original.at("index"));
  if (index >= count) invalid("Saved ROI record row index is outside row count");
  const auto &roi = object_value(original.at("roi"));
  fields(roi, {"name", "selection", "t", "c", "z", "attributes"});
  (void)text_value(roi.at("name"));
  (void)object_value(roi.at("attributes"));
  const auto &replacement = object_value(record.at("replacement"));
  const auto version=uint_value(record.at("version"));
  const auto &old_selection=object_value(roi.at("selection"));
  preflight_selection_wire(old_selection, shape, limits,version>=3,version==4);
  preflight_selection_wire(replacement, shape, limits,version>=3,version==4);
  const bool hasPath=text_value(old_selection.at("kind"))=="path" || text_value(replacement.at("kind"))=="path";
  const bool hasRegion=text_value(old_selection.at("kind"))=="region" || text_value(replacement.at("kind"))=="region";
  const auto rounded = [](const MetadataObject &selection) {
    const auto &kind = text_value(selection.at("kind"));
    return kind == "rounded_rectangle" || kind == "rotated_rounded_rectangle";
  };
  const bool hasRounded = rounded(object_value(roi.at("selection"))) || rounded(replacement);
  if (uint_value(record.at("version")) != (hasPath ? 4u : hasRegion ? 3u : hasRounded ? 2u : 1u))
    invalid("Saved ROI record version does not match geometry");
  const auto &position = object_value(record.at("position"));
  fields(position, {"t", "c", "z"});
  PlanePosition p{index_value(position.at("t")), index_value(position.at("c")), index_value(position.at("z"))};
  position_valid(p, shape);
  const auto id = uint_value(source.at("id"));
  if (!id) invalid("Saved ROI record owner ID must be positive");
  // Codec conversions and decoded selections coexist. Bound them, and all
  // descriptor rows claimed by untrusted roi_count, without allocating rows.
  const auto decoded_bound = add(mul(12, size.native), mul(3, size.wire));
  within(add(add(decoded_bound, fixed_bytes), mul(mul(count, sizeof(Roi)), 4)), limits.max_working_bytes);
  return {shape, pixel_type, sample_policy, id,
      {uint_value(source.at("pixels_revision")), uint_value(source.at("metadata_revision")),
       uint_value(source.at("selection_revision")), 0},
      p, count, index, &roi, &replacement, size, version>=3,version==4};
}
SessionLimits codec_limits(const SavedRoiGeometryLimits &limits,bool paths) {
  SessionLimits result;
  // A segment contributes at most three control pairs. Clamp a generous caller
  // cap to the native path domain before deriving scalar counts, avoiding
  // overflow for an otherwise valid request with max_vertices == SIZE_MAX.
  const auto segments=ImageJRoiLimits{}.max_vertices;
  result.max_nodes = paths ? add(segments,mul(2,std::min(limits.max_vertices,mul(3,segments)))) : limits.max_vertices;
  result.max_samples = limits.max_xy_pixels;
  result.max_json_bytes = limits.max_record_bytes;
  result.max_encoded_bytes = limits.max_record_bytes;
  result.max_working_bytes = limits.max_working_bytes;
  result.max_depth = limits.metadata_depth;
  return result;
}
struct Decoded {
  Roi original;
  Selection replacement;
};
Decoded decode_record(const RecordView &view, const SavedRoiGeometryLimits &limits) {
  auto original = detail::selection_codec::decode_roi(
      detail::session_metadata::to_json(*view.roi), codec_limits(limits,view.paths),view.regional,view.paths);
  auto replacement = detail::selection_codec::decode_selection(
      detail::session_metadata::to_json(*view.replacement), codec_limits(limits,view.paths),view.regional,view.paths);
  // Only callback-free state reconstructed from wire crosses this boundary.
  // Match the session codec's public invalid-wire exception contract without
  // intercepting callback exceptions or later live-owner binding failures.
  try {
    (void)roi_size(original, view.shape, limits, true);
    (void)selection_size(replacement, view.shape, limits, true);
  } catch (const std::out_of_range &error) {
    throw std::invalid_argument(error.what());
  }
  if (!visible(original, view.position)) invalid("Saved ROI record provenance position contradicts original row");
  if (!same_geometry_style(original.selection, replacement))
    invalid("Saved ROI record changes selection style/attributes");
  return {std::move(original), std::move(replacement)};
}
MetadataObject encode_record(const DocumentSnapshot &source, std::size_t index,
                             const Roi &original, const Selection &replacement) {
  const auto shape = source.data().dataset.shape();
  const auto r = source.revisions();
  const auto p = current_position(source);
  const bool regional=original.selection.is_region() || replacement.is_region();
  const bool paths=original.selection.is_path() || replacement.is_path();
  MetadataArray dimensions;
  dimensions.reserve(6);
  for (const auto value : shape.dimensions()) dimensions.emplace_back(value);
  return {{"schema", std::string(schema)}, {"version", paths ? 4 : regional ? 3 : original.selection.is_rounded_box() || replacement.is_rounded_box() ? 2 : 1}, {"operation", std::string(operation)},
      {"source", metadata_object({{"id", source.id()}, {"pixels_revision", r.pixels},
          {"metadata_revision", r.metadata}, {"selection_revision", r.selection},
          {"shape", metadata_array(std::move(dimensions))},
          {"type", std::string(type_name(source.data().dataset.pixel_type()))},
          {"sample_policy", std::string(policy_name(source.data().dataset.sample_policy()))},
          {"roi_count", std::uint64_t(source.data().rois.size())}})},
      {"position", metadata_object({{"t", p.t}, {"c", p.c}, {"z", p.z}})},
      {"original", metadata_object({{"index", std::uint64_t(index)},
          {"roi", detail::session_metadata::from_json(detail::selection_codec::encode_roi(original,regional,paths))}})},
      {"replacement", detail::session_metadata::from_json(detail::selection_codec::encode_selection(replacement,regional,paths))}};
}
void canceled(const ExecutionControl &control) {
  if (control.cancellation && control.cancellation->canceled()) throw OperationCancelled();
}
void bind_record(const DocumentSnapshot &source, const RecordView &view,
                 const Roi &original) {
  const auto &dataset = source.data().dataset;
  if (source.id() != view.id || source.revisions().pixels != view.revisions.pixels ||
      source.revisions().metadata != view.revisions.metadata ||
      dataset.shape().dimensions() != view.shape.dimensions() ||
      dataset.pixel_type() != view.type || dataset.sample_policy() != view.policy ||
      source.data().rois.size() != view.row_count ||
      !(source.data().rois[view.index] == original))
    throw RevisionConflict();
}
} // namespace

namespace detail {
std::size_t saved_roi_geometry_display_payload_bytes(
    const DisplayState &display, const SavedRoiGeometryLimits &limits) {
  limits_valid(limits);
  // Borrowed accounting only, including when called by the typed commit guard.
  // No execution scope, callbacks, owned Display copy or provider access.
  return MetadataSize(limits).object(display.properties).native;
}

SavedRoiGeometryEstimate estimate_saved_roi_geometry_preparation(
    const DocumentSnapshot &captured_source,
    const ResolvedSavedRoiGeometryCommand &command,
    const SavedRoiGeometryLimits &limits) {
  const ExecutionControl quiet;
  ExecutionScope preflight(quiet);
  const auto &bound = command.source();
  if (!captured_source.same_document(bound) ||
      !captured_source.revisions().matches(bound.revisions(), Change::Data) ||
      captured_source.data_identity() != bound.data_identity() ||
      captured_source.data().rois.size() != bound.data().rois.size() ||
      command.row_index() >= captured_source.data().rois.size() ||
      !(captured_source.data().rois[command.row_index()] == command.original_roi()))
    throw RevisionConflict();
  const auto position = current_position(captured_source);
  const auto expected = command.position();
  if (position.t != expected.t || position.c != expected.c || position.z != expected.z)
    throw RevisionConflict();
  // Every factory fully validates this immutable record once. Reapply its
  // borrowed structural/resource admission under tighter preparation limits;
  // no decoded Selection or owned Request is constructed before admission.
  const auto record = inspect_record(command.record(), limits);
  bind_record(captured_source, record, command.original_roi());
  if (record.index != command.row_index()) throw RevisionConflict();
  auto estimate = layout(captured_source, command.row_index(), command.replacement(),
                         limits, !command.is_replay());
  // Numeric refresh may discard a large Display property from the fresh
  // capture, while the immutable command still retains its original Display.
  // Both snapshots remain live in Prepared, so admit the larger payload under
  // the tighter preparation limits without requiring Display equality.
  const auto retained = layout(bound, command.row_index(), command.replacement(),
                               limits, !command.is_replay());
  estimate.source_data_bytes = std::max(estimate.source_data_bytes,
                                        retained.source_data_bytes);
  estimate.record_metadata_bytes = std::max(estimate.record_metadata_bytes, record.size.native);
  estimate.record_wire_bytes = std::max(estimate.record_wire_bytes, record.size.wire);
  finish_estimate(estimate, command.original_roi().selection.is_mask() ||
                            command.replacement().is_mask(), limits);
  return estimate;
}
} // namespace detail

ResolvedSavedRoiGeometryCommand::ResolvedSavedRoiGeometryCommand(std::shared_ptr<const Data> data)
    : data_(std::move(data)) {}
const DocumentSnapshot &ResolvedSavedRoiGeometryCommand::source() const { return data_->source; }
std::size_t ResolvedSavedRoiGeometryCommand::row_index() const { return data_->row_index; }
const Roi &ResolvedSavedRoiGeometryCommand::original_roi() const { return data_->original; }
const Selection &ResolvedSavedRoiGeometryCommand::replacement() const { return data_->replacement; }
PlanePosition ResolvedSavedRoiGeometryCommand::position() const { return data_->position; }
const MetadataObject &ResolvedSavedRoiGeometryCommand::record() const { return data_->record; }
const SavedRoiGeometryEstimate &ResolvedSavedRoiGeometryCommand::estimate() const { return data_->estimate; }
const SavedRoiGeometryLimits &ResolvedSavedRoiGeometryCommand::limits() const { return data_->limits; }
bool ResolvedSavedRoiGeometryCommand::is_replay() const { return data_->replay; }

SavedRoiGeometryEstimate estimate_saved_roi_geometry(const DocumentSnapshot &source,
    const SavedRoiGeometryRequest &request, const SavedRoiGeometryLimits &limits) {
  const ExecutionControl quiet;
  ExecutionScope preflight(quiet);
  return layout(source, request.row_index, request.replacement, limits, true);
}
ResolvedSavedRoiGeometryCommand resolve_saved_roi_geometry(const DocumentSnapshot &source,
    const SavedRoiGeometryRequest &request, const SavedRoiGeometryLimits &limits,
    const ExecutionControl &control) {
  const ExecutionControl quiet;
  ExecutionScope preflight(quiet);
  const auto estimate = layout(source, request.row_index, request.replacement, limits, true);
  canceled(control);
  const auto owned_source = source;
  const auto owned_request = request;
  const auto owned_limits = limits;
  const auto owned_control = control;
  const auto original = owned_source.data().rois[owned_request.row_index];
  ExecutionScope execution(owned_control);
  kernel_checkpoint();
  execution_stage_progress("saved_roi_geometry.capture", 0, 1);
  auto record = encode_record(owned_source, owned_request.row_index, original, owned_request.replacement);
  // Validate the materialized result against the same frozen upper bound.
  const auto actual = MetadataSize(owned_limits).object(record);
  within(actual.wire, estimate.record_wire_bytes);
  within(actual.native, estimate.record_metadata_bytes);
  kernel_checkpoint();
  execution_stage_progress("saved_roi_geometry.capture", 1, 1);
  kernel_checkpoint();
  return ResolvedSavedRoiGeometryCommand(std::make_shared<const ResolvedSavedRoiGeometryCommand::Data>(
      ResolvedSavedRoiGeometryCommand::Data{owned_source, owned_request.row_index,
          original, owned_request.replacement, current_position(owned_source),
          std::move(record), estimate, owned_limits, false}));
}
SavedRoiGeometryRequest decode_saved_roi_geometry_request(const MetadataObject &record,
    const SavedRoiGeometryLimits &limits) {
  const ExecutionControl quiet;ExecutionScope scope(quiet);
  const auto view=inspect_record(record,limits);
  auto decoded=decode_record(view,limits);
  return SavedRoiGeometryRequest{view.index,std::move(decoded.replacement)};
}
void validate_saved_roi_geometry_record(const MetadataObject &record,
                                       const SavedRoiGeometryLimits &limits) {
  const ExecutionControl quiet;
  ExecutionScope preflight(quiet);
  const auto view = inspect_record(record, limits);
  (void)decode_record(view, limits);
}
ResolvedSavedRoiGeometryCommand saved_roi_geometry_from_record(const DocumentSnapshot &source,
    const MetadataObject &record, const SavedRoiGeometryLimits &limits,
    const ExecutionControl &control) {
  const ExecutionControl quiet;
  ExecutionScope preflight(quiet);
  const auto view = inspect_record(record, limits);
  canceled(control);
  auto decoded = decode_record(view, limits);
  bind_record(source, view, decoded.original);
  auto estimate = layout(source, view.index, decoded.replacement, limits, false);
  estimate.record_metadata_bytes = std::max(estimate.record_metadata_bytes, view.size.native);
  estimate.record_wire_bytes = std::max(estimate.record_wire_bytes, view.size.wire);
  finish_estimate(estimate, decoded.original.selection.is_mask() || decoded.replacement.is_mask(), limits);
  const auto owned_source = source;
  const auto owned_record = record;
  const auto owned_limits = limits;
  const auto owned_control = control;
  ExecutionScope execution(owned_control);
  kernel_checkpoint();
  execution_stage_progress("saved_roi_geometry.replay", 1, 1);
  kernel_checkpoint();
  return ResolvedSavedRoiGeometryCommand(std::make_shared<const ResolvedSavedRoiGeometryCommand::Data>(
      ResolvedSavedRoiGeometryCommand::Data{owned_source, view.index,
          std::move(decoded.original), std::move(decoded.replacement), current_position(owned_source),
          owned_record, estimate, owned_limits, true}));
}
ResolvedSavedRoiGeometryCommand rebind_saved_roi_geometry(const DocumentSnapshot &destination,
    const MetadataObject &original_record, const SavedRoiGeometryRequest &complete_request,
    const SavedRoiGeometryLimits &limits, const ExecutionControl &control) {
  // Do not inspect destination or replacement before validating the old record.
  {
    const ExecutionControl quiet;
    ExecutionScope preflight(quiet);
    const auto view = inspect_record(original_record, limits);
    canceled(control);
    (void)decode_record(view, limits);
  }
  return resolve_saved_roi_geometry(destination, complete_request, limits, control);
}
} // namespace imagejxx
