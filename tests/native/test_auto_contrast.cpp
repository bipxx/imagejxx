// Independent native Auto command tests under adoption b4f2acd2999db66e.
// Literal A01--A18 expectations are not computed by the implementation under test.
#include <imagejxx/auto_contrast.hpp>
#include <imagejxx/preferences.hpp>
#include <imagejxx/session.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <locale>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace imagejxx;
namespace {
std::size_t checks = 0;
std::vector<std::string> cases;
void named(std::string value) { cases.push_back(std::move(value)); }
void check(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error((cases.empty() ? "fixture" : cases.back()) + ": " + message);
}
// Actual-value carriers only. The supervising compositor owns file hashing,
// independent contract comparisons and the identity of the complete passing run.
std::string auto_observation_string(const std::string &value) {
  if (value.size() > 256u * 1024u)
    throw std::runtime_error("Auto observation string exceeds its fixed output bound");
  constexpr char digits[] = "0123456789abcdef";
  std::string result = "\"";
  for (const auto character : value) {
    const auto byte = static_cast<unsigned char>(character);
    if (byte == '"' || byte == '\\') { result += '\\'; result += character; }
    else if (byte < 0x20) {
      result += "\\u00"; result += digits[byte >> 4]; result += digits[byte & 15];
    } else result += character;
  }
  result += '"'; return result;
}
class AutoObservationRow {
  std::ostringstream values_;
  std::size_t fields_ = 0;
  void field(const std::string &name) {
    if (fields_ >= 35) throw std::runtime_error("Too many Auto observation fields");
    if (fields_++) values_ << ',';
    values_ << auto_observation_string(name) << ':';
  }
public:
  AutoObservationRow() {
    values_.imbue(std::locale::classic());
    // Preserve number-vs-integer JSON types in the frozen typed trace contract.
    values_ << std::setprecision(std::numeric_limits<double>::max_digits10) << std::showpoint;
  }
  void text(const std::string &name, const std::string &value) {
    field(name); values_ << auto_observation_string(value);
  }
  void integer(const std::string &name, int value) { field(name); values_ << value; }
  void number(const std::string &name, double value) {
    if (!std::isfinite(value)) throw std::runtime_error("Auto observation number is not finite");
    field(name); values_ << value;
  }
  void emit(const std::string &case_id, const std::string &persisted_file = {}) const {
    std::string line = "AUTO_OBSERVATION_JSON {\"schema_version\":1,\"context\":\"cpp_api\",\"case_id\":" +
        auto_observation_string(case_id) + ",\"observations\":{" + values_.str() + "},\"artifacts\":{";
    if (!persisted_file.empty()) line += "\"persisted_file\":" + auto_observation_string(persisted_file);
    line += "}}";
    if (line.size() > 1024u * 1024u) throw std::runtime_error("Auto observation row exceeds1MiB");
    std::cout << line << '\n';
  }
};
void observe_auto_setting(AutoObservationRow &row, const std::string &scenario,
    const std::string &captured_setting, const Settings &current,
    const ResolvedAutoContrastCommand &command) {
  row.text(scenario + ".setting", captured_setting);
  row.text(scenario + ".current", current.text("conversion_unsigned16_range"));
  row.integer(scenario + ".effective", command.request().unsigned16_bits);
  row.number(scenario + ".range_low", command.resolved_range()[0]);
  row.number(scenario + ".range_high", command.resolved_range()[1]);
  row.integer(scenario + ".cycle_next", command.cycle().next);
}
// A supervised run retains the exact consumed configuration in its fresh stage.
// An ordinary no-argument run owns and removes only this one file/directory.
struct AutoPreferencesFile {
  std::filesystem::path directory;
  bool retain = false;
  AutoPreferencesFile() {
    std::string configured;
#ifdef _WIN32
    char *value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, "IMAGEJXX_AUTO_SETTINGS_DIRECTORY"))
      throw std::runtime_error("Cannot read the Auto settings test directory");
    const std::unique_ptr<char, decltype(&std::free)> cleanup(value, &std::free);
    if (value) configured = value;
#else
    if (const auto value = std::getenv("IMAGEJXX_AUTO_SETTINGS_DIRECTORY")) configured = value;
#endif
    if (!configured.empty()) {
      directory = std::filesystem::absolute(std::filesystem::u8path(configured)).lexically_normal();
      retain = true;
      check(std::filesystem::create_directory(directory), "Supervised Auto settings directory must be fresh");
      return;
    }
    const auto parent = std::filesystem::canonical(std::filesystem::temp_directory_path());
    for (unsigned attempt = 0; attempt < 16; ++attempt) {
      directory = parent / ("imagejxx-auto-settings-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(attempt));
      if (std::filesystem::create_directory(directory)) return;
    }
    throw std::runtime_error("Cannot exclusively create the Auto settings directory");
  }
  std::filesystem::path file() const { return directory / "preferences.json"; }
  ~AutoPreferencesFile() {
    if (retain) return;
    std::error_code error;
    const auto resolved = std::filesystem::canonical(directory, error);
    if (error || resolved != directory || std::filesystem::is_symlink(std::filesystem::symlink_status(directory, error))) return;
    std::filesystem::remove(file(), error);
    std::filesystem::remove(directory, error); // Nonrecursive: never delete unexpected siblings.
  }
};
template <class Error = std::invalid_argument, class Function>
void rejects(Function function, const char *message) {
  bool caught = false;
  try { function(); } catch (const Error &) { caught = true; }
  check(caught, message);
}
template <class Function> std::string invalid_message(Function function) {
  try { function(); } catch (const std::invalid_argument &error) { return error.what(); }
  throw std::runtime_error(cases.back() + ": malformed input was admitted");
}
MetadataObject object(const MetadataValue &value) {
  return std::get<MetadataObject>(std::get<std::shared_ptr<const MetadataTree>>(value)->value);
}
MetadataArray array(const MetadataValue &value) {
  return std::get<MetadataArray>(std::get<std::shared_ptr<const MetadataTree>>(value)->value);
}
MetadataObject replace_record(MetadataObject value, const std::vector<std::string> &path,
                       MetadataValue replacement, bool erase = false) {
  check(!path.empty(), "Test mutation path must not be empty");
  if (path.size() == 1) {
    if (erase) value.erase(path[0]); else value[path[0]] = std::move(replacement);
  } else {
    std::vector<std::string> tail(path.begin() + 1, path.end());
    value[path[0]] = metadata_object(replace_record(object(value.at(path[0])), tail,
                                            std::move(replacement), erase));
  }
  return value;
}
std::uint64_t bits(double value) {
  std::uint64_t result;
  std::memcpy(&result, &value, sizeof result);
  return result;
}
double float_bits(std::uint32_t value) {
  float result;
  std::memcpy(&result, &value, sizeof result);
  return static_cast<double>(result);
}
std::vector<double> repeats(std::initializer_list<std::pair<double, int>> rows) {
  std::vector<double> result;
  for (const auto &row : rows) result.insert(result.end(), std::size_t(row.second), row.first);
  return result;
}
Image image(PixelType type, const std::vector<double> &values, int height = 1) {
  check(!values.empty() && values.size() % std::size_t(height) == 0,
        "Fixture image dimensions must fit literal samples");
  Image result(static_cast<int>(values.size() / std::size_t(height)), height, 1, type);
  result.pixels = values;
  return result;
}
SpatialCalibration calibration() {
  SpatialCalibration value;
  value.spacing = {.5, 1.25, 2.5}; value.origin = {11, -7, 19};
  value.unit = "um"; value.frame_interval = .75; value.time_origin = 2.25;
  return value;
}
struct Feed final : PlaneSource {
  DatasetShape descriptor;
  PixelType type;
  std::shared_ptr<const Image> pixels;
  mutable std::size_t reads = 0, regions = 0;
  mutable std::vector<PlanePosition> positions;
  bool forbid = false;
  Feed(Image value, int channels, bool tcz)
      : descriptor{tcz ? 2 : 1, channels, tcz ? 2 : 1,
                   value.height, value.width, 1}, type(value.type),
        pixels(std::make_shared<const Image>(std::move(value))) {}
  DatasetShape shape() const override { return descriptor; }
  PixelType pixel_type() const override { return type; }
  std::shared_ptr<const Image> read_plane(PlanePosition position) const override {
    ++reads; positions.push_back(position);
    if (forbid) throw std::runtime_error("Forbidden Auto pixel acquisition");
    return pixels;
  }
  Image read_region(PlanePosition, int, int, int, int) const override {
    ++regions; throw std::runtime_error("Auto requested an unrelated region");
  }
};
ChannelDisplayState state(int count, ChannelDisplayMode mode = ChannelDisplayMode::Composite,
                          ChannelDisplayVersion version = ChannelDisplayVersion::V1,
                          ChannelDisplayProjection projection = ChannelDisplayProjection::Sum) {
  ChannelDisplayState result;
  result.mode = mode; result.version = version; result.projection = projection;
  for (int c = 0; c < count; ++c) {
    ChannelDisplayChannel channel;
    channel.range = {double(c * 10), double(c * 10 + 99)};
    channel.visible = c % 2 == 0;
    channel.lut.name = c % 3 == 0 ? "Red" : c % 3 == 1 ? "Green" : "Blue";
    result.channels.push_back(channel);
  }
  return result;
}
void configure(Document &owner, const ChannelDisplayState &value, bool tcz = false) {
  auto view = owner.snapshot().display();
  view.t = tcz ? 1 : 0; view.c = value.channels.size() > 1 ? 1 : 0;
  view.crosshair = {0, 0, tcz ? 1 : 0};
  view.zoom = 1.25; view.planeZoom = {1.25, 2.5, 3.75};
  view.pan = {{{1, 2}, {3, 4}, {5, 6}}}; view.orthogonal = true;
  view.linkedZoom = false;
  view.properties["retained"] = metadata_object({{"exact", std::uint64_t(-1)}});
  owner.set_display(with_channel_display(std::move(view), owner.snapshot().data().dataset, value));
}
struct Fixture { std::shared_ptr<Feed> feed; std::shared_ptr<Document> owner; };
Fixture fixture(PixelType type, const std::vector<double> &values, int channels = 1,
                ChannelDisplayMode mode = ChannelDisplayMode::Composite,
                bool tcz = false, int height = 1,
                ChannelDisplayVersion version = ChannelDisplayVersion::V1,
                ChannelDisplayProjection projection = ChannelDisplayProjection::Sum) {
  auto feed = std::make_shared<Feed>(image(type, values, height), channels, tcz);
  auto owner = std::make_shared<Document>(Dataset(feed, calibration()), "Auto literal source");
  configure(*owner, state(channels, mode, version, projection), tcz);
  check(feed->reads == 0, "Fixture metadata setup must not read a plane");
  return {feed, owner};
}
std::shared_ptr<Document> eager(PixelType type, const std::vector<double> &values,
                               FloatingSamplePolicy policy = FloatingSamplePolicy::FiniteOnly) {
  Stack planes; planes.push_back(image(type, values));
  auto owner = std::make_shared<Document>(Dataset::from_planes(
      {1, 1, 1, 1, static_cast<int>(values.size()), 1}, std::move(planes), calibration(), policy));
  configure(*owner, state(1));
  return owner;
}
bool same_view(const DisplayState &a, const DisplayState &b) {
  return a.t == b.t && a.c == b.c && a.crosshair == b.crosshair &&
      a.zoom == b.zoom && a.planeZoom == b.planeZoom && a.pan == b.pan &&
      a.orthogonal == b.orthogonal && a.linkedZoom == b.linkedZoom &&
      a.revision == b.revision && metadata_equal(a.properties, b.properties);
}
void unchanged(const Document &owner, const DocumentSnapshot &before, const HistoryInfo &history) {
  const auto now = owner.snapshot(); const auto actual = owner.history();
  const auto a = now.revisions(), b = before.revisions();
  check(now.shares_data(before) && now.data_identity() == before.data_identity() &&
        now.dirty() == before.dirty() && same_view(now.display(), before.display()) &&
        a.pixels == b.pixels && a.metadata == b.metadata && a.selection == b.selection &&
        a.display == b.display && actual.undo_steps == history.undo_steps &&
        actual.redo_steps == history.redo_steps,
        "Detached resolution/refusal must not mutate source/history/dirty/display");
}
void range(const ResolvedAutoContrastCommand &command, std::array<double, 2> resolved,
           AutoContrastOutcome outcome, int next, std::optional<std::array<int, 2>> indices = {},
           std::optional<std::array<double, 2>> candidate = {},
           std::optional<std::array<double, 2>> applied = {}) {
  check(command.resolved_range() == resolved && command.applied_range() == applied.value_or(resolved),
        "Literal resolved/applied Auto range differs");
  check(command.outcome() == outcome && command.cycle().next == next &&
        command.indices() == indices && command.candidate_range() == candidate,
        "Literal outcome/indices/candidate/cycle differs");
  check(command.effect().action() == ChannelDisplayAction::Set && command.effect().state() &&
        command.requires_captured_state(), "Fresh Auto must own one captured-state Set effect");
  for (int channel : command.channels())
    check(command.effect().state()->channels[std::size_t(channel)].range == command.applied_range(),
          "Every target receives the exact applied range");
  validate_auto_contrast_command_record(command.record());
}
void once(const Fixture &value, PlanePosition position = {}) {
  check(value.feed->reads == 1 && value.feed->regions == 0 && value.feed->positions.size() == 1 &&
        value.feed->positions[0].t == position.t && value.feed->positions[0].c == position.c &&
        value.feed->positions[0].z == position.z, "Auto must acquire exactly the captured TCZ plane once");
}

