#include "desktop.hpp"
#include "planar_annotation_panel.hpp"
#include "planar_annotation_visual.hpp"
#include "roi_import_preview.hpp"
#include "roi_import_panel.hpp"
#include "../../src/native/kernel_control.hpp"
#include <imagejxx/planar_annotations.hpp>
#include <imagejxx/planar_display_annotations.hpp>
#include "display_annotation_renderer.hpp"
#include <imagejxx/imagej_roi_import.hpp>
#include <iostream>
#include <stdexcept>
namespace {
using namespace imagejxx;
void check(bool value,const char *message) {if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F action) {
  try {action();}catch(const std::exception &){return;}
  throw std::runtime_error("Expected restricted publication refusal");
}
void run() {
  Stack planes;for(int i=0;i<12;++i)planes.emplace_back(7,5,1,PixelType::UInt16);
  auto owner=std::make_shared<Document>(Dataset::from_planes({2,2,3,5,7,1},std::move(planes)));
  desktop::Document facade(owner);
  const auto source=owner->snapshot();
  PlanarAnnotation row{"XZ",Selection("rectangle",{{1,0},{4,2}}),Plane::XZ,0,1,1,{}};
  auto edit=prepare_store_planar_annotation(source,row);facade.commitPlanarAnnotation(edit);
  check(planar_annotations(owner->snapshot().data()).size()==1 && owner->undo_steps()==1,"Scientific annotation did not publish");
  check(owner->snapshot().data().dataset.shares_pixels(source.data().dataset),"Annotation changed pixel storage");
  const auto before=owner->snapshot();
  auto other_metadata=owner->begin_edit();other_metadata.set_property("forbidden",true);
  rejects([&]{facade.commitPlanarAnnotation(other_metadata);});
  auto pixels=owner->begin_edit();pixels.replace_plane({1,1,0},Image(7,5,1,PixelType::UInt16));
  rejects([&]{facade.commitPlanarAnnotation(pixels);});
  auto selection=owner->begin_edit();selection.select(Selection("rectangle",{{1,1},{3,3}}));
  rejects([&]{facade.commitPlanarAnnotation(selection);});
  check(owner->snapshot().shares_data(before) && owner->undo_steps()==1,"Refusal mutated source");
  auto display=owner->begin_edit();auto view=owner->snapshot().display();view.crosshair[0]=2;
  display.set_axis_remap_display(view);
  rejects([&]{facade.commitPlanarAnnotation(display);});
  desktop::PlanarAnnotationPanel panel;
  // The UI wrapper must refuse absent contexts before using ImGui. Admission
  // itself is a native metadata-only check, independent of renderer setup.
  rejects([&]{panel.controls(facade,true,1);});
  rejects([&]{desktop::admit_planar_annotation_visuals(before.data().dataset.shape(),planar_annotations(before.data()),1);});
  check(!panel.active() && owner->snapshot().shares_data(before),"Resource refusal changed annotations or retained a draft");
  auto weak=owner->begin_edit(Change::Metadata);weak.remove_extension(planar_annotations_extension);
  rejects([&]{facade.commitPlanarAnnotation(weak);});
  auto pending=prepare_delete_planar_annotation(owner->snapshot(),0);
  auto changed=owner->begin_edit();changed.set_title("concurrent");owner->commit(changed);
  rejects([&]{facade.commitPlanarAnnotation(pending);});
  check(planar_annotations(owner->snapshot().data()).size()==1,"Stale publication removed row");
  ImageJRoiPlaneMapping mapping;mapping.extent={2,2,3,5,7,1};mapping.coordinate=0;
  ImageJRoi record;record.roi={"import",Selection("rectangle",{{1,1},{3,3}}),{},{},{},{}};
  const std::vector<ImageJRoiImportItem> items{{"import.roi",encode_imagej_roi(record),mapping}};
  const auto command=resolve_imagej_roi_import(owner->snapshot(),items);
  {
    auto scheduler=std::make_shared<JobScheduler>();desktop::RoiImportPanel panel(scheduler);
    const auto original=owner->snapshot();panel.show(original);const auto choice=panel.browse_request();
    check(panel.browse_current(choice,original),"Fresh browse request was not current");
    panel.path(choice,original,"chosen.roi");
    check(!panel.browse_current(choice,original),"Accepted file choice remained reusable");
    rejects([&]{panel.path(choice,original,"stale.roi");});
    const auto later=panel.browse_request();panel.cancel("test");panel.show(original);
    check(!panel.browse_current(later,original),"Reopened draft accepted a prior choice");
    auto other=std::make_shared<Document>(original.data().dataset);
    check(!panel.browse_current(panel.browse_request(),other->snapshot()),"Another document accepted a file choice");
    panel.show(other->snapshot());const auto metadata_choice=panel.browse_request();
    auto changed_owner=other->begin_edit();changed_owner.set_title("browse concurrency");other->commit(changed_owner);
    check(!panel.browse_current(metadata_choice,other->snapshot()),"Metadata changes kept the file choice current");
    panel.cancel("test");
    panel.show_record(owner->snapshot(),command.record());
    rejects([&]{panel.browse_request();});panel.wait();panel.poll();
    rejects([&]{panel.browse_request();});
    panel.cancel("test");
  }
  const auto preview_source=owner->snapshot();
  desktop::RoiImportPreview preview(command,256*1048576);
  check(preview.matches(preview_source,256*1048576) && !preview.replaces(),"Preview did not retain exact source/cap binding");
  check(!preview.matches(preview_source,1),"Preview admitted changed resource cap");
  std::size_t checkpoints=0;bool canceled=false;
  auto token=std::make_shared<CancellationToken>();ExecutionControl control;control.cancellation=token;
  control.checkpoint=[&]{if(++checkpoints==3)token->cancel();};
  {ExecutionScope scope(control);try {desktop::RoiImportPreview interrupted(command,256*1048576);}
   catch(const OperationCancelled &){canceled=true;}}
  check(canceled && checkpoints==3 && owner->snapshot().shares_data(preview_source),"Image-space preview cancellation did not preserve source");
  auto wrong_route=prepare_imagej_roi_import(command);
  rejects([&]{facade.commitPlanarAnnotation(wrong_route);});
  const auto import_before=owner->snapshot();
  facade.commitImageJRoiImport(command);
  check(!preview.matches(owner->snapshot(),256*1048576),"Preview survived metadata publication");
  check(planar_annotations(owner->snapshot().data()).size()==2 &&
      owner->snapshot().data().metadata.extensions.count(imagej_roi_import_sources_extension),"Import opt-in did not publish complete state");
  check(owner->snapshot().data().dataset.shares_pixels(import_before.data().dataset),"Import opt-in changed pixels");
  rejects([&]{facade.commitImageJRoiImport(command);});
  owner->undo();check(owner->snapshot().shares_data(import_before),"Import opt-in undo differs");
  TextAnnotationPayload text;text.text="Display only";
  PlanarDisplayAnnotation label{"label",text,Plane::XY,0,0,0,{}};
  const auto display_source=owner->snapshot();
  auto display_edit=prepare_store_planar_display_annotation(display_source,label);
  rejects([&]{facade.commitPlanarAnnotation(display_edit);});
  facade.commitDisplayAnnotation(display_edit);
  check(planar_display_annotations(owner->snapshot().data()).size()==1,"Display annotation opt-in did not publish");
  check(owner->snapshot().data().dataset.shares_pixels(display_source.data().dataset),"Display annotation opt-in changed pixels");
  auto title=owner->begin_edit();title.set_title("forbidden");rejects([&]{facade.commitDisplayAnnotation(title);});
  auto wrong_extension=prepare_store_planar_annotation(owner->snapshot(),row);rejects([&]{facade.commitDisplayAnnotation(wrong_extension);});
  rejects([&]{desktop::admit_display_annotation_visuals({label},1);});
  owner->undo();check(owner->snapshot().shares_data(display_source),"Display annotation undo differs");
  ImageJTextRoi wire_text;wire_text.text="Imported display";wire_text.stroke_color=0xff223344;
  ImageJDisplayRoiMapping display_mapping;display_mapping.plane=mapping;
  const ImageJDisplayRoiImportItem display_item{"display.roi",encode_imagej_text_roi(wire_text),display_mapping,{}};
  const auto mixed_source=owner->snapshot();
  const auto mixed=resolve_imagej_annotation_import(mixed_source,{items[0],display_item});
  auto mixed_wrong_route=prepare_imagej_annotation_import(mixed);
  rejects([&]{facade.commitPlanarAnnotation(mixed_wrong_route);});
  rejects([&]{facade.commitDisplayAnnotation(mixed_wrong_route);});
  desktop::RoiImportPreview mixed_preview(mixed,256*1048576);
  check(mixed_preview.matches(mixed_source,256*1048576) && bool(mixed_preview.display_preview(mixed_source)),"Mixed preview lacks exact source binding");
  facade.commitImageJAnnotationImport(mixed);
  check(planar_display_annotations(owner->snapshot().data()).size()==1 && planar_annotations(owner->snapshot().data()).size()==2,"Mixed opt-in did not publish both row sets");
  check(owner->snapshot().data().dataset.shares_pixels(mixed_source.data().dataset),"Mixed opt-in changed pixels");
  rejects([&]{facade.commitImageJAnnotationImport(mixed);});
  check(!mixed_preview.display_preview(owner->snapshot()),"Stale mixed preview remained admitted");
  owner->undo();check(owner->snapshot().shares_data(mixed_source),"Mixed opt-in undo differs");
}
}
int main() {
  try {run();std::cout<<"PASS restricted planar annotation desktop publication\n";return 0;}
  catch(const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
