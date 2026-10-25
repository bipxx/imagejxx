#include "channel_display_panel.hpp"
#include "../native/floating_charconv.hpp"
#include "rendering.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

namespace imagejxx::desktop {
namespace {
bool whitespace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
double bound(const std::string &text, const char *label) {
  std::string_view value(text);
  while (!value.empty() && whitespace(value.front())) value.remove_prefix(1);
  while (!value.empty() && whitespace(value.back())) value.remove_suffix(1);
  if (!value.empty() && value.front() == '+') {
    value.remove_prefix(1);
    if (!value.empty() && (value.front() == '+' || value.front() == '-'))
      throw std::invalid_argument(std::string(label) + " must have a single numeric sign");
  }
  double result = 0;
  const auto parsed = detail::floating_from_chars(value.data(), value.data()+value.size(),
                                                   result);
  if (value.empty() || parsed.ec != std::errc{} ||
      parsed.ptr != value.data()+value.size() || !std::isfinite(result))
    throw std::invalid_argument(std::string(label) + " must be a finite raw intensity");
  return result;
}
std::string text(double value) {
  char buffer[64];
  const auto converted = detail::floating_to_chars_general17(
      buffer, buffer+sizeof(buffer), value);
  if (converted.ec != std::errc{})
    throw std::runtime_error("Cannot format a channel display bound");
  return {buffer, converted.ptr};
}
const char *modeName(ChannelDisplayMode mode) {
  switch (mode) {
  case ChannelDisplayMode::Color: return "Color";
  case ChannelDisplayMode::Grayscale: return "Grayscale";
  case ChannelDisplayMode::Composite: return "Composite";
  }
  throw std::invalid_argument("Unknown channel display mode");
}
const char *displayName(const ChannelDisplayState &state) {
  if (state.mode == ChannelDisplayMode::Composite) {
    if (state.projection == ChannelDisplayProjection::Max) return "Composite Max";
    if (state.projection == ChannelDisplayProjection::Min) return "Composite Min";
  }
  return modeName(state.mode);
}
} // namespace

void ChannelDisplayPanel::open() {
  open_ = focus_ = true;
  exitReason_.clear();
}
void ChannelDisplayPanel::close() {
  open_ = focus_ = false;
  focusField_ = 0;
  exitReason_ = "close";
  resetControls();
}
bool ChannelDisplayPanel::inputActive() const {
  if (!open_) return false;
  const auto *context = ImGui::GetCurrentContext();
  const auto active = context ? context->ActiveId : 0;
  return windowFocused_ ||
      (active && (active == minimumId_ || active == maximumId_));
}
void ChannelDisplayPanel::resetControls() {
  controls_ = Json::object(); controlRects_ = Json::object();
  controlStates_ = Json::object(); windowFocused_ = false;
  modeLabel_.clear();
}
void ChannelDisplayPanel::control(const std::string &name, bool enabled,
                                  std::optional<float> input_width) {
  const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
  const auto &context = *ImGui::GetCurrentContext();
  const bool actualEnabled = enabled &&
      !(context.LastItemData.ItemFlags & ImGuiItemFlags_Disabled);
  const bool admitted = actualEnabled && ImGui::IsItemVisible();
  const float pointRight = input_width ? std::min(b.x, a.x+*input_width) : b.x;
  const Json point = {(a.x+pointRight)*.5f, (a.y+b.y)*.5f};
  const Json rect = {a.x,a.y,b.x,b.y};
  controls_[name] = point; controlRects_[name] = rect;
  controlStates_[name] = {{"id",ImGui::GetItemID()}, {"rect",rect}, {"point",point},
      {"enabled",actualEnabled}, {"admitted",admitted},
      {"keyboard_admitted",admitted && ImGui::IsItemFocused()},
      {"focused",ImGui::IsItemFocused()}, {"active",ImGui::IsItemActive()},
      {"hovered",ImGui::IsItemHovered()}, {"visible",ImGui::IsItemVisible()}};
}
void ChannelDisplayPanel::parseRangeDraft() {
  draftValid_ = false; draftRange_.reset(); rangeError_.clear();
  try {
    const std::array<double,2> range{bound(minimumText_, "Minimum"), bound(maximumText_, "Maximum")};
    if (range[0] > range[1])
      throw std::invalid_argument("Minimum must be less than or equal to maximum");
    draftRange_ = range; draftValid_ = true;
  } catch (const std::invalid_argument &error) {
    rangeError_ = error.what();
  }
}
void ChannelDisplayPanel::updateRangeDraft(std::uint64_t owner, int channel,
    std::optional<std::array<double,2>> range) {
  if (rangeInitialized_ && !forceRangeRebase_ && rangeOwner_ == owner &&
      rangeChannel_ == channel && cachedRange_.has_value()==range.has_value() &&
      (!range || sameDisplayRange(*cachedRange_,*range)))
    return;
  rangeInitialized_ = true; forceRangeRebase_ = false;
  rangeOwner_ = owner; rangeChannel_ = channel; cachedRange_ = range;
  // Reload only our own active text widget when its real source range changes.
  // Keep the same field focused; unrelated widgets retain their keyboard state.
  const auto active = ImGui::GetActiveID();
  if (active && (active == minimumId_ || active == maximumId_)) {
    focusField_ = active == minimumId_ ? 1 : 2;
    ImGui::ClearActiveID();
  }
  minimumText_ = range ? text((*range)[0]) : "";
  maximumText_ = range ? text((*range)[1]) : "";
  if (range && std::isfinite((*range)[0]) && std::isfinite((*range)[1])) parseRangeDraft();
  else { draftRange_.reset(); draftValid_ = false; rangeError_.clear(); }
}

void ChannelDisplayPanel::draw(const DocumentSnapshot *current, bool busy,
    const Enable &enable, const Apply &apply, const SelectChannel &select_channel,
    const LoadLut &load_lut, const Auto &auto_contrast) {
  resetControls(); busy_ = busy;
  if (!open_) return;
  // This value owns the frame's capture only until draw returns. No callback
  // reads a pointer into a mutable frontend adapter or retains a panel borrow.
  const std::optional<DocumentSnapshot> source = current
      ? std::optional<DocumentSnapshot>(*current) : std::nullopt;
  std::optional<ChannelDisplayState> state;
  sourceValid_ = bool(source); supported_ = false; propertyPresent_ = false;
  sourceError_.clear(); state_ = nullptr; shape_ = nullptr;
  sourceId_ = 0; t_ = c_ = z_ = 0;
  if (!source) sourceError_ = "Open an image to use channel display.";
  else {
    sourceId_ = source->id(); t_ = source->display().t; c_ = source->display().c;
    z_ = source->display().crosshair[2];
    const auto &dataset = source->data().dataset;
    const auto shape = dataset.shape(); shape_ = shape.dimensions();
    const auto property = source->display().properties.find("channel_display");
    propertyPresent_ = property != source->display().properties.end();
    if (propertyPresent_) state_ = metadataJson(property->second);
    const auto type = dataset.pixel_type();
    supported_ = shape.s == 1 && (type == PixelType::UInt8 ||
        type == PixelType::UInt16 || type == PixelType::Float32);
    if (!supported_)
      sourceError_ = "Channel display requires a scalar UInt8, UInt16 or Float32 image.";
    else {
      try { state = recognized_channel_display(source->display(), dataset); }
      catch (const std::invalid_argument &error) {
        sourceError_ = std::string("Cannot interpret channel display: ") + error.what();
      }
    }
  }
  updateRangeDraft(sourceId_, source ? c_ : -1,
      state ? std::optional<std::array<double,2>>(state->channels[c_].range) : std::nullopt);

  enum class Pending { None, Enable, Select, Set, Range, Auto, ResetCurrent, ResetAll, Disable, Load, Close };
  Pending pending = Pending::None;
  bool autoAllChannels = false;
  int selectedChannel = c_;
  std::optional<ChannelDisplayRequest> request;
  const bool editable = source && supported_ && sourceError_.empty() && !busy_;
  const auto *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos({viewport->WorkPos.x+65,
      viewport->WorkPos.y+std::max(100.f,4.f*ImGui::GetFrameHeightWithSpacing())}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({420,0}, ImGuiCond_FirstUseEver);
  if (focus_) ImGui::SetNextWindowFocus();
  bool windowOpen = true;
  const bool contents = ImGui::Begin("Channels###ImageJChannelDisplayPanel", &windowOpen,
      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
  windowFocused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  if (!windowFocused_ || !state) focusField_ = 0;
  if (contents && windowOpen) {
    if (source) ImGui::Text("Captured T %d; Z %d", t_+1, z_+1);
    if (!sourceError_.empty()) ImGui::TextWrapped("%s", sourceError_.c_str());
    if (busy_) ImGui::TextUnformatted("An operation is running.");
    if (!state) {
      if (!propertyPresent_)
        ImGui::TextWrapped("Enable per-channel display to adjust channels. Current display settings are retained.");
      const bool enabled = editable && !propertyPresent_ && bool(enable);
      ImGui::BeginDisabled(!enabled);
      if (ImGui::Button("Enable###channel_display.enable")) pending = Pending::Enable;
      control("channel_display.enable", enabled); ImGui::EndDisabled();
    } else {
      const bool canSet = editable && bool(apply);
      ImGui::BeginDisabled(!canSet);
      modeLabel_ = displayName(*state);
      if (ImGui::BeginCombo("Mode###channel_display.mode", modeLabel_.c_str())) {
        const std::array<ChannelDisplayMode,3> modes{ChannelDisplayMode::Color,
            ChannelDisplayMode::Grayscale,ChannelDisplayMode::Composite};
        const char *keys[] = {"color","grayscale","composite"};
        for (std::size_t i=0; i<modes.size(); ++i) {
          const std::string id = std::string("channel_display.mode.")+keys[i];
          const std::string label = std::string(modeName(modes[i]))+"###"+id;
          const bool selected = state->mode == modes[i] &&
              (modes[i] != ChannelDisplayMode::Composite ||
               state->projection == ChannelDisplayProjection::Sum);
          if (ImGui::Selectable(label.c_str(), selected) &&
              !selected && pending == Pending::None) {
            auto next = *state; next.mode = modes[i];
            if (next.mode == ChannelDisplayMode::Composite) {
              next.projection = ChannelDisplayProjection::Sum;
              for (auto &channel : next.channels) channel.visible = true;
            }
            request = ChannelDisplayRequest{ChannelDisplayAction::Set,std::move(next),{}};
            pending = Pending::Set;
          }
          control(id, canSet);
        }
        if (ImGui::Selectable("Composite Max###channel_display.mode.composite_max",
                state->mode == ChannelDisplayMode::Composite &&
                state->projection == ChannelDisplayProjection::Max) &&
            pending == Pending::None) {
          request = ChannelDisplayRequest{ChannelDisplayAction::SelectMax,{}, {}};
          pending = Pending::Set;
        }
        control("channel_display.mode.composite_max", canSet);
        if (ImGui::Selectable("Composite Min###channel_display.mode.composite_min",
                state->mode == ChannelDisplayMode::Composite &&
                state->projection == ChannelDisplayProjection::Min) &&
            pending == Pending::None) {
          request = ChannelDisplayRequest{ChannelDisplayAction::SelectMin,{}, {}};
          pending = Pending::Set;
        }
        control("channel_display.mode.composite_min", canSet);
        ImGui::EndCombo();
      }
      // EndCombo restores the parent's actual combo item; an open popup's
      // title/last option must never stand in for the combo ID or rectangle.
      control("channel_display.mode", canSet);
      ImGui::EndDisabled();
      if (state->channels.size() > 8)
        ImGui::TextWrapped("Composite requests use Color above eight channels.");

      const bool canSelect = editable && bool(select_channel) && pending == Pending::None;
      ImGui::BeginDisabled(!canSelect);
      const auto activeLabel = std::to_string(c_+1);
      if (ImGui::BeginCombo("Active channel###channel_display.channel", activeLabel.c_str())) {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(state->channels.size()), ImGui::GetTextLineHeightWithSpacing());
        clipper.IncludeItemByIndex(c_);
        while (clipper.Step()) {
          for (int c=clipper.DisplayStart; c<clipper.DisplayEnd; ++c) {
            const std::string id = "channel_display.channel."+std::to_string(c);
            const std::string label = std::to_string(c+1)+"###"+id;
            if (ImGui::Selectable(label.c_str(), c == c_) && c != c_) {
              selectedChannel = c; pending = Pending::Select;
            }
            control(id, canSelect);
            if (c == c_) ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }
      control("channel_display.channel", canSelect);
      ImGui::EndDisabled();

      const bool canEditState = canSet && pending == Pending::None;
      ImGui::BeginDisabled(!canEditState);
      ImGui::TextUnformatted("Visible channels");
      const float rows = static_cast<float>(std::min<std::size_t>(state->channels.size(),6));
      if (ImGui::BeginChild("channel_display.visibility", {0,rows*ImGui::GetFrameHeightWithSpacing()+8}, true)) {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(state->channels.size()), ImGui::GetFrameHeightWithSpacing());
        while (clipper.Step()) {
          for (int c=clipper.DisplayStart; c<clipper.DisplayEnd; ++c) {
            const bool enabled = canEditState && pending == Pending::None;
            ImGui::BeginDisabled(!enabled);
            bool visible = state->channels[c].visible;
            const std::string id = "channel_display.visible."+std::to_string(c);
            const std::string label = "C"+std::to_string(c+1)+"###"+id;
            if (ImGui::Checkbox(label.c_str(), &visible) && pending == Pending::None) {
              auto next = *state; next.channels[c].visible = visible;
              request = ChannelDisplayRequest{ChannelDisplayAction::Set,std::move(next),{}};
              pending = Pending::Set;
            }
            control(id, enabled); ImGui::EndDisabled();
          }
        }
      }
      ImGui::EndChild(); ImGui::EndDisabled();

      if (!std::isfinite(state->channels[c_].range[0]) || !std::isfinite(state->channels[c_].range[1]))
        ImGui::TextWrapped("Computed range contains infinity or NaN. Enter finite bounds to override it.");
      const bool fieldsEnabled = canSet && pending == Pending::None;
      ImGui::BeginDisabled(!fieldsEnabled);
      const float fieldWidth = 14.f*ImGui::GetFontSize();
      ImGui::SetNextItemWidth(fieldWidth);
      if (fieldsEnabled && (focus_ || focusField_ == 1)) ImGui::SetKeyboardFocusHere();
      bool changed = ImGui::InputText("Minimum###channel_display.minimum", &minimumText_, ImGuiInputTextFlags_AutoSelectAll);
      minimumId_ = ImGui::GetItemID(); control("channel_display.minimum", fieldsEnabled, fieldWidth);
      ImGui::SetNextItemWidth(fieldWidth);
      if (fieldsEnabled && focusField_ == 2) ImGui::SetKeyboardFocusHere();
      changed |= ImGui::InputText("Maximum###channel_display.maximum", &maximumText_, ImGuiInputTextFlags_AutoSelectAll);
      maximumId_ = ImGui::GetItemID(); control("channel_display.maximum", fieldsEnabled, fieldWidth);
      ImGui::EndDisabled();
      if (fieldsEnabled) focusField_ = 0;
      if (changed) parseRangeDraft();
      if (!rangeError_.empty()) ImGui::TextWrapped("%s",rangeError_.c_str());
      const bool canRange = fieldsEnabled && draftValid_;
      ImGui::BeginDisabled(!canRange);
      if (ImGui::Button("Apply range###channel_display.apply")) {
        auto next = *state; next.channels[c_].range = *draftRange_;
        request = ChannelDisplayRequest{ChannelDisplayAction::Set,std::move(next),{}};
        pending = Pending::Range;
      }
      control("channel_display.apply",canRange); ImGui::EndDisabled();

      const bool canLut = canSet && pending == Pending::None;
      ImGui::BeginDisabled(!canLut);
      const auto &lut = state->channels[c_].lut;
      if (ImGui::BeginCombo("LUT###channel_display.lut", lut.table ? "Custom" : lut.name.c_str())) {
        const char *names[] = {"Grays","Red","Green","Blue","White","Cyan","Magenta","Yellow"};
        for (const char *name : names) {
          const std::string id = std::string("channel_display.lut.")+name;
          const std::string label = std::string(name)+"###"+id;
          if (ImGui::Selectable(label.c_str(), !lut.table && lut.name == name) &&
              (lut.table || lut.name != name) && pending == Pending::None) {
            auto next = *state; next.channels[c_].lut = ChannelDisplayLut{name,{}};
            request = ChannelDisplayRequest{ChannelDisplayAction::Set,std::move(next),{}};
            pending = Pending::Set;
          }
          control(id,canLut);
        }
        ImGui::EndCombo();
      }
      control("channel_display.lut",canLut);
      ImGui::EndDisabled();
      ImGui::SameLine();
      const bool canLoad = editable && bool(load_lut) && pending == Pending::None;
      ImGui::BeginDisabled(!canLoad);
      if (ImGui::Button("Load LUT###channel_display.load_lut")) pending = Pending::Load;
      control("channel_display.load_lut",canLoad); ImGui::EndDisabled();

      const bool canAuto = canSet && bool(auto_contrast) && pending == Pending::None;
      ImGui::BeginDisabled(!canAuto);
      if (ImGui::Button("Auto###channel_display.auto")) {
        pending = Pending::Auto;
        autoAllChannels = ImGui::GetIO().KeyShift;
      }
      control("channel_display.auto", canAuto);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Adjust the current channel from its image and area selection. Hold Shift to apply the range to all channels.");
      ImGui::SameLine();
      ImGui::Checkbox("Use area selection###channel_display.auto_roi", &autoUseSelection_);
      control("channel_display.auto_roi", canAuto);
      ImGui::EndDisabled();

      const bool canReset = canSet && pending == Pending::None;
      ImGui::BeginDisabled(!canReset);
      if (ImGui::Button("Reset current range###channel_display.reset_current")) {
        request = ChannelDisplayRequest{ChannelDisplayAction::ResetRange,*state,{c_}};
        pending = Pending::ResetCurrent;
      }
      control("channel_display.reset_current",canReset);
      ImGui::SameLine();
      if (ImGui::Button("Reset all ranges###channel_display.reset_all") && pending == Pending::None) {
        request = ChannelDisplayRequest{ChannelDisplayAction::ResetRange,*state,{}};
        pending = Pending::ResetAll;
      }
      control("channel_display.reset_all",canReset); ImGui::EndDisabled();
      const bool canDisable = canSet && pending == Pending::None;
      ImGui::BeginDisabled(!canDisable);
      if (ImGui::Button("Disable###channel_display.disable")) {
        request = ChannelDisplayRequest{ChannelDisplayAction::Disable,{}, {}};
        pending = Pending::Disable;
      }
      control("channel_display.disable",canDisable); ImGui::EndDisabled();
      ImGui::SameLine();
    }
    if (ImGui::Button("Close###channel_display.close")) pending = Pending::Close;
    control("channel_display.close",true);
    if (windowFocused_ && ImGui::IsKeyPressed(ImGuiKey_Escape,false)) {
      pending = Pending::Close; exitReason_ = "escape";
    }
  }
  focus_ = false;
  ImGui::End();
  if (!windowOpen) { pending = Pending::Close; exitReason_ = "titlebar"; }
  if (pending == Pending::Close) {
    const auto reason = exitReason_.empty() ? std::string("close") : exitReason_;
    close(); exitReason_ = reason; return;
  }
  // Host callbacks may publish or throw. All ImGui scopes are closed first;
  // callbacks never run for a second action against the same stale capture.
  if (pending == Pending::Enable) { enable(*source); return; }
  if (pending == Pending::Select) { select_channel(selectedChannel); return; }
  if (pending == Pending::Load) { load_lut(); return; }
  if (pending == Pending::Auto) {
    auto_contrast(*source, autoAllChannels, autoUseSelection_);
    forceRangeRebase_ = true;
    return;
  }
  if (request) {
    const bool success = apply(*source,*request);
    if (success && (pending == Pending::Range || pending == Pending::ResetCurrent || pending == Pending::ResetAll))
      forceRangeRebase_ = true;
    return;
  }
}

Json ChannelDisplayPanel::diagnostics() const {
  return {{"open",open_},{"visible",open_},{"busy",busy_},
      {"source_valid",sourceValid_},{"supported",supported_},{"source_error",sourceError_},
      {"source_id",sourceId_},{"source_position",{{"t",t_},{"c",c_},{"z",z_}}},
      {"captured_t",t_},{"captured_z",z_},{"active_channel",c_},
      {"shape",shape_},{"state",state_},{"property_present",propertyPresent_},
      {"minimum_text",minimumText_},{"maximum_text",maximumText_},
      {"draft_valid",draftValid_},{"range_error",rangeError_},
      {"draft_range",draftRange_ ? Json(*draftRange_) : Json(nullptr)},
      {"range_rebase_pending",forceRangeRebase_},{"window_focused",windowFocused_},
      {"auto_use_selection",autoUseSelection_},
      {"input_active",inputActive()},
      {"mode_label",modeLabel_},
      {"controls",controls_},{"control_rects",controlRects_},{"control_states",controlStates_},
      {"exit_reason",exitReason_}};
}

} // namespace imagejxx::desktop
