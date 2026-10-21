#include "roi_display_replacement_editor.hpp"
#include "display_annotation_panel.hpp"
#include <imgui_internal.h>
#include <imgui_stdlib.h>
namespace imagejxx::desktop {
RoiDisplayReplacementEditor::RoiDisplayReplacementEditor(
    std::size_t i, DatasetShape shape, PlanarDisplayAnnotation row,
    std::optional<DocumentSnapshot> source)
    : index_(i), recorded_(shape), base_(std::move(row)),
      source_(std::move(source)) {
  seed(base_);
}
RoiDisplayReplacementEditor::~RoiDisplayReplacementEditor() = default;
RoiDisplayReplacementEditor::RoiDisplayReplacementEditor(
    RoiDisplayReplacementEditor &&) noexcept = default;
RoiDisplayReplacementEditor &RoiDisplayReplacementEditor::operator=(
    RoiDisplayReplacementEditor &&) noexcept = default;
void RoiDisplayReplacementEditor::seed(const PlanarDisplayAnnotation &r) {
  base_ = r;
  name_ = r.name;
  plane_ = int(r.plane);
  slice_ = r.coordinate + 1;
  all_t_ = !r.t;
  all_c_ = !r.c;
  t_ = r.t.value_or(0) + 1;
  c_ = r.c.value_or(0) + 1;
  confirmed_ = false;
}
void RoiDisplayReplacementEditor::observe(const char *key, bool enabled) {
  const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
  controls_[key] = {
      {"id", ImGui::GetItemID()},
      {"visible", ImGui::IsItemVisible()},
      {"enabled",
       enabled && !(ImGui::GetCurrentContext()->LastItemData.ItemFlags &
                    ImGuiItemFlags_Disabled)},
      {"rect", {a.x, a.y, b.x, b.y}},
      {"point", {(a.x + b.x) / 2, (a.y + b.y) / 2}}};
  if (std::string(key) == "slice" || std::string(key) == "time" ||
      std::string(key) == "channel")
    controls_[key]["point"][0] = a.x + 40;
}
ImageJDisplayRoiNativeReplacement
RoiDisplayReplacementEditor::replacement(DatasetShape shape) const {
  if (!confirmed_)
    throw std::invalid_argument("Confirm this complete display replacement");
  return {shape, value(shape)};
}
PlanarDisplayAnnotation
RoiDisplayReplacementEditor::value(DatasetShape shape) const {
  if (slice_ < 1 || (!all_t_ && t_ < 1) || (!all_c_ && c_ < 1))
    throw std::invalid_argument("Positions must be positive 1-based indices");
  auto row = base_;
  row.name = name_;
  row.plane = Plane(plane_);
  row.coordinate = slice_ - 1;
  row.t = all_t_ ? std::optional<int>{} : t_ - 1;
  row.c = all_c_ ? std::optional<int>{} : c_ - 1;
  row.validate(shape);
  return row;
}
int RoiDisplayReplacementEditor::draw(
    DatasetShape target, const std::vector<PlanarDisplayAnnotation> &rows,
    std::size_t cap) {
  controls_ = Json::object();
  if (focus_)
    ImGui::SetNextWindowFocus();
  ImGui::SetNextWindowSize({620, 500}, ImGuiCond_FirstUseEver);
  ImGui::Begin("Display ROI replacement", &open_);
  focus_ = false;
  {
    const auto a = ImGui::GetCurrentWindow()->InnerRect.Min,
               b = ImGui::GetCurrentWindow()->InnerRect.Max;
    bounds_ = {a.x, a.y, b.x, b.y};
  }
  ImGui::TextWrapped(
      "Keep the admitted text/image payload and opaque attributes, or "
      "explicitly choose a current display annotation as the complete "
      "template. Review its destination positions.");
  ImGui::BeginDisabled(bool(payload_editor_));
  ImGui::Text("Recorded T/C/Z/Y/X/S: %d/%d/%d/%d/%d/%d", recorded_.t,
              recorded_.c, recorded_.z, recorded_.y, recorded_.x, recorded_.s);
  if (ImGui::BeginCombo("Current display template", "Choose explicitly...")) {
    for (std::size_t i = 0; i < rows.size(); ++i) {
      ImGui::PushID(int(i));
      const auto label = std::to_string(i + 1) + ". " + rows[i].name;
      if (ImGui::Selectable(label.c_str()))
        seed(rows[i]);
      observe(("template." + std::to_string(i)).c_str());
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  observe("template");
  ImGui::Text("Payload: %s; %zu opaque attributes",
              base_.payload.index() == 0 ? "text" : "image",
              base_.attributes.size());
  ImGui::SameLine();
  ImGui::BeginDisabled(bool(payload_editor_) || !source_);
  if (ImGui::Button("Edit payload..."))
    try {
      auto row = value(target);
      auto native = std::make_shared<imagejxx::Document>(*source_);
      auto edit = native->begin_edit();
      edit.set_extension(planar_display_annotations_extension,
                         encode_planar_display_annotations(target, {row}));
      native->commit(edit);
      payload_document_ = std::make_unique<Document>(native);
      payload_editor_ = std::make_unique<DisplayAnnotationPanel>();
      payload_editor_->show_draft(native->snapshot(), 0);
      confirmed_ = false;
      error_.clear();
    } catch (const std::exception &e) {
      payload_editor_.reset();
      payload_document_.reset();
      error_ = e.what();
    }
  observe("edit_payload", !payload_editor_ && bool(source_));
  ImGui::EndDisabled();

  if (const auto p = std::get_if<TextAnnotationPayload>(&base_.payload)) {
    ImGui::Text("Font: %s, %.17g; angle %.17g", p->font_name.c_str(),
                p->font_size, p->angle);
    ImGui::TextWrapped("Text: %s", p->text.c_str());
  } else {
    const auto &image = std::get<ImageAnnotationPayload>(base_.payload);
    ImGui::Text("Pixels: %d x %d, %d channels; opacity %u", image.width,
                image.height, image.channels, unsigned(image.opacity));
  }
  bool changed = ImGui::InputText("Name", &name_);
  observe("name");
  const char *planes[]{"XY", "XZ", "YZ"};
  if (ImGui::BeginCombo("Plane", planes[plane_])) {
    for (int i = 0; i < 3; ++i) {
      if (ImGui::Selectable(planes[i], plane_ == i)) {
        plane_ = i;
        changed = true;
      }
      observe(("plane." + std::to_string(i)).c_str());
    }
    ImGui::EndCombo();
  }
  observe("plane");
  ImGui::SetNextItemWidth(110);
  changed |= ImGui::InputInt("Slice (1-based)", &slice_, 0, 0);
  observe("slice");
  changed |= ImGui::Checkbox("All times", &all_t_);
  observe("all_times");
  ImGui::BeginDisabled(all_t_);
  ImGui::SetNextItemWidth(110);
  changed |= ImGui::InputInt("Time (1-based)", &t_, 0, 0);
  observe("time", !all_t_);
  ImGui::EndDisabled();
  changed |= ImGui::Checkbox("All scientific channels", &all_c_);
  observe("all_channels");
  ImGui::BeginDisabled(all_c_);
  ImGui::SetNextItemWidth(110);
  changed |= ImGui::InputInt("Channel (1-based)", &c_, 0, 0);
  observe("channel", !all_c_);
  ImGui::EndDisabled();
  if (changed)
    confirmed_ = false;
  ImGui::Checkbox("Use this complete replacement on this destination",
                  &confirmed_);
  observe("confirm");
  int result = 0;
  ImGui::BeginDisabled(!confirmed_);
  if (ImGui::Button("Stage display replacement"))
    try {
      (void)replacement(target);
      result = 1;
    } catch (const std::exception &e) {
      error_ = e.what();
    }
  observe("stage", confirmed_);
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Discard editor changes"))
    result = 2;
  observe("cancel");
  if (!error_.empty())
    ImGui::TextWrapped("%s", error_.c_str());
  ImGui::EndDisabled();
  ImGui::End();
  if (!open_ || result) {
    payload_editor_.reset();
    payload_document_.reset();
    return !open_ ? 2 : result;
  }
  if (payload_editor_) {
    payload_editor_->draw(
        payload_document_.get(), false, cap,
        [&](const ResolvedPlanarDisplayAnnotationCommand &command) {
          auto edit = prepare_planar_display_annotation(command);
          payload_document_->commitDisplayAnnotation(edit);
        });
    const auto diagnostics = payload_editor_->diagnostics();
    for (const auto &control : diagnostics.at("controls").items())
      controls_["payload." + control.key()] = control.value();
    if (!payload_editor_->visible()) {
      if (auto row = payload_editor_->take_draft())
        seed(*row);
      payload_editor_.reset();
      payload_document_.reset();
    }
  }
  return !open_ ? 2 : result;
}
Json RoiDisplayReplacementEditor::diagnostics() const {
  return {{"index", index_},
          {"plane", plane_},
          {"slice", slice_},
          {"name", name_},
          {"confirmed", confirmed_},
          {"controls", controls_},
          {"payload_editor",
           payload_editor_ ? payload_editor_->diagnostics() : Json(nullptr)},
          {"window", bounds_},
          {"error", error_}};
}
} // namespace imagejxx::desktop
