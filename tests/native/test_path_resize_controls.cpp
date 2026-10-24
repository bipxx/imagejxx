// Shared resize form acceptance with real ImGui events. Expected coordinates
// come from literal anchors/scales, never from SelectionEditor or another UI.
// Framebuffer/font scaling below is component coverage, not OS-DPI evidence.
#include "path_authoring_controls.hpp"
#include <imgui_internal.h>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace imagejxx;
namespace ui=imagejxx::desktop;

namespace {
std::size_t checks=0, cases=0;
std::string current_case;
struct FixtureFailure final : std::runtime_error {
  using std::runtime_error::runtime_error;
};
void named(const std::string &name) { current_case=name; ++cases; }
void check(bool condition,const char *message) {
  ++checks;
  if(!condition)throw FixtureFailure(current_case+": "+message);
}

Selection fixture_path() {
  // Q has x maximum 12; C has y maximum 14. The control hull reaches (14,15).
  auto value=Selection::from_path({0,2,3,1,10,3,2,14,7,10,11,3,8,15,4,15,2,11,4,
                                  0,4,5,1,6,5,1,6,7,4});
  value.width=2.5;
  value.attributes["opaque"]=std::string("kept resize style");
  return value;
}

void affine(const Selection &source,const Selection &actual,
            std::array<double,2> anchor,std::array<double,2> scale) {
  const auto &from=source.path_commands(), &to=actual.path_commands();
  check(from.size()==to.size(),"Resize changed the literal command count");
  for(std::size_t at=0;at<from.size();) {
    const int op=int(from[at]);
    check(to[at]==from[at],"Resize changed a path opcode");
    ++at;
    const std::size_t size=op==4?0:op==3?6:op==2?4:2;
    for(std::size_t i=0;i<size;++i) {
      const auto axis=i%2;
      const double expected=anchor[axis]+scale[axis]*(from[at+i]-anchor[axis]);
      check(to[at+i]==expected,"Resize differs from the independent affine coordinates");
    }
    at+=size;
  }
  check(actual.width==source.width && actual.attributes==source.attributes,
        "Resize changed style or opaque attributes");
}

class Harness {
public:
  explicit Harness(int view=0,float scale=1,Selection value=fixture_path())
      : owner(Dataset::from_stack({Image(64,48,1,PixelType::UInt16)}).observe_reads(reads)),
        original(std::move(value)), visible(original), plane(view), ui_scale(scale) {
    owner.select(original);
    context_=ImGui::CreateContext();
    auto &io=ImGui::GetIO();
    io.IniFilename=nullptr; io.LogFilename=nullptr;
    io.DisplaySize={1400,1000}; io.DeltaTime=1.f/60.f;
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    io.DisplayFramebufferScale={scale,scale};
    io.FontGlobalScale=scale;
    ImGui::GetStyle().ScaleAllSizes(scale);
    unsigned char *pixels=nullptr; int width=0,height=0;
    io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    check(pixels && width>0 && height>0,"Missing ImGui font atlas");
    frame(); frame();
  }
  ~Harness() { ImGui::DestroyContext(context_); }
  Harness(const Harness &)=delete;
  Harness &operator=(const Harness &)=delete;

