#include "desktop.hpp"
#include "window.hpp"
#include <filesystem>
#include <chrono>
#include "path_visual.hpp"
#include "saved_roi_edit.hpp"
#include "../native/session_metadata.hpp"
#include <imagejxx/composite_path.hpp>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
namespace desktop=imagejxx::desktop;
namespace {
int checks=0;
void check(bool value,const char *message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void refuses(F action){bool rejected=false;try{action();}catch(const std::invalid_argument &){rejected=true;}check(rejected,"Missing invalid argument refusal");}
}
int main(){try {
  const std::vector<double> commands{0,1.25,1.125,2,3.25,.25,5.25,1.125,3,6.25,2,6.25,4,5.25,4,1,1.25,4,4};
  auto path=Selection::from_path(commands);path.attributes["opaque"]=std::string("kept");
  DatasetReadObserver reads;
  auto owner=std::make_shared<Document>(Dataset::from_stack({Image(14,10,1,PixelType::UInt16)}).observe_reads(reads));
  auto edit=owner->begin_edit();edit.select(path);edit.set_previous_selection(path);edit.set_rois({{"curve",path}});owner->commit(edit);
  desktop::Document facade(owner);
  const auto before=owner->snapshot();
  check(desktop::selectionFromJson(facade.state().metadata.at("selection"))==path,"State projection lost root path");
  check(desktop::selectionFromJson(facade.state().metadata.at("previous_selection"))==path,"State projection lost previous path");
  check(desktop::selectionFromJson(facade.state().metadata.at("rois")[0].at("selection"))==path,"State projection lost saved path");
  refuses([&]{imagejxx::detail::session_metadata::selection_to_json(path);});
  auto invalid=desktop::selectionJson(path);invalid["ignored"]=true;
  refuses([&]{desktop::selectionFromJson(invalid);});
  check(desktop::selectionControlPointCount(path)==7,"Control count lost native command arity");
  check(desktop::selectionControlPoint(path,1)==std::array<double,2>{3.25,.25},"Wrong quadratic control");
  refuses([&]{desktop::selectionControlPoint(path,7);});
  Application app;app.adopt(owner);
  const auto capture=app.capture_saved_roi_geometry();
  const desktop::SavedRoiCanvasTransform canvas{{10,20},{140,100},{10,10},17,desktop::SavedRoiCanvasKind::OrdinaryXY};
  desktop::SavedRoiEdit draft;
  draft.begin_pointer(capture,0,canvas,{SelectionHandleKind::Vertex,1,{3.25,.25}},{3.25,.25},{});
  draft.update_pointer({3.7500000000000004,.8750000000000001},{},canvas);
  auto expected=commands;expected[4]=3.7500000000000004;expected[5]=.8750000000000001;
  check(draft.candidate()->path_commands()==expected,"Pointer lost exact curve control or topology");
  check(owner->snapshot().shares_data(before),"Private pointer draft changed live owner");
  draft.cancel();check(owner->snapshot().shares_data(before),"Cancel changed root owner");
  draft.begin_pointer(capture,0,canvas,{SelectionHandleKind::Vertex,1,{3.25,.25}},{3.25,.25},{});
  draft.update_pointer({3.75,1.25},{true,false,false},canvas);
  expected=commands;expected[5]=1.25;
  check(draft.candidate()->path_commands()==expected,"Shift-constrained control edit used polygon points");
  draft.cancel();
  auto anisotropic=canvas;anisotropic.scale={10,25};
  draft.begin_pointer(capture,0,anisotropic,{SelectionHandleKind::Vertex,1,{3.25,.25}},{3.25,.25},{});
  draft.update_pointer({3.75,.65},{true,false,false},anisotropic);
  expected=commands;expected[5]=.65;
  check(draft.candidate()->path_commands()==expected,"Shift control edit ignored physical display aspect");
  draft.cancel();
  auto caps=SavedRoiGeometryLimits{};caps.max_working_bytes=1;
  refuses([&]{draft.begin(capture,0,caps);});check(!draft.active(),"Rejected draft retained resources");
  check(owner->snapshot().shares_data(before),"Refusal changed owner");
  desktop::SelectionPanel panel;panel.show(before);
  auto points=desktop::planarControlPoints(path);points[1]={3.8750000000000004,.6250000000000001};
  panel.patch({{"points",points}});expected=commands;expected[4]=points[1][0];expected[5]=points[1][1];
  check(panel.preview()->path_commands()==expected,"Panel lost exact controls");
  check(metadata_equal(panel.preview()->attributes,path.attributes),"Panel lost opaque attributes");
  refuses([&]{panel.patch({{"points",{{1,2}}}});});
  check(owner->snapshot().shares_data(before),"Panel preparation changed live owner");
  check(reads.counts().plane_reads==0 && reads.counts().region_reads==0,"Geometry-only root editing read samples");
  namespace fs=std::filesystem;
  const auto scratch=fs::temp_directory_path()/("imagejxx-root-path-native-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  check(fs::create_directory(scratch),"Could not own root path native scratch");
  {
    desktop::Window window((scratch/"preferences.json").u8string());
    auto legacy=std::make_shared<desktop::Document>(Stack{Image(14,10,1,PixelType::UInt16)});
    auto native=legacy->nativeOwner();auto seed=native->begin_edit();
    auto box=Selection("rectangle",{{1,1},{4,3}});box.attributes=path.attributes;
    seed.set_rois({{"legacy",box}});native->commit(seed);window.addDocument(legacy);
    bool armed=false,rejected=false;
    auto constraint=native->constrain([&](const DocumentData &data,const DisplayState &){
      if(armed && data.rois[0].selection.is_path())throw RevisionConflict();
    });
    armed=true;
    try {window.applySavedRoiGeometry(SavedRoiGeometryRequest(0,path));}catch(const RevisionConflict &){rejected=true;}
    armed=false;constraint.reset();
    check(rejected,"Owner constraint did not refuse typed path publication");
    check(native->snapshot().data().rois[0].selection==box,"Refusal published path geometry");
    refuses([&]{auto invalid=native->begin_edit();invalid.select(path);native->commit(invalid);});
    const auto reply=window.applySavedRoiGeometry(SavedRoiGeometryRequest(0,path));
    check(reply.at("changed").get<bool>() && native->snapshot().data().rois[0].selection==path,"Typed command did not upgrade legacy path admission");
    check(!native->snapshot().data().selection && !native->snapshot().data().previous_selection,"Typed saved command changed other carriers");
  }
  const auto preferences=scratch/"preferences.json";
  if(fs::exists(preferences)){check(fs::is_regular_file(preferences)&&!fs::is_symlink(preferences),"Unexpected native scratch entry");fs::remove(preferences);}
  check(fs::remove(scratch),"Native scratch was not empty");
  std::cout<<"PASS "<<checks<<" desktop root path checks\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
