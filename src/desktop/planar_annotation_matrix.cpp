// Real shared ImGui input; fixture interventions are labeled in each report.
#include "planar_annotation_matrix.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "../native/operation_job_probe_internal.hpp"
#include <condition_variable>
#include <imagejxx/planar_annotations.hpp>
#include <imagejxx/selection_edit.hpp>
#include <algorithm>
namespace imagejxx::desktop {
namespace {
void require(bool v,const char *message){if(!v)throw std::runtime_error(message);}
std::vector<std::string> cases() {
  std::vector<std::string> result;
  for(const auto *plane:{"XY","XZ","YZ"})
    for(const auto *op:{"rectangle","ellipse","line","point","freehand","polygon","multipoint",
        "escape","focus","context","source","wheel","zoom","preferences","resize","vertex","rotate","mask_move","mask_hole"})
      result.push_back(std::string(plane)+"."+op);
  for(const auto *plane:{"XY","XZ","YZ"}) {
    for(const auto *op:{"busy","scientific_busy","modal","time","channel","owner","style","mask_rotate"})result.push_back(std::string(plane)+"."+op);
    for(int handle=0;handle<8;++handle)for(const auto *modifier:{"none","shift","alt","both"})
      result.push_back(std::string(plane)+".resize_"+std::to_string(handle)+"_"+modifier);
    for(int handle=0;handle<8;++handle)for(const auto *modifier:{"none","shift","alt","both"})
      result.push_back(std::string(plane)+".cross_"+std::to_string(handle)+"_"+modifier);
    for(const auto *modifier:{"none","shift","alt","both"})
      result.push_back(std::string(plane)+".collapse_"+modifier);
    for(const auto *bad:{"width_zero","width_negative","width_nan","width_inf","width_overflow","width_partial",
                        "height_zero","center_nan","angle_inf","fixed_negative","time_zero","channel_over"})
      result.push_back(std::string(plane)+".invalid_"+bad);
    for(const auto *profile:{"ellipse","rbox","rellipse"}) {
      for(int handle=0;handle<8;++handle)for(const auto *modifier:{"none","shift","alt","both"})
        result.push_back(std::string(plane)+".profile_"+profile+"_cross_"+std::to_string(handle)+"_"+modifier);
      if(std::string(profile)=="ellipse")for(const auto *modifier:{"none","shift","alt","both"})
        result.push_back(std::string(plane)+".profile_ellipse_collapse_"+modifier);
    }
  }
  return result;
}
Json state(Window &window) {
  const auto source=window.nativeSnapshot();const auto &data=source.data();
  Json pixels=Json::array();const auto shape=data.dataset.shape();
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z)pixels.push_back(data.dataset.plane({t,c,z})->pixels);
  return {{"metadata",metadataJson(data.metadata.extensions)},{"selection",data.selection?selectionJson(*data.selection):Json()},
      {"revisions",{{"pixels",source.revisions().pixels},{"metadata",source.revisions().metadata}}},
      {"owner",source.id()},{"undo",window.nativeDocument(source.id())->undo_steps()},{"workflow",window.workflow},{"pixels",pixels},
      {"crosshair",source.display().crosshair},{"t",source.display().t},{"c",source.display().c}};
}
}
Json planarAnnotationMatrixUiCase(Window &window,const std::string &id,const std::string &context,const std::string &fresh_directory) {
  namespace fs=std::filesystem;const auto all=cases();require(std::find(all.begin(),all.end(),id)!=all.end(),"Unknown planar matrix case");
  const auto directory=fs::absolute(fs::u8path(fresh_directory));
  require(!fs::exists(directory) && window.documents().empty(),"Fresh matrix directory/window required");fs::create_directories(directory);
  const auto split=id.find('.');const auto plane_name=id.substr(0,split);auto op=id.substr(split+1);
  std::string profile;
  if(op.rfind("profile_",0)==0) {const auto end=op.find('_',8);profile=op.substr(8,end-8);op=op.substr(end+1);}
  const bool oblique=profile=="rbox" || profile=="rellipse";
  const auto plane=parse_plane(plane_name);const int view=plane==Plane::XY?0:plane==Plane::XZ?1:2;
  const bool scientific_busy=op=="scientific_busy";
  const DatasetShape shape{op=="busy"?1:2,op=="busy"?1:scientific_busy?3:2,9,13,17,1};Stack planes;
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<9;++z) {
    Image image(17,13,1,PixelType::UInt16);
    for(int y=0;y<13;++y)for(int x=0;x<17;++x)image.at(x,y)=10000*t+1000*c+100*z+10*y+x;
    planes.push_back(std::move(image));
  }
  SpatialCalibration calibration;calibration.spacing={2,3,5};calibration.origin={10,20,30};
  DocumentData data{Dataset::from_planes(shape,std::move(planes),calibration)};
  const bool mask=op=="mask_move" || op=="mask_hole" || op=="mask_rotate";
  const bool crossing=op.rfind("cross_",0)==0,collapse=op.rfind("collapse_",0)==0,invalid=op.rfind("invalid_",0)==0;
  const bool resize_variant=op.rfind("resize_",0)==0 || crossing || collapse;
  const bool edit=mask || resize_variant || invalid || op=="style" || op=="resize" || op=="vertex" || op=="rotate";
  if(edit) {
    Selection selection("rectangle",{{2,2},{6,4}});
    if(op=="resize")selection=Selection("rectangle",{{1,1},{5,5}});
    if(resize_variant)selection=Selection("rectangle",{{3,2},{9,6}});
    if(profile=="ellipse" || profile=="rellipse")selection.kind="ellipse";
    if(oblique) {
      const auto geometry=planar_annotation_geometry(shape,plane);
      selection=SelectionEditor(selection,geometry.width,geometry.height).rotated(30);
    }
    if(op=="vertex")selection=Selection("polygon",{{1,1},{5,1},{3,5}});
    if(mask) {
      const auto dimensions=planar_annotation_geometry(shape,plane);
      Image raster(dimensions.width,dimensions.height,1,PixelType::UInt8);
      for(int y=2;y<7;++y)for(int x=2;x<7;++x)raster.at(x,y)=255;
      raster.at(4,4)=0;raster.at(9,2)=255;selection=Selection::from_mask(raster);
    }
    selection.attributes={{"opaque",std::string("retained")}};
    data.metadata.extensions[planar_annotations_extension]=encode_planar_annotations(shape,
        {{"Seed",selection,plane,view==0?2:view==1?3:4,1,1,{{"tag",std::string("row")}}}});
  }
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));auto display=owner->snapshot().display();
  display.orthogonal=true;display.zoom=op=="rotate"?2:4;display.crosshair={4,3,2};display.t=shape.t-1;display.c=shape.c-1;owner->set_display(display);
  window.addNativeDocument(owner);window.recording=true;
  if(op=="owner") {
    auto other=std::make_shared<imagejxx::Document>(owner->snapshot().data());
    other->set_display(display);window.addNativeDocument(other);window.setActiveIndex(0);
  }
  Json report{{"contract","p06p.planar-matrix.ui.001"},{"case",id},{"context",context},{"acceptance_claim",false},
      {"frames",Json::array()},{"controls",Json::array()},{"states",{{"before",state(window)}}},
      {"api_fixture_actions",{"dataset/seed attachment"}}};
  report["profile"]=profile;
  auto session=std::make_unique<HeadlessInputSession>(window);Json last;
  auto frame=[&](const Json &events=Json::array()) {
    require(report["frames"].size()<160,"Matrix frame bound");last=session->frame(events);
    require(last.at("input_frame").at("raw_inputs")==events,"Matrix raw input mismatch");
    Json compact{{"input_frame",last.at("input_frame")},{"orthogonal_planes",last.at("orthogonal_planes")},
        {"orthogonal_navigation",last.at("orthogonal_navigation")},{"window_error",last.at("window_error")},
        {"preferences_panel",last.at("preferences_panel")},{"file_browser",last.at("file_browser")}};
    for(auto &p:compact["orthogonal_planes"])p.erase("readback");
    report["frames"].push_back({{"events",events},{"rendered",std::move(compact)}});
  };
  auto panel=[&](){return last.at("orthogonal_planes").at(0).at("planar_annotations");};
  auto click=[&](const Json &control,const std::string &name) {
    report["controls"].push_back({{"name",name},{"control",control}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));frame();
  };
  auto point=[&](double u,double v) {
    if(oblique) {
      const double angle=std::acos(-1.)/6,c=std::cos(angle),s=std::sin(angle),x=u-6,y=v-4;
      u=6+c*x-s*y;v=4+s*x+c*y;
    }
    const auto p=last.at("orthogonal_planes").at(view);
    return Json::array({p["origin"][0].get<double>()+u*p["scale"][0].get<double>(),p["origin"][1].get<double>()+v*p["scale"][1].get<double>()});
  };
  auto position=[&](double u,double v){frame(Json::array({{{"type","position"},{"space","screen"},{"point",point(u,v)}}}));};
  auto button=[&](bool down){frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));};
  auto key=[&](const char *name,bool down){frame(Json::array({{{"type","keys"},{"keys",{{{"name",name},{"down",down}}}}}}));};
  auto vertex=[&](double u,double v){position(u,v);button(true);button(false);frame();};
  frame();frame();frame();report["before_render"]=last.at("orthogonal_planes");
  const bool multi=op=="polygon" || op=="multipoint";
  const bool creation=op=="rectangle" || op=="ellipse" || op=="line" || op=="point" || op=="freehand" || multi;
  click(panel().at("controls").at(edit?"edit":creation?op:"rectangle"),"tool");
  if(op=="rotate" || op=="style" || op=="mask_rotate" || invalid) {
    click(panel().at("controls").at("rows"),"rows");click(panel().at("controls").at("row.0"),"row.0");
  }
  auto edit_text=[&](const char *name,const char *value) {
    click(panel().at("controls").at(name),name);
    for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}}}}}));
    frame(Json::array({{{"type","text"},{"value",value}}}));frame();
  };
  // Arm shortcut ownership on this canvas before beginning the interrupted
  // gesture. A zero-distance rectangle click has no candidate/publication.
  if(op=="modal")vertex(1,1);
  if(invalid) {
    const bool scope=op=="invalid_fixed_negative" || op=="invalid_time_zero" || op=="invalid_channel_over";
    click(panel().at("controls").at(scope?"scope":"geometry_section"),"invalid.section");
    const char *field="box_width",*bad="0",*restore="4";
    if(op=="invalid_width_negative")bad="-1";
    if(op=="invalid_width_nan")bad="nan";
    if(op=="invalid_width_inf")bad="inf";
    if(op=="invalid_width_overflow")bad="1e999";
    if(op=="invalid_width_partial")bad="1e";
    if(op=="invalid_height_zero"){field="box_height";restore="2";}
    if(op=="invalid_center_nan"){field="center_u";bad="nan";}
    if(op=="invalid_angle_inf"){field="box_angle";bad="inf";restore="0";}
    std::string original_coordinate=std::to_string(view==0?2:view==1?3:4);
    if(op=="invalid_fixed_negative"){field="scope_coordinate";bad="-1";restore=original_coordinate.c_str();}
    if(op=="invalid_time_zero"){field="scope_t";restore="2";}
    if(op=="invalid_channel_over"){field="scope_c";bad="3";restore="2";}
    report["states"]["during"]=state(window);report["draft"]=panel();
    edit_text(field,bad);click(panel().at("controls").at(scope?"scope_apply":"box_apply"),"invalid.apply");
    report["refused"]=state(window);report["refusal_panel"]=panel();
    edit_text(field,restore);click(panel().at("controls").at(scope?"scope_apply":"box_apply"),"invalid.repair");
    report["repaired"]=state(window);report["repair_panel"]=panel();
    if(scope)click(panel().at("controls").at("geometry_section"),"recovery.geometry");
    edit_text("box_width","6");click(panel().at("controls").at("box_apply"),"recovery.apply");
  } else if(op=="style" || op=="mask_rotate") {
    click(panel().at("controls").at("properties_section"),"properties_section");
    report["states"]["during"]=state(window);report["draft"]=panel();
    if(op=="style") {
      edit_text("name","Styled");edit_text("stroke","#FF112233");edit_text("fill","#80556677");edit_text("width","0");
      click(panel().at("controls").at("properties"),"properties");
    } else {edit_text("rotation","90");click(panel().at("controls").at("geometry"),"geometry");}
  } else if(multi) {
    vertex(1,1);vertex(5,1);if(op=="polygon")vertex(3,5);
    report["draft"]=panel();click(panel().at("controls").at("finish"),"finish");
  } else {
    double u=1,v=1,end_u=5,end_v=5;
    if(op=="point")end_u=end_v=1;
    if(op=="resize") {u=v=5;end_u=6;end_v=5;}
    if(op=="vertex") {u=5;v=1;end_u=6;end_v=2;}
    if(mask) {u=op=="mask_hole"?4.5:3.5;v=op=="mask_hole"?4.5:3.5;end_u=u+1;end_v=v;}
    if(op=="rotate") {
      const auto p=last.at("orthogonal_planes").at(view);const auto scale=p.at("scale")[1].get<double>();
      u=4;v=2-32/scale;end_u=4+(1+32/scale);end_v=3;
    }
    std::string modifier;
    if(resize_variant) {
      const int handle=collapse?1:op[crossing?6:7]-'0';modifier=op.substr(collapse?9:crossing?8:9);
      const std::array<std::array<double,2>,8> handles{{{3,2},{6,2},{9,2},{9,4},{9,6},{6,6},{3,6},{3,4}}};
      u=handles[handle][0];v=handles[handle][1];end_u=u+1;end_v=v+1;
      if(modifier=="shift" || modifier=="both")key("Shift",true);
      if(modifier=="alt" || modifier=="both")key("Alt",true);
      if(crossing) {
        const std::array<int,8> xs{-1,0,1,1,1,0,-1,-1},ys{-1,-1,-1,0,1,1,1,0};
        const bool centered=modifier=="alt" || modifier=="both";
        const double ax=centered || !xs[handle]?6:xs[handle]<0?9:3;
        const double ay=centered || !ys[handle]?4:ys[handle]<0?6:2;
        end_u=ax-xs[handle];end_v=ay-ys[handle];
      }
      if(collapse) {end_u=6;end_v=modifier=="alt" || modifier=="both"?4:6;}
    }
    position(u,v);button(true);report["down_frame"]=report["frames"].size()-1;
    if(crossing) {
      const int h=op[6]-'0';const bool center=modifier=="alt" || modifier=="both";
      const std::array<int,8> xs{-1,0,1,1,1,0,-1,-1},ys{-1,-1,-1,0,1,1,1,0};
      position(center || !xs[h]?6:xs[h]<0?9:3,center || !ys[h]?4:ys[h]<0?6:2);
      report["collapsed_frame"]=report["frames"].size()-1;report["collapsed_panel"]=panel();
      report["collapsed_state"]=state(window);
    }
    position(end_u,end_v);report["end_frame"]=report["frames"].size()-1;
    if(op=="freehand") {position(5,1);position(3,5);}
    frame();report["draft"]=panel();report["states"]["during"]=state(window);
    if(scientific_busy) {
      struct Gate {
        std::mutex mutex;std::condition_variable changed;bool entered=false,released=false;
        std::string stage,operation;std::uint64_t source=0;std::size_t completed=0,total=0;
      };
      auto gate=std::make_shared<Gate>();
      std::optional<OperationJob> job;
      struct Drain {
        Window &window;std::optional<OperationJob> &job;std::shared_ptr<Gate> gate;
        ~Drain(){try {
          window.cancel();
          {std::lock_guard<std::mutex> lock(gate->mutex);gate->released=true;gate->changed.notify_all();}
          if(job && job->wait_for(std::chrono::seconds(2)))window.wait();
        }catch(...) {}}
      } drain{window,job,gate};
      TargetRequest target;target.scope=TargetScope::Positions;target.roi=RoiPolicy::WholePlane;
      target.positions={{1,2,2},{0,1,7}};
      report["api_fixture_actions"].push_back("Scientific Gaussian Apply with explicit frozen positions; native source-copy probe holds its genuine worker; cancelJob API cancellation");
      report["scientific_target"]=metadataJson(resolve_gaussian_command(window.nativeSnapshot(),target,{.85,false}).record());
      {
        detail::ScopedOperationJobProbe probe([gate](const detail::OperationJobProbeEvent &event) {
          if(event.phase!="stage_progress" || event.stage!="ImageJ Gaussian source copy" || !event.completed)return;
          std::unique_lock<std::mutex> lock(gate->mutex);if(gate->entered)return;
          gate->entered=true;gate->stage=event.stage;gate->operation=event.operation;
          gate->source=event.source_id;gate->completed=event.completed;gate->total=event.total;gate->changed.notify_all();
          if(!gate->changed.wait_for(lock,std::chrono::seconds(10),[&]{return gate->released;}))
            throw std::runtime_error("Scientific annotation busy gate exceeded ten seconds");
        });
        window.applyGaussian(target,{.85,false});
      }
      job=GaussianCancellationAccess::laneJob(window,false);require(job.has_value(),"Scientific Apply lane missing");
      {
        std::unique_lock<std::mutex> lock(gate->mutex);
        require(gate->changed.wait_for(lock,std::chrono::seconds(2),[&]{return gate->entered;}),"Scientific worker never copied samples");
        report["worker"]={{"stage",gate->stage},{"operation",gate->operation},{"source",gate->source},
                          {"completed",gate->completed},{"total",gate->total}};
      }
      report["busy"]={{"job_id",job->info().id},{"state",int(job->info().state)},{"window_busy",window.busy()}};
      frame();report["states"]["busy"]=state(window);button(false);frame();report["busy_panel"]=panel();
      report["cancel_accepted"]=window.cancelJob(job->info().id);
      {std::lock_guard<std::mutex> lock(gate->mutex);gate->released=true;gate->changed.notify_all();}
      require(job->wait_for(std::chrono::seconds(2)),"Scientific worker did not terminate");
      window.wait();frame();report["busy_after"]=window.busy();
      report["terminal"]={{"state",int(job->info().state)},{"cancel_requested",job->info().cancel_requested},
                          {"reserved_bytes",GaussianCancellationAccess::scheduler(window)->usage().reserved_bytes}};
    } else if(op=="busy") {
      struct Gate {std::mutex mutex;std::condition_variable changed;bool entered=false,released=false;};
      auto gate=std::make_shared<Gate>();
      report["api_fixture_actions"].push_back("Genuine legacy worker held before compute, canceled and drained");
      auto job=GaussianCancellationAccess::startBusyPreparation(window,[gate](const JobContext &ctx) {
        std::unique_lock<std::mutex> lock(gate->mutex);gate->entered=true;gate->changed.notify_all();
        if(!gate->changed.wait_for(lock,std::chrono::seconds(10),[&]{return gate->released;}))throw std::runtime_error("Annotation busy gate exceeded ten seconds");
        lock.unlock();ctx.checkpoint();
      });
      struct Drain {Window &window;Job job;std::shared_ptr<Gate> gate;
        ~Drain(){try {job.cancel();{std::lock_guard<std::mutex> lock(gate->mutex);gate->released=true;gate->changed.notify_all();}
          if(job.wait_for(std::chrono::seconds(2)))window.wait();}catch(...) {}}
      } drain{window,job,gate};
      {std::unique_lock<std::mutex> lock(gate->mutex);require(gate->changed.wait_for(lock,std::chrono::seconds(2),[&]{return gate->entered;}),"Busy worker never entered");}
      report["busy"]={{"job_id",job.info().id},{"state",int(job.info().state)},{"window_busy",window.busy()}};
      frame();button(false);frame();report["busy_panel"]=panel();
      job.cancel();{std::lock_guard<std::mutex> lock(gate->mutex);gate->released=true;gate->changed.notify_all();}
      require(job.wait_for(std::chrono::seconds(2)),"Busy worker did not terminate");window.wait();frame();report["busy_after"]=window.busy();
    } else if(op=="modal") {
      key("Ctrl",true);key("O",true);key("O",false);key("Ctrl",false);frame();
      report["modal"]=last.at("file_browser");report["modal_input"]=last.at("input_frame");
      if(!last.at("host_controls").contains("browser.cancel")) {
        report["shortcut_failure"]=last.at("shortcuts");writeJson((directory/"failure.json").u8string(),report);
        throw std::runtime_error("Open shortcut did not produce the real modal browser");
      }
      button(false);
      click(last.at("host_controls").at("browser.cancel"),"browser.cancel");
    } else if(op=="time" || op=="channel") {
      report["api_fixture_actions"].push_back("Concurrent explicit T/C navigation");
      auto next=owner->snapshot().display();if(op=="time")next.t=0;else next.c=0;window.setNativeDisplay(next,owner->id);frame();
    } else if(op=="owner") {
      report["api_fixture_actions"].push_back("Active owner switched away and back through Window activation");
      window.setActiveIndex(1);frame();window.setActiveIndex(0);frame();
    } else if(op=="escape") {key("Escape",true);key("Escape",false);}
    else if(op=="focus") {frame(Json::array({{{"type","focus"},{"focused",false}}}));report["focus_lost"]=last.at("input_frame");frame(Json::array({{{"type","focus"},{"focused",true}}}));}
    else if(op=="context") {
      session->close();report["states"]["closed"]=state(window);
      session=std::make_unique<HeadlessInputSession>(window);frame();frame();frame();
    } else if(op=="source") {
      report["api_fixture_actions"].push_back("Concurrent metadata title edit");
      auto tx=owner->snapshot().begin_edit(Change::Metadata);tx.set_title("Concurrent");owner->commit(tx);frame();
    } else if(op=="wheel")frame(Json::array({{{"type","wheel"},{"x",0},{"y",1}}}));
    else if(op=="zoom") {key("Ctrl",true);frame(Json::array({{{"type","wheel"},{"x",0},{"y",1}}}));key("Ctrl",false);}
    else if(op=="preferences") {
      report["api_fixture_actions"].push_back("Open existing Preferences route during capture");window.action("show_preferences");frame();frame();
    }
    button(false);frame();
    if(resize_variant) {
      if(modifier=="shift" || modifier=="both")key("Shift",false);
      if(modifier=="alt" || modifier=="both")key("Alt",false);
    }
  }
  report["states"]["after"]=state(window);report["after_panel"]=panel();report["after_render"]=last.at("orthogonal_planes");
  if(scientific_busy) {
    position(2,2);button(true);report["recovery_start_frame"]=report["frames"].size()-1;
    position(6,6);report["recovery_end_frame"]=report["frames"].size()-1;
    report["recovery_draft"]=panel();button(false);frame();
    report["recovered"]=state(window);report["recovery_panel"]=panel();
  }
  session->close();report["states"]["closed"]=state(window);
  report["state"]="observed";report["error"]=window.error();require(report.dump().size()<16*1024*1024,"Matrix byte bound");
  writeJson((directory/"case.json").u8string(),report);return report;
}
Json planarAnnotationMatrixUiBatch(const std::string &context,const std::string &fresh_directory,int group) {
  require(group>=0 && group<=4,"Unknown planar matrix group");
  namespace fs=std::filesystem;const auto root=fs::absolute(fs::u8path(fresh_directory));require(!fs::exists(root),"Fresh batch directory required");fs::create_directories(root);
  Json index{{"state","observed"},{"error",""},{"cases",Json::array()},{"context",context}};
  for(const auto &id:cases()) {
    if(group==1 && id.find(".cross_")==std::string::npos && id.find(".collapse_")==std::string::npos && id.find(".invalid_")==std::string::npos)continue;
    if(group>=2) {
      const std::array<const char *,3> prefixes{".profile_ellipse_",".profile_rbox_",".profile_rellipse_"};
      if(id.find(prefixes[group-2])==std::string::npos)continue;
    }
    Window window((root/(id+".preferences.json")).u8string());planarAnnotationMatrixUiCase(window,id,context,(root/id).u8string());index["cases"].push_back(id);
  }
  writeJson((root/"case.json").u8string(),index);return index;
}
} // namespace imagejxx::desktop
