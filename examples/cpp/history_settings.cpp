// Installed public SDK conformance consumer. No desktop, private product,
// Python, OS input, third-party JSON or expected-output file dependency.
#include <imagejxx/application.hpp>
#include <imagejxx/document.hpp>
#include <imagejxx/preferences.hpp>
#include <imagejxx/session.hpp>
#include <imagejxx/settings.hpp>
#include "shortcut_observations.hpp" // Example-local standard-library transport only.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace imagejxx;
namespace fs = std::filesystem;
namespace transport = shortcut_sdk_observations;
using Value = transport::Value;
constexpr std::size_t mib = 1048576, plane_bytes = 262144;
constexpr const char *step = "G010.history.step_limit";
constexpr const char *budget = "G010.history.pixel_budget";
std::size_t checks = 0;

void require(bool condition, const char *message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}

class Observations {
public:
  void begin(const char *id) {
    if (!current_.empty()) throw std::logic_error("Unfinished history marker");
    for (const auto &old : case_ids_) if (old == id) throw std::logic_error("Duplicate history marker");
    current_ = id; pending_.clear();
  }
  void scalar(const char *requirement, const char *id, Value value) {
    if (std::holds_alternative<Value::Array>(value.data) || std::holds_alternative<Value::Object>(value.data))
      throw std::invalid_argument("Scalar history observation must be typed scalar");
    add(requirement, id, "scalar", std::move(value));
  }
  void bytes(const char *requirement, const char *id, Value value) {
    add(requirement, id, "bytes_le", std::move(value));
  }
  void auxiliary(const char *id, Value value) {
    add("auxiliary", id, "canonical_json", std::move(value), false);
  }
  void finish() {
    if (current_.empty()) throw std::logic_error("No active history marker");
    const auto row = "{\"id\":" + transport::quote(current_) + ",\"observations\":[" + pending_ + "]}";
    if (!cases_.empty()) cases_ += ',';
    cases_ += row; case_ids_.push_back(current_); current_.clear(); pending_.clear();
    if (cases_.size() > transport::max_bytes) throw std::length_error("History output exceeds16MiB");
  }
  void write(const fs::path &directory) const {
    if (!current_.empty() || case_ids_.size() != 4 || assertion_rows_ != 53)
      throw std::logic_error("Exact four markers and53 distinct assertion rows required");
    const auto raw = "{\"schema_version\":1,\"producer_id\":\"p06h.history-settings.sdk.raw.001\","
        "\"complete\":true,\"cases\":[" + cases_ + "]}\n";
    if (raw.size() > transport::max_bytes) throw std::length_error("History output exceeds16MiB");
    auto *file = std::fopen((directory / "history-settings-observations.json").string().c_str(), "wbx");
    if (!file) throw std::runtime_error("Cannot exclusively create history observations");
    const bool written = std::fwrite(raw.data(), 1, raw.size(), file) == raw.size();
    const bool flushed = std::fflush(file) == 0;
    const bool closed = std::fclose(file) == 0;
    if (!written || !flushed || !closed) throw std::runtime_error("History observations write failed");
  }
private:
  void add(const char *requirement, const char *id, const char *encoding, Value value, bool assertion = true) {
    if (current_.empty()) throw std::logic_error("History observation has no case");
    if (!identities_.insert(std::string(requirement) + "/" + id).second)
      throw std::logic_error("Duplicate history observation identity");
    const auto row = "{\"requirement_id\":" + transport::quote(requirement) +
        ",\"id\":" + transport::quote(id) + ",\"encoding\":" + transport::quote(encoding) +
        ",\"value\":" + transport::json(value) + '}';
    if (cases_.size() + pending_.size() + row.size() + 1024 > transport::max_bytes)
      throw std::length_error("History output exceeds16MiB");
    if (!pending_.empty()) pending_ += ',';
    pending_ += row;
    if (assertion) ++assertion_rows_;
  }
  std::string current_, pending_, cases_;
  std::set<std::string> identities_;
  std::vector<std::string> case_ids_;
  std::size_t assertion_rows_ = 0;
};
Observations observations;

