#include <imagejxx/operations.hpp>
#include <imagejxx/targets.hpp>
#include "target_record_internal.hpp"
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace imagejxx {
namespace {
std::array<int, 3> key(PlanePosition p) { return {p.t, p.c, p.z}; }
void validate_position(PlanePosition p, DatasetShape shape) {
  if (p.t < 0 || p.t >= shape.t || p.c < 0 || p.c >= shape.c || p.z < 0 ||
      p.z >= shape.z)
    throw std::out_of_range("Target position is outside the dataset");
}
std::size_t range_count(const AxisRange &range) {
  return std::size_t(
      (std::int64_t(*range.stop) - range.start - 1) / range.step + 1);
}
AxisRange resolve_range(std::optional<AxisRange> requested, int current,
                        int extent) {
  auto result = requested.value_or(AxisRange{current, current + 1, 1});
  result.stop = result.stop.value_or(extent);
  if (result.start < 0 || result.start >= extent ||
      *result.stop <= result.start || *result.stop > extent || result.step <= 0)
    throw std::invalid_argument(
        "Target ranges require 0 <= start < stop <= extent and positive step");
  return result;
}
bool in_range(int value, const AxisRange &range) {
  return value >= range.start && value < *range.stop &&
         (value - range.start) % range.step == 0;
}
void validate_enums(const TargetRequest &request) {
  switch (request.scope) {
  case TargetScope::CurrentPlane:
  case TargetScope::AllZ:
  case TargetScope::Ranges:
  case TargetScope::Positions:
    break;
  default:
    throw std::invalid_argument("Unknown target scope");
  }
  switch (request.roi) {
  case RoiPolicy::WholePlane:
  case RoiPolicy::ActiveSelection:
  case RoiPolicy::PositionedRois:
    break;
  default:
    throw std::invalid_argument("Unknown target ROI policy");
  }
  if (request.mode != ExecutionMode::Planes &&
      request.mode != ExecutionMode::Volume)
    throw std::invalid_argument("Unknown target execution mode");
  if (request.destination != OutputDestination::InPlace &&
      request.destination != OutputDestination::NewDocument)
    throw std::invalid_argument("Unknown target output destination");
}
const MetadataValue &field(const MetadataObject &record, const char *name) {
  auto found = record.find(name);
  if (found == record.end())
    throw std::invalid_argument(std::string("Missing target field: ") + name);
  return found->second;
}
std::uint64_t unsigned_value(const MetadataValue &value) {
  if (auto n = std::get_if<std::uint64_t>(&value))
    return *n;
  if (auto n = std::get_if<std::int64_t>(&value); n && *n >= 0)
    return std::uint64_t(*n);
  throw std::invalid_argument("Target record requires a nonnegative integer");
}
int index_value(const MetadataValue &value) {
  auto n = unsigned_value(value);
  if (n > std::uint64_t(std::numeric_limits<int>::max()))
    throw std::invalid_argument("Target index exceeds supported range");
  return int(n);
}
const MetadataArray &array_value(const MetadataValue &value) {
  // The compatibility metadata decoder represents an empty Python list as
  // the original string-list alternative. It is also a valid empty array.
  if (auto strings = std::get_if<std::vector<std::string>>(&value);
      strings && strings->empty()) {
    static const MetadataArray empty;
    return empty;
  }
  auto node = std::get_if<std::shared_ptr<const MetadataTree>>(&value);
  if (node && *node)
    if (auto array = std::get_if<MetadataArray>(&(*node)->value))
      return *array;
  throw std::invalid_argument("Target record requires an array");
}
std::string text_value(const MetadataValue &value) {
  if (auto text = std::get_if<std::string>(&value))
    return *text;
  throw std::invalid_argument("Target record requires text");
}
const char *roi_name(RoiPolicy roi) {
  switch (roi) {
  case RoiPolicy::WholePlane:
    return "whole_plane";
  case RoiPolicy::ActiveSelection:
    return "active_selection";
  case RoiPolicy::PositionedRois:
    return "positioned_rois";
  }
  throw std::invalid_argument("Unknown target ROI policy");
}
} // namespace

ResolvedTarget::ResolvedTarget(DocumentSnapshot source, TargetScope scope,
                               std::array<AxisRange, 3> ranges,
                               std::vector<PlanePosition> positions,
                               RoiPolicy roi, ExecutionMode mode,
                               OutputDestination destination)
    : source_(std::move(source)), scope_(scope), ranges_(std::move(ranges)),
      positions_(std::move(positions)), roi_(roi), mode_(mode),
      destination_(destination), count_(positions_.size()) {
  if (scope_ == TargetScope::Ranges) {
    count_ = 1;
    for (const auto &range : ranges_) {
      auto n = range_count(range);
      if (count_ > std::numeric_limits<std::size_t>::max() / n)
        throw std::overflow_error("Target position count exceeds size_t");
      count_ *= n;
    }
  }
}
PlanePosition ResolvedTarget::position(std::size_t index) const {
  if (index >= count_)
    throw std::out_of_range("Target position index is outside the target");
  if (scope_ == TargetScope::Positions)
    return positions_[index];
  std::array<int, 3> result{};
  for (int axis = 2; axis >= 0; --axis) {
    auto n = range_count(ranges_[axis]);
    result[axis] =
        int(ranges_[axis].start + std::int64_t(index % n) * ranges_[axis].step);
    index /= n;
  }
  return {result[0], result[1], result[2]};
}
bool ResolvedTarget::contains(PlanePosition p) const {
  if (scope_ == TargetScope::Ranges)
    return in_range(p.t, ranges_[0]) && in_range(p.c, ranges_[1]) &&
           in_range(p.z, ranges_[2]);
  for (auto candidate : positions_)
    if (key(candidate) == key(p))
      return true;
  return false;
}
Change ResolvedTarget::dependencies() const {
  return roi_ == RoiPolicy::WholePlane ? Change::Pixels | Change::Metadata
                                       : Change::Data;
}
void ResolvedTarget::validate_source(const DocumentSnapshot &current) const {
  if (!source_.same_document(current))
    throw std::invalid_argument("Target belongs to another document");
  if (!current.revisions().matches(source_.revisions(), dependencies()))
    throw RevisionConflict();
}
void ResolvedTarget::validate_operation(const std::string &operation) const {
  const auto &spec = operation_spec(operation);
  if (mode_ == ExecutionMode::Volume && spec.planeOnly)
    throw std::invalid_argument(
        "Operation does not support volume execution: " + operation);
}
DocumentTransaction ResolvedTarget::begin_edit() const {
  return source_.begin_edit(dependencies());
}
std::optional<Image> ResolvedTarget::mask(std::size_t index) const {
  auto p = position(index);
  const auto &data = source_.data();
  const auto shape = data.dataset.shape();
  if (roi_ == RoiPolicy::WholePlane ||
      (roi_ == RoiPolicy::ActiveSelection && !data.selection))
    return std::nullopt;
  if (roi_ == RoiPolicy::ActiveSelection)
    return data.selection->mask(shape.x, shape.y);
  Image result(shape.x, shape.y, 1, PixelType::UInt8);
  for (const auto &roi : data.rois) {
    if ((roi.t && *roi.t != p.t) || (roi.c && *roi.c != p.c) ||
        (roi.z && *roi.z != p.z))
      continue;
    auto mask = roi.selection.mask(shape.x, shape.y);
    for (std::size_t i = 0; i < result.pixels.size(); ++i)
      if (mask.pixels[i] != 0)
        result.pixels[i] = 255;
  }
  return result;
}
ResolvedTarget resolve_target(const DocumentSnapshot &source,
                              const TargetRequest &request,
                              const std::string &operation) {
  validate_enums(request);
  auto shape = source.data().dataset.shape();
  const bool has_ranges = request.t || request.c || request.z;
  if ((request.scope != TargetScope::Ranges && has_ranges) ||
      (request.scope != TargetScope::Positions && !request.positions.empty()))
    throw std::invalid_argument(
        "Target scope and position/range fields disagree");
  auto ranges = std::array<AxisRange, 3>{
      resolve_range(request.t, source.display().t, shape.t),
      resolve_range(request.c, source.display().c, shape.c),
      resolve_range(request.z, source.display().crosshair[2], shape.z)};
  if (request.scope == TargetScope::AllZ)
    ranges[2] = {0, shape.z, 1};
  auto scope = request.scope == TargetScope::Positions ? TargetScope::Positions
                                                       : TargetScope::Ranges;
  if (scope == TargetScope::Positions) {
    ranges = {AxisRange{0, shape.t, 1}, AxisRange{0, shape.c, 1},
              AxisRange{0, shape.z, 1}};
    if (request.positions.empty())
      throw std::invalid_argument("An explicit target requires positions");
    std::set<std::array<int, 3>> seen;
    std::map<std::array<int, 2>, std::set<int>> volumes;
    for (auto p : request.positions) {
      validate_position(p, shape);
      if (!seen.insert(key(p)).second)
        throw std::invalid_argument("Target positions must be unique");
      if (request.mode == ExecutionMode::Volume)
        volumes[{p.t, p.c}].insert(p.z);
    }
    for (const auto &[tc, z] : volumes) {
      (void)tc;
      if (std::size_t(*z.rbegin() - *z.begin()) + 1 != z.size())
        throw std::invalid_argument(
            "Volume targets require contiguous Z planes");
    }
  } else if (request.mode == ExecutionMode::Volume && ranges[2].step != 1)
    throw std::invalid_argument("Volume targets require contiguous Z planes");
  auto result =
      ResolvedTarget(source, scope, std::move(ranges), request.positions,
                     request.roi, request.mode, request.destination);
  if (!operation.empty())
    result.validate_operation(operation);
  return result;
}
ResolvedTarget resolve_legacy_target(const DocumentSnapshot &source,
                                     bool all_slices,
                                     std::optional<int> slice_index,
                                     const std::string &operation) {
  int z = slice_index.value_or(source.display().crosshair[2]);
  validate_position({source.display().t, source.display().c, z},
                    source.data().dataset.shape());
  TargetRequest request;
  if (operation == "convert_image") {
    const auto shape = source.data().dataset.shape();
    request.scope = TargetScope::Ranges;
    request.t = AxisRange{0, shape.t, 1};
    request.c = AxisRange{0, shape.c, 1};
    request.z = AxisRange{0, shape.z, 1};
    request.roi = RoiPolicy::WholePlane;
    return resolve_target(source, request, operation);
  }
  request.scope = TargetScope::Ranges;
  request.z = all_slices ? AxisRange{0, source.data().dataset.shape().z, 1}
                         : AxisRange{z, z + 1, 1};
  return resolve_target(source, request, operation);
}
MetadataObject ResolvedTarget::record() const {
  MetadataArray axes, positions;
  if (scope_ == TargetScope::Ranges)
    for (const auto &range : ranges_)
      axes.push_back(metadata_array({range.start, *range.stop, range.step}));
  for (auto p : positions_)
    positions.push_back(metadata_array({p.t, p.c, p.z}));
  MetadataArray dimensions;
  for (auto n : source_.data().dataset.shape().dimensions())
    dimensions.emplace_back(n);
  const auto revisions = source_.revisions();
  return {
      {"schema_version", 1},
      {"document_id", source_.id()},
      {"source_revisions", metadata_array({revisions.pixels, revisions.metadata,
                                           revisions.selection})},
      {"source_shape", metadata_array(std::move(dimensions))},
      {"scope", scope_ == TargetScope::Positions ? "positions" : "ranges"},
      {"ranges", metadata_array(std::move(axes))},
      {"positions", metadata_array(std::move(positions))},
      {"roi_policy", roi_name(roi_)},
      {"execution_mode", mode_ == ExecutionMode::Planes ? "planes" : "volume"},
      {"destination", destination_ == OutputDestination::InPlace
                          ? "in_place"
                          : "new_document"}};
}
detail::TargetRecord detail::decode_target_record(const MetadataObject &record) {
  validate_metadata(record);
  const std::set<std::string> fields{
      "schema_version", "document_id", "source_revisions", "source_shape",
      "scope",          "ranges",      "positions",        "roi_policy",
      "execution_mode", "destination"};
  for (const auto &[name, value] : record) {
    (void)value;
    if (!fields.count(name))
      throw std::invalid_argument("Unknown target field: " + name);
  }
  if (unsigned_value(field(record, "schema_version")) != 1)
    throw std::invalid_argument("Unsupported target record version");
  const auto document_id = unsigned_value(field(record, "document_id"));
  if (document_id == 0)
    throw std::invalid_argument("Target record requires a positive document ID");
  TargetRequest request;
  auto roi = text_value(field(record, "roi_policy"));
  if (roi == "whole_plane")
    request.roi = RoiPolicy::WholePlane;
  else if (roi == "active_selection")
    request.roi = RoiPolicy::ActiveSelection;
  else if (roi == "positioned_rois")
    request.roi = RoiPolicy::PositionedRois;
  else
    throw std::invalid_argument("Unknown target ROI policy");
  auto mode = text_value(field(record, "execution_mode"));
  if (mode != "planes" && mode != "volume")
    throw std::invalid_argument("Unknown target execution mode");
  request.mode =
      mode == "planes" ? ExecutionMode::Planes : ExecutionMode::Volume;
  auto destination = text_value(field(record, "destination"));
  if (destination != "in_place" && destination != "new_document")
    throw std::invalid_argument("Unknown target destination");
  request.destination = destination == "in_place"
                            ? OutputDestination::InPlace
                            : OutputDestination::NewDocument;
  auto revisions = array_value(field(record, "source_revisions"));
  if (revisions.size() != 3)
    throw std::invalid_argument("Target record requires three data revisions");
  Revisions expected{unsigned_value(revisions[0]), unsigned_value(revisions[1]),
                     unsigned_value(revisions[2]), 0};
  const auto &dimensions = array_value(field(record, "source_shape"));
  if (dimensions.size() != 6)
    throw std::invalid_argument("Target record requires TCZYXS dimensions");
  DatasetShape shape{index_value(dimensions[0]), index_value(dimensions[1]),
                     index_value(dimensions[2]), index_value(dimensions[3]),
                     index_value(dimensions[4]), index_value(dimensions[5])};
  shape.validate();
  auto scope = text_value(field(record, "scope"));
  const auto &ranges = array_value(field(record, "ranges"));
  const auto &positions = array_value(field(record, "positions"));
  if (scope == "ranges") {
    request.scope = TargetScope::Ranges;
    if (ranges.size() != 3 || !positions.empty())
      throw std::invalid_argument(
          "Range target requires three ranges and no positions");
    std::array<AxisRange, 3> values;
    for (std::size_t i = 0; i < 3; ++i) {
      const auto &range = array_value(ranges[i]);
      if (range.size() != 3)
        throw std::invalid_argument(
            "Target range requires start, stop and step");
      values[i] = {index_value(range[0]), index_value(range[1]),
                   index_value(range[2])};
    }
    request.t = values[0];
    request.c = values[1];
    request.z = values[2];
    const std::array<int, 3> extents{shape.t, shape.c, shape.z};
    std::size_t count = 1;
    for (std::size_t i = 0; i < values.size(); ++i) {
      // Records contain fully resolved ranges. Validate their own bounds,
      // without using the destination's navigation or dataset dimensions.
      const auto range = resolve_range(values[i], 0, extents[i]);
      const auto n = range_count(range);
      if (count > std::numeric_limits<std::size_t>::max() / n)
        throw std::overflow_error("Target position count exceeds size_t");
      count *= n;
    }
    if (request.mode == ExecutionMode::Volume && values[2].step != 1)
      throw std::invalid_argument(
          "Volume targets require contiguous Z planes");
  } else if (scope == "positions") {
    request.scope = TargetScope::Positions;
    if (!ranges.empty())
      throw std::invalid_argument("Explicit targets cannot contain ranges");
    for (const auto &item : positions) {
      const auto &p = array_value(item);
      if (p.size() != 3)
        throw std::invalid_argument("Target position requires T,C,Z");
      request.positions.push_back(
          {index_value(p[0]), index_value(p[1]), index_value(p[2])});
    }
    if (request.positions.empty())
      throw std::invalid_argument("An explicit target requires positions");
    std::set<std::array<int, 3>> seen;
    std::map<std::array<int, 2>, std::set<int>> volumes;
    for (const auto p : request.positions) {
      validate_position(p, shape);
      if (!seen.insert(key(p)).second)
        throw std::invalid_argument("Target positions must be unique");
      if (request.mode == ExecutionMode::Volume)
        volumes[{p.t, p.c}].insert(p.z);
    }
    for (const auto &[tc, z] : volumes) {
      (void)tc;
      if (std::size_t(*z.rbegin() - *z.begin()) + 1 != z.size())
        throw std::invalid_argument(
            "Volume targets require contiguous Z planes");
    }
  } else
    throw std::invalid_argument("Unknown resolved target scope");
  return {document_id, expected, shape, std::move(request)};
}

ResolvedTarget detail::strict_target_from_record(
    const DocumentSnapshot &source, const TargetRecord &decoded,
    const std::string &operation) {
  if (decoded.document_id != source.id())
    throw std::invalid_argument("Target record belongs to another document");
  const Change dependencies = decoded.request.roi == RoiPolicy::WholePlane
                                  ? Change::Pixels | Change::Metadata
                                  : Change::Data;
  if (!source.revisions().matches(decoded.source_revisions, dependencies))
    throw RevisionConflict();
  if (decoded.source_shape.dimensions() !=
      source.data().dataset.shape().dimensions())
    throw std::invalid_argument("Target record source shape differs");
  return resolve_target(source, decoded.request, operation);
}

ResolvedTarget target_from_record(const DocumentSnapshot &source,
                                  const MetadataObject &record,
                                  const std::string &operation) {
  return detail::strict_target_from_record(
      source, detail::decode_target_record(record), operation);
}
} // namespace imagejxx