void literals() {
  named("A01.strict_cycle");
  auto a = fixture(PixelType::UInt8, repeats({{10,1},{20,3},{30,200},{40,200},{50,200},{60,3492}}));
  const std::array<int,3> previous{0,5000,2500}, used{5000,2500,1250}, low{10,20,30};
  AutoObservationRow cycle_observations;
  const std::array<const char *,3> cycle_fields{
      "cycle.first.range_low", "cycle.second.range_low", "cycle.third.range_low"};
  for (std::size_t i = 0; i < previous.size(); ++i) {
    AutoContrastRequest request; request.previous_divisor = previous[i];
    const auto c = resolve_auto_contrast_command(a.owner->snapshot(), request);
    range(c, {double(low[i]),50}, AutoContrastOutcome::HistogramBounds, used[i],
          std::array<int,2>{low[i],50}, std::array<double,2>{double(low[i]),50});
    check(c.cycle().previous == previous[i] && c.cycle().used == used[i], "Strict cycle used divisor differs");
    cycle_observations.number(cycle_fields[i], c.resolved_range()[0]);
  }
  check(a.feed->reads == 3, "Each explicit cycle resolve reads only one plane");
  cycle_observations.emit("A01");

  named("A02.dominant_boundary");
  auto b = fixture(PixelType::UInt8, repeats({{1,1},{2,10},{3,11},{4,78}}));
  auto c = resolve_auto_contrast_command(b.owner->snapshot());
  range(c,{1,2},AutoContrastOutcome::HistogramBounds,5000,std::array<int,2>{1,2},std::array<double,2>{1,2}); once(b);

  named("A03.u8_fallback");
  auto u8 = fixture(PixelType::UInt8,std::vector<double>(100,7));
  range(resolve_auto_contrast_command(u8.owner->snapshot()),{0,255},AutoContrastOutcome::ResetFallback,0); once(u8);

  named("A04.equal_candidate");
  auto equal = fixture(PixelType::UInt8,repeats({{10,8},{200,92}}));
  range(resolve_auto_contrast_command(equal.owner->snapshot()),{10,200},AutoContrastOutcome::AreaExtrema,5000,
        std::array<int,2>{10,10},std::array<double,2>{10,10}); once(equal);

  named("A05.u16_auto_fallback");
  auto u16 = fixture(PixelType::UInt16,std::vector<double>(100,7));
  range(resolve_auto_contrast_command(u16.owner->snapshot()),{7,7},AutoContrastOutcome::ResetFallback,0); once(u16);

  named("A06.u16_configured_fallback");
  auto fixed = fixture(PixelType::UInt16,std::vector<double>(100,7));
  AutoContrastRequest request; request.unsigned16_bits=12;
  range(resolve_auto_contrast_command(fixed.owner->snapshot(),request),{0,4095},AutoContrastOutcome::ResetFallback,0); once(fixed);

  named("A07.u16_all_configured_choices");
  const std::array<std::pair<int,int>,7> choices{{{0,7},{8,255},{10,1023},{12,4095},{14,16383},{15,32767},{16,65535}}};
  for (const auto &choice: choices) {
    auto value=fixture(PixelType::UInt16,std::vector<double>(100,7)); request.unsigned16_bits=choice.first;
    const auto result=resolve_auto_contrast_command(value.owner->snapshot(),request);
    range(result,{choice.first?0.:7.,double(choice.second)},AutoContrastOutcome::ResetFallback,0);
    check(result.histogram().origin==7 && result.histogram().bin_size==1./256 &&
          result.histogram().finite_count==100,"Configured bits must not alter the raw histogram"); once(value);
  }

  named("A08.roi_full_plane_fallback");
  auto roi=fixture(PixelType::UInt16,{1,7,7,7,7,7,7,99},3,ChannelDisplayMode::Composite,true);
  roi.owner->select(Selection::from_packed_mask(8,1,{0x7e}));
  const auto r=resolve_auto_contrast_command(roi.owner->snapshot());
  range(r,{1,99},AutoContrastOutcome::ResetFallback,0);
  check(r.histogram().minimum==7 && r.histogram().maximum==7 && r.histogram().finite_count==6 &&
        r.histogram().plane_minimum==1 && r.histogram().plane_maximum==99 &&
        r.histogram().plane_finite_count==8,"Fallback must use same-plane full extrema, not ROI extrema"); once(roi,{1,1,1});

  named("A09.empty_area_refusal");
  auto empty=fixture(PixelType::UInt16,{1,7,9,99});
  empty.owner->select(Selection::from_packed_mask(4,1,{0}));
  const auto before=empty.owner->snapshot(); const auto history=empty.owner->history(); request.unsigned16_bits=12;
  rejects([&]{resolve_auto_contrast_command(before,request);},"Configured fallback cannot replace empty area");
  unchanged(*empty.owner,before,history); once(empty);

  named("A10.no_finite_refusal");
  auto nofinite=eager(PixelType::Float32,{float_bits(0x7fc12345),float_bits(0xffc54321)},FloatingSamplePolicy::PreserveQuietNaNFloat32);
  const auto nan_before=nofinite->snapshot();
  rejects([&]{resolve_auto_contrast_command(nan_before);},"All quiet-NaN area must refuse");
  const auto nan_plane=nofinite->snapshot().data().dataset.plane();
  check(bits(nan_plane->pixels[0])==bits(float_bits(0x7fc12345)) &&
        bits(nan_plane->pixels[1])==bits(float_bits(0xffc54321)),"No-finite refusal preserves exact NaN payloads");

  named("A11.u16_fractional");
  std::vector<double> unsigned_values; for(int i=0;i<16;++i)unsigned_values.push_back(1000+257*i);
  auto fractional=fixture(PixelType::UInt16,unsigned_values);
  const auto fractional_result=resolve_auto_contrast_command(fractional.owner->snapshot());
  range(fractional_result,{1000,4840.9375},AutoContrastOutcome::HistogramBounds,5000,
        std::array<int,2>{0,255},std::array<double,2>{1000,4840.9375});
  check(fractional_result.histogram().bin_size==15.0625,"Pinned U16 bin width differs"); once(fractional);

  named("A12.f32_fractional");
  std::vector<double> floats;for(int i=0;i<16;++i)floats.push_back(-2.5+.5*i);
  auto floating=fixture(PixelType::Float32,floats);
  const auto floating_result=resolve_auto_contrast_command(floating.owner->snapshot());
  range(floating_result,{-2.5,4.970703125},AutoContrastOutcome::HistogramBounds,5000,
        std::array<int,2>{0,255},std::array<double,2>{-2.5,4.970703125});
  check(floating_result.histogram().bin_size==.029296875,"Pinned F32 bin width differs"); once(floating);

  named("A13.shift_active_getter_scopes");
  std::vector<double> grid;for(int y=0;y<6;++y)for(int x=0;x<8;++x)grid.push_back(75+2*y+x);
  for(auto mode:{ChannelDisplayMode::Composite,ChannelDisplayMode::Color,ChannelDisplayMode::Grayscale})
    for(auto scope:{AutoContrastScope::ActiveChannel,AutoContrastScope::CompositeAll}) {
      auto value=fixture(PixelType::UInt16,grid,3,mode,true,6);request={};request.scope=scope;
      const auto result=resolve_auto_contrast_command(value.owner->snapshot(),request);
      const bool all=mode==ChannelDisplayMode::Composite && scope==AutoContrastScope::CompositeAll;
      range(result,{75,91.9453125},AutoContrastOutcome::HistogramBounds,5000,
            std::array<int,2>{0,241},std::array<double,2>{75,91.9453125},
            std::array<double,2>{75,all?92.:91.9453125});
      check(result.channels()==(all?std::vector<int>{0,1,2}:std::vector<int>{1}),"Getter propagation requires actual CompositeAll"); once(value,{1,1,1});
    }

  named("A14.configured_fallback_targets");
  for(auto mode:{ChannelDisplayMode::Composite,ChannelDisplayMode::Color,ChannelDisplayMode::Grayscale}) {
    auto value=fixture(PixelType::UInt16,std::vector<double>(100,7),3,mode);
    request={};request.unsigned16_bits=12;
    const auto result=resolve_auto_contrast_command(value.owner->snapshot(),request);
    range(result,{0,4095},AutoContrastOutcome::ResetFallback,0);
    check(result.channels()==std::vector<int>({0,1,2}) && result.effect().state()->mode==mode,
          "Configured U16 fallback updates all stored C in every mode"); once(value,{0,1,0});
  }

  named("A15.raw_domain_preservation");
  auto plain=fixture(PixelType::UInt16,unsigned_values), tagged=fixture(PixelType::UInt16,unsigned_values);
  auto edit=tagged.owner->begin_edit();edit.set_property("density",metadata_object({{"signed16",true},{"formula",std::string("opaque")}}));
  auto cal=calibration();cal.spacing={.3,1.7,9.25};cal.unit="unrelated";cal.time_origin=99;edit.set_calibration(cal);tagged.owner->commit(edit);
  const auto tagged_before=tagged.owner->snapshot();const auto tagged_history=tagged.owner->history();
  const auto normal=resolve_auto_contrast_command(plain.owner->snapshot()), opaque=resolve_auto_contrast_command(tagged_before);
  check(normal.resolved_range()==std::array<double,2>{1000,4840.9375} && opaque.resolved_range()==normal.resolved_range() &&
        opaque.histogram().origin==1000 && opaque.histogram().bin_size==15.0625,"Opaque density/spatial/time metadata cannot change raw Auto");
  unchanged(*tagged.owner,tagged_before,tagged_history);once(plain);once(tagged);

  named("A16.replay_is_not_enhance");
  auto recorded=fixture(PixelType::UInt8,repeats({{18,1},{19,10},{20,11},{21,78}}));
  const auto original=resolve_auto_contrast_command(recorded.owner->snapshot());
  range(original,{18,19},AutoContrastOutcome::HistogramBounds,5000,std::array<int,2>{18,19},std::array<double,2>{18,19});
  recorded.feed->forbid=true;
  const auto replay=auto_contrast_command_from_record(recorded.owner->snapshot(),original.record());
  const auto proposed=command_display_state(replay,recorded.owner->snapshot());
  check(replay.resolved_range()==std::array<double,2>{18,19} && !replay.requires_captured_state() &&
        recognized_channel_display(proposed,recorded.owner->snapshot().data().dataset)->channels[0].range==std::array<double,2>{18,19} &&
        recorded.feed->reads==1,"Replay applies stored18..19 without Enhance18..21 or provider work");

  named("A17.setting_capture_override_reopen");
  PreferencesDocument preferences;
  AutoObservationRow setting_observations;
  auto settings_source=fixture(PixelType::UInt16,std::vector<double>(100,7));
  const auto default_key=preferences.settings.text("conversion_unsigned16_range");
  request={};request.unsigned16_bits=preferences.settings.conversion_options().unsigned16_bits;
  const auto default_setting=resolve_auto_contrast_command(settings_source.owner->snapshot(),request);
  check(default_setting.request().unsigned16_bits==0,"Default existing setting must be captured as automatic range");
  range(default_setting,{7,7},AutoContrastOutcome::ResetFallback,0);
  observe_auto_setting(setting_observations,"default",default_key,preferences.settings,default_setting);
  preferences.settings.set("conversion_unsigned16_range",std::string("12"));
  const auto changed_key=preferences.settings.text("conversion_unsigned16_range");
  request={};request.unsigned16_bits=preferences.settings.conversion_options().unsigned16_bits;
  const auto changed_setting=resolve_auto_contrast_command(settings_source.owner->snapshot(),request);
  range(changed_setting,{0,4095},AutoContrastOutcome::ResetFallback,0);
  check(preferences.settings.conversion_options().unsigned16_bits==12,"Changed setting consumption must preserve the live12 default");
  observe_auto_setting(setting_observations,"changed",changed_key,preferences.settings,changed_setting);
  AutoPreferencesFile saved;
  check(preferences.save(saved.file().u8string()),"Changed Auto preference must save to a real configuration file");
  std::ifstream input(saved.file(),std::ios::binary);
  const std::string saved_json{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
  const auto reopened=PreferencesDocument::load(saved.file().u8string(),false);
  check(saved_json==preferences.json() && reopened.json()==saved_json,"Auto must reopen the exact saved configuration bytes");
  std::cout<<"auto_settings_file "<<saved.file().u8string()<<'\n';
  check(reopened.settings.conversion_options().unsigned16_bits==12,"Persisted existing setting must reopen as12");
  const auto reopened_key=reopened.settings.text("conversion_unsigned16_range");
  request={};request.unsigned16_bits=reopened.settings.conversion_options().unsigned16_bits;
  const auto persisted_setting=resolve_auto_contrast_command(settings_source.owner->snapshot(),request);
  range(persisted_setting,{0,4095},AutoContrastOutcome::ResetFallback,0);
  check(reopened.settings.conversion_options().unsigned16_bits==12,"Reopened setting must produce4095 with live12 unchanged");
  observe_auto_setting(setting_observations,"persisted",changed_key,reopened.settings,persisted_setting);
  setting_observations.text("persisted.reopened_setting",reopened_key);
  const auto frozen_key=preferences.settings.text("conversion_unsigned16_range");
  bool entered=false;ExecutionControl control;control.checkpoint=[&]{if(!entered){entered=true;preferences.settings.set("conversion_unsigned16_range",std::string("auto"));request.unsigned16_bits=0;}};
  const auto captured=resolve_auto_contrast_command(settings_source.owner->snapshot(),request,{},control);
  range(captured,{0,4095},AutoContrastOutcome::ResetFallback,0);
  check(captured.request().unsigned16_bits==12 && preferences.settings.conversion_options().unsigned16_bits==0,
        "Late settings/request mutation cannot change captured fixed range");
  observe_auto_setting(setting_observations,"frozen",frozen_key,preferences.settings,captured);
  request.unsigned16_bits=10;
  const auto explicit_bits=request.unsigned16_bits;
  const auto overridden=resolve_auto_contrast_command(settings_source.owner->snapshot(),request);
  range(overridden,{0,1023},AutoContrastOutcome::ResetFallback,0);
  observe_auto_setting(setting_observations,"override",frozen_key,preferences.settings,overridden);
  setting_observations.integer("override.explicit",explicit_bits);
  setting_observations.emit("A17",saved.file().u8string());
}

void dimensions() {
  std::vector<double> values;for(int i=0;i<16;++i)values.push_back(1000+257*i);
  const std::array<std::pair<ChannelDisplayVersion,ChannelDisplayProjection>,4> versions{{
      {ChannelDisplayVersion::V1,ChannelDisplayProjection::Sum},
      {ChannelDisplayVersion::V2,ChannelDisplayProjection::Sum},
      {ChannelDisplayVersion::V2,ChannelDisplayProjection::Max},
      {ChannelDisplayVersion::V2,ChannelDisplayProjection::Min}}};
  for(std::size_t v=0;v<versions.size();++v) {
    int m=0;
    for(auto mode:{ChannelDisplayMode::Composite,ChannelDisplayMode::Color,ChannelDisplayMode::Grayscale}) {
      named("D.state"+std::to_string(v)+".mode"+std::to_string(m++));
      auto value=fixture(PixelType::UInt16,values,3,mode,true,1,versions[v].first,versions[v].second);
      const auto before=value.owner->snapshot();const auto history=value.owner->history();
      const auto old=*recognized_channel_display(before.display(),before.data().dataset);
      AutoContrastRequest request;request.scope=AutoContrastScope::CompositeAll;
      const auto result=resolve_auto_contrast_command(before,request);
      const auto &after=*result.effect().state();
      check(after.mode==mode && after.version==versions[v].first && after.projection==versions[v].second &&
            result.effect().version()==versions[v].first,"Auto preserves logical mode/projection and exact state/action version");
      range(result,{1000,4840.9375},AutoContrastOutcome::HistogramBounds,5000,
            std::array<int,2>{0,255},std::array<double,2>{1000,4840.9375},
            std::array<double,2>{1000,mode==ChannelDisplayMode::Composite?4841.:4840.9375});
      for(std::size_t c=0;c<after.channels.size();++c) {
        check(after.channels[c].visible==old.channels[c].visible &&
              after.channels[c].lut.name==old.channels[c].lut.name && after.channels[c].lut.table==old.channels[c].lut.table,
              "Visibility and LUT identity survive Auto");
        if(mode!=ChannelDisplayMode::Composite && c!=1)
          check(after.channels[c].range==old.channels[c].range,"Untargeted channel ranges survive Auto");
      }
      check(result.channels()==(mode==ChannelDisplayMode::Composite?std::vector<int>{0,1,2}:std::vector<int>{1}),
            "CompositeAll does not propagate Color/Grayscale getters");
      unchanged(*value.owner,before,history);once(value,{1,1,1});
    }
  }
  named("D.singleton_composite_getter");
  auto singleton=fixture(PixelType::UInt16,values);AutoContrastRequest request;request.scope=AutoContrastScope::CompositeAll;
  range(resolve_auto_contrast_command(singleton.owner->snapshot(),request),{1000,4840.9375},
        AutoContrastOutcome::HistogramBounds,5000,std::array<int,2>{0,255},
        std::array<double,2>{1000,4840.9375},std::array<double,2>{1000,4841});once(singleton);

  named("D.color_nine_channels");
  auto nine=fixture(PixelType::UInt16,values,9,ChannelDisplayMode::Color,true,1,ChannelDisplayVersion::V2,ChannelDisplayProjection::Min);
  const auto nc=resolve_auto_contrast_command(nine.owner->snapshot(),request);
  check(nc.effect().state()->channels.size()==9 && nc.effect().state()->mode==ChannelDisplayMode::Color &&
        nc.effect().state()->projection==ChannelDisplayProjection::Min && nc.channels()==std::vector<int>{1},
        "Color C>8 retains complete state and active-only nonfallback scope");once(nine,{1,1,1});

  named("D.float_getter_rounding");
  std::vector<double> large;for(int i=0;i<16;++i)large.push_back(16777216.+2*i);
  auto fl=fixture(PixelType::Float32,large,3);request.scope=AutoContrastScope::CompositeAll;
  range(resolve_auto_contrast_command(fl.owner->snapshot(),request),{16777216,16777245.8828125},
        AutoContrastOutcome::HistogramBounds,5000,std::array<int,2>{0,255},
        std::array<double,2>{16777216,16777245.8828125},std::array<double,2>{16777216,16777246});once(fl,{0,1,0});

  named("D.all_legal_cycle_values");
  const std::array<std::pair<int,int>,11> transitions{{{0,5000},{5000,2500},{2500,1250},{1250,625},
      {625,312},{312,156},{156,78},{78,39},{39,19},{19,9},{9,5000}}};
  auto cycle=fixture(PixelType::UInt8,repeats({{1,1},{2,10},{3,11},{4,78}}));
  for(const auto &entry:transitions) {
    request={};request.previous_divisor=entry.first;
    const auto result=resolve_auto_contrast_command(cycle.owner->snapshot(),request);
    check(result.cycle().previous==entry.first && result.cycle().used==entry.second &&
          result.cycle().next==(entry.second==9?0:entry.second),"Every allowed cycle transition follows exact integer division");
    if(entry.second==9)check(result.outcome()==AutoContrastOutcome::ResetFallback,"Used9 cannot pass the dominant limit on this literal input");
  }

  named("D.area_kinds_and_mask_tail");
  std::vector<Selection> areas{
      Selection("rectangle",{{0,0},{4,1}}),Selection("ellipse",{{-10,-10},{10,10}}),
      Selection("rotated_rectangle",{{0,0},{4,0},{0,1}}),
      Selection("rotated_ellipse",{{-10,-10},{10,-10},{-10,10}}),
      Selection("polygon",{{-10,-10},{10,-10},{10,10},{-10,10}}),
      Selection("freehand",{{-10,-10},{10,-10},{10,10},{-10,10}}),
      Selection::from_packed_mask(4,1,{0xff})};
  for(auto selected:areas) {
    auto value=fixture(PixelType::UInt8,{0,1,2,3});
    selected.attributes["kept"]=metadata_object({{"exact",std::uint64_t(-1)}});
    value.owner->select(selected);const auto result=resolve_auto_contrast_command(value.owner->snapshot());
    check(result.selection_provenance()==selected && result.histogram().visited_count==4,
          "Every admitted area kind preserves its full original geometry and attributes");
    range(result,{0,255},AutoContrastOutcome::ResetFallback,0);
    const auto replay=auto_contrast_command_from_record(value.owner->snapshot(),result.record());
    check(replay.selection_provenance()==selected,"Replay preserves packed unused tail bits without canonicalization");once(value);
  }
  auto holes=fixture(PixelType::UInt8,{0,1,2,3});holes.owner->select(Selection::from_packed_mask(4,1,{0xaf}));
  const auto holed=resolve_auto_contrast_command(holes.owner->snapshot());
  check(holed.histogram().visited_count==2 && holed.histogram().minimum==0 && holed.histogram().maximum==2,
        "Disconnected packed area membership uses actual selected cells");once(holes);

  named("D.nonarea_and_explicit_whole");
  for(const std::string kind:{"line","polyline","angle","point","multipoint"}) {
    auto value=fixture(PixelType::UInt8,repeats({{1,1},{2,10},{3,11},{4,78}}));
    Selection selected(kind,{{0,0},{1,0},{2,0}});value.owner->select(selected);
    const auto result=resolve_auto_contrast_command(value.owner->snapshot());
    check(result.selection_provenance()==selected && result.histogram().visited_count==100 &&
          std::get<std::string>(object(result.record().at("roi")).at("effective"))=="whole_nonarea",
          "Known nonarea selection is retained but never rasterized");
    range(result,{1,2},AutoContrastOutcome::HistogramBounds,5000,std::array<int,2>{1,2},std::array<double,2>{1,2});once(value);
  }
  auto whole=fixture(PixelType::UInt8,repeats({{1,1},{2,10},{3,11},{4,78}}));
  whole.owner->select(Selection::from_packed_mask(100,1,std::vector<std::uint8_t>(13,0)));
  request={};request.roi=AutoContrastRoiPolicy::WholePlane;
  const auto explicit_whole=resolve_auto_contrast_command(whole.owner->snapshot(),request);
  check(!explicit_whole.selection_provenance() && explicit_whole.histogram().visited_count==100 &&
        explicit_whole.selection_dependency_revision()==whole.owner->snapshot().revisions().selection,
        "Explicit whole ignores the area operand but retains the Selection dependency");once(whole);

  named("D.quiet_nan_with_finite_area");
  std::vector<double> samples;for(int i=0;i<16;++i)samples.push_back(-2.5+.5*i);
  samples.push_back(float_bits(0x7fc12345));samples.push_back(float_bits(0xffc54321));
  auto nan=eager(PixelType::Float32,samples,FloatingSamplePolicy::PreserveQuietNaNFloat32);
  const auto qr=resolve_auto_contrast_command(nan->snapshot());
  range(qr,{-2.5,4.970703125},AutoContrastOutcome::HistogramBounds,5000,
        std::array<int,2>{0,255},std::array<double,2>{-2.5,4.970703125});
  check(qr.histogram().visited_count==18 && qr.histogram().finite_count==16 && qr.histogram().nan_count==2 &&
        qr.histogram().plane_finite_count==16 && qr.histogram().plane_nan_count==2,
        "Only finite area count controls Auto thresholds; quiet NaNs remain separately observed");
  const auto kept=nan->snapshot().data().dataset.plane();
  for(std::size_t i=0;i<samples.size();++i)check(bits(kept->pixels[i])==bits(samples[i]),"Quiet NaN/source raw bits are immutable");

  named("D.noncanonical_source_refusal_outside_roi");
  for(const auto &entry:std::vector<std::pair<PixelType,double>>{{PixelType::UInt8,.5},
      {PixelType::Float32,.1},{PixelType::UInt16,std::numeric_limits<double>::infinity()}}) {
    auto value=fixture(entry.first,{0,entry.second,2,3});value.owner->select(Selection::from_packed_mask(4,1,{0x80}));
    const auto before=value.owner->snapshot();const auto history=value.owner->history();
    rejects([&]{resolve_auto_contrast_command(before);},"Whole acquired plane validation includes invalid samples outside ROI");
    unchanged(*value.owner,before,history);once(value);
  }
}

MetadataObject golden(const DocumentSnapshot &source, ChannelDisplayVersion version) {
  MetadataObject state_value{{"schema",std::string("imagejxx.channel-display")},
      {"version",std::int64_t(version==ChannelDisplayVersion::V1?1:2)},
      {"mode",std::string("composite")},
      {"channels",metadata_array({metadata_object({{"visible",true},
          {"range",metadata_array({18.,19.})},{"lut",metadata_object({{"name",std::string("Red")}})}})})}};
  if(version==ChannelDisplayVersion::V2)state_value["projection"]=std::string("min");
  MetadataObject effect{{"schema",std::string("imagejxx.channel-display-command")},
      {"version",std::int64_t(version==ChannelDisplayVersion::V1?1:2)},{"action",std::string("set")},
      {"source",metadata_object({{"id",source.id()},{"pixels_revision",source.revisions().pixels},
          {"metadata_revision",source.revisions().metadata},{"shape",metadata_array({1,1,1,1,100,1})},
          {"type",std::string("uint8")},{"sample_policy",std::string("finite_only")}})},
      {"position",metadata_object({{"t",0},{"c",0},{"z",0}})},
      {"state",metadata_object(state_value)},
      {"reset",metadata_object({{"channels",metadata_array({})},{"empty_finite",metadata_array({})}})}};
  if(version==ChannelDisplayVersion::V2){effect["display_dependency"]=std::monostate{};effect["navigation"]=std::monostate{};}
  return {{"schema",std::string("imagejxx.auto-contrast-command")},{"version",std::int64_t(1)},
      {"effect",metadata_object(effect)},
      {"roi",metadata_object({{"policy",std::string("active_selection")},{"effective",std::string("whole_plane")},
          {"selection_revision",source.revisions().selection},{"selection",std::monostate{}}})},
      {"display_dependency",metadata_object({{"revision",source.revisions().display}})},
      {"cycle",metadata_object({{"previous",0},{"used",5000},{"next",5000}})},
      {"scope",std::string("active_channel")},{"unsigned16_bits",0},{"channels",metadata_array({0})},
      {"histogram",metadata_object({{"domain",std::string("raw")},{"binning",std::string("processor_raw")},{"bins",256},
          {"origin",0.},{"bin_size",1.},{"minimum",18.},{"maximum",21.},
          {"visited_count",std::uint64_t(100)},{"finite_count",std::uint64_t(100)},{"nan_count",std::uint64_t(0)},
          {"plane_minimum",18.},{"plane_maximum",21.},{"plane_finite_count",std::uint64_t(100)},
          {"plane_nan_count",std::uint64_t(0)}})},
      {"decision",metadata_object({{"outcome",std::string("histogram_bounds")},
          {"indices",metadata_array({18,19})},{"candidate_range",metadata_array({18.,19.})},
          {"resolved_range",metadata_array({18.,19.})},{"applied_range",metadata_array({18.,19.})}})}};
}

void records() {
  named("R01.independent_exact_v1_v2_wire");
  for(auto version:{ChannelDisplayVersion::V1,ChannelDisplayVersion::V2}) {
    auto value=fixture(PixelType::UInt8,repeats({{18,1},{19,10},{20,11},{21,78}}),1,
        ChannelDisplayMode::Composite,false,1,version,
        version==ChannelDisplayVersion::V1?ChannelDisplayProjection::Sum:ChannelDisplayProjection::Min);
    const auto expected=golden(value.owner->snapshot(),version);
    validate_auto_contrast_command_record(expected);
    const auto command=resolve_auto_contrast_command(value.owner->snapshot());
    check(metadata_equal(command.record(),expected) && command.record().size()==11,
          "Complete normalized Auto record differs from independent literal wire");
    value.feed->forbid=true;const auto replay=auto_contrast_command_from_record(value.owner->snapshot(),expected);
    check(metadata_equal(replay.record(),expected) && replay.effect().version()==version && value.feed->reads==1,
          "Strict replay retains exact action/state version and original wire without reads");
  }

  named("A18.complete_original_integrity_order");
  auto value=fixture(PixelType::UInt8,repeats({{18,1},{19,10},{20,11},{21,78}}));
  auto selected=Selection::from_packed_mask(100,1,std::vector<std::uint8_t>(13,0xff));
  selected.attributes["retained"]=metadata_array({true,std::uint64_t(-1),std::string("exact")});value.owner->select(selected);
  const auto good=resolve_auto_contrast_command(value.owner->snapshot()).record();
  auto foreign=fixture(PixelType::UInt16,std::vector<double>(100,7));foreign.feed->forbid=true;
  AutoContrastRequest invalid;invalid.previous_divisor=-1;std::size_t callbacks=0;
  ExecutionControl control;control.checkpoint=[&]{++callbacks;throw std::runtime_error("Malformed original entered rebind work");};
  std::vector<MetadataObject> bad;
  for(const auto &entry:good)bad.push_back(replace_record(good,{entry.first},{},true));
  auto extra=good;extra["extra"]=true;bad.push_back(extra);
  for(const auto &group:std::vector<std::string>{"roi","display_dependency","cycle","histogram","decision"}) {
    const auto child=object(good.at(group));
    for(const auto &entry:child)bad.push_back(replace_record(good,{group,entry.first},{},true));
    bad.push_back(replace_record(good,{group,"extra"},true));
  }
  const std::vector<std::pair<std::vector<std::string>,MetadataValue>> mutations{
      {{"schema"},std::string("future")},{{"version"},true},{{"version"},1.0},{{"version"},3},
      {{"scope"},std::string("all")},{{"unsigned16_bits"},true},{{"unsigned16_bits"},12.0},{{"unsigned16_bits"},13},
      {{"channels"},metadata_array({0,0})},{{"channels"},metadata_array({})},{{"channels"},metadata_array({0.0})},
      {{"channels"},std::vector<std::string>{"0"}},
      {{"effect","action"},std::string("disable")},{{"effect","action"},std::string("reset_range")},
      {{"effect","source","id"},1.0},{{"effect","source","id"},0},
      {{"effect","source","sample_policy"},std::string("unknown")},{{"effect","source","pixels_revision"},true},
      {{"effect","source","shape"},metadata_array({1,1,1,1,true,1})},
      {{"effect","position","t"},true},{{"effect","state","extra"},true},
      {{"effect","reset","channels"},metadata_array({0})},
      {{"effect","state","channels"},metadata_array({})},
      {{"roi","policy"},std::string("whole_plane")},{{"roi","effective"},std::string("whole_nonarea")},
      {{"roi","selection_revision"},true},{{"roi","selection_revision"},1.0},
      {{"roi","selection","kind"},std::string("unknown")},{{"roi","selection","width"},0.},
      {{"roi","selection","points"},std::vector<std::string>{"bad"}},
      {{"roi","selection","shape"},metadata_array({1,99})},
      {{"roi","selection","shape"},metadata_array({1.0,100})},
      {{"roi","selection","mask"},std::string("?")},
      {{"roi","selection","mask"},std::string(20,'!')},
      {{"display_dependency","revision"},true},{{"display_dependency","revision"},-1},
      {{"cycle","previous"},4999},{{"cycle","used"},2500},{{"cycle","next"},0},
      {{"histogram","domain"},std::string("calibrated")},{{"histogram","bins"},255},
      {{"histogram","bins"},256.0},{{"histogram","origin"},1.},{{"histogram","bin_size"},2.},
      {{"histogram","minimum"},22.},{{"histogram","plane_minimum"},19.},
      {{"histogram","visited_count"},std::uint64_t(99)},{{"histogram","finite_count"},std::uint64_t(0)},
      {{"histogram","nan_count"},std::uint64_t(1)},{{"histogram","plane_finite_count"},std::uint64_t(99)},
      {{"histogram","plane_nan_count"},std::uint64_t(-1)},{{"histogram","finite_count"},true},
      {{"histogram","minimum"},std::uint64_t(9007199254740993ULL)},
      {{"decision","outcome"},std::string("area_extrema")},{{"decision","outcome"},std::string("reset_fallback")},
      {{"decision","indices"},metadata_array({19,18})},{{"decision","indices"},metadata_array({18.0,19})},
      {{"decision","indices"},metadata_array({true,19})},{{"decision","indices"},metadata_array({18,19,20})},
      {{"decision","candidate_range"},std::monostate{}},{{"decision","candidate_range"},metadata_array({18.,20.})},
      {{"decision","resolved_range"},metadata_array({18.,20.})},{{"decision","applied_range"},metadata_array({18.,20.})}};
  for(const auto &mutation:mutations)bad.push_back(replace_record(good,mutation.first,mutation.second));
  // Coherent cycle fields still cannot substantiate a nonfallback decision:
  // for used9, floor(N/9) >= floor(N/10), so no bin can qualify.
  bad.push_back(replace_record(good,{"cycle"},metadata_object({{"previous",19},{"used",9},{"next",9}})));
  // MetadataTree validates each constructed node. Retain the maximum legal
  // subtree and attach it to a raw top-level map: Auto must count that map's
  // additional depth before rejecting fields or inspecting the destination.
  MetadataValue deep=true;for(int i=0;i<128;++i)deep=metadata_array({deep});
  rejects([&]{metadata_array({deep});},"Native metadata rejects constructing a 129-container subtree");
  auto deep_record=good;deep_record["deep"]=deep;
  check(invalid_message([&]{validate_auto_contrast_command_record(deep_record);})==
      "Auto contrast metadata nesting exceeds128","Auto bounds whole-original depth, including the root map");
  bad.push_back(std::move(deep_record));
  auto null_record=good;null_record["null_tree"]=std::shared_ptr<const MetadataTree>{};
  check(invalid_message([&]{validate_auto_contrast_command_record(null_record);})==
      "Auto contrast metadata container is null","Auto rejects null original nodes before binding");
  bad.push_back(std::move(null_record));
  for(const auto &record:bad) {
    const auto expected=invalid_message([&]{validate_auto_contrast_command_record(record);});
    check(invalid_message([&]{auto_contrast_command_from_record(foreign.owner->snapshot(),record);})==expected,
          "Entire malformed original must refuse before foreign source comparison");
    check(invalid_message([&]{rebind_auto_contrast_command(foreign.owner->snapshot(),record,invalid,{},control);})==expected,
          "Entire malformed original must refuse before invalid replacement request/callbacks");
  }
  check(bad.size()>=100 && callbacks==0 && foreign.feed->reads==0 && foreign.feed->regions==0,
        "Deep original validation must not call destination providers or callbacks");

  // P12m (5a508a1) adds the V2 region DTO without excluding legacy payloads.
  // Its encoder still emits V1 for this mask; explicit V2 replay retains the
  // supplied wire. Unsupported-version refusal above therefore uses V3.
  const auto before=value.owner->snapshot();const auto history=value.owner->history();
  const auto foreign_before=foreign.owner->snapshot();const auto foreign_history=foreign.owner->history();
  const auto source_reads=value.feed->reads;value.feed->forbid=true;
  check(std::get<std::int64_t>(good.at("version"))==1,
        "Legacy Auto capture must keep its canonical V1 envelope");
  auto legacy_v2=good;legacy_v2["version"]=std::int64_t(2);
  validate_auto_contrast_command_record(legacy_v2);
  const auto legacy_replay=auto_contrast_command_from_record(before,legacy_v2);
  check(metadata_equal(legacy_replay.record(),legacy_v2) &&
        legacy_replay.selection_provenance()==before.data().selection &&
        !legacy_replay.requires_captured_state(),
        "V2 must retain the supported legacy mask and exact supplied replay wire");
  check(legacy_replay.resolved_range()==std::array<double,2>{18,19} &&
        legacy_replay.applied_range()==std::array<double,2>{18,19} &&
        legacy_replay.indices() && *legacy_replay.indices()==std::array<int,2>{18,19} &&
        legacy_replay.candidate_range() && *legacy_replay.candidate_range()==std::array<double,2>{18,19},
        "Legacy V2 replay must retain literal resolved/applied/candidate bounds and indices");
  check(legacy_replay.outcome()==AutoContrastOutcome::HistogramBounds &&
        legacy_replay.cycle().previous==0 && legacy_replay.cycle().used==5000 &&
        legacy_replay.cycle().next==5000 && legacy_replay.channels()==std::vector<int>{0},
        "Legacy V2 replay must retain literal outcome, cycle and target channel");
  check(!legacy_replay.requires_captured_state() &&
        legacy_replay.effect().action()==ChannelDisplayAction::Set && legacy_replay.effect().state() &&
        legacy_replay.effect().state()->channels.size()==1 &&
        legacy_replay.effect().state()->channels[0].range==std::array<double,2>{18,19},
        "Legacy V2 replay must own the stored Set effect with replay source guards");

  // Independent intrinsic storage: 104 set bits at origin(-2,0) cover the
  // complete 100x1 source plus two off-image cells on each side. The literal
  // base64 encodes 13 bytes of 0xff; there are no unused tail bits.
  auto regional_v2=legacy_v2;
  auto regional_roi=object(regional_v2.at("roi"));
  const MetadataObject regional_wire{
      {"kind",std::string("region")},{"points",metadata_array({})},{"width",1.},
      {"attributes",metadata_object({{"retained",metadata_array({true,std::uint64_t(-1),std::string("exact")})}})},
      {"shape",metadata_array({1,104})},{"mask",std::string("/////////////////w==")},
      {"origin",metadata_array({-2,0})}};
  regional_roi["selection"]=metadata_object(regional_wire);
  regional_v2["roi"]=metadata_object(regional_roi);
  validate_auto_contrast_command_record(regional_v2);
  const auto regional_replay=auto_contrast_command_from_record(before,regional_v2);
  const auto &provenance=regional_replay.selection_provenance();
  check(provenance && provenance->is_region() && provenance->mask_width()==104 &&
        provenance->mask_height()==1 && provenance->mask_origin()==std::array<int,2>{-2,0} &&
        provenance->packed_mask()==std::vector<std::uint8_t>(13,0xff) &&
        metadata_equal(provenance->attributes,selected.attributes) &&
        metadata_equal(regional_replay.record(),regional_v2),
        "V2 replay must preserve literal full regional storage and historical attributes");
  check(regional_replay.resolved_range()==std::array<double,2>{18,19} &&
        regional_replay.applied_range()==std::array<double,2>{18,19} &&
        regional_replay.indices() && *regional_replay.indices()==std::array<int,2>{18,19} &&
        regional_replay.candidate_range() && *regional_replay.candidate_range()==std::array<double,2>{18,19},
        "Regional V2 replay must retain literal resolved/applied/candidate bounds and indices");
  check(regional_replay.outcome()==AutoContrastOutcome::HistogramBounds &&
        regional_replay.cycle().previous==0 && regional_replay.cycle().used==5000 &&
        regional_replay.cycle().next==5000 && regional_replay.channels()==std::vector<int>{0},
        "Regional V2 replay must retain literal outcome, cycle and target channel");
  check(!regional_replay.requires_captured_state() &&
        regional_replay.effect().action()==ChannelDisplayAction::Set && regional_replay.effect().state() &&
        regional_replay.effect().state()->channels.size()==1 &&
        regional_replay.effect().state()->channels[0].range==std::array<double,2>{18,19},
        "Regional V2 replay must own the stored Set effect with replay source guards");

  std::vector<MetadataObject> bad_regional;
  bad_regional.push_back(replace_record(regional_v2,{"version"},std::int64_t(1)));
  bad_regional.push_back(replace_record(regional_v2,{"roi","selection","origin"},{},true));
  const std::vector<std::pair<std::string,MetadataValue>> regional_mutations{
      {"origin",metadata_array({true,0})},{"origin",metadata_array({-2.0,0})},
      {"origin",metadata_array({std::uint64_t(2147483648ULL),0})},
      {"origin",metadata_array({1,0})}, // 99 visible members contradict captured100.
      {"shape",metadata_array({1,103})}, // The unchanged packed tail is noncanonical.
      {"points",metadata_array({metadata_array({-2.,0.})})},
      {"attributes",true},{"extra",true},{"mask",std::string("?")},
      {"mask",std::string("AAAAAAAAAAAAAAAAAA==")}}; // 13 zero bytes contradict counts.
  for(const auto &mutation:regional_mutations)
    bad_regional.push_back(replace_record(regional_v2,{"roi","selection",mutation.first},mutation.second));
  for(const auto &record:bad_regional) {
    const auto expected=invalid_message([&]{validate_auto_contrast_command_record(record);});
    check(invalid_message([&]{auto_contrast_command_from_record(foreign_before,record);})==expected,
          "Malformed V2 or downgraded region must refuse before foreign-source binding");
    check(invalid_message([&]{rebind_auto_contrast_command(foreign_before,record,invalid,{},control);})==expected,
          "Malformed V2 or downgraded region must refuse before replacement request/callbacks");
  }
  check(callbacks==0 && value.feed->reads==source_reads && value.feed->regions==0 &&
        foreign.feed->reads==0 && foreign.feed->regions==0,
        "V2 validation/replay/refusal must not acquire samples or dispatch callbacks");
  unchanged(*value.owner,before,history);unchanged(*foreign.owner,foreign_before,foreign_history);
}

void binding() {
  for(const std::string changed:{"pixels","metadata","selection","display"}) {
    named("B.fresh_guard."+changed);
    auto value=fixture(PixelType::UInt8,repeats({{1,1},{2,10},{3,11},{4,78}}),3,
        ChannelDisplayMode::Composite,true,1,ChannelDisplayVersion::V2,ChannelDisplayProjection::Min);
    const auto before=value.owner->snapshot();const auto command=resolve_auto_contrast_command(before);
    const auto original=command.record();
    if(changed=="pixels") {
      auto edit=value.owner->begin_edit();auto pixels=*value.feed->pixels;pixels.pixels[0]=9;
      edit.replace_plane({1,1,1},std::move(pixels));value.owner->commit(edit);
    } else if(changed=="metadata") {
      auto edit=value.owner->begin_edit();edit.set_title("metadata changed");value.owner->commit(edit);
    } else if(changed=="selection") value.owner->select(Selection("point",{{0,0}}));
    else {
      auto view=value.owner->snapshot().display();view.t=0;view.c=2;view.crosshair[2]=0;view.zoom=3;
      value.owner->set_display(view);
    }
    const auto current=value.owner->snapshot();const auto history=value.owner->history();const auto reads=value.feed->reads;
    value.feed->forbid=true;
    rejects<RevisionConflict>([&]{command_display_state(command,current);},"Fresh Auto requires all four exact captured revisions");
    if(changed=="pixels" || changed=="metadata")
      rejects<RevisionConflict>([&]{auto_contrast_command_from_record(current,original);},"Replay remains strictly Pixels/Metadata bound");
    else {
      const auto replay=auto_contrast_command_from_record(current,original);
      const auto view=command_display_state(replay,current);
      check(!replay.requires_captured_state() && metadata_equal(replay.record(),original) &&
            replay.selection_dependency_revision()==before.revisions().selection &&
            replay.display_dependency_revision()==before.revisions().display &&
            replay.position().t==1 && replay.position().c==1 && replay.position().z==1,
            "Replay retains historical selection/display/TCZ provenance rather than fresh dependencies");
      check(view.t==current.display().t && view.c==current.display().c &&
            view.crosshair==current.display().crosshair && view.zoom==current.display().zoom &&
            recognized_channel_display(view,current.data().dataset)->channels[1].range==std::array<double,2>{1,2},
            "Stored complete Set effect preserves current navigation and cameras");
    }
    check(value.feed->reads==reads,"Fresh guard/replay refusal or proposal cannot read a plane");
    unchanged(*value.owner,current,history);
  }

  named("B.foreign_and_same_revision_descriptors");
  auto old=fixture(PixelType::UInt8,repeats({{1,1},{2,10},{3,11},{4,78}}));
  const auto source=old.owner->snapshot();const auto command=resolve_auto_contrast_command(source);
  auto foreign=fixture(PixelType::UInt8,std::vector<double>(100,7));foreign.feed->forbid=true;
  rejects([&]{auto_contrast_command_from_record(foreign.owner->snapshot(),command.record());},"A new owner is never implicitly rebound");
  rejects([&]{command_display_state(command,foreign.owner->snapshot());},"Fresh Auto cannot publish on a foreign owner");
  for(const auto &profile:std::vector<std::pair<PixelType,int>>{{PixelType::UInt16,100},{PixelType::UInt8,101}}) {
    auto feed=std::make_shared<Feed>(image(profile.first,std::vector<double>(std::size_t(profile.second),7)),1,false);
    feed->forbid=true;auto data=std::make_shared<DocumentData>(source.data());data->dataset=Dataset(feed,calibration());
    DocumentSnapshot inconsistent(old.owner->identity,source.revisions(),data,source.display(),source.dirty());
    rejects([&]{auto_contrast_command_from_record(inconsistent,command.record());},"Same-revision shape/type mismatch refuses");
    check(feed->reads==0,"Descriptor mismatch needs no source samples");
  }
  auto finite_float=fixture(PixelType::Float32,std::vector<double>(100,7));
  const auto finite_source=finite_float.owner->snapshot();const auto finite_command=resolve_auto_contrast_command(finite_source);
  auto quiet=eager(PixelType::Float32,std::vector<double>(100,7),FloatingSamplePolicy::PreserveQuietNaNFloat32);
  auto policy_data=std::make_shared<DocumentData>(finite_source.data());policy_data->dataset=quiet->snapshot().data().dataset;
  DocumentSnapshot inconsistent_policy(finite_float.owner->identity,finite_source.revisions(),policy_data,
                                       finite_source.display(),finite_source.dirty());
  rejects([&]{auto_contrast_command_from_record(inconsistent_policy,finite_command.record());},
          "Same-revision floating sample-policy mismatch refuses independently of dtype");
  check(foreign.feed->reads==0,"Foreign replay needs no destination samples");

  named("B.complete_explicit_rebind");
  auto destination=fixture(PixelType::UInt16,std::vector<double>(100,7),3,ChannelDisplayMode::Color,true,1,
      ChannelDisplayVersion::V2,ChannelDisplayProjection::Sum);
  destination.owner->select(Selection::from_packed_mask(100,1,std::vector<std::uint8_t>(13,0)));
  AutoContrastRequest replacement;replacement.previous_divisor=5000;replacement.scope=AutoContrastScope::CompositeAll;
  replacement.roi=AutoContrastRoiPolicy::WholePlane;replacement.unsigned16_bits=10;
  const auto old_record=command.record();const auto before=destination.owner->snapshot();const auto history=destination.owner->history();
  const auto rebound=rebind_auto_contrast_command(before,old_record,replacement);
  range(rebound,{0,1023},AutoContrastOutcome::ResetFallback,0);
  check(rebound.source().same_document(before) && rebound.cycle().previous==5000 && rebound.cycle().used==2500 &&
        rebound.channels()==std::vector<int>({0,1,2}) && rebound.position().t==1 && rebound.position().c==1 &&
        rebound.position().z==1 && rebound.request().unsigned16_bits==10 && !rebound.selection_provenance() &&
        rebound.effect().state()->mode==ChannelDisplayMode::Color && rebound.effect().state()->version==ChannelDisplayVersion::V2 &&
        rebound.effect().state()->projection==ChannelDisplayProjection::Sum && metadata_equal(command.record(),old_record),
        "Full rebind recomputes new settings/cycle/position/selection/state without borrowing old result");
  once(destination,{1,1,1});unchanged(*destination.owner,before,history);
  replacement.previous_divisor=-1;std::size_t callbacks=0;ExecutionControl control;control.checkpoint=[&]{++callbacks;};
  rejects([&]{rebind_auto_contrast_command(before,old_record,replacement,{},control);},"Valid old record cannot repair invalid explicit replacement");
  check(callbacks==0 && destination.feed->reads==1,"Invalid replacement refuses before callbacks/reads");

  named("B.native_v3_record_new_owner");
  auto saved=eager(PixelType::UInt8,repeats({{18,1},{19,10},{20,11},{21,78}}));
  const auto saved_command=resolve_auto_contrast_command(saved->snapshot());
  saved->set_display(command_display_state(saved_command,saved->snapshot()));
  SessionCapture capture{{saved->snapshot()},{},{metadata_object(saved_command.record())},{}};
  const auto reopened=decode_session(encode_session(capture));
  check(reopened.documents.size()==1 && reopened.workflow.size()==1 && reopened.documents[0]->id!=saved->id &&
        metadata_equal(object(reopened.workflow[0]),saved_command.record()),
        "V3 preserves exact Auto record but creates a new live owner");
  rejects([&]{auto_contrast_command_from_record(reopened.documents[0]->snapshot(),object(reopened.workflow[0]));},
          "Reopened historical record requires explicit rebind");
  const auto rebound_saved=rebind_auto_contrast_command(reopened.documents[0]->snapshot(),object(reopened.workflow[0]),{});
  range(rebound_saved,{18,19},AutoContrastOutcome::HistogramBounds,5000,
        std::array<int,2>{18,19},std::array<double,2>{18,19});
  check(rebound_saved.cycle().previous==0,"New controller/headless request does not restore recorded live cycle");

  named("B.selection_history_metadata_preservation");
  auto kept=fixture(PixelType::UInt16,std::vector<double>(100,7));
  auto edit=kept.owner->begin_edit();
  edit.set_property("opaque",metadata_array({true,std::uint64_t(-1),std::string("unchanged")}));
  edit.select(Selection("rectangle",{{0,0},{100,1}}));
  edit.set_previous_selection(Selection("point",{{1,0}}));
  Roi roi{"positioned",Selection("rectangle",{{0,0},{2,1}})};roi.t=0;roi.c=0;roi.z=0;roi.attributes["kept"]=true;
  edit.set_rois({roi});edit.set_overlays({{{"kind",std::string("text")},{"text",std::string("kept")}}});
  kept.owner->commit(edit);kept.owner->mark_saved();
  auto redo=kept.owner->begin_edit();redo.set_title("redo retained");kept.owner->commit(redo);kept.owner->undo();
  const auto before_kept=kept.owner->snapshot();const auto history_kept=kept.owner->history();
  check(history_kept.redo_steps>0 && !before_kept.dirty(),"Preservation fixture has clean populated redo");
  const auto result=resolve_auto_contrast_command(before_kept);
  (void)command_display_state(result,before_kept);
  unchanged(*kept.owner,before_kept,history_kept);once(kept);
  check(result.selection_provenance()==before_kept.data().selection &&
        kept.feed->pixels->pixels==std::vector<double>(100,7),"Resolved provenance does not change source annotations/raw values");
}

void resources() {
  named("L.invalid_requests_and_state");
  auto value=fixture(PixelType::UInt8,repeats({{1,1},{2,10},{3,11},{4,78}}));
  std::vector<AutoContrastRequest> invalid;
  for(int divisor:{-1,1,10,4999,5001}){AutoContrastRequest r;r.previous_divisor=divisor;invalid.push_back(r);}
  for(int bits_value:{-1,1,9,11,13,17}){AutoContrastRequest r;r.unsigned16_bits=bits_value;invalid.push_back(r);}
  AutoContrastRequest bad;bad.scope=static_cast<AutoContrastScope>(99);invalid.push_back(bad);
  bad={};bad.roi=static_cast<AutoContrastRoiPolicy>(99);invalid.push_back(bad);
  std::size_t calls=0;ExecutionControl control;control.checkpoint=[&]{++calls;};
  for(const auto &r:invalid){rejects([&]{estimate_auto_contrast_command(value.owner->snapshot(),r);},"Bad Auto request metadata admitted");
    rejects([&]{resolve_auto_contrast_command(value.owner->snapshot(),r,{},control);},"Bad Auto request resolved");}
  auto absent=value.owner->snapshot().display();absent.properties.erase("channel_display");
  auto source=value.owner->snapshot().with_display(absent);
  rejects([&]{resolve_auto_contrast_command(source,{}, {},control);},"Auto never implicitly enables absent state");
  absent.properties["channel_display"]=metadata_object({{"schema",std::string("future")},{"version",9}});
  source=value.owner->snapshot().with_display(absent);
  rejects([&]{resolve_auto_contrast_command(source,{}, {},control);},"Auto refuses interpretation of opaque future state");
  check(calls==0 && value.feed->reads==0,"Invalid request/state admission precedes callbacks/provider");

  for(const std::string cap:{"pixels","working","record","vertices"}) {
    named("L.positive_cap."+cap);AutoContrastLimits limits;
    if(cap=="pixels")limits.max_pixels=0;else if(cap=="working")limits.max_working_bytes=0;
    else if(cap=="record")limits.max_record_bytes=0;else limits.max_selection_vertices=0;
    rejects([&]{resolve_auto_contrast_command(value.owner->snapshot(),{},limits,control);},"Every Auto limit must be positive");
    check(calls==0 && value.feed->reads==0,"Zero-cap refusal has no callbacks/reads");
  }

  named("L.estimate_and_exact_working_boundary");
  const auto estimate=estimate_auto_contrast_command(value.owner->snapshot());
  check(value.feed->reads==0 && estimate.histogram.pixel_count==100 && estimate.histogram.plane_bytes==800 &&
        estimate.histogram.histogram_bytes==4104 && estimate.record_bytes>0 && estimate.display_bytes>0 &&
        estimate.retained_bytes>0 && estimate.working_bytes>=estimate.retained_bytes,
        "Auto metadata estimate includes histogram/owned display/record/retention");
  AutoContrastLimits exact;exact.max_working_bytes=estimate.working_bytes;exact.max_record_bytes=estimate.record_bytes;
  const auto result=resolve_auto_contrast_command(value.owner->snapshot(),{},exact);
  check(result.estimate().working_bytes<=estimate.working_bytes,"Admitted final command remains within estimated simultaneous bound");once(value);
  auto short_limit=exact;short_limit.max_working_bytes=estimate.working_bytes-1;
  rejects([&]{resolve_auto_contrast_command(value.owner->snapshot(),{},short_limit,control);},"One-byte-short working cap must refuse");
  short_limit={};short_limit.max_record_bytes=1;
  rejects([&]{resolve_auto_contrast_command(value.owner->snapshot(),{},short_limit,control);},"Small normalized-record cap must refuse");
  short_limit={};short_limit.max_pixels=99;
  rejects([&]{resolve_auto_contrast_command(value.owner->snapshot(),{},short_limit,control);},"Captured whole XY must fit pixel limit");
  check(calls==0 && value.feed->reads==1,"Budget refusals precede any further callbacks/provider reads");

  named("L.selection_and_flat_display_charge");
  auto selected=fixture(PixelType::UInt8,std::vector<double>(100,7));
  selected.owner->select(Selection("polygon",{{0,0},{100,0},{100,1},{0,1}}));
  AutoContrastLimits vertices;vertices.max_selection_vertices=3;
  rejects([&]{resolve_auto_contrast_command(selected.owner->snapshot(),{},vertices,control);},"Complete original selection vertex bound is mandatory");
  const auto plain=estimate_auto_contrast_command(selected.owner->snapshot());
  auto view=selected.owner->snapshot().display();view.properties["large_unrelated"]=std::string(65536,'x');selected.owner->set_display(view);
  const auto large=estimate_auto_contrast_command(selected.owner->snapshot());
  check(large.display_bytes>=plain.display_bytes+65536 && large.working_bytes>plain.working_bytes+65536,
        "Unrelated flat display strings cannot evade owned snapshot/copy accounting");
  AutoContrastLimits bounded;bounded.max_working_bytes=plain.working_bytes;
  rejects([&]{resolve_auto_contrast_command(selected.owner->snapshot(),{},bounded,control);},"Large owned display capture refuses before clone/read/callback");
  check(calls==0 && selected.feed->reads==0,"Metadata and vertex refusals acquire no source samples");

  named("L.bounded_original_dag_and_vertices");
  auto original=fixture(PixelType::UInt8,repeats({{18,1},{19,10},{20,11},{21,78}}));
  original.owner->select(Selection("polygon",{{0,0},{100,0},{100,1},{0,1}}));
  const auto record=resolve_auto_contrast_command(original.owner->snapshot()).record();original.feed->forbid=true;
  rejects([&]{validate_auto_contrast_command_record(record,vertices);},"Replay vertex preflight applies before decoded selection copy");
  // Logical expansion is deliberately finite (4^8 leaves), even if a broken
  // visitor incorrectly charges shared nodes only once.
  MetadataValue dag=std::string(64,'x');for(int i=0;i<8;++i)dag=metadata_array({dag,dag,dag,dag});
  const auto amplified=replace_record(record,{"roi","selection","dag"},dag);AutoContrastLimits record_cap;record_cap.max_record_bytes=1024*1024;
  validate_auto_contrast_command_record(record,record_cap);
  rejects([&]{validate_auto_contrast_command_record(amplified,record_cap);},"Repeated immutable subtrees are charged per logical occurrence before expansion");
  check(original.feed->reads==1,"Record resource validation has no source acquisition");
}

struct CallbackFailure : std::runtime_error { CallbackFailure():std::runtime_error("owned Auto callback failure"){} };
void controls() {
  named("C.precancel_no_callbacks_reads");
  auto value=fixture(PixelType::UInt8,repeats({{1,1},{2,10},{3,11},{4,78}}));
  auto token=std::make_shared<CancellationToken>();token->cancel();std::size_t calls=0;
  ExecutionControl control;control.cancellation=token;control.checkpoint=[&]{++calls;};
  control.progress=[&](std::size_t,std::size_t){++calls;};
  control.stage_progress=[&](std::string_view,std::size_t,std::size_t){++calls;};
  rejects<OperationCancelled>([&]{resolve_auto_contrast_command(value.owner->snapshot(),{}, {},control);},"Valid precanceled Auto refuses");
  check(calls==0 && value.feed->reads==0,"Valid precancellation has no callbacks/provider reads");

  for(const std::string hook:{"checkpoint","progress","stage"}) {
    named("C.cancel."+hook);auto current=fixture(PixelType::UInt8,repeats({{1,1},{2,10},{3,11},{4,78}}));
    const auto before=current.owner->snapshot();const auto history=current.owner->history();
    auto cancel=std::make_shared<CancellationToken>();bool entered=false;ExecutionControl c;c.cancellation=cancel;
    if(hook=="checkpoint")c.checkpoint=[&]{entered=true;cancel->cancel();};
    else if(hook=="progress")c.progress=[&](std::size_t,std::size_t){entered=true;cancel->cancel();};
    else c.stage_progress=[&](std::string_view,std::size_t done,std::size_t){if(done>0){entered=true;cancel->cancel();}};
    rejects<OperationCancelled>([&]{resolve_auto_contrast_command(before,{}, {},c);},"Mid-operation cancellation discards detached Auto result");
    check(entered && current.feed->reads<=(hook=="checkpoint" || hook=="progress"?0u:1u),
          "Cancellation stays bounded and never reacquires the plane");unchanged(*current.owner,before,history);
  }

  for(const std::string hook:{"checkpoint","progress","stage"}) {
    named("C.exception."+hook);auto current=fixture(PixelType::UInt8,repeats({{1,1},{2,10},{3,11},{4,78}}));
    const auto before=current.owner->snapshot();const auto history=current.owner->history();bool entered=false;ExecutionControl c;
    const auto fail=[&]{entered=true;throw CallbackFailure();};
    if(hook=="checkpoint")c.checkpoint=fail;
    else if(hook=="progress")c.progress=[&](std::size_t,std::size_t){fail();};
    else c.stage_progress=[&](std::string_view,std::size_t,std::size_t){fail();};
    rejects<CallbackFailure>([&]{resolve_auto_contrast_command(before,{}, {},c);},"Exact user callback exception must propagate");
    check(entered && current.feed->reads<=1,"Callback failure performs at most one acquired-plane read");unchanged(*current.owner,before,history);
  }

  named("C.reentry_and_all_mutable_capture");
  auto original=fixture(PixelType::UInt16,std::vector<double>(100,7));
  auto foreign=fixture(PixelType::UInt8,repeats({{1,1},{2,10},{3,11},{4,78}}));
  DocumentSnapshot source=original.owner->snapshot();const auto captured_source=source;
  AutoContrastRequest request;request.unsigned16_bits=12;AutoContrastLimits limits;
  bool entered=false;std::size_t nested=0;ExecutionControl captured;
  captured.cancellation=std::make_shared<CancellationToken>();
  captured.checkpoint=[&]{
    if(entered)return;entered=true;
    source=foreign.owner->snapshot();request.previous_divisor=5000;request.unsigned16_bits=0;
    request.scope=AutoContrastScope::CompositeAll;request.roi=AutoContrastRoiPolicy::WholePlane;
    limits.max_working_bytes=1;original.owner->select(Selection("point",{{0,0}}));
    auto canceled=std::make_shared<CancellationToken>();canceled->cancel();captured.cancellation=canceled;
    captured.checkpoint=[]{throw CallbackFailure();};
    Stack planes;planes.push_back(image(PixelType::UInt8,{0,1,2,3}));
    const auto child=Dataset::from_planes({1,1,1,1,4,1},std::move(planes));
    nested=accumulate_dataset_histogram(child).included_count;
  };
  const auto result=resolve_auto_contrast_command(source,request,limits,captured);
  range(result,{0,4095},AutoContrastOutcome::ResetFallback,0);
  check(entered && nested==4 && result.source().same_document(captured_source) && result.request().previous_divisor==0 &&
        result.request().unsigned16_bits==12 && result.request().scope==AutoContrastScope::ActiveChannel &&
        result.request().roi==AutoContrastRoiPolicy::ActiveSelection && !result.selection_provenance() &&
        result.selection_dependency_revision()==captured_source.revisions().selection && foreign.feed->reads==0,
        "All effective source/request/limits/control/selection values precede callbacks and reentrant native work");
  rejects<RevisionConflict>([&]{command_display_state(result,original.owner->snapshot());},
                           "Reentrant live selection edit makes fresh result stale rather than redirecting it");once(original);

  // Reenter on every checkpoint, including checkpoints inside Auto's histogram.
  // A duplicated outer/inner control must fail at depth2 rather than recurse.
  auto always=fixture(PixelType::UInt8,repeats({{1,1},{2,10},{3,11},{4,78}}));
  const auto always_before=always.owner->snapshot();const auto always_history=always.owner->history();
  std::size_t always_depth=0,always_calls=0,always_completed=0;
  ExecutionControl every_checkpoint;
  every_checkpoint.checkpoint=[&]{
    if(always_depth!=0 || always_calls>=512)throw CallbackFailure();
    ++always_depth;++always_calls;
    struct ExitDepth {std::size_t &depth;~ExitDepth(){--depth;}} exit{always_depth};
    Stack planes;planes.push_back(image(PixelType::UInt8,{0,1,2,3}));
    const auto child=Dataset::from_planes({1,1,1,1,4,1},std::move(planes));
    always_completed+=accumulate_dataset_histogram(child).included_count;
  };
  const auto always_result=resolve_auto_contrast_command(always_before,{}, {},every_checkpoint);
  range(always_result,{1,2},AutoContrastOutcome::HistogramBounds,5000,
        std::array<int,2>{1,2},std::array<double,2>{1,2});
  check(always_depth==0 && always_calls>1 && always_calls<=512 && always_completed==4*always_calls,
        "Every Auto checkpoint reenters native Dataset/histogram work once without ancestor redispatch");
  once(always);unchanged(*always.owner,always_before,always_history);

  named("C.provider_failure_is_not_fallback");
  auto unread=fixture(PixelType::UInt16,std::vector<double>(100,7));unread.feed->forbid=true;
  const auto before=unread.owner->snapshot();const auto history=unread.owner->history();
  bool failed=false;try{resolve_auto_contrast_command(before);}catch(const std::runtime_error &error){failed=std::string(error.what())=="Forbidden Auto pixel acquisition";}
  check(failed && unread.feed->reads==1,"Provider exception propagates, never manufacturing fallback");unchanged(*unread.owner,before,history);
}
} // namespace

int main() {
  try {
    literals();dimensions();records();binding();resources();controls();
    check(cases.size()==64,"Frozen no-argument Auto scenario membership changed");
    auto unique=cases;std::sort(unique.begin(),unique.end());
    check(std::adjacent_find(unique.begin(),unique.end())==unique.end(),"Every emitted Auto case identity must be unique");
    std::cout<<"auto_contrast: "<<checks<<" checks; "<<cases.size()<<" named cases; sections literals/dimensions/records/binding/resources/controls\n";
    for(const auto &value:cases)std::cout<<"case "<<value<<'\n';
    return 0;
  } catch(const std::exception &error) {
    std::cerr<<"auto_contrast after "<<checks<<" checks: "<<error.what()<<'\n';return 1;
  }
}
