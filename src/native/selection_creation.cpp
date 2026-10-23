#include <imagejxx/selection_creation.hpp>
#include <imagejxx/selection_enlargement.hpp>
#include <imagejxx/selection_region.hpp>
#include <imagejxx/imagej_roi.hpp>

#include "kernel_control.hpp"
#include "session_metadata.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <initializer_list>
#include <utility>

namespace imagejxx {
namespace {
constexpr const char *schema = "imagejxx.selection-creation-command";
constexpr std::size_t node_bytes =
    sizeof(MetadataObject::value_type) + 8 * sizeof(void *) > 160
        ? sizeof(MetadataObject::value_type) + 8 * sizeof(void *) : 160;
constexpr std::size_t codec_scratch = 16 * 1024;

void start_progress() {
  kernel_checkpoint(); execution_progress(0, 1); kernel_checkpoint();
}
void finish_progress() {
  kernel_checkpoint(); execution_progress(1, 1); kernel_checkpoint();
}

[[noreturn]] void invalid(const char *message) {
  throw std::invalid_argument(message);
}
std::size_t add(std::size_t a, std::size_t b) {
  if (b > std::numeric_limits<std::size_t>::max() - a)
    invalid("Selection creation size overflow");
  return a + b;
}
std::size_t mul(std::size_t a, std::size_t b) {
  if (a && b > std::numeric_limits<std::size_t>::max() / a)
    invalid("Selection creation size overflow");
  return a * b;
}
std::size_t packed_size(std::size_t pixels) { return add(pixels, 7) / 8; }
std::size_t base64_size(std::size_t bytes) { return mul(add(bytes, 2) / 3, 4); }
void budget(std::size_t bytes, const SelectionCreationLimits &limits) {
  if (bytes > limits.max_working_bytes)
    invalid("Selection creation exceeds working memory limit");
}
// Deliberately conservative owned-storage accounting. Immutable nested values
// and masks may be charged more than once. No allocator bookkeeping is claimed.
struct Charge {
  const SelectionCreationLimits &limits;
  std::size_t bytes = 0;
  void take(std::size_t count) { bytes = add(bytes, count); budget(bytes, limits); }
  void text(const std::string &value) { take(add(value.size(), 1)); }
  void value(const MetadataValue &entry, unsigned depth = 0) {
    take(node_bytes);
    if (const auto number = std::get_if<double>(&entry)) {
      if (!std::isfinite(*number)) invalid("Metadata values must be finite");
    } else if (const auto text_value = std::get_if<std::string>(&entry)) {
      text(*text_value);
    } else if (const auto strings = std::get_if<std::vector<std::string>>(&entry)) {
      take(mul(strings->size(), sizeof(std::string)));
      for (const auto &item : *strings) text(item);
    } else if (const auto tree =
                   std::get_if<std::shared_ptr<const MetadataTree>>(&entry)) {
      if (!*tree) invalid("Metadata container must not be null");
      if (depth >= 128) invalid("Metadata nesting exceeds 128 containers");
      take(sizeof(MetadataTree));
      if (const auto array = std::get_if<MetadataArray>(&(*tree)->value)) {
        take(mul(array->size(), sizeof(MetadataValue)));
        for (const auto &item : *array) value(item, depth + 1);
      } else object(std::get<MetadataObject>((*tree)->value), depth + 1);
    }
  }
  void object(const MetadataObject &object, unsigned depth = 0) {
    take(sizeof(MetadataObject));
    for (const auto &[key, entry] : object) { text(key); value(entry, depth); }
  }
};
std::size_t metadata_size(const MetadataObject &object,
                          const SelectionCreationLimits &limits) {
  Charge charge{limits}; charge.object(object); return charge.bytes;
}
std::size_t geometry_size(const Selection &selection,
                          const SelectionCreationLimits &limits) {
  // A mutable public kind must not hide an old private mask payload: V3 writes
  // mask storage only for kind=mask and could not round-trip such a hybrid.
  if (!selection.is_mask() && (selection.mask_width() != 0 ||
      selection.mask_height() != 0 || !selection.packed_mask().empty()))
    invalid("Selection geometry contains hidden mask storage");
  Charge charge{limits};
  charge.take(sizeof(Selection)); charge.text(selection.kind);
  charge.take(mul(selection.points.size(), sizeof(selection.points[0])));
  charge.object(selection.attributes);
  if (selection.is_mask())
    charge.take(add(node_bytes, selection.packed_mask().size()));
  return charge.bytes;
}
std::size_t wire_size(const Selection &selection,
                      const SelectionCreationLimits &limits) {
  auto bytes = add(metadata_size(selection.attributes, limits), 12 * node_bytes);
  // One immutable row node, two scalar values, array slot/container, and slack.
  bytes = add(bytes, mul(selection.points.size(), 6 * node_bytes));
  bytes = add(bytes, selection.kind.size());
  if (selection.is_mask())
    bytes = add(bytes, add((selection.is_region()?16:8) * node_bytes,
                           base64_size(selection.packed_mask().size())));
  budget(bytes, limits); return bytes;
}
std::size_t optional_size(const std::optional<Selection> &value,
                          const SelectionCreationLimits &limits) {
  return value ? geometry_size(*value, limits) : sizeof(value);
}
std::size_t display_size(const DisplayState &display,
                         const SelectionCreationLimits &limits) {
  return add(sizeof(DisplayState), metadata_size(display.properties, limits));
}
std::size_t data_copy_size(const DocumentSnapshot &source,
                           const SelectionCreationLimits &limits) {
  const auto &data = source.data();
  Charge charge{limits};
  charge.take(sizeof(DocumentData));
  charge.take(optional_size(data.selection, limits));
  charge.take(optional_size(data.previous_selection, limits));
  charge.text(data.metadata.title);
  charge.object(data.metadata.properties);
  charge.object(data.metadata.extensions);
  charge.object(data.metadata.record_layout);
  charge.take(mul(data.rois.size(), sizeof(Roi)));
  for (const auto &roi : data.rois) {
    charge.text(roi.name); charge.take(geometry_size(roi.selection, limits));
    charge.object(roi.attributes);
  }
  charge.take(mul(data.overlays.size(), sizeof(MetadataObject)));
  for (const auto &overlay : data.overlays) charge.object(overlay);
  charge.text(data.dataset.calibration().unit);
  charge.text(data.dataset.calibration().time_unit);
  charge.take(display_size(source.display(), limits));
  return charge.bytes;
}

const char *operation_name(SelectionCreationOperation operation) {
  switch (operation) {
  case SelectionCreationOperation::Replace: return "replace";
  case SelectionCreationOperation::Add: return "add";
  case SelectionCreationOperation::Subtract: return "subtract";
  case SelectionCreationOperation::Intersect: return "intersect";
  case SelectionCreationOperation::Xor: return "xor";
  }
  invalid("Unknown selection creation operation");
}
bool rounded_kind(const std::string &kind) {
  return kind == "rounded_rectangle" || kind == "rotated_rounded_rectangle";
}
std::size_t rounded_scratch(std::size_t pixels, bool rounded) {
  if (!rounded) return 0;
  if (pixels > ImageJRoiLimits{}.max_mask_pixels)
    invalid("Rounded selection exceeds raster pixel limit");
  return add(512 * 1024, packed_size(pixels));
}
bool area(const Selection &selection) {
  const auto &k = selection.kind;
  return rounded_kind(k) || k == "rectangle" || k == "ellipse" || k == "polygon" ||
         k == "freehand" || k == "rotated_rectangle" ||
         k == "rotated_ellipse" || k == "mask";
}
bool combines(SelectionCreationOperation operation,
              const std::optional<Selection> &base) {
  return operation != SelectionCreationOperation::Replace && bool(base);
}
void candidate_admission(const Selection &candidate, DatasetShape shape,
                         const SelectionCreationLimits &limits) {
  if (candidate.kind != "rectangle" && candidate.kind != "ellipse" &&
      candidate.kind != "polygon" && candidate.kind != "freehand" &&
      candidate.kind != "rounded_rectangle")
    invalid("Selection creation requires a complete rectangle, rounded rectangle, ellipse, polygon or freehand candidate");
  if (candidate.points.size() > limits.max_candidate_vertices)
    invalid("Selection candidate exceeds vertex limit");
  candidate.validate(shape.x, shape.y);
  std::array<double, 2> low{std::numeric_limits<double>::max(),
                            std::numeric_limits<double>::max()},
                        high{-std::numeric_limits<double>::max(),
                             -std::numeric_limits<double>::max()};
  for (const auto &point : candidate.points)
    for (int axis = 0; axis < 2; ++axis) {
      low[axis] = std::min(low[axis], point[axis]);
      high[axis] = std::max(high[axis], point[axis]);
    }
  for (int axis = 0; axis < 2; ++axis)
    if (!std::isfinite(high[axis] - low[axis]) || high[axis] <= low[axis])
      invalid("Selection candidate has no complete XY extent");
}
void operands_admission(DatasetShape shape, const std::optional<Selection> &base,
                        const SelectionCreationRequest &request,
                        const SelectionCreationLimits &limits) {
  (void)operation_name(request.operation);
  candidate_admission(request.candidate, shape, limits);
  if (base) base->validate(shape.x, shape.y);
  if (request.operation == SelectionCreationOperation::Replace) return;
  if (!base) {
    if (request.operation == SelectionCreationOperation::Add) return;
    invalid("Selection combination requires an existing area base");
  }
  if (!area(*base)) invalid("Selection combination requires an area base");
}

const char *type_name(PixelType type) {
  switch (type) {
  case PixelType::UInt8: return "uint8";
  case PixelType::UInt16: return "uint16";
  case PixelType::Float32: return "float32";
  case PixelType::Float64: return "float64";
  }
  invalid("Unknown selection source type");
}
const char *policy_name(FloatingSamplePolicy policy) {
  switch (policy) {
  case FloatingSamplePolicy::FiniteOnly: return "finite_only";
  case FloatingSamplePolicy::PreserveQuietNaNFloat32:
    return "preserve_quiet_nan_float32";
  case FloatingSamplePolicy::PreserveIEEEFloat32:
    return "preserve_ieee_float32";
  }
  invalid("Unknown selection source sample policy");
}
struct Source {
  std::uint64_t id;
  Revisions revisions;
  DatasetShape shape;
  PixelType type;
  FloatingSamplePolicy policy;
  PlanePosition position;
};
std::size_t source_admission(const Source &source,
                             const SelectionCreationLimits &limits) {
  if (!source.id) invalid("Selection command requires a positive source ID");
  source.shape.validate();
  (void)type_name(source.type); (void)policy_name(source.policy);
  if (source.policy != FloatingSamplePolicy::FiniteOnly &&
      (source.type != PixelType::Float32 || source.shape.s != 1))
    invalid("Nonfinite selection source requires scalar Float32");
  if (source.position.t < 0 || source.position.t >= source.shape.t ||
      source.position.c < 0 || source.position.c >= source.shape.c ||
      source.position.z < 0 || source.position.z >= source.shape.z)
    invalid("Selection command position is outside source");
  const auto count = mul(static_cast<std::size_t>(source.shape.x),
                         static_cast<std::size_t>(source.shape.y));
  (void)packed_size(count);
  if (count > limits.max_pixels)
    invalid("Selection creation exceeds XY pixel limit");
  return count;
}
Source source_info(const DocumentSnapshot &snapshot) {
  const auto &data = snapshot.data().dataset;
  return {snapshot.id(), snapshot.revisions(), data.shape(), data.pixel_type(),
          data.sample_policy(), {snapshot.display().t, snapshot.display().c,
                                  snapshot.display().crosshair[2]}};
}

std::size_t algebra_scratch(DatasetShape shape,
                            const std::optional<Selection> &base,
                            const SelectionCreationRequest &request,
                            const SelectionCreationLimits &limits) {
  if (!combines(request.operation, base)) return 0;
  const auto pixels = mul(static_cast<std::size_t>(shape.x), shape.y);
  auto bytes = add(2 * sizeof(Selection) + 2 * sizeof(const Selection *),
                   add(geometry_size(*base, limits),
                       geometry_size(request.candidate, limits)));
  bytes = add(bytes, packed_size(pixels));
  // The candidate is always a non-mask area. Native Boolean streams each
  // operand; no TCZ volume or second double canvas is retained.
  bytes = add(bytes, mul(pixels, sizeof(double)));
  const auto vertices = std::max<std::size_t>(
      4, std::max(base->points.size(), request.candidate.points.size()));
  return add(add(bytes, mul(vertices, 3 * sizeof(double))),
             rounded_scratch(pixels, base->is_rounded_box() || request.candidate.is_rounded_box()));
}
SelectionCreationEstimate estimate_parts(
    const Source &source, const std::optional<Selection> &base,
    const SelectionCreationRequest &request,
    const SelectionCreationLimits &limits,
    const DocumentSnapshot *snapshot, const Selection *known_result = nullptr,
    std::size_t known_record_bytes = 0) {
  SelectionCreationEstimate estimate;
  estimate.pixels = source_admission(source, limits);
  estimate.candidate_vertices = request.candidate.points.size();
  // Charge before validate() traverses attributes, and before any owned copies.
  const auto candidate_bytes = geometry_size(request.candidate, limits);
  const auto base_bytes = optional_size(base, limits);
  operands_admission(source.shape, base, request, limits);
  const bool boolean = combines(request.operation, base);
  const auto result_bytes = known_result ? geometry_size(*known_result, limits)
      : boolean ? add(sizeof(Selection) + node_bytes, packed_size(estimate.pixels))
                : candidate_bytes;
  const auto result_wire = known_result ? wire_size(*known_result, limits)
      : boolean ? add(24 * node_bytes, base64_size(packed_size(estimate.pixels)))
                : wire_size(request.candidate, limits);
  auto record_bytes = add(96 * node_bytes,
                          add(base ? wire_size(*base, limits) : node_bytes,
                              add(wire_size(request.candidate, limits), result_wire)));
  record_bytes = std::max(record_bytes, known_record_bytes);
  estimate.retained_bytes = add(sizeof(ResolvedSelectionCreationCommand),
      add(candidate_bytes, add(base_bytes, add(result_bytes, record_bytes))));
  if (snapshot)
    estimate.retained_bytes = add(estimate.retained_bytes,
                                  display_size(snapshot->display(), limits));
  const auto resolve_bytes = add(codec_scratch, algebra_scratch(
      source.shape, base, request, limits));
  std::size_t prepare_bytes = 0;
  if (snapshot) {
    prepare_bytes = add(estimate.retained_bytes,
        add(sizeof(DocumentTransaction),
            add(data_copy_size(*snapshot, limits),
                add(optional_size(snapshot->data().selection, limits), result_bytes))));
  }
  estimate.working_bytes = add(estimate.retained_bytes,
                                std::max(resolve_bytes, prepare_bytes));
  budget(estimate.working_bytes, limits);
  return estimate;
}

Selection algebra(DatasetShape shape, const std::optional<Selection> &base,
                   const SelectionCreationRequest &request) {
  kernel_checkpoint();
  if (!combines(request.operation, base)) return request.candidate;
  SelectionBooleanOperation operation;
  switch (request.operation) {
  case SelectionCreationOperation::Add: operation = SelectionBooleanOperation::Union; break;
  case SelectionCreationOperation::Subtract: operation = SelectionBooleanOperation::Subtraction; break;
  case SelectionCreationOperation::Intersect: operation = SelectionBooleanOperation::Intersection; break;
  case SelectionCreationOperation::Xor: operation = SelectionBooleanOperation::Xor; break;
  default: invalid("Invalid selection creation combination");
  }
  // Avoid initializer_list's second pair of geometry copies at the peak.
  std::vector<Selection> operands;
  operands.reserve(2);
  operands.push_back(*base);
  operands.push_back(request.candidate);
  kernel_checkpoint();
  return combine_selections(shape.x, shape.y, operands, operation);
}

bool text_equal(const std::string &a, const std::string &b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); i += std::min<std::size_t>(1024, a.size() - i)) {
    kernel_checkpoint();
    if (std::memcmp(a.data() + i, b.data() + i,
                    std::min<std::size_t>(1024, a.size() - i)) != 0) return false;
  }
  return true;
}
bool value_equal(const MetadataValue &, const MetadataValue &);
bool object_equal(const MetadataObject &a, const MetadataObject &b) {
  if (a.size() != b.size()) return false;
  auto right = b.begin();
  for (const auto &[key, value] : a) {
    kernel_checkpoint_periodic();
    if (!text_equal(key, right->first) || !value_equal(value, right->second)) return false;
    ++right;
  }
  return true;
}
bool value_equal(const MetadataValue &a, const MetadataValue &b) {
  kernel_checkpoint_periodic();
  const auto strings_array_equal = [](const std::vector<std::string> &strings,
                                       const MetadataValue &other) {
    const auto tree = std::get_if<std::shared_ptr<const MetadataTree>>(&other);
    if (!tree || !*tree) return false;
    const auto array = std::get_if<MetadataArray>(&(*tree)->value);
    if (!array || array->size() != strings.size()) return false;
    for (std::size_t i = 0; i < strings.size(); ++i) {
      kernel_checkpoint_periodic();
      const auto text = std::get_if<std::string>(&(*array)[i]);
      if (!text || !text_equal(strings[i], *text)) return false;
    }
    return true;
  };
  if (a.index() != b.index()) {
    if (const auto strings = std::get_if<std::vector<std::string>>(&a))
      return strings_array_equal(*strings, b);
    if (const auto strings = std::get_if<std::vector<std::string>>(&b))
      return strings_array_equal(*strings, a);
    return metadata_equal(a, b); // Constant-size signed/unsigned alternatives.
  }
  if (const auto text = std::get_if<std::string>(&a))
    return text_equal(*text, std::get<std::string>(b));
  if (const auto strings = std::get_if<std::vector<std::string>>(&a)) {
    const auto &right = std::get<std::vector<std::string>>(b);
    if (strings->size() != right.size()) return false;
    for (std::size_t i = 0; i < strings->size(); ++i)
      if (!text_equal((*strings)[i], right[i])) return false;
    return true;
  }
  if (const auto tree = std::get_if<std::shared_ptr<const MetadataTree>>(&a)) {
    const auto &right = std::get<std::shared_ptr<const MetadataTree>>(b);
    if (*tree == right) return true;
    if (!*tree || !right || (*tree)->value.index() != right->value.index()) return false;
    if (const auto array = std::get_if<MetadataArray>(&(*tree)->value)) {
      const auto &right_array = std::get<MetadataArray>(right->value);
      if (array->size() != right_array.size()) return false;
      for (std::size_t i = 0; i < array->size(); ++i)
        if (!value_equal((*array)[i], right_array[i])) return false;
      return true;
    }
    return object_equal(std::get<MetadataObject>((*tree)->value),
                         std::get<MetadataObject>(right->value));
  }
  return metadata_equal(a, b);
}
bool selection_equal(const Selection &a, const Selection &b) {
  if (!text_equal(a.kind, b.kind) || a.width != b.width ||
      a.points.size() != b.points.size() || a.mask_width() != b.mask_width() ||
      a.mask_height() != b.mask_height() || a.mask_origin()!=b.mask_origin()) return false;
  for (std::size_t i = 0; i < a.points.size(); ++i) {
    kernel_checkpoint_periodic();
    if (a.points[i] != b.points[i]) return false;
  }
  const auto &left = a.packed_mask(), &right = b.packed_mask();
  if (left.size() != right.size()) return false;
  bool same = true;
  kernel_blocks("Selection creation equality", left.size(),
                 [&](std::size_t first, std::size_t last) {
    if (same && last > first &&
        std::memcmp(left.data() + first, right.data() + first, last - first)) same = false;
  });
  return same && object_equal(a.attributes, b.attributes);
}

