#pragma once

#include "desktop.hpp"
#include "operation_target_panel.hpp"
#include <imagejxx/gaussian_command.hpp>
#include <cstdint>
#include <functional>
#include <imgui.h>
#include <optional>
#include <string>

namespace imagejxx::desktop {

// Session state and actual widgets shared by the native and Python launchers.
// Callbacks own jobs and transient pixels; this panel only resolves metadata.
class GaussianPanel {
public:
  using Preview =
      std::function<void(const ResolvedGaussianBlurCommand &, std::uint64_t)>;
  using CancelPreview = std::function<void()>;
  using Apply = std::function<void(const ResolvedGaussianBlurCommand &)>;
  using ShowPreviewPlane = std::function<void(PlanePosition)>;
  using PreviewVisible = std::function<bool(PlanePosition)>;
  struct Callbacks {
    // Copy the resolved command when retaining it for asynchronous work.
    // A newer generation supersedes the prior preview from this panel.
    Preview preview;
    CancelPreview cancel_preview;
    Apply apply;
    ShowPreviewPlane show_preview_plane;
    PreviewVisible preview_visible;
  };

  // Cancel an existing visible dialog before replacing its captured source.
  void show(const DocumentSnapshot &source);
  void draw(const std::optional<DocumentSnapshot> &current, bool apply_busy,
            const Callbacks &callbacks);
  void cancel(const CancelPreview &cancel_preview);
  void setPreviewError(std::string error);
  bool visible() const { return open_; }
  Json diagnostics() const;
  const GaussianBlurCommandOptions &remembered_options() const {
    return remembered_;
  }

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
  bool draftValid_ = false, scaledAvailable_ = false, scaled_ = false;
  bool preview_ = false, previewIssued_ = false, previewNeedsRelease_ = false;
  bool stackPrompt_ = false, applyBusy_ = false;
  bool scientificTargets_ = false;
  std::uint64_t generation_ = 0, sourceId_ = 0;
  GaussianBlurCommandOptions remembered_;
  std::optional<DocumentSnapshot> source_;
  std::optional<ResolvedGaussianBlurCommand> draft_, previewDraft_, accepted_;
  OperationTargetPanel targetPanel_;
  std::string sigmaText_, error_, previewError_, exitReason_;
  Json target_ = Json::object(), controls_ = Json::object();
  Json controlRects_ = Json::object(), controlStates_ = Json::object();
  Json windowDrag_ = nullptr;
  ImVec2 closePoint_{-1, -1};
};

} // namespace imagejxx::desktop
