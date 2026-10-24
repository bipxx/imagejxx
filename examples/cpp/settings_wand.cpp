#include <imagejxx/image.hpp>
#include <imagejxx/preferences.hpp>

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
std::size_t checks = 0;
void check(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
}

// Read an actual UI-written file through the installed public SDK. This client
// never writes the file and never calls private desktop or test implementation.
int main(int argc, char **argv) {
  try {
    check(argc == 2, "Use settings_wand ACTUAL_UI_PREFERENCES_PATH");
    const auto preferences = imagejxx::PreferencesDocument::load(argv[1], false);
    check(preferences.settings.number("wand_tolerance") == 1., "UI tolerance");
    check(preferences.settings.integer("wand_connectivity") == 8, "UI connectivity");
    check(preferences.settings.values().size() == 61, "Installed settings schema");
    check(!preferences.write_blocked && preferences.warnings.empty(), "Strict file load");
    const auto future = std::string(R"({"cfg1_future":{"items":[true,null,")") +
        "\xCE\xBB" + R"(",18446744073709551615]}})";
    const auto extras = std::string(R"({"cfg1_extra":{"label":")") +
        "\xCE\xBC" + R"(","values":[3,1]},"recent":[]})";
    check(preferences.unknown_values_json == future, "Opaque values and uint64");
    check(preferences.extras_json == extras, "Opaque extras and UTF-8");
    check(preferences.unknown_fields_json ==
        R"({"cfg1_root":{"note":"root","uint":18446744073709551615}})",
        "Opaque root fields");
    imagejxx::Image image(3, 3, 1, imagejxx::PixelType::UInt8);
    image.pixels = {10, 20, 20, 20, 10, 20, 20, 20, 11};
    const auto original = image.pixels;
    const auto selected = imagejxx::wand_mask(image, 0, 0,
        preferences.settings.number("wand_tolerance"),
        static_cast<int>(preferences.settings.integer("wand_connectivity")));
    constexpr std::array<double, 9> expected{255, 0, 0, 0, 255, 0, 0, 0, 255};
    check(selected.width == 3 && selected.height == 3 && selected.channels == 1,
          "Wand output extent");
    for (std::size_t i = 0; i < expected.size(); ++i)
      check(selected.pixels.at(i) == expected[i], "Literal UI-consumed mask");
    check(image.pixels == original, "Wand preserves source words");
    auto detached = preferences.settings;
    detached.update({{"wand_tolerance", 0.}, {"wand_connectivity", std::int64_t(4)}});
    check(preferences.settings.number("wand_tolerance") == 1. &&
          preferences.settings.integer("wand_connectivity") == 8,
          "Detached settings do not mutate loaded preferences");
    std::cout << "PASS " << checks << " installed settings/Wand readback checks\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