std::string encode_mask(const std::vector<unsigned char> &bytes) {
  std::string encoded; encoded.reserve(base64_size(bytes.size()));
  for (std::size_t first = 0; first < bytes.size();) {
    kernel_checkpoint();
    const auto last = first + std::min<std::size_t>(3072, bytes.size() - first);
    const std::vector<unsigned char> chunk(bytes.begin() + first, bytes.begin() + last);
    encoded += detail::session_metadata::encode_base64(chunk);
    first = last;
  }
  kernel_checkpoint(); return encoded;
}
MetadataValue encode_selection(const Selection &selection,bool structured=false) {
  kernel_checkpoint();
  if(selection.is_region() && !structured)invalid("Regional selection requires a V2 command codec");
  MetadataObject object=structured?MetadataObject{{"attributes",metadata_object(selection.attributes)}}:selection.attributes;
  MetadataArray points; points.reserve(selection.points.size());
  for (const auto &point : selection.points) {
    kernel_checkpoint_periodic();
    points.emplace_back(metadata_array({point[0], point[1]}));
  }
  object.emplace("kind", selection.kind);
  object.emplace("points", metadata_array(std::move(points)));
  object.emplace("width", selection.width);
  if (selection.is_mask()) {
    if(selection.is_region())object.emplace("origin",metadata_array({std::int64_t(selection.mask_origin()[0]),std::int64_t(selection.mask_origin()[1])}));
    object.emplace("shape", metadata_array({std::int64_t(selection.mask_height()),
                                             std::int64_t(selection.mask_width())}));
    object.emplace("mask", encode_mask(selection.packed_mask()));
  }
  kernel_checkpoint();
  return metadata_object(std::move(object));
}
MetadataObject encode_record(const Source &source,
                             const std::optional<Selection> &base,
                             const SelectionCreationRequest &request,
                             const Selection &result) {
  MetadataArray dimensions; dimensions.reserve(6);
  for (const int extent : source.shape.dimensions()) dimensions.emplace_back(std::int64_t(extent));
  MetadataObject binding{{"id", source.id},
    {"pixels_revision", source.revisions.pixels},
    {"metadata_revision", source.revisions.metadata},
    {"selection_revision", source.revisions.selection},
    {"shape", metadata_array(std::move(dimensions))},
    {"type", std::string(type_name(source.type))},
    {"sample_policy", std::string(policy_name(source.policy))}};
  return {{"schema", std::string(schema)}, {"version", std::int64_t(request.candidate.is_rounded_box() ||
      (base && base->is_rounded_box()) || result.is_rounded_box() ? 2 : 1)},
    {"source", metadata_object(std::move(binding))},
    {"position", metadata_object({{"t", std::int64_t(source.position.t)},
                                   {"c", std::int64_t(source.position.c)},
                                   {"z", std::int64_t(source.position.z)}})},
    {"operation", std::string(operation_name(request.operation))},
    {"base", base ? encode_selection(*base) : MetadataValue{}},
    {"candidate", encode_selection(request.candidate)},
    {"result", encode_selection(result)}};
}

