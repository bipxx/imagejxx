// Independent P06i Window-host assertions. These six finite cases are not
// paired UI/OS evidence. Source samples and world coordinates are owned
// literals; neither a product extractor nor a normalized record supplies them.
#include "window.hpp"
#include "axis_range_orientation_ui_probe.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "headless_session_internal.hpp"
#include "../../src/native/operation_job_probe_internal.hpp"
#include <imagejxx/axis_extraction_command.hpp>
#include <imagejxx/axis_orientation_command.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <variant>
#include <vector>

namespace {
using namespace imagejxx;
namespace desktop = imagejxx::desktop;
namespace fs = std::filesystem;
using desktop::GaussianCancellationAccess;
using desktop::HeadlessInputSession;
using desktop::Json;
using desktop::Window;
using namespace std::chrono_literals;
std::size_t assertions = 0;
const char *current_case = "entry";

void check(bool value, const char *message) {
  ++assertions;
  if (!value) throw std::runtime_error(std::string(current_case) + ": " + message);
}
template<class Function> void rejects(Function function, const char *message) {
  bool refused = false;
  try { function(); }
  catch (const std::bad_alloc &) { throw; }
  catch (const std::exception &) { refused = true; }
  check(refused, message);
}
template<class Function> void invalid_argument_first(Function function, const char *message) {
  bool invalid = false;
  try { function(); }
  catch (const std::invalid_argument &) { invalid = true; }
  check(invalid, message);
}
bool same_display(const DisplayState &a, const DisplayState &b) {
  return a.t == b.t && a.c == b.c && a.crosshair == b.crosshair &&
      a.orthogonal == b.orthogonal && a.linkedZoom == b.linkedZoom &&
      a.zoom == b.zoom && a.planeZoom == b.planeZoom && a.pan == b.pan &&
      a.revision == b.revision && metadata_equal(a.properties, b.properties);
}
SpatialCalibration calibration() {
  SpatialCalibration c;
  c.spacing = {.4, 1.2, 3.}; c.origin = {11., -7., 19.};
  c.direction = {1., .25, -.5, .5, -1., .125, 0., .75, 1.5};
  c.unit = "um"; c.frame_interval = .75; c.time_origin = 2.25;
  c.time_unit = "min"; return c;
}
double literal(PixelType type, int t, int c, int z, int y, int x, int s) {
  const int q = (97*t + 31*c + 11*z + 5*y + 3*x + s) % 251;
  return type == PixelType::UInt16 ? 40000. + q :
      type == PixelType::Float64 ? q - 125.125 : double(q);
}
constexpr std::array<std::uint32_t, 6> quiet_bits{{
    0x00000000u, 0x80000000u, 0x3f800000u,
    0xbf800000u, 0x7fc00001u, 0xffc12345u}};
std::uint32_t literal_bits(int channels, int t, int c, int z, int y, int x) {
  const auto index = std::size_t((((t*channels+c)*5+z)*7+y)*9+x);
  return quiet_bits[index % quiet_bits.size()];
}
std::shared_ptr<Document> make_owner(PixelType type = PixelType::UInt16,
                                    int samples = 1, int channels = 3,
                                    bool quiet = false, bool ordinary = false) {
  const int times = ordinary ? 1 : 2;
  if (ordinary) channels = 1;
  Stack planes;
  for (int t = 0; t < times; ++t) for (int c = 0; c < channels; ++c)
    for (int z = 0; z < 5; ++z) {
      Image image(9, 7, samples, type);
      for (int y = 0; y < 7; ++y) for (int x = 0; x < 9; ++x)
        for (int s = 0; s < samples; ++s)
          image.at(x,y,s) = quiet ? float32_source_sample(
              literal_bits(channels,t,c,z,y,x), FloatingSamplePolicy::PreserveQuietNaNFloat32)
              : literal(type,t,c,z,y,x,s);
      planes.push_back(std::move(image));
    }
  const auto policy = quiet ? FloatingSamplePolicy::PreserveQuietNaNFloat32
                            : FloatingSamplePolicy::FiniteOnly;
  DocumentData data{Dataset::from_planes({times,channels,5,7,9,samples},
                                        std::move(planes),calibration(),policy)};
  data.metadata.title = "P06i owned source";
  data.metadata.properties["vendor"] = metadata_object({
      {"unsigned",std::numeric_limits<std::uint64_t>::max()},
      {"array",metadata_array({true,1,std::string("preserve")})}});
  data.metadata.extensions["opaque"] = metadata_object({{"version",17}});
  data.metadata.record_layout = {{"presence",true}};
  data.selection = Selection("rectangle",{{1,1},{5,4}});
  data.previous_selection = Selection("line",{{0,0},{7,5}});
  data.rois = {{"explicit-zero",Selection("point",{{2,3}}),0,0,0,{}},
               {"wildcard",Selection("rectangle",{{0,0},{4,2}}),{},{},{},{}}};
  if (ordinary) for (auto &roi : data.rois) { roi.t.reset(); roi.c.reset(); }
  data.overlays = {{{"vendor",std::string("source-only")}}};
  DisplayState display;
  display.t = times-1; display.c = channels-1;
  display.crosshair = {4,3,2}; display.orthogonal = true;
  display.linkedZoom = false; display.zoom = 2.5;
  display.planeZoom = {1.25,2.,3.}; display.pan = {{{3.,-4.},{-.5,.25},{0.,-2.}}};
  display.properties = {{"lut",std::string("Ice")}};
  auto owner = std::make_shared<Document>(std::move(data),display);
  auto edit = owner->begin_edit(); edit.set_property("history-probe",true);
  owner->commit(edit); owner->undo(); owner->mark_saved();
  return owner;
}
std::array<int,3> axes(Plane p) {
  return p == Plane::XY ? std::array<int,3>{0,1,2} :
      p == Plane::XZ ? std::array<int,3>{0,2,1} : std::array<int,3>{1,2,0};
}
const char *plane_name(Plane p) { return p == Plane::XY ? "XY" : p == Plane::XZ ? "XZ" : "YZ"; }
AxisRangeOrientationRequest request(Plane p = Plane::XZ, int t = 1, int c = 2,
                                    bool reverse = true, bool flip = true,
                                    bool transpose = true,
                                    std::size_t cap = 8*1024*1024) {
  const int last = p == Plane::XY ? 4 : p == Plane::XZ ? 6 : 8;
  return {{p,1,last,2,t,c,cap},reverse,flip,transpose};
}
int count(const AxisRangeOrientationRequest &q) {
  return 1 + (q.range.last-q.range.first)/q.range.step;
}
Json direct_json(std::uint64_t id, const AxisRangeOrientationRequest &q) {
  return {{"document_id",id},{"plane",plane_name(q.range.plane)},
      {"first",q.range.first},{"last",q.range.last},{"step",q.range.step},
      {"reverse_order",q.reverse_order},{"flip_vertical",q.flip_vertical},
      {"transpose",q.transpose}};
}
Json rebind_json(const AxisRangeOrientationRequest &q) {
  auto out = direct_json(0,q); out.erase("document_id");
  out["operation"] = "axis_range_orientation"; out["t"] = q.range.t;
  out["c"] = q.range.c; out["max_working_bytes"] = q.range.max_working_bytes;
  return out;
}
MetadataObject object(const MetadataValue &value) {
  const auto *node = std::get_if<std::shared_ptr<const MetadataTree>>(&value);
  check(node && *node,"Expected immutable metadata tree");
  const auto *result = std::get_if<MetadataObject>(&(*node)->value);
  check(result != nullptr,"Expected metadata object rather than array"); return *result;
}
std::uint64_t integer(const MetadataValue &value) {
  if (const auto *u = std::get_if<std::uint64_t>(&value)) return *u;
  const auto *i = std::get_if<std::int64_t>(&value);
  check(i && *i >= 0,"Expected exact nonnegative integer"); return std::uint64_t(*i);
}
std::array<double,3> world(const SpatialCalibration &c, const std::array<double,3> &v) {
  auto out = c.origin;
  for (int row = 0; row < 3; ++row) for (int col = 0; col < 3; ++col)
    out[row] += c.direction[row*3+col]*c.spacing[col]*v[col];
  return out;
}
void near(double actual, double expected, const char *message) {
  check(std::isfinite(actual) && std::abs(actual-expected) <= 1.e-12,message);
}
void dataset_output(const Dataset &out, const DocumentSnapshot &source,
                    const AxisRangeOrientationRequest &q) {
  const auto &input = source.data().dataset;
  const auto shape = input.shape(); const auto a = axes(q.range.plane);
  const std::array<int,3> extent{{shape.x,shape.y,shape.z}};
  const int width = extent[a[q.transpose ? 1 : 0]];
  const int height = extent[a[q.transpose ? 0 : 1]];
  check(out.shape().dimensions() == std::array<int,6>{1,1,count(q),height,width,shape.s},
        "Orientation output TCZYXS/permuted shape differs");
  check(out.pixel_type() == input.pixel_type() && out.sample_policy() == input.sample_policy(),
        "Orientation changed dtype/sample policy/interleaved sample interpretation");
  const int last = q.range.first + (count(q)-1)*q.range.step;
  const auto &original = input.calibration(); const auto &actual = out.calibration();
  std::array<double,3> first{};
  first[a[1]] = q.flip_vertical ? extent[a[1]]-1 : 0;
  first[a[2]] = q.reverse_order ? last : q.range.first;
  const auto origin = world(original,first);
  for (int row = 0; row < 3; ++row) near(actual.origin[row],origin[row],"Oriented affine origin differs");
  for (int col = 0; col < 3; ++col) {
    const int base_col = col < 2 && q.transpose ? 1-col : col;
    const int source_axis = a[base_col];
    const int sign = base_col == 1 && q.flip_vertical ? -1 :
                     base_col == 2 && q.reverse_order ? -1 : 1;
    check(actual.spacing[col] == original.spacing[source_axis]*(col == 2 ? q.range.step : 1),
          "Oriented positive spacing/permuted singleton stride differs");
    for (int row = 0; row < 3; ++row)
      check(actual.direction[row*3+col] == sign*original.direction[row*3+source_axis],
            "Oriented shear/reflection/signed direction column differs");
  }
  check(actual.unit == original.unit && actual.time_unit == original.time_unit &&
        actual.frame_interval == original.frame_interval &&
        actual.time_origin == original.time_origin + q.range.t*original.frame_interval,
        "Oriented captured-T/time calibration differs");
  for (int k = 0; k < count(q); ++k) {
    const auto image = out.plane({0,0,k});
    for (int v = 0; v < height; ++v) for (int u = 0; u < width; ++u) {
      const int horizontal = q.transpose ? v : u;
      const int vertical = q.transpose ? u : v;
      std::array<int,3> xyz{};
      xyz[a[0]] = horizontal;
      xyz[a[1]] = q.flip_vertical ? extent[a[1]]-1-vertical : vertical;
      xyz[a[2]] = q.reverse_order ? last-k*q.range.step : q.range.first+k*q.range.step;
      for (int s = 0; s < shape.s; ++s) {
        if (input.sample_policy() == FloatingSamplePolicy::PreserveQuietNaNFloat32)
          check(float32_source_sample_bits(image->at(u,v,s),input.sample_policy()) ==
                literal_bits(shape.c,q.range.t,q.range.c,xyz[2],xyz[1],xyz[0]),
                "Orientation changed exact quiet-NaN payload/signed-zero bits");
        else check(image->at(u,v,s) == literal(input.pixel_type(),q.range.t,q.range.c,xyz[2],xyz[1],xyz[0],s),
                   "Orientation samples differ from independent owned indexing");
      }
      if ((u == 0 && v == 0) || (u == width-1 && v == height-1)) {
        const auto expected = world(original,{double(xyz[0]),double(xyz[1]),double(xyz[2])});
        const auto observed = world(actual,{double(u),double(v),double(k)});
        for (int row = 0; row < 3; ++row)
          near(observed[row],expected[row],"Output world landmark differs from independent source affine");
      }
    }
  }
}
void unchanged(const std::shared_ptr<Document> &owner, const DocumentSnapshot &before,
               const HistoryInfo &history) {
  const auto after = owner->snapshot(); const auto now = owner->history();
  check(after.same_document(before) && after.shares_data(before) &&
        after.data_identity() == before.data_identity() &&
        after.revisions().matches(before.revisions(),Change::All) &&
        after.dirty() == before.dirty() && same_display(after.display(),before.display()) &&
        now.undo_steps == history.undo_steps && now.redo_steps == history.redo_steps &&
        now.step_limit == history.step_limit && now.pixel_budget == history.pixel_budget &&
        now.pixels.logical_bytes == history.pixels.logical_bytes &&
        now.pixels.current_bytes == history.pixels.current_bytes &&
        now.pixels.retained_bytes == history.pixels.retained_bytes &&
        now.pixels.obsolete_bytes == history.pixels.obsolete_bytes &&
        now.pixels.retained_planes == history.pixels.retained_planes &&
        now.pixels.exact == history.pixels.exact,
        "Orientation changed source data/save identity/revisions/cameras/history policy/resources");
  const auto old = before.data().dataset.shared_planes(), current = after.data().dataset.shared_planes();
  check(old && current && old->size() == current->size(),"Source eager storage changed");
  for (std::size_t i = 0; i < old->size(); ++i)
    check(old->shared(i) == current->shared(i),"Orientation replaced an immutable source plane");
}
struct Collection {
  Json results,workflow,preferences; bool recording; std::uint64_t active = 0;
  std::vector<DocumentSnapshot> snapshots; std::vector<HistoryInfo> histories;
  explicit Collection(const Window &w) : results(w.results),workflow(w.workflow),
      preferences(w.preferencesJson()),recording(w.recording) {
    if (!w.documents().empty()) active = w.document()->id;
    for (const auto &d : w.documents()) {
      snapshots.push_back(d->nativeSnapshot()); histories.push_back(d->nativeOwner()->history());
    }
  }
  void same(const Window &w) const {
    check(w.documents().size() == snapshots.size() && w.results == results &&
          w.workflow == workflow && w.preferencesJson() == preferences && w.recording == recording &&
          (w.documents().empty() ? 0 : w.document()->id) == active,
          "Refusal changed collection/results/workflow/preferences/activation");
    for (std::size_t i = 0; i < snapshots.size(); ++i) {
      check(w.documents()[i]->id == snapshots[i].id(),"Refusal changed owner order/membership");
      unchanged(w.nativeDocument(snapshots[i].id()),snapshots[i],histories[i]);
    }
  }
};
void output(Window &w, std::uint64_t id, const DocumentSnapshot &source,
            const AxisRangeOrientationRequest &q) {
  const auto snapshot = w.nativeSnapshot(id); const auto &data = snapshot.data();
  const auto owner = w.nativeDocument(id);
  check(id != source.id() && w.document()->id == id && !w.documentById(id)->scientific() &&
        !snapshot.dirty() && owner->undo_steps() == 0 && owner->redo_steps() == 0,
        "Oriented result is not a fresh active clean ordinary owner with empty history");
  dataset_output(data.dataset,source,q);
  check(!data.dataset.shares_pixels(source.data().dataset) && !data.selection &&
        !data.previous_selection && data.rois.empty() && data.overlays.empty() && !data.spectrum &&
        data.metadata.properties.empty() && data.metadata.record_layout.empty() &&
        data.metadata.extensions.size() == 1 && data.metadata.extensions.count("axis_range_orientation") == 1,
        "Oriented output copied source pixels/annotations/opaque metadata");
  const auto provenance = object(data.metadata.extensions.at("axis_range_orientation"));
  const auto expected_title = source.data().metadata.title + " " + plane_name(q.range.plane) + " " +
      std::to_string(q.range.first+1) + "-" + std::to_string(q.range.last+1) + " step " +
      std::to_string(q.range.step) + " [R" + std::to_string(int(q.reverse_order)) +
      " F" + std::to_string(int(q.flip_vertical)) + " T" + std::to_string(int(q.transpose)) + "]";
  check(data.metadata.title == expected_title,"Oriented output changed the generated title convention");
  check(integer(provenance.at("source_document_id")) == source.id() &&
        integer(provenance.at("t")) == std::uint64_t(q.range.t) &&
        integer(provenance.at("c")) == std::uint64_t(q.range.c) &&
        integer(provenance.at("first")) == std::uint64_t(q.range.first) &&
        integer(provenance.at("last")) == std::uint64_t(q.range.last) &&
        integer(provenance.at("step")) == std::uint64_t(q.range.step) &&
        integer(provenance.at("last_selected")) == std::uint64_t(q.range.first+(count(q)-1)*q.range.step) &&
        integer(provenance.at("count")) == std::uint64_t(count(q)) &&
        std::get<std::string>(provenance.at("plane")) == plane_name(q.range.plane) &&
        std::get<bool>(provenance.at("reverse_order")) == q.reverse_order &&
        std::get<bool>(provenance.at("flip_vertical")) == q.flip_vertical &&
        std::get<bool>(provenance.at("transpose")) == q.transpose,
        "Oriented provenance lost original owner/captured T/C/inclusive request/flags");
}

void scientific_admission(const fs::path &dir) {
  Window w((dir/"preferences.json").u8string());
  auto source = make_owner(), other = make_owner(), foreign = make_owner();
  w.addNativeDocument(source); w.addNativeDocument(other); w.recording = true;
  w.results = Json::array({Json{{"sentinel",9}}});
  const auto captured = source->snapshot(); const Collection inactive(w);
  rejects([&]{w.resliceOrientedAxisRange(captured,request());},"Inactive scientific source was admitted"); inactive.same(w);
  rejects([&]{w.resliceOrientedAxisRange(foreign->snapshot(),request());},"Foreign scientific source was admitted"); inactive.same(w);
  w.setActiveIndex(0); const Collection active(w);
  rejects([&]{w.resliceOrientedAxisRange(captured,request(Plane::XY,0,0));},"New scientific request ignored captured T/C"); active.same(w);
  for (const auto *name : {"reslice","reslice_spaced_axis_range","reslice_slicer_axis_range"}) {
    rejects([&]{w.action(name);},"An out-of-scope scientific slicing sibling was admitted"); active.same(w);
  }
  for (auto q : {request(static_cast<Plane>(99)),request(Plane::XY)}) {
    if (q.range.plane == Plane::XY) q.range.step = 0;
    rejects([&]{w.resliceOrientedAxisRange(captured,q);},"Malformed oriented request was admitted"); active.same(w);
  }
  auto display = source->snapshot().display(); display.t = 0; display.c = 0;
  display.crosshair = {8,6,0}; w.setNativeDisplay(display,source->id);
  source->select(Selection("point",{{3,2}}));
  const auto navigated = source->snapshot(); const auto history = source->history();
  const auto other_before = other->snapshot(); const auto other_history = other->history();
  const auto id = w.resliceOrientedAxisRange(captured,request());
  output(w,id,captured,request()); unchanged(source,navigated,history); unchanged(other,other_before,other_history);
  check(w.workflow.size() == 1 && w.results == Json::array({Json{{"sentinel",9}}}),"Captured output did not append exactly once/preserve results");
  w.closeDocument(id,true); w.setActiveIndex(0);
  const auto direct = request(Plane::YZ,0,0,false,true,true);
  const auto valid = direct_json(source->id,direct);
  std::vector<Json> malformed;
  auto p = valid; p.erase("document_id"); malformed.push_back(p);
  p = valid; p["document_id"] = 0; malformed.push_back(p);
  p = valid; p["document_id"] = true; malformed.push_back(p);
  p = valid; p["step"] = true; malformed.push_back(p);
  p = valid; p["first"] = 1.5; malformed.push_back(p);
  p = valid; p["last"] = std::uint64_t(std::numeric_limits<int>::max())+1; malformed.push_back(p);
  p = valid; p["reverse_order"] = 1; malformed.push_back(p);
  p = valid; p["flip_vertical"] = "false"; malformed.push_back(p);
  p = valid; p["transpose"] = nullptr; malformed.push_back(p);
  p = valid; p["extra"] = false; malformed.push_back(p);
  p = valid; p["plane"] = 0; malformed.push_back(p);
  const Collection before_direct(w);
  for (const auto &bad : malformed) {
    rejects([&]{w.action("reslice_oriented_axis_range",bad);},"Direct orientation dictionary silently coerced/ignored an invalid field");
    before_direct.same(w);
  }
  w.action("reslice_oriented_axis_range",valid);
  output(w,w.document()->id,navigated,direct); unchanged(source,navigated,history);
  check(w.workflow.size() == 2,"Direct orientation did not record exactly once");
}

// Small actual-frame host driver. It does not write a UI observation artifact
// or confer paired/physical delivery credit. Every point comes from the latest
// real control, and moving popup geometry is rechecked before a press.
struct Form {
  HeadlessInputSession session; Json last; std::size_t frames = 0;
  const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  explicit Form(Window &w) : session(w) { frame(); frame(); }
  void frame(const Json &events = Json::array()) {
    check(frames++ < 180 && std::chrono::steady_clock::now()-start < 15s,
          "Form host exceeded adopted 180-frame/15-second case bound");
    last = session.frame(events);
  }
  Json control(const std::string &name) const { return last.at("dialog").at("controls").at(name); }
  void settle() { frame(); frame(); }
  void plane(const char *value) {
    click("plane"); frame(); click(std::string("plane.choice.")+value); frame();
  }
  void click(const std::string &name) {
    for (int attempt = 0; attempt < 3; ++attempt) {
      const auto c = control(name); const auto p = c.at("point"); const auto rect = c.at("rect");
      const double x = std::floor(p.at(0).get<double>()), y = std::floor(p.at(1).get<double>());
      check(c.at("enabled") == true && c.at("id").get<std::uint64_t>() != 0 &&
            x >= rect.at(0).get<double>() && x < rect.at(2).get<double>() &&
            y >= rect.at(1).get<double>() && y < rect.at(3).get<double>(),
            "Form host target is not a current eligible control");
      frame(Json::array({Json{{"type","position"},{"space","screen"},{"point",{x,y}}}}));
      const auto now = control(name);
      if (now.at("id") != c.at("id") || now.at("rect") != rect) continue;
      frame(Json::array({Json{{"type","button"},{"name","left"},{"down",true}}}));
      frame(Json::array({Json{{"type","button"},{"name","left"},{"down",false}}}));
      return;
    }
    throw std::runtime_error("Form control geometry did not settle in three position observations");
  }
  void text(const char *name, const char *value) {
    click(name);
#if defined(__APPLE__)
    const char *primary = "Super";
#else
    const char *primary = "Ctrl";
#endif
    const auto chord = [&](bool down) {
      return Json::array({Json{{"type","keys"},{"keys",Json::array({
          Json{{"name",primary},{"down",down}},Json{{"name","A"},{"down",down}}})}}});
    };
    frame(chord(true)); frame(chord(false));
    frame(Json::array({Json{{"type","text"},{"value",value}}})); frame();
    check(last.at("dialog").at("text").at(name) == value,"Actual form text did not retain typed bytes");
  }
};
void dialog_defaults(const fs::path &dir) {
  Window w((dir/"preferences.json").u8string()); w.updateSettings({{"processing_memory_mb",1}},false);
  auto source = make_owner(); w.addNativeDocument(source); w.recording = true;
  Form form(w); const Collection before(w);
  w.action("show_axis_range_orientation",{{"document_id",source->id}}); form.settle();
  const auto opened = form.last.at("dialog");
  check(opened.at("open") == true && opened.at("retained_source") == true &&
        opened.at("opened_document_id") == source->id &&
        opened.at("opened_document_id_semantics") == "captured_native_execution_source" &&
        opened.at("captured_t") == 1 && opened.at("captured_c") == 2 &&
        opened.at("captured_channel_label") == "Channel 3 / 3" &&
        opened.at("captured_time_label") == "Time point 2 / 2",
        "Orientation form did not retain/expose exact captured scientific owner/T/C");
  const Json defaults{{"plane","XZ"},{"first",1},{"last",7},{"step",1},
      {"reverse_order",false},{"flip_vertical",false},{"transpose",false}};
  check(opened.at("values") == defaults && opened.at("fixed_axis_label") == "Y indices: 1 to 7" &&
        opened.at("emitted_plane_count") == 7 && opened.at("can_apply") == true &&
        opened.at("controls").at("submit").at("enabled") == true,
        "Shared scientific orientation defaults/labels/readiness differ");
  auto default_request = request(Plane::XZ,1,2,false,false,false,1024*1024);
  default_request.range.first = 0; default_request.range.last = 6; default_request.range.step = 1;
  const auto layout = describe_axis_range_orientation(source->snapshot().data().dataset,default_request);
  check(opened.at("working_bytes") == layout.working_bytes &&
        opened.at("max_working_bytes") == 1024*1024,
        "Form readiness did not use metadata descriptor/effective configured cap");
  form.text("first","-"); form.click("flip_vertical"); form.frame();
  check(form.last.at("dialog").at("text").at("first") == "-" &&
        form.last.at("dialog").at("can_apply") == false &&
        form.control("submit").at("enabled") == false,
        "Partial invalid form text was clamped/restored or permitted publication");
  form.plane("YZ");
  check(form.last.at("dialog").at("values").at("plane") == "YZ" &&
        form.last.at("dialog").at("fixed_axis_label") == "X indices: 1 to 9" &&
        form.last.at("dialog").at("text").at("first") == "-" &&
        form.last.at("dialog").at("text").at("last") == "7" &&
        form.last.at("dialog").at("can_apply") == false && form.control("submit").at("enabled") == false,
        "Plane switch silently replaced/clamped an explicitly invalid orientation draft");
  form.plane("XZ");
  check(form.last.at("dialog").at("text").at("first") == "-" && form.control("submit").at("enabled") == false,
        "Returning to original plane repaired partial invalid text");
  before.same(w);
  form.text("first","2"); form.text("last","7"); form.text("step","2");
  form.click("reverse_order"); form.click("transpose"); form.settle();
  const auto draft = form.last.at("dialog");
  check(draft.at("can_apply") == true && draft.at("emitted_plane_count") == 3 &&
        draft.at("last_selected_1based") == 6 && draft.at("requested_last_1based") == 7 &&
        draft.at("effective_first_selected_1based") == 6 && draft.at("effective_last_selected_1based") == 2 &&
        draft.at("output_axis_order") == Json::array({"Z","X","Y"}) &&
        draft.at("output_axis_signs") == Json::array({-1,1,-1}) &&
        draft.at("flip_before_transpose") == true,
        "Metadata form summary lost unaligned Last/flag order/signed axis permutation");
  auto display = source->snapshot().display(); display.t = 0; display.c = 0;
  display.crosshair = {8,6,0}; w.setNativeDisplay(display,source->id); form.frame();
  check(form.last.at("dialog").at("captured_t") == 1 &&
        form.last.at("dialog").at("captured_c") == 2 &&
        form.last.at("dialog").at("values") == draft.at("values"),
        "Allowed display navigation retargeted the retained form");
  const Collection navigated(w); form.click("cancel"); form.frame();
  check(form.last.at("dialog").at("open") == false,"Actual Cancel left the orientation form reachable");
  navigated.same(w); check(w.workflow.empty() && w.results.empty(),"Canceled draft changed workflow/results");
}

void staged_publication(const fs::path &dir) {
  {
    Window w((dir/"success.json").u8string());
    w.updateSettings({{"history_limit",3},{"default_zoom",2.0},{"processing_memory_mb",1}},false);
    auto source = make_owner(); w.addNativeDocument(source); w.recording = true;
    const auto capture = source->snapshot(); const auto history = source->history();
    const auto q = request(); std::size_t staged = 0,attached = 0; std::exception_ptr callback_error;
    desktop::AxisRangeOrientationUiProbeAccess::setPublicationProbe(w,
        [&](const char *phase,const DocumentSnapshot &s,const AxisRangeOrientationRequest &r,
            const std::shared_ptr<Document> &candidate) {
      try {
        check(s.same_document(capture) && r.range.t == 1 && r.range.c == 2,
              "Publication observation retargeted the typed source/request");
        if (std::strcmp(phase,"before_native_publish") == 0) {
          ++staged; check(candidate->history_limit() == 3 && !candidate->snapshot().dirty(),
                          "Frontend policy was not staged before native publication");
          check(w.documents().size() == 1 && w.workflow.empty(),"Candidate leaked into facade/workflow before native publication");
        }
        if (std::strcmp(phase,"after_attach") == 0) {
          ++attached; check(w.document()->id == candidate->id && w.workflow.size() == 1,
                           "Post-attach observation preceded complete publication");
        }
      } catch (...) { callback_error = std::current_exception(); }
    });
    const auto id = w.resliceOrientedAxisRange(capture,q);
    desktop::AxisRangeOrientationUiProbeAccess::setPublicationProbe(w,{});
    if (callback_error) std::rethrow_exception(callback_error);
    output(w,id,capture,q); unchanged(source,capture,history);
    check(staged == 1 && attached == 1 && w.nativeDocument(id)->history_limit() == 3,
          "Common staged publication did not attach exactly once with configured history");
    const auto &c = w.nativeSnapshot(id).data().dataset.calibration(); double length = 0;
    for (int row = 0; row < 3; ++row) length += c.direction[row*3]*c.direction[row*3];
    check(w.nativeSnapshot(id).display().zoom == 2./(c.spacing[0]*std::sqrt(length)),
          "Derived owner ignored configured physical initial zoom");
    check(w.workflow.at(0).at("request").at("max_working_bytes") == q.range.max_working_bytes,
          "Effective frontend cap mutated the captured/recorded request");
  }
  {
    Window w((dir/"cap.json").u8string()); w.updateSettings({{"processing_memory_mb",1}},false);
    Stack planes; for (int i = 0; i < 30; ++i) planes.emplace_back(257,257,1,PixelType::UInt16);
    auto source = std::make_shared<Document>(Dataset::from_planes({2,3,5,257,257,1},std::move(planes),calibration()));
    w.addNativeDocument(source); w.recording = true;
    auto q = request(Plane::XY,0,0); const Collection before(w); std::size_t staged = 0;
    desktop::AxisRangeOrientationUiProbeAccess::setPublicationProbe(w,
        [&](const char *,const DocumentSnapshot &,const AxisRangeOrientationRequest &,const std::shared_ptr<Document> &){++staged;});
    rejects([&]{w.resliceOrientedAxisRange(source->snapshot(),q);},"Request cap raised the smaller configured processing cap");
    before.same(w); check(staged == 0,"Cap refusal reached staged output publication");
    desktop::AxisRangeOrientationUiProbeAccess::setPublicationProbe(w,{});
  }
  for (int scenario = 0; scenario < 5; ++scenario) {
    Window w((dir/("conflict-"+std::to_string(scenario)+".json")).u8string());
    auto source = make_owner(), other = make_owner();
    w.addNativeDocument(source); w.addNativeDocument(other); w.setActiveIndex(0); w.recording = true;
    const auto capture = source->snapshot(); std::size_t calls = 0;
    std::shared_ptr<Document> candidate; std::unique_ptr<Collection> intervening; std::exception_ptr callback_error;
    desktop::AxisRangeOrientationUiProbeAccess::setPublicationProbe(w,
        [&](const char *phase,const DocumentSnapshot &,const AxisRangeOrientationRequest &,
            const std::shared_ptr<Document> &fresh) {
      if (std::strcmp(phase,"before_native_publish") != 0) return;
      ++calls; candidate = fresh;
      try {
        if (scenario == 0) { auto edit = source->begin_edit(); edit.set_property("intervening",true); source->commit(edit); }
        if (scenario == 1) { auto edit = source->begin_edit(); edit.replace_plane({1,2,0},Image(9,7,1,PixelType::UInt16)); source->commit(edit); }
        if (scenario == 2) w.closeDocument(source->id,true);
        if (scenario == 3) w.setActiveIndex(1);
        if (scenario == 4) { auto edit = fresh->begin_edit(); edit.set_title("Changed staged candidate"); fresh->commit(edit); }
        intervening = std::make_unique<Collection>(w);
      } catch (...) { callback_error = std::current_exception(); }
    });
    rejects([&]{w.resliceOrientedAxisRange(capture,request());},"Intervening owner/candidate change was adopted");
    desktop::AxisRangeOrientationUiProbeAccess::setPublicationProbe(w,{});
    if (callback_error) std::rethrow_exception(callback_error);
    check(calls == 1 && candidate && intervening,"Conflict did not reach actual staged candidate boundary");
    intervening->same(w);
    rejects([&]{(void)w.nativeDocument(candidate->id);},"Rejected output left a controller/facade member");
    check(candidate.use_count() == 1,"Rejected output retained an orphan Pane/facade/preparation owner");
    check(w.workflow.empty() && w.results.empty(),"Rejected publication appended workflow/results");
  }
  {
    Window w((dir/"bad-workflow.json").u8string()); auto source = make_owner(); w.addNativeDocument(source);
    w.recording = true; w.workflow = Json{{"invalid","array required"}}; const Collection before(w);
    rejects([&]{w.resliceOrientedAxisRange(source->snapshot(),request());},"Malformed workflow container permitted partial output adoption");
    before.same(w);
  }
}

void strict_replay(const fs::path &dir) {
  Window w((dir/"preferences.json").u8string()); auto source = make_owner(); w.addNativeDocument(source);
  w.updateSettings({{"processing_memory_mb",1}},false); w.recording = true;
  const Json legacy{{"operation","add"},{"parameters",{{"value",1}}},{"all_slices",false},{"slice_index",0}};
  w.workflow = Json::array({legacy}); w.results = Json::array({Json{{"sentinel",9}}});
  const auto capture = source->snapshot(); const auto q = request(Plane::YZ);
  const auto record = resolve_axis_orientation_command(capture,q).record();
  const auto record_json = desktop::metadataJson(record);
  check(record_json.at("version") == 2 && record_json.at("operation") == "axis_range_orientation" &&
        record_json.at("request").at("reverse_order").is_boolean(),"Resolved orientation record is not a closed typed V2 record");
  const auto id = w.resliceOrientedAxisRange(capture,q); output(w,id,capture,q);
  check(w.workflow == Json::array({legacy,record_json}),"New orientation did not append its normalized V2 record exactly once");
  w.closeDocument(id,true); w.setActiveIndex(0);
  auto display = source->snapshot().display(); display.t = 0; display.c = 0; display.crosshair = {8,6,0};
  w.setNativeDisplay(display,source->id); source->select(Selection("point",{{3,2}}));
  const auto navigated = source->snapshot(); const auto history = source->history();
  const auto replayed = w.replayAxisOrientation(record,{},source->id);
  output(w,replayed,capture,q); unchanged(source,navigated,history);
  check(w.workflow == Json::array({legacy,record_json,record_json}),"Strict replay retargeted T/C or appended recursively");
  w.closeDocument(replayed,true); w.setActiveIndex(0);
  w.action("replay_axis_orientation",{{"record",record_json},{"document_id",source->id}});
  output(w,w.document()->id,capture,q);
  check(w.workflow.size() == 4 && w.workflow.back() == record_json,"Thin strict replay action did not append exactly once");
  w.closeDocument(w.document()->id,true); w.setActiveIndex(0); w.recording = false;
  const auto workflow = w.workflow; const auto off = w.replayAxisOrientation(record,{},source->id);
  check(w.workflow == workflow,"Recording-off replay appended a record"); w.closeDocument(off,true); w.setActiveIndex(0);
  auto malformed = record; malformed["destination"] = std::string("in_place"); const Collection valid(w);
  invalid_argument_first([&]{w.replayAxisOrientation(malformed,{},std::numeric_limits<std::uint64_t>::max());},
                         "Malformed original was not validated before absent target lookup"); valid.same(w);
  invalid_argument_first([&]{w.replayAxisOrientation(malformed,request(),std::numeric_limits<std::uint64_t>::max());},
                         "Explicit rebind repaired/bypassed malformed original before target lookup"); valid.same(w);
  auto derived = object(record.at("derived")); derived["working_bytes"] = std::uint64_t(1);
  auto tampered = record; tampered["derived"] = metadata_object(derived);
  rejects([&]{w.replayAxisOrientation(tampered,request(),source->id);},"Explicit rebind trusted tampered original derivation"); valid.same(w);
  const auto path = dir/"orientation-workspace.ijx";
  desktop::saveNativeSession(path.u8string(),w.documents(),w.results,w.workflow);
  Window reopened((dir/"reopened.json").u8string()); reopened.openFile(path.u8string());
  check(reopened.documents().size() == 1 && reopened.workflow == workflow && reopened.results == w.results,
        "V3 session lost mixed V1/V2 workflow/results");
  auto new_source = reopened.nativeDocument(); const Collection before(reopened);
  rejects([&]{reopened.replayAxisOrientation(record,{},new_source->id);},"Strict replay silently rebound fresh session owner"); before.same(reopened);
  auto c = calibration(); c.origin = {13.,-5.,21.}; c.spacing = {2.,4.,.5};
  c.direction = {0.,-1.,.25,1.,.5,0.,0.,0.,-1.}; c.unit = "mm";
  auto edit = new_source->begin_edit(); edit.set_calibration(c); new_source->commit(edit);
  auto view = new_source->snapshot().display(); view.t = 0; view.c = 1; reopened.setNativeDisplay(view,new_source->id);
  const auto rebound_capture = new_source->snapshot(); const auto rebound_history = new_source->history();
  const auto explicit_request = request(Plane::XY,0,1,false,true,false);
  const auto rebound_record = desktop::metadataJson(rebind_axis_orientation_command(rebound_capture,record,explicit_request).record());
  reopened.recording = true;
  const auto rebound = reopened.replayAxisOrientation(record,explicit_request,new_source->id);
  output(reopened,rebound,rebound_capture,explicit_request); unchanged(new_source,rebound_capture,rebound_history);
  check(reopened.workflow.size() == workflow.size()+1 && reopened.workflow.back() == rebound_record &&
        reopened.workflow.at(1) == record_json,"Explicit full rebind changed original or failed single new append");
  reopened.closeDocument(rebound,true); reopened.setActiveIndex(0);
  const auto before_action = reopened.workflow.size();
  reopened.action("replay_axis_orientation",{{"record",record_json},{"document_id",new_source->id},
      {"rebind_request",rebind_json(explicit_request)}});
  output(reopened,reopened.document()->id,rebound_capture,explicit_request);
  check(reopened.workflow.size() == before_action+1 && reopened.workflow.back() == rebound_record,
        "Closed full rebind action failed matching output/single record");
  reopened.closeDocument(reopened.document()->id,true); reopened.setActiveIndex(0); const Collection action_before(reopened);
  std::vector<Json> malformed_requests;
  auto r = rebind_json(explicit_request); r.erase("transpose"); malformed_requests.push_back(r);
  r = rebind_json(explicit_request); r["flip_vertical"] = 0; malformed_requests.push_back(r);
  r = rebind_json(explicit_request); r["t"] = true; malformed_requests.push_back(r);
  r = rebind_json(explicit_request); r["operation"] = "axis_range_reslice"; malformed_requests.push_back(r);
  r = rebind_json(explicit_request); r["extra"] = 1; malformed_requests.push_back(r);
  for (const auto &bad : malformed_requests) {
    rejects([&]{reopened.action("replay_axis_orientation",{{"record",record_json},{"document_id",new_source->id},{"rebind_request",bad}});},
            "Action accepted incomplete/coerced/cross-operation full rebind request"); action_before.same(reopened);
  }
  rejects([&]{reopened.replayAxisOrientation(record,request(Plane::XY,1,2),new_source->id);},
          "Frontend rebind ignored destination captured current T/C"); action_before.same(reopened);
  w.recording = true; auto changed = source->begin_edit(); changed.set_property("changed",true); source->commit(changed); source->undo();
  const Collection stale(w);
  rejects([&]{w.replayAxisOrientation(record,{},source->id);},"Undo restoring equal values revived stale strict replay revisions"); stale.same(w);
}

bool same_usage(const JobUsage &a, const JobUsage &b) {
  return a.running == b.running && a.queued == b.queued && a.reserved_bytes == b.reserved_bytes &&
      a.running_bytes == b.running_bytes && a.retained_bytes == b.retained_bytes;
}
void released(Window &w) {
  const auto scheduler = GaussianCancellationAccess::scheduler(w); w.poll(); const auto usage = scheduler->usage();
  check(!usage.running && !usage.queued && !usage.reserved_bytes && !usage.running_bytes && !usage.retained_bytes,
        "Lifecycle retained native work or image-buffer reservations");
}
void drain(Window &w) {
  const auto end = std::chrono::steady_clock::now()+5s;
  while (w.busy() && std::chrono::steady_clock::now() < end) { w.poll(); std::this_thread::yield(); }
  check(!w.busy(),"Native lifecycle did not terminate in 5000ms"); w.poll();
}
TargetRequest gaussian_target() {
  TargetRequest q; q.scope = TargetScope::Positions; q.positions = {{1,2,2}};
  q.roi = RoiPolicy::WholePlane; return q;
}
struct Gate {
  std::mutex mutex; std::condition_variable changed; bool entered = false,open = false;
  void block() {
    std::unique_lock<std::mutex> lock(mutex); entered = true; changed.notify_all();
    if (!changed.wait_for(lock,5s,[&]{return open;})) throw std::runtime_error("Busy gate exceeded 5000ms");
  }
  void await() {
    std::unique_lock<std::mutex> lock(mutex);
    check(changed.wait_for(lock,5s,[&]{return entered;}),"Busy operation never reached its real bounded gate");
  }
  void release() { std::lock_guard<std::mutex> lock(mutex); open = true; changed.notify_all(); }
};
struct Release { std::shared_ptr<Gate> gate; ~Release(){gate->release();} };
void owner_lifecycle(const fs::path &dir) {
  {
    Window w((dir/"preview.json").u8string()); auto source = make_owner(PixelType::Float32); w.addNativeDocument(source); w.recording = true;
    const auto captured = source->snapshot();
    GaussianCancellationAccess::request(w,resolve_gaussian_command(captured,gaussian_target(),{.85,false}),601);
    const auto job = GaussianCancellationAccess::laneJob(w,true);
    check(job && job->wait_for(5s) && job->info().state == JobState::Ready,"Gaussian preview did not reach actual READY");
    w.poll(); const auto diagnostics = GaussianCancellationAccess::diagnostics(w);
    check(diagnostics.at("gaussian_jobs").at("active_installed_generation") == 601 &&
          diagnostics.at("gaussian_jobs").at("preview").at("exit") == "installed","READY preview was not actually installed");
    const auto scheduler = GaussianCancellationAccess::scheduler(w); const auto usage = scheduler->usage(); const Collection before(w);
    auto too_small = request(); too_small.range.max_working_bytes = 1;
    rejects([&]{w.resliceOrientedAxisRange(captured,too_small);},"Preparation failure was admitted beside READY preview"); before.same(w);
    check(GaussianCancellationAccess::diagnostics(w) == diagnostics && same_usage(scheduler->usage(),usage),
          "Refusal released/retargeted retained READY preview or resources");
    auto view = source->snapshot().display(); view.t = 0; view.c = 1; view.crosshair = {8,6,0}; w.setNativeDisplay(view,source->id);
    source->select(Selection("point",{{3,2}})); const auto navigated = source->snapshot(); const auto history = source->history();
    const auto id = w.resliceOrientedAxisRange(captured,request()); output(w,id,captured,request()); unchanged(source,navigated,history);
    check(w.workflow.size() == 1,"Committed-source orientation beside preview did not append once"); released(w);
    check(GaussianCancellationAccess::diagnostics(w).at("gaussian_jobs").at("active_installed_generation").is_null(),
          "New output activation retained old source preview generation");
  }
  for (const std::string mode : {"queued","running","ready"}) {
    Window w((dir/(mode+".json")).u8string()); auto source = make_owner(PixelType::Float32); w.addNativeDocument(source); w.recording = true;
    const auto captured = source->snapshot(); const auto scheduler = GaussianCancellationAccess::scheduler(w);
    scheduler->set_limits({1,0,64}); auto gate = std::make_shared<Gate>(); Release release{gate}; std::optional<Job> blocker;
    if (mode == "queued") { blocker = scheduler->submit([gate](const JobContext &c){gate->block();c.checkpoint();}); gate->await(); }
    auto stopped = std::make_shared<std::atomic<bool>>(false);
    {
      detail::ScopedOperationJobProbe probe([mode,gate,stopped](const detail::OperationJobProbeEvent &e) {
        if (mode == "running" && e.phase == "stage_progress" && e.stage == "ImageJ Gaussian source copy" &&
            e.completed > 0 && !stopped->exchange(true)) gate->block();
      });
      w.applyGaussian(gaussian_target(),{.85,false},source->id);
    }
    const auto job = GaussianCancellationAccess::laneJob(w,false); check(bool(job),"Busy Apply did not create a native job");
    if (mode == "running") gate->await();
    if (mode == "ready") check(job->wait_for(5s) && job->info().state == JobState::Ready,"Apply never reached unpublished READY");
    const auto state = mode == "queued" ? JobState::Queued : mode == "running" ? JobState::Running : JobState::Ready;
    check(w.busy() && job->info().state == state,"Busy fixture did not establish its declared actual state");
    const Collection before(w); const auto usage = scheduler->usage(); const auto diagnostics = GaussianCancellationAccess::diagnostics(w);
    rejects([&]{w.resliceOrientedAxisRange(captured,request());},"Orientation was admitted during actual Apply busy"); before.same(w);
    rejects([&]{w.action("show_axis_range_orientation");},"Form opened during actual Apply busy"); before.same(w);
    check(same_usage(scheduler->usage(),usage) && GaussianCancellationAccess::diagnostics(w) == diagnostics,
          "Busy refusal disturbed active lane/job/reservations");
    w.cancel(); gate->release(); if (blocker) check(blocker->wait_for(5s),"Queue blocker did not terminate");
    check(job->wait_for(5s),"Canceled native Apply did not terminate"); released(w);
  }
  for (const std::string mode : {"legacy","threshold"}) {
    Window w((dir/(mode+".json")).u8string()); auto source = make_owner(PixelType::UInt16,1,1,false,true);
    w.addNativeDocument(source); w.recording = true; const auto capture = source->snapshot();
    if (mode == "legacy") w.applyOperation("add",{{"value",0}},false,true,source->id);
    else { w.action("show_manual_threshold",{{"document_id",source->id}});
      w.action("apply_manual_threshold",{{"document_id",source->id},{"lower",40010},{"upper",40100}}); }
    const auto job = w.jobInfo(); check(w.busy() && job && job->id != 0 && job->document_id == source->id,
                                      "Ordinary Apply did not create an actual owned busy job");
    const Collection before(w);
    rejects([&]{w.resliceOrientedAxisRange(capture,request(Plane::XZ,0,0));},"Orientation admitted ordinary threshold/legacy busy lane"); before.same(w);
    check(w.jobInfo() && w.jobInfo()->id == job->id,"Refusal replaced active ordinary job");
    w.cancel(); drain(w); released(w);
  }
  {
    Window w((dir/"closed-stale.json").u8string()); auto source = make_owner(); w.addNativeDocument(source);
    const auto captured = source->snapshot(); auto edit = source->begin_edit(); edit.set_property("changed",true); source->commit(edit);
    const Collection stale(w); rejects([&]{w.resliceOrientedAxisRange(captured,request());},"Stale retained source was admitted"); stale.same(w);
    w.closeDocument(source->id,true); const Collection closed(w);
    rejects([&]{w.resliceOrientedAxisRange(captured,request());},"Closed retained source was admitted"); closed.same(w);
  }
}

void eager_v1_compatibility(const fs::path &dir) {
  static_assert(std::variant_size_v<AxisExtractionRequest> == 2,"V1 public variant acquired an orientation alternative");
  {
    Window w((dir/"inactive.json").u8string()); auto source = make_owner(PixelType::UInt16,1,1,false,true), other = make_owner();
    w.addNativeDocument(source); w.addNativeDocument(other); w.recording = true;
    check(!w.documentById(source->id)->scientific() && w.document()->id == other->id,
          "Inactive ordinary-owner fixture is not actually ordinary/inactive");
    const auto capture = source->snapshot(); const auto history = source->history();
    const auto other_before = other->snapshot(); const auto other_history = other->history();
    auto q = request(Plane::XZ,0,0,false,false,false); auto p = direct_json(source->id,q);
    p.erase("reverse_order"); p.erase("flip_vertical"); p.erase("transpose");
    w.action("reslice_oriented_axis_range",p); const auto id = w.document()->id;
    output(w,id,capture,q); unchanged(source,capture,history); unchanged(other,other_before,other_history);
    check(w.workflow.size() == 1 && w.workflow.at(0).at("version") == 2,"All-false default direct action lost V2 record");
    const auto oriented = w.nativeSnapshot(id).data().dataset;
    w.closeDocument(id,true); w.setActiveIndex(1);
    const auto old_range = AxisExtractionRequest{q.range};
    const auto old_record = resolve_axis_extraction_command(capture,old_range).record();
    const auto plain_id = w.resliceAxisRange(capture,q.range); const auto plain = w.nativeSnapshot(plain_id).data().dataset;
    const auto &pc = plain.calibration(), &oc = oriented.calibration();
    check(plain.shape().dimensions() == oriented.shape().dimensions() &&
          pc.spacing == oc.spacing && pc.origin == oc.origin && pc.direction == oc.direction &&
          pc.unit == oc.unit && pc.time_unit == oc.time_unit &&
          pc.frame_interval == oc.frame_interval && pc.time_origin == oc.time_origin,
          "All-false orientation changed plain V1 shape/calibration");
    for (int k = 0; k < plain.shape().z; ++k)
      check(plain.plane({0,0,k})->pixels == oriented.plane({0,0,k})->pixels,"All-false orientation changed plain V1 samples");
    check(w.workflow.size() == 2 && w.workflow.back() == desktop::metadataJson(old_record) && w.workflow.back().at("version") == 1,
          "Plain range command changed V1 grammar/normalization");
    w.closeDocument(plain_id,true); w.setActiveIndex(1); const Collection before(w);
    rejects([&]{w.replayAxisOrientation(old_record,{},source->id);},"Orientation replay accepted V1 range record"); before.same(w);
    const auto oriented_record = resolve_axis_orientation_command(capture,q).record();
    rejects([&]{w.replayAxisExtraction(oriented_record,{},source->id);},"V1 replay accepted oriented V2 record"); before.same(w);
    const AxisPlaneExportRequest plane{Plane::XY,2,0,0,1024*1024};
    const auto plane_record = resolve_axis_extraction_command(capture,AxisExtractionRequest{plane}).record();
    const auto plane_id = w.exportAxisPlane(capture,plane);
    const auto image = w.nativeSnapshot(plane_id).data().dataset.plane({0,0,0});
    for (int y = 0; y < 7; ++y) for (int x = 0; x < 9; ++x)
      check(image->at(x,y) == literal(PixelType::UInt16,0,0,2,y,x,0),"V1 plane export changed samples");
    check(w.workflow.size() == 3 && w.workflow.back() == desktop::metadataJson(plane_record),"V1 plane workflow no longer appends once");
    unchanged(source,capture,history); unchanged(other,other_before,other_history);
  }
  struct Profile { PixelType type; int samples,channels; bool quiet; Plane plane; };
  const std::array<Profile,3> profiles{{
      {PixelType::UInt8,3,3,false,Plane::XY},
      {PixelType::Float64,4,2,false,Plane::XZ},
      {PixelType::Float32,1,5,true,Plane::YZ}}};
  for (std::size_t i = 0; i < profiles.size(); ++i) {
    const auto p = profiles[i]; Window w((dir/("profile-"+std::to_string(i)+".json")).u8string());
    auto source = make_owner(p.type,p.samples,p.channels,p.quiet); w.addNativeDocument(source);
    const auto capture = source->snapshot(); const auto history = source->history();
    const auto q = request(p.plane,1,p.channels-1); const auto id = w.resliceOrientedAxisRange(capture,q);
    output(w,id,capture,q); unchanged(source,capture,history);
    const auto path = dir/("profile-"+std::to_string(i)+".ijx");
    desktop::saveNativeSession(path.u8string(),{w.documentById(id)},Json::array(),Json::array());
    Json results,workflow; const auto opened = desktop::loadNativeSession(path.u8string(),results,workflow);
    check(opened.size() == 1 && results.empty() && workflow.empty(),"V3 oriented result reopened wrong owners/results/workflow");
    dataset_output(opened.at(0)->nativeSnapshot().data().dataset,capture,q);
    check(w.workflow.empty(),"Recording-off RGB/RGBA/quiet-bit orientation appended workflow");
  }
}
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 2 || argc == 3,"Supply a fresh artifact directory and optional exact H01-H06 case ID");
    const auto dir = fs::absolute(fs::u8path(argv[1])).lexically_normal();
    check(dir != dir.root_path() && !fs::exists(dir),"Window orientation artifact directory must be fresh");
    fs::create_directories(dir); const std::string selected = argc == 3 ? argv[2] : "all";
    bool matched = false;
    const auto run = [&](const char *id, auto function) {
      if (selected != "all" && selected != id) return;
      matched = true; current_case = id; const auto path = dir/id; fs::create_directory(path);
      function(path); std::cout << "PASS " << id << '\n';
    };
    run("H01.scientific_admission_and_captured_position",scientific_admission);
    run("H02.dialog_defaults_metadata_readiness_and_cancel",dialog_defaults);
    run("H03.common_staged_publication_and_effective_cap",staged_publication);
    run("H04.strict_replay_explicit_rebind_recording",strict_replay);
    run("H05.owner_busy_preview_lifecycle",owner_lifecycle);
    run("H06.eager_and_v1_plane_range_compatibility",eager_v1_compatibility);
    check(matched,"Unknown exact scientific orientation host case ID");
    std::cout << "scientific-axis-orientation-window assertions=" << assertions << '\n'; return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL scientific-axis-orientation-window after " << assertions << " assertions: " << error.what() << '\n'; return 1;
  }
}
