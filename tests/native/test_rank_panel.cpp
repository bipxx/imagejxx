// Widget-level checks for the shared rank editor. Real ImGuiIO events drive
// the published diagnostic control points; no processing oracle, OS backend,
// installed launcher or private RankPanel state is substituted here.
#include "../../src/desktop/rank_panel.hpp"

#include <imgui_internal.h>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace imagejxx;
using imagejxx::desktop::Json;
using imagejxx::desktop::RankPanel;

namespace {
std::size_t checks = 0;
std::vector<std::string> cases;

void named(std::string name) { cases.push_back(std::move(name)); }
void check(bool condition, const char *message) {
  ++checks;
  if (!condition)
    throw std::runtime_error((cases.empty() ? "fixture" : cases.back()) + ": " + message);
}

bool position_is(PlanePosition position, int t, int c, int z) {
  return position.t == t && position.c == c && position.z == z;
}
constexpr std::array<RankFilterMethod, 4> methods{
    RankFilterMethod::Minimum, RankFilterMethod::Maximum,
    RankFilterMethod::Median, RankFilterMethod::Mean};
constexpr std::array<const char *, 4> names{"minimum", "maximum", "median", "mean"};

std::shared_ptr<Document> document(DatasetShape shape = {1, 1, 1, 7, 9, 1}) {
  Stack planes;
  for (int t = 0; t < shape.t; ++t)
    for (int c = 0; c < shape.c; ++c)
      for (int z = 0; z < shape.z; ++z) {
        Image plane(shape.x, shape.y, 1, PixelType::UInt16);
        for (int y = 0; y < shape.y; ++y)
          for (int x = 0; x < shape.x; ++x)
            plane.at(x, y) = 1 + x + 11 * y + 101 * z + 1001 * c + 10001 * t;
        planes.push_back(std::move(plane));
      }
  return std::make_shared<Document>(Dataset::from_planes(shape, std::move(planes)));
}

class Harness {
public:
  explicit Harness(std::shared_ptr<Document> value = document()) : owner(std::move(value)) {
    context_ = ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = {1600, 1200};
    io.DeltaTime = 1.f / 60.f;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    unsigned char *pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    check(pixels && width > 0 && height > 0, "ImGui font atlas was not built");
    current = owner->snapshot();
    callbacks.preview = [&](const ResolvedRankFilterCommand &command, std::uint64_t generation) {
      outside_panel();
      check(previews.empty() || generation > generations.back(),
            "Panel Preview generations did not strictly increase");
      previews.push_back(command);
      generations.push_back(generation);
      events.push_back("preview");
      if (throwPreview)
        throw std::runtime_error("Owned rank Preview submit failure");
    };
    callbacks.cancel_preview = [&] {
      outside_panel();
      ++cancellations;
      events.push_back("cancel_preview");
    };
    callbacks.apply = [&](const ResolvedRankFilterCommand &command) {
      outside_panel();
      check(!panel.visible(), "Rank Apply callback ran before the panel closed");
      applies.push_back(command);
      events.push_back("apply");
    };
    callbacks.show_preview_plane = [&](PlanePosition position) {
      outside_panel();
      shown.push_back(position);
      events.push_back("show_preview_plane");
      if (throwShow)
        throw std::runtime_error("Owned Show preview plane failure");
    };
    callbacks.preview_visible = [&](PlanePosition position) {
      outside_panel();
      observedCanvas.push_back(position);
      if (throwVisible)
        throw std::runtime_error("Owned Preview visibility failure");
      return staged;
    };
  }

  ~Harness() { ImGui::DestroyContext(context_); }
  Harness(const Harness &) = delete;
  Harness &operator=(const Harness &) = delete;

  void show(RankFilterMethod method = RankFilterMethod::Median) {
    current = owner->snapshot();
    panel.show(*current, method);
    frame();
    frame();
    check(panel.visible(), "Panel did not open after its bounded initial frames");
  }

  void frame() {
    check(++frames_ <= 2500, "Widget test exceeded its explicit frame bound");
    ImGui::SetCurrentContext(context_);
    ImGui::NewFrame();
    // Reading the ImGui stack observes callback placement; it does not edit
    // widget IDs, focus, panel fields or any input state.
    baselineDepth_ = context_->CurrentWindowStack.Size;
    drawing_ = true;
    try {
      panel.draw(current, busy, callbacks);
      check(callbackScopeValid_, "A host callback violated the panel Begin/End boundary");
      check(context_->CurrentWindowStack.Size == baselineDepth_,
            "Rank panel left an unmatched ImGui Begin/End scope");
      drawing_ = false;
      ImGui::Render();
    } catch (...) {
      drawing_ = false;
      ImGui::EndFrame();
      throw;
    }
  }

  Json state() const { return panel.diagnostics(); }
  Json point(const char *name) const { return state().at("controls").at(name); }
  Json target_point(const char *name) const {
    return state().at("operation_target").at("controls").at(name);
  }
  bool enabled(const char *name) const {
    return state().at("control_states").at(name).at("enabled").get<bool>();
  }

