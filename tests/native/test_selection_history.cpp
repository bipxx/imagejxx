#include <imagejxx/document.hpp>
#include <imagejxx/composite_path.hpp>
#include <iostream>
using namespace imagejxx;
namespace {int checks=0;void check(bool v,const char *message){if(!v)throw std::runtime_error(message);++checks;}}
int main(){try {
  DatasetReadObserver reads;
  Document owner(Dataset::from_stack({Image(8,6,1,PixelType::UInt8)}).observe_reads(reads));
  auto path=Selection::from_path({0,1,1,2,3,5,6,1,4});
  owner.select(path);const auto before=owner.snapshot();
  check(owner.undo_steps()==0 && !before.dirty(),"Transient selection unexpectedly created history");
  auto edit=before.begin_edit();edit.select(Selection::from_path(path.path_geometry().transformed({1,0,1,0,1,0})));edit.retain_selection_history();
  check(edit.selection_history(),"Selection history intent lost");owner.commit(edit);
  const auto after=owner.snapshot();
  check(owner.undo_steps()==1 && after.dirty(),"Authored selection did not create one dirty history entry");
  check(after.data().previous_selection==before.data().selection,"Authored selection lost Previous");
  check(after.data().metadata==before.data().metadata,"Selection history invented metadata");
  check(after.data().dataset.shares_pixels(before.data().dataset),"Selection history replaced pixels");
  owner.undo();check(owner.snapshot().shares_data(before) && !owner.snapshot().dirty(),"Selection Undo lost captured state");
  owner.redo();check(owner.snapshot().shares_data(after),"Selection Redo lost authored state");
  auto noop=owner.snapshot().begin_edit();noop.retain_selection_history();owner.commit(noop);
  check(owner.undo_steps()==1 && owner.snapshot().shares_data(after),"No-op authored transaction added history");
  auto canceled=owner.snapshot().begin_edit();canceled.retain_selection_history();canceled.rollback();
  bool refused=false;try{canceled.retain_selection_history();}catch(const std::exception &){refused=true;}
  check(refused,"Inactive transaction accepted history intent");
  auto stale=owner.snapshot().begin_edit();stale.retain_selection_history();stale.select(path);
  owner.select(Selection("rectangle",{{0,0},{2,2}}));const auto current=owner.snapshot();
  refused=false;try{owner.commit(stale);}catch(const RevisionConflict &){refused=true;}
  check(refused && owner.snapshot().shares_data(current) && owner.undo_steps()==1,"Stale history transaction partially published");
  owner.set_history_limit(1);auto latest=owner.snapshot().begin_edit();latest.select(path);latest.retain_selection_history();owner.commit(latest);
  check(owner.undo_steps()==1,"Authored selection ignored history limit");
  owner.undo();check(owner.snapshot().shares_data(current),"History pruning discarded immediate authoring source");
  check(reads.counts().plane_reads==0 && reads.counts().region_reads==0,"Selection history read image samples");
  std::cout<<"PASS "<<checks<<" selection history checks\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
