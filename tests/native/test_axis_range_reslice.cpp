#include <imagejxx/application.hpp>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace imagejxx;
namespace {
void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
template <class Error, class Function>
void rejects(Function function, const char *message) {
  bool rejected = false;
  try { function(); } catch (const Error &) { rejected = true; }
  check(rejected, message);
}
std::array<int, 3> axes(Plane plane) {
  if (plane == Plane::XY) return {0, 1, 2};
  if (plane == Plane::XZ) return {0, 2, 1};
  return {1, 2, 0};
}
std::array<int, 3> xyz(Plane plane, int coordinate, int u, int v) {
  if (plane == Plane::XY) return {u, v, coordinate};
  if (plane == Plane::XZ) return {u, coordinate, v};
  return {coordinate, u, v};
}
SpatialCalibration calibration() {
  SpatialCalibration result;
  result.spacing = {.4, 1.2, 3}; result.origin = {11, -7, 19};
  result.direction = {1, .25, -.5, .5, -1, .125, 0, .75, 1.5};
  result.frame_interval = .75; result.time_origin = 2.25;
  result.unit = "um"; result.time_unit = "s";
  return result;
}
double finite_value(PixelType type, int t, int c, int z, int y, int x, int s) {
  const int code = (97*t + 31*c + 11*z + 5*y + 3*x + s) % 251;
  if (type == PixelType::UInt16) return 40000 + code;
  if (type == PixelType::Float32) return code - 125.25;
  if (type == PixelType::Float64) return code - 125.125;
  return code;
}
Dataset finite_source(PixelType type = PixelType::Float64, int samples = 1) {
  Stack planes;
  for (int t = 0; t < 2; ++t)
    for (int c = 0; c < 3; ++c)
      for (int z = 0; z < 9; ++z) {
        Image image(32, 17, samples, type);
        for (int y = 0; y < 17; ++y)
          for (int x = 0; x < 32; ++x)
            for (int s = 0; s < samples; ++s)
              image.at(x, y, s) = finite_value(type, t, c, z, y, x, s);
        planes.push_back(std::move(image));
      }
  return Dataset::from_planes({2, 3, 9, 17, 32, samples}, std::move(planes), calibration());
}
void geometry(const Dataset &source, const AxisRangeResliceResult &result) {
  const auto mapping = axes(result.request.plane);
  const auto &original = source.calibration(); const auto &actual = result.dataset.calibration();
  check(actual.unit == "um" && actual.time_unit == "s" && actual.frame_interval == .75 &&
        actual.time_origin == 3, "Chosen T and calibration units differ");
  for (int column = 0; column < 3; ++column) {
    check(actual.spacing[column] == original.spacing[mapping[column]] *
              (column == 2 ? result.request.step : 1), "Range stride spacing differs");
    for (int row = 0; row < 3; ++row)
      check(actual.direction[row*3+column] == original.direction[row*3+mapping[column]],
            "Full sheared/reflected direction permutation differs");
  }
  const auto shape = result.dataset.shape();
  for (int k = 0; k < shape.z; ++k)
    for (int v : {0, shape.y-1})
      for (int u : {0, shape.x-1}) {
        const auto point = xyz(result.request.plane, result.request.first+k*result.request.step, u, v);
        const auto expected = original.voxel_to_world({double(point[0]), double(point[1]), double(point[2])});
        const auto observed = actual.voxel_to_world({double(u), double(v), double(k)});
        for (int row = 0; row < 3; ++row)
          check(std::abs(expected[row]-observed[row]) <= 1e-12 * std::max(1.0, std::abs(expected[row])),
                "Range output voxel world position differs from source");
      }
  check(result.first_plane_geometry.origin == actual.origin, "First-plane geometry origin differs");
}
void finite_ranges_and_singletons() {
  for (auto type : {PixelType::UInt8, PixelType::UInt16, PixelType::Float32, PixelType::Float64})
    for (int samples : {1, 3, 4}) {
      const auto source = finite_source(type, samples);
      for (auto plane : {Plane::XY, Plane::XZ, Plane::YZ}) {
        const int n = plane == Plane::XY ? 9 : plane == Plane::XZ ? 17 : 32;
        const std::array<std::array<int, 3>, 5> ranges{{{0,0,1},{0,n-1,1},{1,n-2,3},
                                                      {n-1,n-1,3},{1,n-1,5}}};
        for (auto range : ranges) {
          const auto output = reslice_axis_range(source, {plane,range[0],range[1],range[2],1,2});
          const auto shape = output.dataset.shape();
          const int count = 1+(range[1]-range[0])/range[2];
          check(shape.dimensions() == std::array<int,6>{1,1,count,plane==Plane::XY?17:9,
                plane==Plane::YZ?17:32,samples}, "Range shape/count/axis mapping differs");
          check(output.dataset.pixel_type() == type && output.dataset.sample_policy() == source.sample_policy() &&
                !output.dataset.shares_pixels(source), "Range type/policy/independent storage differs");
          for (int k = 0; k < count; ++k) {
            const auto image = output.dataset.plane({0,0,k});
            for (int v = 0; v < shape.y; ++v)
              for (int u = 0; u < shape.x; ++u) {
                const auto point = xyz(plane,range[0]+k*range[2],u,v);
                for (int s = 0; s < samples; ++s)
                  check(image->at(u,v,s) == finite_value(type,1,2,point[2],point[1],point[0],s),
                        "Selected positive range order/pixel/component/T/C differs");
              }
          }
          geometry(source, output);
          const auto pixels = std::size_t(count)*shape.x*shape.y*samples*sizeof(double);
          const auto strip = plane==Plane::XY?0:std::size_t(shape.x)*samples*sizeof(double);
          check(output.working_bytes == pixels+strip+std::size_t(32*17*samples)*sizeof(double),
                "Complete stack estimate omits planes/strip/fallback storage");
        }
        const auto one = reslice_axis_range(source,{plane,n-1,n-1,1,1,2});
        const auto old = export_axis_plane(source,{plane,n-1,1,2});
        check(one.dataset.plane()->pixels == old.dataset.plane()->pixels &&
              one.dataset.calibration().spacing == old.dataset.calibration().spacing &&
              one.dataset.calibration().direction == old.dataset.calibration().direction &&
              one.dataset.calibration().origin == old.dataset.calibration().origin,
              "Step-one singleton changed P05f extraction/geometry");
        check(reslice_axis_range(source,{plane,1,n-1,std::numeric_limits<int>::max(),1,2}).dataset.shape().z == 1,
              "Large positive step did not emit exactly the first plane");
      }
    }
  auto large = calibration(); large.origin = {1e100,-1e100,1e100};
  const auto output = reslice_axis_range(finite_source().with_calibration(large), {Plane::XZ,1,15,3,1,2});
  check(output.first_plane_geometry.horizontal_step == std::array<double,3>{.4,.2,0} &&
        output.first_plane_geometry.vertical_step == std::array<double,3>{-1.5,.375,4.5},
        "Large origins canceled the direct first-plane basis");
  std::cout << "G029.range.native.R01_finite_ranges_geometry_singletons\n";
}
double extension_value(int kind, int t, int c, int z, int y, int x, int s) {
  if (kind == 0) return 30000*t+8000*c+600*z+30*y+3*x+s;
  if (kind == 1) {
    if (t==1 && c==2 && z==4 && y==7 && x==13) return -0.0;
    return 1e6*t+1e5*c+1e4*z+100*y+x+.125;
  }
  constexpr auto policy = FloatingSamplePolicy::PreserveQuietNaNFloat32;
  if (t==1 && c==1 && z==4 && y==7 && x==13) return float32_source_sample(0x7fc01234u,policy);
  if (t==1 && c==1 && z==1 && y==1 && x==1) return float32_source_sample(0x80000000u,policy);
  return 1e6*t+1e5*c+1e4*z+100*y+x+.25;
}
void owned_extensions_bits() {
  for (int kind=0;kind<3;++kind) {
    const auto type=kind==0?PixelType::UInt16:kind==1?PixelType::Float64:PixelType::Float32;
    const int samples=kind==0?4:1, selected_c=kind==1?2:1;
    const auto policy=kind==2?FloatingSamplePolicy::PreserveQuietNaNFloat32:FloatingSamplePolicy::FiniteOnly;
    auto cal=calibration(); if(kind==1) cal.direction={-2,0,0,0,1.5,0,0,0,.75};
    Stack planes;
    for(int t=0;t<2;++t) for(int c=0;c<3;++c) for(int z=0;z<9;++z) {
      Image image(32,17,samples,type);
      for(int y=0;y<17;++y) for(int x=0;x<32;++x) for(int s=0;s<samples;++s)
        image.at(x,y,s)=extension_value(kind,t,c,z,y,x,s);
      planes.push_back(std::move(image));
    }
    const auto source=Dataset::from_planes({2,3,9,17,32,samples},std::move(planes),cal,policy);
    for(auto plane:{Plane::XY,Plane::XZ,Plane::YZ}) {
      const int n=plane==Plane::XY?9:plane==Plane::XZ?17:32;
      const auto result=reslice_axis_range(source,{plane,1,n-2,3,1,selected_c});
      check(result.dataset.sample_policy()==policy,"Range lost admitted Float32 policy");
      geometry(source,result);
      const auto shape=result.dataset.shape();
      bool seen_special=false;
      for(int k=0;k<shape.z;++k) {
        const auto image=result.dataset.plane({0,0,k});
        for(int v=0;v<shape.y;++v) for(int u=0;u<shape.x;++u) for(int s=0;s<samples;++s) {
          const auto point=xyz(plane,1+3*k,u,v);
          const auto expected=extension_value(kind,1,selected_c,point[2],point[1],point[0],s);
          const auto actual=image->at(u,v,s);
          if(kind==2) check(float32_source_sample_bits(actual,policy)==float32_source_sample_bits(expected,policy),
                            "Range changed quiet-NaN payload/Float32 signed-zero bits");
          else check(std::memcmp(&expected,&actual,sizeof(double))==0,"Range changed exact RGBA/Float64 bits");
          if(point==std::array<int,3>{13,7,4}) seen_special=true;
        }
      }
      check(seen_special,"Owned extension range omitted the independent special-sample location");
    }
  }
  std::cout << "G029.range.native.R02_owned_C_T_RGBA_float_bits\n";
}
class CountingSource final : public PlaneSource {
public:
  DatasetShape sizes{2,3,9,17,32,1};
  mutable std::vector<PlanePosition> planes;
  mutable std::vector<std::array<int,7>> regions;
  bool regional_only=false;
  DatasetShape shape() const override { return sizes; }
  PixelType pixel_type() const override { return PixelType::Float64; }
  std::shared_ptr<const Image> read_plane(PlanePosition position) const override {
    planes.push_back(position);
    if(regional_only) throw std::runtime_error("Unexpected full plane read");
    auto result=std::make_shared<Image>(sizes.x,sizes.y,sizes.s,pixel_type());
    fill(*result,position,0,0); return result;
  }
  Image read_region(PlanePosition position,int x,int y,int width,int height) const override {
    regions.push_back({position.t,position.c,position.z,x,y,width,height});
    Image result(width,height,sizes.s,pixel_type()); fill(result,position,x,y); return result;
  }
  void fill(Image &result,PlanePosition p,int x,int y) const {
    for(int v=0;v<result.height;++v) for(int u=0;u<result.width;++u) for(int s=0;s<sizes.s;++s)
      result.at(u,v,s)=finite_value(pixel_type(),p.t,p.c,p.z,y+v,x+u,s);
  }
};
void validation_budget_and_reads() {
  auto counter=std::make_shared<CountingSource>(); const Dataset source(counter,calibration());
  for(const auto request:{AxisRangeResliceRequest{static_cast<Plane>(99),0,0,1},
      AxisRangeResliceRequest{Plane::XY,-1,0,1}, {Plane::XY,2,1,1}, {Plane::XY,0,9,1},
      {Plane::XY,0,1,0}, {Plane::XY,0,1,-1}, {Plane::XY,0,1,1,-1,0},
      {Plane::XY,0,1,1,2,0}, {Plane::XY,0,1,1,0,-1}, {Plane::XY,0,1,1,0,3},
      {Plane::XY,0,1,1,0,0,0}, {Plane::XY,0,1,1,0,0,1}})
    rejects<std::exception>([&]{reslice_axis_range(source,request);},"Invalid range/index/cap was admitted");
  const std::size_t cap=std::size_t(3*32*9+32+32*17)*sizeof(double);
  rejects<std::invalid_argument>([&]{reslice_axis_range(source,{Plane::XZ,1,7,3,1,2,cap-1});},
                                 "Complete-output cap rejection admitted an extra plane");
  auto cal=calibration(); cal.spacing[1]=1e308;
  rejects<std::invalid_argument>([&]{reslice_axis_range(source.with_calibration(cal),{Plane::XZ,0,0,3});},
                                 "Singleton stride geometry overflow was admitted");
  cal=calibration();cal.origin[0]=1e308;cal.spacing[0]=1e308;
  rejects<std::invalid_argument>([&]{reslice_axis_range(source.with_calibration(cal),{Plane::XY,0,0,1});},
                                 "Emitted volume corner geometry overflow was admitted");
  auto huge=std::make_shared<CountingSource>(); huge->sizes={1,1,1,1000000000,1000000000,4};
  rejects<std::invalid_argument>([&]{reslice_axis_range(Dataset(huge),{Plane::XY,0,0,1,0,0,
      std::numeric_limits<std::size_t>::max()});},"Complete output/fallback storage overflow was admitted");
  check(counter->planes.empty() && counter->regions.empty() && huge->planes.empty() && huge->regions.empty(),
        "Metadata/range/geometry/cap/overflow rejection read source pixels");
  const auto xy=reslice_axis_range(source,{Plane::XY,1,7,3,1,2});
  check(counter->planes.size()==3 && counter->regions.empty(),"XY read planes outside emitted range");
  for(int k=0;k<3;++k) check(counter->planes[k].t==1 && counter->planes[k].c==2 && counter->planes[k].z==1+3*k,
                            "XY read order or selected scientific T/C differs");
  counter->planes.clear(); counter->regional_only=true;
  const auto xz=reslice_axis_range(source,{Plane::XZ,1,7,3,1,2,cap});
  check(xz.working_bytes==cap && counter->planes.empty() && counter->regions.size()==27,
        "XZ did not use exactly one selected strip per source Z/output plane");
  for(int k=0;k<3;++k) for(int z=0;z<9;++z)
    check(counter->regions[k*9+z]==std::array<int,7>{1,2,z,0,1+3*k,32,1},"XZ strip extent/order differs");
  counter->regions.clear();
  reslice_axis_range(source,{Plane::YZ,1,7,3,1,2});
  check(counter->planes.empty() && counter->regions.size()==27,"YZ read an unselected full plane/range");
  for(int k=0;k<3;++k) for(int z=0;z<9;++z)
    check(counter->regions[k*9+z]==std::array<int,7>{1,2,z,1+3*k,0,1,17},"YZ strip extent/order differs");
  std::cout << "G029.range.native.R03_validation_budget_selected_reads\n";
}
void captured_callbacks_and_cancellation() {
  auto source=finite_source(); auto retained=source;
  const auto replacement=finite_source(PixelType::UInt8);
  AxisRangeResliceRequest request{Plane::XZ,1,7,3,1,2};
  ExecutionControl control; bool changed=false; std::size_t previous=0;
  control.checkpoint=[&]{changed=true; request={Plane::XY,0,0,1,0,0,0}; source=replacement;};
  control.progress=[&](std::size_t done,std::size_t total){check(total==27 && done>=previous && done<=total,
      "Range read progress did not use checked complete-work totals");previous=done;};
  const auto result=reslice_axis_range(source,request,control);
  check(changed && previous==27 && result.request.first==1 && result.request.last==7 && result.request.step==3 &&
        result.request.t==1 && result.request.c==2 && result.dataset.pixel_type()==PixelType::Float64 &&
        result.dataset.plane({0,0,2})->at(3,4)==finite_value(PixelType::Float64,1,2,4,7,3,0),
        "A callback retargeted the range request or retained Dataset");
  for(auto plane:{Plane::XY,Plane::XZ,Plane::YZ}) {
    auto counter=std::make_shared<CountingSource>();
    counter->sizes=plane==Plane::YZ?DatasetShape{1,1,3,5000,3,1}:DatasetShape{1,1,3,3,5000,1};
    auto token=std::make_shared<CancellationToken>(); bool copied=false;
    ExecutionControl cancel; cancel.cancellation=token;
    cancel.stage_progress=[&](std::string_view stage,std::size_t done,std::size_t){
      if(stage=="plane-extract.copy" && done>=1024){copied=true;token->cancel();}};
    rejects<OperationCancelled>([&]{reslice_axis_range(Dataset(counter),{plane,0,1,1},cancel);},
                                "Positive owned-copy cancellation failed");
    check(copied,"Cancellation did not occur after actual copied samples");
  }
  auto counter=std::make_shared<CountingSource>(); auto token=std::make_shared<CancellationToken>(); token->cancel();
  rejects<OperationCancelled>([&]{reslice_axis_range(Dataset(counter),{}, {token});},"Pre-canceled range was admitted");
  check(counter->planes.empty() && counter->regions.empty(),"Pre-cancellation read pixels");
  token=std::make_shared<CancellationToken>(); ExecutionControl between; between.cancellation=token;
  between.progress=[&](std::size_t done,std::size_t){if(done==1)token->cancel();};
  rejects<OperationCancelled>([&]{reslice_axis_range(Dataset(counter),{Plane::XY,0,8,1},between);},
                              "Between-plane cancellation failed");
  check(counter->planes.size()==1,"Between-plane cancellation read another output plane");
  Application app; auto owner=std::make_shared<Document>(retained,"Captured");app.adopt(owner);
  auto snapshot=owner->snapshot(); const auto old=snapshot;
  const auto other_snapshot=Document(replacement,"Other").snapshot();
  request={Plane::XY,1,7,3,1,2}; control.progress={};
  control.checkpoint=[&]{request={Plane::YZ,0,0,1};snapshot=other_snapshot;};
  auto prepared=prepare_axis_range_reslice(snapshot,request,control);
  check(prepared.target().source().same_document(old) && prepared.target().position_count()==3 &&
        prepared.prepared_new_document()->snapshot().data().metadata.title=="Captured XY 2-8 step 3",
        "A callback retargeted the captured snapshot/request/title");
  app.publish(prepared);
  std::cout << "G029.range.native.R04_capture_progress_cancellation\n";
}
std::shared_ptr<Document> annotated_owner() {
  DocumentData data{finite_source(),{"Source",{{"property",std::int64_t(7)}}},{},{}};
  data.metadata.extensions["opaque"]=true;data.metadata.record_layout={{"present",true}};
  data.selection=Selection("rectangle",{{1,1},{4,3}});data.previous_selection=Selection("point",{{2,2}});
  data.rois={{"saved",Selection("point",{{3,3}}),1,2,4}};data.overlays={{{"overlay",true}}};
  data.spectrum=FourierState(Spectrum{32,17,std::vector<std::complex<double>>(544)},std::array<int,2>{17,32});
  auto owner=std::make_shared<Document>(std::move(data));
  auto edit=owner->begin_edit();edit.set_property("history",true);owner->commit(edit);owner->undo();
  return owner;
}
void fresh_publication_and_rejections() {
  Application app;auto owner=annotated_owner();app.adopt(owner);
  const auto before=owner->snapshot(); const auto history=owner->history();
  auto prepared=app.prepare_axis_range_reslice({Plane::XZ,1,7,3,1,2},owner->id);
  auto candidate=prepared.prepared_new_document();const auto snapshot=candidate->snapshot();const auto &data=snapshot.data();
  check(prepared.operation()=="axis_range_reslice" && prepared.target().dependencies()==(Change::Pixels|Change::Metadata) &&
        prepared.target().position_count()==9 && candidate && !app.contains(candidate->id) && !snapshot.dirty() &&
        candidate->history().undo_steps==0 && candidate->history().redo_steps==0,"Range preparation fresh-owner semantics differ");
  check(data.metadata.title=="Source XZ 2-8 step 3" && data.metadata.properties.empty() && data.metadata.record_layout.empty() &&
        data.metadata.extensions.size()==1 && data.metadata.extensions.count("axis_range_reslice")==1 && !data.selection &&
        !data.previous_selection && data.rois.empty() && data.overlays.empty() && !data.spectrum,"Range output retained source annotations");
  const auto tree=std::get<std::shared_ptr<const MetadataTree>>(data.metadata.extensions.at("axis_range_reslice"));
  const auto &provenance=std::get<MetadataObject>(tree->value);
  check(std::get<std::int64_t>(provenance.at("count"))==3 && std::get<std::int64_t>(provenance.at("first"))==1 &&
        std::get<std::int64_t>(provenance.at("last"))==7 && std::get<std::int64_t>(provenance.at("step"))==3 &&
        std::get<std::int64_t>(provenance.at("t"))==1 && std::get<std::int64_t>(provenance.at("c"))==2,
        "Range provenance differs from the frozen typed request");
  auto display=snapshot.display();display.zoom=2;display.planeZoom={2,2,2};candidate->set_display(display);candidate->set_history_policy(3,4096);
  const auto published=app.publish(prepared);
  check(published.created && published.document==candidate && app.active_document_id()==candidate->id && !prepared.active() &&
        candidate->history().undo_steps==0 && candidate->snapshot().display().zoom==2,"Range did not publish the exact staged owner once");
  check(owner->snapshot().shares_data(before) && owner->snapshot().revisions().matches(before.revisions(),Change::All) &&
        owner->snapshot().dirty()==before.dirty() && owner->history().undo_steps==history.undo_steps &&
        owner->history().redo_steps==history.redo_steps,"Range publication changed source data/history/cameras");
  display=candidate->snapshot().display();display.crosshair[2]=2;candidate->set_display(display);
  check(candidate->snapshot().data().dataset.plane({0,0,2})->at(3,4)==finite_value(PixelType::Float64,1,2,4,7,3,0),
        "Last emitted plane is not ordinarily navigable");
  rejects<std::invalid_argument>([&]{app.publish(prepared);},"Range preparation published twice");
  for(int scenario=0;scenario<9;++scenario) {
    Application local;auto source=annotated_owner();local.adopt(source);auto token=std::make_shared<CancellationToken>();
    auto operation=local.prepare_axis_range_reslice({Plane::XY,1,7,3,1,2},source->id,{token});auto fresh=operation.prepared_new_document();
    if(scenario==0){auto edit=source->begin_edit();edit.set_property("stale",true);source->commit(edit);}
    else if(scenario==1){auto edit=source->begin_edit();edit.replace_plane({1,2,1},Image(32,17));source->commit(edit);}
    else if(scenario==2)local.close(source->id,true);
    else if(scenario==3)token->cancel();
    else if(scenario==4){auto edit=fresh->begin_edit();edit.set_title("Tampered");fresh->commit(edit);}
    else if(scenario==5){auto edit=fresh->begin_edit();edit.select(Selection("point",{{1,1}}));fresh->commit(edit);}
    else if(scenario==6){auto edit=fresh->begin_edit();edit.replace_plane({},Image(32,17));fresh->commit(edit);fresh->undo();fresh->set_history_policy(1,1);}
    else if(scenario==7)fresh->checkpoint();
    else {auto edit=source->begin_edit();auto changed=source->snapshot().data().dataset.calibration();
          changed.spacing[0]=.8;edit.set_calibration(changed);source->commit(edit);}
    const auto ids=local.document_ids();const auto preserved=source->snapshot();
    if(scenario==2)rejects<DocumentUnavailable>([&]{local.publish(operation);},"Closed range source was adopted");
    else if(scenario==3)rejects<OperationCancelled>([&]{local.publish(operation);},"Canceled range preparation was adopted");
    else rejects<RevisionConflict>([&]{local.publish(operation);},"Stale/modified range source/candidate was adopted");
    check(operation.active() && local.document_ids()==ids && !local.contains(fresh->id) && source->snapshot().shares_data(preserved),
          "Range rejection consumed/adopted output or changed the source");
  }
  Application local;auto source=annotated_owner();local.adopt(source);
  auto operation=local.prepare_axis_range_reslice({Plane::XY,1,7,3,1,2},source->id);
  rejects<std::invalid_argument>([&]{local.publish(operation,"Different");},"Range accepted a new publication title");
  Application foreign;foreign.create(Dataset::from_stack(Stack{Image(2,2)}));
  rejects<DocumentUnavailable>([&]{foreign.publish(operation);},"Range source rebound to another application");
  auto view=source->snapshot().display();view.t=0;view.c=0;view.crosshair={31,16,8};source->set_display(view);
  source->select(Selection("point",{{2,2}}));const auto allowed=source->snapshot();
  const auto output=local.publish(operation);
  check(source->snapshot().shares_data(allowed) && output.document->snapshot().data().dataset.plane({0,0,2})->at(3,4)==
        finite_value(PixelType::Float64,1,2,7,4,3,0),"Allowed selection/display changes retargeted the range");
  std::cout << "G029.range.native.R05_fresh_publication_rejections\n";
}
void retained_generic_publication() {
  Application app;app.create(Dataset::from_stack(Stack{Image(3,2,1,PixelType::UInt16)}));
  TargetRequest target;target.destination=OutputDestination::NewDocument;
  auto operation=app.prepare("add",app.resolve_target(target),{{"value",8}});
  check(!operation.prepared_new_document(),"Generic publication acquired a range fresh owner");
  const auto output=app.publish(operation,"Generic");
  check(output.document->history().undo_steps==1 && output.document->snapshot().data().metadata.title=="Generic" &&
        output.document->snapshot().data().dataset.plane()->at(0,0)==8,"Generic publication behavior changed");
  std::cout << "G029.range.native.R06_generic_publication_retained\n";
}
} // namespace
int main() {
  try {
    finite_ranges_and_singletons();owned_extensions_bits();validation_budget_and_reads();
    captured_callbacks_and_cancellation();fresh_publication_and_rejections();retained_generic_publication();
    return 0;
  } catch(const std::exception &error) {std::cerr << error.what() << '\n';return 1;}
}
