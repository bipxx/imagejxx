#pragma once
#include <imagejxx/execution.hpp>
#include <imagejxx/imagej_projection_execution.hpp>

namespace imagejxx::detail {
// One original source snapshot and its optional application calibration capture.
// Geometry and output policies remain owned by each existing factory.
struct CalibrationPreparation {
  static ResolvedTarget target(const ResolvedTarget&, const TargetRequest&);
  static PreparedOperation prepare_imagej_projection(const ResolvedTarget&,
      const ImageJProjectionDocumentRequest&, const ExecutionControl&);
  static PreparedOperation prepare_axis_plane_export(const ResolvedTarget&,
      const AxisPlaneExportRequest&, const ExecutionControl&);
  static PreparedOperation prepare_axis_range_reslice(const ResolvedTarget&,
      const AxisRangeResliceRequest&, const ExecutionControl&);
  static PreparedOperation prepare_axis_range_orientation(const ResolvedTarget&,
      const AxisRangeOrientationRequest&, const ExecutionControl&);
  static PreparedOperation prepare_axis_range_spacing(const ResolvedTarget&,
      const AxisRangeSpacingRequest&, const ExecutionControl&);
  static PreparedOperation prepare_axis_range_slicer_resize(const ResolvedTarget&,
      const AxisRangeSlicerResizeRequest&, const ExecutionControl&);
};
ImageJProjectionDocumentEstimate estimate_calibrated_projection(
    const DocumentSnapshot&, const EffectiveCalibration*,
    const ImageJProjectionDocumentRequest&);
} // namespace imagejxx::detail
