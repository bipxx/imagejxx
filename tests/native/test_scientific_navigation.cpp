// Independent P06b scientific read/navigation literals. No renderer output or
// product serializer supplies expected values or plane coordinates.
#include "desktop.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <new>
#include <stdexcept>
#include <vector>

namespace {
using namespace imagejxx;
namespace desktop=imagejxx::desktop;
namespace fs=std::filesystem;
using Json=desktop::Json;
std::size_t assertions=0;
void check(bool value,const char *message) {
  if(!value)throw std::runtime_error(message);
  ++assertions;
}
template<class Function>void rejects(Function function,const char *message) {
  bool rejected=false;
  try{function();}catch(const std::bad_alloc&){throw;}catch(const std::exception&){rejected=true;}
  check(rejected,message);
}
double value(int t,int c,int z,int y,int x,int s,int samples) {
  return samples==1 ? 10000*t+1000*c+100*z+10*y+x : 73*t+41*c+17*z+7*y+3*x+s;
}
SpatialCalibration calibration() {
  SpatialCalibration result;
  result.spacing={.25,1.5,3};result.origin={-7,11,-13};
  result.direction={-1,.5,0,0,1,.25,0,0,1};
  result.unit="um";result.time_unit="ms";
  result.frame_interval=.75;result.time_origin=2.5;
  return result;
}
std::shared_ptr<Document> owner(DatasetShape shape,PixelType type) {
  Stack planes;
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z) {
    Image image(shape.x,shape.y,shape.s,type);
    for(int y=0;y<shape.y;++y)for(int x=0;x<shape.x;++x)for(int s=0;s<shape.s;++s)
      image.at(x,y,s)=value(t,c,z,y,x,s,shape.s);
    planes.push_back(std::move(image));
  }
  return std::make_shared<Document>(Dataset::from_planes(shape,std::move(planes),calibration()),"scientific literal");
}
void image(const Image &actual,DatasetShape shape,PixelType type,Plane plane,int fixed,int t,int c) {
  const int width=plane==Plane::YZ?shape.y:shape.x;
  const int height=plane==Plane::XY?shape.y:shape.z;
  check(actual.width==width && actual.height==height && actual.channels==shape.s && actual.type==type,
        "Scientific facade returned wrong plane extent/sample layout/type");
  for(int v=0;v<height;++v)for(int u=0;u<width;++u)for(int s=0;s<shape.s;++s) {
    const int x=plane==Plane::YZ?fixed:u;
    const int y=plane==Plane::XY?v:plane==Plane::XZ?fixed:u;
    const int z=plane==Plane::XY?fixed:v;
    check(actual.at(u,v,s)==value(t,c,z,y,x,s,shape.s),"Scientific facade read a wrong T/C/Z or interleaved sample");
  }
}
void unchanged_data(const desktop::Document &facade,const DocumentSnapshot &before) {
  const auto after=facade.nativeSnapshot();
  check(after.same_document(before) && after.shares_data(before) && after.data_identity()==before.data_identity() &&
        after.revisions().pixels==before.revisions().pixels && after.revisions().metadata==before.revisions().metadata &&
        after.revisions().selection==before.revisions().selection && after.dirty()==before.dirty() &&
        facade.undoSteps()==0 && facade.redoSteps()==0,"Scientific navigation changed source identity/data/history");
  const auto shape=before.data().dataset.shape();
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z)
    check(after.data().dataset.plane({t,c,z})==before.data().dataset.plane({t,c,z}),
          "Scientific facade copied or replaced immutable source planes");
}
void inspect(DatasetShape shape,PixelType type) {
  auto native=owner(shape,type);
  const auto before=native->snapshot();
  desktop::Document facade(native);
  check(facade.nativeOwner()==native && facade.id==native->id,"Facade created a second scientific owner");
  check(facade.shape().dimensions()==shape.dimensions() && facade.scientific()==(shape.t>1 || shape.c>1),
        "Facade scientific shape/capability classification changed");
  const auto x=shape.x-1,y=shape.y-1,z=shape.z-1;
  auto display=facade.view();
  display.crosshair={x,y,z};display.orthogonal=true;display.linkedZoom=false;
  display.zoom=2.5;display.planeZoom={1.25,0,4};display.pan={{{3,-4},{-.5,.25},{0,-2}}};
  facade.setNativeDisplay(display);
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c) {
    // Keep all spatial/camera coordinates fixed while changing scientific axes.
    if(facade.scientific())facade.setView({{"t",t},{"c",c}});
    else {auto view=facade.view();view.t=t;view.c=c;facade.setNativeDisplay(view);}
    const auto view=facade.view();
    const auto json=facade.viewJson();
    const auto position=facade.position();
    check(view.t==t && view.c==c && view.crosshair==std::array<int,3>{x,y,z} &&
          position.t==t && position.c==c && position.z==z && json.at("crosshair")==Json::array({x,y,z}) &&
          (facade.scientific() ? json.at("t")==t && json.at("c")==c : !json.contains("t") && !json.contains("c")),
          "Scientific facade did not expose the selected native T/C/Z position");
    check(view.zoom==2.5 && view.planeZoom==display.planeZoom && view.pan==display.pan &&
          view.orthogonal && !view.linkedZoom,"T/C navigation changed independent cameras");
    check(facade.slice()==z && &facade.frame()==before.data().dataset.plane({t,c,z}).get(),
          "Facade frame flattened T/C or copied its selected immutable XY source");
    check(facade.state().frames.size()==std::size_t(shape.z),"Scientific read state is not the selected Z table");
    for(int zi=0;zi<shape.z;++zi)
      check(facade.state().frames.shared(zi)==before.data().dataset.plane({t,c,zi}),
            "Selected scientific Z table copied or retargeted a plane");
    image(facade.frame(),shape,type,Plane::XY,z,t,c);
    image(facade.plane(Plane::XY,z),shape,type,Plane::XY,z,t,c);
    image(facade.plane(Plane::XZ,y),shape,type,Plane::XZ,y,t,c);
    image(facade.plane(Plane::YZ,x),shape,type,Plane::YZ,x,t,c);
    unchanged_data(facade,before);
  }
  check(facade.spatialCalibration().spacing==std::array<double,3>{.25,1.5,3},
        "Facade changed native voxel spacing");
  // Analytic lengths of this fixture's three physical basis vectors:
  // (-.25,0,0), (.75,1.5,0), (0,.75,3). Shear changes display lengths.
  const std::array<double,3> expectedSpacing{.25,.75*std::sqrt(5.),.75*std::sqrt(17.)};
  const auto displayedSpacing=facade.displaySpacing();
  for(std::size_t axis=0;axis<3;++axis)
    check(std::abs(displayedSpacing[axis]-expectedSpacing[axis])<=1e-14,
          "Facade changed calibrated physical axis lengths");
  check(facade.spatialCalibration().time(shape.t-1)==2.5+.75*(shape.t-1),"Facade changed calibrated time position");
  if(shape.x==7 && shape.y==5 && shape.z==4)
    check(facade.spatialCalibration().voxel_to_world({6,4,3})==std::array<double,3>{-5.5,19.25,-4},
          "Facade changed oriented physical coordinates");
  const auto valid=facade.viewJson();
  for(const auto &invalid:std::vector<Json>{{{"t",-1}},{{"t",shape.t}},{{"c",-1}},{{"c",shape.c}},
                                          {{"t",0},{"c",shape.c},{"zoom",7}}}) {
    rejects([&]{facade.setView(invalid);},"Out-of-range scientific view patch accepted");
    check(facade.viewJson()==valid,"Failed scientific view patch partially changed display");
    unchanged_data(facade,before);
  }
}

