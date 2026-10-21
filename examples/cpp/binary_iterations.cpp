#include <imagejxx/binary.hpp>
#include <imagejxx/preferences.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace {
namespace fs = std::filesystem;
using namespace imagejxx;

// Exact raw UInt8 fixture outputs, binary-imagej154r.json cases 7/103/199.
// Fixture SHA256: f5e6178a452a8b277577ff1c166c35be043e997553eb5d09f523c9111f132ddd.
const std::array<std::array<unsigned char, 63>, 3> expected{{
    {{0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 255, 255, 255, 255, 255, 255, 255, 0,
      0, 255, 255, 255, 255, 255, 255, 255, 0,
      0, 255, 255, 255, 255, 255, 255, 255, 0,
      0, 255, 255, 255, 255, 255, 255, 255, 0,
      0, 255, 255, 255, 255, 255, 255, 255, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {{0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 255, 255, 255, 255, 255, 0, 0,
      0, 0, 255, 255, 255, 255, 255, 0, 0,
      0, 0, 255, 255, 255, 255, 255, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {{0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 255, 255, 255, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0}},
}};

void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

std::array<int, 4> optionValues(const BinaryMorphologyOptions &options) {
  return {{options.iterations, options.neighbors, int(options.black_background),
           int(options.pad_edges)}};
}

std::string quote(const std::string &text) {
  const char *hex = "0123456789abcdef";
  std::string result = "\"";
  for (const auto byte : text) {
    const auto c = static_cast<unsigned char>(byte);
    if (c == '"' || c == '\\') {
      result += '\\';
      result += char(c);
    } else if (c < 32) {
      result += "\\u00";
      result += hex[c >> 4];
      result += hex[c & 15];
    } else {
      result += char(c);
    }
  }
  return result + '"';
}

struct Observation {
  std::string id;
  int setting = 0, current = 0, effective = 0;
  int overrideValue = 0;
  bool sourcePreserved = false, optionsPreserved = false;
  std::vector<int> pixels;
  std::string persistenceFile, persistenceBytes;
  int reopenedValue = 0;
};

Observation compute(const std::string &id, const Settings &settings,
                    const Image &source,
                    const BinaryMorphologyOptions &captured,
                    const BinaryMorphologyOptions &execution,
                    int overrideValue = 0) {
  const auto original = source.pixels;
  const auto capturedBefore = optionValues(captured);
  const auto executionBefore = optionValues(execution);
  const auto output = binary_morphology(source, "erode", execution);
  check(output.width == 9 && output.height == 7 && output.channels == 1 &&
            output.type == PixelType::UInt8 && output.pixels.size() == 63,
        "Consumer changed output shape/type");
  Observation row;
  row.id = id;
  row.setting = captured.iterations;
  row.current = static_cast<int>(settings.integer("binary_iterations"));
  row.effective = execution.iterations;
  row.overrideValue = overrideValue;
  row.sourcePreserved = source.pixels == original;
  row.optionsPreserved = optionValues(captured) == capturedBefore &&
                         optionValues(execution) == executionBefore;
  check(row.sourcePreserved && row.optionsPreserved,
        "Consumer mutated its source or supplied options");
  for (std::size_t index = 0; index < output.pixels.size(); ++index) {
    check(output.pixels[index] == expected.at(row.effective - 1)[index],
          "Consumer differs from exact pinned UInt8 pixels");
    row.pixels.push_back(static_cast<int>(output.pixels[index]));
  }
  return row;
}

void writeReport(const fs::path &path,
                 const std::vector<Observation> &observations) {
  std::ofstream stream(path, std::ios::binary);
  stream.exceptions(std::ios::badbit | std::ios::failbit);
  stream << "{\n\"schema_version\":1,\"context\":\"cpp_api\","
            "\"requirement_id\":\"G052.binary_erode.iterations\",\"scenarios\":[\n";
  for (std::size_t index = 0; index < observations.size(); ++index) {
    const auto &row = observations[index];
    if (index)
      stream << ",\n";
    stream << "{\"id\":" << quote(row.id)
           << ",\"setting_value\":" << row.setting
           << ",\"current_setting_value\":" << row.current
           << ",\"effective_parameter_value\":" << row.effective
           << ",\"source_preserved\":" << (row.sourcePreserved ? "true" : "false")
           << ",\"options_preserved\":" << (row.optionsPreserved ? "true" : "false")
           << ",\"pixels\":[";
    for (std::size_t pixel = 0; pixel < row.pixels.size(); ++pixel) {
      if (pixel)
        stream << ',';
      stream << row.pixels[pixel];
    }
    stream << ']';
    if (row.id == "frozen")
      stream << ",\"captured_scenario\":\"changed\"";
    if (row.overrideValue)
      stream << ",\"override_value\":" << row.overrideValue;
    if (!row.persistenceFile.empty())
      stream << ",\"persistence_file\":{\"path\":" << quote(row.persistenceFile)
             << ",\"bytes_utf8\":" << quote(row.persistenceBytes)
             << ",\"envelope\":" << row.persistenceBytes
             << ",\"reopened_setting_value\":" << row.reopenedValue << '}';
    stream << '}';
  }
  stream << "\n]}\n";
  stream.close();
}
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 3 && std::string(argv[1]) == "--consumer-dir",
          "Use --consumer-dir <fresh-directory>");
    const auto directory = fs::absolute(fs::u8path(argv[2])).lexically_normal();
    fs::create_directories(directory.parent_path());
    check(fs::create_directory(directory), "Consumer output directory must be fresh");
    const auto source = fill(Image(9, 7, 1, PixelType::UInt8), 255);
    std::vector<Observation> rows;
    Settings settings;
    auto captured = settings.binary_options();
    rows.push_back(compute("default", settings, source, captured, captured));
    settings.set("binary_iterations", std::int64_t(2));
    captured = settings.binary_options();
    rows.push_back(compute("changed", settings, source, captured, captured));
    settings.set("binary_iterations", std::int64_t(3));
    rows.push_back(compute("frozen", settings, source, captured, captured));
    settings.set("binary_iterations", std::int64_t(2));
    captured = settings.binary_options();
    settings.set("binary_iterations", std::int64_t(3));
    auto explicitOptions = captured;
    explicitOptions.iterations = 1;
    rows.push_back(compute("override", settings, source, captured,
                           explicitOptions, explicitOptions.iterations));
    const auto persistence = directory / "preferences.json";
    PreferencesDocument preferences;
    preferences.settings.set("binary_iterations", std::int64_t(2));
    check(preferences.save(persistence.u8string()), "Preferences were not published");
    const auto reopened = PreferencesDocument::load(persistence.u8string(), false);
    captured = reopened.settings.binary_options();
    auto persisted = compute("persisted", reopened.settings, source, captured, captured);
    persisted.persistenceFile = persistence.u8string();
    std::ifstream saved(persistence, std::ios::binary);
    saved.exceptions(std::ios::badbit | std::ios::failbit);
    persisted.persistenceBytes = std::string(std::istreambuf_iterator<char>(saved),
                                             std::istreambuf_iterator<char>());
    persisted.reopenedValue =
        static_cast<int>(reopened.settings.integer("binary_iterations"));
    check(PreferencesDocument::parse(persisted.persistenceBytes, false)
                  .settings.integer("binary_iterations") == 2,
          "Retained Preferences bytes differ from the reopened setting");
    rows.push_back(std::move(persisted));
    writeReport(directory / "cpp-api.json", rows);
    std::cout << "C++ API binary_iterations: default1 changed2 frozen2/current3 "
                 "override1/capture2/current3 persisted2; exact pinned pixels; "
                 "source/options preserved\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
