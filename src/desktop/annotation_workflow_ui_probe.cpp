#include "planar_path_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "path_visual.hpp"
#include "window.hpp"
#include <imagejxx/composite_path.hpp>
#include <imagejxx/native_workflow_program.hpp>
#include <chrono>
#include <thread>

namespace imagejxx::desktop {
namespace {
void require(bool value,const char *message) {if(!value)throw std::runtime_error(message);}
Json observe(Window &window) {
  const auto source=window.nativeSnapshot();Json rows=Json::array(),pixels=Json::array(),saved=Json::array();
  for(const auto &row:planar_annotations(source.data()))rows.push_back({{"name",row.name},{"plane",int(row.plane)},
      {"coordinate",row.coordinate},{"selection",planarSelectionJson(row.selection)},{"attributes",metadataJson(row.attributes)}});
  for(int z=0;z<source.data().dataset.shape().z;++z)pixels.push_back(source.data().dataset.plane({0,0,z})->pixels);
  for(const auto &row:source.data().rois)saved.push_back({{"name",row.name},{"selection",planarSelectionJson(row.selection)},{"attributes",metadataJson(row.attributes)}});
  return {{"rows",rows},{"saved_rows",saved},{"pixels",pixels},{"undo",window.nativeDocument(source.id())->undo_steps()},
      {"workflow",window.workflow}};
}
}
Json annotationWorkflowUiCase(Window &window,const std::string &context,const std::string &fresh_directory) {
  namespace fs=std::filesystem;
  const auto directory=fs::absolute(fs::u8path(fresh_directory));
  require(!fs::exists(directory) && window.documents().empty(),"Fresh annotation workflow case required");
  fs::create_directories(directory);
  const DatasetShape shape{1,1,6,10,14,1};Stack planes;
  for(int z=0;z<shape.z;++z){Image image(14,10,1,PixelType::UInt16);
    for(int y=0;y<10;++y)for(int x=0;x<14;++x)image.at(x,y)=100*z+10*y+x;
    planes.push_back(std::move(image));}
  auto path=with_roi_style(Selection::from_path({0,1.25,1.125,2,3.25,.25,5.25,1.125,3,6.25,2,6.25,4,5.25,4,1,1.25,4,4}),
      {{"stroke_color",std::string("#FF22CCEE")},{"fill_color",std::string("#804488CC")},{"stroke_width",1.}});
  path.attributes["opaque"]=std::string("kept");
  DocumentData data{Dataset::from_planes(shape,std::move(planes))};
  std::vector<PlanarAnnotation> rows;
  for(auto plane:{Plane::XY,Plane::XZ,Plane::YZ})rows.push_back({"curve",path,plane,0,0,0,{{"row",std::string("kept")}}});
  data.metadata.extensions[planar_annotations_extension]=encode_planar_annotations(shape,rows);
  auto saved_geometry=Selection("rectangle",{{1,1},{4,3}});saved_geometry.attributes=path.attributes;
  data.rois={{"saved",saved_geometry,{},{},{},{{"opaque",std::string("saved row")}}}};
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));
  auto display=owner->snapshot().display();display.orthogonal=true;display.crosshair={0,0,0};owner->set_display(display);
  window.addNativeDocument(owner);window.recording=false;
  const auto capture=resolve_planar_annotation_command(owner->snapshot(),{PlanarAnnotationOperation::Update,1,rows[1]});
  NativeWorkflowOperation saved_step;saved_step.operation="saved_roi_geometry";saved_step.target.roi=RoiPolicy::WholePlane;saved_step.enabled=false;
  saved_step.original=resolve_saved_roi_geometry(owner->snapshot(),SavedRoiGeometryRequest{0,path}).record();
  window.workflow=Json::array({metadataJson(capture.record()),metadataJson(make_native_workflow_program({saved_step}).record())});
  Json report{{"contract","p13d.annotation-workflow.ui.001"},{"context",context},{"original",metadataJson(capture.record())},
      {"frames",Json::array()},{"controls",Json::array()},{"states",{{"before",observe(window)}}}};
  HeadlessInputSession session(window);Json last;
  auto frame=[&](const Json &events=Json::array()) {
    require(report["frames"].size()<360,"Annotation workflow frame bound");last=session.frame(events);
    require(last.at("input_frame").at("raw_inputs")==events,"Annotation workflow input mismatch");
    report["frames"].push_back({{"events",events},{"rendered",last}});
  };
  auto control=[&](const std::string &name)->Json {
    if(name.rfind("parameters.",0)==0)return last.at("native_workflow").at("editor").at("controls").at(name.substr(11));
    if(name.rfind("program.",0)==0)return last.at("native_workflow").at("controls").at(name.substr(8));
    return last.at("host_controls").at(name);
  };
  auto click=[&](const std::string &name) {
    Json hit;
    for(int retry=0;retry<25;++retry) {
      hit=control(name);if(!hit.at("enabled").get<bool>()) {writeJson((directory/"failure.json").u8string(),last);throw std::runtime_error("Annotation workflow control disabled: "+name);}
      if(name.rfind("parameters.",0)!=0 && name.rfind("program.",0)!=0)break;
      const auto bounds=name.rfind("parameters.",0)==0?last.at("native_workflow").at("editor").at("window"):last.at("native_workflow").at("window");
      const auto y=hit.at("point")[1].get<double>();
      if(y>=bounds[1].get<double>() && y<=bounds[3].get<double>() && hit.value("visible",true))break;
      require(retry<24,"Annotation workflow control outside viewport");
      frame(Json::array({{{"type","position"},{"space","screen"},{"point",{bounds[2].get<double>()-14,(bounds[1].get<double>()+bounds[3].get<double>())/2}}}}));
      frame(Json::array({{{"type","wheel"},{"x",0},{"y",y>bounds[3].get<double>()?-4:4}}}));
    }
    report["controls"].push_back({{"name",name},{"control",hit}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",hit.at("point")}}}));
    for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));
    frame();
  };
  auto text=[&](const std::string &name,const char *value) {
    click(name);
    for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}}}}}));
    frame(Json::array({{{"type","text"},{"value",value}}}));frame();
  };
  auto settle=[&] {
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    do {require(std::chrono::steady_clock::now()<end,"Annotation workflow worker timeout");
      std::this_thread::sleep_for(std::chrono::milliseconds(3));frame();
    }while(last.at("native_workflow").at("working").get<bool>());
  };
  frame();frame();frame();click("workflow.run");click("program.edit_parameters");settle();
  text("parameters.vertex","2");text("parameters.vertex_u","3.7500000000000004");text("parameters.vertex_v","0.8750000000000001");
  text("parameters.name","workflow curve");click("parameters.apply");
  require(!last.at("native_workflow").at("editor").value("visible",false),"Annotation choices refused");
  report["states"]["choices_kept"]=observe(window);
  click("program.confirm");click("program.step.1");click("program.edit_parameters");settle();
  click("parameters.confirm_extent");text("parameters.vertex","2");
  text("parameters.vertex_u","3.8750000000000004");text("parameters.vertex_v","0.6250000000000001");
  click("parameters.keep_parameters");
  require(!last.at("native_workflow").at("editor").value("visible",false),"Saved geometry choices refused");
  report["states"]["saved_choices_kept"]=observe(window);
  click("program.confirm");click("program.prepare_program");settle();
  require(last.at("native_workflow").at("program_ready").get<bool>(),"Annotation program not ready");
  click("program.keep_program");report["program"]=window.workflow.at(0);
  click("workflow.run");click("program.confirm");click("program.step.1");click("program.confirm");click("program.prepare");settle();
  require(last.at("native_workflow").at("ready").get<bool>(),"Annotation workflow not ready");
  report["states"]["prepared"]=observe(window);click("program.apply");frame();
  report["states"]["applied"]=observe(window);report["rendered"]=last.at("orthogonal_planes");
  click("program.cancel");
  click("menu.Edit");click("menu.Edit.Undo");frame();report["states"]["undone"]=observe(window);
  click("menu.Edit");click("menu.Edit.Redo");frame();report["states"]["redone"]=observe(window);
  session.close();const auto saved=(directory/"workflow.ijx").u8string();window.action("save_document",{{"path",saved}});
  Window reopened((directory/"reopened.json").u8string());reopened.openFile(saved);report["states"]["reopened"]=observe(reopened);
  report["state"]="observed";report["error"]=window.error();
  require(report.dump().size()<48*1024*1024,"Annotation workflow report bound");
  writeJson((directory/"case.json").u8string(),report);return report;
}
} // namespace imagejxx::desktop
