#include "window.hpp"
#include <imagejxx/preferences.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace imagejxx::desktop {
namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

Json bytes(const Image &image) {
  require(image.width == 9 && image.height == 7 && image.channels == 1 &&
              image.type == PixelType::UInt8 && image.pixels.size() == 63,
          "Pad-edges consumer changed the pinned UInt8 geometry");
  Json result = Json::array();
  for (const auto value : image.pixels) {
    require(std::isfinite(value) && (value == 0 || value == 255),
            "Pad-edges consumer returned a nonbinary byte");
    result.push_back(static_cast<unsigned int>(value));
  }
  return result;
}

Json erosionOracle(bool pad) {
  // Independently enumerate the full-foreground source and its 3x3 erosion.
  // No kernel or rendered/output pixel is consulted by this expected loop.
  Json result = Json::array();
  for (int y = 0; y < 7; ++y)
    for (int x = 0; x < 9; ++x) {
      bool foreground = true;
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
          const int xx = x + dx, yy = y + dy;
          if ((xx < 0 || xx >= 9 || yy < 0 || yy >= 7) && !pad)
            foreground = false;
        }
      result.push_back(foreground ? 255 : 0);
    }
  return result;
}

void fixedSettings(const Window &window) {
  const auto options = window.settings().binary_options();
  require(options.iterations == 1 && options.neighbors == 1 &&
              options.black_background,
          "Pad-edges consumer changed another binary setting");
}

void parameters(const Json &values, bool pad) {
  require(values.at("iterations") == 1 && values.at("neighbors") == 1 &&
              values.at("black_background").is_boolean() &&
              boolean(values.at("black_background")) &&
              values.at("pad_edges").is_boolean() &&
              boolean(values.at("pad_edges")) == pad,
          "Pad-edges operation parameters differ from the frozen typed options");
}

void addSource(Window &window, const Image &source, const std::string &title) {
  fixedSettings(window);
  DatasetShape shape;
  shape.x = 9;
  shape.y = 7;
  window.addNativeDocument(std::make_shared<imagejxx::Document>(
      Dataset::from_planes(shape, {source}), title));
  window.recording = true;
  require(window.nativeDocument()->undo_steps() == 0 && window.workflow.empty(),
          "Pad-edges consumer needs a fresh document owner");
}

void apiAction(Window &window, Json &inputs, const std::string &name,
               const Json &values) {
  window.action(name, values);
  inputs.push_back({{"method", "action"}, {"name", name}, {"parameters", values}});
}

void apiPadEdges(Window &window, Json &inputs, bool value) {
  const Json values{{"binary_pad_edges", value}};
  window.updateSettings(values, false);
  inputs.push_back({{"method", "updateSettings"}, {"values", values},
                    {"persist", false}});
  fixedSettings(window);
}

Json click(const Json &location) {
  require(location.is_array() && location.size() == 2 &&
              location[0].is_number() && location[1].is_number() &&
              std::isfinite(number(location[0])) &&
              std::isfinite(number(location[1])) &&
              number(location[0]) >= 0 && number(location[1]) >= 0,
          "Pad-edges consumer has no visible control point");
  // ImGui floors mouse positions. These large controls admit their nearest
  // integral center, so record that actual raster position explicitly.
  const Json raster = {std::round(number(location[0])),
                       std::round(number(location[1]))};
  return Json::array(
      {{{"type", "position"}, {"space", "screen"}, {"point", raster}},
       {{"type", "button"}, {"name", "left"}, {"down", true}},
       {{"type", "button"}, {"name", "left"}, {"down", false}},
       {{"type", "wait"}, {"frames", 2}}});
}

struct Recorder {
  std::filesystem::path directory;
  std::size_t sequence = 0;

