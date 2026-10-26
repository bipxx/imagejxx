#include "settings_panel.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <utility>
#include <type_traits>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

namespace imagejxx::desktop {
void SettingsPanel::swap(SettingsPanel &other) noexcept {
  using std::swap;
  swap(open_, other.open_);
  swap(focused_, other.focused_);
  shortcutEditor_.swap(other.shortcutEditor_);
  draft_.swap(other.draft_);
  search_.swap(other.search_);
  group_.swap(other.group_);
  error_.swap(other.error_);
  measurementName_.swap(other.measurementName_);
  toggleName_.swap(other.toggleName_);
  swap(toggleValue_, other.toggleValue_);
  swap(toggle_, other.toggle_);
  swap(apply_, other.apply_);
  swap(searchPoint_, other.searchPoint_);
  swap(closePoint_, other.closePoint_);
  visibleNames_.swap(other.visibleNames_);
  fields_.swap(other.fields_);
  widgetControls_.swap(other.widgetControls_);
}
namespace {
std::string lower(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}
ImVec2 center() {
  auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
  return {(a.x + b.x) * .5f, (a.y + b.y) * .5f};
}
Json itemControl() {
  const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
  const auto &context = *ImGui::GetCurrentContext();
  return {{"id", ImGui::GetItemID()},
          {"rect", {a.x, a.y, b.x, b.y}},
          {"point", {(a.x + b.x) * .5f, (a.y + b.y) * .5f}},
          {"enabled", !(context.LastItemData.ItemFlags & ImGuiItemFlags_Disabled)},
          {"active", ImGui::IsItemActive()},
          {"focused", ImGui::IsItemFocused()}};
}
} // namespace
void SettingsPanel::show(const Settings &settings, std::string search) {
  auto nextDraft = settings.values();
  auto nextEditor = shortcutEditor_;
  nextEditor.synchronize(settings.strings("shortcut_bindings"));
  nextEditor.synchronizeApplied(settings.strings("shortcut_bindings"));
  nextEditor.beginFrame();
  draft_.swap(nextDraft);
  using std::swap;
  swap(shortcutEditor_, nextEditor);
  search_ = std::move(search);
  group_.clear();
  measurementName_.clear();
  error_.clear();
  widgetControls_ = Json::object();
  focused_ = false;
  open_ = true;
}
void SettingsPanel::accept_startup_workspace(const std::filesystem::path &path) {
  if (!open_)
    return;
  if (path.empty() || !path.is_absolute())
    throw std::invalid_argument("Startup workspace selection must be an absolute path");
  auto nextDraft = draft_;
  nextDraft.at("startup_workspace") = path.u8string();
  nextDraft.at("startup_mode") = std::string("workspace");
  draft_.swap(nextDraft);
  error_.clear();
}
void SettingsPanel::draw(const Settings &current,
                         const std::function<void(const Settings &)> &apply,
                         const std::function<void(bool)> &file,
                         const std::function<void()> &browse_workspace,
                         const std::function<void()> &reset_layout) {
  visibleNames_.clear();
  fields_ = Json::object();
  widgetControls_ = Json::object();
  shortcutEditor_.beginFrame();
  focused_ = false;
  toggle_ = apply_ = searchPoint_ = closePoint_ = {-1, -1};
  toggleName_.clear();
  if (!open_) {
    shortcutEditor_.cancelCapture("closed");
    return;
  }
  const float font = ImGui::GetFontSize();
  const auto display = ImGui::GetIO().DisplaySize;
  ImGui::SetNextWindowSize(
      {std::min(44 * font, std::max(1.f, display.x - 4 * font)),
       std::min(40 * font, std::max(1.f, display.y - 4 * font))},
      ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(1.f);
  const bool contentsVisible = ImGui::Begin("Preferences", &open_);
  focused_ = open_ && !ImGui::GetIO().AppFocusLost &&
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  const auto *window = ImGui::GetCurrentWindowRead();
  bool standardTitleBar = !(window->Flags & ImGuiWindowFlags_NoTitleBar);
#ifdef IMGUI_HAS_DOCK
  standardTitleBar &= !window->DockIsActive;
#endif
  if (open_ && window->HasCloseButton && !window->Hidden && standardTitleBar) {
    // ImGui 1.91.9 places the standard close hitbox at the title's right edge,
    // inset by FramePadding, with a square side equal to the window font size.
    const auto title = window->TitleBarRect();
    const auto &padding = ImGui::GetStyle().FramePadding;
    const float halfButton = ImGui::GetFontSize() * .5f;
    closePoint_ = {title.Max.x - padding.x - halfButton,
                   title.Min.y + padding.y + halfButton};
#if IMGUI_VERSION_NUM == 19190
    // Close is submitted inside Begin(), so LastItemData does not describe it
    // here. Bind its ID to this window and derive its interaction rectangle
    // from the reviewed 1.91.9 standard-titlebar layout instead.
    const float side = ImGui::GetFontSize();
    ImRect visual({title.Max.x - padding.x - side,
                   title.Min.y + padding.y},
                  {title.Max.x - padding.x,
                   title.Min.y + padding.y + side});
    auto interaction = visual;
    if (window->OuterRectClipped.GetArea() / visual.GetArea() < 1.5f) {
      const float inset = std::trunc(side * -.25f);
      interaction.Expand(ImVec2(inset, inset));
    }
    const auto closeId = ImGui::GetID("#CLOSE");
    const auto &context = *ImGui::GetCurrentContext();
    widgetControls_["close"] = {
        {"id", closeId},
        {"rect", {interaction.Min.x, interaction.Min.y,
                  interaction.Max.x, interaction.Max.y}},
        {"point", {closePoint_.x, closePoint_.y}},
        {"visual_rect", {visual.Min.x, visual.Min.y,
                         visual.Max.x, visual.Max.y}},
        {"enabled", !(context.CurrentItemFlags & ImGuiItemFlags_Disabled)},
        {"active", context.ActiveId == closeId},
        {"focused", context.NavWindow == window && context.NavId == closeId},
        {"identity_source", "current_window_scoped_#CLOSE"},
        {"geometry_source", "imgui_1.91.9_standard_titlebar_layout_derived"},
        {"rect_kind", "interaction"}};
#endif
  }
  if (!contentsVisible || !open_) {
    shortcutEditor_.cancelCapture(open_ ? "collapsed" : "close");
    ImGui::End();
    if (!open_) {
      measurementName_.clear();
      draft_ = current.values();
      shortcutEditor_.synchronize(current.strings("shortcut_bindings"));
      shortcutEditor_.synchronizeApplied(current.strings("shortcut_bindings"));
      shortcutEditor_.cancelCapture("close");
    }
    return;
  }
  const float available = ImGui::GetContentRegionAvail().x;
  const bool inlineFilters = available >= 28 * font;
  const auto &style = ImGui::GetStyle();
  const float labels = ImGui::CalcTextSize("Search").x +
                       ImGui::CalcTextSize("Group").x +
                       2 * style.ItemInnerSpacing.x + style.ItemSpacing.x;
  const float fields = std::max(font, available - labels);
  ImGui::BeginDisabled(shortcutEditor_.captureActive());
  ImGui::SetNextItemWidth(inlineFilters ? fields * .65f : -1.f);
  ImGui::InputText("Search", &search_);
  searchPoint_ = center();
  widgetControls_["search"] = itemControl();
  if (inlineFilters)
    ImGui::SameLine();
  ImGui::SetNextItemWidth(inlineFilters ? fields * .35f : -1.f);
  if (ImGui::BeginCombo("Group", group_.empty() ? "All" : group_.c_str())) {
    if (ImGui::Selectable("All", group_.empty()))
      group_.clear();
    widgetControls_["group_choices"]["All"] = itemControl();
    widgetControls_["group_choices"]["All"]["visible"] = ImGui::IsItemVisible();
    const auto allClip = ImGui::GetCurrentWindowRead()->ClipRect;
    widgetControls_["group_choices"]["All"]["clip_rect"] =
        {allClip.Min.x, allClip.Min.y, allClip.Max.x, allClip.Max.y};
    std::vector<std::string> groups;
    for (const auto &d : Settings::schema())
      if (std::find(groups.begin(), groups.end(), d.group) == groups.end()) {
        groups.push_back(d.group);
        if (ImGui::Selectable(d.group.c_str(), group_ == d.group))
          group_ = d.group;
        widgetControls_["group_choices"][d.group] = itemControl();
        widgetControls_["group_choices"][d.group]["visible"] = ImGui::IsItemVisible();
        const auto clip = ImGui::GetCurrentWindowRead()->ClipRect;
        widgetControls_["group_choices"][d.group]["clip_rect"] =
            {clip.Min.x, clip.Min.y, clip.Max.x, clip.Max.y};
      }
    ImGui::EndCombo();
  }
  widgetControls_["group"] = itemControl();
  ImGui::BeginDisabled(shortcutEditor_.candidateBlocksApply());
  if (ImGui::Button("Apply")) {
    try {
      auto proposed = current;
      proposed.update(draft_);
      auto nextDraft = proposed.values();
      auto nextEditor = shortcutEditor_;
      nextEditor.synchronize(proposed.strings("shortcut_bindings"));
      nextEditor.synchronizeApplied(proposed.strings("shortcut_bindings"));
      apply(proposed);
      draft_.swap(nextDraft);
      using std::swap;
      swap(shortcutEditor_, nextEditor);
      error_.clear();
    } catch (const std::exception &error) {
      error_ = error.what();
    }
  }
  apply_ = center();
  widgetControls_["apply"] = itemControl();
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Defaults")) {
    try {
      auto nextDraft = draft_;
      for (const auto &d : Settings::schema())
        if (group_.empty() || d.group == group_)
          nextDraft[d.name] = d.default_value;
      auto nextEditor = shortcutEditor_;
      nextEditor.synchronize(std::get<std::vector<std::string>>(nextDraft.at("shortcut_bindings")));
      draft_.swap(nextDraft);
      using std::swap;
      swap(shortcutEditor_, nextEditor);
      if (group_.empty() || group_ == "Measurements") measurementName_.clear();
      error_.clear();
    } catch (const std::exception &error) { error_ = error.what(); }
  }
  widgetControls_["defaults"] = itemControl();
  ImGui::SameLine();
  if (ImGui::Button("Revert")) {
    try {
      auto nextDraft = current.values();
      auto nextEditor = shortcutEditor_;
      nextEditor.synchronize(current.strings("shortcut_bindings"));
      nextEditor.synchronizeApplied(current.strings("shortcut_bindings"));
      draft_.swap(nextDraft);
      using std::swap;
      swap(shortcutEditor_, nextEditor);
      measurementName_.clear();
      error_.clear();
    } catch (const std::exception &error) { error_ = error.what(); }
  }
  widgetControls_["revert"] = itemControl();
  ImGui::SameLine();
  if (ImGui::Button("Import..."))
    file(false);
  widgetControls_["import"] = itemControl();
  ImGui::SameLine();
  if (ImGui::Button("Export..."))
    file(true);
  widgetControls_["export"] = itemControl();
  ImGui::EndDisabled();
  if (reset_layout) {
    ImGui::BeginDisabled(shortcutEditor_.captureActive());
    if (ImGui::Button("Reset layout###preferences.reset_layout")) {
      try {
        reset_layout();
        error_.clear();
      } catch (const std::exception &error) {
        error_ = error.what();
      }
    }
    widgetControls_["reset_layout"] = itemControl();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Save removal of the stored window layout and reset the current layout.");
    ImGui::EndDisabled();
    ImGui::SameLine();
  } else
    ImGui::SameLine();
  const bool closeRequested = ImGui::Button("Close###preferences.close");
  widgetControls_["close_button"] = itemControl();
  if (closeRequested) {
    measurementName_.clear();
    shortcutEditor_.cancelCapture("close");
    open_ = false;
    focused_ = false;
    ImGui::End();
    draft_ = current.values();
    shortcutEditor_.synchronize(current.strings("shortcut_bindings"));
    shortcutEditor_.synchronizeApplied(current.strings("shortcut_bindings"));
    shortcutEditor_.cancelCapture("close");
    return;
  }
  if (!error_.empty())
    ImGui::TextWrapped("%s", error_.c_str());
  ImGui::Separator();
  ImGui::BeginChild("settings", {0, 0}, false);
  std::string previous;
  bool shortcutDrawn = false;
  auto needle = lower(search_);
  for (const auto &d : Settings::schema()) {
    if (!group_.empty() && d.group != group_)
      continue;
    if (!needle.empty() &&
        lower(d.name + " " + d.group + " " + d.label + " " + d.description)
                .find(needle) == std::string::npos) {
      const auto &commands = shortcut_commands();
      const bool commandMatch = d.name == "shortcut_bindings" &&
          std::any_of(commands.begin(), commands.end(), [&](const ShortcutCommand &command) {
            return lower(command.id + " " + command.label).find(needle) != std::string::npos;
          });
      if (!commandMatch) continue;
    }
    if (d.group != previous) {
      ImGui::Spacing();
      ImGui::TextUnformatted(d.group.c_str());
      ImGui::Separator();
      previous = d.group;
      if (d.group == "Startup") {
        ImGui::TextWrapped("Restore an explicitly saved native workspace on the next launch. "
                           "Workspaces preserve supported eager images, views and cameras; "
                           "saved results and workflows remain inert data.");
        const bool changed = draft_.at("startup_mode") != current.at("startup_mode") ||
                             draft_.at("startup_workspace") != current.at("startup_workspace");
        ImGui::TextUnformatted(changed ? "Startup changes are pending Apply." :
                                        "Startup settings match the applied configuration.");
      }
    }
    visibleNames_.push_back(d.name);
    ImGui::PushID(d.name.c_str());
    ImGui::BeginDisabled(shortcutEditor_.captureActive() && d.name != "shortcut_bindings");
    ImGui::SetNextItemWidth(16 * font);
    auto &v = draft_.at(d.name);
    if (d.name == "shortcut_bindings") {
      shortcutDrawn = true;
      shortcutEditor_.draw(std::get<std::vector<std::string>>(v),
                           current.strings("shortcut_bindings"));
      fields_[d.name] = {{"name", d.name}, {"kind", "shortcuts"},
                         {"draft", std::get<std::vector<std::string>>(v)}};
    } else if (d.kind == SettingKind::Boolean) {
      bool b = std::get<bool>(v);
      if (ImGui::Checkbox(d.label.c_str(), &b))
        v = b;
      auto observed = itemControl();
      const auto *fieldWindow = ImGui::GetCurrentWindowRead();
      observed["hovered"] = ImGui::IsItemHovered();
      observed["visible"] = ImGui::IsItemVisible();
      observed["clip_rect"] = {fieldWindow->ClipRect.Min.x, fieldWindow->ClipRect.Min.y,
                                fieldWindow->ClipRect.Max.x, fieldWindow->ClipRect.Max.y};
      fields_[d.name] = {{"name", d.name}, {"kind", "boolean"},
                         {"draft", std::get<bool>(v)}, {"applied", current.boolean(d.name)},
                         {"control", std::move(observed)}};
      if (toggle_.x < 0 || d.name == "auto_dpi") {
        toggle_ = center();
        toggleName_ = d.name;
        toggleValue_ = std::get<bool>(v);
      }
    } else if (d.kind == SettingKind::Integer) {
      int n = static_cast<int>(std::get<std::int64_t>(v));
      // InputInt submits a group. Its editable InputText has this scoped label
      // ID, while the final group item can inherit a different child ID.
      const auto editorId = ImGui::GetID(d.label.c_str());
      const auto *editorWindow = ImGui::GetCurrentWindowRead();
      const bool editorEnabled =
          !(ImGui::GetCurrentContext()->CurrentItemFlags &
            ImGuiItemFlags_Disabled);
      if (ImGui::InputInt(d.label.c_str(), &n))
        v = static_cast<std::int64_t>(n);
      // InputInt's full item includes the step buttons and label. Keep this
      // point inside its editable text area for actual keyboard interaction.
      const auto point =
          ImVec2(ImGui::GetItemRectMin().x + 4 * font, center().y);
      fields_[d.name] = {{"name", d.name},
                         {"kind", "integer"},
                         {"draft", std::get<std::int64_t>(v)},
                         {"point", {point.x, point.y}}};
      if ((d.name == "processing_memory_mb" || d.name == "history_limit" ||
           d.name == "history_memory_mb" || d.name == "wand_connectivity") &&
          !editorWindow->SkipItems) {
        const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        const auto &context = *ImGui::GetCurrentContext();
        auto &field = fields_[d.name];
        field["id"] = editorId;
        field["rect"] = {a.x, a.y, b.x, b.y};
        field["enabled"] = editorEnabled;
        field["active"] = context.ActiveId == editorId;
        field["focused"] =
            context.NavWindow == editorWindow && context.NavId == editorId;
        field["identity_source"] = "current_scoped_InputInt_text_label_id";
        field["geometry_source"] = "actual_InputInt_group_item_rect";
        field["rect_kind"] = "group_including_step_buttons_and_label";
        if (d.name == "wand_connectivity") {
          field["applied"] = current.integer(d.name);
          field["hovered"] = context.HoveredId == editorId;
        }
        if (d.name == "history_limit" || d.name == "history_memory_mb" ||
            d.name == "wand_connectivity") {
          field["visible"] = ImGui::IsItemVisible();
          field["clip_rect"] = {editorWindow->ClipRect.Min.x, editorWindow->ClipRect.Min.y,
                                editorWindow->ClipRect.Max.x, editorWindow->ClipRect.Max.y};
        }
      }
    } else if (d.kind == SettingKind::Number) {
      double n = std::get<double>(v);
      const auto editorId = ImGui::GetID(d.label.c_str());
      const auto *editorWindow = ImGui::GetCurrentWindowRead();
      const bool editorEnabled =
          !(ImGui::GetCurrentContext()->CurrentItemFlags &
            ImGuiItemFlags_Disabled);
      if (ImGui::InputDouble(d.label.c_str(), &n, 0, 0, "%.8g"))
        v = n;
      if (!editorWindow->SkipItems) {
        const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        const auto &context = *ImGui::GetCurrentContext();
        // A zero-step InputDouble has one editable InputText. Keep the point
        // inside that text box, excluding its trailing visible label.
        fields_[d.name] = {
            {"name", d.name}, {"kind", "number"},
            {"draft", std::get<double>(v)}, {"applied", current.number(d.name)},
            {"id", editorId}, {"rect", {a.x, a.y, b.x, b.y}},
            {"point", {a.x + 4 * font, (a.y + b.y) * .5f}},
            {"enabled", editorEnabled}, {"active", context.ActiveId == editorId},
            {"focused", context.NavWindow == editorWindow && context.NavId == editorId},
            {"hovered", context.HoveredId == editorId},
            {"visible", ImGui::IsItemVisible()},
            {"clip_rect", {editorWindow->ClipRect.Min.x, editorWindow->ClipRect.Min.y,
                           editorWindow->ClipRect.Max.x, editorWindow->ClipRect.Max.y}},
            {"identity_source", "current_scoped_InputDouble_text_label_id"},
            {"geometry_source", "actual_InputDouble_item_rect"},
            {"rect_kind", "input_including_label"}};
      }
    } else if (d.kind == SettingKind::Choice) {
      auto &text = std::get<std::string>(v);
      Json choiceControls = Json::object();
      Json comboControl;
      if (ImGui::BeginCombo(d.label.c_str(), text.c_str())) {
        for (const auto &choice : d.choices) {
          if (ImGui::Selectable(choice.c_str(), choice == text))
            text = choice;
          choiceControls[choice] = itemControl();
        }
        ImGui::EndCombo();
      } else {
        comboControl = itemControl();
      }
      fields_[d.name] = {{"name", d.name}, {"kind", "choice"},
                         {"draft", text}, {"applied", current.text(d.name)},
                         {"combo", std::move(comboControl)},
                         {"choices", std::move(choiceControls)}};
    } else if (d.kind == SettingKind::Text) {
      auto &text = std::get<std::string>(v);
      ImGui::InputText(d.label.c_str(), &text);
      auto textControl = itemControl();
      fields_[d.name] = {{"name", d.name}, {"kind", "text"},
                         {"draft", text}, {"applied", current.text(d.name)},
                         {"editor", std::move(textControl)}};
      if (d.name == "startup_workspace") {
        ImGui::SameLine();
        ImGui::BeginDisabled(!browse_workspace);
        if (ImGui::Button("Browse...###startup_workspace.browse")) {
          try {
            browse_workspace();
            error_.clear();
          } catch (const std::exception &error) {
            error_ = error.what();
          }
        }
        fields_[d.name]["browse"] = itemControl();
        ImGui::EndDisabled();
        ImGui::TextWrapped("Choose a saved .ijx workspace. Selecting a file edits this draft; "
                           "Apply saves the startup choice. Saving the workspace is a separate action.");
      }
    } else if (d.kind == SettingKind::Color) {
      auto &text = std::get<std::string>(v);
      ImGui::InputText(d.label.c_str(), &text);
      auto editorControl = itemControl();
      const auto *fieldWindow = ImGui::GetCurrentWindowRead();
      editorControl["hovered"] = ImGui::IsItemHovered();
      editorControl["visible"] = ImGui::IsItemVisible();
      editorControl["clip_rect"] = {fieldWindow->ClipRect.Min.x, fieldWindow->ClipRect.Min.y,
                                     fieldWindow->ClipRect.Max.x, fieldWindow->ClipRect.Max.y};
      ImGui::SameLine();
      unsigned int r = 255, g = 255, b = 255, a = 255;
      if (text.size() == 7 || text.size() == 9)
        std::sscanf(text.c_str(), "#%2x%2x%2x%2x", &r, &g, &b, &a);
      float rgba[]{r / 255.f, g / 255.f, b / 255.f, a / 255.f};
      if (ImGui::ColorEdit4("##picker", rgba, ImGuiColorEditFlags_NoInputs)) {
        char hex[10];
        std::snprintf(hex, sizeof(hex), "#%02x%02x%02x%02x",
                      int(rgba[0] * 255 + .5), int(rgba[1] * 255 + .5),
                      int(rgba[2] * 255 + .5), int(rgba[3] * 255 + .5));
        text = hex;
      }
      fields_[d.name] = {{"name", d.name}, {"kind", "color"},
                         {"draft", text}, {"applied", current.text(d.name)},
                         {"editor", std::move(editorControl)}, {"picker", itemControl()}};
    } else {
      auto &columns = std::get<std::vector<std::string>>(v);
      ImGui::TextUnformatted(d.label.c_str());
      Json controls = Json::object(), choiceControls = Json::object();
      const auto observed = [&] {
        auto item = itemControl();
        const auto *owner = ImGui::GetCurrentWindowRead();
        item["visible"] = ImGui::IsItemVisible();
        item["clip_rect"] = {owner->ClipRect.Min.x, owner->ClipRect.Min.y,
                              owner->ClipRect.Max.x, owner->ClipRect.Max.y};
        item["window_name"] = owner->Name;
        return item;
      };
      if (d.name == "measurements") {
        ImGui::TextWrapped("Add a column name to this draft, then Apply to save. "
                           "Names match result keys exactly. Missing values appear blank.");
        const bool entered = ImGui::InputText("Column name", &measurementName_,
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        controls["pending"] = observed();
        ImGui::SameLine();
        const bool added = ImGui::Button("Add");
        controls["add"] = observed();
        if (entered || added) {
          try {
            auto candidate = columns;
            candidate.push_back(measurementName_);
            auto validator = current;
            validator.set("measurements", candidate);
            columns.swap(candidate);
            measurementName_.clear();
            error_.clear();
          } catch (const std::exception &error) {
            error_ = error.what();
          }
        }
      }
      bool all = columns.empty();
      if (ImGui::Checkbox("All available columns", &all))
        columns = all ? std::vector<std::string>{}
                      : std::vector<std::string>{d.choices[0]};
      controls["all"] = observed();
      controls["all"]["selected"] = columns.empty();
      auto choices = d.choices;
      for (const auto &column : columns)
        if (std::find(choices.begin(), choices.end(), column) == choices.end())
          choices.push_back(column);
      if (!columns.empty()) {
        // Column names are data and may equal any of the surrounding labels.
        ImGui::PushID("measurement_choices");
        for (std::size_t index = 0; index < choices.size(); ++index) {
          ImGui::PushID(static_cast<int>(index));
          const auto &choice = choices[index];
          auto found = std::find(columns.begin(), columns.end(), choice);
          bool selected = found != columns.end();
          bool changed;
          if (choice.find("##") != std::string::npos) {
            // Authored names are data, not ImGui label/ID markup.
            changed = ImGui::Checkbox("##column", &selected);
            choiceControls[choice] = observed();
            ImGui::SameLine();
            ImGui::TextUnformatted(choice.data(), choice.data() + choice.size());
            if (ImGui::IsItemClicked()) { selected = !selected; changed = true; }
          } else {
            changed = ImGui::Checkbox(choice.c_str(), &selected);
            choiceControls[choice] = observed();
          }
          if (changed) {
            if (selected) columns.push_back(choice);
            else columns.erase(found);
          }
          choiceControls[choice]["selected"] = selected;
          choiceControls[choice]["label_text"] = choice;
          choiceControls[choice]["label_text_source"] =
              choice.find("##") != std::string::npos ? "TextUnformatted" : "Checkbox";
          ImGui::PopID();
        }
        ImGui::PopID();
      }
      fields_[d.name] = {{"name", d.name}, {"kind", "string_list"},
          {"draft", columns}, {"applied", current.strings(d.name)},
          {"pending_name", d.name == "measurements" ? measurementName_ : std::string()},
          {"controls", std::move(controls)}, {"choices", std::move(choiceControls)}};
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", d.description.c_str());
    ImGui::EndDisabled();
    ImGui::PopID();
  }
  ImGui::EndChild();
  if (!shortcutDrawn) shortcutEditor_.cancelCapture("hidden");
  focused_ = open_ && !ImGui::GetIO().AppFocusLost &&
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  ImGui::End();
}
Json SettingsPanel::diagnostics() const {
  Json draft = Json::object();
  for (const auto &entry : draft_) {
    draft[entry.first] = std::visit([](const auto &value) -> Json {
      if constexpr (std::is_same_v<std::decay_t<decltype(value)>, std::monostate>) return Json();
      else return Json(value);
    }, entry.second);
  }
  return {{"open", open_},
          {"group", group_},
          {"focused", focused_},
          {"shortcut_capture_active", shortcutEditor_.captureActive()},
          {"shortcut_editor", shortcutEditor_.diagnostics()},
          {"controls", visibleNames_},
          {"fields", fields_},
          {"draft", std::move(draft)},
          {"measurements_pending_name", measurementName_},
          {"widget_controls", widgetControls_},
          {"search", search_},
          {"search_point", {searchPoint_.x, searchPoint_.y}},
          {"close_point", {closePoint_.x, closePoint_.y}},
          {"toggle_name", toggleName_},
          {"toggle_value", toggleName_.empty() ? Json(nullptr) : Json(toggleValue_)},
          {"toggle_point", {toggle_.x, toggle_.y}},
          {"apply_point", {apply_.x, apply_.y}},
          {"error", error_}};
}
} // namespace imagejxx::desktop
