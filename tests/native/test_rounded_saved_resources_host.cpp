#include "rounded_saved_resource_cases.hpp"
#include "saved_roi_edit.hpp"
using namespace rounded_saved_resources;
using namespace imagejxx::desktop;
using Interaction=SavedRoiGeometryInteraction;
int main(){try {
  for(bool rotated:{false,true})for(int diameter:{0,6,std::numeric_limits<int>::max()}) {
    Fixture f(rotated,diameter);const auto before=f.owner->snapshot();
    const auto original=before.data().rois[0].selection;
    const auto capture=f.app.capture_saved_roi_geometry();SavedRoiEdit edit;
    std::size_t callbacks=0;ExecutionControl control;control.checkpoint=[&]{++callbacks;};
    const auto estimate=estimate_saved_roi_geometry(before,SavedRoiGeometryRequest(0,original));
    for(int boundary=0;boundary<5;++boundary) {
      SavedRoiGeometryLimits low;
      if(boundary==0)low.max_xy_pixels=119;
      if(boundary==1)low.max_vertices=1;
      if(boundary==2)low.max_working_bytes=estimate.working_bytes-1;
      if(boundary==3)low.max_record_bytes=estimate.record_wire_bytes-1;
      if(boundary==4)low.metadata_depth=1;
      callbacks=0;rejects([&]{edit.begin(capture,0,low,control);},"Rounded private draft cap refusal missing");
      check(callbacks==0 && !edit.active() && edit.retained_count()==0 && edit.retained_working_bytes()==0,
            "Rounded private cap dispatched callback or retained state");f.unread();
    }
    edit.begin(capture,0,{},control);check(edit.active() && callbacks>0,"Rounded admitted draft missing callbacks");
    const auto frame=*original.box_frame();
    check(bool(edit.hit_test(frame.center,{.0001,.0001})),"Rounded center misses analytic hit test");
    check(bool(edit.hit_test(frame.image_point({-.99,-.99}),{.0001,.0001}))==(diameter==0),
          "Rounded corner analytic membership differs");
    auto invalid=original;invalid.attributes["corner_diameter"]=2.5;callbacks=0;
    rejects([&]{edit.stage_numeric(invalid,control);},"Rounded fractional diameter staged");
    check(edit.active() && !edit.candidate() && callbacks==0,"Invalid rounded numeric stage retained candidate or dispatched callback");
    auto changed=replacement(original);const auto expected=changed;
    control.checkpoint=[&]{changed.attributes["corner_diameter"]=false;};
    edit.stage_numeric(changed,control);
    check(edit.candidate() && *edit.candidate()==expected,"Rounded numeric stage failed to freeze caller geometry");
    auto prepared=edit.prepare(f.app,Interaction::Numeric);edit.cancel();
    rejects<OperationCancelled>([&]{f.app.publish_saved_roi_geometry(prepared);},"Canceled rounded draft published");
    check(!prepared.active() && prepared.retained_working_bytes()==0 && f.owner->snapshot().shares_data(before),
          "Canceled rounded numeric preparation retained resources or changed source");
    f.unread();++cases;

    const SavedRoiCanvasTransform transform{{17,11},{120,100},{3,2},41,SavedRoiCanvasKind::OrdinaryXY};
    for(int action=0;action<3;++action) {
      Fixture p(rotated,diameter);SavedRoiEdit pointer;const auto old=p.owner->snapshot();
      const auto base=old.data().rois[0].selection;
      SelectionHandle hit{action==0?SelectionHandleKind::Move:action==1?SelectionHandleKind::Resize:SelectionHandleKind::Rotate,
                          std::size_t(action==1?3:0),{6,5}};
      const auto start=action==2?std::array<double,2>{10,5}:std::array<double,2>{6,5};
      const auto end=action==2?std::array<double,2>{6,9}:action==1?std::array<double,2>{11,5}:std::array<double,2>{7,4};
      pointer.begin_pointer(p.app.capture_saved_roi_geometry(),0,transform,hit,start,{});
      check(pointer.pointer_active() && !pointer.candidate(),"Undelivered rounded pointer already publishable");
      pointer.update_pointer(end,{false,false,true},transform);
      check(pointer.candidate() && pointer.candidate()->is_rounded_box() && pointer.candidate()->corner_diameter()==diameter,
            "Rounded pointer geometry/diameter lost");
      const auto candidate=*pointer.candidate();
      auto proposal=pointer.prepare(p.app,Interaction::Pointer);const auto result=p.app.publish_saved_roi_geometry(proposal);pointer.cancel();
      check(result.changed && p.owner->snapshot().data().rois[0].selection==candidate && p.owner->undo_steps()==1,
            "Rounded pointer metadata publication failed");
      check(!proposal.active() && proposal.retained_working_bytes()==0 && pointer.retained_count()==0 &&
            p.owner->snapshot().data().dataset.shares_pixels(old.data().dataset),"Rounded pointer retained ownership or changed pixels");
      p.owner->undo();check(p.owner->snapshot().data().rois[0].selection==base,"Rounded pointer Undo lost original");
      p.owner->redo();check(p.owner->snapshot().data().rois[0].selection==candidate,"Rounded pointer Redo lost result");p.unread();++cases;
    }
    Fixture stale(rotated,diameter);SavedRoiEdit pointer;
    pointer.begin_pointer(stale.app.capture_saved_roi_geometry(),0,transform,{SelectionHandleKind::Move,0,{6,5}},{6,5},{});
    pointer.update_pointer({7,4},{},transform);auto view=stale.owner->snapshot().display();view.pan[0][0]+=1;stale.owner->set_display(view);
    rejects<RevisionConflict>([&]{pointer.prepare(stale.app,Interaction::Pointer);},"Rounded pointer accepted changed camera");
    check(!pointer.active() && pointer.retained_working_bytes()==0,"Stale rounded pointer retained draft");stale.unread();
    Fixture numeric(rotated,diameter);SavedRoiEdit number;
    number.begin(numeric.app.capture_saved_roi_geometry(),0,{});number.stage_numeric(replacement(number.original()));
    view=numeric.owner->snapshot().display();view.pan[0][0]+=1;numeric.owner->set_display(view);
    auto refreshed=number.prepare(numeric.app,Interaction::Numeric);
    check(numeric.app.publish_saved_roi_geometry(refreshed).changed,"Rounded numeric edit rejected camera-only refresh");number.cancel();numeric.unread();
    Fixture abort(rotated,diameter);SavedRoiEdit reentrant;
    reentrant.begin(abort.app.capture_saved_roi_geometry(),0,{});ExecutionControl stop;
    stop.checkpoint=[&]{reentrant.cancel();};
    rejects<OperationCancelled>([&]{reentrant.stage_numeric(replacement(abort.owner->snapshot().data().rois[0].selection),stop);},
                               "Rounded callback cancellation returned a candidate");
    check(!reentrant.active() && reentrant.retained_working_bytes()==0 && abort.owner->undo_steps()==0,
          "Reentrant rounded cancellation retained ownership or published");abort.unread();++cases;
  }
  std::cout<<"rounded_saved_host_resources: "<<cases<<" cases / "<<checks<<" checks; provider reads zero\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
