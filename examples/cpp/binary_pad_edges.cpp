#include <imagejxx/application.hpp>
#include <imagejxx/binary.hpp>
#include <imagejxx/preferences.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace {
namespace fs = std::filesystem;
using namespace imagejxx;

void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

std::vector<int> bytes(const Image &image) {
  check(image.width == 9 && image.height == 7 && image.channels == 1 &&
            image.type == PixelType::UInt8 && image.pixels.size() == 63,
        "Pad-edges consumer changed the pinned UInt8 geometry");
  std::vector<int> result;
  for (const auto value : image.pixels) {
    check(std::isfinite(value) && (value == 0 || value == 255),
          "Pad-edges consumer returned a nonbinary byte");
    result.push_back(static_cast<int>(value));
  }
  return result;
}

Image sourceImage(bool hole = false) {
  Image image(9, 7, 1, PixelType::UInt8);
  std::fill(image.pixels.begin(), image.pixels.end(), 255);
  if (hole)
    image.at(4, 3) = 0;
  return image;
}

std::vector<int> erosionOracle(bool pad, bool hole) {
  // Owned enumeration, independent of the kernel and its output. The pinned
  // ImageJ cases 7/10 use a full 3x3 foreground test, including the center.
  std::vector<int> result;
  for (int y = 0; y < 7; ++y)
    for (int x = 0; x < 9; ++x) {
      bool foreground = true;
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
          const int xx = x + dx, yy = y + dy;
          const bool inside = xx >= 0 && xx < 9 && yy >= 0 && yy < 7;
          if ((!inside && !pad) || (inside && hole && xx == 4 && yy == 3))
            foreground = false;
        }
      result.push_back(foreground ? 255 : 0);
    }
  return result;
}

std::array<int, 4> optionValues(const BinaryMorphologyOptions &options) {
  return {{options.iterations, options.neighbors, int(options.black_background),
           int(options.pad_edges)}};
}

void fixedOptions(const BinaryMorphologyOptions &options) {
  check(options.iterations == 1 && options.neighbors == 1 &&
            options.black_background,
        "Pad-edges consumer changed another binary option");
}

std::string quote(const std::string &value) {
  const char *hex = "0123456789abcdef";
  std::string result = "\"";
  for (const auto byte : value) {
    const auto c = static_cast<unsigned char>(byte);
    if (c == '"' || c == '\\') {
      result += '\\';
      result += static_cast<char>(c);
    } else if (c < 32) {
      result += "\\u00";
      result += hex[c >> 4];
      result += hex[c & 15];
    } else
      result += static_cast<char>(c);
  }
  return result + '"';
}

void writePixels(std::ostream &stream, const std::vector<int> &values) {
  stream << '[';
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i)
      stream << ',';
    stream << values[i];
  }
  stream << ']';
}

void writeOptions(std::ostream &stream, bool pad) {
  stream << "{\"iterations\":1,\"neighbors\":1,\"black_background\":true,"
            "\"pad_edges\":"
         << (pad ? "true" : "false") << '}';
}

struct Observation {
  std::string id;
  bool setting = false, current = false, effective = false;
  bool sourcePreserved = false, optionsPreserved = false, undoPerformed = false;
  bool capturedAfter = false;
  bool undoRestoredSource = false, preparedPad = false, publishedInPlace = false;
  bool identity = false;
  std::size_t historySteps = 0, undoStepsAfter = 0, redoStepsAfter = 0;
  std::uint64_t documentId = 0;
  std::vector<int> source, pixels, preview, published, restored;
  std::string persistenceFile, persistenceBytes;
  bool reopenedValue = false;
};

