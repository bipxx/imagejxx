#include "native_bare_import_ui_probe.hpp"
#include <imagejxx/composite_path.hpp>
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <chrono>
#include <fstream>
#include <imagejxx/imagej_roi_import.hpp>
#include <imagejxx/session.hpp>
#include <thread>

namespace imagejxx::desktop {
namespace {
void require(bool v, const std::string &message) {
  if (!v)
    throw std::runtime_error(message);
}
Json state(Window &window) {
  const auto s = window.nativeSnapshot();
  const auto &d = s.data();
  const auto shape = d.dataset.shape();
  Json pixels = Json::array();
  for (int t = 0; t < shape.t; ++t)
    for (int c = 0; c < shape.c; ++c)
      for (int z = 0; z < shape.z; ++z)
        pixels.push_back(d.dataset.plane({t, c, z})->pixels);
  return {
      {"shape", shape.dimensions()},
      {"pixels", pixels},
      {"metadata", metadataJson(d.metadata.extensions)},
      {"title", d.metadata.title},
      {"undo", window.nativeDocument(s.id())->undo_steps()},
      {"workflow", window.workflow},
      {"selection", d.selection ? selectionJson(*d.selection) : Json(nullptr)},
      {"legacy_roi_count", d.rois.size()}};
}
void bytes(const std::filesystem::path &file,
           const std::vector<unsigned char> &data) {
  std::ofstream out(file, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            std::streamsize(data.size()));
  require(bool(out), "Cannot write owned ROI fixture");
}
} // namespace
Json nativeBareImportUiCase(Window &window,const std::string &context,
                            const std::string &fresh_directory) {
  namespace fs=std::filesystem;
  const auto directory=fs::absolute(fs::u8path(fresh_directory));
  require(!fs::exists(directory) && window.documents().empty(),"Fresh native bare import fixture required");
  fs::create_directories(directory);
  Stack planes;
  for(int z=0;z<6;++z) {
    Image image(14,10,1,PixelType::UInt16);
    for(int y=0;y<10;++y)for(int x=0;x<14;++x)image.at(x,y)=100*z+10*y+x;
    planes.push_back(std::move(image));
  }
  auto owner=std::make_shared<imagejxx::Document>(Dataset::from_planes({1,1,6,10,14,1},std::move(planes)));
  auto view=owner->snapshot().display();view.orthogonal=true;view.crosshair={0,0,0};view.zoom=4;owner->set_display(view);
  window.addNativeDocument(owner);window.recording=true;
  const std::vector<double> commands{
      0,1.25,1.125,2,3.25,.25,5.25,1.125,3,6.25,2,6.25,4,5.25,4,1,1.25,4,4,
      0,2.25,2,1,4.25,2,1,4.25,3,1,2.25,3,4};
  auto geometry=with_roi_style(Selection::from_path(commands),
      {{"stroke_color",std::string("#FF22CCEE")},{"fill_color",std::string("#804488CC")}});
  ImageJRoi original;original.roi={"curve",geometry,{},{},{},{}};
  original.properties="source: native bare curves\n";original.group=7;
  const auto wire=encode_imagej_roi(original);
  bytes(directory/"single.roi",wire);
  bytes(directory/"curves.zip",encode_imagej_roi_zip({{"xy.roi",wire},{"xz.roi",wire},{"yz.roi",wire}}));
  ImageJRoi primitive;primitive.roi={"primitive",Selection("rectangle",{{1,1},{3,3}}),{},{},{},{}};
  bytes(directory/"bad.zip",encode_imagej_roi_zip({{"curve.roi",wire},{"primitive.roi",encode_imagej_roi(primitive)}}));
  Json report{{"contract","p13c.native-bare-import.ui.001"},{"context",context},
      {"acceptance_claim",false},{"frames",Json::array()},{"controls",Json::array()},
      {"states",{{"before",state(window)}}},{"source_bytes",wire},
      {"api_fixture_actions",{"Owned scientific dataset and byte fixtures","Session save/reopen"}}};
  struct FailureGuard {
    Json &report;fs::path directory;
    ~FailureGuard() {
      if(report.value("state",std::string{})!="observed")try {
        writeJson((directory/"failure.json").u8string(),report);
      }catch(...){}
    }
  } failure{report,directory};
  HeadlessInputSession session(window);
  Json last;
  auto frame = [&](const Json &events = Json::array()) {
    require(report["frames"].size() < 650, "ROI import UI frame bound");
    last = session.frame(events);
    require(last.at("input_frame").at("raw_inputs") == events,
            "ROI import raw input mismatch");
    report["frames"].push_back({{"events", events}, {"rendered", last}});
  };
  auto click = [&](const Json &control, const std::string &name) {
    require(control.at("enabled").get<bool>() &&
                control.at("id").get<std::uint64_t>() != 0,
            "Unavailable ROI import control: " + name);
    report["controls"].push_back({{"name", name}, {"control", control}});
    frame(Json::array({{{"type", "position"},
                        {"space", "screen"},
                        {"point", control.at("point")}}}));
    frame(
        Json::array({{{"type", "button"}, {"name", "left"}, {"down", true}}}));
    frame(
        Json::array({{{"type", "button"}, {"name", "left"}, {"down", false}}}));
    frame();
  };
  auto panel = [&]() { return last.at("roi_import"); };
  auto control = [&](const std::string &key) {
    for (int retry = 0; retry < 45; ++retry) {
      auto p = panel();
      require(p.at("controls").contains(key),
              "Missing bare control: " + key + " " + p.dump());
      auto hit = p.at("controls").at(key);
      auto bounds = p.at("window");
      if (key.rfind("replacement.payload.", 0) == 0)
        bounds =
            p.at("replacement_editor").at("payload_editor").at("fields_window");
      else if (key.rfind("replacement.", 0) == 0)
        bounds = p.at("replacement_editor").at("window");
      const double y = hit.at("point")[1];
      if (hit.value("visible", y > bounds[1].get<double>() + 4 &&
                                   y < bounds[3].get<double>() - 5))
        return hit;
      frame(Json::array(
          {{{"type", "position"},
            {"space", "screen"},
            {"point",
             {bounds[2].get<double>() - 14,
              (bounds[1].get<double>() + bounds[3].get<double>()) / 2}}},
           {{"type", "wheel"},
            {"x", 0},
            {"y", y < bounds[1].get<double>() ? 3 : -3}}}));
      frame();
    }
    throw std::runtime_error("Could not reveal bare control: " + key);
  };
  auto settle = [&] {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (panel().at("worker_active").get<bool>()) {
      require(std::chrono::steady_clock::now() < deadline,
              "ROI import worker did not settle");
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      frame();
    }
  };
  auto open = [&] {
    click(last.at("host_controls").at("menu.File"), "menu.File");
    click(last.at("host_controls").at("menu.file.import_rois"),
          "menu.file.import_rois");
    require(panel().at("visible").get<bool>(),
            "ROI import menu did not open panel");
  };
  auto path = [&](const fs::path &file) {
    click(control("path"), "path");
    for (bool down : {true, false})
      frame(Json::array({{{"type", "keys"},
                          {"keys",
                           {{{"name", "Ctrl"}, {"down", down}},
                            {{"name", "A"}, {"down", down}}}}}}));
    frame(Json::array({{{"type", "text"}, {"value", file.u8string()}}}));
    frame();
    click(control("load"), "load");
    settle();
    require(panel().at("error") == "",
            "ROI member load failed: " + panel().dump());
  };
  auto choose = [&](const char *key, int index) {
    click(control(key), key);
    click(control(std::string(key) + "." + std::to_string(index)),
          std::string(key) + "." + std::to_string(index));
  };
  auto review = [&] {
    click(control("validate"), "validate");
    settle();
    frame();
  };
  frame();frame();frame();open();path(directory/"curves.zip");
  review();report["unmapped"]=panel();report["states"]["unmapped"]=state(window);
  auto map_native=[&](int index,int plane) {
    click(control("member."+std::to_string(index)),"member."+std::to_string(index));
    choose("format",2);choose("plane",plane+1);click(control("explicit_slice"),"explicit_slice");
  };
  for(int i=0;i<3;++i)map_native(i,i);
  review();require(panel().at("ready").get<bool>(),"Native bare XYZ review failed: "+panel().dump());
  report["review"]=panel();report["states"]["review"]=state(window);
  click(control("image_review"),"review native curves on image");frame();
  report["preview_render"]=last.at("orthogonal_planes");
  frame();report["idle_before"]=last.at("orthogonal_planes");frame();frame();report["idle_after"]=last.at("orthogonal_planes");
  click(control("image_review"),"edit native choices");
  choose("format",0);report["invalidated"]=panel();choose("format",2);review();
  require(panel().at("ready").get<bool>(),"Native review did not recover");
  click(control("apply"),"apply native bare import");frame();
  report["states"]["applied"]=state(window);report["applied_render"]=last.at("orthogonal_planes");
  click(last.at("host_controls").at("menu.Edit"),"menu.Edit");
  click(last.at("host_controls").at("menu.Edit.Undo"),"undo native import");frame();report["states"]["undone"]=state(window);
  click(last.at("host_controls").at("menu.Edit"),"menu.Edit");
  click(last.at("host_controls").at("menu.Edit.Redo"),"redo native import");frame();report["states"]["redone"]=state(window);
  open();path(directory/"bad.zip");for(int i=0;i<2;++i)map_native(i,0);review();
  report["bad_review"]=panel();report["states"]["refused"]=state(window);
  require(!panel().at("ready").get<bool>() && panel().at("error")!="","Non-composite native source was not refused");
  open();path(directory/"single.roi");map_native(0,1);
  click(control("edit_replacement"),"edit native replacement");settle();frame();
  auto type=[&](const char *key,const char *value) {
    click(control(key),key);
    for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}}}}}));
    frame(Json::array({{{"type","text"},{"value",value}}}));frame();
  };
  type("replacement.vertex","2");
  type("replacement.vertex_u","3.7500000000000004");
  type("replacement.vertex_v","0.8750000000000001");
  click(control("replacement.stage_vertex"),"stage native control");
  report["native_editor"]=panel().at("replacement_editor");
  click(control("replacement.confirm"),"confirm native destination");
  click(control("replacement.keep"),"keep native replacement");frame();
  review();require(panel().at("ready").get<bool>(),"Native edited import review failed: "+panel().dump());
  report["edited_review"]=panel();report["states"]["edited_review"]=state(window);
  click(control("apply"),"apply edited native import");frame();report["states"]["edited_import"]=state(window);
  session.close();const auto saved=(directory/"imports.ijx").u8string();window.action("save_document",{{"path",saved}});
  Window reopened((directory/"reopened.json").u8string());reopened.openFile(saved);
  report["states"]["reopened"]=state(reopened);report["state"]="observed";report["error"]=window.error();
  require(report.dump().size()<=64*1048576,"Native bare import report bound");
  writeJson((directory/"case.json").u8string(),report);return report;
}
} // namespace imagejxx::desktop
