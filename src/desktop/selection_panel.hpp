#pragma once
#include "desktop.hpp"
#include "path_segment_controls.hpp"
#include "path_authoring_controls.hpp"
#include <imagejxx/selection_edit.hpp>
#include <imagejxx/saved_roi_geometry.hpp>
#include <imgui.h>
#include <map>
#include <algorithm>

namespace imagejxx::desktop {
class SelectionPanel {
public:
  using Apply =
      std::function<void(const Selection &, const DocumentSnapshot &)>;
  void show(const DocumentSnapshot &source);
  void show_saved_roi_geometry(const DocumentSnapshot &source,
                               const Selection &selection, std::size_t row,
                               const SavedRoiGeometryLimits &limits,
                               const ExecutionControl &control = {});
  void patch(const Json &values, const ExecutionControl &control = {});
  void apply(const Apply &publish);
  void cancel();
  void draw(const std::optional<DocumentSnapshot> &current, bool busy,
            const Apply &publish, const ExecutionControl &control = {});
  bool visible() const { return open_; }
  bool matches(const DocumentSnapshot &current) const;
  const std::optional<Selection> &preview() const { return preview_; }
  bool geometry_only() const { return geometryOnly_; }
  bool path_edited() const { return pathEdited_; }
  void working_limit(std::size_t cap) { geometryLimits_.max_working_bytes = std::min(savedLimits_.max_working_bytes, cap); }
  Json diagnostics() const;

private:
  void initialize(const DocumentSnapshot &, const Selection &, bool,
                  std::optional<std::size_t>, const SavedRoiGeometryLimits &,
                  const ExecutionControl &);
  void rebuild(const ExecutionControl &control = {});
  PathSegmentControls pathControls_;
  PathResizeControls resizeControls_;
  bool pathTransform_ = false;
  bool pathEdited_ = false;
  bool geometryOnly_ = false;
  std::shared_ptr<const int> draftIdentity_;
  std::optional<std::size_t> savedRow_;
  SavedRoiGeometryLimits savedLimits_;
  SelectionEditLimits geometryLimits_;
  std::map<std::string, std::string> rawNumbers_;
  bool open_ = false, focus_ = false, box_ = false, geometryChanged_ = false,
       labelChanged_ = false, colorChanged_ = false, area_ = false,
       strokeChanged_ = false, fillChanged_ = false,
       strokeWidthChanged_ = false;
  bool legacyColorEnabled_ = true, legacyWidthEnabled_ = true,
       strokeWidthEnabled_ = true;
  std::shared_ptr<const DocumentSnapshot> source_;
  std::optional<SelectionEditor> editor_;
  std::optional<Selection> preview_;
  std::array<double, 2> center_{}, size_{}, translation_{};
  std::vector<std::array<double, 2>> points_;
  double angle_ = 0, rotation_ = 0, width_ = 1, strokeWidth_ = 1;
  std::string label_, color_ = "#ffd54bff", strokeColor_, fillColor_ = "none",
                      error_;
  int vertex_ = 0, cornerDiameter_ = 0;
  bool rounded_ = false, cornerChanged_ = false;
  std::string cornerText_;
  ImVec2 applyPoint_{-1, -1}, cancelPoint_{-1, -1};
  Json controls_ = Json::object();
  Json controlStates_ = Json::object();
};
} // namespace imagejxx::desktop
