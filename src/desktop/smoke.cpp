#include "window.hpp"
#include <imagejxx/preferences.hpp>
#include <iostream>
#include <stdexcept>
namespace imagejxx::desktop {
namespace {
Json selection(const std::string &kind, const Json &points) {
  return {{"kind", kind}, {"points", points}, {"width", 1}};
}
} // namespace
int Window::smokeTest(const std::string &directory) {
  auto commands = catalog();
  auto check = [](bool ok, const char *message) {
    if (!ok)
      throw std::runtime_error(message);
  };
  Image input(64, 48, 1, PixelType::UInt16);
  for (int y = 0; y < input.height; ++y)
    for (int x = 0; x < input.width; ++x)
      input.at(x, y) = x * 100 + y;
  auto d = std::make_shared<Document>(Stack{input, input}, "Native test");
  addDocument(d);
  d->select(selection("rectangle", {Json{4, 4}, Json{20, 20}}));
  auto selected = d->state().metadata["selection"];
  auto proposed = d->compute("add", {{"value", 10}}, true);
  d->commit(proposed);
  check(d->frame().at(0, 0) == 0 && d->frame().at(5, 5) == 515, "Masked edit");
  d->undo();
  check(d->frame().at(5, 5) == 505, "Undo");
  d->redo();
  check(d->frame().at(5, 5) == 515, "Redo");
  action("add_roi");
  auto cal = d->state().metadata["calibration"];
  cal["pixel_width"] = .5;
  cal["pixel_height"] = 2;
  auto calibrationEdit = d->state();
  calibrationEdit.metadata["calibration"] = cal;
  d->commit(std::move(calibrationEdit));
  results.push_back(d->measure());
  check(d->measure()["Area"].get<double>() == 256, "Calibration / ROI area");
  workflow = Json::array({Json{{"operation", "add"},
                               {"parameters", Json{{"value", 10}}},
                               {"all_slices", true},
                               {"slice_index", 0},
                               {"selection", selected}}});
  auto file = directory + "/native.ijx";
  saveSession(file, documents(), results, workflow);
  Json rows = Json::array(), steps = Json::array();
  auto restored = loadSession(file, rows, steps);
  check(restored.size() == 1 && restored[0]->state().frames.size() == 2 &&
            restored[0]->frame().pixels == d->frame().pixels &&
            rows.size() == 1 && steps.size() == 1,
        "Session round trip");
  auto result = runWorkflow(*restored[0], steps);
  check(result.frames[0].at(5, 5) == 525, "Workflow replay");
  writeImages(directory + "/native.npy", d->state().frames.materialize());
  check(readImages(directory + "/native.npy")[0].pixels == d->frame().pixels,
        "NPY round trip");
  if (tiffAvailable()) {
    writeImages(directory + "/µ-日本.tif", d->state().frames.materialize());
    check(readImages(directory + "/µ-日本.tif")[0].pixels == d->frame().pixels,
          "Unicode TIFF path");
    writeImages(directory + "/native.tif", d->state().frames.materialize());
    check(readImages(directory + "/native.tif")[0].pixels == d->frame().pixels,
          "TIFF round trip");
  }
  writeImages(directory + "/µ-日本.pgm", {d->frame()});
  check(readImages(directory + "/µ-日本.pgm")[0].pixels == d->frame().pixels,
        "Unicode PNM path");
  for (auto type : {PixelType::UInt8, PixelType::UInt16}) {
    for (int channels : {1, 3, 4}) {
      Image raster(8, 6, channels, type);
      for (int y = 0; y < raster.height; ++y)
        for (int x = 0; x < raster.width; ++x)
          for (int c = 0; c < channels; ++c)
            raster.at(x, y, c) =
                (x + y) *
                13; // Gray values in an explicitly RGB image must stay RGB.
      auto fileName = directory + "/raster-" + typeName(type) + "-" +
                      std::to_string(channels) + ".png";
      writeImages(fileName, {raster});
      const auto returned = readImages(fileName)[0];
      check(returned.channels == channels && returned.type == type &&
                returned.pixels == raster.pixels,
            "PNG pixel type / channels round trip");
    }
  }
  // Dispatch every catalog command with suitable sample inputs; preserve exact
  // results for cross-UI checks.
  Json outputs = Json::array();
  for (auto value : commands) {
    auto c = value;
    auto id = c["id"].get<std::string>();
    Image source(16, 16, 1, id == "remove_nans_imagej" ? PixelType::Float32 : PixelType::UInt8);
    for (int y = 0; y < 16; ++y)
      for (int x = 0; x < 16; ++x)
        source.at(x, y) = x > 3 && x < 12 && y > 3 && y < 12 ? 100 + x : 0;
    if (id == "binary_erode" || id == "binary_dilate" || id == "binary_open" ||
        id == "binary_close")
      source = threshold(source, 1, 255);
    auto parameters = defaults(c, source);
    if (id == "invert_uint16")
      source.type = PixelType::UInt16;
    if (id == "rgb_to_hsv" || id == "color_deconvolution")
      source = merge_channels({source, source, source});
    if (id == "hsv_to_rgb") {
      source = Image(16, 16, 3, PixelType::Float64);
      for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
          source.at(x, y, 0) = x / 16.0;
          source.at(x, y, 1) = .5;
          source.at(x, y, 2) = .8;
        }
    }
    if (id == "distance_transform")
      source.at(0, 0) = 0;
    if (id == "sqrt" || id == "log" || id == "log1p" || id == "exp")
      source = fill(Image(16, 16, 1, PixelType::Float64), 2);
    if (id == "divide")
      parameters["value"] = 2;
    if (id == "calculate") {
      Json other = Json::array();
      for (int y = 0; y < 16; ++y) {
        Json row = Json::array();
        for (int x = 0; x < 16; ++x)
          row.push_back(2);
        other.push_back(row);
      }
      parameters["other"] = other;
    }
    auto output = canonical(execute(id, source, parameters));
    Json pixels = Json::array(), original = Json::array();
    for (auto v : output.pixels)
      pixels.push_back(v);
    for (auto v : source.pixels)
      original.push_back(v);
    outputs.push_back(Json{{"operation", id},
                           {"parameters", parameters},
                           {"width", output.width},
                           {"height", output.height},
                           {"channels", output.channels},
                           {"dtype", typeName(output.type)},
                           {"pixels", pixels},
                           {"input", original},
                           {"input_channels", source.channels},
                           {"input_dtype", typeName(source.type)}});
    if (id == "calculate") {
      for (auto operation : {"add", "and", "or", "xor"}) {
        auto variantParameters = parameters;
        variantParameters["operation"] = operation;
        variantParameters["dtype"] = "float64";
        const auto variant = canonical(execute(id, source, variantParameters));
        Json variantPixels = Json::array();
        for (auto sample : variant.pixels)
          variantPixels.push_back(sample);
        outputs.push_back(Json{{"operation", id},
                               {"parameters", variantParameters},
                               {"width", variant.width},
                               {"height", variant.height},
                               {"channels", variant.channels},
                               {"dtype", typeName(variant.type)},
                               {"pixels", variantPixels},
                               {"input", original},
                               {"input_channels", source.channels},
                               {"input_dtype", typeName(source.type)}});
      }
    }
  }
  writeJson(directory + "/native-commands.json", {{"outputs", outputs}});
  action("export_pdf", {{"path", directory + "/native.pdf"}});
  check(readBytes(directory + "/native.pdf")[0] == '%', "PDF export");
  auto drawing = renderHeadless();
  check(drawing["vertices"].get<int>() > 0, "ImGui drawing");
  writeJson(directory + "/imgui-frame.json", drawing);
  writeImages(directory + "/native-display.png",
              {renderImage(d->frame(), d->state().metadata)});
  Window inputTest(directory + "/input-test-prefs.json");
  inputTest.action("new", {{"width", 40}, {"height", 32}});
  auto interaction = inputTest.renderHeadless(4, true);
  auto bounds = selectionBounds(
      inputTest.document()->state().metadata["selection"], 40, 32);
  check(bounds.x == 3 && bounds.y == 4 && bounds.width == 10 &&
            bounds.height == 10,
        "ImGui mouse selection");
  inputTest.recording = true;
  inputTest.applyOperation("add", {{"value", 20}}, false);
  inputTest.wait();
  check(inputTest.error().empty() &&
            inputTest.document()->frame().at(5, 6) == 20 &&
            inputTest.document()->frame().at(0, 0) == 0,
        "ImGui worker edit");
  inputTest.action("add_roi");
  inputTest.action("measure");
  inputTest.action("copy");
  inputTest.action("paste_new");
  check(inputTest.document()->frame().width == 10 &&
            inputTest.results[0]["count"] == 100 &&
            inputTest.workflow.size() == 1,
        "ImGui ROI, measurements and clipboard");
  Window styleTest(directory + "/roi-style-input-prefs.json");
  auto styleOwner = std::make_shared<imagejxx::Document>(
      Dataset::from_stack({Image(32, 24, 1, PixelType::UInt16)}),
      "ROI style input");
  imagejxx::Selection original("rectangle", {{8, 7}, {18, 15}}, 3);
  original.attributes["color"] = std::string("#12345678");
  original.attributes["extension"] = metadata_object({{"retained", true}});
  styleOwner->select(original);
  styleTest.addNativeDocument(styleOwner);
  const auto source = styleOwner->snapshot();
  auto click = [](const Json &point) {
    return Json::array(
        {{{"type", "position"}, {"space", "screen"}, {"point", point}},
         {{"type", "button"}, {"name", "left"}, {"down", true}},
         {{"type", "button"}, {"name", "left"}, {"down", false}}});
  };
  auto typing = [&](const Json &point, const char *text) {
    auto events = click(point);
    events.push_back({{"type", "key"}, {"name", "Ctrl"}, {"down", true}});
    events.push_back({{"type", "key"}, {"name", "A"}, {"down", true}});
    events.push_back({{"type", "key"}, {"name", "A"}, {"down", false}});
    events.push_back({{"type", "key"}, {"name", "Ctrl"}, {"down", false}});
    events.push_back({{"type", "text"}, {"value", text}});
    events.push_back({{"type", "key"}, {"name", "Enter"}, {"down", true}});
    events.push_back({{"type", "key"}, {"name", "Enter"}, {"down", false}});
    return events;
  };
  styleTest.action("show_selection_properties");
  auto panel = styleTest.renderHeadless()["selection_properties"];
  auto events = typing(panel["controls"]["Stroke color"], "green");
  for (const auto &event : typing(panel["controls"]["Fill color"], "#80123456"))
    events.push_back(event);
  for (const auto &event : typing(panel["controls"]["Stroke width"], "0"))
    events.push_back(event);
  const auto draftFrame = styleTest.renderHeadless(3, false, false, events);
  panel = draftFrame["selection_properties"];
  check(panel["open"] == true && string(panel["error"]).empty() &&
            styleOwner->snapshot().data().selection ==
                source.data().selection &&
            styleOwner->snapshot().revisions().selection ==
                source.revisions().selection,
        "Installed ImGui ROI style typing published its detached draft");
  const auto appliedFrame =
      styleTest.renderHeadless(3, false, false, click(panel["apply_point"]));
  const auto styled = *styleOwner->snapshot().data().selection;
  const auto typedStyle = roi_style(styled);
  check(appliedFrame["selection_properties"]["open"] == false &&
            typedStyle.present &&
            typedStyle.stroke_color == RoiColor{0, 255, 0, 255} &&
            typedStyle.fill_color == RoiColor{18, 52, 86, 128} &&
            typedStyle.stroke_width == 0 && styled.width == 3 &&
            styled.points == original.points &&
            styleOwner->snapshot().data().previous_selection ==
                source.data().selection &&
            styleOwner->snapshot().revisions().selection ==
                source.revisions().selection + 1 &&
            !styleOwner->snapshot().dirty() && styleOwner->undo_steps() == 0,
        "Installed ImGui ROI style Apply did not publish the complete "
        "transient style");
  styleTest.action("show_selection_properties");
  panel = styleTest.renderHeadless()["selection_properties"];
  const auto cancelDraft = styleTest.renderHeadless(
      3, false, false, typing(panel["controls"]["Stroke color"], "red"));
  const auto cancelledFrame = styleTest.renderHeadless(
      3, false, false,
      click(cancelDraft["selection_properties"]["cancel_point"]));
  check(cancelledFrame["selection_properties"]["open"] == false &&
            styleOwner->snapshot().data().selection ==
                std::optional<imagejxx::Selection>{styled} &&
            !styleOwner->snapshot().dirty() && styleOwner->undo_steps() == 0,
        "Installed ImGui ROI style Cancel published a draft");
  styleTest.action("add_roi", {{"name", "Typed ROI style"}});
  const auto styleFile = directory + "/roi-style-input.ijx";
  saveSession(styleFile, styleTest.documents(), styleTest.results,
              styleTest.workflow);
  Json styleRows = Json::array(), styleSteps = Json::array();
  const auto styleRestored = loadSession(styleFile, styleRows, styleSteps);
  check(styleRestored.size() == 1 &&
            styleRestored[0]->nativeSnapshot().data().selection ==
                std::optional<imagejxx::Selection>{styled} &&
            styleRestored[0]->nativeSnapshot().data().rois[0].selection ==
                styled,
        "Installed input-created ROI style did not survive session records");
  writeJson(directory + "/roi-style-input.json",
            {{"draft_frame", draftFrame},
             {"applied_frame", appliedFrame},
             {"cancelled_frame", cancelledFrame},
             {"selection", selectionJson(styled)},
             {"restored_document", documentJson(*styleRestored[0])}});
  const auto preferencesFile = directory + "/preferences-input-prefs.json";
  check(!std::filesystem::exists(std::filesystem::u8path(preferencesFile)),
        "Preferences input producer needs a fresh saved-file destination");
  Window preferencesTest(preferencesFile);
  const auto initialAutoDpi = preferencesTest.settings().boolean("auto_dpi");
  const auto preferencesFrame =
      preferencesTest.renderHeadless(3, false, false, Json::array(), true);
  const auto &preferencesInteraction = preferencesFrame["preferences_interaction"];
  const auto &before = preferencesInteraction["before"];
  const auto &draft = preferencesInteraction["changed_draft"];
  const auto &applied = preferencesInteraction["applied"];
  check(before["visible"] == true && before["toggle_name"] == "auto_dpi" &&
            before["toggle_value"] == initialAutoDpi &&
            before["controls"] == Json::array({"auto_dpi", "print_dpi"}) &&
            before["search"] == "dpi" && before["error"] == "" &&
            draft["toggle_value"] == !initialAutoDpi &&
            draft["settings_auto_dpi"] == initialAutoDpi &&
            applied["toggle_value"] == !initialAutoDpi &&
            applied["settings_auto_dpi"] == !initialAutoDpi &&
            applied["error"] == "" && preferencesTest.error().empty(),
        "Installed Preferences input did not preserve a detached draft and Apply it");
  const auto savedBytes = readBytes(preferencesFile);
  const std::string savedText(savedBytes.begin(), savedBytes.end());
  const auto savedEnvelope = Json::parse(savedText);
  const auto savedDocument = PreferencesDocument::parse(savedText, false);
  check(savedEnvelope["format"] == "imagejxx-preferences" &&
            savedEnvelope["version"] == Settings::schema_version &&
            savedEnvelope["values"]["auto_dpi"] == !initialAutoDpi &&
            savedEnvelope["values"] == preferencesTest.preferencesJson()["values"] &&
            savedDocument.settings.values() == preferencesTest.settings().values() &&
            !savedDocument.write_blocked && savedDocument.warnings.empty(),
        "Actual Preferences Apply did not immediately publish a native version-1 file");
  Window preferencesReopened(preferencesFile);
  check(preferencesReopened.settings().boolean("auto_dpi") == !initialAutoDpi &&
            preferencesReopened.settings().values() == savedDocument.settings.values(),
        "Fresh standalone Window did not reopen the input-created preferences");
  const auto reopenedFrame =
      preferencesReopened.renderHeadless(3, false, false, Json::array(), true);
  const auto &reopenedInteraction = reopenedFrame["preferences_interaction"];
  check(reopenedInteraction["before"]["visible"] == true &&
            reopenedInteraction["before"]["toggle_name"] == "auto_dpi" &&
            reopenedInteraction["before"]["toggle_value"] == !initialAutoDpi &&
            reopenedInteraction["before"]["controls"] ==
                Json::array({"auto_dpi", "print_dpi"}) &&
            reopenedInteraction["changed_draft"]["toggle_value"] == initialAutoDpi &&
            reopenedInteraction["applied"]["toggle_value"] == initialAutoDpi &&
            preferencesReopened.settings().boolean("auto_dpi") == initialAutoDpi &&
            PreferencesDocument::load(preferencesFile, false)
                    .settings.boolean("auto_dpi") == initialAutoDpi,
        "Fresh Preferences control did not display/reapply its saved checkbox value");
  writeJson(directory + "/preferences-input.json",
            {{"setting", "auto_dpi"},
             {"initial_value", initialAutoDpi},
             {"saved_path", preferencesFile},
             {"saved_text_before_reopen", savedText},
             {"saved_envelope_before_reopen", savedEnvelope},
             {"applied_frame", preferencesFrame},
             {"reopened_frame", reopenedFrame},
             {"final_envelope", Json::parse(PreferencesDocument::load(
                                               preferencesFile, false).json())}});
  std::cout << "Native Preferences input: actual Edit/Preferences pointer opening, "
               "Search keyboard dpi, checkbox draft/Apply, immediate version-1 "
               "save and fresh-window displayed-value reopen passed\n";
  for (auto image : documents())
    image->markSaved();
  std::cout << "Native desktop: " << commands.size()
            << " commands, selection, history, stacks, calibration, session, "
               "NPY passed"
            << (tiffAvailable() ? ", TIFF passed\n" : "\n");
  return 0;
}
} // namespace imagejxx::desktop