void fields(const MetadataObject &object,
             std::initializer_list<const char *> expected) {
  if (object.size() != expected.size()) invalid("Unexpected selection command fields");
  for (const auto *key : expected)
    if (!object.count(key)) invalid("Missing selection command field");
}
const MetadataObject &object_value(const MetadataValue &value) {
  if (const auto tree = std::get_if<std::shared_ptr<const MetadataTree>>(&value);
      tree && *tree)
    if (const auto object = std::get_if<MetadataObject>(&(*tree)->value)) return *object;
  invalid("Selection command requires an object");
}
const MetadataArray &array_value(const MetadataValue &value) {
  if (const auto strings = std::get_if<std::vector<std::string>>(&value);
      strings && strings->empty()) {
    static const MetadataArray empty; return empty;
  }
  if (const auto tree = std::get_if<std::shared_ptr<const MetadataTree>>(&value);
      tree && *tree)
    if (const auto array = std::get_if<MetadataArray>(&(*tree)->value)) return *array;
  invalid("Selection command requires an array");
}
const std::string &text_value(const MetadataValue &value) {
  if (const auto text = std::get_if<std::string>(&value)) return *text;
  invalid("Selection command requires text");
}
std::uint64_t uint_value(const MetadataValue &value) {
  if (const auto number = std::get_if<std::uint64_t>(&value)) return *number;
  if (const auto number = std::get_if<std::int64_t>(&value); number && *number >= 0)
    return static_cast<std::uint64_t>(*number);
  invalid("Selection command requires an exact nonnegative integer");
}
int index_value(const MetadataValue &value, bool positive = false) {
  const auto number = uint_value(value);
  if ((positive && !number) || number > std::uint64_t(std::numeric_limits<int>::max()))
    invalid("Selection command integer is outside native range");
  return static_cast<int>(number);
}
double number_value(const MetadataValue &value) {
  if (const auto number = std::get_if<double>(&value)) {
    if (!std::isfinite(*number)) invalid("Selection number must be finite");
    return *number;
  }
  std::uint64_t magnitude;
  double result;
  if (const auto number = std::get_if<std::int64_t>(&value)) {
    magnitude = *number < 0 ? std::uint64_t(-(*number + 1)) + 1 : std::uint64_t(*number);
    result = static_cast<double>(*number);
  } else if (const auto unsigned_number = std::get_if<std::uint64_t>(&value)) {
    magnitude = *unsigned_number; result = static_cast<double>(*unsigned_number);
  } else invalid("Selection number must be numeric and not Boolean");
  while (magnitude && !(magnitude & 1u)) magnitude >>= 1;
  if (magnitude > (std::uint64_t(1) << 53))
    invalid("Selection integer is not losslessly representable");
  return result;
}
SelectionCreationOperation read_operation(const MetadataValue &value) {
  const auto &name = text_value(value);
  for (const auto operation : {SelectionCreationOperation::Replace, SelectionCreationOperation::Add,
       SelectionCreationOperation::Subtract, SelectionCreationOperation::Intersect, SelectionCreationOperation::Xor})
    if (name == operation_name(operation)) return operation;
  invalid("Unknown selection creation operation");
}
Source read_source(const MetadataObject &object, const MetadataObject &position) {
  fields(object, {"id", "pixels_revision", "metadata_revision", "selection_revision",
                   "shape", "type", "sample_policy"});
  fields(position, {"t", "c", "z"});
  const auto &axes = array_value(object.at("shape"));
  if (axes.size() != 6) invalid("Selection command shape requires six TCZYXS axes");
  DatasetShape shape{index_value(axes[0], true), index_value(axes[1], true),
    index_value(axes[2], true), index_value(axes[3], true),
    index_value(axes[4], true), index_value(axes[5], true)};
  PixelType type;
  const auto &type_text = text_value(object.at("type"));
  if (type_text == "uint8") type = PixelType::UInt8;
  else if (type_text == "uint16") type = PixelType::UInt16;
  else if (type_text == "float32") type = PixelType::Float32;
  else if (type_text == "float64") type = PixelType::Float64;
  else invalid("Unknown selection command type");
  FloatingSamplePolicy policy;
  const auto &policy_text = text_value(object.at("sample_policy"));
  if (policy_text == "finite_only") policy = FloatingSamplePolicy::FiniteOnly;
  else if (policy_text == "preserve_quiet_nan_float32") policy = FloatingSamplePolicy::PreserveQuietNaNFloat32;
  else if (policy_text == "preserve_ieee_float32") policy = FloatingSamplePolicy::PreserveIEEEFloat32;
  else invalid("Unknown selection command sample policy");
  return {uint_value(object.at("id")),
    {uint_value(object.at("pixels_revision")), uint_value(object.at("metadata_revision")),
     uint_value(object.at("selection_revision")), 0}, shape, type, policy,
    {index_value(position.at("t")), index_value(position.at("c")), index_value(position.at("z"))}};
}

