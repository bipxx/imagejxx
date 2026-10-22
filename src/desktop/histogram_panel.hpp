#pragma once
#include "desktop.hpp"
#include <imagejxx/target_histogram.hpp>
#include <imagejxx/histogram_command.hpp>
#include <imagejxx/settings.hpp>
#include "operation_target_panel.hpp"
#include <functional>
#include <optional>
#include <memory>

namespace imagejxx::desktop {
class HistogramPanel {
public:
  using Remember = std::function<void(const SettingsPatch &, const MetadataObject &)>;
  void show(const DocumentSnapshot &, DatasetHistogramLimits, const Settings &);
  void showRebind(const DocumentSnapshot &, const MetadataObject &,
                  DatasetHistogramLimits, const Settings &);
  void replay(const ResolvedHistogramCommand &, DatasetHistogramLimits,
              const Settings &, const Remember &);
  void configure(const Json &);
  void configureTarget(const TargetRequest &);
  void calculate(const std::optional<DocumentSnapshot> &, bool busy, const Remember & = {});
  void draw(const std::optional<DocumentSnapshot> &, bool busy,
            const std::function<void(std::string)> &export_csv, const Remember & = {});
  void sourceClosed(std::uint64_t);
  void cancel();
  bool visible() const { return open_; }
  std::string csv() const;
  Json diagnostics() const;
private:
  void calculateImpl(const std::optional<DocumentSnapshot> &,bool,const Remember &);
  std::uint64_t generation_=0;
  DatasetHistogramRequest request() const;
  TargetHistogramOptions options() const;
  ResolvedHistogramCommand command() const;
  void loadOptions(const ResolvedHistogramCommand &);
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
  Json resultCommand_=Json::object();
  std::shared_ptr<const MetadataObject> rebindRecord_;
  std::optional<TargetRequest> frozenTarget_;
  OperationTargetPanel targetPanel_;
  std::uint64_t sourceId_=0;
  std::shared_ptr<const DocumentSnapshot> source_;
  DatasetHistogramLimits limits_;
  std::array<std::string,3> position_;
  std::string bins_="256", lower_="0", upper_="1", error_, sourceError_, actionError_;
  std::shared_ptr<const DatasetHistogramResult> result_;
  std::vector<float> heights_;
  Json controls_=Json::object(), states_=Json::object();
};
}
