#pragma once
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

namespace imagejxx::desktop {
inline std::string geometry_number_text(double value) {
  std::ostringstream text;text.imbue(std::locale::classic());
  text << std::setprecision(17) << value;return text.str();
}
inline double parse_geometry_number(const std::string &text) {
  if(text.empty() || text.size()>128)
    throw std::invalid_argument("Enter a complete finite number (at most 128 characters)");
  std::istringstream input(text);input.imbue(std::locale::classic());
  double value;input >> std::noskipws >> value;
  if(!input || input.peek()!=std::char_traits<char>::eof())
    throw std::invalid_argument("Enter a complete finite number");
  if(!std::isfinite(value))throw std::invalid_argument("Geometry coordinate must be finite");
  return value;
}
} // namespace imagejxx::desktop