  void observe(const std::string &name,bool available,float) {
    const auto low=ImGui::GetItemRectMin(), high=ImGui::GetItemRectMax();
    controls[name]={{"enabled",available},{"visible",ImGui::IsItemVisible()},
      {"point",{(low.x+high.x)/2,(low.y+high.y)/2}},
      {"rect",{low.x,low.y,high.x,high.y}}};
  }
  void frame() {
    check(++frames_<=2200,"Resize fixture exceeded its explicit frame bound");
    ImGui::SetCurrentContext(context_); ImGui::NewFrame();
    ImGui::SetNextWindowPos({20,20},ImGuiCond_Always);
    ImGui::SetNextWindowSize({1150,850},ImGuiCond_Always);
    ImGui::Begin("Path resize input fixture",nullptr,
        ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|
        ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoScrollbar);
    ImGui::PushItemWidth(400*ui_scale);
    controls=ui::Json::object();
    auto *draw=ImGui::GetWindowDrawList();
    const auto initial=ImGui::GetCursorScreenPos();
    const int first_vertex=draw->VtxBuffer.Size;
    subject.draw(visible,64,48,plane,limits,enabled,
      [&](const Selection &candidate) {
        try {
          ++attempts;
          observed=candidate;
          if(on_stage)on_stage(candidate);
          else { visible=candidate; staged=candidate; }
        } catch(const FixtureFailure &error) {
          // A product callback catch must not turn a failed assertion into a
          // successful error-recovery test.
          callback_failure_=error.what();
          throw;
        }
      },[&](const auto &name,bool available,float value) { observe(name,available,value); });
    error_vertices=0;
    if(!state().at("error").get<std::string>().empty()) {
      const float cutoff=controls.contains("Stage resize")
          ? controls.at("Stage resize").at("rect")[3].get<float>() : initial.y-1;
      for(int i=first_vertex;i<draw->VtxBuffer.Size;++i)
        if(draw->VtxBuffer[i].pos.y>cutoff)++error_vertices;
    }
    // Minimal host adapter: cancellation invokes only the helper's public reset
    // and restores the caller-owned preview, as a real owner is required to do.
    if(ImGui::Button("Cancel resize draft")) {
      subject.reset(); visible=original; staged.reset();
    }
    observe("Cancel resize draft",true,0);
    ImGui::PopItemWidth(); ImGui::End(); ImGui::Render();
    check(callback_failure_.empty(),callback_failure_.c_str());
    const auto *data=ImGui::GetDrawData();
    check(data && data->Valid && data->FramebufferScale.x==ui_scale &&
          data->FramebufferScale.y==ui_scale,"Resize form did not render at the declared component scale");
  }
  ui::Json state() const { return subject.diagnostics(); }
  const char *label(int axis) const {
    static const char *labels[3][2]={{"Resize target X","Resize target Y"},
                                    {"Resize target X","Resize target Z"},
                                    {"Resize target Y","Resize target Z"}};
    return labels[plane][axis];
  }
  bool available(const std::string &name) const { return controls.at(name).at("enabled").get<bool>(); }
  void click(const std::string &name) {
    const auto item=controls.at(name);
    check(item.at("visible").get<bool>(),"Requested resize control is clipped");
    const auto point=item.at("point");
    const float x=point[0].get<float>(),y=point[1].get<float>();
    check(std::isfinite(x) && std::isfinite(y) && x>=0 && x<1400 && y>=0 && y<1000,
          "Resize control is outside its viewport");
    auto &io=ImGui::GetIO(); io.AddMousePosEvent(x,y); frame();
    io.AddMouseButtonEvent(0,true); frame();
    io.AddMouseButtonEvent(0,false); frame(); frame();
  }
  void key(ImGuiKey value) {
    ImGui::GetIO().AddKeyEvent(value,true); frame();
    ImGui::GetIO().AddKeyEvent(value,false); frame(); frame();
  }
  void replace(int axis,const char *text) {
    click(label(axis));
    auto &io=ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl,true); frame();
    io.AddKeyEvent(ImGuiKey_A,true); frame();
    io.AddKeyEvent(ImGuiKey_A,false); frame();
    io.AddKeyEvent(ImGuiMod_Ctrl,false); frame();
    if(*text) { io.AddInputCharactersUTF8(text); frame(); frame(); }
    else key(ImGuiKey_Backspace);
  }
  void choose(const char *handle,int index) {
    click("Resize handle");
    const std::string choice=std::string("Resize handle.")+handle;
    check(controls.contains(choice) && available(choice),"Required handle is not available in the real combo");
    click(choice);
    check(state().at("handle")==index,"Actual combo input did not select the requested handle");
  }
  void result(std::array<double,2> anchor,std::array<double,2> scale) const {
    check(staged.has_value() && subject.active() && !subject.invalid(),"Valid Stage did not create a ready private result");
    check(state().at("staged").get<bool>() && !state().at("unstaged").get<bool>(),
          "Accepted Stage retained an unstaged field");
    affine(original,*staged,anchor,scale);
  }
  void unchanged(const DocumentSnapshot &source) const {
    check(owner.snapshot().shares_data(source) && owner.undo_steps()==0,
          "Resize controls published a private edit or history");
    check(owner.snapshot().data().selection==original,"Resize controls changed the source selection");
    const auto count=reads.counts();
    check(count.plane_reads==0 && count.region_reads==0,"Resize controls read source samples");
  }
  void clean() const {
    const auto value=state();
    check(!subject.active() && !subject.invalid() && !value.at("staged").get<bool>() &&
          !value.at("unstaged").get<bool>() && value.at("error")=="",
          "Reset revived staging or retained an invalid draft");
    check(value.at("handle")==4 && !value.at("aspect").get<bool>() && !value.at("center").get<bool>(),
          "Reset retained resize interaction options");
  }

