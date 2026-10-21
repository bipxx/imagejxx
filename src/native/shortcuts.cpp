#include <imagejxx/shortcuts.hpp>

#include <algorithm>
#include <array>
#ifdef IMAGEJXX_SHORTCUTS_TEST_MATERIALIZATION
#include <atomic>
#endif
#include <limits>
#include <string_view>
#include <utility>

namespace imagejxx {
namespace {
constexpr const char *key_names[] = {
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
    "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
    "Digit0", "Digit1", "Digit2", "Digit3", "Digit4", "Digit5", "Digit6",
    "Digit7", "Digit8", "Digit9", "F1", "F2", "F3", "F4", "F5", "F6",
    "F7", "F8", "F9", "F10", "F11", "F12", "F13", "F14", "F15", "F16",
    "F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24", "Escape",
    "Enter", "Tab", "Space", "Backspace", "Delete", "Insert", "Home", "End",
    "PageUp", "PageDown", "Left", "Right", "Up", "Down", "Minus", "Equal",
    "LeftBracket", "RightBracket", "Backslash", "Semicolon", "Apostrophe",
    "Grave", "Comma", "Period", "Slash"};
constexpr std::size_t key_count = sizeof(key_names) / sizeof(*key_names);
constexpr std::size_t command_count = 18;
static_assert(key_count == static_cast<std::size_t>(ShortcutKey::Slash) + 1,
              "Shortcut key names must cover the closed enum");
constexpr std::array<ShortcutPlatform, 3> platforms{
    ShortcutPlatform::Windows, ShortcutPlatform::Linux, ShortcutPlatform::MacOS};
constexpr std::size_t absent = std::numeric_limits<std::size_t>::max();
#ifdef IMAGEJXX_SHORTCUTS_TEST_MATERIALIZATION
// Private standalone-test observations of named logical staging boundaries.
// These are not allocation counts/bytes, public callbacks or shipping state.
std::atomic<std::size_t> edit_admissions{0};
std::atomic<std::size_t> canonical_materializations{0};
std::atomic<std::size_t> profile_materializations{0};
#endif
constexpr std::array<std::pair<ShortcutModifiers, const char *>, 5> modifier_names{{
    {ShortcutModifiers::Primary, "Primary+"}, {ShortcutModifiers::Ctrl, "Ctrl+"},
    {ShortcutModifiers::Alt, "Alt+"}, {ShortcutModifiers::Shift, "Shift+"},
    {ShortcutModifiers::Super, "Super+"}}};

std::size_t add(std::size_t a, std::size_t b) {
  if (b > std::numeric_limits<std::size_t>::max() - a)
    throw std::length_error("Shortcut resource size overflow");
  return a + b;
}
std::size_t multiply(std::size_t a, std::size_t b) {
  if (a && b > std::numeric_limits<std::size_t>::max() / a)
    throw std::length_error("Shortcut resource size overflow");
  return a * b;
}
std::size_t platform_index(ShortcutPlatform platform) {
  switch (platform) {
  case ShortcutPlatform::Windows: return 0;
  case ShortcutPlatform::Linux: return 1;
  case ShortcutPlatform::MacOS: return 2;
  }
  throw std::invalid_argument("Unknown shortcut platform");
}
std::size_t key_index(ShortcutKey key) {
  const auto index = static_cast<std::size_t>(key);
  if (index >= key_count)
    throw std::invalid_argument("Unknown shortcut key");
  return index;
}
unsigned bits(ShortcutModifiers modifiers) {
  return static_cast<unsigned>(modifiers);
}
bool has(ShortcutModifiers value, ShortcutModifiers modifier) {
  return (bits(value) & bits(modifier)) != 0;
}
void validate(const ShortcutChord &chord) {
  (void)key_index(chord.key);
  if (bits(chord.modifiers) & ~31u)
    throw std::invalid_argument("Unknown shortcut modifier bits");
  if (has(chord.modifiers, ShortcutModifiers::Primary) &&
      (has(chord.modifiers, ShortcutModifiers::Ctrl) ||
       has(chord.modifiers, ShortcutModifiers::Super)))
    throw std::invalid_argument("Primary cannot coexist with Ctrl or Super");
}
std::size_t canonical_bytes(const ShortcutChord &chord) {
  validate(chord);
  auto result = std::char_traits<char>::length(key_names[key_index(chord.key)]);
  for (const auto &entry : modifier_names)
    if (has(chord.modifiers, entry.first))
      result = add(result, std::char_traits<char>::length(entry.second));
  return result;
}
unsigned char lower(unsigned char c) {
  return c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
}
bool equal_folded(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (lower(static_cast<unsigned char>(a[i])) !=
        lower(static_cast<unsigned char>(b[i]))) return false;
  return true;
}
bool whitespace(unsigned char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}
ShortcutChord parse(std::string_view text) {
  while (!text.empty() && whitespace(static_cast<unsigned char>(text.front())))
    text.remove_prefix(1);
  while (!text.empty() && whitespace(static_cast<unsigned char>(text.back())))
    text.remove_suffix(1);
  if (text.empty()) throw std::invalid_argument("Shortcut chord is empty");
  ShortcutChord result;
  bool found_key = false;
  std::size_t offset = 0;
  while (offset < text.size()) {
    const auto end = text.find('+', offset);
    const auto token = text.substr(offset, end == std::string_view::npos
                                               ? text.size() - offset : end - offset);
    if (token.empty()) throw std::invalid_argument("Empty shortcut chord segment");
    ShortcutModifiers modifier = ShortcutModifiers::None;
    if (equal_folded(token, "Primary") || equal_folded(token, "CtrlOrCmd"))
      modifier = ShortcutModifiers::Primary;
    else if (equal_folded(token, "Ctrl") || equal_folded(token, "Control"))
      modifier = ShortcutModifiers::Ctrl;
    else if (equal_folded(token, "Alt")) modifier = ShortcutModifiers::Alt;
    else if (equal_folded(token, "Shift")) modifier = ShortcutModifiers::Shift;
    else if (equal_folded(token, "Super") || equal_folded(token, "Cmd") ||
             equal_folded(token, "Command") || equal_folded(token, "Meta"))
      modifier = ShortcutModifiers::Super;
    if (modifier != ShortcutModifiers::None) {
      if (found_key || has(result.modifiers, modifier))
        throw std::invalid_argument("Repeated or misplaced shortcut modifier");
      result.modifiers = result.modifiers | modifier;
    } else {
      if (found_key || end != std::string_view::npos)
        throw std::invalid_argument("Shortcut requires one final named key");
      for (std::size_t i = 0; i < key_count; ++i)
        if (equal_folded(token, key_names[i])) {
          result.key = static_cast<ShortcutKey>(i);
          found_key = true;
          break;
        }
      if (!found_key) throw std::invalid_argument("Unknown shortcut key token");
    }
    if (end == std::string_view::npos) break;
    offset = end + 1;
    if (offset == text.size())
      throw std::invalid_argument("Trailing shortcut separator");
  }
  if (!found_key) throw std::invalid_argument("Modifier-only shortcut is invalid");
  validate(result);
  return result;
}
bool valid_id(std::string_view id) {
  bool segment_start = true, has_dot = false;
  for (unsigned char c : id) {
    if (c == '.') {
      if (segment_start) return false;
      has_dot = true; segment_start = true;
    } else if (segment_start) {
      if (c < 'a' || c > 'z') return false;
      segment_start = false;
    } else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
      return false;
  }
  return has_dot && !segment_start;
}
std::size_t command_index(std::string_view id) {
  const auto &commands = shortcut_commands();
  for (std::size_t i = 0; i < commands.size(); ++i)
    if (id == commands[i].id) return i;
  return absent;
}
void require_command(const std::string &id) {
  if (command_index(id) == absent)
    throw std::invalid_argument("Unknown registered shortcut command");
}
bool reserved(ShortcutChord chord, ShortcutPlatform platform) {
  const auto modifiers = chord.modifiers;
  const auto key = chord.key;
  if (modifiers == ShortcutModifiers::None &&
      key_index(key) >= key_index(ShortcutKey::Escape) &&
      key_index(key) <= key_index(ShortcutKey::Down)) return true;
  if (platform == ShortcutPlatform::Windows || platform == ShortcutPlatform::Linux) {
    if (has(modifiers, ShortcutModifiers::Super) ||
        (has(modifiers, ShortcutModifiers::Ctrl) &&
         has(modifiers, ShortcutModifiers::Alt))) return true;
    if (modifiers == ShortcutModifiers::Alt &&
        (key == ShortcutKey::Tab || key == ShortcutKey::F4 ||
         (platform == ShortcutPlatform::Linux && key == ShortcutKey::F2))) return true;
  } else {
    if (modifiers == ShortcutModifiers::Super &&
        (key == ShortcutKey::Tab || key == ShortcutKey::Space || key == ShortcutKey::Q ||
         key == ShortcutKey::H || key == ShortcutKey::M)) return true;
    if (modifiers == (ShortcutModifiers::Super | ShortcutModifiers::Alt) &&
        key == ShortcutKey::Escape) return true;
    if (modifiers == ShortcutModifiers::Ctrl &&
        (key == ShortcutKey::Space || key == ShortcutKey::Left ||
         key == ShortcutKey::Right || key == ShortcutKey::Up || key == ShortcutKey::Down))
      return true;
  }
  return false;
}
struct ParsedRow {
  std::string_view line, id, rhs;
  std::size_t command = absent;
  std::optional<ShortcutChord> chord;
};
void validate_limits(const ShortcutLimits &limits) {
  const ShortcutLimits ceilings;
  const std::array<std::size_t, 7> values{
      limits.max_override_entries, limits.max_entry_bytes, limits.max_total_entry_bytes,
      limits.max_command_id_bytes, limits.max_chord_bytes, limits.max_registry_commands,
      limits.max_native_working_bytes};
  const std::array<std::size_t, 7> maximum{
      ceilings.max_override_entries, ceilings.max_entry_bytes, ceilings.max_total_entry_bytes,
      ceilings.max_command_id_bytes, ceilings.max_chord_bytes, ceilings.max_registry_commands,
      ceilings.max_native_working_bytes};
  for (std::size_t i = 0; i < values.size(); ++i)
    if (!values[i] || values[i] > maximum[i])
      throw std::invalid_argument("Shortcut limits must be positive and within owned ceilings");
}
std::size_t preflight(std::size_t entries, std::size_t total,
                      std::size_t largest, const ShortcutLimits &limits) {
  validate_limits(limits);
  if (entries > limits.max_override_entries || largest > limits.max_entry_bytes ||
      total > limits.max_total_entry_bytes || command_count > limits.max_registry_commands)
    throw std::length_error("Shortcut profile exceeds its logical input limits");
  // Charge simultaneous input/canonical/inactive/error strings and row scratch,
  // three full binding tables, all possible pair diagnostics and an immutable
  // holder/control block. Logical charges intentionally exceed active lengths;
  // allocator bookkeeping/capacity rounding/RSS are not measured here.
  const auto pairs = command_count * (command_count - 1) / 2;
  auto bound = add(1024, multiply(total, 6));
  bound = add(bound, multiply(entries, sizeof(ParsedRow) + 6 * sizeof(std::string)));
  bound = add(bound, multiply(add(entries, add(command_count, pairs)),
      sizeof(ShortcutDiagnostic) + 4 * 96 + 3 * sizeof(ShortcutPlatform)));
  bound = add(bound, multiply(command_count,
      3 * sizeof(ShortcutBinding) + 6 * 96 + 3 * sizeof(std::optional<ShortcutChord>)));
  if (bound > limits.max_native_working_bytes)
    throw std::length_error("Shortcut compilation exceeds its logical working-byte limit");
  return bound;
}
std::size_t preflight_lines(const std::vector<std::string> &lines,
                            const ShortcutLimits &limits) {
  validate_limits(limits);
  if (lines.size() > limits.max_override_entries)
    throw std::length_error("Too many shortcut override entries");
  std::size_t total = 0, largest = 0;
  for (const auto &line : lines) {
    total = add(total, line.size());
    largest = std::max(largest, line.size());
  }
  return preflight(lines.size(), total, largest, limits);
}
ParsedRow inspect(std::string_view line, const ShortcutLimits &limits) {
  const auto equals = line.find('=');
  if (equals == std::string_view::npos)
    throw std::invalid_argument("Shortcut override requires command_id=chord");
  ParsedRow row{line, line.substr(0, equals), line.substr(equals + 1)};
  if (row.id.size() > limits.max_command_id_bytes ||
      row.rhs.size() > limits.max_chord_bytes)
    throw std::length_error("Shortcut command or chord exceeds its byte limit");
  if (!valid_id(row.id)) throw std::invalid_argument("Invalid shortcut command ID");
  row.command = command_index(row.id);
  if (row.command != absent) {
    if (!row.rhs.empty()) row.chord = parse(row.rhs);
  } else {
    for (unsigned char c : row.rhs)
      if (c < 32 || c > 126)
        throw std::invalid_argument("Inactive shortcut content must be printable ASCII");
  }
  return row;
}
ShortcutChord physical(const ShortcutEvent &event) {
  (void)key_index(event.key);
  auto modifiers = ShortcutModifiers::None;
  if (event.ctrl) modifiers = modifiers | ShortcutModifiers::Ctrl;
  if (event.alt) modifiers = modifiers | ShortcutModifiers::Alt;
  if (event.shift) modifiers = modifiers | ShortcutModifiers::Shift;
  if (event.super) modifiers = modifiers | ShortcutModifiers::Super;
  return {event.key, modifiers};
}
ShortcutDecision decision(ShortcutDecisionReason reason,
                           ShortcutDisposition disposition = ShortcutDisposition::None) {
  return {std::nullopt, reason, disposition};
}
} // namespace

struct ShortcutProfile::Data {
  std::vector<std::string> overrides, inactive;
  std::array<std::vector<ShortcutBinding>, 3> tables;
  std::vector<ShortcutDiagnostic> diagnostics;
  std::size_t working_bytes = 0;
};
struct ShortcutAccess {
  static const std::vector<ShortcutBinding> &table(const ShortcutProfile &profile,
                                                  ShortcutPlatform platform) {
    return profile.data().tables[platform_index(platform)];
  }
  static const std::vector<std::string> &lines(const ShortcutProfile &profile) {
    return profile.data().overrides;
  }
};
#ifdef IMAGEJXX_SHORTCUTS_TEST_MATERIALIZATION
namespace detail {
// Declaration is local to the standalone native test, never the public header.
std::array<std::size_t, 3> shortcut_test_materializations() noexcept {
  return {{edit_admissions.load(std::memory_order_relaxed),
           canonical_materializations.load(std::memory_order_relaxed),
           profile_materializations.load(std::memory_order_relaxed)}};
}
} // namespace detail
#endif
ShortcutProfileError::ShortcutProfileError(
    std::string message, std::vector<ShortcutDiagnostic> diagnostics)
    : std::invalid_argument(std::move(message)), diagnostics_(std::move(diagnostics)) {}
const std::vector<ShortcutCommand> &shortcut_commands() {
  static const std::vector<ShortcutCommand> commands = [] {
    std::vector<ShortcutCommand> result;
    result.reserve(command_count);
    const auto append = [&](const char *id, const char *label, ShortcutScope scope,
                            const char *chord) {
      result.push_back({id, label, scope,
                        chord ? std::optional<ShortcutChord>(parse(chord)) : std::nullopt});
    };
    append("file.open", "Open image or session", ShortcutScope::Shell, "Primary+O");
    append("file.save", "Save document", ShortcutScope::Document, "Primary+S");
    append("file.close", "Close image", ShortcutScope::Document, "Primary+W");
    append("edit.undo", "Undo", ShortcutScope::Document, "Primary+Z");
    append("edit.redo", "Redo", ShortcutScope::Document, "Primary+Y");
    append("edit.copy", "Copy", ShortcutScope::Document, "Primary+C");
    append("edit.cut", "Cut", ShortcutScope::Document, "Primary+X");
    append("edit.paste", "Paste", ShortcutScope::Document, "Primary+V");
    append("edit.select_all", "Select all image", ShortcutScope::Document, "Primary+A");
    append("analysis.measure", "Measure", ShortcutScope::Document, "M");
    append("app.preferences", "Preferences", ShortcutScope::Shell, nullptr);
    append("app.jobs", "Jobs", ShortcutScope::Shell, nullptr);
    append("view.channel_display", "Channel display", ShortcutScope::Document, nullptr);
    append("image.auto_contrast", "Auto display range", ShortcutScope::Document, nullptr);
    append("image.extract_plane", "Export plane", ShortcutScope::Document, nullptr);
    append("view.orthogonal", "Toggle XY/XZ/YZ", ShortcutScope::Document, nullptr);
    append("filter.gaussian_imagej", "Gaussian blur", ShortcutScope::Document, nullptr);
    append("filter.rank_imagej", "Rank filter", ShortcutScope::Document, nullptr);
    return result;
  }();
  return commands;
}
ShortcutChord parse_shortcut_chord(const std::string &text) {
  if (text.size() > ShortcutLimits{}.max_chord_bytes)
    throw std::length_error("Shortcut chord exceeds its owned byte ceiling");
  return parse(text);
}
std::string format_shortcut_chord(const ShortcutChord &chord) {
  validate(chord);
#ifdef IMAGEJXX_SHORTCUTS_TEST_MATERIALIZATION
  canonical_materializations.fetch_add(1, std::memory_order_relaxed);
#endif
  std::string result;
  for (const auto &entry : modifier_names)
    if (has(chord.modifiers, entry.first)) result += entry.second;
  result += key_names[key_index(chord.key)];
  return result;
}
ShortcutChord resolve_shortcut_chord(const ShortcutChord &chord,
                                     ShortcutPlatform platform) {
  validate(chord);
  (void)platform_index(platform);
  auto result = chord;
  if (has(result.modifiers, ShortcutModifiers::Primary)) {
    auto resolved = bits(result.modifiers) & ~bits(ShortcutModifiers::Primary);
    resolved |= bits(platform == ShortcutPlatform::MacOS ? ShortcutModifiers::Super
                                                        : ShortcutModifiers::Ctrl);
    result.modifiers = static_cast<ShortcutModifiers>(resolved);
  }
  return result;
}
ShortcutProfile::ShortcutProfile(std::shared_ptr<const Data> data)
    : data_(std::move(data)) {}
ShortcutProfile::ShortcutProfile() : ShortcutProfile(compile_shortcuts({})) {}
const ShortcutProfile::Data &ShortcutProfile::data() const {
  if (!data_) throw std::invalid_argument("Moved-from shortcut profile");
  return *data_;
}
std::vector<std::string> ShortcutProfile::override_lines() const { return data().overrides; }
std::vector<std::string> ShortcutProfile::inactive_rows() const { return data().inactive; }
std::vector<ShortcutBinding> ShortcutProfile::bindings(ShortcutPlatform platform) const {
  return data().tables[platform_index(platform)];
}
std::vector<ShortcutDiagnostic> ShortcutProfile::diagnostics() const { return data().diagnostics; }
std::optional<ShortcutChord> ShortcutProfile::chord(const std::string &command_id,
                                                  ShortcutPlatform platform) const {
  const auto index = command_index(command_id);
  if (index == absent) throw std::invalid_argument("Unknown registered shortcut command");
  return data().tables[platform_index(platform)][index].chord;
}
std::size_t ShortcutProfile::working_bytes() const { return data().working_bytes; }

ShortcutProfile compile_shortcuts(const std::vector<std::string> &lines,
                                  const ShortcutLimits &limits) {
  const auto bound = preflight_lines(lines, limits);
  std::vector<ParsedRow> rows;
  rows.reserve(lines.size());
  for (const auto &line : lines) {
    const auto row = inspect(line, limits);
    for (const auto &seen : rows)
      if (row.id == seen.id) throw std::invalid_argument("Duplicate shortcut command ID");
    rows.push_back(row);
  }
  std::array<std::optional<ShortcutChord>, command_count> effective;
  std::array<bool, command_count> overridden{};
  const auto &commands = shortcut_commands();
  for (std::size_t i = 0; i < command_count; ++i) effective[i] = commands[i].default_chord;
  for (const auto &row : rows)
    if (row.command != absent) {
      overridden[row.command] = true;
      effective[row.command] = row.chord;
    }
#ifdef IMAGEJXX_SHORTCUTS_TEST_MATERIALIZATION
  profile_materializations.fetch_add(1, std::memory_order_relaxed);
#endif
  auto result = std::make_shared<ShortcutProfile::Data>();
  result->working_bytes = bound;
  result->overrides.reserve(rows.size());
  result->inactive.reserve(rows.size());
  result->diagnostics.reserve(rows.size() + command_count + command_count * (command_count - 1) / 2);
  for (std::size_t i = 0; i < command_count; ++i)
    if (overridden[i]) result->overrides.push_back(commands[i].id + "=" +
        (effective[i] ? format_shortcut_chord(*effective[i]) : std::string()));
  for (const auto &row : rows)
    if (row.command == absent) {
      result->overrides.emplace_back(row.line);
      result->inactive.emplace_back(row.line);
      result->diagnostics.push_back({ShortcutDiagnosticKind::InactiveUnknown,
          std::string(row.id), {}, std::string(row.rhs), {}, {}});
    }
  bool invalid = false;
  for (std::size_t p = 0; p < platforms.size(); ++p) {
    auto &table = result->tables[p];
    table.reserve(command_count);
    for (std::size_t i = 0; i < command_count; ++i)
      table.push_back({commands[i].id, effective[i]
          ? std::optional<ShortcutChord>(resolve_shortcut_chord(*effective[i], platforms[p]))
          : std::nullopt});
  }
  for (std::size_t i = 0; i < command_count; ++i) {
    if (!effective[i]) continue;
    ShortcutDiagnostic diagnostic{ShortcutDiagnosticKind::Reserved, commands[i].id,
                                  {}, format_shortcut_chord(*effective[i]), {}, {}};
    for (std::size_t p = 0; p < platforms.size(); ++p)
      if (reserved(*result->tables[p][i].chord, platforms[p]))
        diagnostic.platforms.push_back(platforms[p]);
    if (!diagnostic.platforms.empty()) {
      invalid = true;
      result->diagnostics.push_back(std::move(diagnostic));
    }
    for (std::size_t j = i + 1; j < command_count; ++j) {
      if (!effective[j]) continue;
      ShortcutDiagnostic conflict{ShortcutDiagnosticKind::Conflict, commands[i].id,
          commands[j].id, format_shortcut_chord(*effective[i]),
          format_shortcut_chord(*effective[j]), {}};
      for (std::size_t p = 0; p < platforms.size(); ++p)
        if (result->tables[p][i].chord == result->tables[p][j].chord)
          conflict.platforms.push_back(platforms[p]);
      if (!conflict.platforms.empty()) {
        invalid = true;
        result->diagnostics.push_back(std::move(conflict));
      }
    }
  }
  if (invalid)
    throw ShortcutProfileError("Shortcut profile has reserved or conflicting bindings",
                                std::move(result->diagnostics));
  return ShortcutProfile(std::move(result));
}

namespace {
bool same_tables(const ShortcutProfile &a, const ShortcutProfile &b) {
  for (const auto platform : platforms) {
    const auto &left = ShortcutAccess::table(a, platform);
    const auto &right = ShortcutAccess::table(b, platform);
    for (std::size_t i = 0; i < left.size(); ++i)
      if (left[i].chord != right[i].chord) return false;
  }
  return true;
}
ShortcutEditResult edited(const ShortcutProfile &before, ShortcutProfile after) {
  const auto changed = ShortcutAccess::lines(before) != ShortcutAccess::lines(after);
  const auto bindings_changed = !same_tables(before, after);
  return {changed ? std::move(after) : before,
          changed ? ShortcutEditStatus::Changed : ShortcutEditStatus::NoOp,
          bindings_changed};
}
enum class OverrideEdit { Assign, Clear, Reset };
ShortcutEditResult modify(const ShortcutProfile &profile, const std::string &id,
                           OverrideEdit edit, std::optional<ShortcutChord> chord,
                           const ShortcutLimits &limits) {
  require_command(id);
  const auto &original = ShortcutAccess::lines(profile);
  // Chord parsing and exact canonical-length arithmetic borrow input or use
  // stack-only typed values. No owned candidate text precedes full admission.
  const auto rhs_bytes = edit == OverrideEdit::Assign ? canonical_bytes(*chord) : 0;
  const bool replace = edit != OverrideEdit::Reset;
  std::size_t count = replace ? 1 : 0;
  const auto replacement_bytes = replace ? add(add(id.size(), 1), rhs_bytes) : 0;
  std::size_t total = replacement_bytes, largest = replacement_bytes;
  for (const auto &line : original) {
    if (std::string_view(line).substr(0, line.find('=')) == id) continue;
    ++count; total = add(total, line.size()); largest = std::max(largest, line.size());
  }
  (void)preflight(count, total, largest, limits);
  // Inspect borrowed retained rows and replacement sizes before copying them.
  if (id.size() > limits.max_command_id_bytes || rhs_bytes > limits.max_chord_bytes)
    throw std::length_error("Shortcut command or chord exceeds its byte limit");
  for (const auto &line : original)
    if (std::string_view(line).substr(0, line.find('=')) != id) (void)inspect(line, limits);
#ifdef IMAGEJXX_SHORTCUTS_TEST_MATERIALIZATION
  edit_admissions.fetch_add(1, std::memory_order_relaxed);
  profile_materializations.fetch_add(1, std::memory_order_relaxed);
#endif
  std::vector<std::string> lines;
  lines.reserve(count);
  for (const auto &line : original)
    if (std::string_view(line).substr(0, line.find('=')) != id) lines.push_back(line);
  if (replace) lines.push_back(id + "=" +
      (edit == OverrideEdit::Assign ? format_shortcut_chord(*chord) : std::string()));
  return edited(profile, compile_shortcuts(lines, limits));
}
ShortcutDecision evaluate(const ShortcutProfile &profile, ShortcutPlatform platform,
                            const ShortcutEvent *events, std::size_t count,
                            const ShortcutContext &context) {
  const auto &table = ShortcutAccess::table(profile, platform);
  switch (context.focus) {
  case ShortcutFocus::None: case ShortcutFocus::Workspace: case ShortcutFocus::Canvas:
  case ShortcutFocus::ROIManager: case ShortcutFocus::Control: case ShortcutFocus::Text:
  case ShortcutFocus::Modal: case ShortcutFocus::Capture: break;
  default: throw std::invalid_argument("Unknown shortcut focus");
  }
  for (const auto &entry : context.availability) require_command(entry.first);
  for (std::size_t i = 0; i < count; ++i) (void)key_index(events[i].key);
  if (!context.window_focused) return decision(ShortcutDecisionReason::Unfocused);
  if (!context.rearmed) return decision(ShortcutDecisionReason::Rearming);
  if (context.focus == ShortcutFocus::Capture)
    return decision(ShortcutDecisionReason::Capture, ShortcutDisposition::LocalContext);
  if (context.focus == ShortcutFocus::Modal)
    return decision(ShortcutDecisionReason::Modal, ShortcutDisposition::LocalContext);
  if (context.focus == ShortcutFocus::Text || context.focus == ShortcutFocus::Control)
    return decision(ShortcutDecisionReason::Control, ShortcutDisposition::LocalContext);
  if (context.gesture_active)
    return decision(ShortcutDecisionReason::Gesture, ShortcutDisposition::LocalContext);
  if (context.local_key_claimed && context.focus != ShortcutFocus::ROIManager)
    return decision(ShortcutDecisionReason::LocalClaim, ShortcutDisposition::LocalContext);
  if (context.focus == ShortcutFocus::None) return decision(ShortcutDecisionReason::NoWorkspace);
  std::array<bool, key_count> seen{};
  std::size_t matches = 0, match = absent;
  bool match_is_bare = false;
  bool local_select_all = false, any_edge = false, any_repeat = false;
  for (std::size_t e = 0; e < count; ++e) {
    const auto &event = events[e];
    if (!event.down_edge) continue;
    if (event.repeat) { any_repeat = true; continue; }
    any_edge = true;
    const auto chord = physical(event);
    if (context.focus == ShortcutFocus::ROIManager &&
        chord == resolve_shortcut_chord({ShortcutKey::A, ShortcutModifiers::Primary}, platform)) {
      local_select_all = true;
      continue;
    }
    if (context.local_key_claimed) continue;
    for (std::size_t i = 0; i < table.size(); ++i) {
      if (!table[i].chord || *table[i].chord != chord) continue;
      if (!seen[key_index(event.key)]) {
        seen[key_index(event.key)] = true;
        ++matches;
        match = i;
        match_is_bare = chord.modifiers == ShortcutModifiers::None;
      }
      break;
    }
  }
  if (matches > 1) return decision(ShortcutDecisionReason::Ambiguous);
  if (matches == 1) {
    if (match_is_bare && context.focus != ShortcutFocus::Workspace &&
        context.focus != ShortcutFocus::Canvas)
      return decision(ShortcutDecisionReason::NoWorkspace);
    const auto available = context.availability.find(table[match].command_id);
    if (available == context.availability.end() || !available->second)
      return decision(ShortcutDecisionReason::Unavailable);
    return {table[match].command_id, ShortcutDecisionReason::Dispatch,
            ShortcutDisposition::GlobalCommand};
  }
  if (local_select_all)
    return decision(ShortcutDecisionReason::ROIManagerSelectAll, ShortcutDisposition::LocalContext);
  if (context.local_key_claimed)
    return decision(ShortcutDecisionReason::LocalClaim, ShortcutDisposition::LocalContext);
  if (!any_edge) return decision(any_repeat ? ShortcutDecisionReason::Repeat
                                          : ShortcutDecisionReason::NoEdge);
  return decision(ShortcutDecisionReason::Unbound);
}
} // namespace
ShortcutEditResult assign_shortcut(const ShortcutProfile &profile,
                                   const std::string &command_id,
                                   const std::string &chord,
                                   const ShortcutLimits &limits) {
  // Empty RHS is the explicit Clear representation, not a modifier-only capture.
  if (chord.size() > limits.max_chord_bytes)
    throw std::length_error("Shortcut chord exceeds its byte limit");
  const auto parsed = chord.empty() ? std::optional<ShortcutChord>()
                                    : std::optional<ShortcutChord>(parse(chord));
  return modify(profile, command_id, chord.empty() ? OverrideEdit::Clear : OverrideEdit::Assign,
                parsed, limits);
}
ShortcutEditResult clear_shortcut(const ShortcutProfile &profile,
                                  const std::string &command_id,
                                  const ShortcutLimits &limits) {
  return modify(profile, command_id, OverrideEdit::Clear, std::nullopt, limits);
}
ShortcutEditResult reset_shortcut(const ShortcutProfile &profile,
                                  const std::string &command_id,
                                  const ShortcutLimits &limits) {
  return modify(profile, command_id, OverrideEdit::Reset, std::nullopt, limits);
}
ShortcutEditResult reset_shortcuts(const ShortcutProfile &profile,
                                   const ShortcutLimits &limits) {
  return edited(profile, compile_shortcuts({}, limits));
}
ShortcutDecision evaluate_shortcut(const ShortcutProfile &profile,
                                    ShortcutPlatform platform,
                                    const ShortcutEvent &event,
                                    const ShortcutContext &context) {
  return evaluate(profile, platform, &event, 1, context);
}
ShortcutDecision evaluate_shortcuts(const ShortcutProfile &profile,
                                     ShortcutPlatform platform,
                                     const std::vector<ShortcutEvent> &events,
                                     const ShortcutContext &context) {
  return evaluate(profile, platform, events.data(), events.size(), context);
}
} // namespace imagejxx
