#pragma once
#include "rounded_saved_resource_cases.hpp"
#include <imagejxx/session.hpp>
namespace ieee_saved_geometry {
using namespace imagejxx;
using rounded_saved_resources::Source;
using rounded_saved_resources::check;
using rounded_saved_resources::rejects;
inline Selection area(int kind) {
  const char *names[]{"rectangle","ellipse","rotated_rectangle","rotated_ellipse","rounded_rectangle","rotated_rounded_rectangle","polygon","freehand","mask"};
  Selection value(names[kind],{{2,2},{9,7}});
  if(kind==2 || kind==3 || kind==5)value.points={{6,5},{9,6},{5,8}};
  if(kind==6 || kind==7)value.points={{2,2},{9,2},{8,7},{3,8}};
  if(kind==8){std::vector<unsigned char> packed(15);packed[4]=0x3c;packed[5]=0x0f;value=Selection::from_packed_mask(12,10,std::move(packed));}
  value.attributes={{"opaque",std::string("kept")}};
  if(kind==4 || kind==5)value.attributes["corner_diameter"]=std::int64_t(6);
  return value;
}
struct Fixture {
  std::shared_ptr<Source> source;DatasetReadObserver observed;std::shared_ptr<Document> owner;Application app;
  Fixture(int kind,FloatingSamplePolicy policy) {
    source=std::make_shared<Source>(12,10,PixelType::Float32,1);
    auto dataset=[&] {
      if(policy==FloatingSamplePolicy::FiniteOnly)return Dataset(source,SpatialCalibration{},policy);
      Stack planes;
      for(int i=0;i<12;++i) {
        Image image(12,10,1,PixelType::Float32);
        const double values[]{0.,-0.,std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),
                              std::numeric_limits<double>::quiet_NaN(),-std::numeric_limits<double>::quiet_NaN()};
        for(std::size_t j=0;j<image.pixels.size();++j)
          image.pixels[j]=policy==FloatingSamplePolicy::PreserveQuietNaNFloat32 && (j%6==2 || j%6==3)
              ? (j%6==2?1.:-1.) : values[j%6];
        planes.push_back(std::move(image));
      }
      return Dataset::from_planes({2,2,3,10,12,1},std::move(planes),SpatialCalibration{},policy).observe_reads(observed);
    }();
    DocumentData data{std::move(dataset)};
    data.selection=Selection("point",{{0,0}});data.previous_selection=Selection("line",{{0,0},{1,1}});
    data.rois={{"area",area(kind),{},{},{},{{"row",std::string("kept")}}}};
    owner=std::make_shared<Document>(std::move(data));app.adopt(owner);
  }
  void unread()const{const auto counts=observed.counts();check(source->planes==0 && source->regions==0 && counts.plane_reads==0 && counts.region_reads==0,"IEEE saved area acquired source data");}
};
inline MetadataObject source_fields(const MetadataObject &r) {
  return std::get<MetadataObject>(std::get<std::shared_ptr<const MetadataTree>>(r.at("source"))->value);
}
inline void changed_source(MetadataObject &r,MetadataObject fields) {r["source"]=metadata_object(std::move(fields));}
inline std::size_t run() {
  const auto first=rounded_saved_resources::checks;std::size_t cases=0;
  for(int kind=0;kind<9;++kind)for(auto policy:{FloatingSamplePolicy::FiniteOnly,FloatingSamplePolicy::PreserveQuietNaNFloat32,FloatingSamplePolicy::PreserveIEEEFloat32}) {
    Fixture f(kind,policy);const auto before=f.owner->snapshot();
    auto next=SelectionEditor(before.data().rois[0].selection,12,10).moved({1,-1},false);
    SavedRoiGeometryRequest request(0,next);
    const auto command=resolve_saved_roi_geometry(before,request);
    const auto record=command.record();const auto sf=source_fields(record);
    const auto expected=policy==FloatingSamplePolicy::FiniteOnly?"finite_only":policy==FloatingSamplePolicy::PreserveQuietNaNFloat32?"preserve_quiet_nan_float32":"preserve_ieee_float32";
    check(std::get<std::string>(sf.at("sample_policy"))==expected,"Saved area serialized wrong source policy");
    check(std::get<std::int64_t>(record.at("version"))==((kind==4||kind==5)?2:1),"IEEE policy changed geometry record version");
    validate_saved_roi_geometry_record(record);
    check(saved_roi_geometry_from_record(before,record).replacement()==next,"IEEE saved replay lost replacement");
    const auto estimate=estimate_saved_roi_geometry(before,request);
    SavedRoiGeometryLimits low;low.max_working_bytes=estimate.working_bytes-1;
    std::size_t calls=0;ExecutionControl control;control.checkpoint=[&]{++calls;};
    rejects([&]{resolve_saved_roi_geometry(before,request,low,control);},"IEEE saved cap ignored");
    check(calls==0,"IEEE saved cap dispatched callback");
    auto prepared=f.app.prepare_saved_roi_geometry(f.app.capture_saved_roi_geometry(),command);
    const auto result=f.app.publish_saved_roi_geometry(prepared);
    check(result.changed && !prepared.active() && prepared.retained_working_bytes()==0,"IEEE saved publish/resource retirement failed");
    check(f.owner->snapshot().data().dataset.sample_policy()==policy && f.owner->snapshot().data().dataset.shares_pixels(before.data().dataset),"Saved geometry changed source sample policy/storage");
    f.owner->undo();check(f.owner->snapshot().data().rois[0].selection==before.data().rois[0].selection,"IEEE saved Undo failed");
    f.owner->redo();check(f.owner->snapshot().data().rois[0].selection==next,"IEEE saved Redo failed");f.unread();
    if(policy==FloatingSamplePolicy::PreserveIEEEFloat32) {
      for(int damage=0;damage<3;++damage) {
        auto bad=record;auto source=source_fields(bad);
        if(damage==0)source["type"]=std::string("uint16");
        if(damage==1){auto shape=before.data().dataset.shape().dimensions();MetadataArray dims;for(auto v:shape)dims.emplace_back(v);dims[5]=3;source["shape"]=metadata_array(std::move(dims));}
        if(damage==2)source["sample_policy"]=std::string("unknown_ieee_policy");
        changed_source(bad,std::move(source));calls=0;
        rejects<std::invalid_argument>([&]{rebind_saved_roi_geometry(before,bad,request,{},control);},"Invalid IEEE record source admitted");
        check(calls==0,"Invalid IEEE original reached callbacks");
      }
      Fixture legacy(kind,FloatingSamplePolicy::FiniteOnly);
      const auto legacy_record=resolve_saved_roi_geometry(legacy.owner->snapshot(),SavedRoiGeometryRequest(0,next)).record();
      Fixture destination(kind,FloatingSamplePolicy::PreserveIEEEFloat32);
      const auto rebound=rebind_saved_roi_geometry(destination.owner->snapshot(),legacy_record,SavedRoiGeometryRequest(0,next));
      check(std::get<std::string>(source_fields(rebound.record()).at("sample_policy"))=="preserve_ieee_float32","Finite original did not explicitly rebind to IEEE destination");
      legacy.unread();destination.unread();
      auto mismatched=record;auto fields=source_fields(mismatched);fields["sample_policy"]=std::string("finite_only");changed_source(mismatched,std::move(fields));
      rejects<RevisionConflict>([&]{saved_roi_geometry_from_record(before,mismatched);},"Strict replay silently changed IEEE source policy");f.unread();
    }
    ++cases;
  }
  std::cout<<"ieee_saved_geometry: "<<cases<<" cases / "<<rounded_saved_resources::checks-first<<" checks; provider reads zero\n";return cases;
}
}
