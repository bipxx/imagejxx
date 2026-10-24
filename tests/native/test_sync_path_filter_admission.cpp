// Real exposed Window calls: synchronous and asynchronous Apply share the
// finite host mask allowance. No ImGui gesture or lazy-provider claim.
#include "../../src/desktop/window.hpp"
#include "../../src/desktop/gaussian_cancellation_internal.hpp"
#include "../../src/desktop/legacy_target_resources.hpp"
#include "../../src/native/kernel_control.hpp"
#include "../../src/native/target_mask_resources.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using namespace imagejxx;
using imagejxx::desktop::GaussianCancellationAccess;
using imagejxx::desktop::Json;
using imagejxx::desktop::Window;
namespace fs = std::filesystem;
constexpr int width = 100, height = 77;
constexpr std::size_t low_cap = 1024 * 1024;
constexpr std::size_t literal_base = 1047200; // 100*77*8*(1+16)
constexpr std::size_t literal_remaining = 1376;
std::size_t checks = 0;

void check(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
Selection triangle() {
  return Selection::from_path({0., 2.2, 1.2, 1., 9.2, 1.2, 1., 2.2, 8.2, 4.});
}
std::shared_ptr<imagejxx::Document> fixture(bool path) {
  Image image(width, height, 1, PixelType::UInt8);
  image.pixels.assign(std::size_t(width) * height, 20.);
  DocumentData data{Dataset::from_planes({1, 1, 1, height, width, 1},
                                       Stack{std::move(image)})};
  if (path) data.selection = triangle();
  auto owner = std::make_shared<imagejxx::Document>(std::move(data));
  owner->mark_saved();
  return owner;
}

// Literal retained Java mask: gaussian-000-u8-triangle in
// path-typed-gaussian-targets-imagej154r.json.gz, origin [2,1], dimensions [8,8].
// Its 28 cells are unaffected by expanding the containing canvas to 100x77.
bool selected(int x, int y) {
  constexpr std::array<int, 8> rows{7, 6, 5, 4, 3, 2, 1, 0};
  return y >= 1 && y <= 8 && x >= 2 && x < 2 + rows[std::size_t(y - 1)];
}
void pixels(const DocumentSnapshot &snapshot, bool path, bool inverted) {
  const auto image = snapshot.data().dataset.plane({});
  check(image->width == width && image->height == height && image->channels == 1 &&
            image->type == PixelType::UInt8,
        "Synchronous resource test changed image type or dimensions");
  std::size_t changed = 0;
  for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
    const bool target = !path || selected(x, y);
    const double expected = inverted && target ? 235. : 20.;
    check(image->pixels[std::size_t(y) * width + x] == expected,
          "Window Invert differs from literal 255-v membership expectation");
    if (inverted && target) ++changed;
  }
  check(changed == (inverted ? (path ? 28u : std::size_t(width) * height) : 0u),
        "Literal mask fixture cardinality changed");
}
void settle(Window &window) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  do {
    window.poll();
    if (!window.busy()) break;
    check(std::chrono::steady_clock::now() < deadline,
          "Window operation exceeded bounded completion wait");
    std::this_thread::yield();
  } while (true);
  check(window.error().empty(), "Window operation failed during completion");
}
void idle_ledger(Window &window) {
  const auto usage = GaussianCancellationAccess::scheduler(window)->usage();
  check(usage.running == 0 && usage.queued == 0 && usage.running_bytes == 0 &&
            usage.retained_bytes == 0 && usage.reserved_bytes == 0,
        "Synchronous refusal or completed Apply retained scheduler resources");
}
void published(Window &window, const std::shared_ptr<imagejxx::Document> &owner,
               bool path, bool asynchronous) {
  const auto result = owner->snapshot();
  pixels(result, path, true);
  check(owner->undo_steps() == 1 && owner->redo_steps() == 0 && result.dirty(),
        "Accepted Apply did not create exactly one history entry");
  check(window.workflow.size() == 1 && window.results.empty(),
        "Accepted Apply did not publish exactly one workflow record");
  const auto &record = window.workflow.front();
  check(record.at("operation") == "invert" && record.at("parameters").empty() &&
            record.at("all_slices") == false && record.at("slice_index") == 0,
        "Synchronous Apply changed the captured operation choices");
  if (path) {
    check(result.data().selection && *result.data().selection == triangle(),
          "Invert changed the original binary64 path");
    check(record.at("selection").at("path") ==
              Json::array({0., 2.2, 1.2, 1., 9.2, 1.2, 1., 2.2, 8.2, 4.}),
          "Workflow lost the original binary64 path commands");
  }
  const auto jobs = window.jobs();
  check(jobs.size() == (asynchronous ? 1u : 0u),
        "Execution-mode choice unexpectedly created or removed a scheduler job");
  if (asynchronous)
    check(jobs.front().state == JobState::Completed,
          "Asynchronous control did not reach Completed");
  idle_ledger(window);
  const auto workflow = window.workflow;
  owner->undo();
  pixels(owner->snapshot(), path, false);
  check(owner->undo_steps() == 0 && owner->redo_steps() == 1 &&
            window.workflow == workflow,
        "Undo changed the recorder or history cardinality");
  owner->redo();
  pixels(owner->snapshot(), path, true);
  check(owner->undo_steps() == 1 && owner->redo_steps() == 0 &&
            window.workflow == workflow,
        "Redo changed the recorder or history cardinality");
}
void whole_plane(const fs::path &directory, bool asynchronous) {
  Window window((directory / (asynchronous ? "whole-async.json" : "whole-sync.json")).u8string());
  window.updateSettings({{"processing_memory_mb", 1}, {"processing_workers", 1}}, false);
  auto owner = fixture(false);
  window.addNativeDocument(owner);
  window.recording = true;
  const auto source = owner->snapshot();
  check(imagejxx::desktop::legacy_job_base_workspace(source) == literal_base &&
            low_cap - literal_base == literal_remaining,
        "Independent legacy baseline no longer distinguishes the mask allowance");
  window.applyOperation("invert", Json::object(), false, asynchronous, owner->id);
  settle(window);
  published(window, owner, false, asynchronous);
}
void path_refusal_and_recovery(const fs::path &directory, bool asynchronous) {
  Window window((directory / (asynchronous ? "path-async.json" : "path-sync.json")).u8string());
  window.updateSettings({{"processing_memory_mb", 1}, {"processing_workers", 1}}, false);
  auto owner = fixture(true);
  window.addNativeDocument(owner);
  window.recording = true;
  const auto before = owner->snapshot();
  const auto workflow = window.workflow, results = window.results;
  const auto target = resolve_legacy_target(before, false, 0, "invert");
  const auto base = imagejxx::desktop::legacy_job_base_workspace(before);
  const auto mask = detail::target_mask_workspace(target, "invert");
  check(base == literal_base && base < low_cap && mask > low_cap - base,
        "Fixed 100x77 fixture does not isolate path-mask working refusal");
  std::cout << Json{{"case", asynchronous ? "sync-mask-async-control" : "sync-mask-direct"},
                    {"cap", low_cap}, {"base", base}, {"remaining", low_cap - base},
                    {"mask", mask}, {"required", base + mask}}.dump() << '\n';

  std::array<std::size_t, 3> callbacks{};
  ExecutionControl control;
  control.checkpoint = [&] { ++callbacks[0]; };
  control.progress = [&](auto, auto) { ++callbacks[1]; };
  control.stage_progress = [&](auto, auto, auto) { ++callbacks[2]; };
  std::string diagnostic;
  {
    ExecutionScope inherited(control);
    try { window.applyOperation("invert", Json::object(), false, asynchronous, owner->id); }
    catch (const std::invalid_argument &error) { diagnostic = error.what(); }
    check(diagnostic == "ImageJ shape exceeds working-byte limit",
          "Active path was not refused specifically by the finite mask allowance");
    check(callbacks == std::array<std::size_t, 3>{} && !inherited.dispatching_callback,
          "Host mask refusal dispatched inherited user callbacks or damaged their scope");
  }
  const auto after = owner->snapshot();
  check(after.shares_data(before) && after.revisions().matches(before.revisions(), Change::All) &&
            after.dirty() == before.dirty() && owner->undo_steps() == 0 &&
            owner->redo_steps() == 0 && after.data().selection == before.data().selection &&
            metadata_equal(after.display().properties, before.display().properties) &&
            window.workflow == workflow && window.results == results &&
            !window.busy() && window.jobs().empty(),
        "Refused synchronous/async request changed owner, history, tables, or jobs");
  check(window.settings().integer("processing_memory_mb") == 1,
        "Refused request changed the live host processing setting");
  idle_ledger(window);
  pixels(after, true, false);

  window.updateSettings({{"processing_memory_mb", 64}}, false);
  window.applyOperation("invert", Json::object(), false, asynchronous, owner->id);
  settle(window);
  published(window, owner, true, asynchronous);
}
void cleanup_success(const fs::path &directory) {
  // Every Window has been destroyed. Delete only the fresh root created by
  // this test, never an entry or a subtree; nonempty/failure scratch belongs
  // to C's retention step. Inspect names without following child links.
  std::error_code error;
  const auto status = fs::symlink_status(directory, error);
  if (error || !fs::is_directory(status) || fs::is_symlink(status)) {
    std::cout << "Scratch retained; owned root changed: " << directory.u8string() << '\n';
    return;
  }
  if (fs::remove(directory, error)) {
    std::cout << "Removed empty success scratch: " << directory.u8string() << '\n';
    return;
  }
  std::cout << "Success scratch retained for C: " << directory.u8string()
            << " (" << error.message() << ")\n";
  error.clear();
  std::size_t reported = 0;
  fs::directory_iterator entry(directory, fs::directory_options::none, error), end;
  for (; !error && entry != end && reported < 16; entry.increment(error), ++reported)
    std::cout << "Retained scratch entry: " << entry->path().filename().u8string() << '\n';
  if (!error && entry != end)
    std::cout << "Additional scratch entries retained; listing capped at 16\n";
  if (error) std::cout << "Scratch listing error: " << error.message() << '\n';
}
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 2, "Supply FRESH_DIRECTORY for synchronous host admission checks");
    const auto directory = fs::absolute(fs::u8path(argv[1])).lexically_normal();
    check(directory != directory.root_path() && !fs::exists(directory),
          "Synchronous host admission scratch must be fresh and non-root");
    check(fs::create_directories(directory), "Could not create owned admission scratch");
    for (const bool asynchronous : {false, true}) {
      whole_plane(directory, asynchronous);
      path_refusal_and_recovery(directory, asynchronous);
    }
    cleanup_success(directory);
    std::cout << "Synchronous/async Window mask admission: 4 groups, " << checks
              << " checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