Observation compute(const std::string &id, const Settings &settings,
                    const Image &source, bool hole,
                    const BinaryMorphologyOptions &captured,
                    const BinaryMorphologyOptions &execution) {
  fixedOptions(captured);
  fixedOptions(execution);
  const auto original = bytes(source);
  check(original == bytes(sourceImage(hole)),
        "Pad-edges consumer did not use the independent source fixture");
  const auto capturedBefore = optionValues(captured);
  const auto executionBefore = optionValues(execution);
  const auto expected = erosionOracle(execution.pad_edges, hole);
  const auto output = binary_morphology(source, "erode", execution);
  Observation row;
  row.id = id;
  row.setting = captured.pad_edges;
  row.current = settings.boolean("binary_pad_edges");
  row.effective = execution.pad_edges;
  row.source = original;
  row.pixels = bytes(output);
  check(row.pixels == expected,
        "Direct pad-edges output differs from the independent border oracle");
  const auto count = std::count(row.pixels.begin(), row.pixels.end(), 255);
  check(count == (hole ? (row.effective ? 54 : 26)
                       : (row.effective ? 63 : 35)),
        "Pad-edges output has the wrong foreground count");

  Application application;
  row.documentId = application.create(Dataset::from_stack({source}), id);
  const auto originalSnapshot = application.snapshot(row.documentId);
  TargetRequest request;
  request.roi = RoiPolicy::WholePlane;
  const auto target =
      application.resolve_target(request, row.documentId, "binary_erode");
  const OperationParameters parameters{
      {"iterations", std::int64_t(1)}, {"neighbors", std::int64_t(1)},
      {"black_background", true}, {"pad_edges", execution.pad_edges}};
  auto prepared = application.prepare("binary_erode", target, parameters);
  row.preparedPad = std::get<bool>(prepared.parameters().at("pad_edges"));
  check(prepared.active() && row.preparedPad == execution.pad_edges &&
            std::get<std::int64_t>(prepared.parameters().at("iterations")) == 1 &&
            std::get<std::int64_t>(prepared.parameters().at("neighbors")) == 1 &&
            std::get<bool>(prepared.parameters().at("black_background")),
        "Application preparation did not retain strict typed binary parameters");
  row.preview = bytes(*prepared.preview().data().dataset.plane());
  check(row.preview == expected &&
            bytes(*application.snapshot(row.documentId).data().dataset.plane()) == original &&
            application.history(row.documentId).undo_steps == 0,
        "Application preparation changed its source or produced incorrect preview");
  const auto published = application.publish(prepared);
  row.publishedInPlace = published.document_id == row.documentId && !published.created;
  row.published = bytes(*application.snapshot(row.documentId).data().dataset.plane());
  row.historySteps = application.history(row.documentId).undo_steps;
  row.identity = expected == original;
  check(row.publishedInPlace && !prepared.active() &&
            published.outputs.size() == 1 && !published.outputs.front().skipped &&
            row.published == expected && row.historySteps == (row.identity ? 0 : 1) &&
            application.history(row.documentId).redo_steps == 0,
        "Application publication changed output, membership or history semantics");
  if (!row.identity) {
    application.undo(row.documentId);
    row.undoPerformed = true;
  }
  row.restored = bytes(*application.snapshot(row.documentId).data().dataset.plane());
  row.undoStepsAfter = application.history(row.documentId).undo_steps;
  row.redoStepsAfter = application.history(row.documentId).redo_steps;
  row.undoRestoredSource = row.restored == original && row.undoStepsAfter == 0 &&
                           row.redoStepsAfter == (row.identity ? 0 : 1);
  row.sourcePreserved = bytes(source) == original &&
                        bytes(*originalSnapshot.data().dataset.plane()) == original &&
                        bytes(*target.source().data().dataset.plane()) == original;
  row.optionsPreserved = optionValues(captured) == capturedBefore &&
                         optionValues(execution) == executionBefore &&
                         bytes(*prepared.preview().data().dataset.plane()) == expected;
  check(row.sourcePreserved && row.optionsPreserved && row.undoRestoredSource,
        "Pad-edges source/options preservation or exact undo failed");
  row.capturedAfter = captured.pad_edges;
  return row;
}

void writeRow(std::ostream &stream, const Observation &row) {
  stream << "{\"id\":" << quote(row.id)
         << ",\"setting_value\":" << (row.setting ? "true" : "false")
         << ",\"current_setting_value\":" << (row.current ? "true" : "false")
         << ",\"effective_parameter_value\":" << (row.effective ? "true" : "false")
         << ",\"source_preserved\":" << (row.sourcePreserved ? "true" : "false")
         << ",\"options_preserved\":" << (row.optionsPreserved ? "true" : "false")
         << ",\"source_pixels\":";
  writePixels(stream, row.source);
  stream << ",\"pixels\":";
  writePixels(stream, row.pixels);
  stream << ",\"foreground\":" << std::count(row.pixels.begin(), row.pixels.end(), 255)
         << ",\"captured_options\":";
  writeOptions(stream, row.setting);
  stream << ",\"execution_parameters\":";
  writeOptions(stream, row.effective);
  stream << ",\"options_after\":";
  writeOptions(stream, row.capturedAfter);
  stream << ",\"history_steps\":" << row.historySteps
         << ",\"undo_performed\":" << (row.undoPerformed ? "true" : "false")
         << ",\"undo_restored_source\":" << (row.undoRestoredSource ? "true" : "false")
         << ",\"identity_preserved_history\":" << (row.identity ? "true" : "false")
         << ",\"publication\":{\"document_id\":" << row.documentId
         << ",\"in_place\":" << (row.publishedInPlace ? "true" : "false")
         << ",\"prepared_pad_edges\":" << (row.preparedPad ? "true" : "false")
         << ",\"preview_pixels\":";
  writePixels(stream, row.preview);
  stream << ",\"published_pixels\":";
  writePixels(stream, row.published);
  stream << ",\"restored_pixels\":";
  writePixels(stream, row.restored);
  stream << ",\"undo_steps_after\":" << row.undoStepsAfter
         << ",\"redo_steps_after\":" << row.redoStepsAfter << '}';
  if (row.id == "frozen")
    stream << ",\"captured_scenario\":\"default\"";
  if (row.id == "override" || row.id == "reverse_override")
    stream << ",\"override_value\":" << (row.effective ? "true" : "false");
  if (!row.persistenceFile.empty())
    stream << ",\"persistence_file\":{\"path\":" << quote(row.persistenceFile)
           << ",\"bytes_utf8\":" << quote(row.persistenceBytes)
           << ",\"envelope\":" << row.persistenceBytes
           << ",\"reopened_setting_value\":" << (row.reopenedValue ? "true" : "false") << '}';
  stream << '}';
}