  Json render(Window &window, Json &row, const std::string &purpose,
              const Json &events = Json::array()) {
    const Json before{{"document_info", window.documentInfo()},
                      {"source_pixels", bytes(window.document()->frame())},
                      {"settings", window.preferencesJson()},
                      {"workflow", window.workflow}};
    const auto rendered = window.renderHeadless(3, false, false, events);
    const auto path =
        (directory / ("diagnostic-" + std::to_string(sequence++) + ".json")).u8string();
    // Save actual observations before assertions. This is diagnostic evidence,
    // not a passed scenario, and is retained even when validation throws.
    writeJson(path, {{"status", "diagnostic_unverified"},
                     {"scenario", row.at("id")}, {"purpose", purpose},
                     {"requested_events", events}, {"before", before},
                     {"rendered", rendered}});
    row["renders"].push_back({{"purpose", purpose},
                               {"requested_events", events},
                               {"diagnostic_path", path},
                               {"before", before}, {"rendered", rendered}});
    const auto &trace = rendered.at("input_trace");
    require(trace.is_array() && trace.size() == events.size(),
            "Pad-edges consumer did not deliver every requested input");
    bool down = false;
    for (std::size_t i = 0; i < events.size(); ++i) {
      const auto &event = events[i];
      const auto type = string(event.at("type"));
      require(trace[i].at("type") == type,
              "Pad-edges consumer trace disagrees with delivered input");
      if (type == "position" || type == "button") {
        if (type == "button")
          down = boolean(event.at("down"));
        row["input_events"].push_back({{"type", "pointer"},
                                       {"position", trace[i].at("mouse_screen")},
                                       {"button", "left"}, {"down", down}});
      } else if (type == "key")
        row["input_events"].push_back({{"type", "key"}, {"key", event.at("name")},
                                       {"down", event.at("down")}});
      else if (type == "text")
        row["input_events"].push_back({{"type", "text"}, {"text", event.at("value")}});
    }
    require(window.error().empty(), "Pad-edges editor input reported an error");
    return rendered;
  }
};

Json scenario(const std::string &id, bool setting) {
  return {{"id", id}, {"setting_value", setting},
          {"input_events", Json::array()}, {"api_inputs", Json::array()},
          {"renders", Json::array()}};
}

Json preferences(Window &window, Recorder &recorder, Json &row) {
  apiAction(window, row["api_inputs"], "show_preferences",
            {{"search", "binary_pad_edges"}});
  auto rendered = recorder.render(window, row, "Preferences.open");
  const auto &panel = rendered.at("preferences_panel");
  require(panel.at("open") == true && panel.at("error") == "" &&
              panel.at("controls") == Json::array({"binary_pad_edges"}) &&
              panel.at("toggle_name") == "binary_pad_edges" &&
              panel.at("toggle_value").is_boolean(),
          "Preferences did not display the filtered pad-edges checkbox");
  return rendered;
}

Json capture(Window &window, Recorder &recorder, Json &row, bool expected) {
  apiAction(window, row["api_inputs"], "show_operation",
            {{"operation", "binary_erode"}});
  auto rendered = recorder.render(window, row, "binary_erode.capture");
  const auto &dialog = rendered.at("operation_dialog");
  require(dialog.at("open") == true && dialog.at("id") == "binary_erode" &&
              dialog.at("controls").at("pad_edges").at("kind") == "bool",
          "Binary erosion did not display its Boolean operation checkbox");
  parameters(dialog.at("parameters"), expected);
  require(window.settings().boolean("binary_pad_edges") == expected,
          "Binary erosion draft did not capture the live pad-edges setting");
  row["captured_parameters"] = dialog.at("parameters");
  return rendered;
}

