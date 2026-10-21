#include "headless_session_internal.hpp"
#include "window.hpp"
#include <imagejxx/gaussian.hpp>
#include <imgui_internal.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace imagejxx::desktop {
namespace {
using namespace std::chrono_literals;
constexpr int width = 23, height = 17;

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
std::string threadId() {
  std::ostringstream value;
  value << std::this_thread::get_id();
  return value.str();
}
Json currentContext() {
  const auto *context = ImGui::GetCurrentContext();
  return {{"current_context_id", static_cast<std::uint64_t>(
              reinterpret_cast<std::uintptr_t>(context))},
          {"imgui_frame", context ? Json(context->FrameCount) : Json()},
          {"imgui_time", context ? Json(context->Time) : Json()}};
}
Json usageJson(const JobUsage &usage) {
  return {{"running", usage.running}, {"queued", usage.queued},
          {"running_bytes", usage.running_bytes}, {"retained_bytes", usage.retained_bytes},
          {"reserved_bytes", usage.reserved_bytes}};
}
std::filesystem::path freshDirectory(const std::string &directory) {
  require(!directory.empty(), "Session consumer requires an output directory");
  const auto path = std::filesystem::absolute(std::filesystem::u8path(directory)).lexically_normal();
  require(path != path.root_path(), "Session consumer cannot own a filesystem root");
  std::filesystem::create_directories(path.parent_path());
  require(std::filesystem::create_directory(path), "Session consumer directory must be fresh");
  return path;
}
int caseNumber(const std::string &caseId) {
  for (int number = 1; number <= 8; ++number)
    if (caseId == "S0" + std::to_string(number)) return number;
  throw std::invalid_argument("Session case must be S01..S08");
}
Stack inputStack(int count) {
  Stack planes;
  for (int z = 0; z < count; ++z) {
    Image plane(width, height, 1, PixelType::UInt8);
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x)
        plane.at(x, y) = (37*x + 61*y + 13*x*y + 43*z) % 251;
    planes.push_back(std::move(plane));
  }
  return planes;
}
std::vector<unsigned char> rawPixels(const Dataset &dataset) {
  const auto shape = dataset.shape();
  require(shape.t == 1 && shape.c == 1 && shape.x == width && shape.y == height &&
              shape.s == 1 && dataset.pixel_type() == PixelType::UInt8,
          "Session fixture shape/type changed");
  std::vector<unsigned char> bytes;
  bytes.reserve(static_cast<std::size_t>(shape.z)*width*height);
  for (int z = 0; z < shape.z; ++z)
    for (double value : dataset.plane({0,0,z})->pixels) {
      require(std::isfinite(value) && value >= 0 && value <= 255 && std::floor(value) == value,
              "Session UInt8 observation is not an exact byte");
      bytes.push_back(static_cast<unsigned char>(value));
    }
  return bytes;
}
Json exceptionJson(const std::exception &error) {
  const char *type = dynamic_cast<const std::invalid_argument *>(&error)
      ? "invalid_argument" : dynamic_cast<const std::logic_error *>(&error)
      ? "logic_error" : dynamic_cast<const std::runtime_error *>(&error)
      ? "runtime_error" : "exception";
  return {{"type", type}, {"message", error.what()}};
}
Json key(const char *name, bool down) {
  return {{"type", "key"}, {"name", name}, {"down", down}};
}

