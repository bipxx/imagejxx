#pragma once
#include <imagejxx/target_analysis.hpp>
#include <imagejxx/roi_style.hpp>

namespace imagejxx {
inline constexpr const char* measurement_overlay_property="imagejxx.measurement_overlay";
inline constexpr const char* measurement_overlay_row="imagejxx.measurement_overlay_row";
struct MeasurementOverlayOptions {
  bool enabled=false,composite=false,draw_labels=true;
};
struct MeasurementOverlayLimits {
  std::size_t max_overlays=100000,max_results_rows=1000000,max_name_bytes=4096;
  std::size_t max_working_bytes=64ULL*1024*1024;
};
struct MeasurementOverlayPresentation {
  bool present=false,draw_labels=false,draw_names=false,draw_backgrounds=false;
  RoiColor label_color{255,255,255,255};
};
// Strict owned V1 property; absence is an ordinary legacy document.
MeasurementOverlayPresentation measurement_overlay_presentation(const MetadataObject&);
class ResolvedMeasurementOverlay {
public:
  const ResolvedTargetAnalysis& analysis() const noexcept {return analysis_;}
  MeasurementOverlayOptions options() const noexcept {return options_;}
  bool will_add() const noexcept {return add_;}
  std::size_t result_count() const noexcept {return result_count_;}
  std::size_t working_bytes() const noexcept {return working_bytes_;}
  void validate_source(const DocumentSnapshot&) const;
private:
  friend ResolvedMeasurementOverlay resolve_measurement_overlay(const ResolvedTargetAnalysis&,
      std::size_t,const MeasurementOverlayOptions&,const MeasurementOverlayLimits&);
  friend std::optional<DocumentTransaction> prepare_measurement_overlay(const ResolvedMeasurementOverlay&,std::size_t);
  ResolvedMeasurementOverlay(ResolvedTargetAnalysis analysis,MeasurementOverlayOptions options,
      MeasurementOverlayLimits limits,bool add,std::size_t count,std::size_t bytes)
      :analysis_(std::move(analysis)),options_(options),limits_(limits),add_(add),result_count_(count),working_bytes_(bytes){}
  ResolvedTargetAnalysis analysis_;
  MeasurementOverlayOptions options_;
  MeasurementOverlayLimits limits_;
  bool add_;
  std::size_t result_count_,working_bytes_;
};
// Metadata-only and callback-free preflight. No source pixels or owned geometry
// tree are acquired. Manager Measure deliberately ignores ADD_TO_OVERLAY.
ResolvedMeasurementOverlay resolve_measurement_overlay(const ResolvedTargetAnalysis&,
    std::size_t results_before,const MeasurementOverlayOptions& = {},const MeasurementOverlayLimits& = {});
// Captured single-XY-selection clone. The prepared transaction changes overlay
// metadata only; numerical rows are published by the caller after guarded commit.
// cap=0 uses the captured bound. Disabled/manager/no-selection returns nullopt.
std::optional<DocumentTransaction> prepare_measurement_overlay(const ResolvedMeasurementOverlay&,
    std::size_t max_working_bytes=0);
} // namespace imagejxx
