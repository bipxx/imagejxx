#include <imagejxx/rgb_contrast_session_editor.hpp>
#include <iostream>
#include <limits>
using namespace imagejxx;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>void refuses(F f){bool refused=false;try{f();}catch(const std::invalid_argument &){refused=true;}check(refused,"Expected admission refusal");}
void savedEditing(){
  Image image(2,1,3,PixelType::UInt8);std::fill(image.pixels.begin(),image.pixels.end(),30);
  auto reads=std::make_shared<DatasetReadObserver>();Document owner(Dataset::from_stack({image}).observe_reads(*reads));
  RgbContrastSessionEditor editor(owner.snapshot());editor.set_range(10,40);editor.promote();editor.set_range(0,170);editor.promote();
  const auto original=editor.command();editor.set_saved_stage(0,0,{0,60,4});
  check(editor.plan().visits[0].stages[0].options.channels==4 && original.plan().visits[0].stages[0].options.channels==7,"Saved mapping edit retains independent prior capture");
  editor.move_saved_stage(0,1,0);check(editor.plan().visits[0].stages[0].options.maximum==170,"Saved order changes explicitly");
  editor.remove_saved_stage(0,1);check(editor.plan().visits[0].stages.size()==2 && editor.plan().visits[0].stages.back().options.maximum==255,"Removal preserves live identity draft");
  const auto stable=editor.command().record();refuses([&]{editor.set_saved_stage(0,0,{20,10,7});});refuses([&]{editor.set_saved_stage(0,0,{0,255,0});});
  bool refused=false;try{editor.remove_saved_stage(0,1);}catch(const std::out_of_range &){refused=true;}
  check(refused && metadata_equal(stable,editor.command().record()),"Draft refusal and invalid mapping preserve complete plan");
  check(reads->counts().plane_reads==0 && owner.undo_steps()==0,"Historical editing remains metadata-only and private");
  const auto plane=execute_rgb_contrast_session_command(editor.command()).preview().data().dataset.plane();
  check(plane->at(0,0,0)==45 && plane->at(0,0,1)==45 && plane->at(0,0,2)==45,"Saved edit/delete evaluates exact remaining mapping");
}
void savedSelection(){
  Image image(2,1,3,PixelType::UInt8);std::fill(image.pixels.begin(),image.pixels.end(),30);
  auto reads=std::make_shared<DatasetReadObserver>();Document owner(Dataset::from_stack({image}).observe_reads(*reads));
  RgbContrastSessionEditor editor(owner.snapshot());editor.set_range(10,40);editor.promote();const auto old=editor.command();
  Selection area("rectangle",{{1,0},{2,1}});editor.set_saved_selection(0,0,area);
  check(editor.plan().visits[0].stages[0].selection==area && !old.plan().visits[0].stages[0].selection,"Explicit historical area edit freezes detached geometry");
  const auto stable=editor.command().record();area.points[0][0]=std::numeric_limits<double>::quiet_NaN();
  for(bool whole:{false,true})refuses([&]{editor.set_saved_selection(0,0,area,whole);});
  check(metadata_equal(stable,editor.command().record()) && reads->counts().plane_reads==0,"Invalid geometry, including ignored masks, refuses atomically without IO");
  auto result=execute_rgb_contrast_session_command(editor.command()).preview().data().dataset.plane();
  check(result->at(0,0,0)==30 && result->at(1,0,0)==170,"Saved selection maps only new frozen region");
  editor.set_saved_selection(0,0,editor.plan().visits[0].stages[0].selection,true);
  result=execute_rgb_contrast_session_command(editor.command()).preview().data().dataset.plane();
  check(result->at(0,0,0)==170 && result->at(1,0,0)==170 && owner.undo_steps()==0,"Whole-plane area changes private output without publication");
}
void savedVisits(){
  Image image(2,1,3,PixelType::UInt8);std::fill(image.pixels.begin(),image.pixels.end(),30);
  auto reads=std::make_shared<DatasetReadObserver>();Document owner(Dataset::from_planes({1,1,2,1,2,3},Stack(2,image)).observe_reads(*reads));
  RgbContrastSessionEditor editor(owner.snapshot());const auto draft=editor.plan().visits.back();
  editor.insert_saved_visit(0,{0,0,0},{{{10,70,7},std::nullopt,false}});
  editor.insert_saved_visit(1,{0,0,0},{{{0,170,7},std::nullopt,false}});editor.move_saved_visit(1,0);
  check(editor.plan().visits.back().position.z==draft.position.z && reads->counts().plane_reads==0,"Visit authoring is metadata-only and protects live position");
  auto mapped=execute_rgb_contrast_session_command(editor.command()).preview();check(mapped.data().dataset.plane()->at(0,0,0)==149,"Repeated visits respect explicit history order");
  editor.set_saved_visit_position(0,{0,0,1});editor.remove_saved_visit(1);
  editor.set_range(10,70);editor.promote();editor.transfer_saved_stage(1,0,0,0);
  const auto old=editor.command().record();bool refused=false;try{editor.remove_saved_visit(1);}catch(const std::out_of_range &){refused=true;}
  check(refused && metadata_equal(old,editor.command().record()),"Live visit refusal preserves complete history");
  mapped=execute_rgb_contrast_session_command(editor.command()).preview();
  check(mapped.data().dataset.plane({0,0,0})->at(0,0,0)==30 && mapped.data().dataset.plane({0,0,1})->at(0,0,0)==128 && owner.undo_steps()==0,"Cross-visit transfer keeps mappings and leaves owner private");
}
void run(){
  auto reads=std::make_shared<DatasetReadObserver>();Image image(2,1,3,PixelType::UInt8);
  for(int x=0;x<2;++x){image.at(x,0,0)=20;image.at(x,0,1)=30;image.at(x,0,2)=30;}
  Document owner(Dataset::from_planes({1,1,2,1,2,3},Stack(2,image)).observe_reads(*reads));
  const auto original=owner.snapshot();RgbContrastSessionEditor editor(original);editor.set_channels(4);editor.set_range(10,40);
  check(editor.synchronize(original)==RgbContrastSessionTransition::Unchanged,"Identical source has no transition");
  owner.select(Selection("rectangle",{{0,0},{1,1}}));
  check(editor.synchronize(owner.snapshot())==RgbContrastSessionTransition::Selection,"Selection replaces draft membership");
  editor.set_channels(2);editor.set_range(10,40);
  auto view=owner.snapshot().display();view.crosshair[2]=1;owner.set_display(view);
  check(editor.synchronize(owner.snapshot())==RgbContrastSessionTransition::Visit,"Navigation captures outgoing visit");
  editor.set_range(0,60);view.crosshair[2]=0;owner.set_display(view);editor.synchronize(owner.snapshot());editor.reset();
  check(editor.plan().visits.size()==3 && editor.plan().visits[0].stages.size()==2,"Revisits and component promotions survive Reset");
  auto saved=editor.command();auto copy=editor;editor.set_channels(4);editor.set_range(0,170);editor.set_entire_stack(true);
  check(copy.plan().visits.back().stages.back().options.channels==2 && saved.plan().visits.size()==3,"Copies retain independent immutable intent");
  const auto stable=editor.command().record();
  refuses([&]{editor.set_range(std::numeric_limits<double>::quiet_NaN(),10);});
  refuses([&]{editor.set_channels(0);});auto limits=editor.limits();limits.max_record_bytes=1;
  refuses([&]{editor.set_limits(limits);});check(metadata_equal(stable,editor.command().record()),"Refusal preserves full source and history");
  Document other(original);refuses([&]{editor.synchronize(other.snapshot());});
  auto changed=owner.snapshot().begin_edit(Change::Metadata);auto cal=owner.snapshot().data().dataset.calibration();cal.spacing[0]=2;changed.set_calibration(cal);owner.commit(changed);
  refuses([&]{editor.synchronize(owner.snapshot());});check(metadata_equal(stable,editor.command().record()),"Incompatible source is never silently adopted");
  check(reads->counts().plane_reads==0,"All editing and admission remains sample free");
  const auto history=owner.undo_steps();
  auto preview=execute_rgb_contrast_session_command(editor.preview_command()).preview();
  const auto a=preview.data().dataset.plane({0,0,0}),b=preview.data().dataset.plane({0,0,1});
  check(a->at(0,0,0)==128 && a->at(0,0,1)==170 && b->at(0,0,0)==20 && b->at(0,0,1)==128,"Navigation preview contains historical output without final stack mapping");
  auto result=execute_rgb_contrast_session_command(editor.command()).preview();
  check(result.data().dataset.plane({0,0,1})->at(0,0,0)==30,"Final stack mapping occurs only in Apply command");
  check(original.shares_data(editor.source())==false && original.data().dataset.shares_pixels(editor.source().data().dataset),"Selection source adoption preserves original pixels");
  check(owner.undo_steps()==history,"Private transition state never publishes edits");
}
}
int main(){try{run();savedEditing();savedSelection();savedVisits();std::cout<<"PASS native RGB session transitions, admission and preview/Apply separation\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
