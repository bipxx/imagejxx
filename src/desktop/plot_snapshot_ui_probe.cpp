#include "plot_snapshot_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <imagejxx/imagej_intensity_calibration.hpp>
#include <filesystem>

namespace imagejxx::desktop {
Json plotSnapshotUiCase(Window& window,const std::string& context,const std::string& output) {
  namespace fs=std::filesystem;
  const auto directory=fs::absolute(fs::u8path(output));
  if(fs::exists(directory))throw std::invalid_argument("Snapshot probe output must be fresh");
  fs::create_directories(directory);
  Image image(3,1,1,PixelType::UInt16);image.pixels={0,32768,65535};
  DocumentData data{Dataset::from_planes({1,1,1,1,3,1},Stack{image}),{},{},{},{},{},{}};
  data.metadata.title="Snapshot source \xc2\xb5 #1";
  auto source=std::make_shared<imagejxx::Document>(std::move(data));window.addNativeDocument(source);
  const auto calibration=ImageJIntensityCalibration::signed16("HU snapshot");
  window.action("set_intensity_calibration",{{"record",metadataJson(calibration.record())},{"show_plot",true}});
  Json report{{"contract","calibration-plot-snapshots-ui.001"},{"context",context},{"acceptance_claim",false},
      {"frames",Json::array()},{"controls",Json::array()},
      {"fixture_actions",{"source attachment and calibration","source metadata edit","source closure"}}};
  struct Failure {Json& report;const fs::path& directory;bool done=false;~Failure(){if(!done)try{writeJson((directory/"failure.json").u8string(),report);}catch(...){}}} failure{report,directory};
  HeadlessInputSession session(window);Json last;
  auto frame=[&](const Json& events=Json::array()) {
    if(report["frames"].size()>=192)throw std::runtime_error("Snapshot probe frame bound");
    last=session.frame(events);
    report["frames"].push_back({{"events",events},{"input_frame",last.at("input_frame")},{"dialog",last.at("dialog")},{"error_modal",last.at("error_modal")}});
  };
  auto click=[&](Json control,const std::string& name) {
    if(!control.at("enabled").get<bool>() || control.at("id").get<std::uint64_t>()==0)throw std::runtime_error("Snapshot control unavailable: "+name);
    report["controls"].push_back({{"name",name},{"control",control}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));frame();
  };
  auto snapshot=[&](std::uint64_t id) {
    for(const auto& row:last.at("plot_snapshots"))if(row.at("plot_id")==id)return row.at("plot");
    throw std::runtime_error("Snapshot observation unavailable");
  };
  auto editText=[&](const char* name,const char* value) {
    click(last.at("dialog").at("controls").at(name),name);
    for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}}}}}));
    frame(Json::array({{{"type","text"},{"value",value}}}));frame();
  };
  for(int i=0;i<4;++i)frame();
  report["live"]=last.at("fit_plot");report["memory_before"]=window.renderMemoryInfo();
  const auto original=source->snapshot();const auto undo=source->undo_steps();
  click(last.at("host_controls").at("fit_plot.snapshot"),"keep_snapshot");frame();frame();
  report["first_info"]=window.plotSnapshotsInfo();const auto first=report["first_info"][0]["plot_id"].get<std::uint64_t>();
  report["memory_first"]=window.renderMemoryInfo();report["first"]=snapshot(first);
  click(last.at("host_controls").at("plot_snapshot."+std::to_string(first)+".snapshot"),"duplicate_snapshot");frame();frame();
  report["second_info"]=window.plotSnapshotsInfo();const auto second=report["second_info"][1]["plot_id"].get<std::uint64_t>();
  report["memory_second"]=window.renderMemoryInfo();
  report["source_unchanged_after_keep"]=source->snapshot().shares_data(original) && source->snapshot().revisions().matches(original.revisions(),Change::All) && source->undo_steps()==undo;
  set_imagej_intensity_calibration(*source,ImageJIntensityCalibration::signed16("edited source"));
  auto renamed=source->begin_edit();renamed.set_title("Renamed source");source->commit(renamed);frame();frame();
  report["after_source_edit"]={{"first",snapshot(first)},{"second",snapshot(second)},{"live",last.at("fit_plot")}};
  std::weak_ptr<imagejxx::Document> weakSource=source;
  window.closeDocument(source->snapshot().id(),true);source.reset();frame();frame();
  report["source_document_released"]=weakSource.expired();
  click(last.at("host_controls").at("plot_snapshot."+std::to_string(second)+".axes"),"snapshot_axes");
  editText("x_min","100");editText("x_max","200");editText("y_min","-50");editText("y_max","50");
  click(last.at("dialog").at("controls").at("nice_ticks"),"snapshot_nice_ticks");
  click(last.at("dialog").at("controls").at("minor_ticks"),"snapshot_minor_ticks");
  click(last.at("dialog").at("controls").at("apply"),"snapshot_axes_apply");frame();frame();
  report["independent_views"]={{"first",snapshot(first)},{"second",snapshot(second)}};
  click(last.at("host_controls").at("plot_snapshot."+std::to_string(second)+".style"),"snapshot_style");
  click(last.at("dialog").at("controls").at("annotation_visible"),"snapshot_model_visible");
  editText("line_width","3");editText("point_radius","5");editText("font_scale","1.2");
  editText("annotation_x","0.75");editText("annotation_y","0.25");editText("curve_color","#00ff00");
  click(last.at("dialog").at("controls").at("apply"),"snapshot_style_apply");frame();frame();
  report["styled"]={{"first",snapshot(first)},{"second",snapshot(second)}};
  report["after_source_close"]={{"first",snapshot(first)},{"second",snapshot(second)},{"live",last.at("fit_plot")}};
  report["memory_source_closed"]=window.renderMemoryInfo();
  click(last.at("host_controls").at("plot_snapshot."+std::to_string(second)+".export"),"snapshot_export");
  click(last.at("file_browser").at("controls").at("path"),"snapshot_export_path");
  for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}}}}}));
  const auto csv=directory/fs::u8path("snapshot-\xc2\xb5.csv");
  frame(Json::array({{{"type","text"},{"value",csv.u8string()}}}));frame();
  click(last.at("file_browser").at("controls").at("submit"),"snapshot_export_save");frame();frame();
  const auto bytes=readBytes(csv.u8string());report["csv_path"]=csv.u8string();report["csv_text"]=std::string(bytes.begin(),bytes.end());
  click(last.at("host_controls").at("plot_snapshot."+std::to_string(second)+".image_export"),"snapshot_image_export");
  click(last.at("dialog").at("controls").at("apply"),"snapshot_image_dimensions_apply");frame();frame();
  click(last.at("file_browser").at("controls").at("path"),"snapshot_image_path");
  for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}}}}}));
  const auto png=directory/fs::u8path("plot-\xc2\xb5.png");
  frame(Json::array({{{"type","text"},{"value",png.u8string()}}}));frame();
  click(last.at("file_browser").at("controls").at("submit"),"snapshot_image_save");frame();frame();
  report["png_export"]=last.at("plot_image_export");report["png_path"]=png.u8string();
  click(last.at("host_controls").at("plot_snapshot."+std::to_string(second)+".close"),"close_second");frame();frame();
  report["after_second_close"]=window.plotSnapshotsInfo();report["memory_one_left"]=window.renderMemoryInfo();
  click(last.at("host_controls").at("plot_snapshot."+std::to_string(first)+".close"),"close_first");frame();frame();
  report["final_info"]=window.plotSnapshotsInfo();report["memory_final"]=window.renderMemoryInfo();
  session.close();report["lifecycle"]=session.lifecycle();report["error"]=window.error();report["state"]="observed";
  if(report.dump().size()>=4*1024*1024)throw std::runtime_error("Snapshot probe report byte bound");
  writeJson((directory/"case.json").u8string(),report);failure.done=true;return report;
}
Json plotWorkspaceUiCase(Window& w,const std::string& context,const std::string& output) {
  namespace fs=std::filesystem;const auto dir=fs::absolute(fs::u8path(output));
  if(fs::exists(dir))throw std::invalid_argument("Plot workspace capture must be fresh");fs::create_directories(dir);
  Json report={{"contract","retained-plot-workspace-ui.001"},{"context",context},{"acceptance_claim",false},{"frames",Json::array()},{"controls",Json::array()},{"fixture_actions",{"source attachment/calibration/fit/profile/histogram calculations","source closure; workspace contains no images","CSV export readers"}}};
  HeadlessInputSession session(w);Json last;
  auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
  auto frame=[&](const Json& inputs=Json::array()) {
    require(report["frames"].size()<400,"Plot workspace frame bound");last=session.frame(inputs);
    require(last.at("input_frame").at("raw_inputs")==inputs,"Plot workspace raw input differs");
    report["frames"].push_back({{"events",inputs},{"input_frame",last.at("input_frame")}});
  };
  auto click=[&](Json control,const std::string& name) {
    require(control.at("enabled").get<bool>() && control.at("id").get<std::uint64_t>()!=0,"Plot workspace control unavailable");report["controls"].push_back({{"name",name},{"control",control}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
    for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));frame();
  };
  auto type=[&](const std::string& value){for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}}}}}));frame(Json::array({{{"type","text"},{"value",value}}}));frame();};
  auto settle=[&]{frame();frame();};
  auto file=[&](const fs::path& path){click(last.at("file_browser").at("controls").at("path"),"file_path");type(path.u8string());click(last.at("file_browser").at("controls").at("submit"),"file_submit");settle();};
  auto csvs=[&](const Json& infos,const std::string& prefix){Json out=Json::array();for(std::size_t i=0;i<infos.size();++i){const auto path=dir/(prefix+std::to_string(i)+".csv");w.action("export_plot_csv",{{"plot_id",infos[i].at("plot_id")},{"path",path.u8string()}});const auto bytes=readBytes(path.u8string());out.push_back(std::string(bytes.begin(),bytes.end()));}return out;};
  try {
    Image image(3,1,1,PixelType::UInt16);image.pixels={0,32768,65535};auto source=std::make_shared<imagejxx::Document>(Dataset::from_stack({image}),"Workspace source");w.addNativeDocument(source);
    w.action("set_intensity_calibration",{{"record",metadataJson(ImageJIntensityCalibration::signed16("HU workspace").record())},{"show_plot",true}});for(int i=0;i<4;++i)frame();
    click(last.at("host_controls").at("fit_plot.snapshot"),"keep_table");settle();
    const auto first=w.plotSnapshotsInfo()[0].at("plot_id").get<std::uint64_t>();
    click(last.at("host_controls").at("plot_snapshot."+std::to_string(first)+".snapshot"),"duplicate_table");settle();
    const auto second=w.plotSnapshotsInfo()[1].at("plot_id").get<std::uint64_t>();
    click(last.at("host_controls").at("plot_snapshot."+std::to_string(second)+".style"),"table_style");
    click(last.at("dialog").at("controls").at("line_width"),"style_width");type("3");click(last.at("dialog").at("controls").at("curve_color"),"style_color");type("#00ff00");click(last.at("dialog").at("controls").at("apply"),"style_apply");settle();
    w.action("set_plot_limits",{{"plot_id",second},{"limits",{100,200,-50,50}},{"nice_ticks",true},{"minor_ticks",true}});
    w.action("fit_intensity_calibration",{{"function","straight_line"},{"measured",{0,1}},{"known",{2,4}},{"show_plot",true}});w.wait();settle();
    click(last.at("host_controls").at("fit_plot.snapshot"),"keep_fit");settle();
    source->select(Selection("line",{{0,0},{2,0}}));w.action("profile");w.wait();settle();click(last.at("host_controls").at("plot.snapshot"),"keep_profile");settle();
    w.action("select_none");w.action("histogram",{{"bins",4}});settle();click(last.at("host_controls").at("plot.snapshot"),"keep_histogram");settle();
    const auto before=source->snapshot();const auto history=source->undo_steps();
    report["before_info"]=w.plotSnapshotsInfo();report["before_csv"]=csvs(report["before_info"],"before");report["before_plots"]=last.at("plot_snapshots");
    require(report["before_info"].size()==5,"Five snapshots not kept");require(report["before_info"][0].at("payload_id")==report["before_info"][1].at("payload_id"),"Duplicate not shared");
    report["read_only_save_source"]=before.shares_data(source->snapshot()) && history==source->undo_steps();
    std::weak_ptr<imagejxx::Document> weak=source;w.closeDocument(source->snapshot().id(),true);source.reset();settle();report["source_released"]=weak.expired();
    click(last.at("host_controls").at("menu.File"),"file_menu_save");click(last.at("host_controls").at("menu.path.save_workspace"),"save_workspace");const auto path=dir/"plots-only.ijx";file(path);report["workspace_path"]=path.u8string();
    for(std::size_t i=5;i>0;--i){const auto id=report["before_info"][i-1].at("plot_id").get<std::uint64_t>();click(last.at("host_controls").at("plot_snapshot."+std::to_string(id)+".close"),"close_before_reopen");settle();}
    require(w.plotSnapshotsInfo().empty(),"Original snapshots not closed");report["memory_closed"]=w.renderMemoryInfo();
    click(last.at("host_controls").at("menu.File"),"file_menu_open");click(last.at("host_controls").at("menu.file.open"),"open_workspace");file(path);
    report["after_info"]=w.plotSnapshotsInfo();report["after_plots"]=last.at("plot_snapshots");report["after_csv"]=csvs(report["after_info"],"after");report["document_count"]=w.documents().size();report["memory_open"]=w.renderMemoryInfo();
    require(report["after_info"].size()==5 && w.documents().empty(),"Plot-only workspace not restored");require(report["before_csv"]==report["after_csv"],"Reopened plot CSV differs");
    require(report["after_info"][0].at("payload_id")==report["after_info"][1].at("payload_id"),"Restored payload sharing lost");
    for(std::size_t i=5;i>0;--i){const auto id=report["after_info"][i-1].at("plot_id").get<std::uint64_t>();click(last.at("host_controls").at("plot_snapshot."+std::to_string(id)+".close"),"close_restored");settle();}
    report["memory_final"]=w.renderMemoryInfo();report["final_info"]=w.plotSnapshotsInfo();report["error"]=w.error();report["state"]="observed";session.close();report["lifecycle"]=session.lifecycle();writeJson((dir/"case.json").u8string(),report);return report;
  }catch(const std::exception& error){w.cancel();w.wait();writeJson((dir/"failure.json").u8string(),{{"error",error.what()},{"report",report},{"last",last}});throw;}
}
} // namespace imagejxx::desktop
