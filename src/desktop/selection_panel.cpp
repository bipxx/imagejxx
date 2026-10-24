#include "selection_panel.hpp"
#include "path_visual.hpp"
#include <imagejxx/composite_path.hpp>
#include "geometry_number.hpp"
#include <algorithm>
#include <charconv>
#include <limits>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <locale>
#include <sstream>
#include <imgui_stdlib.h>

namespace imagejxx::desktop {
namespace {
std::array<double, 2> pair(const Json &value, bool strict = false) {
  if (!value.is_array() || value.size() != 2 || !value[0].is_number() ||
      !value[1].is_number())
    throw std::invalid_argument(
        "Selection property needs two numeric coordinates");
  auto result = value.get<std::array<double, 2>>();
  if (strict)
    for (std::size_t i = 0; i < 2; ++i) {
      if (value[i].is_number_unsigned()) {
        const auto n = value[i].get<std::uint64_t>();
        if (result[i] < 0 || result[i] >= std::ldexp(1., 64) ||
            static_cast<std::uint64_t>(result[i]) != n)
          throw std::invalid_argument("Selection coordinate loses integer precision");
      } else if (value[i].is_number_integer()) {
        const auto n = value[i].get<std::int64_t>();
        if (result[i] < -std::ldexp(1., 63) || result[i] >= std::ldexp(1., 63) ||
            static_cast<std::int64_t>(result[i]) != n)
          throw std::invalid_argument("Selection coordinate loses integer precision");
      }
    }
  for (auto item : result)
    require_finite(item, "Selection property coordinate");
  return result;
}
void checkColor(const std::string &value) {
  if ((value.size() != 7 && value.size() != 9) || value[0] != '#' ||
      !std::all_of(value.begin() + 1, value.end(),
                   [](unsigned char c) { return std::isxdigit(c) != 0; }))
    throw std::invalid_argument("Selection color must be #RRGGBB or #RRGGBBAA");
}
} // namespace
void SelectionPanel::show(const DocumentSnapshot &source) {
  if (!source.data().selection)
    throw std::invalid_argument("Draw a selection first");
  initialize(source, *source.data().selection, false, {}, {}, {});
}
void SelectionPanel::show_saved_roi_geometry(
    const DocumentSnapshot &source, const Selection &selection, std::size_t row,
    const SavedRoiGeometryLimits &limits, const ExecutionControl &control) {
  const auto identity = std::make_shared<const int>(0);
  draftIdentity_ = identity;
  try {
  if (row >= source.data().rois.size() ||
      source.data().rois[row].selection != selection)
    throw std::invalid_argument("Saved area target does not match its captured row");
  // The native factory owns visibility, area/style and complete resource
  // admission; this panel never installs a temporary global selection.
  const auto admitted = resolve_saved_roi_geometry(
      source, SavedRoiGeometryRequest(row, selection), limits, control);
  if (draftIdentity_ != identity) throw OperationCancelled();
  initialize(admitted.source(), admitted.original_roi().selection, true, row,
             admitted.limits(), control);
  if (draftIdentity_ != identity) { cancel(); throw OperationCancelled(); }
  } catch (...) { cancel(); throw; }
}
void SelectionPanel::initialize(
    const DocumentSnapshot &source, const Selection &selection,
    bool geometryOnly, std::optional<std::size_t> row,
    const SavedRoiGeometryLimits &limits, const ExecutionControl &control) {
  SelectionPanel next;
  next.source_ = std::make_shared<const DocumentSnapshot>(source);
  next.geometryOnly_ = geometryOnly;
  next.draftIdentity_ = geometryOnly ? draftIdentity_ : nullptr;
  next.savedRow_ = row;
  next.savedLimits_ = limits;
  next.geometryLimits_ = {limits.max_xy_pixels, limits.max_vertices,
                          limits.max_working_bytes};
  const auto shape = source.data().dataset.shape();
  if (geometryOnly)
    next.editor_.emplace(selection, shape.x, shape.y, next.geometryLimits_, control);
  else
    next.editor_.emplace(selection, shape.x, shape.y);
  next.preview_ = selection;
  next.points_ = planarControlPoints(*next.preview_);
  next.width_ = next.preview_->width;
  next.rounded_ = next.preview_->is_rounded_box();
  if (next.rounded_) next.cornerDiameter_ = next.preview_->corner_diameter();
  next.cornerText_ = std::to_string(next.cornerDiameter_);
  next.area_ = selection_is_area(*next.preview_);
  const auto style = resolve_roi_style(*next.preview_);
  next.strokeWidth_ = style.stroke_width;
  next.strokeColor_ = roi_color_string(style.stroke_color);
  next.fillColor_ = roi_color_string(style.fill_color);
  const auto typedStyle = imagejxx::roi_style(*next.preview_);
  next.strokeWidthEnabled_ =
      next.preview_->kind != "point" && next.preview_->kind != "multipoint";
  next.legacyColorEnabled_ = !typedStyle.has_stroke && !typedStyle.fill_color;
  next.legacyWidthEnabled_ =
      !typedStyle.stroke_width || !next.strokeWidthEnabled_;
  next.open_ = true;
  next.focus_ = true;
  if (const auto frame = next.preview_->box_frame()) {
    next.box_ = true;
    next.center_ = frame->center;
    next.size_ = {2 * std::hypot(frame->u[0], frame->u[1]),
                  2 * std::hypot(frame->v[0], frame->v[1])};
    next.angle_ = std::atan2(frame->u[1], frame->u[0]) * 180 / std::acos(-1.);
  }
  const auto json = next.preview_->is_path()?metadataJson(next.preview_->attributes):selectionJson(*next.preview_);
  next.label_ = string(get(json, "label"));
  if (get(json, "color").is_string()) {
    const auto value = string(get(json, "color"));
    try {
      checkColor(value);
      next.color_ = value;
    } catch (const std::invalid_argument &) {
    }
  }
  if (geometryOnly && draftIdentity_ != next.draftIdentity_)
    throw OperationCancelled();
  *this = std::move(next);
}
bool SelectionPanel::matches(const DocumentSnapshot &current) const {
  return source_ && source_->same_document(current) &&
         source_->revisions().matches(current.revisions(), Change::Data) &&
         (!geometryOnly_ ||
          (source_->display().t == current.display().t &&
           source_->display().c == current.display().c &&
           source_->display().crosshair[2] == current.display().crosshair[2]));
}
void SelectionPanel::rebuild(const ExecutionControl &control) {
  Selection result = editor_->selection();
  const auto shape = source_->data().dataset.shape();
  if (geometryChanged_) {
    if (box_)
      result = geometryOnly_
          ? editor_->box_geometry(center_, size_, angle_, geometryLimits_, control)
          : editor_->box_geometry(center_, size_, angle_);
    else {
      if(result.is_path()) {
        if(points_.size()!=selectionControlPointCount(result))throw std::invalid_argument("Path controls cannot change segment topology");
        auto commands=result.path_commands();std::size_t index=0;
        for(std::size_t at=0;at<commands.size();) {
          const int op=int(commands[at++]);const auto n=op==4?0u:op==3?6u:op==2?4u:2u;
          for(unsigned i=0;i<n;i+=2){commands[at+i]=points_[index][0];commands[at+i+1]=points_[index++][1];}
          at+=n;
        }
        auto replacement=Selection::from_path(ImageJCompositePath(std::move(commands)));
        replacement.width=result.width;replacement.attributes=result.attributes;result=std::move(replacement);
      } else result.points = points_;
      result.validate(shape.x, shape.y);
      if (geometryOnly_) {
        result = SelectionEditor(result, shape.x, shape.y, geometryLimits_, control)
                     .moved(translation_, false, geometryLimits_, control);
        result = SelectionEditor(result, shape.x, shape.y, geometryLimits_, control)
                     .rotated(rotation_, {}, geometryLimits_, control);
      } else {
        result = SelectionEditor(result, shape.x, shape.y).moved(translation_);
        result = SelectionEditor(result, shape.x, shape.y).rotated(rotation_);
      }
    }
  }
  if (cornerChanged_) result.attributes["corner_diameter"] = std::int64_t(cornerDiameter_);
  if (geometryOnly_) {
    const auto admitted = resolve_saved_roi_geometry(*source_,
        SavedRoiGeometryRequest(*savedRow_, std::move(result)), savedLimits_, control);
    preview_ = admitted.replacement();
    error_.clear();
    return;
  }
  MetadataObject patch;
  if (labelChanged_)
    patch["label"] = label_;
  if (colorChanged_) {
    checkColor(color_);
    patch["color"] = color_;
  }
  preview_ = SelectionEditor(std::move(result), shape.x, shape.y)
                 .properties(width_, patch);
  const auto baseStyle = imagejxx::roi_style(*preview_);
  if (!strokeChanged_ && !baseStyle.has_stroke && !baseStyle.fill_color)
    strokeColor_ = roi_color_string(resolve_roi_style(*preview_).stroke_color);
  if (!strokeWidthChanged_ && !baseStyle.stroke_width)
    strokeWidth_ = width_;
  MetadataObject stylePatch;
  if (fillChanged_) {
    stylePatch["fill_color"] = fillColor_;
    // The ImageJ properties dialog applies its displayed stroke after fill.
    stylePatch["stroke_color"] = strokeColor_;
  }
  if (strokeChanged_)
    stylePatch["stroke_color"] = strokeColor_;
  if (strokeWidthChanged_)
    stylePatch["stroke_width"] = strokeWidth_;
  if (!stylePatch.empty())
    preview_ =
        SelectionEditor(*preview_, shape.x, shape.y).roi_style(stylePatch);
  const auto effective = imagejxx::roi_style(*preview_);
  legacyColorEnabled_ = !effective.has_stroke && !effective.fill_color;
  legacyWidthEnabled_ = !effective.stroke_width || !strokeWidthEnabled_;
  error_.clear();
}
void SelectionPanel::patch(const Json &values, const ExecutionControl &control) {
  try {
  if (!open_ || !values.is_object())
    throw std::invalid_argument(
        "Open selection properties before editing its draft");
  auto next = *this;
  for (const auto &entry : values.items()) {
    const auto &name = entry.key();
    const auto &value = entry.value();
    if (geometryOnly_ && name != "center" && name != "size" &&
        name != "angle" && name != "rotation" && name != "translation" &&
        name != "points" && name != "corner_diameter")
      throw std::invalid_argument("Saved area geometry leaves width, name, style and attributes unchanged");
    if (name == "corner_diameter") {
      if (!next.rounded_ || !value.is_number_integer() ||
          (value.is_number_unsigned() ? value.get<std::uint64_t>() > 2147483647u
                                     : value.get<std::int64_t>() < 0 || value.get<std::int64_t>() > 2147483647))
        throw std::invalid_argument("Corner diameter requires an integer in 0..INT_MAX on a rounded box");
      next.cornerDiameter_ = value.get<int>();
      next.cornerText_ = std::to_string(next.cornerDiameter_);
      next.cornerChanged_ = true;
    } else if (name == "center" || name == "size") {
      if (!box_)
        throw std::invalid_argument(
            "Center/size fields require a box selection");
      (name == "center" ? next.center_ : next.size_) = pair(value, geometryOnly_ || editor_->selection().is_path());
      next.geometryChanged_ = true;
    } else if (name == "angle" || name == "rotation" || name == "width" ||
               name == "stroke_width") {
      if (!value.is_number())
        throw std::invalid_argument("Selection property must be numeric");
      const double n = value.get<double>();
      require_finite(n, "Selection property");
      if (name == "stroke_width") {
        if (!next.strokeWidthEnabled_)
          throw std::invalid_argument("Point marker size uses legacy width");
        next.strokeWidth_ = n;
        next.strokeWidthChanged_ = true;
      } else if (name == "width")
        next.width_ = n;
      else {
        if ((name == "angle") != box_)
          throw std::invalid_argument("Invalid angle field for this selection");
        (name == "angle" ? next.angle_ : next.rotation_) = n;
        next.geometryChanged_ = true;
      }
    } else if (name == "translation") {
      if (box_)
        throw std::invalid_argument("Box position uses center coordinates");
      next.translation_ = pair(value, geometryOnly_ || editor_->selection().is_path());
      next.geometryChanged_ = true;
    } else if (name == "points") {
      if (box_ || editor_->selection().kind == "mask" || !value.is_array())
        throw std::invalid_argument(
            "This selection does not expose vertex coordinates");
      if(editor_->selection().is_path() && value.size()!=points_.size())
        throw std::invalid_argument("Path controls cannot change segment topology");
      if (geometryOnly_ && value.size() > geometryLimits_.max_vertices)
        throw std::invalid_argument("Saved area vertex limit exceeded");
      next.points_.clear();
      for (const auto &point : value)
        next.points_.push_back(pair(point, geometryOnly_ || editor_->selection().is_path()));
      next.geometryChanged_ = true;
    } else if (name == "stroke_color" || name == "fill_color") {
      if (name == "fill_color" && !area_)
        throw std::invalid_argument("Fill color requires an area selection");
      if (!value.is_null() && !value.is_string())
        throw std::invalid_argument("ROI color must be text or null");
      const auto text = value.is_null() ? "none" : value.get<std::string>();
      parse_roi_color(text);
      if (name == "stroke_color") {
        next.strokeColor_ = text;
        next.strokeChanged_ = true;
      } else {
        next.fillColor_ = text;
        next.fillChanged_ = true;
      }
    } else if (name == "label" || name == "color") {
      if (!value.is_string())
        throw std::invalid_argument("Selection label/color must be text");
      if (name == "label") {
        next.label_ = value.get<std::string>();
        next.labelChanged_ = true;
      } else {
        next.color_ = value.get<std::string>();
        checkColor(next.color_);
        next.colorChanged_ = true;
      }
    } else
      throw std::invalid_argument("Unknown selection property");
  }
  if (geometryOnly_ || editor_->selection().is_path()) {
    for (const auto &entry : values.items()) {
      const auto &name = entry.key();
      if (name == "center") {
        next.rawNumbers_["Center X"] = geometry_number_text(next.center_[0]);
        next.rawNumbers_["Center Y"] = geometry_number_text(next.center_[1]);
      } else if (name == "size") {
        next.rawNumbers_["Width"] = geometry_number_text(next.size_[0]);
        next.rawNumbers_["Height"] = geometry_number_text(next.size_[1]);
      } else if (name == "translation") {
        next.rawNumbers_["Move X"] = geometry_number_text(next.translation_[0]);
        next.rawNumbers_["Move Y"] = geometry_number_text(next.translation_[1]);
      } else if (name == "angle") next.rawNumbers_["Angle (degrees)"] = geometry_number_text(next.angle_);
      else if (name == "rotation") next.rawNumbers_["Rotate (degrees)"] = geometry_number_text(next.rotation_);
      else if (name == "points")
        for (auto it = next.rawNumbers_.begin(); it != next.rawNumbers_.end(); )
          if (it->first.rfind("Vertex ", 0) == 0) it = next.rawNumbers_.erase(it);
          else ++it;
    }
    for (const auto &entry : next.rawNumbers_) (void)parse_geometry_number(entry.second);
  }
  next.rebuild(control);
  if (geometryOnly_ && (!open_ || draftIdentity_ != next.draftIdentity_))
    throw OperationCancelled();
  *this = std::move(next);
  } catch (const std::exception &e) {
    if (geometryOnly_) {
      preview_.reset();
      error_ = e.what();
    }
    throw;
  } catch (...) {
    if (geometryOnly_) { preview_.reset(); error_ = "Selection draft failed"; }
    throw;
  }
}
void SelectionPanel::apply(const Apply &publish) {
  if (!open_ || !preview_ || !error_.empty())
    throw std::invalid_argument("Fix the selection draft before applying");
  if (geometryOnly_) {
    const auto selection = *preview_;
    const auto source = *source_;
    try { publish(selection, source); }
    catch (...) { preview_.reset(); error_ = "Saved area publication failed"; throw; }
  } else publish(*preview_, *source_);
  cancel();
}
void SelectionPanel::cancel() {
  open_ = false;
  draftIdentity_.reset();
  source_.reset();
  editor_.reset();
  preview_.reset();
  savedRow_.reset();
  rawNumbers_.clear();
  std::vector<std::array<double, 2>>().swap(points_);
}
void SelectionPanel::draw(const std::optional<DocumentSnapshot> &current,
                          bool busy, const Apply &publish,
                          const ExecutionControl &execution) {
  applyPoint_ = {-1, -1};
  cancelPoint_ = {-1, -1};
  if (!open_)
    return;
  if (!current || !matches(*current) || (geometryOnly_ && busy)) {
    cancel();
    return;
  }
  ImGui::SetNextWindowSize({390, 600}, ImGuiCond_FirstUseEver);
  if (focus_)
    ImGui::SetNextWindowFocus();
  ImGui::Begin(geometryOnly_ ? "Saved area geometry" : "Selection properties", &open_);
  focus_ = false;
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
      ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    open_ = false;
    if (geometryOnly_) { ImGui::End(); cancel(); return; }
  }
  bool changed = false;
  std::string numericError;
  controls_ = Json::object();
  controlStates_ = Json::object();
  auto control = [&](const char *name, bool disabled = false, bool outsideBusy = false) {
    const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    controls_[name] = {a.x + (b.x - a.x) / 4, (a.y + b.y) / 2};
    controlStates_[name] = {{"id", ImGui::GetItemID()},
        {"rect", {a.x, a.y, b.x, b.y}}, {"focused", ImGui::IsItemFocused()},
        {"active", ImGui::IsItemActive()}, {"hovered", ImGui::IsItemHovered()},
        {"disabled", disabled || (!outsideBusy && busy)}};
  };
  auto number = [&](const char *name, double *value, bool disabled = false) {
    if (geometryOnly_ || editor_->selection().is_path()) {
      const std::string key = std::string(name) +
          ((std::string(name) == "Vertex X" || std::string(name) == "Vertex Y")
               ? "@" + std::to_string(vertex_) : std::string());
      auto entry = rawNumbers_.try_emplace(key, geometry_number_text(*value)).first;
      const bool edited = ImGui::InputText(name, &entry->second);
      control(name, disabled);
      try {
        const auto parsed = parse_geometry_number(entry->second);
        if (edited) *value = parsed;
      } catch (const std::exception &e) {
        if (numericError.empty())
          numericError = std::string(name) + ": " + e.what();
      }
      return edited;
    }
    const bool edited = ImGui::InputDouble(name, value, 0, 0, "%.12g");
    control(name, disabled);
    return edited;
  };
  ImGui::TextWrapped("Image pixel coordinates. Preview leaves source pixels "
                     "and geometry unchanged until Apply.");
  if (geometryOnly_ && savedRow_) {
    const auto &row = source_->data().rois[*savedRow_];
    auto position = [](const std::optional<int> &v) {
      return v ? std::to_string(*v + 1) : std::string("*");
    };
    ImGui::TextWrapped("Saved area %zu: %s | T %s C %s Z %s", *savedRow_ + 1,
                       row.name.c_str(), position(row.t).c_str(),
                       position(row.c).c_str(), position(row.z).c_str());
  }
  ImGui::BeginDisabled(busy);
  if (box_) {
    changed |= number("Center X", &center_[0]);
    changed |= number("Center Y", &center_[1]);
    changed |= number("Width", &size_[0]);
    changed |= number("Height", &size_[1]);
    changed |= number("Angle (degrees)", &angle_);
  } else {
    changed |= number("Move X", &translation_[0]);
    changed |= number("Move Y", &translation_[1]);
    changed |= number("Rotate (degrees)", &rotation_);
    if (!points_.empty() && editor_->selection().kind != "mask") {
      if(editor_->selection().is_path())ImGui::InputInt("Vertex", &vertex_,0,0);
      else ImGui::InputInt("Vertex", &vertex_);
      control("Vertex");
      vertex_ = std::clamp(vertex_, 0, int(points_.size() - 1));
      changed |= number("Vertex X", &points_[vertex_][0]);
      changed |= number("Vertex Y", &points_[vertex_][1]);
    }
  }
  if (changed)
    geometryChanged_ = true;
  if (rounded_) {
    const bool edited = ImGui::InputText("Corner diameter", &cornerText_);
    control("Corner diameter");
    int parsed = 0;
    const auto result = std::from_chars(cornerText_.data(), cornerText_.data()+cornerText_.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != cornerText_.data()+cornerText_.size() || parsed < 0) {
      numericError = "Corner diameter requires an integer in 0..INT_MAX";
    } else if (edited) {
      cornerDiameter_ = parsed; cornerChanged_ = true; changed = true;
    }
  }
  ImGui::BeginDisabled(geometryOnly_);
  ImGui::BeginDisabled(!legacyWidthEnabled_);
  changed |= number("Line width", &width_, geometryOnly_ || !legacyWidthEnabled_);
  ImGui::SetItemTooltip(
      "Legacy width remains stored; explicit stroke width is edited below.");
  ImGui::EndDisabled();
  if (ImGui::InputText("Name", &label_)) {
    labelChanged_ = true;
    changed = true;
  }
  control("Name", geometryOnly_);
  ImGui::BeginDisabled(!legacyColorEnabled_);
  if (ImGui::InputText("Color", &color_)) {
    colorChanged_ = true;
    changed = true;
  }
  control("Color", geometryOnly_ || !legacyColorEnabled_);
  ImGui::SetItemTooltip("Legacy color uses #RRGGBB or #RRGGBBAA.");
  ImGui::EndDisabled();
  if (ImGui::InputText("Stroke color", &strokeColor_)) {
    strokeChanged_ = true;
    changed = true;
  }
  control("Stroke color", geometryOnly_);
  ImGui::SetItemTooltip("ImageJ names, #RRGGBB, #AARRGGBB, or none.");
  ImGui::BeginDisabled(!area_);
  if (ImGui::InputText("Fill color", &fillColor_)) {
    fillChanged_ = true;
    changed = true;
  }
  control("Fill color", geometryOnly_ || !area_);
  ImGui::SetItemTooltip(
      "Area fill uses ImageJ names or #AARRGGBB; none removes fill.");
  ImGui::EndDisabled();
  ImGui::BeginDisabled(!strokeWidthEnabled_);
  if (number("Stroke width", &strokeWidth_, geometryOnly_ || !strokeWidthEnabled_)) {
    strokeWidthChanged_ = true;
    changed = true;
  }
  ImGui::SetItemTooltip(
      "Zero draws one device pixel; positive values use image pixels.");
  ImGui::EndDisabled();
  ImGui::EndDisabled();
  if (geometryOnly_ || editor_->selection().is_path())
    for (const auto &entry : rawNumbers_)
      try { (void)parse_geometry_number(entry.second); }
      catch (const std::exception &e) {
        if (numericError.empty()) numericError = entry.first + ": " + e.what();
      }
  if (!numericError.empty()) {
    error_ = numericError;
    preview_.reset();
  } else if (changed) {
    try {
      if (geometryOnly_) {
        auto next = *this;
        next.rebuild(execution);
        if (!open_ || draftIdentity_ != next.draftIdentity_)
          throw OperationCancelled();
        *this = std::move(next);
      } else rebuild(execution);
    } catch (const std::exception &e) {
      error_ = e.what();
      preview_.reset();
    } catch (...) {
      error_ = "Selection draft failed";
      preview_.reset();
    }
  }
  if (!error_.empty())
    ImGui::TextWrapped("%s", error_.c_str());
  const bool applyDisabled = !preview_ || !error_.empty();
  ImGui::BeginDisabled(applyDisabled);
  if (ImGui::Button("Apply")) {
    try {
      apply(publish);
    } catch (const std::exception &e) {
      error_ = e.what();
      preview_.reset();
    } catch (...) {
      error_ = "Saved area publication failed";
      preview_.reset();
    }
  }
  const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
  control("Apply", applyDisabled);
  applyPoint_ = {(a.x + b.x) / 2, (a.y + b.y) / 2};
  ImGui::EndDisabled();
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel"))
    open_ = false;
  const auto cancelMin = ImGui::GetItemRectMin(),
             cancelMax = ImGui::GetItemRectMax();
  control("Cancel", false, true);
  cancelPoint_ = {(cancelMin.x + cancelMax.x) / 2,
                  (cancelMin.y + cancelMax.y) / 2};
  ImGui::End();
  if (!open_)
    cancel();
}
Json SelectionPanel::diagnostics() const {
  return {{"open", open_},
          {"geometry_only", geometryOnly_},
          {"saved_row_index", savedRow_ ? Json(*savedRow_) : Json()},
          {"numeric_text", rawNumbers_},
          {"control_states", controlStates_},
          {"box", box_},
          {"center", center_},
          {"size", size_},
          {"angle", angle_},
          {"translation", translation_},
          {"rotation", rotation_},
          {"width", width_},
          {"rounded", rounded_},
          {"corner_diameter", cornerDiameter_},
          {"corner_text", cornerText_},
          {"label", label_},
          {"color", color_},
          {"area", area_},
          {"stroke_color", strokeColor_},
          {"fill_color", fillColor_},
          {"stroke_width", strokeWidth_},
          {"legacy_color_enabled", legacyColorEnabled_ && !geometryOnly_},
          {"legacy_width_enabled", legacyWidthEnabled_ && !geometryOnly_},
          {"stroke_width_enabled", strokeWidthEnabled_ && !geometryOnly_},
          {"error", error_},
          {"apply_point", {applyPoint_.x, applyPoint_.y}},
          {"cancel_point", {cancelPoint_.x, cancelPoint_.y}},
          {"controls", controls_},
          {"preview", preview_ ? selectionJson(*preview_) : Json::object()}};
}
} // namespace imagejxx::desktop
