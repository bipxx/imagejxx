#pragma once
#include "calibration_plot_payload.hpp"
#include <algorithm>
#include <cmath>

namespace imagejxx::desktop {
inline std::shared_ptr<const CalibrationPlotPayload> retain_profile_plot(RenderMemory& memory,
    std::vector<double> x,std::vector<double> y,std::string title,std::string xLabel,
    std::uint64_t source,std::uint64_t payloadId,std::string sourceTitle,std::string context={}) {
  if(x.empty() || x.size()!=y.size())throw std::invalid_argument("Profile plot requires matching nonempty samples");
  for(std::size_t i=0;i<x.size();++i)if(!std::isfinite(x[i]) || !std::isfinite(y[i]) || (i && x[i]<x[i-1]))throw std::invalid_argument("Profile plot requires finite samples and nondecreasing distances");
  ImageJIntensityFitPlot data;data.function=std::move(title);data.measured=std::move(x);data.known=std::move(y);
  const auto xb=std::minmax_element(data.measured.begin(),data.measured.end()),yb=std::minmax_element(data.known.begin(),data.known.end());
  data.x_min=*xb.first;data.x_max=*xb.second;data.y_min=*yb.first;data.y_max=*yb.second;
  CalibrationPlotAnnotation annotation;
  auto bytes=CalibrationPlotPayload::bytes(data,annotation,sourceTitle,context);
  bytes=RenderMemory::add(bytes,RenderMemory::add(xLabel.capacity()+1,64));
  auto working=memory.acquire(bytes);
  auto result=std::make_shared<CalibrationPlotPayload>(std::move(data),std::move(annotation),false,source,std::move(sourceTitle),std::move(context));
  result->kind=PlotKind::Profile;result->x_axis=std::move(xLabel);result->y_axis="Pixel value (raw)";
  result->payload_id=payloadId;result->memory=working.retain(bytes);return result;
}
inline std::shared_ptr<const CalibrationPlotPayload> retain_histogram_plot(RenderMemory& memory,
    std::shared_ptr<const DatasetHistogramResult> histogram,std::uint64_t source,
    std::uint64_t payloadId,std::string sourceTitle,bool rawCsv=true,std::string context={}) {
  if(!histogram || histogram->request.area_roi || histogram->histogram.counts.empty() ||
      histogram->histogram.edges.size()!=histogram->histogram.counts.size()+1)
    throw std::invalid_argument("Histogram plot requires standalone bins without a retained source selection");
  const auto& h=histogram->histogram;
  for(std::size_t i=0;i<h.edges.size();++i)if(!std::isfinite(h.edges[i]) || (i && h.edges[i]<h.edges[i-1]))throw std::invalid_argument("Histogram plot requires finite ordered edges");
  ImageJIntensityFitPlot data;data.function="Histogram";data.x_min=h.edges.front();data.x_max=h.edges.back();
  data.y_min=0;data.y_max=double(*std::max_element(h.counts.begin(),h.counts.end()));
  CalibrationPlotAnnotation annotation;
  auto bytes=CalibrationPlotPayload::bytes(data,annotation,sourceTitle,context);
  bytes=RenderMemory::add(bytes,RenderMemory::add(sizeof(DatasetHistogramResult),128));
  bytes=RenderMemory::add(bytes,RenderMemory::mul(h.counts.capacity(),sizeof(std::uint64_t)));
  bytes=RenderMemory::add(bytes,RenderMemory::mul(h.edges.capacity(),sizeof(double)));
  auto working=memory.acquire(bytes);
  auto result=std::make_shared<CalibrationPlotPayload>(std::move(data),std::move(annotation),false,source,std::move(sourceTitle),std::move(context));
  result->kind=PlotKind::Histogram;result->histogram_raw_csv=rawCsv;result->histogram=std::move(histogram);result->x_axis="Pixel value (raw)";result->y_axis="Count";
  result->payload_id=payloadId;result->memory=working.retain(bytes);return result;
}
} // namespace imagejxx::desktop
