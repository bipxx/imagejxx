#pragma once
#include "desktop.hpp"
#include <imagejxx/target_histogram.hpp>
#include "operation_target_panel.hpp"
#include <functional>
#include <optional>

namespace imagejxx::desktop {
class HistogramPanel {
public:
  void show(const DocumentSnapshot &, DatasetHistogramLimits);
  void configure(const Json &);
  void configureTarget(const TargetRequest &);
  void calculate(const std::optional<DocumentSnapshot> &, bool busy);
  void draw(const std::optional<DocumentSnapshot> &, bool busy,
            const std::function<void(std::string)> &export_csv);
  void sourceClosed(std::uint64_t);
  void cancel();
  bool visible() const { return open_; }
  std::string csv() const;
  Json diagnostics() const;
private:
  DatasetHistogramRequest request() const;
  TargetHistogramOptions options() const;
  void validateDraft() const;
  void drawTargets(bool);
  void validateSource(const std::optional<DocumentSnapshot> &, bool) const;
  void control(const char *, bool);
  bool open_=false, focus_=false, custom_=false, roi_=true, log_=false, draftChanged_=true;
  bool scientific_=false, targetsOpen_=false, targetValid_=true;
  bool resultScientific_=false, resultUsesRoi_=false;
  std::size_t resultPlaneCount_=0;
  std::string targetError_, resultRoiPolicy_;
  Json resultTarget_=Json::object(), resultEstimate_=Json::object();
  OperationTargetPanel targetPanel_;
  std::uint64_t sourceId_=0;
  std::optional<DocumentSnapshot> source_;
  DatasetHistogramLimits limits_;
  std::array<std::string,3> position_;
  std::string bins_="256", lower_="0", upper_="1", error_, sourceError_, actionError_;
  std::optional<DatasetHistogramResult> result_;
  std::vector<float> heights_;
  Json controls_=Json::object(), states_=Json::object();
};
}
