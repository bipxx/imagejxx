#include "analysis_measurement.hpp"
#include "render_memory.hpp"
#include "../native/kernel_control.hpp"
#include <cmath>

namespace imagejxx::desktop {
bool captured_profile_available(const DocumentSnapshot& source) {
  const auto& data=source.data();const auto& dataset=data.dataset;
  if(dataset.shape().s!=1 || dataset.sample_policy()!=FloatingSamplePolicy::FiniteOnly ||
      (dataset.pixel_type()!=PixelType::UInt8 && dataset.pixel_type()!=PixelType::UInt16 && dataset.pixel_type()!=PixelType::Float32))return false;
  if(!data.selection)return false;const auto& s=*data.selection;
  if((s.kind!="line" && s.kind!="polyline") || s.width>1 || s.points.size()<2 || s.points.size()>4096)return false;
  const auto shape=dataset.shape();
  for(const auto& p:s.points)if(p[0]<0 || p[1]<0 || p[0]>shape.x-1 || p[1]>shape.y-1)return false;
  return true;
}
bool captured_measurement_available(const DocumentSnapshot& source) {
  const auto& data=source.data();const auto& dataset=data.dataset;
  if(dataset.shape().s!=1 || dataset.sample_policy()!=FloatingSamplePolicy::FiniteOnly ||
      (dataset.pixel_type()!=PixelType::UInt8 && dataset.pixel_type()!=PixelType::UInt16 &&
       dataset.pixel_type()!=PixelType::Float32))return false;
  if(!data.selection)return true;
  const auto& kind=data.selection->kind;
  if(kind=="polyline" && data.selection->points.size()==1 && data.selection->width<=1) {
    const auto& p=data.selection->points.front();const auto shape=dataset.shape();
    return p[0]>=0 && p[1]>=0 && p[0]<=shape.x-1 && p[1]<=shape.y-1;
  }
  if(kind=="line" || kind=="polyline")return captured_profile_available(source);
  if(kind=="angle")return data.selection->points.size()==3;
  if(kind=="point" || kind=="multipoint")return !data.selection->points.empty();
  return kind!="line" && kind!="polyline" && kind!="angle" && kind!="point" && kind!="multipoint";
}
bool captured_roi_measurement_available(const DocumentSnapshot& source) {
  const auto& dataset=source.data().dataset;
  return dataset.shape().s==1 && dataset.sample_policy()==FloatingSamplePolicy::FiniteOnly &&
      (dataset.pixel_type()==PixelType::UInt8 || dataset.pixel_type()==PixelType::UInt16 || dataset.pixel_type()==PixelType::Float32);
}
std::size_t captured_measurement_row_bytes(const ResolvedTargetAnalysis& command) {
  const auto& source=command.target().source();
  auto text=RenderMemory::add(source.data().metadata.title.size(),command.spatial().unit.size());
  text=RenderMemory::add(text,command.intensity().value_unit().size());
  if(command.options().redirect) {
    text=RenderMemory::add(text,command.options().redirect->source().data().metadata.title.size());
    text=RenderMemory::add(text,command.target().calibration().unit.size());
  }
  // Map/value/key holders plus owned strings and formatting slack. No sampled
  // array is retained by a measurement row. Getter/externally injected rows are
  // outside this managed frontend allocation.
  auto bytes=RenderMemory::mul(command.estimate().row_count,RenderMemory::add(8192,RenderMemory::mul(text,8)));
  if(command.options().display_label) {
    const auto& title=command.options().redirect?command.options().redirect->source().data().metadata.title:source.data().metadata.title;
    // The captured Label owns another title plus bounded UTF16 name/slice text.
    const auto label=RenderMemory::add(title.size(),272);
    bytes=RenderMemory::add(bytes,RenderMemory::mul(command.estimate().row_count,RenderMemory::mul(label,8)));
  }
  if(command.options().measurement==TargetMeasurementMode::RoiManager) {
    const auto& rois=source.data().rois;const auto& indices=command.options().roi_indices;
    const auto parent_count=indices.empty()?rois.size():indices.size();
    for(std::size_t n=0;n<parent_count;++n) {
      const auto& roi=rois[indices.empty()?n:indices[n]];
      const auto count=roi.selection.kind=="point" || roi.selection.kind=="multipoint"?roi.selection.points.size():1;
      bytes=RenderMemory::add(bytes,RenderMemory::mul(RenderMemory::mul(roi.name.size(),8),count));
    }
  }
  return bytes;
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
  if(result.planes.size()!=command.estimate().row_count)
    throw std::invalid_argument("Measurement result differs from its captured extent");
  Json rows=Json::array();rows.get_ref<Json::array_t&>().reserve(result.planes.size());
  const auto& source=command.target().source();
  for(const auto& row:result.planes) {
    kernel_checkpoint();
    if(!row.statistics)throw std::invalid_argument("Measurement requires scalar statistics");
    const auto& s=*row.statistics;
    Json values{{"Image",source.data().metadata.title},{"Slice",row.position.z+1},
      {"T",row.position.t+1},{"C",row.position.c+1},{"Z",row.position.z+1},
      {"count",s.pixel_count},{"minimum",scalar(s.minimum)},{"maximum",scalar(s.maximum)},
      {"mean",scalar(s.mean)},{"stddev",scalar(s.stddev)},{"median",scalar(s.median)},
      {"sum",scalar(s.mean*double(s.pixel_count))},{"integrated_density",scalar(s.integrated_density)},
      {"Mode",scalar(s.mode_value)},{"IntDen",scalar(s.integrated_density)},
      {"RawIntDen",scalar(double(s.pixel_count)*s.raw_mean)},
      {"Area",scalar(s.area)},{"Unit",result.spatial.unit},{"Value unit",command.intensity().value_unit()}};
    if(row.label)values["Label"]=*row.label;
    if(row.area_fraction)values["%Area"]=scalar(*row.area_fraction);
    if(row.skewness)values["Skew"]=scalar(*row.skewness);
    if(row.kurtosis)values["Kurt"]=scalar(*row.kurtosis);
    if(command.options().redirect) {
      const auto& redirect=*command.options().redirect;
      if(!row.sampling_position)throw std::invalid_argument("Redirect measurement requires captured sampling position");
      const auto p=*row.sampling_position;
      values["Redirect image"]=redirect.source().data().metadata.title;
      values["Redirect document"]=redirect.document_id();
      values["Redirect T"]=p.t+1;values["Redirect C"]=p.c+1;values["Redirect Z"]=p.z+1;
      values["Coordinate unit"]=(row.point_world || row.midpoint_world || (row.angle_degrees && !row.ellipse_angle_degrees))?command.target().calibration().unit:result.spatial.unit;
    }
    if(row.roi_index) {
      const auto& roi=source.data().rois[*row.roi_index];values["ROI"]=roi.name;
      values["ROI index"]=*row.roi_index;values["ROI kind"]=roi.selection.kind;
      values["Bounds"]=row.bounds?Json(*row.bounds):Json();
    }else if(command.target().roi_policy()==RoiPolicy::ActiveSelection && source.data().selection) {
      values["ROI"]=source.data().selection->kind;
      values["Bounds"]=row.bounds?Json(*row.bounds):Json();
    }
    if(row.perimeter) {
      values["Perim."]=scalar(*row.perimeter);
      values["Perim. unit"]=row.perimeter_in_pixels?std::string("pixel"):row.length?result.spatial.unit:command.target().calibration().unit;
    }
    if(row.bounding_x) {
      values["BX"]=scalar(*row.bounding_x);values["BY"]=scalar(*row.bounding_y);
      values["Width"]=scalar(*row.bounding_width);values["Height"]=scalar(*row.bounding_height);
    }
    if(row.feret) {
      values["Feret"]=scalar(*row.feret);values["MinFeret"]=scalar(*row.min_feret);
      values["FeretAngle"]=scalar(*row.feret_angle);values["FeretX"]=scalar(*row.feret_x);values["FeretY"]=scalar(*row.feret_y);
      values["Feret unit"]=row.feret_in_pixels?std::string("pixel"):command.target().calibration().unit;
      values["Feret coordinate unit"]=row.feret_coordinates_scaled?command.target().calibration().unit:std::string("pixel");
    }
    if(row.circularity) {
      values["Circ."]=scalar(*row.circularity);values["AR"]=scalar(*row.aspect_ratio);
      values["Round"]=scalar(*row.roundness);values["Solidity"]=scalar(*row.solidity);
    }
    if(row.center_of_mass_world) {
      values["XM"]=scalar((*row.center_of_mass_world)[0]);values["YM"]=scalar((*row.center_of_mass_world)[1]);
    }
    if(row.length)values["Length"]=scalar(*row.length);
    if(row.angle_degrees)values["Angle"]=scalar(*row.angle_degrees);
    if(row.ellipse_major) {
      values["Major"]=scalar(*row.ellipse_major);values["Minor"]=scalar(*row.ellipse_minor);
      values["Angle"]=scalar(*row.ellipse_angle_degrees);
    }
    if(row.threshold_lower) {
      values["MinThr"]=scalar(*row.threshold_lower);values["MaxThr"]=scalar(*row.threshold_upper);
    }
    if(row.point_index) {
      if(!row.point_world)throw std::invalid_argument("Point measurement requires captured coordinates");
      values["Point index"]=*row.point_index;
      if(row.point_counter_count){values["Counter"]=*row.point_counter;values["Count"]=*row.point_counter_count;}
      values["X"]=(*row.point_world)[0];values["Y"]=(*row.point_world)[1];
      values["centroid_x"]=(*row.point_world)[0];values["centroid_y"]=(*row.point_world)[1];
      values["centroid_z"]=(*row.point_world)[2];
    }else if(row.centroid_world) {
      values["X"]=scalar((*row.centroid_world)[0]);values["Y"]=scalar((*row.centroid_world)[1]);
      values["centroid_x"]=scalar((*row.centroid_world)[0]);values["centroid_y"]=scalar((*row.centroid_world)[1]);
      values["centroid_z"]=scalar((*row.centroid_world)[2]);
    }else if(row.midpoint_world) {
      values["X"]=(*row.midpoint_world)[0];values["Y"]=(*row.midpoint_world)[1];
      values["centroid_x"]=(*row.midpoint_world)[0];values["centroid_y"]=(*row.midpoint_world)[1];
      values["centroid_z"]=(*row.midpoint_world)[2];
    }else if(row.length || row.angle_degrees) {
      // Analyzer's angle/irregular-line rows leave these summaries at zero.
      values["X"]=0;values["Y"]=0;values["centroid_x"]=0;values["centroid_y"]=0;
    }
    rows.push_back(std::move(values));
  }
  return rows;
}
}