int signed_index_value(const MetadataValue &value) {
  if(const auto n=std::get_if<std::int64_t>(&value)) {
    if(*n<std::numeric_limits<int>::min() || *n>std::numeric_limits<int>::max())invalid("Regional origin exceeds int32");
    return static_cast<int>(*n);
  }
  if(const auto n=std::get_if<std::uint64_t>(&value)) {
    if(*n>std::uint64_t(std::numeric_limits<int>::max()))invalid("Regional origin exceeds int32");
    return static_cast<int>(*n);
  }
  invalid("Regional origin requires exact signed integers");
}
struct Wire {
  const MetadataObject *object;
  const MetadataArray *points;
  const std::string *kind;
  double width;
  const std::string *mask = nullptr;
  const MetadataObject *attributes = nullptr;
  int stored_width=0,stored_height=0;
  std::array<int,2> origin{0,0};
};
Wire inspect_wire(const MetadataValue &value, DatasetShape shape,
                   bool candidate, const SelectionCreationLimits &limits,bool structured=false) {
  const auto &object = object_value(value);
  for (const auto *field : {"kind", "points", "width"})
    if (!object.count(field)) invalid("Selection geometry is incomplete");
  const auto &kind = text_value(object.at("kind"));
  const auto &points = array_value(object.at("points"));
  const auto width = number_value(object.at("width"));
  if (width <= 0) invalid("Selection width must be positive");
  if (candidate && points.size() > limits.max_candidate_vertices)
    invalid("Selection candidate exceeds vertex limit");
  std::array<double, 2> low{std::numeric_limits<double>::max(), std::numeric_limits<double>::max()},
                        high{-std::numeric_limits<double>::max(), -std::numeric_limits<double>::max()};
  for (const auto &point_value : points) {
    const auto &point = array_value(point_value);
    if (point.size() != 2) invalid("Selection point requires two coordinates");
    for (int axis = 0; axis < 2; ++axis) {
      const auto coordinate = number_value(point[axis]);
      low[axis] = std::min(low[axis], coordinate); high[axis] = std::max(high[axis], coordinate);
    }
  }
  if (candidate) {
    if (kind != "rectangle" && kind != "ellipse" && kind != "polygon" && kind != "freehand" && kind != "rounded_rectangle")
      invalid("Unsupported selection creation candidate kind");
    if (((kind == "rectangle" || kind == "ellipse" || kind == "rounded_rectangle") && points.size() != 2) ||
        ((kind == "polygon" || kind == "freehand") && points.size() < 3))
      invalid("Incomplete selection creation candidate");
    for (int axis = 0; axis < 2; ++axis)
      if (!std::isfinite(high[axis] - low[axis]) || high[axis] <= low[axis])
        invalid("Selection candidate has no complete XY extent");
  }
  Wire wire{&object, &points, &kind, width};
  wire.stored_width=shape.x;wire.stored_height=shape.y;
  if(structured) {
    if(kind=="region")fields(object,{"kind","points","width","attributes","shape","mask","origin"});
    else if(kind=="mask")fields(object,{"kind","points","width","attributes","shape","mask"});
    else fields(object,{"kind","points","width","attributes"});
    wire.attributes=&object_value(object.at("attributes"));
  } else if(kind=="region")invalid("Regional selection requires command version 2");
  if (kind == "mask" || kind=="region") {
    if (!object.count("shape") || !object.count("mask")) invalid("Mask storage is incomplete");
    const auto &dimensions = array_value(object.at("shape"));
    if(dimensions.size()!=2)invalid("Selection mask requires two dimensions");
    wire.stored_height=index_value(dimensions[0],true);wire.stored_width=index_value(dimensions[1],true);
    if(kind=="mask" && (wire.stored_height!=shape.y || wire.stored_width!=shape.x))invalid("Selection mask shape mismatch");
    if(kind=="region") {
      if(!points.empty())invalid("Regional mask cannot contain vector points");
      const auto &origin=array_value(object.at("origin"));
      if(origin.size()!=2)invalid("Regional origin requires two integers");
      wire.origin={signed_index_value(origin[0]),signed_index_value(origin[1])};
    }
    const auto stored=mul(wire.stored_width,wire.stored_height);
    if(stored>limits.max_pixels)invalid("Regional mask exceeds pixel limit");
    wire.mask = &text_value(object.at("mask"));
    if (wire.mask->size() != base64_size(packed_size(stored)))
      invalid("Selection mask encoded size mismatch");
  }
  return wire;
}
std::vector<unsigned char> decode_mask(const std::string &encoded, std::size_t expected) {
  std::vector<unsigned char> bytes; bytes.reserve(expected);
  for (std::size_t first = 0; first < encoded.size();) {
    kernel_checkpoint();
    const auto count = std::min<std::size_t>(4096, encoded.size() - first);
    // Padding is legal only in the final chunk. The existing V3 decoder
    // supplies strict alphabet/padding/tail validation for each whole quartet.
    if (first + count < encoded.size())
      for (std::size_t i = first; i < first + count; ++i)
        if (encoded[i] == '=') invalid("Nonfinal selection base64 padding");
    const auto chunk = detail::session_metadata::decode_base64(encoded.substr(first, count));
    if (chunk.size() > expected - bytes.size()) invalid("Selection mask decoded size mismatch");
    bytes.insert(bytes.end(), chunk.begin(), chunk.end());
    first += count;
  }
  if (bytes.size() != expected) invalid("Selection mask decoded size mismatch");
  kernel_checkpoint(); return bytes;
}
Selection decode_selection(const Wire &wire, DatasetShape shape) {
  kernel_checkpoint();
  auto selection=[&] {
    if(!wire.mask)return Selection(*wire.kind);
    auto bytes=decode_mask(*wire.mask,packed_size(mul(wire.stored_width,wire.stored_height)));
    return *wire.kind=="region"?Selection::from_packed_region(wire.stored_width,wire.stored_height,std::move(bytes),wire.origin)
        :Selection::from_packed_mask(wire.stored_width,wire.stored_height,std::move(bytes));
  }();
  selection.width = wire.width;
  selection.points.reserve(wire.points->size());
  for (const auto &point_value : *wire.points) {
    kernel_checkpoint_periodic();
    const auto &point = array_value(point_value);
    selection.points.push_back({number_value(point[0]), number_value(point[1])});
  }
  if(wire.attributes)selection.attributes=*wire.attributes;
  else for (const auto &[key, value] : *wire.object) {
    kernel_checkpoint_periodic();
    if (key != "kind" && key != "points" && key != "width" &&
        (!wire.mask || (key != "shape" && key != "mask")))
      selection.attributes.emplace(key, value);
  }
  selection.validate(shape.x, shape.y);
  kernel_checkpoint(); return selection;
}

