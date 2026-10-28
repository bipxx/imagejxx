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
  click(last.at("dialog").at("controls").at("apply"),"snapshot_axes_apply");frame();frame();
  report["independent_views"]={{"first",snapshot(first)},{"second",snapshot(second)}};
  report["after_source_close"]={{"first",snapshot(first)},{"second",snapshot(second)},{"live",last.at("fit_plot")}};
  report["memory_source_closed"]=window.renderMemoryInfo();
  click(last.at("host_controls").at("plot_snapshot."+std::to_string(second)+".export"),"snapshot_export");
  click(last.at("file_browser").at("controls").at("path"),"snapshot_export_path");
  for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}}}}}));
  const auto csv=directory/fs::u8path("snapshot-\xc2\xb5.csv");
  frame(Json::array({{{"type","text"},{"value",csv.u8string()}}}));frame();
  click(last.at("file_browser").at("controls").at("submit"),"snapshot_export_save");frame();frame();
  const auto bytes=readBytes(csv.u8string());report["csv_path"]=csv.u8string();report["csv_text"]=std::string(bytes.begin(),bytes.end());
  click(last.at("host_controls").at("plot_snapshot."+std::to_string(second)+".close"),"close_second");frame();frame();
  report["after_second_close"]=window.plotSnapshotsInfo();report["memory_one_left"]=window.renderMemoryInfo();
  click(last.at("host_controls").at("plot_snapshot."+std::to_string(first)+".close"),"close_first");frame();frame();
  report["final_info"]=window.plotSnapshotsInfo();report["memory_final"]=window.renderMemoryInfo();
  session.close();report["lifecycle"]=session.lifecycle();report["error"]=window.error();report["state"]="observed";
  if(report.dump().size()>=4*1024*1024)throw std::runtime_error("Snapshot probe report byte bound");
  writeJson((directory/"case.json").u8string(),report);failure.done=true;return report;
}
} // namespace imagejxx::desktop
