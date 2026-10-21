#include <imagejxx/shortcuts.hpp>

#include <array>
#include <iostream>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace imagejxx;
#ifdef IMAGEJXX_SHORTCUTS_TEST_MATERIALIZATION
namespace imagejxx { namespace detail {
std::array<std::size_t, 3> shortcut_test_materializations() noexcept;
} }
#endif
namespace {
int checks = 0, groups = 0;
void check(bool condition, const char *message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
template <class Error = std::invalid_argument, class Function>
void rejects(Function function) {
  bool rejected = false;
  try { function(); } catch (const Error &) { rejected = true; }
  check(rejected, "Expected shortcut refusal was absent");
}
ShortcutContext admitted(ShortcutFocus focus = ShortcutFocus::Canvas) {
  ShortcutContext context;
  context.focus = focus;
  for (const auto &command : shortcut_commands()) context.availability[command.id] = true;
  return context;
}
ShortcutEvent edge(ShortcutKey key, bool ctrl = false, bool alt = false,
                    bool shift = false, bool super = false) {
  return {key, ctrl, alt, shift, super, true, false};
}
void command(const ShortcutDecision &result, const char *id) {
  check(result.reason == ShortcutDecisionReason::Dispatch && result.command_id &&
            *result.command_id == id && result.consumed() &&
            result.disposition == ShortcutDisposition::GlobalCommand,
        "Decision did not contain the literal expected command");
}
void refused(const ShortcutDecision &result, ShortcutDecisionReason reason,
              ShortcutDisposition disposition = ShortcutDisposition::None) {
  check(!result.command_id && result.reason == reason && result.disposition == disposition,
        "Decision did not contain the literal expected refusal");
}
void registry() {
  const std::array<const char *, 18> ids{
      "file.open", "file.save", "file.close", "edit.undo", "edit.redo", "edit.copy",
      "edit.cut", "edit.paste", "edit.select_all", "analysis.measure", "app.preferences",
      "app.jobs", "view.channel_display", "image.auto_contrast", "image.extract_plane",
      "view.orthogonal", "filter.gaussian_imagej", "filter.rank_imagej"};
  const std::array<const char *, 10> defaults{
      "Primary+O", "Primary+S", "Primary+W", "Primary+Z", "Primary+Y", "Primary+C",
      "Primary+X", "Primary+V", "Primary+A", "M"};
  const auto &commands = shortcut_commands();
  check(shortcut_registry_version == 1 && commands.size() == ids.size(), "Registry version/count");
  std::set<std::string> unique;
  for (std::size_t i = 0; i < commands.size(); ++i) {
    check(commands[i].id == ids[i] && unique.insert(commands[i].id).second,
          "Exact ordered registry ID");
    check(!commands[i].label.empty(), "Registry label");
    check(commands[i].scope == ((i == 0 || i == 10 || i == 11)
              ? ShortcutScope::Shell : ShortcutScope::Document), "Exact registry scope");
    if (i < defaults.size())
      check(commands[i].default_chord &&
                format_shortcut_chord(*commands[i].default_chord) == defaults[i],
            "Exact default chord");
    else check(!commands[i].default_chord, "Initially unbound consumer");
  }
  ShortcutProfile profile;
  check(profile.override_lines().empty() && profile.inactive_rows().empty() &&
            profile.diagnostics().empty(), "Defaults contain no synthetic overrides");
  for (const auto platform : {ShortcutPlatform::Windows, ShortcutPlatform::Linux,
                              ShortcutPlatform::MacOS}) {
    const auto table = profile.bindings(platform);
    check(table.size() == ids.size(), "Each platform has a full registry table");
    std::size_t bound = 0;
    for (const auto &binding : table) if (binding.chord) ++bound;
    check(bound == 10, "Exactly ten defaults on each platform");
  }
}
void parser() {
  const std::array<std::pair<const char *, const char *>, 9> aliases{{
      {"  control+o\t", "Ctrl+O"}, {"ctrlOrcmd+k", "Primary+K"},
      {"CMD+digit2", "Super+Digit2"}, {"Command+f24", "Super+F24"},
      {"Meta+Pageup", "Super+PageUp"}, {"shift+alt+primary+F20", "Primary+Alt+Shift+F20"},
      {"ctrl+super+k", "Ctrl+Super+K"}, {"shift+equal", "Shift+Equal"},
      {"gRaVe", "Grave"}}};
  for (const auto &value : aliases)
    check(format_shortcut_chord(parse_shortcut_chord(value.first)) == value.second,
          "Literal parser alias/canonical order");
  for (int value = 0; value <= static_cast<int>(ShortcutKey::Slash); ++value) {
    ShortcutChord chord{static_cast<ShortcutKey>(value), ShortcutModifiers::None};
    check(parse_shortcut_chord(format_shortcut_chord(chord)) == chord,
          "Closed key token roundtrip");
  }
  for (const auto *text : {"", " ", "Ctrl", "Ctrl+", "+K", "Ctrl++K", "K+Ctrl",
       "Ctrl+Control+K", "Primary+Ctrl+K", "Primary+Super+K", "Ctrl +K", "Mouse1",
       "WheelUp", "+", "Ctrl+1", "Ctrl+U+P", "Ctrl+é", "Ctrl+Escape\nX"})
    rejects([&] { (void)parse_shortcut_chord(text); });
  rejects([&] { (void)format_shortcut_chord({static_cast<ShortcutKey>(999), ShortcutModifiers::None}); });
  rejects([&] { (void)format_shortcut_chord({ShortcutKey::K, static_cast<ShortcutModifiers>(32)}); });
  rejects<std::length_error>([&] { (void)parse_shortcut_chord(std::string(97, ' ')); });
}
void platforms_and_exact_modifiers() {
  const auto chord = parse_shortcut_chord("Primary+Shift+K");
  check(format_shortcut_chord(resolve_shortcut_chord(chord, ShortcutPlatform::Windows)) == "Ctrl+Shift+K",
        "Windows Primary is Ctrl");
  check(format_shortcut_chord(resolve_shortcut_chord(chord, ShortcutPlatform::Linux)) == "Ctrl+Shift+K",
        "Linux Primary is Ctrl policy");
  check(format_shortcut_chord(resolve_shortcut_chord(chord, ShortcutPlatform::MacOS)) == "Shift+Super+K",
        "Mac Primary is Super policy");
  const ShortcutProfile profile;
  const auto context = admitted();
  for (const auto platform : {ShortcutPlatform::Windows, ShortcutPlatform::Linux}) {
    command(evaluate_shortcut(profile, platform, edge(ShortcutKey::Z, true), context), "edit.undo");
    refused(evaluate_shortcut(profile, platform, edge(ShortcutKey::Z, true, false, true), context),
            ShortcutDecisionReason::Unbound);
    refused(evaluate_shortcut(profile, platform, edge(ShortcutKey::Z, false, false, false, true), context),
            ShortcutDecisionReason::Unbound);
    command(evaluate_shortcut(profile, platform, edge(ShortcutKey::Y, true), context), "edit.redo");
  }
  command(evaluate_shortcut(profile, ShortcutPlatform::MacOS,
      edge(ShortcutKey::Z, false, false, false, true), context), "edit.undo");
  refused(evaluate_shortcut(profile, ShortcutPlatform::MacOS,
      edge(ShortcutKey::Z, true), context), ShortcutDecisionReason::Unbound);
  auto explicit_control = compile_shortcuts({"analysis.measure=Ctrl+K"});
  command(evaluate_shortcut(explicit_control, ShortcutPlatform::MacOS,
      edge(ShortcutKey::K, true), context), "analysis.measure");
  refused(evaluate_shortcut(explicit_control, ShortcutPlatform::MacOS,
      edge(ShortcutKey::K, false, false, false, true), context), ShortcutDecisionReason::Unbound);
}
void inheritance_edits_unknowns() {
  const std::vector<std::string> input{
      "future.z= Ctrl?? = ", "analysis.measure=ctrlOrcmd+k", "future.a=", "file.save="};
  const auto profile = compile_shortcuts(input);
  check(profile.override_lines() == std::vector<std::string>{
      "file.save=", "analysis.measure=Primary+K", "future.z= Ctrl?? = ", "future.a="},
      "Known rows use registry order; unknown rows retain bytes/relative order");
  check(profile.inactive_rows() == std::vector<std::string>{"future.z= Ctrl?? = ", "future.a="},
        "Opaque inactive rows are owned losslessly");
  check(profile.diagnostics().size() == 2 &&
            profile.diagnostics()[0].kind == ShortcutDiagnosticKind::InactiveUnknown,
        "Unknown IDs produce nonfatal inactive diagnostics");
  auto same = assign_shortcut(profile, "analysis.measure", "Primary+K");
  check(same.status == ShortcutEditStatus::NoOp && !same.bindings_changed,
        "Equivalent canonical assignment is an explicit no-op");
  auto cleared = clear_shortcut(profile, "analysis.measure");
  check(cleared.status == ShortcutEditStatus::Changed && cleared.bindings_changed &&
            !cleared.profile.chord("analysis.measure", ShortcutPlatform::Windows), "Clear unbinds");
  auto reset = reset_shortcut(cleared.profile, "analysis.measure");
  check(reset.status == ShortcutEditStatus::Changed && reset.bindings_changed &&
            format_shortcut_chord(*reset.profile.chord("analysis.measure", ShortcutPlatform::Windows)) == "M",
        "Reset removes an override and inherits the default");
  auto already_default = reset_shortcut(reset.profile, "analysis.measure");
  check(already_default.status == ShortcutEditStatus::NoOp, "Reset without override is a no-op");
  const ShortcutProfile defaults;
  auto explicit_default = assign_shortcut(defaults, "file.open", "Primary+O");
  check(explicit_default.status == ShortcutEditStatus::Changed && !explicit_default.bindings_changed,
        "Storage change is distinct from effective-map change");
  auto clear_unbound = clear_shortcut(defaults, "app.jobs");
  check(clear_unbound.status == ShortcutEditStatus::Changed && !clear_unbound.bindings_changed,
        "Explicit unbind differs from inherited unbound state");
  check(clear_shortcut(clear_unbound.profile, "app.jobs").status == ShortcutEditStatus::NoOp,
        "Repeated explicit clear is a no-op");
  auto all = reset_shortcuts(profile);
  check(all.status == ShortcutEditStatus::Changed && all.profile.override_lines().empty() &&
            all.profile.inactive_rows().empty(), "Explicit Defaults also drops inactive overrides");
  rejects([&] { (void)assign_shortcut(profile, "future.z", "K"); });
  rejects([&] { (void)reset_shortcut(profile, "future.z"); });
  check(profile.override_lines()[2] == input[0], "Immutable source profile survives edits");
}
void conflicts() {
  const ShortcutProfile original;
  bool observed = false;
  try { (void)assign_shortcut(original, "analysis.measure", "Primary+S"); }
  catch (const ShortcutProfileError &error) {
    for (const auto &diagnostic : error.diagnostics())
      if (diagnostic.kind == ShortcutDiagnosticKind::Conflict) {
        check(diagnostic.command_id == "file.save" && diagnostic.other_command_id == "analysis.measure" &&
                  diagnostic.chord == "Primary+S" && diagnostic.other_chord == "Primary+S",
              "Conflict identifies both IDs and chords");
        check(diagnostic.platforms == std::vector<ShortcutPlatform>{ShortcutPlatform::Windows,
                  ShortcutPlatform::Linux, ShortcutPlatform::MacOS}, "Conflict reports all affected platforms");
        observed = true;
      }
  }
  check(observed && original.override_lines().empty(), "Inherited conflict refuses without partial apply");
  observed = false;
  try { (void)compile_shortcuts({"analysis.measure=Primary+K", "app.jobs=Ctrl+K"}); }
  catch (const ShortcutProfileError &error) {
    for (const auto &diagnostic : error.diagnostics())
      if (diagnostic.kind == ShortcutDiagnosticKind::Conflict) {
        check(diagnostic.platforms == std::vector<ShortcutPlatform>{ShortcutPlatform::Windows,
                  ShortcutPlatform::Linux}, "Logical aliases collide on a proper platform subset");
        observed = true;
      }
  }
  check(observed, "Complete portable table validation finds alias conflict");
  auto reassigned = compile_shortcuts({"file.save=", "analysis.measure=Primary+S",
                                      "future.command=Primary+S"});
  command(evaluate_shortcut(reassigned, ShortcutPlatform::Windows, edge(ShortcutKey::S, true), admitted()),
          "analysis.measure");
  rejects([&] { (void)compile_shortcuts({"file.open=Primary+O", "file.open=Primary+O"}); });
  rejects([&] { (void)compile_shortcuts({"future.x=", "future.x="}); });
}
void reservations() {
  for (const auto *key : {"Escape", "Enter", "Tab", "Space", "Backspace", "Delete", "Insert",
       "Home", "End", "PageUp", "PageDown", "Left", "Right", "Up", "Down"})
    rejects([&] { (void)compile_shortcuts({std::string("app.jobs=") + key}); });
  for (const auto *chord : {"Super+K", "Ctrl+Super+K", "Alt+Tab", "Alt+F4", "Alt+F2",
       "Ctrl+Alt+K", "Ctrl+Alt+Shift+F20", "Primary+Q", "Primary+H", "Primary+M",
       "Primary+Tab", "Primary+Space", "Primary+Alt+Escape", "Ctrl+Space", "Ctrl+Left"})
    rejects([&] { (void)compile_shortcuts({std::string("app.jobs=") + chord}); });
  // The stated exact deny-list does not silently expand Alt+F4 to Alt+Shift+F4.
  const auto profile = compile_shortcuts({"app.jobs=Alt+Shift+F4"});
  command(evaluate_shortcut(profile, ShortcutPlatform::Windows,
      edge(ShortcutKey::F4, false, true, true), admitted()), "app.jobs");
  bool linux_only = false;
  try { (void)compile_shortcuts({"app.jobs=Alt+F2"}); }
  catch (const ShortcutProfileError &error) {
    for (const auto &diagnostic : error.diagnostics())
      if (diagnostic.kind == ShortcutDiagnosticKind::Reserved)
        linux_only = diagnostic.platforms == std::vector<ShortcutPlatform>{ShortcutPlatform::Linux};
  }
  check(linux_only, "Linux Alt+F2 is a named subset reservation");
}
void focus_priority() {
  const ShortcutProfile profile;
  auto context = admitted();
  const auto m = edge(ShortcutKey::M);
  context.window_focused = false; context.rearmed = false; context.focus = ShortcutFocus::Capture;
  refused(evaluate_shortcut(profile, ShortcutPlatform::Windows, m, context), ShortcutDecisionReason::Unfocused);
  context.window_focused = true;
  refused(evaluate_shortcut(profile, ShortcutPlatform::Windows, m, context), ShortcutDecisionReason::Rearming);
  context.rearmed = true; context.gesture_active = true; context.local_key_claimed = true;
  refused(evaluate_shortcut(profile, ShortcutPlatform::Windows, m, context),
          ShortcutDecisionReason::Capture, ShortcutDisposition::LocalContext);
  context.focus = ShortcutFocus::Modal;
  refused(evaluate_shortcut(profile, ShortcutPlatform::Windows, m, context),
          ShortcutDecisionReason::Modal, ShortcutDisposition::LocalContext);
  for (const auto focus : {ShortcutFocus::Text, ShortcutFocus::Control}) {
    context.focus = focus;
    refused(evaluate_shortcut(profile, ShortcutPlatform::Windows, m, context),
            ShortcutDecisionReason::Control, ShortcutDisposition::LocalContext);
  }
  context.focus = ShortcutFocus::Canvas;
  refused(evaluate_shortcut(profile, ShortcutPlatform::Windows, m, context),
          ShortcutDecisionReason::Gesture, ShortcutDisposition::LocalContext);
  context.gesture_active = false;
  refused(evaluate_shortcut(profile, ShortcutPlatform::Windows, m, context),
          ShortcutDecisionReason::LocalClaim, ShortcutDisposition::LocalContext);
  context.local_key_claimed = false; context.focus = ShortcutFocus::None;
  refused(evaluate_shortcut(profile, ShortcutPlatform::Windows, m, context), ShortcutDecisionReason::NoWorkspace);
  context.focus = ShortcutFocus::Workspace;
  command(evaluate_shortcut(profile, ShortcutPlatform::Windows, m, context), "analysis.measure");
}
void edges_ambiguity_rearm() {
  const ShortcutProfile profile;
  auto context = admitted();
  auto m = edge(ShortcutKey::M);
  m.down_edge = false;
  refused(evaluate_shortcut(profile, ShortcutPlatform::Windows, m, context), ShortcutDecisionReason::NoEdge);
  m.down_edge = true; m.repeat = true;
  refused(evaluate_shortcut(profile, ShortcutPlatform::Windows, m, context), ShortcutDecisionReason::Repeat);
  m.repeat = false;
  context.rearmed = false;
  refused(evaluate_shortcut(profile, ShortcutPlatform::Windows, m, context), ShortcutDecisionReason::Rearming);
  context.rearmed = true;
  command(evaluate_shortcut(profile, ShortcutPlatform::Windows, m, context), "analysis.measure");
  refused(evaluate_shortcuts(profile, ShortcutPlatform::Windows,
      {m, edge(ShortcutKey::S, true)}, context), ShortcutDecisionReason::Ambiguous);
  context.availability["file.save"] = false;
  refused(evaluate_shortcuts(profile, ShortcutPlatform::Windows,
      {edge(ShortcutKey::S, true), m}, context), ShortcutDecisionReason::Ambiguous);
  command(evaluate_shortcuts(profile, ShortcutPlatform::Windows,
      {m, edge(ShortcutKey::S, true, false, true)}, context), "analysis.measure");
  command(evaluate_shortcuts(profile, ShortcutPlatform::Windows, {m, m}, context), "analysis.measure");
  refused(evaluate_shortcuts(profile, ShortcutPlatform::Windows, {}, context), ShortcutDecisionReason::NoEdge);
}
void availability_and_roi_local() {
  const ShortcutProfile defaults;
  auto context = admitted();
  context.availability.clear();
  refused(evaluate_shortcut(defaults, ShortcutPlatform::Windows, edge(ShortcutKey::M), context),
          ShortcutDecisionReason::Unavailable);
  context.availability["analysis.measure"] = false;
  refused(evaluate_shortcut(defaults, ShortcutPlatform::Windows, edge(ShortcutKey::M), context),
          ShortcutDecisionReason::Unavailable);
  context.availability["analysis.measure"] = true;
  command(evaluate_shortcut(defaults, ShortcutPlatform::Windows, edge(ShortcutKey::M), context), "analysis.measure");
  context = admitted(ShortcutFocus::ROIManager);
  const auto reassigned = compile_shortcuts({"edit.select_all=Primary+K", "app.jobs=Primary+A"});
  refused(evaluate_shortcut(reassigned, ShortcutPlatform::Windows, edge(ShortcutKey::A, true), context),
          ShortcutDecisionReason::ROIManagerSelectAll, ShortcutDisposition::LocalContext);
  refused(evaluate_shortcut(reassigned, ShortcutPlatform::MacOS,
      edge(ShortcutKey::A, false, false, false, true), context),
          ShortcutDecisionReason::ROIManagerSelectAll, ShortcutDisposition::LocalContext);
  context.local_key_claimed = true;
  refused(evaluate_shortcut(reassigned, ShortcutPlatform::Windows, edge(ShortcutKey::A, true), context),
          ShortcutDecisionReason::ROIManagerSelectAll, ShortcutDisposition::LocalContext);
  context.local_key_claimed = false;
  command(evaluate_shortcut(reassigned, ShortcutPlatform::Windows, edge(ShortcutKey::K, true), context),
          "edit.select_all");
  refused(evaluate_shortcut(defaults, ShortcutPlatform::Windows, edge(ShortcutKey::M), context),
          ShortcutDecisionReason::NoWorkspace);
  context.focus = ShortcutFocus::Canvas;
  command(evaluate_shortcut(reassigned, ShortcutPlatform::Windows, edge(ShortcutKey::A, true), context), "app.jobs");
}
void grammar_enums_and_caps() {
  for (const auto *line : {"no_dot=K", "Upper.id=K", "a..b=K", ".a.b=K", "a.b.=K",
       "a.2b=K", "a.b-c=K", "file.open", "file.open= ", "future.x=\t", "future.x=é"})
    rejects([&] { (void)compile_shortcuts({line}); });
  const ShortcutProfile profile;
  auto context = admitted();
  context.availability["future.x"] = true;
  rejects([&] { (void)evaluate_shortcut(profile, ShortcutPlatform::Windows, edge(ShortcutKey::M), context); });
  context = admitted(); context.focus = static_cast<ShortcutFocus>(999);
  rejects([&] { (void)evaluate_shortcut(profile, ShortcutPlatform::Windows, edge(ShortcutKey::M), context); });
  context = admitted();
  rejects([&] { (void)evaluate_shortcut(profile, static_cast<ShortcutPlatform>(999), edge(ShortcutKey::M), context); });
  rejects([&] { (void)evaluate_shortcut(profile, ShortcutPlatform::Windows, edge(static_cast<ShortcutKey>(-1)), context); });
  rejects([&] { (void)profile.bindings(static_cast<ShortcutPlatform>(999)); });
  rejects([&] { (void)profile.chord("future.x", ShortcutPlatform::Windows); });
  auto limits = ShortcutLimits{};
  limits.max_override_entries = 1;
  rejects<std::length_error>([&] { (void)compile_shortcuts({"future.a=", "future.b="}, limits); });
  limits = {}; limits.max_entry_bytes = 8;
  rejects<std::length_error>([&] { (void)compile_shortcuts({"future.a="}, limits); });
  limits = {}; limits.max_total_entry_bytes = 8;
  rejects<std::length_error>([&] { (void)compile_shortcuts({"future.a="}, limits); });
  limits = {}; limits.max_command_id_bytes = 7;
  rejects<std::length_error>([&] { (void)compile_shortcuts({"future.a="}, limits); });
  limits = {}; limits.max_chord_bytes = 2;
  rejects<std::length_error>([&] { (void)compile_shortcuts({"analysis.measure=Ctrl+K"}, limits); });
  limits = {}; limits.max_registry_commands = 17;
  rejects<std::length_error>([&] { (void)compile_shortcuts({}, limits); });
  limits = {}; limits.max_native_working_bytes = profile.working_bytes();
  check(compile_shortcuts({}, limits).working_bytes() == profile.working_bytes(), "Inclusive working cap");
  --limits.max_native_working_bytes;
  rejects<std::length_error>([&] { (void)compile_shortcuts({}, limits); });
  limits = {}; limits.max_override_entries = 1025;
  rejects([&] { (void)compile_shortcuts({}, limits); });
  limits = {}; limits.max_native_working_bytes = 0;
  rejects([&] { (void)compile_shortcuts({}, limits); });
  const auto exact_rhs = std::string(96, '?');
  check(compile_shortcuts({"future.a=" + exact_rhs}).inactive_rows()[0] == "future.a=" + exact_rhs,
        "Inclusive opaque chord byte cap");
  rejects<std::length_error>([&] { (void)compile_shortcuts({"future.a=" + exact_rhs + "?"}); });
  std::vector<std::string> many;
  for (int i = 0; i < 1024; ++i) many.push_back("future.c" + std::to_string(i) + "=");
  check(compile_shortcuts(many).inactive_rows() == many, "Inclusive override count cap");
  many.push_back("future.extra=");
  rejects<std::length_error>([&] { (void)compile_shortcuts(many); });
}
void detached_ownership_and_no_effects() {
  auto lines = std::vector<std::string>{"analysis.measure=Primary+K", "future.keep=Opaque ???"};
  const auto original_lines = lines;
  const auto profile = compile_shortcuts(lines);
  check(lines == original_lines, "Compilation does not rewrite borrowed input");
  lines[0] = "file.open=";
  auto returned = profile.override_lines();
  returned.clear();
  auto inactive = profile.inactive_rows(); inactive[0] = "future.changed=";
  auto table = profile.bindings(ShortcutPlatform::Windows); table[0].command_id = "other.id";
  auto diagnostics = profile.diagnostics(); diagnostics[0].command_id = "other.id";
  check(profile.override_lines() == original_lines && profile.inactive_rows()[0] == original_lines[1] &&
            profile.bindings(ShortcutPlatform::Windows)[0].command_id == "file.open" &&
            profile.diagnostics()[0].command_id == "future.keep", "Getters are detached owned values");
  auto context = admitted(); const auto availability = context.availability;
  const auto event = edge(ShortcutKey::K, true);
  command(evaluate_shortcut(profile, ShortcutPlatform::Windows, event, context), "analysis.measure");
  check(context.availability == availability && event.down_edge && event.ctrl,
        "Evaluation does not mutate inputs or invoke a callback");
  rejects([&] { (void)assign_shortcut(profile, "analysis.measure", "Primary+S"); });
  check(profile.override_lines() == original_lines, "Failed immutable edit leaves prior profile intact");
  static_assert(noexcept(std::declval<ShortcutProfile &>().swap(std::declval<ShortcutProfile &>())),
                "Live installation must use a nonthrowing ownership swap");
}
#ifdef IMAGEJXX_SHORTCUTS_TEST_MATERIALIZATION
void assignment_preflight_before_owned_staging() {
  const ShortcutProfile profile;
  const std::string command_id = "app.jobs";
  const std::string candidate = "Primary+Shift+LeftBracket";
  const auto original_lines = profile.override_lines();
  const auto before = detail::shortcut_test_materializations();
  auto limits = ShortcutLimits{};
  limits.max_native_working_bytes = 1; // Positive valid cap; candidate is grammatical.
  rejects<std::length_error>([&] { (void)assign_shortcut(profile, command_id, candidate, limits); });
  check(detail::shortcut_test_materializations() == before,
        "Working refusal must precede admission and owned candidate/profile staging");
  limits = {};
  limits.max_entry_bytes = command_id.size() + 1 + candidate.size() - 1;
  rejects<std::length_error>([&] { (void)assign_shortcut(profile, command_id, candidate, limits); });
  check(detail::shortcut_test_materializations() == before,
        "Complete entry refusal must precede owned staging, not merely final compilation");
  limits = {};
  limits.max_total_entry_bytes = command_id.size() + 1 + candidate.size() - 1;
  rejects<std::length_error>([&] { (void)assign_shortcut(profile, command_id, candidate, limits); });
  check(detail::shortcut_test_materializations() == before,
        "Prospective total-byte refusal must precede owned staging");
  const auto accepted = assign_shortcut(profile, command_id, candidate);
  const auto after = detail::shortcut_test_materializations();
  check(after[0] == before[0] + 1 && after[1] > before[1] && after[2] >= before[2] + 2,
        "Positive control observes admission, canonical text and proposed/compiled profile staging");
  check(accepted.status == ShortcutEditStatus::Changed && accepted.bindings_changed &&
            accepted.profile.override_lines() == std::vector<std::string>{command_id + "=" + candidate} &&
            profile.override_lines() == original_lines,
        "Admitted candidate preserves canonical behavior and immutable source");
  // The private counters observe these named logical boundaries, not allocator
  // calls, exception diagnostics, process/RSS bytes or arbitrary library work.
}
#endif
template <class Function> void group(const char *name, Function function) {
  function(); ++groups; std::cout << "pure-shortcuts " << name << " passed\n";
}
} // namespace
int main() {
  try {
    group("registry18-defaults10", registry);
    group("closed-parser-aliases", parser);
    group("primary-three-platforms-exact-modifiers", platforms_and_exact_modifiers);
    group("inherit-clear-reset-inactive-preservation", inheritance_edits_unknowns);
    group("complete-map-conflicts", conflicts);
    group("owned-reservations", reservations);
    group("focus-priority", focus_priority);
    group("edge-repeat-rearm-ambiguity", edges_ambiguity_rearm);
    group("availability-roi-local-shadow", availability_and_roi_local);
    group("grammar-enums-logical-caps", grammar_enums_and_caps);
    group("detached-inputs-no-execution", detached_ownership_and_no_effects);
#ifdef IMAGEJXX_SHORTCUTS_TEST_MATERIALIZATION
    group("assignment-preflight-before-owned-staging", assignment_preflight_before_owned_staging);
#endif
    std::cout << groups << " pure shortcut groups / " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