std::size_t bytes_from_settings(const Settings &settings) {
  const auto units = settings.integer("history_memory_mb");
  require(units >= 0 && static_cast<std::uint64_t>(units) <=
      std::numeric_limits<std::size_t>::max() / mib, "Checked MiB conversion overflow");
  return static_cast<std::size_t>(units) * mib;
}
HistoryPolicy policy(const Settings &settings) {
  return {static_cast<std::size_t>(settings.integer("history_limit")), bytes_from_settings(settings)};
}
void apply(Application &application, std::uint64_t id, const Settings &settings) {
  const auto requested = policy(settings);
  application.set_history_policy(requested.step_limit, requested.pixel_budget, id);
}

// Exact frozen contract.literal_fixture recipes. The metadata is independent
// preservation context; it does not alter any declared pixel coefficient.
DocumentData fixture(bool large) {
  const int width = large ? 256 : 8, height = large ? 128 : 6, depth = large ? 1 : 2;
  Stack planes;
  for (int z = 0; z < depth; ++z) {
    Image image(width, height, 1, PixelType::UInt8);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
      image.at(x, y) = large ? 0 : 7*z + 3*y + x;
    planes.push_back(std::move(image));
  }
  SpatialCalibration calibration;
  calibration.spacing = {2,3,5}; calibration.origin = {11,-7,4}; calibration.unit = "um";
  DocumentData result{Dataset::from_planes({1,1,depth,height,width,1}, std::move(planes), calibration)};
  result.metadata.title = large ? "Owned history budget" : "Owned history steps";
  result.metadata.properties["fixture_recipe"] = std::string(large ? "zero;fill1..10" : "7*z+3*y+x;add1x10");
  result.metadata.extensions["future"] = metadata_array({true, std::int64_t(17)});
  result.selection = Selection("rectangle", {{1,1},{4,4}});
  result.previous_selection = Selection("line", {{0,0},{2,2}});
  Roi retained{"retained area",Selection("rectangle",{{1,1},{4,4}}),{},{},0,{}};
  retained.attributes["future.style"] = metadata_object({{"rgba",metadata_array({
      std::int64_t(1),std::int64_t(2),std::int64_t(3),std::int64_t(255)})}});
  result.rois.push_back(std::move(retained));
  return result;
}
struct Owned {
  Application application;
  std::shared_ptr<Document> document;
  std::uint64_t id;
  explicit Owned(bool large) : document(std::make_shared<Document>(fixture(large))),
                              id(application.adopt(document)) {}
};

