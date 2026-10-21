// Public installed-core consumer. The mapping oracle uses literal image rows,
// not the display mapper, LUT builder, or reset helper under test.
#include <imagejxx/channel_display_command.hpp>
#include <imagejxx/session.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using namespace imagejxx;
std::size_t finite_rows = 0, edge_cases = 0, command_cases = 0;
std::size_t session_cases = 0, shape_cases = 0;
const auto started = std::chrono::steady_clock::now();
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
  if (std::chrono::steady_clock::now() - started > std::chrono::seconds(45))
    throw std::runtime_error("SDK channel display exceeded 45 seconds");
}
template<class Exception = std::invalid_argument, class F>
void rejects(F work, const char *message) {
  bool caught = false;
  try { work(); } catch (const Exception &) { caught = true; }
  require(caught, message);
}
const MetadataObject &object(const MetadataValue &value) {
  return std::get<MetadataObject>(
      std::get<std::shared_ptr<const MetadataTree>>(value)->value);
}
const MetadataArray &array(const MetadataValue &value) {
  return std::get<MetadataArray>(
      std::get<std::shared_ptr<const MetadataTree>>(value)->value);
}
SpatialCalibration calibration() {
  SpatialCalibration result;
  result.spacing = {.5, 1.25, 2.5}; result.origin = {11, -7, 19};
  result.direction = {-1, .25, .5, 0, 1, .5, .5, 0, 1};
  result.unit = "um"; result.frame_interval = .75;
  result.time_origin = 2.25; result.time_unit = "s";
  return result;
}
bool same_calibration(const SpatialCalibration &a, const SpatialCalibration &b) {
  return a.spacing == b.spacing && a.origin == b.origin &&
         a.direction == b.direction && a.unit == b.unit &&
         a.frame_interval == b.frame_interval && a.time_origin == b.time_origin &&
         a.time_unit == b.time_unit;
}
double literal(int t, int c, int z, int y, int x) {
  return 40*t + 30*c + 5*z + 2*y + x;
}
Dataset fixture(PixelType type, int channels = 3) {
  Stack planes;
  for (int t = 0; t < 2; ++t) for (int c = 0; c < channels; ++c)
    for (int z = 0; z < 4; ++z) {
      Image image(5, 3, 1, type);
      for (int y = 0; y < 3; ++y) for (int x = 0; x < 5; ++x)
        image.at(x, y) = literal(t, c, z, y, x);
      planes.push_back(std::move(image));
    }
  return Dataset::from_planes({2, channels, 4, 3, 5, 1},
                              std::move(planes), calibration());
}
ChannelDisplayState palette(int count = 3,
                            ChannelDisplayMode mode = ChannelDisplayMode::Composite) {
  ChannelDisplayState result;
  result.mode = mode; result.channels.resize(static_cast<std::size_t>(count));
  const char *names[] = {"Red", "Green", "Blue"};
  for (int c = 0; c < count; ++c) {
    result.channels[c].range = {0, 255};
    result.channels[c].lut.name = names[c % 3];
  }
  return result;
}
DisplayState view(const Dataset &dataset) {
  DisplayState result;
  result.t = 1; result.c = dataset.shape().c > 2 ? 2 : 0;
  result.crosshair = {3, 1, 1}; result.orthogonal = true;
  result.zoom = 1.25; result.linkedZoom = false;
  result.planeZoom = {1.25, 2, 3}; result.pan = {{{3, -2}, {1, 4}, {-5, 2}}};
  result.properties["display_range"] = metadata_array({7., 19.});
  result.properties["lut"] = std::string("legacy-retained");
  result.properties["unrelated"] = metadata_object({{"future", std::uint64_t(9007199254740993ULL)}});
  return result;
}
bool same_navigation(const DisplayState &a, const DisplayState &b) {
  return a.t == b.t && a.c == b.c && a.crosshair == b.crosshair &&
         a.orthogonal == b.orthogonal && a.linkedZoom == b.linkedZoom &&
         a.zoom == b.zoom && a.planeZoom == b.planeZoom && a.pan == b.pan;
}
struct FrozenPixels {
  Dataset dataset;
  std::vector<std::shared_ptr<const Image>> planes;
  std::vector<std::vector<double>> samples;
  explicit FrozenPixels(const Dataset &source) : dataset(source) {
    const auto shape = source.shape();
    for (int t = 0; t < shape.t; ++t) for (int c = 0; c < shape.c; ++c)
      for (int z = 0; z < shape.z; ++z) {
        planes.push_back(source.plane({t, c, z}));
        samples.push_back(planes.back()->pixels);
      }
  }
  void unchanged(const Dataset &source) const {
    require(source.shape().dimensions() == dataset.shape().dimensions() &&
            source.pixel_type() == dataset.pixel_type() &&
            source.sample_policy() == dataset.sample_policy() &&
            same_calibration(source.calibration(), dataset.calibration()),
            "SDK display changed source TCZYXS/type/policy/affine/time");
    const auto shape = source.shape(); std::size_t i = 0;
    for (int t = 0; t < shape.t; ++t) for (int c = 0; c < shape.c; ++c)
      for (int z = 0; z < shape.z; ++z, ++i) {
        const auto current = source.plane({t, c, z});
        require(current == planes[i] && current->pixels.data() == planes[i]->pixels.data() &&
                current->pixels.size() == samples[i].size() &&
                std::memcmp(current->pixels.data(), samples[i].data(),
                            samples[i].size()*sizeof(double)) == 0,
                "SDK display mutated/replaced borrowed source pixels");
      }
  }
};
void unchanged_owner(const Document &source, const DocumentSnapshot &before,
                     const HistoryInfo &history, bool display_changed) {
  const auto now = source.snapshot();
  require(now.shares_data(before) && now.data_identity() == before.data_identity() &&
          now.revisions().matches(before.revisions(), Change::Data) &&
          now.dirty() == before.dirty(), "SDK presentation changed data/revisions/dirty identity");
  const auto after_history = source.history();
  require(after_history.undo_steps == history.undo_steps &&
          after_history.redo_steps == history.redo_steps &&
          after_history.step_limit == history.step_limit &&
          after_history.pixel_budget == history.pixel_budget,
          "SDK presentation changed history/policy");
  if (!display_changed)
    require(now.revisions().matches(before.revisions(), Change::All) &&
            now.display().revision == before.display().revision &&
            same_navigation(now.display(), before.display()) &&
            metadata_equal(now.display().properties, before.display().properties),
            "SDK read-only display operation changed presentation");
  else
    require(now.revisions().display == before.revisions().display + 1 &&
            now.display().revision == before.display().revision + 1,
            "SDK display publication did not advance only display revision");
}
void rgb(const Image &image, int width, int height) {
  require(image.width == width && image.height == height && image.channels == 3 &&
          image.type == PixelType::UInt8 && image.pixels.size() == std::size_t(width*height*3),
          "SDK display output is not a fresh U8 RGB plane");
}
void matrix() {
  // For integer samples below 255 and range [0,255], the pinned U8/F32 maps
  // and U16's denominator 256 all yield the input index exactly. These are
  // literal C0 rows at T1 and fixed coordinate1; C1/C2 add 30/60.
  const std::array<std::vector<int>, 3> base{{
      {45,46,47,48,49, 47,48,49,50,51, 49,50,51,52,53},
      {42,43,44,45,46, 47,48,49,50,51, 52,53,54,55,56, 57,58,59,60,61},
      {41,43,45, 46,48,50, 51,53,55, 56,58,60}}};
  const std::array<Plane, 3> views{Plane::XY, Plane::XZ, Plane::YZ};
  const std::array<ChannelDisplayMode, 3> modes{
      ChannelDisplayMode::Color, ChannelDisplayMode::Grayscale, ChannelDisplayMode::Composite};
  for (const auto type : {PixelType::UInt8, PixelType::UInt16, PixelType::Float32}) {
    Document source(fixture(type), "Channel display SDK literal");
    source.set_display(view(source.snapshot().data().dataset));
    const auto before = source.snapshot(); const auto history = source.history();
    const FrozenPixels pixels(before.data().dataset);
    for (const auto mode : modes) for (std::size_t axis = 0; axis < views.size(); ++axis) {
      auto output = compose_channel_display(before.data().dataset, palette(3, mode), views[axis], 1, 1, 2);
      rgb(output, axis == 2 ? 3 : 5, axis == 0 ? 3 : 4);
      for (std::size_t i = 0; i < base[axis].size(); ++i) {
        const int v = base[axis][i];
        const std::array<int, 3> expected = mode == ChannelDisplayMode::Composite
            ? std::array<int, 3>{v, v+30, v+60}
            : mode == ChannelDisplayMode::Grayscale ? std::array<int, 3>{v+60, v+60, v+60}
                                                   : std::array<int, 3>{0, 0, v+60};
        for (int s = 0; s < 3; ++s)
          require(output.pixels[3*i+s] == expected[s], "SDK literal RGB mapping mismatch");
      }
      output.pixels[0] = 231; // Returned RGB storage is independently owned.
      pixels.unchanged(source.snapshot().data().dataset);
      unchanged_owner(source, before, history, false);
      ++finite_rows;
    }
  }
}
double quiet_nan() {
  const std::uint32_t bits = 0x7fc12345; float value;
  std::memcpy(&value, &bits, sizeof(value)); return double(value);
}
void edges() {
  auto ordinary = fixture(PixelType::Float32);
  auto planes = ordinary.shared_planes()->materialize();
  planes[13].at(2, 1) = quiet_nan(); // ((T1*C3 + C0)*Z4 + Z1).
  const auto nan_source = Dataset::from_planes({2,3,4,3,5,1}, std::move(planes), calibration(),
                                              FloatingSamplePolicy::PreserveQuietNaNFloat32);
  const FrozenPixels nan_pixels(nan_source);
  auto custom = palette();
  custom.channels[1].visible = custom.channels[2].visible = false;
  custom.channels[0].lut.name.clear();
  ChannelDisplayTable table{}; table[0] = {7,11,13}; custom.channels[0].lut.table = table;
  std::size_t provider_calls = 0;
  const auto borrowed = nan_source.plane({1,0,1});
  auto output = compose_channel_display(nan_source, custom, Plane::XY, 1, 1, 2,
      [&](Plane plane, int coordinate, int t, int c) {
        require(plane == Plane::XY && coordinate == 1 && t == 1 && c == 0,
                "SDK custom NaN provider received wrong source indices");
        ++provider_calls; return borrowed;
      });
  rgb(output, 5, 3);
  for (int y = 0; y < 3; ++y) for (int x = 0; x < 5; ++x)
    for (int s = 0; s < 3; ++s)
      require(output.at(x,y,s) == (x == 2 && y == 1 ? table[0][s] : 0),
              "SDK quiet NaN did not select custom LUT entry zero");
  require(provider_calls == 1, "SDK compositor did not stream the single visible channel");
  output.pixels[0] = 199; nan_pixels.unchanged(nan_source); ++edge_cases;

  Image empty(3,2,1,PixelType::Float32);
  for (auto &sample : empty.pixels) sample = quiet_nan();
  const auto empty_source = Dataset::from_planes({1,1,1,2,3,1}, {empty}, {},
                                               FloatingSamplePolicy::PreserveQuietNaNFloat32);
  const FrozenPixels empty_pixels(empty_source);
  auto empty_state = palette(1, ChannelDisplayMode::Color);
  const auto reset = reset_channel_display_ranges(empty_source, empty_state, 0, 0);
  require(reset.first.channels[0].range == std::array<double,2>{0,1} &&
          reset.second.size() == 1 && reset.second[0].empty_finite &&
          reset.second[0].range == std::array<double,2>{0,1},
          "SDK all-NaN reset lost owned empty-finite policy");
  Document empty_owner(empty_source);
  const auto empty_command = resolve_channel_display_command(empty_owner.snapshot(),
      {ChannelDisplayAction::ResetRange, empty_state, {}});
  const auto &empty_record = object(empty_command.record().at("reset"));
  require(array(empty_record.at("channels")).size() == 1 &&
          std::get<bool>(array(empty_record.at("empty_finite"))[0]),
          "SDK all-NaN command lost empty-finite flag");
  empty_state = reset.first; empty_state.channels[0].lut.name.clear();
  empty_state.channels[0].lut.table = table;
  const auto empty_rgb = compose_channel_display(empty_source, empty_state, Plane::XY, 0, 0, 0);
  rgb(empty_rgb, 3, 2);
  for (std::size_t i = 0; i < empty_rgb.pixels.size(); ++i)
    require(empty_rgb.pixels[i] == table[0][i%3], "SDK all-NaN color output lost custom LUT zero");
  empty_pixels.unchanged(empty_source); ++edge_cases;

  const FrozenPixels ordinary_pixels(ordinary);
  auto hidden = palette(); for (auto &channel : hidden.channels) channel.visible = false;
  auto black = compose_channel_display(ordinary, hidden, Plane::XY, 1, 1, 2);
  rgb(black, 5, 3);
  for (double sample : black.pixels) require(sample == 0, "SDK hidden composite is not black");
  hidden.mode = ChannelDisplayMode::Color;
  const auto active = compose_channel_display(ordinary, hidden, Plane::XY, 1, 1, 2);
  rgb(active, 5, 3);
  for (int y = 0; y < 3; ++y) for (int x = 0; x < 5; ++x)
    require(active.at(x,y,0) == 0 && active.at(x,y,1) == 0 &&
            active.at(x,y,2) == 105+2*y+x, "SDK Color incorrectly used visibility");
  ordinary_pixels.unchanged(ordinary); ++edge_cases;

  const auto singleton = fixture(PixelType::UInt16, 1); const FrozenPixels one_pixels(singleton);
  const auto one = compose_channel_display(singleton, palette(1), Plane::XY, 1, 1, 0);
  rgb(one, 5, 3);
  for (int y = 0; y < 3; ++y) for (int x = 0; x < 5; ++x) for (int s = 0; s < 3; ++s)
    require(one.at(x,y,s) == 45+2*y+x, "SDK singleton Composite did not use grayscale");
  one_pixels.unchanged(singleton); ++edge_cases;

  const auto many = fixture(PixelType::UInt16, 9); const FrozenPixels many_pixels(many);
  const auto requested = palette(9);
  rejects([&]{validate_channel_display(requested, many.shape(), many.pixel_type(), many.sample_policy());},
          "SDK stored Composite with more than eight channels was admitted");
  const auto normalized = normalize_channel_display(requested, many.shape(), many.pixel_type(), many.sample_policy());
  require(normalized.mode == ChannelDisplayMode::Color, "SDK explicit Composite>8 was not normalized");
  auto encoded = encode_channel_display(normalized, many.shape(), many.pixel_type(), many.sample_policy());
  encoded["mode"] = std::string("composite");
  rejects([&]{parse_channel_display(encoded, many.shape(), many.pixel_type(), many.sample_policy());},
          "SDK stored parser normalized invalid Composite>8");
  require(parse_requested_channel_display(encoded, many.shape(), many.pixel_type(), many.sample_policy()).mode ==
              ChannelDisplayMode::Color,
          "SDK explicit requested-state bridge did not normalize Composite>8");
  Document many_owner(many);
  const auto many_command = resolve_channel_display_command(many_owner.snapshot(),
      {ChannelDisplayAction::Set, requested, {}});
  require(many_command.state()->mode == ChannelDisplayMode::Color &&
          std::get<std::string>(object(many_command.record().at("state")).at("mode")) == "color",
          "SDK >8 Set command did not record the normalized Color state");
  const auto many_rgb = compose_channel_display(many, normalized, Plane::YZ, 1, 1, 2);
  rgb(many_rgb, 3, 4);
  for (int z = 0; z < 4; ++z) for (int y = 0; y < 3; ++y)
    require(many_rgb.at(y,z,0) == 0 && many_rgb.at(y,z,1) == 0 &&
            many_rgb.at(y,z,2) == 101+5*z+2*y, "SDK >8 Color output used wrong active channel");
  many_pixels.unchanged(many); ++edge_cases;
}
void commands() {
  Document source(fixture(PixelType::UInt16), "Commands");
  source.set_display(view(source.snapshot().data().dataset));
  auto edit = source.begin_edit(); edit.set_property("history_probe", 7); source.commit(edit);
  source.mark_saved();
  const FrozenPixels pixels(source.snapshot().data().dataset);
  const auto captured = source.snapshot(); const auto history = source.history();
  std::size_t callbacks = 0; ExecutionControl forbidden;
  forbidden.checkpoint = [&]{++callbacks; throw std::runtime_error("metadata-only callback");};
  forbidden.progress = [&](std::size_t, std::size_t){++callbacks;};
  auto state = palette();
  const auto set = resolve_channel_display_command(captured, {ChannelDisplayAction::Set, state, {}}, {}, forbidden);
  validate_channel_display_command_record(set.record());
  require(callbacks == 0 && set.action() == ChannelDisplayAction::Set && set.state().has_value(),
          "SDK Set did not remain metadata-only");
  const auto &binding = object(set.record().at("source"));
  require(std::get<std::uint64_t>(binding.at("id")) == captured.id() &&
          array(binding.at("shape")).size() == 6 &&
          metadata_equal(object(set.record().at("position")), {{"t",1},{"c",2},{"z",1}}) &&
          array(object(set.record().at("reset")).at("channels")).empty(),
          "SDK normalized Set lost source/captured position/reset provenance");
  source.set_display(command_display_state(set, source.snapshot()));
  auto now = source.snapshot();
  require(same_navigation(now.display(), captured.display()) &&
          metadata_equal(now.display().properties.at("unrelated"), captured.display().properties.at("unrelated")) &&
          metadata_equal(now.display().properties.at("display_range"), captured.display().properties.at("display_range")) &&
          metadata_equal(now.display().properties.at("lut"), captured.display().properties.at("lut")),
          "SDK Set discarded unrelated/legacy presentation");
  unchanged_owner(source, captured, history, true); ++command_cases;

  auto moved = source.snapshot().display(); moved.t = 0; moved.c = 1;
  moved.crosshair = {1,2,3}; moved.zoom = 4; source.set_display(moved);
  source.select(Selection("rectangle", {{0,0},{2,2}}));
  const auto before_replay = source.snapshot(); const auto replay_history = source.history();
  const auto replay = channel_display_command_from_record(before_replay, set.record());
  require(metadata_equal(replay.record(), set.record()), "SDK strict replay rewrote resolved record");
  source.set_display(command_display_state(replay, before_replay));
  require(same_navigation(source.snapshot().display(), before_replay.display()) &&
          source.snapshot().data().selection == before_replay.data().selection,
          "SDK replay retargeted navigation/selection");
  unchanged_owner(source, before_replay, replay_history, true); ++command_cases;

  source.set_display(view(source.snapshot().data().dataset));
  const auto before_reset = source.snapshot(); const auto reset_history = source.history();
  const auto reset = resolve_channel_display_command(before_reset,
      {ChannelDisplayAction::ResetRange, state, {2,0}});
  require(reset.state()->channels[2].range == std::array<double,2>{105,113} &&
          reset.state()->channels[0].range == std::array<double,2>{45,53} &&
          reset.state()->channels[1].range == std::array<double,2>{0,255},
          "SDK Reset did not read captured live XY ranges");
  const auto &observations = object(reset.record().at("reset"));
  require(metadata_equal(array(observations.at("channels")), MetadataArray{2,0}) &&
          metadata_equal(array(observations.at("empty_finite")), MetadataArray{false,false}),
          "SDK Reset lost explicit channel order/finite observations");
  moved = before_reset.display(); moved.t = 0; moved.c = 0; moved.crosshair[2] = 0;
  const auto detached = before_reset.with_display(moved);
  const auto replay_reset = channel_display_command_from_record(detached, reset.record());
  require(replay_reset.state()->channels[2].range == std::array<double,2>{105,113} &&
          same_navigation(command_display_state(replay_reset, detached), moved),
          "SDK Reset replay recomputed Auto or restored old navigation");
  unchanged_owner(source, before_reset, reset_history, false); ++command_cases;
  source.set_display(command_display_state(reset, source.snapshot()));

  const auto before_disable = source.snapshot(); const auto disable_history = source.history();
  require(before_disable.display().properties.count("channel_display") == 1,
          "SDK Disable fixture did not contain an installed channel state");
  const auto disable = resolve_channel_display_command(before_disable,
      {ChannelDisplayAction::Disable, {}, {}}, {}, forbidden);
  require(callbacks == 0 && !disable.state() &&
          std::holds_alternative<std::monostate>(disable.record().at("state")),
          "SDK Disable read pixels or retained state");
  source.set_display(command_display_state(disable, before_disable));
  auto without = before_disable.display().properties; without.erase("channel_display");
  require(metadata_equal(source.snapshot().display().properties, without) &&
          same_navigation(source.snapshot().display(), before_disable.display()),
          "SDK Disable altered unrelated display state");
  unchanged_owner(source, before_disable, disable_history, true); ++command_cases;

  Document destination(fixture(PixelType::UInt16), "Explicit new owner");
  const auto destination_before = destination.snapshot(); const auto destination_history = destination.history();
  rejects([&]{channel_display_command_from_record(destination_before, set.record());},
          "SDK strict replay silently rebound a new owner");
  auto explicit_state = palette(); explicit_state.channels[0].range = {7,11};
  const auto rebound = rebind_channel_display_command(destination_before, reset.record(),
      {ChannelDisplayAction::Set, explicit_state, {}}, {}, forbidden);
  require(rebound.action() == ChannelDisplayAction::Set && callbacks == 0 &&
          rebound.state()->channels[0].range == std::array<double,2>{7,11} &&
          std::get<std::uint64_t>(object(rebound.record().at("source")).at("id")) == destination.id &&
          metadata_equal(object(rebound.record().at("position")), {{"t",0},{"c",0},{"z",0}}),
          "SDK explicit Set rebind retained old binding or recomputed ranges");
  unchanged_owner(destination, destination_before, destination_history, false); ++command_cases;

  const auto rebound_reset = rebind_channel_display_command(destination_before, set.record(),
      {ChannelDisplayAction::ResetRange, explicit_state, {}});
  for (int c = 0; c < 3; ++c)
    require(rebound_reset.state()->channels[c].range == std::array<double,2>{double(30*c),double(30*c+8)},
            "SDK explicit Reset rebind did not use destination live position");
  require(metadata_equal(array(object(rebound_reset.record().at("reset")).at("channels")), MetadataArray{0,1,2}),
          "SDK all-channel rebind reset did not normalize explicit C order");
  unchanged_owner(destination, destination_before, destination_history, false); ++command_cases;

  std::vector<MetadataObject> malformed;
  auto bad = set.record(); bad["version"] = true; malformed.push_back(bad);
  bad = set.record(); bad["unexpected"] = 1; malformed.push_back(bad);
  bad = set.record(); bad["state"] = MetadataValue{}; malformed.push_back(bad);
  bad = set.record(); auto nested = object(bad.at("source")); nested["id"] = 1.;
  bad["source"] = metadata_object(nested); malformed.push_back(bad);
  nested = object(set.record().at("source")); nested["shape"] = metadata_array({2,3,4,3,5,true});
  bad = set.record(); bad["source"] = metadata_object(nested); malformed.push_back(bad);
  nested = object(set.record().at("position")); nested["z"] = std::uint64_t(0xffffffffffffffffULL);
  bad = set.record(); bad["position"] = metadata_object(nested); malformed.push_back(bad);
  nested = object(set.record().at("source")); nested["metadata_revision"] = -1;
  bad = set.record(); bad["source"] = metadata_object(nested); malformed.push_back(bad);
  bad = reset.record(); bad["reset"] = metadata_object({{"channels",metadata_array({2,2})},
                                                       {"empty_finite",metadata_array({false,false})}});
  malformed.push_back(bad);
  bad = reset.record(); bad["reset"] = metadata_object({{"channels",metadata_array({2,0})},
                                                       {"empty_finite",metadata_array({true,false})}});
  malformed.push_back(bad);
  bad = disable.record(); bad["state"] = set.record().at("state"); malformed.push_back(bad);
  for (const auto &record : malformed) {
    rejects([&]{validate_channel_display_command_record(record);}, "SDK malformed original validated");
    rejects([&]{channel_display_command_from_record(destination_before, record);},
            "SDK malformed original bypassed strict replay decoding");
    rejects([&]{rebind_channel_display_command(destination_before, record,
                    {ChannelDisplayAction::Set, state, {}}, {}, forbidden);},
            "SDK explicit rebind repaired malformed original");
  }
  require(callbacks == 0 && malformed.size() == 10, "SDK malformed rebind reached callback");
  unchanged_owner(destination, destination_before, destination_history, false); ++command_cases;

  edit = source.begin_edit(); edit.set_property("metadata_changed", true); source.commit(edit);
  const auto stale_before = source.snapshot(); const auto stale_history = source.history();
  rejects<RevisionConflict>([&]{channel_display_command_from_record(stale_before, set.record());},
                            "SDK strict replay ignored Metadata revision");
  rejects<RevisionConflict>([&]{command_display_state(set, stale_before);},
                            "SDK retained command ignored Metadata revision");
  unchanged_owner(source, stale_before, stale_history, false);
  const auto pixel_command = resolve_channel_display_command(destination.snapshot(),
      {ChannelDisplayAction::Set, state, {}});
  auto pixel_edit = destination.begin_edit();
  auto replacement = *destination.snapshot().data().dataset.plane({0,0,0});
  replacement.at(0,0) = 9; pixel_edit.replace_plane({0,0,0}, std::move(replacement));
  destination.commit(pixel_edit);
  const auto changed = destination.snapshot(); const auto changed_history = destination.history();
  rejects<RevisionConflict>([&]{channel_display_command_from_record(changed, pixel_command.record());},
                            "SDK strict replay ignored Pixels revision");
  rejects<RevisionConflict>([&]{command_display_state(pixel_command, changed);},
                            "SDK retained command ignored Pixels revision");
  unchanged_owner(destination, changed, changed_history, false); ++command_cases;
  pixels.unchanged(source.snapshot().data().dataset);
}
void persistence_and_shape() {
  Document source(fixture(PixelType::Float32), "Native display roundtrip");
  source.set_display(view(source.snapshot().data().dataset));
  const auto set = resolve_channel_display_command(source.snapshot(), {ChannelDisplayAction::Set, palette(), {}});
  source.set_display(command_display_state(set, source.snapshot()));
  const auto before = source.snapshot(); const auto history = source.history(); const FrozenPixels pixels(before.data().dataset);
  SessionCapture capture; capture.documents = {before}; capture.workflow = {metadata_object(set.record())};
  capture.extensions["opaque"] = metadata_object({{"value",std::uint64_t(9007199254740993ULL)}});
  const auto restored = decode_session(encode_session(capture));
  require(restored.source_version == 3 && restored.documents.size() == 1 &&
          metadata_equal(restored.workflow, capture.workflow) && metadata_equal(restored.extensions, capture.extensions),
          "SDK native V3 lost command/extension data");
  const auto reopened = restored.documents[0]->snapshot();
  require(reopened.id() != before.id() && !reopened.dirty() && restored.documents[0]->undo_steps() == 0 &&
          restored.documents[0]->redo_steps() == 0 &&
          metadata_equal(reopened.display().properties, before.display().properties) &&
          same_navigation(reopened.display(), before.display()) &&
          same_calibration(reopened.data().dataset.calibration(), calibration()) &&
          reopened.data().dataset.shape().dimensions() == std::array<int,6>{2,3,4,3,5,1},
          "SDK native V3 lost known display/geometry or fresh-owner policy");
  require(recognized_channel_display(reopened.display(), reopened.data().dataset)->channels.size() == 3,
          "SDK known channel state did not survive native V3");
  for (int t = 0; t < 2; ++t) for (int c = 0; c < 3; ++c) for (int z = 0; z < 4; ++z)
    for (int y = 0; y < 3; ++y) for (int x = 0; x < 5; ++x)
      require(reopened.data().dataset.plane({t,c,z})->at(x,y) == literal(t,c,z,y,x),
              "SDK native V3 changed literal source pixels");
  pixels.unchanged(source.snapshot().data().dataset); unchanged_owner(source, before, history, false); ++session_cases;

  Document future(fixture(PixelType::UInt16), "Opaque future display");
  auto future_view = view(future.snapshot().data().dataset);
  // V3 is now the recognized IEEE display schema. Keep this transport case
  // genuinely unknown, and separately reject the malformed known V3 object.
  const auto opaque = metadata_object({{"schema",std::string("imagejxx.channel-display")},{"version",37},
      {"future",metadata_object({{"exact",std::uint64_t(9007199254740993ULL)},
                                {"nested",metadata_array({true,MetadataValue{},metadata_object({{"",std::string("retained")}})})}})}});
  const auto unchanged_future = future.snapshot();
  auto malformed_known = object(opaque); malformed_known["version"] = 3;
  auto invalid_view = future_view;
  invalid_view.properties["channel_display"] = metadata_object(std::move(malformed_known));
  rejects([&]{future.set_display(invalid_view);}, "SDK malformed known V3 display was accepted");
  require(future.snapshot().revisions().display == unchanged_future.revisions().display &&
          future.snapshot().shares_data(unchanged_future),
          "SDK malformed known V3 display changed the owner");
  future_view.properties["channel_display"] = opaque; future.set_display(future_view);
  const auto future_before = future.snapshot(); const auto future_history = future.history();
  const FrozenPixels future_pixels(future_before.data().dataset);
  SessionCapture future_capture; future_capture.documents = {future_before};
  const auto future_session = decode_session(encode_session(future_capture));
  const auto future_reopened = future_session.documents[0]->snapshot();
  require(metadata_equal(future_reopened.display().properties.at("channel_display"), opaque),
          "SDK native V3 discarded opaque future channel display");
  rejects([&]{recognized_channel_display(future_reopened.display(), future_reopened.data().dataset);},
          "SDK future state silently selected legacy interpretation");
  const auto clear = resolve_channel_display_command(future_reopened, {ChannelDisplayAction::Disable, {}, {}});
  require(!command_display_state(clear, future_reopened).properties.count("channel_display"),
          "SDK explicit Disable could not remove opaque future state");
  future_pixels.unchanged(future.snapshot().data().dataset);
  unchanged_owner(future, future_before, future_history, false); ++session_cases;

  auto invalid = source.begin_edit(); invalid.replace_dataset(fixture(PixelType::Float32, 1));
  rejects([&]{source.commit(invalid);}, "SDK C-count edit retained invalid known channel state");
  require(invalid.active(), "SDK refused C-count edit consumed the transaction");
  unchanged_owner(source, before, history, false); pixels.unchanged(source.snapshot().data().dataset); ++shape_cases;
}
} // namespace
int main(int argc, char **) {
  try {
    require(argc == 1, "Channel display SDK consumer accepts no expected-output arguments");
    matrix(); edges(); commands(); persistence_and_shape();
    require(finite_rows == 27 && edge_cases == 5 && command_cases == 8 && session_cases == 2 && shape_cases == 1,
            "SDK frozen channel-display membership changed");
    std::cout << "channel_display: finite_rows=" << finite_rows << " edge_cases=" << edge_cases
              << " command_cases=" << command_cases << " session_cases=" << session_cases
              << " shape_cases=" << shape_cases << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "channel_display: " << error.what() << '\n'; return 1;
  }
}
