#pragma once

#include "desktop.hpp"
#include "operation_target_panel.hpp"
#include <imagejxx/rank_command.hpp>
#include <array>
#include <cstdint>
#include <functional>
#include <imgui.h>
#include <optional>
#include <string>

namespace imagejxx::desktop {

// Shared native/Python-launcher widgets and session drafts. The host owns the
// existing plane-operation lanes, transient pixels and atomic publication.
class RankPanel {
public:
  using Preview =
      std::function<void(const ResolvedRankFilterCommand &, std::uint64_t)>;
  using CancelPreview = std::function<void()>;
  using Apply = std::function<void(const ResolvedRankFilterCommand &)>;
  using ShowPreviewPlane = std::function<void(PlanePosition)>;
  using PreviewVisible = std::function<bool(PlanePosition)>;
  struct Callbacks {
    // Retain a copy of a command submitted for asynchronous work. A newer
    // generation supersedes the prior rank preview from this panel.
    Preview preview;
    CancelPreview cancel_preview;
    Apply apply;
    ShowPreviewPlane show_preview_plane;
    PreviewVisible preview_visible;
  };

  // Cancel a visible panel before replacing its source or selected method.
  void show(const DocumentSnapshot &source,
            RankFilterMethod method = RankFilterMethod::Median);
  // No parameter dialog for scalar images: direct Apply or Process Stack.
  // Scientific sources retain the shared target/preview chooser, fixed at 1.
  void show_remove_nans(const DocumentSnapshot &source);
  void show_top_hat(const DocumentSnapshot &source);
  void show_outliers(const DocumentSnapshot &source);
  void show_despeckle(const DocumentSnapshot &source);
  void draw(const std::optional<DocumentSnapshot> &current, bool apply_busy,
            const Callbacks &callbacks);
  void cancel(const CancelPreview &cancel_preview);
  void setPreviewError(std::string error);
  bool visible() const { return open_; }
  RankFilterMethod selected_method() const { return method_; }
  RankFilterCommandOptions remembered_options(RankFilterMethod method) const;
  RankFilterCommandOptions remembered_options() const {
    return remembered_options(method_);
  }
  Json diagnostics() const;

private:
  enum class Action { None, Cancel, Accept, StackYes, StackNo };
  void rebuildDraft();
  void requestPreview(const Callbacks &callbacks);
  void releasePreview(const CancelPreview &cancel_preview);
  void close(const CancelPreview &cancel_preview, const char *reason);
  void accept(const Callbacks &callbacks);
  void apply(bool all_z, const Callbacks &callbacks);
  void resetControls();
  void control(const char *name, bool enabled = true,
               std::optional<float> width = std::nullopt);

  bool open_ = false, focus_ = false, focusStack_ = false;
  bool draftValid_ = false;
  bool preview_ = false, previewIssued_ = false, previewNeedsRelease_ = false;
  bool stackPrompt_ = false, applyBusy_ = false, newDocument_ = false;
  bool scientificTargets_ = false;
  bool methodPopupOpen_ = false;
  bool fixedDespeckle_ = false, outliersMode_ = false;
  bool topHatMode_ = false, lightBackground_ = false, dontSubtract_ = false;
  bool removeNaNsMode_ = false;
  RemoveNaNsCommandOptions rememberedRemoveNaNs_;
  TopHatCommandOptions rememberedTopHat_;
  RemoveOutliersCommandOptions rememberedOutliers_;
  std::string thresholdText_ = "50", whichOutliers_ = "bright";
  std::uint64_t generation_ = 0, sourceId_ = 0;
  RankFilterMethod method_ = RankFilterMethod::Median;
  // Accepted first-dialog values only, scoped to this Window's panel instance.
  // Diagnostic/control clearing never resets these five session values.
  std::array<double, 5> rememberedRadii_{{2.0, 2.0, 2.0, 2.0, 2.0}};
  std::optional<DocumentSnapshot> source_;
  std::optional<ResolvedRankFilterCommand> draft_, previewDraft_, accepted_;
  OperationTargetPanel targetPanel_;
  std::string radiusText_, error_, previewError_, exitReason_;
  Json target_ = Json::object(), controls_ = Json::object();
  Json controlRects_ = Json::object(), controlStates_ = Json::object();
  Json windowDrag_ = nullptr;
  ImVec2 closePoint_{-1, -1};
};

} // namespace imagejxx::desktop