void edits(Owned &owned, bool large, int first = 1, int last = 10) {
  for (int iteration = first; iteration <= last; ++iteration) {
    // These borrows end with each edit, before any exact storage observation.
    const auto source = owned.application.snapshot(owned.id);
    auto transaction = source.begin_edit();
    const auto shape = source.data().dataset.shape();
    for (int z = 0; z < shape.z; ++z) {
      Image replacement = *source.data().dataset.plane({0,0,z});
      for (auto &value : replacement.pixels) value = large ? iteration : value + 1;
      transaction.replace_plane({0,0,z}, std::move(replacement));
    }
    owned.application.commit(transaction);
  }
}
void exact_pixels(const Document &document, bool large, int iteration) {
  const auto source = document.snapshot(); const auto shape = source.data().dataset.shape();
  require(shape.dimensions() == (large ? std::array<int,6>{1,1,1,128,256,1} :
                                       std::array<int,6>{1,1,2,6,8,1}), "Frozen full dataset extent changed");
  for (int z = 0; z < shape.z; ++z) {
    const auto plane = source.data().dataset.plane({0,0,z});
    require(plane->type == PixelType::UInt8 && plane->channels == 1, "Frozen declared type/layout changed");
    for (int y = 0; y < shape.y; ++y) for (int x = 0; x < shape.x; ++x)
      require(plane->at(x,y) == (large ? iteration : 7*z + 3*y + x + iteration),
              "Independent full declared-pixel recipe differs");
  }
}
Value pixel_transport(const Document &document) {
  static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559,
                "Explicit F64LE transport requires IEEE binary64");
  constexpr char digits[] = "0123456789abcdef";
  std::string declared, storage;
  const auto source = document.snapshot(); const auto shape = source.data().dataset.shape();
  for (int t=0;t<shape.t;++t) for (int c=0;c<shape.c;++c) for (int z=0;z<shape.z;++z) {
    const auto plane = source.data().dataset.plane({t,c,z});
    require(plane->type == PixelType::UInt8, "Declared-byte transport requires actual UInt8");
    for (const double value : plane->pixels) {
      require(std::isfinite(value) && value >= 0 && value <= 255 && std::trunc(value) == value,
              "Actual UInt8 sample is not a canonical finite integer");
      const auto byte = static_cast<unsigned>(value);
      declared += digits[byte >> 4]; declared += digits[byte & 15];
      std::uint64_t bits = 0; std::memcpy(&bits, &value, sizeof(bits));
      for (int i=0;i<8;++i) {
        const auto item = static_cast<unsigned>((bits >> (8*i)) & 255);
        storage += digits[item >> 4]; storage += digits[item & 15];
      }
    }
  }
  Value::Array dimensions;
  for (int dimension : shape.dimensions()) dimensions.emplace_back(dimension);
  return Value::Object{{"pixel_type","uint8"},{"shape",std::move(dimensions)},
      {"order","TCZYXS"},{"hex",std::move(declared)},{"storage_f64le_hex",std::move(storage)}};
}
std::vector<double> pixels(const Dataset &dataset) {
  std::vector<double> result; const auto shape = dataset.shape();
  for (int t=0;t<shape.t;++t) for (int c=0;c<shape.c;++c) for (int z=0;z<shape.z;++z) {
    const auto plane = dataset.plane({t,c,z});
    result.insert(result.end(), plane->pixels.begin(), plane->pixels.end());
  }
  return result;
}
bool calibration_equal(const SpatialCalibration &a, const SpatialCalibration &b) {
  return a.spacing==b.spacing && a.origin==b.origin && a.direction==b.direction && a.unit==b.unit &&
      a.frame_interval==b.frame_interval && a.time_origin==b.time_origin && a.time_unit==b.time_unit;
}
bool display_equal(const DisplayState &a, const DisplayState &b) {
  return a.t==b.t && a.c==b.c && a.crosshair==b.crosshair && a.orthogonal==b.orthogonal &&
      a.linkedZoom==b.linkedZoom && a.zoom==b.zoom && a.planeZoom==b.planeZoom && a.pan==b.pan &&
      a.revision==b.revision && metadata_equal(a.properties,b.properties);
}
bool history_equal(const HistoryInfo &a, const HistoryInfo &b) {
  return a.undo_steps==b.undo_steps && a.redo_steps==b.redo_steps && a.step_limit==b.step_limit &&
      a.pixel_budget==b.pixel_budget && a.pixels.logical_bytes==b.pixels.logical_bytes &&
      a.pixels.current_bytes==b.pixels.current_bytes && a.pixels.retained_bytes==b.pixels.retained_bytes &&
      a.pixels.obsolete_bytes==b.pixels.obsolete_bytes && a.pixels.retained_planes==b.pixels.retained_planes &&
      a.pixels.exact==b.pixels.exact;
}
// Detached scalars/geometry/copies only: no snapshots, Dataset or plane aliases.
struct State {
  std::uint64_t id;
  std::optional<std::uint64_t> identity;
  Revisions revisions;
  bool dirty;
  DatasetShape shape;
  std::vector<double> samples;
  DocumentMetadata metadata;
  SpatialCalibration calibration;
  std::optional<Selection> selection, previous;
  std::vector<Roi> rois;
  DisplayState display;
  HistoryInfo history;
  explicit State(const Document &document) : id(document.id) {
    const auto source = document.snapshot(); const auto &data = source.data();
    identity=source.data_identity(); revisions=source.revisions(); dirty=source.dirty();
    shape=data.dataset.shape(); samples=pixels(data.dataset); metadata=data.metadata;
    calibration=data.dataset.calibration(); selection=data.selection; previous=data.previous_selection;
    rois=data.rois; display=source.display(); history=document.history();
    require(data.overlays.empty() && !data.spectrum, "Fixture unexpectedly acquired extra records");
  }
  bool same_data(const State &other) const {
    return id==other.id && identity==other.identity && revisions.matches(other.revisions,Change::All) &&
        dirty==other.dirty && shape.dimensions()==other.shape.dimensions() && samples==other.samples &&
        metadata==other.metadata && calibration_equal(calibration,other.calibration) &&
        selection==other.selection && previous==other.previous && rois==other.rois && display_equal(display,other.display);
  }
};
void policy_preservation(const char *requirement, const State &before, const Document &document) {
  const State after(document);
  require(before.same_data(after), "Policy prune changed pixels/identity/revisions/metadata/geometry/display/dirty");
  observations.scalar(requirement,"policy.pixel_revision_delta",after.revisions.pixels-before.revisions.pixels);
  observations.scalar(requirement,"policy.metadata_revision_delta",after.revisions.metadata-before.revisions.metadata);
  observations.scalar(requirement,"policy.selection_revision_delta",after.revisions.selection-before.revisions.selection);
  observations.scalar(requirement,"policy.dirty_changed",after.dirty!=before.dirty);
}
void settings_and_effect(const char *requirement, const char *phase, const Settings &settings,
                         const Document &document, std::size_t effect, bool memory) {
  const auto history = document.history();
  require(history.pixel_budget % mib == 0, "Actual policy is not whole-MiB for this fixture");
  const std::string prefix = phase;
  const auto put = [&](const char *suffix, Value value) {
    observations.scalar(requirement,(prefix+suffix).c_str(),std::move(value));
  };
  put(".setting",settings.integer(memory ? "history_memory_mb" : "history_limit"));
  put(".current",memory ? history.pixel_budget/mib : document.history_limit());
  put(".effective",memory ? history.pixel_budget : history.step_limit);
  put(".effect",effect);
}