std::vector<unsigned char> bytes(const fs::path &path) {
  std::ifstream stream(path,std::ios::binary);
  return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
}
void guarded_mutations_and_native_refresh(const fs::path &directory) {
  auto native=owner({2,3,4,5,7,1},PixelType::UInt16);
  auto facade=std::make_shared<desktop::Document>(native);
  facade->setView({{"t",1},{"c",2},{"crosshair",{6,4,3}},{"zoom",2.5}});
  const auto before=facade->nativeSnapshot();
  const auto view=facade->viewJson();
  const auto unchanged=[&] {
    unchanged_data(*facade,before);
    check(facade->viewJson()==view && facade->revisions().matches(before.revisions(),Change::All),
          "Rejected scientific legacy mutation changed display/revisions");
  };
  rejects([&]{facade->requireLegacyEditable();},"Scientific capability guard admitted legacy editing");unchanged();
  rejects([&]{facade->checkpoint();},"Scientific checkpoint was not rejected");unchanged();
  rejects([&]{facade->compute("add",{{"value",1}},false);},"Scientific legacy compute was not rejected");unchanged();
  auto proposed=facade->state();proposed.metadata["title"]="must not publish";
  rejects([&]{facade->commit(proposed);},"Scientific legacy state commit was not rejected");unchanged();
  auto transaction=native->begin_edit();transaction.set_title("must remain staged");
  rejects([&]{facade->commitNative(transaction);},"Scientific facade native-commit bypass was admitted");unchanged();
  check(transaction.active(),"Rejected facade commit consumed its independent transaction");
  rejects([&]{facade->select({{"kind","rectangle"},{"points",{{1,1},{4,3}}},{"width",1}});},
          "Scientific legacy selection mutation was not rejected");unchanged();
  rejects([&]{facade->undo();},"Scientific facade undo was not rejected");unchanged();
  rejects([&]{facade->redo();},"Scientific facade redo was not rejected");unchanged();
  rejects([&]{facade->measure();},"Scientific legacy measurement was not rejected");unchanged();
  rejects([&]{desktop::runWorkflow(*facade,Json::array({{{"operation","add"},{"parameters",{{"value",1}}}}}));},
          "Scientific legacy replay was not rejected");unchanged();
  rejects([&]{desktop::documentJson(*facade);},"Scientific legacy JSON export silently flattened axes");unchanged();
  const auto destination=directory/"existing-v2.ijx";
  const std::vector<unsigned char> sentinel{0,255,1,2,3};
  {std::ofstream stream(destination,std::ios::binary);stream.write(reinterpret_cast<const char*>(sentinel.data()),sentinel.size());
   check(bool(stream),"Could not stage owned legacy destination sentinel");}
  auto legacy=std::make_shared<desktop::Document>(Stack{Image(7,5,1,PixelType::UInt16)},"legacy");
  rejects([&]{desktop::saveSession(destination.u8string(),{legacy,facade},Json::array(),Json::array());},
          "Mixed scientific legacy session export silently flattened axes");unchanged();
  check(bytes(destination)==sentinel && std::distance(fs::directory_iterator(directory),fs::directory_iterator())==1,
        "Rejected legacy export changed its existing destination or created a temporary");
  const auto modern=directory/"scientific-v3.ijx";
  desktop::saveNativeSession(modern.u8string(),{facade},Json::array({{{"value",7}}}),Json::array());
  check(fs::file_size(modern)<=32ULL*1024*1024,"Scientific navigation archive exceeded its 32 MiB fixture bound");
  Json results=Json::array(),workflow=Json::array(),extensions=Json::object();
  const auto loaded=desktop::loadNativeSession(modern.u8string(),results,workflow,&extensions);
  check(loaded.size()==1 && loaded[0]->shape().dimensions()==std::array<int,6>{2,3,4,5,7,1} &&
        loaded[0]->position().t==1 && loaded[0]->position().c==2 && loaded[0]->position().z==3 &&
        loaded[0]->frame().at(6,4)==12346 && results==Json::array({{{"value",7}}}),
        "Native scientific session reopened with wrong axes/position/samples");
  check(!loaded[0]->dirty() && loaded[0]->undoSteps()==0 && loaded[0]->id!=facade->id,
        "Scientific reopen created dirty history or reused live identity");
  // The facade guard does not restrict the independent public native owner.
  auto edit=native->begin_edit();Image replacement(7,5,1,PixelType::UInt16);
  std::fill(replacement.pixels.begin(),replacement.pixels.end(),999);
  edit.replace_plane({1,2,3},std::move(replacement));edit.set_title("edited through native owner");native->commit(edit);
  check(facade->frame().at(6,4)==999 && facade->plane(Plane::XZ,4).at(6,3)==999 &&
        facade->plane(Plane::YZ,6).at(4,3)==999 && facade->title()=="edited through native owner" &&
        facade->undoSteps()==1 && facade->dirty(),"Facade cache failed to follow direct native publication");
  const auto edited=native->snapshot();
  for(int t=0;t<2;++t)for(int c=0;c<3;++c)for(int z=0;z<4;++z)
    if(t!=1 || c!=2 || z!=3)
      check(edited.data().dataset.plane({t,c,z})==before.data().dataset.plane({t,c,z}),
            "Direct native edit copied an untargeted scientific plane");
  native->undo();
  check(&facade->frame()==before.data().dataset.plane({1,2,3}).get() && facade->frame().at(6,4)==12346 &&
        !facade->dirty() && facade->undoSteps()==0 && facade->redoSteps()==1 && facade->viewJson()==view,
        "Facade did not refresh selected T/C after independent native undo");
}