struct RecordView {
  Source source;
  SelectionCreationOperation operation;
  std::optional<Wire> base;
  Wire candidate, result;
  std::size_t bytes;
  std::size_t validation_peak;
};
std::size_t wire_geometry_size(const Wire &wire, DatasetShape shape,
                               const SelectionCreationLimits &limits) {
  Charge charge{limits};
  charge.take(sizeof(Selection)); charge.text(*wire.kind);
  charge.take(mul(wire.points->size(), sizeof(std::array<double, 2>)));
  charge.take(sizeof(MetadataObject));
  if(wire.attributes)charge.object(*wire.attributes);
  else for (const auto &[key, value] : *wire.object)
    if (key != "kind" && key != "points" && key != "width" &&
        (!wire.mask || (key != "shape" && key != "mask"))) {
      charge.text(key); charge.value(value);
    }
  if (wire.mask) charge.take(add(node_bytes, packed_size(mul(wire.stored_width,wire.stored_height))));
  return charge.bytes;
}
std::size_t wire_encoded_size(const Wire &wire, DatasetShape shape,
                              const SelectionCreationLimits &limits) {
  Charge attributes{limits}; attributes.take(sizeof(MetadataObject));
  for (const auto &[key, value] : *wire.object)
    if (key != "kind" && key != "points" && key != "width" &&
        (!wire.mask || (key != "shape" && key != "mask"))) {
      attributes.text(key); attributes.value(value);
    }
  auto bytes = add(attributes.bytes, 12 * node_bytes);
  bytes = add(bytes, mul(wire.points->size(), 6 * node_bytes));
  bytes = add(bytes, wire.kind->size());
  if (wire.mask) bytes = add(bytes, add(8 * node_bytes,
      base64_size(packed_size(mul(shape.x, shape.y)))));
  budget(bytes, limits); return bytes;
}
RecordView inspect_record(const MetadataObject &record,
                           const SelectionCreationLimits &limits) {
  // The complete borrowed metadata is bounded and finite before decoder copies,
  // kernels or callbacks. The charge includes repeated immutable subtrees.
  const auto bytes = metadata_size(record, limits);
  fields(record, {"schema", "version", "source", "position", "operation", "base", "candidate", "result"});
  const auto version = uint_value(record.at("version"));
  if (text_value(record.at("schema")) != schema || (version != 1 && version != 2))
    invalid("Unsupported selection creation command schema/version");
  auto source = read_source(object_value(record.at("source")), object_value(record.at("position")));
  const auto pixels = source_admission(source, limits);
  const auto operation = read_operation(record.at("operation"));
  std::optional<Wire> base;
  if (!std::holds_alternative<std::monostate>(record.at("base")))
    base = inspect_wire(record.at("base"), source.shape, false, limits);
  auto candidate = inspect_wire(record.at("candidate"), source.shape, true, limits);
  auto result = inspect_wire(record.at("result"), source.shape, false, limits);
  const bool rounded = rounded_kind(*candidate.kind) || rounded_kind(*result.kind) ||
                       (base && rounded_kind(*base->kind));
  if (version != (rounded ? 2u : 1u))
    invalid("Selection creation version does not match rounded geometry");
  if (operation != SelectionCreationOperation::Replace && !base && operation != SelectionCreationOperation::Add)
    invalid("Selection combination requires an original base");
  if (operation != SelectionCreationOperation::Replace && base) {
    const auto &kind = *base->kind;
    if (kind != "rectangle" && kind != "ellipse" && kind != "polygon" && kind != "freehand" &&
        kind != "rotated_rectangle" && kind != "rotated_ellipse" && kind != "mask" && !rounded_kind(kind))
      invalid("Selection combination requires an original area base");
  }
  // The caller's wire tree is borrowed. Count decoded native geometries rather
  // than multiplying its much larger points-array encoding. Keep all decoded
  // operands/result plus recomputation's copied operands and raster alive.
  const auto candidate_bytes = wire_geometry_size(candidate, source.shape, limits);
  const auto base_bytes = base ? wire_geometry_size(*base, source.shape, limits) : 0;
  const auto result_bytes = wire_geometry_size(result, source.shape, limits);
  const auto decoded_bytes = add(3 * sizeof(Selection) + sizeof(Source),
                                  add(base_bytes, add(candidate_bytes, result_bytes)));
  const auto vertices = std::max<std::size_t>(4, std::max(candidate.points->size(),
                                      base ? base->points->size() : 0));
  auto scratch = std::max(codec_scratch, candidate_bytes);
  if (operation != SelectionCreationOperation::Replace && base) {
    scratch = add(2 * sizeof(Selection) + 2 * sizeof(const Selection *),
                    add(base_bytes, candidate_bytes));
    scratch = add(scratch, add(mul(pixels, sizeof(double)),
                                add(packed_size(pixels), mul(vertices, 3 * sizeof(double)))));
    scratch = add(scratch, rounded_scratch(pixels,
        rounded_kind(*candidate.kind) || rounded_kind(*base->kind)));
    scratch = std::max(scratch, codec_scratch);
  }
  // Freeze the admitted wire tree before user code can replace its map entries.
  const auto peak = add(bytes, add(decoded_bytes, scratch));
  budget(peak, limits);
  return {source, operation, std::move(base), candidate, result, bytes, peak};
}
SelectionCreationEstimate estimate_record_command(
    const RecordView &view, const DocumentSnapshot &snapshot,
    const SelectionCreationLimits &limits) {
  SelectionCreationEstimate estimate;
  estimate.pixels = source_admission(view.source, limits);
  estimate.candidate_vertices = view.candidate.points->size();
  const auto candidate = wire_geometry_size(view.candidate, view.source.shape, limits);
  const auto base = view.base ? wire_geometry_size(*view.base, view.source.shape, limits)
                             : sizeof(std::optional<Selection>);
  const auto result = wire_geometry_size(view.result, view.source.shape, limits);
  auto record = add(96 * node_bytes, add(view.base
      ? wire_encoded_size(*view.base, view.source.shape, limits) : node_bytes,
      add(wire_encoded_size(view.candidate, view.source.shape, limits),
          wire_encoded_size(view.result, view.source.shape, limits))));
  record = std::max(record, view.bytes);
  estimate.retained_bytes = add(sizeof(ResolvedSelectionCreationCommand),
      add(candidate, add(base, add(result, record))));
  estimate.retained_bytes = add(estimate.retained_bytes,
                                 display_size(snapshot.display(), limits));
  auto resolve = codec_scratch;
  if (view.operation != SelectionCreationOperation::Replace && view.base) {
    const auto vertices = std::max<std::size_t>(4,
        std::max(view.candidate.points->size(), view.base->points->size()));
    resolve = add(resolve, add(2 * sizeof(Selection) + 2 * sizeof(const Selection *),
        add(base, add(candidate, add(mul(estimate.pixels, sizeof(double)),
            add(packed_size(estimate.pixels), mul(vertices, 3 * sizeof(double))))))));
    resolve = add(resolve, rounded_scratch(estimate.pixels,
        rounded_kind(*view.candidate.kind) || rounded_kind(*view.base->kind)));
  }
  const auto prepare = add(estimate.retained_bytes,
      add(sizeof(DocumentTransaction), add(data_copy_size(snapshot, limits),
          add(optional_size(snapshot.data().selection, limits), result))));
  estimate.working_bytes = std::max(view.validation_peak,
      add(estimate.retained_bytes, std::max(resolve, prepare)));
  budget(estimate.working_bytes, limits);
  return estimate;
}
struct Decoded {
  Source source;
  SelectionCreationRequest request;
  std::optional<Selection> base;
  Selection result;
  std::size_t record_bytes;
};
Decoded decode_record(const RecordView &view,
                       const SelectionCreationLimits &limits) {
  kernel_checkpoint();
  std::optional<Selection> base;
  if (view.base) base = decode_selection(*view.base, view.source.shape);
  SelectionCreationRequest request{view.operation, decode_selection(view.candidate, view.source.shape)};
  auto result = decode_selection(view.result, view.source.shape);
  operands_admission(view.source.shape, base, request, limits);
  const auto recomputed = algebra(view.source.shape, base, request);
  if (!selection_equal(recomputed, result))
    invalid("Selection command result differs from its original operation");
  kernel_checkpoint();
  return {view.source, std::move(request), std::move(base), std::move(result), view.bytes};
}
// Preserve original semantic-validation priority even when a later new-source
// admission already predicts refusal. That refusal must invoke no user callback;
// cancellation still interrupts the bounded original check. On the admitted
// path all original validation retains the caller's checkpoint/stage callbacks.
[[noreturn]] void reject_after_original(const RecordView &view,
                           const SelectionCreationLimits &limits,
                           const ExecutionControl &control,
                           std::exception_ptr rejection) {
  ExecutionControl admission; admission.cancellation = control.cancellation;
  ExecutionScope scope(admission);
  (void)decode_record(view, limits);
  std::rethrow_exception(rejection);
}
void require_binding(const Source &expected, const Source &current) {
  if (expected.id != current.id) invalid("Selection command differs from its source owner");
  if (!current.revisions.matches(expected.revisions, Change::Pixels | Change::Metadata))
    throw RevisionConflict();
  if (expected.shape.dimensions() != current.shape.dimensions() ||
      expected.type != current.type || expected.policy != current.policy)
    invalid("Selection command differs from its source metadata");
}
} // namespace

