// Component acceptance through real ImGuiIO events. Literal command streams
// are the geometry oracle. This does not claim OS-window or launcher coverage.
#include "path_authoring_controls.hpp"
#include <imagejxx/selection_creation.hpp>
#include <imgui_internal.h>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace imagejxx;
namespace ui = imagejxx::desktop;

namespace {
std::size_t checks = 0;
std::string current_case;
struct FixtureFailure final : std::runtime_error {
  using std::runtime_error::runtime_error;
};

void check(bool condition, const char *message) {
  ++checks;
  if (!condition) throw FixtureFailure(current_case + ": " + message);
}

class Harness {
public:
  Harness()
      : owner(Dataset::from_stack({Image(64,48,1,PixelType::UInt16)}).observe_reads(reads)),
        original("rectangle",{{1,1},{4,5}}), previous("ellipse",{{2,2},{6,7}}) {
    owner.select(previous);
    owner.select(original);
    context_ = ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = {1200,900};
    io.DeltaTime = 1.f/60.f;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    unsigned char *pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    check(pixels && width > 0 && height > 0,"Missing ImGui font atlas");
    style.width = 2.5;
    style.attributes["opaque"] = std::string("retained input style");
    frame();
    frame();
  }
  ~Harness() { ImGui::DestroyContext(context_); }
  Harness(const Harness &) = delete;
  Harness &operator=(const Harness &) = delete;

  void frame() {
    check(++frames_ <= 1600,"Component fixture exceeded its frame bound");
    ImGui::SetCurrentContext(context_);
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({20,20},ImGuiCond_Always);
    ImGui::SetNextWindowSize({1000,760},ImGuiCond_Always);
    ImGui::Begin("Path authoring input fixture",nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoScrollbar);
    auto *draw = ImGui::GetWindowDrawList();
    const int first_control_vertex = draw->VtxBuffer.Size;
    shown_->controls(enabled,[&](const DocumentSnapshot &source,
                                  const Selection &selection,
                                  const ui::PathAuthoringCanvas &captured) {
      try {
      ++publications;
      check(source.same_document(owner.snapshot()),"Publisher captured another owner");
      check(captured == canvas,"Publisher lost the captured canvas");
      if (on_publish) {
        on_publish(source,selection,captured);
        return;
      }
      SelectionCreationLimits limits;
      limits.max_working_bytes = publication_cap;
      ExecutionControl control;
      control.checkpoint = [&] { ++native_callbacks; };
      control.progress = [&](auto,auto) { ++native_callbacks; };
      const auto command = resolve_selection_creation_command(source,
          {SelectionCreationOperation::Replace,selection,
           SelectionCreationGeometry::NativePathV4},limits,control);
      auto edit = prepare_selection_creation(command,control,publication_cap);
      owner.commit(edit);
      } catch(const FixtureFailure &error) {
        // The component deliberately catches publisher exceptions. Keep test
        // assertion failures distinct so that this recovery cannot hide one.
        callback_failure_=error.what();
        throw;
      }
    });
    // The last controls row is its error TextUnformatted. Observe actual glyph
    // vertices in that row, rather than inventing a diagnostic "visible" flag.
    const auto after_controls = ImGui::GetCursorScreenPos();
    const float bottom = after_controls.y-ImGui::GetStyle().ItemSpacing.y;
    const float top = bottom-ImGui::GetTextLineHeight();
    error_vertices = 0;
    for (int i=first_control_vertex;i<draw->VtxBuffer.Size;++i) {
      const auto p = draw->VtxBuffer[i].pos;
      if (p.y >= top && p.y <= bottom) ++error_vertices;
    }
    ImGui::SetCursorScreenPos({80+canvas_offset,260});
    ImGui::InvisibleButton("path_canvas",{512,384},ImGuiButtonFlags_MouseButtonLeft);
    const auto low = ImGui::GetItemRectMin(), high = ImGui::GetItemRectMax();
    canvas.id = ImGui::GetItemID();
    canvas.plane = 0; canvas.coordinate = 0;
    canvas.width = 64; canvas.height = 48;
    canvas.origin = canvas.low = {low.x,low.y};
    canvas.high = {high.x,high.y};
    canvas.scale = {8,8};
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    shown_->pointer(owner.snapshot(),canvas,enabled,hovered,style,collection_cap);
    if (draw_preview) shown_->draw(canvas,draw,collection_cap);
    ImGui::End();
    ImGui::Render();
    check(callback_failure_.empty(),callback_failure_.c_str());
    check(ImGui::GetDrawData() && ImGui::GetDrawData()->Valid,
          "ImGui did not produce an actual rendered frame");
  }

