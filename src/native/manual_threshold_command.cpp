#include <imagejxx/manual_threshold_command.hpp>
#include "session_metadata.hpp"
#include "target_record_internal.hpp"
#include <cmath>
#include <set>
#include <utility>

namespace imagejxx {
namespace {
using Json = detail::session_metadata::Json;
namespace codec = detail::session_metadata;
void fields(const Json &value, std::initializer_list<const char *> names) {
  if (!value.is_object() || value.size() != names.size())
    throw std::invalid_argument("Invalid manual threshold record fields");
  for (auto name : names)
    if (!value.contains(name))
      throw std::invalid_argument("Missing manual threshold record field");
}
void request_valid(const TargetRequest &request) {
  if (request.mode != ExecutionMode::Planes ||
      request.destination != OutputDestination::InPlace ||
      (request.roi != RoiPolicy::ActiveSelection && request.roi != RoiPolicy::WholePlane) ||
      !((request.scope == TargetScope::CurrentPlane) ||
        (request.scope == TargetScope::Positions && request.positions.size() == 1)))
    throw std::invalid_argument("Manual threshold requires one in-place captured plane and active or whole-plane ROI");
}
void source_valid(const DocumentSnapshot &source) {
  const auto &dataset = source.data().dataset;
  if (dataset.shape().s != 1 || !dataset.shared_planes() ||
      dataset.sample_policy() != FloatingSamplePolicy::FiniteOnly)
    throw std::invalid_argument("Manual threshold requires eager finite-only scalar data");
}
struct Record {
  ManualThresholdRange range;
  detail::TargetRecord target;
  std::optional<Selection> selection;
};
Record decode(const MetadataObject &record) {
  validate_metadata(record);
  const auto value = codec::to_json(record);
  fields(value, {"schema_version", "operation", "profile", "lower", "upper", "target", "selection"});
  if (!value["schema_version"].is_number_integer() || value["schema_version"] != 1 ||
      value["operation"] != "threshold" || value["profile"] != "manual_raw_inclusive_v1")
    throw std::invalid_argument("Unsupported manual threshold command record");
  // Accept only lossless real bounds, including JSON integers. Booleans and
  // large integers must not silently become a different threshold.
  auto number = [&](const char *key) {
    const auto &item = value[key];
    if (!item.is_number()) throw std::invalid_argument("Threshold bounds require numbers");
    const double result = item.get<double>();
    if (!std::isfinite(result)) throw std::invalid_argument("Threshold bounds must be finite");
    if (item.is_number_unsigned()) {
      if (result >= std::ldexp(1.0, 64) || static_cast<std::uint64_t>(result) != item.get<std::uint64_t>())
        throw std::invalid_argument("Threshold bound loses integer precision");
    } else if (item.is_number_integer()) {
      if (result >= std::ldexp(1.0, 63) || result < -std::ldexp(1.0, 63) ||
          static_cast<std::int64_t>(result) != item.get<std::int64_t>())
        throw std::invalid_argument("Threshold bound loses integer precision");
    }
    return result;
  };
  ManualThresholdRange range{number("lower"), number("upper")};
  range.validate();
  auto target = detail::decode_target_record(codec::object_from_json(value["target"]));
  request_valid(target.request);
  if (target.source_shape.s != 1 || target.request.scope != TargetScope::Positions)
    throw std::invalid_argument("Threshold record requires a captured scalar position");
  std::optional<Selection> selection;
  if (!value["selection"].is_null()) {
    selection = codec::selection_from_json(value["selection"]);
    selection->validate(target.source_shape.x, target.source_shape.y);
    if (codec::selection_to_json(*selection) != value["selection"])
      throw std::invalid_argument("Threshold selection must be canonical");
  }
  if (target.request.roi == RoiPolicy::WholePlane && selection)
    throw std::invalid_argument("Whole-plane threshold cannot claim an active ROI");
  return {range, std::move(target), std::move(selection)};
}
} // namespace

ResolvedManualThresholdCommand::ResolvedManualThresholdCommand(
    ManualThresholdRange range, ResolvedTarget target)
    : range_(range), target_(std::move(target)) {}
OperationParameters ResolvedManualThresholdCommand::parameters() const {
  return {{"lower", range_.lower}, {"upper", range_.upper}};
}
MetadataObject ResolvedManualThresholdCommand::record() const {
  const auto &selection = target_.source().data().selection;
  return {{"schema_version", 1}, {"operation", std::string("threshold")},
      {"profile", std::string("manual_raw_inclusive_v1")},
      {"lower", range_.lower}, {"upper", range_.upper},
      {"target", metadata_object(target_.record())},
      {"selection", target_.roi_policy() == RoiPolicy::ActiveSelection && selection
          ? codec::from_json(codec::selection_to_json(*selection)) : MetadataValue{std::monostate{}}}};
}
ResolvedManualThresholdCommand resolve_manual_threshold_command(
    const DocumentSnapshot &source, ManualThresholdRange range, const TargetRequest &request) {
  range.validate(); source_valid(source); request_valid(request);
  // Normalize CurrentPlane now; replay never consults a later camera position.
  auto captured = request;
  if (captured.scope == TargetScope::CurrentPlane) {
    if (captured.t || captured.c || captured.z || !captured.positions.empty())
      throw std::invalid_argument("Current-plane threshold cannot carry ranges or positions");
    captured.scope = TargetScope::Positions;
    captured.positions = {{source.display().t, source.display().c, source.display().crosshair[2]}};
  }
  return {range, resolve_target(source, captured, "threshold")};
}
void validate_manual_threshold_command_record(const MetadataObject &record) { (void)decode(record); }
ResolvedManualThresholdCommand manual_threshold_command_from_record(
    const DocumentSnapshot &source, const MetadataObject &record) {
  const auto decoded = decode(record); source_valid(source);
  auto target = detail::strict_target_from_record(source, decoded.target, "threshold");
  if (target.roi_policy() == RoiPolicy::ActiveSelection && decoded.selection != source.data().selection)
    throw std::invalid_argument("Threshold record selection differs from its source");
  return {decoded.range, std::move(target)};
}
ResolvedManualThresholdCommand rebind_manual_threshold_command(
    const DocumentSnapshot &source, const MetadataObject &record, const TargetRequest &request) {
  const auto decoded = decode(record);
  return resolve_manual_threshold_command(source, decoded.range, request);
}
} // namespace imagejxx