Json publishScenario(Window &window, Recorder &recorder, const Image &source,
                     const DocumentSnapshot &original, Json row,
                     const Json &rendered, bool expected) {
  const auto sourcePixels = bytes(source);
  const auto captured = row.at("captured_parameters");
  const auto execution = rendered.at("operation_dialog").at("parameters");
  parameters(captured, boolean(row.at("setting_value")));
  parameters(execution, expected);
  require(bytes(window.document()->frame()) == sourcePixels &&
              window.nativeDocument()->undo_steps() == 0,
          "Operation Apply did not start from the untouched source");
  row["execution_parameters"] = execution;
  const auto applied = recorder.render(
      window, row, "binary_erode.Apply",
      click(rendered.at("operation_dialog").at("apply_point")));
  require(applied.at("operation_dialog").at("open") == false,
          "Actual operation Apply did not close the modal");
  window.wait();
  require(window.error().empty() && window.workflow.size() == 1 &&
              window.workflow[0].at("operation") == "binary_erode" &&
              window.nativeDocument()->undo_steps() == 1 &&
              window.nativeDocument()->redo_steps() == 0,
          "Desktop erosion did not publish one recorded compatibility undo step");
  const auto workflowParameters = window.workflow[0].at("parameters");
  parameters(workflowParameters, expected);
  const auto actualPixels = bytes(window.document()->frame());
  require(actualPixels == erosionOracle(expected),
          "Desktop pad-edges pixels differ from the independent border oracle");
  row["current_setting_value"] = window.settings().boolean("binary_pad_edges");
  row["effective_parameter_value"] = workflowParameters.at("pad_edges");
  row["workflow_pad_edges"] = workflowParameters.at("pad_edges");
  row["workflow"] = window.workflow;
  row["options_after"] = workflowParameters;
  row["options_preserved"] = workflowParameters == execution &&
                              row.at("captured_parameters") == captured;
  row["pixels"] = actualPixels;
  row["source_pixels"] = sourcePixels;
  row["foreground"] = std::count(actualPixels.begin(), actualPixels.end(), Json(255));
  row["history_steps"] = window.nativeDocument()->undo_steps();
  row["source_preserved"] = bytes(*original.data().dataset.plane()) == sourcePixels &&
                              bytes(source) == sourcePixels;
  row["published_document_info"] = window.documentInfo();
  apiAction(window, row["api_inputs"], "undo", Json::object());
  row["restored_pixels"] = bytes(window.document()->frame());
  row["undo_steps_after"] = window.nativeDocument()->undo_steps();
  row["redo_steps_after"] = window.nativeDocument()->redo_steps();
  row["undo_restored_source"] = row.at("restored_pixels") == sourcePixels &&
                                  window.nativeDocument()->undo_steps() == 0 &&
                                  window.nativeDocument()->redo_steps() == 1;
  require(row.at("source_preserved") == true && row.at("options_preserved") == true &&
              row.at("undo_restored_source") == true,
          "Desktop pad-edges source/options preservation or exact undo failed");
  fixedSettings(window);
  return row;
}

Json persistence(const std::string &path, const std::string &saved,
                 bool reopened, bool displayed) {
  return {{"path", path}, {"bytes_utf8", saved}, {"envelope", Json::parse(saved)},
          {"reopened_setting_value", reopened}, {"displayed_setting_value", displayed}};
}
} // namespace