  ui::Json state() const { return shown_->diagnostics(); }
  std::uint64_t generation() const { return state().at("generation").get<std::uint64_t>(); }
  bool control_enabled(const char *name) const {
    return state().at("controls").at(name).at("enabled").get<bool>();
  }
  void show(ui::PathAuthoringControls &value) {
    shown_ = &value;
    frame(); frame();
  }
  void move(float x,float y) {
    ImGui::GetIO().AddMousePosEvent(x,y);
    frame();
  }
  void move_canvas(double x,double y) {
    move(canvas.origin[0]+float(x)*canvas.scale[0],
         canvas.origin[1]+float(y)*canvas.scale[1]);
  }
  void press_canvas(double x,double y) {
    move_canvas(x,y);
    ImGui::GetIO().AddMouseButtonEvent(0,true);
    frame();
  }
  void release() {
    ImGui::GetIO().AddMouseButtonEvent(0,false);
    frame(); frame();
  }
  void point(double x,double y) {
    press_canvas(x,y);
    release();
  }
  void click(const char *name) {
    const auto item = state().at("controls").at(name);
    check(item.at("visible").get<bool>(),"Requested control is clipped");
    const auto p = item.at("point");
    check(p.is_array() && p.size()==2,"Control point is malformed");
    const float x=p[0].get<float>(), y=p[1].get<float>();
    check(std::isfinite(x) && std::isfinite(y) && x>=0 && x<1200 && y>=0 && y<900,
          "Control is outside the fixture viewport");
    move(x,y);
    ImGui::GetIO().AddMouseButtonEvent(0,true);
    frame();
    release();
  }
  void key(ImGuiKey value) {
    ImGui::GetIO().AddKeyEvent(value,true);
    frame();
    ImGui::GetIO().AddKeyEvent(value,false);
    frame(); frame();
  }
  void commands(const std::vector<double> &expected) const {
    check(shown_->preview().has_value(),"Missing private complete-command preview");
    check(shown_->preview()->path_commands()==expected,"Private commands differ from literal input");
    check(shown_->preview()->width==2.5 && shown_->preview()->attributes==style.attributes,
          "Private authoring changed captured style");
  }
  void unread() const {
    const auto counts = reads.counts();
    check(counts.plane_reads==0 && counts.region_reads==0,"Authoring acquired source samples");
  }
  void unchanged(const DocumentSnapshot &before) const {
    check(owner.snapshot().shares_data(before),"Private input changed the document");
    check(owner.undo_steps()==0,"Private input created history");
    unread();
  }
  void cleared(const char *reason) const {
    const auto value=state();
    check(!shown_->active() && !shown_->ready() && !shown_->preview() && !shown_->canvas(),
          "Ended authoring retained a draft or canvas");
    check(value.at("pending").empty() && !value.at("press_pending").get<bool>() &&
          !value.at("finish_requested").get<bool>() && value.at("source").is_null() &&
          value.at("error")=="","Ended authoring retained private input or error");
    check(value.at("last_cancel")==reason,"Unexpected authoring termination reason");
  }
  void published(const DocumentSnapshot &before,const std::vector<double> &expected) const {
    const auto after=owner.snapshot();
    check(after.data().selection && after.data().selection->path_commands()==expected,
          "Publisher committed different path commands");
    check(after.data().previous_selection==original && owner.undo_steps()==1,
          "Authored Finish did not publish one Previous/history transition");
    check(after.data().dataset.shares_pixels(before.data().dataset),"Finish replaced source pixels");
    unread();
  }

