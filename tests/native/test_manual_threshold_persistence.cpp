#include "desktop.hpp"
#include <imagejxx/manual_threshold.hpp>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace imagejxx;
using imagejxx::desktop::Json;
using DesktopDocument = imagejxx::desktop::Document;
using NativeDocument = imagejxx::Document;

void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
template <class Function> void rejects(Function function) {
  bool rejected = false;
  try { function(); }
  catch (const std::invalid_argument &) { rejected = true; }
  catch (const std::out_of_range &) { rejected = true; }
  check(rejected, "Malformed threshold state was accepted");
}
Stack planes(int count = 3, int samples = 1) {
  Stack result;
  for (int z = 0; z < count; ++z) {
    Image image(3, 2, samples, PixelType::UInt16);
    for (std::size_t i = 0; i < image.pixels.size(); ++i)
      image.pixels[i] = 1000 * z + i;
    result.push_back(std::move(image));
  }
  return result;
}
Json record(double lower = -0.0, double upper = 0.0, int z = 2) {
  return {{"version", 1}, {"lower", lower}, {"upper", upper},
          {"t", 0}, {"c", 0}, {"z", z}};
}
void put(DisplayState &display, const Json &value) {
  display.properties[manual_threshold_property] =
      imagejxx::desktop::metadataFromJson(value);
}
ManualThresholdState state(const DocumentSnapshot &snapshot) {
  const auto value = manual_threshold_state(snapshot.display(),
                                            snapshot.data().dataset.shape());
  check(bool(value), "Enabled threshold state was lost");
  return *value;
}
void check_bounds(const ManualThresholdState &value, int z = 2) {
  check(value.position.t == 0 && value.position.c == 0 && value.position.z == z,
        "Captured threshold plane changed");
  check(value.range.lower == 0 && std::signbit(value.range.lower) &&
        value.range.upper == 0 && !std::signbit(value.range.upper),
        "Threshold bounds lost their signed-zero representation");
}

void validation_and_normalization() {
  const DatasetShape shape{1, 1, 3, 2, 3, 1};
  DisplayState display;
  put(display, record());
  display.validate(shape);
  display.normalize({1, 1, 3, 1, 1, 1});
  check_bounds(*manual_threshold_state(display, {1, 1, 3, 1, 1, 1}));
  auto reduced = display;
  reduced.normalize({1, 1, 2, 1, 1, 1});
  check(!manual_threshold_state(reduced, {1, 1, 2, 1, 1, 1}),
        "Removed threshold plane was not disabled");
  auto color = display;
  color.normalize({1, 1, 3, 1, 1, 3});
  check(!color.properties.count(manual_threshold_property),
        "Scalar-to-color conversion retained a threshold");
  for (Json bad : {Json(), Json::array(), record(2, 1),
                   Json{{"version", 1}}, record(0, 1, -1)}) {
    auto invalid = display;
    put(invalid, bad);
    rejects([&] { invalid.validate(shape); });
    rejects([&] { invalid.normalize({1, 1, 1, 1, 1, 3}); });
    check(invalid.properties.count(manual_threshold_property) == 1,
          "Normalization silently removed malformed threshold data");
  }
  auto unsupported = display;
  auto bad_version = record();
  bad_version["version"] = 2;
  put(unsupported, bad_version);
  rejects([&] { unsupported.normalize(shape); });
  auto wrong_plane = display;
  put(wrong_plane, record(0, 1, 3));
  rejects([&] { wrong_plane.validate(shape); });
}

