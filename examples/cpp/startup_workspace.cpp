// Installed core SDK consumer: public native headers and the C++ standard
// library only. Expected pixels, precedence and failure effects are literals.
#include <imagejxx/application.hpp>
#include <imagejxx/session.hpp>
#include <imagejxx/startup.hpp>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

namespace {
using namespace imagejxx;
namespace fs = std::filesystem;
std::size_t checks = 0;
constexpr std::size_t file_limit = 4 * 1024 * 1024;
void require(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
std::string quote(const std::string &text) {
  constexpr char digits[] = "0123456789abcdef";
  std::string out = "\"";
  for (unsigned char c : text) {
    if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
    else if (c < 32) { out += "\\u00"; out += digits[c >> 4]; out += digits[c & 15]; }
    else out += char(c);
  }
  return out + '"';
}
std::string hex(const std::string &bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  require(bytes.size() <= file_limit, "Observed file exceeds4MiB");
  for (unsigned char c : bytes) { result += digits[c >> 4]; result += digits[c & 15]; }
  return result;
}
template<class Values> std::string numbers(const Values &values) {
  std::ostringstream stream;
  stream.precision(17); stream << '[';
  bool first = true;
  for (const auto value : values) { if (!first) stream << ','; stream << value; first = false; }
  return stream.str() + ']';
}
std::string strings(const std::vector<std::string> &values) {
  std::string out = "[";
  for (const auto &value : values) { if (out.size() > 1) out += ','; out += quote(value); }
  return out + ']';
}
struct Observations {
  std::map<std::string, std::map<std::string, std::string>> cases;
  void set(const std::string &id, const std::string &key, std::string json) {
    require(cases[id].emplace(key, std::move(json)).second, "Duplicate actual observation");
  }
  void write(const fs::path &work) const {
    require(cases.size() == 6, "Exactly six SDK markers required");
    std::string out = "{\"schema_version\":1,\"producer_id\":\"p06j.startup.sdk.raw.001\",\"complete\":true,\"cases\":[";
    bool first_case = true;
    for (const auto &[id, rows] : cases) {
      if (!first_case) out += ',';
      first_case = false;
      out += "{\"id\":" + quote(id) + ",\"observations\":[";
      bool first_row = true;
      for (const auto &[key, value] : rows) {
        if (!first_row) out += ',';
        first_row = false;
        out += "{\"id\":" + quote(key) + ",\"encoding\":\"canonical_json\",\"value\":" + value + '}';
      }
      out += "]}";
    }
    out += "]}\n";
    require(out.size() <= 16 * 1024 * 1024, "SDK JSON exceeds16MiB");
    auto *file = std::fopen((work / "startup-observations.json").string().c_str(), "wbx");
    require(file != nullptr, "SDK output must be exclusively created");
    const bool written = std::fwrite(out.data(), 1, out.size(), file) == out.size();
    const bool flushed = std::fflush(file) == 0;
    const bool closed = std::fclose(file) == 0;
    require(written && flushed && closed, "Cannot write complete actual SDK JSON");
  }
} observed;
std::string read(const fs::path &path) {
  require(fs::is_regular_file(path) && fs::file_size(path) <= file_limit, "Bounded regular file required");
  std::ifstream stream(path, std::ios::binary);
  std::string bytes((std::istreambuf_iterator<char>(stream)), {});
  require(stream.good() || stream.eof(), "Cannot read owned file");
  return bytes;
}
StartupRequest request(const fs::path &work) {
  StartupRequest value;
  value.launch_cwd = work.u8string();
  value.preferences_write_path = (work / "preferences.json").u8string();
  return value;
}
SessionLimits limits() {
  SessionLimits value;
  value.max_documents = 4; value.max_planes = 14; value.max_samples = 672;
  value.max_json_bytes = value.max_encoded_bytes = file_limit;
  value.max_working_bytes = 32 * 1024 * 1024;
  return value;
}
class CountedUnreadSource final : public PlaneSource {
public:
  DatasetShape shape() const override { return {1,1,2,6,8,1}; }
  PixelType pixel_type() const override { return PixelType::UInt8; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++plane_reads; throw std::runtime_error("Planner acquired a provider plane");
  }
  Image read_region(PlanePosition,int,int,int,int) const override {
    ++region_reads; throw std::runtime_error("Planner acquired a provider region");
  }
  mutable std::size_t plane_reads=0,region_reads=0;
};
// One owned legacy V2 JSON payload framed with a single uncompressed DEFLATE
// block. Standard-library byte construction adds no zlib consumer dependency.
std::vector<unsigned char> legacy_v2() {
  const std::string text=R"({"format":"imagejxx-session","version":2,"documents":[{"title":"Owned V2 first","is_stack":false,"dtype":"uint8","shape":[1,1],"pixels":"AAAAAAAA8D8=","slice_index":0},{"title":"Owned V2 last","is_stack":false,"dtype":"uint8","shape":[1,1],"pixels":"AAAAAAAAAEA=","slice_index":0}],"results":[],"workflow":[]})";
  require(text.size()<65536,"Owned V2 stored block bound");
  std::vector<unsigned char> result{'I','M','A','G','E','J','X','X','2','\n'};
  for(int shift=24;shift>=0;shift-=8)result.push_back(static_cast<unsigned char>(text.size()>>shift));
  result.insert(result.end(),{0x78,0x01,0x01});
  const auto length=static_cast<unsigned>(text.size());
  result.push_back(static_cast<unsigned char>(length));result.push_back(static_cast<unsigned char>(length>>8));
  result.push_back(static_cast<unsigned char>(~length));result.push_back(static_cast<unsigned char>((~length)>>8));
  result.insert(result.end(),text.begin(),text.end());
  std::uint32_t a=1,b=0;
  for(unsigned char c:text){a=(a+c)%65521;b=(b+a)%65521;}
  const auto adler=(b<<16)|a;
  for(int shift=24;shift>=0;shift-=8)result.push_back(static_cast<unsigned char>(adler>>shift));
  return result;
}
SessionCapture fixture() {
  std::vector<DocumentSnapshot> snapshots;
  for (int scientific = 0; scientific != 2; ++scientific) {
    const DatasetShape shape = scientific ? DatasetShape{2,3,2,6,8,1} : DatasetShape{1,1,2,6,8,1};
    Stack planes;
    for (int t = 0; t < shape.t; ++t) for (int c = 0; c < shape.c; ++c) for (int z = 0; z < 2; ++z) {
      Image image(8, 6, 1, scientific ? PixelType::UInt16 : PixelType::UInt8);
      for (int y = 0; y < 6; ++y) for (int x = 0; x < 8; ++x)
        image.at(x,y) = scientific ? 40*t+30*c+5*z+2*y+x : 7*z+3*y+x;
      planes.push_back(std::move(image));
    }
    SpatialCalibration calibration;
    calibration.spacing = {1.25,.5,2.5}; calibration.unit = "um";
    DocumentData data{Dataset::from_planes(shape, std::move(planes), calibration)};
    data.metadata.title = scientific ? "Owned U16 scientific" : "Owned U8 stack";
    data.metadata.properties["opaque"] = metadata_object({{"future", true}, {"ordinal", std::int64_t(9)}});
    data.selection = Selection("rectangle", {{1,1},{5,4}});
    data.previous_selection = Selection("line", {{0,0},{7,5}});
    data.rois.push_back({"positioned", Selection("rectangle", {{2,1},{6,5}}),
                        scientific ? 1 : 0, scientific ? 2 : 0, 1, {{"retained", true}}});
    data.overlays.push_back({{"type", std::string("text")}, {"text", std::string("inert annotation")}});
    auto owner = std::make_shared<Document>(std::move(data));
    auto display = owner->snapshot().display();
    display.t = scientific ? 1 : 0; display.c = scientific ? 2 : 0;
    display.crosshair = {3,2,1}; display.zoom = 2.25; display.orthogonal = scientific;
    display.linkedZoom = false; display.planeZoom = {1.5,2.5,3.5};
    display.pan = {{{.25,-.5},{1.25,-1.5},{2.25,-2.5}}};
    owner->set_display(display);
    snapshots.push_back(owner->snapshot());
  }
  auto extensions = with_workspace_presentation({{"vendor", std::string("retained")}}, 0, 2).extensions;
  return {std::move(snapshots), {metadata_object({{"value", std::int64_t(17)}})},
          {metadata_object({{"operation", std::string("never_execute")}, {"historical_id", std::int64_t(777)}})},
          std::move(extensions)};
}
void SU01(const fs::path &work) {
  const auto provider=std::make_shared<CountedUnreadSource>();
  Document untouched{Dataset(provider)};
  const auto before=untouched.snapshot();
  PreferencesDocument preferences;
  preferences.extras_json = R"({"recent":["deliberately-missing.ijx"]})";
  const auto plan = plan_startup(preferences, request(work));
  const auto empty = with_workspace_presentation({}, {}, 0);
  const auto presentation = workspace_presentation(empty.extensions, 0);
  require(plan.source == StartupSource::Empty && plan.paths.empty() && !presentation.active_index,
          "Defaults/Recent/empty workspace must select no owner");
  require(provider->plane_reads==0 && provider->region_reads==0 &&
          untouched.snapshot().shares_data(before) &&
          untouched.snapshot().revisions().matches(before.revisions(),Change::All),
          "Pure planner read samples or changed unrelated owner");
  observed.set("SU01", "plan.source", std::to_string(static_cast<int>(plan.source)));
  observed.set("SU01", "plan.paths", strings(plan.paths));
  observed.set("SU01", "workspace.active_index", presentation.active_index ? std::to_string(*presentation.active_index) : "null");
  observed.set("SU01", "preferences.extras_json", quote(preferences.extras_json));
  observed.set("SU01", "native.provider_plane_reads", std::to_string(provider->plane_reads));
  observed.set("SU01", "native.provider_region_reads", std::to_string(provider->region_reads));
  observed.set("SU01", "native.document_revision_delta", std::to_string(untouched.snapshot().revisions().pixels-before.revisions().pixels));
}
void SU03(const fs::path &work) {
  const auto capture = fixture();
  const auto path = work / "workspace.ijx";
  save_session(path.u8string(), capture, limits());
  const auto decoded = load_session(path.u8string(), limits());
  const auto presentation = workspace_presentation(decoded.extensions, decoded.documents.size());
  Application app;
  std::vector<DocumentAdoption> incoming;
  for (const auto &owner : decoded.documents) incoming.push_back({owner,{}});
  const auto adopted = app.adopt_all_with_history_policy(std::move(incoming), HistoryPolicy{3,1048576},
                                                        IncomingDocumentActivation{*presentation.active_index});
  require(app.active_document_id() == adopted.document_ids.front() && adopted.document_ids.size() == 2,
          "First owner must be active without reordering");
  for (std::size_t index = 0; index < decoded.documents.size(); ++index) {
    const auto snapshot = decoded.documents[index]->snapshot();
    const auto &data = snapshot.data();
    const auto shape = data.dataset.shape();
    std::vector<double> actual;
    for (int t=0;t<shape.t;++t) for (int c=0;c<shape.c;++c) for (int z=0;z<shape.z;++z)
      for (int y=0;y<6;++y) for (int x=0;x<8;++x) {
        const double value = data.dataset.plane({t,c,z})->at(x,y);
        require(value == (index ? 40*t+30*c+5*z+2*y+x : 7*z+3*y+x), "Owned TCZYXS literal mismatch");
        actual.push_back(value);
      }
    const auto revisions = snapshot.revisions();
    require(snapshot.id() != capture.documents[index].id() && !snapshot.dirty() &&
            revisions.pixels == 0 && revisions.metadata == 0 && revisions.selection == 0 && revisions.display == 0 &&
            decoded.documents[index]->undo_steps() == 0 && decoded.documents[index]->redo_steps() == 0,
            "Restored owner is not fresh/clean/zero revision/history");
    require(data.dataset.calibration().spacing == std::array<double,3>{1.25,.5,2.5} &&
            data.selection && data.previous_selection && data.rois.size() == 1 && data.overlays.size() == 1 &&
            metadata_equal(data.metadata.properties, capture.documents[index].data().metadata.properties),
            "Workspace lost calibration, positioned ROI or opaque state");
    observed.set("SU03", "document."+std::to_string(index)+".samples", numbers(actual));
    observed.set("SU03", "document."+std::to_string(index)+".shape", numbers(shape.dimensions()));
    observed.set("SU03", "document."+std::to_string(index)+".id", std::to_string(snapshot.id()));
    observed.set("SU03", "document."+std::to_string(index)+".revisions", numbers(std::array<std::uint64_t,4>{revisions.pixels,revisions.metadata,revisions.selection,revisions.display}));
    observed.set("SU03", "document."+std::to_string(index)+".dirty", snapshot.dirty()?"true":"false");
    const auto history=decoded.documents[index]->history();
    observed.set("SU03", "document."+std::to_string(index)+".history", numbers(std::array<std::size_t,4>{history.undo_steps,history.redo_steps,history.step_limit,history.pixel_budget}));
  }
  const auto scientific = decoded.documents[1]->snapshot();
  require(scientific.display().t == 1 && scientific.display().c == 2 && scientific.display().crosshair[2] == 1 &&
          scientific.data().dataset.plane({1,2,1})->at(3,2) == 112 && decoded.workflow.size() == 1 && decoded.results.size() == 1,
          "Scientific view or inert session tables differ");
  observed.set("SU03", "active_index", std::to_string(*presentation.active_index));
  observed.set("SU03", "document_ids", numbers(app.document_ids()));
  observed.set("SU03", "source.file_hex", quote(hex(read(path))));
  observed.set("SU03", "scientific.tcz", numbers(std::array<int,3>{scientific.display().t,scientific.display().c,scientific.display().crosshair[2]}));
  observed.set("SU03", "scientific.current_sample", std::to_string(scientific.data().dataset.plane({1,2,1})->at(3,2)));
  observed.set("SU03", "scientific.calibration_spacing", numbers(scientific.data().dataset.calibration().spacing));
  observed.set("SU03", "scientific.camera_zoom", std::to_string(scientific.display().zoom));
  observed.set("SU03", "scientific.plane_zoom", numbers(scientific.display().planeZoom));
  const auto &roi=scientific.data().rois.at(0);
  observed.set("SU03", "scientific.positioned_roi_tcz", numbers(std::array<int,3>{*roi.t,*roi.c,*roi.z}));
}
void SU05(const fs::path &work) {
  PreferencesDocument preferences;
  const auto prefs = work / "atomic-preferences.json";
  preferences.save(prefs.u8string());
  const auto before = read(prefs);
  const auto source_before=read(work/"workspace.ijx");
  Application app;
  bool failed = false;
  try {
    std::vector<std::shared_ptr<Document>> candidates;
    for (const auto &path : {work / "workspace.ijx", work / "missing.ijx", work / "workspace.ijx"}) {
      auto session = load_session(path.u8string(), limits());
      candidates.insert(candidates.end(), session.documents.begin(), session.documents.end());
    }
    app.adopt_all(std::move(candidates), IncomingDocumentActivation{0});
  } catch (const std::exception &) { failed = true; }
  require(failed && app.document_ids().empty() && app.active_document_id() == 0 && read(prefs) == before,
          "Good/missing/good staged consumer published prefix or changed preferences");
  observed.set("SU05", "failure.observed", failed ? "true" : "false");
  observed.set("SU05", "publication.document_ids", numbers(app.document_ids()));
  observed.set("SU05", "publication.active_id", std::to_string(app.active_document_id()));
  observed.set("SU05", "preferences.before_hex", quote(hex(before)));
  observed.set("SU05", "preferences.after_hex", quote(hex(read(prefs))));
  observed.set("SU05", "source.before_hex", quote(hex(source_before)));
  observed.set("SU05", "source.after_hex", quote(hex(read(work/"workspace.ijx"))));
  auto retained=load_session((work/"workspace.ijx").u8string(),limits()).documents;
  bool reject=false;
  const auto lease=retained[1]->constrain([&](const DocumentData &,const DisplayState &) {
    if(reject)throw std::invalid_argument("Owned second retained validator refusal");
  });
  reject=true;bool late_refused=false;
  try { app.adopt_all_with_history_policy({{retained[0],{}},{retained[1],{}}},HistoryPolicy{3,1048576},IncomingDocumentActivation{0}); }
  catch(const std::exception &){late_refused=true;}
  require(late_refused && app.document_ids().empty() && app.active_document_id()==0 &&
          retained[0]->history().step_limit==8 && retained[1]->history().step_limit==8,
          "Second retained validator published prefix or first history policy");
  observed.set("SU05", "retained_validator.refused",late_refused?"true":"false");
  observed.set("SU05", "retained_validator.document_ids",numbers(app.document_ids()));
  observed.set("SU05", "retained_validator.active_id",std::to_string(app.active_document_id()));
}
void SU07(const fs::path &) {
  const auto v2=decode_session(legacy_v2(),limits());
  const auto v2_presentation=workspace_presentation(v2.extensions,v2.documents.size());
  require(v2.source_version==2 && v2.documents.size()==2 && v2_presentation.active_index==1 &&
          v2.documents[0]->snapshot().data().dataset.plane()->at(0,0)==1 &&
          v2.documents[1]->snapshot().data().dataset.plane()->at(0,0)==2,"Owned V2 migration/fallback literal differs");
  const auto missing = workspace_presentation({}, 2);
  const auto known = workspace_presentation(with_workspace_presentation({}, 0, 2).extensions, 2);
  MetadataObject future{{"imagejxx_workspace", metadata_object({{"version",std::int64_t(23)}, {"opaque",true}})}};
  const auto next = workspace_presentation(future, 2);
  const auto preserved = with_workspace_presentation(future, 0, 2);
  bool refused = false;
  try { workspace_presentation(with_workspace_presentation({}, 0, 2).extensions, 0); }
  catch (const StartupError &) { refused = true; }
  require(missing.active_index == 1 && known.active_index == 0 && next.active_index == 1 && next.future_owned &&
          metadata_equal(future, preserved.extensions) && refused, "Presentation compatibility policy differs");
  observed.set("SU07", "missing.active_index", std::to_string(*missing.active_index));
  observed.set("SU07", "known.active_index", std::to_string(*known.active_index));
  observed.set("SU07", "future.active_index", std::to_string(*next.active_index));
  observed.set("SU07", "future.diagnostic_reason", quote(startup_reason_name(next.diagnostics.at(0).reason)));
  observed.set("SU07", "future.preserved", metadata_equal(future,preserved.extensions) ? "true" : "false");
  observed.set("SU07", "known.invalid.refused", refused ? "true" : "false");
  observed.set("SU07", "v2.source_version", std::to_string(v2.source_version));
  observed.set("SU07", "v2.active_index", std::to_string(*v2_presentation.active_index));
  observed.set("SU07", "v2.samples", numbers(std::array<double,2>{v2.documents[0]->snapshot().data().dataset.plane()->at(0,0),v2.documents[1]->snapshot().data().dataset.plane()->at(0,0)}));
}
void SU08(const fs::path &work) {
  PreferencesDocument preferences;
  preferences.settings.update({{"startup_mode",std::string("workspace")},{"startup_workspace",std::string("workspace.ijx")}});
  const auto old = preferences.json();
  bool refused = false;
  try { preferences.settings.update({{"startup_workspace",std::string("")},{"shortcut_bindings",std::vector<std::string>{"analysis.measure=Ctrl+K"}}}); }
  catch (const std::exception &) { refused = true; }
  require(refused && preferences.json() == old, "Compound startup/shortcut validation was not atomic");
  const auto path = work / "settings-export.json";
  preferences.save(path.u8string());
  const auto reopened = PreferencesDocument::load(path.u8string(), false);
  const auto plan = plan_startup(reopened, request(work));
  require(plan.source == StartupSource::PersistedWorkspace && plan.paths == std::vector<std::string>{(work/"workspace.ijx").u8string()},
          "Persisted startup pair was not consumed by core planner");
  observed.set("SU08", "compound.refused", refused ? "true" : "false");
  observed.set("SU08", "preferences.before_json", quote(old));
  observed.set("SU08", "preferences.after_json", quote(preferences.json()));
  observed.set("SU08", "plan.paths", strings(plan.paths));
  observed.set("SU08", "persistence.file_hex", quote(hex(read(path))));
  const auto blocked=work/"blocked-parent";
  {std::ofstream stream(blocked,std::ios::binary);stream<<"owned blocking file";}
  auto candidate=preferences;
  candidate.settings.update({{"startup_workspace",std::string("new.ijx")}});
  bool failed=false;
  try{candidate.save((blocked/"preferences.json").u8string());}catch(const std::exception &){failed=true;}
  require(failed && preferences.json()==old && read(blocked)=="owned blocking file",
          "Failed destination mutated applied detached configuration or owned file");
  observed.set("SU08", "failed_destination.refused",failed?"true":"false");
  observed.set("SU08", "failed_destination.applied_json",quote(preferences.json()));
}
void SU09(const fs::path &work) {
  const auto path = work / "malformed-preferences.json";
  { std::ofstream stream(path, std::ios::binary); stream << "{broken\0original"; }
  const auto before = read(path);
  auto recovered = PreferencesDocument::load(path.u8string());
  const auto plan = plan_startup(recovered, request(work));
  require(recovered.write_blocked && plan.source == StartupSource::Empty && !recovered.save(path.u8string()) && read(path) == before,
          "Malformed preferences did not preserve original/write block safe empty");
  auto retained = PreferencesDocument::parse(R"({"format":"imagejxx-preferences","version":1,"values":{"startup_mode":"workspace","startup_workspace":"workspace.ijx","shortcut_bindings":["analysis.measure=Ctrl+K"],"vendor":{"opaque":true}},"extras":{"recent":["inert"],"layout":"[Window][Owned]"},"vendor_root":17})",false);
  const auto extras = retained.extras_json;
  const auto shortcuts = retained.settings.strings("shortcut_bindings");
  retained.settings.reset("Startup");
  require(retained.settings.text("startup_mode") == "empty" && retained.settings.text("startup_workspace").empty() &&
          retained.extras_json == extras && retained.settings.strings("shortcut_bindings") == shortcuts,
          "Reset Startup changed unrelated settings/extras");
  observed.set("SU09", "recovery.write_blocked", recovered.write_blocked ? "true" : "false");
  observed.set("SU09", "recovery.original_before_hex", quote(hex(before)));
  observed.set("SU09", "recovery.original_after_hex", quote(hex(read(path))));
  observed.set("SU09", "reset.extras_json", quote(retained.extras_json));
  observed.set("SU09", "reset.shortcut_bindings", strings(retained.settings.strings("shortcut_bindings")));
  observed.set("SU09", "reset.unknown_values_json", quote(retained.unknown_values_json));
}
}
int main(int argc, char **argv) {
  try {
    require(argc == 2, "Usage: imagejxx_startup_workspace_example FRESH_DIRECTORY");
    const auto work = fs::absolute(fs::u8path(argv[1])).lexically_normal();
    require(work.has_filename() && fs::is_directory(work.parent_path()) && !fs::exists(work), "Existing parent and fresh directory required");
    for (auto parent = work.parent_path(); !parent.empty();) {
      require(!fs::is_symlink(fs::symlink_status(parent)), "Symlink parent refused");
      const auto next = parent.parent_path(); if (next == parent) break; parent = next;
    }
    require(fs::create_directory(work), "Cannot exclusively create SDK work directory");
    const auto start = std::chrono::steady_clock::now();
    SU01(work); std::cout << "SU01\n";
    SU03(work); std::cout << "SU03\n";
    SU05(work); std::cout << "SU05\n";
    SU07(work); std::cout << "SU07\n";
    SU08(work); std::cout << "SU08\n";
    SU09(work); std::cout << "SU09\n";
    require(std::chrono::steady_clock::now()-start <= std::chrono::seconds(30), "SDK workload exceeds30seconds");
    observed.write(work);
    std::cout << "6 startup SDK markers / " << checks << " checks\n";
    return 0;
  } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