void step_effects() {
  const Settings defaults;
  require(defaults.integer("history_limit")==8 && defaults.integer("history_memory_mb")==0,
          "Independent default8/0 policy");
  {
    Owned owned(false);
    require(owned.document->history_limit()==8 && owned.document->history_pixel_budget()==0,
            "Fresh native owner must inherit8/0 without an explicit policy write");
    edits(owned,false);
    const auto history=owned.document->history();
    require(history.undo_steps==8 && history.redo_steps==0 && history.step_limit==8 && history.pixel_budget==0,
            "Ten default changed edits must retain exactly8 undo states");
    exact_pixels(*owned.document,false,10);
    settings_and_effect(step,"default",defaults,*owned.document,history.undo_steps,false);
  }
  Settings changed; changed.set("history_limit",std::int64_t(3));
  Owned owned(false); apply(owned.application,owned.id,changed); edits(owned,false);
  auto history=owned.document->history();
  require(history.step_limit==3 && history.undo_steps==3 && history.redo_steps==0,
          "Changed step3 must retain exactly3 undo states");
  settings_and_effect(step,"changed",changed,*owned.document,history.undo_steps,false);
  exact_pixels(*owned.document,false,10);
  const State before(*owned.document);
  owned.application.set_history_policy(2,0,owned.id);
  policy_preservation(step,before,*owned.document);
  history=owned.document->history();
  require(history.undo_steps==2 && history.step_limit==2, "Step2 prune must be immediate");
  observations.scalar(step,"pruned_two.undo_steps",history.undo_steps);
  observations.bytes(step,"policy.all_pixels",pixel_transport(*owned.document));
  owned.application.undo(owned.id); exact_pixels(*owned.document,false,9);
  const auto first=owned.document->snapshot().data().dataset.plane({0,0,0})->at(0,0);
  require(first==9 && std::trunc(first)==first,"Undo after prune must restore first pixel9");
  observations.scalar(step,"pruned_two.undo_first_pixel",static_cast<std::int64_t>(first));
  observations.bytes(step,"pruned_two.undo_all_pixels",pixel_transport(*owned.document));
}

