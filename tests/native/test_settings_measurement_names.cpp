#include <imagejxx/image.hpp>
#include <imagejxx/preferences.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace imagejxx;
namespace {
namespace fs = std::filesystem;
constexpr const char *names_key = "measurements";
constexpr std::size_t maximum_file_bytes = 64 * 1024;
std::size_t checks = 0;
const std::vector<std::string> particle_names{"min", "max", "bbox", "pixels"};
constexpr std::array<double, 30> source_words{
    0, 0, 0, 0, 0, 0,
    0, 2, 3, 4, 0, 0,
    0, 5, 6, 7, 0, 0,
    0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0};
constexpr std::array<std::int32_t, 30> expected_labels{
    0, 0, 0, 0, 0, 0,
    0, 1, 1, 1, 0, 0,
    0, 1, 1, 1, 0, 0,
    0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0};

void check(bool condition, const std::string &message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
template<class Function>
void refuses(Function function, const std::string &field) {
  try { function(); }
  catch (const std::invalid_argument &error) {
    check(field.empty() || std::string(error.what()).find(field) != std::string::npos,
          "Refusal did not identify the invalid measurement setting");
    return;
  }
  throw std::runtime_error("Invalid measurement names were admitted");
}
std::vector<std::string> distinct_names(std::size_t count) {
  check(count <= 1025, "Test fixture exceeds its bounded name count");
  std::vector<std::string> result;
  result.reserve(count);
  for (std::size_t i = 0; i < count; ++i)
    result.push_back("column_" + std::to_string(i));
  return result;
}
std::string accented_name(std::size_t count) {
  check(count <= 129, "Test fixture exceeds its bounded UTF-8 length");
  std::string result;
  result.reserve(2 * count);
  for (std::size_t i = 0; i < count; ++i) result += "\xc3\xa9";
  return result;
}

void schema_order_limits() {
  Settings settings;
  const auto &definition = Settings::definition(names_key);
  check(Settings::schema_version == 1 && definition.kind == SettingKind::StringList &&
            definition.group == "Measurements" && settings.strings(names_key).empty() &&
            settings.integer("measurement_precision") == -1,
        "Measurement defaults or public schema changed");
  for (const auto *name : {"min", "max", "bbox"})
    check(std::find(definition.choices.begin(), definition.choices.end(), name) == definition.choices.end(),
          "Distinguishing custom name unexpectedly became a preset");
  check(std::find(definition.choices.begin(), definition.choices.end(), "pixels") != definition.choices.end(),
        "The final selected particle name must remain a preset");

  settings.set(names_key, std::vector<std::string>{"min"});
  check(settings.strings(names_key) == std::vector<std::string>{"min"},
        "The first explicit name did not replace the empty-list sentinel");
  settings.set(names_key, particle_names);
  check(settings.strings(names_key) == particle_names, "Explicit names changed order");
  auto detached = settings.strings(names_key);
  detached.erase(detached.begin() + 2);
  check(settings.strings(names_key) == particle_names, "Detached editing changed applied names");
  settings.set(names_key, detached);
  check(settings.strings(names_key) == std::vector<std::string>{"min", "max", "pixels"},
        "Removing a name changed remaining order");

  // Preserve the existing validator exactly: only space, tab, CR and LF are
  // rejected as an all-whitespace name. VT/FF and non-ASCII names are retained.
  const std::vector<std::string> exact{"min", "Min", " min", "min ", "\v", "\f", "\xc3\xa9"};
  settings.set(names_key, exact);
  check(settings.strings(names_key) == exact, "Name bytes were trimmed, folded or normalized");
  const auto boundary = distinct_names(1024);
  settings.set(names_key, boundary);
  check(settings.strings(names_key) == boundary, "The 1024-entry boundary was changed");
  const auto utf8_boundary = accented_name(128);
  check(utf8_boundary.size() == 256, "UTF-8 boundary fixture differs");
  const std::vector<std::string> byte_boundaries{std::string(256, 'a'), utf8_boundary};
  settings.set(names_key, byte_boundaries);
  check(settings.strings(names_key) == byte_boundaries, "The 256-byte boundary was changed");

  settings.update({{"theme", std::string("classic")}, {names_key, particle_names},
                   {"measurement_precision", std::int64_t(3)}});
  settings.reset("Measurements");
  check(settings.strings(names_key).empty() && settings.integer("measurement_precision") == -1 &&
            settings.text("theme") == "classic",
        "Measurements defaults changed an unrelated group or lost the all-columns sentinel");
}

void atomic_invalid_candidates() {
  Settings settings;
  settings.update({{names_key, std::vector<std::string>{"mean"}},
                   {"theme", std::string("light")}, {"measurement_precision", std::int64_t(-1)}});
  const auto before = settings.values();
  const std::vector<SettingValue> invalid{
      std::monostate{}, true, std::int64_t(2), 2., std::string("min"),
      std::vector<std::string>{""}, std::vector<std::string>{" \t\r\n"},
      std::vector<std::string>{"min", "max", "min"},
      std::vector<std::string>{std::string(257, 'x')},
      std::vector<std::string>{accented_name(129)}, distinct_names(1025)};
  for (const auto &candidate : invalid) {
    refuses([&] { settings.update({{names_key, candidate},
                                    {"measurement_precision", std::int64_t(2)},
                                    {"theme", std::string("classic")}}); }, names_key);
    check(settings.values() == before, "Rejected candidate partially published the settings map");
  }
  settings.set(names_key, particle_names);
  check(settings.strings(names_key) == particle_names && settings.text("theme") == "light" &&
            settings.integer("measurement_precision") == -1,
        "Correction changed an unrelated setting or failed to preserve order");
  settings.set(names_key, std::vector<std::string>{});
  check(settings.strings(names_key).empty(), "The explicit all-columns sentinel was rejected");
}

void source_unchanged(const Image &source) {
  check(source.width == 6 && source.height == 5 && source.channels == 1 &&
            source.type == PixelType::UInt8 && source.pixels.size() == source_words.size(),
        "Particle consumer changed the source descriptor");
  check(std::memcmp(source.pixels.data(), source_words.data(), sizeof(source_words)) == 0,
        "Particle consumer changed source storage words");
}
void particle_consumer(const Settings &settings) {
  check(settings.strings(names_key) == particle_names && settings.integer("measurement_precision") == -1,
        "Consumer did not receive the exact ordered measurement-name intent");
  const auto applied = settings.values();
  Image source(6, 5, 1, PixelType::UInt8);
  source.pixels.assign(source_words.begin(), source_words.end());
  const auto labels = connected_components(source, 8);
  check(labels.width == 6 && labels.height == 5 && labels.count == 1 &&
            labels.pixels.size() == expected_labels.size(), "Particle label descriptor differs");
  for (std::size_t i = 0; i < expected_labels.size(); ++i)
    check(labels.pixels[i] == expected_labels[i], "Connected component differs from literal six-cell label");
  const auto label_copy = labels.pixels;
  const auto particles = region_properties(labels, &source, 1., 1.);
  check(particles.size() == 1, "Native particle backend did not return one interior component");
  const auto &particle = particles.front();
  // These are the same public Particle fields selected by the desktop result
  // adapter. No test-side measurement algorithm or table/CSV formatter exists.
  check(particle.minimum == 2. && particle.maximum == 7., "Literal min/max differ");
  check(std::array<int, 4>{particle.x, particle.y, particle.width, particle.height} ==
            std::array<int, 4>{1, 1, 3, 2}, "Literal bbox differs");
  check(particle.pixels == 6 && particle.label == 1 && particle.area == 6. && !particle.touches_border,
        "Literal particle count, unit area or border status differs");
  check(particle.mean == 4.5 && particle.integrated_density == 27., "Literal raw measurements differ");
  check(labels.pixels == label_copy && settings.values() == applied,
        "Native consumption mutated labels or applied name settings");
  source_unchanged(source);
}
void native_particle_consumer() {
  Settings settings;
  settings.set(names_key, particle_names);
  particle_consumer(settings);
}

constexpr const char *opaque_wire = R"({"format":"imagejxx-preferences","version":1,"values":{"measurements":["min","max","bbox","pixels"],"measurement_precision":-1,"cfg3_future":{"word":18446744073709551615,"text":"\u03bb"}},"extras":{"recent":[],"cfg3_extra":[true,null,"\u03bc"],"measurements":"opaque extra"},"cfg3_root":{"signed":-9223372036854775808}})";
void opaque_preserved(const PreferencesDocument &document) {
  check(document.unknown_values_json == "{\"cfg3_future\":{\"text\":\"\xce\xbb\",\"word\":18446744073709551615}}" &&
            document.extras_json == "{\"cfg3_extra\":[true,null,\"\xce\xbc\"],\"measurements\":\"opaque extra\",\"recent\":[]}" &&
            document.unknown_fields_json == R"({"cfg3_root":{"signed":-9223372036854775808}})",
        "Preferences changed an unrelated opaque compartment");
}
void opaque_codec_recovery() {
  auto document = PreferencesDocument::parse(opaque_wire, false);
  opaque_preserved(document);
  check(document.settings.strings(names_key) == particle_names &&
            !document.write_blocked && document.warnings.empty(), "Strict ordered names entered recovery");
  const std::vector<std::string> exact{" min", "MIN", "min ", "\v", "\f", accented_name(128)};
  document.settings.set(names_key, exact);
  const auto canonical = document.json();
  const auto decoded = PreferencesDocument::parse(canonical, false);
  check(decoded.settings.strings(names_key) == exact && decoded.json() == canonical,
        "UTF-8, whitespace or case changed during strict round trip");
  opaque_preserved(decoded);

  constexpr const char *duplicate_wire = R"({"format":"imagejxx-preferences","version":1,"values":{"measurements":["min","min"],"measurement_precision":2},"extras":{"recent":[]}})";
  refuses([&] { (void)PreferencesDocument::parse(duplicate_wire, false); }, names_key);
  const auto recovered = PreferencesDocument::parse(duplicate_wire, true);
  check(recovered.settings.strings(names_key).empty() && recovered.settings.integer("measurement_precision") == 2 &&
            recovered.write_blocked && recovered.warnings.size() == 1 &&
            recovered.warnings.front().find(names_key) != std::string::npos,
        "Recovery did not default only the invalid name list and retain the write guard");
  const auto legacy = PreferencesDocument::parse(R"({"measurements":["min","max","bbox","pixels"],"measurement_precision":-1})", false);
  check(legacy.settings.strings(names_key) == particle_names && legacy.warnings.empty() && !legacy.write_blocked,
        "Legacy preference names lost exact order or entered recovery");
}

std::string file_bytes(const fs::path &path) {
  const auto status = fs::symlink_status(path);
  check(fs::is_regular_file(status) && !fs::is_symlink(status) && fs::file_size(path) <= maximum_file_bytes,
        "Owned measurement preferences must be a bounded regular file");
  std::ifstream stream(path, std::ios::binary);
  check(stream.is_open(), "Cannot read owned measurement preferences");
  const std::string result{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
  check(!stream.bad(), "Reading measurement preferences failed");
  return result;
}
void strict_file_consumer(const fs::path &directory) {
  const auto path = directory / "preferences.json";
  auto document = PreferencesDocument::parse(opaque_wire, false);
  check(document.json().size() <= maximum_file_bytes && document.save(path.u8string()),
        "Saving exact measurement names failed");
  const auto accepted = file_bytes(path);
  const auto loaded = PreferencesDocument::load(path.u8string(), false);
  check(loaded.settings.values() == document.settings.values() && loaded.json() == accepted &&
            loaded.warnings.empty() && !loaded.write_blocked, "Strict file reopen changed settings or encoding");
  opaque_preserved(loaded);
  particle_consumer(loaded.settings);
  check(file_bytes(path) == accepted, "Read-only native particle consumer rewrote preferences");

  const auto applied = document.settings.values();
  refuses([&] { document.settings.update({{names_key, std::vector<std::string>{"min", "min"}},
                                         {"measurement_precision", std::int64_t(2)}}); }, names_key);
  check(document.settings.values() == applied && file_bytes(path) == accepted,
        "Invalid name candidate changed applied settings or the saved file");
  auto invalid = document;
  // Settings retains its historical byte-based string admission. The strict
  // Preferences encoder must reject malformed UTF-8 before filesystem work.
  invalid.settings.set(names_key, std::vector<std::string>{std::string("\xc3\x28", 2)});
  refuses([&] { (void)invalid.save(path.u8string(), true); }, "");
  check(file_bytes(path) == accepted, "Unencodable name replaced accepted preference bytes");
  const auto absent = directory / "invalid-never-created.json";
  refuses([&] { (void)invalid.save(absent.u8string(), true); }, "");
  check(!fs::exists(absent), "Unencodable name created a destination before validation");
  const auto recovered = PreferencesDocument::parse(
      R"({"format":"imagejxx-preferences","version":1,"values":{"measurements":[""]}})", true);
  check(recovered.write_blocked && !recovered.save(path.u8string()) && file_bytes(path) == accepted,
        "Protected measurement recovery overwrote the accepted file implicitly");

  std::size_t files = 0;
  for (const auto &entry : fs::directory_iterator(directory))
    check(++files == 1 && entry.path().filename() == path.filename() &&
              fs::is_regular_file(entry.symlink_status()) && !fs::is_symlink(entry.symlink_status()),
          "Measurement preference save left an unexpected file or temporary");
  check(files == 1, "The retained measurement preference evidence is missing");
  std::cout << "Owned scratch retained for C: " << directory.u8string()
            << ", files=1, bytes=" << accepted.size() << '\n';
}
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 2, "Supply one C-owned FRESH_DIRECTORY for CFG3 file checks");
    const auto directory = fs::absolute(fs::u8path(argv[1])).lexically_normal();
    check(directory != directory.root_path(), "Scratch must not be a filesystem root");
    std::error_code error;
    const auto status = fs::symlink_status(directory, error);
    check(status.type() == fs::file_type::not_found, "CFG3 scratch must be absent, including links");
    const auto parent = fs::symlink_status(directory.parent_path());
    check(fs::is_directory(parent) && !fs::is_symlink(parent), "C must supply an existing regular scratch parent");
    check(fs::create_directory(directory), "Cannot create owned CFG3 scratch");
    const std::pair<const char *, void (*)()> groups[]{
        {"CFG3-N01-schema-order-limits", schema_order_limits},
        {"CFG3-N02-atomic-invalid-candidates", atomic_invalid_candidates},
        {"CFG3-N03-native-particle-consumer", native_particle_consumer},
        {"CFG3-P01-opaque-codec-recovery", opaque_codec_recovery}};
    for (const auto &group : groups) {
      std::cout << "RUN " << group.first << '\n'; group.second();
      std::cout << "PASS " << group.first << '\n';
    }
    std::cout << "RUN CFG3-P02-strict-file-consumer\n";
    strict_file_consumer(directory);
    std::cout << "PASS CFG3-P02-strict-file-consumer\n";
    std::cout << "CFG3 native measurement-name consumer: 5 groups, " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "CFG3 failure; retain owned scratch: " << error.what() << '\n';
    return 1;
  }
}
