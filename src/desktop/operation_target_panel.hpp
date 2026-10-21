#pragma once

#include "desktop.hpp"
#include <imagejxx/targets.hpp>
#include <array>
#include <optional>
#include <string>

namespace imagejxx::desktop {

// Metadata-only controls shared by plane-operation panels. The native target
// resolver remains authoritative for positions, counts and ROI semantics.
class OperationTargetPanel {
public:
  struct PreviewDraw {
    bool changed = false;
    bool show_requested = false;
  };

  void capture(const DocumentSnapshot &source);
  bool draw(bool enabled = true);
  bool enterConsumed() const { return enterConsumed_; }
  TargetRequest request() const;
  // Call after resolving the complete Apply target. Retains a contained preview
  // choice, otherwise prefers the captured position and then the first target.
  void resolvePreview(const ResolvedTarget &target);
  PlanePosition previewPosition() const;
  TargetRequest previewRequest(const ResolvedTarget &target) const;
  PreviewDraw drawPreview(const ResolvedTarget &target, PlanePosition canvas,
                          bool preview_requested, bool staged_preview,
                          bool show_enabled = true);
  Json diagnostics() const;
  void clearControls() { resetControls(); }

private:
  enum class Mode { Current, Range, All };
  struct Field {
    std::string text;
    bool pending = false;
  };
  struct AxisDraft {
    Mode mode = Mode::Current;
    int extent = 1, captured = 0;
    Field first, last, step;
  };

  static AxisRange range(const AxisDraft &axis);
  bool field(const char *label, const char *name, Field &value, float width,
             bool enabled);
  void changedTarget();
  void control(const std::string &name, bool enabled,
               std::optional<float> width = std::nullopt);
  void resetControls();

  bool captured_ = false, hasSelection_ = false, targetChanged_ = true;
  bool enterConsumed_ = false;
  bool previewExplicit_ = false;
  int samples_ = 1;
  // Canonical native range order is T,C,Z, even though the visible order is C,Z,T.
  std::array<AxisDraft, 3> axes_;
  RoiPolicy roi_ = RoiPolicy::ActiveSelection;
  PlanePosition capturedPosition_{}, canvasPosition_{};
  std::optional<PlanePosition> previewPosition_;
  Field previewOrdinal_;
  std::size_t previewCount_ = 0;
  bool previewRequested_ = false, stagedPreview_ = false;
  Json target_ = Json::object(), controls_ = Json::object();
  Json controlRects_ = Json::object(), controlStates_ = Json::object();
};

} // namespace imagejxx::desktop