void budget_effects() {
  Settings defaults; defaults.set("history_limit",std::int64_t(128));
  {
    Owned owned(true);
    require(owned.document->history_pixel_budget()==0,"Fresh native byte cap must be zero");
    apply(owned.application,owned.id,defaults); edits(owned,true);
    const auto history=owned.document->history();
    require(history.undo_steps==10 && history.pixel_budget==0 && history.pixels.exact &&
        history.pixels.current_bytes==plane_bytes && history.pixels.obsolete_bytes==2621440,
        "Unbounded bytes/step128 must retain ten actual262144-byte obsolete buffers");
    exact_pixels(*owned.document,true,10);
    settings_and_effect(budget,"default",defaults,*owned.document,history.pixels.obsolete_bytes,true);
  }
  Settings changed=defaults; changed.set("history_memory_mb",std::int64_t(1));
  Owned owned(true); apply(owned.application,owned.id,defaults); edits(owned,true);
  const State before(*owned.document);
  apply(owned.application,owned.id,changed);
  policy_preservation(budget,before,*owned.document);
  auto history=owned.document->history();
  require(history.undo_steps==4 && history.redo_steps==0 && history.pixel_budget==mib &&
      history.pixels.exact && history.pixels.current_bytes==plane_bytes &&
      history.pixels.retained_planes==5 && history.pixels.obsolete_bytes==mib,
      "One MiB must immediately retain exactly four obsolete eager planes");
  exact_pixels(*owned.document,true,10);
  settings_and_effect(budget,"changed",changed,*owned.document,history.pixels.obsolete_bytes,true);
  observations.scalar(budget,"one_mib.obsolete_buffer_count",history.pixels.retained_planes-1);
  observations.scalar(budget,"one_mib.pixel_bytes",history.pixels.obsolete_bytes);
  observations.bytes(budget,"policy.all_pixels",pixel_transport(*owned.document));
  owned.application.set_history_policy(2,mib,owned.id);
  history=owned.document->history();
  require(history.undo_steps==2 && history.pixels.obsolete_bytes==524288,"Step2 must retain two obsolete buffers");
  observations.scalar(budget,"step_two.pixel_bytes",history.pixels.obsolete_bytes);
  owned.application.set_history_policy(2,0,owned.id); edits(owned,true,11,13);
  history=owned.document->history();
  const bool zero_cap=history.pixel_budget==0 && history.step_limit==2 && history.undo_steps==2 &&
      history.pixels.obsolete_bytes==524288;
  require(zero_cap,"Byte cap zero must retain the independent positive step cap");
  observations.scalar(budget,"zero_budget.step_cap_applies",zero_cap);

  // Separate discriminator: metadata history aliases the same current plane.
  // It does not change the frozen two-step byte scenario above.
  Owned dedup(true); apply(dedup.application,dedup.id,changed); edits(dedup,true);
  for (int i=1;i<=2;++i) {
    auto tx=dedup.application.begin_edit(dedup.id);
    tx.set_property("metadata_only",std::int64_t(i)); dedup.application.commit(tx);
  }
  const auto aliased=dedup.document->history();
  require(aliased.undo_steps==6 && aliased.pixels.retained_planes==5 &&
      aliased.pixels.obsolete_bytes==mib && aliased.pixels.current_bytes==plane_bytes && aliased.pixels.exact,
      "Metadata-only history must deduplicate actual shared eager plane buffers");
  observations.auxiliary("deduplicated_metadata_history",Value::Object{{"undo_steps",aliased.undo_steps},
      {"retained_planes",aliased.pixels.retained_planes},{"obsolete_bytes",aliased.pixels.obsolete_bytes},
      {"current_bytes",aliased.pixels.current_bytes},{"exact",aliased.pixels.exact}});
}