  DatasetReadObserver reads;
  Document owner;
  Selection original,visible;
  ui::PathResizeControls subject;
  SelectionEditLimits limits;
  std::optional<Selection> staged,observed;
  std::function<void(const Selection &)> on_stage;
  ui::Json controls=ui::Json::object();
  int plane=0;
  float ui_scale=1;
  bool enabled=true;
  std::size_t attempts=0,error_vertices=0;
private:
  ImGuiContext *context_=nullptr;
  std::size_t frames_=0;
  std::string callback_failure_;
};

struct HandleCase {
  const char *name,*seed_x,*seed_y,*target_x,*target_y;
  bool x,y;
  std::array<double,2> anchor,scale;
};
const std::array<HandleCase,8> handles{{
  {"NW","2","3","-8","-19",true,true,{12,14},{2,3}},
  {"N","7","3","7","-19",false,true,{7,14},{1,3}},
  {"NE","12","3","22","-19",true,true,{2,14},{2,3}},
  {"E","12","8.5","22","8.5",true,false,{2,8.5},{2,1}},
  {"SE","12","14","22","36",true,true,{2,3},{2,3}},
  {"S","7","14","7","36",false,true,{7,3},{1,3}},
  {"SW","2","14","-8","36",true,true,{12,3},{2,3}},
  {"W","2","8.5","-8","8.5",true,false,{12,8.5},{2,1}}
}};

void handles_and_plane_labels() {
  for(int plane=0;plane<3;++plane)for(std::size_t index=0;index<handles.size();++index) {
    const auto &item=handles[index];
    named("PR01_plane"+std::to_string(plane)+"_"+item.name);
    Harness h(plane); const auto before=h.owner.snapshot();
    check(h.controls.contains(h.label(0)) && h.controls.contains(h.label(1)),"Scientific-plane numeric labels are missing");
    const char *absent=plane==0?"Resize target Z":plane==1?"Resize target Y":"Resize target X";
    check(!h.controls.contains(absent),"Resize form displayed the wrong scientific axis");
    h.choose(item.name,int(index));
    check(h.state().at("coordinates")==ui::Json::array({item.seed_x,item.seed_y}),
          "Handle seed used a control hull or another plane's coordinates");
    check(h.available(h.label(0))==item.x && h.available(h.label(1))==item.y,
          "Side-handle axis enablement is wrong");
    if(!item.x) { h.replace(0,"99"); check(h.state().at("coordinates")[0]==item.seed_x,"Disabled horizontal field accepted input"); }
    if(!item.y) { h.replace(1,"99"); check(h.state().at("coordinates")[1]==item.seed_y,"Disabled vertical field accepted input"); }
    if(item.x)h.replace(0,item.target_x);
    if(item.y)h.replace(1,item.target_y);
    h.click("Stage resize");
    check(h.attempts==1,"Stage did not call the owner exactly once");
    h.result(item.anchor,item.scale); h.unchanged(before);
    h.click("Stage resize");
    check(h.attempts==2,"Repeated Stage did not dispatch one frozen-base result");
    h.result(item.anchor,item.scale); h.unchanged(before);
    h.click("Cancel resize draft"); h.clean();
    check(h.visible==h.original && !h.staged,"Cancel did not release the caller's private preview");
    h.unchanged(before);
  }
}