ResolvedSelectionCreationCommand::ResolvedSelectionCreationCommand(
    DocumentSnapshot source, SelectionCreationRequest request,
    std::optional<Selection> base, Selection result, MetadataObject record,
    SelectionCreationEstimate estimate, SelectionCreationLimits limits)
    : source_(std::move(source)), request_(std::move(request)), base_(std::move(base)),
      result_(std::move(result)), record_(std::move(record)), estimate_(estimate), limits_(limits),
      changes_(!source_.data().selection || !selection_equal(*source_.data().selection, result_)) {}

SelectionCreationEstimate estimate_selection_creation(
    const DocumentSnapshot &source, const SelectionCreationRequest &request,
    const SelectionCreationLimits &limits) {
  return estimate_parts(source_info(source), source.data().selection, request, limits, &source);
}
Selection compose_selection_creation(const DocumentSnapshot &source_input,
    const SelectionCreationRequest &request_input, const SelectionCreationLimits &limits_input,
    const ExecutionControl &control_input) {
  (void)estimate_selection_creation(source_input, request_input, limits_input);
  const auto source = source_input;
  const auto request = request_input;
  const auto control = control_input;
  ExecutionScope scope(control); start_progress();
  auto result = algebra(source.data().dataset.shape(), source.data().selection, request);
  finish_progress(); return result;
}
ResolvedSelectionCreationCommand resolve_selection_creation_command(
    const DocumentSnapshot &source_input, const SelectionCreationRequest &request_input,
    const SelectionCreationLimits &limits_input, const ExecutionControl &control_input) {
  const auto estimate = estimate_selection_creation(source_input, request_input, limits_input);
  auto source = source_input;
  auto request = request_input;
  const auto limits = limits_input;
  const auto control = control_input;
  ExecutionScope scope(control); start_progress();
  auto result = algebra(source.data().dataset.shape(), source.data().selection, request);
  auto record = encode_record(source_info(source), source.data().selection, request, result);
  auto base = source.data().selection;
  ResolvedSelectionCreationCommand command{std::move(source), std::move(request), std::move(base),
      std::move(result), std::move(record), estimate, limits};
  finish_progress(); return command;
}
void validate_selection_creation_command_record(const MetadataObject &record_input,
    const SelectionCreationLimits &limits_input, const ExecutionControl &control_input) {
  (void)inspect_record(record_input, limits_input);
  const auto record = record_input;
  const auto limits = limits_input;
  const auto control = control_input;
  const auto view = inspect_record(record, limits);
  ExecutionScope scope(control); start_progress();
  (void)decode_record(view, limits);
  finish_progress();
}
ResolvedSelectionCreationCommand selection_creation_command_from_record(
    const DocumentSnapshot &source_input, const MetadataObject &record_input,
    const SelectionCreationLimits &limits_input, const ExecutionControl &control_input) {
  (void)inspect_record(record_input, limits_input);
  auto source = source_input;
  auto record = record_input;
  const auto limits = limits_input;
  const auto control = control_input;
  const auto view = inspect_record(record, limits);
  SelectionCreationEstimate estimate;
  try { estimate = estimate_record_command(view, source, limits); }
  catch (...) { reject_after_original(view, limits, control, std::current_exception()); }
  ExecutionScope scope(control); start_progress();
  auto decoded = decode_record(view, limits);
  require_binding(decoded.source, source_info(source));
  ResolvedSelectionCreationCommand command{std::move(source), std::move(decoded.request),
      std::move(decoded.base), std::move(decoded.result), std::move(record), estimate, limits};
  finish_progress(); return command;
}
ResolvedSelectionCreationCommand rebind_selection_creation_command(
    const DocumentSnapshot &source_input, const MetadataObject &record_input,
    const SelectionCreationRequest &request_input, const SelectionCreationLimits &limits_input,
    const ExecutionControl &control_input) {
  (void)inspect_record(record_input, limits_input);
  auto source = source_input;
  auto record = record_input;
  const auto limits = limits_input;
  const auto control = control_input;
  const auto view = inspect_record(record, limits);
  SelectionCreationEstimate estimate;
  try {
    estimate = estimate_selection_creation(source, request_input, limits);
    // Original validation and the frozen replacement request coexist before
    // the new equation is evaluated. Bound that peak independently of the
    // replacement command's (possibly much smaller) algebra/retention estimate.
    const auto original_peak = add(view.validation_peak,
        add(geometry_size(request_input.candidate, limits),
            display_size(source.display(), limits)));
    estimate.working_bytes = std::max(original_peak,
        add(estimate.working_bytes, view.bytes));
    budget(estimate.working_bytes, limits);
  }
  catch (...) { reject_after_original(view, limits, control, std::current_exception()); }
  auto request = request_input;
  ExecutionScope scope(control); start_progress();
  (void)decode_record(view, limits);
  auto result = algebra(source.data().dataset.shape(), source.data().selection, request);
  auto rebound_record = encode_record(source_info(source), source.data().selection, request, result);
  auto base = source.data().selection;
  ResolvedSelectionCreationCommand command{std::move(source), std::move(request), std::move(base),
      std::move(result), std::move(rebound_record), estimate, limits};
  finish_progress(); return command;
}
DocumentTransaction prepare_selection_creation(
    const ResolvedSelectionCreationCommand &command_input, const ExecutionControl &control_input,
    std::size_t execution_max_working_bytes) {
  auto limits = command_input.limits_;
  limits.max_working_bytes = std::min(limits.max_working_bytes, execution_max_working_bytes);
  budget(command_input.estimate_.working_bytes, limits);
  const auto command = command_input;
  const auto control = control_input;
  ExecutionScope scope(control); start_progress();
  auto transaction = command.source_.begin_edit(Change::Data);
  kernel_checkpoint();
  if (command.changes_) transaction.select(command.result_);
  finish_progress(); return transaction;
}
namespace {
constexpr const char *enlargement_schema="imagejxx.selection-enlargement-command";
SelectionEditLimits enlargement_geometry_limits(const SelectionCreationLimits &limits){
  return {limits.max_pixels,limits.max_candidate_vertices,limits.max_working_bytes};
}
const char *enlargement_units(SelectionEnlargementUnits units){
  if(units==SelectionEnlargementUnits::Pixels)return "pixels";
  if(units==SelectionEnlargementUnits::Calibrated)return "calibrated";
  invalid("Unknown selection enlargement units");
}
// Count the transport root too, including typed string arrays that native
// MetadataValue otherwise treats as a scalar alternative. Every V2 record must
// round-trip through the same 128-container Python/JSON boundary.
void enlargement_wire_depth(const MetadataValue &value,unsigned depth){
  if(std::get_if<std::vector<std::string>>(&value)) {
    if(depth>=128)invalid("V2 command metadata nesting exceeds 128 containers");
    return;
  }
  const auto node=std::get_if<std::shared_ptr<const MetadataTree>>(&value);
  if(!node)return;
  if(!*node)invalid("Metadata container must not be null");
  if(depth>=128)invalid("V2 command metadata nesting exceeds 128 containers");
  if(const auto object=std::get_if<MetadataObject>(&(*node)->value))
    for(const auto &[key,child]:*object)enlargement_wire_depth(child,depth+1);
  else for(const auto &child:std::get<MetadataArray>((*node)->value))enlargement_wire_depth(child,depth+1);
}
bool enlargement_full(const SelectionEnlargementRequest &request){
  if(request.geometry==SelectionEnlargementGeometry::CanvasV1)return false;
  if(request.geometry==SelectionEnlargementGeometry::FullRegionV2)return true;
  invalid("Unknown selection enlargement geometry mode");
}
double enlargement_pixels(const SelectionEnlargementRequest &request,double spacing){
  (void)enlargement_units(request.units);require_finite(request.distance,"Enlargement distance");
  require_finite(spacing,"Enlargement pixel width");if(spacing<=0)invalid("Enlargement pixel width must be positive");
  const auto pixels=request.units==SelectionEnlargementUnits::Pixels?request.distance:request.distance/spacing;
  require_finite(pixels,"Enlargement pixel distance");return pixels;
}
ExecutionControl enlargement_kernel_control(const ExecutionControl &control){
  auto kernel=control;kernel.progress={};return kernel;
}
SelectionEnlargementCommandEstimate enlargement_estimate(const Source &source,const Selection &base,
    const SelectionEnlargementRequest &request,double spacing,const std::string &unit,
    const SelectionCreationLimits &limits,const DocumentSnapshot *snapshot=nullptr,
    std::size_t original_record_bytes=0,const ExecutionControl &control={}){
  const auto pixels=source_admission(source,limits);
  const auto full=enlargement_full(request);
  if(full)for(const auto &[key,value]:base.attributes)enlargement_wire_depth(value,3);
  if(!area(base) && !(full && base.is_region()))invalid("Selection enlargement requires an area selection");
  const auto base_bytes=geometry_size(base,limits),attributes=metadata_size(base.attributes,limits);
  const auto pixel_distance=enlargement_pixels(request,spacing);
  const auto geometry=full?estimate_selection_region_enlargement_geometry(base,source.shape.x,source.shape.y,
      pixel_distance,enlargement_geometry_limits(limits),control):estimate_selection_enlargement_geometry(base,source.shape.x,source.shape.y,
      pixel_distance,enlargement_geometry_limits(limits),control);
  const auto result_pixels=full?std::max({pixels,geometry.padded_pixels,mul(base.mask_width(),base.mask_height())}):pixels;
  const auto result_bytes=std::max(base_bytes,add(sizeof(Selection)+32*node_bytes,add(attributes,packed_size(result_pixels))));
  const auto result_wire=std::max(wire_size(base,limits),add(64*node_bytes,add(attributes,base64_size(packed_size(result_pixels)))));
  const auto record_bytes=std::max(original_record_bytes,add(128*node_bytes,add(unit.size(),add(wire_size(base,limits),result_wire))));
  auto retained=add(sizeof(ResolvedSelectionEnlargementCommand),add(mul(3,record_bytes),add(mul(2,base_bytes),mul(2,result_bytes))));
  auto working=add(retained,add(geometry.working_bytes,4*codec_scratch));
  if(snapshot){const auto display=display_size(snapshot->display(),limits);retained=add(retained,display);working=add(working,add(display,mul(2,data_copy_size(*snapshot,limits))));}
  budget(working,limits);return {pixel_distance,retained,working};
}
Selection enlargement_result(const Selection &base,DatasetShape shape,double pixels,
    const SelectionCreationLimits &limits,const ExecutionControl &control,bool full=false){
  // The geometry service has its own coarse progress pair. Forward its kernel
  // callbacks once while suppressing duplicate outer coarse/forwarded callbacks.
  struct ChildDispatch {
    ExecutionScope *scope=nullptr;bool previous=false;
    explicit ChildDispatch(const ExecutionControl &control){
      if(current_execution_scope && &current_execution_scope->control==&control){scope=current_execution_scope;previous=scope->dispatching_callback;scope->dispatching_callback=true;}
    }
    ~ChildDispatch(){if(scope)scope->dispatching_callback=previous;}
  } dispatch(control);
  auto kernel=enlargement_kernel_control(control);
  if(full) {
    if(pixels==0)return base;
    const SelectionRegion original(base,shape.x,shape.y,{0,0},enlargement_geometry_limits(limits));
    const auto result=original.enlarged(pixels,enlargement_geometry_limits(limits),kernel);
    if(result.origin()==original.origin() && result.shape()==original.shape() &&
        result.local_selection()==original.local_selection())return base;
    return result.as_selection();
  }
  SelectionEditor editor(base,shape.x,shape.y,enlargement_geometry_limits(limits),kernel);
  return editor.enlarged(pixels,enlargement_geometry_limits(limits),kernel);
}
MetadataObject enlargement_record(const DocumentSnapshot &snapshot,const SelectionEnlargementRequest &request,const Selection &result){
  const auto source=source_info(snapshot);MetadataArray dimensions;
  for(int extent:source.shape.dimensions())dimensions.emplace_back(std::int64_t(extent));
  MetadataObject binding{{"id",source.id},{"pixels_revision",source.revisions.pixels},{"metadata_revision",source.revisions.metadata},
      {"selection_revision",source.revisions.selection},{"shape",metadata_array(std::move(dimensions))},
      {"type",std::string(type_name(source.type))},{"sample_policy",std::string(policy_name(source.policy))}};
  const auto &calibration=snapshot.data().dataset.calibration();
  const auto full=enlargement_full(request);
  return {{"schema",std::string(enlargement_schema)},{"version",std::int64_t(full?2:1)},{"source",metadata_object(std::move(binding))},
      {"position",metadata_object({{"t",std::int64_t(source.position.t)},{"c",std::int64_t(source.position.c)},{"z",std::int64_t(source.position.z)}})},
      {"distance",request.distance},{"units",std::string(enlargement_units(request.units))},
      {"pixel_width",calibration.spacing[0]},{"unit",calibration.unit},
      {"base",encode_selection(*snapshot.data().selection,full)},{"result",encode_selection(result,full)}};
}
struct EnlargementDecoded {
  Source source;SelectionEnlargementRequest request;double spacing;std::string unit;
  Selection base,result;MetadataObject record;SelectionEnlargementCommandEstimate estimate;
};
EnlargementDecoded inspect_enlargement(const MetadataObject &record,const SelectionCreationLimits &limits,const ExecutionControl &control){
  const auto bytes=metadata_size(record,limits);
  fields(record,{"schema","version","source","position","distance","units","pixel_width","unit","base","result"});
  const auto version=uint_value(record.at("version"));
  if(text_value(record.at("schema"))!=enlargement_schema || (version!=1 && version!=2))
    invalid("Unsupported selection enlargement schema/version");
  const bool full=version==2;
  if(full)for(const auto &[key,value]:record)enlargement_wire_depth(value,1);
  const auto source=read_source(object_value(record.at("source")),object_value(record.at("position")));
  (void)source_admission(source,limits);
  const auto &units=text_value(record.at("units"));SelectionEnlargementUnits mode;
  if(units=="pixels")mode=SelectionEnlargementUnits::Pixels;
  else if(units=="calibrated")mode=SelectionEnlargementUnits::Calibrated;
  else invalid("Unknown recorded enlargement units");
  const SelectionEnlargementRequest request{number_value(record.at("distance")),mode,
      full?SelectionEnlargementGeometry::FullRegionV2:SelectionEnlargementGeometry::CanvasV1};
  const auto spacing=number_value(record.at("pixel_width"));(void)enlargement_pixels(request,spacing);
  const auto &unit=text_value(record.at("unit"));
  const auto base_wire=inspect_wire(record.at("base"),source.shape,false,limits,full),result_wire=inspect_wire(record.at("result"),source.shape,false,limits,full);
  if(base_wire.points->size()>limits.max_candidate_vertices || result_wire.points->size()>limits.max_candidate_vertices)
    invalid("Enlargement record exceeds vertex limit");
  // Borrowed record charge precedes owned decoding/copying, callbacks or rasters.
  const auto decoded_bytes=add(mul(6,bytes),add(mul(6,wire_geometry_size(base_wire,source.shape,limits)),mul(4,wire_geometry_size(result_wire,source.shape,limits))));
  budget(decoded_bytes,limits);ExecutionControl admission;admission.cancellation=control.cancellation;ExecutionScope scope(admission);
  auto base=decode_selection(base_wire,source.shape),result=decode_selection(result_wire,source.shape);
  if((!area(base) && !(full && base.is_region())) || (!area(result) && !(full && result.is_region())))invalid("Enlargement record requires area geometry");
  auto estimate=enlargement_estimate(source,base,request,spacing,unit,limits,nullptr,bytes,admission);
  estimate.working_bytes=add(estimate.working_bytes,decoded_bytes);budget(estimate.working_bytes,limits);
  return {source,request,spacing,unit,std::move(base),std::move(result),record,estimate};
}
void enlargement_equation(const EnlargementDecoded &decoded,const SelectionCreationLimits &limits,const ExecutionControl &control){
  const auto result=enlargement_result(decoded.base,decoded.source.shape,decoded.estimate.pixel_distance,limits,control,enlargement_full(decoded.request));
  if(!selection_equal(result,decoded.result))invalid("Enlargement result differs from its original operation");
}
SelectionEnlargementCommandEstimate enlargement_publication_estimate(const EnlargementDecoded &decoded,
    const DocumentSnapshot &source,const SelectionCreationLimits &limits){
  auto estimate=decoded.estimate;const auto display=display_size(source.display(),limits);estimate.retained_bytes=add(estimate.retained_bytes,display);estimate.working_bytes=add(estimate.working_bytes,add(display,mul(2,data_copy_size(source,limits))));
  budget(estimate.working_bytes,limits);return estimate;
}
[[noreturn]] void enlargement_reject_after_original(const EnlargementDecoded &decoded,const SelectionCreationLimits &limits,
    const ExecutionControl &control,std::exception_ptr exception){
  ExecutionControl admission;admission.cancellation=control.cancellation;ExecutionScope scope(admission);
  enlargement_equation(decoded,limits,admission);std::rethrow_exception(exception);
}
}
ResolvedSelectionEnlargementCommand::ResolvedSelectionEnlargementCommand(DocumentSnapshot source,
    SelectionEnlargementRequest request,Selection result,MetadataObject record,
    SelectionEnlargementCommandEstimate estimate,SelectionCreationLimits limits)
    :source_(std::move(source)),request_(request),result_(std::move(result)),record_(std::move(record)),estimate_(estimate),limits_(limits),
     changes_(!source_.data().selection || !selection_equal(*source_.data().selection,result_)){}