SessionLimits session_limits() {
  SessionLimits limits;
  limits.max_documents=4; limits.max_planes=8; limits.max_samples=1024*1024;
  limits.max_json_bytes=16*mib; limits.max_encoded_bytes=16*mib; limits.max_working_bytes=128*mib;
  return limits;
}
void bounded_file(const fs::path &path) {
  require(fs::is_regular_file(path) && !fs::is_symlink(path) && fs::file_size(path)<=16*mib,
          "Owned persisted input must be a regular file at most16MiB");
}
void persisted_consumer(const fs::path &work, bool large) {
  const auto requirement=large ? budget : step;
  PreferencesDocument preferences;
  preferences.settings.set("history_limit",std::int64_t(large ? 128 : 3));
  preferences.settings.set("history_memory_mb",std::int64_t(large ? 1 : 0));
  const auto preference_path=work/(large ? "budget-preferences.json" : "step-preferences.json");
  require(preferences.save(preference_path.u8string()),"Persist independent consumer Preferences");
  bounded_file(preference_path);
  const auto reopened=PreferencesDocument::load(preference_path.u8string(),false);
  require(reopened.settings.integer("history_limit")== (large ? 128 : 3) &&
      reopened.settings.integer("history_memory_mb")== (large ? 1 : 0) && !reopened.write_blocked,
      "Fresh strict Preferences reopen lost the actual policy");
  Owned owned(large); apply(owned.application,owned.id,reopened.settings); edits(owned,large);
  exact_pixels(*owned.document,large,10);
  const auto history=owned.document->history();
  require(large ? history.pixels.obsolete_bytes==mib : history.undo_steps==3,
          "Persisted policy does not reproduce the same ten-edit effect");
  settings_and_effect(requirement,"persisted",reopened.settings,*owned.document,
      large ? history.pixels.obsolete_bytes : history.undo_steps,large);
  observations.scalar(requirement,"persisted.reopened_setting",
      reopened.settings.integer(large ? "history_memory_mb" : "history_limit"));

  const auto session_path=work/(large ? "budget-session.ijx" : "step-session.ijx");
  {
    SessionCapture capture; capture.documents.push_back(owned.document->snapshot());
    save_session(session_path.u8string(),capture,session_limits());
  }
  bounded_file(session_path);
  auto decoded=load_session(session_path.u8string(),session_limits());
  require(decoded.documents.size()==1 && decoded.source_version==3,"Fresh session must decode one nativeV3 owner");
  auto incoming=decoded.documents.front();
  require(!incoming->snapshot().dirty() && incoming->undo_steps()==0 && incoming->redo_steps()==0 &&
      incoming->history_limit()==8 && incoming->history_pixel_budget()==0,
      "Session must yield a clean empty-history owner without restoring history policy");
  exact_pixels(*incoming,large,10);
  Application application;
  {
    auto adoption=application.adopt_all_with_history_policy({{incoming,std::nullopt}},policy(reopened.settings));
    require(adoption.document_ids==std::vector<std::uint64_t>{incoming->id},"Actual reopened owner adoption identity");
  }
  const auto actual=application.history(incoming->id);
  require(actual.step_limit==(large ? 128u : 3u) && actual.pixel_budget==(large ? mib : 0) &&
      actual.undo_steps==0 && actual.redo_steps==0,"Reopened adoption must install policy with empty history");
  observations.scalar(requirement,"persisted.reopened_undo_steps",actual.undo_steps);
  observations.scalar(requirement,"persisted.reopened_redo_steps",actual.redo_steps);
  observations.auxiliary(large ? "budget_reopened_owner" : "step_reopened_owner",Value::Object{
      {"document_id",incoming->id},{"limit",actual.step_limit},{"pixel_budget",actual.pixel_budget},
      {"undo_steps",actual.undo_steps},{"redo_steps",actual.redo_steps},{"dirty",incoming->snapshot().dirty()}});
}
void persist_reopen_adopt(const fs::path &work) {
  persisted_consumer(work,false); persisted_consumer(work,true);
  PreferencesDocument pair;
  pair.settings.set("history_limit",std::int64_t(3)); pair.settings.set("history_memory_mb",std::int64_t(1));
  const auto path=work/"pair-preferences.json";
  require(pair.save(path.u8string()),"Persist additional literal3/1 pair"); bounded_file(path);
  const auto reopened=PreferencesDocument::load(path.u8string(),false);
  auto left=load_session((work/"step-session.ijx").u8string(),session_limits());
  auto right=load_session((work/"budget-session.ijx").u8string(),session_limits());
  auto fresh=std::make_shared<Document>(fixture(false));
  Application application;
  {
    auto adoption=application.adopt_all_with_history_policy(
        {{fresh,std::nullopt},{left.documents.at(0),std::nullopt},{right.documents.at(0),std::nullopt}},
        policy(reopened.settings));
    require(adoption.document_ids==std::vector<std::uint64_t>{fresh->id,left.documents[0]->id,right.documents[0]->id},
            "New and session-reopened batch must retain exact membership order");
  }
  require(application.document_ids().size()==3,"Additional pair must adopt exactly three distinct owners");
  for (const auto id : application.document_ids()) {
    const auto history=application.history(id);
    require(history.step_limit==3 && history.pixel_budget==mib && history.undo_steps==0 && history.redo_steps==0 &&
        !application.snapshot(id).dirty(),"Additional3/1 pair must reach every fresh/reopened owner");
  }
  const auto step_history=application.history(left.documents[0]->id);
  const auto budget_history=application.history(right.documents[0]->id);
  observations.scalar(step,"adopted_pair.limit",step_history.step_limit);
  observations.scalar(step,"adopted_pair.pixel_budget_bytes",step_history.pixel_budget);
  observations.scalar(budget,"adopted_pair.limit",budget_history.step_limit);
  observations.scalar(budget,"adopted_pair.pixel_budget_bytes",budget_history.pixel_budget);
}

