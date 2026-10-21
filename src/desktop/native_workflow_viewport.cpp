#include "native_workflow_viewport.hpp"
#include "../native/kernel_control.hpp"
#include "display_annotation_renderer.hpp"
#include "orthogonal.hpp"
#include "planar_annotation_visual.hpp"
#include <imgui_internal.h>
namespace imagejxx::desktop {
void admit_native_workflow_viewport(const DocumentSnapshot &source,
                                    std::size_t cap) {
  const auto shape = source.data().dataset.shape();
  // Three retained source/RGB planes, extraction/composition scratch, texture
  // uploads and optional diagnostic copies. Account the largest orientation
  // conservatively, including contributing channels, before reading pixels.
  const auto extent =
      std::max({std::size_t(shape.x) * shape.y, std::size_t(shape.x) * shape.z,
                std::size_t(shape.y) * shape.z});
  if (!cap || extent > cap / 1024)
    throw std::invalid_argument(
        "Workflow viewport exceeds render memory setting");
  // The desktop facade materializes metadata and legacy visual caches too.
  // Bound these before constructing it; immutable provenance is not omitted.
  std::size_t metadata_bytes = 0, legacy_bytes = 0;
  auto charge = [cap](std::size_t &used, std::size_t count, std::size_t unit) {
    if (count > (cap / 8 - used) / unit)
      throw std::invalid_argument(
          "Workflow viewport metadata/visual memory setting");
    used += count * unit;
  };
  std::function<void(const MetadataValue &)> value;
  auto object = [&](const MetadataObject &object) {
    for (const auto &[key, child] : object) {
      charge(metadata_bytes, key.size(), 4);
      value(child);
    }
  };
  value = [&](const MetadataValue &node) {
    kernel_checkpoint_periodic();
    charge(metadata_bytes, 1, 256);
    std::visit(
        [&](const auto &v) {
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<T, std::string>)
            charge(metadata_bytes, v.size(), 4);
          else if constexpr (std::is_same_v<T, std::vector<std::string>>)
            for (const auto &text : v) {
              charge(metadata_bytes, 1, 256);
              charge(metadata_bytes, text.size(), 4);
            }
          else if constexpr (std::is_same_v<
                                 T, std::shared_ptr<const MetadataTree>>) {
            if (!v)
              throw std::invalid_argument("Null viewport metadata tree");
            if (const auto p = std::get_if<MetadataObject>(&v->value))
              object(*p);
            else
              for (const auto &child : std::get<MetadataArray>(v->value))
                value(child);
          }
        },
        node);
  };
  const auto &data = source.data();
  object(data.metadata.extensions);
  object(data.metadata.properties);
  object(data.metadata.record_layout);
  object(source.display().properties);
  charge(metadata_bytes, data.metadata.title.size(), 4);
  auto selection = [&](const Selection &selection, bool visible) {
    charge(metadata_bytes, selection.points.size(), 128);
    charge(metadata_bytes, selection.packed_mask().size(), 4);
    object(selection.attributes);
    if (visible)
      charge(legacy_bytes, extent, 96);
  };
  if (data.selection)
    selection(*data.selection, true);
  if (data.previous_selection)
    selection(*data.previous_selection, false);
  for (const auto &roi : data.rois) {
    selection(roi.selection, true);
    object(roi.attributes);
    charge(metadata_bytes, roi.name.size(), 4);
  }
  for (const auto &overlay : data.overlays) {
    object(overlay);
    charge(legacy_bytes, extent, 128);
  }
  admit_planar_annotation_visuals(shape, planar_annotations(source.data()),
                                  cap / 8);
  admit_display_annotation_visuals(planar_display_annotations(source.data()),
                                   cap / 8);
}
struct NativeWorkflowViewport::State {
  Document document;
  OrthogonalViews planes;
  DisplayAnnotationRenderer annotations;
  std::size_t cap;
  bool open = true, focus = true, readback = false;
  Json controls = Json::object();
  std::string status;
  State(const DocumentSnapshot &source, std::size_t limit)
      : document(std::make_shared<imagejxx::Document>(source)), cap(limit) {}
  void observe(const char *name) {
    const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    controls[name] = {
        {"id", ImGui::GetItemID()},
        {"enabled", !(ImGui::GetCurrentContext()->LastItemData.ItemFlags &
                      ImGuiItemFlags_Disabled)},
        {"visible", ImGui::IsItemVisible()},
        {"point", {a.x + std::min(40.f, (b.x - a.x) / 2), (a.y + b.y) / 2}},
        {"rect", {a.x, a.y, b.x, b.y}}};
  }
};
NativeWorkflowViewport::NativeWorkflowViewport(const DocumentSnapshot &source,
                                               std::size_t cap) {
  admit_native_workflow_viewport(source, cap);
  state_ = std::make_unique<State>(source, cap);
}
NativeWorkflowViewport::~NativeWorkflowViewport() { release_textures(); }
void NativeWorkflowViewport::release_textures() {
  state_->planes.releaseTextures();
  state_->annotations.release_textures();
}
bool NativeWorkflowViewport::draw(bool gl, bool enabled, bool diagnostics) {
  auto &s = *state_;
  s.controls = Json::object();
  ImGui::SetNextWindowSize({1040, 620}, ImGuiCond_FirstUseEver);
  if (s.focus) {
    ImGui::SetNextWindowFocus();
    s.focus = false;
  }
  const bool expanded = ImGui::Begin("Prepared workflow image", &s.open);
  if (expanded) {
    ImGui::TextWrapped("Private preview. Navigation changes only this viewer. "
                       "Image, workflow and output files stay unchanged.");
    ImGui::BeginDisabled(!enabled);
    const auto shape = s.document.shape();
    auto display = s.document.nativeSnapshot().display();
    int t = display.t, c = display.c;
    ImGui::SetNextItemWidth(90);
    bool changed = ImGui::InputInt("Time (zero based)", &t, 0, 0);
    s.observe("t");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    changed |= ImGui::InputInt("Channel (zero based)", &c, 0, 0);
    s.observe("c");
    if (changed) {
      display.t = std::clamp(t, 0, shape.t - 1);
      display.c = std::clamp(c, 0, shape.c - 1);
      s.document.setNativeDisplay(std::move(display));
    }
    ImGui::SameLine();
    if (ImGui::Button("Close preview"))
      s.open = false;
    s.observe("close");
    OrthogonalInputContext input;
    input.active_document = enabled;
    input.input_enabled = enabled;
    input.keyboard_admitted = enabled;
    // Navigation is enabled; annotation publication is deliberately absent.
    input.annotation_admitted = false;
    input.read_only = true;
    input.annotation_max_working_bytes = s.cap / 8;
    input.display_annotations = [&](const PlanarCanvas &canvas,
                                    ImDrawList *draw) {
      return s.annotations.draw(s.document.nativeSnapshot(), canvas, draw, gl,
                                true, s.cap / 8, diagnostics);
    };
    s.readback = diagnostics;
    ChannelDisplayLimits limits;
    limits.max_working_bytes = s.cap / 4;
    limits.max_pixels = std::min(limits.max_pixels, s.cap / 1024);
    const auto result =
        s.planes.draw(s.document, gl, true, false, s.status, input, nullptr, 0,
                      diagnostics, nullptr, limits);
    if (result.escape_requested)
      s.open = false;
    ImGui::EndDisabled();
  } else {
    s.planes.resetInput();
  }
  ImGui::End();
  return s.open;
}
Json NativeWorkflowViewport::diagnostics() const {
  const auto &s = *state_;
  return {{"visible", s.open},
          {"document_id", s.document.id},
          {"display", s.document.viewJson()},
          {"controls", s.controls},
          {"planes", s.planes.diagnostics(s.readback)},
          {"render_cap", s.cap},
          {"undo", s.document.undoSteps()},
          {"status", s.status}};
}
} // namespace imagejxx::desktop