void display_only_history_and_shape_changes() {
  NativeDocument owner(Dataset::from_stack(planes()));
  const auto before = owner.snapshot();
  auto display = before.display();
  put(display, record());
  owner.set_display(display);
  const auto enabled = owner.snapshot();
  check(enabled.shares_data(before) && enabled.data_identity() == before.data_identity() &&
        !enabled.dirty() && owner.undo_steps() == 0 &&
        enabled.revisions().matches(before.revisions(), Change::Data) &&
        enabled.revisions().display == before.revisions().display + 1,
        "Threshold display changes altered data, history or dirty state");
  check_bounds(state(enabled));

  auto edit = enabled.begin_edit();
  edit.replace_dataset(Dataset::from_stack(planes(2)));
  // A prepared edit may request its captured source display, whose threshold
  // position no longer exists. Camera coordinates here remain valid.
  owner.commit(edit, enabled.display(), true);
  auto shrunk = owner.snapshot();
  check(!manual_threshold_state(shrunk.display(), shrunk.data().dataset.shape()) &&
        owner.undo_steps() == 1 && shrunk.dirty(),
        "Shape-changing publication was blocked or retained the removed plane");
  owner.undo();
  check(!manual_threshold_state(owner.snapshot().display(),
                                 owner.snapshot().data().dataset.shape()),
        "Undo unexpectedly revived cleared display state");
  display = owner.snapshot().display();
  put(display, record());
  owner.set_display(display);
  const auto before_redo = owner.snapshot();
  owner.redo();
  const auto after_redo = owner.snapshot();
  check(!after_redo.display().properties.count(manual_threshold_property) &&
        after_redo.revisions().display == before_redo.revisions().display + 1,
        "Redo shape normalization did not advance the display revision");

  auto grow = owner.begin_edit();
  grow.replace_dataset(Dataset::from_stack(planes()));
  owner.commit(grow);
  display = owner.snapshot().display();
  put(display, record());
  owner.set_display(display);
  const auto before_undo = owner.snapshot();
  owner.undo();
  check(!owner.snapshot().display().properties.count(manual_threshold_property) &&
        owner.snapshot().revisions().display == before_undo.revisions().display + 1,
        "Undo shape normalization did not advance the display revision");
}

void atomic_threshold_reset_publication() {
  auto owner = std::make_shared<NativeDocument>(Dataset::from_stack(planes()));
  auto display = owner->snapshot().display();
  set_manual_threshold_state(display, owner->snapshot().data().dataset.shape(),
                              {{1001, 1004}, {0, 0, 1}});
  owner->set_display(display);
  auto validator = owner->constrain([](const DocumentData &, const DisplayState &view) {
    if (!view.properties.count(manual_threshold_property))
      throw std::invalid_argument("Reject threshold reset during publication");
  });
  Application app;
  app.adopt(owner);
  TargetRequest request;
  request.scope = TargetScope::Positions;
  request.positions = {{0, 0, 1}};
  ExecutionOptions options;
  options.reset_manual_threshold = true;
  auto prepared = app.prepare("threshold", app.resolve_target(request),
                              {{"lower", 1001.0}, {"upper", 1004.0}}, options);
  check(prepared.options().reset_manual_threshold &&
        !prepared.preview().display().properties.count(manual_threshold_property),
        "Prepared threshold preview did not clear threshold display");

  display = owner->snapshot().display();
  display.crosshair = {1, 1, 2};
  display.zoom = 2.5;
  display.pan[0] = {3, -4};
  display.properties["lut"] = std::string("Fire");
  display.properties["display_range"] =
      imagejxx::desktop::metadataFromJson(Json::array({-0.0, 4096.0}));
  owner->set_display(display);
  const auto before = owner->snapshot();
  rejects([&] { app.publish(prepared); });
  const auto rejected = owner->snapshot();
  check(prepared.active() && rejected.shares_data(before) &&
        rejected.data_identity() == before.data_identity() &&
        rejected.revisions().matches(before.revisions(), Change::All) &&
        rejected.dirty() == before.dirty() && owner->undo_steps() == 0 &&
        owner->redo_steps() == 0 &&
        imagejxx::desktop::metadataJson(rejected.display().properties).dump() ==
            imagejxx::desktop::metadataJson(before.display().properties).dump(),
        "Rejected threshold reset published pixels, display or history");

  validator.reset();
  const auto published = app.publish(prepared);
  const auto after = owner->snapshot();
  auto expected_properties = before.display().properties;
  expected_properties.erase(manual_threshold_property);
  check(published.document == owner && !published.created && !prepared.active() &&
        after.dirty() && owner->undo_steps() == 1 &&
        after.display().crosshair == before.display().crosshair &&
        after.display().zoom == before.display().zoom &&
        after.display().pan == before.display().pan &&
        imagejxx::desktop::metadataJson(after.display().properties).dump() ==
            imagejxx::desktop::metadataJson(expected_properties).dump() &&
        after.revisions().display == before.revisions().display + 1 &&
        after.data().dataset.plane({0, 0, 1})->pixels ==
            std::vector<double>({0, 255, 255, 255, 255, 0}) &&
        after.data().dataset.plane({0, 0, 0})->pixels ==
            before.data().dataset.plane({0, 0, 0})->pixels &&
        after.data().dataset.plane({0, 0, 2})->pixels ==
            before.data().dataset.plane({0, 0, 2})->pixels,
        "Atomic threshold publication lost concurrent display or target boundaries");
}