void positioned_roi_visibility() {
  auto source=owner({2,3,4,5,7,1},PixelType::UInt16);
  DocumentData data=source->snapshot().data();
  const Selection rectangle("rectangle",{{1,1},{4,3}});
  data.rois={Roi{"all",rectangle,{},{},{},{}},Roi{"zero",rectangle,0,0,0,{}},
             Roi{"selected",rectangle,1,2,3,{{"vendor",std::string("kept")}}},
             Roi{"channel",rectangle,{},1,{},{}},Roi{"time-z",rectangle,1,{},3,{}}};
  auto native=std::make_shared<Document>(std::move(data));
  desktop::Document facade(native);
  const auto before=native->snapshot();
  for(const auto &entry:std::vector<std::pair<PlanePosition,std::vector<std::string>>>{
      {{0,0,0},{"all","zero"}},{{1,2,3},{"all","selected","time-z"}},
      {{1,1,3},{"all","channel","time-z"}},{{0,2,3},{"all"}}}) {
    facade.setView({{"t",entry.first.t},{"c",entry.first.c},{"crosshair",{3,2,entry.first.z}}});
    const auto visible=facade.visibleRois();std::vector<std::string> names;
    for(const auto &roi:visible)names.push_back(roi.at("name").get<std::string>());
    check(names==entry.second,"Scientific ROI visibility ignored optional T/C/Z or original order");
    check(facade.state().metadata.at("rois").size()==5 && facade.nativeSnapshot().data().rois.size()==5,
          "ROI visibility filtering discarded manager/native records");
    unchanged_data(facade,before);
  }
  // Explicit T/C position0 is meaningful even on a singleton source.
  DocumentData singleton{Dataset::from_stack({Image(7,5,1,PixelType::UInt16)})};
  singleton.rois={Roi{"explicit-zero",rectangle,0,0,0,{}}};
  auto single=std::make_shared<Document>(std::move(singleton));desktop::Document one(single);
  check(one.scientific() && one.visibleRois().size()==1 && one.viewJson().at("t")==0 && one.viewJson().at("c")==0,
        "Explicit singleton ROI positions were treated as absence");
  rejects([&]{one.requireLegacyEditable();},"Positioned singleton ROI bypassed legacy capability guard");
}
} // namespace

