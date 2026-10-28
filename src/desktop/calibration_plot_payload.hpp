#pragma once

#include "calibration_plot_annotation.hpp"
#include "render_memory.hpp"
#include <imagejxx/imagej_intensity_plot.hpp>
#include <memory>

namespace imagejxx::desktop {

// Creator-thread retained plot data. No document, provider or transaction is
// held here; snapshots share this one immutable allocation and its one lease.
struct CalibrationPlotPayload {
  ImageJIntensityFitPlot data;
  CalibrationPlotAnnotation annotation;
  std::string source_title;
  bool table=false;
  std::uint64_t source_id=0,payload_id=0;
  RenderMemory::Lease memory;
  CalibrationPlotPayload(ImageJIntensityFitPlot plot,CalibrationPlotAnnotation notes,
                         bool isTable,std::uint64_t source,std::string sourceTitle)
      :data(std::move(plot)),annotation(std::move(notes)),source_title(std::move(sourceTitle)),table(isTable),source_id(source){}

  static std::size_t bytes(const ImageJIntensityFitPlot& data,
                           const CalibrationPlotAnnotation& annotation,const std::string& sourceTitle) {
    auto total=RenderMemory::add(sizeof(CalibrationPlotPayload),256);
    for(const auto* values:{&data.measured,&data.known})
      total=RenderMemory::add(total,RenderMemory::mul(values->capacity(),sizeof(double)));
    for(const auto* values:{&data.curve_x,&data.curve_y})
      total=RenderMemory::add(total,RenderMemory::mul(values->capacity(),sizeof(float)));
    for(const auto* text:{&data.function,&data.value_unit,&data.objective,
                         &annotation.formula,&annotation.coefficients,&annotation.detail,&annotation.objective,&annotation.canvas_text})
      total=RenderMemory::add(total,RenderMemory::add(text->capacity(),1));
    return RenderMemory::add(total,RenderMemory::add(sourceTitle.capacity(),1));
  }
};

inline std::shared_ptr<const CalibrationPlotPayload> retain_calibration_plot(
    RenderMemory& memory,ImageJIntensityFitPlot data,CalibrationPlotAnnotation annotation,
    bool table,std::uint64_t source,std::uint64_t payloadId=0,std::string sourceTitle={}) {
  annotation.canvas_text=calibration_plot_canvas_text(annotation);
  const auto bytes=CalibrationPlotPayload::bytes(data,annotation,sourceTitle);
  auto working=memory.acquire(bytes);
  auto result=std::make_shared<CalibrationPlotPayload>(std::move(data),std::move(annotation),table,source,std::move(sourceTitle));
  result->payload_id=payloadId;
  result->memory=working.retain(bytes);
  return result;
}

} // namespace imagejxx::desktop
