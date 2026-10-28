#pragma once
#include "calibration_plot_payload.hpp"
#include <algorithm>
#include <cmath>
#include <optional>

namespace imagejxx::desktop {
struct PlotHoverSample {
  bool histogram=false,upper_inclusive=false;
  std::size_t index=0;
  double x=0,y=0,lower=0,upper=0;
  std::uint64_t count=0;
};
// Retained data only. Profile distances are validated nondecreasing at capture.
// Interior histogram boundaries select the right bin; the final edge is closed.
// Nearest-profile ties, including duplicate distances, use the first index.
inline std::optional<PlotHoverSample> plot_hover_sample(const CalibrationPlotPayload& payload,double x) {
  if(!std::isfinite(x))return {};
  if(payload.histogram) {
    const auto& h=payload.histogram->histogram;
    if(h.counts.empty() || x<h.edges.front() || x>h.edges.back())return {};
    const auto found=std::upper_bound(h.edges.begin(),h.edges.end(),x);
    const auto i=std::min(h.counts.size()-1,std::size_t(found-h.edges.begin()-1));
    return PlotHoverSample{true,i+1==h.counts.size(),i,h.edges[i]/2+h.edges[i+1]/2,double(h.counts[i]),h.edges[i],h.edges[i+1],h.counts[i]};
  }
  if(payload.kind!=PlotKind::Profile || payload.data.measured.empty())return {};
  const auto& distances=payload.data.measured;
  auto found=std::lower_bound(distances.begin(),distances.end(),x);
  std::size_t i=found==distances.end()?distances.size()-1:std::size_t(found-distances.begin());
  if(found!=distances.end() && i && std::abs(x-distances[i-1])<=std::abs(distances[i]-x))--i;
  i=std::size_t(std::lower_bound(distances.begin(),distances.end(),distances[i])-distances.begin());
  return PlotHoverSample{false,false,i,distances[i],payload.data.known[i],0,0,0};
}
} // namespace imagejxx::desktop
