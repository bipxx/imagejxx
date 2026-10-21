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
          "Binary iterations consumer changed the pinned UInt8 geometry");
  Json result = Json::array();
  for (const auto value : image.pixels) {
    require(std::isfinite(value) && (value == 0 || value == 255),
            "Binary iterations consumer returned a nonbinary UInt8 pixel");
    result.push_back(static_cast<unsigned int>(value));
  }
  return result;
}

Json pinnedPixels(int iterations) {
  // Exact raw UInt8 outputs of pinned ByteProcessor cases 7, 103 and 199:
  // an all-foreground 9x7 plane, black background, no edge padding.
  require(iterations >= 1 && iterations <= 3,
          "Unknown pinned binary iterations fixture");
  static const std::vector<std::vector<unsigned int>> cases{
      {0, 0, 0, 0, 0, 0, 0, 0, 0,
       0, 255, 255, 255, 255, 255, 255, 255, 0,
       0, 255, 255, 255, 255, 255, 255, 255, 0,
       0, 255, 255, 255, 255, 255, 255, 255, 0,
       0, 255, 255, 255, 255, 255, 255, 255, 0,
       0, 255, 255, 255, 255, 255, 255, 255, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0, 0, 0, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0,
       0, 0, 255, 255, 255, 255, 255, 0, 0,
       0, 0, 255, 255, 255, 255, 255, 0, 0,
       0, 0, 255, 255, 255, 255, 255, 0, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0, 0, 0, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0,
       0, 0, 0, 255, 255, 255, 0, 0, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0}};
  return cases[static_cast<std::size_t>(iterations - 1)];
}

void addSource(Window &window, const Image &source, const std::string &title) {
  DatasetShape shape;
  shape.x = 9;
  shape.y = 7;
  window.addNativeDocument(std::make_shared<imagejxx::Document>(
      Dataset::from_planes(shape, {source}), title));
  window.recording = true;
  require(window.nativeDocument()->undo_steps() == 0 && window.workflow.empty(),
          "Binary iterations consumer needs a fresh document owner");
}

void apiAction(Window &window, Json &inputs, const std::string &name,
               const Json &parameters) {
  window.action(name, parameters);
  inputs.push_back({{"method", "action"},
                    {"name", name},
                    {"parameters", parameters}});
}

void apiIterations(Window &window, Json &inputs, int iterations) {
  const Json values{{"binary_iterations", iterations}};
  window.updateSettings(values, false);
  inputs.push_back({{"method", "updateSettings"},
                    {"values", values},
                    {"persist", false}});
}

Json point(const Json &value) {
  require(value.is_array() && value.size() == 2 && value[0].is_number() &&
              value[1].is_number() && number(value[0]) >= 0 &&
              number(value[1]) >= 0,
          "Binary iterations consumer has no visible control point");
  return value;
}

Json click(const Json &location) {
  return Json::array(
      {{{"type", "position"}, {"space", "screen"}, {"point", point(location)}},
       {{"type", "button"}, {"name", "left"}, {"down", true}},
       {{"type", "button"}, {"name", "left"}, {"down", false}},
       {{"type", "wait"}, {"frames", 2}}});
}

Json typeInteger(const Json &location, int value) {
  auto events = click(location);
  for (const auto &event : Json::array(
           {{{"type", "key"}, {"name", "Ctrl"}, {"down", true}},
            {{"type", "key"}, {"name", "A"}, {"down", true}},
            {{"type", "key"}, {"name", "A"}, {"down", false}},
            {{"type", "key"}, {"name", "Ctrl"}, {"down", false}},
            {{"type", "text"}, {"value", std::to_string(value)}},
            {{"type", "key"}, {"name", "Enter"}, {"down", true}},
            {{"type", "key"}, {"name", "Enter"}, {"down", false}},
            {{"type", "wait"}, {"frames", 2}}}))
    events.push_back(event);
  return events;
}

Json inject(Window &window, const Json &events, Json &record,
            const std::string &) {
  const auto rendered = window.renderHeadless(3, false, false, events);
  const auto &trace = rendered.at("input_trace");
  require(trace.is_array() && trace.size() == events.size(),
          "Binary iterations consumer did not deliver every requested input");
  bool down = false;
  for (std::size_t i = 0; i < events.size(); ++i) {
    const auto &event = events[i];
    const auto type = string(event.at("type"));
    require(trace[i].at("type") == type,
            "Binary iterations consumer input trace disagrees with delivery");
    if (type == "position" || type == "button") {
      if (type == "button")
        down = boolean(event.at("down"));
      record.push_back({{"type", "pointer"},
                        {"position", trace[i].at("mouse_screen")},
                        {"button", "left"},
                        {"down", down}});
    } else if (type == "key")
      record.push_back({{"type", "key"},
                        {"key", event.at("name")},
                        {"down", event.at("down")}});
    else if (type == "text")
      record.push_back({{"type", "text"},
                        {"text", event.at("value")}});
  }
  require(window.error().empty(),
          "Binary iterations consumer encountered an editor input error");
  return rendered;
}

