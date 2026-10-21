#include "manual_threshold_ui_probe.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <imagejxx/manual_threshold.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <stdexcept>

// Fixed input/observation producer. Expected membership, histogram, display,
// output type and publication behavior live only in the independent reader.
namespace imagejxx::desktop {
namespace {
using Clock = std::chrono::steady_clock;
constexpr const char *contract = "p05k.manual-threshold-ui.001";
constexpr std::size_t maxFrames = 180, maxBytes = 16u * 1024u * 1024u;
constexpr double maxSeconds = 15.;
constexpr const char *roiRows[] = {
    "000000000", "011111000", "010001000", "010101000", "010001000",
    "011111000", "000000010", "000000000", "000000000"};

void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
Json revisionsJson(const Revisions &r) {
  return {{"pixels",r.pixels},{"metadata",r.metadata},
          {"selection",r.selection},{"display",r.display}};
}
Json displayJson(const DisplayState &d) {
  return {{"t",d.t},{"c",d.c},{"crosshair",d.crosshair},
          {"orthogonal",d.orthogonal},{"zoom",d.zoom},
          {"properties",metadataJson(d.properties)},{"revision",d.revision}};
}
Json selectionJson(const std::optional<Selection> &selection) {
  if (!selection) return nullptr;
  auto value = imagejxx::desktop::selectionJson(*selection);
  const auto mask = selection->mask(9,9);
  value["observed_membership"] = mask.pixels;
  return value;
}
PixelType inputType(const std::string &id) {
  if (id == "T02") return PixelType::Float32;
  if (id == "T03" || id == "T05") return PixelType::Float64;
  if (id == "T04" || id == "T08" || id == "T10") return PixelType::UInt16;
  return PixelType::UInt8;
}
DocumentData inputData(const std::string &id, bool other = false) {
  if (id == "T10") {
    Stack planes;
    for (int t=0;t<2;++t) for (int c=0;c<2;++c) for (int z=0;z<3;++z) {
      Image plane(9,9,1,PixelType::UInt16);
      for (int y=0;y<9;++y) for (int x=0;x<9;++x)
        plane.at(x,y)=10000*t+1000*c+100*z+10*y+x;
      planes.push_back(std::move(plane));
    }
    DocumentData data{Dataset::from_planes({2,2,3,9,9,1},std::move(planes))};
    data.metadata.title="Scientific manual threshold";
    Image mask(9,9,1,PixelType::UInt8);
    for (int y=0;y<9;++y) for (int x=0;x<9;++x)
      mask.at(x,y)=roiRows[y][x]=='1'?255:0;
    data.selection=Selection::from_mask(mask);
    return data;
  }
  const int depth = id == "T04" && !other ? 2 : 1;
  Stack frames;
  for (int z=0;z<depth;++z) {
    Image plane(9,9,1,inputType(id));
    for (int y=0;y<9;++y) for (int x=0;x<9;++x) {
      double value = (31*x+17*y+7*x*y)%256;
      if (other) value = 177;
      else if (id == "T02") value = double(float((x-4)*1.25+(y-4)*.5));
      else if (id == "T03") value = std::max(std::abs(x-4),std::abs(y-4)) == 2 || (x==0 && y==0) ? 1 : 0;
      else if (id == "T05") value = (x-4)*.75+(y-4)*.125;
      else if (id == "T04" || id == "T08") value = 1000+10000*z+100*y+x;
      plane.at(x,y) = value;
    }
    frames.push_back(std::move(plane));
  }
  DocumentData data{Dataset::from_stack(std::move(frames))};
  data.metadata.title = other ? "Other threshold source" : "Manual threshold "+id;
  data.metadata.properties = metadataObjectFromJson({{"retained","source property"}});
  if (id == "T08" && !other) {
    Image mask(9,9,1,PixelType::UInt8);
    for (int y=0;y<9;++y) for (int x=0;x<9;++x)
      mask.at(x,y) = roiRows[y][x] == '1' ? 255 : 0;
    data.selection = Selection::from_mask(mask);
  }
  return data;
}

class Driver {
public:
  Driver(Window &window, std::string id, const std::string &context,
         std::filesystem::path directory)
      : window_(window), id_(std::move(id)), directory_(std::move(directory)),
        start_(Clock::now()), source_(std::make_shared<imagejxx::Document>(inputData(id_))) {
    window_.addNativeDocument(source_);
    auto display = source_->snapshot().display();
    display.properties = metadataObjectFromJson({{"lut","Grays"}});
    if (id_ == "T04") display.crosshair[2] = 1;
    if (id_ == "T10") {
      display.t=1; display.c=1; display.crosshair[2]=2;
      window_.recording=true;
    }
    if (id_ == "T02")
      set_manual_threshold_state(display,source_->snapshot().data().dataset.shape(),{{-2,2},{0,0,0}});
    window_.setNativeDisplay(display,source_->id);
    if (id_ == "T04" || id_ == "T07") {
      other_ = std::make_shared<imagejxx::Document>(inputData(id_,true));
      window_.addNativeDocument(other_);
      window_.setActiveIndex(0);
    }
    report_ = {{"schema_version",1},{"contract_id",contract},{"case_id",id_},
               {"context",context},{"acceptance_claim",false},
               {"limits",{{"max_frames",maxFrames},{"max_seconds",maxSeconds},
                           {"max_report_bytes",maxBytes},{"max_shape_zyx",{2,9,9}}}},
               {"frames",Json::array()},{"control_observations",Json::array()},
               {"states",Json::object()},{"lifecycle",Json::array()}};
    report_["states"]["before"] = collection();
    if (id_ == "T10") {
      report_["contract_id"]="p06l.scientific-manual-threshold.ui.001";
      report_["api_fixture_actions"]={"dataset/selection setup","T/C/Z navigation"};
      report_["limits"]["max_shape_tczyxs"]={2,2,3,9,9,1};
    }
  }
  Json run() {
    HeadlessInputSession session(window_);
    report_["lifecycle"].push_back(session.lifecycle());
    try {
      frame(session,"warmup.1"); frame(session,"warmup.2");
      click(session,"menu.Process"); frame(session,"process_menu_open");
      click(session,"menu.manual_threshold"); checkpoint(session,"opened");
      if (id_ == "T02") configure(session,"-3.5","0.5");
      else if (id_ == "T03") configure(session,"1","1");
      else if (id_ == "T04") configure(session,"11101","11303");
      else if (id_ == "T05") configure(session,"-1.5","1.5");
      else if (id_ == "T08") configure(session,"1101","1505");
      else if (id_ == "T10") configure(session,"11212","11253");
      else configure(session,"60","140");
      click(session,"threshold.set"); checkpoint(session,"overlay_set");

      if (id_ == "T10") {
        auto view=source_->snapshot().display();
        view.t=0;view.c=0;view.crosshair[2]=0;
        window_.setNativeDisplay(view,source_->id);
        checkpoint(session,"navigated");
        click(session,"threshold.apply");window_.wait();checkpoint(session,"applied");
        click(session,"menu.Edit");frame(session,"edit_menu_open");
        click(session,"menu.Edit.Undo");checkpoint(session,"undone");
        click(session,"workflow.manual_threshold.0.rebind");
        window_.wait();checkpoint(session,"rebound");
        click(session,"menu.Edit");frame(session,"rebound_edit_menu_open");
        click(session,"menu.Edit.Undo");checkpoint(session,"rebound_undone");
      } else if (id_ == "T01") {
        click(session,"threshold.reset"); checkpoint(session,"reset");
        click(session,"threshold.cancel");
      } else if (id_ == "T02") {
        click(session,"threshold.cancel");
      } else if (id_ == "T03") {
        click(session,"threshold.to_selection"); checkpoint(session,"selected");
        click(session,"threshold.to_selection"); checkpoint(session,"selected_again");
        click(session,"threshold.cancel");
      } else if (id_ == "T04" || id_ == "T05" || id_ == "T08") {
        if (other_) {
          click(session,"document."+std::to_string(other_->id));
          checkpoint(session,"other_active");
        }
        checkpoint(session,"before_apply"); click(session,"threshold.apply");
        window_.wait(); checkpoint(session,"applied");
        if (other_) click(session,"document."+std::to_string(source_->id));
        // The shortcut dispatcher deliberately suppresses document chords
        // while a document tab/control owns focus. Exercise the visible Undo
        // command; keyboard focus/rearming is qualified by its own contract.
        click(session,"menu.Edit");frame(session,"legacy_edit_menu_open");
        click(session,"menu.Edit.Undo");
        checkpoint(session,"undone");
      } else if (id_ == "T06") {
        edit(session,"lower","nan","invalid_nonfinite");
        click(session,"threshold.apply",true); checkpoint(session,"invalid_guard");
        edit(session,"lower","200","invalid_order");
        click(session,"threshold.apply",true); checkpoint(session,"order_guard");
        edit(session,"lower","60","valid_again");
        auto plane = *source_->snapshot().data().dataset.plane();
        plane.at(0,0) = 7;
        auto transaction = window_.beginEdit(source_->id);
        transaction.replace_plane({0,0,0},std::move(plane));
        window_.commitEdit(transaction); checkpoint(session,"stale");
        click(session,"threshold.apply",true); checkpoint(session,"stale_guard");
        click(session,"threshold.cancel");
      } else if (id_ == "T07") {
        window_.closeDocument(source_->id,true); checkpoint(session,"closed_source");
        click(session,"threshold.apply",true); checkpoint(session,"closed_guard");
        click(session,"threshold.cancel");
      } else {
        busy(session);
      }
      checkpoint(session,"final"); session.close();
      report_["lifecycle"].push_back(session.lifecycle());
      report_["window_error"] = window_.error();
      report_["frame_count"] = report_["frames"].size();
      report_["elapsed_seconds"] = seconds();
      report_["observation_state"] = "observed";
      write("case.json");
      return report_;
    } catch (const std::exception &error) {
      try {window_.cancel();window_.wait();} catch (...) {}
      try {session.close();} catch (...) {}
      report_["observation_state"] = "failed";
      report_["failure"] = error.what();
      try {write("case-failed.json");} catch (...) {}
      throw;
    }
  }
private:
  Window &window_;
  std::string id_;
  std::filesystem::path directory_;
  Clock::time_point start_;
  std::shared_ptr<imagejxx::Document> source_,other_;
  Json report_,last_;
  double seconds() const {return std::chrono::duration<double>(Clock::now()-start_).count();}
  void budget() const {
    require(report_["frames"].size() <= maxFrames && seconds() <= maxSeconds,
            "Manual threshold UI probe frame/time budget exceeded");
  }
  bool available(std::uint64_t id) const {
    try {return bool(window_.nativeDocument(id));}
    catch (const DocumentUnavailable &) {return false;}
    catch (const std::out_of_range &) {return false;}
  }
  Json state(const std::shared_ptr<imagejxx::Document> &owner) const {
    const auto snapshot = owner->snapshot(); const auto &data = snapshot.data();
    const auto shape = data.dataset.shape(); Json pixels = Json::array();
    for (int t=0;t<shape.t;++t) for (int c=0;c<shape.c;++c)
      for (int z=0;z<shape.z;++z) pixels.push_back(data.dataset.plane({t,c,z})->pixels);
    return {{"document_id",owner->id},{"available",available(owner->id)},
            {"data_identity",snapshot.data_identity() ? Json(*snapshot.data_identity()) : Json(nullptr)},
            {"dirty",snapshot.dirty()},{"undo_steps",owner->undo_steps()},{"redo_steps",owner->redo_steps()},
            {"revisions",revisionsJson(snapshot.revisions())},
            {"dtype",imagejxx::desktop::typeName(data.dataset.pixel_type())},
            {"shape",shape.dimensions()},{"pixels",std::move(pixels)},
            {"selection",selectionJson(data.selection)},
            {"previous_selection",selectionJson(data.previous_selection)},
            {"display",displayJson(snapshot.display())},
            {"metadata",{{"title",data.metadata.title},{"properties",metadataJson(data.metadata.properties)},
                         {"extensions",metadataJson(data.metadata.extensions)}}}};
  }
  Json collection() const {
    auto &application = GaussianCancellationAccess::application(window_);
    return {{"source",state(source_)},{"other",other_ ? state(other_) : Json(nullptr)},
            {"active_document_id",application.active_document_id()},
            {"document_ids",application.document_ids()},{"busy",window_.busy()},
            {"workflow",window_.workflow},{"results",window_.results}};
  }
  void frame(HeadlessInputSession &session,const std::string &label,
             const Json &events = Json::array()) {
    require(report_["frames"].size() < maxFrames,"Manual threshold UI frame cap");
    budget(); last_ = session.frame(events);
    report_["frames"].push_back({{"ordinal",report_["frames"].size()+1},
        {"label",label},{"requested_events",events},{"rendered",last_},{"native_state",collection()}});
    budget();
  }
  void checkpoint(HeadlessInputSession &session,const std::string &label) {
    frame(session,label); report_["states"][label] = collection();
  }
  Json control(const std::string &selector) const {
    if (selector.rfind("threshold.",0) == 0) {
      const auto name = selector.substr(10); const auto &panel = last_.at("threshold_panel");
      Json value = panel.at("control_states").at(name);
      value["rect"] = panel.at("control_rects").at(name);
      value["point"] = panel.at("controls").at(name); return value;
    }
    if (selector.rfind("document.",0) == 0) {
      const auto id = std::stoull(selector.substr(9));
      for (const auto &tab : last_.at("document_tabs")) if (tab.at("document_id") == id) return tab;
      throw std::runtime_error("Manual threshold document tab unavailable");
    }
    return last_.at("host_controls").at(selector);
  }
  void click(HeadlessInputSession &session,const std::string &selector,bool disabled = false) {
    const auto actual = control(selector);
    require(actual.at("enabled").get<bool>() == !disabled &&
            actual.at("id").get<std::uint64_t>() > 0,
            "Manual threshold pointer target eligibility mismatch");
    const auto &rect = actual.at("rect"); const auto &center = actual.at("point");
    const double x=std::floor(center[0].get<double>()),y=std::floor(center[1].get<double>());
    require(x>=rect[0].get<double>() && x<rect[2].get<double>() &&
            y>=rect[1].get<double>() && y<rect[3].get<double>(),
            "Manual threshold pointer escaped observed widget");
    report_["control_observations"].push_back({{"selector",selector},{"control",actual},
        {"position",{x,y}},{"before_frame_ordinal",report_["frames"].size()},
        {"disabled_attempt",disabled}});
    frame(session,selector+".position",Json::array({{{"type","position"},{"space","screen"},{"point",{x,y}}}}));
    frame(session,selector+".down",Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(session,selector+".up",Json::array({{{"type","button"},{"name","left"},{"down",false}}}));
  }
  void edit(HeadlessInputSession &session,const char *field,const char *text,const std::string &label) {
    click(session,std::string("threshold.")+field);
    for (bool down : {true,false})
      frame(session,label+(down?".ctrl_a.down":".ctrl_a.up"),Json::array({
          {{"type","keys"},{"keys",Json::array({{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}})}}}));
    frame(session,label+".text",Json::array({{{"type","text"},{"value",text}}}));
    checkpoint(session,label);
  }
  void configure(HeadlessInputSession &session,const char *lower,const char *upper) {
    edit(session,"lower",lower,"lower_edited"); edit(session,"upper",upper,"upper_edited");
    checkpoint(session,"configured");
  }
  void chord(HeadlessInputSession &session,const char *label,bool down) {
    frame(session,std::string(label)+(down?".down":".up"),Json::array({
      {{"type","keys"},{"keys",Json::array({{{"name","Ctrl"},{"down",down}},{{"name","Z"},{"down",down}}})}}}));
  }
  void busy(HeadlessInputSession &session) {
    struct Gate {std::mutex mutex;std::condition_variable condition;bool entered=false,released=false;};
    auto gate=std::make_shared<Gate>();
    auto job=GaussianCancellationAccess::startBusyPreparation(window_,[gate](const JobContext &context) {
      std::unique_lock<std::mutex> lock(gate->mutex);gate->entered=true;gate->condition.notify_all();
      if (!gate->condition.wait_for(lock,std::chrono::seconds(10),[&]{return gate->released;}))
        throw std::runtime_error("Manual threshold busy worker gate timed out");
      lock.unlock();context.checkpoint();
    });
    struct Release {
      Window &window;Job job;std::shared_ptr<Gate> gate;
      ~Release() {try {job.cancel();{std::lock_guard<std::mutex> lock(gate->mutex);
        gate->released=true;gate->condition.notify_all();}
        if (job.wait_for(std::chrono::seconds(2))) window.wait();} catch (...) {}}
    } release{window_,job,gate};
    {std::unique_lock<std::mutex> lock(gate->mutex);
      require(gate->condition.wait_for(lock,std::chrono::seconds(2),[&]{return gate->entered;}),
              "Manual threshold held worker did not enter");}
    checkpoint(session,"busy"); click(session,"threshold.apply",true);
    checkpoint(session,"busy_guard"); click(session,"threshold.cancel");
    checkpoint(session,"busy_canceled_panel");
    job.cancel();{std::lock_guard<std::mutex> lock(gate->mutex);gate->released=true;gate->condition.notify_all();}
    require(job.wait_for(std::chrono::seconds(2)),"Manual threshold held worker did not terminate");
    window_.wait(); checkpoint(session,"busy_released");
  }
  void write(const char *name) const {
    const auto json=report_.dump();require(json.size()<=maxBytes,"Manual threshold observation16MiB cap");
    writeBytes((directory_/name).u8string(),std::vector<unsigned char>(json.begin(),json.end()));
  }
};
} // namespace

Json manualThresholdUiCase(Window &window,const std::string &caseId,
                           const std::string &context,const std::string &directory) {
  window.checkThread();
  if (caseId!="T10" && (caseId.size()!=3 || caseId[0]!='T' || caseId[1]!='0' || caseId[2]<'1' || caseId[2]>'9'))
    throw std::invalid_argument("Manual threshold UI case must be T01..T10");
  if (context!="imgui_cpp" && context!="imgui_python")
    throw std::invalid_argument("Manual threshold UI needs a real launch context");
  if (!window.documents().empty() || window.busy())
    throw std::invalid_argument("Manual threshold UI needs a fresh idle editor");
  if (directory.empty()) throw std::invalid_argument("Manual threshold observation directory required");
  const auto path=std::filesystem::absolute(std::filesystem::u8path(directory)).lexically_normal();
  require(path!=path.root_path(),"Manual threshold probe cannot own a filesystem root");
  std::filesystem::create_directories(path.parent_path());
  require(std::filesystem::create_directory(path),"Manual threshold observation directory must be fresh");
  return Driver(window,caseId,context,path).run();
}
} // namespace imagejxx::desktop
