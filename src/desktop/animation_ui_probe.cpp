// Actual-input observer only. Expected positions, sample words and lifetimes
// belong to the independent reader, not this driver.
#include "animation_ui_probe.hpp"
#include "histogram_ui_probe.hpp"
#include "target_histogram_ui_probe.hpp"
#include "auto_threshold_ui_probe.hpp"
#include "automatic_display_ui_probe.hpp"
#include "scientific_threshold_target_ui_probe.hpp"
#include "movie_export_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace imagejxx::desktop {
Json animationUiCase(Window &window, const std::string &id,
                     const std::string &context, const std::string &directory) {
  if(id.rfind("target-histogram-",0)==0)return targetHistogramUiCase(window,id.substr(17),context,directory);
  if(id.rfind("histogram-",0)==0)return histogramUiCase(window,id.substr(10),context,directory);
  if(id.rfind("target-threshold-",0)==0)return scientificThresholdTargetUiCase(window,id.substr(17),context,directory);
  if(id.rfind("auto-display-",0)==0)return automaticDisplayUiCase(window,id.substr(13),context,directory);
  if(id.rfind("threshold-",0)==0)return autoThresholdUiCase(window,id.substr(10),context,directory);
  if(id.rfind("movie-",0)==0)return movieExportUiCase(window,id,context,directory);
  const std::vector<std::string> cases{"z","c","t","legacy","singleton","switch",
      "edit","close","stop","fps","ieee","rgb","busy","range","reverse","ping",
      "range-offgrid","range-c","range-t","range-invalid","cal-s","cal-ms","cal-us",
      "cal-step","cal-fps","cal-invalid","cal-edit","cal-unit","cal-legacy",
      "cal-legacy-step","cal-legacy-reverse","cal-legacy-ping","cal-legacy-edit","cal-legacy-unit"};
  if (std::find(cases.begin(),cases.end(),id)==cases.end() ||
      (context!="imgui_cpp" && context!="imgui_python") || !window.documents().empty())
    throw std::invalid_argument("Animation observation requires a fresh editor and a known case/host");
  const auto root=std::filesystem::absolute(std::filesystem::u8path(directory)).lexically_normal();
  if(directory.empty() || root==root.root_path()) throw std::invalid_argument("Fresh animation directory required");
  std::filesystem::create_directories(root.parent_path());
  if(!std::filesystem::create_directory(root)) throw std::invalid_argument("Animation directory must be fresh");
  const bool legacyTime=id.rfind("cal-legacy",0)==0;
  const bool legacy=id=="legacy" || id=="busy" || legacyTime, ieee=id=="ieee", rgb=id=="rgb";
  const bool ranged=id=="range" || id=="reverse" || id=="ping" || id.rfind("range-",0)==0;
  const bool calibrated=id.rfind("cal-",0)==0;
  const DatasetShape shape=legacy?DatasetShape{1,1,legacyTime?5:4,5,7,1}:DatasetShape{calibrated?5:2,3,id=="singleton"?1:ranged?8:4,5,7,rgb?3:1};
  const auto policy=ieee?FloatingSamplePolicy::PreserveIEEEFloat32:FloatingSamplePolicy::FiniteOnly;
  Stack images;
  constexpr std::uint32_t special[]{0x7f800001,0xffc01234,0x80000000,0x7f800000,0xff800000,0x3f800000};
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z) {
    Image image(shape.x,shape.y,shape.s,rgb?PixelType::UInt8:legacy?PixelType::UInt16:PixelType::Float32);
    for(int y=0;y<shape.y;++y)for(int x=0;x<shape.x;++x)for(int s=0;s<shape.s;++s)
      image.at(x,y,s)=ieee?float32_source_sample(special[(t+c+z+y+x)%6],policy):
          rgb?(40*t+20*c+5*z+y+x+s)%251:1000*t+100*c+10*z+y+x+.0;
    images.push_back(std::move(image));
  }
  SpatialCalibration calibration;
  calibration.spacing={.5,1.25,2.5};calibration.origin={11,-7,19};
  calibration.unit="um";calibration.frame_interval=.75;calibration.time_origin=2.25;
  if(calibrated) {
    calibration.time_unit=id=="cal-ms" || id=="cal-step" || id=="cal-legacy-step"?"ms":id=="cal-us"?"us":id=="cal-unit" || id=="cal-legacy-unit"?"frame":"s";
    calibration.frame_interval=id=="cal-step" || id=="cal-legacy-step"?25:id=="cal-ms"?50:id=="cal-us"?50000:.05;
  }
  DocumentData data{Dataset::from_planes(shape,std::move(images),calibration,policy)};
  data.metadata.title="Playback source";
  data.selection=Selection("rectangle",{{1,1},{4,3}});
  data.previous_selection=Selection("point",{{2,2}});
  data.rois.push_back({"saved",Selection("point",{{3,2}}),{},{},{},{}});
  auto source=std::make_shared<imagejxx::Document>(std::move(data));
  window.addNativeDocument(source);
  std::shared_ptr<imagejxx::Document> other;
  if(id=="switch" || id=="close") {
    other=std::make_shared<imagejxx::Document>(Dataset::from_stack(Stack{Image(7,5,1,PixelType::UInt8)}),"Other");
    window.addNativeDocument(other);window.setActiveIndex(0);
  }
  auto display=source->snapshot().display();
  display.t=legacy?0:1;display.c=legacy?0:1;display.crosshair={3,2,shape.z>1?1:0};
  if(id=="range-offgrid")display.crosshair[2]=0;
  display.orthogonal=!legacy;display.zoom=1.25;display.planeZoom={1.25,1.75,2.25};display.linkedZoom=false;
  window.setNativeDisplay(display,source->id);
  window.updateSettings({{"animation_fps",60.}},false);window.recording=true;
  const auto original=source->snapshot();
  auto state=[&](bool pixels=false) {
    const auto snap=source->snapshot();const auto &d=snap.data();const auto &v=snap.display();
    Json result{{"source_id",source->id},{"shares_original",snap.shares_data(original)},
        {"data_identity",snap.data_identity()?Json(*snap.data_identity()):Json()},{"dirty",snap.dirty()},{"undo",source->undo_steps()},
        {"redo",source->redo_steps()},{"position",{v.t,v.c,v.crosshair[2]}},{"crosshair",v.crosshair},
        {"display_revision",snap.revisions().display},{"pixels_revision",snap.revisions().pixels},
        {"metadata_revision",snap.revisions().metadata},{"selection_revision",snap.revisions().selection},
        {"workflow",window.workflow},{"results",window.results},{"title",d.metadata.title},
        {"properties",metadataJson(d.metadata.properties)},{"selection",selectionJson(*d.selection)},
        {"previous",selectionJson(*d.previous_selection)},{"roi",selectionJson(d.rois[0].selection)},
        {"zoom",v.zoom},{"plane_zoom",v.planeZoom},{"linked_zoom",v.linkedZoom},
        {"orthogonal",v.orthogonal},{"spacing",d.dataset.calibration().spacing},
        {"origin",d.dataset.calibration().origin},
        {"frame_interval",d.dataset.calibration().frame_interval},{"time_unit",d.dataset.calibration().time_unit}};
    Json ids=Json::array();for(const auto &doc:window.documents())ids.push_back(doc->id);
    result["document_ids"]=ids;result["active_index"]=window.activeIndex();
    if(pixels) {
      Json values=Json::array();
      for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z)
        for(double value:d.dataset.plane({t,c,z})->pixels)
          if(!legacy&&!rgb)values.push_back(float32_source_sample_bits(value,policy));else values.push_back(value);
      result["samples"]=std::move(values);
    }
    return result;
  };
  Json report{{"contract_id","p09s.animation-ui.001"},{"case_id",id},{"context",context},
      {"observation_state","running"},{"acceptance_claim",false},{"shape",shape.dimensions()},
      {"frames",Json::array()},{"controls",Json::array()},{"api",Json::array()},{"before",state(true)}};
  struct Failure {Json &r;std::filesystem::path p;bool done=false;~Failure(){if(!done)try{r["observation_state"]="failed";writeJson((p/"failure.json").u8string(),r);}catch(...){}}}failure{report,root};
  HeadlessInputSession session(window);Json last;
  const auto started=std::chrono::steady_clock::now();std::size_t frames=0;
  auto frame=[&](const Json &events=Json::array(),const char *label="idle") {
    if(++frames>180 || std::chrono::steady_clock::now()-started>std::chrono::seconds(15))throw std::runtime_error("Animation observation bound");
    last=session.frame(events);
    if(last.at("input_frame").at("raw_inputs")!=events)throw std::runtime_error("Animation raw input mismatch");
    report["frames"].push_back({{"label",label},{"events",events},{"source",state()},
        {"rendered",{{"animation",last.at("animation")},{"input_frame",last.at("input_frame")},
          {"view",get(last,"view")},{"orthogonal_planes",get(last,"orthogonal_planes")},
          {"dialog",get(last,"dialog")},
          {"window_error",last.at("window_error")}}}});
    if(!window.error().empty())throw std::runtime_error("Animation window error: "+window.error());
  };
  auto click=[&](const std::string &name,bool disabled=false) {
    const auto control=name.rfind("dialog.",0)==0 ? last.at("dialog").at("controls").at(name.substr(7)) : last.at("host_controls").at(name);
    if(!control.at("id").get<std::uint64_t>() || control.at("enabled").get<bool>()==disabled)
      throw std::runtime_error("Animation control unavailable: "+name);
    report["controls"].push_back({{"name",name},{"control",control},{"disabled_attempt",disabled}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
    for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));frame();
  };
  auto menu=[&] {click("menu.Image");click("menu.Image.Stacks");};
  auto type=[&](const char *field,const char *text) {
    click(std::string("dialog.")+field);
    for(bool down:{true,false}) {
      const auto keys=Json::array({{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}});
      frame(Json::array({{{"type","keys"},{"keys",keys}}}));
    }
    frame(Json::array({{{"type","text"},{"value",text}}}));
    for(bool down:{true,false})frame(Json::array({{{"type","key"},{"name","Enter"},{"down",down}}}));
    frame();
  };
  auto api=[&](const char *label,auto operation) {
    Json item{{"label",label},{"before",state()}};
    try{operation();item["accepted"]=true;}catch(const std::exception &error){item["accepted"]=false;item["error"]=error.what();}
    item["after"]=state();report["api"].push_back(std::move(item));
  };
  frame();frame();frame();menu();
  const std::string axis=id=="c" || id=="range-c"?"c":id=="t" || id=="range-t" || (calibrated && !legacyTime)?"t":"z";
  if(id=="singleton") {
    click("menu.action.animation",true);
    api("singleton_refusal",[&]{window.action("animation",{{"playing",true}});});
  } else {
    if(axis!="z") {click("menu.animation.axis");click("menu.animation.axis."+axis);menu();}
    if(ranged || calibrated) {
      click("menu.action.show_animation_options");
      type("first",axis=="z" && !legacyTime?"2":"1");
      type("last",legacyTime?"5":axis=="z"?"7":axis=="c"?"3":calibrated?"5":"2");
      type("step",(axis=="t" || legacyTime) && id!="cal-step" && id!="cal-legacy-step"?"1":"2");
      if(calibrated && id!="cal-unit" && id!="cal-legacy-unit") {
        click("dialog.timing");click("dialog.timing.choice.Calibrated time");
      }
      if(id=="reverse" || id=="range-c" || id=="ping" || id=="range-t" || id=="cal-legacy-reverse" || id=="cal-legacy-ping") {
        click("dialog.mode");
        click(std::string("dialog.mode.choice.")+(id=="reverse" || id=="range-c" || id=="cal-legacy-reverse"?"Reverse":"Ping-pong"));
      }
      report["options_before_submit"]=last.at("dialog");
      click("dialog.apply");menu();
    }
    click("menu.action.animation");
    while(last.at("animation").at("ticks").get<int>()<3)frame();
    if(id=="switch")api("switch",[&]{window.setActiveIndex(1);});
    else if(id=="close")api("close",[&]{window.closeDocument(source->id,true);});
    else if(id=="edit" || id=="cal-edit" || id=="cal-legacy-edit")api("edit",[&]{auto edit=source->begin_edit();edit.set_property("changed",true);source->commit(edit);});
    else if(id=="stop") {menu();click("menu.action.animation");}
    else if(id=="busy") {
      api("submit",[&]{window.applyOperation("add",{{"value",1}},true,true);});
      api("busy_refusal",[&]{window.action("animation",{{"playing",true}});});
      window.wait();
    } else {
      if(id=="fps")api("fps",[&]{window.updateSettings({{"animation_fps",15.}},false);});
      if(id=="cal-fps")api("fps",[&]{window.updateSettings({{"animation_fps",15.}},false);});
      if(id=="cal-unit" || id=="cal-legacy-unit")api("unknown_unit",[&]{window.action("animation",{{"timing","calibrated_time"}});});
      if(id=="cal-invalid") {
        api("timing_unknown",[&]{window.action("animation",{{"timing","unknown"}});});
        api("timing_type",[&]{window.action("animation",{{"timing",true}});});
        api("timing_axis",[&]{window.action("animation",{{"axis","z"},{"timing","calibrated_time"}});});
      }
      if(id=="z") {
        api("invalid_axis",[&]{window.action("animation",{{"axis","invalid"}});});
        api("invalid_playing",[&]{window.action("animation",{{"playing",1}});});
        api("unknown_parameter",[&]{window.action("animation",{{"unused",true}});});
      }
      if(id=="range-invalid") {
        api("range_negative",[&]{window.action("animation",{{"first",-1}});});
        api("range_outside",[&]{window.action("animation",{{"last",8}});});
        api("range_zero_step",[&]{window.action("animation",{{"step",0}});});
        api("range_singleton",[&]{window.action("animation",{{"first",1},{"last",1}});});
        api("range_mode",[&]{window.action("animation",{{"mode","unknown"}});});
      }
      const int extent=axis=="z"?shape.z:axis=="c"?shape.c:shape.t;
      while(last.at("animation").at("ticks").get<int>()<(ranged || calibrated?8:extent+3))frame();
      window.action("animation",{{"playing",false}});
    }
    for(int i=0;i<5;++i)frame(Json::array(),"after_stop");
  }
  report["after"]=state(true);report["animation"]=last.at("animation");
  report["frame_count"]=frames;report["elapsed_seconds"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
  session.close();report["lifecycle"]=session.lifecycle();report["observation_state"]="observed";
  writeJson((root/"case.json").u8string(),report);failure.done=true;return report;
}
}
