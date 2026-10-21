#include <imagejxx/preferences.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace imagejxx;
namespace {
namespace fs = std::filesystem;
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class F> void rejects(F function) {
  try {
    function();
  } catch (const std::invalid_argument &) {
    return;
  }
  throw std::runtime_error("Invalid preferences were accepted");
}
template <class F> void representationRejects(F function) {
  try {
    function();
  } catch (const PreferencesRepresentationError &) {
    return;
  }
  throw std::runtime_error("Overflowing integer lost its representation error");
}
std::string read(const fs::path &path) {
  std::ifstream stream(path, std::ios::binary);
  check(stream.is_open(), "Cannot open retained preferences test file");
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}
void write(const fs::path &path, const std::string &text) {
  std::ofstream stream(path, std::ios::binary);
  stream << text;
  stream.close();
  check(!stream.fail(), "Cannot write preferences test fixture");
}
void noTemporary(const fs::path &directory, const fs::path &destination) {
  const auto prefix = destination.filename().u8string() + ".tmp.";
  for (const auto &entry : fs::directory_iterator(directory))
    check(entry.path().filename().u8string().rfind(prefix, 0) != 0,
          "Failed publication left its owned temporary file");
}

void codecChecks() {
  PreferencesDocument defaults;
  const auto canonical = defaults.json();
  const auto reopened = PreferencesDocument::parse(canonical, false);
  check(reopened.settings.values() == defaults.settings.values() &&
            reopened.extras_json == R"({"recent":[]})" &&
            reopened.warnings.empty() && !reopened.write_blocked &&
            reopened.json() == canonical,
        "Default versioned document must round trip exactly");

  const auto legacy = PreferencesDocument::parse(
      R"({"foreground":"red","background":"#03A","brush_width":14,"recent":["old.tif"],"future":{"nested":[true,null,9]}})",
      false);
  check(legacy.settings.text("foreground") == "#ff0000" &&
            legacy.settings.text("background") == "#0033aa" &&
            legacy.settings.number("brush_width") == 14 &&
            legacy.settings.number("point_size") == 14 &&
            legacy.extras_json.find("future") != std::string::npos &&
            legacy.unknown_values_json == "{}" && !legacy.write_blocked,
        "Legacy colors, brush/point migration and extras must be retained");
  const auto legacyPoint = PreferencesDocument::parse(
      R"({"brush_width":14,"point_size":5})", false);
  check(legacyPoint.settings.number("point_size") == 5,
        "Legacy migration preserves an explicitly configured point size");

  const auto original =
      R"({"format":"imagejxx-preferences","version":1,"values":{"binary_iterations":2,"future":[true,null,{"x":9}]},"extras":{"recent":["\u914d\u7f6e.tif"],"layout":"dock\nlayout","theme":"opaque shadow"},"future_field":{"object":{"array":[1,2,3]}}})";
  const auto opaque = PreferencesDocument::parse(original, false);
  check(opaque.settings.integer("binary_iterations") == 2 &&
            opaque.settings.text("theme") == "dark" &&
            opaque.extras_json.find("opaque shadow") != std::string::npos &&
            opaque.unknown_values_json.find("future") != std::string::npos &&
            opaque.unknown_fields_json.find("future_field") != std::string::npos,
        "All opaque compartments, including shadowed known extras, survive");
  const auto opaqueAgain = PreferencesDocument::parse(opaque.json(), false);
  check(opaqueAgain.extras_json == opaque.extras_json &&
            opaqueAgain.unknown_values_json == opaque.unknown_values_json &&
            opaqueAgain.unknown_fields_json == opaque.unknown_fields_json,
        "Opaque compartment values must round trip exactly");

  auto deep = PreferencesDocument{};
  deep.unknown_fields_json = "{\"deep\":";
  for (int i = 0; i < 300; ++i)
    deep.unknown_fields_json += "{\"child\":";
  deep.unknown_fields_json += "null" + std::string(301, '}');
  check(PreferencesDocument::parse(deep.json(), false).unknown_fields_json ==
            deep.unknown_fields_json,
        "Opaque trees do not inherit MetadataObject depth limits");

  auto integers = PreferencesDocument{};
  integers.unknown_values_json =
      R"({"minimum":-9223372036854775808,"signed_maximum":9223372036854775807,"unsigned_maximum":18446744073709551615})";
  const auto exact = PreferencesDocument::parse(integers.json(), false);
  check(exact.unknown_values_json == integers.unknown_values_json &&
            exact.json().find("18446744073709551615") != std::string::npos &&
            exact.json().find("-9223372036854775808") != std::string::npos,
        "Signed and unsigned 64-bit opaque integer boundaries stay exact");
  for (const auto &number :
       {std::string("18446744073709551616"),
        std::string("-9223372036854775809"), "1" + std::string(500, '0')}) {
    representationRejects([&] {
      PreferencesDocument::parse("{\"future\":" + number + "}", true);
    });
    representationRejects([&] {
      auto invalid = PreferencesDocument{};
      invalid.extras_json = "{\"future\":" + number + "}";
      invalid.json();
    });
    representationRejects([&] {
      auto invalid = PreferencesDocument{};
      invalid.unknown_values_json = "{\"future\":" + number + "}";
      invalid.json();
    });
    representationRejects([&] {
      auto invalid = PreferencesDocument{};
      invalid.unknown_fields_json = "{\"future\":" + number + "}";
      invalid.json();
    });
  }
  const auto tooLargeKnown = PreferencesDocument::parse(
      R"({"history_limit":18446744073709551615})", true);
  check(tooLargeKnown.settings.integer("history_limit") == 8 &&
            tooLargeKnown.warnings.size() == 1 && tooLargeKnown.write_blocked,
        "Exact uint64 values outside a known integer type recover defaults");
  rejects([] { PreferencesDocument::parse(R"({"future":1e500})"); });
  rejects([] { PreferencesDocument::parse(R"({"future":NaN})"); });
  rejects([] { PreferencesDocument::parse(R"({"future":"\ud800"})"); });
  rejects([] { PreferencesDocument::parse(R"({"future":"\udc00"})"); });
  rejects([] {
    PreferencesDocument::parse(std::string("{\"future\":\"") +
                               char(0xff) + "\"}");
  });
  const auto unicode = PreferencesDocument::parse(
      R"({"future":"\ud83d\ude00"})", false);
  check(PreferencesDocument::parse(unicode.json(), false).extras_json ==
            unicode.extras_json,
        "Valid Unicode surrogate pairs retain their UTF-8 value");

  const auto recovered = PreferencesDocument::parse(
      R"({"format":"imagejxx-preferences","version":1,"values":{"binary_iterations":true,"ui_scale":2},"extras":{"recent":[4],"layout":false}})");
  check(recovered.settings.integer("binary_iterations") == 1 &&
            recovered.settings.number("ui_scale") == 2 &&
            recovered.extras_json == R"({"recent":[]})" &&
            recovered.warnings.size() == 3 && recovered.write_blocked,
        "Recovery retains valid settings and reports all invalid fields");
  rejects([&] {
    PreferencesDocument::parse(
        R"({"format":"imagejxx-preferences","version":1,"values":{"binary_iterations":true}})",
        false);
  });
  for (const auto &text :
       {"[]", "{", R"({"format":"other","version":1,"values":{}})",
        R"({"format":"imagejxx-preferences","version":1.0,"values":{}})",
        R"({"format":"imagejxx-preferences","version":2,"values":{}})",
        R"({"format":"imagejxx-preferences","version":1,"values":[]})",
        R"({"format":"imagejxx-preferences","version":1,"values":{},"extras":[]})"})
    rejects([&] { PreferencesDocument::parse(text, true); });
  for (const auto &extras :
       {"[]", R"({"recent":false})", R"({"layout":7})"})
    rejects([&] {
      PreferencesDocument invalid;
      invalid.extras_json = extras;
      invalid.json();
    });
  rejects([] {
    PreferencesDocument invalid;
    invalid.unknown_values_json = "[]";
    invalid.json();
  });
  rejects([] {
    PreferencesDocument invalid;
    invalid.unknown_fields_json = "null";
    invalid.json();
  });
  rejects([] {
    PreferencesDocument::parse(
        R"({"format":"imagejxx-preferences","version":1,"values":{"foreground":"red"}})",
        false);
  });
}

void fileChecks(const fs::path &directory) {
  check(!fs::exists(directory), "Native preferences report directory must be fresh");
  fs::create_directories(directory);
  const auto path = directory / fs::u8path(u8"\u914d\u7f6e-\u00e9.json");
  const auto missing = PreferencesDocument::load(path.u8string());
  check(!missing.write_blocked && missing.warnings.empty(),
        "Missing file recovers to writable defaults");
  rejects([&] { PreferencesDocument::load(path.u8string(), false); });
  PreferencesDocument document;
  document.settings.set("binary_iterations", std::int64_t(2));
  check(document.save(path.u8string()), "First save must publish");
  const auto first = read(path);
  const auto reopened = PreferencesDocument::load(path.u8string(), false);
  check(reopened.settings.integer("binary_iterations") == 2 &&
            read(path) == reopened.json(),
        "Unicode-path saved file is the exact strict reopened document");
  document.settings.set("binary_iterations", std::int64_t(3));
  check(document.save(path.u8string()) && read(path) != first &&
            PreferencesDocument::load(path.u8string(), false)
                    .settings.integer("binary_iterations") == 3,
        "Atomic publication replaces an existing destination");
  noTemporary(directory, path);

  write(path, "{malformed");
  const auto recovered = PreferencesDocument::load(path.u8string());
  check(recovered.write_blocked && recovered.warnings.size() == 1 &&
            recovered.settings.integer("binary_iterations") == 1,
        "Malformed input load retains defaults with a blocked write");
  rejects([&] { PreferencesDocument::load(path.u8string(), false); });
  const auto old = read(path);
  check(!recovered.save(path.u8string()) && read(path) == old,
        "Blocked automatic save must leave original bytes untouched");
  auto backup = path;
  backup += ".bak";
  check(recovered.save(path.u8string(), true) && read(backup) == old &&
            recovered.write_blocked && recovered.warnings.size() == 1,
        "Explicit save preserves malformed original and const recovery flags");
  auto backupAgain = path;
  backupAgain += ".bak.1";
  const auto second = read(path);
  check(recovered.save(path.u8string(), true) && read(backupAgain) == second &&
            read(backup) == old,
        "Repeated explicit recovery saves select .bak.1 without overwriting");

  const auto invalidParent = directory / "must-not-create";
  for (int compartment = 0; compartment < 4; ++compartment) {
    PreferencesDocument invalid;
    if (compartment == 0)
      invalid.extras_json = R"({"recent":[3]})";
    else if (compartment == 1)
      invalid.extras_json = R"({"layout":false})";
    else if (compartment == 2)
      invalid.unknown_values_json = "[]";
    else
      invalid.unknown_fields_json = "[1]";
    rejects([&] {
      invalid.save((invalidParent / "preferences.json").u8string(), true);
    });
    check(!fs::exists(invalidParent),
          "Invalid compartments must not create parent directories");
  }
  write(path, R"({"future":18446744073709551616})");
  representationRejects([&] { PreferencesDocument::load(path.u8string(), false); });
  const auto overflowRecovered = PreferencesDocument::load(path.u8string());
  check(overflowRecovered.write_blocked && overflowRecovered.warnings.size() == 1,
        "Recovery load catches opaque integer representation failures");
  const auto directoryRecovered = PreferencesDocument::load(directory.u8string());
  check(directoryRecovered.write_blocked && !directoryRecovered.warnings.empty(),
        "Recovery load catches filesystem read failures");

  const auto blocked = directory / "blocked.json";
  write(blocked, "original bytes must survive replacement failure");
  const auto retained = read(blocked);
#ifdef _WIN32
  // Denying FILE_SHARE_DELETE deterministically prevents MoveFileEx replacement
  // while permitting the original to remain readable, including backup reads.
  const auto handle = CreateFileW(blocked.c_str(), GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  check(handle != INVALID_HANDLE_VALUE, "Cannot reserve denying-delete handle");
  bool failed = false;
  try {
    document.save(blocked.u8string());
  } catch (const fs::filesystem_error &) {
    failed = true;
  } catch (...) {
    CloseHandle(handle);
    throw;
  }
  CloseHandle(handle);
  check(failed && read(blocked) == retained,
        "Failed Windows replacement preserves exact existing bytes");
  noTemporary(directory, blocked);
#else
  const auto occupied = directory / "occupied.json";
  fs::create_directory(occupied);
  write(occupied / "original", retained);
  bool failed = false;
  try {
    document.save(occupied.u8string());
  } catch (const fs::filesystem_error &) {
    failed = true;
  }
  check(failed && read(occupied / "original") == retained,
        "Failed replacement preserves the occupied destination and bytes");
  noTemporary(directory, occupied);
#endif
}
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 2, "Pass a fresh report work directory");
    codecChecks();
    fileChecks(fs::u8path(argv[1]));
    std::cout << "Native preferences codec, recovery, Unicode paths, backups and "
                 "atomic replacement checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
