#include <imagejxx/settings.hpp>
#include <iostream>
#include <stdexcept>

int main() {
  using namespace imagejxx;
  Settings settings;
  settings.update({{"theme", std::string("light")}, {"brush_width", 12.}});
  try {
    settings.update({{"brush_width", 20.}, {"history_limit", std::int64_t(0)}});
    return 1;
  } catch (const std::invalid_argument &) {
    if (settings.number("brush_width") != 12.)
      return 1;
  }
  std::vector<std::string> warnings;
  auto migrated =
      Settings::migrate({{"foreground", std::string("red")}}, warnings, true);
  std::cout << Settings::schema().size() << " settings; legacy paint color "
            << migrated.text("foreground") << '\n';
  return warnings.empty() && migrated.text("foreground") == "#ff0000" ? 0 : 1;
}
