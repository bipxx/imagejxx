#include <imagejxx/image.hpp>
#include <imagejxx/preferences.hpp>
#include <imagejxx/settings.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace imagejxx;
namespace {
namespace fs = std::filesystem;
std::size_t checks = 0;
constexpr std::size_t maximum_file_bytes = 64 * 1024;
constexpr std::array<double, 9> source_words{10, 20, 20, 20, 10, 20, 20, 20, 11};
constexpr std::array<unsigned char, 9> default_mask{255, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr std::array<unsigned char, 9> diagonal_mask{255, 0, 0, 0, 255, 0, 0, 0, 0};
constexpr std::array<unsigned char, 9> tolerant_mask{255, 0, 0, 0, 255, 0, 0, 0, 255};

// Independent input and expected opaque compartments. No product flood fill,
// serialization or rasterization generates expected masks or opaque values.
constexpr const char *opaque_wire = R"({"format":"imagejxx-preferences","version":1,"values":{"wand_tolerance":1,"wand_connectivity":8,"cfg1_future":{"array":[true,null,{"text":"keep"}],"wide":18446744073709551615}},"extras":{"recent":["one.tif"],"layout":"[Window][CFG1]\nPos=1,2\n","cfg1_extra":{"keep":true},"wand_connectivity":"opaque extra"},"cfg1_root":{"opaque":[0,false,"keep"],"signed":-9223372036854775808}})";
constexpr const char *expected_unknown_values = R"({"cfg1_future":{"array":[true,null,{"text":"keep"}],"wide":18446744073709551615}})";
constexpr const char *expected_unknown_fields = R"({"cfg1_root":{"opaque":[0,false,"keep"],"signed":-9223372036854775808}})";
constexpr const char *expected_extras = R"({"cfg1_extra":{"keep":true},"layout":"[Window][CFG1]\nPos=1,2\n","recent":["one.tif"],"wand_connectivity":"opaque extra"})";

void check(bool condition, const std::string &message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
template<class F>
void refuses(F function, const std::string &field, const std::string &context) {
  try {
    function();
  } catch (const std::invalid_argument &error) {
    check(field.empty() || std::string(error.what()).find(field) != std::string::npos,
          context + ": refusal did not name the invalid setting");
    return;
  }
  throw std::runtime_error(context + ": expected invalid_argument refusal");
}
Image source_image() {
  Image image(3, 3, 1, PixelType::UInt8);
  image.pixels.assign(source_words.begin(), source_words.end());
  return image;
}
void source_unchanged(const Image &image, const std::string &context) {
  check(image.width == 3 && image.height == 3 && image.channels == 1 &&
            image.type == PixelType::UInt8 && image.pixels.size() == source_words.size(),
        context + ": source descriptor changed");
  check(std::memcmp(image.pixels.data(), source_words.data(),
                    source_words.size() * sizeof(double)) == 0,
        context + ": source words changed");
}
void mask_is(const Image &image, const std::array<unsigned char, 9> &expected,
             const std::string &context) {
  check(image.width == 3 && image.height == 3 && image.channels == 1 &&
            image.type == PixelType::UInt8 && image.pixels.size() == expected.size(),
        context + ": mask descriptor differs");
  for (std::size_t i = 0; i < expected.size(); ++i)
    check(image.pixels[i] == static_cast<double>(expected[i]),
          context + ": literal mask mismatch at index " + std::to_string(i));
}
Image consume(const Image &image, const Settings &settings) {
  return wand_mask(image, 0, 0, settings.number("wand_tolerance"),
                   static_cast<int>(settings.integer("wand_connectivity")));
}
void opaque_is_preserved(const PreferencesDocument &document, const std::string &context) {
  check(document.unknown_values_json == expected_unknown_values,
        context + ": unknown values changed");
  check(document.unknown_fields_json == expected_unknown_fields,
        context + ": unknown root fields changed");
  check(document.extras_json == expected_extras, context + ": extras changed");
}
void pair_is(const Settings &settings, double tolerance, std::int64_t connectivity,
             const std::string &context) {
  check(settings.number("wand_tolerance") == tolerance &&
            settings.integer("wand_connectivity") == connectivity,
        context + ": wrong setting pair");
}
std::string read_file(const fs::path &path) {
  const auto status = fs::symlink_status(path);
  check(fs::is_regular_file(status) && !fs::is_symlink(status),
        "Expected owned regular preferences file");
  check(fs::file_size(path) <= maximum_file_bytes, "Preferences fixture exceeded byte bound");
  std::ifstream stream(path, std::ios::binary);
  check(stream.is_open(), "Cannot read preferences fixture");
  const std::string bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
  check(!stream.bad(), "Preferences fixture read failed");
  return bytes;
}
void write_fixture(const fs::path &path, const std::string &bytes) {
  check(bytes.size() <= maximum_file_bytes && !fs::exists(path),
        "Fixture write must be bounded and new");
  std::ofstream stream(path, std::ios::binary | std::ios::out);
  check(stream.is_open(), "Cannot create preferences fixture");
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  stream.close();
  check(!stream.fail(), "Preferences fixture write failed");
}
void no_temporary(const fs::path &directory) {
  std::size_t entries = 0;
  for (const auto &entry : fs::directory_iterator(directory)) {
    check(++entries <= 16, "Owned scratch contains too many entries");
    const auto status = entry.symlink_status();
    check(fs::is_regular_file(status) && !fs::is_symlink(status),
          "Unexpected directory or link in owned scratch");
    check(entry.path().filename().u8string().find(".tmp.") == std::string::npos,
          "Preferences publication left a temporary sibling");
  }
}

void defaults_and_types() {
  Settings settings;
  check(Settings::schema_version == 1, "CFG1 unexpectedly changes settings version");
  pair_is(settings, 0, 4, "Defaults");
  check(std::holds_alternative<double>(settings.at("wand_tolerance")) &&
            std::holds_alternative<std::int64_t>(settings.at("wand_connectivity")),
        "Defaults lost their declared number/integer types");
  check(Settings::definition("wand_tolerance").kind == SettingKind::Number &&
            Settings::definition("wand_connectivity").kind == SettingKind::Integer,
        "Wand definitions changed kind");

  settings.set("wand_tolerance", std::int64_t(1));
  check(std::holds_alternative<double>(settings.at("wand_tolerance")) &&
            settings.number("wand_tolerance") == 1,
        "Integral tolerance was not normalized to the supported number type");
  settings.set("wand_tolerance", std::numeric_limits<double>::max());
  pair_is(settings, std::numeric_limits<double>::max(), 4, "Finite upper bound");
  settings.update({{"theme", std::string("light")}, {"wand_tolerance", 1.},
                   {"wand_connectivity", std::int64_t(8)}});
  settings.reset("Selection");
  pair_is(settings, 0, 4, "Selection Defaults");
  check(settings.text("theme") == "light", "Selection Defaults reset another group");
}

void atomic_invalid_pair() {
  Settings settings;
  settings.set("theme", std::string("light"));
  const auto before = settings.values();
  const std::vector<SettingValue> invalid_connectivity{
      std::int64_t(6), std::int64_t(5), std::int64_t(7), std::int64_t(-1),
      std::int64_t(0), std::int64_t(9), true, 8., std::string("8"), std::monostate{}};
  for (const auto &value : invalid_connectivity) {
    refuses([&] {
      settings.update({{"theme", std::string("classic")},
                       {"wand_tolerance", 1.}, {"wand_connectivity", value}});
    }, "wand_connectivity", "Invalid connectivity pair");
    check(settings.values() == before, "Connectivity refusal partially changed the settings map");
  }
  const std::vector<SettingValue> invalid_tolerance{
      -1., std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
      true, std::string("1"), std::monostate{}};
  for (const auto &value : invalid_tolerance) {
    refuses([&] {
      settings.update({{"theme", std::string("classic")},
                       {"wand_connectivity", std::int64_t(8)}, {"wand_tolerance", value}});
    }, "wand_tolerance", "Invalid tolerance pair");
    check(settings.values() == before, "Tolerance refusal partially changed the settings map");
  }
  settings.update({{"wand_tolerance", 1.}, {"wand_connectivity", std::int64_t(8)}});
  pair_is(settings, 1, 8, "Correction after refused pair");
  check(settings.text("theme") == "light", "Correction changed an unrelated preference");
}

void four_literal_masks() {
  const auto image = source_image();
  Settings settings;
  struct Case {
    double tolerance;
    std::int64_t connectivity;
    const std::array<unsigned char, 9> *mask;
    const char *name;
  };
  const std::array<Case, 4> cases{{
      {0, 4, &default_mask, "tolerance0-connectivity4"},
      {0, 8, &diagonal_mask, "tolerance0-connectivity8"},
      {1, 8, &tolerant_mask, "tolerance1-connectivity8"},
      {1, 4, &default_mask, "tolerance1-connectivity4"}}};
  for (const auto &row : cases) {
    settings.update({{"wand_tolerance", row.tolerance}, {"wand_connectivity", row.connectivity}});
    const auto before = settings.values();
    mask_is(consume(image, settings), *row.mask, row.name);
    check(settings.values() == before, std::string(row.name) + ": consumer changed settings");
    source_unchanged(image, row.name);
  }
}

void explicit_arguments_and_native_refusals() {
  const auto image = source_image();
  Settings settings;
  const auto before = settings.values();
  mask_is(wand_mask(image, 0, 0, 1, 8), tolerant_mask, "Explicit native arguments");
  check(settings.values() == before, "Explicit arguments changed detached settings");
  mask_is(consume(image, settings), default_mask, "Detached defaults after explicit arguments");
  mask_is(wand_mask(image, 0, 0), default_mask, "Native default arguments");
  for (const double tolerance : {-1., std::numeric_limits<double>::quiet_NaN(),
                                 std::numeric_limits<double>::infinity(),
                                 -std::numeric_limits<double>::infinity()}) {
    refuses([&] { (void)wand_mask(image, 0, 0, tolerance, 8); }, "", "Invalid native tolerance");
    source_unchanged(image, "Invalid native tolerance");
  }
  refuses([&] { (void)wand_mask(image, 0, 0, 1, 6); }, "", "Native connectivity6");
  source_unchanged(image, "Explicit/native refusal calls");
  check(settings.values() == before, "Native refusal changed detached settings");
}

void unknown_compartment_codec() {
  const auto document = PreferencesDocument::parse(opaque_wire, false);
  pair_is(document.settings, 1, 8, "Opaque fixture");
  opaque_is_preserved(document, "Opaque fixture");
  check(document.warnings.empty() && !document.write_blocked, "Valid fixture unexpectedly recovered");
  const auto encoded = document.json();
  check(encoded.size() <= maximum_file_bytes &&
            encoded.find("\"format\": \"imagejxx-preferences\"") != std::string::npos &&
            encoded.find("\"version\": 1") != std::string::npos,
        "Preferences envelope/version changed");
  const auto reopened = PreferencesDocument::parse(encoded, false);
  pair_is(reopened.settings, 1, 8, "Strict codec reopen");
  opaque_is_preserved(reopened, "Strict codec reopen");
  check(reopened.settings.values() == document.settings.values() && reopened.json() == encoded,
        "Codec roundtrip changed known settings or stable serialized bytes");

  auto shadowed = document;
  shadowed.unknown_values_json = R"({"cfg1_future":{"array":[true,null,{"text":"keep"}],"wide":18446744073709551615},"wand_connectivity":4,"wand_tolerance":0})";
  const auto unknown_before = shadowed.unknown_values_json;
  const auto effective = PreferencesDocument::parse(shadowed.json(), false);
  pair_is(effective.settings, 1, 8, "Known values override unknown-compartment collisions");
  opaque_is_preserved(effective, "Known values override unknown-compartment collisions");
  check(shadowed.unknown_values_json == unknown_before,
        "Const serialization modified the caller's unknown compartment");
  const auto image = source_image();
  mask_is(consume(image, effective.settings), tolerant_mask, "Reopened preference consumer");
  source_unchanged(image, "Reopened preference consumer");
}

void file_roundtrip_and_refused_save(const fs::path &directory) {
  const auto path = directory / "prefs.json";
  auto document = PreferencesDocument::parse(opaque_wire, false);
  document.settings.update({{"wand_tolerance", 0.}, {"wand_connectivity", std::int64_t(4)}});
  check(document.json().size() <= maximum_file_bytes, "Initial save exceeds fixture bound");
  check(document.save(path.u8string()), "Initial preference save did not publish");
  const auto first_bytes = read_file(path);
  pair_is(PreferencesDocument::load(path.u8string(), false).settings, 0, 4, "First file reopen");

  document.settings.update({{"wand_tolerance", 1.}, {"wand_connectivity", std::int64_t(8)}});
  check(document.save(path.u8string()), "Changed preference save did not publish");
  const auto accepted_bytes = read_file(path);
  check(accepted_bytes != first_bytes, "Changed pair did not change file bytes");
  const auto reopened = PreferencesDocument::load(path.u8string(), false);
  pair_is(reopened.settings, 1, 8, "Persisted pair");
  opaque_is_preserved(reopened, "Persisted pair");
  check(reopened.settings.values() == document.settings.values() && reopened.json() == accepted_bytes,
        "Strict file reopen differs from the published settings");
  const auto image = source_image();
  mask_is(consume(image, reopened.settings), tolerant_mask, "Persisted pair consumer");
  source_unchanged(image, "Persisted pair consumer");

  const auto accepted_map = document.settings.values();
  refuses([&] {
    document.settings.update({{"wand_tolerance", 0.}, {"wand_connectivity", std::int64_t(6)}});
  }, "wand_connectivity", "Rejected persisted pair");
  check(document.settings.values() == accepted_map && read_file(path) == accepted_bytes,
        "Rejected pair changed settings or persisted bytes");

  auto invalid = document;
  invalid.extras_json = "[]";
  refuses([&] { (void)invalid.save(path.u8string(), true); }, "", "Invalid save over accepted file");
  check(read_file(path) == accepted_bytes, "Invalid save changed accepted bytes");
  const auto absent = directory / "invalid-never-created.json";
  refuses([&] { (void)invalid.save(absent.u8string(), true); }, "", "Invalid save to absent file");
  check(!fs::exists(absent), "Invalid save created a destination before validation");
  no_temporary(directory);
}

void protected_wand_recovery(const fs::path &directory) {
  const auto path = directory / "recovery.json";
  const auto backup = directory / "recovery.json.bak";
  const auto next_backup = directory / "recovery.json.bak.1";
  std::string invalid_wire = opaque_wire;
  const std::string valid_pair_member = "\"wand_connectivity\":8";
  const auto at = invalid_wire.find(valid_pair_member);
  check(at != std::string::npos && invalid_wire.find(valid_pair_member, at + 1) == std::string::npos,
        "Recovery fixture member is not unique");
  invalid_wire.replace(at, valid_pair_member.size(), "\"wand_connectivity\":6");
  write_fixture(path, invalid_wire);
  const std::string earlier_backup = "CFG1 pre-existing backup must survive\n";
  write_fixture(backup, earlier_backup);
  refuses([&] { (void)PreferencesDocument::load(path.u8string(), false); },
          "wand_connectivity", "Strict invalid-file load");
  const auto recovered = PreferencesDocument::load(path.u8string(), true);
  pair_is(recovered.settings, 1, 4, "Recovery preserves valid tolerance and defaults connectivity");
  check(recovered.write_blocked && recovered.warnings.size() == 1 &&
            recovered.warnings.front().find("wand_connectivity") != std::string::npos,
        "Recovery lost its protection or field-specific warning");
  opaque_is_preserved(recovered, "Recovered opaque compartments");
  check(!recovered.save(path.u8string()), "Protected file was overwritten implicitly");
  check(read_file(path) == invalid_wire && read_file(backup) == earlier_backup &&
            !fs::exists(next_backup),
        "Blocked save changed the original or backup files");
  check(recovered.json().size() <= maximum_file_bytes, "Recovered save exceeds fixture bound");
  check(recovered.save(path.u8string(), true), "Explicit recovery save did not publish");
  check(read_file(backup) == earlier_backup && read_file(next_backup) == invalid_wire,
        "Recovery overwrote an earlier backup or lost exact original bytes");
  const auto reopened = PreferencesDocument::load(path.u8string(), false);
  pair_is(reopened.settings, 1, 4, "Explicit recovered file");
  opaque_is_preserved(reopened, "Explicit recovered file");
  check(!reopened.write_blocked && reopened.warnings.empty() && recovered.write_blocked,
        "Reopen/const recovery flags changed unexpectedly");
  const auto image = source_image();
  mask_is(consume(image, reopened.settings), default_mask, "Recovered four-connected consumer");
  source_unchanged(image, "Recovered four-connected consumer");
  no_temporary(directory);
}

void report_scratch(const fs::path &directory) {
  const std::set<std::string> expected{
      "prefs.json", "recovery.json", "recovery.json.bak", "recovery.json.bak.1"};
  std::set<std::string> actual;
  std::uintmax_t bytes = 0;
  for (const auto &entry : fs::directory_iterator(directory)) {
    check(actual.size() < expected.size(), "Unexpected extra scratch file");
    check(expected.count(entry.path().filename().u8string()) == 1, "Unexpected scratch file name");
    check(fs::is_regular_file(entry.symlink_status()), "Scratch file changed type");
    actual.insert(entry.path().filename().u8string());
    const auto size = fs::file_size(entry.path());
    check(size <= maximum_file_bytes, "Scratch file exceeds bounded fixture size");
    bytes += size;
  }
  check(actual == expected, "Expected preference/backup evidence is missing");
  std::cout << "Owned scratch retained for C: " << directory.u8string()
            << ", files=" << actual.size() << ", bytes=" << bytes << '\n';
}
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 2, "Supply one C-owned FRESH_DIRECTORY for CFG1 file checks");
    const auto directory = fs::absolute(fs::u8path(argv[1])).lexically_normal();
    check(directory != directory.root_path(), "Scratch must not be a filesystem root");
    std::error_code error;
    const auto status = fs::symlink_status(directory, error);
    check(status.type() == fs::file_type::not_found, "CFG1 scratch must be absent, including links");
    const auto parent = fs::symlink_status(directory.parent_path());
    check(fs::is_directory(parent) && !fs::is_symlink(parent),
          "C must supply an existing regular scratch parent");
    check(fs::create_directory(directory), "Could not create owned CFG1 scratch");

    const std::pair<const char *, void (*)()> pure_groups[] = {
        {"CFG1-N01-defaults-types", defaults_and_types},
        {"CFG1-N01-atomic-pair", atomic_invalid_pair},
        {"CFG1-N02-four-literal-masks", four_literal_masks},
        {"CFG1-N02-explicit-native-isolation", explicit_arguments_and_native_refusals},
        {"CFG1-P01-opaque-codec", unknown_compartment_codec}};
    for (const auto &group : pure_groups) {
      std::cout << "RUN " << group.first << '\n';
      group.second();
      std::cout << "PASS " << group.first << '\n';
    }
    std::cout << "RUN CFG1-P01-file-atomicity\n";
    file_roundtrip_and_refused_save(directory);
    std::cout << "PASS CFG1-P01-file-atomicity\n";
    std::cout << "RUN CFG1-P01-protected-recovery\n";
    protected_wand_recovery(directory);
    std::cout << "PASS CFG1-P01-protected-recovery\n";
    report_scratch(directory);
    std::cout << "CFG1 native settings/wand consumer: 7 groups, " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "CFG1 failure; retain owned scratch: " << error.what() << '\n';
    return 1;
  }
}
