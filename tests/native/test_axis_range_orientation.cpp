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
std::array<int,3> axes(Plane plane) {
  if (plane == Plane::XY) return {0,1,2};
  if (plane == Plane::XZ) return {0,2,1};
  return {1,2,0};
}
SpatialCalibration calibration() {
  SpatialCalibration value;
  value.spacing = {.4,1.2,3}; value.origin = {11,-7,19};
  value.direction = {1,.25,-.5,.5,-1,.125,0,.75,1.5};
  value.unit = "um"; value.time_unit = "min";
  value.frame_interval = .75; value.time_origin = 2.25;
  return value;
}
double code(PixelType type, int t, int c, int z, int y, int x, int s) {
  const int value = (97*t + 31*c + 11*z + 5*y + 3*x + s) % 251;
  if (type == PixelType::UInt16) return 40000 + value;
  if (type == PixelType::Float32) return value - 125.25;
  if (type == PixelType::Float64) return value - 125.125;
  return value;
}
Dataset source(PixelType type, int samples, DatasetShape shape = {2,3,9,17,32,1}) {
  shape.s = samples;
  Stack planes;
  for (int t=0;t<shape.t;++t) for (int c=0;c<shape.c;++c) for (int z=0;z<shape.z;++z) {
    Image image(shape.x,shape.y,samples,type);
    for (int y=0;y<shape.y;++y) for (int x=0;x<shape.x;++x) for (int s=0;s<samples;++s)
      image.at(x,y,s) = code(type,t,c,z,y,x,s);
    planes.push_back(std::move(image));
  }
  return Dataset::from_planes(shape,std::move(planes),calibration());
}
AxisRangeOrientationRequest request(Plane plane, int first, int last, int step, int flags,
                                    int t=1, int c=2) {
  return {{plane,first,last,step,t,c},bool(flags&1),bool(flags&2),bool(flags&4)};
}
void compare(const Dataset &input, const AxisRangeOrientationResult &result) {
  const auto &r = result.request; const auto &range = r.range;
  const auto mapping = axes(range.plane); const auto in = input.shape();
  const std::array<int,3> extent{in.x,in.y,in.z};
  const int width = extent[mapping[0]], height = extent[mapping[1]];
  const int count = 1+(range.last-range.first)/range.step;
  const auto actual = result.dataset.shape();
  check(actual.dimensions()==std::array<int,6>{1,1,count,r.transpose?width:height,
        r.transpose?height:width,in.s},"Orientation shape/type/sample dimensions differ");
  check(result.dataset.pixel_type()==input.pixel_type() &&
        result.dataset.sample_policy()==input.sample_policy() && !result.dataset.shares_pixels(input),
        "Orientation changed type/policy or reused source pixel storage");
  const auto &cal = result.dataset.calibration();
  check(cal.unit==input.calibration().unit && cal.time_unit==input.calibration().time_unit &&
        cal.time_origin==input.calibration().time(range.t) &&
        cal.frame_interval==input.calibration().frame_interval,
        "Selected scientific T/calibration units differ");
  // Independent forward mapping: place each selected source voxel in the
  // output, rather than repeat the implementation's output-to-source sampler.
  for (int n=0;n<count;++n) {
    const int k = r.reverse_order ? count-1-n : n;
    const auto output = result.dataset.plane({0,0,k});
    const int fixed = range.first+n*range.step;
    for (int v=0;v<height;++v) for (int u=0;u<width;++u) {
      std::array<int,3> point{};
      point[mapping[0]]=u; point[mapping[1]]=v; point[mapping[2]]=fixed;
      const int flipped = r.flip_vertical ? height-1-v : v;
      const int out_x = r.transpose ? flipped : u;
      const int out_y = r.transpose ? u : flipped;
      const auto original = input.plane({range.t,range.c,point[2]});
      for (int s=0;s<in.s;++s) {
        const auto expected = original->at(point[0],point[1],s);
        const auto observed = output->at(out_x,out_y,s);
        if (input.sample_policy()==FloatingSamplePolicy::PreserveQuietNaNFloat32)
          check(float32_source_sample_bits(expected,input.sample_policy())==
                float32_source_sample_bits(observed,input.sample_policy()),
                "Orientation changed quiet-NaN payload or Float32 signed-zero bits");
        else check(std::memcmp(&expected,&observed,sizeof(double))==0,
                   "Orientation changed finite component/signed-zero bits");
      }
      const auto world = input.calibration().voxel_to_world(
          {double(point[0]),double(point[1]),double(point[2])});
      const auto oriented = cal.voxel_to_world({double(out_x),double(out_y),double(k)});
      for (int row=0;row<3;++row)
        check(std::abs(world[row]-oriented[row])<=1e-12*std::max(1.0,std::abs(world[row])),
              "Orientation lost source world coordinates");
    }
  }
  check(result.first_plane_geometry.origin==cal.origin,"First-plane geometry origin differs");
  const std::array<std::string,3> labels{"X","Y","Z"};
  check(result.first_plane_geometry.horizontal_axis==labels[mapping[r.transpose?1:0]] &&
        result.first_plane_geometry.vertical_axis==labels[mapping[r.transpose?0:1]],
        "First-plane source axis labels differ");
  for (int column=0;column<3;++column) {
    const int original_axis = mapping[column==2?2:r.transpose?1-column:column];
    check(cal.spacing[column]==input.calibration().spacing[original_axis]*(column==2?range.step:1),
          "Orientation spacing is not positive permuted/strided source spacing");
    for (int row=0;row<3;++row) {
      const bool negative = column==2 ? r.reverse_order :
          r.flip_vertical && (r.transpose ? column==0 : column==1);
      check(cal.direction[row*3+column]==input.calibration().direction[row*3+original_axis]*(negative?-1:1),
            "Orientation sign/permutation differs, including singleton output");
    }
  }
  for (int row=0;row<3;++row)
    check(result.first_plane_geometry.horizontal_step[row]==cal.direction[row*3]*cal.spacing[0] &&
          result.first_plane_geometry.vertical_step[row]==cal.direction[row*3+1]*cal.spacing[1],
          "First-plane direct basis differs");
  const auto plane_bytes = std::size_t(width)*height*in.s*sizeof(double);
  const auto fallback = std::size_t(in.x)*in.y*in.s*sizeof(double);
  const auto strip = range.plane==Plane::XY ? 0 : std::size_t(width)*in.s*sizeof(double);
  check(result.working_bytes==std::size_t(count)*plane_bytes+plane_bytes+fallback+strip,
        "Orientation conservative estimate omits scratch/output/fallback/strip");
}
void all_flags_and_owned_samples() {
  const std::array<std::pair<PixelType,int>,6> principal{{
      {PixelType::UInt8,1},{PixelType::UInt16,1},{PixelType::Float32,1},
      {PixelType::Float64,1},{PixelType::UInt8,3},{PixelType::UInt8,4}}};
  std::size_t mappings=0;
  for (auto profile:principal) {
    const auto input=source(profile.first,profile.second);
    for (auto plane:{Plane::XY,Plane::XZ,Plane::YZ}) {
      int last=(plane==Plane::XY?9:plane==Plane::XZ?17:32)-1;
      if ((last-1)%3==0) --last;
      for (int flags=0;flags<8;++flags) {
        compare(input,reslice_oriented_axis_range(input,request(plane,1,last,3,flags)));
        ++mappings;
      }
    }
  }
  check(mappings==144,"Principal mapping denominator differs");
  for (auto profile:std::array<std::pair<PixelType,int>,6>{{
      {PixelType::UInt16,3},{PixelType::UInt16,4},{PixelType::Float32,3},
      {PixelType::Float32,4},{PixelType::Float64,3},{PixelType::Float64,4}}}) {
    const auto input=source(profile.first,profile.second);
    for (auto plane:{Plane::XY,Plane::XZ,Plane::YZ}) for (int flags=0;flags<8;++flags)
      compare(input,reslice_oriented_axis_range(input,request(plane,1,7,3,flags)));
  }
  std::cout << "G029.orientation.native.O01_144_principal_and144_admitted_extended_mappings\n";
}
void singletons_defaults_and_bits() {
  const auto input=source(PixelType::Float64,1);
  for (auto plane:{Plane::XY,Plane::XZ,Plane::YZ}) {
    const int last=(plane==Plane::XY?9:plane==Plane::XZ?17:32)-1;
    for (int flags=0;flags<8;++flags) {
      compare(input,reslice_oriented_axis_range(input,request(plane,last,last,3,flags)));
      compare(input,reslice_oriented_axis_range(input,request(plane,1,last,std::numeric_limits<int>::max(),flags)));
    }
    const auto plain=reslice_axis_range(input,{plane,1,7,3,1,2});
    const auto oriented=reslice_oriented_axis_range(input,request(plane,1,7,3,0));
    check(plain.dataset.calibration().spacing==oriented.dataset.calibration().spacing &&
          plain.dataset.calibration().direction==oriented.dataset.calibration().direction &&
          plain.dataset.calibration().origin==oriented.dataset.calibration().origin,
          "Flags-off geometry changed the existing range default");
    for (int k=0;k<3;++k)
      check(plain.dataset.plane({0,0,k})->pixels==oriented.dataset.plane({0,0,k})->pixels,
            "Flags-off pixels changed the existing range default");
  }
  const auto degenerate=source(PixelType::UInt8,4,{1,1,1,1,1,4});
  for (auto plane:{Plane::XY,Plane::XZ,Plane::YZ}) for (int flags=0;flags<8;++flags)
    compare(degenerate,reslice_oriented_axis_range(degenerate,request(plane,0,0,3,flags,0,0)));
  constexpr auto policy=FloatingSamplePolicy::PreserveQuietNaNFloat32;
  const std::array<std::uint32_t,8> bits{0,0x80000000u,1,0x007fffffu,0x7fc12345u,0xffc34567u,0x3f800000u,0xbf800000u};
  Stack planes;
  for (int z=0;z<5;++z) {
    Image image(7,5,1,PixelType::Float32);
    for (std::size_t i=0;i<image.pixels.size();++i)
      image.pixels[i]=float32_source_sample(bits[(i+std::size_t(z)*35)%bits.size()],policy);
    planes.push_back(std::move(image));
  }
  const auto quiet=Dataset::from_planes({1,1,5,5,7,1},std::move(planes),calibration(),policy);
  for (auto plane:{Plane::XY,Plane::XZ,Plane::YZ}) for (int flags=0;flags<8;++flags)
    compare(quiet,reslice_oriented_axis_range(quiet,request(plane,0,4,2,flags,0,0)));
  std::cout << "G029.orientation.native.O02_singletons_defaults_degenerate_quiet_bits\n";
}
class CountingSource final : public PlaneSource {
public:
  DatasetShape sizes{2,3,9,17,32,1};
  mutable std::vector<PlanePosition> planes;
  mutable std::vector<std::array<int,7>> regions;
  bool regional_only=false;
  DatasetShape shape() const override { return sizes; }
  PixelType pixel_type() const override { return PixelType::Float64; }
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    planes.push_back(p);
    if (regional_only) throw std::runtime_error("Unexpected full-plane read");
    auto result=std::make_shared<Image>(sizes.x,sizes.y,sizes.s,pixel_type());
    fill(*result,p,0,0); return result;
  }
  Image read_region(PlanePosition p,int x,int y,int width,int height) const override {
    regions.push_back({p.t,p.c,p.z,x,y,width,height});
    Image result(width,height,sizes.s,pixel_type()); fill(result,p,x,y); return result;
  }
  void fill(Image &image,PlanePosition p,int x,int y) const {
    for (int v=0;v<image.height;++v) for (int u=0;u<image.width;++u) for (int s=0;s<sizes.s;++s)
      image.at(u,v,s)=code(pixel_type(),p.t,p.c,p.z,y+v,x+u,s);
  }
};
void validation_budget_and_read_order() {
  auto counter=std::make_shared<CountingSource>(); const Dataset input(counter,calibration());
  for (const auto bad:{AxisRangeResliceRequest{static_cast<Plane>(99),0,0,1},
      AxisRangeResliceRequest{Plane::XY,-1,0,1},{Plane::XY,2,1,1},{Plane::XY,0,9,1},
      {Plane::XY,0,1,0},{Plane::XY,0,1,-1},{Plane::XY,0,1,1,-1,0},
      {Plane::XY,0,1,1,2,0},{Plane::XY,0,1,1,0,-1},{Plane::XY,0,1,1,0,3},
      {Plane::XY,0,1,1,0,0,0},{Plane::XY,0,1,1,0,0,1}})
    rejects<std::exception>([&]{reslice_oriented_axis_range(input,{bad,true,true,true});},
                            "Invalid orientation range/index/cap was admitted");
  const auto cap=std::size_t(4*32*9+32*17+32)*sizeof(double);
  auto r=request(Plane::XZ,1,8,3,7); r.range.max_working_bytes=cap-1;
  rejects<std::invalid_argument>([&]{reslice_oriented_axis_range(input,r);},"Scratch/output budget was not enforced");
  auto cal=calibration(); cal.spacing[1]=1e308;
  rejects<std::invalid_argument>([&]{reslice_oriented_axis_range(input.with_calibration(cal),request(Plane::XZ,0,0,3,7));},
                                 "Signed singleton basis overflow was admitted");
  auto huge=std::make_shared<CountingSource>(); huge->sizes={1,1,1,1000000000,1000000000,4};
  auto giant=request(Plane::XY,0,0,1,7,0,0); giant.range.max_working_bytes=std::numeric_limits<std::size_t>::max();
  rejects<std::invalid_argument>([&]{reslice_oriented_axis_range(Dataset(huge),giant);},"Storage overflow was admitted");
  check(counter->planes.empty() && counter->regions.empty() && huge->planes.empty() && huge->regions.empty(),
        "Invalid orientation metadata/budget read source pixels");
  reslice_oriented_axis_range(input,request(Plane::XY,1,8,3,7));
  check(counter->planes.size()==3 && counter->regions.empty(),"XY read unselected planes");
  for (int k=0;k<3;++k)
    check(counter->planes[k].t==1 && counter->planes[k].c==2 && counter->planes[k].z==7-3*k,
          "Reverse used requested last instead of last selected plane");
  counter->planes.clear(); counter->regional_only=true;
  r.range.max_working_bytes=cap;
  const auto result=reslice_oriented_axis_range(input,r);
  check(result.working_bytes==cap && counter->planes.empty() && counter->regions.size()==27,
        "XZ strip/scratch budget or selected read total differs");
  for (int k=0;k<3;++k) for (int z=0;z<9;++z)
    check(counter->regions[k*9+z]==std::array<int,7>{1,2,z,0,7-3*k,32,1},
          "XZ reversal or selected strip extent differs");
  counter->regions.clear();
  reslice_oriented_axis_range(input,request(Plane::YZ,1,8,3,7));
  check(counter->planes.empty() && counter->regions.size()==27,"YZ read total/full-plane scope differs");
  for (int k=0;k<3;++k) for (int z=0;z<9;++z)
    check(counter->regions[k*9+z]==std::array<int,7>{1,2,z,7-3*k,0,1,17},
          "YZ reversal or selected strip extent differs");
  std::cout << "G029.orientation.native.O03_zero_reads_budget_selected_reverse_order\n";
}
void captured_callbacks() {
  auto input=source(PixelType::Float64,1); const auto retained=input;
  const auto replacement=source(PixelType::UInt8,1);
  auto r=request(Plane::XZ,1,8,3,7); const auto captured=r;
  ExecutionControl control; bool changed=false; std::size_t previous=0;
  control.checkpoint=[&]{changed=true; input=replacement; r={{Plane::XY,0,0,1,0,0,0},false,false,false};};
  control.progress=[&](std::size_t done,std::size_t total){
    check(total==31 && done>=previous && done<=total,"Orientation progress/capture total differs"); previous=done;};
  const auto result=reslice_oriented_axis_range(input,r,control);
  check(changed && previous==31 && result.request.range.first==captured.range.first &&
        result.request.reverse_order && result.request.flip_vertical && result.request.transpose,
        "Callback changed the captured nested request");
  compare(retained,result);
  Application app; auto owner=std::make_shared<Document>(retained,"Captured"); app.adopt(owner);
  auto snapshot=owner->snapshot(); const auto original=snapshot;
  const auto other=Document(source(PixelType::UInt8,1),"Other").snapshot();
  r=request(Plane::XY,1,8,3,7); control.progress={};
  control.checkpoint=[&]{snapshot=other; r={{Plane::YZ,0,0,1},false,false,false};};
  auto prepared=prepare_axis_range_orientation(snapshot,r,control);
  check(prepared.target().source().same_document(original) && prepared.target().position_count()==3 &&
        prepared.prepared_new_document()->snapshot().data().metadata.title=="Captured XY 2-9 step 3 [R1 F1 T1]",
        "Preparation callback changed captured source/request/title");
  app.publish(prepared);
  std::cout << "G029.orientation.native.O04_captured_nested_request_source_progress\n";
}
std::shared_ptr<Document> annotated_owner() {
  DocumentData data{source(PixelType::Float64,1),{"Source",{{"property",std::int64_t(7)}}},{},{}};
  data.metadata.extensions["opaque"]=true; data.metadata.record_layout={{"present",true}};
  data.selection=Selection("rectangle",{{1,1},{4,3}}); data.previous_selection=Selection("point",{{2,2}});
  data.rois={{"saved",Selection("point",{{3,3}}),1,2,4}}; data.overlays={{{"overlay",true}}};
  data.spectrum=FourierState(Spectrum{32,17,std::vector<std::complex<double>>(544)},std::array<int,2>{17,32});
  auto owner=std::make_shared<Document>(std::move(data));
  auto edit=owner->begin_edit(); edit.set_property("history",true); owner->commit(edit); owner->undo();
  return owner;
}
void fresh_publication_and_rejections() {
  Application app; auto owner=annotated_owner(); app.adopt(owner);
  const auto before=owner->snapshot(); const auto history=owner->history();
  auto prepared=app.prepare_axis_range_orientation(request(Plane::XZ,1,8,3,7),owner->id);
  auto candidate=prepared.prepared_new_document(); const auto snapshot=candidate->snapshot(); const auto &data=snapshot.data();
  check(prepared.operation()=="axis_range_orientation" && prepared.target().dependencies()==(Change::Pixels|Change::Metadata) &&
        candidate && !app.contains(candidate->id) && !snapshot.same_document(before) && !snapshot.shares_data(before) &&
        !snapshot.dirty() && candidate->history().undo_steps==0 && candidate->history().redo_steps==0,
        "Orientation did not stage an independent exact fresh owner");
  check(data.metadata.title=="Source XZ 2-9 step 3 [R1 F1 T1]" && data.metadata.properties.empty() &&
        data.metadata.record_layout.empty() && data.metadata.extensions.size()==1 &&
        data.metadata.extensions.count("axis_range_orientation")==1 && !data.selection && !data.previous_selection &&
        data.rois.empty() && data.overlays.empty() && !data.spectrum,"Orientation retained source annotations");
  const auto tree=std::get<std::shared_ptr<const MetadataTree>>(data.metadata.extensions.at("axis_range_orientation"));
  const auto &provenance=std::get<MetadataObject>(tree->value);
  check(std::get<std::int64_t>(provenance.at("last_selected"))==7 && std::get<std::int64_t>(provenance.at("count"))==3 &&
        std::get<bool>(provenance.at("reverse_order")) && std::get<bool>(provenance.at("flip_vertical")) &&
        std::get<bool>(provenance.at("transpose")),"Frozen orientation provenance differs");
  auto display=snapshot.display(); display.zoom=2; display.planeZoom={2,2,2}; candidate->set_display(display);
  candidate->set_history_policy(3,4096);
  const auto published=app.publish(prepared);
  check(published.created && published.document==candidate && app.active_document_id()==candidate->id && !prepared.active() &&
        candidate->history().undo_steps==0 && candidate->snapshot().display().zoom==2,
        "Orientation did not attach the exact staged owner once");
  check(owner->snapshot().shares_data(before) && owner->snapshot().revisions().matches(before.revisions(),Change::All) &&
        owner->history().undo_steps==history.undo_steps && owner->history().redo_steps==history.redo_steps,
        "Orientation publication changed the source/history/cameras");
  rejects<std::invalid_argument>([&]{app.publish(prepared);},"Orientation published twice");
  for (int scenario=0;scenario<9;++scenario) {
    Application local; auto input=annotated_owner(); local.adopt(input); auto token=std::make_shared<CancellationToken>();
    auto operation=local.prepare_axis_range_orientation(request(Plane::XY,1,8,3,7),input->id,{token});
    auto fresh=operation.prepared_new_document();
    if (scenario==0) {auto edit=input->begin_edit();edit.set_property("stale",true);input->commit(edit);}
    else if (scenario==1) {auto edit=input->begin_edit();edit.replace_plane({1,2,1},Image(32,17));input->commit(edit);}
    else if (scenario==2) local.close(input->id,true);
    else if (scenario==3) token->cancel();
    else if (scenario==4) {auto edit=fresh->begin_edit();edit.set_title("Tampered");fresh->commit(edit);}
    else if (scenario==5) {auto edit=fresh->begin_edit();edit.select(Selection("point",{{1,1}}));fresh->commit(edit);}
    else if (scenario==6) {auto edit=fresh->begin_edit();edit.replace_plane({},Image(17,32));fresh->commit(edit);fresh->undo();}
    else if (scenario==7) fresh->checkpoint();
    else {auto edit=input->begin_edit();auto cal=input->snapshot().data().dataset.calibration();cal.spacing[0]=.8;
          edit.set_calibration(cal);input->commit(edit);}
    const auto ids=local.document_ids(); const auto preserved=input->snapshot(); const auto active=local.active_document_id();
    if (scenario==2) rejects<DocumentUnavailable>([&]{local.publish(operation);},"Closed orientation source was adopted");
    else if (scenario==3) rejects<OperationCancelled>([&]{local.publish(operation);},"Canceled orientation output was adopted");
    else rejects<RevisionConflict>([&]{local.publish(operation);},"Stale/modified orientation output was adopted");
    check(operation.active() && local.document_ids()==ids && local.active_document_id()==active &&
          !local.contains(fresh->id) && input->snapshot().shares_data(preserved),
          "Rejected orientation changed membership/active owner/source");
  }
  Application local; auto input=annotated_owner(); local.adopt(input);
  auto operation=local.prepare_axis_range_orientation(request(Plane::XY,1,8,3,7),input->id);
  rejects<std::invalid_argument>([&]{local.publish(operation,"Different");},"Orientation accepted a replacement title");
  Application foreign; foreign.create(Dataset::from_stack(Stack{Image(2,2)}));
  rejects<DocumentUnavailable>([&]{foreign.publish(operation);},"Orientation source rebound to foreign application");
  auto view=input->snapshot().display();view.crosshair={31,16,8};view.t=0;view.c=0;input->set_display(view);
  input->select(Selection("point",{{2,2}})); const auto allowed=input->snapshot();
  const auto output=local.publish(operation);
  check(input->snapshot().shares_data(allowed) && output.document==operation.prepared_new_document(),
        "Allowed selection/display changes retargeted the orientation");
  std::cout << "G029.orientation.native.O05_exact_fresh_owner_state_atomic_publication\n";
}
void admitted_samples_and_scientific_channels() {
  for (int samples : {2,5}) {
    auto counter = std::make_shared<CountingSource>();
    counter->sizes.s = samples;
    rejects<std::invalid_argument>([&] { Dataset invalid(counter, calibration()); },
        "Unsupported S2/S5 dataset was admitted");
    check(counter->planes.empty() && counter->regions.empty(),
          "Unsupported sample layout read source pixels before admission");
  }
  std::size_t mappings = 0;
  for (int channels : {2,5}) {
    const auto input = source(PixelType::UInt16, 1, {2,channels,9,17,32,1});
    for (auto plane : {Plane::XY,Plane::XZ,Plane::YZ}) for (int flags=0;flags<8;++flags) {
      compare(input, reslice_oriented_axis_range(input,
          request(plane,1,7,3,flags,1,channels-1)));
      ++mappings;
    }
  }
  check(mappings == 48, "Scientific C2/C5 mapping denominator differs");
  std::cout << "G029.orientation.native.O06_S2_S5_admission_zero_reads_scientific_C2_C5_48\n";
}
}
int main() {
  try {
    all_flags_and_owned_samples(); singletons_defaults_and_bits(); validation_budget_and_read_order();
    captured_callbacks(); fresh_publication_and_rejections(); admitted_samples_and_scientific_channels(); return 0;
  } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
