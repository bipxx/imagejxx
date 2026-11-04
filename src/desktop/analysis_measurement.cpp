#include "analysis_measurement.hpp"
#include "render_memory.hpp"
#include "../native/kernel_control.hpp"
#include <cmath>

namespace imagejxx::desktop {
bool captured_measurement_available(const DocumentSnapshot& source) {
  const auto& data=source.data();const auto& dataset=data.dataset;
  if(dataset.shape().s!=1 || dataset.sample_policy()!=FloatingSamplePolicy::FiniteOnly ||
      (dataset.pixel_type()!=PixelType::UInt8 && dataset.pixel_type()!=PixelType::UInt16 &&
       dataset.pixel_type()!=PixelType::Float32))return false;
  if(!data.selection)return true;
  const auto& kind=data.selection->kind;
  return kind!="line" && kind!="polyline" && kind!="angle" && kind!="point" && kind!="multipoint";
}
std::size_t captured_measurement_row_bytes(const ResolvedTargetAnalysis& command) {
  const auto& source=command.target().source();
  auto text=RenderMemory::add(source.data().metadata.title.size(),command.spatial().unit.size());
  text=RenderMemory::add(text,command.intensity().value_unit().size());
  // Map/value/key holders plus owned strings and formatting slack. No sampled
  // array is retained by a measurement row. Getter/externally injected rows are
  // outside this managed frontend allocation.
  return RenderMemory::mul(command.estimate().plane_count,RenderMemory::add(8192,RenderMemory::mul(text,8)));
}
namespace {
Json scalar(double value) {
  if(std::isfinite(value))return value;
  // Preserve non-finite numerical meaning in JSON/CSV rather than losing it to
  // JSON null. Empty populations and non-finite density tables are deliberate.
  return std::isnan(value)?"nan":value>0?"inf":"-inf";
}
}
Json captured_measurement_rows(const ResolvedTargetAnalysis& command,const TargetAnalysisResult& result) {
  if(result.planes.size()!=command.estimate().plane_count)
    throw std::invalid_argument("Measurement result differs from its captured extent");
  Json rows=Json::array();rows.get_ref<Json::array_t&>().reserve(result.planes.size());
  const auto& source=command.target().source();
  std::size_t ordinal=0;
  for(const auto& row:result.planes) {
    kernel_checkpoint();
    if(!row.statistics)throw std::invalid_argument("Measurement requires scalar statistics");
    const auto& s=*row.statistics;
    Json values{{"Image",source.data().metadata.title},{"Slice",row.position.z+1},
      {"T",row.position.t+1},{"C",row.position.c+1},{"Z",row.position.z+1},
      {"count",s.pixel_count},{"minimum",scalar(s.minimum)},{"maximum",scalar(s.maximum)},
      {"mean",scalar(s.mean)},{"stddev",scalar(s.stddev)},{"median",scalar(s.median)},
      {"sum",scalar(s.mean*double(s.pixel_count))},{"integrated_density",scalar(s.integrated_density)},
      {"Area",scalar(s.area)},{"Unit",result.spatial.unit},{"Value unit",command.intensity().value_unit()}};
    if(command.target().roi_policy()==RoiPolicy::ActiveSelection && source.data().selection) {
      values["ROI"]=source.data().selection->kind;
      // Preserve the existing tight raster Bounds column. The preparation
      // currently returns statistics without mask bounds; this serial raster
      // fits its admitted mask workspace and performs no provider read.
      const auto mask=command.target().mask(ordinal);
      int x0=mask->width,y0=mask->height,x1=-1,y1=-1;
      for(std::size_t i=0;i<mask->pixels.size();++i) {
        kernel_checkpoint_periodic();
        if(mask->pixels[i]){const auto x=int(i%mask->width),y=int(i/mask->width);
          x0=std::min(x0,x);y0=std::min(y0,y);x1=std::max(x1,x);y1=std::max(y1,y);}
      }
      values["Bounds"]=x1<0?Json():Json::array({x0,y0,x1-x0+1,y1-y0+1});
    }
    rows.push_back(std::move(values));
    ++ordinal;
  }
  return rows;
}
}