std::string readText(const fs::path &path) {
  std::ifstream stream(path, std::ios::binary);
  check(stream.is_open(), "Cannot read saved Preferences bytes");
  const std::string value((std::istreambuf_iterator<char>(stream)),
                          std::istreambuf_iterator<char>());
  check(!stream.bad(), "Reading saved Preferences bytes failed");
  return value;
}
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 3 && std::string(argv[1]) == "--consumer-dir",
          "Use --consumer-dir <fresh-directory>");
    const auto directory = fs::absolute(fs::u8path(argv[2])).lexically_normal();
    fs::create_directories(directory.parent_path());
    check(fs::create_directory(directory), "Consumer output directory must be fresh");
    const auto source = sourceImage();
    std::vector<Observation> rows;
    Settings defaults;
    const auto defaultOptions = defaults.binary_options();
    check(!defaultOptions.pad_edges, "Native pad-edges default is not false");
    rows.push_back(compute("default", defaults, source, false,
                           defaultOptions, defaultOptions));

    Settings changed;
    changed.set("binary_pad_edges", true);
    const auto changedOptions = changed.binary_options();
    rows.push_back(compute("changed", changed, source, false,
                           changedOptions, changedOptions));

    Settings frozen;
    const auto frozenOptions = frozen.binary_options();
    frozen.set("binary_pad_edges", true);
    check(!frozenOptions.pad_edges, "Global mutation replaced detached false options");
    rows.push_back(compute("frozen", frozen, source, false,
                           frozenOptions, frozenOptions));

    Settings overrides;
    const auto overrideBase = overrides.binary_options();
    auto explicitTrue = overrideBase;
    explicitTrue.pad_edges = true;
    rows.push_back(compute("override", overrides, source, false,
                           overrideBase, explicitTrue));

    const auto preferencesPath = directory / "preferences.json";
    PreferencesDocument preferences;
    preferences.settings.set("binary_pad_edges", true);
    check(preferences.save(preferencesPath.u8string()), "Preferences were not published");
    const auto savedBytes = readText(preferencesPath);
    const auto savedDocument = PreferencesDocument::parse(savedBytes, false);
    const auto reopened = PreferencesDocument::load(preferencesPath.u8string(), false);
    check(savedDocument.settings.values() == preferences.settings.values() &&
              reopened.settings.values() == preferences.settings.values() &&
              reopened.warnings.empty() && !reopened.write_blocked,
          "Saved Preferences bytes or fresh reopen differ from live settings");
    const auto persistedOptions = reopened.settings.binary_options();
    auto persisted = compute("persisted", reopened.settings, source, false,
                              persistedOptions, persistedOptions);
    persisted.persistenceFile = preferencesPath.u8string();
    persisted.persistenceBytes = savedBytes;
    persisted.reopenedValue = reopened.settings.boolean("binary_pad_edges");
    rows[1].persistenceFile = persisted.persistenceFile;
    rows[1].persistenceBytes = savedBytes;
    rows[1].reopenedValue = persisted.reopenedValue;
    rows.push_back(std::move(persisted));

    Settings reverse;
    reverse.set("binary_pad_edges", true);
    const auto reverseBase = reverse.binary_options();
    auto explicitFalse = reverseBase;
    explicitFalse.pad_edges = false;
    const auto reverseRow = compute("reverse_override", reverse, source, false,
                                    reverseBase, explicitFalse);
    const auto holeSource = sourceImage(true);
    const auto holeFalse = compute("nonidentity-false", defaults, holeSource, true,
                                   defaultOptions, defaultOptions);
    const auto holeTrue = compute("nonidentity-true", changed, holeSource, true,
                                  changedOptions, changedOptions);
    check(readText(preferencesPath) == savedBytes,
          "Computation or fresh reopen changed saved Preferences bytes");
    std::ofstream stream(directory / "cpp-api.json", std::ios::binary);
    stream.exceptions(std::ios::badbit | std::ios::failbit);
    stream << "{\n\"schema_version\":1,\"context\":\"cpp_api\","
              "\"requirement_id\":\"G052.binary_erode.pad_edges\",\"shape\":[7,9],\"source\":";
    writePixels(stream, bytes(source));
    stream << ",\"scenarios\":[\n";
    for (std::size_t i = 0; i < rows.size(); ++i) {
      if (i)
        stream << ",\n";
      writeRow(stream, rows[i]);
    }
    stream << "],\"supplementary\":{\"reverse_override\":";
    writeRow(stream, reverseRow);
    stream << ",\"nonidentity_publication\":[";
    writeRow(stream, holeFalse);
    stream << ',';
    writeRow(stream, holeTrue);
    stream << "]}}\n";
    stream.close();
    std::cout << "C++ API binary_pad_edges: five scenarios, both overrides, "
                 "exact independent bytes, native publication/history and undo\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
