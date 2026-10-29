#pragma once
#include <imagejxx/execution.hpp>
#include <imagejxx/imagej_projection_execution.hpp>
#include <variant>

namespace imagejxx {
using CalibratedCommandRequest = std::variant<AxisPlaneExportRequest,
    AxisRangeResliceRequest, AxisRangeOrientationRequest, AxisRangeSpacingRequest,
    AxisRangeSlicerResizeRequest, ImageJProjectionDocumentRequest>;

struct CalibratedCommandLimits {
  std::size_t max_record_bytes = 16ULL * 1024 * 1024;
  std::size_t max_working_bytes = 512ULL * 1024 * 1024;
};

// Additive record grammar. Existing source-only command classes/formats stay
// unchanged. The capture keeps the original source and its publication guard;
// the descriptor records effective geometry separately from local provenance.
class ResolvedCalibratedCommand {
public:
  const ResolvedTarget& capture() const;
  const CalibratedCommandRequest& request() const;
  const CalibratedCommandLimits& limits() const;
  const std::string& operation() const;
  const MetadataObject& record() const;
  std::size_t working_bytes() const;
private:
  struct State;
  explicit ResolvedCalibratedCommand(std::shared_ptr<const State>);
  friend ResolvedCalibratedCommand resolve_calibrated_command(
      const ResolvedTarget&, const CalibratedCommandRequest&, CalibratedCommandLimits);
  std::shared_ptr<const State> state_;
};

// Metadata-only; no source samples or user callbacks. Resolution freezes the
// supplied target, not current Application state. Publication validates it.
ResolvedCalibratedCommand resolve_calibrated_command(const ResolvedTarget&,
    const CalibratedCommandRequest&, CalibratedCommandLimits = {});
void validate_calibrated_command_record(const MetadataObject&);
// Explicit current capture required. Match local source, effective values and
// binding/bypass policy; serialized records never restore a live app identity.
ResolvedCalibratedCommand calibrated_command_from_record(
    const ResolvedTarget& current_capture, const MetadataObject&);
// Validate original claims first, then resolve the explicit replacement request
// against the caller's new capture. Rebinding cannot change operation families.
ResolvedCalibratedCommand rebind_calibrated_command(const ResolvedTarget&,
    const MetadataObject&, const CalibratedCommandRequest&,
    CalibratedCommandLimits = {});
PreparedOperation prepare_calibrated_command(const ResolvedCalibratedCommand&,
    const ExecutionControl& = {},
    std::size_t runtime_max_working_bytes = std::numeric_limits<std::size_t>::max());
} // namespace imagejxx
