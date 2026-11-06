#include <imagejxx/application.hpp>
#include <imagejxx/roi_manager.hpp>
#include <imagejxx/roi_style.hpp>
#include <iostream>
#include <stdexcept>

using namespace imagejxx;
namespace {
int checks = 0;
void check(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
template<class F> void refuses(F f) {
  bool rejected = false;
  try { f(); } catch (const std::invalid_argument &) { rejected = true; }
  catch (const std::out_of_range &) { rejected = true; }
  catch (const RevisionConflict &) { rejected = true; }
  check(rejected, "Expected an atomic refusal");
}
void publish(Document &owner, PreparedRoiManagerEdit edit) {
  owner.commit(edit.transaction, edit.display);
}
}
int main() {
  try {
    const DatasetShape shape{2, 2, 3, 5, 7, 1};
    Stack planes;
    for (int t=0;t<2;++t) for (int c=0;c<2;++c) for (int z=0;z<3;++z) {
      Image image(7,5,1,PixelType::UInt16);
      for (int y=0;y<5;++y) for (int x=0;x<7;++x) image.at(x,y)=1000*t+100*c+10*z+7*y+x;
      planes.push_back(std::move(image));
    }
    Document owner(Dataset::from_planes(shape, std::move(planes)), "manager");
    const auto original = owner.snapshot();
    owner.select(Selection("rectangle", {{1,1},{5,4}}));
    owner.mark_saved();
    publish(owner, prepare_store_roi(owner.snapshot(), "zero"));
    auto stored = owner.snapshot();
    check(stored.data().rois.size()==1 && stored.data().rois[0].t==0 &&
          stored.data().rois[0].c==0 && stored.data().rois[0].z==0,
          "Store discarded explicit zero positions");
    check(owner.history().undo_steps==1 && stored.dirty(), "Store must be one metadata edit");
    auto view=stored.display();view.t=1;view.c=1;view.crosshair[2]=2;owner.set_display(view);
    owner.select(Selection("ellipse", {{0,0},{2,2}}));
    const auto before=owner.snapshot();
    publish(owner, prepare_select_roi(before,0));
    auto selected=owner.snapshot();
    check(selected.display().t==0 && selected.display().c==0 && selected.display().crosshair[2]==0,
          "Reselect did not restore captured zero T/C/Z");
    auto named_stored=*stored.data().selection;named_stored.attributes["imagej_roi_name"]=std::string("zero");
    check(selected.data().selection==named_stored &&
          selected.data().previous_selection==before.data().selection && owner.history().undo_steps==1,
          "Reselect changed metadata history or lost Previous");
    const auto noop=owner.snapshot();
    publish(owner, prepare_select_roi(noop,0));
    check(owner.snapshot().revisions().matches(noop.revisions(),Change::All), "No-op selection advanced revisions");
    publish(owner, prepare_update_roi(owner.snapshot(),0));
    check(owner.snapshot().revisions().matches(noop.revisions(),Change::All) &&
          owner.snapshot().data().rois==stored.data().rois && owner.history().undo_steps==1,
          "Projected name created a stored attribute or no-op history");
    auto stale=prepare_rename_roi(owner.snapshot(),0,"stale");
    view=owner.snapshot().display();view.t=1;owner.set_display(view);
    const auto after_navigation=owner.snapshot();
    refuses([&]{owner.commit(stale.transaction,stale.display);});
    check(owner.snapshot().shares_data(after_navigation) && owner.history().undo_steps==1,
          "Stale manager edit published");
    publish(owner, prepare_roi_properties(owner.snapshot(),0,"styled",
        {{"stroke_color",std::string("red")},{"fill_color",std::string("#8000ff00")},{"stroke_width",2.5}},
        {},1,2));
    auto properties=owner.snapshot();
    check(!properties.data().rois[0].t && properties.data().rois[0].c==1 && properties.data().rois[0].z==2,
          "Properties lost wildcard/explicit position");
    check(resolve_roi_style(properties.data().rois[0].selection).stroke_width==2.5,
          "Stored ROI style did not reach its native consumer");
    const auto count=owner.history().undo_steps;
    refuses([&]{prepare_roi_properties(owner.snapshot(),0,"bad",{},2,0,0);});
    refuses([&]{prepare_delete_roi(owner.snapshot(),999);});
    check(owner.snapshot().shares_data(properties) && owner.history().undo_steps==count, "Invalid row/position changed state");
    publish(owner, prepare_roi_properties(owner.snapshot(),0,"styled",{}, {},1,2));
    check(owner.snapshot().shares_data(properties) && owner.history().undo_steps==count, "Exact properties no-op created history");
    owner.undo(); owner.redo();
    check(owner.snapshot().data().rois==properties.data().rois, "Undo/Redo lost positions/style");
    owner.select(Selection("polygon", {{1,1},{5,1},{3,4}}));
    publish(owner, prepare_update_roi(owner.snapshot(),0));
    check(owner.snapshot().data().rois[0].selection.kind=="polygon" && !owner.snapshot().data().rois[0].t,
          "Update did not retain saved positions");
    publish(owner, prepare_store_roi(owner.snapshot(),"all Z",true));
    check(!owner.snapshot().data().rois[1].z && owner.snapshot().data().rois[1].t==1,
          "all_slices must wildcard only Z");
    publish(owner, prepare_delete_roi(owner.snapshot(),0));
    check(owner.snapshot().data().rois.size()==1 && owner.snapshot().data().rois[0].name=="all Z", "Delete removed wrong row");
    for(int t=0;t<2;++t)for(int c=0;c<2;++c)for(int z=0;z<3;++z)
      check(owner.snapshot().data().dataset.plane({t,c,z})==original.data().dataset.plane({t,c,z}),
            "ROI manager copied or replaced pixel planes");
    {
      Document named_owner(original.data().dataset, "attribute round trip");
      Selection geometry("rectangle", {{1,1},{5,4}});
      geometry.attributes["imagej_roi_name"]=std::string("original private value");
      geometry.attributes["future"]=std::string("keep");
      named_owner.select(geometry);
      publish(named_owner,prepare_store_roi(named_owner.snapshot(),"row name"));
      publish(named_owner,prepare_select_roi(named_owner.snapshot(),0));
      const auto selected_state=named_owner.snapshot();
      const auto steps=named_owner.history().undo_steps;
      publish(named_owner,prepare_update_roi(selected_state,0));
      check(named_owner.snapshot().revisions().matches(selected_state.revisions(),Change::All) &&
            named_owner.snapshot().data().rois[0].selection==geometry &&
            named_owner.history().undo_steps==steps,
            "Name projection did not restore the original stored private attribute");
      auto edited=*selected_state.data().selection;edited.points[0][0]=2;
      edited.attributes["future"]=std::string("edited");named_owner.select(edited);
      publish(named_owner,prepare_update_roi(named_owner.snapshot(),0));
      auto expected=geometry;expected.points[0][0]=2;
      expected.attributes["future"]=std::string("edited");
      check(named_owner.snapshot().data().rois[0].selection==expected &&
            named_owner.history().undo_steps==steps+1,
            "Update lost edited geometry, unrelated attributes or original name attribute");
    }
    std::cout<<"PASS roi-manager "<<checks<<" assertions\n";
    return 0;
  } catch(const std::exception &error) {
    std::cerr<<"FAIL roi-manager after "<<checks<<": "<<error.what()<<'\n';return 1;
  }
}
