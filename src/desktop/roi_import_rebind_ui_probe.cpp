#include "headless_session_internal.hpp"
#include "roi_import_ui_probe.hpp"
#include "window.hpp"
#include <chrono>
#include <imagejxx/imagej_roi_import.hpp>
#include <thread>

namespace imagejxx::desktop {
namespace {
void require(bool value, const std::string &message) {
  if (!value)
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
  return {{"id", s.id()},
          {"shape", shape.dimensions()},
          {"pixels", pixels},
          {"metadata", metadataJson(d.metadata.extensions)},
          {"undo", window.nativeDocument(s.id())->undo_steps()},
          {"workflow", window.workflow}};
}
std::shared_ptr<imagejxx::Document> fixture(const DatasetShape &shape,
                                            int offset) {
  Stack planes;
  for (int t = 0; t < shape.t; ++t)
    for (int c = 0; c < shape.c; ++c)
      for (int z = 0; z < shape.z; ++z) {
        Image image(shape.x, shape.y, 1, PixelType::UInt16);
        for (int y = 0; y < shape.y; ++y)
          for (int x = 0; x < shape.x; ++x)
            image.at(x, y) =
                offset + 10000 * t + 1000 * c + 100 * z + 10 * y + x;
        planes.push_back(std::move(image));
      }
  SpatialCalibration cal;
  cal.spacing = {2, 3, 5};
  cal.origin = {10, 20, 30};
  auto result = std::make_shared<imagejxx::Document>(
      Dataset::from_planes(shape, std::move(planes), cal));
  auto view = result->snapshot().display();
  view.orthogonal = true;
  view.c = 1;
  view.crosshair = {4, 1, 2};
  result->set_display(view);
  return result;
}
} // namespace
Json roiImportRebindUiCase(Window &window, const std::string &context,
                           const std::string &fresh_directory) {
  namespace fs = std::filesystem;
  const auto directory = fs::absolute(fs::u8path(fresh_directory));
  require((context == "imgui_cpp" || context == "imgui_python") &&
              !fs::exists(directory) && window.documents().empty(),
          "Fresh ROI rebind fixture required");
  fs::create_directories(directory);
  const DatasetShape shape{2, 2, 6, 9, 13, 1};
  auto source = fixture(shape, 0);
  window.addNativeDocument(source);
  window.recording = true;
  ImageJRoi bare;
  bare.roi = {
      "bare", Selection("rectangle", {{1.25, 1.5}, {3.75, 3.5}}), 0, 1, 1, {}};
  bare.group = 7;
  bare.properties = "source: \xce\xbb\n";
  ImageJRoiPlaneMapping mapping;
  mapping.extent = shape;
  const auto mapped = map_imagej_roi(bare, mapping);
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
  const auto plain = encode_imagej_roi(bare);
  const auto envelope = encode_imagej_roi_native_envelope(
      rebind_imagej_roi_mapping(mapped, shape, row), projection);
  const std::vector<ImageJRoiImportItem> items{{"bare.roi", plain, mapping},
                                               {"native.roi", envelope, {}}};
  const auto original =
      resolve_imagej_roi_import(source->snapshot(), items).record();
  const auto captured = metadataJson(original);
  window.action("replay_imagej_roi_import", {{"record", captured}});
  const auto source_before = source->snapshot();
  auto destination = fixture(shape, 20000);
  window.addNativeDocument(destination);
  Json report{{"contract", "p06q.roi-import-rebind.ui.001"},
              {"context", context},
              {"acceptance_claim", false},
              {"frames", Json::array()},
              {"controls", Json::array()},
              {"original", captured},
              {"api_fixture_actions",
               {"owned datasets and native captured imports",
                "attach captured/corrupted workflow arrays",
                "activate new destination documents"}},
              {"states", {{"before", state(window)}}}};
  HeadlessInputSession session(window);
  Json last;
  auto frame = [&](const Json &events = Json::array()) {
    require(report["frames"].size() < 300, "ROI rebind frame bound");
    last = session.frame(events);
    require(last.at("input_frame").at("raw_inputs") == events,
            "ROI rebind raw input mismatch");
    report["frames"].push_back({{"events", events}, {"rendered", last}});
  };
  auto click = [&](const Json &control, const std::string &name) {
    require(control.at("enabled").get<bool>() &&
                control.at("id").get<std::uint64_t>() != 0,
            "Unavailable rebind control: " + name);
    report["controls"].push_back({{"name", name}, {"control", control}});
    frame(Json::array({{{"type", "position"},
                        {"space", "screen"},
                        {"point", control.at("point")}}}));
    for (bool down : {true, false})
      frame(Json::array(
          {{{"type", "button"}, {"name", "left"}, {"down", down}}}));
    frame();
  };
  auto panel = [&] { return last.at("roi_import"); };
  auto control = [&](const std::string &key) {
    return panel().at("controls").at(key);
  };
  auto settle = [&] {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (panel().at("worker_active").get<bool>()) {
      require(std::chrono::steady_clock::now() < deadline,
              "ROI rebind worker timeout");
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      frame();
    }
    frame();
  };
  auto open = [&](const Json &record) {
    window.workflow = Json::array({record});
    frame();
    frame();
    click(last.at("host_controls").at("workflow.roi_import.0.review"),
          "workflow review");
    settle();
  };
  auto review = [&] {
    click(control("validate"), "validate");
    settle();
  };
  auto choose = [&](const std::string &key, int index) {
    click(control(key), key);
    click(control(key + "." + std::to_string(index)),
          key + "." + std::to_string(index));
  };
  frame();
  frame();
  frame();
  open(captured);
  report["loaded"] = panel();
  review();
  report["unconfirmed"] = panel();
  report["states"]["unconfirmed"] = state(window);
  click(control("confirm_destination"), "confirm destination");
  choose("plane", 2);
  choose("z_policy", 1);
  review();
  require(panel().at("ready").get<bool>(),
          "Rebound review failed: " + panel().dump());
  report["review"] = panel();
  report["states"]["review"] = state(window);
  report["review_render"] = last.at("orthogonal_planes");
  click(control("apply"), "apply");
  frame();
  report["states"]["applied"] = state(window);
  require(window.workflow.at(0) == captured && window.workflow.size() == 2,
          "Rebind changed captured command");
  open(captured);
  click(control("confirm_destination"), "confirm append");
  review();
  report["append_review"] = panel();
  click(control("cancel"), "cancel append");
  report["states"]["canceled"] = state(window);
  Json bad = captured;
  bad["after"]["rows"][0]["name"] = "forged";
  open(bad);
  report["invalid_original"] = panel();
  report["states"]["invalid_original"] = state(window);
  click(control("cancel"), "cancel invalid original");
  const DatasetShape different{2, 2, 5, 8, 12, 1};
  auto target = fixture(different, 30000);
  window.addNativeDocument(target);
  frame();
  frame();
  report["states"]["different_before"] = state(window);
  open(captured);
  click(control("confirm_destination"), "confirm different extent");
  review();
  report["extent_refusal"] = panel();
  report["states"]["extent_refusal"] = state(window);
  click(control("cancel"), "cancel extent refusal");
  const auto bare_record =
      metadataJson(resolve_imagej_roi_import(source->snapshot(),
                                             {{"bare.roi", plain, mapping}})
                       .record());
  report["bare_original"] = bare_record;
  open(bare_record);
  click(control("confirm_destination"), "confirm bare different extent");
  review();
  require(panel().at("ready").get<bool>(),
          "Explicit bare extent review failed: " + panel().dump());
  report["bare_review"] = panel();
  click(control("apply"), "apply bare different extent");
  frame();
  report["states"]["different_applied"] = state(window);
  require(source->snapshot().shares_data(source_before),
          "Rebinding mutated original document");
  report["source_unchanged"] = true;
  report["source_history"] = source->undo_steps();
  report["destination_history"] = destination->undo_steps();
  report["destination_metadata"] =
      metadataJson(destination->snapshot().data().metadata.extensions);
  session.close();
  report["state"] = "observed";
  report["error"] = window.error();
  require(report.dump().size() <= 64 * 1048576, "ROI rebind report byte bound");
  writeJson((directory / "case.json").u8string(), report);
  return report;
}
} // namespace imagejxx::desktop
