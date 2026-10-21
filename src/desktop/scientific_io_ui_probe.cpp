#include "scientific_io_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <chrono>
#include <fstream>
#include <thread>
namespace imagejxx::desktop {
Json scientificTiffUiCase(Window &w, const std::string &context,
                          const std::string &directory,
                          const std::string &fixture) {
#ifndef IMAGEJXX_HAS_SCIENTIFIC_IO
  throw std::invalid_argument(
      "Build scientific TIFF UI with IMAGEJXX_BUILD_IO=ON");
#else
  namespace fs = std::filesystem;
  auto require = [](bool value, const std::string &message) {
    if (!value)
      throw std::runtime_error(message);
  };
  const auto dir = fs::absolute(fs::u8path(directory));
  require(!fs::exists(dir) && w.documents().empty(),
          "Fresh scientific TIFF UI case required");
  fs::create_directories(dir);
  fs::copy_file(fs::u8path(fixture), dir / "input.tif");
  w.recording = true;
  Json report = {{"contract", "p06r.scientific-tiff.ui.001"},
                 {"context", context},
                 {"directory", dir.u8string()},
                 {"acceptance_claim", false},
                 {"frames", Json::array()},
                 {"controls", Json::array()}};
  struct Guard {
    Json &report;
    fs::path dir;
    bool done = false;
    ~Guard() {
      if (!done)
        try {
          report["state"] = "failed";
          writeJson((dir / "failure.json").u8string(), report);
        } catch (...) {
        }
    }
  } guard{report, dir};
  {
    // Exercise the actual startup form in a fresh Window before creating the
    // ordinary-open session below. Both installed hosts run this producer.
    const auto startupDir = dir / "startup001";
    fs::create_directory(startupDir);
    fs::copy_file(fs::u8path(fixture), startupDir / "first.tif");
    fs::copy_file(fs::u8path(fixture), startupDir / "last.tif");
    Window startup((startupDir / "preferences.json").u8string());
    StartupRequest request;
    request.explicit_paths = {(startupDir / "first.tif").u8string(),
                              (startupDir / "last.tif").u8string()};
    startup.startStartup(request);
    auto &observed = report["startup_review"];
    observed = {{"frames", Json::array()}, {"controls", Json::array()}};
    HeadlessInputSession startupSession(startup);
    Json current;
    auto frameStartup = [&](const Json &events = Json::array()) {
      require(observed["frames"].size() < 400, "Startup frame bound");
      current = startupSession.frame(events);
      observed["frames"].push_back(
          {{"events", events},
           {"input_frame", current.at("input_frame")},
           {"scientific_io", current.at("scientific_io")},
           {"startup", startup.startupState()}});
    };
    auto settleStartup = [&] {
      const auto until =
          std::chrono::steady_clock::now() + std::chrono::seconds(20);
      while (current.at("scientific_io").at("working").get<bool>()) {
        require(std::chrono::steady_clock::now() < until,
                "Startup worker timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        frameStartup();
      }
      frameStartup();
      require(
          current.at("scientific_io").at("error").get<std::string>().empty(),
          "Startup worker error: " + current.at("scientific_io").dump());
    };
    auto pressStartup = [&](const std::string &name) {
      const auto hit = current.at("scientific_io").at("controls").at(name);
      require(hit.at("enabled").get<bool>() &&
                  hit.at("id").get<std::uint64_t>(),
              "Startup control unavailable: " + name);
      observed["controls"].push_back({{"name", name}, {"control", hit}});
      frameStartup(Json::array({{{"type", "position"},
                                 {"space", "screen"},
                                 {"point", hit.at("point")}}}));
      for (bool down : {true, false})
        frameStartup(Json::array(
            {{{"type", "button"}, {"name", "left"}, {"down", down}}}));
      frameStartup();
    };
    frameStartup();
    frameStartup();
    settleStartup();
    observed["first_review"] = current.at("scientific_io");
    require(startup.documents().empty(), "Startup published before consent");
    pressStartup("confirm_axes");
    pressStartup("apply");
    settleStartup();
    observed["documents_after_first"] = startup.documents().size();
    observed["second_review"] = current.at("scientific_io");
    require(startup.documents().empty() &&
                !current.at("scientific_io").at("confirm_axes").get<bool>(),
            "Startup batch lost atomicity or fresh consent");
    pressStartup("confirm_axes");
    pressStartup("apply");
    settleStartup();
    observed["startup"] = startup.startupState();
    observed["shapes"] = Json::array();
    for (const auto &document : startup.documents())
      observed["shapes"].push_back(
          document->nativeSnapshot().data().dataset.shape().dimensions());
    observed["workflow"] = startup.workflow;
    require(startup.documents().size() == 2 &&
                startup.startupState().at("status") == "loaded",
            "Startup batch did not attach both owners");
    startupSession.close();
    startup.run({true, 4, (startupDir / "screen.png").u8string()});
  }
  {
    const auto mixedDir = dir / "mixed001";
    fs::create_directory(mixedDir);
    Window mixed((mixedDir / "preferences.json").u8string());
    StartupRequest request;
    request.restore_workspace = StartupOverride::Suppress;
    mixed.startStartup(request);
    Image pixels(5, 3, 1, PixelType::UInt8);
    pixels.pixels.assign(15, 11);
    Document input({pixels}, "captured append input");
    const auto document = documentJson(input);
    mixed.action("queue_launch",
                 {{"items", Json::array({{{"document", document}},
                                         {{"path", fixture}},
                                         {{"document", document}}})}});
    auto &observed = report["mixed_review"];
    observed = {{"frames", Json::array()}, {"controls", Json::array()}};
    HeadlessInputSession mixedSession(mixed);
    Json current;
    auto frameMixed = [&](const Json &events = Json::array()) {
      require(observed["frames"].size() < 400, "Mixed launch frame bound");
      current = mixedSession.frame(events);
      observed["frames"].push_back(
          {{"events", events},
           {"input_frame", current.at("input_frame")},
           {"scientific_io", current.at("scientific_io")},
           {"startup", mixed.startupState()}});
    };
    auto settleMixed = [&] {
      const auto until =
          std::chrono::steady_clock::now() + std::chrono::seconds(20);
      while (current.at("scientific_io").at("working").get<bool>()) {
        require(std::chrono::steady_clock::now() < until,
                "Mixed launch worker timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        frameMixed();
      }
      frameMixed();
      require(
          current.at("scientific_io").at("error").get<std::string>().empty(),
          "Mixed launch IO error");
    };
    auto pressMixed = [&](const std::string &name) {
      const auto hit = current.at("scientific_io").at("controls").at(name);
      require(hit.at("enabled").get<bool>() &&
                  hit.at("id").get<std::uint64_t>(),
              "Mixed launch control unavailable: " + name);
      observed["controls"].push_back({{"name", name}, {"control", hit}});
      frameMixed(Json::array({{{"type", "position"},
                               {"space", "screen"},
                               {"point", hit.at("point")}}}));
      for (bool down : {true, false})
        frameMixed(Json::array(
            {{{"type", "button"}, {"name", "left"}, {"down", down}}}));
      frameMixed();
    };
    frameMixed();
    frameMixed();
    settleMixed();
    observed["before"] = mixed.startupState().at("append");
    observed["documents_before_review"] = mixed.documents().size();
    require(mixed.documents().size() == 1,
            "Mixed launch published a suffix before review");
    pressMixed("confirm_axes");
    pressMixed("apply");
    settleMixed();
    observed["after"] = mixed.startupState().at("append");
    observed["shapes"] = Json::array();
    for (const auto &owner : mixed.documents())
      observed["shapes"].push_back(
          owner->nativeSnapshot().data().dataset.shape().dimensions());
    require(mixed.documents().size() == 3 &&
                mixed.startupState().at("append").at("status") == "completed",
            "Mixed launch did not resume ordered append");
    observed["workflow"] = mixed.workflow;
    mixedSession.close();
    mixed.run({true, 4, (mixedDir / "screen.png").u8string()});
  }
  auto session = std::make_unique<HeadlessInputSession>(w);
  Json last;
  auto frame = [&](const Json &events = Json::array()) {
    require(report["frames"].size() < 1200, "Scientific TIFF frame bound");
    last = session->frame(events);
    require(last.at("input_frame").at("raw_inputs") == events,
            "Scientific TIFF raw inputs differ");
    Json menu = Json::object();
    for (const auto *key :
         {"menu.File", "menu.file.open", "menu.file.scientific_open",
          "menu.file.scientific_export"})
      if (last.at("host_controls").contains(key))
        menu[key] = last.at("host_controls").at(key);
    report["frames"].push_back({{"events", events},
                                {"rendered",
                                 {{"input_frame", last.at("input_frame")},
                                  {"scientific_io", last.at("scientific_io")},
                                  {"file_browser", last.at("file_browser")},
                                  {"host_controls", menu}}}});
    if (last.contains("orthogonal_planes"))
      report["frames"].back()["rendered"]["orthogonal_planes"] =
          last.at("orthogonal_planes");
  };
  auto panel = [&] { return last.at("scientific_io"); };
  auto click = [&](const Json &hit, const std::string &name) {
    require(hit.at("enabled").get<bool>() && hit.at("id").get<std::uint64_t>(),
            "Missing/enabled scientific TIFF control: " + name);
    report["controls"].push_back({{"name", name}, {"control", hit}});
    frame(Json::array({{{"type", "position"},
                        {"space", "screen"},
                        {"point", hit.at("point")}}}));
    for (bool down : {true, false})
      frame(Json::array(
          {{{"type", "button"}, {"name", "left"}, {"down", down}}}));
    frame();
  };
  auto press = [&](const std::string &name) {
    click(panel().at("controls").at(name), name);
  };
  auto type = [&](const std::string &name, const std::string &text) {
    press(name);
    for (bool down : {true, false})
      frame(Json::array({{{"type", "keys"},
                          {"keys",
                           {{{"name", "Ctrl"}, {"down", down}},
                            {{"name", "A"}, {"down", down}}}}}}));
    frame(Json::array({{{"type", "text"}, {"value", text}}}));
    frame();
  };
  auto settle = [&] {
    const auto until =
        std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (panel().at("working").get<bool>()) {
      require(std::chrono::steady_clock::now() < until,
              "Scientific TIFF worker timeout");
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
      frame();
    }
    frame();
    require(panel().at("error").get<std::string>().empty(),
            "Scientific TIFF worker failed: " + panel().dump());
  };
  auto menu = [&](const std::string &name) {
    frame();
    click(last.at("host_controls").at("menu.File"), "menu.File");
    click(last.at("host_controls").at(name), name);
    require(panel().at("open").get<bool>(),
            "Scientific TIFF form did not open");
  };
  auto state = [&] {
    const auto source = w.nativeSnapshot();
    return Json{{"id", source.id()},
                {"shape", source.data().dataset.shape().dimensions()},
                {"dirty", source.dirty()},
                {"undo", w.nativeDocument()->undo_steps()},
                {"redo", w.nativeDocument()->redo_steps()},
                {"data_identity", source.data_identity()
                                      ? Json(*source.data_identity())
                                      : Json()},
                {"metadata", metadataJson(source.data().metadata.properties)},
                {"workflow", w.workflow}};
  };
  frame();
  frame();
  // Ordinary File > Open must lead to review, never silently flatten this file.
  click(last.at("host_controls").at("menu.File"), "menu.File");
  click(last.at("host_controls").at("menu.file.open"), "menu.file.open");
  click(last.at("file_browser").at("controls").at("path"),
        "ordinary.browser.path");
  for (bool down : {true, false})
    frame(Json::array({{{"type", "keys"},
                        {"keys",
                         {{{"name", "Ctrl"}, {"down", down}},
                          {{"name", "A"}, {"down", down}}}}}}));
  frame(Json::array(
      {{{"type", "text"}, {"value", (dir / "input.tif").u8string()}}}));
  frame();
  click(last.at("file_browser").at("controls").at("submit"),
        "ordinary.browser.submit");
  settle();
  report["ordinary_open_review"] = panel();
  require(w.documents().empty() && !panel().at("confirm_axes").get<bool>(),
          "Ordinary open bypassed review");
  require(!panel().at("controls").at("flat_open").at("enabled").get<bool>(),
          "Flat import enabled without loss consent");
  press("accept_flat");
  require(!panel().at("controls").at("apply").at("enabled").get<bool>(),
          "Flat consent authorized scientific open");
  press("flat_open");
  require(w.documents().size() == 1, "Flat import membership");
  const auto flat = w.nativeSnapshot();
  report["flat_import"] = {
      {"shape", flat.data().dataset.shape().dimensions()},
      {"spacing", flat.data().dataset.calibration().spacing},
      {"unit", flat.data().dataset.calibration().unit},
      {"dirty", flat.dirty()},
      {"workflow", w.workflow}};
  const auto reviewedShape=report.at("ordinary_open_review").at("inspection").at("shape");
  const std::array<int,6> expectedFlat{1,1,reviewedShape[0].get<int>()*reviewedShape[1].get<int>()*reviewedShape[2].get<int>(),
      reviewedShape[3].get<int>(),reviewedShape[4].get<int>(),reviewedShape[5].get<int>()};
  require(flat.data().dataset.shape().dimensions()==expectedFlat,"Explicit flat import mapping");
  w.closeDocument(flat.id(), false);
  frame();
  menu("menu.file.scientific_open");
  type("path", (dir / "input.tif").u8string());
  {
    std::ofstream ignored(dir / "ignored.png", std::ios::binary);
    ignored << "not a TIFF input";
  }
  press("browse");
  report["browser_review"] = last.at("file_browser");
  click(last.at("file_browser").at("controls").at("filter"), "browser.filter");
  for (bool down : {true, false})
    frame(Json::array({{{"type", "keys"},
                        {"keys",
                         {{{"name", "Ctrl"}, {"down", down}},
                          {{"name", "A"}, {"down", down}}}}}}));
  frame(Json::array({{{"type", "text"}, {"value", "input"}}}));
  frame();
  report["browser_filtered"] = last.at("file_browser");
  click(last.at("file_browser").at("controls").at("submit"), "browser.submit");
  press("inspect");
  settle();
  report["review_open"] = panel();
  report["documents_before_open"] = w.documents().size();
  require(!panel().at("controls").at("apply").at("enabled").get<bool>(),
          "Open enabled before axes consent");
  press("confirm_axes");
  press("apply");
  settle();
  require(w.documents().size() == 1, "Scientific TIFF open membership");
  report["opened"] = state();
  const auto source = w.nativeSnapshot();
  Json values = Json::array();
  const auto shape = source.data().dataset.shape();
  for (int t = 0; t < shape.t; ++t)
    for (int c = 0; c < shape.c; ++c)
      for (int z = 0; z < shape.z; ++z) {
        auto image = source.data().dataset.plane({t, c, z});
        values.push_back(image->pixels);
      }
  report["native_planes"] = std::move(values);
  report["calibration"] = {
      {"spacing", source.data().dataset.calibration().spacing},
      {"unit", source.data().dataset.calibration().unit},
      {"frame_interval", source.data().dataset.calibration().frame_interval},
      {"time_unit", source.data().dataset.calibration().time_unit}};
  const bool recording = w.recording;
  w.recording = false;
  w.action("display_range", {{"minimum", 0}, {"maximum", 114}});
  w.recording = recording;
  frame();
  const auto navigation = last.at("orthogonal_navigation");
  click({{"id", navigation.at("toggle_id")},
         {"point", navigation.at("toggle_point")},
         {"enabled", !w.busy()}},
        "linked_views");
  require(w.nativeSnapshot().display().orthogonal,
          "Imported dataset did not enter linked views");
  report["linked_views"] = last.at("orthogonal_planes");
  menu("menu.file.scientific_export");
  type("path", (dir / "output.tif").u8string());
  const auto codec=report.at("review_open").at("compression").get<std::string>();
  const bool tiled=report.at("review_open").at("storage")=="tiled";
  const bool bigtiff=report.at("review_open").at("container")=="bigtiff";
  if(bigtiff){press("container");press("container.bigtiff");report["container_selected"]=panel();require(panel().at("container")=="bigtiff","BigTIFF selection did not update export");}
  if(tiled){press("storage");press("storage.tiled");report["storage_selected"]=panel();require(panel().at("storage")=="tiled","Tile selection did not update export");}
  if(codec!="uncompressed") {
    press("compression");press("compression."+codec);
    report["compression_selected"]=panel();
    require(panel().at("compression")==codec,"Compression selection did not update export");
  }
  const auto prediction=report.at("review_open").at("predictor").get<std::string>();
  if(prediction!="none") {
    press("predictor");press("predictor."+prediction);report["predictor_selected"]=panel();
    require(panel().at("predictor")==prediction,"Prediction selection did not update export");
  }
  press("inspect");
  settle();
  report["review_export"] = panel();
  require(!panel().at("controls").at("apply").at("enabled").get<bool>(),
          "Export enabled before loss consent");
  press("accept_loss");
  press("apply");
  settle();
  report["exported"] = state();
  require(fs::exists(dir / "output.tif"), "Scientific TIFF export absent");
  menu("menu.file.scientific_export");
  type("path", (dir / "canceled.tif").u8string());
  if(bigtiff){press("container");press("container.bigtiff");}
  if(tiled){press("storage");press("storage.tiled");}
  press("inspect");
  settle();
  press("cancel");
  require(!fs::exists(dir / "canceled.tif"), "Canceled form wrote a file");
  report["canceled"] = panel();
  const auto original = readBytes((dir / "output.tif").u8string());
  w.action("review_scientific_io", {{"record", w.workflow.back()}});
  frame();
  report["replay_review"] = panel();
  require(readBytes((dir / "output.tif").u8string()) == original,
          "Replay review wrote a file");
  press("cancel");
  report["after"] = state();
  session->close();
  report["lifecycle"] = session->lifecycle();
  session.reset();
  w.run({true, 4, (dir / "screen.png").u8string()});
  // Hidden captures deliberately avoid automatic preference writes. Exercise
  // the ordinary explicit-save API before checking process-independent reload.
  w.updateSettings(Json::object(), true);
  report["preferences"] = w.preferencesJson();
  Window reloaded((dir.parent_path() / "preferences.json").u8string());
  reloaded.action("show_scientific_open");
  auto reopened = reloaded.renderHeadless(3);
  report["reopened_form"] = reopened.at("scientific_io");
  reloaded.action("cancel_scientific_io");
  report["state"] = "observed";
  report["error"] = w.error();
  writeJson((dir / "case.json").u8string(), report);
  guard.done = true;
  return report;
#endif
}
} // namespace imagejxx::desktop

namespace imagejxx::desktop {
Json float32TiffUiCase(Window &w,const std::string &id,const std::string &context,
                       const std::string &directory,const std::string &fixture) {
#ifndef IMAGEJXX_HAS_SCIENTIFIC_IO
  throw std::invalid_argument("Build scientific IO");
#else
  namespace fs=std::filesystem;
  auto require=[](bool value,const std::string &message){if(!value)throw std::runtime_error(message);};
  require(id>="TF01" && id<="TF06" && (context=="imgui_cpp" || context=="imgui_python"),"Unknown Float32 file case");
  const auto dir=fs::absolute(fs::u8path(directory)).lexically_normal();
  require(dir!=dir.root_path() && !fs::exists(dir) && w.documents().empty(),"Fresh Float32 case required");
  fs::create_directories(dir);fs::copy_file(fs::u8path(fixture),dir/"input.tif");
  w.recording=true;
  Json report={{"contract","p08a.float32-tiff.ui.001"},{"case_id",id},{"context",context},
    {"acceptance_claim",false},{"observation_scope","Actual ImGui inputs and CPU cache/upload-input values; no OS input or GPU equivalence"},
    {"state","running"},{"frames",Json::array()},{"controls",Json::array()},{"states",Json::object()},{"preparation",Json::array()}};
  HeadlessInputSession session(w);Json last;
  const auto started=std::chrono::steady_clock::now();
  auto frame=[&](const std::string &label,const Json &events=Json::array()){
    require(report["frames"].size()<450 && std::chrono::steady_clock::now()-started<std::chrono::seconds(25),"Float32 input bounds");
    last=session.frame(events);require(last.at("input_frame").at("raw_inputs")==events,"Float32 input mismatch");
    Json rendered=Json::object();for(const auto *key:{"input_frame","scientific_io","file_browser","host_controls","rank_panel","gaussian_jobs"})if(last.contains(key))rendered[key]=last.at(key);
    report["frames"].push_back({{"label",label},{"events",events},{"rendered",std::move(rendered)}});
  };
  auto click=[&](const Json &hit,const std::string &name){
    require(hit.at("enabled").get<bool>() && hit.at("id").get<std::uint64_t>(),"Missing Float32 control "+name);
    report["controls"].push_back({{"name",name},{"hit",hit}});
    frame(name+".position",Json::array({{{"type","position"},{"space","screen"},{"point",hit.at("point")}}}));
    for(bool down:{true,false})frame(name+(down?".down":".up"),Json::array({{{"type","button"},{"name","left"},{"down",down}}}));
    frame(name+".settled");
  };
  auto press=[&](const std::string &name){click(last.at("scientific_io").at("controls").at(name),name);};
  auto rankHit=[&](const std::string &name){
    const auto &panel=last.at("rank_panel");auto hit=panel.at("control_states").at(name);
    hit["point"]=panel.at("controls").at(name);hit["rect"]=panel.at("control_rects").at(name);return hit;
  };
  auto text=[&](const Json &hit,const std::string &name,const std::string &value){
    click(hit,name);for(bool down:{true,false})frame(name+".select_all",Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}}}}}));
    frame(name+".text",Json::array({{{"type","text"},{"value",value}}}));frame(name+".settled");
  };
  auto settle=[&](const std::string &label){
    while(last.at("scientific_io").at("working").get<bool>()){std::this_thread::sleep_for(std::chrono::milliseconds(2));frame(label+".wait");}
    frame(label);report["states"][label]=last.at("scientific_io");
  };
  auto hex=[](const Image &image,FloatingSamplePolicy policy){
    std::string raw;constexpr char digits[]="0123456789abcdef";
    for(double v:image.pixels){auto word=float32_source_sample_bits(v,policy);for(int b=0;b<4;++b){auto byte=(word>>(8*b))&255;raw+=digits[byte>>4];raw+=digits[byte&15];}}return raw;
  };
  auto snapshot=[&]{
    const auto v=w.nativeSnapshot();const auto &data=v.data();const auto shape=data.dataset.shape();Json planes=Json::array();
    for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z)planes.push_back(hex(*data.dataset.plane({t,c,z}),data.dataset.sample_policy()));
    return Json{{"shape",shape.dimensions()},{"policy",int(data.dataset.sample_policy())},{"planes",planes},{"dirty",v.dirty()},
      {"undo",w.nativeDocument()->undo_steps()},{"redo",w.nativeDocument()->redo_steps()},
      {"display",metadataJson(v.display().properties)},{"workflow",w.workflow},{"metadata",metadataJson(data.metadata.properties)}};
  };
  auto menu=[&](const std::string &name){click(last.at("host_controls").at("menu.File"),"menu.File");click(last.at("host_controls").at(name),name);};
  auto policy=[&](const std::string &value){press("sample_policy");press("sample_policy."+value);};
  try {
    frame("initial");frame("layout");
    menu("menu.file.open");text(last.at("file_browser").at("controls").at("path"),"browser.path",(dir/"input.tif").u8string());
    click(last.at("file_browser").at("controls").at("submit"),"browser.submit");settle("file.inspected");
    require(w.documents().empty(),"File published before consent");
    const auto codec=last.at("scientific_io").at("compression").get<std::string>();
    const bool compressed=codec!="uncompressed";
    const bool tiled=last.at("scientific_io").at("storage")=="tiled";
    const bool bigtiff=last.at("scientific_io").at("container")=="bigtiff";report["input_container"]=bigtiff?"bigtiff":"classic";
    report["input_storage"]=tiled?"tiled":"strips";
    report["input_compression"]=codec;
    const bool floating_prediction=last.at("scientific_io").at("predictor")=="floating";report["input_predictor"]=floating_prediction?"floating":"none";
    press("confirm_axes");
    if(id=="TF04") {press("apply");settle("file.default_failed");require(w.documents().empty() && !last.at("scientific_io").at("error").get<std::string>().empty(),"Finite IEEE read should fail before publication");}
    if(id=="TF06") {
      fs::rename(dir/"input.tif",dir/"retired-input.tif");fs::copy_file(dir/"retired-input.tif",dir/"input.tif");
      report["preparation"].push_back({{"role","declared API fixture identity replacement; no physical-input credit"}});
    }
    if(id=="TF01") {policy("preserve_ieee_float32");policy("finite_only");}
    else policy(id=="TF02"||id=="TF05"?"preserve_quiet_nan_float32":"preserve_ieee_float32");
    report["states"]["file.policy_changed"]=last.at("scientific_io");
    require(!last.at("scientific_io").at("confirm_axes").get<bool>(),"Policy retained old consent");
    if(id=="TF05") {press("cancel");}
    else {
      press("confirm_axes");press("apply");settle("file.applied");
      if(id=="TF06") {require(w.documents().empty() && !last.at("scientific_io").at("error").get<std::string>().empty(),"Changed input published");press("cancel");}
      else {
        require(w.documents().size()==1,"Float32 input absent");report["states"]["opened"]=snapshot();
        if(compressed) {
          auto toggle=[&]{const auto nav=last.at("orthogonal_navigation");click({{"id",nav.at("toggle_id")},{"point",nav.at("toggle_point")},{"enabled",!w.busy()}},"linked_views");};
          toggle();require(w.nativeSnapshot().display().orthogonal,"Linked Float32 view absent");
          report["linked_views"]=last.at("orthogonal_planes");report["states"]["linked_source"]=snapshot();
          toggle();require(!w.nativeSnapshot().display().orthogonal,"Ordinary Float32 view not restored");
          report["states"]["ordinary_restored"]=snapshot();
        }
        click(last.at("host_controls").at("menu.Process"),"menu.Process");
        click(last.at("host_controls").at("menu.Process.Noise"),"menu.Process.Noise");
        click(last.at("host_controls").at("menu.command.remove_nans_imagej"),"menu.command.remove_nans_imagej");
        text(rankHit("radius"),"rank.radius","1");
        click(rankHit("preview"),"rank.preview");
        while(last.at("gaussian_jobs").at("active_installed_generation").is_null() || last.at("gaussian_jobs").at("active_installed_generation")!=last.at("rank_panel").at("generation")){frame("rank.preview.wait");std::this_thread::yield();}
        report["states"]["preview_source"]=snapshot();report["preview_pixels"]=last.at("ordinary_pixels");
        click(rankHit("ok"),"rank.ok");
        while(w.busy()){frame("rank.apply.wait");std::this_thread::yield();}
        frame("rank.applied");report["states"]["processed"]=snapshot();
        if(w.nativeDocument()->undo_steps()){
          click(last.at("host_controls").at("menu.Edit"),"menu.Edit");click(last.at("host_controls").at("menu.Edit.Undo"),"menu.Edit.Undo");report["states"]["undone"]=snapshot();
          click(last.at("host_controls").at("menu.Edit"),"menu.Edit");click(last.at("host_controls").at("menu.Edit.Redo"),"menu.Edit.Redo");report["states"]["redone"]=snapshot();
        }
        menu("menu.file.scientific_export");text(last.at("scientific_io").at("controls").at("path"),"export.path",(dir/"output.tif").u8string());
        if(bigtiff){press("container");press("container.bigtiff");}
        if(tiled){press("storage");press("storage.tiled");}
        if(compressed){press("compression");press("compression."+codec);}
        if(floating_prediction){press("predictor");press("predictor.floating");}
        press("inspect");settle("export.reviewed");press("accept_loss");press("apply");settle("export.applied");
        report["states"]["exported"]=snapshot();require(fs::exists(dir/"output.tif"),"Float32 output absent");
        menu("menu.file.scientific_export");text(last.at("scientific_io").at("controls").at("path"),"cancel.path",(dir/"canceled.tif").u8string());
        if(bigtiff){press("container");press("container.bigtiff");}
        if(tiled){press("storage");press("storage.tiled");}
        if(compressed){press("compression");press("compression."+codec);}
        if(floating_prediction){press("predictor");press("predictor.floating");}
        press("inspect");settle("export.cancel_reviewed");press("cancel");
        require(!fs::exists(dir/"canceled.tif"),"Canceled Float32 form wrote output");
        if(compressed) {
          const auto original=readBytes((dir/"output.tif").u8string());
          w.action("review_scientific_io",{{"record",w.workflow.back()}});frame("replay.reviewed");
          report["states"]["replay.reviewed"]=last.at("scientific_io");
          require(last.at("scientific_io").at("compression")==codec && !last.at("scientific_io").at("accept_loss").get<bool>(),"Compressed replay retained consent or lost codec");
          require(readBytes((dir/"output.tif").u8string())==original,"Replay wrote Float32 output");
          press("cancel");report["states"]["after_replay"]=snapshot();
        }
      }
    }
    frame("final");report["document_count"]=w.documents().size();report["workflow"]=w.workflow;report["error"]=w.error();
    session.close();report["lifecycle"]=session.lifecycle();report["state"]="observed";
    writeJson((dir/"case.json").u8string(),report);return report;
  } catch(const std::exception &error) {
    session.close();report["state"]="failed";report["error"]=error.what();writeJson((dir/"failure.json").u8string(),report);throw;
  }
#endif
}
} // namespace imagejxx::desktop