void roundtrip_and_legacy_defaults(const std::filesystem::path &directory) {
  auto document = std::make_shared<DesktopDocument>(planes(), "Threshold persistence");
  auto display = document->view();
  put(display, record());
  display.properties["lut"] = std::string("Fire");
  display.properties["display_range"] =
      imagejxx::desktop::metadataFromJson(Json::array({-0.0, 255.0}));
  Json custom = Json::array();
  for (int i = 0; i < 256; ++i)
    custom.push_back({i, 255 - i, i / 2});
  display.properties["custom_lut"] = imagejxx::desktop::metadataFromJson(custom);
  const auto original = document->nativeSnapshot();
  document->setNativeDisplay(display);
  const auto shown = document->nativeSnapshot();
  check(shown.shares_data(original) && !shown.dirty() && document->undoSteps() == 0,
        "Desktop threshold display changed pixels or history");
  const auto wire = imagejxx::desktop::documentJson(*document);
  check(wire.at(manual_threshold_property).dump() == record().dump(),
        "Threshold state was not projected into the session wire");
  check(!shown.data().metadata.extensions.count(manual_threshold_property),
        "Threshold display was duplicated as document metadata");
  auto restored = imagejxx::desktop::documentFromJson(wire);
  check_bounds(state(restored->nativeSnapshot()));
  const auto expected_display = imagejxx::desktop::metadataJson(display.properties).dump();
  check(imagejxx::desktop::metadataJson(restored->view().properties).dump() == expected_display,
        "Session JSON lost threshold, LUT or range representation");

  const auto session = directory / "threshold.ijx";
  const Json results = Json::array({Json{{"value", 7}}});
  const Json workflow = Json::array();
  imagejxx::desktop::saveSession(session.u8string(), {document}, results, workflow);
  Json restored_results, restored_workflow;
  const auto reopened = imagejxx::desktop::loadSession(
      session.u8string(), restored_results, restored_workflow);
  check(reopened.size() == 1 && restored_results == results && restored_workflow == workflow,
        "Session envelope changed");
  check_bounds(state(reopened[0]->nativeSnapshot()));
  check(imagejxx::desktop::metadataJson(reopened[0]->view().properties).dump() == expected_display &&
        !reopened[0]->dirty() && reopened[0]->undoSteps() == 0,
        "Binary session lost display state or loaded as a dirty edit");

  auto legacy_wire = wire;
  legacy_wire.erase(manual_threshold_property);
  auto legacy = imagejxx::desktop::documentFromJson(legacy_wire);
  check(!manual_threshold_state(legacy->view(), legacy->nativeSnapshot().data().dataset.shape()) &&
        !imagejxx::desktop::documentJson(*legacy).contains(manual_threshold_property),
        "Absent legacy v2 threshold did not remain absent and disabled");
  auto malformed = wire;
  malformed[manual_threshold_property] = nullptr;
  rejects([&] { imagejxx::desktop::documentFromJson(malformed); });

  auto state_edit = document->state();
  state_edit.metadata.erase(manual_threshold_property);
  document->commit(std::move(state_edit));
  const auto reset = document->nativeSnapshot();
  check(!reset.display().properties.count(manual_threshold_property) &&
        !reset.dirty() && document->undoSteps() == 0 &&
        reset.data_identity() == shown.data_identity(),
        "Legacy State threshold reset was classified as a data edit");
}

