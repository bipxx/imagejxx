#include "roi_import_replacement_editor.hpp"
#include "geometry_number.hpp"
#include <cmath>
#include <charconv>
#include <imagejxx/roi_style.hpp>
#include <imagejxx/selection_edit.hpp>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
namespace imagejxx::desktop {
RoiImportReplacementEditor::RoiImportReplacementEditor(std::size_t index,
                                                       DatasetShape recorded,
                                                       PlanarAnnotation row)
    : index_(index), recorded_(recorded), base_(std::move(row)) {
  seed(base_);
}
void RoiImportReplacementEditor::seed(const PlanarAnnotation &row) {
  base_ = row;
  name_ = row.name;
  plane_ = int(row.plane);
  slice_ = row.coordinate + 1;
  all_t_ = !row.t;
  all_c_ = !row.c;
  t_ = row.t.value_or(0) + 1;
  c_ = row.c.value_or(0) + 1;
  const auto style = resolve_roi_style(row.selection, {64, 210, 240, 255});
  stroke_ = roi_color_string(style.stroke_color);
  fill_ = roi_color_string(style.fill_color);
  stroke_width_ = geometry_number_text(style.stroke_width);
  geometry_dirty_ = point_dirty_ = style_dirty_ = false;
  corner_diameter_ = row.selection.is_rounded_box() ? std::to_string(row.selection.corner_diameter()) : std::string{};
  vertex_ = 1;
  load_vertex();
  error_.clear();
  if (const auto frame = row.selection.box_frame()) {
    const std::array<double, 5> v{frame->center[0], frame->center[1],
                                  2 * std::hypot(frame->u[0], frame->u[1]),
                                  2 * std::hypot(frame->v[0], frame->v[1]),
                                  std::atan2(frame->u[1], frame->u[0]) * 180 /
                                      std::acos(-1.)};
    for (int i = 0; i < 5; ++i)
      box_[i] = geometry_number_text(v[i]);
  }
}
void RoiImportReplacementEditor::load_vertex() {
  if (vertex_ > 0 && std::size_t(vertex_) <= base_.selection.points.size())
    for (int i = 0; i < 2; ++i)
      point_[i] = geometry_number_text(base_.selection.points[vertex_ - 1][i]);
  point_dirty_ = false;
}
Selection RoiImportReplacementEditor::vertex_geometry() const {
  if (vertex_ < 1 || std::size_t(vertex_) > base_.selection.points.size())
    throw std::invalid_argument("Choose a valid vertex");
  auto geometry = base_.selection;
  geometry.points[vertex_ - 1] = {parse_geometry_number(point_[0]),
                                  parse_geometry_number(point_[1])};
  return geometry;
}
void RoiImportReplacementEditor::change_geometry(int kind,
                                                 DatasetShape target) {
  const auto dims = planar_annotation_geometry(target, Plane(plane_));
  const double u = dims.width, v = dims.height;
  const char *names[]{"rectangle", "ellipse",  "line",      "point",
                      "polygon",   "polyline", "multipoint"};
  std::vector<std::array<double, 2>> points{{u * .25, v * .25},
                                            {u * .75, v * .75}};
  if (kind == 3)
    points.resize(1);
  if (kind == 4)
    points = {{u * .25, v * .25}, {u * .75, v * .25}, {u * .5, v * .75}};
  auto updated = base_;
  updated.selection = Selection(names[kind], points, base_.selection.width);
  updated.selection.attributes = base_.selection.attributes;
  // Geometry replacement preserves the user's other pending fields/style.
  const auto name = name_, stroke = stroke_, fill = fill_,
             width = stroke_width_;
  const bool all_t = all_t_, all_c = all_c_, styled = style_dirty_;
  const int slice = slice_, t = t_, c = c_, plane = plane_;
  seed(updated);
  name_ = name;
  stroke_ = stroke;
  fill_ = fill;
  stroke_width_ = width;
  style_dirty_ = styled;
  all_t_ = all_t;
  all_c_ = all_c;
  slice_ = slice;
  t_ = t;
  c_ = c;
  plane_ = plane;
}
ImageJRoiNativeReplacement
RoiImportReplacementEditor::replacement(DatasetShape target) const {
  if (!confirmed_)
    throw std::invalid_argument("Confirm use of the current destination");
  if (slice_ < 1 || (!all_t_ && t_ < 1) || (!all_c_ && c_ < 1))
    throw std::invalid_argument(
        "Slice, time and channel must be positive 1-based indices");
  auto row = base_;
  row.name = name_;
  row.plane = Plane(plane_);
  row.coordinate = slice_ - 1;
  row.t = all_t_ ? std::optional<int>{} : t_ - 1;
  row.c = all_c_ ? std::optional<int>{} : c_ - 1;
  if (geometry_dirty_ && row.selection.box_frame()) {
    std::array<double, 5> n{};
    for (int i = 0; i < 5; ++i)
      n[i] = parse_geometry_number(box_[i]);
    const bool ellipse = row.selection.kind == "ellipse" ||
                         row.selection.kind == "rotated_ellipse";
    Selection unit(row.selection.is_rounded_box() ? "rounded_rectangle" : ellipse ? "ellipse" : "rectangle", {{0, 0}, {1, 1}},
                   row.selection.width);
    unit.attributes = row.selection.attributes;
    const auto dims = planar_annotation_geometry(target, row.plane);
    row.selection =
        SelectionEditor(unit, dims.width, dims.height, SelectionEditLimits{})
            .box_geometry({n[0], n[1]}, {n[2], n[3]}, n[4]);
  } else if (point_dirty_)
    row.selection = vertex_geometry();
  if (row.selection.is_rounded_box()) {
    int diameter = 0;
    const auto parsed = std::from_chars(corner_diameter_.data(), corner_diameter_.data()+corner_diameter_.size(), diameter);
    if (parsed.ec != std::errc{} || parsed.ptr != corner_diameter_.data()+corner_diameter_.size() || diameter < 0)
      throw std::invalid_argument("Corner diameter requires an integer in 0..INT_MAX");
    row.selection.attributes["corner_diameter"] = std::int64_t(diameter);
  }
  if (style_dirty_)
    row.selection = with_roi_style(
        row.selection,
        {{"stroke_color", stroke_},
         {"fill_color", fill_},
         {"stroke_width", parse_geometry_number(stroke_width_)}});
  row.validate(target);
  (void)encode_planar_annotations(target, {row});
  return {target, std::move(row)};
}
void RoiImportReplacementEditor::observe(const std::string &key, bool enabled,
                                         ImGuiID id) {
  const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
  controls_[key] = {
      {"id", id ? id : ImGui::GetItemID()},
      {"visible",ImGui::IsItemVisible()},
      {"enabled",
       enabled && !(ImGui::GetCurrentContext()->LastItemData.ItemFlags &
                    ImGuiItemFlags_Disabled)},
      {"rect", {a.x, a.y, b.x, b.y}},
      {"point", {(a.x + b.x) / 2, (a.y + b.y) / 2}}};
  if (id)
    controls_[key]["point"][0] = a.x + 35;
}
bool RoiImportReplacementEditor::text(const char *label, std::string &value,
                                      const char *key, float width) {
  ImGui::SetNextItemWidth(width);
  const bool changed = ImGui::InputText(label, &value);
  observe(key);
  controls_[key]["point"][0] = ImGui::GetItemRectMin().x + width / 2;
  return changed;
}
bool RoiImportReplacementEditor::combo(const char *label, int &value,
                                       const char *const *names, int count,
                                       const char *key) {
  bool changed = false;
  ImGui::SetNextItemWidth(200);
  if (ImGui::BeginCombo(label, names[value])) {
    for (int i = 0; i < count; ++i) {
      ImGui::PushID(i);
      if (ImGui::Selectable(names[i], value == i)) {
        value = i;
        changed = true;
      }
      observe(std::string(key) + "." + std::to_string(i));
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  observe(key);
  return changed;
}
int RoiImportReplacementEditor::draw(
    DatasetShape target, const std::vector<PlanarAnnotation> &rows) {
  controls_ = Json::object();
  if (focus_)
    ImGui::SetNextWindowFocus();
  focus_ = false;
  ImGui::SetNextWindowSize({660, 690}, ImGuiCond_FirstUseEver);
  ImGui::Begin("Native ROI replacement", &open_);
  const auto a=ImGui::GetCurrentWindow()->InnerRect.Min,b=ImGui::GetCurrentWindow()->InnerRect.Max;bounds_={a.x,a.y,b.x,b.y};
  ImGui::TextWrapped("Choose a complete native annotation for this "
                     "destination. The imported file bytes stay unchanged.");
  const auto old = recorded_, d = target;
  ImGui::Text("Recorded T/C/Z/Y/X/S: %d/%d/%d/%d/%d/%d", old.t, old.c, old.z,
              old.y, old.x, old.s);
  ImGui::Text("Destination T/C/Z/Y/X/S: %d/%d/%d/%d/%d/%d", d.t, d.c, d.z, d.y,
              d.x, d.s);
  ImGui::Checkbox("Use this destination extent", &confirmed_);
  observe("confirm");
  if (ImGui::BeginCombo("Load annotation from image",
                        "Choose a complete annotation")) {
    ImGuiListClipper clip;
    clip.Begin(int(rows.size()));
    while (clip.Step())
      for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
        ImGui::PushID(i);
        if (ImGui::Selectable(rows[i].name.c_str()))
          seed(rows[i]);
        observe("load." + std::to_string(i));
        ImGui::PopID();
      }
    ImGui::EndCombo();
  }
  observe("load");
  text("Name", name_, "name", 240);
  const char *planes[]{"XY", "XZ", "YZ"};
  combo("Plane", plane_, planes, 3, "plane");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(80);
  const auto sid = ImGui::GetID("Slice (1-based)");
  ImGui::InputInt("Slice (1-based)", &slice_, 0, 0);
  observe("slice", true, sid);
  ImGui::Checkbox("All time points", &all_t_);
  observe("all_t");
  ImGui::SameLine();
  ImGui::BeginDisabled(all_t_);
  ImGui::SetNextItemWidth(80);
  const auto tid = ImGui::GetID("Time (1-based)");
  ImGui::InputInt("Time (1-based)", &t_, 0, 0);
  observe("t", !all_t_, tid);
  ImGui::EndDisabled();
  ImGui::Checkbox("All scientific channels", &all_c_);
  observe("all_c");
  ImGui::SameLine();
  ImGui::BeginDisabled(all_c_);
  ImGui::SetNextItemWidth(80);
  const auto cid = ImGui::GetID("Channel (1-based)");
  ImGui::InputInt("Channel (1-based)", &c_, 0, 0);
  observe("c", !all_c_, cid);
  ImGui::EndDisabled();
  int kind = 0;
  const char *kinds[]{"Keep current geometry",
                      "New rectangle",
                      "New ellipse",
                      "New line",
                      "New point",
                      "New polygon",
                      "New polyline",
                      "New multipoint"};
  if (combo("Geometry", kind, kinds, 8, "geometry") && kind > 0)
    change_geometry(kind - 1, target);
  if (base_.selection.box_frame()) {
    geometry_dirty_ |= text("Center U", box_[0], "center_u");
    ImGui::SameLine();
    geometry_dirty_ |= text("Center V", box_[1], "center_v");
    geometry_dirty_ |= text("Width", box_[2], "width");
    ImGui::SameLine();
    geometry_dirty_ |= text("Height", box_[3], "height");
    ImGui::SameLine();
    geometry_dirty_ |= text("Angle", box_[4], "angle", 80);
    if (base_.selection.is_rounded_box())
      text("Corner diameter", corner_diameter_, "corner_diameter");
  } else if (base_.selection.kind != "mask" &&
             !base_.selection.points.empty()) {
    ImGui::Text("%s: %zu vertices", base_.selection.kind.c_str(),
                base_.selection.points.size());
    ImGui::SetNextItemWidth(80);
    int next = vertex_;
    const auto id = ImGui::GetID("Vertex (1-based)");
    if (ImGui::InputInt("Vertex (1-based)", &next, 0, 0))
      try {
        if (point_dirty_)
          base_.selection = vertex_geometry();
        vertex_ = next;
        load_vertex();
      } catch (const std::exception &e) {
        error_ = e.what();
      }
    observe("vertex", true, id);
    point_dirty_ |= text("Vertex U", point_[0], "vertex_u");
    ImGui::SameLine();
    point_dirty_ |= text("Vertex V", point_[1], "vertex_v");
    if (ImGui::Button("Stage vertex"))
      try {
        base_.selection = vertex_geometry();
        point_dirty_ = false;
        error_.clear();
      } catch (const std::exception &e) {
        error_ = e.what();
      }
    observe("stage_vertex");
    const auto &k = base_.selection.kind;
    const bool variable = k == "polygon" || k == "freehand" ||
                          k == "polyline" || k == "multipoint";
    ImGui::SameLine();
    ImGui::BeginDisabled(!variable || base_.selection.points.size() >= 65536);
    if (ImGui::Button("Add vertex"))
      try {
        if (point_dirty_)
          base_.selection = vertex_geometry();
        base_.selection.points.push_back(base_.selection.points.back());
        vertex_ = int(base_.selection.points.size());
        load_vertex();
      } catch (const std::exception &e) {
        error_ = e.what();
      }
    observe("add_vertex", variable && base_.selection.points.size() < 65536);
    ImGui::EndDisabled();
    const std::size_t minimum = k == "multipoint" ? 1 : k == "polyline" ? 2 : 3;
    ImGui::SameLine();
    const bool removable =
        variable && base_.selection.points.size() > minimum && vertex_ > 0 &&
        std::size_t(vertex_) <= base_.selection.points.size();
    ImGui::BeginDisabled(!removable);
    if (ImGui::Button("Remove vertex")) {
      base_.selection.points.erase(base_.selection.points.begin() + vertex_ -
                                   1);
      vertex_ = std::min(vertex_, int(base_.selection.points.size()));
      load_vertex();
    }
    observe("remove_vertex", removable);
    ImGui::EndDisabled();
  } else
    ImGui::TextWrapped("Mask size is retained. Load a compatible image "
                       "annotation or choose another geometry.");
  style_dirty_ |= text("Stroke (#AARRGGBB/none)", stroke_, "stroke", 130);
  ImGui::SameLine();
  style_dirty_ |= text("Fill (#AARRGGBB/none)", fill_, "fill", 130);
  style_dirty_ |= text("Stroke width", stroke_width_, "stroke_width");
  const bool keep = ImGui::Button("Keep replacement");
  observe("keep");
  ImGui::SameLine();
  const bool discard = ImGui::Button("Discard edits");
  observe("discard");
  if (!error_.empty())
    ImGui::TextWrapped("%s", error_.c_str());
  const bool escape =
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
      ImGui::IsKeyPressed(ImGuiKey_Escape);
  ImGui::End();
  if (!open_ || discard || escape)
    return 2;
  if (keep)
    try {
      (void)replacement(target);
      error_.clear();
      return 1;
    } catch (const std::exception &e) {
      error_ = e.what();
    }
  return 0;
}
Json RoiImportReplacementEditor::diagnostics() const {
  return {{"visible", open_},
          {"window",bounds_},
          {"member", index_},
          {"confirmed", confirmed_},
          {"error", error_},
          {"controls", controls_},
          {"name", name_},
          {"plane", plane_},
          {"slice", slice_},
          {"kind", base_.selection.kind},
          {"corner_diameter", corner_diameter_},
          {"vertices", base_.selection.points.size()}};
}
} // namespace imagejxx::desktop
