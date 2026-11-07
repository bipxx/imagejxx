#include <imagejxx/measurement_overlay.hpp>
#include <imagejxx/point_annotations.hpp>
#include <iostream>
#include <stdexcept>
#include <limits>
using namespace imagejxx;
namespace {
void check_at(bool value,int line){if(!value)throw std::runtime_error("Measurement overlay assertion line "+std::to_string(line));}
#define check(...) check_at((__VA_ARGS__),__LINE__)
template<class E,class F> void refuses(F f){bool caught=false;try{f();}catch(const E&){caught=true;}check(caught);}
const MetadataObject& object(const MetadataValue& v){return std::get<MetadataObject>(std::get<std::shared_ptr<const MetadataTree>>(v)->value);}
const MetadataArray& array(const MetadataValue& v){return std::get<MetadataArray>(std::get<std::shared_ptr<const MetadataTree>>(v)->value);}
std::uint64_t number(const MetadataValue& v){if(const auto p=std::get_if<std::uint64_t>(&v))return *p;return std::uint64_t(std::get<std::int64_t>(v));}
struct Source final:PlaneSource {
  DatasetShape extent;mutable std::size_t reads=0;
  explicit Source(DatasetShape shape):extent(shape){}
  DatasetShape shape()const override{return extent;}
  PixelType pixel_type()const override{return PixelType::UInt8;}
  std::shared_ptr<const Image> read_plane(PlanePosition)const override{++reads;return std::make_shared<const Image>(extent.x,extent.y,1,PixelType::UInt8);}
};
ResolvedTargetAnalysis command(const DocumentSnapshot& source,bool manager=false) {
  TargetAnalysisOptions options;options.measurement=manager?TargetMeasurementMode::RoiManager:TargetMeasurementMode::Selection;
  const auto request=manager?roi_measurement_target_request(source):TargetRequest{};
  return resolve_target_analysis(resolve_target(source,request),options);
}
MetadataValue presentation(bool labels) {
  return metadata_object({{"version",1},{"draw_labels",labels},{"draw_names",false},{"draw_backgrounds",false},
      {"label_color",metadata_array({255,0,0,255})}});
}
}
int main() try {
  const DatasetShape shapes[]={{1,1,1,6,8,1},{1,1,3,6,8,1},{2,2,2,6,8,1},{1,2,1,6,8,1},
      {2,1,1,6,8,1},{1,2,2,6,8,1},{2,1,2,6,8,1},{2,2,1,6,8,1},{1,2,1,6,8,1},{2,2,2,6,8,1},{1,1,3,6,8,1}};
  // Expected original unpositioned-ROI audit slots for11 scalar/composite layouts.
  const unsigned positions[]={0,2,0,2,2,0,0,0,0,0,2};
  const bool hypers[]={false,false,true,false,false,true,true,true,true,true,false};
  std::size_t cases=0;
  for(std::size_t layout=0;layout<11;++layout)for(bool named:{false,true})for(bool labels:{false,true}) {
    const auto shape=shapes[layout];auto provider=std::make_shared<Source>(shape);Document owner{Dataset(provider)};
    Selection selection("multipoint",{{1.25,1.5},{5.5,3.25}});if(named)selection.attributes["imagej_roi_name"]=std::string("named,\"alpha");
    selection.attributes["opaque"]=metadata_object({{"nested",std::string("retained")}});owner.select(selection);
    auto edit=owner.begin_edit();edit.set_overlays({{{"old",17}}});edit.set_property(measurement_overlay_property,presentation(labels));owner.commit(edit);
    auto view=owner.snapshot().display();view.t=shape.t-1;view.c=shape.c-1;view.crosshair[2]=shape.z>1?1:0;owner.set_display(view);
    const auto before=owner.snapshot();const auto history=owner.undo_steps();const auto analysis=command(before);
    MeasurementOverlayOptions options;options.enabled=true;options.composite=layout==8 || layout==9;options.draw_labels=false;
    const auto plan=resolve_measurement_overlay(analysis,3,options);check(plan.will_add() && plan.result_count()==5 && plan.working_bytes()>0);
    options.enabled=false;check(plan.options().enabled);refuses<std::invalid_argument>([&]{prepare_measurement_overlay(plan,plan.working_bytes()-1);});
    auto staged=prepare_measurement_overlay(plan);check(staged && owner.snapshot().shares_data(before) && provider->reads==0);
    const auto preview=staged->preview();check(preview.data().overlays.size()==2 && metadata_equal(preview.data().overlays[0],before.data().overlays[0]));
    const auto& row=preview.data().overlays[1];check(std::get<std::string>(row.at("name"))==(named?"named,\"alpha":"5"));
    const auto& audit=object(row.at(measurement_overlay_row));check(number(audit.at("position"))==positions[layout] && std::get<bool>(audit.at("hyperstack"))==hypers[layout]);
    const auto& clone=object(row.at("selection"));check(std::get<std::string>(clone.at("imagej_roi_name"))==std::get<std::string>(row.at("name")));
    check(preview.data().selection==before.data().selection && preview.data().previous_selection==before.data().previous_selection);
    const auto display=measurement_overlay_presentation(preview.data().metadata.properties);check(display.present && display.draw_labels==labels && display.draw_names && display.draw_backgrounds && display.label_color==RoiColor{255,255,255,255});
    owner.commit(*staged);check(owner.undo_steps()==history+1 && provider->reads==0);
    refuses<RevisionConflict>([&]{plan.validate_source(owner.snapshot());});auto stale=prepare_measurement_overlay(plan);refuses<RevisionConflict>([&]{owner.commit(*stale);});
    owner.undo();check(owner.snapshot().shares_data(before));owner.redo();check(owner.snapshot().data().overlays.size()==2 && provider->reads==0);++cases;
  }
  {
    auto provider=std::make_shared<Source>(DatasetShape{1,1,1,6,8,1});Document owner{Dataset(provider)};
    MeasurementOverlayOptions enabled;enabled.enabled=true;
    const auto whole=command(owner.snapshot());check(!resolve_measurement_overlay(whole,0,enabled).will_add());
    owner.select(Selection("rectangle",{{1,1},{7,5}}));const auto source=owner.snapshot();auto analysis=command(source);
    check(!prepare_measurement_overlay(resolve_measurement_overlay(analysis,0)));
    MeasurementOverlayLimits limits;limits.max_name_bytes=1;refuses<std::invalid_argument>([&]{resolve_measurement_overlay(analysis,99,enabled,limits);});
    limits=MeasurementOverlayLimits{};limits.max_results_rows=3;refuses<std::invalid_argument>([&]{resolve_measurement_overlay(analysis,3,enabled,limits);});
    const auto plan=resolve_measurement_overlay(analysis,0,enabled);limits=MeasurementOverlayLimits{};limits.max_working_bytes=plan.working_bytes()-1;
    refuses<std::invalid_argument>([&]{resolve_measurement_overlay(analysis,0,enabled,limits);});check(owner.snapshot().shares_data(source) && provider->reads==0);
    auto edit=owner.begin_edit();edit.set_rois({Roi{"row",*source.data().selection,{},{},{},{}}});owner.commit(edit);
    const auto manager=command(owner.snapshot(),true);check(!resolve_measurement_overlay(manager,std::numeric_limits<std::size_t>::max(),enabled).will_add());
    auto malformed=*source.data().selection;malformed.attributes["imagej_roi_name"]=true;owner.select(malformed);analysis=command(owner.snapshot());
    refuses<std::invalid_argument>([&]{resolve_measurement_overlay(analysis,0,enabled);});
    malformed.attributes["imagej_roi_name"]=std::string();owner.select(malformed);analysis=command(owner.snapshot());limits=MeasurementOverlayLimits{};limits.max_name_bytes=0;
    check(resolve_measurement_overlay(analysis,0,enabled,limits).will_add());
    edit=owner.begin_edit();edit.set_property(measurement_overlay_property,metadata_object({{"version",2}}));owner.commit(edit);analysis=command(owner.snapshot());
    refuses<std::invalid_argument>([&]{resolve_measurement_overlay(analysis,0,enabled);});check(provider->reads==0);
  }
  {
    for(bool hyper:{false,true}) {
      auto provider=std::make_shared<Source>(hyper?DatasetShape{2,2,2,6,8,1}:DatasetShape{1,1,3,6,8,1});
      Document owner{Dataset(provider)};Selection selected("multipoint",{{1,1},{3,3}});
      const std::vector<PointAnnotation> entries={{7,PlanePosition{0,0,0}},{9,PlanePosition{0,0,2}}};
      auto safe=entries;if(hyper)safe[1].position=PlanePosition{0,1,1};
      owner.select(with_point_annotations(selected,safe));auto view=owner.snapshot().display();view.crosshair[2]=1;owner.set_display(view);
      const auto before=owner.snapshot();MeasurementOverlayOptions options;options.enabled=true;
      const auto plan=resolve_measurement_overlay(command(before),0,options);auto transaction=prepare_measurement_overlay(plan);
      const auto preview=transaction->preview();const auto& encoded=object(preview.data().overlays.back().at("selection"));
      const auto& points=array(object(encoded.at(point_annotations_attribute)).at("points"));
      check(points.size()==2 && number(object(points[0]).at("counter"))==7 && number(object(points[1]).at("counter"))==9);
      for(std::size_t i=0;i<2;++i) {
        const auto& actual=array(object(points[i]).at("position"));
        const auto expected=hyper?*safe[i].position:PlanePosition{0,0,1};
        check(actual.size()==3 && number(actual[0])==std::uint64_t(expected.t) && number(actual[1])==std::uint64_t(expected.c) && number(actual[2])==std::uint64_t(expected.z));
      }
      check(owner.snapshot().shares_data(before) && provider->reads==0);
    }
  }
  std::cout<<"PASS measurement overlays:44 original layout/name/presentation controls,clone/stale/history/no-read/noop/limits/malformed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