Json preferences(Window &window, Json &apiInputs) {
  apiAction(window, apiInputs, "show_preferences",
            {{"search", "binary_iterations"}});
  auto rendered = window.renderHeadless();
  const auto &panel = rendered.at("preferences_panel");
  require(panel.at("open") == true && panel.at("error") == "" &&
              panel.at("controls") == Json::array({"binary_iterations"}) &&
              panel.at("fields").at("binary_iterations").at("kind") ==
                  "integer",
          "Binary iterations Preferences integer field was not displayed");
  return rendered;
}

Json capture(Window &window, Json &apiInputs, int expected) {
  apiAction(window, apiInputs, "show_operation",
            {{"operation", "binary_erode"}});
  auto rendered = window.renderHeadless();
  const auto &dialog = rendered.at("operation_dialog");
  require(dialog.at("open") == true && dialog.at("id") == "binary_erode" &&
              dialog.at("parameters").at("iterations") == expected &&
              dialog.at("parameters").at("neighbors") == 1 &&
              dialog.at("parameters").at("black_background") == true &&
              dialog.at("parameters").at("pad_edges") == false,
          "Binary iterations operation draft did not capture the live settings");
  return rendered;
}

Json publish(Window &window, const Image &source,
             const DocumentSnapshot &original, Json row,
             const Json &rendered, int expectedIterations) {
  const auto sourcePixels = bytes(source);
  require(bytes(window.document()->frame()) == sourcePixels &&
              window.nativeDocument()->undo_steps() == 0,
          "Binary iterations Apply did not start from the untouched source");
  auto inputs = row.at("input_events");
  const auto applied =
      inject(window, click(rendered.at("operation_dialog").at("apply_point")),
             inputs, "binary_erode.Apply");
  require(applied.at("operation_dialog").at("open") == false,
          "Actual operation Apply pointer input did not close the modal");
  window.wait();
  require(window.error().empty() && window.workflow.size() == 1 &&
              window.workflow[0].at("operation") == "binary_erode" &&
              window.nativeDocument()->undo_steps() == 1,
          "Binary iterations operation did not publish one recorded undo step");
  const auto effective =
      integer(window.workflow[0].at("parameters").at("iterations"));
  const auto actualPixels = bytes(window.document()->frame());
  require(effective == expectedIterations &&
              actualPixels == pinnedPixels(expectedIterations) &&
              actualPixels != pinnedPixels(3),
          "Binary iterations output differs from the exact pinned UInt8 case");
  row["current_setting_value"] = window.settings().integer("binary_iterations");
  row["effective_parameter_value"] = effective;
  row["workflow_iterations"] = effective;
  row["workflow"] = window.workflow;
  row["pixels"] = actualPixels;
  row["input_events"] = std::move(inputs);
  row["history_steps"] = window.nativeDocument()->undo_steps();
  row["source_preserved"] =
      bytes(*original.data().dataset.plane()) == sourcePixels &&
      bytes(source) == sourcePixels;
  apiAction(window, row["api_inputs"], "undo", Json::object());
  row["undo_restored_source"] =
      bytes(window.document()->frame()) == sourcePixels &&
      window.nativeDocument()->undo_steps() == 0 &&
      window.nativeDocument()->redo_steps() == 1;
  require(row.at("source_preserved") == true &&
              row.at("undo_restored_source") == true,
          "Binary iterations consumer changed its source or failed exact undo");
  return row;
}

Json scenario(const std::string &id, int settingValue) {
  return {{"id", id},
          {"setting_value", settingValue},
          {"input_events", Json::array()},
          {"api_inputs", Json::array()}};
}

Json persistence(const std::string &path, const std::string &saved,
                 std::int64_t reopenedValue, const Json &displayedValue) {
  return {{"path", path},
          {"bytes_utf8", saved},
          {"envelope", Json::parse(saved)},
          {"reopened_setting_value", reopenedValue},
          {"displayed_setting_value", displayedValue}};
}
} // namespace

