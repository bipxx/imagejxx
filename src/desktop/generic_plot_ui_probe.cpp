#include "generic_plot_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <filesystem>

namespace imagejxx::desktop {
Json genericPlotUiCase(Window& window,const std::string& context,const std::string& output) {
  namespace fs=std::filesystem;const auto directory=fs::absolute(fs::u8path(output));
  if(fs::exists(directory))throw std::invalid_argument("Generic plot probe output must be fresh");fs::create_directories(directory);
  Image image(3,3,1,PixelType::UInt8);image.pixels={0,10,20,30,40,50,60,70,80};
  SpatialCalibration calibration;calibration.spacing={2,3,1};calibration.unit="um";
  DocumentData data{Dataset::from_planes({1,1,1,3,3,1},Stack{image}).with_calibration(calibration),{},{},{},{},{},{}};
  data.metadata.title="Generic source";data.selection=Selection("line",{{0,0},{2,0}});
  auto source=std::make_shared<imagejxx::Document>(std::move(data));window.addNativeDocument(source);
  Json report{{"contract","generic-plot-ui.001"},{"context",context},{"acceptance_claim",false},{"frames",Json::array()},{"controls",Json::array()},
    {"fixture_actions",{"source attachment and calibrated line selection","legacy profile/histogram commands","clear selection","scientific histogram configuration/calculation","source closure and histogram panel closure","profile reversed/log view diagnostics"}}};
  struct Failure {Json& report;const fs::path& directory;bool done=false;~Failure(){if(!done)try{writeJson((directory/"failure.json").u8string(),report);}catch(...){}}} failure{report,directory};
  HeadlessInputSession session(window);Json last;
  auto frame=[&](const Json& events=Json::array()) {
    if(report["frames"].size()>=160)throw std::runtime_error("Generic plot probe frame bound");
    last=session.frame(events);report["frames"].push_back({{"events",events},{"input_frame",last.at("input_frame")},{"error_modal",last.at("error_modal")}});
  };
  auto click=[&](const Json& control,const std::string& name) {
    if(!control.at("enabled").get<bool>() || control.at("id").get<std::uint64_t>()==0)throw std::runtime_error("Generic plot control unavailable: "+name);
    report["controls"].push_back({{"name",name},{"control",control}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));frame();
  };
  auto text=[&](const std::string& value) {
    for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}}}}}));
    frame(Json::array({{{"type","text"},{"value",value}}}));frame();
  };
  auto snapshot=[&](std::uint64_t id) {for(const auto& row:last.at("plot_snapshots"))if(row.at("plot_id")==id)return row.at("plot");throw std::runtime_error("Generic snapshot unavailable");};
  auto control=[&](std::uint64_t id,const char* suffix){return last.at("host_controls").at("plot_snapshot."+std::to_string(id)+"."+suffix);};
  auto hover=[&](std::uint64_t id,double xFraction,double yFraction) {
    const auto plot=snapshot(id);const auto origin=plot.at("canvas_origin"),size=plot.at("canvas_size");
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",{origin[0].get<double>()+xFraction*size[0].get<double>(),origin[1].get<double>()+yFraction*size[1].get<double>()}}}}));frame();
    return snapshot(id).at("cursor");
  };
  const auto before=source->snapshot();const auto undo=source->undo_steps();
  window.action("profile");for(int i=0;i<4;++i)frame();report["profile"]=last.at("plot");
  report["producer_source_unchanged"]=source->snapshot().shares_data(before) && source->undo_steps()==undo;
  click(last.at("host_controls").at("plot.snapshot"),"keep_profile");frame();frame();
  const auto profile=window.plotSnapshotsInfo()[0]["plot_id"].get<std::uint64_t>();
  window.action("select_none");window.action("histogram",{{"bins",4}});for(int i=0;i<4;++i)frame();report["legacy_histogram"]=last.at("plot");
  click(last.at("host_controls").at("plot.snapshot"),"keep_histogram");frame();frame();
  const auto legacy=window.plotSnapshotsInfo()[1]["plot_id"].get<std::uint64_t>();
  window.action("show_histogram");window.action("configure_histogram",{{"custom",true},{"bins","4"},{"lower","0"},{"upper","80"},{"roi",false}});
  window.action("calculate_histogram");window.action("wait_histogram");for(int i=0;i<4;++i)frame();
  report["scientific_result"]=last.at("histogram_panel").at("result");
  auto button=last.at("histogram_panel").at("control_states").at("interactive_plot");button["point"]=last.at("histogram_panel").at("controls").at("interactive_plot");
  click(button,"open_scientific_plot");frame();frame();
  const auto scientific=window.plotSnapshotsInfo()[2]["plot_id"].get<std::uint64_t>();
  report["scientific_plot"]=snapshot(scientific);report["ids"]={profile,legacy,scientific};
  std::weak_ptr<imagejxx::Document> weak=source;window.closeDocument(source->id,true);window.action("close_histogram");source.reset();frame();frame();
  report["source_released"]=weak.expired();report["after_source_close"]={{"profile",snapshot(profile)},{"legacy",snapshot(legacy)},{"scientific",snapshot(scientific)}};
  report["histogram_hover"]=hover(scientific,.375,.5);
  click(control(scientific,"export"),"scientific_csv");click(last.at("file_browser").at("controls").at("path"),"csv_path");
  const auto csv=directory/fs::u8path("histogram-\xc2\xb5.csv");text(csv.u8string());click(last.at("file_browser").at("controls").at("submit"),"csv_save");frame();frame();
  report["csv_path"]=csv.u8string();const auto csvBytes=readBytes(csv.u8string());report["csv_text"]=std::string(csvBytes.begin(),csvBytes.end());
  click(control(scientific,"close"),"close_scientific");frame();frame();click(control(legacy,"close"),"close_legacy");frame();frame();
  report["profile_hover"]=hover(profile,.5,.5);
  window.action("set_plot_limits",{{"plot_id",profile},{"limits",{4,1,20,1}},{"log_x",true},{"log_y",true}});frame();frame();
  report["profile_log_reversed_hover"]=hover(profile,.5,.5);
  window.action("set_plot_limits",{{"plot_id",profile},{"limits",{0,4,0,20}},{"log_x",false},{"log_y",false}});frame();frame();
  click(control(profile,"style"),"profile_style");report["style_schema"]=last.at("dialog").at("schema");
  click(last.at("dialog").at("controls").at("line_width"),"profile_width");text("3");
  click(last.at("dialog").at("controls").at("curve_color"),"profile_color");text("#00ff00");
  click(last.at("dialog").at("controls").at("apply"),"profile_style_apply");frame();frame();
  click(control(profile,"image_export"),"profile_png");click(last.at("dialog").at("controls").at("apply"),"png_dimensions");frame();frame();
  click(last.at("file_browser").at("controls").at("path"),"png_path");const auto png=directory/fs::u8path("profile-\xc2\xb5.png");text(png.u8string());
  click(last.at("file_browser").at("controls").at("submit"),"png_save");frame();frame();report["png_path"]=png.u8string();report["png_export"]=last.at("plot_image_export");
  click(control(profile,"close"),"close_profile");frame();frame();report["memory_final"]=window.renderMemoryInfo();report["final_info"]=window.plotSnapshotsInfo();
  session.close();report["error"]=window.error();report["state"]="observed";
  if(report.dump().size()>=4*1024*1024)throw std::runtime_error("Generic plot report byte bound");
  writeJson((directory/"case.json").u8string(),report);failure.done=true;return report;
}
} // namespace imagejxx::desktop
