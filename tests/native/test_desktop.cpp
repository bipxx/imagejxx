#include "selection_panel.hpp"
#include "window.hpp"
#include "headless_session_internal.hpp"
#include <chrono>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <imagejxx/binary.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <zlib.h>

using namespace imagejxx;
using namespace imagejxx::desktop;
using DesktopDocument = imagejxx::desktop::Document;

namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void testSnapshot() {
  DesktopDocument original(Stack{Image(4, 3, 1, PixelType::UInt8)});
  for (int i = 0; i < 8; ++i)
    original.commit(original.compute("add", {{"value", 1}}, false));
  original.undo();
  auto spectralEdit = original.state();
  spectralEdit.spectrum = FourierState(fft2(original.frame()));
  original.commit(std::move(spectralEdit));
  const auto undo = original.undoSteps(), redo = original.redoSteps();
  auto before = original.state().frames[0].pixels;
  auto copy = original.snapshot();
  check(copy.state().source->same_document(original.nativeSnapshot()),
        "Prepared worker state retains its source owner");
  check((copy.undoSteps() == 0) && (copy.redoSteps() == 0),
        "Worker snapshot must omit undo and redo histories");
  check(copy.state().source->revisions().matches(original.revisions()),
        "Prepared worker state retains its source revisions");
  check(copy.state().frames.shared(0) == original.state().frames.shared(0),
        "Worker snapshots share immutable plane buffers");
  auto replacement = copy.state().frames[0];
  replacement.pixels[0] = 200;
  auto prepared = copy.state();
  prepared.frames.replace(0, std::move(replacement));
  prepared.metadata["title"] = "Worker";
  check(copy.state().spectrum->shares_storage(*original.state().spectrum),
        "Worker snapshots share immutable Fourier buffers");
  auto spectrum = copy.state().spectrum->spectrum();
  spectrum.pixels[0] = 0;
  prepared.spectrum = FourierState(std::move(spectrum));
  copy.commit(std::move(prepared));
  check(original.state().frames[0].pixels == before &&
            original.title() == "Untitled" &&
            original.state().spectrum->spectrum().pixels[0] != 0.0,
        "Worker state must be independent of the live document");
  Json steps = Json::array(
      {{{"operation", "add"}, {"parameters", {{"value", 3}}}},
       {{"operation", "multiply"}, {"parameters", {{"value", 2}}}}});
  auto result = runWorkflow(original, steps);
  check(result.frames[0].pixels[0] == 20 &&
            original.state().frames[0].pixels == before &&
            original.undoSteps() == undo && original.redoSteps() == redo,
        "Workflow must preserve original pixels and histories");
  bool cancelled = false;
  try {
    runWorkflow(original, steps, [] { return true; });
  } catch (const std::runtime_error &) {
    cancelled = true;
  }
  check(cancelled && original.state().frames[0].pixels == before,
        "Cancelled workflow must preserve original pixels");
}