void replay_threshold_policy() {
  DesktopDocument document(planes(), "Threshold replay");
  document.setView({{"crosshair", {1, 1, 2}}});
  const auto original = document.nativeSnapshot();
  Json step{{"operation", "threshold"},
            {"parameters", {{"lower", 1001.0}, {"upper", 1004.0}}},
            {"all_slices", false}, {"slice_index", 1},
            {"selection", Json()}, {"dataset_policy", "preserve_untargeted"}};
  auto result = imagejxx::desktop::runWorkflow(document, Json::array({step}));
  check(result.frames[0].type == PixelType::UInt16 &&
        result.frames[2].pixels == original.data().dataset.plane({0, 0, 2})->pixels &&
        result.frames[0].pixels == original.data().dataset.plane({0, 0, 0})->pixels &&
        result.frames[1].pixels == std::vector<double>({0, 255, 255, 255, 255, 0}),
        "Threshold replay changed untargeted planes or recorded plane selection");

  Image roi(3, 2, 1, PixelType::UInt8);
  roi.pixels = {0, 255, 255, 0, 255, 0};
  step["selection"] = imagejxx::desktop::selectionJson(Selection::from_mask(roi));
  result = imagejxx::desktop::runWorkflow(document, Json::array({step}));
  check(result.frames[1].type == PixelType::UInt16 &&
        result.frames[1].pixels == std::vector<double>({1000, 255, 255, 1003, 255, 1005}) &&
        result.frames[2].pixels == original.data().dataset.plane({0, 0, 2})->pixels,
        "Threshold replay did not preserve irregular ROI exterior values");
  check(document.nativeSnapshot().shares_data(original) &&
        document.nativeSnapshot().data_identity() == original.data_identity() &&
        document.undoSteps() == 0 && !document.dirty(),
        "Workflow preparation published pixels into its live source");

  auto legacy = step;
  legacy.erase("dataset_policy");
  legacy["selection"] = nullptr;
  const auto old_result = imagejxx::desktop::runWorkflow(document, Json::array({legacy}));
  check(old_result.frames[0].type == PixelType::UInt8 &&
        old_result.frames[2].pixels.front() == 255,
        "Untagged threshold replay changed historical dtype conversion");

  DesktopDocument single(planes(1), "Whole threshold replay");
  auto whole = step;
  whole["slice_index"] = 0;
  whole["selection"] = nullptr;
  whole["parameters"] = {{"lower", 1.0}, {"upper", 4.0}};
  const auto converted = imagejxx::desktop::runWorkflow(single, Json::array({whole}));
  check(converted.frames[0].type == PixelType::UInt8 &&
        converted.frames[0].pixels == std::vector<double>({0, 255, 255, 255, 255, 0}),
        "Full-document tagged threshold replay did not produce UInt8");

  for (const Json policy : {Json("legacy_desktop"), Json("unknown"), Json(), Json(1)}) {
    auto bad = step;
    bad["dataset_policy"] = policy;
    int callbacks = 0;
    rejects([&] {
      imagejxx::desktop::runWorkflow(document, Json::array({step, bad}), [&] {
        ++callbacks;
        return false;
      });
    });
    check(callbacks == 0, "Invalid policy was not preflighted before workflow steps");
  }
  auto misuse = step;
  misuse["operation"] = "add";
  rejects([&] { imagejxx::desktop::runWorkflow(document, Json::array({misuse})); });
  misuse = step;
  misuse["schema_version"] = 1;
  rejects([&] { imagejxx::desktop::runWorkflow(document, Json::array({misuse})); });
}
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 2, "Supply a fresh threshold-persistence artifact directory");
    const auto directory = std::filesystem::absolute(std::filesystem::u8path(argv[1]));
    check(!std::filesystem::exists(directory), "Artifact directory must be fresh");
    std::filesystem::create_directories(directory);
    validation_and_normalization();
    display_only_history_and_shape_changes();
    atomic_threshold_reset_publication();
    roundtrip_and_legacy_defaults(directory);
    replay_threshold_policy();
    std::cout << "PASS manual_threshold_persistence\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL manual_threshold_persistence: " << error.what() << '\n';
    return 1;
  }
}