class SessionDriver {
public:
  SessionDriver(Window &window, std::shared_ptr<JobScheduler> scheduler,
                int number, std::string context, std::filesystem::path directory)
      : window_(window), scheduler_(std::move(scheduler)), number_(number),
        directory_(std::move(directory)), input_(inputStack(number == 4 ? 3 : 1)) {
    report_ = {{"schema_version", 1}, {"contract_id", "p02a.headless-session.001"},
        {"case_id", "S0" + std::to_string(number)}, {"context", std::move(context)},
        {"status", "unverified"}, {"acceptance_claim", false},
        {"fixture_id", "session-owned-u8-23x17"}, {"frames", Json::array()},
        {"lifecycle", Json::array()}, {"recovery", Json::array()},
        {"exceptions", Json::array()}, {"probes", Json::array()},
        {"control_observations", Json::array()},
        {"resource_checkpoints", Json::array()}, {"api_inputs", Json::array()},
        {"member_ids", Json::array()}, {"creator_thread", threadId()},
        {"scope", "Persistent renderer integration; no new upstream numerical/UI parity claim"}};
    owner_ = std::make_shared<imagejxx::Document>(Dataset::from_stack(input_), "Owned session source");
    window_.addNativeDocument(owner_);
    auto display = owner_->snapshot().display();
    display.crosshair = {7,5,number_ == 4 ? 1 : 0};
    display.properties["display_range"] = metadata_array({0.,255.});
    window_.setNativeDisplay(display);
    owner_->mark_saved();
    original_ = owner_->snapshot();
    report_["before"] = sourceState("source-before.u8");
    report_["api_inputs"].push_back({{"method", "addNativeDocument"},
        {"fixture_formula", "(37*x+61*y+13*x*y+43*z)%251"},
        {"shape", {input_.size(),height,width}}, {"mark_saved", true},
        {"crosshair", {7,5,number_ == 4 ? 1 : 0}}});
    checkpoint("initial");
  }
  ~SessionDriver() {
    if (!cleaned_) {
      try { cleanupJobs(); } catch (...) {}
    }
  }
  Json run() {
    try {
      switch (number_) {
      case 1: menuTextCase(); break;
      case 2: tabEnterCase(); break;
      case 3: previewEscapeCase(); break;
      case 4: stackCase(); break;
      case 5: defaultsAndPreflightCase(); break;
      case 6: ownershipCase(); break;
      case 7: initializationFailureCase(); break;
      case 8: frameFailureCase(); break;
      }
      cleanupJobs();
      report_["after"] = sourceState("source-after.u8");
      report_["original_after_raw"] = rawMember("original-after.u8", original_->data().dataset);
      report_["final_usage"] = usageJson(scheduler_->usage());
      report_["final_context"] = currentContext();
      report_["window_error"] = window_.error();
      checkpoint("final_driver_cleanup");
      save("case-unverified.json");
      verifyFinal();
      report_["status"] = "passed";
      save("case.json");
      cleaned_ = true;
      return report_;
    } catch (const std::exception &error) {
      report_["status"] = "failed";
      report_["failure"] = exceptionJson(error);
      try {
        cleanupJobs();
        report_["failure_context"] = currentContext();
        report_["failure_usage"] = usageJson(scheduler_->usage());
        save("case-failed.json");
      } catch (...) {}
      throw;
    }
  }

private:
  Window &window_;
  std::shared_ptr<JobScheduler> scheduler_;
  int number_;
  std::filesystem::path directory_;
  Stack input_;
  std::shared_ptr<imagejxx::Document> owner_;
  std::optional<DocumentSnapshot> original_;
  Json report_, last_;
  std::size_t frameOrdinal_ = 0, probeOrdinal_ = 0;
  bool cleaned_ = false;

