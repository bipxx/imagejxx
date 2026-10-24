#include "path_authoring_controls.hpp"
#include "geometry_number.hpp"
#include "path_visual.hpp"
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <atomic>
#include <cmath>

namespace imagejxx::desktop {
bool PathAuthoringCanvas::operator==(const PathAuthoringCanvas &v) const {
  return id==v.id && plane==v.plane && coordinate==v.coordinate && width==v.width &&
      height==v.height && origin==v.origin && scale==v.scale && low==v.low && high==v.high;
}
namespace {
std::uint64_t nextResizeLifecycle() {
  // Distinct independently prepared controllers must not share a local counter.
  static std::atomic<std::uint64_t> next{1};
  return next.fetch_add(1,std::memory_order_relaxed);
}
std::size_t commandArity(int op) { return op==4?0:op==3?6:op==2?4:2; }
bool drawable(const Selection &s, bool last_contour = false) {
  bool found=false;
  const auto &commands=s.path_commands();
  for(std::size_t at=0;at<commands.size();) {
    const int op=int(commands[at++]);
    if(op==0 && last_contour)found=false;
    if(op>=1 && op<=3)found=true;
    at+=commandArity(op);
  }
  return found;
}
}
void PathAuthoringControls::observe(const char *name,bool enabled) {
  const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
  const auto clip=ImGui::GetCurrentWindowRead()->ClipRect;
  controls_[name]={{"id",ImGui::GetItemID()},{"enabled",enabled},
      {"point",{(a.x+b.x)/2,(a.y+b.y)/2}},{"rect",{a.x,a.y,b.x,b.y}},
      {"visible",ImGui::IsItemVisible()},{"hovered",ImGui::IsItemHovered()},{"active",ImGui::IsItemActive()},
      {"clip_rect",{clip.Min.x,clip.Min.y,clip.Max.x,clip.Max.y}}};
}
void PathAuthoringControls::cancel(const char *reason) {
  if(active() || press_pending_)last_cancel_=reason;
  source_.reset();canvas_.reset();selection_.reset();pending_.clear();
  press_pending_=new_contour_=after_close_=finish_requested_=false;error_.clear();++generation_;
  visual_.reset();visual_selection_.reset();visual_canvas_.reset();
}
void PathAuthoringControls::sync(const DocumentSnapshot &s,bool enabled,std::size_t cap) {
  if(cap)limits_.max_working_bytes=cap;
  if(!source_)return;
  if(!enabled || !source_->same_document(s) ||
      !source_->revisions().matches(s.revisions(),Change::Data) ||
      source_->display().t!=s.display().t || source_->display().c!=s.display().c ||
      source_->display().crosshair!=s.display().crosshair)
    cancel(enabled?"source_changed":"busy");
}
bool PathAuthoringControls::ready() const {
  return selection_ && pending_.empty() && !press_pending_ && error_.empty() && drawable(*selection_);
}
void PathAuthoringControls::append(const std::vector<double> &command) {
  auto result=[&] {
    if(!selection_)return ImageJCompositePath(command);
    const ImageJCompositePath source(selection_->path_commands());
    return edit_composite_path(source,PathSegmentEdit::InsertBefore,source.segment_count(),command,limits_);
  }();
  auto value=Selection::from_path(result);value.width=style_.width;value.attributes=style_.attributes;
  selection_=std::move(value);error_.clear();
}
void PathAuthoringControls::accept(std::array<double,2> point) {
  (void)ImageJCompositePath({0,point[0],point[1]});
  if(!selection_ || new_contour_ || after_close_) {
    append({0,point[0],point[1]});new_contour_=after_close_=false;return;
  }
  auto points=pending_;points.push_back(point);
  const auto needed=std::size_t(segment_+1);
  if(points.size()<needed){pending_=std::move(points);return;}
  std::vector<double> command{double(segment_+1)};
  for(const auto &p:points){command.push_back(p[0]);command.push_back(p[1]);}
  append(command);pending_.clear();
}
void PathAuthoringControls::remove() {
  error_.clear();
  if(new_contour_){new_contour_=false;return;}
  if(!pending_.empty()){pending_.clear();return;}
  if(!selection_)return;
  const ImageJCompositePath path(selection_->path_commands());
  if(path.segment_count()==1){selection_.reset();after_close_=false;return;}
  auto result=edit_composite_path(path,PathSegmentEdit::Remove,path.segment_count()-1,{},limits_);
  auto value=Selection::from_path(result);value.width=style_.width;value.attributes=style_.attributes;
  selection_=std::move(value);int last=0;
  const auto &commands=selection_->path_commands();
  for(std::size_t at=0;at<commands.size();){last=int(commands[at++]);at+=commandArity(last);}
  after_close_=last==4;
}
void PathAuthoringControls::finish(const Publish &publish) {
  if(!ready())throw std::invalid_argument("Finish a complete drawable path first");
  const auto source=*source_;const auto value=*selection_;const auto canvas=*canvas_;
  const auto generation=generation_;publish(source,value,canvas);
  if(generation_==generation)cancel("finished");
}
void PathAuthoringControls::controls(bool enabled,const Publish &publish) {
  controls_=Json::object();ImGui::PushID("path_authoring");
  const float right=ImGui::GetCursorScreenPos().x+ImGui::GetContentRegionAvail().x;
  auto beside=[&](const char *label,bool radio=false) {
    const auto &style=ImGui::GetStyle();
    const float width=ImGui::CalcTextSize(label).x+(radio?ImGui::GetFrameHeight()+style.ItemInnerSpacing.x:2*style.FramePadding.x);
    if(ImGui::GetItemRectMax().x+style.ItemSpacing.x+width<=right)ImGui::SameLine();
  };
  ImGui::TextUnformatted("Path: Replace only. Finish keeps open contours open.");
  auto action=[&](auto fn){const auto generation=generation_;try{fn();if(generation_==generation)error_.clear();}
    catch(const std::exception &e){if(generation_==generation)error_=e.what();}};
  if(finish_requested_){finish_requested_=false;if(enabled && ready())action([&]{finish(publish);});}
  ImGui::BeginDisabled(!enabled || !pending_.empty() || press_pending_);
  for(int i=0;i<3;++i){const char *names[]{"Line","Quadratic","Cubic"};if(i)beside(names[i],true);
    if(ImGui::RadioButton(names[i],segment_==i))segment_=i;observe(names[i],enabled && pending_.empty() && !press_pending_);}
  ImGui::EndDisabled();
  const bool complete=enabled && selection_ && pending_.empty() && !press_pending_;
  ImGui::BeginDisabled(!complete);
  if(ImGui::Button("New contour"))new_contour_=true;observe("New contour",complete);
  ImGui::EndDisabled();beside("Close contour");
  const bool close=complete && !new_contour_ && !after_close_ && drawable(*selection_,true);
  ImGui::BeginDisabled(!close);
  if(ImGui::Button("Close contour"))action([&]{append({4});after_close_=true;});observe("Close contour",close);
  ImGui::EndDisabled();
  ImGui::BeginDisabled(!enabled || !active());
  if(ImGui::Button("Remove last command"))action([&]{remove();});observe("Remove last command",enabled && active());
  ImGui::EndDisabled();beside("Finish path");
  const bool can_finish=enabled && ready();ImGui::BeginDisabled(!can_finish);
  if(ImGui::Button("Finish path"))action([&]{finish(publish);});observe("Finish path",can_finish);
  ImGui::EndDisabled();beside("Cancel path");
  ImGui::BeginDisabled(!active());if(ImGui::Button("Cancel path"))cancel("cancel_button");observe("Cancel path",active());ImGui::EndDisabled();
  // Keep status height fixed so collecting a group cannot move the canvas.
  ImGui::Text("Pending points: %zu%s",pending_.size(),new_contour_||after_close_?"; next point starts a contour":"");
  ImGui::TextUnformatted(error_.empty()?" ":error_.c_str());
  const auto *inputContext=ImGui::GetCurrentContext();
  bool ownsKeyboard=inputContext->NavId==0;
  for(const auto &item:controls_.items())ownsKeyboard|=item.value().at("id").get<ImGuiID>()==inputContext->NavId;
  if(active() && enabled && ownsKeyboard && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
      !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel)) {
    if(ImGui::IsKeyPressed(ImGuiKey_Escape)){keyboard_frame_=ImGui::GetFrameCount();cancel("escape");}
    else if(ImGui::IsKeyPressed(ImGuiKey_Backspace)){keyboard_frame_=ImGui::GetFrameCount();action([&]{remove();});}
    else if(ImGui::IsKeyPressed(ImGuiKey_Enter) && ready()){keyboard_frame_=ImGui::GetFrameCount();action([&]{finish(publish);});}
  }
  ImGui::PopID();
}
bool PathAuthoringControls::pointer(const DocumentSnapshot &source,const PathAuthoringCanvas &canvas,
    bool enabled,bool hovered,const Selection &style,std::size_t cap,bool input_admitted) {
  sync(source,enabled,cap);auto &io=ImGui::GetIO();
  if(active() && canvas_->id!=canvas.id)return false;
  if(active() && !(*canvas_==canvas)){cancel("canvas_changed");return true;}
  const auto *context=ImGui::GetCurrentContext();
  const bool ownsPointer=hovered && context->HoveredWindow==ImGui::GetCurrentWindowRead();
  if(active() && (io.AppFocusLost || (ownsPointer && (io.MouseWheel!=0 || ImGui::IsMouseDown(ImGuiMouseButton_Middle))))) {
    cancel("navigation_or_focus");return true;
  }
  const bool inside=io.MousePos.x>=canvas.low[0] && io.MousePos.y>=canvas.low[1] &&
      io.MousePos.x<canvas.high[0] && io.MousePos.y<canvas.high[1];
  const bool allowed=enabled && input_admitted && !io.WantTextInput && !io.AppFocusLost &&
      !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel) &&
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
      (context->ActiveId==0 || context->ActiveId==canvas.id);
  if(active() && allowed && (hovered || context->NavId==canvas.id) && !press_pending_ && keyboard_frame_!=ImGui::GetFrameCount()) {
    if(ImGui::IsKeyPressed(ImGuiKey_Escape)){cancel("escape");return true;}
    if(ImGui::IsKeyPressed(ImGuiKey_Backspace)){try{remove();}catch(const std::exception &e){error_=e.what();}return true;}
    if(ImGui::IsKeyPressed(ImGuiKey_Enter) && ready()){finish_requested_=true;return true;}
  }
  if(press_pending_) {
    if(!allowed){cancel("capture_lost");return true;}
    if(ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
      press_pending_=false;
      if(!inside || !ownsPointer){cancel("outside_release");return true;}
      try {accept({(double(io.MousePos.x)-canvas.origin[0])/canvas.scale[0],(double(io.MousePos.y)-canvas.origin[1])/canvas.scale[1]});}
      catch(const std::exception &e){error_=e.what();}
    } else if(!ImGui::IsMouseDown(ImGuiMouseButton_Left))cancel("missing_release");
    return true;
  }
  if(allowed && ownsPointer && inside && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    if(!source_){source_=source;canvas_=canvas;style_=style;limits_.max_working_bytes=cap;last_cancel_.clear();++generation_;}
    limits_.max_working_bytes=cap;press_pending_=true;return true;
  }
  return active() || hovered;
}
void PathAuthoringControls::draw(const PathAuthoringCanvas &canvas,ImDrawList *draw,std::size_t cap) {
  if(!active() || canvas_->id!=canvas.id)return;
  if(selection_) {
    if(!visual_ || !visual_selection_ || *visual_selection_!=*selection_ || !visual_canvas_ || !(*visual_canvas_==canvas) || cap!=visual_cap_) {
      visual_=pathVisual(*selection_,canvas.width,canvas.height,canvas.scale,cap);
      visual_selection_=selection_;visual_canvas_=canvas;visual_cap_=cap;++visual_builds_;
    }
    drawRoiVisual(*visual_,draw,canvas.origin,canvas.scale);
  }
  for(const auto &p:pending_)draw->AddCircleFilled({canvas.origin[0]+float(p[0])*canvas.scale[0],
      canvas.origin[1]+float(p[1])*canvas.scale[1]},4,IM_COL32(255,255,255,255));
}
Json PathAuthoringControls::diagnostics() const {
  Json source;
  if(source_){const auto r=source_->revisions();source={{"id",source_->id()},
      {"revisions",{{"pixels",r.pixels},{"metadata",r.metadata},{"selection",r.selection},{"display",r.display}}},
      {"t",source_->display().t},{"c",source_->display().c},{"crosshair",source_->display().crosshair}};}
  return {{"active",active()},{"ready",ready()},{"press_pending",press_pending_},{"source",source},
      {"generation",generation_},{"finish_requested",finish_requested_},{"next_segment",segment_+1},{"new_contour",new_contour_},{"after_close",after_close_},
      {"pending",pending_},{"selection",selection_?planarSelectionJson(*selection_):Json()},
      {"controls",controls_},{"error",error_},{"last_cancel",last_cancel_},{"visual_builds",visual_builds_},
      {"canvas",canvas_?Json{{"id",canvas_->id},{"plane",canvas_->plane},{"coordinate",canvas_->coordinate},
        {"origin",canvas_->origin},{"scale",canvas_->scale},{"low",canvas_->low},{"high",canvas_->high}}:Json()}};
}
void PathResizeControls::reset(){lifecycle_=nextResizeLifecycle();base_.reset();coordinates_={};handle_=4;aspect_=center_=staged_=unstaged_=false;error_.clear();}
void PathResizeControls::seed(const Selection &s,int w,int h,const SelectionEditLimits &limits) {
  base_=s;const auto handles=SelectionEditor(s,w,h,limits).handles(SelectionHandleMode::Bounds,limits);
  auto it=std::find_if(handles.begin(),handles.end(),[&](const auto &v){return v.index==std::size_t(handle_);});
  if(it==handles.end() && !handles.empty()){it=handles.begin();handle_=int(it->index);}
  if(it!=handles.end())for(int axis=0;axis<2;++axis)coordinates_[axis]=geometry_number_text(it->position[axis]);
}
void PathResizeControls::draw(const Selection &selection,int width,int height,int plane,
    const SelectionEditLimits &limits,bool enabled,const std::function<void(const Selection &)> &stage,const Observe &observe) {
  auto drawLifecycle=lifecycle_;
  try {
  if(!base_ || (!active() && *base_!=selection)) {
    lifecycle_=nextResizeLifecycle();drawLifecycle=lifecycle_;
    seed(selection,width,height,limits);
  }
  const auto handles=SelectionEditor(*base_,width,height,limits).handles(SelectionHandleMode::Bounds,limits);
  static const char *names[]{"NW","N","NE","E","SE","S","SW","W"};
  ImGui::TextUnformatted("Path transform (Shift: aspect; Ctrl: center)");
  const bool combo=ImGui::BeginCombo("Resize handle",names[handle_]);observe("Resize handle",enabled && !handles.empty(),0);
  if(combo) {
    for(int i=0;i<8;++i){const bool available=std::any_of(handles.begin(),handles.end(),[&](const auto &v){return v.index==std::size_t(i);});
      ImGui::BeginDisabled(!enabled || !available);
      if(ImGui::Selectable(names[i],handle_==i)){handle_=i;seed(*base_,width,height,limits);unstaged_=true;}
      observe(std::string("Resize handle.")+names[i],enabled && available,0);ImGui::EndDisabled();}
    ImGui::EndCombo();
  }
  const bool available=std::any_of(handles.begin(),handles.end(),[&](const auto &v){return v.index==std::size_t(handle_);});
  const char *labels[3][2]={{"Resize target X","Resize target Y"},{"Resize target X","Resize target Z"},{"Resize target Y","Resize target Z"}};
  ImGui::BeginDisabled(!enabled || !available);
  for(int axis=0;axis<2;++axis){const bool controlled=axis==0?(handle_!=1 && handle_!=5):(handle_!=3 && handle_!=7);
    ImGui::BeginDisabled(!controlled);
    if(ImGui::InputText(labels[plane][axis],&coordinates_[axis]))unstaged_=true;
    observe(labels[plane][axis],enabled && available && controlled,0);ImGui::EndDisabled();}
  if(ImGui::Checkbox("Preserve aspect",&aspect_))unstaged_=true;observe("Preserve aspect",enabled && available,0);
  if(ImGui::Checkbox("From center",&center_))unstaged_=true;observe("From center",enabled && available,0);
  if(ImGui::Button("Stage resize")) {
    const auto stageLifecycle=lifecycle_;
    try {const std::array<double,2> point{parse_geometry_number(coordinates_[0]),parse_geometry_number(coordinates_[1])};
      auto value=SelectionEditor(*base_,width,height,limits).resized(SelectionHandleMode::Bounds,std::size_t(handle_),point,aspect_,center_,limits);
      stage(value);
      if(lifecycle_==stageLifecycle){staged_=true;unstaged_=false;error_.clear();}
    }catch(const std::exception &e){if(lifecycle_==stageLifecycle){error_=e.what();unstaged_=true;}}
  }
  observe("Stage resize",enabled && available,0);ImGui::EndDisabled();
  if(!error_.empty())ImGui::TextWrapped("%s",error_.c_str());
  }catch(const std::exception &e){if(lifecycle_==drawLifecycle){error_=e.what();unstaged_=true;ImGui::TextWrapped("%s",error_.c_str());}}
}
Json PathResizeControls::diagnostics() const {
  return {{"handle",handle_},{"coordinates",coordinates_},{"aspect",aspect_},{"center",center_},{"lifecycle",lifecycle_},
      {"staged",staged_},{"unstaged",unstaged_},{"invalid",invalid()},{"error",error_}};
}
} // namespace imagejxx::desktop
