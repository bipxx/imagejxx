#include "movie_export_panel.hpp"
#include "rendering.hpp"
#include "../native/kernel_control.hpp"
#include <imagejxx/avi_movie.hpp>
#include <imagejxx/channel_display.hpp>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <mutex>
#include <climits>
#include <cmath>
#include <charconv>
#include <sstream>
#include <locale>
#include <iomanip>

namespace imagejxx::desktop {
namespace {
bool matches(const DocumentSnapshot &a,const std::optional<DocumentSnapshot> &b){
  return b && a.same_document(*b) && b->revisions().matches(a.revisions(),Change::Pixels|Change::Metadata|Change::Display);
}
int extent(DatasetShape s,const std::string &axis){return axis=="z"?s.z:axis=="t"?s.t:s.c;}
std::string trim(const std::string &text){const auto first=text.find_first_not_of(" \t\r\n");return first==std::string::npos?std::string():text.substr(first,text.find_last_not_of(" \t\r\n")-first+1);}
int integer_draft(const std::string &text,const char *name){const auto value=trim(text);int number=0;const auto parsed=std::from_chars(value.data(),value.data()+value.size(),number);if(value.empty()||parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size())throw std::invalid_argument(std::string("Movie ")+name+" must be a complete integer");return number;}
double fps_draft(const std::string &text){const auto value=trim(text);std::istringstream stream(value);stream.imbue(std::locale::classic());double fps=0;stream>>std::noskipws>>fps;if(!stream||!stream.eof()||!std::isfinite(fps))throw std::invalid_argument("Movie FPS must be a complete finite number");return fps;}
std::string fps_text(double fps){std::ostringstream stream;stream.imbue(std::locale::classic());stream<<std::fixed<<std::setprecision(6)<<fps;auto text=stream.str();while(!text.empty()&&text.back()=='0')text.pop_back();if(!text.empty()&&text.back()=='.')text.pop_back();return text;}
void reset_valid_draft(std::string &text,int value){try{(void)integer_draft(text,"range");text=std::to_string(value);}catch(const std::invalid_argument &){}}
struct Result{std::mutex mutex;std::unique_ptr<StagedAviMovie> staged;};
}
struct MovieExportPanel::State {
  std::shared_ptr<JobScheduler> scheduler;
  std::optional<DocumentSnapshot> source;
  Json metadata,controls=Json::object();
  bool open=false,focus=false,overwrite=false,frozenOverwrite=false,controlEnabled=true,gray=false;
  std::string path,axis="z",timing="fps",message,error;
  int first=1,last=1,step=1,mode=0;
  std::string firstText="1",lastText="1",stepText="1",fpsText="8";
  double fps=8;
  Job job;
  std::shared_ptr<Result> result;
  std::function<void()> workerGate;
  std::size_t reservation=0,fileBytes=0;
  bool active()const{if(!job.valid())return false;const auto state=job.info().state;return state==JobState::Queued||state==JobState::Running||state==JobState::Ready||state==JobState::Publishing;}
  void observe(const std::string &name){const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();controls[name]={{"id",std::uint64_t(ImGui::GetItemID())},{"point",{(a.x+b.x)/2,(a.y+b.y)/2}},{"enabled",controlEnabled}};}
};
MovieExportPanel::MovieExportPanel(std::shared_ptr<JobScheduler> jobs):state_(std::make_unique<State>()){state_->scheduler=std::move(jobs);}
MovieExportPanel::~MovieExportPanel(){cancel();}
bool MovieExportPanel::visible()const{return state_->open;}
bool MovieExportPanel::working()const{return state_->active();}
void MovieExportPanel::cancel(){auto &s=*state_;if(s.job.valid())s.job.cancel();s.result.reset();s.open=false;s.source.reset();}
void MovieExportPanel::wait()const{if(state_->job.valid())state_->job.wait();}
void MovieExportPanel::worker_gate(std::function<void()> gate){state_->workerGate=std::move(gate);}
void MovieExportPanel::show(const DocumentSnapshot &source,Json metadata,std::string axis,int first,std::optional<int> last,int step,PlaybackMode mode,std::string timing,double fps){
  cancel();auto &s=*state_;s.job=Job{};s.source=source;s.metadata=std::move(metadata);s.axis=std::move(axis);
  s.first=first+1;s.last=last.value_or(extent(source.data().dataset.shape(),s.axis)-1)+1;s.step=step;
  s.mode=mode==PlaybackMode::Forward?0:mode==PlaybackMode::Reverse?1:2;s.timing=std::move(timing);s.fps=fps;
  s.firstText=std::to_string(s.first);s.lastText=std::to_string(s.last);s.stepText=std::to_string(s.step);s.fpsText=fps_text(s.fps);
  s.path.clear();s.overwrite=false;s.gray=false;s.open=s.focus=true;s.message.clear();s.error.clear();s.reservation=s.fileBytes=0;
  s.workerGate={};
}
void MovieExportPanel::poll(const std::optional<DocumentSnapshot> &current,std::size_t cap){
  auto &s=*state_;if(!s.source)return;
  if(!matches(*s.source,current)||s.reservation>cap){
    if(s.job.valid())s.job.cancel();s.result.reset();s.message.clear();s.error="Source, display or memory limit changed; reopen movie export";return;
  }
  if(!s.job.valid()||!s.job.ready())return;
  try{
    s.job.rethrow();
    if(s.job.info().state==JobState::Ready){
      s.job.complete([&]{if(!matches(*s.source,current))throw RevisionConflict();std::lock_guard<std::mutex> lock(s.result->mutex);s.result->staged->publish(s.frozenOverwrite);});
      s.message="Movie exported";s.error.clear();
    }
  }catch(const OperationCancelled &){s.message="Export canceled";}
   catch(const std::exception &e){s.message.clear();s.error=e.what();}
  s.result.reset();s.job=Job{};
}
void MovieExportPanel::draw(const std::optional<DocumentSnapshot> &current,std::size_t cap){
  auto &s=*state_;if(!s.open)return;
  if(s.focus){ImGui::SetNextWindowFocus();s.focus=false;}
  ImGui::SetNextWindowSize(ImVec2(520,490),ImGuiCond_FirstUseEver);
  bool open=true;const bool drawn=ImGui::Begin("Export movie (AVI)",&open);s.controls=Json::object();
  if(drawn && s.source){
    ImGui::TextUnformatted("Uncompressed AVI; display colors, no overlays or audio");
    ImGui::TextUnformatted("One cycle; ping-pong does not repeat endpoints.");
    const bool enabled=matches(*s.source,current)&&!s.active();
    s.controlEnabled=enabled;
    ImGui::BeginDisabled(!enabled);
    if(ImGui::InputText("Output .avi",&s.path))s.overwrite=false;s.observe("path");
    ImGui::Checkbox("Replace an existing file",&s.overwrite);s.observe("overwrite");
    ImGui::Checkbox("Gray8 (requires grayscale display)",&s.gray);s.observe("gray8");
    if(ImGui::BeginCombo("Axis",s.axis.c_str())){
      for(const char *axis:{"z","c","t"}){if(ImGui::Selectable(axis,s.axis==axis)){s.axis=axis;s.first=1;s.last=extent(s.source->data().dataset.shape(),s.axis);s.step=1;reset_valid_draft(s.firstText,s.first);reset_valid_draft(s.lastText,s.last);reset_valid_draft(s.stepText,s.step);s.timing="fps";}s.observe(std::string("axis.choice.")+axis);}
      ImGui::EndCombo();
    }s.observe("axis");
    ImGui::InputText("First (1-based)",&s.firstText);s.observe("first");ImGui::InputText("Last (inclusive)",&s.lastText);s.observe("last");ImGui::InputText("Step",&s.stepText);s.observe("step");
    const char *modes[]{"Forward","Reverse","Ping-pong"};
    if(ImGui::BeginCombo("Direction",modes[s.mode])){for(int i=0;i<3;++i){if(ImGui::Selectable(modes[i],i==s.mode))s.mode=i;s.observe(std::string("mode.choice.")+modes[i]);}ImGui::EndCombo();}s.observe("mode");
    bool calibrated=s.timing=="calibrated_time";
    if(ImGui::Checkbox("Use calibrated time",&calibrated))s.timing=calibrated?"calibrated_time":"fps";s.observe("calibrated");
    ImGui::BeginDisabled(calibrated);ImGui::InputText("Frames per second",&s.fpsText);s.observe("fps");s.controls["fps"]["enabled"]=enabled&&!calibrated;ImGui::EndDisabled();
    if(ImGui::Button("Export movie"))try{
      const auto shape=s.source->data().dataset.shape();
      const int first=integer_draft(s.firstText,"First"),last=integer_draft(s.lastText,"Last"),step=integer_draft(s.stepText,"Step");
      const double fps=calibrated?s.fps:fps_draft(s.fpsText);
      if(first<1||last<1)throw std::invalid_argument("Movie First and Last must be positive");
      const PlaybackSequence sequence(extent(shape,s.axis),first-1,last-1,step,s.mode==0?PlaybackMode::Forward:s.mode==1?PlaybackMode::Reverse:PlaybackMode::PingPong);
      const std::int64_t count=sequence.count(),frames=s.mode==2&&count>1?2*count-2:count;
      if(frames>INT_MAX)throw std::invalid_argument("Movie frame count exceeds the AVI limit");
      if(calibrated && s.axis!="t" && !(s.axis=="z"&&shape.t==1&&shape.c==1))throw std::invalid_argument("Calibrated movie timing requires T or an ordinary Z time-series stack");
      const auto &cal=s.source->data().dataset.calibration();
      const double seconds=playback_interval_seconds(fps,s.timing,cal.frame_interval,cal.time_unit,step);
      if(s.path.empty()||extension(s.path)!=".avi")throw std::invalid_argument("Choose an output path ending in .avi");
      const auto target=std::filesystem::absolute(std::filesystem::u8path(s.path)).lexically_normal().u8string();
      if(!s.overwrite && std::filesystem::exists(std::filesystem::u8path(target)))throw std::invalid_argument("File exists; choose another name or confirm replacement");
      AviMovieSpec spec{shape.x,shape.y,static_cast<int>(frames),s.gray?1:3,1/seconds,1024ULL*1024*1024,cap};
      const auto estimate=estimate_avi_movie(spec);const auto source=*s.source;const auto metadata=s.metadata;const auto axis=s.axis;
      const auto channel=recognized_channel_display(source.display(),source.data().dataset);
      if(channel)(void)estimate_channel_display(source.data().dataset,*channel,Plane::XY,source.display().crosshair[2],source.display().t,source.display().c,ChannelDisplayLimits{std::size_t(shape.x)*shape.y,cap});
      const std::uint64_t extra=std::uint64_t(shape.x)*shape.y*(5*shape.s+4)*sizeof(double)+std::uint64_t(shape.x)*sizeof(double)+65536;
      if(extra>cap || estimate.working_bytes>cap-extra)throw std::invalid_argument("Movie display rendering exceeds the working-byte limit");
      s.first=first;s.last=last;s.step=step;s.fps=fps;
      s.reservation=estimate.working_bytes+static_cast<std::size_t>(extra);s.fileBytes=estimate.file_bytes;s.frozenOverwrite=s.overwrite;
      auto result=std::make_shared<Result>();
      const auto gate=s.workerGate;
      s.job=s.scheduler->submit([result,source,metadata,axis,sequence,spec,channel,target,gate](const JobContext &context){
        ExecutionScope scope(context.execution_control());
        auto staged=stage_avi_movie(target,spec,[&](std::size_t frame){
          if(frame==0 && gate)gate();
          const auto count=sequence.count();
          const std::int64_t index=sequence.mode()==PlaybackMode::Reverse?count-1-std::int64_t(frame):
            sequence.mode()==PlaybackMode::PingPong && frame>=static_cast<std::size_t>(count)?2*std::int64_t(count)-2-frame:frame;
          const int selected=static_cast<int>(sequence.first()+index*sequence.step());
          PlanePosition p{source.display().t,source.display().c,source.display().crosshair[2]};
          if(axis=="z")p.z=selected;else if(axis=="t")p.t=selected;else p.c=selected;
          const auto &data=source.data().dataset;
          Image rendered=channel?compose_channel_display(data,*channel,Plane::XY,p.z,p.t,p.c,{},ChannelDisplayLimits{std::size_t(spec.width)*spec.height,spec.max_working_bytes},context.execution_control()):
                         displayImage(*data.plane(p),metadata,data.sample_policy());
          for(double &value:rendered.pixels){kernel_checkpoint_periodic();if(!std::isfinite(value)||value<0||value>255)throw std::invalid_argument("Movie display color is outside the byte range");value=std::floor(value);}
          if(spec.channels==3)return rendered;
          Image gray(spec.width,spec.height,1,PixelType::UInt8);
          for(std::size_t i=0;i<gray.pixels.size();++i){kernel_checkpoint_periodic();if(rendered.pixels[i*3]!=rendered.pixels[i*3+1]||rendered.pixels[i*3]!=rendered.pixels[i*3+2])throw std::invalid_argument("Gray8 requires a grayscale display; choose RGB");gray.pixels[i]=rendered.pixels[i*3];}
          return gray;
        },context.execution_control());
        std::lock_guard<std::mutex> lock(result->mutex);result->staged=std::move(staged);
      },JobOptions{"AVI movie export","",source.id(),s.reservation});
      s.result=std::move(result);s.error.clear();s.message="Exporting";
    }catch(const std::exception &e){s.message.clear();s.error=e.what();}s.observe("export");
    ImGui::EndDisabled();
    s.controlEnabled=true;
    if(s.active()){const auto info=s.job.info();ImGui::Text("%s: %zu / %zu",info.stage.c_str(),info.stage_completed,info.stage_total);if(ImGui::Button("Cancel export")){s.job.cancel();s.message="Cancel requested";}s.observe("cancel");}
    if(!s.error.empty())ImGui::TextWrapped("%s",s.error.c_str());if(!s.message.empty())ImGui::TextWrapped("%s",s.message.c_str());
    if(ImGui::Button("Close"))open=false;s.observe("close");
  }
  ImGui::End();if(!open)cancel();
}
Json MovieExportPanel::diagnostics()const{const auto &s=*state_;return {{"open",s.open},{"working",s.active()},{"controls",s.controls},{"path",s.path},{"axis",s.axis},{"first",s.first},{"last",s.last},{"step",s.step},{"mode",s.mode},{"timing",s.timing},{"fps",s.fps},{"text",{{"first",s.firstText},{"last",s.lastText},{"step",s.stepText},{"fps",s.fpsText}}},{"channels",s.gray?1:3},{"overwrite",s.overwrite},{"error",s.error},{"message",s.message},{"working_bytes",s.reservation},{"file_bytes",s.fileBytes}};}
} // namespace imagejxx::desktop