int Window::binaryIterationsConsumerTest(const std::string &directory) {
  checkThread();
  require(!directory.empty() && documents().empty(),
          "Binary iterations consumer requires a fresh directory and Window");
  const auto root =
      std::filesystem::absolute(std::filesystem::u8path(directory)).lexically_normal();
  require(std::filesystem::create_directory(root),
          "Binary iterations consumer directory must not already exist");
  const auto preferencesPath = (root / "preferences.json").u8string();
  require(!std::filesystem::exists(std::filesystem::u8path(preferencesPath)) &&
              settings().integer("binary_iterations") == 1,
          "Binary iterations consumer did not start with native defaults");
  Image source(9, 7, 1, PixelType::UInt8);
  std::fill(source.pixels.begin(), source.pixels.end(), 255);
  Json report{{"schema_version", 1},
              {"context", "imgui_cpp"},
              {"requirement_id", "G052.binary_erode.iterations"},
              {"shape", {7, 9}},
              {"source", bytes(source)},
              {"scenarios", Json::array()}};

  addSource(*this, source, "Default binary iterations");
  const auto defaultSource = nativeSnapshot();
  auto defaultRow = scenario("default", settings().integer("binary_iterations"));
  const auto defaultDraft = capture(*this, defaultRow["api_inputs"], 1);
  defaultRow["captured_parameters"] = defaultDraft["operation_dialog"]["parameters"];
  report["scenarios"].push_back(publish(*this, source, defaultSource,
                                        std::move(defaultRow), defaultDraft, 1));

  Window changed(preferencesPath);
  addSource(changed, source, "Changed binary iterations");
  const auto changedSource = changed.nativeSnapshot();
  auto changedRow = scenario("changed", 2);
  const auto before = preferences(changed, changedRow["api_inputs"]);
  require(before["preferences_panel"]["fields"]["binary_iterations"]["draft"] == 1,
          "Initial binary iterations field did not display the default");
  const auto typed =
      inject(changed,
             typeInteger(before["preferences_panel"]["fields"]["binary_iterations"]["point"], 2),
             changedRow["input_events"], "Preferences.binary_iterations");
  require(typed["preferences_panel"]["fields"]["binary_iterations"]["draft"] == 2 &&
              changed.settings().integer("binary_iterations") == 1,
          "Actual Preferences integer typing failed to retain a detached draft");
  const auto applied = inject(changed, click(typed["preferences_panel"]["apply_point"]),
                              changedRow["input_events"], "Preferences.Apply");
  require(changed.settings().integer("binary_iterations") == 2 &&
              applied["preferences_panel"]["fields"]["binary_iterations"]["draft"] == 2 &&
              applied["preferences_panel"]["error"] == "",
          "Actual Preferences Apply did not publish binary iterations 2");
  const auto savedBytes = readBytes(preferencesPath);
  const std::string saved(savedBytes.begin(), savedBytes.end());
  const auto savedDocument = PreferencesDocument::parse(saved, false);
  const auto savedEnvelope = Json::parse(saved);
  require(savedEnvelope.at("format") == "imagejxx-preferences" &&
              savedEnvelope.at("version") == Settings::schema_version &&
              savedEnvelope.at("values") == changed.preferencesJson().at("values") &&
              savedDocument.settings.integer("binary_iterations") == 2 &&
              savedDocument.settings.values() == changed.settings().values() &&
              savedDocument.warnings.empty() && !savedDocument.write_blocked,
          "Actual integer Preferences Apply did not immediately save native bytes");
  changedRow["preferences_before"] = before["preferences_panel"];
  changedRow["preferences_typed"] = typed["preferences_panel"];
  changedRow["preferences_applied"] = applied["preferences_panel"];
  changedRow["persistence_file"] = {{"path", preferencesPath},
                                    {"bytes_utf8", saved},
                                    {"envelope", savedEnvelope}};
  changedRow["setting_value"] = changed.settings().integer("binary_iterations");
  const auto changedDraft = capture(changed, changedRow["api_inputs"], 2);
  changedRow["captured_parameters"] = changedDraft["operation_dialog"]["parameters"];
  report["scenarios"].push_back(publish(changed, source, changedSource,
                                        std::move(changedRow), changedDraft, 2));

  Window frozen(preferencesPath);
  addSource(frozen, source, "Frozen binary iterations");
  const auto frozenSource = frozen.nativeSnapshot();
  auto frozenRow = scenario("frozen", frozen.settings().integer("binary_iterations"));
  const auto frozenDraft = capture(frozen, frozenRow["api_inputs"], 2);
  frozenRow["captured_parameters"] = frozenDraft["operation_dialog"]["parameters"];
  frozenRow["captured_scenario"] = "changed";
  apiIterations(frozen, frozenRow["api_inputs"], 3);
  const auto retainedDraft = frozen.renderHeadless();
  require(frozen.settings().integer("binary_iterations") == 3 &&
              retainedDraft["operation_dialog"]["open"] == true &&
              retainedDraft["operation_dialog"]["parameters"] ==
                  frozenDraft["operation_dialog"]["parameters"],
          "API preference mutation replaced the existing captured operation draft");
  frozenRow["retained_parameters"] = retainedDraft["operation_dialog"]["parameters"];
  report["scenarios"].push_back(publish(frozen, source, frozenSource,
                                        std::move(frozenRow), retainedDraft, 2));

  Window overrideWindow(preferencesPath);
  addSource(overrideWindow, source, "Overridden binary iterations");
  const auto overrideSource = overrideWindow.nativeSnapshot();
  auto overrideRow = scenario("override", 2);
  apiIterations(overrideWindow, overrideRow["api_inputs"], 2);
  overrideRow["setting_value"] =
      overrideWindow.settings().integer("binary_iterations");
  const auto overrideDraft = capture(overrideWindow, overrideRow["api_inputs"], 2);
  overrideRow["captured_parameters"] = overrideDraft["operation_dialog"]["parameters"];
  apiIterations(overrideWindow, overrideRow["api_inputs"], 3);
  const auto retainedOverride = overrideWindow.renderHeadless();
  require(retainedOverride["operation_dialog"]["parameters"] ==
              overrideDraft["operation_dialog"]["parameters"],
          "Override scenario did not retain its distinct capture of 2");
  const auto overridden =
      inject(overrideWindow,
             typeInteger(retainedOverride["operation_dialog"]["controls"]["iterations"]["point"], 1),
             overrideRow["input_events"], "binary_erode.iterations");
  require(overridden["operation_dialog"]["parameters"]["iterations"] == 1 &&
              overrideWindow.settings().integer("binary_iterations") == 3,
          "Actual operation integer input did not override the retained draft");
  overrideRow["override_value"] =
      overridden["operation_dialog"]["parameters"]["iterations"];
  overrideRow["overridden_parameters"] = overridden["operation_dialog"]["parameters"];
  report["scenarios"].push_back(publish(overrideWindow, source, overrideSource,
                                        std::move(overrideRow), overridden, 1));

  const auto unchangedBytes = readBytes(preferencesPath);
  require(unchangedBytes == savedBytes,
          "Post-capture API mutations altered the saved Preferences file");
  Window reopened(preferencesPath);
  addSource(reopened, source, "Reopened binary iterations");
  const auto reopenedSource = reopened.nativeSnapshot();
  auto persistedRow = scenario("persisted", reopened.settings().integer("binary_iterations"));
  const auto displayed = preferences(reopened, persistedRow["api_inputs"]);
  require(reopened.settings().values() == savedDocument.settings.values() &&
              displayed["preferences_panel"]["fields"]["binary_iterations"]["draft"] == 2,
          "Fresh Window did not display the saved binary iterations value");
  persistedRow["preferences_reopened"] = displayed["preferences_panel"];
  persistedRow["persistence_file"] =
      persistence(preferencesPath, saved,
                  reopened.settings().integer("binary_iterations"),
                  displayed["preferences_panel"]["fields"]["binary_iterations"]["draft"]);
  const auto persistedDraft = capture(reopened, persistedRow["api_inputs"], 2);
  persistedRow["captured_parameters"] = persistedDraft["operation_dialog"]["parameters"];
  report["scenarios"].push_back(publish(reopened, source, reopenedSource,
                                        std::move(persistedRow), persistedDraft, 2));
  require(readBytes(preferencesPath) == savedBytes,
          "Fresh Window rendering or computation altered saved Preferences bytes");
  const auto reportPath = (root / "imgui-cpp.json").u8string();
  writeJson(reportPath, report);
  require(readJson(reportPath) == report,
          "Binary iterations consumer report was not written exactly");
  std::cout << "Native binary_iterations consumer: actual Preferences integer "
               "typing/Apply, frozen capture, operation override, saved bytes, "
               "fresh Window displayed reopen, exact UInt8 pixels and one-step "
               "undo passed\n";
  return 0;
}
} // namespace imagejxx::desktop
