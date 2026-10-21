#include <cmath>
#include <imagejxx/settings.hpp>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace imagejxx;

const char *kind_name(SettingKind kind) {
  switch (kind) {
  case SettingKind::Boolean:
    return "bool";
  case SettingKind::Integer:
    return "int";
  case SettingKind::Number:
    return "float";
  case SettingKind::Choice:
    return "choice";
  case SettingKind::Color:
    return "color";
  case SettingKind::StringList:
    return "columns";
  case SettingKind::Text:
    return "text";
  }
  throw std::runtime_error("Unknown native setting kind");
}

void quoted(std::ostream &out, const std::string &value) {
  static constexpr char hex[] = "0123456789abcdef";
  out << '"';
  for (std::size_t i = 0; i < value.size();) {
    const auto byte = static_cast<unsigned char>(value[i]);
    if (byte >= 0x80) {
      std::size_t length;
      unsigned codepoint, minimum;
      if (byte >= 0xc2 && byte <= 0xdf) {
        length = 2;
        codepoint = byte & 0x1f;
        minimum = 0x80;
      } else if (byte >= 0xe0 && byte <= 0xef) {
        length = 3;
        codepoint = byte & 0x0f;
        minimum = 0x800;
      } else if (byte >= 0xf0 && byte <= 0xf4) {
        length = 4;
        codepoint = byte & 0x07;
        minimum = 0x10000;
      } else
        throw std::runtime_error("Setting text contains invalid UTF-8");
      if (length > value.size() - i)
        throw std::runtime_error("Setting text contains truncated UTF-8");
      for (std::size_t n = 1; n < length; ++n) {
        const auto next = static_cast<unsigned char>(value[i + n]);
        if ((next & 0xc0) != 0x80)
          throw std::runtime_error("Setting text contains invalid UTF-8");
        codepoint = (codepoint << 6) | (next & 0x3f);
      }
      if (codepoint < minimum || codepoint > 0x10ffff ||
          (codepoint >= 0xd800 && codepoint <= 0xdfff))
        throw std::runtime_error("Setting text contains invalid UTF-8");
      out.write(value.data() + i, static_cast<std::streamsize>(length));
      i += length;
      continue;
    }
    ++i;
    switch (byte) {
    case '"':
      out << "\\\"";
      break;
    case '\\':
      out << "\\\\";
      break;
    case '\b':
      out << "\\b";
      break;
    case '\f':
      out << "\\f";
      break;
    case '\n':
      out << "\\n";
      break;
    case '\r':
      out << "\\r";
      break;
    case '\t':
      out << "\\t";
      break;
    default:
      if (byte < 0x20)
        out << "\\u00" << hex[byte >> 4] << hex[byte & 0x0f];
      else
        out << static_cast<char>(byte);
    }
  }
  out << '"';
}

void number(std::ostream &out, double value) {
  if (!std::isfinite(value))
    throw std::runtime_error("Setting schema contains a nonfinite number");
  std::ostringstream encoded;
  encoded.imbue(std::locale::classic());
  encoded << std::setprecision(std::numeric_limits<double>::max_digits10)
          << value;
  auto text = encoded.str();
  if (text.find_first_of(".eE") == std::string::npos)
    text += ".0";
  out << text;
}

void strings(std::ostream &out, const std::vector<std::string> &values) {
  out << '[';
  bool first = true;
  for (const auto &value : values) {
    if (!first)
      out << ',';
    first = false;
    quoted(out, value);
  }
  out << ']';
}

void setting_value(std::ostream &out, const SettingValue &value) {
  std::visit(
      [&](const auto &item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::monostate>)
          out << "null";
        else if constexpr (std::is_same_v<T, bool>)
          out << (item ? "true" : "false");
        else if constexpr (std::is_same_v<T, std::int64_t>)
          out << item;
        else if constexpr (std::is_same_v<T, double>)
          number(out, item);
        else if constexpr (std::is_same_v<T, std::string>)
          quoted(out, item);
        else
          strings(out, item);
      },
      value);
}

void validate_defaults(const Settings &settings) {
  const auto &definitions = Settings::schema();
  if (definitions.empty() || definitions.size() != settings.values().size())
    throw std::runtime_error("Setting definitions/defaults differ in size");
  std::set<std::string> names;
  for (const auto &definition : definitions) {
    if (definition.name.empty() || !names.insert(definition.name).second)
      throw std::runtime_error(
          "Setting definitions need unique nonempty names");
    kind_name(definition.kind);
    if (definition.default_value != settings.at(definition.name))
      throw std::runtime_error("Setting definition/default value differs: " +
                               definition.name);
    const bool numeric = definition.kind == SettingKind::Integer ||
                         definition.kind == SettingKind::Number;
    if (numeric && (!std::isfinite(definition.minimum) ||
                    !std::isfinite(definition.maximum) ||
                    definition.minimum > definition.maximum))
      throw std::runtime_error("Setting bounds are invalid: " +
                               definition.name);
    const std::set<std::string> choices(definition.choices.begin(),
                                        definition.choices.end());
    if (choices.size() != definition.choices.size())
      throw std::runtime_error("Setting choices contain duplicates: " +
                               definition.name);
  }
  // Use the public native validator, including constraints beyond the schema's
  // numeric bounds. Exporting definitions does not establish consumer behavior.
  Settings validated;
  validated.update(settings.values());
  if (validated.values() != settings.values())
    throw std::runtime_error(
        "Setting defaults are not canonical native values");
}
} // namespace

int main() {
  try {
    const Settings settings;
    validate_defaults(settings);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"schema_version\":" << Settings::schema_version
        << ",\"definitions\":[";
    bool first = true;
    for (const auto &definition : Settings::schema()) {
      if (!first)
        out << ',';
      first = false;
      out << "{\"name\":";
      quoted(out, definition.name);
      out << ",\"group\":";
      quoted(out, definition.group);
      out << ",\"label\":";
      quoted(out, definition.label);
      out << ",\"description\":";
      quoted(out, definition.description);
      out << ",\"kind\":";
      quoted(out, kind_name(definition.kind));
      out << ",\"default\":";
      setting_value(out, definition.default_value);
      const bool numeric = definition.kind == SettingKind::Integer ||
                           definition.kind == SettingKind::Number;
      out << ",\"minimum\":";
      if (numeric)
        number(out, definition.minimum);
      else
        out << "null";
      out << ",\"maximum\":";
      if (numeric)
        number(out, definition.maximum);
      else
        out << "null";
      out << ",\"choices\":";
      strings(out, definition.choices);
      out << '}';
    }
    out << "],\"defaults\":{";
    first = true;
    for (const auto &[name, value] : settings.values()) {
      if (!first)
        out << ',';
      first = false;
      quoted(out, name);
      out << ':';
      setting_value(out, value);
    }
    out << "}}\n";
    if (!out)
      throw std::runtime_error("Settings JSON serialization failed");
    std::cout << out.str();
    std::cout.flush();
    if (!std::cout)
      throw std::runtime_error("Writing settings JSON failed");
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Settings schema export failed: " << error.what() << '\n';
    return 1;
  }
}