int main(int argc,char **argv) {
  try {
    check(argc==2 || argc==3,"Supply a fresh scientific-navigation artifact directory and optional group");
    const auto directory=fs::absolute(fs::u8path(argv[1]));
    check(!fs::exists(directory),"Scientific-navigation artifact directory must be fresh");
    fs::create_directories(directory);
    const std::string group=argc==3?argv[2]:"all";
    bool matched=false;
    const auto run=[&](const char *name,auto function) {
      if(group=="all" || group==name){matched=true;function();std::cout<<"PASS scientific-navigation."<<name<<'\n';}
    };
    run("scalar",[]{for(auto type:{PixelType::UInt16,PixelType::Float32,PixelType::Float64})inspect({2,3,4,5,7,1},type);});
    run("samples",[]{for(int samples:{3,4})inspect({2,3,4,5,7,samples},PixelType::UInt8);});
    run("singletons",[]{for(auto shape:{DatasetShape{1,3,1,1,7,1},DatasetShape{2,1,4,5,1,1},
                                       DatasetShape{1,1,1,1,1,1}})inspect(shape,PixelType::UInt16);});
    run("guards",[&]{guarded_mutations_and_native_refresh(directory);});
    run("rois",positioned_roi_visibility);
    check(matched,"Unknown scientific-navigation test group");
    std::cout<<"scientific-navigation assertions="<<assertions<<'\n';return 0;
  }catch(const std::exception &error){std::cerr<<"FAIL scientific-navigation: "<<error.what()<<'\n';return 1;}
}
