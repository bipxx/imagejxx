#pragma once
#include "desktop.hpp"
#include "operation_target_panel.hpp"
#include <imagejxx/histogram_job.hpp>
#include <imagejxx/rgb_contrast_apply_job.hpp>
#include <functional>
#include <atomic>

namespace imagejxx::desktop {
// The panel retains immutable captures and detached histogram results only.
// The host owns atomic publication and the native Auto/Apply commands.
class ContrastPanel {
public:
  using Publish=std::function<void(const DocumentSnapshot &,std::array<double,2>)>;
  using Apply=std::function<void(const DocumentSnapshot &,std::array<double,2>,const TargetRequest &)>;
  void configureTarget(const TargetRequest &);
  using RebindApply=std::function<void(const DocumentSnapshot &,const MetadataObject &,const TargetRequest &)>;
  void showRebind(const DocumentSnapshot &,const MetadataObject &,const ContrastApplyCommandLimits &);
  bool rebindVisible()const{return bool(rebind_);}
  void cancelRebind(){rebind_.reset();}
  void tickRebind(const std::optional<DocumentSnapshot> &);
  void configureRebindTarget(const TargetRequest &,bool busy);
  void applyRebind(bool busy,const RebindApply &);
  void drawRebind(const std::optional<DocumentSnapshot> &,bool busy,const RebindApply &);
  Json rebindDiagnostics()const;
  using Auto=std::function<void(const DocumentSnapshot &,bool,bool)>;
  ~ContrastPanel();
  void open();
  void close();
  bool visible()const{return open_;}
  std::array<double,2> currentRange()const{return range_;}
  bool inputActive()const;
  static bool handles(const std::string &);
  void request_cancel(JobScheduler &);
  bool wait_for(std::chrono::milliseconds)const;
  bool rgbSource()const;
  int rgbChannels()const{return rgbChannels_;}
  std::optional<DocumentSnapshot> rgbPreview(const DocumentSnapshot &)const;
  const ResolvedTarget *rgbPreviewTarget(const DocumentSnapshot &)const;
  std::uint64_t rgbPreviewGeneration()const{return rgbGeneration_;}
  ContrastApplyJob takeRgbPreview(const ResolvedRgbContrastApplyCommand &);
  void sourceClosed(std::uint64_t);
  void tick(const std::optional<DocumentSnapshot> &,bool,JobScheduler &,DatasetHistogramLimits,const ContrastApplyCommandLimits & = {});
  void draw(bool,const Publish &,const Auto &,const std::function<void(int)> &,int unsigned16_bits,const Apply & = {},bool applying=false,const std::function<void()> &cancel={});
  void action(const std::string &,const Json &,bool,const Publish &,const Auto &,int unsigned16_bits,const Apply & = {});
  Json diagnostics()const;
  static std::array<double,2> windowLevel(double window,double level);
  static std::array<double,2> brightness(std::array<double,2>,std::array<double,2>,double);
  static std::array<double,2> contrast(std::array<double,2>,std::array<double,2>,double);
private:
  struct RebindReview {
    DocumentSnapshot source;
    MetadataObject original;
    ContrastApplyOptions options;
    OperationTargetPanel target;
    int rgbChannels=0;
    bool valid=true,focus=true,focused=false;
    std::uint64_t cancelEpoch=0;
    std::string error;
    Json controls=Json::object();
  };
  std::optional<RebindReview> rebind_;
  std::atomic<std::uint64_t> rebindCancelEpoch_{0};
  OperationTargetPanel target_;
  std::optional<ResolvedRgbContrastApplyCommand> rgbCommand_;
  std::optional<ContrastApplyJob> rgbJob_;
  std::optional<DocumentSnapshot> rgbPreview_;
  std::optional<DocumentSnapshot> rgbSource_;
  ContrastApplyCommandLimits rgbLimits_;
  int rgbChannels_=7;
  bool rgbAttempted_=false;
  std::uint64_t rgbGeneration_=0,rgbSubmitted_=0,rgbDiscarded_=0;
  std::atomic<std::uint64_t> rgbCancelId_{0};
  std::uint64_t rgbCancelEpoch_=0;
  void invalidateRgb(bool retry=true);
  void tickRgb(bool,JobScheduler &,const ContrastApplyCommandLimits &);
  Json rgbDiagnostics()const;
  bool open_=false,focus_=false,focused_=false,histogramAttempted_=false;
  bool allChannels_=false,useSelection_=true,resolved_=false;
  std::string key_,error_;
  Json rangeKey_=nullptr,controls_=Json::object();
  std::optional<DocumentSnapshot> source_;
  std::optional<HistogramJob> job_;
  std::shared_ptr<const TargetHistogramResult> histogram_;
  std::vector<float> heights_;
  std::array<double,2> range_{0,255},domain_{0,255};
  double minimum_=0,maximum_=255,window_=255,level_=127.5;
  std::uint64_t submitted_=0,published_=0,discarded_=0;
  std::atomic<std::uint64_t> cancelId_{0},cancelEpoch_{0};
  bool histogramSupported(const Dataset &)const;
  std::array<double,2> effectiveRange(const DocumentSnapshot &)const;
  std::array<double,2> resetRange(int)const;
  void draft(std::array<double,2>);
  void publish(std::array<double,2>,bool,const Publish &);
  void cancelHistogram();
  void refresh();
  void control(const char *,bool=true);
};
} // namespace imagejxx::desktop
