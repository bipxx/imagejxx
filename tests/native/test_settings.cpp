#include <imagejxx/image.hpp>
#include <imagejxx/settings.hpp>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

using namespace imagejxx;
namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class F> void rejects(F function) {
  bool rejected = false;
  try {
    function();
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  check(rejected, "Invalid setting or wand input was accepted");
}
} // namespace
int main() {
  try {
    Settings settings;
    std::set<std::string> names;
    for (const auto &d : Settings::schema()) {
      check(names.insert(d.name).second, "Setting names must be unique");
      settings.set(d.name, d.default_value);
    }
    settings.update(
        {{"theme", std::string("light")}, {"brush_width", std::int64_t(12)}});
    auto before = settings.values();
    rejects([&] {
      settings.update(
          {{"brush_width", 20.}, {"history_limit", std::int64_t(0)}});
    });
    check(settings.values() == before,
          "Rejected patches cannot partly change settings");
    rejects([&] { settings.set("history_limit", true); });
    rejects([&] {
      settings.set("ui_scale", std::numeric_limits<double>::quiet_NaN());
    });
    rejects([&] { settings.set("wand_connectivity", std::int64_t(6)); });
    rejects([&] { settings.set("profile_samples", std::int64_t(1)); });
    rejects([&] { settings.set("foreground", std::string("white")); });
    rejects([&] { settings.set("unknown", false); });
    settings.set("foreground", std::string("#AA33CC"));
    check(settings.text("foreground") == "#aa33cc", "Color normalization");
    settings.set("measurements", std::vector<std::string>{"MyMetric", "mean"});
    rejects([&] {
      settings.set("measurements", std::vector<std::string>{"mean", "mean"});
    });
    settings.reset("Drawing");
    check(settings.text("theme") == "light" &&
              settings.number("brush_width") == 8,
          "Group reset preserves other groups");
    std::vector<std::string> warnings;
    auto migrated = Settings::migrate(
        {{"brush_width", 14.}, {"history_limit", std::int64_t(-1)}}, warnings,
        true);
    check(migrated.number("point_size") == 14 &&
              migrated.integer("history_limit") == 8 && warnings.size() == 1,
          "Legacy migration retains valid settings and reports invalid values");
    warnings.clear();
    migrated = Settings::migrate({{"foreground", std::string("red")},
                                  {"background", std::string("#03A")}},
                                 warnings, true);
    check(warnings.empty() && migrated.text("foreground") == "#ff0000" &&
              migrated.text("background") == "#0033aa",
          "Legacy named and short colors preserve their original meaning");
    Image image = fill(Image(3, 3), 20);
    image.at(0, 0) = image.at(1, 1) = 10;
    image.at(2, 2) = 11;
    auto four = wand_mask(image, 0, 0, 1, 4);
    auto eight = wand_mask(image, 0, 0, 0, 8);
    auto tolerant = wand_mask(image, 0, 0, 1, 8);
    check(four.at(1, 1) == 0 && eight.at(1, 1) == 255 && eight.at(2, 2) == 0 &&
              tolerant.at(2, 2) == 255,
          "Wand connectivity and seed-relative tolerance");
    rejects([&] { wand_mask(image, 0, 0, -1, 4); });
    rejects([&] { wand_mask(image, 0, 0, 0, 6); });
    std::cout << "Shared settings and native wand checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