  DatasetReadObserver reads;
  Document owner;
  Selection original, previous, style{"rectangle"};
  ui::PathAuthoringControls subject;
  ui::PathAuthoringControls::Publish on_publish;
  ui::PathAuthoringCanvas canvas;
  std::size_t publications=0, native_callbacks=0, error_vertices=0;
  std::size_t collection_cap=16*1024*1024, publication_cap=16*1024*1024;
  bool enabled=true, draw_preview=true;
  float canvas_offset=0;
private:
  ImGuiContext *context_=nullptr;
  ui::PathAuthoringControls *shown_=&subject;
  std::size_t frames_=0;
  std::string callback_failure_;
};

void matched_groups_and_finish() {
  current_case="PA01_matched_input_private_groups_and_finish";
  Harness h;
  const auto before=h.owner.snapshot();
  h.press_canvas(2,3);
  check(h.state().at("press_pending").get<bool>() && !h.subject.preview(),
        "Press alone added a point");
  h.move_canvas(3.5,4.25);
  check(!h.subject.preview(),"Held pointer movement added a point");
  h.release();
  h.commands({0,3.5,4.25});
  check(!h.subject.ready(),"An isolated Move enabled Finish");
  h.click("Quadratic");
  h.point(6,2);
  h.commands({0,3.5,4.25});
  check(h.state().at("pending")==ui::Json::array({ui::Json::array({6.,2.})}),
        "Quadratic control was not kept as a separate pending point");
  check(!h.control_enabled("Cubic") && !h.control_enabled("Finish path"),
        "Incomplete group enabled mode switching or Finish");
  h.click("Finish path");
  h.click("Cubic");
  check(h.publications==0 && h.state().at("next_segment")==2,
        "Disabled controls changed intent or published");
  h.click("Remove last command");
  h.commands({0,3.5,4.25});
  check(h.state().at("pending").empty(),"Remove did not discard only the pending group");
  h.point(6,2); h.point(10,5);
  h.commands({0,3.5,4.25,2,6,2,10,5});
  h.click("Cubic");
  h.point(11,7); h.point(13,7);
  h.commands({0,3.5,4.25,2,6,2,10,5});
  check(h.state().at("pending").size()==2 && !h.subject.ready(),
        "Partial cubic was accepted as a complete command");
  h.point(15,4);
  h.click("Close contour");
  h.click("New contour");
  h.point(20,10);
  h.click("Line");
  h.point(24,14);
  const std::vector<double> expected{0,3.5,4.25,2,6,2,10,5,3,11,7,13,7,15,4,4,
                                     0,20,10,1,24,14};
  h.commands(expected); h.unchanged(before);
  check(h.subject.ready() && h.state().at("visual_builds").get<std::size_t>()>0,
        "Complete drawn path did not become ready");
  h.click("Finish path");
  check(h.publications==1,"Finish did not call its publisher exactly once");
  h.cleared("finished"); h.published(before,expected);
  h.owner.undo();
  check(h.owner.snapshot().data().selection==h.original &&
        h.owner.snapshot().data().previous_selection==h.previous,"Finish Undo lost source selections");
  h.owner.redo();
  check(h.owner.snapshot().data().selection->path_commands()==expected,
        "Finish Redo lost literal commands or closed an open contour");
}

void cancel_and_capture() {
  current_case="PA02_cancel_and_unmatched_release";
  Harness h;
  const auto before=h.owner.snapshot();
  h.move_canvas(2,2); h.release();
  check(!h.subject.active() && !h.subject.preview(),"Unmatched release started a draft");
  h.point(2,2);
  h.press_canvas(4,4);
  h.move(h.canvas.high[0]+20,h.canvas.low[1]+16);
  h.release();
  h.cleared("outside_release"); h.unchanged(before);
  h.point(2,2); h.click("Cubic"); h.point(4,4);
  check(h.state().at("pending").size()==1,"Cancel fixture did not collect a private point");
  h.click("Cancel path");
  h.cleared("cancel_button"); h.unchanged(before);
  h.point(2,2); h.key(ImGuiKey_Escape);
  h.cleared("escape"); h.unchanged(before);
  h.point(2,2); h.enabled=false; h.frame();
  h.cleared("busy"); h.unchanged(before);
  h.enabled=true; h.frame(); h.point(2,2);
  h.canvas_offset=8; h.frame();
  h.cleared("canvas_changed"); h.unchanged(before);
  check(h.publications==0,"Canceled collection called the publisher");
}

void callback_cancel(bool throws) {
  current_case=throws?"PA03_callback_cancel_then_throw":"PA03_callback_cancel";
  Harness h;
  const auto before=h.owner.snapshot();
  h.point(2,2); h.point(6,4);
  h.on_publish=[&](const DocumentSnapshot &source,const Selection &value,const auto &) {
    h.subject.cancel("callback_cancel");
    check(source.shares_data(before) && value.path_commands()==std::vector<double>({0,2,2,1,6,4}),
          "Cancellation invalidated copied publisher arguments");
    if(throws)throw std::runtime_error("Stale canceled callback error");
  };
  h.click("Finish path");
  check(h.publications==1,"Canceling publisher was dispatched repeatedly");
  h.cleared("callback_cancel"); h.unchanged(before);
}

void callback_replace(bool throws) {
  current_case=throws?"PA04_callback_replace_then_throw":"PA04_callback_replace";
  Harness h;
  const auto before=h.owner.snapshot();
  h.point(2,2); h.point(6,4);
  const auto original_generation=h.generation();
  // The replacement is independently built by actual input. Its public cancel
  // starts a later lifecycle; no internal fields or synthetic ready flags.
  ui::PathAuthoringControls replacement;
  replacement.cancel("replacement_lifecycle");
  h.show(replacement);
  h.point(30,20); h.point(40,30);
  const std::vector<double> expected{0,30,20,1,40,30};
  h.commands(expected);
  const auto replacement_generation=h.generation();
  check(replacement_generation>original_generation && replacement.ready(),
        "Replacement fixture is not a newer complete draft");
  h.show(h.subject);
  h.on_publish=[&](const DocumentSnapshot &source,const Selection &value,const auto &) {
    h.subject.cancel("callback_replace");
    h.subject=std::move(replacement);
    check(source.shares_data(before) && value.path_commands()==std::vector<double>({0,2,2,1,6,4}),
          "Replacement changed the older publisher's frozen arguments");
    if(throws)throw std::runtime_error("Stale replaced callback error");
  };
  h.click("Finish path");
  check(h.publications==1 && h.subject.active() && h.subject.ready() &&
        h.generation()==replacement_generation,"Older Finish cleared the replacement generation");
  check(h.state().at("error")=="","Older callback overwrote replacement error state");
  h.commands(expected); h.unchanged(before);
  h.on_publish={}; h.click("Finish path");
  check(h.publications==2,"Replacement Finish did not publish exactly once");
  h.cleared("finished"); h.published(before,expected);
}

void callback_error_and_recovery() {
  current_case="PA05_callback_exception_visible_and_recoverable";
  Harness h;
  const auto before=h.owner.snapshot();
  h.point(2,2); h.point(6,4);
  check(h.error_vertices==0,"Blank error row unexpectedly contains glyphs");
  h.on_publish=[](const auto &,const auto &,const auto &) {
    throw std::runtime_error("Owned path publisher refusal");
  };
  h.click("Finish path");
  check(h.publications==1 && h.subject.active() && !h.subject.ready(),
        "Publisher exception discarded the valid draft or left Finish enabled");
  h.commands({0,2,2,1,6,4}); h.unchanged(before);
  check(h.state().at("error")=="Owned path publisher refusal" && h.error_vertices>0,
        "Publisher exception did not render a visible error");
  check(!h.control_enabled("Finish path"),"Refused draft still enables Finish");
  h.on_publish={}; h.point(10,7);
  check(h.state().at("error")=="" && h.subject.ready(),"Valid input did not recover the draft");
  h.click("Finish path");
  h.cleared("finished"); h.published(before,{0,2,2,1,6,4,1,10,7});
}

void cap_refusal_and_recovery() {
  current_case="PA06_publication_cap_before_owner_mutation";
  Harness h;
  const auto before=h.owner.snapshot();
  h.point(2,2); h.point(6,4);
  h.publication_cap=1;
  h.click("Finish path");
  check(h.publications==1 && h.native_callbacks==0,"Publication cap ran native callbacks");
  check(h.subject.active() && !h.subject.ready() && !h.state().at("error").get<std::string>().empty() &&
        h.error_vertices>0,"Cap refusal discarded its recoverable draft or hid the error");
  h.commands({0,2,2,1,6,4}); h.unchanged(before);
  h.publication_cap=16*1024*1024;
  h.point(10,7); h.click("Finish path");
  h.cleared("finished"); h.published(before,{0,2,2,1,6,4,1,10,7});
}

void collection_cap_and_keyboard_cleanup() {
  current_case="PA07_collection_cap_and_keyboard_reset";
  Harness h;
  const auto before=h.owner.snapshot();
  h.point(2,2); h.point(6,4);
  // Isolate collection admission from renderer admission. The Window suite
  // owns the latter; this case still renders the real controls/error row.
  h.draw_preview=false;
  h.collection_cap=1;
  h.point(10,7);
  check(h.subject.active() && !h.subject.ready() && h.publications==0 &&
        !h.state().at("error").get<std::string>().empty() && h.error_vertices>0,
        "Collection cap failed to refuse privately and visibly");
  h.commands({0,2,2,1,6,4}); h.unchanged(before);
  h.collection_cap=16*1024*1024; h.draw_preview=true;
  h.point(10,7);
  check(h.subject.ready() && h.state().at("error")=="","Collection did not recover after valid cap");
  h.key(ImGuiKey_Escape);
  h.cleared("escape"); h.unchanged(before);
  const auto builds=h.state().at("visual_builds").get<std::size_t>();
  h.point(2,2); h.point(6,4);
  check(h.state().at("visual_builds").get<std::size_t>()>builds,"Reset retained the old drawing cache");
  h.key(ImGuiKey_Enter);
  check(h.publications==1,"Keyboard Finish was lost or repeated");
  h.cleared("finished"); h.published(before,{0,2,2,1,6,4});
}
} // namespace

int main() {
  try {
    matched_groups_and_finish();
    cancel_and_capture();
    callback_cancel(false); callback_cancel(true);
    callback_replace(false); callback_replace(true);
    callback_error_and_recovery();
    cap_refusal_and_recovery();
    collection_cap_and_keyboard_cleanup();
    std::cout << checks << " assertions passed across 9 actual-input path authoring cases\n";
  } catch(const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
