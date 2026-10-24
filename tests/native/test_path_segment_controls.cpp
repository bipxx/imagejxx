#include "path_segment_controls.hpp"
#include "selection_panel.hpp"
#include <iostream>
using namespace imagejxx;
namespace ui=imagejxx::desktop;
namespace {
int checks=0;
void check(bool v,const char *message){if(!v)throw std::runtime_error(message);++checks;}
template<class F>void refuses(F f){try{f();}catch(const std::invalid_argument &){++checks;return;}throw std::runtime_error("Expected path transport refusal");}
}
int main(){try {
  auto path=Selection::from_path({0,1,2,3,3,-4,8,15,11,3,4});
  path.width=3;path.attributes["opaque"]=std::string("kept");
  const ui::Json split{{"operation","split"},{"index",1},{"t",.25}};
  const auto divided=ui::editPathSegments(path,split,{});
  check(divided.path_commands()==split_composite_path(path.path_geometry(),1,.25).commands(),"Transport split changed curve");
  check(divided.width==3 && metadata_equal(divided.attributes,path.attributes),"Transport changed properties");
  for(const ui::Json request:{ui::Json{{"operation","split"},{"index",-1},{"t",.5}},
      {{"operation","remove"},{"index",1},{"command",ui::Json::array()}},
      {{"operation","split"},{"index",true},{"t",.5}},
      {{"operation","split"},{"index",1},{"t",true}},
      {{"operation","insert"},{"index",1},{"command",{1,true,2}}},
      {{"operation","remove"},{"index",0}},
      {{"operation","unknown"},{"index",1}},
      {{"operation","split"},{"index",1},{"t",.5},{"extra",1}}})
    refuses([&]{ui::editPathSegments(path,request,{});});
  DatasetReadObserver reads;
  Document owner(Dataset::from_stack({Image(16,12,1,PixelType::UInt16)}).observe_reads(reads));
  auto tx=owner.begin_edit();tx.select(path);tx.set_previous_selection(path);tx.set_rois({{"saved",path}});owner.commit(tx);
  const auto before=owner.snapshot();
  ui::SelectionPanel panel;panel.show(before);
  panel.patch({{"translation",{.25,.5}}});
  const auto moved=*panel.preview();panel.patch({{"path_edit",split}});
  check(panel.preview()->path_commands()==split_composite_path(moved.path_geometry(),1,.25).commands(),"Staging ignored prior private transform");
  panel.patch({{"path_edit",{{"operation","remove"},{"index",2}}}});
  check(panel.preview()->path_geometry().segment_count()==path.path_geometry().segment_count(),"Repeated staging did not compose");
  check(owner.snapshot().shares_data(before),"Staging published geometry");
  panel.cancel();check(!panel.visible() && owner.snapshot().shares_data(before),"Cancel changed owner");
  panel.show_saved_roi_geometry(before,path,0,{});panel.patch({{"path_edit",split}});
  check(panel.preview()==divided,"Saved draft changed row properties");
  panel.apply([&](const Selection &candidate,const DocumentSnapshot &source){
    check(source.shares_data(before),"Saved staging replaced captured source");
    check(candidate==divided,"Saved Apply lost staged command");
  });
  check(!panel.visible() && owner.snapshot().shares_data(before),"Apply bypassed publisher");
  panel.show(before);ExecutionControl control;control.progress=[&](auto,auto){panel.cancel();};
  try{panel.patch({{"path_edit",split}},control);throw std::runtime_error("Canceled stage reopened");}catch(const OperationCancelled &){++checks;}
  check(!panel.visible(),"Canceled panel revived");
  panel.show(before);bool once=false;
  control.progress=[&](auto,auto){if(!once){once=true;panel.patch({{"translation",{1,0}}});}};
  try{panel.patch({{"path_edit",split}},control);throw std::runtime_error("Reentrant stage replaced newer draft");}catch(const OperationCancelled &){++checks;}
  check(panel.preview()->path_commands()==SelectionEditor(path,16,12).moved({1,0}).path_commands(),"Outer stage overwrote nested draft");
  check(owner.snapshot().shares_data(before) && reads.counts().plane_reads==0 && reads.counts().region_reads==0,"Staging changed/read pixels");
  std::cout<<"PASS "<<checks<<" path segment control checks\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
