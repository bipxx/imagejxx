#include "scientific_io_panel.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <iostream>
#include <limits>
using namespace imagejxx;
using namespace imagejxx::desktop;
namespace {
void check(bool v, const char *m) {
  if (!v)
    throw std::runtime_error(m);
}
template <class F> void refuses(F f) {
  bool failed = false;
  try {
    f();
  } catch (const std::exception &) {
    failed = true;
  }
  check(failed, "Expected panel refusal");
}
void replayChecks(const Json &record) {
  const auto draft = scientific_io_replay_draft(record);
  check(draft.size() == 3 && draft.at("path") == record.at("path") &&
            draft.at("limits") == record.at("limits"),
        "Replay must restore only inert path/limits/mode");
  const bool exporting = record.at("operation") == "export";
  std::size_t refusals = 0;
  auto bad = [&](const std::function<void(Json &)> &mutate) {
    auto value = record;
    mutate(value);
    refuses([&] { scientific_io_replay_draft(value); });
    ++refusals;
  };
  // Exercise fields in records emitted by actual open/export, including nested
  // corruption that used to pass the generic size/depth visitor.
  const auto cal =
      exporting ? "/encoded_calibration" : "/reviewed_series/calibration";
  const auto shape = exporting ? "/source_shape" : "/reviewed_series/shape";
  bad([&](auto &v) {
    v[Json::json_pointer(std::string(cal) + "/spacing/0")] = -1;
  });
  for (const auto &item : record.items())
    bad([&](auto &v) { v.erase(item.key()); });
  bad([](auto &v) { v["path"] = ""; });
  bad([](auto &v) { v["path"] = std::string("a\0b", 3); });
  bad([](auto &v) { v["path"] = std::string("\xc0\xaf", 2); });
  bad([](auto &v) { v["path"] = std::string("\xed\xa0\x80", 3); });
  auto absent = record;
  absent["path"] = "missing-parent-\xe6\x97\xa5/absent.tif";
  scientific_io_replay_draft(absent);
  bad([](auto &v) { v["series_index"] = false; });
  bad([](auto &v) { v["limits"]["max_ifds"] = -1; });
  bad([](auto &v) { v["limits"]["max_total_samples"] = 1; });
  bad([&](auto &v) { v[Json::json_pointer(shape)] = {1, 2}; });
  for (auto value :
       {Json(false), Json(0), Json(-1), Json(1.5), Json(UINT64_MAX)})
    bad([&](auto &v) {
      v[Json::json_pointer(std::string(shape) + "/0")] = value;
    });
  bad([&](auto &v) { v[Json::json_pointer(std::string(shape) + "/5")] = 3; });
  bad([&](auto &v) { v[Json::json_pointer(std::string(cal) + "/extra")] = 1; });
  bad([&](auto &v) {
    v[Json::json_pointer(std::string(cal) + "/spacing")] = {1, 2};
  });
  for (auto value : {Json(false), Json(0), Json("1"),
                     Json(std::numeric_limits<double>::infinity())})
    bad([&](auto &v) {
      v[Json::json_pointer(std::string(cal) + "/frame_interval")] = value;
    });
  bad([&](auto &v) {
    v[Json::json_pointer(std::string(cal) + "/unit")] = "unsupported";
  });
  bad([&](auto &v) {
    v[Json::json_pointer(std::string(cal) + "/time_unit")] = "sec";
  });
  if (exporting) {
    bad([](auto &v) { v["source_document_id"] = -1; });
    bad([](auto &v) { v["source_data_identity"] = false; });
    bad([](auto &v) { v["pixels_and_calibration_only"] = false; });
    bad([](auto &v) { v["canonicalized_fields"] = {"unknown"}; });
    bad([](auto &v) { v["canonicalized_fields"] = {"unit", "unit"}; });
    bad([](auto &v) { v["xy_resolution"] = {{1, 0}, {1, 1}}; });
    bad([](auto &v) { v["xy_resolution"] = {{UINT64_MAX, 1}, {1, 1}}; });
    bad([](auto &v) { v["xy_resolution"] = {{1, 1}, {1, 1}}; });
    bad([](auto &v) { v["reviewed_series"] = Json::object(); });
    auto nullable = record;
    nullable["source_data_identity"] = nullptr;
    scientific_io_replay_draft(nullable);
  } else {
    bad([](auto &v) { v["replace_existing"] = true; });
    bad([](auto &v) { v["pixels_and_calibration_only"] = true; });
    bad([](auto &v) { v["inspected_file_bytes"] = 0; });
    bad([](auto &v) { v["reviewed_series"]["series_index"] = 1; });
    bad([](auto &v) { v["reviewed_series"]["dtype"] = "float32"; });
    bad([](auto &v) { v["reviewed_series"]["metadata"]["unknown"] = 1; });
    bad([](auto &v) {
      v["reviewed_series"]["metadata"]["defaulted_fields"] = {"unknown"};
    });
    bad([](auto &v) {
      v["reviewed_series"]["metadata"]["source_description"] = Json::object();
    });
    bad([](auto &v) { v["encoded_calibration"] = Json::object(); });
  }
  std::cout << "PASS " << (exporting ? "export" : "open")
            << " replay: " << refusals << " malformed records refused\n";
}
} // namespace
int main(int argc, char **argv) {
  try {
    check(argc == 3, "Panel test paths required");
    namespace fs = std::filesystem;
    auto root =
        fs::absolute(fs::u8path(argv[1])) /
        std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(root);
    auto fixtures = fs::absolute(fs::u8path(argv[2]));
    auto scheduler = std::make_shared<JobScheduler>();
    ScientificIoPanel panel(scheduler);
    std::shared_ptr<imagejxx::Document> owner;
    int opens = 0, exports = 0;
    Json record;
    auto current = [&]() -> std::optional<DocumentSnapshot> {
      return owner ? std::optional<DocumentSnapshot>{owner->snapshot()}
                   : std::optional<DocumentSnapshot>{};
    };
    auto publish_open = [&](auto document, const Json &r, bool recording) {
      check(recording, "Captured recording flag");
      owner = std::move(document);
      record = r;
      ++opens;
    };
    auto publish_export = [&](const DocumentSnapshot &source, const Json &r,
                              bool recording,
                              const std::function<void()> &commit) {
      check(source.same_document(owner->snapshot()), "Captured export owner");
      check(recording, "Captured export recording");
      record = r;
      commit();
      ++exports;
    };
    auto tick = [&] {
      panel.tick(current(), false, 128 * 1048576, publish_open, publish_export);
    };
    auto wait = [&] {
      check(panel.wait_for(std::chrono::seconds(10)), "Bounded IO wait");
      tick();
    };
    panel.show(false, {}, 128 * 1048576, (fixtures / "little.tif").u8string(),
               true);
    refuses([&] { panel.apply(); });
    refuses([&] { panel.flat_import_path(); });
    panel.patch({{"accept_flat_pixels_only", true}});
    check(panel.flat_import_path() == (fixtures / "little.tif").u8string(),
          "Explicit flat consent must name the reviewed TIFF");
    panel.inspect();
    wait();
    refuses([&] { panel.flat_import_path(); });
    check(panel.diagnostics().contains("inspection") && opens == 0,
          "Inspection published an owner");
    panel.patch({{"confirm_axes", true}});
    panel.apply();
    wait();
    check(opens == 1 && !panel.visible() && !owner->snapshot().dirty() &&
              owner->undo_steps() == 0,
          "Open must publish one clean owner");
    auto snapshot = owner->snapshot();
    check(snapshot.data().dataset.shape().dimensions() ==
              std::array<int, 6>{2, 3, 2, 3, 5, 1},
          "Scientific TCZYXS owner");
    for (int t = 0; t < 2; ++t)
      for (int c = 0; c < 3; ++c)
        for (int z = 0; z < 2; ++z) {
          auto image = snapshot.data().dataset.plane({t, c, z});
          for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 5; ++x)
              check(image->at(x, y) ==
                        10000 * t + 1000 * c + 100 * z + 5 * y + x,
                    "Panel literal TCZ pixels");
        }
    check(record.at("replay_policy") == "explicit_review_required",
          "File identity/replay policy");
    replayChecks(record);
    panel.show(true, current(), 128 * 1048576, (root / "export.tif").u8string(),
               true);
    panel.inspect();
    wait();
    refuses([&] { panel.apply(); });
    panel.patch({{"accept_pixels_and_calibration_only", true}});
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1000, 800};
    io.DeltaTime = 1.0f / 60;
    unsigned char *pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ImGui::NewFrame();
    panel.draw([] {});
    ImGui::Render();
    check(panel.diagnostics().at("controls").contains("accept_loss") &&
              panel.diagnostics().at("controls").at("apply").at("enabled") ==
                  true,
          "Real shared ImGui controls");
    ImGui::DestroyContext();
    panel.apply();
    panel.wait();
    check(!fs::exists(root / "export.tif"), "Worker published export early");
    tick();
    check(exports == 1 && fs::exists(root / "export.tif"),
          "Caller-thread export publication");
    replayChecks(record);
    check(owner->snapshot().shares_data(snapshot) &&
              owner->snapshot().revisions().matches(snapshot.revisions(),
                                                    Change::All),
          "Export mutated source");
    for (bool stale : {false, true}) {
      auto path = root / (stale ? "stale.tif" : "cancel.tif");
      panel.show(true, current(), 128 * 1048576, path.u8string(), true);
      panel.inspect();
      wait();
      panel.patch({{"accept_pixels_and_calibration_only", true}});
      panel.apply();
      panel.wait();
      if (stale) {
        auto edit = owner->begin_edit();
        edit.set_property("changed", std::string("after staging"));
        owner->commit(edit);
      } else
        panel.cancel("test_cancel");
      tick();
      check(!fs::exists(path), "Stale/canceled UI export published");
    }
    panel.show(false, current(), 128 * 1048576,
               (fixtures / "compression.tif").u8string(), true);
    panel.inspect();
    wait();
    check(!panel.diagnostics().at("error").get<std::string>().empty() &&
              opens == 1,
          "Malformed input changed membership");
    panel.cancel("done");
    tick();
    check(scheduler->usage().reserved_bytes == 0,
          "IO retained job reservation leaked");
    for (const auto &entry : fs::directory_iterator(root))
      check(entry.path().extension() == ".tif", "Panel temporary leaked");
    scheduler->shutdown();
    std::cout << "PASS scientific IO shared form, review, jobs, clean open, "
                 "staged export and lifecycle\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