  void writeMember(const std::string &name, const Json &value) {
    const auto path = directory_ / name;
    require(!std::filesystem::exists(path), "Session observation member already exists");
    writeJson(path.u8string(), value);
    report_["member_ids"].push_back(name);
  }
  void save(const std::string &name) {
    report_["member_ids"].push_back(name);
    const auto path = directory_/name;
    require(!std::filesystem::exists(path), "Session case report already exists");
    writeJson(path.u8string(), report_);
  }
  Json rawMember(const std::string &name, const Dataset &dataset) {
    const auto bytes = rawPixels(dataset);
    const auto path = directory_/name;
    require(!std::filesystem::exists(path), "Session raw member already exists");
    writeBytes(path.u8string(), bytes);
    report_["member_ids"].push_back(name);
    return {{"member_id", name}, {"path", path.u8string()}, {"encoding", "UInt8"},
        {"dtype", "uint8"}, {"shape", {dataset.shape().z,height,width}},
        {"sample_count", bytes.size()}, {"byte_count", bytes.size()}};
  }
  Json sourceSummary() const {
    const auto snapshot = owner_->snapshot();
    const auto revisions = snapshot.revisions();
    return {{"document_id", owner_->id},
        {"data_identity", snapshot.data_identity() ? Json(*snapshot.data_identity()) : Json()},
        {"dirty", snapshot.dirty()}, {"undo_steps", owner_->undo_steps()},
        {"redo_steps", owner_->redo_steps()},
        {"revisions", {{"pixels", revisions.pixels}, {"metadata", revisions.metadata},
            {"selection", revisions.selection}, {"display", revisions.display}}},
        {"document_info", window_.documentInfo()}, {"workflow", window_.workflow},
        {"results", window_.results}};
  }
  Json sourceState(const std::string &member) {
    auto result = sourceSummary();
    result["source_raw"] = rawMember(member, owner_->snapshot().data().dataset);
    return result;
  }
  void checkpoint(const char *name) {
    report_["resource_checkpoints"].push_back({{"name", name},
        {"usage", usageJson(scheduler_->usage())}, {"native_state", sourceSummary()}});
  }
  void lifecycle(HeadlessInputSession &session, const char *label, bool recovery = false) {
    Json value{{"label", label}, {"observed", session.lifecycle()}};
    report_[recovery ? "recovery" : "lifecycle"].push_back(std::move(value));
  }
  const Json &frame(HeadlessInputSession &session, const std::string &label,
                    const Json &events = Json::array(), bool recovery = false) {
    auto rendered = session.frame(events);
    std::ostringstream name;
    name << "frame-" << std::setw(4) << std::setfill('0') << ++frameOrdinal_ << ".json";
    Json observed{{"ordinal", frameOrdinal_}, {"label", label}, {"recovery", recovery},
        {"member_id", name.str()}, {"requested_events", events}, {"rendered", rendered},
        {"native_state", sourceSummary()}, {"usage", usageJson(scheduler_->usage())}};
    writeMember(name.str(), observed);
    report_["frames"].push_back(std::move(observed));
    last_ = std::move(rendered);
    const auto &identity = last_.at("session");
    require(identity.at("open") == true && identity.at("context_id") == identity.at("current_context_id") &&
                identity.at("context_id").get<std::uint64_t>() > 0,
            "Actual session frame has no owned current ImGui context");
    return last_;
  }
  void warm(HeadlessInputSession &session, bool recovery = false) {
    lifecycle(session, "opened", recovery);
    frame(session, "warmup.1", Json::array(), recovery);
    frame(session, "warmup.2", Json::array(), recovery);
  }
  Json control(const std::string &selector) const {
    if (selector.rfind("gaussian.",0) == 0) {
      const auto name = selector.substr(9);
      const auto &panel = last_.at("gaussian_panel");
      auto value = panel.at("control_states").at(name);
      value["rect"] = panel.at("control_rects").at(name);
      value["point"] = panel.at("controls").at(name);
      return value;
    }
    return last_.at("host_controls").at(selector);
  }
  void click(HeadlessInputSession &session, const std::string &selector, bool recovery = false) {
    const auto actual = control(selector);
    require(actual.at("enabled") == true && actual.at("id").get<std::uint64_t>() > 0,
            "Physical session selector is not enabled/current");
    const auto &rect = actual.at("rect");
    require(rect.is_array() && rect.size() == 4, "Session control has no actual bounds");
    const double x = std::floor((rect[0].get<double>()+rect[2].get<double>())*.5);
    const double y = std::floor((rect[1].get<double>()+rect[3].get<double>())*.5);
    require(x > rect[0].get<double>() && x < rect[2].get<double>() &&
                y > rect[1].get<double>() && y < rect[3].get<double>(),
            "Integral pointer is not inside actual control bounds");
    report_["control_observations"].push_back({{"selector", selector}, {"control", actual},
        {"position", {x,y}}, {"before_frame_ordinal", frameOrdinal_}});
    frame(session, selector+".position", Json::array({
        Json{{"type", "position"}, {"point", {x,y}}, {"space", "screen"}}}), recovery);
    frame(session, selector+".down", Json::array({Json{{"type","button"},
        {"name","left"}, {"down",true}}}), recovery);
    frame(session, selector+".up", Json::array({Json{{"type","button"},
        {"name","left"}, {"down",false}}}), recovery);
  }
  void pulse(HeadlessInputSession &session, const char *name, bool recovery = false) {
    frame(session, std::string("key.")+name+".down", Json::array({key(name,true)}), recovery);
    frame(session, std::string("key.")+name+".up", Json::array({key(name,false)}), recovery);
  }
  void openGaussian(HeadlessInputSession &session, bool recovery = false) {
    click(session, "menu.Process", recovery);
    frame(session, "process_popup_retained", Json::array(), recovery);
    require(!last_.at("input_frame").at("popup_stack").empty(),
            "Physical Process menu did not retain its actual popup");
    click(session, "menu.Process.Filters", recovery);
    frame(session, "filters_popup_retained", Json::array(), recovery);
    require(!last_.at("input_frame").at("popup_stack").empty(),
            "Physical Filters menu did not retain its actual popup");
    click(session, "menu.command.gaussian_blur_imagej", recovery);
    frame(session, "gaussian_initial_focus.1", Json::array(), recovery);
    frame(session, "gaussian_initial_focus.2", Json::array(), recovery);
    const auto &panel = last_.at("gaussian_panel");
    require(panel.at("open") == true && panel.at("sigma") == 2. &&
                panel.at("preview") == false && panel.at("stack_prompt") == false,
            "Physical Gaussian menu did not open the shared default panel");
  }
  void initialTextFocus() const {
    const auto &sigma = last_.at("gaussian_panel").at("control_states").at("sigma");
    const auto &input = last_.at("input_frame");
    const auto &text = input.at("input_text");
    require(sigma.at("active") == true && input.at("focus").at("active_id") == sigma.at("id") &&
                text.at("id") == sigma.at("id") && text.at("length").get<int>() > 0 &&
                std::abs(text.at("selection_end").get<int>()-text.at("selection_start").get<int>()) ==
                    text.at("length").get<int>(),
            "Gaussian actual sigma input did not own initial selected text");
  }
  void typeSigma(HeadlessInputSession &session, bool recovery = false) {
    frame(session, "sigma.ctrl_a.down", Json::array({Json{{"type","keys"}, {"keys",Json::array({
        Json{{"name","Ctrl"},{"down",true}}, Json{{"name","A"},{"down",true}}})}}}), recovery);
    frame(session, "sigma.ctrl_a.up", Json::array({Json{{"type","keys"}, {"keys",Json::array({
        Json{{"name","Ctrl"},{"down",false}}, Json{{"name","A"},{"down",false}}})}}}), recovery);
    frame(session, "sigma.text", Json::array({Json{{"type","text"},{"value","0.5"}}}), recovery);
    frame(session, "sigma.text_retained", Json::array(), recovery);
    require(last_.at("gaussian_panel").at("sigma_text") == "0.5" &&
                last_.at("gaussian_panel").at("sigma") == .5 &&
                last_.at("gaussian_panel").at("draft_valid") == true,
            "Separate actual key/text frames did not preserve sigma input focus");
  }
  void focusCancel(HeadlessInputSession &session) {
    for (int count = 0; count < 16; ++count) {
      const auto &cancel = last_.at("gaussian_panel").at("control_states").at("cancel");
      if (cancel.at("focused") == true &&
          last_.at("input_frame").at("focus").at("nav_id") == cancel.at("id")) {
        frame(session, "cancel_focused");
        return;
      }
      pulse(session, "Tab");
    }
    throw std::runtime_error("Actual Tab navigation did not reach Cancel within16 pulses");
  }
  template<class Predicate> void await(HeadlessInputSession &session,
                                      const char *label, Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now()+5s;
    for (int count = 0; count < 900; ++count) {
      frame(session,label);
      if (predicate(last_)) return;
      if (std::chrono::steady_clock::now() >= deadline) break;
      std::this_thread::sleep_for(1ms);
    }
    throw std::runtime_error(std::string("Real session job watchdog expired: ")+label);
  }
  void readyPreview(HeadlessInputSession &session) {
    click(session, "gaussian.preview");
    await(session, "preview_ready", [](const Json &result) {
      const auto &jobs = result.at("gaussian_jobs");
      return !jobs.at("active_installed_generation").is_null() &&
          jobs.at("preview").at("owns_job") == true &&
          jobs.at("resource_usage").at("running_reserved_bytes") == 0 &&
          jobs.at("resource_usage").at("retained_reserved_bytes").get<std::uint64_t>() > 0 &&
          result.at("ordinary_pixels").at("pixels_source") == "preview";
    });
    checkpoint("ready_preview");
  }
  void closeSession(HeadlessInputSession &session, bool recovery = false) {
    lifecycle(session,"before_close",recovery);
    session.close();
    lifecycle(session,"closed",recovery);
    require(session.lifecycle().at("open") == false && ImGui::GetCurrentContext() == nullptr,
            "Session close retained its context");
  }
  void cancelDialog(HeadlessInputSession &session) {
    pulse(session,"Escape");
    require(last_.at("gaussian_panel").at("open") == false &&
                last_.at("gaussian_panel").at("exit_reason") == "cancel",
            "Actual Escape did not cancel the focused Gaussian panel");
  }
  void cleanupJobs() {
    report_["api_inputs"].push_back({{"method","cancel_and_wait_owned_jobs"},
        {"timeout_ms",5000}, {"purpose","driver cleanup; renderer owns no native jobs"}});
    window_.cancel();
    window_.action("wait_jobs",{{"timeout_ms",5000}});
    window_.poll();
  }
  void menuTextCase() {
    HeadlessInputSession session(window_);
    warm(session); openGaussian(session); initialTextFocus();
    frame(session,"initial_text_selected");
    typeSigma(session); cancelDialog(session); closeSession(session);
  }
  void tabEnterCase() {
    HeadlessInputSession session(window_);
    warm(session); openGaussian(session); focusCancel(session);
    frame(session,"shift_tab.down",Json::array({Json{{"type","keys"},{"keys",Json::array({
        Json{{"name","Shift"},{"down",true}}, Json{{"name","Tab"},{"down",true}}})}}}));
    frame(session,"shift_tab.up",Json::array({Json{{"type","keys"},{"keys",Json::array({
        Json{{"name","Shift"},{"down",false}}, Json{{"name","Tab"},{"down",false}}})}}}));
    require(last_.at("input_frame").at("focus").at("nav_id") !=
                last_.at("gaussian_panel").at("control_states").at("cancel").at("id"),
            "Shift+Tab did not change actual Cancel navigation focus");
    focusCancel(session); pulse(session,"Enter");
    require(last_.at("gaussian_panel").at("open") == false &&
                last_.at("gaussian_panel").at("exit_reason") == "cancel",
            "Focused Cancel Enter incorrectly accepted Gaussian");
    closeSession(session);
  }
  void previewEscapeCase() {
    HeadlessInputSession session(window_);
    warm(session); openGaussian(session); readyPreview(session);
    cancelDialog(session);
    await(session,"preview_canceled",[](const Json &result) {
      return result.at("gaussian_jobs").at("active_installed_generation").is_null() &&
          result.at("gaussian_jobs").at("resource_usage").at("reserved_bytes") == 0;
    });
    closeSession(session);
  }
  void stackCase() {
    HeadlessInputSession session(window_);
    warm(session); openGaussian(session); click(session,"gaussian.ok");
    frame(session,"stack_prompt_retained.1"); frame(session,"stack_prompt_retained.2");
    require(last_.at("gaussian_panel").at("stack_prompt") == true && !window_.busy() &&
                owner_->undo_steps() == 0 && scheduler_->usage().reserved_bytes == 0,
            "Stack prompt submitted before an actual choice");
    const auto yes = last_.at("gaussian_panel").at("control_states").at("stack_yes");
    require(yes.at("focused") == true && last_.at("input_frame").at("focus").at("nav_id") == yes.at("id"),
            "Actual stack prompt did not default focus to Yes");
    frame(session,"stack_default_yes_focus");
    bool focused = false;
    for (int count=0; count<16; ++count) {
      const auto &no = last_.at("gaussian_panel").at("control_states").at("stack_no");
      if (no.at("focused") == true && last_.at("input_frame").at("focus").at("nav_id") == no.at("id")) {
        focused = true; break;
      }
      pulse(session,"Tab");
    }
    require(focused,"Actual Tab did not reach stack No");
    frame(session,"stack_no_focused"); pulse(session,"Enter");
    await(session,"stack_apply_completed",[](const Json &result) {
      return result.at("gaussian_jobs").at("apply").at("exit") == "published";
    });
    auto expected = input_;
    expected[1] = gaussian_blur_imagej(input_[1],2,2);
    require(rawPixels(owner_->snapshot().data().dataset) == rawPixels(Dataset::from_stack(expected)),
            "Session stack No pixels differ from owned public primitive integration expectation");
    report_["expected_changed_raw"] = rawMember("expected-changed.u8",Dataset::from_stack(expected));
    report_["expected_output_policy"] = "Owned public primitive integration check; no upstream numerical oracle";
    closeSession(session);
  }
  void recordRejection(HeadlessInputSession &session, const char *label,
                        const Json &events) {
    const auto before = session.lifecycle();
    Json failure;
    try { session.frame(events); }
    catch (const std::exception &error) { failure = exceptionJson(error); }
    const auto after = session.lifecycle();
    Json observed{{"label",label},{"requested_events",events},{"before",before},{"after",after},
        {"exception",failure}, {"native_state",sourceSummary()}, {"usage",usageJson(scheduler_->usage())}};
    if (std::string(label)=="nonfinite_pointer") observed["nonfinite_input"]="positive_infinity";
    writeMember(std::string("rejection-")+label+".json",observed);
    report_["exceptions"].push_back(observed);
    require(failure.is_object() && failure.at("type") == "invalid_argument" && before == after,
            "Session input preflight did not reject atomically with the session intact");
  }
  void defaultsAndPreflightCase() {
    report_["legacy_default_before"] = window_.renderHeadless();
    require(report_.at("legacy_default_before").at("input_frames").size()==3 &&
                report_.at("legacy_default_before").at("input_trace").empty(),
            "Public renderHeadless defaults changed");
    {
      HeadlessInputSession session(window_);
      warm(session); openGaussian(session); initialTextFocus();
      recordRejection(session,"unknown_field",Json::array({Json{{"type","key"},
          {"name","Tab"},{"down",true},{"unexpected",1}}}));
      recordRejection(session,"unknown_key",Json::array({key("NotAKey",true)}));
      recordRejection(session,"nonfinite_pointer",Json::array({Json{{"type","position"},
          {"space","screen"},{"point",{std::numeric_limits<double>::infinity(),100.}}}}));
      frame(session,"valid_after_preflight_rejections"); typeSigma(session);
      cancelDialog(session); closeSession(session);
    }
    report_["legacy_default_after"] = window_.renderHeadless();
    require(report_.at("legacy_default_after").at("input_frames").size()==3 &&
                report_.at("legacy_default_after").at("input_trace").empty(),
            "Session closure poisoned public renderHeadless defaults");
    legacyReadbackCompatibility();
  }
  void legacyReadbackCompatibility() {
    // This API compatibility supplement owns another Window, so its explicit
    // display navigation cannot change the primary physical session baseline.
    Window compatible((directory_/"legacy-preferences.json").u8string());
    auto source = std::make_shared<imagejxx::Document>(Dataset::from_stack(input_), "Legacy readback source");
    compatible.addNativeDocument(source);
    auto display = source->snapshot().display();
    display.crosshair = {7,5,0};
    display.properties["display_range"] = metadata_array({0.,255.});
    compatible.setNativeDisplay(display);
    source->mark_saved();
    const auto before = source->snapshot();
    Json supplement{{"classification","API compatibility; no physical full61 credit"},
        {"source_before_raw",rawMember("legacy-source-before.u8",before.data().dataset)},
        {"document_id",source->id},
        {"data_identity_before",before.data_identity() ? Json(*before.data_identity()) : Json()},
        {"pixels_revision_before",before.revisions().pixels}, {"api_inputs",Json::array()}};
    const Json event{{"type","action"},{"name","show_operation"},
        {"parameters",{{"operation","gaussian_blur_imagej"}}}};
    supplement["api_inputs"].push_back({{"method","renderHeadless"},{"input_events",Json::array({event})}});
    supplement["opened_during_call"] = compatible.renderHeadless(3,false,false,Json::array({event}));
    writeMember("legacy-opened-during-call.json",supplement.at("opened_during_call"));
    require(supplement.at("opened_during_call").at("gaussian_panel").at("open")==true,
            "Legacy input action did not open the Gaussian command during its call");
    supplement["subsequent_ordinary"] = compatible.renderHeadless();
    writeMember("legacy-subsequent-ordinary.json",supplement.at("subsequent_ordinary"));
    auto checkReadback = [](const Json &actual, const std::vector<double> &pixels, int w, int h) {
      require(actual.at("available")==true &&
                  actual.at("source").at("shape")==Json::array({h,w,1}) &&
                  actual.at("source").at("dtype")=="uint8" &&
                  actual.at("source").at("pixels")==Json(pixels),
              "Legacy wrapper lost actual source readback after opening Gaussian");
      std::vector<double> rgb;
      rgb.reserve(pixels.size()*3);
      for (double value:pixels) for (int sample=0;sample<3;++sample) rgb.push_back(value);
      require(actual.at("rendered_rgb").at("shape")==Json::array({h,w,3}) &&
                  actual.at("rendered_rgb").at("dtype")=="uint8" &&
                  actual.at("rendered_rgb").at("pixels")==Json(rgb),
              "Legacy wrapper lost the actual fixed-range RGB readback");
    };
    for (const char *name:{"opened_during_call","subsequent_ordinary"}) {
      const auto &ordinary = supplement.at(name).at("ordinary_pixels");
      require(ordinary.at("pixels_source")=="live", "Legacy compatibility unexpectedly enabled Preview");
      checkReadback(ordinary.at("readback"),input_[0].pixels,width,height);
    }
    supplement["api_inputs"].push_back({{"method","action"},{"name","view"},
        {"parameters",{{"orthogonal",true}}}});
    compatible.action("view",{{"orthogonal",true}});
    supplement["subsequent_xyz"] = compatible.renderHeadless();
    writeMember("legacy-subsequent-xyz.json",supplement.at("subsequent_xyz"));
    const auto &planes = supplement.at("subsequent_xyz").at("orthogonal_planes");
    require(planes.is_array() && planes.size()==3,"Legacy compatibility lost linked XYZ canvases");
    for (int plane=0;plane<3;++plane) {
      std::vector<double> expected;
      if (plane==0) expected=input_[0].pixels;
      else if (plane==1) for (int x=0;x<width;++x) expected.push_back(input_[0].at(x,5));
      else for (int y=0;y<height;++y) expected.push_back(input_[0].at(7,y));
      require(planes[plane].at("visible")==true && planes[plane].at("pixels_source")=="live_document",
              "Legacy XYZ compatibility selected a transient source");
      checkReadback(planes[plane].at("readback"),expected,plane==2?height:width,plane==0?height:1);
    }
    const auto after = source->snapshot();
    supplement["source_after_raw"]=rawMember("legacy-source-after.u8",after.data().dataset);
    supplement["data_identity_after"]=after.data_identity() ? Json(*after.data_identity()) : Json();
    supplement["pixels_revision_after"]=after.revisions().pixels;
    supplement["undo_steps"]=source->undo_steps();
    supplement["redo_steps"]=source->redo_steps();
    supplement["dirty"]=after.dirty();
    supplement["final_context"]=currentContext();
    writeMember("legacy-readback-compatibility.json",supplement);
    report_["legacy_readback_compatibility"]=supplement;
    require(rawPixels(after.data().dataset)==rawPixels(before.data().dataset) &&
                after.data_identity()==before.data_identity() && after.revisions().pixels==before.revisions().pixels &&
                source->undo_steps()==0 && source->redo_steps()==0 && !after.dirty() &&
                !compatible.busy() && compatible.workflow.empty() && compatible.results.empty() &&
                ImGui::GetCurrentContext()==nullptr,
            "Legacy readback compatibility changed source/history or retained a renderer");
  }
  void ownershipCase() {
    Window other((directory_/"other-preferences.json").u8string());
    {
      HeadlessInputSession session(window_); warm(session);
      const auto before = session.lifecycle();
      Json threadResults = Json::array();
      std::string actorId;
      std::exception_ptr actorFailure;
      std::thread actor([&] {
        try {
          actorId=threadId();
          for (const char *operation : {"frame","close"}) {
            Json exception;
            try { if (std::string(operation)=="frame") session.frame(); else session.close(); }
            catch (const std::exception &error) { exception=exceptionJson(error); }
            threadResults.push_back({{"operation",operation},{"exception",exception}});
          }
        } catch (...) { actorFailure=std::current_exception(); }
      });
      struct JoinActor { std::thread &actor; ~JoinActor() { if(actor.joinable()) actor.join(); } } join{actor};
      actor.join();
      if (actorFailure) std::rethrow_exception(actorFailure);
      const auto after = session.lifecycle();
      report_["thread_rejections"]={{"actor_language","cpp"},{"thread_id",actorId},
          {"results",threadResults},{"before",before},{"after",after}};
      writeMember("thread-rejections.json",report_.at("thread_rejections"));
      require(actorId!=threadId() && before==after && threadResults.size()==2,
              "Off-thread session operations altered creator context");
      for (const auto &observed:threadResults)
        require(observed.at("exception").is_object() && observed.at("exception").at("type")=="runtime_error",
                "Off-thread session frame/close was not rejected before access");
      for (const char *operation : {"same_window_begin","other_window_begin","legacy_render"}) {
        const auto prior=session.lifecycle();
        Json exception;
        try {
          if (std::string(operation)=="legacy_render") window_.renderHeadless();
          else { HeadlessInputSession nested(std::string(operation)=="same_window_begin"?window_:other); }
        } catch (const std::exception &error) { exception=exceptionJson(error); }
        Json observed{{"label",operation},{"exception",exception},{"before",prior},
            {"after",session.lifecycle()}};
        writeMember(std::string("rejection-")+operation+".json",observed);
        report_["exceptions"].push_back(observed);
        require(exception.is_object() && exception.at("type")=="runtime_error" && prior==session.lifecycle(),
                "Nested renderer admission changed the live creator session");
      }
      frame(session,"valid_after_thread_and_nested_rejections"); closeSession(session);
    }
    {
      HeadlessInputSession session(other);
      report_["other_window_recovery"]={{"opened",session.lifecycle()},{"frame",session.frame()}};
      session.close();
      report_["other_window_recovery"]["closed"]=session.lifecycle();
      require(session.lifecycle().at("open")==false,"Second Window recovery did not close");
    }
  }
  void probe(const char *phase) {
    Json observed{{"phase",phase},{"actual_context",currentContext()},{"thread_id",threadId()}};
    std::ostringstream member;
    member<<"probe-"<<std::setw(4)<<std::setfill('0')<<++probeOrdinal_<<".json";
    writeMember(member.str(),observed);
    report_["probes"].push_back(std::move(observed));
  }
  void recover() {
    HeadlessInputSession session(window_);
    warm(session,true);
    if (last_.at("gaussian_panel").at("open")==true) {
      click(session,"gaussian.cancel",true);
      frame(session,"recovery_prior_dialog_closed",Json::array(),true);
    }
    openGaussian(session,true); initialTextFocus(); typeSigma(session,true);
    pulse(session,"Escape",true);
    require(last_.at("gaussian_panel").at("open")==false &&
                last_.at("gaussian_panel").at("exit_reason")=="cancel",
            "Fresh renderer recovery could not receive actual Gaussian input");
    closeSession(session,true);
  }
  void initializationFailureCase() {
    report_["initialization_failure"]={{"before",currentContext()}};
    Json failure;
    try {
      HeadlessInputSession session(window_,[this](const char *phase) {
        probe(phase);
        if (std::string(phase)=="after_context_creation")
          throw std::runtime_error("S07 renderer initialization probe");
      });
    } catch (const std::exception &error) { failure=exceptionJson(error); }
    report_["initialization_failure"]["exception"]=failure;
    report_["initialization_failure"]["after"]=currentContext();
    writeMember("initialization-failure.json",report_.at("initialization_failure"));
    require(failure.is_object() && failure.at("message")=="S07 renderer initialization probe" &&
                ImGui::GetCurrentContext()==nullptr,
            "Partial session initialization did not clean its actual context");
    recover();
  }
  void frameFailureCase() {
    bool armed=false;
    {
      HeadlessInputSession session(window_,[this,&armed](const char *phase) {
        probe(phase);
        if (armed && std::string(phase)=="after_new_frame")
          throw std::runtime_error("S08 renderer frame probe");
      });
      warm(session); openGaussian(session); readyPreview(session);
      report_["frame_failure"]={{"before",session.lifecycle()},
          {"usage_before",usageJson(scheduler_->usage())}};
      armed=true;
      Json failure;
      try { session.frame(); }
      catch (const std::exception &error) { failure=exceptionJson(error); }
      armed=false;
      report_["frame_failure"]["exception"]=failure;
      report_["frame_failure"]["after"]=session.lifecycle();
      report_["frame_failure"]["usage_after_renderer_close"]=usageJson(scheduler_->usage());
      report_["frame_failure"]["actual_context_after"]=currentContext();
      writeMember("frame-failure.json",report_.at("frame_failure"));
      require(failure.is_object() && failure.at("message")=="S08 renderer frame probe" &&
                  session.lifecycle().at("open")==false && ImGui::GetCurrentContext()==nullptr &&
                  scheduler_->usage().retained_bytes>0,
              "Frame exception did not close renderer independently of actual retained jobs");
      checkpoint("renderer_closed_before_driver_job_cleanup");
      cleanupJobs();
      checkpoint("driver_jobs_canceled_after_renderer_failure");
      require(scheduler_->usage().reserved_bytes==0,
              "Explicit driver cleanup did not release actual preview storage");
      lifecycle(session,"closed_after_failure");
    }
    recover();
  }
  void verifyFinal() const {
    require(report_.at("final_context").at("current_context_id")==0 &&
                scheduler_->usage().running==0 && scheduler_->usage().queued==0 &&
                scheduler_->usage().reserved_bytes==0 && scheduler_->usage().retained_bytes==0 &&
                scheduler_->usage().running_bytes==0,
            "Session case leaked context or native reservations");
    require(rawPixels(original_->data().dataset)==rawPixels(Dataset::from_stack(input_)),
            "Session case mutated retained original input buffers");
    const auto &before=report_.at("before"), &after=report_.at("after");
    require(window_.workflow.empty() && window_.results.empty() && owner_->redo_steps()==0,
            "Session case unexpectedly published records/results or redo history");
    if (number_==4) {
      require(owner_->undo_steps()==1 && after.at("dirty")==true &&
                  after.at("revisions").at("pixels").get<std::uint64_t>()==
                      before.at("revisions").at("pixels").get<std::uint64_t>()+1,
              "Actual stack choice did not publish exactly one pixel history transaction");
    } else {
      require(rawPixels(owner_->snapshot().data().dataset)==rawPixels(Dataset::from_stack(input_)),
              "Session-only interaction changed native source pixels");
      for (const char *field:{"document_id","data_identity","dirty","undo_steps","redo_steps","revisions",
                              "document_info","workflow","results"})
        require(after.at(field)==before.at(field),"Session-only interaction changed native source/history state");
    }
    require(report_.at("window_error")=="","Session case produced an unexpected editor error");
  }
};
} // namespace