SelectionEnlargementCommandEstimate estimate_selection_enlargement_command(const DocumentSnapshot &source,
    const SelectionEnlargementRequest &request,const SelectionCreationLimits &limits){
  if(!source.data().selection)invalid("Draw an area selection first");
  const auto &calibration=source.data().dataset.calibration();
  return enlargement_estimate(source_info(source),*source.data().selection,request,calibration.spacing[0],calibration.unit,limits,&source);
}
ResolvedSelectionEnlargementCommand resolve_selection_enlargement_command(const DocumentSnapshot &source,
    const SelectionEnlargementRequest &request,const SelectionCreationLimits &limits,const ExecutionControl &control){
  const auto frozen_request=request;const auto frozen_limits=limits;const auto frozen_control=control;
  const auto estimate=estimate_selection_enlargement_command(source,frozen_request,frozen_limits);
  const auto frozen_source=source;
  ExecutionScope scope(frozen_control);start_progress();
  auto result=enlargement_result(*frozen_source.data().selection,frozen_source.data().dataset.shape(),estimate.pixel_distance,frozen_limits,frozen_control,enlargement_full(frozen_request));
  auto record=enlargement_record(frozen_source,frozen_request,result);
  ResolvedSelectionEnlargementCommand command(frozen_source,frozen_request,std::move(result),std::move(record),estimate,frozen_limits);
  finish_progress();return command;
}
void validate_selection_enlargement_command_record(const MetadataObject &record,const SelectionCreationLimits &limits,const ExecutionControl &control){
  const auto frozen_limits=limits;const auto frozen_control=control;
  const auto decoded=inspect_enlargement(record,frozen_limits,frozen_control);
  ExecutionScope scope(frozen_control);start_progress();enlargement_equation(decoded,frozen_limits,frozen_control);finish_progress();
}
ResolvedSelectionEnlargementCommand selection_enlargement_command_from_record(const DocumentSnapshot &source,
    const MetadataObject &record,const SelectionCreationLimits &limits,const ExecutionControl &control){
  const auto frozen_limits=limits;const auto frozen_control=control;auto decoded=inspect_enlargement(record,frozen_limits,frozen_control);
  SelectionEnlargementCommandEstimate estimate;
  try{estimate=enlargement_publication_estimate(decoded,source,frozen_limits);}
  catch(...){enlargement_reject_after_original(decoded,frozen_limits,frozen_control,std::current_exception());}
  const auto frozen_source=source;
  ExecutionScope scope(frozen_control);start_progress();enlargement_equation(decoded,frozen_limits,frozen_control);
  require_binding(decoded.source,source_info(frozen_source));
  const auto current=source_info(frozen_source);const auto &cal=frozen_source.data().dataset.calibration();
  if(current.position.t!=decoded.source.position.t || current.position.c!=decoded.source.position.c || current.position.z!=decoded.source.position.z)
    invalid("Enlargement replay requires its captured T/C/Z; use explicit rebind");
  if(cal.spacing[0]!=decoded.spacing || cal.unit!=decoded.unit)invalid("Enlargement replay calibration differs");
  ResolvedSelectionEnlargementCommand command(frozen_source,decoded.request,std::move(decoded.result),std::move(decoded.record),estimate,frozen_limits);
  finish_progress();return command;
}
ResolvedSelectionEnlargementCommand rebind_selection_enlargement_command(const DocumentSnapshot &source,
    const MetadataObject &record,const SelectionEnlargementRequest &request,const SelectionCreationLimits &limits,const ExecutionControl &control){
  const auto frozen_request=request;const auto frozen_limits=limits;const auto frozen_control=control;
  auto decoded=inspect_enlargement(record,frozen_limits,frozen_control);SelectionEnlargementCommandEstimate estimate;
  try{estimate=estimate_selection_enlargement_command(source,frozen_request,frozen_limits);
    estimate.working_bytes=add(estimate.working_bytes,decoded.estimate.working_bytes);budget(estimate.working_bytes,frozen_limits);}
  catch(...){enlargement_reject_after_original(decoded,frozen_limits,frozen_control,std::current_exception());}
  const auto frozen_source=source;
  ExecutionScope scope(frozen_control);start_progress();enlargement_equation(decoded,frozen_limits,frozen_control);
  auto result=enlargement_result(*frozen_source.data().selection,frozen_source.data().dataset.shape(),estimate.pixel_distance,frozen_limits,frozen_control,enlargement_full(frozen_request));
  auto rebound=enlargement_record(frozen_source,frozen_request,result);
  ResolvedSelectionEnlargementCommand command(frozen_source,frozen_request,std::move(result),std::move(rebound),estimate,frozen_limits);
  finish_progress();return command;
}
DocumentTransaction prepare_selection_enlargement(const ResolvedSelectionEnlargementCommand &command,
    const ExecutionControl &control,std::size_t execution_max_working_bytes){
  auto limits=command.limits_;limits.max_working_bytes=std::min(limits.max_working_bytes,execution_max_working_bytes);
  budget(command.estimate_.working_bytes,limits);
  const auto frozen=command;const auto frozen_control=control;ExecutionScope scope(frozen_control);start_progress();
  auto transaction=frozen.source_.begin_edit(Change::All);
  if(frozen.changes_)transaction.select(frozen.result_);
  finish_progress();return transaction;
}

} // namespace imagejxx
