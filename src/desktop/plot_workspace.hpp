#pragma once

#include "desktop.hpp"
#include "calibration_plot_payload.hpp"
#include "calibration_plot_view.hpp"
#include "calibration_plot_style.hpp"

namespace imagejxx::desktop {

// Detached plotted output, not an executable analysis request or source binding.
struct PlotWorkspaceEntry {
  std::shared_ptr<const CalibrationPlotPayload> payload;
  CalibrationPlotView view;
  CalibrationPlotStyle style;
};
struct PlotWorkspaceRecord {
  Json value;
  RenderMemory::Lease memory;
};
struct DecodedPlotWorkspace {
  std::vector<PlotWorkspaceEntry> entries;
  std::uint64_t next_payload_id=0;
  RenderMemory::Lease memory;
};
inline constexpr const char* plot_workspace_key="retained_numeric_plots";
PlotWorkspaceRecord encodePlotWorkspace(const std::vector<PlotWorkspaceEntry>&,
                                        RenderMemory&);
DecodedPlotWorkspace decodePlotWorkspace(const Json&,RenderMemory&,
                                         std::uint64_t firstPayloadId);

} // namespace imagejxx::desktop
