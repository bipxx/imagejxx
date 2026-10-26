#include "desktop.hpp"
#include "planar_annotation_panel.hpp"
#include "planar_annotation_visual.hpp"
#include "orthogonal.hpp"
#include "window.hpp"
#include "headless_session_internal.hpp"
#include "roi_import_preview.hpp"
#include "roi_import_panel.hpp"
#include "../../src/native/kernel_control.hpp"
#include <imagejxx/planar_annotations.hpp>
#include <imagejxx/planar_display_annotations.hpp>
#include "display_annotation_renderer.hpp"
#include <imagejxx/imagej_roi_import.hpp>
#include <imgui_internal.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
namespace {
using namespace imagejxx;
void check(bool value,const char *message) {if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F action) {
  try {action();}catch(const std::exception &){return;}
  throw std::runtime_error("Expected restricted publication refusal");
}
void scaled_linked_layout(float scale) {
  auto *context=ImGui::CreateContext();
  struct Context {ImGuiContext *value;~Context(){ImGui::DestroyContext(value);}} guard{context};
  auto &io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
  io.DisplaySize={2560,1600};io.DeltaTime=1.f/60;
  ImGui::StyleColorsLight();auto &style=ImGui::GetStyle();
  style.FramePadding={7,5};style.ScaleAllSizes(scale);
  // The observed Windows C2 failure used a51px font and style scale3.
  ImFontConfig font;font.SizePixels=17*scale;io.Fonts->AddFontDefault(&font);
  unsigned char *pixels=nullptr;int width=0,height=0;
  io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);io.Fonts->SetTexID(1);
  check(pixels && width>0 && height>0,"Scaled layout font atlas unavailable");
  SpatialCalibration calibration;calibration.spacing={.5,.5,.5};
  Stack planes{Image(2,2,1,PixelType::UInt8),Image(2,2,1,PixelType::UInt8)};
  planes[0].pixels={0,128,128,0};planes[1].pixels={128,0,0,128};
  auto owner=std::make_shared<Document>(Dataset::from_planes({1,1,2,2,2,1},std::move(planes),calibration));
  auto display=owner->snapshot().display();display.orthogonal=true;display.zoom=64;
  display.properties["display_range"]=metadata_array({0.,255.});owner->set_display(display);
  desktop::Document facade(owner);desktop::OrthogonalViews views;
  desktop::OrthogonalInputContext input;input.active_document=true;input.input_enabled=true;
  input.keyboard_admitted=true;input.annotation_gui_scale=scale;
  const auto before=owner->snapshot();const auto history=owner->history();
  desktop::Json report,hitFrames=desktop::Json::array();std::string status;int frames=0,wheelFrames=0;
  auto frame=[&] {
    check(++frames<=64,"Scaled linked layout exceeded bounded input frames");
    ImGui::NewFrame();ImGui::SetNextWindowPos({20,20},ImGuiCond_Always);
    // Capture003 had1502x751 available before linked controls. Keep that
    // content area fixed; only the real font/style metrics differ across cases.
    ImGui::SetNextWindowSize({1502+2*style.WindowPadding.x,751+2*style.WindowPadding.y},ImGuiCond_Always);
    ImGui::Begin("Scaled linked layout",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
    views.draw(facade,false,true,false,status,input);
    const std::array<float,2> drawWheel{io.MouseWheelH,io.MouseWheel};
    if(drawWheel[1]!=0)++wheelFrames;
    ImGui::End();ImGui::Render();report=views.diagnostics();
    // Retain only the last eight actual frames, without adding settling/input.
    const auto &path=report[0].at("planar_annotations").at("controls").at("path");
    auto *ownerWindow=ImGui::FindWindowByName(path.at("window_name").get<std::string>().c_str());
    auto windowState=[](const ImGuiWindow *window)->desktop::Json {
      if(!window)return nullptr;
      return {{"name",window->Name},{"id",window->ID},{"active",window->Active},{"hidden",window->Hidden},
          {"last_frame_active",window->LastFrameActive},{"skip_items",window->SkipItems},
          {"position",{window->Pos.x,window->Pos.y}},{"size",{window->Size.x,window->Size.y}},
          {"content_size",{window->ContentSize.x,window->ContentSize.y}},
          {"clip",{window->ClipRect.Min.x,window->ClipRect.Min.y,window->ClipRect.Max.x,window->ClipRect.Max.y}},
          {"inner_clip",{window->InnerClipRect.Min.x,window->InnerClipRect.Min.y,window->InnerClipRect.Max.x,window->InnerClipRect.Max.y}},
          {"outer_clip",{window->OuterRectClipped.Min.x,window->OuterRectClipped.Min.y,window->OuterRectClipped.Max.x,window->OuterRectClipped.Max.y}},
          {"scroll",{window->Scroll.x,window->Scroll.y}},{"scroll_max",{window->ScrollMax.x,window->ScrollMax.y}},
          {"scroll_target",{window->ScrollTarget.x,window->ScrollTarget.y}},
          {"scrollbars",{window->ScrollbarX,window->ScrollbarY}}};
    };
    hitFrames.push_back({{"frame",frames},{"imgui_frame",context->FrameCount},{"rendered_frame",context->FrameCountRendered},
        {"mouse",{io.MousePos.x,io.MousePos.y}},{"mouse_delta",{io.MouseDelta.x,io.MouseDelta.y}},
        {"draw_wheel",drawWheel},{"queued_events",context->InputEventsQueue.Size},{"processed_events",context->InputEventsTrail.Size},
        {"path",path},{"controls",windowState(ownerWindow)},{"hovered_window",windowState(context->HoveredWindow)},
        {"hovered_id",context->HoveredId},{"previous_hovered_id",context->HoveredIdPreviousFrame},
        {"active_id",context->ActiveId},{"hover_disabled",context->HoveredIdIsDisabled},
        {"nav_highlight_item",context->NavHighlightItemUnderNav},
        {"wheeling_window",context->WheelingWindow?desktop::Json(context->WheelingWindow->Name):desktop::Json(nullptr)},
        {"wheeling_scrolled_frame",context->WheelingWindowScrolledFrame},
        {"wheel_remainder",{context->WheelingWindowWheelRemainder.x,context->WheelingWindowWheelRemainder.y}}});
    if(hitFrames.size()>8)hitFrames.erase(hitFrames.begin());
  };
  for(int i=0;i<4;++i)frame();
  auto *parent=ImGui::FindWindowByName("Scaled linked layout");
  check(parent && report.size()==3,"Missing actual linked layout windows");
  auto checkCanvases=[&] {
    for(const auto &plane:report) {
      const auto center=plane.at("canvas_point").get<std::array<float,2>>();
      const auto extent=plane.at("extent").get<std::array<float,2>>();
      const auto origin=plane.at("origin").get<std::array<float,2>>();
      const auto spacing=plane.at("scale").get<std::array<float,2>>();
      check(plane.at("visible").get<bool>() && spacing==std::array<float,2>{32,32},"Scaled linked canvas is not visible at fixed image zoom");
      ImGuiWindow *child=nullptr;
      for(auto *candidate:context->Windows)
        if(candidate->ParentWindow==parent && candidate->Active && !candidate->Hidden &&
           std::string(candidate->Name).find("/plane_")!=std::string::npos &&
           candidate->InnerClipRect.Contains(ImVec2(center[0],center[1]))) {
          check(!child,"Linked canvas matched multiple actual child clips");child=candidate;
        }
      check(child,"Linked canvas center is outside its actual child clip");
      for(int axis=0;axis<2;++axis) {
        const float low=center[axis]-extent[axis]/2,high=center[axis]+extent[axis]/2;
        const float clipLow=axis?child->InnerClipRect.Min.y:child->InnerClipRect.Min.x;
        const float clipHigh=axis?child->InnerClipRect.Max.y:child->InnerClipRect.Max.x;
        if(!(low>=clipLow-1 && high<=clipHigh+1 && origin[axis]>=low+2 && origin[axis]+64<=high-2))
          std::cerr<<"Scaled linked geometry "<<desktop::Json{{"scale",scale},{"plane",plane.at("plane")},
              {"axis",axis},{"canvas",{low,high}},{"clip",{clipLow,clipHigh}},{"image",{origin[axis],origin[axis]+64}}}.dump()<<'\n';
        check(low>=clipLow-1 && high<=clipHigh+1,"Linked canvas exceeds its actual child clip");
        check(origin[axis]>=low+2 && origin[axis]+64<=high-2,"Scaled controls consumed the declared64px linked image canvas");
      }
    }
  };
  checkCanvases();
  ImGuiWindow *controls=nullptr;
  for(auto *candidate:context->Windows)
    if(candidate->ParentWindow==parent && std::string(candidate->Name).find("/orthogonal_controls_")!=std::string::npos)
      controls=candidate;
  check(controls && controls->Active,"Missing actual scrollable linked controls");
  auto pathVisible=[&] {
    const auto &path=report[0].at("planar_annotations").at("controls").at("path");
    const auto rect=path.at("rect").get<std::array<float,4>>();
    return path.at("visible").get<bool>() && rect[0]>=controls->InnerClipRect.Min.x && rect[1]>=controls->InnerClipRect.Min.y &&
        rect[2]<=controls->InnerClipRect.Max.x && rect[3]<=controls->InnerClipRect.Max.y;
  };
  if(scale==3)check(controls->ScrollMax.y>0,"Large-font fixture did not exercise real controls overflow");
  io.AddMousePosEvent(controls->InnerClipRect.GetCenter().x,controls->InnerClipRect.GetCenter().y);frame();
  for(int i=0;i<12 && !pathVisible();++i) {io.AddMouseWheelEvent(0,-2);frame();frame();}
  check(pathVisible(),"Last annotation tool is unreachable through actual controls scrolling");
  if(scale==3)check(wheelFrames>0 && controls->Scroll.y>0,"Large-font Path reachability did not deliver actual wheel input and scroll");
  const auto path=report[0].at("planar_annotations").at("controls").at("path");
  const auto point=path.at("point").get<std::array<float,2>>();
  io.AddMousePosEvent(point[0],point[1]);frame();
  if(!(context->HoveredWindow==controls && context->HoveredId==path.at("id").get<ImGuiID>()))
    std::cerr<<"Scaled Path hit diagnostic "<<desktop::Json{{"scale",scale},{"stale_path",path},
        {"requested_point",point},{"current_path",report[0].at("planar_annotations").at("controls").at("path")},
        {"actual_frames",hitFrames}}.dump()<<'\n';
  check(context->HoveredWindow==controls && context->HoveredId==path.at("id").get<ImGuiID>(),"Actual Path hitpoint does not belong to the displayed control");
  io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();
  for(int i=0;i<3;++i)frame();
  check(report[0].at("planar_annotations").at("mode")==9,"Actual annotation tool click was not accepted");
  checkCanvases();
  const auto after=owner->snapshot();
  check(after.shares_data(before) && after.revisions().matches(before.revisions(),Change::All) && after.dirty()==before.dirty() &&
        owner->history().undo_steps==history.undo_steps && owner->history().redo_steps==history.redo_steps,
        "Layout or control scrolling changed source, display revisions or history");
  std::cout<<"PASS real ImGui linked layout scale="<<scale<<" frames="<<frames<<" (no GL)\n";
}
void small_workspace_layout() {
  namespace fs=std::filesystem;
  const auto root=fs::temp_directory_path()/std::string("imagejxx-small-workspace-"+
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  check(fs::create_directory(root),"Fresh workspace fixture directory required");
  struct Scratch {fs::path root;~Scratch(){std::error_code error;if(fs::is_empty(root,error) && !error)fs::remove(root,error);}} scratch{root};
  desktop::Window window((root/"preferences.json").u8string());
  window.updateSettings({{"ui_scale",3.},{"auto_dpi",false}},false);
  SpatialCalibration calibration;calibration.spacing={.5,.5,.5};
  auto step=std::make_shared<Document>(Dataset::from_stack({Image(2,4,1,PixelType::UInt8)}),"Step");
  window.addNativeDocument(step);
  Stack images{Image(2,2,1,PixelType::UInt8),Image(2,2,1,PixelType::UInt8)};
  images[0].pixels={0,128,128,0};images[1].pixels={128,0,0,128};
  auto volume=std::make_shared<Document>(Dataset::from_planes({1,1,2,2,2,1},std::move(images),calibration),"Volume");
  auto display=volume->snapshot().display();display.orthogonal=true;display.zoom=64;
  display.properties["display_range"]=metadata_array({0.,255.});volume->set_display(display);
  window.addNativeDocument(volume);const auto before=volume->snapshot();
  desktop::HeadlessInputSession session(window);
  auto &io=ImGui::GetIO();io.DisplaySize={1028,749};
  // Match the captured51px font height while retaining headless ownership.
  // This uses a default font, not the Windows Segoe glyphs or a GL renderer.
  io.Fonts->Clear();ImFontConfig config;config.SizePixels=51;
  io.Fonts->AddFontDefault(&config);unsigned char *pixels=nullptr;int width=0,height=0;
  io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);io.Fonts->SetTexID(1);
  desktop::Json report;for(int i=0;i<8;++i)report=session.frame();
  auto *parent=ImGui::FindWindowByName("Images");check(parent,"Images workspace missing");
  check(ImGui::GetFontSize()==51 && ImGui::GetStyle().FramePadding.x==21 && ImGui::GetStyle().FramePadding.y==15,"Captured small-workspace metrics differ");
  for(auto *child:ImGui::GetCurrentContext()->Windows)
    if(child==parent || (child->ParentWindow==parent && std::string(child->Name).find("orthogonal_controls")!=std::string::npos))
      std::cerr<<"Workspace window "<<desktop::Json{{"name",child->Name},{"position",{child->Pos.x,child->Pos.y}},
        {"size",{child->Size.x,child->Size.y}},{"content_rect",{child->ContentRegionRect.Min.x,child->ContentRegionRect.Min.y,child->ContentRegionRect.Max.x,child->ContentRegionRect.Max.y}},
        {"clip",{child->InnerClipRect.Min.x,child->InnerClipRect.Min.y,child->InnerClipRect.Max.x,child->InnerClipRect.Max.y}},
        {"scroll",{child->Scroll.x,child->Scroll.y}},{"scroll_max",{child->ScrollMax.x,child->ScrollMax.y}}}.dump()<<'\n';
  for(const auto &plane:report.at("orthogonal_planes")) {
    ImGuiWindow *child=nullptr;
    for(auto *candidate:ImGui::GetCurrentContext()->Windows)
      if(candidate->ParentWindow==parent && candidate->GetID("image")==plane.at("canvas_id").get<ImGuiID>())child=candidate;
    check(child,"Missing actual linked plane child");
    const auto center=plane.at("canvas_point").get<std::array<float,2>>();
    const auto extent=plane.at("extent").get<std::array<float,2>>();
    const auto origin=plane.at("origin").get<std::array<float,2>>();
    const ImRect canvas{{center[0]-extent[0]/2,center[1]-extent[1]/2},
                        {center[0]+extent[0]/2,center[1]+extent[1]/2}};
    std::cerr<<"Small workspace "<<desktop::Json{{"plane",plane.at("plane")},{"visible",plane.at("visible")},
      {"parent_clip",{parent->InnerClipRect.Min.x,parent->InnerClipRect.Min.y,parent->InnerClipRect.Max.x,parent->InnerClipRect.Max.y}},
      {"child_clip",{child->InnerClipRect.Min.x,child->InnerClipRect.Min.y,child->InnerClipRect.Max.x,child->InnerClipRect.Max.y}},
      {"canvas",{canvas.Min.x,canvas.Min.y,canvas.Max.x,canvas.Max.y}},{"origin",origin},{"extent",extent}}.dump()<<'\n';
    check(plane.at("visible").get<bool>() && child->InnerClipRect.Contains(canvas),"Small workspace clips linked canvas");
    check(plane.at("scale")==desktop::Json::array({32.,32.}) && origin[0]>=canvas.Min.x+2 && origin[1]>=canvas.Min.y+2 &&
        origin[0]+64<=canvas.Max.x-2 && origin[1]+64<=canvas.Max.y-2,"Small workspace lost fixed64px source or background margin");
  }
  const auto after=volume->snapshot();
  check(after.shares_data(before) && after.revisions().matches(before.revisions(),Change::All) &&
        after.dirty()==before.dirty() && volume->undo_steps()==0 && volume->redo_steps()==0,
        "Workspace layout changed source/view/history");
  session.close();std::cout<<"PASS actual docked1028x749/gui3 workspace geometry (no GL)\n";
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
  try {run();scaled_linked_layout(1);scaled_linked_layout(3);small_workspace_layout();std::cout<<"PASS restricted planar annotation desktop publication\n";return 0;}
  catch(const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
