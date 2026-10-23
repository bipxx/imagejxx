#pragma once
#include <imagejxx/contrast_apply_command.hpp>
#include "axis_command_record.hpp"
#include "selection_codec.hpp"
#include "session_metadata.hpp"
#include "regional_geometry.hpp"
#include <cmath>
namespace imagejxx::detail::contrast_command_record {
namespace wire=axis_command_record;
namespace scalar=session_metadata;
namespace geometry=selection_codec;
[[noreturn]]inline void invalid(const char *s){throw std::invalid_argument(s);}
inline std::size_t add(std::size_t a,std::size_t b){if(b>SIZE_MAX-a)invalid("Apply command size overflow");return a+b;}
inline std::size_t mul(std::size_t a,std::size_t b){if(b && a>SIZE_MAX/b)invalid("Apply command size overflow");return a*b;}
struct Budget {
  std::size_t remaining,used=0;
  void take(std::size_t n){if(n>remaining)invalid("Apply command metadata budget");remaining-=n;used=add(used,n);}
  void text(const std::string &s){take(add(64,mul(s.size(),6)));}
  void object(const MetadataObject &o,unsigned depth=0){
    if(depth>128)invalid("Apply command metadata depth");take(64);
    for(const auto &[k,v]:o){text(k);value(v,depth+1);}
  }
  void value(const MetadataValue &v,unsigned depth){
    if(depth>128)invalid("Apply command metadata depth");take(64);
    if(const auto p=std::get_if<double>(&v)){if(!std::isfinite(*p))invalid("Apply metadata must be finite");}
    else if(const auto p=std::get_if<std::string>(&v))text(*p);
    else if(const auto p=std::get_if<std::vector<std::string>>(&v)){for(const auto &s:*p)text(s);}
    else if(const auto p=std::get_if<std::shared_ptr<const MetadataTree>>(&v)){
      if(!*p)invalid("Apply command null metadata node");
      if(const auto o=std::get_if<MetadataObject>(&(*p)->value))object(*o,depth);
      else for(const auto &item:std::get<MetadataArray>((*p)->value))value(item,depth+1);
    }
  }
  void selection(const Selection &s){text(s.kind);take(mul(s.points.size(),256));take(mul(s.packed_mask().size(),2));take(mul(s.path_commands().size(),64));object(s.attributes);}
  void roi(const Roi &r){take(512);text(r.name);selection(r.selection);object(r.attributes);}
};
inline std::size_t source_budget(const DocumentSnapshot &s,std::size_t limit){
  Budget b{limit};b.take(16384);const auto &d=s.data();
  b.text(d.metadata.title);b.object(d.metadata.properties);b.object(d.metadata.extensions);b.object(d.metadata.record_layout);
  b.object(s.display().properties);b.text(d.dataset.calibration().unit);b.text(d.dataset.calibration().time_unit);
  if(d.selection)b.selection(*d.selection);if(d.previous_selection)b.selection(*d.previous_selection);
  for(const auto &r:d.rois)b.roi(r);for(const auto &o:d.overlays)b.object(o);
  return b.used;
}
inline bool regional_target(const ResolvedTarget &t){
  const auto &data=t.source().data();bool regional=false;
  if(t.roi_policy()==RoiPolicy::ActiveSelection)regional=data.selection && data.selection->is_region();
  if(t.roi_policy()==RoiPolicy::PositionedRois)for(const auto &r:data.rois)regional=regional || r.selection.is_region();
  if(!regional)return false;
  for(const auto &[key,value]:t.source().display().properties)regional_wire_depth(value,2);
  if(t.roi_policy()==RoiPolicy::ActiveSelection && data.selection) {
    validate_region_storage(*data.selection);
    for(const auto &[key,value]:data.selection->attributes)regional_wire_depth(value,3);
  }
  if(t.roi_policy()==RoiPolicy::PositionedRois)for(const auto &r:data.rois) {
    validate_region_storage(r.selection);
    for(const auto &[key,value]:r.selection.attributes)regional_wire_depth(value,5);
    for(const auto &[key,value]:r.attributes)regional_wire_depth(value,4);
  }
  return true;
}
inline void regional_capture_depth(const DocumentSnapshot &source,unsigned extra){
  for(const auto &[key,value]:source.display().properties)regional_wire_depth(value,2+extra);
  if(source.data().selection) {
    validate_region_storage(*source.data().selection);
    for(const auto &[key,value]:source.data().selection->attributes)regional_wire_depth(value,3+extra);
  }
}
inline MetadataValue selected(const ResolvedTarget &t,bool regional=false){
  const auto &s=t.source().data().selection;
  return t.roi_policy()==RoiPolicy::ActiveSelection && s?scalar::from_json(geometry::encode_selection(*s,regional)):MetadataValue{};
}
inline MetadataValue rois(const ResolvedTarget &t,bool regional=false){
  MetadataArray result;
  if(t.roi_policy()==RoiPolicy::PositionedRois)for(const auto &r:t.source().data().rois)result.push_back(scalar::from_json(geometry::encode_roi(r,regional)));
  return metadata_array(std::move(result));
}
} // namespace imagejxx::detail::contrast_command_record