void aspect_center_and_frozen_base() {
  named("PR02_center_aspect_and_frozen_base");
  Harness h; const auto before=h.owner.snapshot();
  h.click("From center"); h.replace(0,"17"); h.replace(1,"30.5");
  h.click("Stage resize"); h.result({7,8.5},{2,4});
  h.click("Preserve aspect"); h.click("Stage resize"); h.result({7,8.5},{4,4});
  h.click("From center"); h.click("Stage resize"); h.result({2,3},{2.5,2.5});
  h.choose("E",3); h.replace(0,"22"); h.click("Stage resize");
  h.result({2,8.5},{2,2});
  h.click("From center"); h.replace(0,"17"); h.click("Stage resize");
  h.result({7,8.5},{2,2});
  h.click("Cancel resize draft"); h.clean();
  h.replace(0,"22"); h.replace(1,"25"); h.click("Stage resize");
  h.result({2,3},{2,2});
  // Opposite-handle restaging distinguishes the frozen original from the
  // already enlarged visible preview: the opposite anchor remains (12,14).
  h.choose("NW",0); h.replace(0,"-8"); h.replace(1,"-19");
  h.click("Stage resize"); h.result({12,14},{2,3}); h.unchanged(before);
}

void invalid_collapse_caps_and_correction() {
  named("PR03_invalid_collapse_caps_and_correction");
  Harness h; const auto before=h.owner.snapshot();
  h.replace(0,"not-a-number"); h.click("Stage resize");
  check(h.attempts==0 && h.subject.invalid() && h.visible==h.original && h.error_vertices>0,
        "Invalid numeric input staged geometry or hid its error");
  h.replace(0,"2"); h.click("Stage resize");
  check(h.attempts==0 && h.state().at("error")=="Selection resize is empty" && h.error_vertices>0,
        "Collapsed target did not refuse visibly before the callback");
  h.replace(0,"22"); h.click("Stage resize"); h.result({2,3},{2,1});
  const auto accepted=*h.staged;
  h.replace(1,"nan"); h.click("Stage resize");
  check(h.attempts==1 && h.visible==accepted && h.subject.invalid() && h.error_vertices>0,
        "Invalid correction destroyed the last valid private result");
  h.replace(1,"14"); h.replace(0,"-8"); h.click("Stage resize");
  h.result({2,3},{-1,1});
  for(int cap=0;cap<3;++cap) {
    const auto attempts=h.attempts; const auto prior=h.visible;
    if(cap==0)h.limits.max_working_bytes=1;
    if(cap==1)h.limits.max_xy_pixels=64*48-1;
    if(cap==2)h.limits.max_vertices=1;
    h.frame();
    check(h.attempts==attempts && h.visible==prior && h.subject.invalid() &&
          h.error_vertices>0 && !h.controls.contains("Stage resize"),
          "Current geometry limit admitted a callback or left an actionable form");
    h.limits={}; h.frame(); h.click("Stage resize");
    h.result({2,3},{-1,1}); h.unchanged(before);
  }
  h.click("Cancel resize draft"); h.clean(); h.unchanged(before);
}