int Window::binaryPadEdgesConsumerTest(const std::string &directory) {
  checkThread();
  require(!directory.empty() && documents().empty(),
          "Pad-edges consumer requires a fresh directory and Window");
  const auto root =
      std::filesystem::absolute(std::filesystem::u8path(directory)).lexically_normal();
  std::filesystem::create_directories(root.parent_path());
  require(std::filesystem::create_directory(root),
          "Pad-edges consumer output directory must be fresh");
  const auto preferencesPath = (root / "preferences.json").u8string();
  require(!std::filesystem::exists(std::filesystem::u8path(preferencesPath)) &&
              !settings().boolean("binary_pad_edges"),
          "Pad-edges consumer did not start from default false");
  Recorder recorder{root};
  Image source(9, 7, 1, PixelType::UInt8);
  std::fill(source.pixels.begin(), source.pixels.end(), 255);
  Json report{{"schema_version", 1}, {"context", "imgui_cpp"},
              {"requirement_id", "G052.binary_erode.pad_edges"},
              {"shape", {7, 9}}, {"source", bytes(source)},
              {"scenarios", Json::array()}, {"supplementary", Json::object()}};

  addSource(*this, source, "Default binary pad edges");
  const auto defaultSource = nativeSnapshot();
  auto defaultRow = scenario("default", settings().boolean("binary_pad_edges"));
  const auto defaultDraft = capture(*this, recorder, defaultRow, false);
  report["scenarios"].push_back(publishScenario(
      *this, recorder, source, defaultSource, std::move(defaultRow), defaultDraft, false));

  Window changed(preferencesPath);
  addSource(changed, source, "Changed binary pad edges");
  const auto changedSource = changed.nativeSnapshot();
  auto changedRow = scenario("changed", false);
  const auto before = preferences(changed, recorder, changedRow);
  require(before.at("preferences_panel").at("toggle_value") == false,
          "Initial Preferences checkbox did not display false");
  const auto toggled = recorder.render(
      changed, changedRow, "Preferences.binary_pad_edges",
      click(before.at("preferences_panel").at("toggle_point")));
  require(toggled.at("preferences_panel").at("toggle_value") == true &&
              !changed.settings().boolean("binary_pad_edges"),
          "Preferences checkbox did not retain a detached true draft");
  const auto applied = recorder.render(
      changed, changedRow, "Preferences.Apply",
      click(toggled.at("preferences_panel").at("apply_point")));
  require(changed.settings().boolean("binary_pad_edges") &&
              applied.at("preferences_panel").at("toggle_value") == true &&
              applied.at("preferences_panel").at("error") == "",
          "Actual Preferences Apply did not publish pad-edges true");
  const auto savedBytes = readBytes(preferencesPath);
  const std::string saved(savedBytes.begin(), savedBytes.end());
  const auto savedDocument = PreferencesDocument::parse(saved, false);
  const auto immediatelyReopened = PreferencesDocument::load(preferencesPath, false);
  const auto savedEnvelope = Json::parse(saved);
  require(savedEnvelope.at("format") == "imagejxx-preferences" &&
              savedEnvelope.at("version") == Settings::schema_version &&
              savedEnvelope.at("values") == changed.preferencesJson().at("values") &&
              savedDocument.settings.boolean("binary_pad_edges") &&
              savedDocument.settings.values() == changed.settings().values() &&
              immediatelyReopened.settings.values() == changed.settings().values() &&
              savedDocument.warnings.empty() && !savedDocument.write_blocked,
          "Actual Preferences Apply did not immediately save the exact envelope");
  changedRow["preferences_before"] = before.at("preferences_panel");
  changedRow["preferences_toggled"] = toggled.at("preferences_panel");
  changedRow["preferences_applied"] = applied.at("preferences_panel");
  changedRow["persistence_file"] = persistence(
      preferencesPath, saved, immediatelyReopened.settings.boolean("binary_pad_edges"),
      boolean(applied.at("preferences_panel").at("toggle_value")));
  changedRow["setting_value"] = changed.settings().boolean("binary_pad_edges");
  const auto changedDraft = capture(changed, recorder, changedRow, true);
  report["scenarios"].push_back(publishScenario(
      changed, recorder, source, changedSource, std::move(changedRow), changedDraft, true));

  // These contexts deliberately start with default settings, not the true file.
  Window frozen;
  addSource(frozen, source, "Frozen binary pad edges");
  const auto frozenSource = frozen.nativeSnapshot();
  auto frozenRow = scenario("frozen", frozen.settings().boolean("binary_pad_edges"));
  const auto frozenDraft = capture(frozen, recorder, frozenRow, false);
  frozenRow["captured_scenario"] = "default";
  apiPadEdges(frozen, frozenRow["api_inputs"], true);
  const auto retained = recorder.render(frozen, frozenRow, "binary_erode.retained_after_API");
  require(frozen.settings().boolean("binary_pad_edges") &&
              retained.at("operation_dialog").at("open") == true &&
              retained.at("operation_dialog").at("parameters") ==
                  frozenDraft.at("operation_dialog").at("parameters"),
          "Global API preparation replaced the captured false operation draft");
  frozenRow["retained_parameters"] = retained.at("operation_dialog").at("parameters");
  report["scenarios"].push_back(publishScenario(
      frozen, recorder, source, frozenSource, std::move(frozenRow), retained, false));

  Window overrideWindow;
  addSource(overrideWindow, source, "Overridden binary pad edges");
  const auto overrideSource = overrideWindow.nativeSnapshot();
  auto overrideRow = scenario("override", overrideWindow.settings().boolean("binary_pad_edges"));
  const auto overrideDraft = capture(overrideWindow, recorder, overrideRow, false);
  const auto overridden = recorder.render(
      overrideWindow, overrideRow, "binary_erode.pad_edges.override_true",
      click(overrideDraft.at("operation_dialog").at("controls").at("pad_edges").at("point")));
  parameters(overridden.at("operation_dialog").at("parameters"), true);
  require(!overrideWindow.settings().boolean("binary_pad_edges"),
          "Actual operation override changed the global setting");
  overrideRow["override_value"] = overridden.at("operation_dialog").at("parameters").at("pad_edges");
  overrideRow["overridden_parameters"] = overridden.at("operation_dialog").at("parameters");
  report["scenarios"].push_back(publishScenario(
      overrideWindow, recorder, source, overrideSource, std::move(overrideRow), overridden, true));

  require(readBytes(preferencesPath) == savedBytes,
          "Global API preparation changed the saved Preferences bytes");
  Window reopened(preferencesPath);
  addSource(reopened, source, "Reopened binary pad edges");
  const auto reopenedSource = reopened.nativeSnapshot();
  auto persistedRow = scenario("persisted", reopened.settings().boolean("binary_pad_edges"));
  const auto displayed = preferences(reopened, recorder, persistedRow);
  require(reopened.settings().values() == savedDocument.settings.values() &&
              displayed.at("preferences_panel").at("toggle_value") == true,
          "Fresh Window did not display the saved true Preferences checkbox");
  persistedRow["preferences_reopened"] = displayed.at("preferences_panel");
  persistedRow["persistence_file"] = persistence(
      preferencesPath, saved, reopened.settings().boolean("binary_pad_edges"),
      boolean(displayed.at("preferences_panel").at("toggle_value")));
  const auto persistedDraft = capture(reopened, recorder, persistedRow, true);
  report["scenarios"].push_back(publishScenario(
      reopened, recorder, source, reopenedSource, std::move(persistedRow), persistedDraft, true));

  Window reverse(preferencesPath);
  addSource(reverse, source, "Reverse overridden binary pad edges");
  const auto reverseSource = reverse.nativeSnapshot();
  auto reverseRow = scenario("reverse_override", reverse.settings().boolean("binary_pad_edges"));
  const auto reverseDraft = capture(reverse, recorder, reverseRow, true);
  const auto reversed = recorder.render(
      reverse, reverseRow, "binary_erode.pad_edges.override_false",
      click(reverseDraft.at("operation_dialog").at("controls").at("pad_edges").at("point")));
  parameters(reversed.at("operation_dialog").at("parameters"), false);
  require(reverse.settings().boolean("binary_pad_edges"),
          "Actual reverse operation override changed the global true setting");
  reverseRow["override_value"] = reversed.at("operation_dialog").at("parameters").at("pad_edges");
  reverseRow["overridden_parameters"] = reversed.at("operation_dialog").at("parameters");
  report["supplementary"]["reverse_override"] = publishScenario(
      reverse, recorder, source, reverseSource, std::move(reverseRow), reversed, false);
  require(readBytes(preferencesPath) == savedBytes,
          "Fresh Window rendering or erosion altered saved Preferences bytes");
  const auto reportPath = (root / "imgui-cpp.json").u8string();
  writeJson(reportPath, report);
  require(readJson(reportPath) == report, "Pad-edges report was not written exactly");
  std::cout << "Native binary_pad_edges consumer: actual Preferences checkbox/Apply, "
               "false frozen capture, both actual operation overrides, displayed "
               "fresh reopen, exact border bytes and one-step compatibility undo\n";
  return 0;
}
} // namespace imagejxx::desktop
