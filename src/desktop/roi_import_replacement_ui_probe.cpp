#include "headless_session_internal.hpp"
#include "roi_import_ui_probe.hpp"
#include "rounded_roi_import_ui_probe.hpp"
#include <imagejxx/roi_style.hpp>
#include "window.hpp"
#include <chrono>
#include <fstream>
#include <imagejxx/imagej_roi_import.hpp>
#include <imagejxx/planar_annotations.hpp>
#include <thread>

namespace imagejxx::desktop {
namespace {
void require(bool ok, const std::string &message) {
  if (!ok)
    throw std::runtime_error(message);
}
Json state(Window &w) {
  const auto s = w.nativeSnapshot();
  return {{"metadata", metadataJson(s.data().metadata.extensions)},
          {"undo", w.nativeDocument(s.id())->undo_steps()},
          {"workflow", w.workflow}};
}
} // namespace
static Json roiImportReplacementUiVariant(Window &w, const std::string &context,
                                const std::string &fresh_directory, const std::string &variant) {
  const bool rounded = !variant.empty();
  namespace fs = std::filesystem;
  const auto dir = fs::absolute(fs::u8path(fresh_directory));
  require(!fs::exists(dir) && w.documents().empty(),
          "Fresh replacement fixture required");
  fs::create_directories(dir);
  const DatasetShape old{2, 2, 6, 9, 13, 1}, target{3, 2, 8, 10, 14, 1};
  Stack planes;
  for (int t = 0; t < target.t; ++t)
    for (int c = 0; c < target.c; ++c)
      for (int z = 0; z < target.z; ++z) {
        Image im(target.x, target.y, 1, PixelType::UInt16);
        for (int y = 0; y < target.y; ++y)
          for (int x = 0; x < target.x; ++x)
            im.at(x, y) = 20000 + 10000 * t + 1000 * c + 100 * z + 10 * y + x;
        planes.push_back(std::move(im));
      }
  auto doc = std::make_shared<imagejxx::Document>(
      Dataset::from_planes(target, std::move(planes)));
  auto view = doc->snapshot().display();
  view.orthogonal = true;
  view.t = 2;
  view.c = 1;
  view.crosshair = {4, 4, 2};
  doc->set_display(view);
  w.addNativeDocument(doc);
  w.recording = true;
  ImageJRoi bare;
  bare.roi = {
      "bare", Selection("rectangle", {{1.25, 1.5}, {3.75, 3.5}}), 0, 1, 1, {}};
  bare.group = 7;
  bare.properties = "source: \xce\xbb\xf0\x9f\x98\x80\n";
  ImageJRoiPlaneMapping mapping;
  mapping.extent = old;
  PlanarAnnotation row{
      "native",
      Selection("rectangle", {{1.250000000001, 1.500000000001},
                              {3.750000000001, 3.500000000001}}),
      Plane::YZ,
      4,
      {},
      {},
      {{"opaque", std::string("kept")}}};
  ImageJRoi projection;
  projection.roi = {row.name, row.selection, {}, {}, 4, {}};
  if (rounded) {
    bare.rounded_rectangle = std::array<double,5>{2.25,1.5,8.5,4.75,6};
    bare.roi.selection = imagej_rounded_rectangle_mask(*bare.rounded_rectangle,target.x,target.z);
    bare.roi.selection = with_roi_style(bare.roi.selection, {{"stroke_color",std::string("#ff00ff00")},{"fill_color",std::string("#80ff0000")},{"stroke_width",2.5}});
    bare.roi.name = "rounded source";bare.roi.t=2;bare.roi.c=1;bare.roi.z=4;
  }
  const auto bytes = rounded ? encode_imagej_roi(bare) : encode_imagej_roi_native_envelope(
      rebind_imagej_roi_mapping(map_imagej_roi(bare, mapping), old, row),
      projection);
  const auto file = dir / "original.roi";
  {
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char *>(bytes.data()),
              std::streamsize(bytes.size()));
    require(bool(out), "Write owned ROI fixture");
  }
  const auto before = doc->snapshot();
  Json report{{"contract", "p06q.roi-import-replacement.ui.001"},
              {"context", context},
              {"acceptance_claim", false},
              {"frames", Json::array()},
              {"controls", Json::array()},
              {"states", {{"before", state(w)}}}};
  if (rounded) { report["contract"]="p08t.rounded-roi-import.ui.001";report["case_id"]=variant; }
  HeadlessInputSession session(w);
  Json last;
  // Retain bounded raw failure observations as well as successful captures.
  struct Capture {
    Json &report;
    fs::path dir;
    bool done = false;
    ~Capture() {
      if (!done)
        try {
          report["state"] = "failed";
          if (report.dump().size() < 64 * 1048576)
            writeJson((dir / "failure.json").u8string(), report);
        } catch (...) {
        }
    }
  } capture{report, dir};
  auto frame = [&](const Json &events = Json::array()) {
    require(report["frames"].size() < (rounded ? 650 : 450), "Replacement frame bound");
    last = session.frame(events);
    Json projected;
    for (const char *key :
         {"input_frame", "roi_import", "host_controls", "orthogonal_planes"})
      projected[key] = last.at(key);
    require(last.at("input_frame").at("raw_inputs") == events,
            "Replacement raw input mismatch");
    report["frames"].push_back(
        {{"events", events}, {"rendered", std::move(projected)}});
  };
  auto click = [&](const Json &hit, const std::string &name) {
    require(hit.at("enabled").get<bool>() && hit.at("id").get<std::uint64_t>(),
            "Unavailable replacement control: " + name);
    report["controls"].push_back({{"name", name}, {"control", hit}});
    frame(Json::array({{{"type", "position"},
                        {"space", "screen"},
                        {"point", hit.at("point")}}}));
    for (bool down : {true, false})
      frame(Json::array(
          {{{"type", "button"}, {"name", "left"}, {"down", down}}}));
    frame();
  };
  auto panel = [&] { return last.at("roi_import"); };
  auto control = [&](const std::string &key) {
    for (int retry = 0; retry < 30; ++retry) {
      auto p = panel();
      require(p.at("controls").contains(key),
              "Missing replacement control: " + key + " " + p.dump());
      auto hit = p.at("controls").at(key);
      if (!rounded) return hit;
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
    throw std::runtime_error("Could not reveal replacement control: " + key);
  };
  auto settle = [&] {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (panel().at("worker_active").get<bool>()) {
      require(std::chrono::steady_clock::now() < deadline,
              "Replacement worker timeout");
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      frame();
    }
    frame();
  };
  auto text = [&](const std::string &key, const std::string &value) {
    click(control(key), key);
    for (bool down : {true, false})
      frame(Json::array({{{"type", "keys"},
                          {"keys",
                           {{{"name", "Ctrl"}, {"down", down}},
                            {{"name", "A"}, {"down", down}}}}}}));
    frame(Json::array({{{"type", "text"}, {"value", value}}}));
    frame();
  };
  auto choose = [&](const std::string &key, int i) {
    click(control(key), key);
    click(control(key + "." + std::to_string(i)),
          key + "." + std::to_string(i));
  };
  auto review = [&] {
    click(control("validate"), "validate");
    settle();
  };
  frame();
  frame();
  frame();
  click(last.at("host_controls").at("menu.File"), "menu.File");
  click(last.at("host_controls").at("menu.file.import_rois"),
        "menu.file.import_rois");
  text("path", file.u8string());
  click(control("load"), "load");
  settle();
  if (rounded) {
    click(control("promote_rounded"), "refuse unmapped promotion");settle();
    require(!panel().at("error").get<std::string>().empty(),"Unmapped promotion was accepted");
    report["unmapped"] = panel();
    choose("plane",2);choose("z_policy",1);review();
    require(panel().at("ready").get<bool>(),"Bare rounded review failed");
    report["default_review"]=panel();
    click(control("edit_replacement"),"edit ordinary mapped mask");settle();
    report["default_seed"]=panel();
    require(panel().at("replacement_editor").at("kind")=="mask","Legacy import no longer defaults to mask");
    click(control("replacement.discard"),"discard mask edit");
    click(control("promote_rounded"),"promote rounded primitive");settle();
    report["promoted_seed"]=panel();
    require(panel().at("replacement_editor").at("kind")=="rounded_rectangle","Promotion did not seed editable geometry");
    click(control("replacement.keep"),"unconfirmed rounded keep");
    report["unconfirmed"]=panel();
    click(control("replacement.confirm"),"confirm rounded destination");
    text("replacement.corner_diameter","2.5");
    click(control("replacement.keep"),"refuse fractional diameter");
    report["invalid_diameter"]=panel();
    require(!panel().at("replacement_editor").at("error").get<std::string>().empty(),"Invalid diameter accepted");
    text("replacement.corner_diameter",variant=="zero"?"0":variant=="oversized"?"2147483647":"3");
    if(variant=="rotated")text("replacement.angle","23");
    text("replacement.name","rounded \xce\xbb\xf0\x9f\x98\x80");
    click(control("replacement.keep"),"keep rounded replacement");review();
    require(panel().at("ready").get<bool>(),"Rounded review failed: "+panel().dump());
    report["review"]=panel();report["states"]["review"]=state(w);
    click(control("image_review"),"review rounded on image");frame();
    report["preview_render"]=last.at("orthogonal_planes");
    click(control("apply"),"apply rounded import");frame();
    report["states"]["applied"]=state(w);
    click(last.at("host_controls").at("menu.Edit"),"menu.Edit");
    click(last.at("host_controls").at("menu.Edit.Undo"),"undo");report["states"]["undone"]=state(w);
    click(last.at("host_controls").at("menu.Edit"),"menu.Edit");
    click(last.at("host_controls").at("menu.Edit.Redo"),"redo");report["states"]["redone"]=state(w);
    click(last.at("host_controls").at("workflow.roi_import.0.review"),"review captured rounded import");settle();
    click(control("edit_replacement"),"edit captured rounded replacement");settle();
    report["captured_seed"]=panel();
    text("replacement.name","discarded");
    click(control("replacement.discard"),"discard captured rounded edits");
    click(control("confirm_destination"),"confirm captured destination");review();
    report["discard_review"]=panel();
    click(control("reset_replacement"),"reset explicit rounded promotion");review();
    report["reset_review"]=panel();
    require(panel().at("ready").get<bool>(),"Reset rounded review failed");
    click(control("cancel"),"cancel reset draft");
    report["states"]["canceled"]=state(w);
    require(doc->snapshot().data().dataset.shares_pixels(before.data().dataset),"Rounded import changed pixels");
    std::ifstream original(file,std::ios::binary);
    require(std::vector<unsigned char>((std::istreambuf_iterator<char>(original)),{})==bytes,"Rounded import changed original bytes");
    report["original_unchanged"]=true;report["pixels_shared"]=true;
    report["original_file"]=file.u8string();
    session.close();report["state"]="observed";report["error"]=w.error();
    require(report.dump().size()<64*1048576,"Rounded import observation byte bound");
    writeJson((dir/"case.json").u8string(),report);capture.done=true;return report;
  }
  review();
  report["extent_refusal"] = panel();
  report["states"]["extent_refusal"] = state(w);
  click(control("edit_replacement"), "edit_replacement");
  settle();
  click(control("replacement.keep"), "unconfirmed keep");
  report["unconfirmed"] = panel();
  choose("replacement.geometry",
         0); // Selecting Keep must be a harmless operation.
  choose("replacement.plane", 1);
  text("replacement.slice", "5");
  click(control("replacement.all_t"), "time restriction");
  text("replacement.t", "3");
  click(control("replacement.all_c"), "channel restriction");
  text("replacement.c", "2");
  text("replacement.name", "edited \xce\xbb");
  choose("replacement.geometry", 4);
  text("replacement.vertex_u", "7.250000000001");
  text("replacement.vertex_v", "0.500000000001");
  click(control("replacement.confirm"), "confirm extent");
  click(control("replacement.keep"), "keep point");
  review();
  require(panel().at("ready").get<bool>(),
          "Replacement review failed: " + panel().dump());
  report["review"] = panel();
  report["states"]["review"] = state(w);
  click(control("image_review"), "review on image");
  frame();
  report["preview_render"] = last.at("orthogonal_planes");
  click(control("apply"), "apply point");
  frame();
  report["states"]["applied"] = state(w);
  click(last.at("host_controls").at("menu.Edit"), "menu.Edit");
  click(last.at("host_controls").at("menu.Edit.Undo"), "undo");
  report["states"]["undone"] = state(w);
  click(last.at("host_controls").at("menu.Edit"), "menu.Edit");
  click(last.at("host_controls").at("menu.Edit.Redo"), "redo");
  report["states"]["redone"] = state(w);
  click(last.at("host_controls").at("workflow.roi_import.0.review"),
        "workflow review");
  settle();
  click(control("edit_replacement"), "edit captured replacement");
  settle();
  report["captured_seed"] = panel();
  text("replacement.name", "discarded");
  click(control("replacement.discard"), "discard edits");
  click(control("confirm_destination"), "confirm captured destination");
  review();
  report["discard_review"] = panel();
  click(control("edit_replacement"), "edit rotated rectangle");
  settle();
  choose("replacement.geometry", 1);
  text("replacement.center_u", "5.500000000001");
  text("replacement.width", "4.25");
  text("replacement.height", "2.5");
  text("replacement.angle", "30");
  text("replacement.stroke", "#ff00ff00");
  text("replacement.stroke_width", "2.5");
  click(control("replacement.confirm"), "confirm box extent");
  click(control("replacement.keep"), "keep box");
  review();
  require(panel().at("ready").get<bool>(),
          "Box review failed: " + panel().dump());
  report["box_review"] = panel();
  click(control("apply"), "apply box");
  report["states"]["box_applied"] = state(w);
  require(doc->snapshot().data().dataset.shares_pixels(before.data().dataset),
          "Replacement changed pixels");
  std::ifstream in(file, std::ios::binary);
  std::vector<unsigned char> after((std::istreambuf_iterator<char>(in)), {});
  require(after == bytes, "Replacement changed original file");
  report["original_unchanged"] = true;
  report["pixels_shared"] = true;
  auto mask_doc = std::make_shared<imagejxx::Document>(before.data().dataset);
  PlanarAnnotation mask_row{
      "destination mask",
      Selection::from_packed_mask(10, 8, {0x80, 0, 0, 0, 0, 0, 0, 0, 0, 1}),
      Plane::YZ,
      4,
      2,
      1,
      {{"mask-origin", std::string("destination")}}};
  auto mask_edit =
      prepare_store_planar_annotation(mask_doc->snapshot(), mask_row);
  mask_doc->commit(mask_edit);
  mask_doc->set_display(view);
  w.addNativeDocument(mask_doc);
  frame();
  frame();
  report["states"]["mask_before"] = state(w);
  click(last.at("host_controls").at("menu.File"), "mask menu.File");
  click(last.at("host_controls").at("menu.file.import_rois"), "mask import");
  text("path", file.u8string());
  click(control("load"), "mask load");
  settle();
  click(control("edit_replacement"), "edit mask replacement");
  settle();
  choose("replacement.load", 0);
  choose("replacement.plane", 0);
  click(control("replacement.confirm"), "confirm mask extent");
  click(control("replacement.keep"), "refuse incompatible mask");
  report["mask_refusal"] = panel();
  choose("replacement.plane", 2);
  click(control("replacement.keep"), "keep compatible mask");
  choose("mode", 1);
  review();
  require(panel().at("ready").get<bool>(),
          "Mask review failed: " + panel().dump());
  report["mask_review"] = panel();
  click(control("image_review"), "review mask on image");
  frame();
  report["mask_preview_render"] = last.at("orthogonal_planes");
  click(control("cancel"), "cancel mask replacement");
  report["states"]["mask_canceled"] = state(w);
  session.close();
  report["state"] = "observed";
  report["error"] = w.error();
  require(report.dump().size() < 64 * 1048576, "Replacement report byte bound");
  writeJson((dir / "case.json").u8string(), report);
  capture.done = true;
  return report;
}
Json roiImportReplacementUiCase(Window &w,const std::string &context,const std::string &directory) {
  return roiImportReplacementUiVariant(w,context,directory,{});
}
Json roundedRoiImportUiCase(Window &w,const std::string &variant,const std::string &context,const std::string &directory) {
  if(variant!="axis" && variant!="rotated" && variant!="zero" && variant!="oversized")
    throw std::invalid_argument("Unknown rounded import UI case");
  return roiImportReplacementUiVariant(w,context,directory,variant);
}
} // namespace imagejxx::desktop