void unavailable_axes() {
  for(bool horizontal:{true,false}) {
    named(horizontal?"PR04_horizontal_degenerate_axis":"PR04_vertical_degenerate_axis");
    auto line=Selection::from_path(horizontal?std::vector<double>{0,2,5,1,12,5}
                                            :std::vector<double>{0,5,3,1,5,14});
    line.width=3; line.attributes["line"]=std::string("kept");
    Harness h(2,1,line); const auto before=h.owner.snapshot();
    check(h.state().at("handle")== (horizontal?3:1),"Degenerate path did not seed an available handle");
    h.click("Resize handle");
    for(std::size_t index=0;index<handles.size();++index) {
      const bool expected=horizontal?(index==3 || index==7):(index==1 || index==5);
      check(h.available(std::string("Resize handle.")+handles[index].name)==expected,
            "Degenerate combo exposed a collapsed source axis");
    }
    h.click(std::string("Resize handle.")+(horizontal?"W":"S"));
    check(h.available(h.label(0))==horizontal && h.available(h.label(1))!=horizontal,
          "Degenerate path enabled the wrong coordinate field");
    h.replace(horizontal?0:1,horizontal?"-8":"36");
    h.click("Preserve aspect"); h.click("Stage resize");
    check(h.attempts==0 && h.subject.invalid() && h.error_vertices>0,
          "Aspect preservation admitted a collapsed source dimension");
    h.click("Preserve aspect"); h.click("Stage resize");
    h.result(horizontal?std::array<double,2>{12,5}:std::array<double,2>{5,3},
             horizontal?std::array<double,2>{2,1}:std::array<double,2>{1,3});
    h.unchanged(before);
  }
  named("PR04_no_resize_axes");
  Harness h(0,1,Selection::from_path({0,4,6}));
  const auto before=h.owner.snapshot();
  check(!h.available("Stage resize") && !h.available(h.label(0)) && !h.available(h.label(1)),
        "Point path exposed a resize operation");
  h.click("Stage resize");
  check(h.attempts==0,"Disabled zero-axis Stage dispatched"); h.unchanged(before);
}

void disabled_and_scaled_form() {
  named("PR05_disabled_then_scaled_component");
  Harness h(2,1.5f); const auto before=h.owner.snapshot();
  h.enabled=false; h.frame();
  const auto initial=h.state();
  h.replace(0,"99"); h.click("From center"); h.click("Preserve aspect"); h.click("Stage resize");
  check(h.attempts==0 && h.state().at("coordinates")==initial.at("coordinates") &&
        !h.state().at("center").get<bool>() && !h.state().at("aspect").get<bool>(),
        "Disabled scaled form accepted input");
  h.enabled=true; h.frame();
  h.choose("SE",4); h.replace(0,"22"); h.replace(1,"36");
  h.click("Stage resize"); h.result({2,3},{2,3}); h.unchanged(before);
  h.click("Cancel resize draft"); h.clean();
}

void publisher_exception() {
  named("PR06_staging_exception_and_recovery");
  Harness h; const auto before=h.owner.snapshot();
  h.replace(0,"22"); h.replace(1,"36");
  h.on_stage=[](const auto &) { throw std::runtime_error("Owned resize staging refusal"); };
  h.click("Stage resize");
  check(h.attempts==1 && !h.staged && h.visible==h.original && h.subject.invalid() &&
        h.state().at("error")=="Owned resize staging refusal" && h.error_vertices>0,
        "Staging exception published, hid its error or lost the recoverable form");
  h.unchanged(before); h.on_stage={}; h.click("Stage resize");
  h.result({2,3},{2,3}); h.unchanged(before);
}

void reentrant_reset(bool throws) {
  named(throws?"PR07_callback_reset_then_throw":"PR07_callback_reset");
  Harness h; const auto before=h.owner.snapshot();
  h.replace(0,"22"); h.replace(1,"36");
  h.on_stage=[&](const Selection &candidate) {
    h.subject.reset();
    affine(h.original,candidate,{2,3},{2,3});
    if(throws)throw std::runtime_error("Stale reset callback error");
  };
  h.click("Stage resize");
  check(h.attempts==1 && !h.staged && h.visible==h.original,"Reset callback changed the caller's preview");
  h.clean(); h.unchanged(before);
  h.on_stage={}; h.replace(0,"22"); h.click("Stage resize");
  h.result({2,3},{2,1}); h.unchanged(before);
}
} // namespace

int main() {
  try {
    handles_and_plane_labels();
    aspect_center_and_frozen_base();
    invalid_collapse_caps_and_correction();
    unavailable_axes();
    disabled_and_scaled_form();
    publisher_exception();
    reentrant_reset(false); reentrant_reset(true);
    std::cout << checks << " assertions passed across " << cases
              << " actual-input path resize cases\n";
  } catch(const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
