#pragma once

#include "calibration_plot_annotation.hpp"
#include "render_memory.hpp"
#include <imagejxx/imagej_intensity_plot.hpp>
#include <memory>
#include <imagejxx/dataset_histogram.hpp>
#include <sstream>
#include <locale>

namespace imagejxx::desktop {

enum class PlotKind { Fit, CalibrationTable, Histogram, Profile };

// Creator-thread retained plot data. No document, provider or transaction is
// held here; snapshots share this one immutable allocation and its one lease.
struct CalibrationPlotPayload {
  ImageJIntensityFitPlot data;
  CalibrationPlotAnnotation annotation;
  std::string source_title,x_axis="pixel value",y_axis;
  PlotKind kind=PlotKind::Fit;
  std::shared_ptr<const DatasetHistogramResult> histogram;
  bool table=false,histogram_raw_csv=true;
  std::uint64_t source_id=0,payload_id=0;
  RenderMemory::Lease memory;
  CalibrationPlotPayload(ImageJIntensityFitPlot plot,CalibrationPlotAnnotation notes,
                         bool isTable,std::uint64_t source,std::string sourceTitle)
      :data(std::move(plot)),annotation(std::move(notes)),source_title(std::move(sourceTitle)),table(isTable),source_id(source){kind=isTable?PlotKind::CalibrationTable:PlotKind::Fit;}

  bool calibration() const noexcept {return kind==PlotKind::Fit || kind==PlotKind::CalibrationTable;}
  const char* kindName() const noexcept {return kind==PlotKind::Fit?"fit":kind==PlotKind::CalibrationTable?"calibration_table":kind==PlotKind::Histogram?"histogram":"profile";}
  const std::string& yLabel() const noexcept {return calibration()?data.value_unit:y_axis;}
  std::size_t curveSize() const noexcept {return histogram?histogram->histogram.counts.size():calibration()?data.curve_x.size():data.measured.size();}
  double curveX(std::size_t i) const {return histogram?histogram->histogram.edges[i]/2+histogram->histogram.edges[i+1]/2:calibration()?double(data.curve_x[i]):data.measured[i];}
  double curveY(std::size_t i) const {return histogram?double(histogram->histogram.counts[i]):calibration()?double(data.curve_y[i]):data.known[i];}
  std::string caption() const {return calibration()?std::string(table?"Calibration table: ":"Calibration fit: ")+data.function:data.function;}
  std::string genericCsv() const {
    if(histogram && histogram_raw_csv)return dataset_histogram_csv(*histogram);
    std::ostringstream stream;stream.imbue(std::locale::classic());stream.precision(17);stream<<"x,y\n";
    for(std::size_t i=0;i<curveSize();++i){stream<<curveX(i)<<',';if(histogram)stream<<histogram->histogram.counts[i];else stream<<curveY(i);stream<<'\n';}
    return stream.str();
  }
  std::string csv() const {
    if(!calibration())return genericCsv();
    std::ostringstream stream;stream.imbue(std::locale::classic());stream.precision(17);stream<<"series,x,y\n";
    for(std::size_t i=0;i<data.measured.size();++i)stream<<"data,"<<data.measured[i]<<','<<data.known[i]<<'\n';
    for(std::size_t i=0;i<data.curve_x.size();++i){stream<<(table?"table,":"fit,")<<data.curve_x[i]<<',';if(std::isfinite(data.curve_y[i]))stream<<data.curve_y[i];stream<<'\n';}
    return stream.str();
  }
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
