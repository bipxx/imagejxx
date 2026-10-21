#pragma once
#include <imagejxx/color_components.hpp>
#include <imagejxx/rgb_dataset_command.hpp>

namespace imagejxx {
enum class ColorStackAction { Split, Merge, FlattenRgb };
enum class ColorStackLayoutPolicy { PreserveAxes, ImageJLegacy };
struct ColorStackRequest {
  ColorStackAction action = ColorStackAction::Split;
  ColorComponentSpace space = ColorComponentSpace::RGB;
  ColorStackLayoutPolicy layout = ColorStackLayoutPolicy::PreserveAxes;
  OutputDestination destination = OutputDestination::NewDocument;
  bool invert_rgb = false;
  // Legacy layouts can turn T/C into Z or consume physical Z as components.
  // Consent resets the entire spatial/time calibration to pixels/frame indices;
  // the complete original scalar metadata is retained as provenance.
  bool allow_calibration_reset = false;
  bool allow_nonxy_annotation_drop = false;
  std::size_t max_working_bytes = 512ULL*1024*1024;
};
struct ColorStackLayout {
  DatasetShape shape;
  PixelType pixel_type;
  SpatialCalibration calibration;
  bool calibration_reset = false;
  std::size_t output_bytes = 0, working_bytes = 0;
};
// Default split: RGB S3 -> C*3 scalar components. Default merge: consecutive
// groups of three C -> RGB S3; RGB also accepts exactly two C (blue zero).
// T/Z, spatial calibration, XY selection and positioned annotations survive.
// Legacy RGB split requires C1; legacy HSB split flattens input T/Z/C (C-fast)
// into Z. Legacy merge consumes an entire 2/3-plane stack. FlattenRgb renders
// scalar input or retains existing UInt8 RGB, preserves C1 Z/T, otherwise
// flattens all T/Z/C into Z.
ColorStackLayout describe_color_stack(DatasetShape,PixelType,FloatingSamplePolicy,
    const SpatialCalibration &,const ColorStackRequest &);
class ResolvedColorStackCommand {
public:
  const DocumentSnapshot &source() const{return source_;}
  const ColorStackRequest &request() const{return request_;}
  const ColorStackLayout &layout() const{return layout_;}
  const MetadataObject &record() const{return record_;}
private:
  friend ResolvedColorStackCommand resolve_color_stack_command(const DocumentSnapshot &,const ColorStackRequest &);
  friend ResolvedColorStackCommand resolve_color_stack_command(const DocumentSnapshot &,const ColorStackRequest &,const std::optional<RgbDatasetRequest> &);
  ResolvedColorStackCommand(DocumentSnapshot,ColorStackRequest,ColorStackLayout,MetadataObject);
  DocumentSnapshot source_;
  ColorStackRequest request_;
  ColorStackLayout layout_;
  MetadataObject record_;
};
ResolvedColorStackCommand resolve_color_stack_command(const DocumentSnapshot &,const ColorStackRequest & = {});
// Scalar FlattenRgb captures PerPlane rendering before legacy C-fast reindexing.
// An explicit request freezes range/LUT/channel intent; absent ranges defer a
// common opening-plane reset range to preparation. No samples are read here.
// Existing RGB and split/merge retain their closed V1 records. Scalar flatten
// emits V2 with a complete nested RGB rendering capture; no DTO layout changes.
ResolvedColorStackCommand resolve_color_stack_command(const DocumentSnapshot &,const ColorStackRequest &,
    const std::optional<RgbDatasetRequest> &);
void validate_color_stack_command_record(const MetadataObject &);
ResolvedColorStackCommand color_stack_command_from_record(const DocumentSnapshot &,const MetadataObject &);
ResolvedColorStackCommand rebind_color_stack_command(const DocumentSnapshot &,const MetadataObject &,const ColorStackRequest &);
PreparedOperation prepare_color_stack(const ResolvedColorStackCommand &,const ExecutionControl & = {},
    std::size_t execution_max_working_bytes = std::numeric_limits<std::size_t>::max());
} // namespace imagejxx
