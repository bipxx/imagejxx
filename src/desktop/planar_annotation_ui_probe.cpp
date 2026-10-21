// Actual shared-ImGui control input. Fixture setup and session I/O use explicit
// APIs. Independent pixel/metadata expectations belong to the Python reader.
#include "planar_annotation_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <imagejxx/session.hpp>
#include <imagejxx/planar_annotations.hpp>
#include <imagejxx/planar_annotation_command.hpp>
#include <stdexcept>
namespace imagejxx::desktop {
namespace {
void require(bool v,const char *message){if(!v)throw std::runtime_error(message);}
Json state(Window &window) {
  const auto source=window.nativeSnapshot();const auto &data=source.data();
  const auto shape=data.dataset.shape();const auto cal=data.dataset.calibration();Json pixels=Json::array(),rois=Json::array();
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z)pixels.push_back(data.dataset.plane({t,c,z})->pixels);
  for(const auto &roi:data.rois)rois.push_back({{"name",roi.name},{"z",roi.z?Json(*roi.z):Json()},{"selection",selectionJson(roi.selection)}});
  return {{"revisions",{{"pixels",source.revisions().pixels},{"metadata",source.revisions().metadata}}},{"shape",shape.dimensions()},{"pixels",pixels},{"origin",cal.origin},{"spacing",cal.spacing},
    {"rois",rois},{"crosshair",source.display().crosshair},{"t",source.display().t},{"c",source.display().c},
    {"undo",window.nativeDocument(source.id())->undo_steps()},{"workflow",window.workflow},
    {"document_count",window.documents().size()},{"metadata",metadataJson(data.metadata.extensions)},{"selection",data.selection?selectionJson(*data.selection):Json()}};
}
}
Json planarAnnotationUiCase(Window &window,const std::string &context,const std::string &fresh_directory) {
  namespace fs=std::filesystem;const auto directory=fs::absolute(fs::u8path(fresh_directory));
  require((context=="imgui_cpp" || context=="imgui_python") && !fs::exists(directory) && window.documents().empty(),"Fresh Planar annotation UI fixture required");
  fs::create_directories(directory);
  Stack planes;
  for(int t=0;t<2;++t)for(int c=0;c<2;++c)for(int z=0;z<6;++z){
    Image image(13,9,1,PixelType::UInt16);
    for(int y=0;y<9;++y)for(int x=0;x<13;++x)image.at(x,y)=10000*t+1000*c+100*z+10*y+x;
    planes.push_back(std::move(image));
  }
  SpatialCalibration cal;cal.spacing={2,3,5};cal.origin={10,20,30};
  DocumentData data{Dataset::from_planes({2,2,6,9,13,1},std::move(planes),cal)};
  Selection selection("rectangle",{{1,1},{5,4}});
  data.rois={{"zero",selection,0,0,0,{}},{"first",selection,0,0,1,{}},{"last",selection,1,1,3,{}},{"wild",selection,{},{},{},{}}};
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));auto view=owner->snapshot().display();view.t=1;view.c=1;view.crosshair={4,3,2};view.orthogonal=true;view.zoom=4;owner->set_display(view);
  window.addNativeDocument(owner);window.recording=false;
  Json report{{"contract","p06p.planar-annotation.ui.001"},{"context",context},{"acceptance_claim",false},
      {"frames",Json::array()},{"controls",Json::array()},{"states",{{"before",state(window)}}},
      {"api_fixture_actions",{"dataset/ROI attachment","session Save/Open"}}};
  HeadlessInputSession session(window);Json last;
  auto frame=[&](const Json &events=Json::array()){
    require(report["frames"].size()<250,"Planar annotation UI frame bound");last=session.frame(events);
    require(last.at("input_frame").at("raw_inputs")==events,"Planar annotation input mismatch");report["frames"].push_back({{"events",events},{"rendered",last}});
  };
  auto click=[&](const Json &control,const std::string &name){
    if(!control.at("enabled").get<bool>() || control.at("id").get<std::uint64_t>()==0) {
      report["unavailable_control"]={{"name",name},{"control",control}};
      writeJson((directory/"failure.json").u8string(),report);
      throw std::runtime_error("Planar annotation control unavailable: "+name);
    }
    report["controls"].push_back({{"name",name},{"control",control}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));frame();
  };
  auto panel=[&](){return last.at("orthogonal_planes").at(0).at("planar_annotations");};
  auto control=[&](const char *name){
    if(!panel().at("controls").contains(name)) {
      writeJson((directory/"failure.json").u8string(),report);
      throw std::runtime_error(std::string("Missing planar annotation control: ")+name+" "+panel().dump());
    }
    return panel().at("controls").at(name);
  };
  auto point=[&](int view,double u,double v) {
    const auto plane=last.at("orthogonal_planes").at(view);
    return Json::array({plane["origin"][0].get<double>()+u*plane["scale"][0].get<double>(),
                        plane["origin"][1].get<double>()+v*plane["scale"][1].get<double>()});
  };
  auto drag=[&](int view,double u,double v,double end_u,double end_v,bool escape=false) {
    const auto start=point(view,u,v),end=point(view,end_u,end_v);
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",start}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",end}}}));frame();
    report["drafts"].push_back(panel());
    if(escape) {
      frame(Json::array({{{"type","keys"},{"keys",{{{"name","Escape"},{"down",true}}}}}}));
      frame(Json::array({{{"type","keys"},{"keys",{{{"name","Escape"},{"down",false}}}}}}));
    }
    frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));frame();
  };
  report["drafts"]=Json::array();
  frame();frame();frame();report["before_render"]=last.at("orthogonal_planes");
  click(control("rectangle"),"annotation.rectangle");
  drag(1,1,1,4,4);report["states"]["created"]=state(window);report["created_render"]=last.at("orthogonal_planes");
  click(control("edit"),"annotation.edit");
  drag(1,2,2,3,2);report["states"]["moved"]=state(window);report["moved_render"]=last.at("orthogonal_planes");
  click(control("ellipse"),"annotation.ellipse");
  drag(2,1,1,3,3,true);report["states"]["cancelled"]=state(window);report["cancel_render"]=last.at("orthogonal_planes");
  // Name/style fields use real widget inputs; fixture/session APIs are explicit.
  auto editText=[&](const char *name,const char *value) {
    click(control(name),name);
    for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}}}}}));
    frame(Json::array({{{"type","text"},{"value",value}}}));frame();
  };
  click(control("properties_section"),"annotation.open_properties");
  editText("name","XZ area");editText("fill","#8040D2F0");click(control("properties"),"annotation.properties");
  report["states"]["styled"]=state(window);report["styled_render"]=last.at("orthogonal_planes");
  frame();report["idle_before"]=last.at("orthogonal_planes");frame();frame();report["idle_after"]=last.at("orthogonal_planes");
  click(last.at("host_controls").at("menu.Edit"),"menu.Edit");
  click(last.at("host_controls").at("menu.Edit.Undo"),"undo");frame();report["states"]["undone"]=state(window);
  click(last.at("host_controls").at("menu.Edit"),"menu.Edit");
  click(last.at("host_controls").at("menu.Edit.Redo"),"redo");frame();report["states"]["redone"]=state(window);
  session.close();
  window.action("save_document",{{"path",(directory/"annotations.ijx").u8string()}});
  Window reopened((directory/"reopened.json").u8string());reopened.openFile((directory/"annotations.ijx").u8string());
  report["states"]["reopened"]=state(reopened);
  report["state"]="observed";report["error"]=window.error();
  require(report.dump().size()<=16*1024*1024,"Planar annotation report byte bound");
  writeJson((directory/"case.json").u8string(),report);return report;
}
Json planarAnnotationToolsUiCase(Window &window,const std::string &context,const std::string &fresh_directory) {
  namespace fs=std::filesystem;const auto directory=fs::absolute(fs::u8path(fresh_directory));
  require((context=="imgui_cpp" || context=="imgui_python") && !fs::exists(directory) && window.documents().empty(),"Fresh Planar annotation UI fixture required");
  fs::create_directories(directory);
  Stack planes;
  for(int t=0;t<2;++t)for(int c=0;c<2;++c)for(int z=0;z<6;++z){
    Image image(13,9,1,PixelType::UInt16);
    for(int y=0;y<9;++y)for(int x=0;x<13;++x)image.at(x,y)=10000*t+1000*c+100*z+10*y+x;
    planes.push_back(std::move(image));
  }
  SpatialCalibration cal;cal.spacing={2,3,5};cal.origin={10,20,30};
  DocumentData data{Dataset::from_planes({2,2,6,9,13,1},std::move(planes),cal)};
  Selection selection("rectangle",{{1,1},{5,4}});
  data.rois={{"zero",selection,0,0,0,{}},{"first",selection,0,0,1,{}},{"last",selection,1,1,3,{}},{"wild",selection,{},{},{},{}}};
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));auto view=owner->snapshot().display();view.t=1;view.c=1;view.crosshair={4,3,2};view.orthogonal=true;view.zoom=4;owner->set_display(view);
  window.addNativeDocument(owner);window.recording=true;
  Json report{{"contract","p06p.planar-tools.ui.001"},{"context",context},{"acceptance_claim",false},
      {"frames",Json::array()},{"controls",Json::array()},{"states",{{"before",state(window)}}},
      {"api_fixture_actions",{"dataset/ROI attachment","session Save/Open"}}};
  HeadlessInputSession session(window);Json last;
  auto frame=[&](const Json &events=Json::array()){
    require(report["frames"].size()<512,"Planar annotation UI frame bound");last=session.frame(events);
    require(last.at("input_frame").at("raw_inputs")==events,"Planar annotation input mismatch");report["frames"].push_back({{"events",events},{"rendered",last}});
  };
  auto click=[&](const Json &control,const std::string &name){
    if(!control.at("enabled").get<bool>() || control.at("id").get<std::uint64_t>()==0) {
      report["unavailable_control"]={{"name",name},{"control",control}};
      writeJson((directory/"failure.json").u8string(),report);
      throw std::runtime_error("Planar annotation control unavailable: "+name);
    }
    report["controls"].push_back({{"name",name},{"control",control}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));frame();
  };
  auto panel=[&](){return last.at("orthogonal_planes").at(0).at("planar_annotations");};
  auto control=[&](const char *name){
    if(!panel().at("controls").contains(name)) {
      writeJson((directory/"failure.json").u8string(),report);
      throw std::runtime_error(std::string("Missing planar annotation control: ")+name+" "+panel().dump());
    }
    return panel().at("controls").at(name);
  };
  auto point=[&](int view,double u,double v) {
    const auto plane=last.at("orthogonal_planes").at(view);
    return Json::array({plane["origin"][0].get<double>()+u*plane["scale"][0].get<double>(),
                        plane["origin"][1].get<double>()+v*plane["scale"][1].get<double>()});
  };
  auto drag=[&](int view,double u,double v,double end_u,double end_v,bool escape=false) {
    const auto start=point(view,u,v),end=point(view,end_u,end_v);
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",start}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",end}}}));frame();
    report["drafts"].push_back(panel());
    if(escape) {
      frame(Json::array({{{"type","keys"},{"keys",{{{"name","Escape"},{"down",true}}}}}}));
      frame(Json::array({{{"type","keys"},{"keys",{{{"name","Escape"},{"down",false}}}}}}));
    }
    frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));frame();
  };
  auto vertex=[&](int view,double u,double v) {
    const auto position=point(view,u,v);
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",position}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));frame();
  };
  auto editText=[&](const char *name,const char *value) {
    click(control(name),name);
    for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}}}}}));
    frame(Json::array({{{"type","text"},{"value",value}}}));frame();
  };
  report["drafts"]=Json::array();frame();frame();frame();
  click(control("polygon"),"annotation.polygon");
  vertex(2,1,1);vertex(2,3,1);vertex(2,3,4);
  report["drafts"].push_back(panel());
  click(control("remove_last"),"annotation.remove_last");report["removed_draft"]=panel();
  vertex(2,2,4);click(control("finish"),"annotation.finish_polygon");
  report["states"]["polygon"]=state(window);
  click(control("scope"),"annotation.scope");
  editText("scope_coordinate","999");click(control("scope_apply"),"annotation.invalid_scope");
  report["scope_error"]=panel();report["states"]["invalid_scope"]=state(window);
  editText("scope_coordinate","0");click(control("all_t"),"annotation.all_t");click(control("all_c"),"annotation.all_c");
  click(control("scope_apply"),"annotation.apply_scope");report["states"]["scoped"]=state(window);
  click(control("scope_apply"),"annotation.noop_scope");report["states"]["noop"]=state(window);
  click(control("scope"),"annotation.close_scope");
  // Wheel only scrolls the actual Workflow window; no document navigation API.
  const auto record_control=last.at("host_controls").at("workflow.record");
  auto scroll_point=record_control.at("point");scroll_point[1]=scroll_point[1].get<double>()+65;
  frame(Json::array({{{"type","position"},{"space","screen"},{"point",scroll_point}}}));
  frame(Json::array({{{"type","wheel"},{"x",0},{"y",-8}}}));frame();
  click(last.at("host_controls").at("workflow.planar.2.replay"),"workflow.planar.replay");
  report["states"]["replayed"]=state(window);
  click(control("go_to"),"annotation.go_to");report["states"]["positioned"]=state(window);
  click(control("multipoint"),"annotation.multipoint");
  vertex(0,2,2);vertex(0,5,2);report["drafts"].push_back(panel());
  click(control("finish"),"annotation.finish_multipoint");
  report["states"]["multipoint"]=state(window);report["final_render"]=last.at("orthogonal_planes");
  // Numeric fields use the same native SelectionEditor and command publication.
  click(control("geometry_section"),"annotation.open_geometry");
  editText("vertex_u","3");click(control("vertex_apply"),"annotation.move_vertex");
  report["states"]["vertex_moved"]=state(window);
  click(control("vertex_apply"),"annotation.noop_vertex");report["states"]["vertex_noop"]=state(window);
  editText("vertex_index","0");click(control("vertex_apply"),"annotation.invalid_vertex_index");
  report["states"]["vertex_index_refused"]=state(window);report["vertex_index_error"]=panel();
  editText("vertex_index","3");editText("vertex_u","7");editText("vertex_v","3");
  click(control("vertex_insert"),"annotation.insert_vertex");report["states"]["vertex_inserted"]=state(window);
  editText("vertex_index","2");click(control("vertex_remove"),"annotation.remove_vertex");
  report["states"]["vertex_removed"]=state(window);
  editText("vertex_index","2");click(control("vertex_remove"),"annotation.remove_to_minimum");
  report["states"]["vertex_minimum"]=state(window);
  click(control("vertex_remove"),"annotation.refuse_minimum");
  report["states"]["vertex_minimum_refused"]=state(window);report["vertex_minimum_error"]=panel();
  click(control("geometry_section"),"annotation.close_geometry");
  click(control("rectangle"),"annotation.numeric_rectangle");drag(1,1,1,4,4);
  report["states"]["numeric_rectangle"]=state(window);
  click(control("geometry_section"),"annotation.open_box_geometry");
  editText("box_width","0");click(control("box_apply"),"annotation.invalid_box");
  report["states"]["box_refused"]=state(window);report["box_error"]=panel();
  editText("box_width","6");editText("box_height","2");
  editText("center_u","6");editText("center_v","3");editText("box_angle","90");
  click(control("box_apply"),"annotation.resize_box");report["states"]["box_resized"]=state(window);
  click(control("box_apply"),"annotation.noop_box");report["states"]["box_noop"]=state(window);
  editText("box_width","nan");click(control("box_apply"),"annotation.invalid_number");
  report["states"]["number_refused"]=state(window);report["number_error"]=panel();
  click(control("geometry_section"),"annotation.close_box_geometry");
  click(last.at("host_controls").at("menu.Edit"),"menu.Edit");
  click(last.at("host_controls").at("menu.Edit.Undo"),"numeric.undo");frame();report["states"]["numeric_undone"]=state(window);
  click(last.at("host_controls").at("menu.Edit"),"menu.Edit");
  click(last.at("host_controls").at("menu.Edit.Redo"),"numeric.redo");frame();report["states"]["numeric_redone"]=state(window);
  session.close();
  window.action("save_document",{{"path",(directory/"annotations.ijx").u8string()}});
  Window reopened((directory/"reopened.json").u8string());reopened.openFile((directory/"annotations.ijx").u8string());
  report["states"]["reopened"]=state(reopened);
  report["state"]="observed";report["error"]=window.error();
  require(report.dump().size()<=64*1024*1024,"Planar annotation report byte bound");
  writeJson((directory/"case.json").u8string(),report);return report;
}
Json planarAnnotationRebindUiCase(Window &window,const std::string &context,const std::string &fresh_directory) {
  namespace fs=std::filesystem;const auto directory=fs::absolute(fs::u8path(fresh_directory));
  require((context=="imgui_cpp" || context=="imgui_python") && !fs::exists(directory) && window.documents().empty(),"Fresh rebind UI fixture required");
  fs::create_directories(directory);
  auto dataset=[](DatasetShape shape) {
    Stack planes;
    for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z) {
      Image image(shape.x,shape.y,1,PixelType::UInt16);
      for(int y=0;y<shape.y;++y)for(int x=0;x<shape.x;++x)image.at(x,y)=10000*t+1000*c+100*z+10*y+x;
      planes.push_back(std::move(image));
    }
    SpatialCalibration cal;cal.spacing={2,3,5};cal.origin={10,20,30};
    return Dataset::from_planes(shape,std::move(planes),cal);
  };
  imagejxx::Document original_owner(DocumentData{dataset({2,2,9,15,17,1})});
  Selection rectangle("rectangle",{{1,1},{6,8}},2);rectangle.attributes={{"opaque",std::string("kept")}};
  PlanarAnnotation captured{"Captured XZ",rectangle,Plane::XZ,12,0,1,{{"tag",std::string("preserved")}}};
  const auto original=resolve_planar_annotation_command(original_owner.snapshot(),{PlanarAnnotationOperation::Store,{},captured});
  auto data=DocumentData{dataset({2,2,6,9,13,1})};data.metadata.title="Destination";
  data.metadata.extensions[planar_annotations_extension]=encode_planar_annotations(data.dataset.shape(),
      {{"Keeper",Selection("polygon",{{1,1},{3,1},{2,4}}),Plane::YZ,4,1,1,{}}});
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));auto view=owner->snapshot().display();
  view.t=1;view.c=1;view.crosshair={4,3,2};view.orthogonal=true;view.zoom=4;owner->set_display(view);
  window.addNativeDocument(owner);window.recording=true;window.workflow=Json::array({metadataJson(original.record())});
  Json report{{"contract","p06p.planar-rebind.ui.001"},{"context",context},{"acceptance_claim",false},
      {"original",metadataJson(original.record())},{"frames",Json::array()},{"controls",Json::array()},
      {"states",{{"before",state(window)}}},{"api_fixture_actions",{"dataset/annotation/workflow attachment","concurrent metadata edit","forged action preflight","session Save/Open"}}};
  HeadlessInputSession session(window);Json last;
  auto frame=[&](const Json &events=Json::array()) {
    require(report["frames"].size()<512,"Rebind UI frame bound");last=session.frame(events);
    require(last.at("input_frame").at("raw_inputs")==events,"Rebind UI input mismatch");
    report["frames"].push_back({{"events",events},{"rendered",last}});
  };
  auto panel=[&](){return last.at("planar_annotation_rebind");};
  auto control=[&](const std::string &name){
    if(!panel().at("controls").contains(name)) {writeJson((directory/"failure.json").u8string(),report);throw std::runtime_error("Missing rebind control: "+name);}
    return panel().at("controls").at(name);
  };
  auto click=[&](const Json &control,const std::string &name) {
    require(control.at("enabled").get<bool>() && control.at("id").get<std::uint64_t>()>0,"Unavailable rebind control");
    report["controls"].push_back({{"name",name},{"control",control}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));frame();
  };
  auto edit=[&](const char *key,const char *value) {
    click(control(key),key);
    for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}}}}}));
    frame(Json::array({{{"type","text"},{"value",value}}}));frame();
  };
  auto open=[&](){click(last.at("host_controls").at("workflow.planar.0.rebind"),"workflow.rebind");require(panel().at("visible").get<bool>(),"Rebind form failed to open");};
  frame();frame();frame();report["before_render"]=last.at("orthogonal_planes");
  open();click(control("cancel"),"rebind.cancel");report["states"]["canceled"]=state(window);
  open();click(control("apply"),"rebind.invalid_template");report["invalid_template"]=panel();report["states"]["invalid_template"]=state(window);
  edit("coordinate","0");edit("name","Rebound XZ");edit("center_u","4");edit("center_v","3");edit("width","4");edit("height","2");
  click(control("all_t"),"rebind.all_t");click(control("all_c"),"rebind.all_c");
  click(control("apply"),"rebind.store");report["states"]["stored"]=state(window);report["stored_render"]=last.at("orthogonal_planes");report["stored_form"]=panel();
  open();click(control("operation"),"rebind.operation");click(control("operation.1"),"rebind.operation.update");
  click(control("apply"),"rebind.missing_destination");report["missing_destination"]=panel();report["states"]["missing_destination"]=state(window);
  edit("target","1");click(control("replacement_choice"),"rebind.replacement_choice");click(control("replacement.0"),"rebind.replacement_row");
  edit("name","Replaced");edit("vertex_u","2");edit("vertex_v","2");click(control("stage_vertex"),"rebind.stage_vertex");
  edit("vertex","2");edit("vertex_u","4");click(control("apply"),"rebind.update");report["states"]["updated"]=state(window);report["updated_render"]=last.at("orthogonal_planes");
  open();click(control("operation"),"rebind.operation");click(control("operation.2"),"rebind.operation.delete");
  edit("target","2");click(control("apply"),"rebind.delete");report["states"]["deleted"]=state(window);report["deleted_render"]=last.at("orthogonal_planes");
  open();report["states"]["before_concurrent"]=state(window);
  auto concurrent=owner->snapshot().begin_edit(Change::Metadata);concurrent.set_title("Concurrent");owner->commit(concurrent);
  report["states"]["concurrent"]=state(window);frame();report["stale_form"]=panel();report["states"]["stale_canceled"]=state(window);
  auto forged=metadataJson(original.record());forged["version"]=true;
  try {window.action("show_planar_annotation_rebind",{{"record",forged},{"document_id",std::uint64_t(999999999)}});report["forged_refused"]=false;}
  catch(const std::exception &) {report["forged_refused"]=true;}
  report["states"]["forged_refused"]=state(window);
  session.close();window.action("save_document",{{"path",(directory/"annotations.ijx").u8string()}});
  Window reopened((directory/"reopened.json").u8string());reopened.openFile((directory/"annotations.ijx").u8string());
  report["states"]["reopened"]=state(reopened);report["state"]="observed";report["error"]=window.error();
  require(report.dump().size()<=64*1024*1024,"Rebind UI byte bound");writeJson((directory/"case.json").u8string(),report);return report;
}

}
