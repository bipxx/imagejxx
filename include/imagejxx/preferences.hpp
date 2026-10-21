#pragma once

#include <imagejxx/settings.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace imagejxx {
// Opaque JSON integers outside the signed/unsigned 64-bit range cannot be
// retained by the native codec. Never round them through a floating value.
class PreferencesRepresentationError : public std::invalid_argument {
public:
  using std::invalid_argument::invalid_argument;
};

// Opaque ImGui layout text, bounded to 1 MiB of strict UTF-8 without NUL.
// This checks storage safety, not arbitrary INI grammar or monitor geometry.
void validate_preferences_layout(const std::string &layout);

// A detached version-1 preferences document. Opaque compartments are UTF-8
// JSON object strings and have no MetadataObject nesting limit.
struct PreferencesDocument {
  Settings settings;
  std::string extras_json = R"({"recent":[]})";
  std::string unknown_values_json = "{}";
  std::string unknown_fields_json = "{}";
  std::vector<std::string> warnings;
  bool write_blocked = false;

  // parse recovers invalid known settings/recent/layout only. Malformed JSON,
  // invalid envelopes and unsupported representations always throw.
  static PreferencesDocument parse(const std::string &utf8_json,
                                   bool recover = true);
  // Recovery loading retains defaults and blocks automatic writes after an
  // unreadable/malformed file. A missing file returns writable defaults only
  // with recover=true; strict loading throws for a missing file.
  static PreferencesDocument load(const std::string &utf8_path,
                                  bool recover = true);
  std::string json() const;
  // Validate before creating files, then publish using an exclusive sibling
  // temporary. Explicit recovery saves preserve the original in .bak/.bak.N.
  // Recovery flags remain unchanged on this const document.
  bool save(const std::string &utf8_path, bool explicit_save = false) const;
};
} // namespace imagejxx