Json Window::headlessSessionCase(const std::string &caseId,const std::string &context,
                                  const std::string &directory) {
  checkThread();
  const int number=caseNumber(caseId);
  if (context!="imgui_cpp" && context!="imgui_python")
    throw std::invalid_argument("Session context must be imgui_cpp or imgui_python");
  require(documents_.empty() && workflow.empty() && results.empty() && ImGui::GetCurrentContext()==nullptr,
          "Session case requires a fresh caller Window and no live ImGui context");
  SessionDriver driver(*this,controller_->job_scheduler(),number,context,freshDirectory(directory));
  return driver.run();
}
int Window::headlessSessionConsumerTest(const std::string &directory) {
  checkThread();
  require(documents_.empty() && ImGui::GetCurrentContext()==nullptr,
          "Session standalone consumer requires a fresh Window");
  const auto root=freshDirectory(directory);
  Json report{{"schema_version",1},{"contract_id","p02a.headless-session.001"},
      {"context","imgui_cpp"},{"status","unverified"},{"acceptance_claim",false},{"cases",Json::array()},
      {"pending",Json::array({"Full61 variants in both installed launch modes","R16 actual retained Preferences lowering"})}};
  try {
    for (int number=1;number<=8;++number) {
      const auto caseId="S0"+std::to_string(number);
      Window window((root/(caseId+"-preferences.json")).u8string());
      report["cases"].push_back(window.headlessSessionCase(caseId,"imgui_cpp",(root/caseId).u8string()));
      std::cout<<"PASS "<<caseId<<" imgui_cpp\n";
    }
    report["status"]="passed";
    writeJson((root/"imgui-cpp.json").u8string(),report);
    return 0;
  } catch (const std::exception &error) {
    report["status"]="failed"; report["failure"]=exceptionJson(error);
    writeJson((root/"session-failed.json").u8string(),report);
    throw;
  }
}
} // namespace imagejxx::desktop
