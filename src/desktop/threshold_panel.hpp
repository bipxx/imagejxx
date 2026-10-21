#pragma once

#include "desktop.hpp"
#include "operation_target_panel.hpp"
#include <imagejxx/manual_threshold.hpp>
#include <imagejxx/settings.hpp>
#include <imagejxx/automatic_threshold_display.hpp>
#include <functional>
#include <imgui.h>
#include <optional>
#include <string>
#include <vector>

namespace imagejxx::desktop {

// The shared widgets retain an immutable source and raw-intensity summary.
// The host owns display state, image processing, publication and cancellation.
class ThresholdPanel {
public:
  using RangeAction = std::function<void(ManualThresholdRange)>;
  using Action = std::function<void()>;
  struct Callbacks {
    RangeAction set;
    Action reset;
    RangeAction to_selection;
    RangeAction apply;
    Action cancel;
    std::function<AutomaticThresholdDisplayResult(const std::string &,bool,bool,bool,bool)> automatic;
    Action scope_changed;
    std::function<void(PlanePosition)> show_target;
  };

  void show(const DocumentSnapshot &source, ManualThresholdSummary summary);
  void draw(const std::optional<DocumentSnapshot> &current, bool busy,
            const Callbacks &callbacks);
  void cancel(const Action &callback = {});
  void setAutomaticOptions(const Settings &);
  void setAutomaticResult(const AutomaticThresholdDisplayResult &);
  // Keep the modeless controls visibly disabled after close without retaining
  // the source's pixel storage through a snapshot.
  void source_closed() { source_.reset(); }
  // Rebase selection-only publication while keeping the captured plane and
  // original histogram. Pixel/metadata changes must open a new panel instead.
  void rebase(const DocumentSnapshot &source);
  // The executor supplies its actual effective result type and target extent.
  void setApplyPreview(PixelType result_type, std::string extent,bool conditional=false);
  TargetRequest targetRequest() const;
  ManualThresholdState displayState(ManualThresholdRange) const;
  bool scientificTargets() const { return scientificTargets_; }
  bool histogramUsesRoi() const { return histogramUsesRoi_; }
  void configureTarget(const TargetRequest &,bool histogram_roi);
  void setHistogramUsesRoi(bool value) { histogramUsesRoi_=value;clearAutomatic(); }
  bool stackHistogram() const { return stackHistogram_; }
  bool visible() const { return open_; }
  Json diagnostics() const;

private:
  enum class Pending { None, Set, Reset, ToSelection, Apply, Cancel, Automatic, Scope };
  void rebuildDraft();
  void clearAutomatic();
  void drawTargets(bool,const Callbacks &);
  void resetControls();
  void control(const char *name, bool enabled = true,
               std::optional<float> width = std::nullopt);
  void drawHistogram();
  void close(const char *reason);

  bool open_ = false, focus_ = false, busy_ = false, sourceValid_ = false;
  std::uint64_t sourceId_ = 0;
  PlanePosition position_,canvasPosition_;
  bool conditionalType_=false;
  std::optional<DocumentSnapshot> source_;
  ManualThresholdSummary summary_;
  std::vector<float> plotCounts_;
  std::optional<ManualThresholdRange> draft_;
  std::string lowerText_, upperText_, error_, actionError_, sourceError_;
  std::string exitReason_, applyExtent_;
  std::optional<PixelType> applyType_;
  std::string automaticMethod_="default";
  bool stackHistogram_=false;
  bool scientificTargets_=false,targetsOpen_=false,histogramUsesRoi_=false,targetValid_=true;
  std::string targetError_;
  OperationTargetPanel targetPanel_;
  bool automaticBright_=true, ignoreBlack_=false, ignoreWhite_=false;
  std::optional<AutomaticThresholdDisplayResult> automaticResult_;
  Json controls_ = Json::object(), controlRects_ = Json::object();
  Json controlStates_ = Json::object();
  ImVec2 closePoint_{-1, -1};
};

} // namespace imagejxx::desktop
