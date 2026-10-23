#include <imagejxx/auto_contrast.hpp>
#include <imagejxx/roi_style.hpp>

#include "channel_display_command_internal.hpp"
#include "kernel_control.hpp"
#include "session_metadata.hpp"
#include "regional_geometry.hpp"
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <utility>

namespace imagejxx {
namespace detail {
struct AutoContrastStorage {
  static constexpr std::size_t bytes = sizeof(ResolvedAutoContrastCommand::Data);
};
} // namespace detail
namespace {
constexpr auto command_bytes = sizeof(ResolvedAutoContrastCommand) +
    detail::AutoContrastStorage::bytes;
constexpr const char *schema = "imagejxx.auto-contrast-command";
constexpr std::size_t node_bytes =
    sizeof(MetadataObject::value_type) + 8 * sizeof(void *) > 160
        ? sizeof(MetadataObject::value_type) + 8 * sizeof(void *) : 160;
constexpr std::size_t codec_scratch = 16384;

[[noreturn]] void invalid(const char *message) { throw std::invalid_argument(message); }
std::size_t add(std::size_t a, std::size_t b) {
  if (b > std::numeric_limits<std::size_t>::max() - a)
    invalid("Auto contrast size overflow");
  return a + b;
}
std::size_t mul(std::size_t a, std::size_t b) {
  if (a && b > std::numeric_limits<std::size_t>::max() / a)
    invalid("Auto contrast size overflow");
  return a * b;
}
std::uint64_t count_add(std::uint64_t a, std::uint64_t b) {
  if (b > std::numeric_limits<std::uint64_t>::max() - a)
    invalid("Auto contrast count overflow");
  return a + b;
}
std::size_t packed_size(std::size_t count) { return add(count, 7) / 8; }
std::size_t base64_size(std::size_t count) { return mul(add(count, 2) / 3, 4); }
void limits_valid(const AutoContrastLimits &limits) {
  if (!limits.max_pixels || !limits.max_working_bytes ||
      !limits.max_record_bytes || !limits.max_selection_vertices)
    invalid("Auto contrast limits must be positive");
}
void working(std::size_t count, const AutoContrastLimits &limits) {
  if (count > limits.max_working_bytes)
    invalid("Auto contrast exceeds working memory limit");
}
void record_budget(std::size_t count, const AutoContrastLimits &limits) {
  working(count, limits);
  if (count > limits.max_record_bytes)
    invalid("Auto contrast exceeds record memory limit");
}

// Borrowed traversal: no tree copies, callbacks, decoder allocations or DAG
// expansion precede its depth and per-occurrence byte admission.
struct Charge {
  const AutoContrastLimits &limits;
  std::size_t bytes = 0;
  bool record = false;
  void take(std::size_t amount) {
    bytes = add(bytes, amount);
    if (record) record_budget(bytes, limits);
    else working(bytes, limits);
  }
  void text(const std::string &value) { take(add(value.size(), 1)); }
  void object(const MetadataObject &object, unsigned depth = 0) {
    if (depth >= 128) invalid("Auto contrast metadata nesting exceeds128");
    take(sizeof(MetadataObject));
    for (const auto &[key, entry] : object) {
      take(node_bytes); text(key); value(entry, depth + 1);
    }
  }
  void value(const MetadataValue &entry, unsigned depth) {
    take(sizeof(MetadataValue));
    if (const auto number = std::get_if<double>(&entry)) {
      if (!std::isfinite(*number)) invalid("Auto contrast metadata must be finite");
    } else if (const auto string = std::get_if<std::string>(&entry)) text(*string);
    else if (const auto strings = std::get_if<std::vector<std::string>>(&entry)) {
      take(mul(strings->size(), sizeof(std::string)));
      for (const auto &item : *strings) text(item);
    } else if (const auto tree = std::get_if<std::shared_ptr<const MetadataTree>>(&entry)) {
      if (!*tree) invalid("Auto contrast metadata container is null");
      if (depth >= 128) invalid("Auto contrast metadata nesting exceeds128");
      take(sizeof(MetadataTree));
      if (const auto array = std::get_if<MetadataArray>(&(*tree)->value)) {
        take(sizeof(MetadataArray)); take(mul(array->size(), sizeof(MetadataValue)));
        for (const auto &item : *array) value(item, depth + 1);
      } else object(std::get<MetadataObject>((*tree)->value), depth);
    }
  }
};
std::size_t metadata_size(const MetadataObject &object,
                          const AutoContrastLimits &limits, unsigned depth = 0,
                          bool record = false) {
  Charge charge{limits, 0, record};
  charge.object(object, depth); return charge.bytes;
}
void fields(const MetadataObject &object,
            std::initializer_list<const char *> expected) {
  if (object.size() != expected.size()) invalid("Unexpected Auto contrast record fields");
  for (const auto *key : expected)
    if (!object.count(key)) invalid("Missing Auto contrast record field");
}
const MetadataObject &object_value(const MetadataValue &value) {
  const auto tree = std::get_if<std::shared_ptr<const MetadataTree>>(&value);
  if (tree && *tree)
    if (const auto object = std::get_if<MetadataObject>(&(*tree)->value)) return *object;
  invalid("Auto contrast requires an object");
}
const MetadataArray &array_value(const MetadataValue &value) {
  if (const auto strings = std::get_if<std::vector<std::string>>(&value);
      strings && strings->empty()) {
    static const MetadataArray empty; return empty;
  }
  const auto tree = std::get_if<std::shared_ptr<const MetadataTree>>(&value);
  if (tree && *tree)
    if (const auto array = std::get_if<MetadataArray>(&(*tree)->value)) return *array;
  invalid("Auto contrast requires an array");
}
const std::string &text_value(const MetadataValue &value) {
  if (const auto text = std::get_if<std::string>(&value)) return *text;
  invalid("Auto contrast requires text");
}
const MetadataValue &required(const MetadataObject &object, const char *key) {
  const auto found = object.find(key);
  if (found == object.end()) invalid("Missing Auto contrast nested field");
  return found->second;
}
bool is_null(const MetadataValue &value) { return std::holds_alternative<std::monostate>(value); }
std::uint64_t uint_value(const MetadataValue &value) {
  if (const auto n = std::get_if<std::uint64_t>(&value)) return *n;
  if (const auto n = std::get_if<std::int64_t>(&value); n && *n >= 0)
    return static_cast<std::uint64_t>(*n);
  invalid("Auto contrast requires an exact nonnegative integer");
}
int index_value(const MetadataValue &value) {
  const auto n = uint_value(value);
  if (n > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
    invalid("Auto contrast integer exceeds int32");
  return static_cast<int>(n);
}
double number_value(const MetadataValue &value) {
  if (const auto floating = std::get_if<double>(&value)) {
    if (std::isfinite(*floating)) return *floating;
  } else if (const auto signed_integer = std::get_if<std::int64_t>(&value)) {
    const double converted = static_cast<double>(*signed_integer);
    if (converted >= -9223372036854775808.0 && converted < 9223372036854775808.0 &&
        static_cast<std::int64_t>(converted) == *signed_integer) return converted;
  } else if (const auto unsigned_integer = std::get_if<std::uint64_t>(&value)) {
    const double converted = static_cast<double>(*unsigned_integer);
    // MSVC long double is double. Range checks precede the exact reverse cast.
    if (converted < 18446744073709551616.0 &&
        static_cast<std::uint64_t>(converted) == *unsigned_integer) return converted;
  }
  invalid("Auto contrast requires a finite lossless number");
}
std::array<double, 2> pair_value(const MetadataValue &value) {
  const auto &array = array_value(value);
  if (array.size() != 2) invalid("Auto contrast range requires two endpoints");
  return {number_value(array[0]), number_value(array[1])};
}
void ordered(const std::array<double, 2> &pair) {
  if (!std::isfinite(pair[0]) || !std::isfinite(pair[1]) || pair[0] > pair[1])
    invalid("Auto contrast range must be finite and ordered");
}
bool divisor_valid(int value) {
  switch (value) {
  case 0: case 5000: case 2500: case 1250: case 625: case 312:
  case 156: case 78: case 39: case 19: case 9: return true;
  default: return false;
  }
}
bool bits_valid(int value) {
  return value == 0 || value == 8 || value == 10 || value == 12 ||
         value == 14 || value == 15 || value == 16;
}
const char *scope_name(AutoContrastScope scope) {
  switch (scope) {
  case AutoContrastScope::ActiveChannel: return "active_channel";
  case AutoContrastScope::CompositeAll: return "composite_all";
  }
  invalid("Unknown Auto contrast scope");
}
const char *roi_name(AutoContrastRoiPolicy policy) {
  switch (policy) {
  case AutoContrastRoiPolicy::ActiveSelection: return "active_selection";
  case AutoContrastRoiPolicy::WholePlane: return "whole_plane";
  }
  invalid("Unknown Auto contrast ROI policy");
}
const char *outcome_name(AutoContrastOutcome outcome) {
  switch (outcome) {
  case AutoContrastOutcome::HistogramBounds: return "histogram_bounds";
  case AutoContrastOutcome::AreaExtrema: return "area_extrema";
  case AutoContrastOutcome::ResetFallback: return "reset_fallback";
  }
  invalid("Unknown Auto contrast outcome");
}
void request_valid(const AutoContrastRequest &request) {
  if (!divisor_valid(request.previous_divisor)) invalid("Invalid Auto contrast divisor");
  if (!bits_valid(request.unsigned16_bits)) invalid("Invalid Auto contrast unsigned16 bits");
  (void)scope_name(request.scope); (void)roi_name(request.roi);
}
bool nonarea(const std::string &kind) {
  return kind == "line" || kind == "polyline" || kind == "angle" ||
         kind == "point" || kind == "multipoint";
}
void type_valid(DatasetShape shape, PixelType type, FloatingSamplePolicy policy) {
  shape.validate();
  if (shape.s != 1 || (type != PixelType::UInt8 && type != PixelType::UInt16 &&
                       type != PixelType::Float32))
    invalid("Auto contrast requires scalar UInt8/UInt16/Float32");
  if (policy != FloatingSamplePolicy::FiniteOnly &&
      policy != FloatingSamplePolicy::PreserveQuietNaNFloat32)
    invalid("Invalid Auto contrast sample policy");
  if (policy == FloatingSamplePolicy::PreserveQuietNaNFloat32 && type != PixelType::Float32)
    invalid("Auto contrast quiet NaN policy requires Float32");
}
std::size_t xy_size(DatasetShape shape) {
  return mul(static_cast<std::size_t>(shape.x), static_cast<std::size_t>(shape.y));
}
std::size_t selection_size(const Selection &selection, DatasetShape shape,
                           const AutoContrastLimits &limits) {
  if (selection.points.size() > limits.max_selection_vertices)
    invalid("Auto contrast selection exceeds vertex limit");
  if (!selection.is_mask() && (selection.mask_width() || selection.mask_height() ||
                                    !selection.packed_mask().empty()))
    invalid("Auto contrast selection hides mask storage");
  Charge charge{limits}; charge.take(sizeof(Selection)); charge.text(selection.kind);
  charge.take(mul(selection.points.size(), sizeof(std::array<double, 2>)));
  charge.object(selection.attributes, 2); charge.take(selection.packed_mask().size());
  selection.validate(shape.x, shape.y);
  detail::validate_region_storage(selection);
  if (!selection_is_area(selection) && !nonarea(selection.kind))
    invalid("Unknown Auto contrast selection kind");
  return charge.bytes;
}
std::size_t selection_wire_bound(const Selection *selection,
                                 const AutoContrastLimits &limits) {
  if (!selection) return node_bytes;
  auto bytes = add(metadata_size(selection->attributes, limits, 2, true), 32 * node_bytes);
  bytes = add(bytes, mul(selection->points.size(), 6 * node_bytes));
  bytes = add(bytes, add(selection->kind.size(), 1));
  if (selection->is_mask())
    bytes = add(bytes, base64_size(selection->packed_mask().size()));
  if(selection->is_region()) {
    for(const auto &[key,value]:selection->attributes)detail::regional_wire_depth(value,4);
    bytes=add(bytes,16*node_bytes);
  }
  record_budget(bytes, limits); return bytes;
}
std::size_t state_size(const ChannelDisplayState &state,
                       const AutoContrastLimits &limits) {
  Charge charge{limits}; charge.take(sizeof(ChannelDisplayState));
  charge.take(mul(state.channels.size(), sizeof(ChannelDisplayChannel)));
  for (const auto &channel : state.channels) charge.text(channel.lut.name);
  return charge.bytes;
}

// Full borrowed preflight bounds all captures before constructing the owning
// histogram request. Its estimate then delegates to the adopted shared service.
struct Layout {
  DatasetShape shape;
  PixelType type;
  FloatingSamplePolicy policy;
  PlanePosition position;
  const Selection *selection;
  AutoContrastEstimate estimate;
  std::size_t state_bytes, histogram_bound, base_bytes;
};
Layout layout(const DocumentSnapshot &source, const AutoContrastRequest &request,
              const AutoContrastLimits &limits) {
  limits_valid(limits); request_valid(request);
  const auto &dataset = source.data().dataset;
  Layout result{dataset.shape(), dataset.pixel_type(), dataset.sample_policy(),
    {source.display().t, source.display().c, source.display().crosshair[2]}, nullptr, {}, 0, 0, 0};
  type_valid(result.shape, result.type, result.policy);
  dataset.calibration().validate();
  const auto pixels = xy_size(result.shape);
  if (pixels > limits.max_pixels) invalid("Auto contrast exceeds pixel limit");
  const auto &p = result.position;
  if (p.t < 0 || p.t >= result.shape.t || p.c < 0 || p.c >= result.shape.c ||
      p.z < 0 || p.z >= result.shape.z)
    invalid("Auto contrast TCZ is outside source");
  auto &estimate = result.estimate;
  const auto state_property = source.display().properties.find("channel_display");
  if (state_property == source.display().properties.end())
    invalid("Auto contrast requires enabled recognized channel display");
  const auto &state_wire = object_value(state_property->second);
  const auto state_wire_bytes = metadata_size(state_wire, limits, 2, true);
  if (request.roi == AutoContrastRoiPolicy::ActiveSelection && source.data().selection)
    result.selection = &*source.data().selection;
  const auto selection_wire = selection_wire_bound(result.selection, limits);
  // Every serialized subtree gets its record cap before unrelated retained
  // display/native geometry traversal can consume a larger working allowance.
  estimate.display_bytes = add(sizeof(DisplayState), metadata_size(source.display().properties, limits));
  if (result.selection)
    estimate.selection_bytes = selection_size(*result.selection, result.shape, limits);
  // Include all typed custom LUT storage before the parser allocates it.
  result.state_bytes = add(sizeof(ChannelDisplayState),
      add(mul(static_cast<std::size_t>(result.shape.c), sizeof(ChannelDisplayChannel)), state_wire_bytes));
  estimate.record_bytes = add(128 * node_bytes, add(selection_wire,
      add(mul(2, state_wire_bytes), mul(static_cast<std::size_t>(result.shape.c), node_bytes))));
  record_budget(estimate.record_bytes, limits);
  auto bytes = mul(3, estimate.display_bytes);
  bytes = add(bytes, mul(5, result.state_bytes));
  bytes = add(bytes, mul(2, estimate.record_bytes));
  bytes = add(bytes, mul(2, add(estimate.selection_bytes, mul(2, selection_wire))));
  bytes = add(bytes, codec_scratch + 4 * command_bytes +
      sizeof(ExecutionControl) + sizeof(AutoContrastRequest) + sizeof(AutoContrastLimits));
  result.base_bytes = bytes;
  const bool area = result.selection && selection_is_area(*result.selection);
  result.histogram_bound = add(mul(pixels, area ? 2 * sizeof(double) : sizeof(double)),
      add(32768, add(mul(2, estimate.selection_bytes),
          result.selection ? mul(result.selection->points.size(), 3 * sizeof(double)) : 0)));
  result.histogram_bound = add(result.histogram_bound,
      add(dataset.calibration().unit.size(), dataset.calibration().time_unit.size()));
  estimate.working_bytes = add(result.base_bytes, result.histogram_bound);
  working(estimate.working_bytes, limits);
  // Parsing/validating the recognized state is now inside the admitted bound.
  const auto state = recognized_channel_display(source.display(), dataset);
  if (!state) invalid("Auto contrast requires recognized channel display");
  source.display().validate(result.shape);
  DatasetHistogramRequest histogram;
  histogram.position = result.position;
  if (area) histogram.area_roi = *result.selection;
  estimate.histogram = estimate_dataset_histogram(dataset, histogram,
      {limits.max_pixels, limits.max_working_bytes});
  estimate.working_bytes = add(result.base_bytes,
      std::max(result.histogram_bound, estimate.histogram.working_bytes));
  working(estimate.working_bytes, limits);
  estimate.retained_bytes = add(command_bytes,
      add(estimate.display_bytes, add(result.state_bytes,
          add(estimate.record_bytes, add(estimate.selection_bytes,
              mul(static_cast<std::size_t>(result.shape.c), sizeof(int)))))));
  return result;
}

std::string encode_mask(const std::vector<unsigned char> &bytes) {
  std::string encoded; encoded.reserve(base64_size(bytes.size()));
  for (std::size_t first = 0; first < bytes.size();) {
    kernel_checkpoint();
    const auto last = first + std::min<std::size_t>(3072, bytes.size() - first);
    encoded += detail::session_metadata::encode_base64(
        std::vector<unsigned char>(bytes.begin() + first, bytes.begin() + last));
    first = last;
  }
  kernel_checkpoint(); return encoded;
}
MetadataValue encode_selection(const std::optional<Selection> &selection) {
  if (!selection) return {};
  kernel_checkpoint();
  MetadataObject wire=selection->is_region()?MetadataObject{{"attributes",metadata_object(selection->attributes)}}:selection->attributes;
  MetadataArray points; points.reserve(selection->points.size());
  for (const auto &point : selection->points) {
    kernel_checkpoint_periodic();
    points.emplace_back(metadata_array({point[0], point[1]}));
  }
  wire.emplace("kind", selection->kind);
  wire.emplace("points", metadata_array(std::move(points)));
  wire.emplace("width", selection->width);
  if (selection->is_mask()) {
    if(selection->is_region())wire.emplace("origin",metadata_array({selection->mask_origin()[0],selection->mask_origin()[1]}));
    wire.emplace("shape", metadata_array({selection->mask_height(), selection->mask_width()}));
    wire.emplace("mask", encode_mask(selection->packed_mask()));
  }
  kernel_checkpoint(); return metadata_object(std::move(wire));
}
// Validate base64 alphabet/padding/tail before allocating decoded mask bytes.
void base64_valid(const std::string &encoded, std::size_t expected) {
  if (encoded.size() != base64_size(expected)) invalid("Auto contrast mask encoded size mismatch");
  auto digit = [](char c) -> unsigned {
    if (c >= 'A' && c <= 'Z') return unsigned(c - 'A');
    if (c >= 'a' && c <= 'z') return unsigned(c - 'a') + 26;
    if (c >= '0' && c <= '9') return unsigned(c - '0') + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    invalid("Invalid Auto contrast base64 alphabet");
  };
  for (std::size_t first = 0; first < encoded.size(); first += 4) {
    unsigned value = 0, padding = 0;
    for (unsigned k = 0; k < 4; ++k) {
      const auto c = encoded[first + k];
      if (c == '=') {
        if (k < 2 || first + 4 != encoded.size()) invalid("Invalid Auto contrast base64 padding");
        ++padding; value <<= 6;
      } else {
        if (padding) invalid("Invalid Auto contrast base64 padding");
        value = (value << 6) | digit(c);
      }
    }
    if (padding > 2 || (padding == 2 && (value & 65535u)) ||
        (padding == 1 && (value & 255u))) invalid("Invalid Auto contrast base64 tail");
    if (first + 4 == encoded.size() && 3 - padding !=
        (expected % 3 == 0 ? 3 : expected % 3)) invalid("Auto contrast mask decoded size mismatch");
  }
}
struct SelectionWire {
  const MetadataObject *object;
  const MetadataArray *points;
  const std::string *kind;
  double width;
  const std::string *mask;
  std::size_t native_bytes;
  const MetadataObject *attributes=nullptr;
  int stored_width=0,stored_height=0;
  std::array<int,2> origin{0,0};
};
int regional_index(const MetadataValue &value) {
  if(const auto n=std::get_if<std::int64_t>(&value)) {
    if(*n<std::numeric_limits<int>::min() || *n>std::numeric_limits<int>::max())invalid("Auto regional origin exceeds int32");
    return static_cast<int>(*n);
  }
  const auto n=uint_value(value);
  if(n>std::uint64_t(std::numeric_limits<int>::max()))invalid("Auto regional origin exceeds int32");
  return static_cast<int>(n);
}
SelectionWire inspect_selection(const MetadataValue &value, DatasetShape shape,
                                 const AutoContrastLimits &limits,bool regional=false) {
  const auto &wire = object_value(value);
  for (const auto *key : {"kind", "points", "width"})
    if (!wire.count(key)) invalid("Auto contrast selection geometry is incomplete");
  const auto &kind = text_value(wire.at("kind"));
  const auto &points = array_value(wire.at("points"));
  const double width = number_value(wire.at("width"));
  if (width <= 0 || points.size() > limits.max_selection_vertices)
    invalid("Invalid Auto contrast selection width/vertex count");
  for (const auto &item : points) {
    const auto &point = array_value(item);
    if (point.size() != 2) invalid("Auto contrast selection point needs two coordinates");
    (void)number_value(point[0]); (void)number_value(point[1]);
  }
  const std::string *mask = nullptr;
  int stored_width=shape.x,stored_height=shape.y;std::array<int,2> origin{0,0};
  const MetadataObject *attributes=nullptr;
  if(kind=="region") {
    if(!regional)invalid("Regional selection requires Auto command version 2");
    fields(wire,{"kind","points","width","attributes","shape","mask","origin"});
    if(!points.empty())invalid("Regional mask cannot contain vector points");
    attributes=&object_value(wire.at("attributes"));
    const auto &xy=array_value(wire.at("origin"));
    if(xy.size()!=2)invalid("Regional origin requires two integers");
    origin={regional_index(xy[0]),regional_index(xy[1])};
  }
  if (kind == "mask" || kind=="region") {
    if (!wire.count("shape") || !wire.count("mask")) invalid("Auto contrast mask storage is incomplete");
    const auto &dimensions = array_value(wire.at("shape"));
    if(dimensions.size()!=2)invalid("Auto mask requires two dimensions");
    stored_height=index_value(dimensions[0]);stored_width=index_value(dimensions[1]);
    if(stored_height<=0 || stored_width<=0 || (kind=="mask" && (stored_height!=shape.y || stored_width!=shape.x)))invalid("Auto contrast mask shape mismatch");
    mask = &text_value(wire.at("mask"));
    base64_valid(*mask, packed_size(mul(stored_width,stored_height)));
  }
  const auto native = add(sizeof(Selection), add(kind.size() + 1,
      add(mul(points.size(), sizeof(std::array<double, 2>)),
          add(metadata_size(wire, limits, 2, true), mask ? packed_size(mul(stored_width,stored_height)) : 0))));
  working(add(native, codec_scratch), limits);
  return {&wire, &points, &kind, width, mask, native,attributes,stored_width,stored_height,origin};
}
Selection decode_selection(const SelectionWire &wire, DatasetShape shape) {
  auto selection = [&] {
    if (!wire.mask) return Selection(*wire.kind);
    std::vector<unsigned char> bytes; bytes.reserve(packed_size(mul(wire.stored_width,wire.stored_height)));
    for (std::size_t first = 0; first < wire.mask->size();) {
      const auto length = std::min<std::size_t>(4096, wire.mask->size() - first);
      const auto chunk = detail::session_metadata::decode_base64(wire.mask->substr(first, length));
      bytes.insert(bytes.end(), chunk.begin(), chunk.end()); first += length;
    }
    return *wire.kind=="region"?Selection::from_packed_region(wire.stored_width,wire.stored_height,std::move(bytes),wire.origin)
        :Selection::from_packed_mask(wire.stored_width,wire.stored_height,std::move(bytes));
  }();
  selection.width = wire.width;
  selection.points.reserve(wire.points->size());
  for (const auto &item : *wire.points) {
    const auto &point = array_value(item);
    selection.points.push_back({number_value(point[0]), number_value(point[1])});
  }
  if(wire.attributes)selection.attributes=*wire.attributes;
  else for (const auto &[key, value] : *wire.object)
    if (key != "kind" && key != "points" && key != "width" &&
        (!wire.mask || (key != "shape" && key != "mask")))
      selection.attributes.emplace(key, value);
  selection.validate(shape.x, shape.y);
  detail::validate_region_storage(selection);
  if (!selection_is_area(selection) && !nonarea(selection.kind))
    invalid("Unknown Auto contrast selection kind");
  return selection;
}

AutoContrastCycle cycle_for(int previous, AutoContrastOutcome outcome) {
  const int used = previous < 10 ? 5000 : previous / 2;
  return {previous, used, outcome == AutoContrastOutcome::ResetFallback ? 0 : used};
}
bool propagates(const AutoContrastRequest &request, const ChannelDisplayState &state) {
  return request.scope == AutoContrastScope::CompositeAll && state.mode == ChannelDisplayMode::Composite;
}
std::vector<int> target_channels(const AutoContrastRequest &request,
    const ChannelDisplayState &state, DatasetShape shape, PixelType type,
    PlanePosition position, AutoContrastOutcome outcome) {
  if (propagates(request, state) || (outcome == AutoContrastOutcome::ResetFallback &&
        type == PixelType::UInt16 && request.unsigned16_bits != 0)) {
    std::vector<int> channels; channels.reserve(static_cast<std::size_t>(shape.c));
    for (int c = 0; c < shape.c; ++c) channels.push_back(c);
    return channels;
  }
  return {position.c};
}
std::array<double, 2> applied_range(std::array<double, 2> range,
    const AutoContrastRequest &request, const ChannelDisplayState &state, PixelType type) {
  if (propagates(request, state))
    for (auto &value : range)
      value = type == PixelType::Float32 ? double(static_cast<float>(value)) : std::floor(value + .5);
  ordered(range); return range;
}
std::array<double, 2> fallback_range(const AutoContrastRequest &request,
    const AutoContrastHistogramSummary &histogram, PixelType type) {
  if (type == PixelType::UInt8) return {0, 255};
  if (type == PixelType::UInt16 && request.unsigned16_bits)
    return {0, double((std::uint32_t(1) << request.unsigned16_bits) - 1)};
  return {histogram.plane_minimum, histogram.plane_maximum};
}
AutoContrastHistogramSummary summary(const DatasetHistogramResult &result) {
  if (!result.finite_count || !result.minimum || !result.maximum ||
      !result.plane_minimum || !result.plane_maximum || !result.bin_size)
    invalid("Auto contrast area has no finite samples");
  return {result.histogram_origin, *result.bin_size, *result.minimum, *result.maximum,
    result.visited_count, result.finite_count, result.nan_count,
    *result.plane_minimum, *result.plane_maximum, result.plane_finite_count, result.plane_nan_count};
}
MetadataObject encode_summary(const AutoContrastHistogramSummary &value) {
  return {{"domain", std::string("raw")}, {"binning", std::string("processor_raw")}, {"bins", 256},
    {"origin", value.origin}, {"bin_size", value.bin_size},
    {"minimum", value.minimum}, {"maximum", value.maximum},
    {"visited_count", value.visited_count}, {"finite_count", value.finite_count},
    {"nan_count", value.nan_count}, {"plane_minimum", value.plane_minimum},
    {"plane_maximum", value.plane_maximum}, {"plane_finite_count", value.plane_finite_count},
    {"plane_nan_count", value.plane_nan_count}};
}
const char *effective_roi(AutoContrastRoiPolicy policy, const std::optional<Selection> &selection) {
  if (policy == AutoContrastRoiPolicy::WholePlane || !selection) return "whole_plane";
  return selection_is_area(*selection) ? "area" : "whole_nonarea";
}
MetadataObject encode_record(const ResolvedChannelDisplayCommand &effect,
    const AutoContrastRequest &request, const std::optional<Selection> &selection,
    std::uint64_t selection_revision, std::uint64_t display_revision,
    AutoContrastCycle cycle, const AutoContrastHistogramSummary &histogram,
    AutoContrastOutcome outcome, const std::optional<std::array<int, 2>> &indices,
    const std::optional<std::array<double, 2>> &candidate,
    const std::vector<int> &channels, std::array<double, 2> resolved,
    std::array<double, 2> applied) {
  MetadataArray targets; targets.reserve(channels.size());
  for (const auto c : channels) { kernel_checkpoint_periodic(); targets.emplace_back(c); }
  MetadataValue encoded_indices, encoded_candidate;
  if (indices) encoded_indices = metadata_array({(*indices)[0], (*indices)[1]});
  if (candidate) encoded_candidate = metadata_array({(*candidate)[0], (*candidate)[1]});
  return {{"schema", std::string(schema)}, {"version", selection && selection->is_region()?2:1}, {"effect", metadata_object(effect.record())},
    {"roi", metadata_object({{"policy", std::string(roi_name(request.roi))},
      {"effective", std::string(effective_roi(request.roi, selection))},
      {"selection_revision", selection_revision}, {"selection", encode_selection(selection)}})},
    {"display_dependency", metadata_object({{"revision", display_revision}})},
    {"cycle", metadata_object({{"previous", cycle.previous}, {"used", cycle.used}, {"next", cycle.next}})},
    {"scope", std::string(scope_name(request.scope))}, {"unsigned16_bits", request.unsigned16_bits},
    {"channels", metadata_array(std::move(targets))}, {"histogram", metadata_object(encode_summary(histogram))},
    {"decision", metadata_object({{"outcome", std::string(outcome_name(outcome))},
      {"indices", std::move(encoded_indices)}, {"candidate_range", std::move(encoded_candidate)},
      {"resolved_range", metadata_array({resolved[0], resolved[1]})},
      {"applied_range", metadata_array({applied[0], applied[1]})}})}};
}

void extrema_valid(double value, PixelType type) {
  if (!std::isfinite(value)) invalid("Auto contrast extrema must be finite");
  if (type == PixelType::Float32) {
    if (std::abs(value) > std::numeric_limits<float>::max() ||
        double(static_cast<float>(value)) != value)
      invalid("Auto contrast Float32 extrema are not canonical");
  } else if (value < 0 || value > (type == PixelType::UInt8 ? 255 : 65535) || std::trunc(value) != value)
    invalid("Auto contrast unsigned extrema are not canonical");
}
void validate_summary(const AutoContrastHistogramSummary &value, DatasetShape shape,
    PixelType type, FloatingSamplePolicy policy, bool area) {
  for (const auto n : {value.minimum, value.maximum, value.plane_minimum, value.plane_maximum})
    extrema_valid(n, type);
  if (value.minimum > value.maximum || value.plane_minimum > value.minimum ||
      value.plane_maximum < value.maximum) invalid("Auto contrast extrema contradict plane bounds");
  const auto pixels = xy_size(shape);
  if (!value.finite_count || count_add(value.finite_count, value.nan_count) != value.visited_count ||
      value.visited_count > pixels || count_add(value.plane_finite_count, value.plane_nan_count) != pixels ||
      value.finite_count > value.plane_finite_count || value.nan_count > value.plane_nan_count)
    invalid("Auto contrast histogram counts are inconsistent");
  if ((value.nan_count || value.plane_nan_count) &&
      (type != PixelType::Float32 || policy != FloatingSamplePolicy::PreserveQuietNaNFloat32))
    invalid("Auto contrast NaN counts contradict sample policy");
  if (!area && (value.visited_count != pixels || value.finite_count != value.plane_finite_count ||
      value.nan_count != value.plane_nan_count || value.minimum != value.plane_minimum ||
      value.maximum != value.plane_maximum)) invalid("Auto contrast whole-plane summary differs");
  const double origin = type == PixelType::UInt8 ? 0 : value.minimum;
  const double width = type == PixelType::UInt8 ? 1 : type == PixelType::UInt16
      ? (value.maximum - value.minimum + 1) / 256 : (value.maximum - value.minimum) / 256;
  if (!std::isfinite(value.origin) || !std::isfinite(value.bin_size) ||
      value.origin != origin || value.bin_size != width)
    invalid("Auto contrast raw histogram geometry is inconsistent");
}
AutoContrastHistogramSummary read_summary(const MetadataObject &wire) {
  fields(wire, {"domain", "binning", "bins", "origin", "bin_size", "minimum", "maximum",
    "visited_count", "finite_count", "nan_count", "plane_minimum", "plane_maximum",
    "plane_finite_count", "plane_nan_count"});
  if (text_value(wire.at("domain")) != "raw" || text_value(wire.at("binning")) != "processor_raw" ||
      uint_value(wire.at("bins")) != 256) invalid("Unsupported Auto contrast histogram domain/binning");
  return {number_value(wire.at("origin")), number_value(wire.at("bin_size")),
    number_value(wire.at("minimum")), number_value(wire.at("maximum")),
    uint_value(wire.at("visited_count")), uint_value(wire.at("finite_count")), uint_value(wire.at("nan_count")),
    number_value(wire.at("plane_minimum")), number_value(wire.at("plane_maximum")),
    uint_value(wire.at("plane_finite_count")), uint_value(wire.at("plane_nan_count"))};
}
struct Decoded {
  AutoContrastRequest request;
  detail::ChannelDisplayCommandDescriptor descriptor;
  std::optional<Selection> selection;
  std::uint64_t selection_revision, display_revision;
  AutoContrastCycle cycle;
  AutoContrastHistogramSummary histogram;
  AutoContrastOutcome outcome;
  std::optional<std::array<int, 2>> indices;
  std::optional<std::array<double, 2>> candidate;
  std::vector<int> channels;
  std::array<double, 2> resolved, applied;
  AutoContrastEstimate estimate;
};
Decoded decode_record(const MetadataObject &record, const AutoContrastLimits &limits) {
  limits_valid(limits);
  const auto bytes = metadata_size(record, limits, 0, true);
  record_budget(bytes, limits);
  fields(record, {"schema", "version", "effect", "roi", "display_dependency", "cycle",
    "scope", "unsigned16_bits", "channels", "histogram", "decision"});
  const auto version=uint_value(record.at("version"));
  if (text_value(record.at("schema")) != schema || (version!=1 && version!=2))
    invalid("Unsupported Auto contrast command schema/version");
  if(version==2)detail::regional_record_depth(record);
  const auto &effect = object_value(record.at("effect"));
  // Bound decoded inline LUT/state storage before invoking the owning decoder.
  const auto &state_wire = object_value(required(effect, "state"));
  const auto state_count = array_value(required(state_wire, "channels")).size();
  const auto typed_bound = add(sizeof(ChannelDisplayState), mul(state_count, sizeof(ChannelDisplayChannel)));
  working(add(2 * command_bytes + codec_scratch,
      add(mul(4, bytes), mul(5, typed_bound))), limits);
  auto descriptor = detail::channel_display_command_descriptor(effect);
  if (descriptor.action != ChannelDisplayAction::Set || !descriptor.state)
    invalid("Auto contrast effect must be a complete Set command");
  type_valid(descriptor.shape, descriptor.type, descriptor.policy);
  if (xy_size(descriptor.shape) > limits.max_pixels) invalid("Auto contrast record exceeds pixel limit");
  Decoded result{{}, std::move(descriptor), {}, 0, 0, {}, {},
    AutoContrastOutcome::HistogramBounds, {}, {}, {}, {}, {}, {}};
  const auto &scope = text_value(record.at("scope"));
  if (scope == "active_channel") result.request.scope = AutoContrastScope::ActiveChannel;
  else if (scope == "composite_all") result.request.scope = AutoContrastScope::CompositeAll;
  else invalid("Unknown Auto contrast scope");
  result.request.unsigned16_bits = index_value(record.at("unsigned16_bits"));
  const auto &roi = object_value(record.at("roi"));
  fields(roi, {"policy", "effective", "selection_revision", "selection"});
  const auto &policy = text_value(roi.at("policy"));
  if (policy == "active_selection") result.request.roi = AutoContrastRoiPolicy::ActiveSelection;
  else if (policy == "whole_plane") result.request.roi = AutoContrastRoiPolicy::WholePlane;
  else invalid("Unknown Auto contrast ROI policy");
  result.selection_revision = uint_value(roi.at("selection_revision"));
  if (!is_null(roi.at("selection"))) {
    const auto selection = inspect_selection(roi.at("selection"), result.descriptor.shape, limits,version==2);
    result.estimate.selection_bytes = selection.native_bytes;
    working(add(codec_scratch, add(mul(4, bytes),
        add(mul(5, typed_bound), mul(2, selection.native_bytes)))), limits);
    result.selection = decode_selection(selection, result.descriptor.shape);
  }
  if ((result.request.roi == AutoContrastRoiPolicy::WholePlane && result.selection) ||
      text_value(roi.at("effective")) != effective_roi(result.request.roi, result.selection))
    invalid("Auto contrast ROI adaptation is inconsistent");
  const auto &dependency = object_value(record.at("display_dependency"));
  fields(dependency, {"revision"});
  result.display_revision = uint_value(dependency.at("revision"));
  const auto &cycle = object_value(record.at("cycle"));
  fields(cycle, {"previous", "used", "next"});
  result.cycle = {index_value(cycle.at("previous")), index_value(cycle.at("used")), index_value(cycle.at("next"))};
  result.request.previous_divisor = result.cycle.previous;
  request_valid(result.request);
  result.histogram = read_summary(object_value(record.at("histogram")));
  validate_summary(result.histogram, result.descriptor.shape, result.descriptor.type, result.descriptor.policy,
      result.selection && selection_is_area(*result.selection));
  // A literal empty packed area cannot substantiate a successful finite count.
  if(result.selection && result.selection->is_region()) {
    if(detail::regional_visible_members(*result.selection,result.descriptor.shape.x,result.descriptor.shape.y)!=result.histogram.visited_count)
      invalid("Auto regional visible membership contradicts counts");
  }
  if (result.selection && result.selection->kind == "mask") {
    const auto &mask = result.selection->packed_mask();
    std::uint64_t members = 0;
    const auto pixels = xy_size(result.descriptor.shape);
    for (std::size_t i = 0; i < pixels; ++i)
      members += (mask[i / 8] >> (7 - i % 8)) & 1u;
    if (members != result.histogram.visited_count)
      invalid("Auto contrast mask membership contradicts counts");
  }
  const auto &decision = object_value(record.at("decision"));
  fields(decision, {"outcome", "indices", "candidate_range", "resolved_range", "applied_range"});
  const auto &outcome = text_value(decision.at("outcome"));
  if (outcome == "histogram_bounds") result.outcome = AutoContrastOutcome::HistogramBounds;
  else if (outcome == "area_extrema") result.outcome = AutoContrastOutcome::AreaExtrema;
  else if (outcome == "reset_fallback") result.outcome = AutoContrastOutcome::ResetFallback;
  else invalid("Unknown Auto contrast outcome");
  const auto expected_cycle = cycle_for(result.cycle.previous, result.outcome);
  if (result.cycle.used != expected_cycle.used || result.cycle.next != expected_cycle.next)
    invalid("Auto contrast cycle transition is inconsistent");
  if (result.outcome != AutoContrastOutcome::ResetFallback &&
      result.histogram.finite_count / 10 <=
          result.histogram.finite_count / static_cast<std::uint64_t>(result.cycle.used))
    invalid("Auto contrast counts cannot substantiate nonfallback bounds");
  result.resolved = pair_value(decision.at("resolved_range")); ordered(result.resolved);
  result.applied = pair_value(decision.at("applied_range")); ordered(result.applied);
  if (result.outcome == AutoContrastOutcome::ResetFallback) {
    if (!is_null(decision.at("indices")) || !is_null(decision.at("candidate_range")) ||
        result.resolved != fallback_range(result.request, result.histogram, result.descriptor.type))
      invalid("Auto contrast fallback is inconsistent");
  } else {
    const auto &indices = array_value(decision.at("indices"));
    if (indices.size() != 2) invalid("Auto contrast indices require two values");
    result.indices = std::array<int, 2>{index_value(indices[0]), index_value(indices[1])};
    if ((*result.indices)[0] > (*result.indices)[1] || (*result.indices)[1] > 255)
      invalid("Auto contrast indices are invalid");
    result.candidate = pair_value(decision.at("candidate_range")); ordered(*result.candidate);
    const std::array<double, 2> expected{
      result.histogram.origin + double((*result.indices)[0]) * result.histogram.bin_size,
      result.histogram.origin + double((*result.indices)[1]) * result.histogram.bin_size};
    if (*result.candidate != expected || (*result.candidate)[0] < result.histogram.minimum ||
        (*result.candidate)[1] > result.histogram.maximum)
      invalid("Auto contrast candidate differs from indices/extrema");
    if (result.outcome == AutoContrastOutcome::HistogramBounds) {
      if ((*result.candidate)[0] == (*result.candidate)[1] || result.resolved != *result.candidate)
        invalid("Auto contrast histogram bounds are inconsistent");
    } else if ((*result.candidate)[0] != (*result.candidate)[1] ||
        result.resolved != std::array<double, 2>{result.histogram.minimum, result.histogram.maximum})
      invalid("Auto contrast equal-candidate extrema are inconsistent");
  }
  const auto &state = *result.descriptor.state;
  if (result.applied != applied_range(result.resolved, result.request, state, result.descriptor.type))
    invalid("Auto contrast applied range is inconsistent");
  const auto &targets = array_value(record.at("channels"));
  if (targets.empty() || targets.size() > static_cast<std::size_t>(result.descriptor.shape.c))
    invalid("Auto contrast targets are invalid");
  result.channels.reserve(targets.size());
  for (const auto &target : targets) result.channels.push_back(index_value(target));
  const auto expected_targets = target_channels(result.request, state, result.descriptor.shape,
      result.descriptor.type, result.descriptor.position, result.outcome);
  if (result.channels != expected_targets) invalid("Auto contrast target list is inconsistent");
  for (const auto c : result.channels)
    if (state.channels[static_cast<std::size_t>(c)].range != result.applied)
      invalid("Auto contrast Set range differs from resolved effect");
  result.estimate.record_bytes = bytes;
  result.estimate.working_bytes = add(2 * command_bytes + codec_scratch, add(mul(4, bytes),
      add(mul(5, typed_bound), mul(2, result.estimate.selection_bytes))));
  working(result.estimate.working_bytes, limits);
  return result;
}
} // namespace

ResolvedAutoContrastCommand::ResolvedAutoContrastCommand(
    ResolvedChannelDisplayCommand effect, AutoContrastRequest request,
    PlanePosition position, std::optional<Selection> selection,
    std::uint64_t selection_revision, std::uint64_t display_revision,
    AutoContrastCycle cycle, AutoContrastHistogramSummary histogram,
    AutoContrastOutcome outcome, std::optional<std::array<int, 2>> indices,
    std::optional<std::array<double, 2>> candidate, std::vector<int> channels,
    std::array<double, 2> resolved, std::array<double, 2> applied,
    MetadataObject record, AutoContrastEstimate estimate, bool fresh)
    : data_(std::make_shared<const Data>(Data{std::move(effect), request, position,
      std::move(selection), selection_revision, display_revision, cycle, histogram,
      outcome, indices, candidate, std::move(channels), resolved, applied,
      std::move(record), estimate, fresh})) {}

AutoContrastEstimate estimate_auto_contrast_command(const DocumentSnapshot &source,
    const AutoContrastRequest &request, const AutoContrastLimits &limits) {
  return layout(source, request, limits).estimate;
}

ResolvedAutoContrastCommand resolve_auto_contrast_command(const DocumentSnapshot &source,
    const AutoContrastRequest &request, const AutoContrastLimits &limits,
    const ExecutionControl &control) {
  // Do not invoke inherited callbacks during estimator/decoder admission.
  const auto admitted = layout(source, request, limits);
  const auto owned_request = request;
  const auto owned_limits = limits;
  const auto owned_control = control;
  const auto captured_source = source;
  std::optional<Selection> selection;
  if (admitted.selection) selection = *admitted.selection;
  auto state = *recognized_channel_display(captured_source.display(), captured_source.data().dataset);
  if (owned_control.cancellation && owned_control.cancellation->canceled()) throw OperationCancelled();
  ExecutionScope execution(owned_control);
  kernel_checkpoint();
  DatasetHistogramRequest histogram_request;
  histogram_request.position = admitted.position;
  if (selection && selection_is_area(*selection)) histogram_request.area_roi = *selection;
  const auto histogram = accumulate_dataset_histogram(captured_source.data().dataset,
      histogram_request, {owned_limits.max_pixels, owned_limits.max_working_bytes}, {});
  const auto counts = summary(histogram);
  validate_summary(counts, admitted.shape, admitted.type, admitted.policy,
      selection && selection_is_area(*selection));
  const int used = owned_request.previous_divisor < 10 ? 5000 : owned_request.previous_divisor / 2;
  const auto dominant = counts.finite_count / 10;
  const auto threshold = counts.finite_count / static_cast<std::uint64_t>(used);
  int first = -1, last = -1;
  for (int i = 0; i < 256; ++i) {
    kernel_checkpoint_periodic();
    const auto count = histogram.histogram.counts[static_cast<std::size_t>(i)];
    if (count <= dominant && count > threshold) {
      if (first < 0) first = i;
      last = i;
    }
  }
  AutoContrastOutcome outcome;
  std::optional<std::array<int, 2>> indices;
  std::optional<std::array<double, 2>> candidate;
  std::array<double, 2> resolved;
  if (first < 0) {
    outcome = AutoContrastOutcome::ResetFallback;
    resolved = fallback_range(owned_request, counts, admitted.type);
  } else {
    indices = std::array<int, 2>{first, last};
    candidate = std::array<double, 2>{counts.origin + double(first) * counts.bin_size,
                                     counts.origin + double(last) * counts.bin_size};
    outcome = (*candidate)[0] == (*candidate)[1] ? AutoContrastOutcome::AreaExtrema
                                               : AutoContrastOutcome::HistogramBounds;
    resolved = outcome == AutoContrastOutcome::AreaExtrema
        ? std::array<double, 2>{counts.minimum, counts.maximum} : *candidate;
  }
  ordered(resolved);
  const auto applied = applied_range(resolved, owned_request, state, admitted.type);
  auto channels = target_channels(owned_request, state, admitted.shape,
      admitted.type, admitted.position, outcome);
  for (const auto c : channels) {
    kernel_checkpoint_periodic(); state.channels[static_cast<std::size_t>(c)].range = applied;
  }
  kernel_checkpoint();
  auto effect = resolve_channel_display_command(captured_source,
      {ChannelDisplayAction::Set, std::move(state), {}},
      {owned_limits.max_pixels, owned_limits.max_working_bytes});
  const auto cycle = cycle_for(owned_request.previous_divisor, outcome);
  const auto revisions = captured_source.revisions();
  auto record = encode_record(effect, owned_request, selection, revisions.selection,
      revisions.display, cycle, counts, outcome, indices, candidate, channels, resolved, applied);
  const auto actual_record_bytes = metadata_size(record, owned_limits, 0, true);
  record_budget(actual_record_bytes, owned_limits);
  if (actual_record_bytes > admitted.estimate.record_bytes)
    invalid("Auto contrast record exceeded admitted bound");
  kernel_checkpoint();
  if (owned_control.cancellation && owned_control.cancellation->canceled()) throw OperationCancelled();
  return {std::move(effect), owned_request, admitted.position, std::move(selection), revisions.selection,
    revisions.display, cycle, counts, outcome, indices, candidate, std::move(channels), resolved,
    applied, std::move(record), admitted.estimate, true};
}

void validate_auto_contrast_command_record(const MetadataObject &record,
                                           const AutoContrastLimits &limits) {
  (void)decode_record(record, limits);
}

ResolvedAutoContrastCommand auto_contrast_command_from_record(const DocumentSnapshot &source,
    const MetadataObject &record, const AutoContrastLimits &limits) {
  // No destination inspection or source binding precedes complete original validation.
  auto decoded = decode_record(record, limits);
  auto &estimate = decoded.estimate;
  estimate.display_bytes = add(sizeof(DisplayState), metadata_size(source.display().properties, limits));
  const auto typed = state_size(*decoded.descriptor.state, limits);
  estimate.retained_bytes = add(command_bytes,
      add(estimate.display_bytes, add(typed, add(estimate.record_bytes,
          add(estimate.selection_bytes, mul(decoded.channels.size(), sizeof(int)))))));
  estimate.working_bytes = add(estimate.working_bytes, mul(3, estimate.display_bytes));
  working(estimate.working_bytes, limits);
  auto effect = channel_display_command_from_record(source, object_value(record.at("effect")));
  return {std::move(effect), decoded.request, decoded.descriptor.position, std::move(decoded.selection),
    decoded.selection_revision, decoded.display_revision, decoded.cycle, decoded.histogram, decoded.outcome,
    decoded.indices, decoded.candidate, std::move(decoded.channels), decoded.resolved, decoded.applied,
    record, estimate, false};
}

ResolvedAutoContrastCommand rebind_auto_contrast_command(const DocumentSnapshot &source,
    const MetadataObject &record, const AutoContrastRequest &complete_request,
    const AutoContrastLimits &limits, const ExecutionControl &control) {
  (void)decode_record(record, limits);
  return resolve_auto_contrast_command(source, complete_request, limits, control);
}

DisplayState command_display_state(const ResolvedAutoContrastCommand &command,
                                  const DocumentSnapshot &current) {
  if (!command.source().same_document(current))
    invalid("Auto contrast command differs from retained owner");
  if (command.requires_captured_state() &&
      !current.revisions().matches(command.source().revisions(), Change::All))
    throw RevisionConflict();
  return command_display_state(command.effect(), current);
}

} // namespace imagejxx