void testSharedHistory() {
  DesktopDocument document(Stack{Image(5, 4), Image(5, 4), Image(5, 4)});
  document.setView({{"crosshair", {2, 1, 1}}});
  auto original = document.state().frames;
  auto proposal = document.compute("add", {{"value", 10}}, false);
  check(proposal.frames.shared(0) == original.shared(0) &&
            proposal.frames.shared(2) == original.shared(2) &&
            proposal.frames.shared(1) != original.shared(1),
        "A local operation allocates only the edited plane");
  check((document.undoSteps() == 0) && !document.dirty(),
        "An uncommitted preview does not change history or dirty state");
  document.commit(std::move(proposal));
  auto changed = document.state().frames.shared(1);
  check(document.pixelStorage()["retained_bytes"] ==
                4 * 5 * 4 * sizeof(double) &&
            document.pixelStorage()["plane_buffers"] == 4,
        "Retained memory counts each shared pixel buffer once");
  auto metadataEdit = document.state();
  metadataEdit.metadata["title"] = "Renamed";
  document.commit(std::move(metadataEdit));
  check(document.state().frames.shared(1) == changed &&
            document.pixelStorage()["plane_buffers"] == 4,
        "Metadata history does not allocate pixel buffers");
  document.undo();
  document.undo();
  check(document.state().frames.shared(1) == original.shared(1),
        "Undo restores shared plane identity");
  document.redo();
  check(document.state().frames.shared(1) == changed,
        "Redo restores the committed plane buffer");
  auto invalid = document.state();
  invalid.frames.replace(0, Image(2, 2));
  const auto revision = document.revision();
  bool rejected = false;
  try {
    document.commit(std::move(invalid));
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  check(rejected && document.revision() == revision &&
            document.state().frames.shared(1) == changed,
        "Invalid edits fail before changing live pixels or history");
  DesktopDocument bounded(Stack{Image(5, 4)});
  bounded.setHistoryPolicy(1, 0);
  std::weak_ptr<const Image> retired = bounded.state().frames.shared(0);
  bounded.commit(bounded.compute("add", {{"value", 1}}, false));
  check(!retired.expired(), "Undo keeps replaced pixels alive");
  bounded.commit(bounded.compute("add", {{"value", 1}}, false));
  check(retired.expired(), "Pruning history releases obsolete pixel buffers");
}

void testViews() {
  Stack frames;
  for (int z = 0; z < 3; ++z) {
    Image f(5, 4, 1, PixelType::UInt16);
    for (int y = 0; y < 4; ++y)
      for (int x = 0; x < 5; ++x)
        f.at(x, y) = 100 * z + 10 * y + x;
    frames.push_back(std::move(f));
  }
  auto d = std::make_shared<DesktopDocument>(std::move(frames));
  auto identity = d->state().identity;
  auto revision = d->revision();
  d->setView({{"orthogonal", true}, {"crosshair", {3, 2, 1}}});
  check(!d->dirty() && (d->undoSteps() == 0) && d->revision() == revision &&
            d->state().identity == identity,
        "View navigation must not create pixel/history edits");
  check(d->plane(Plane::XZ, 2).at(3, 1) == 123 &&
            d->plane(Plane::YZ, 3).at(2, 1) == 123,
        "Desktop orthogonal coordinates");
  d->commit(d->compute("add", {{"value", 10}}, false));
  d->setView({{"crosshair", {1, 1, 2}}});
  d->undo();
  check(d->slice() == 2 && d->view().crosshair == std::array<int, 3>{1, 1, 2},
        "Pixel undo retains view navigation");
  d->redo();
  check(d->slice() == 2 && d->state().frames[1].at(3, 2) == 133,
        "Redo restores pixels without resetting navigation");
  auto restored = documentFromJson(documentJson(*d));
  check(restored->viewJson() == d->viewJson() && restored->id != d->id,
        "Session restores views with a new live identity");
  auto before = d->viewJson();
  bool rejected = false;
  try {
    d->setView({{"orthogonal", false}, {"crosshair", {99, 0, 0}}});
  } catch (const std::out_of_range &) {
    rejected = true;
  }
  check(rejected && d->viewJson() == before, "Invalid view changes are atomic");
  Window editor;
  editor.addDocument(d);
  auto info = editor.documentInfo(d->id);
  check(info["id"] == d->id && info["axis_sizes"]["Z"] == 3 &&
            !info.contains("pixels"),
        "Metadata inspection avoids pixel serialization");
  editor.addDocument(std::make_shared<DesktopDocument>(
      Stack{Image(5, 4, 1, PixelType::UInt16)}));
  editor.applyOperation("add", {{"value", 1}}, false, false, d->id);
  check(editor.document()->frame().at(0, 0) == 0 && d->frame().at(0, 0) == 201,
        "Editing an identified document does not switch active tabs");
}

void testSelectionFit() {
  DesktopDocument document(Stack{Image(40, 32)});
  auto calibrated = document.state();
  calibrated.metadata["calibration"].update(
      {{"pixel_width", .5}, {"pixel_height", 2.}, {"pixel_depth", 6.}});
  document.commit(std::move(calibrated));
  document.select(
      {{"kind", "rectangle"}, {"points", {{4, 3}, {12, 11}}}, {"width", 1}});
  const auto identity = document.state().identity;
  const auto revision = document.revisions();
  const auto undo = document.undoSteps();
  document.fitSelection({160, 100});
  check(std::abs(document.view().zoom - 6) < 1e-9 &&
            document.view().pan[0] == std::array<double, 2>{36, 108},
        "Selection fit centers the ROI at a calibrated display scale");
  check(document.state().identity == identity &&
            document.revisions().matches(revision, Change::Data) &&
            document.undoSteps() == undo,
        "Selection fit changes view state without editing pixels/history");
  document.setView({{"linked_zoom", false}, {"plane_zoom", {2, 3, 4}}});
  document.fitSelection({160, 100});
  check(document.view().planeZoom == std::array<double, 3>{6, 3, 4},
        "Independent selection fit retains other plane zooms");
  const auto before = document.viewJson();
  bool rejected = false;
  try {
    document.fitSelection({160, 0});
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  check(rejected && document.viewJson() == before,
        "Invalid fit extents leave the view unchanged");
}
void testOutlineSpans() {
  for (auto [w, h] : {std::pair<int, int>{1, 1}, {9, 7}, {32, 24}}) {
    for (int pattern = 0; pattern < 4; ++pattern) {
      Image mask(w, h, 1, PixelType::UInt8);
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
          mask.at(x, y) = pattern == 0   ? 0
                          : pattern == 1 ? 255
                          : pattern == 2
                              ? ((x + y) % 2) * 255
                              : (x > 1 && y > 1 && x < w - 2 && y < h - 2 &&
                                 !(x == w / 2 && y == h / 2)) *
                                    255;
      auto expected = binary_outline(mask);
      auto spans = selectionOutlineSpans(maskSelection(mask), w, h);
      Image actual(w, h, 1, PixelType::UInt8);
      for (const auto &span : spans) {
        check(span.width > 0 && span.height == 1 && span.x >= 0 &&
                  span.x + span.width <= w && span.y >= 0 && span.y < h,
              "Outline span must stay inside image bounds");
        for (int x = span.x; x < span.x + span.width; ++x)
          actual.at(x, span.y) = 255;
      }
      check(actual.pixels == expected.pixels,
            "Cached spans must exactly match bitmap outlines");
    }
  }
}

void testProjectionRangeDialog() {
  namespace fs = std::filesystem;
  const auto root = fs::temp_directory_path() / ("imagejxx-projection-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  check(fs::create_directory(root), "Projection fixture requires fresh scratch");
  struct Scratch { fs::path path; ~Scratch() { std::error_code e;
    if (fs::is_empty(path, e) && !e) fs::remove(path, e); } } scratch{root};
  Window window((root / "preferences.json").u8string());
  window.updateSettings({{"ui_scale", 1.}, {"auto_dpi", false}}, false);
  Stack planes;
  for (double v : {999., 2., 6., 888.}) {
    Image image(3, 2, 1, PixelType::UInt16);
    std::fill(image.pixels.begin(), image.pixels.end(), v);
    planes.push_back(std::move(image));
  }
  auto source = std::make_shared<DesktopDocument>(planes, "Range source");
  window.addDocument(source);
  const auto before = source->nativeSnapshot();
  HeadlessInputSession session(window);
  Json last; auto frame = [&] { last = session.frame(); };
  for (int i = 0; i < 3; ++i) frame();
  auto click = [&](const Json &control) {
    check(control.at("enabled").get<bool>(), "Projection control is disabled");
    const auto point = control.at("point").get<std::array<float,2>>();
    auto &io = ImGui::GetIO(); io.AddMousePosEvent(point[0], point[1]); frame();
    io.AddMouseButtonEvent(0, true); frame();
    io.AddMouseButtonEvent(0, false); frame();
    frame();
  };
  auto field = [&](const char *name, const char *value) {
    click(last.at("dialog").at("controls").at(name));
    auto &io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, true); io.AddKeyEvent(ImGuiKey_A, true); frame();
    io.AddKeyEvent(ImGuiKey_A, false); io.AddKeyEvent(ImGuiMod_Ctrl, false); frame();
    io.AddInputCharactersUTF8(value); frame();
    check(last.at("dialog").at("text").at(name) == value,
          "Projection field did not consume actual text input");
  };
  click(last.at("host_controls").at("menu.Image"));
  const auto stacks = last.at("host_controls").at("menu.Image.Stacks").at("point").get<std::array<float,2>>();
  ImGui::GetIO().AddMousePosEvent(stacks[0], stacks[1]);
  for (int i = 0; i < 30 && !last.at("host_controls").contains("menu.action.show_z_projection"); ++i) frame();
  click(last.at("host_controls").at("menu.action.show_z_projection"));
  check(last.at("dialog").at("values").at("first") == 1 &&
        last.at("dialog").at("values").at("last") == 4,
        "Z Project menu did not open full-range defaults");
  field("first", "2"); field("last", "3");
  click(last.at("dialog").at("controls").at("method"));
  click(last.at("dialog").at("controls").at("method.choice.mean"));
  click(last.at("dialog").at("controls").at("apply"));
  window.wait(); frame();
  check(window.documents().size() == 2 && window.document()->frame().type == PixelType::Float64,
        "Range projection did not create a separate output");
  for (double v : window.document()->frame().pixels)
    check(v == 4., "Projection included an excluded endpoint slice");
  const auto after = source->nativeSnapshot();
  check(after.shares_data(before) && after.revisions().matches(before.revisions(), Change::All) &&
        source->undoSteps() == 0 && source->redoSteps() == 0,
        "Range projection changed its source or history");
  window.setActiveIndex(0);
  window.action("show_z_projection"); for (int i = 0; i < 3; ++i) frame();
  field("first", "0"); click(last.at("dialog").at("controls").at("apply"));
  check(window.documents().size() == 2 && !window.busy() && last.at("dialog").at("open") == true,
        "Invalid range started a job or lost its draft");
  auto acknowledge = [&] {
    Json previous; int stable = 0;
    for (int i = 0; i < 8 && stable < 2; ++i) {
      frame(); const auto &current = last.at("error_modal").at("controls").at("ok");
      stable = !previous.is_null() && current.at("rect") == previous.at("rect") ? stable + 1 : 0;
      previous = current;
    }
    check(stable == 2, "Error acknowledgement geometry did not stabilize");
    const auto point = previous.at("point").get<std::array<float,2>>();
    ImGui::GetIO().AddMousePosEvent(point[0], point[1]); frame();
    check(ImGui::GetCurrentContext()->HoveredId == previous.at("id").get<ImGuiID>(),
          "Actual error acknowledgement pointer missed its current item");
    click(previous); frame();
  };
  acknowledge();
  if (!last.at("dialog").at("controls").contains("cancel"))
    std::cerr << "Projection modal diagnostic " << Json{{"dialog",last.at("dialog")},
        {"error_modal",last.at("error_modal")}}.dump() << '\n';
  click(last.at("dialog").at("controls").at("cancel"));
  check(window.documents().size() == 2 && !window.busy(), "Cancel published a projection");
  // An API-driven owner transition while this modal is open must not redirect
  // an Apply to the other document.
  window.action("show_z_projection"); for (int i = 0; i < 3; ++i) frame();
  window.setActiveIndex(1); for (int i = 0; i < 3; ++i) frame();
  click(last.at("dialog").at("controls").at("apply"));
  check(window.documents().size() == 2 && !window.busy() && last.at("error_modal").at("open") == true,
        "Stale projection draft was redirected to another owner");
  acknowledge(); click(last.at("dialog").at("controls").at("cancel"));
  session.close();
}
void testDerivedStacks() {
  Window editor;
  Stack frames;
  for (int z = 0; z < 4; ++z) {
    Image frame(6, 5, 4, PixelType::UInt16);
    for (int y = 0; y < frame.height; ++y)
      for (int x = 0; x < frame.width; ++x)
        for (int c = 0; c < frame.channels; ++c)
          frame.at(x, y, c) = 1000 * z + 100 * c + 10 * y + x;
    frames.push_back(std::move(frame));
  }
  auto source = std::make_shared<DesktopDocument>(frames, "Color volume");
  auto calibrated = source->state();
  calibrated.metadata["calibration"] = {
      {"pixel_width", .5}, {"pixel_height", .75}, {"pixel_depth", 3},
      {"unit", "um"},      {"frame_interval", 2}, {"time_unit", "s"}};
  source->commit(std::move(calibrated));
  source->setView({{"crosshair", {2, 1, 2}}});
  editor.addDocument(source);
  editor.action("split_channels");
  check(editor.documents().size() == 5,
        "RGBA splitting must create four documents");
  for (int c = 0; c < 4; ++c) {
    const auto &out = *editor.documents()[c + 1];
    check(out.state().frames.size() == 4 && out.slice() == 2 &&
              out.state().metadata["calibration"] ==
                  source->state().metadata["calibration"],
          "Channel splitting must retain every slice and its calibration");
    for (int z = 0; z < 4; ++z) {
      const auto &frame = out.state().frames[z];
      check(frame.channels == 1 && frame.type == PixelType::UInt16,
            "Channel splitting must preserve the source pixel type");
      for (int y = 0; y < frame.height; ++y)
        for (int x = 0; x < frame.width; ++x)
          check(
              frame.at(x, y) == source->state().frames[z].at(x, y, c),
              "Split channel values must match the corresponding source plane");
    }
  }
  editor.setActiveIndex(0);
  editor.action("substack", {{"first", 2}, {"last", 4}, {"step", 2}});
  const auto &out = *editor.document();
  check(out.state().frames.size() == 2 &&
            out.state().frames[0].pixels == frames[1].pixels &&
            out.state().frames[1].pixels == frames[3].pixels,
        "Substack must contain the requested slices");
  auto expected = source->state().metadata["calibration"];
  expected["pixel_depth"] = 6;
  // Regular-Z extraction retains world positions: output Z=0 is input Z=1.
  expected["origin"] = {0, 0, 3};
  check(out.state().metadata["calibration"] == expected &&
            source->state().metadata["calibration"]["pixel_depth"] == 3,
        "Substack must preserve calibration and scale depth by its stride");
  const auto &calibration = out.nativeSnapshot().data().dataset.calibration();
  check(calibration.voxel_to_world({0, 0, 0}) == std::array<double, 3>{0, 0, 3} &&
        calibration.voxel_to_world({0, 0, 1}) == std::array<double, 3>{0, 0, 9},
        "Substack world coordinates must match original retained slices 1 and 3");
}

void testVolumeFilterSelection() {
  Window editor;
  Stack frames(3, Image(14, 12, 1, PixelType::UInt16));
  for (auto &frame : frames)
    frame.at(6, 5) = 2000;
  auto source = std::make_shared<DesktopDocument>(frames);
  auto selected = Json{{"kind", "rectangle"}, {"points", {{5, 4}, {8, 7}}}};
  source->select(selected);
  editor.addDocument(source);
  editor.action("gaussian3d", {{"sigma_x", 1}, {"sigma_y", 1}, {"sigma_z", 1}});
  editor.wait();
  check(editor.error().empty(), "Selected 3D filter must succeed");
  auto expected = gaussian_blur3d(frames, 1, 1, 1, Border::Reflect);
  auto mask = selectionMask(selected, 14, 12);
  for (size_t z = 0; z < frames.size(); ++z) {
    auto result =
        paste(frames[z], canonical(std::move(expected[z])), 0, 0, &mask);
    check(source->state().frames[z].pixels == result.pixels,
          "3D filter must preserve all pixels outside the selected area");
  }
  source->undo();
  check(source->state().frames[0].pixels == frames[0].pixels,
        "Selected 3D filter must undo in one step");
}

void testWorkspace(const std::string &directory) {
  std::filesystem::create_directories(std::filesystem::u8path(directory));
  Window editor(directory + "/preferences.json");
  editor.addDocument(std::make_shared<DesktopDocument>(Stack{Image(4, 3)}));
  editor.addDocument(std::make_shared<DesktopDocument>(Stack{Image(4, 3)}));
  const auto workspace = directory + "/workspace.ijx";
  editor.action("save_workspace", {{"path", workspace}});
  Window reopened(directory + "/reopened.json");
  reopened.openFile(workspace);
  for (const auto &document : reopened.documents())
    check(document->path.empty(),
          "Workspace documents must not default to overwriting the workspace");
  const auto before = readBytes(workspace);
  bool rejected = false;
  try {
    reopened.action("save_document", {{"path", workspace}});
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  check(rejected && before == readBytes(workspace),
        "Single-document save must preserve the workspace file");
  const auto separate = directory + "/document.ijx";
  reopened.action("save_document", {{"path", separate}});
  check(reopened.document()->path == separate,
        "Separate document save must associate its own path");
  Window single(directory + "/single.json");
  single.openFile(separate);
  check(single.document()->path == separate,
        "Single-document sessions must retain their save path");
  single.applyOperation("add", {{"value", 1}}, false, false);
  single.action("save_document", {{"path", separate}});
  reopened.action("save_workspace", {{"path", workspace}});
}
void testPreferences(const std::string &directory) {
  std::filesystem::create_directories(directory);
  const auto path = directory + "/legacy-settings.json";
  writeJson(path, {{"history_limit", 3},
                   {"brush_width", 14},
                   {"foreground", "red"},
                   {"plugin_data", {{"keep", true}}}});
  Window editor(path);
  check(editor.settings().number("point_size") == 14 &&
            editor.settings().integer("history_limit") == 3 &&
            editor.settings().text("foreground") == "#ff0000",
        "Native startup migrates legacy defaults");
  editor.updateSettings({{"theme", "light"}, {"animation_fps", 11}});
  auto saved = readJson(path);
  check(saved["version"] == 1 && saved["extras"]["plugin_data"]["keep"] == true,
        "Versioned native files preserve legacy extension fields");
  auto before = editor.settings().values();
  auto bytes = readBytes(path);
  bool rejected = false;
  try {
    editor.updateSettings({{"brush_width", 20}, {"history_limit", 0}});
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  check(rejected && editor.settings().values() == before &&
            readBytes(path) == bytes,
        "Failed settings patches cannot partly update state or files");
  editor.addDocument(std::make_shared<DesktopDocument>(
      Stack{Image(5, 4, 3, PixelType::UInt8)}));
  editor.updateSettings({{"foreground", "#ff0000"}, {"text_size", 23}}, false);
  editor.action("fill");
  editor.wait();
  check(editor.document()->frame().at(0, 0, 0) == 255 &&
            editor.document()->frame().at(0, 0, 1) == 0,
        "Drawing consumes shared color defaults");
  editor.action("text", {{"x", 1}, {"y", 1}, {"text", "label"}});
  check(editor.document()->state().metadata["overlays"][0]["font_size"] == 23,
        "Text consumes its shared default");
  DesktopDocument bounded(Stack{Image(16, 16)});
  bounded.setHistoryPolicy(16, 2 * 16 * 16 * sizeof(double));
  for (int i = 0; i < 10; ++i)
    bounded.commit(bounded.compute("add", {{"value", 1}}, false));
  check(bounded.undoSteps() == 2 &&
            bounded.historyPixelBytes() == bounded.historyPixelBudget(),
        "Undo byte limits count obsolete shared buffers and prune history");
}
void testNativeRecords() {
  DesktopDocument base(Stack{Image(5, 4)});
  auto initial = base.state();
  Json style{{"color", "red"}, {"vendor", {{"id", UINT64_MAX}}}};
  Json selected{
      {"kind", "point"}, {"points", {{1, 1}}}, {"width", 2}, {"style", style}};
  initial.metadata["selection"] = selected;
  initial.metadata["rois"] = {{{"name", "sample"},
                               {"selection", selected},
                               {"color", "red"},
                               {"vendor", {{"id", UINT64_MAX}}}}};
  initial.metadata["overlays"] = {
      {{"kind", "text"}, {"text", "label"}, {"style", style}}};
  initial.metadata["native_properties"] = {
      {"nested", {{"values", {1, nullptr, UINT64_MAX}}}}};
  initial.metadata["vendor"] = style;
  initial.metadata["calibration"]["intensity"] = {{"coefficients", {0, 1.25}}};
  initial.spectrum = FourierState(fft2(base.frame()), std::array<int, 2>{4, 5});
  DesktopDocument document(std::move(initial));
  auto snapshot = document.nativeSnapshot();
  check(
      metadataJson(snapshot.data().selection->attributes) ==
              Json{{"style", style}} &&
          metadataJson(snapshot.data().metadata.extensions.at("vendor")) ==
              style &&
          snapshot.data().spectrum->shares_storage(*document.state().spectrum),
      "Desktop snapshots expose complete retained native records");
  auto edit = snapshot.begin_edit();
  edit.set_title("Changed");
  document.commitNative(edit);
  check(
      document.state().metadata["selection"] == selected &&
          document.state().metadata["vendor"] == style &&
          document.state().spectrum->shares_storage(*snapshot.data().spectrum),
      "Native metadata edits retain annotation and Fourier data");
  auto clearStyles = document.nativeSnapshot().begin_edit();
  auto rois = snapshot.data().rois;
  rois[0].attributes.clear();
  clearStyles.set_rois(rois);
  document.commitNative(clearStyles);
  check(!document.state().metadata["rois"][0].contains("color"),
        "Complete ROI replacements can explicitly clear styles");
  auto serialized = documentJson(document);
  auto reopened = documentFromJson(serialized);
  check(documentJson(*reopened) == serialized &&
            reopened->nativeSnapshot().data().spectrum->original_shape() ==
                std::optional<std::array<int, 2>>(std::array<int, 2>{4, 5}),
        "Legacy sessions round-trip complete records and original FFT extents");
  auto display = document.nativeSnapshot().display();
  display.properties = {{"lut", std::string("Ice")},
                        {"display_range", metadata_array({-1.0, 2.0})}};
  auto steps = document.undoSteps();
  auto dirty = document.dirty();
  document.setNativeDisplay(display);
  check(document.undoSteps() == steps && document.dirty() == dirty &&
            document.state().metadata["lut"] == "Ice",
        "Native display records do not change history or data identity");
}
void testSharedNativeOwner() {
  auto native = std::make_shared<imagejxx::Document>(
      Dataset::from_shared_planes({1, 1, 2, 4, 5, 1},
                                  Stack{Image(5, 4), Image(5, 4)}),
      "Shared");
  Window editor;
  editor.addNativeDocument(native);
  auto desktop = editor.document();
  Application headless;
  headless.adopt(native);
  auto retained = editor.nativeSnapshot();
  desktop->commit(desktop->state());
  check(!desktop->dirty() && desktop->undoSteps() == 0 &&
            editor.nativeSnapshot().shares_data(retained),
        "Reading and reapplying an unchanged native adapter does not "
        "synthesize metadata edits");
  check(
      editor.nativeDocument() == native &&
          retained.same_document(headless.snapshot()),
      "Desktop and headless controllers share the exact native document owner");
  auto edit = headless.begin_edit();
  edit.replace_plane({0, 0, 1}, fill(Image(5, 4), 12));
  headless.commit(edit);
  check(editor.nativeSnapshot().data().dataset.plane({0, 0, 1})->at(0, 0) ==
                12 &&
            desktop->state().frames[1].at(0, 0) == 12,
        "Headless edits are immediately visible through desktop views");
  editor.applyOperation("add", {{"value", 3}}, false, false);
  check(headless.snapshot().data().dataset.plane({0, 0, 0})->at(0, 0) == 3,
        "Desktop edits are immediately visible through the headless owner");
  auto stale = desktop->compute("add", {{"value", 20}}, false);
  auto rename = native->begin_edit();
  rename.set_title("Renamed");
  native->commit(rename);
  bool rejected = false;
  try {
    desktop->commit(std::move(stale));
  } catch (const RevisionConflict &) {
    rejected = true;
  }
  check(rejected && desktop->frame().at(0, 0) == 3,
        "Prepared desktop edits cannot overwrite newer native-owner changes");
  auto wrongThread = native->begin_edit();
  wrongThread.set_title("Wrong thread");
  bool threadRejected = false;
  std::thread worker([&] {
    try {
      native->commit(wrongThread);
    } catch (const std::runtime_error &) {
      threadRejected = true;
    }
  });
  worker.join();
  check(threadRejected && wrongThread.active() && desktop->title() == "Renamed",
        "Attached native owners enforce frontend publication thread rules");
  auto unsupported = native->begin_edit();
  unsupported.set_extension("title", std::string("Shadow"));
  rejected = false;
  try {
    native->commit(unsupported);
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  check(rejected && unsupported.active() && desktop->title() == "Renamed",
        "Headless publication cannot bypass attached frontend record "
        "constraints");
  editor.closeDocument(native->id, true);
  rejected = false;
  try {
    desktop->nativeSnapshot();
  } catch (const DocumentUnavailable &) {
    rejected = true;
  }
  check(rejected && retained.data().dataset.plane({0, 0, 0})->at(0, 0) == 0,
        "Closing rejects live facade access while retained snapshots remain "
        "readable");
  native->commit(unsupported);
  check(!unsupported.active(),
        "Detaching releases frontend capability constraints");
  std::shared_ptr<DesktopDocument> surviving;
  auto another =
      std::make_shared<imagejxx::Document>(Dataset::from_stack({Image(5, 4)}));
  {
    Window temporary;
    temporary.addNativeDocument(another);
    surviving = temporary.document();
  }
  rejected = false;
  try {
    surviving->nativeSnapshot();
  } catch (const DocumentUnavailable &) {
    rejected = true;
  }
  check(rejected,
        "Destroying a window closes membership for retained facade handles");
}
void testSelectionGestures() {
  for (const auto &mode : {"move", "escape", "stale"}) {
    auto native = std::make_shared<imagejxx::Document>(
        Dataset::from_stack({Image(32, 24, 1, PixelType::UInt16)}));
    imagejxx::Selection original("rectangle", {{8, 7}, {18, 15}}, 3);
    original.attributes["label"] = std::string("Retained");
    native->select(original);
    Window window;
    window.addNativeDocument(native);
    window.action("edit_selection");
    const auto initial = native->snapshot();
    auto events =
        Json::array({{{"type", "position"}, {"point", {13, 11}}},
                     {{"type", "button"}, {"name", "left"}, {"down", true}},
                     {{"type", "position"}, {"point", {16, 13}}},
                     {{"type", "wait"}, {"frames", 2}}});
    if (std::string(mode) == "escape") {
      events.push_back({{"type", "key"}, {"name", "Escape"}, {"down", true}});
      events.push_back({{"type", "key"}, {"name", "Escape"}, {"down", false}});
    } else if (std::string(mode) == "stale")
      events.push_back({{"type", "action"}, {"name", "select_all"}});
    events.push_back({{"type", "button"}, {"name", "left"}, {"down", false}});
    events.push_back({{"type", "wait"}, {"frames", 2}});
    const auto result = window.renderHeadless(3, false, false, events);
    const auto &trace = result["input_trace"];
    check(trace[2]["gesture_active"].get<bool>() &&
              trace[2]["selection_revision"] == initial.revisions().selection,
          "Drag changed the native source before release");
    check(!trace.back()["gesture_active"].get<bool>(),
          "Gesture not released/canceled");
    const auto final = native->snapshot();
    const auto &points = final.data().selection->points;
    if (std::string(mode) == "move") {
      const double dx = (number(trace[2]["mouse_screen"][0]) -
                         number(trace[0]["mouse_screen"][0])) /
                        number(trace[2]["canvas_scale"][0]);
      const double dy = (number(trace[2]["mouse_screen"][1]) -
                         number(trace[0]["mouse_screen"][1])) /
                        number(trace[2]["canvas_scale"][1]);
      check(std::abs(points[0][0] - (8 + dx)) < 1e-5 &&
                std::abs(points[0][1] - (7 + dy)) < 1e-5 &&
                std::abs(points[1][0] - (18 + dx)) < 1e-5 &&
                std::abs(points[1][1] - (15 + dy)) < 1e-5 &&
                std::abs(dx - 3) * number(trace[2]["canvas_scale"][0]) <= 1 &&
                std::abs(dy - 2) * number(trace[2]["canvas_scale"][1]) <= 1,
            "C++ canvas drag moved to incorrect coordinates");
      check(final.data().previous_selection ==
                std::optional<imagejxx::Selection>(original),
            "Drag lost previous selection");
      check(metadata_equal(final.data().selection->attributes,
                           original.attributes),
            "Drag lost selection style");
    } else if (std::string(mode) == "escape")
      check(final.data().selection ==
                    std::optional<imagejxx::Selection>(original) &&
                final.revisions().selection == initial.revisions().selection,
            "Escape published a preview");
    else
      check(points == std::vector<std::array<double, 2>>{{0, 0}, {32, 24}},
            "Stale drag overwrote the new selection");
    check(!final.dirty() && native->undo_steps() == 0 &&
              final.revisions().pixels == initial.revisions().pixels,
          "Selection gesture changed pixel history");
  }
}
} // namespace

void testBusySelectionProperties() {
  imagejxx::Document document(Dataset::from_stack({Image(32, 24)}));
  document.select(imagejxx::Selection("rectangle", {{8, 7}, {18, 15}}, 3));
  const auto source = document.snapshot();
  SelectionPanel panel;
  panel.show(source);
  panel.patch({{"width", 4},
               {"stroke_color", "red"},
               {"fill_color", "#80123456"},
               {"stroke_width", 0}});
  auto context = ImGui::CreateContext();
  try {
    auto &io = ImGui::GetIO();
    io.DisplaySize = {1250, 820};
    io.DeltaTime = 1.f / 60;
    unsigned char *pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    int publications = 0;
    auto frame = [&] {
      ImGui::NewFrame();
      panel.draw(source, true,
                 [&](const imagejxx::Selection &, const DocumentSnapshot &) {
                   ++publications;
                 });
      ImGui::Render();
    };
    auto click = [&](const Json &point) {
      io.AddMousePosEvent(float(number(point[0])), float(number(point[1])));
      frame();
      io.AddMouseButtonEvent(0, true);
      frame();
      io.AddMouseButtonEvent(0, false);
      frame();
    };
    frame();
    frame();
    const auto draft = panel.diagnostics()["preview"];
    for (const auto *control :
         {"Line width", "Stroke color", "Fill color", "Stroke width"}) {
      click(panel.diagnostics()["controls"][control]);
      io.AddKeyEvent(ImGuiMod_Ctrl, true);
      frame();
      io.AddKeyEvent(ImGuiKey_A, true);
      frame();
      io.AddKeyEvent(ImGuiKey_A, false);
      frame();
      io.AddKeyEvent(ImGuiMod_Ctrl, false);
      frame();
      io.AddInputCharactersUTF8("8");
      frame();
    }
    check(number(panel.diagnostics()["width"]) == 4 &&
              panel.diagnostics()["stroke_color"] == "red" &&
              panel.diagnostics()["fill_color"] == "#80123456" &&
              number(panel.diagnostics()["stroke_width"]) == 0 &&
              panel.diagnostics()["preview"] == draft,
          "Busy properties accepted typed geometry or ROI style edits");
    click(panel.diagnostics()["apply_point"]);
    check(panel.visible() && publications == 0,
          "Busy properties allowed Apply");
    click(panel.diagnostics()["cancel_point"]);
    check(!panel.visible() && publications == 0 &&
              document.snapshot().data().selection == source.data().selection,
          "Busy properties did not allow nondestructive Cancel");
  } catch (...) {
    ImGui::DestroyContext(context);
    throw;
  }
  ImGui::DestroyContext(context);
}

void testSelectionProperties() {
  for (const auto &mode : {"apply", "typed", "cancel", "stale"}) {
    auto native = std::make_shared<imagejxx::Document>(
        Dataset::from_stack({Image(32, 24, 1, PixelType::UInt16)}));
    imagejxx::Selection original("rectangle", {{8, 7}, {18, 15}}, 3);
    original.attributes["extension"] = std::string("Preserved");
    native->select(original);
    const auto source = native->snapshot();
    Window window;
    window.addNativeDocument(native);
    window.action("show_selection_properties");
    window.action("selection_properties_draft", {{"center", {16, 12}},
                                                 {"size", {12, 8}},
                                                 {"angle", 90},
                                                 {"width", 4},
                                                 {"label", "Adjusted"},
                                                 {"color", "#12ab34ff"}});
    auto rendered = window.renderHeadless(3);
    check(rendered["selection_properties"]["preview"]["kind"] ==
                  "rotated_rectangle" &&
              native->snapshot().data().selection == source.data().selection,
          "Properties draft changed native source");
    if (std::string(mode) == "typed") {
      const auto point =
          rendered["selection_properties"]["controls"]["Center X"];
      const auto events = Json::array(
          {{{"type", "position"}, {"space", "screen"}, {"point", point}},
           {{"type", "button"}, {"name", "left"}, {"down", true}},
           {{"type", "button"}, {"name", "left"}, {"down", false}},
           {{"type", "key"}, {"name", "Ctrl"}, {"down", true}},
           {{"type", "key"}, {"name", "A"}, {"down", true}},
           {{"type", "key"}, {"name", "A"}, {"down", false}},
           {{"type", "key"}, {"name", "Ctrl"}, {"down", false}},
           {{"type", "text"}, {"value", "17.125"}},
           {{"type", "key"}, {"name", "Enter"}, {"down", true}},
           {{"type", "key"}, {"name", "Enter"}, {"down", false}}});
      rendered = window.renderHeadless(3, false, false, events);
      check(
          number(rendered["selection_properties"]["preview"]["points"][0][0]) ==
                  17.125 &&
              native->snapshot().data().selection == source.data().selection,
          "Native numeric input did not stage exact geometry");
    }
    Json events = Json::array();
    if (std::string(mode) == "cancel") {
      events.push_back({{"type", "key"}, {"name", "Escape"}, {"down", true}});
      events.push_back({{"type", "key"}, {"name", "Escape"}, {"down", false}});
    } else if (std::string(mode) == "stale") {
      native->select(imagejxx::Selection("ellipse", {{2, 2}, {6, 6}}));
    } else {
      const auto point = rendered["selection_properties"]["apply_point"];
      events.push_back(
          {{"type", "position"}, {"space", "screen"}, {"point", point}});
      events.push_back({{"type", "button"}, {"name", "left"}, {"down", true}});
      events.push_back({{"type", "button"}, {"name", "left"}, {"down", false}});
    }
    rendered = window.renderHeadless(3, false, false, events);
    const auto final = native->snapshot();
    if (rendered["selection_properties"]["open"].get<bool>() || final.dirty() ||
        native->undo_steps() != 0)
      std::cerr << "Properties mode=" << mode << ": "
                << rendered["selection_properties"].dump()
                << "; dirty=" << final.dirty()
                << "; undo=" << native->undo_steps() << '\n';
    check(!rendered["selection_properties"]["open"].get<bool>() &&
              !final.dirty() && native->undo_steps() == 0,
          "Properties lifecycle or transient history changed");
    if (std::string(mode) == "cancel")
      check(final.data().selection == source.data().selection,
            "Cancel published properties draft");
    else if (std::string(mode) == "stale")
      check(final.data().selection->kind == "ellipse",
            "Stale properties replaced current selection");
    else {
      const auto &selection = *final.data().selection;
      check(selection.kind == "rotated_rectangle" && selection.width == 4 &&
                selection.box_frame()->center[0] ==
                    (std::string(mode) == "typed" ? 17.125 : 16) &&
                std::get<std::string>(selection.attributes.at("extension")) ==
                    "Preserved" &&
                final.data().previous_selection == source.data().selection &&
                final.revisions().selection == source.revisions().selection + 1,
            "Properties Apply lost geometry, attributes or previous selection");
    }
  }
}

void testBinaryOptionControls() {
  for (const auto *operation : {"erode", "dilate", "open", "close"}) {
    const auto image = fill(Image(9, 7, 1, PixelType::UInt8), 255);
    Window window;
    window.addDocument(std::make_shared<DesktopDocument>(Stack{image}));
    window.updateSettings({{"binary_iterations", 2},
                           {"binary_neighbors", 3},
                           {"binary_black_background", false},
                           {"binary_pad_edges", true}},
                          false);
    const auto id = "binary_" + std::string(operation);
    window.action("show_operation", {{"operation", id}});
    auto rendered = window.renderHeadless(3);
    auto dialog = rendered["operation_dialog"];
    check(dialog["parameters"]["iterations"] == 2 &&
              dialog["parameters"]["neighbors"] == 3 &&
              dialog["controls"]["neighbors"]["kind"] == "int" &&
              dialog["controls"]["black_background"]["kind"] == "bool",
          "Binary dialog did not consume preferences with typed controls");
    Json events = Json::array();
    auto click = [&](const Json &point) {
      events.push_back(
          {{"type", "position"}, {"point", point}, {"space", "screen"}});
      events.push_back({{"type", "button"}, {"name", "left"}, {"down", true}});
      events.push_back({{"type", "button"}, {"name", "left"}, {"down", false}});
    };
    click(dialog["controls"]["black_background"]["point"]);
    rendered = window.renderHeadless(3, false, false, events);
    check(rendered["operation_dialog"]["parameters"]["black_background"] ==
              true,
          "Actual binary checkbox input did not update its draft");
    window.recording = true;
    window.renderHeadless(3, true); // Click the real Preview control.
    check(window.document()->frame().pixels == image.pixels &&
              window.renderHeadless(3)["operation_dialog"]["has_preview"] ==
                  true,
          "Binary preview changed the document or did not produce pixels");
    window.renderHeadless(3, true); // Click the real Apply control.
    check(window.document()->frame().pixels ==
                  binary_morphology(image, operation, {2, 3, true, true})
                      .pixels &&
              window.workflow.size() == 1 &&
              window.workflow[0]["parameters"]["black_background"] == true &&
              window.document()->undoSteps() == 1,
          "Binary Apply lost effective options, recording or atomic history");
  }
}

namespace {
void appendClick(Json &events, const Json &point) {
  events.push_back(
      {{"type", "position"}, {"point", point}, {"space", "screen"}});
  events.push_back({{"type", "button"}, {"name", "left"}, {"down", true}});
  events.push_back({{"type", "button"}, {"name", "left"}, {"down", false}});
}

void appendTyping(Json &events, const Json &point, const char *text) {
  appendClick(events, point);
  events.push_back({{"type", "key"}, {"name", "Ctrl"}, {"down", true}});
  events.push_back({{"type", "key"}, {"name", "A"}, {"down", true}});
  events.push_back({{"type", "key"}, {"name", "A"}, {"down", false}});
  events.push_back({{"type", "key"}, {"name", "Ctrl"}, {"down", false}});
  events.push_back({{"type", "text"}, {"value", text}});
  events.push_back({{"type", "key"}, {"name", "Enter"}, {"down", true}});
  events.push_back({{"type", "key"}, {"name", "Enter"}, {"down", false}});
}

Json clickOperationControl(Window &window, const Json &point) {
  Json events = Json::array();
  appendClick(events, point);
  auto result = window.renderHeadless(3, false, false, events);
  window.wait();
  check(window.error().empty(), "Operation control reported an error");
  return result;
}

void checkStableViewCache(const Json &rendered, bool orthogonal) {
  const auto &trace = rendered.at("cache_trace");
  check(
      trace.is_array() && trace.size() >= 2,
      "Cache verification needs two actual frames from the same ImGui context");
  const auto &previous = trace[trace.size() - 2];
  const auto &current = trace.back();
  const auto canvas = [&](const Json &sample) {
    return orthogonal ? sample["orthogonal_planes"][0]
                      : sample["ordinary_canvas"];
  };
  const auto before = canvas(previous), after = canvas(current);
  bool stable =
      integer(current["frame"]) == integer(previous["frame"]) + 1 &&
      previous["data_revision"] == current["data_revision"] &&
      previous["slice_index"] == current["slice_index"] &&
      previous["mask_outline_builds"] == current["mask_outline_builds"];
  for (const auto *field :
       {"origin", "scale", "extent", "style_layers", "overlay_layers"})
    stable = stable && before[field] == after[field];
  const char *buildField = orthogonal ? "builds" : "overlay_builds";
  stable = stable && before[buildField] == after[buildField];
  if (!stable)
    std::cerr << "Consecutive view cache diagnostics: "
              << Json{{"orthogonal", orthogonal},
                      {"previous_frame", previous},
                      {"final_frame", current}}
                     .dump(2)
              << std::endl;
  check(stable,
        "Consecutive equal viewport frames did not reuse ROI/overlay coverage");
}
} // namespace

void testRoiStyleControls() {
  for (const auto *mode : {"apply", "fill_only", "positive_width", "cancel",
                           "escape", "stale", "noop", "invalid"}) {
    auto native = std::make_shared<imagejxx::Document>(
        Dataset::from_stack({fill(Image(32, 24, 1, PixelType::UInt16), 10)}));
    imagejxx::Selection original("rectangle", {{8, 7}, {18, 15}}, 3);
    original.attributes = {
        {"color", std::string("#12345678")},
        {"extension",
         metadata_object({{"flags", metadata_array({true, std::monostate{},
                                                    std::uint64_t(-1)})}})}};
    native->select(original);
    Window window;
    window.addNativeDocument(native);
    const auto source = native->snapshot();
    window.action("show_selection_properties");
    auto panel = window.renderHeadless(3)["selection_properties"];
    check(panel["area"] == true && panel["stroke_color"] == "#78123456" &&
              panel["fill_color"] == "none" &&
              number(panel["stroke_width"]) == 3,
          "Properties did not initialize ROI style from retained legacy color");
    Json events = Json::array();
    const std::string chosen(mode);
    if (chosen != "noop") {
      appendTyping(events, panel["controls"]["Stroke color"],
                   chosen == "fill_only" ? "none" : "#80403020");
      appendTyping(events, panel["controls"]["Fill color"], "#80123456");
      appendTyping(events, panel["controls"]["Stroke width"],
                   chosen == "positive_width" ? "2.5"
                   : chosen == "invalid"      ? "-1"
                                              : "0");
      panel = window.renderHeadless(3, false, false,
                                    events)["selection_properties"];
      check(native->snapshot().data().selection == source.data().selection &&
                native->snapshot().revisions().selection ==
                    source.revisions().selection,
            "Typed ROI style draft published before Apply");
      if (chosen == "invalid")
        check(!string(panel["error"]).empty() && panel["preview"].empty(),
              "Invalid typed stroke width left an applicable draft");
      else {
        const auto preview = selectionFromJson(panel["preview"]);
        const auto style = roi_style(preview);
        check(style.present && style.fill_color == RoiColor{18, 52, 86, 128} &&
                  style.stroke_color == (chosen == "fill_only"
                                             ? std::optional<RoiColor>{}
                                             : std::optional<RoiColor>{RoiColor{
                                                   64, 48, 32, 128}}) &&
                  style.stroke_width ==
                      (chosen == "positive_width" ? 2.5 : 0.) &&
                  preview.width == 3 && preview.points == original.points &&
                  metadata_equal(preview.attributes.at("extension"),
                                 original.attributes.at("extension")),
              "Real ROI style inputs produced incorrect typed draft");
      }
    }
    auto display = native->snapshot().display();
    display.zoom = 2;
    display.pan[0] = {4, 5};
    display.properties = {{"lut", std::string("Ice")},
                          {"display_range", metadata_array({3., 100.})}};
    window.setNativeDisplay(display);
    events = Json::array();
    if (chosen == "cancel")
      appendClick(events, panel["cancel_point"]);
    else if (chosen == "escape") {
      events.push_back({{"type", "key"}, {"name", "Escape"}, {"down", true}});
      events.push_back({{"type", "key"}, {"name", "Escape"}, {"down", false}});
    } else if (chosen == "stale")
      native->select(imagejxx::Selection("ellipse", {{2, 2}, {6, 6}}));
    else
      appendClick(events, panel["apply_point"]);
    auto rendered = window.renderHeadless(3, false, false, events);
    const auto final = native->snapshot();
    check(!final.dirty() && native->undo_steps() == 0 &&
              final.data().dataset.plane({0, 0, 0}) ==
                  source.data().dataset.plane({0, 0, 0}) &&
              final.display().zoom == 2 &&
              final.display().pan[0] == std::array<double, 2>{4, 5} &&
              metadata_equal(final.display().properties, display.properties),
          "ROI style editing changed pixels, history or concurrent display "
          "state");
    if (chosen == "invalid") {
      check(rendered["selection_properties"]["open"] == true &&
                final.data().selection == source.data().selection,
            "Disabled Apply published an invalid typed style");
      events = Json::array();
      appendClick(events, rendered["selection_properties"]["cancel_point"]);
      check(window.renderHeadless(3, false, false,
                                  events)["selection_properties"]["open"] ==
                false,
            "Invalid ROI style draft could not be cancelled");
    } else {
      check(rendered["selection_properties"]["open"] == false,
            "ROI style properties lifecycle left the panel open");
      if (chosen == "stale")
        check(final.data().selection->kind == "ellipse",
              "Stale style draft overwrote a newer selection");
      else if (chosen == "cancel" || chosen == "escape" || chosen == "noop")
        check(final.data().selection == source.data().selection &&
                  final.revisions().selection == source.revisions().selection,
              "Cancel/Escape/no-op published an ROI style edit");
      else
        check(*final.data().selection == selectionFromJson(panel["preview"]) &&
                  final.data().previous_selection == source.data().selection &&
                  final.revisions().selection ==
                      source.revisions().selection + 1,
              "Apply did not publish the complete ROI style exactly once");
    }
  }
}

void testRoiStyleFrozenWorkflow() {
  const auto image = fill(Image(32, 24, 1, PixelType::UInt16), 10);
  auto native =
      std::make_shared<imagejxx::Document>(Dataset::from_stack({image}));
  imagejxx::Selection line("line", {{8, 10}, {18, 10}}, 3);
  line.attributes["extension"] = metadata_object({{"id", std::uint64_t(-1)}});
  native->select(line);
  Window window;
  window.addNativeDocument(native);
  window.action("show_selection_properties");
  auto panel = window.renderHeadless(3)["selection_properties"];
  Json events = Json::array();
  appendTyping(events, panel["controls"]["Stroke color"], "#80123456");
  appendTyping(events, panel["controls"]["Stroke width"], "5");
  panel =
      window.renderHeadless(3, false, false, events)["selection_properties"];
  check(panel["area"] == false && panel["fill_color"] == "none",
        "Line properties offered an area fill");
  events = Json::array();
  appendClick(events, panel["apply_point"]);
  window.renderHeadless(3, false, false, events);
  const auto selected = *native->snapshot().data().selection;
  const auto mask = selected.mask(32, 24);
  check(selected.width == 3 && selection_processing_width(selected) == 5,
        "UI line style overwrote legacy width or failed to reach processing");
  window.recording = true;
  window.applyOperation("add", {{"value", 7}}, false, false);
  check(window.workflow.size() == 1 &&
            window.workflow[0]["selection"] == selectionJson(selected),
        "Recorded pixel operation omitted the styled selection");
  auto wanted = image;
  for (std::size_t i = 0; i < wanted.pixels.size(); ++i)
    if (mask.pixels[i])
      wanted.pixels[i] += 7;
  check(native->snapshot().data().dataset.plane({0, 0, 0})->pixels ==
            wanted.pixels,
        "Styled line width did not determine the operation's pixel mask");
  window.action("undo");
  native->select(imagejxx::Selection("point", {{0, 0}}));
  window.action("run_workflow");
  window.wait();
  check(window.error().empty() &&
            native->snapshot().data().dataset.plane({0, 0, 0})->pixels ==
                wanted.pixels &&
            selectionFromJson(window.workflow[0]["selection"]) == selected,
        "Frozen ROI style did not survive native workflow replay");
}

void testRoiStyleVisualLayers() {
  auto context = ImGui::CreateContext();
  try {
    auto &io = ImGui::GetIO();
    io.DisplaySize = {400, 300};
    io.DeltaTime = 1.f / 60;
    unsigned char *pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    const auto style = [](double width) {
      return with_roi_style(imagejxx::Selection("line", {{5, 5}, {15, 5}}, 3),
                            {{"stroke_color", std::string("#80ff0000")},
                             {"stroke_width", width}});
    };
    for (const auto strokeWidth : {0., 5.})
      for (const auto zoom : {1.f, 2.f}) {
        ImGui::NewFrame();
        // ImGui initializes the command buffer and texture state for this list.
        // A freshly constructed standalone ImDrawList is not ready for
        // clipping.
        auto &draw = *ImGui::GetForegroundDrawList();
        draw.Flags &= ~(ImDrawListFlags_AntiAliasedLines |
                        ImDrawListFlags_AntiAliasedFill);
        draw.PushClipRect({0, 0}, {200, 200});
        const auto visual = roiVisual(selectionJson(style(strokeWidth)), 32, 24,
                                      {0, 255, 0, 255});
        drawRoiVisual(visual, &draw, {0, 0}, {zoom, zoom * 3});
        float low = std::numeric_limits<float>::infinity(), high = -low;
        for (const auto &vertex : draw.VtxBuffer) {
          check(vertex.col == IM_COL32(255, 0, 0, 128),
                "Drawn ROI layer ignored its explicit color/alpha");
          low = std::min(low, vertex.pos.y);
          high = std::max(high, vertex.pos.y);
        }
        check(!draw.VtxBuffer.empty() &&
                  std::abs((high - low) -
                           (strokeWidth == 0 ? 1. : strokeWidth * zoom)) < 1e-5,
              "Cosmetic stroke must stay one device pixel; positive width "
              "follows X zoom");
        draw.PopClipRect();
        ImGui::EndFrame();
      }
    Image mask(32, 24, 1, PixelType::UInt8);
    for (int y = 2; y < 7; ++y)
      for (int x = 2; x < 7; ++x)
        mask.at(x, y) = 255;
    mask.at(4, 4) = 0;
    mask.at(10, 10) = 255;
    const auto first =
        with_roi_style(imagejxx::Selection::from_mask(mask),
                       {{"fill_color", std::string("#800ac81e")}});
    const auto second =
        with_roi_style(imagejxx::Selection("rectangle", {{20, 15}, {25, 20}}),
                       {{"fill_color", std::string("#4028dc50")}});
    const auto visual =
        roiVisual(selectionJson(first), 32, 24, {255, 0, 0, 255});
    Image covered(32, 24, 1, PixelType::UInt8);
    for (const auto &span : visual.fill)
      for (int x = span.x; x < span.x + span.width; ++x)
        covered.at(x, span.y) = 255;
    check(covered.pixels == mask.pixels && visual.edges.empty() &&
              visual.style.fill_color == RoiColor{10, 200, 30, 128},
          "Visual fill spans lost holes, disconnected components or fill-only "
          "semantics");
    ImGui::NewFrame();
    auto &draw = *ImGui::GetForegroundDrawList();
    draw.Flags &= ~ImDrawListFlags_AntiAliasedFill;
    draw.PushClipRect({0, 0}, {200, 200});
    drawRoiVisual(visual, &draw, {0, 0}, {2, 3});
    drawRoiVisual(roiVisual(selectionJson(second), 32, 24, {255, 0, 0, 255}),
                  &draw, {0, 0}, {2, 3});
    bool firstColor = false, secondColor = false;
    for (const auto &vertex : draw.VtxBuffer) {
      firstColor |= vertex.col == IM_COL32(10, 200, 30, 128);
      secondColor |= vertex.col == IM_COL32(40, 220, 80, 64);
      check(std::isfinite(vertex.pos.x) && std::isfinite(vertex.pos.y),
            "Visual layer produced nonfinite ImGui coordinates");
    }
    check(firstColor && secondColor,
          "Independent ROI visual layers lost their individual colors/alpha");
    draw.PopClipRect();
    ImGui::EndFrame();
  } catch (...) {
    ImGui::DestroyContext(context);
    throw;
  }
  ImGui::DestroyContext(context);
}

void testReopenedRoiStyleControls() {
  auto native = std::make_shared<imagejxx::Document>(
      Dataset::from_stack({Image(32, 24, 1, PixelType::UInt16)}));
  imagejxx::Selection original("rectangle", {{8, 7}, {18, 15}}, 3);
  original.attributes["color"] = std::string("#12345678");
  const auto styled =
      with_roi_style(original, {{"stroke_color", std::string("green")},
                                {"fill_color", std::string("#80123456")},
                                {"stroke_width", 0.}});
  native->select(styled);
  Window window;
  window.addNativeDocument(native);
  const auto source = native->snapshot();
  window.action("show_selection_properties");
  auto panel = window.renderHeadless(3)["selection_properties"];
  check(panel["legacy_color_enabled"] == false &&
            panel["legacy_width_enabled"] == false,
        "Reopened styled selection offered superseded legacy controls");
  Json events = Json::array();
  appendTyping(events, panel["controls"]["Color"], "#ff0000ff");
  appendTyping(events, panel["controls"]["Line width"], "9");
  const auto rendered = window.renderHeadless(3, false, false, events);
  const auto ignored = rendered["selection_properties"];
  const auto afterTyping = native->snapshot();
  const bool ownerUnchanged =
      afterTyping.data().selection == source.data().selection &&
      afterTyping.revisions().selection == source.revisions().selection;
  const bool draftUnchanged =
      ignored["open"] == true && ignored["error"] == "" &&
      ignored["preview"] == panel["preview"] &&
      ignored["color"] == "#12345678" && number(ignored["width"]) == 3;
  if (!ownerUnchanged || !draftUnchanged)
    std::cerr
        << "Reopened disabled-control diagnostics: "
        << Json{{"before", panel},
                {"after", ignored},
                {"before_selection_revision", source.revisions().selection},
                {"after_selection_revision", afterTyping.revisions().selection},
                {"input_trace", rendered["input_trace"]}}
               .dump(2)
        << std::endl;
  check(ownerUnchanged,
        "Disabled property typing reached a global image shortcut or changed "
        "the live selection");
  check(draftUnchanged,
        "Disabled legacy color/width controls changed the ROI style draft");
  events = Json::array();
  appendClick(events, ignored["apply_point"]);
  check(window.renderHeadless(
            3, false, false, events)["selection_properties"]["open"] == false &&
            native->snapshot().data().selection == source.data().selection &&
            native->snapshot().revisions().selection ==
                source.revisions().selection &&
            native->undo_steps() == 0 && !native->snapshot().dirty(),
        "Reopened unchanged style did not remain a transient no-op");
  const auto point = with_roi_style(
      imagejxx::Selection("point", {{8, 10}}, 3),
      {{"stroke_color", std::string("red")}, {"stroke_width", 1e300}});
  native->select(point);
  window.action("show_selection_properties");
  panel = window.renderHeadless(3)["selection_properties"];
  check(panel["stroke_width_enabled"] == false &&
            panel["legacy_width_enabled"] == true,
        "Point stroke controls superseded separate legacy marker sizing");
  events = Json::array();
  appendTyping(events, panel["controls"]["Stroke width"], "0");
  appendTyping(events, panel["controls"]["Line width"], "5");
  panel =
      window.renderHeadless(3, false, false, events)["selection_properties"];
  check(number(panel["width"]) == 5 &&
            metadata_equal(selectionFromJson(panel["preview"]).attributes,
                           point.attributes),
        "Point width input changed its retained opt-in style metadata");
  events = Json::array();
  appendClick(events, panel["apply_point"]);
  window.renderHeadless(3, false, false, events);
  const auto resizedPoint = *native->snapshot().data().selection;
  check(resizedPoint.width == 5 &&
            roi_style(resizedPoint).stroke_width == 1e300 &&
            resizedPoint.mask(32, 24).pixels ==
                imagejxx::Selection("point", {{8, 10}}, 5).mask(32, 24).pixels,
        "Point marker sizing did not remain independent of ROI stroke width");
}

void testRoiStyleViewCaches() {
  for (const auto orthogonal : {false, true}) {
    auto native = std::make_shared<imagejxx::Document>(
        Dataset::from_stack({Image(32, 24, 1, PixelType::UInt16),
                             Image(32, 24, 1, PixelType::UInt16)}));
    Image mask(32, 24, 1, PixelType::UInt8);
    for (int y = 2; y < 7; ++y)
      for (int x = 2; x < 7; ++x)
        mask.at(x, y) = 255;
    mask.at(4, 4) = 0;
    mask.at(10, 10) = 255;
    const auto first = with_roi_style(
        imagejxx::Selection::from_mask(mask),
        {{"stroke_color", std::string("#80ff0000")}, {"stroke_width", 0.}});
    const auto second =
        with_roi_style(imagejxx::Selection("rectangle", {{20, 15}, {25, 20}}),
                       {{"fill_color", std::string("#400000ff")},
                        {"stroke_color", std::string("green")},
                        {"stroke_width", 2.5}});
    const auto active = with_roi_style(
        imagejxx::Selection("line", {{8, 10}, {18, 10}}, 3),
        {{"stroke_color", std::string("#c8123456")}, {"stroke_width", 0.}});
    auto tx = native->begin_edit();
    tx.set_rois({{"Holed", first, {}, {}, {}, {}},
                 {"Separate", second, {}, {}, 0, {}}});
    tx.select(active);
    native->commit(tx);
    auto unstyled = std::make_shared<imagejxx::Document>(
        Dataset::from_stack({Image(32, 24, 1, PixelType::UInt16)}));
    const auto unstyledSource = unstyled->snapshot();
    Window window;
    window.addNativeDocument(unstyled);
    window.addNativeDocument(native);
    window.updateSettings({{"show_rois", true}}, false);
    window.action(
        "view",
        {{"orthogonal", orthogonal}, {"crosshair", {1, 2, 0}}, {"zoom", 2}});
    const auto layers = [&](const Json &frame) {
      return orthogonal ? frame["orthogonal_planes"][0]["style_layers"]
                        : frame["ordinary_canvas"]["style_layers"];
    };
    const auto checkOwner = [&](const Json &frame) {
      const auto selectedSource = native->snapshot();
      const auto dataRevision = [](const DocumentSnapshot &source) {
        const auto revisions = source.revisions();
        return revisions.pixels + revisions.metadata + revisions.selection;
      };
      const auto wantedRevision = dataRevision(selectedSource);
      check(wantedRevision != dataRevision(unstyledSource),
            "Multi-document owner trace needs distinguishable data revisions");
      bool correct = window.activeIndex() == 1 &&
                     window.nativeSnapshot().same_document(selectedSource);
      // These two owners have distinct data revisions, so every bounded actual
      // frame also identifies which owner the renderer kept active.
      for (const auto &sample : frame["cache_trace"])
        correct = correct && sample["data_revision"] == wantedRevision;
      if (!correct)
        std::cerr << "Multi-document view owner diagnostics: "
                  << Json{{"active_index", window.activeIndex()},
                          {"expected_data_revision", wantedRevision},
                          {"unstyled_data_revision",
                           dataRevision(unstyledSource)},
                          {"cache_trace", frame["cache_trace"]}}
                         .dump(2)
                  << std::endl;
      check(correct,
            "A fresh ImGui context changed the active styled document, even "
            "temporarily");
    };
    const auto rendered = window.renderHeadless(4);
    checkOwner(rendered);
    checkStableViewCache(rendered, orthogonal);
    const auto drawn = layers(rendered);
    check(drawn.size() == 3 &&
              drawn[0]["stroke_color"] == Json({255, 0, 0, 128}) &&
              drawn[1]["stroke_color"] == Json({0, 255, 0, 255}) &&
              drawn[1]["fill_color"] == Json({0, 0, 255, 64}) &&
              drawn[2]["stroke_color"] == Json({18, 52, 86, 200}) &&
              drawn[0]["cosmetic"] == true && drawn[1]["cosmetic"] == false &&
              drawn[2]["cosmetic"] == true &&
              integer(drawn[0]["device_spans"]) > 0 &&
              integer(drawn[0]["device_builds"]) > 0,
          "Ordinary/orthogonal XY drawing lost individual ROI styles or device "
          "contours");
    const auto repeatedFrame = window.renderHeadless(4);
    checkOwner(repeatedFrame);
    checkStableViewCache(repeatedFrame, orthogonal);
    const auto repeated = layers(repeatedFrame);
    check(repeatedFrame["mask_outline_builds"] ==
              rendered["mask_outline_builds"],
          "A new ImGui context rebuilt unchanged image-space ROI geometry");
    for (std::size_t i = 0; i < drawn.size(); ++i)
      for (const auto *field :
           {"stroke_color", "fill_color", "stroke_width", "cosmetic", "area",
            "fill_spans", "contour_edges", "role"})
        check(
            repeated[i][field] == drawn[i][field],
            "Reopening a headless context changed retained ROI geometry/style");
    if (orthogonal)
      check(rendered["orthogonal_planes"][1]["style_layers"].empty() &&
                rendered["orthogonal_planes"][2]["style_layers"].empty(),
            "XZ/YZ drew an unsupported XY ROI projection");

    const auto source = native->snapshot();
    const auto undoSteps = native->undo_steps();
    window.action("show_selection_properties");
    const auto openedFrame = window.renderHeadless(4);
    checkOwner(openedFrame);
    const auto opened = openedFrame["selection_properties"];
    check(opened["open"] == true &&
              selectionFromJson(opened["preview"]) == active,
          "Selection properties captured a different tab's owner");
    Json events = Json::array();
    appendTyping(events, opened["controls"]["Stroke color"], "#ff123456");
    const auto typedFrame = window.renderHeadless(3, false, false, events);
    checkOwner(typedFrame);
    const auto draft = typedFrame["selection_properties"];
    const auto wanted =
        with_roi_style(active, {{"stroke_color", std::string("#ff123456")}});
    check(
        draft["open"] == true && draft["error"] == "" &&
            selectionFromJson(draft["preview"]) == wanted &&
            native->snapshot().data().selection == source.data().selection,
        "Second-owner style typing did not remain a detached properties draft");
    for (int i = 0; i < 2; ++i) {
      const auto draftFrame = window.renderHeadless(4);
      checkOwner(draftFrame);
      const auto panel = draftFrame["selection_properties"];
      check(panel["open"] == true && panel["error"] == "" &&
                panel["preview"] == draft["preview"] &&
                native->snapshot().revisions().matches(source.revisions(),
                                                       Change::Data),
            "Recreating ImGui transiently selected a default tab and discarded "
            "the captured properties draft");
    }
    events = Json::array();
    appendClick(events, draft["apply_point"]);
    const auto appliedFrame = window.renderHeadless(3, false, false, events);
    const auto published = native->snapshot();
    check(appliedFrame["selection_properties"]["open"] == false &&
              window.activeIndex() == 1 &&
              window.nativeSnapshot().same_document(published) &&
              published.data().selection == wanted &&
              published.data().previous_selection == active &&
              published.revisions().selection ==
                  source.revisions().selection + 1 &&
              published.revisions().metadata == source.revisions().metadata &&
              published.revisions().pixels == source.revisions().pixels &&
              native->undo_steps() == undoSteps &&
              unstyled->snapshot().revisions().matches(
                  unstyledSource.revisions(), Change::Data) &&
              !unstyled->snapshot().data().selection &&
              unstyled->undo_steps() == 0,
          "Captured draft Apply did not publish once to the second owner while "
          "preserving the unstyled first owner");
  }
}

void testClosingActiveMiddleTab() {
  auto first = std::make_shared<imagejxx::Document>(
      Dataset::from_stack({fill(Image(32, 24, 1, PixelType::UInt16), 10)}),
      "A");
  auto middle = std::make_shared<imagejxx::Document>(
      Dataset::from_stack({fill(Image(32, 24, 1, PixelType::UInt16), 20)}),
      "B");
  auto last = std::make_shared<imagejxx::Document>(
      Dataset::from_stack({fill(Image(32, 24, 1, PixelType::UInt16), 30)}),
      "C");
  middle->select(with_roi_style(
      imagejxx::Selection("line", {{4, 6}, {12, 6}}, 3),
      {{"stroke_color", std::string("red")}, {"stroke_width", 0.}}));
  last->select(imagejxx::Selection("point", {{22, 18}}, 2));
  last->select(with_roi_style(
      imagejxx::Selection("line", {{16, 14}, {26, 14}}, 3),
      {{"stroke_color", std::string("green")}, {"stroke_width", 0.}}));
  const auto firstSource = first->snapshot(), middleSource = middle->snapshot(),
             lastSource = last->snapshot();
  check(firstSource.revisions().selection == 0 &&
            middleSource.revisions().selection == 1 &&
            lastSource.revisions().selection == 2 && !middleSource.dirty(),
        "Close-tab owner tracing needs distinct clean source revisions");

  Window window;
  window.addNativeDocument(first);
  window.addNativeDocument(middle);
  window.addNativeDocument(last);
  window.setActiveIndex(0);
  const auto tabPoint = [](const Json &rendered, std::uint64_t id, int index) {
    const auto &tabs = rendered.at("document_tabs");
    check(
        tabs.is_array() && index < int(tabs.size()) &&
            tabs[index]["document_id"] == id && tabs[index]["index"] == index &&
            tabs[index]["point"].is_array() && tabs[index]["point"].size() == 2,
        "Observed document tab headers do not match native membership");
    return tabs[index]["point"];
  };
  const auto starting = window.renderHeadless(4);
  const auto opened = clickOperationControl(window,
      starting.at("host_controls").at("tools.tool.hand").at("point"));
  const auto firstPoint = tabPoint(opened, firstSource.id(), 0),
             middlePoint = tabPoint(opened, middleSource.id(), 1);
  tabPoint(opened, lastSource.id(), 2);
  check(window.activeIndex() == 0 &&
            window.nativeSnapshot().same_document(firstSource),
        "Tab coordinate capture changed the selected first owner");

  Json events = Json::array();
  appendClick(events, firstPoint);
  events.push_back({{"type", "wait"}, {"frames", 3}});
  const auto selectedFirst = events.size() - 1;
  appendClick(events, middlePoint);
  events.push_back({{"type", "wait"}, {"frames", 3}});
  const auto selectedMiddle = events.size() - 1;
  // Document shortcuts require canvas/workspace focus, not a tab control.
  // Hand focus does not edit the retained selection or pixel history.
  const auto &canvas = opened.at("ordinary_canvas");
  appendClick(events, Json::array({
      canvas.at("origin")[0].get<double>() + canvas.at("scale")[0].get<double>() * .25,
      canvas.at("origin")[1].get<double>() + canvas.at("scale")[1].get<double>() * .25}));
  events.push_back({{"type", "wait"}, {"frames", 3}});
  events.push_back({{"type", "key"}, {"name", "Ctrl"}, {"down", true}});
  const auto closedMiddle = events.size();
  events.push_back({{"type", "key"}, {"name", "W"}, {"down", true}});
  events.push_back({{"type", "key"}, {"name", "W"}, {"down", false}});
  events.push_back({{"type", "key"}, {"name", "Ctrl"}, {"down", false}});
  // Capture each actual post-close frame, including the ImGui selection
  // transition, rather than observing only the final settled owner.
  for (int i = 0; i < 6; ++i)
    events.push_back({{"type", "wait"}});
  const auto promotedLast = events.size();
  appendClick(events, firstPoint);
  events.push_back({{"type", "wait"}, {"frames", 3}});
  const auto selectedFirstAgain = events.size() - 1;
  // The equal-width A/B/C headers put the surviving C header at B's observed
  // point after removal. Verify that actual position in the final diagnostics.
  appendClick(events, middlePoint);
  events.push_back({{"type", "wait"}, {"frames", 8}});
  const auto selectedLastAgain = events.size() - 1;
  const auto rendered = window.renderHeadless(4, false, false, events);
  const auto &trace = rendered.at("input_trace");
  check(trace.size() == events.size(),
        "Tab close scenario omitted actual input observations");
  const auto checkInputOwner = [&](std::size_t index,
                                   const DocumentSnapshot &source) {
    const auto &sample = trace[index];
    const auto selection = source.data().selection
                               ? selectionJson(*source.data().selection)
                               : Json::object();
    const bool correct =
        sample["selection"] == selection &&
        sample["selection_revision"] == source.revisions().selection &&
        sample["pixel_revision"] == source.revisions().pixels &&
        sample["dirty"] == false && sample["undo_steps"] == 0;
    if (!correct)
      std::cerr << "Close-tab input owner diagnostics: "
                << Json{{"event", index},
                        {"expected_document", source.id()},
                        {"expected_selection", selection},
                        {"expected_selection_revision",
                         source.revisions().selection},
                        {"input_sample", sample},
                        {"actual_frame_owners", [&] {
                          Json frames = Json::array();
                          for (const auto &frame : rendered.at("input_frames"))
                            if (get(frame,"event_index") == index)
                              frames.push_back({{"document_info",get(frame,"document_info")},
                                  {"keys_down",get(frame,"keys_down")},
                                  {"focus",get(frame,"focus")},
                                  {"phase",get(frame,"phase")}});
                          return frames;
                        }()}}
                       .dump(2)
                << std::endl;
    check(correct,
          "An actual tab click or middle-tab close selected a different owner");
  };
  checkInputOwner(selectedFirst, firstSource);
  checkInputOwner(selectedMiddle, middleSource);
  for (auto i = closedMiddle; i < promotedLast; ++i)
    checkInputOwner(i, lastSource);
  checkInputOwner(selectedFirstAgain, firstSource);
  checkInputOwner(selectedLastAgain, lastSource);

  bool retainedLast = window.activeIndex() == 1 &&
                      window.nativeSnapshot().same_document(lastSource);
  for (const auto &sample : rendered["cache_trace"])
    retainedLast = retainedLast && sample["data_revision"] == 2;
  if (!retainedLast)
    std::cerr << "Close-tab final owner diagnostics: "
              << Json{{"active_index", window.activeIndex()},
                      {"expected_document", lastSource.id()},
                      {"cache_trace", rendered["cache_trace"]}}
                     .dump(2)
              << std::endl;
  check(retainedLast,
        "The promoted last owner did not remain selected at the middle index");
  checkStableViewCache(rendered, false);
  check(window.error().empty() && window.documents().size() == 2 &&
            window.documents()[0]->id == firstSource.id() &&
            window.documents()[1]->id == lastSource.id() &&
            rendered["document_tabs"].size() == 2 &&
            tabPoint(rendered, firstSource.id(), 0) == firstPoint &&
            tabPoint(rendered, lastSource.id(), 1) == middlePoint,
        "Closing the middle tab lost surviving membership or header positions");
  bool unavailable = false;
  try {
    window.nativeSnapshot(middleSource.id());
  } catch (const std::out_of_range &) {
    unavailable = true;
  }
  check(unavailable, "The closed middle document still belongs to the editor");
  const auto checkPreserved = [](const auto &owner,
                                 const DocumentSnapshot &source) {
    const auto current = owner->snapshot();
    check(current.same_document(source) && current.shares_data(source) &&
              current.revisions().matches(source.revisions(), Change::Data) &&
              current.data().selection == source.data().selection &&
              current.data().previous_selection ==
                  source.data().previous_selection &&
              current.data().dataset.plane({0, 0, 0}) ==
                  source.data().dataset.plane({0, 0, 0}) &&
              !current.dirty() && owner->undo_steps() == 0 &&
              owner->redo_steps() == 0,
          "Tab selection or close changed retained source records or history");
  };
  checkPreserved(first, firstSource);
  checkPreserved(middle, middleSource);
  checkPreserved(last, lastSource);
}

void testOrderedOverlayStylesAndDeviceWidths() {
  for (const auto orthogonal : {false, true}) {
    auto native = std::make_shared<imagejxx::Document>(
        Dataset::from_stack({Image(32, 24, 1, PixelType::UInt16),
                             Image(32, 24, 1, PixelType::UInt16)}));
    const auto cosmetic = with_roi_style(
        imagejxx::Selection("line", {{8, 8}, {18, 8}}, 3),
        {{"stroke_color", std::string("#80ff0000")}, {"stroke_width", 0.}});
    const auto positive = with_roi_style(
        imagejxx::Selection("line", {{8, 14}, {18, 14}}, 3),
        {{"stroke_color", std::string("#80ff00ff")}, {"stroke_width", 5.}});
    const auto fill = with_roi_style(
        imagejxx::Selection("rectangle", {{5, 4}, {22, 20}}),
        {{"fill_color", std::string("#4000ffff")}, {"stroke_width", 0.}});
    auto positioned = selectionJson(with_roi_style(
        imagejxx::Selection("line", {{8, 18}, {18, 18}}),
        {{"stroke_color", std::string("green")}, {"stroke_width", 0.}}));
    positioned["slice_index"] = 1;
    Json overlays = Json::array({selectionJson(cosmetic),
                                 {{"kind", "rectangle"},
                                  {"points", {{6, 8}, {20, 18}}},
                                  {"width", 1},
                                  {"color", "#0000ff"}},
                                 {{"kind", "text"},
                                  {"points", {{10, 9}}},
                                  {"text", "M"},
                                  {"font_size", 8},
                                  {"color", "#00ff00"}},
                                 {{"kind", "roi"},
                                  {"selection", selectionJson(positive)},
                                  {"vendor", {{"id", UINT64_MAX}}}},
                                 selectionJson(fill),
                                 positioned});
    auto publish = [&] {
      std::vector<MetadataObject> records;
      for (const auto &overlay : overlays)
        records.push_back(metadataObjectFromJson(overlay));
      auto tx = native->begin_edit();
      tx.set_overlays(std::move(records));
      native->commit(tx);
    };
    publish();
    const auto original = native->snapshot();
    Window window;
    window.addNativeDocument(native);
    window.action(
        "view",
        {{"orthogonal", orthogonal}, {"crosshair", {1, 2, 0}}, {"zoom", 1}});
    const auto canvas = [&](const Json &frame) {
      return orthogonal ? frame["orthogonal_planes"][0]
                        : frame["ordinary_canvas"];
    };
    const auto cacheBuilds = [&](const Json &view) {
      return view[orthogonal ? "builds" : "overlay_builds"];
    };
    const auto firstFrame = window.renderHeadless(4);
    checkStableViewCache(firstFrame, orthogonal);
    auto view = canvas(firstFrame);
    const auto firstLayers = view["overlay_layers"];
    check(firstLayers.size() == 5,
          "A positioned overlay leaked onto another XY slice");
    const Json kinds{"line", "rectangle", "text", "roi", "rectangle"};
    const Json styled{true, false, false, true, true};
    for (std::size_t i = 0; i < firstLayers.size(); ++i)
      check(firstLayers[i]["index"] == i &&
                firstLayers[i]["kind"] == kinds[i] &&
                firstLayers[i]["styled"] == styled[i] &&
                firstLayers[i]["role"] == "overlay",
            "Styled, legacy and text annotations lost metadata draw order");
    check(integer(firstLayers[1]["raster_spans"]) > 0 &&
              integer(firstLayers[2]["raster_spans"]) > 0 &&
              firstLayers[0]["stroke_color"] == Json({255, 0, 0, 128}) &&
              firstLayers[3]["stroke_color"] == Json({255, 0, 255, 128}) &&
              firstLayers[4]["fill_color"] == Json({0, 255, 255, 64}) &&
              firstLayers[4]["device_bounds"].is_null(),
          "Mixed overlay layers lost raster annotations, nested style or "
          "fill-only semantics");
    const auto verifyWidth = [&](const Json &layers, double zoom) {
      for (const auto index : {0, 3}) {
        const auto &layer = layers[index];
        const auto &bounds = layer["device_bounds"];
        const double wanted = index == 0 ? 1. : 5 * zoom;
        check(layer["device_stroke_width"] == wanted && bounds.is_array() &&
                  bounds.size() == 4 &&
                  number(bounds[3]) - number(bounds[1]) == wanted &&
                  integer(layer["device_spans"]) > 0,
              "Overlay stroke width was baked into image pixels instead of "
              "resolved for this viewport");
      }
    };
    verifyWidth(firstLayers, 1);
    const auto repeatedFrame = window.renderHeadless(4);
    checkStableViewCache(repeatedFrame, orthogonal);
    const auto repeated = canvas(repeatedFrame);
    check(cacheBuilds(repeated) == cacheBuilds(view),
          "Identical viewport frames regenerated ordered overlay layers");
    verifyWidth(repeated["overlay_layers"], 1);
    window.action("view", {{"zoom", 2}});
    const auto zoomedFrame = window.renderHeadless(4);
    checkStableViewCache(zoomedFrame, orthogonal);
    view = canvas(zoomedFrame);
    verifyWidth(view["overlay_layers"], 2);
    for (const auto index : {0, 3})
      check(view["overlay_layers"][index]["device_key"][2] == 2 &&
                firstLayers[index]["device_key"][2] == 1,
            "Zoom reused an overlay contour keyed to a different device scale");
    const auto zoomRepeatedFrame = window.renderHeadless(4);
    checkStableViewCache(zoomRepeatedFrame, orthogonal);
    const auto zoomRepeated = canvas(zoomRepeatedFrame);
    verifyWidth(zoomRepeated["overlay_layers"], 2);
    check(cacheBuilds(zoomRepeated) == cacheBuilds(view),
          "Stable zoom regenerated unchanged image-space overlay layers");
    window.action("view", {{"crosshair", {1, 2, 1}}});
    const auto sliced = window.renderHeadless(4);
    checkStableViewCache(sliced, orthogonal);
    const auto sliceLayers = canvas(sliced)["overlay_layers"];
    check(sliceLayers.size() == 6 && sliceLayers[5]["index"] == 5 &&
              sliceLayers[5]["stroke_color"] == Json({0, 255, 0, 255}),
          "XY slicing did not rebuild positioned overlay visibility");
    if (orthogonal)
      check(sliced["orthogonal_planes"][1]["overlay_layers"].empty() &&
                sliced["orthogonal_planes"][2]["overlay_layers"].empty(),
            "XZ/YZ drew unprojected XY annotations");
    window.action("view", {{"crosshair", {1, 2, 0}}});
    const auto beforeOrder =
        renderImage(*original.data().dataset.plane({0, 0, 0}),
                    {{"overlays", overlays}, {"slice_index", 0}});
    std::swap(overlays[0], overlays[1]);
    publish();
    const auto reordered = canvas(window.renderHeadless(4))["overlay_layers"];
    check(reordered[0]["styled"] == false && reordered[1]["styled"] == true &&
              reordered[2]["kind"] == "text" && reordered[3]["kind"] == "roi",
          "Metadata publication did not refresh mixed overlay draw order");
    const auto afterOrder =
        renderImage(*native->snapshot().data().dataset.plane({0, 0, 0}),
                    {{"overlays", overlays}, {"slice_index", 0}});
    check(beforeOrder.pixels != afterOrder.pixels &&
              native->snapshot().data().dataset.plane({0, 0, 0}) ==
                  original.data().dataset.plane({0, 0, 0}),
          "Overlapping annotation order must affect composition without "
          "modifying source pixels");
  }
}

void testMixedOverlayDrawVertices() {
  const auto styled = [](const char *color, double width, int y) {
    return selectionJson(with_roi_style(
        imagejxx::Selection("line", {{8, double(y)}, {18, double(y)}}, 3),
        {{"stroke_color", std::string(color)}, {"stroke_width", width}}));
  };
  const Json metadata{
      {"slice_index", 0},
      {"overlays",
       Json::array(
           {styled("#80ff0000", 0, 8),
            {{"kind", "rectangle"},
             {"points", {{6, 8}, {20, 18}}},
             {"width", 1},
             {"color", "#0000ff"}},
            {{"kind", "text"},
             {"points", {{10, 9}}},
             {"text", "M"},
             {"font_size", 8},
             {"color", "#00ff00"}},
            {{"kind", "roi"}, {"selection", styled("#80ff00ff", 5, 14)}},
            selectionJson(with_roi_style(
                imagejxx::Selection("rectangle", {{5, 4}, {22, 20}}),
                {{"fill_color", std::string("#4000ffff")}}))})}};
  check(hasStyledOverlays(metadata),
        "Mixed styled overlays did not enable ordered device drawing");
  const auto layers = overlayVisuals(metadata, 32, 24);
  const std::array<ImU32, 5> colors{
      IM_COL32(255, 0, 0, 128), IM_COL32(0, 0, 255, 255),
      IM_COL32(0, 255, 0, 255), IM_COL32(255, 0, 255, 128),
      IM_COL32(0, 255, 255, 64)};
  auto context = ImGui::CreateContext();
  try {
    auto &io = ImGui::GetIO();
    io.DisplaySize = {400, 300};
    io.DeltaTime = 1.f / 60;
    unsigned char *pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ImGui::NewFrame();
    auto &draw = *ImGui::GetForegroundDrawList();
    draw.Flags &= ~ImDrawListFlags_AntiAliasedFill;
    draw.PushClipRect({0, 0}, {200, 200});
    check(layers.size() == colors.size(),
          "Mixed view dropped a styled, legacy or text layer");
    for (std::size_t i = 0; i < layers.size(); ++i) {
      const auto before = draw.VtxBuffer.Size;
      drawOverlayVisual(layers[i], &draw, {0, 0}, {2, 2});
      check(layers[i].index == i && draw.VtxBuffer.Size > before,
            "Ordered overlay layer did not append actual ImGui vertices");
      for (int j = before; j < draw.VtxBuffer.Size; ++j) {
        const auto actual = draw.VtxBuffer[j].col;
        check((actual & ~IM_COL32_A_MASK) == (colors[i] & ~IM_COL32_A_MASK) &&
                  (actual & IM_COL32_A_MASK) != 0 &&
                  (i == 2 || actual == colors[i]),
              "Drawn mixed annotation vertices lost their original order, "
              "color or alpha coverage");
      }
    }
    draw.PopClipRect();
    ImGui::EndFrame();
  } catch (...) {
    ImGui::DestroyContext(context);
    throw;
  }
  ImGui::DestroyContext(context);
}

void testNestedRoiStyleRenderingAndRecords() {
  Image rgb(32, 24, 3, PixelType::UInt8);
  for (std::size_t i = 0; i < 32 * 24; ++i) {
    rgb.pixels[i * 3] = 100;
    rgb.pixels[i * 3 + 1] = 30;
    rgb.pixels[i * 3 + 2] = 210;
  }
  auto native =
      std::make_shared<imagejxx::Document>(Dataset::from_stack({rgb}));
  const auto styled =
      with_roi_style(imagejxx::Selection("rectangle", {{8, 7}, {18, 15}}, 3),
                     {{"fill_color", std::string("#80123456")},
                      {"stroke_color", std::string("none")},
                      {"stroke_width", 0.}});
  auto nested = styled.attributes;
  nested["kind"] = styled.kind;
  nested["width"] = styled.width;
  nested["points"] =
      metadata_array({metadata_array({8., 7.}), metadata_array({18., 15.})});
  const MetadataObject overlay{
      {"kind", std::string("roi")},
      {"selection", metadata_object(std::move(nested))},
      {"vendor", metadata_object({{"id", std::uint64_t(-1)}})}};
  auto tx = native->begin_edit();
  tx.set_overlays({overlay});
  native->commit(tx);
  Window window;
  window.addNativeDocument(native);
  const auto record = documentJson(*window.document());
  const auto reopened = documentFromJson(record);
  check(metadata_equal(reopened->nativeSnapshot().data().overlays[0], overlay),
        "Native JSON/session record adapter lost a valid nested ROI style");
  window.action("flatten");
  check(window.document()->frame().pixels ==
                render_roi_style(rgb, styled).pixels &&
            native->snapshot().data().dataset.plane({0, 0, 0})->pixels ==
                rgb.pixels,
        "Nested styled overlay did not reach alpha flattening or mutated its "
        "source");
}

void testConversionWeightControls() {
  Image rgba(9, 8, 4, PixelType::UInt8);
  for (int y = 0; y < rgba.height; ++y)
    for (int x = 0; x < rgba.width; ++x) {
      rgba.at(x, y, 0) = 120;
      rgba.at(x, y, 1) = 40;
      rgba.at(x, y, 2) = 20;
      rgba.at(x, y, 3) = x + y;
    }
  auto native =
      std::make_shared<imagejxx::Document>(Dataset::from_stack({rgba}));
  native->select(imagejxx::Selection("rectangle", {{1, 1}, {4, 5}}));
  const auto source = native->snapshot();
  Window window;
  window.addNativeDocument(native);
  window.action("show_operation", {{"operation", "convert_image"},
                                   {"parameters", {{"dtype", "float32"}}}});
  auto dialog = window.renderHeadless(3)["operation_dialog"];
  check(dialog["controls"]["weighted_rgb"]["kind"] == "bool" &&
            dialog["controls"]["red_weight"]["kind"] == "float" &&
            dialog["parameters"]["weighted_rgb"] == false,
        "Conversion weights did not expose typed native controls");
  Json events = Json::array();
  appendClick(events, dialog["controls"]["weighted_rgb"]["point"]);
  appendTyping(events, dialog["controls"]["red_weight"]["point"], "0.5");
  appendTyping(events, dialog["controls"]["green_weight"]["point"], "0.25");
  appendTyping(events, dialog["controls"]["blue_weight"]["point"], "0.25");
  dialog = window.renderHeadless(3, false, false, events)["operation_dialog"];
  const auto parameters = dialog["parameters"];
  check(parameters["weighted_rgb"] == true &&
            number(parameters["red_weight"]) == 0.5 &&
            number(parameters["green_weight"]) == 0.25 &&
            number(parameters["blue_weight"]) == 0.25,
        "Actual conversion checkbox/numeric input did not update the draft");
  clickOperationControl(window, dialog["preview_point"]);
  dialog = window.renderHeadless(3)["operation_dialog"];
  check(dialog["has_preview"] == true && native->undo_steps() == 0 &&
            !native->snapshot().dirty() &&
            native->snapshot().data().dataset.plane({0, 0, 0})->pixels ==
                rgba.pixels,
        "Conversion Preview changed the shared native owner");
  window.updateSettings({{"conversion_red_weight", 1},
                         {"conversion_green_weight", 0},
                         {"conversion_blue_weight", 0}},
                        false);
  window.recording = true;
  clickOperationControl(window, dialog["apply_point"]);
  const auto applied = native->snapshot();
  const auto converted = applied.data().dataset.plane({0, 0, 0});
  check(
      converted->type == PixelType::Float32 && converted->channels == 1 &&
          converted->pixels == std::vector<double>(9 * 8, 75) &&
          applied.data().selection == source.data().selection &&
          native->undo_steps() == 1 && applied.dirty() &&
          window.workflow.size() == 1 &&
          window.workflow[0]["parameters"] == parameters &&
          window.workflow[0]["all_slices"] == true,
      "Conversion Apply lost frozen weights, selection, recording or history");
  window.action("undo");
  check(native->snapshot().data().dataset.plane({0, 0, 0})->pixels ==
                rgba.pixels &&
            native->snapshot().data().selection == source.data().selection,
        "Conversion undo did not restore the native RGBA source and selection");
}

void testConversionRangeControls() {
  Image first(5, 4, 1, PixelType::UInt16), second = first;
  for (std::size_t i = 0; i < first.pixels.size(); ++i) {
    first.pixels[i] = double(i * 10);
    second.pixels[i] = double(i * 10 + 100);
  }
  const std::vector<double> firstExpected{0,   0,   0,   25,  51,  76,  101,
                                          127, 152, 177, 203, 228, 253, 255,
                                          255, 255, 255, 255, 255, 255};
  const std::vector<double> secondExpected{203, 228, 253, 255, 255, 255, 255,
                                           255, 255, 255, 255, 255, 255, 255,
                                           255, 255, 255, 255, 255, 255};
  auto native = std::make_shared<imagejxx::Document>(
      Dataset::from_stack({first, second}));
  native->select(imagejxx::Selection("rectangle", {{1, 1}, {3, 3}}));
  const auto source = native->snapshot();
  Window window;
  window.addNativeDocument(native);
  window.action("display_range", {{"minimum", 0}, {"maximum", 190}});
  window.action("show_operation", {{"operation", "convert_image"}});
  auto dialog = window.renderHeadless(3)["operation_dialog"];
  check(dialog["parameters"]["range_policy"] == "explicit" &&
            number(dialog["parameters"]["range_max"]) == 190,
        "Conversion dialog did not capture the native display range");
  Json events = Json::array();
  appendTyping(events, dialog["controls"]["range_min"]["point"], "20");
  appendTyping(events, dialog["controls"]["range_max"]["point"], "120");
  dialog = window.renderHeadless(3, false, false, events)["operation_dialog"];
  const auto parameters = dialog["parameters"];
  check(number(parameters["range_min"]) == 20 &&
            number(parameters["range_max"]) == 120,
        "Actual conversion range input did not stage the explicit limits");
  window.action("display_range", {{"minimum", 0}, {"maximum", 50}});
  clickOperationControl(window, dialog["preview_point"]);
  dialog = window.renderHeadless(3)["operation_dialog"];
  check(dialog["has_preview"] == true && native->undo_steps() == 0 &&
            native->snapshot().data().dataset.plane({0, 0, 0})->pixels ==
                first.pixels &&
            native->snapshot().data().dataset.plane({0, 0, 1})->pixels ==
                second.pixels &&
            !native->snapshot().dirty(),
        "Conversion range Preview published pixels/history");
  window.recording = true;
  clickOperationControl(window, dialog["apply_point"]);
  const auto applied = native->snapshot();
  check(
      applied.data().dataset.plane({0, 0, 0})->pixels == firstExpected &&
          applied.data().dataset.plane({0, 0, 1})->pixels == secondExpected &&
          applied.data().dataset.plane({0, 0, 0})->type == PixelType::UInt8 &&
          applied.data().selection == source.data().selection &&
          native->undo_steps() == 1 && window.workflow.size() == 1 &&
          window.workflow[0]["parameters"] == parameters &&
          window.workflow[0]["all_slices"] == true,
      "Conversion Apply changed range/ROI semantics or omitted a stack plane");
  window.recording = false;
  window.action("undo");
  check(native->snapshot().data().dataset.plane({0, 0, 0})->pixels ==
                first.pixels &&
            native->snapshot().data().dataset.plane({0, 0, 1})->pixels ==
                second.pixels,
        "Conversion range undo did not restore the complete native stack");
  window.action("run_workflow");
  window.wait();
  check(
      window.error().empty() &&
          native->snapshot().data().dataset.plane({0, 0, 0})->pixels ==
              firstExpected &&
          native->snapshot().data().dataset.plane({0, 0, 1})->pixels ==
              secondExpected &&
          native->undo_steps() == 1,
      "Conversion replay did not retain the typed range and whole-stack edit");
}

void testInversionControls() {
  for (const bool chooseRange : {false, true}) {
    Image image(4, 1, 1, PixelType::UInt16);
    image.pixels = chooseRange ? std::vector<double>{100, 200, 50, 0}
                               : std::vector<double>{100, 200, 500, 1000};
    const std::vector<double> expected =
        chooseRange ? std::vector<double>{155, 55, 205, 255}
                    : std::vector<double>{3995, 3895, 3595, 3095};
    auto native =
        std::make_shared<imagejxx::Document>(Dataset::from_stack({image}));
    Window window;
    window.addNativeDocument(native);
    window.updateSettings(
        {{"conversion_unsigned16_range", chooseRange ? "auto" : "12"},
         {"conversion_full_range_inversions", false}},
        false);
    window.action("show_operation", {{"operation", "invert_uint16"}});
    auto dialog = window.renderHeadless(3)["operation_dialog"];
    check(dialog["parameters"]["unsigned16_bits"] == (chooseRange ? 0 : 12) &&
              dialog["parameters"]["full_range_inversions"] == false &&
              dialog["controls"]["full_range_inversions"]["kind"] == "bool",
          "Inversion dialog did not consume the unsigned-range preferences");
    clickOperationControl(window,
                          dialog["controls"]["full_range_inversions"]["point"]);
    dialog = window.renderHeadless(3)["operation_dialog"];
    check(dialog["parameters"]["full_range_inversions"] == true,
          "Actual inversion checkbox did not enable full-range inversion");
    if (chooseRange)
      window.renderHeadless(3, true); // Real combo chooses 8, then Preview.
    else
      clickOperationControl(window, dialog["preview_point"]);
    dialog = window.renderHeadless(3)["operation_dialog"];
    const auto parameters = dialog["parameters"];
    check(dialog["has_preview"] == true &&
              parameters["unsigned16_bits"] == (chooseRange ? 8 : 12) &&
              native->snapshot().data().dataset.plane({0, 0, 0})->pixels ==
                  image.pixels &&
              native->undo_steps() == 0 && !native->snapshot().dirty(),
          "Inversion range input/Preview changed the source or selected range");
    window.updateSettings({{"conversion_unsigned16_range", "14"},
                           {"conversion_full_range_inversions", false}},
                          false);
    window.recording = true;
    clickOperationControl(window, dialog["apply_point"]);
    check(native->snapshot().data().dataset.plane({0, 0, 0})->pixels ==
                  expected &&
              native->snapshot().data().dataset.plane({0, 0, 0})->type ==
                  PixelType::UInt16 &&
              native->undo_steps() == 1 && window.workflow.size() == 1 &&
              window.workflow[0]["parameters"] == parameters,
          "Inversion Apply lost the frozen range, recording or native history");
    window.action("undo");
    check(native->snapshot().data().dataset.plane({0, 0, 0})->pixels ==
              image.pixels,
          "Inversion undo did not restore the native uint16 source");
  }
}

namespace {
Json readPresenceSessionRoot(const std::string &path) {
  const auto payload = readBytes(path);
  const std::string magic = "IMAGEJXX2\n";
  check(payload.size() > 14 && std::equal(magic.begin(), magic.end(), payload.begin()),
        "Presence fixture lacks an actual v2 session envelope");
  std::uint32_t size = 0;
  for (int i = 10; i < 14; ++i)
    size = (size << 8) | payload[i];
  check(size > 0 && size < 1024 * 1024, "Presence test envelope is unexpectedly large");
  std::vector<unsigned char> decoded(size);
  uLongf written = size;
  uLong consumed = static_cast<uLong>(payload.size() - 14);
  check(uncompress2(decoded.data(), &written, payload.data() + 14, &consumed) == Z_OK &&
            written == size && consumed == payload.size() - 14,
        "Presence test could not inspect actual compressed native session bytes");
  return Json::parse(decoded.begin(), decoded.end());
}
void writePresenceSessionRoot(const std::string &path, const Json &root) {
  const auto json = root.dump();
  check(!json.empty() && json.size() < 1024 * 1024, "Presence fixture exceeds its bounded test size");
  std::vector<unsigned char> bytes{'I', 'M', 'A', 'G', 'E', 'J', 'X', 'X', '2', '\n'};
  const auto size = static_cast<std::uint32_t>(json.size());
  for (int i = 24; i >= 0; i -= 8)
    bytes.push_back(static_cast<unsigned char>(size >> i));
  uLongf bound = compressBound(static_cast<uLong>(json.size()));
  bytes.resize(14 + bound);
  check(compress2(bytes.data() + 14, &bound,
                  reinterpret_cast<const Bytef *>(json.data()),
                  static_cast<uLong>(json.size()), Z_DEFAULT_COMPRESSION) == Z_OK,
        "Cannot encode the independent malformed/unmarked v2 fixture");
  bytes.resize(14 + bound);
  writeBytes(path, bytes);
}
void checkPresenceCameras(const DisplayState &actual, const DisplayState &expected) {
  check(actual.t == expected.t && actual.c == expected.c &&
            actual.orthogonal == expected.orthogonal && actual.linkedZoom == expected.linkedZoom &&
            actual.crosshair == expected.crosshair && actual.zoom == expected.zoom &&
            actual.planeZoom == expected.planeZoom && actual.pan == expected.pan &&
            metadata_equal(actual.properties, expected.properties),
        "Marked session changed native camera values or property presence/null/value");
}
void testSessionDisplayPresence(const std::string &workspace) {
  namespace fs = std::filesystem;
  const auto directory = fs::u8path(workspace);
  fs::create_directories(directory);
  const auto path = (directory / "display-presence.ijx").u8string();
  Json custom = Json::array();
  for (int i = 0; i < 256; ++i)
    custom.push_back({i, 255 - i, (7 * i) % 256});
  const std::vector<Json> maps{
      Json::object(), Json{{"display_range", {-1.0, 10500.0}}},
      Json{{"display_range", nullptr}, {"lut", nullptr}, {"custom_lut", nullptr}},
      Json{{"lut", "Fire"}}, Json{{"custom_lut", custom}}};
  const Json expectedKeys = Json::array({
      Json::array(), Json::array({"display_range"}),
      Json::array({"display_range", "lut", "custom_lut"}),
      Json::array({"lut"}), Json::array({"custom_lut"})});
  Documents original;
  std::vector<DocumentSnapshot> before;
  for (std::size_t index = 0; index < maps.size(); ++index) {
    Stack planes;
    for (int z = 0; z < 2; ++z) {
      Image plane(5, 4, 1, PixelType::Float32);
      for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 5; ++x)
          plane.at(x, y) = 10000 * z + 100 * y + x;
      planes.push_back(std::move(plane));
    }
    SpatialCalibration calibration;
    calibration.spacing = {.4, 1.2, 3};
    calibration.origin = {10, -5, 2};
    calibration.direction = {0, -1, 0, 1, 0, 0, 0, 0, 1};
    calibration.unit = "um";
    DocumentData data{Dataset::from_stack(std::move(planes), calibration)};
    data.metadata.title = "Display presence " + std::to_string(index);
    data.metadata.properties["unknown_property"] = metadata_object({
        {"vendor", "property-kept"},
        {"unsigned", std::numeric_limits<std::uint64_t>::max()},
        {"signed", std::int64_t(17)},
        {"negative_zero", -0.0},
        {"values", metadata_array({std::monostate{}, true, .5})}});
    // This remains arbitrary DOCUMENT metadata; the new marker is ROOT-only.
    data.metadata.extensions["native_document_state"] =
        metadata_object({{"version", "vendor"}, {"documents", "unknown-kept"}});
    if (index == 3) {
      data.metadata.extensions["is_stack"] = true;
      data.metadata.extensions["labels"] = false;
      data.metadata.record_layout = metadataObjectFromJson(Json{
          {"root_fields", {"native_properties", "selection", "previous_selection", "rois", "overlays"}},
          {"null_fields", {"selection", "previous_selection"}},
          {"calibration_fields", {"pixel_width", "pixel_height", "pixel_depth", "unit", "origin",
                                   "direction", "frame_interval", "time_origin", "time_unit"}},
          {"roi_slice_fields", {"0"}},
          {"vendor_layout", {{"unknown", "layout-kept"}}}});
    }
    Selection roi("rectangle", {{1, 1}, {4, 3}});
    roi.attributes["shape_unknown"] = "kept";
    data.rois.push_back({"retained", roi, std::nullopt, std::nullopt, 1,
                         {{"roi_unknown", "kept"}}});
    data.overlays.push_back({{"kind", "point"}, {"points", metadata_array({metadata_array({2., 1.})})},
                             {"slice_index", std::int64_t(1)}, {"overlay_unknown", "kept"}});
    DisplayState display;
    display.orthogonal = true;
    display.linkedZoom = false;
    display.crosshair = {2, 1, 1};
    display.zoom = 1.25;
    display.planeZoom = {2, 3.75, .5};
    display.pan = {{{3.25, -2.5}, {5.5, -4.25}, {7.75, -6.5}}};
    display.properties = metadataObjectFromJson(maps[index]);
    auto native = std::make_shared<imagejxx::Document>(std::move(data), display);
    native->mark_saved();
    before.push_back(native->snapshot());
    original.push_back(std::make_shared<DesktopDocument>(std::move(native)));
  }
  const Json results = Json::array({Json{{"unknown_result", "preserved"}}});
  const Json workflow = Json::array({Json{{"operation", "add"}, {"parameters", {{"value", 1}}}}});
  saveSession(path, original, results, workflow);
  const auto root = readPresenceSessionRoot(path);
  check(root.at("version") == 2 && root.at("native_document_state").at("version") == 1 &&
            root.at("native_document_state").at("documents").size() == maps.size(),
        "Native saver omitted/versioned native document-state entries");
  Json rows, steps;
  const auto reopened = loadSession(path, rows, steps);
  check(reopened.size() == maps.size() && rows == results && steps == workflow,
        "Marked native session changed document membership/results/workflow");
  for (std::size_t index = 0; index < maps.size(); ++index) {
    const auto live = original[index]->nativeSnapshot();
    const auto restored = reopened[index]->nativeSnapshot();
    const auto &a = before[index].data(), &b = restored.data();
    const Json savedMetadata{{"title", a.metadata.title},
                             {"properties", metadataJson(a.metadata.properties)},
                             {"extensions", metadataJson(a.metadata.extensions)},
                             {"record_layout", metadataJson(a.metadata.record_layout)}};
    check(root.at("native_document_state").at("documents").at(index) ==
              Json{{"display_properties", expectedKeys[index]}, {"metadata", savedMetadata}},
          "Saver changed declared ordinal display presence or exact retained native metadata");
    check(before[index].shares_data(live) &&
              before[index].revisions().matches(live.revisions(), Change::All) &&
              before[index].dirty() == live.dirty() && !restored.dirty() &&
              original[index]->undoSteps() == 0 && original[index]->redoSteps() == 0 &&
              reopened[index]->undoSteps() == 0 && reopened[index]->redoSteps() == 0,
          "Session serialization mutated source identity/revisions/dirty/history");
    checkPresenceCameras(live.display(), before[index].display());
    checkPresenceCameras(restored.display(), before[index].display());
    check(b.dataset.pixel_type() == PixelType::Float32 && b.dataset.shape().t == 1 &&
              b.dataset.shape().c == 1 && b.dataset.shape().s == 1 && b.dataset.shape().z == 2 &&
              b.dataset.shape().x == 5 && b.dataset.shape().y == 4 &&
              b.dataset.calibration().spacing == a.dataset.calibration().spacing &&
              b.dataset.calibration().origin == a.dataset.calibration().origin &&
              b.dataset.calibration().direction == a.dataset.calibration().direction &&
              b.dataset.calibration().unit == a.dataset.calibration().unit &&
              b.dataset.calibration().frame_interval == a.dataset.calibration().frame_interval &&
              b.dataset.calibration().time_origin == a.dataset.calibration().time_origin &&
              b.dataset.calibration().time_unit == a.dataset.calibration().time_unit &&
              b.metadata == a.metadata &&
              b.selection == a.selection && b.previous_selection == a.previous_selection && b.rois == a.rois,
          "Marked native session changed calibration/annotations/unknown metadata");
    check(b.overlays.size() == a.overlays.size(), "Marked native session changed overlays");
    const auto numeric = metadataJson(b.metadata.properties).at("unknown_property");
    check(numeric.at("unsigned").is_number_unsigned() &&
              numeric.at("unsigned").get<std::uint64_t>() == std::numeric_limits<std::uint64_t>::max() &&
              numeric.at("signed").is_number_integer() && !numeric.at("signed").is_number_unsigned() &&
              numeric.at("signed").get<std::int64_t>() == 17 &&
              numeric.at("negative_zero").is_number_float() &&
              numeric.at("negative_zero").get<double>() == 0 &&
              std::signbit(numeric.at("negative_zero").get<double>()),
          "Marked native metadata lost exact large unsigned/signed/negative-zero values or numeric types");
    for (std::size_t i = 0; i < a.overlays.size(); ++i)
      check(metadata_equal(b.overlays[i], a.overlays[i]), "Marked session changed unknown overlay fields");
    for (int z = 0; z < 2; ++z)
      check(b.dataset.plane({0, 0, z})->pixels == a.dataset.plane({0, 0, z})->pixels &&
                live.data().dataset.plane({0, 0, z})->pixels == a.dataset.plane({0, 0, z})->pixels,
            "Marked save/reopen changed exact owned Float32 pixels");
    const auto serialized = documentJson(*reopened[index]);
    for (const auto *key : {"display_range", "lut", "custom_lut"})
      check(serialized.contains(key) == maps[index].contains(key) &&
                (!maps[index].contains(key) || serialized[key] == maps[index][key]),
            "Marked reopened document changed serialized display key presence/null/value");
  }
  std::cout << "PASS native_session_exact_document_state_maps\n";

  auto unmarked = root;
  unmarked.erase("native_document_state");
  const auto legacyPath = (directory / "display-presence-legacy.ijx").u8string();
  writePresenceSessionRoot(legacyPath, unmarked);
  const auto legacy = loadSession(legacyPath, rows, steps);
  // Same native_properties and complete view_state shape; only explicit marker
  // chooses exact absence. No heuristic interprets that legal historical shape.
  check(metadataJson(legacy[0]->nativeSnapshot().display().properties).at("lut") == "Grays" &&
            metadataJson(legacy[1]->nativeSnapshot().display().properties).at("lut") == "Grays" &&
            rows == results && steps == workflow,
        "Unmarked same-shaped v2 records lost legacy Grays defaults");
  const auto adapted = documentFromJson(root["documents"][0]);
  check(metadataJson(adapted->nativeSnapshot().display().properties).at("lut") == "Grays",
        "Public document JSON adapter changed its historical defaults");
  std::cout << "PASS native_session_exact_document_state_legacy_defaults\n";

  auto stable = std::make_shared<DesktopDocument>(
      Stack{Image(5, 4, 1, PixelType::Float32)}, "Stable legacy desktop");
  stable->markSaved();
  const auto stableBefore = stable->nativeSnapshot();
  const Json stableRoot{{"format", "imagejxx-session"}, {"version", 2},
                        {"documents", Json::array({documentJson(*stable)})},
                        {"results", results}, {"workflow", workflow}};
  const auto stablePath = (directory / "display-presence-stable.ijx").u8string();
  const auto stableExpectedPath = (directory / "display-presence-stable-expected.ijx").u8string();
  saveSession(stablePath, Documents{stable}, results, workflow);
  writePresenceSessionRoot(stableExpectedPath, stableRoot);
  check(readPresenceSessionRoot(stablePath) == stableRoot &&
            readBytes(stablePath) == readBytes(stableExpectedPath),
        "Already stable legacy native document changed exact unextended v2 envelope bytes");
  const auto stableRestored = loadSession(stablePath, rows, steps);
  check(stableRestored.size() == 1 &&
            stableRestored[0]->nativeSnapshot().data().metadata == stableBefore.data().metadata,
        "Stable unmarked native session changed its existing metadata inference");
  auto mixed = original;
  mixed.push_back(stable);
  const auto mixedPath = (directory / "display-presence-mixed.ijx").u8string();
  saveSession(mixedPath, mixed, results, workflow);
  const auto mixedRoot = readPresenceSessionRoot(mixedPath);
  check(mixedRoot.at("native_document_state").at("documents").size() == mixed.size(),
        "Mixed workspace marker omitted the stable legacy member");
  const auto mixedRestored = loadSession(mixedPath, rows, steps);
  check(mixedRestored.size() == mixed.size() &&
            mixedRestored.back()->nativeSnapshot().data().metadata == stableBefore.data().metadata,
        "Marked mixed workspace changed stable legacy metadata/layout");
  checkPresenceCameras(mixedRestored.back()->view(), stableBefore.display());
  std::cout << "PASS native_session_exact_document_state_legacy_bytes_and_mixed\n";

  const auto exactStates = root.at("native_document_state").at("documents");
  std::vector<Json> invalid;
  const auto bad = [&](Json marker) {
    auto value = root;
    value["native_document_state"] = std::move(marker);
    invalid.push_back(std::move(value));
  };
  bad(nullptr);
  bad(Json::array());
  for (const auto &version : std::vector<Json>{Json(0), Json(2), Json(true), Json(nullptr), Json("1"), Json(std::numeric_limits<std::uint64_t>::max())})
    bad(Json{{"version", version}, {"documents", exactStates}});
  bad(Json{{"version", 1}});
  bad(Json{{"version", 1}, {"documents", exactStates}, {"unknown", true}});
  bad(Json{{"version", 1}, {"documents", nullptr}});
  bad(Json{{"version", 1}, {"documents", Json::object()}});
  auto states = exactStates;
  states.erase(states.end() - 1);
  bad(Json{{"version", 1}, {"documents", states}});
  states = exactStates;
  states.push_back(exactStates[0]);
  bad(Json{{"version", 1}, {"documents", states}});
  for (const auto &entry : std::vector<Json>{Json(nullptr), Json::array(), Json::object(),
      Json{{"display_properties", Json::array()}},
      Json{{"display_properties", Json::array()}, {"metadata", exactStates[0]["metadata"]}, {"unknown", true}}}) {
    states = exactStates;
    states[0] = entry;
    bad(Json{{"version", 1}, {"documents", states}});
  }
  for (const auto &keys : std::vector<Json>{Json(nullptr), Json::object(), Json::array({true}),
      Json::array({"unsupported"}), Json::array({"lut", "lut"}), Json::array({"lut"})}) {
    states = exactStates;
    states[0]["display_properties"] = keys;
    bad(Json{{"version", 1}, {"documents", states}});
  }
  states = exactStates;
  states[2]["display_properties"] = Json::array({"lut", "display_range", "custom_lut"});
  bad(Json{{"version", 1}, {"documents", states}});
  for (const auto &value : std::vector<Json>{Json(nullptr), Json::array(), Json::object()}) {
    states = exactStates;
    states[0]["metadata"] = value;
    bad(Json{{"version", 1}, {"documents", states}});
  }
  for (const auto &field : std::vector<std::pair<std::string, Json>>{
       {"title", 1}, {"properties", Json::array()}, {"extensions", false}, {"record_layout", nullptr}}) {
    states = exactStates;
    states[0]["metadata"][field.first] = field.second;
    bad(Json{{"version", 1}, {"documents", states}});
  }
  states = exactStates;
  states[0]["metadata"].erase("title");
  bad(Json{{"version", 1}, {"documents", states}});
  states = exactStates;
  states[0]["metadata"]["unknown"] = true;
  bad(Json{{"version", 1}, {"documents", states}});
  states = exactStates;
  states[0]["metadata"]["properties"][""] = "invalid-property-name";
  bad(Json{{"version", 1}, {"documents", states}});
  states = exactStates;
  states[0]["metadata"]["record_layout"]["root_fields"] = Json::array({true});
  bad(Json{{"version", 1}, {"documents", states}});
  states = exactStates;
  states[0]["metadata"]["extensions"]["lut"] = "reserved-document-field";
  bad(Json{{"version", 1}, {"documents", states}});
  for (const auto &field : {"title", "properties", "extensions", "record_layout"}) {
    states = exactStates;
    if (std::string(field) == "title")
      states[0]["metadata"][field] = "contradictory title";
    else if (std::string(field) == "properties")
      states[0]["metadata"][field]["unknown_property"] = "contradictory property";
    else if (std::string(field) == "extensions")
      states[0]["metadata"][field]["native_document_state"] = "contradictory unknown extension";
    else
      states[0]["metadata"][field]["root_fields"] = Json::array();
    bad(Json{{"version", 1}, {"documents", states}});
  }
  for (const auto &mutation : std::vector<std::pair<std::string, double>>{
           {"unsigned", std::ldexp(1.0, 64)}, {"signed", 17.0}, {"negative_zero", 0.0}}) {
    states = exactStates;
    states[0]["metadata"]["properties"]["unknown_property"][mutation.first] = mutation.second;
    bad(Json{{"version", 1}, {"documents", states}});
    auto wireContradiction = root;
    wireContradiction["documents"][0]["native_properties"]["unknown_property"][mutation.first] = mutation.second;
    invalid.push_back(std::move(wireContradiction));
  }
  auto contradictory = root;
  contradictory["documents"][0]["lut"] = "Grays";
  invalid.push_back(std::move(contradictory));
  contradictory = root;
  contradictory["documents"][1].erase("display_range");
  invalid.push_back(std::move(contradictory));
  contradictory = root;
  contradictory["documents"][0]["view_state"]["zoom"] = 42;
  // This alters actual decoded camera state and remains a consistent file;
  // use a conflicting root slice index to verify generated view consistency.
  contradictory["documents"][0]["slice_index"] = 0;
  invalid.push_back(std::move(contradictory));
  for (std::size_t index = 0; index < invalid.size(); ++index) {
    const auto badPath = (directory / ("display-presence-invalid-" + std::to_string(index) + ".ijx")).u8string();
    writePresenceSessionRoot(badPath, invalid[index]);
    Json retainedRows = Json::array({"results-before"}), retainedSteps = Json::array({"workflow-before"});
    bool rejected = false;
    try { (void)loadSession(badPath, retainedRows, retainedSteps); }
    catch (const std::invalid_argument &) { rejected = true; }
    check(rejected && retainedRows == Json::array({"results-before"}) &&
              retainedSteps == Json::array({"workflow-before"}),
          "Malformed/contradictory presence extension was accepted or published tables");
  }
  std::cout << "PASS native_session_exact_document_state_atomic_rejection\n";
}
} // namespace

int main(int argc, char **argv) {
  try {
    const auto run = [](const char *name, auto test) {
      // Flush before entering each case so native crashes identify their case.
      std::cerr << "Desktop case: " << name << std::endl;
      test();
    };
    run("snapshot", testSnapshot);
    run("shared history", testSharedHistory);
    run("views", testViews);
    run("selection fit", testSelectionFit);
    run("outline spans", testOutlineSpans);
    run("derived stacks", testDerivedStacks);
    run("Z projection range dialog", testProjectionRangeDialog);
    run("volume filter selection", testVolumeFilterSelection);
    run("native records", testNativeRecords);
    run("shared native owner", testSharedNativeOwner);
    run("selection gestures", testSelectionGestures);
    run("selection properties", testSelectionProperties);
    run("busy selection properties", testBusySelectionProperties);
    run("ROI style controls", testRoiStyleControls);
    run("ROI style frozen workflow", testRoiStyleFrozenWorkflow);
    run("ROI style visual layers", testRoiStyleVisualLayers);
    run("reopened ROI style controls", testReopenedRoiStyleControls);
    run("ROI style view caches", testRoiStyleViewCaches);
    run("closing active middle tab", testClosingActiveMiddleTab);
    run("ordered overlay styles and widths",
        testOrderedOverlayStylesAndDeviceWidths);
    run("mixed overlay draw vertices", testMixedOverlayDrawVertices);
    run("nested ROI style records", testNestedRoiStyleRenderingAndRecords);
    run("binary option controls", testBinaryOptionControls);
    run("conversion weights", testConversionWeightControls);
    run("conversion ranges", testConversionRangeControls);
    run("inversion controls", testInversionControls);
    if (argc > 1) {
      run("workspace", [&] { testWorkspace(argv[1]); });
      run("session display-property presence", [&] { testSessionDisplayPresence(argv[1]); });
      run("preferences", [&] { testPreferences(argv[1]); });
    }
    std::cout << "Desktop snapshots, outlines, derived stacks and workspace "
                 "saves passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
