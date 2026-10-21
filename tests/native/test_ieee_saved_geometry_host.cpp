#include "ieee_saved_geometry_cases.hpp"
#include "saved_roi_edit.hpp"
using namespace imagejxx;
using namespace ieee_saved_geometry;
using namespace imagejxx::desktop;
int main(){try {
  const auto first=rounded_saved_resources::checks;std::size_t cases=0;
  for(int kind=0;kind<9;++kind) {
    Fixture f(kind,FloatingSamplePolicy::PreserveIEEEFloat32);const auto before=f.owner->snapshot();
    SavedRoiEdit edit;edit.begin(f.app.capture_saved_roi_geometry(),0,{});
    const auto next=SelectionEditor(edit.original(),12,10).moved({1,-1},false);edit.stage_numeric(next);
    auto prepared=edit.prepare(f.app,SavedRoiGeometryInteraction::Numeric);
    check(f.app.publish_saved_roi_geometry(prepared).changed,"IEEE private numeric publication failed");edit.cancel();
    check(f.owner->snapshot().data().dataset.shares_pixels(before.data().dataset) && f.owner->snapshot().data().rois[0].selection==next,"IEEE private numeric changed pixels or lost geometry");f.unread();
    Fixture p(kind,FloatingSamplePolicy::PreserveIEEEFloat32);SavedRoiEdit pointer;
    const SavedRoiCanvasTransform transform{{0,0},{120,100},{2,2},9,SavedRoiCanvasKind::OrdinaryXY};
    pointer.begin_pointer(p.app.capture_saved_roi_geometry(),0,transform,{SelectionHandleKind::Move,0,{6,5}},{6,5},{});
    pointer.update_pointer({7,4},{false,false,true},transform);
    check(pointer.candidate() && *pointer.candidate()==next,"IEEE private pointer translation mismatch");
    auto proposal=pointer.prepare(p.app,SavedRoiGeometryInteraction::Pointer);
    check(p.app.publish_saved_roi_geometry(proposal).changed,"IEEE private pointer publication failed");pointer.cancel();p.unread();++cases;
  }
  std::cout<<"ieee_saved_geometry_host: "<<cases<<" cases / "<<rounded_saved_resources::checks-first<<" checks; provider reads zero\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