void detached_settings() {
  Owned left(false), right(true); edits(left,false,1,3); edits(right,true,1,3);
  const State left_before(*left.document), right_before(*right.document);
  Settings detached;
  detached.set("history_limit",std::int64_t(2)); detached.set("history_memory_mb",std::int64_t(1));
  const State left_after(*left.document), right_after(*right.document);
  std::size_t effects=0;
  effects += !left_before.same_data(left_after) || !history_equal(left_before.history,left_after.history);
  effects += !right_before.same_data(right_after) || !history_equal(right_before.history,right_after.history);
  require(detached.integer("history_limit")==2 && detached.integer("history_memory_mb")==1 && effects==0,
          "Detached Settings must not change any native owner without explicit policy application");
  observations.scalar(step,"detached_settings.owner_effect_count",effects);
  observations.scalar(budget,"detached_settings.owner_effect_count",effects);
  observations.auxiliary("detached_actual_policies",Value::Object{
      {"left_limit",left_after.history.step_limit},{"right_limit",right_after.history.step_limit},
      {"left_pixel_budget",left_after.history.pixel_budget},{"right_pixel_budget",right_after.history.pixel_budget},
      {"left_undo_steps",left_after.history.undo_steps},{"right_undo_steps",right_after.history.undo_steps}});
}
template<class F> void marker(const char *id,F function) {
  observations.begin(id); function(); observations.finish(); std::cout<<id<<'\n';
}
} // namespace

int main(int argc,char **argv) {
  try {
    require(argc==2,"Usage: imagejxx_history_settings_example FRESH_DIRECTORY");
    const auto work=fs::absolute(fs::u8path(argv[1])).lexically_normal();
    require(work.has_filename() && fs::is_directory(work.parent_path()),"Existing parent and absent owned directory required");
    for (auto parent=work.parent_path();!parent.empty();) {
      require(!fs::is_symlink(fs::symlink_status(parent)),"Portable symlink parent refused");
      const auto next=parent.parent_path(); if (next==parent) break; parent=next;
    }
    std::error_code error; const auto status=fs::symlink_status(work,error);
    require(status.type()==fs::file_type::not_found && (!error || error==std::errc::no_such_file_or_directory),
            "Owned work directory must be absent");
    require(fs::create_directory(work),"Cannot exclusively create owned work directory");
    const auto start=std::chrono::steady_clock::now();
    marker("HISTORYSDK01.step_policy_effects",step_effects);
    marker("HISTORYSDK02.pixel_budget_effects",budget_effects);
    marker("HISTORYSDK03.persist_reopen_adopt",[&]{persist_reopen_adopt(work);});
    marker("HISTORYSDK04.detached_settings_no_owner_effects",detached_settings);
    require(std::chrono::steady_clock::now()-start<=std::chrono::seconds(30),"History SDK workload exceeded30 seconds");
    observations.write(work);
    std::cout<<"4 history SDK markers / "<<checks<<" checks\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr<<error.what()<<'\n'; return 1;
  }
}