  void click(const Json &point) {
    check(point.is_array() && point.size() == 2 &&
              point[0].is_number() && point[1].is_number(),
          "Widget diagnostic did not provide a usable control point");
    const float x = point[0].get<float>(), y = point[1].get<float>();
    check(std::isfinite(x) && std::isfinite(y) && x >= 0 && y >= 0 &&
              x < 1600 && y < 1200,
          "Widget diagnostic control point is outside the bounded viewport");
    auto &io = ImGui::GetIO();
    io.AddMousePosEvent(x, y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    frame();
  }
  void click(const char *name) { click(point(name)); }
  void target_click(const char *name) { click(target_point(name)); }

  void key(ImGuiKey key) {
    auto &io = ImGui::GetIO();
    io.AddKeyEvent(key, true);
    frame();
    io.AddKeyEvent(key, false);
    frame();
    frame();
  }

  void replace(const Json &point, const std::string &text) {
    click(point);
    auto &io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    frame();
    io.AddKeyEvent(ImGuiKey_A, true);
    frame();
    io.AddKeyEvent(ImGuiKey_A, false);
    frame();
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    frame();
    if (text.empty())
      key(ImGuiKey_Backspace);
    else {
      io.AddInputCharactersUTF8(text.c_str());
      frame();
      frame();
    }
  }
  void radius(const std::string &text) {
    replace(point("radius"), text);
    check(state().at("radius_text") == text,
          "Actual ImGui text input did not replace the complete radius draft");
  }
  void select(RankFilterMethod method) {
    const auto name = rank_filter_method_name(method);
    click("method");
    check(state().at("method_popup").get<bool>(), "Actual method combo did not open");
    const auto control = std::string("method.") + name;
    click(control.c_str());
    check(panel.selected_method() == method && !state().at("method_popup").get<bool>(),
          "Actual method selection did not commit the requested selector choice");
  }
  void navigate(int t, int c, int z) {
    auto display = owner->snapshot().display();
    display.t = t;
    display.c = c;
    display.crosshair[2] = z;
    owner->set_display(display);
    current = owner->snapshot();
    frame();
    frame();
  }
  void unchanged(const DocumentSnapshot &before, std::size_t undo = 0,
                 std::size_t redo = 0) const {
    const auto after = owner->snapshot();
    check(after.shares_data(before) &&
              after.data_identity() == before.data_identity() &&
              after.revisions().matches(before.revisions(), Change::All) &&
              after.dirty() == before.dirty() && owner->undo_steps() == undo &&
              owner->redo_steps() == redo,
          "Widget-only editing changed the native source data/revisions/dirty/history");
  }
  void memory(RankFilterMethod method, double radius) const {
    const auto options = panel.remembered_options(method);
    check(options.method == method && options.radius == radius,
          "Session accepted-radius memory differs from the selected method's value");
  }

  std::shared_ptr<Document> owner;
  RankPanel panel;
  std::optional<DocumentSnapshot> current;
  RankPanel::Callbacks callbacks;
  std::vector<ResolvedRankFilterCommand> previews, applies;
  std::vector<std::uint64_t> generations;
  std::vector<PlanePosition> shown, observedCanvas;
  std::vector<std::string> events;
  int cancellations = 0;
  bool busy = false, throwPreview = false, throwShow = false;
  bool throwVisible = false, staged = false;

private:
  void outside_panel() {
    const bool valid = drawing_ && context_->WithinFrameScope &&
                       context_->CurrentWindowStack.Size == baselineDepth_;
    callbackScopeValid_ = callbackScopeValid_ && valid;
    check(valid,
          "Rank host callback ran inside a panel Begin/End scope or outside its draw");
  }
  ImGuiContext *context_ = nullptr;
  int baselineDepth_ = 0, frames_ = 0;
  bool drawing_ = false;
  bool callbackScopeValid_ = true;
};

void defaults_and_selector() {
  named("RP01_four_defaults_and_actual_method_selector");
  Harness h;
  const auto before = h.owner->snapshot();
  for (const auto method : methods) h.memory(method, 2);
  h.show();
  check(h.panel.selected_method() == RankFilterMethod::Median &&
            h.state().at("radius") == 2 && h.state().at("radius_units") == "pixels" &&
            !h.state().at("new_document").get<bool>(),
        "Default rank dialog is not Median with radius2 pixels");
  for (const auto method : methods) {
    h.select(method);
    check(h.state().at("draft_valid").get<bool>() && h.state().at("radius") == 2 &&
              h.enabled("ok") && h.enabled("preview"),
          "Selecting one of the four methods did not load a valid radius2 draft");
  }
  h.click("cancel");
  check(!h.panel.visible() && h.applies.empty() && h.previews.empty() && h.cancellations == 1,
        "Default dialog cancellation did not release once without Preview/Apply");
  for (const auto method : methods) h.memory(method, 2);
  h.unchanged(before);
}

void selector_uses_only_accepted_memory() {
  named("RP02_selector_discards_drafts_and_loads_only_accepted_method_memory");
  Harness h;
  h.show(RankFilterMethod::Mean);
  h.radius("4.25");
  h.click("ok");
  check(h.applies.size() == 1 && h.applies.back().options().method == RankFilterMethod::Mean &&
            h.applies.back().options().radius == 4.25,
        "First-dialog acceptance did not retain the selected method and fractional radius");
  h.memory(RankFilterMethod::Mean, 4.25);
  h.show(RankFilterMethod::Median);
  h.radius("7.5");
  h.select(RankFilterMethod::Mean);
  check(h.state().at("radius") == 4.25, "Selector did not restore accepted Mean memory");
  h.select(RankFilterMethod::Median);
  check(h.state().at("radius") == 2, "Unaccepted Median text leaked into session memory");
  h.radius("9.5");
  h.click("cancel");
  h.memory(RankFilterMethod::Median, 2);
  h.memory(RankFilterMethod::Mean, 4.25);
  h.show(RankFilterMethod::Mean);
  check(h.state().at("radius") == 4.25, "Control reset/reopen erased accepted session radius");
  h.click("cancel");
  Harness fresh;
  for (const auto method : methods) fresh.memory(method, 2);
}

void strict_radius_matrix() {
  for (std::size_t index = 0; index < methods.size(); ++index) {
    named(std::string("RP03_strict_radius_") + names[index]);
    Harness h;
    h.show(methods[index]);
    const auto before = h.owner->snapshot();
    std::vector<std::string> invalid{"", "nan", "inf", "-0.25", "2.5pixels", "1e999", "++2"};
    invalid.push_back(methods[index] == RankFilterMethod::Median ? "100.0001" : "1000.0001");
    for (const auto &text : invalid) {
      h.radius(text);
      check(!h.state().at("draft_valid").get<bool>() &&
                !h.state().at("error").get<std::string>().empty() &&
                !h.enabled("ok") && !h.enabled("preview") && h.state().at("radius").is_null(),
            "Invalid/nonfinite/incomplete/out-of-range radius remained actionable");
      h.click("ok");
      check(h.panel.visible() && h.applies.empty(), "Invalid radius was accepted by the actual OK button");
      h.memory(methods[index], 2);
    }
    const std::pair<const char *, double> valid[] = {
        {"0", 0}, {"2.625", 2.625}, {" +2.5 ", 2.5}, {"1e1", 10},
        {methods[index] == RankFilterMethod::Median ? "100" : "1000",
         methods[index] == RankFilterMethod::Median ? 100. : 1000.}};
    for (const auto &value : valid) {
      h.radius(value.first);
      check(h.state().at("draft_valid").get<bool>() && h.state().at("radius") == value.second &&
                h.enabled("ok") && h.enabled("preview") &&
                h.state().at("error").get<std::string>().empty(),
            "Valid full fractional/exponent/boundary radius did not recover the dialog");
      h.memory(methods[index], 2);
    }
    h.click("cancel");
    h.memory(methods[index], 2);
    h.unchanged(before);
  }
}

void accepted_memory_matrix() {
  for (std::size_t index = 0; index < methods.size(); ++index) {
    named(std::string("RP04_accept_commits_only_") + names[index]);
    Harness h;
    h.show(methods[index]);
    const auto before = h.owner->snapshot();
    h.radius("6.375");
    h.click("ok");
    check(!h.panel.visible() && h.applies.size() == 1 && h.cancellations == 1 &&
              h.applies.back().options().method == methods[index] &&
              h.applies.back().options().radius == 6.375 &&
              h.applies.back().target().document_id() == before.id() &&
              h.applies.back().target().destination() == OutputDestination::InPlace,
          "Actual OK acceptance did not emit one frozen typed command and one lane release");
    for (const auto method : methods) h.memory(method, method == methods[index] ? 6.375 : 2);
    h.unchanged(before);
    h.show(methods[index]);
    check(h.state().at("radius") == 6.375, "Same-session reopening lost accepted method memory");
    h.click("cancel");
  }
}

void cancel_escape_and_title_close() {
  for (const auto mode : {"cancel", "escape", "title_close"}) {
    named(std::string("RP05_first_dialog_") + mode + "_does_not_commit");
    Harness h;
    h.show(RankFilterMethod::Maximum);
    const auto before = h.owner->snapshot();
    h.radius("17.25");
    h.click("preview");
    check(h.previews.size() == 1 && h.previews.back().options().radius == 17.25,
          "Preview did not submit the unaccepted fractional draft");
    h.memory(RankFilterMethod::Maximum, 2);
    if (std::string(mode) == "cancel") h.click("cancel");
    else if (std::string(mode) == "escape") h.key(ImGuiKey_Escape);
    else h.click(h.state().at("close_point"));
    check(!h.panel.visible() && h.cancellations == 1 && h.applies.empty() &&
              h.state().at("exit_reason") == "cancel",
          "First-dialog Cancel/Escape/title close failed its single nondestructive release");
    h.memory(RankFilterMethod::Maximum, 2);
    h.frame();
    check(h.cancellations == 1, "Closed panel retried its lane release");
    h.show(RankFilterMethod::Maximum);
    check(h.state().at("radius") == 2, "Canceled radius draft survived reopening");
    h.click("cancel");
    h.unchanged(before);
  }
}

void combo_escape_isolation() {
  named("RP06_Escape_closes_method_popup_before_the_rank_panel");
  Harness h;
  h.show();
  h.radius("3.25");
  h.click("method");
  check(h.state().at("method_popup").get<bool>(), "Method popup fixture did not open");
  h.key(ImGuiKey_Escape);
  check(h.panel.visible() && !h.state().at("method_popup").get<bool>() &&
            h.cancellations == 0 && h.applies.empty(),
        "Popup Escape also canceled or accepted its enclosing rank dialog");
  h.key(ImGuiKey_Escape);
  check(!h.panel.visible() && h.cancellations == 1, "Second Escape did not cancel the rank dialog");
  h.memory(RankFilterMethod::Median, 2);
}

void legacy_stack_choices() {
  for (const auto choice : {"stack_yes", "stack_no", "stack_cancel"}) {
    named(std::string("RP07_legacy_") + choice + "_retains_first_acceptance");
    Harness h(document({1, 1, 3, 7, 9, 1}));
    h.navigate(0, 0, 1);
    h.show(RankFilterMethod::Mean);
    const auto captured = h.owner->snapshot();
    h.radius("7.25");
    h.click("new_document");
    h.click("preview");
    check(h.previews.size() == 1 && h.previews.back().target().position_count() == 1 &&
              h.previews.back().target().destination() == OutputDestination::InPlace &&
              position_is(h.previews.back().target().position(0), 0, 0, 1),
          "Legacy preview did not use its captured single Z plane");
    h.click("ok");
    check(h.panel.visible() && h.state().at("stack_prompt").get<bool>() &&
              h.applies.empty() && h.cancellations == 1,
          "First OK did not release Preview and enter the legacy stack prompt");
    h.memory(RankFilterMethod::Mean, 7.25);
    h.navigate(0, 0, 2);
    const auto navigated = h.owner->snapshot();
    h.click(choice);
    check(!h.panel.visible() && h.cancellations == 1,
          "Stack choice duplicated Preview release or failed to close");
    if (std::string(choice) == "stack_cancel") {
      check(h.applies.empty(), "Stack Cancel emitted Apply");
    } else {
      check(h.applies.size() == 1 && h.applies.back().options().radius == 7.25 &&
                h.applies.back().target().destination() == OutputDestination::NewDocument &&
                h.applies.back().target().source().revisions().matches(captured.revisions(), Change::All),
            "Stack choice lost accepted options or captured source revisions");
      const auto &target = h.applies.back().target();
      // Native resolution canonicalizes both raw AllZ and CurrentPlane intent
      // to compact Ranges. Check that public resolved representation and the
      // independently listed frozen TCZ membership, rather than raw UI enums.
      if (std::string(choice) == "stack_yes")
        check(target.scope() == TargetScope::Ranges && target.position_count() == 3 &&
                  position_is(target.position(0), 0, 0, 0) &&
                  position_is(target.position(1), 0, 0, 1) &&
                  position_is(target.position(2), 0, 0, 2) &&
                  target.contains({0, 0, 0}) && target.contains({0, 0, 1}) &&
                  target.contains({0, 0, 2}),
              "Stack Yes failed to select all captured Z planes");
      else
        check(target.scope() == TargetScope::Ranges && target.position_count() == 1 &&
                  position_is(target.position(0), 0, 0, 1) &&
                  !target.contains({0, 0, 0}) && !target.contains({0, 0, 2}),
              "Stack No silently retargeted the current navigated Z plane");
    }
    h.memory(RankFilterMethod::Mean, 7.25);
    h.unchanged(navigated);
    h.show(RankFilterMethod::Mean);
    check(h.state().at("radius") == 7.25 && !h.state().at("new_document").get<bool>(),
          "Reopening lost accepted radius or retained a prior dialog's output destination");
    h.click("cancel");
  }
}

void legacy_keyboard_choices() {
  const std::pair<ImGuiKey, const char *> choices[] = {
      {ImGuiKey_Y, "Y"}, {ImGuiKey_N, "N"},
      {ImGuiKey_Escape, "Escape"}, {ImGuiKey_Enter, "Enter"}};
  for (const auto &choice : choices) {
    named(std::string("RP08_stack_keyboard_") + choice.second);
    Harness h(document({1, 1, 3, 7, 9, 1}));
    h.show(RankFilterMethod::Minimum);
    h.radius("8.5");
    h.click("ok");
    check(h.state().at("stack_prompt").get<bool>(), "Keyboard stack fixture did not enter prompt");
    h.key(choice.first);
    check(!h.panel.visible() && h.cancellations == 1, "Stack keyboard choice did not close exactly once");
    if (choice.first == ImGuiKey_Escape)
      check(h.applies.empty(), "Stack Escape emitted an Apply command");
    else {
      check(h.applies.size() == 1, "Stack keyboard acceptance did not emit exactly one Apply");
      check(h.applies.back().target().position_count() == (choice.first == ImGuiKey_N ? 1 : 3),
            "Stack keyboard choice selected the wrong captured target");
    }
    h.memory(RankFilterMethod::Minimum, 8.5);
  }
}

void scientific_full_apply_contained_preview() {
  named("RP09_scientific_full_Apply_and_contained_Preview_survive_navigation");
  Harness h(document({2, 3, 4, 7, 9, 1}));
  h.navigate(1, 1, 2);
  h.owner->select(Selection("rectangle", {{1, 1}, {7, 6}}));
  h.show(RankFilterMethod::Maximum);
  const auto captured = h.owner->snapshot();
  const auto capturedUndo = h.owner->undo_steps(), capturedRedo = h.owner->redo_steps();
  // A transient active selection advances Selection, without a pixel/metadata
  // history edit or dirty identity. Freeze that fixture before panel actions.
  check(capturedUndo == 0 && capturedRedo == 0 && !captured.dirty(),
        "Selection-only fixture unexpectedly created history or dirtied its source");
  check(h.state().at("scientific_targets").get<bool>(), "Scientific source used the legacy stack dialog");
  h.target_click("t.all");
  h.target_click("c.all");
  h.target_click("z.all");
  h.target_click("roi.positioned_rois");
  h.click("new_document");
  h.radius("2.75");
  check(h.state().at("operation_target").at("selected_plane_count") == 24 &&
            h.state().at("preview_position") == Json::array({1, 1, 2}),
        "Full scientific target lost its count or contained captured preview choice");
  h.click("preview");
  check(h.previews.size() == 1 && h.previews.back().target().position_count() == 1 &&
            h.previews.back().target().destination() == OutputDestination::InPlace &&
            h.previews.back().target().scope() == TargetScope::Positions &&
            h.previews.back().target().roi_policy() == RoiPolicy::PositionedRois &&
            position_is(h.previews.back().target().position(0), 1, 1, 2),
        "Scientific Preview substituted the complete Apply target or lost ROI/captured TCZ");
  const auto generation = h.generations.back();
  h.navigate(0, 0, 0);
  const auto navigated = h.owner->snapshot();
  check(navigated.shares_data(captured) &&
            navigated.data_identity() == captured.data_identity() &&
            navigated.revisions().matches(captured.revisions(), Change::Data) &&
            navigated.revisions().display == captured.revisions().display + 1 &&
            navigated.dirty() == captured.dirty() &&
            h.owner->undo_steps() == capturedUndo && h.owner->redo_steps() == capturedRedo,
        "Intentional fresh navigation changed more than the expected display revision");
  check(h.panel.visible() && h.previews.size() == 1 && h.generations.back() == generation &&
            h.state().at("operation_target").at("canvas_position") == Json::array({0, 0, 0}) &&
            h.state().at("preview_position") == Json::array({1, 1, 2}),
        "Fresh navigation retargeted or unnecessarily resubmitted captured Preview");
  h.target_click("preview.show");
  check(h.shown.size() == 1 && position_is(h.shown.back(), 1, 1, 2),
        "Show preview plane callback did not identify the contained selected plane");
  h.target_click("preview.next");
  check(h.previews.size() == 2 && h.previews.back().target().position_count() == 1 &&
            position_is(h.previews.back().target().position(0), 1, 1, 3),
        "Preview Next did not submit the next contained plane only");
  h.click("ok");
  check(!h.panel.visible() && h.applies.size() == 1 && h.cancellations == 1,
        "Scientific Apply entered a legacy prompt or did not close/release exactly once");
  const auto &apply = h.applies.back();
  check(apply.options().method == RankFilterMethod::Maximum && apply.options().radius == 2.75 &&
            apply.target().scope() == TargetScope::Ranges && apply.target().position_count() == 24 &&
            apply.target().destination() == OutputDestination::NewDocument &&
            apply.target().roi_policy() == RoiPolicy::PositionedRois &&
            apply.target().contains({0, 0, 0}) && apply.target().contains({1, 2, 3}) &&
            apply.target().source().revisions().matches(captured.revisions(), Change::All),
        "Contained Preview or canvas navigation replaced the complete frozen Apply command");
  check(h.events.size() >= 2 && h.events[h.events.size() - 2] == "cancel_preview" &&
            h.events.back() == "apply", "Apply callback ran before release of its Preview lane");
  h.memory(RankFilterMethod::Maximum, 2.75);
  h.unchanged(navigated, capturedUndo, capturedRedo);
}

void scientific_range_and_preview_ordinal() {
  named("RP10_scientific_range_and_preview_ordinal_use_committed_widget_input");
  Harness h(document({2, 3, 4, 7, 9, 1}));
  h.show();
  h.target_click("c.range");
  h.replace(h.target_point("c.first"), "2");
  check(!h.state().at("draft_valid").get<bool>(), "Uncommitted range edit left Apply actionable");
  h.key(ImGuiKey_Enter);
  check(h.panel.visible() && h.applies.empty() && h.state().at("draft_valid").get<bool>(),
        "Range-field Enter accepted the enclosing editor or failed to commit the range");
  h.target_click("z.all");
  h.target_click("t.all");
  check(h.state().at("operation_target").at("selected_plane_count") == 16,
        "Committed C2-3, all Z/T target has an unexpected plane count");
  h.replace(h.target_point("preview.ordinal"), "17");
  h.key(ImGuiKey_Enter);
  check(h.panel.visible() && !h.state().at("draft_valid").get<bool>() &&
            !h.enabled("ok") && h.applies.empty(),
        "Out-of-target preview ordinal was accepted or applied");
  h.replace(h.target_point("preview.ordinal"), "16");
  h.key(ImGuiKey_Enter);
  check(h.panel.visible() && h.state().at("draft_valid").get<bool>() &&
            h.state().at("preview_position") == Json::array({1, 2, 3}) && h.applies.empty(),
        "Valid preview ordinal did not recover to the final contained TCZ plane");
  h.click("preview");
  check(h.previews.size() == 1 && position_is(h.previews.back().target().position(0), 1, 2, 3),
        "Explicit Preview ordinal did not drive the actual typed Preview callback");
  h.click("ok");
  check(h.applies.size() == 1 && h.applies.back().target().position_count() == 16 &&
            !h.applies.back().target().contains({0, 0, 0}) &&
            h.applies.back().target().contains({0, 1, 0}),
        "Explicit contained preview collapsed or expanded the complete Apply range");
}

void preview_lifecycle_and_errors() {
  named("RP11_Preview_toggle_invalid_draft_error_and_generation_lifecycle");
  Harness h;
  h.show(RankFilterMethod::Mean);
  const auto before = h.owner->snapshot();
  h.click("preview");
  check(h.previews.size() == 1 && h.state().at("preview_requested").get<bool>(),
        "Preview checkbox did not issue the typed callback");
  h.panel.setPreviewError("Owned asynchronous Preview failure");
  h.frame();
  check(h.state().at("preview_error") == "Owned asynchronous Preview failure",
        "Public asynchronous error report did not reach the visible panel");
  h.radius("3.5");
  check(h.previews.size() == 2 && h.previews.back().options().radius == 3.5 &&
            h.state().at("preview_error").get<std::string>().empty(),
        "Valid changed draft did not supersede Preview or clear its old error");
  h.radius("bad");
  check(h.cancellations == 1 && !h.state().at("preview_requested").get<bool>() &&
            h.previews.size() == 2 && !h.enabled("ok"),
        "Invalid draft retained its old Preview lane or submitted invalid work");
  h.radius("4.5");
  check(h.previews.size() == 3 && h.previews.back().options().radius == 4.5,
        "Correcting an invalid radius failed to restore the requested Preview");
  h.click("preview");
  check(h.cancellations == 2 && !h.state().at("preview").get<bool>() &&
            !h.state().at("preview_requested").get<bool>(),
        "Unchecking Preview did not release exactly once");
  h.click("cancel");
  check(h.cancellations == 2 && h.applies.empty(), "Final Cancel repeated an already released Preview");
  h.memory(RankFilterMethod::Mean, 2);
  h.unchanged(before);
}

void synchronous_preview_failure() {
  named("RP12_throwing_Preview_submit_releases_partial_work_outside_BeginEnd");
  Harness h;
  h.show();
  h.throwPreview = true;
  h.click("preview");
  check(h.panel.visible() && h.previews.size() == 1 && h.cancellations == 1 &&
            !h.state().at("preview_requested").get<bool>() &&
            h.state().at("preview_error") == "Owned rank Preview submit failure",
        "Synchronous Preview failure retained its partial lane or escaped the panel error path");
  h.throwPreview = false;
  h.radius("3.25");
  check(h.previews.size() == 2 && h.state().at("preview_requested").get<bool>() &&
            h.state().at("preview_error").get<std::string>().empty(),
        "Changed valid input did not recover from synchronous Preview failure");
  h.click("cancel");
  check(h.cancellations == 2 && h.applies.empty(), "Recovered Preview did not release once on Cancel");
}

void scientific_host_callback_failures() {
  named("RP13_scientific_visibility_and_show_errors_stay_outside_BeginEnd");
  Harness h(document({2, 2, 2, 7, 9, 1}));
  h.show();
  h.throwVisible = true;
  h.frame();
  check(h.panel.visible() && h.state().at("preview_error") == "Owned Preview visibility failure",
        "Visibility callback error escaped or closed the rank editor");
  h.throwVisible = false;
  h.throwShow = true;
  h.target_click("preview.show");
  check(h.shown.size() == 1 && h.panel.visible() &&
            h.state().at("preview_error") == "Owned Show preview plane failure",
        "Show preview plane callback error was not contained after ImGui::End");
  h.click("cancel");
  check(h.applies.empty() && h.cancellations == 1, "Host callback error caused publication or duplicate release");
}

void stale_missing_foreign_close() {
  for (const auto mode : {"metadata", "selection", "missing", "foreign"}) {
    named(std::string("RP14_") + mode + "_source_closes_without_Apply");
    Harness h;
    h.show(RankFilterMethod::Maximum);
    h.radius("5.5");
    h.click("preview");
    if (std::string(mode) == "metadata") {
      auto edit = h.owner->begin_edit();
      edit.set_property("owned_intervening_edit", std::int64_t(1));
      h.owner->commit(edit);
      h.current = h.owner->snapshot();
    } else if (std::string(mode) == "selection") {
      h.owner->select(Selection("point", {{2, 3}}));
      h.current = h.owner->snapshot();
    } else if (std::string(mode) == "missing") h.current.reset();
    else h.current = document()->snapshot();
    const auto protected_state = h.owner->snapshot();
    const auto history = h.owner->undo_steps();
    h.frame();
    check(!h.panel.visible() && h.cancellations == 1 && h.applies.empty() &&
              h.state().at("exit_reason") == "source_changed",
          "Stale/missing/foreign source retained its panel or emitted Apply");
    h.frame();
    check(h.cancellations == 1, "Repeated stale draw retried its Preview release");
    h.memory(RankFilterMethod::Maximum, 2);
    h.unchanged(protected_state, history);
  }
}

void busy_prevents_acceptance() {
  named("RP15_outstanding_Apply_disables_acceptance_without_committing_memory");
  Harness h;
  h.show(RankFilterMethod::Mean);
  h.radius("9.25");
  h.busy = true;
  h.frame();
  check(!h.enabled("ok") && !h.enabled("new_document"),
        "Outstanding Apply left rank acceptance or destination controls enabled");
  h.click("new_document");
  check(!h.state().at("new_document").get<bool>(), "Busy rank accepted an output destination change");
  h.click("ok");
  h.key(ImGuiKey_Enter);
  check(h.panel.visible() && h.applies.empty(), "Busy rank accepted by button or Enter");
  h.memory(RankFilterMethod::Mean, 2);
  h.click("cancel");
  check(!h.panel.visible() && h.cancellations == 1, "Busy rank did not permit nondestructive Cancel");
  h.memory(RankFilterMethod::Mean, 2);
}

void unavailable_preview_callback() {
  named("RP16_missing_Preview_callback_reports_error_and_Cancel_releases_once");
  Harness h;
  h.callbacks.preview = {};
  h.show();
  h.click("preview");
  check(h.panel.visible() && h.previews.empty() &&
            h.state().at("preview_error") == "Preview is unavailable" &&
            !h.state().at("preview_requested").get<bool>(),
        "Missing Preview callback silently accepted unavailable work");
  h.click("cancel");
  check(!h.panel.visible() && h.cancellations == 1 && h.applies.empty(),
        "Unavailable Preview lost the panel's one required exit release");
}

void single_plane_destination_and_preview() {
  named("RP17_NewDocument_Apply_never_changes_contained_Preview_destination");
  Harness h;
  h.show(RankFilterMethod::Mean);
  const auto before = h.owner->snapshot();
  h.radius("3.125");
  h.click("new_document");
  check(h.state().at("new_document").get<bool>(), "Actual Create new image checkbox did not toggle");
  h.click("preview");
  check(h.previews.size() == 1 &&
            h.previews.back().target().destination() == OutputDestination::InPlace &&
            h.previews.back().target().document_id() == before.id(),
        "Create new image incorrectly gave Preview a new-owner destination");
  h.click("new_document");
  h.click("new_document");
  check(h.previews.size() == 3 &&
            h.previews[1].target().destination() == OutputDestination::InPlace &&
            h.previews[2].target().destination() == OutputDestination::InPlace,
        "Destination changes failed to keep every Preview source-bound and in place");
  h.click("ok");
  check(h.applies.size() == 1 &&
            h.applies.back().target().destination() == OutputDestination::NewDocument &&
            h.applies.back().target().position_count() == 1 &&
            h.applies.back().options().radius == 3.125 && h.cancellations == 1,
        "Single-plane acceptance lost captured NewDocument intent or duplicated release");
  h.unchanged(before);
  h.show(RankFilterMethod::Mean);
  check(h.state().at("radius") == 3.125 && !h.state().at("new_document").get<bool>(),
        "Destination leaked across shows or accepted radius was reset with destination");
  h.click("cancel");
}
void despeckle_presets() {
  {
    named("RP18_despeckle_single_plane_has_no_parameter_dialog");Harness h;
    h.panel.show_despeckle(*h.current);h.frame();
    check(!h.panel.visible() && h.applies.size()==1 && h.applies[0].options().radius==1.,"Single-plane Despeckle did not apply directly");
    check(h.panel.remembered_options(RankFilterMethod::Median).radius==2.,"Despeckle changed accepted median radius memory");
  }
  {
    named("RP19_despeckle_stack_prompt_and_cancel_preserve_memory");Harness h(document({1,1,3,7,9,1}));
    h.panel.show_despeckle(*h.current);h.frame();h.frame();
    check(h.state().at("stack_prompt").get<bool>() && !h.state().at("controls").contains("radius"),"Despeckle exposed a parameter dialog");
    h.click("stack_yes");check(h.applies.size()==1 && h.applies[0].target().position_count()==3,"Despeckle stack target collapsed");
    h.panel.show_despeckle(*h.current);h.frame();h.frame();h.click("stack_cancel");
    check(h.applies.size()==1 && h.panel.remembered_options(RankFilterMethod::Median).radius==2.,"Cancelled preset applied or changed median memory");
  }
  {
    named("RP20_despeckle_scientific_targets_keep_fixed_radius");Harness h(document({2,2,3,7,9,1}));
    h.panel.show_despeckle(*h.current);h.frame();h.frame();
    check(!h.state().at("controls").contains("radius") && !h.state().at("controls").contains("method"),"Scientific preset exposed rank parameters");
    h.target_click("t.all");h.target_click("c.all");h.target_click("z.all");h.click("preview");
    check(!h.previews.empty() && h.previews.back().options().radius==1.,"Despeckle preview lost fixed radius");
    h.click("ok");check(h.applies.size()==1 && h.applies[0].target().position_count()==12 && h.applies[0].options().radius==1.,"Scientific Despeckle lost captured full target");
    check(h.panel.remembered_options(RankFilterMethod::Median).radius==2.,"Scientific Despeckle changed rank memory");
  }
}
void outlier_controls_and_memory() {
  {
    named("RP21_outlier_parameters_preview_and_independent_memory");
    Harness h(document({1,1,3,7,9,1}));h.panel.show_outliers(*h.current);h.frame();
    check(!h.state().at("controls").contains("method") && h.state().at("controls").contains("threshold"),"Outlier parameter controls missing");
    h.replace(h.point("radius"),"1.75");h.replace(h.point("threshold"),"60.25");
    h.click("which");h.click("outlier.dark");h.click("preview");
    check(h.previews.back().outlier_options()->threshold==60.25 && h.previews.back().outlier_options()->which=="dark","Outlier preview options changed");
    h.click("ok");check(h.state().at("stack_prompt")==true,"Outlier legacy stack prompt missing");
    h.click("stack_cancel");check(h.applies.empty(),"Cancelled outliers dispatched Apply");
    check(h.panel.remembered_options(RankFilterMethod::Median).radius==2.,"Outliers changed Median memory");
    h.panel.show_outliers(*h.current);h.frame();check(h.state().at("radius_text")=="1.75" && h.state().at("threshold_text")=="60.25" && h.state().at("which_outliers")=="dark","Outlier accepted memory lost");
    h.replace(h.point("threshold"),"-1");check(!h.enabled("ok"),"Invalid outlier threshold enabled Apply");h.click("cancel");
  }
  {
    named("RP22_scientific_outlier_full_target_and_preview");
    Harness h(document({2,2,3,7,9,1}));h.panel.show_outliers(*h.current);h.frame();
    h.target_click("t.all");h.target_click("c.all");h.target_click("z.all");h.click("new_document");h.click("preview");h.click("ok");
    check(h.applies.size()==1 && h.applies.back().target().position_count()==12 && h.applies.back().outlier_options().has_value() && h.applies.back().target().destination()==OutputDestination::NewDocument && h.previews.back().target().destination()==OutputDestination::InPlace,"Scientific outlier target changed");
  }
}
void top_hat_controls() {
 {
  named("RP23_top_hat_independent_memory_and_stack_cancel");Harness h(document({1,1,3,7,9,1}));
  h.panel.show_top_hat(*h.current);h.frame();h.frame();
  check(!h.state().at("controls").contains("method") && h.state().at("controls").contains("light_background"),"Top Hat controls missing");
  h.replace(h.point("radius"),"1.75");h.click("light_background");h.click("dont_subtract");h.click("preview");
  check(h.previews.back().top_hat_options()->dont_subtract && h.previews.back().top_hat_options()->light_background,"Top Hat preview lost flags");
  h.click("ok");h.click("stack_cancel");check(h.applies.empty(),"Cancelled Top Hat applied");
  h.panel.show_top_hat(*h.current);h.frame();
  check(h.state().at("radius_text")=="1.75" && h.state().at("dont_subtract")==true,"Top Hat accepted memory lost");
  check(h.panel.remembered_options(RankFilterMethod::Minimum).radius==2.,"Top Hat changed Minimum memory");
  h.replace(h.point("radius"),"1001");check(!h.enabled("ok"),"Invalid Top Hat enabled Apply");h.click("cancel");
 }
 {
  named("RP24_top_hat_scientific_new_document_and_contained_preview");Harness h(document({2,2,3,7,9,1}));
  h.panel.show_top_hat(*h.current);h.frame();h.frame();h.target_click("t.all");h.target_click("c.all");h.target_click("z.all");
  h.click("new_document");h.click("preview");h.click("ok");
  check(h.applies.size()==1 && h.applies.back().target().position_count()==12 && h.applies.back().top_hat_options().has_value(),"Top Hat full target lost");
  check(h.applies.back().target().destination()==OutputDestination::NewDocument && h.previews.back().target().destination()==OutputDestination::InPlace,"Top Hat output/preview destination changed");
 }
}
std::shared_ptr<Document> nan_document(DatasetShape shape) {
  Stack planes;
  for(std::size_t i=0;i<shape.plane_count();++i) {
    Image image(shape.x,shape.y,1,PixelType::Float32);image.pixels.assign(image.pixels.size(),7.);
    image.at(2,2)=float32_source_sample(0xffc12345u,FloatingSamplePolicy::PreserveQuietNaNFloat32);
    planes.push_back(std::move(image));
  }
  return std::make_shared<Document>(Dataset::from_planes(shape,std::move(planes),{},FloatingSamplePolicy::PreserveQuietNaNFloat32));
}
void remove_nans_controls() {
 {
  named("RP25_remove_nans_memory_and_stack_cancel");Harness h(nan_document({1,1,3,7,9,1}));
  h.panel.show_remove_nans(*h.current);h.frame();h.frame();
  check(h.state().at("draft_valid") && h.state().at("preset")=="remove_nans","Quiet-NaN editor not admitted");
  check(!h.state().at("controls").contains("method") && !h.state().at("controls").contains("light_background") && !h.state().at("controls").contains("threshold"),"Remove NaNs shows unrelated controls");
  h.replace(h.point("radius"),"1.75");h.click("preview");
  check(h.previews.back().remove_nans_options()->radius==1.75 && h.previews.back().parameters().size()==1,"Remove NaNs preview lost radius-only intent");
  h.click("ok");h.click("stack_cancel");check(h.applies.empty(),"Stack Cancel applied Remove NaNs");
  h.panel.show_remove_nans(*h.current);h.frame();
  check(h.state().at("radius_text")=="1.75" && h.panel.remembered_options(RankFilterMethod::Median).radius==2.,"Remove NaNs memory leaked into Median");
  h.replace(h.point("radius"),"100.1");check(!h.enabled("ok"),"Invalid Remove NaNs radius enabled Apply");h.click("cancel");
 }
 {
  named("RP26_remove_nans_scientific_full_target_and_contained_preview");Harness h(nan_document({2,2,3,7,9,1}));
  h.panel.show_remove_nans(*h.current);h.frame();h.frame();h.target_click("t.all");h.target_click("c.all");h.target_click("z.all");
  h.click("new_document");h.click("preview");h.click("ok");
  check(h.applies.size()==1 && h.applies.back().target().position_count()==12 && h.applies.back().remove_nans_options().has_value(),"Remove NaNs full target lost");
  check(h.applies.back().target().destination()==OutputDestination::NewDocument && h.previews.back().target().position_count()==1 && h.previews.back().target().destination()==OutputDestination::InPlace,"Remove NaNs preview replaced full Apply target");
 }
 {
  named("RP27_remove_nans_rejects_integer_source");Harness h(document());h.panel.show_remove_nans(*h.current);h.frame();h.frame();
  check(!h.state().at("draft_valid") && !h.enabled("ok") && !h.enabled("preview"),"Remove NaNs admitted integer source");h.click("cancel");
 }
}

} // namespace

int main() {
  try {
    defaults_and_selector();
    selector_uses_only_accepted_memory();
    strict_radius_matrix();
    accepted_memory_matrix();
    cancel_escape_and_title_close();
    combo_escape_isolation();
    legacy_stack_choices();
    legacy_keyboard_choices();
    scientific_full_apply_contained_preview();
    scientific_range_and_preview_ordinal();
    preview_lifecycle_and_errors();
    synchronous_preview_failure();
    scientific_host_callback_failures();
    stale_missing_foreign_close();
    busy_prevents_acceptance();
    unavailable_preview_callback();
    single_plane_destination_and_preview();
    despeckle_presets();
    outlier_controls_and_memory();
    top_hat_controls();
    remove_nans_controls();
    check(cases.size() == 43, "Rank panel case membership changed unexpectedly");
    for (const auto &name : cases) std::cout << "PASS " << name << '\n';
    std::cout << "Rank ImGui panel: " << cases.size() << " cases, " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
