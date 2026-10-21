#include <imagejxx/channel_display_command.hpp>
#include "channel_display_command_internal.hpp"
#include "kernel_control.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace imagejxx {
namespace {

constexpr const char *command_schema = "imagejxx.channel-display-command";

void exact_fields(const MetadataObject &record,
                  std::initializer_list<const char *> fields) {
  std::set<std::string> expected;
  for (const auto *field : fields) {
    expected.emplace(field);
    if (!record.count(field))
      throw std::invalid_argument(
          std::string("Missing channel display command field: ") + field);
  }
  for (const auto &[field, value] : record) {
    (void)value;
    if (!expected.count(field))
      throw std::invalid_argument("Unknown channel display command field: " + field);
  }
}

const MetadataObject &object_value(const MetadataValue &value) {
  if (const auto node = std::get_if<std::shared_ptr<const MetadataTree>>(&value);
      node && *node)
    if (const auto object = std::get_if<MetadataObject>(&(*node)->value))
      return *object;
  throw std::invalid_argument("Channel display command requires an object");
}

const MetadataArray &array_value(const MetadataValue &value) {
  // The shared Python metadata adapter preserves its legacy string-vector
  // representation for an empty list. Only that exact empty form is also an
  // empty typed array; nonempty string vectors cannot become command indices
  // or Boolean observations.
  if (const auto strings = std::get_if<std::vector<std::string>>(&value);
      strings && strings->empty()) {
    static const MetadataArray empty;
    return empty;
  }
  if (const auto node = std::get_if<std::shared_ptr<const MetadataTree>>(&value);
      node && *node)
    if (const auto array = std::get_if<MetadataArray>(&(*node)->value))
      return *array;
  throw std::invalid_argument("Channel display command requires an array");
}

const std::string &text_value(const MetadataValue &value) {
  if (const auto text = std::get_if<std::string>(&value))
    return *text;
  throw std::invalid_argument("Channel display command requires text");
}

// Exact integer alternatives only: neither Booleans nor floating point values
// can stand in for IDs, revisions, versions, dimensions or position indices.
std::uint64_t unsigned_value(const MetadataValue &value) {
  if (const auto integer = std::get_if<std::uint64_t>(&value))
    return *integer;
  if (const auto integer = std::get_if<std::int64_t>(&value);
      integer && *integer >= 0)
    return static_cast<std::uint64_t>(*integer);
  throw std::invalid_argument("Channel display command requires an exact nonnegative integer");
}

int index_value(const MetadataValue &value, bool positive = false) {
  const auto integer = unsigned_value(value);
  if ((positive && !integer) ||
      integer > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
    throw std::invalid_argument("Channel display command integer is outside native range");
  return static_cast<int>(integer);
}

bool bool_value(const MetadataValue &value) {
  if (const auto boolean = std::get_if<bool>(&value))
    return *boolean;
  throw std::invalid_argument("Channel display command requires a Boolean flag");
}

const char *type_name(PixelType type) {
  switch (type) {
  case PixelType::UInt8: return "uint8";
  case PixelType::UInt16: return "uint16";
  case PixelType::Float32: return "float32";
  case PixelType::Float64: return "float64";
  }
  throw std::invalid_argument("Invalid channel display command source type");
}

PixelType read_type(const MetadataValue &value) {
  const auto &name = text_value(value);
  if (name == "uint8") return PixelType::UInt8;
  if (name == "uint16") return PixelType::UInt16;
  if (name == "float32") return PixelType::Float32;
  if (name == "float64") return PixelType::Float64;
  throw std::invalid_argument("Unknown channel display command source type");
}

const char *policy_name(FloatingSamplePolicy policy) {
  switch (policy) {
  case FloatingSamplePolicy::FiniteOnly: return "finite_only";
  case FloatingSamplePolicy::PreserveQuietNaNFloat32:
    return "preserve_quiet_nan_float32";
  case FloatingSamplePolicy::PreserveIEEEFloat32: return "preserve_ieee_float32";
  }
  throw std::invalid_argument("Invalid channel display command sample policy");
}

FloatingSamplePolicy read_policy(const MetadataValue &value) {
  const auto &name = text_value(value);
  if (name == "finite_only") return FloatingSamplePolicy::FiniteOnly;
  if (name == "preserve_quiet_nan_float32")
    return FloatingSamplePolicy::PreserveQuietNaNFloat32;
  if (name == "preserve_ieee_float32") return FloatingSamplePolicy::PreserveIEEEFloat32;
  throw std::invalid_argument("Unknown channel display command sample policy");
}

const char *action_name(ChannelDisplayAction action) {
  switch (action) {
  case ChannelDisplayAction::Set: return "set";
  case ChannelDisplayAction::ResetRange: return "reset_range";
  case ChannelDisplayAction::Disable: return "disable";
  case ChannelDisplayAction::SelectMax: return "select_max";
  case ChannelDisplayAction::SelectMin: return "select_min";
  }
  throw std::invalid_argument("Invalid channel display command action");
}

ChannelDisplayAction read_action(const MetadataValue &value) {
  const auto &name = text_value(value);
  if (name == "set") return ChannelDisplayAction::Set;
  if (name == "reset_range") return ChannelDisplayAction::ResetRange;
  if (name == "disable") return ChannelDisplayAction::Disable;
  if (name == "select_max") return ChannelDisplayAction::SelectMax;
  if (name == "select_min") return ChannelDisplayAction::SelectMin;
  throw std::invalid_argument("Unknown channel display command action");
}

bool projection_intent(ChannelDisplayAction action) {
  return action == ChannelDisplayAction::SelectMax ||
         action == ChannelDisplayAction::SelectMin;
}

const char *projection_intent_label(ChannelDisplayAction action) {
  return action == ChannelDisplayAction::SelectMin ? "SelectMin" : "SelectMax";
}

struct SourceRecord {
  std::uint64_t id;
  Revisions revisions;
  DatasetShape shape;
  PixelType type;
  FloatingSamplePolicy policy;
};

void validate_source(const SourceRecord &source) {
  if (!source.id)
    throw std::invalid_argument("Channel display command requires a positive owner ID");
  source.shape.validate();
  (void)type_name(source.type);
  (void)policy_name(source.policy);
  if (source.policy != FloatingSamplePolicy::FiniteOnly &&
      (source.type != PixelType::Float32 || source.shape.s != 1))
    throw std::invalid_argument("Quiet-NaN channel display source requires scalar Float32");
}

SourceRecord source_metadata(const DocumentSnapshot &source) {
  const auto &dataset = source.data().dataset;
  SourceRecord result{source.id(), source.revisions(), dataset.shape(),
                      dataset.pixel_type(), dataset.sample_policy()};
  validate_source(result);
  return result;
}

MetadataObject encode_source(const SourceRecord &source) {
  MetadataArray shape;
  shape.reserve(6);
  for (int extent : source.shape.dimensions())
    shape.emplace_back(static_cast<std::int64_t>(extent));
  return {{"id", source.id}, {"pixels_revision", source.revisions.pixels},
          {"metadata_revision", source.revisions.metadata},
          {"shape", metadata_array(std::move(shape))},
          {"type", std::string(type_name(source.type))},
          {"sample_policy", std::string(policy_name(source.policy))}};
}

SourceRecord read_source(const MetadataObject &record) {
  exact_fields(record, {"id", "pixels_revision", "metadata_revision", "shape",
                        "type", "sample_policy"});
  const auto &array = array_value(record.at("shape"));
  if (array.size() != 6)
    throw std::invalid_argument("Channel display command shape requires six TCZYXS axes");
  DatasetShape shape{index_value(array[0], true), index_value(array[1], true),
                     index_value(array[2], true), index_value(array[3], true),
                     index_value(array[4], true), index_value(array[5], true)};
  Revisions revisions;
  revisions.pixels = unsigned_value(record.at("pixels_revision"));
  revisions.metadata = unsigned_value(record.at("metadata_revision"));
  SourceRecord result{unsigned_value(record.at("id")), revisions, shape,
                      read_type(record.at("type")), read_policy(record.at("sample_policy"))};
  validate_source(result);
  return result;
}

void validate_position(PlanePosition position, DatasetShape shape) {
  if (position.t < 0 || position.t >= shape.t || position.c < 0 ||
      position.c >= shape.c || position.z < 0 || position.z >= shape.z)
    throw std::invalid_argument("Channel display command position is outside its source");
}

PlanePosition source_position(const DocumentSnapshot &source) {
  PlanePosition position{source.display().t, source.display().c,
                         source.display().crosshair[2]};
  validate_position(position, source.data().dataset.shape());
  return position;
}

MetadataObject encode_position(PlanePosition position) {
  return {{"t", static_cast<std::int64_t>(position.t)},
          {"c", static_cast<std::int64_t>(position.c)},
          {"z", static_cast<std::int64_t>(position.z)}};
}

std::size_t checked_add(std::size_t a, std::size_t b) {
  if (a > std::numeric_limits<std::size_t>::max() - b)
    throw std::invalid_argument("Channel display command working size overflows");
  return a + b;
}

std::size_t checked_mul(std::size_t a, std::size_t b) {
  if (b && a > std::numeric_limits<std::size_t>::max() / b)
    throw std::invalid_argument("Channel display command working size overflows");
  return a * b;
}

// Immutable nested nodes are shared by a snapshot copy. Legacy top-level
// strings/string vectors, map nodes and keys are copied and must be charged.
std::size_t display_copy_bytes(const DisplayState &display) {
  constexpr auto node = sizeof(MetadataObject::value_type) + 8 * sizeof(void *);
  std::size_t bytes = sizeof(DocumentSnapshot);
  for (const auto &[key, value] : display.properties) {
    bytes = checked_add(bytes, checked_add(node, checked_add(key.size(), 1)));
    if (const auto flat_text = std::get_if<std::string>(&value))
      bytes = checked_add(bytes, checked_add(flat_text->size(), 1));
    else if (const auto strings = std::get_if<std::vector<std::string>>(&value)) {
      bytes = checked_add(bytes, checked_mul(strings->size(), sizeof(std::string)));
      for (const auto &entry_text : *strings)
        bytes = checked_add(bytes, checked_add(entry_text.size(), 1));
    }
  }
  return bytes;
}

void preflight_projection_intent(const DocumentSnapshot &source,
                                const SourceRecord &metadata,
                                const ChannelDisplayLimits &limits,
                                ChannelDisplayAction action) {
  const auto label = projection_intent_label(action);
  if (!limits.max_pixels || !limits.max_working_bytes)
    throw std::invalid_argument("Channel display command limits must be positive");
  if (metadata.shape.s != 1 ||
      (metadata.type != PixelType::UInt8 && metadata.type != PixelType::UInt16 &&
       metadata.type != PixelType::Float32))
    throw std::invalid_argument(std::string(label) + " requires scalar UInt8, UInt16 or Float32");
  const auto found = source.display().properties.find("channel_display");
  if (found == source.display().properties.end())
    throw std::invalid_argument(std::string(label) + " requires recognized channel display state");
  const auto &record = object_value(found->second);
  if (!record.count("schema") || !record.count("version") ||
      text_value(record.at("schema")) != "imagejxx.channel-display")
    throw std::invalid_argument(std::string(label) + " requires recognized channel display state");
  const auto version = unsigned_value(record.at("version"));
  if (version == 1)
    exact_fields(record, {"schema", "version", "mode", "channels"});
  else if (version == 2 || version == 3)
    exact_fields(record, {"schema", "version", "mode", "projection", "channels"});
  else
    throw std::invalid_argument(std::string("Unsupported ") + label + " channel display version");
  const auto &channels = array_value(record.at("channels"));
  if (channels.size() != static_cast<std::size_t>(metadata.shape.c))
    throw std::invalid_argument(std::string(label) + " channel count differs from its source");

  constexpr auto value = sizeof(MetadataValue);
  constexpr auto node = sizeof(MetadataObject::value_type) + 8 * sizeof(void *);
  constexpr auto tree = sizeof(MetadataTree) + 4 * sizeof(void *);
  // Worst case: every C becomes an explicit 256xRGB table. Charge two full
  // encoded trees (construction/return), three typed state arrays, two source
  // snapshot display copies, four fixed LUTs and a bounded envelope allowance.
  const auto row = checked_add(checked_mul(3, value), tree);
  const auto table = checked_add(checked_add(checked_mul(256, value), tree),
                                 checked_mul(256, row));
  const auto base_wire_channel = checked_add(table,
      checked_add(checked_mul(4, node),
          checked_add(checked_mul(4, tree), checked_add(checked_mul(3, value), 64))));
  // V3 may add a raw-range object/key/tree around the two endpoint words.
  const auto wire_channel = checked_add(base_wire_channel, version == 3
      ? checked_add(node, checked_add(tree, checked_add(2 * value, 32))) : 0);
  const auto per_channel = checked_add(
      checked_mul(3, sizeof(ChannelDisplayChannel)), checked_mul(2, wire_channel));
  auto bytes = checked_mul(channels.size(), per_channel);
  bytes = checked_add(bytes, checked_mul(2, display_copy_bytes(source.display())));
  bytes = checked_add(bytes, checked_mul(4, sizeof(ChannelDisplayTable)));
  bytes = checked_add(bytes, checked_add(checked_mul(64, node),
      checked_add(checked_mul(32, tree), checked_add(checked_mul(128, value), 4096))));
  if (bytes > limits.max_working_bytes)
    throw std::invalid_argument(std::string(label) + " exceeds its metadata working-memory limit");
  // Refuse huge malformed name payloads before the official parser can copy
  // one. Valid names are tiny; custom table payloads use the fixed bound above.
  for (const auto &channel : channels) {
    const auto &item = object_value(channel);
    exact_fields(item, {"visible", "range", "lut"});
    const auto &lut = object_value(item.at("lut"));
    if (const auto name = lut.find("name"); name != lut.end())
      bytes = checked_add(bytes,
          checked_mul(4, checked_add(text_value(name->second).size(), 1)));
    if (bytes > limits.max_working_bytes)
      throw std::invalid_argument(std::string(label) + " exceeds its metadata working-memory limit");
  }
  if (bytes > limits.max_working_bytes)
    throw std::invalid_argument(std::string(label) + " exceeds its metadata working-memory limit");
}

ChannelDisplayTable table_for(const ChannelDisplayChannel &channel) {
  return channel.lut.table ? *channel.lut.table : channel_display_lut(channel.lut.name);
}

bool inverted_table(const ChannelDisplayTable &table) {
  auto brightness = [](const auto &rgb) {
    return int(rgb[0]) + int(rgb[1]) + int(rgb[2]);
  };
  int previous = brightness(table[0]);
  // Source excludes the final 254 -> 255 step, and constants are inverted.
  for (std::size_t i = 1; i < 255; ++i) {
    kernel_checkpoint_periodic();
    const int current = brightness(table[i]);
    if (previous < current)
      return false;
    previous = current;
  }
  return true;
}

std::optional<int> select_projection(ChannelDisplayState &state, DatasetShape shape,
                                    int active_c, ChannelDisplayAction action) {
  const bool was_composite = state.mode == ChannelDisplayMode::Composite;
  const bool gray = state.mode == ChannelDisplayMode::Grayscale ||
                    (was_composite && shape.c == 1);
  const auto active_table = gray ? channel_display_lut("Grays")
                                : table_for(state.channels[active_c]);
  const bool selecting_max = action == ChannelDisplayAction::SelectMax;
  state.projection = selecting_max ? ChannelDisplayProjection::Max
                                  : ChannelDisplayProjection::Min;
  std::optional<int> navigation;
  if (inverted_table(active_table) == selecting_max) {
    for (auto &channel : state.channels) {
      kernel_checkpoint();
      auto table = table_for(channel);
      for (auto &rgb : table) {
        kernel_checkpoint_periodic();
        const int low = std::min({int(rgb[0]), int(rgb[1]), int(rgb[2])});
        const int high = std::max({int(rgb[0]), int(rgb[1]), int(rgb[2])});
        const int shift = 255 - low - high;
        for (auto &component : rgb)
          component = static_cast<std::uint8_t>(int(component) + shift);
      }
      channel.lut.name.clear();
      channel.lut.table = std::move(table);
    }
    navigation = 0; // Deliberate even if the original active C was already 0.
    state.projection = inverted_table(table_for(state.channels[0]))
                           ? ChannelDisplayProjection::Min
                           : ChannelDisplayProjection::Max;
  }
  if (!was_composite)
    for (auto &channel : state.channels) {
      kernel_checkpoint_periodic();
      channel.visible = true;
    }
  if (state.version != ChannelDisplayVersion::V3) state.version = ChannelDisplayVersion::V2;
  state.mode = shape.c > 8 ? ChannelDisplayMode::Color : ChannelDisplayMode::Composite;
  return navigation;
}

void validate_channels(const std::vector<int> &channels, DatasetShape shape) {
  std::set<int> seen;
  for (int channel : channels)
    if (channel < 0 || channel >= shape.c || !seen.insert(channel).second)
      throw std::invalid_argument("Reset channels must be unique and inside the C axis");
}

std::vector<int> explicit_channels(const std::vector<int> &requested,
                                   DatasetShape shape) {
  validate_channels(requested, shape);
  if (!requested.empty())
    return requested;
  std::vector<int> channels;
  channels.reserve(static_cast<std::size_t>(shape.c));
  for (int channel = 0; channel < shape.c; ++channel)
    channels.push_back(channel);
  return channels;
}

MetadataObject encode_reset(const std::vector<int> &channels,
                            const std::vector<bool> &empty_finite) {
  MetadataArray encoded_channels, encoded_empty;
  encoded_channels.reserve(channels.size());
  encoded_empty.reserve(empty_finite.size());
  for (int channel : channels)
    encoded_channels.emplace_back(static_cast<std::int64_t>(channel));
  for (bool empty : empty_finite)
    encoded_empty.emplace_back(empty);
  return {{"channels", metadata_array(std::move(encoded_channels))},
          {"empty_finite", metadata_array(std::move(encoded_empty))}};
}

void validate_reset(ChannelDisplayAction action, const SourceRecord &source,
                    const std::optional<ChannelDisplayState> &state,
                    const std::vector<int> &channels,
                    const std::vector<bool> &empty_finite) {
  validate_channels(channels, source.shape);
  if (action != ChannelDisplayAction::ResetRange) {
    if (!channels.empty() || !empty_finite.empty())
      throw std::invalid_argument("Only reset_range may record reset observations");
    return;
  }
  if (!state || channels.empty() || channels.size() != empty_finite.size())
    throw std::invalid_argument("Reset observations require explicit channels and one flag per channel");
  for (std::size_t index = 0; index < channels.size(); ++index) {
    if (!empty_finite[index]) continue;
    const auto &range = state->channels[channels[index]].range;
    const bool quiet = source.policy == FloatingSamplePolicy::PreserveQuietNaNFloat32 &&
                       range == std::array<double, 2>{0, 1};
    const bool ieee = source.policy == FloatingSamplePolicy::PreserveIEEEFloat32 &&
                      !std::isfinite(range[0]) && !std::isfinite(range[1]);
    if (source.type != PixelType::Float32 || (!quiet && !ieee))
      throw std::invalid_argument("Empty-finite reset observation differs from its typed range");
  }
}

struct DecodedCommand {
  SourceRecord source;
  ChannelDisplayAction action;
  std::optional<ChannelDisplayState> state;
  ChannelDisplayVersion version;
  std::optional<int> navigation;
  std::optional<std::uint64_t> display_dependency;
  PlanePosition position;
};

DecodedCommand decode_record(const MetadataObject &record) {
  validate_metadata(record);
  // Preserve V1 missing-field diagnostics before interpreting an incomplete header.
  if (!record.count("schema") || !record.count("version"))
    exact_fields(record, {"schema", "version", "action", "source", "position",
                          "state", "reset"});
  if (!record.count("schema") || !record.count("version") ||
      text_value(record.at("schema")) != command_schema)
    throw std::invalid_argument("Unsupported channel display command schema/version");
  const auto encoded_version = unsigned_value(record.at("version"));
  ChannelDisplayVersion version;
  if (encoded_version == 1) {
    version = ChannelDisplayVersion::V1;
    exact_fields(record, {"schema", "version", "action", "source", "position",
                          "state", "reset"});
  } else if (encoded_version == 2 || encoded_version == 3) {
    version = encoded_version == 3 ? ChannelDisplayVersion::V3 : ChannelDisplayVersion::V2;
    exact_fields(record, {"schema", "version", "action", "source", "position",
                          "state", "reset", "display_dependency", "navigation"});
  } else
    throw std::invalid_argument("Unsupported channel display command schema/version");
  auto source = read_source(object_value(record.at("source")));
  if ((version == ChannelDisplayVersion::V3) !=
      (source.policy == FloatingSamplePolicy::PreserveIEEEFloat32))
    throw std::invalid_argument("IEEE channel commands require explicit V3 records");
  const auto action = read_action(record.at("action"));
  if (version == ChannelDisplayVersion::V1 && projection_intent(action))
    throw std::invalid_argument(std::string(projection_intent_label(action)) +
                                " requires channel display command V2");
  const auto &position = object_value(record.at("position"));
  exact_fields(position, {"t", "c", "z"});
  const PlanePosition captured_position{index_value(position.at("t")),
      index_value(position.at("c")), index_value(position.at("z"))};
  validate_position(captured_position, source.shape);
  std::optional<ChannelDisplayState> state;
  const bool null_state = std::holds_alternative<std::monostate>(record.at("state"));
  if (action == ChannelDisplayAction::Disable) {
    if (!null_state)
      throw std::invalid_argument("Disable command state must be null");
  } else {
    if (null_state)
      throw std::invalid_argument("Set/reset_range command requires complete state");
    state = parse_channel_display(object_value(record.at("state")), source.shape,
                                   source.type, source.policy);
    if (state->version != version)
      throw std::invalid_argument("Channel display state/command versions differ");
  }
  const auto &reset = object_value(record.at("reset"));
  exact_fields(reset, {"channels", "empty_finite"});
  const auto &encoded_channels = array_value(reset.at("channels"));
  const auto &encoded_empty = array_value(reset.at("empty_finite"));
  if (action == ChannelDisplayAction::ResetRange) {
    if (encoded_channels.empty() ||
        encoded_channels.size() > static_cast<std::size_t>(source.shape.c) ||
        encoded_channels.size() != encoded_empty.size())
      throw std::invalid_argument("Reset observations require a unique nonempty C subset and matching flags");
  } else if (!encoded_channels.empty() || !encoded_empty.empty())
    throw std::invalid_argument("Only reset_range may record reset observations");
  std::vector<int> channels;
  channels.reserve(encoded_channels.size());
  for (const auto &value : encoded_channels)
    channels.push_back(index_value(value));
  std::vector<bool> empty_finite;
  empty_finite.reserve(encoded_empty.size());
  for (const auto &value : encoded_empty)
    empty_finite.push_back(bool_value(value));
  validate_reset(action, source, state, channels, empty_finite);
  std::optional<int> navigation;
  std::optional<std::uint64_t> display_dependency;
  if (version != ChannelDisplayVersion::V1) {
    if (!std::holds_alternative<std::monostate>(record.at("display_dependency"))) {
      const auto &dependency = object_value(record.at("display_dependency"));
      exact_fields(dependency, {"revision"});
      display_dependency = unsigned_value(dependency.at("revision"));
    }
    if (!std::holds_alternative<std::monostate>(record.at("navigation"))) {
      const auto &effect = object_value(record.at("navigation"));
      exact_fields(effect, {"c"});
      const int channel = index_value(effect.at("c"));
      if (channel != 0 || channel >= source.shape.c)
        throw std::invalid_argument(std::string(projection_intent_label(action)) +
                                    " navigation must explicitly select C0");
      navigation = channel;
    }
    if (projection_intent(action)) {
      if (!display_dependency || !state ||
          (state->projection != ChannelDisplayProjection::Max &&
           state->projection != ChannelDisplayProjection::Min) ||
          state->mode != (source.shape.c > 8 ? ChannelDisplayMode::Color
                                           : ChannelDisplayMode::Composite))
        throw std::invalid_argument(std::string(projection_intent_label(action)) +
                                    " record has an inconsistent resolved effect");
      if (action == ChannelDisplayAction::SelectMax &&
          state->projection == ChannelDisplayProjection::Min && !navigation)
        throw std::invalid_argument("SelectMax Min requires deliberate C0 navigation");
      if (action == ChannelDisplayAction::SelectMin &&
          state->projection == ChannelDisplayProjection::Max && !navigation)
        throw std::invalid_argument("SelectMin Max requires deliberate C0 navigation");
    } else if (navigation || display_dependency)
      throw std::invalid_argument("Only SelectMax and SelectMin accept display/navigation effects");
  }
  return {std::move(source), action, std::move(state), version, navigation,
          display_dependency, captured_position};
}

void require_binding(const SourceRecord &expected, const SourceRecord &current) {
  if (expected.id != current.id)
    throw std::invalid_argument("Channel display command differs from its source owner");
  if (!current.revisions.matches(expected.revisions, Change::Pixels | Change::Metadata))
    throw RevisionConflict();
  if (expected.shape.dimensions() != current.shape.dimensions() ||
      expected.type != current.type || expected.policy != current.policy)
    throw std::invalid_argument("Channel display command differs from its source metadata");
}

} // namespace

detail::ChannelDisplayCommandDescriptor
detail::channel_display_command_descriptor(const MetadataObject &record) {
  auto decoded = decode_record(record);
  return {decoded.source.shape, decoded.source.type, decoded.source.policy,
          decoded.position, decoded.action, decoded.version,
          std::move(decoded.state)};
}

ResolvedChannelDisplayCommand::ResolvedChannelDisplayCommand(
    DocumentSnapshot source, ChannelDisplayAction action,
    std::optional<ChannelDisplayState> state, MetadataObject record,
    ChannelDisplayVersion version, std::optional<int> navigation,
    std::optional<std::uint64_t> display_dependency, bool requires_captured_display)
    : source_(std::move(source)), action_(action), state_(std::move(state)),
      record_(std::move(record)), version_(version), navigation_(navigation),
      displayDependency_(display_dependency),
      requiresCapturedDisplay_(requires_captured_display) {}

ResolvedChannelDisplayCommand resolve_channel_display_command(
    const DocumentSnapshot &source, const ChannelDisplayRequest &request,
    const ChannelDisplayLimits &limits, const ExecutionControl &control) {
  // A checkpoint may mutate/reassign every caller-owned argument. Freeze all
  // consumed values before its first invocation, including the returned owner.
  const auto action = request.action;
  const auto metadata = source_metadata(source);
  const auto position = source_position(source);
  (void)action_name(action);
  const auto owned_limits = limits;
  if (projection_intent(action)) {
    if (request.state || !request.reset_channels.empty())
      throw std::invalid_argument(std::string(projection_intent_label(action)) +
                                  " accepts neither state nor reset channels");
    preflight_projection_intent(source, metadata, owned_limits, action);
  }
  auto captured_source = source;
  const auto owned_control =
      projection_intent(action) || action == ChannelDisplayAction::ResetRange
          ? control : ExecutionControl{};
  std::optional<ChannelDisplayState> state;
  std::vector<int> channels;
  std::vector<bool> empty_finite;
  auto version = metadata.policy == FloatingSamplePolicy::PreserveIEEEFloat32
      ? ChannelDisplayVersion::V3 : ChannelDisplayVersion::V1;
  std::optional<int> navigation;
  std::optional<std::uint64_t> display_dependency;
  std::optional<ExecutionScope> scope;
  if (projection_intent(action)) {
    state = recognized_channel_display(captured_source.display(),
                                       captured_source.data().dataset);
    if (!state)
      throw std::invalid_argument(std::string(projection_intent_label(action)) +
                                  " requires recognized channel display state");
    // Admission/preflight and immutable capture precede cancellation/callbacks.
    // Check a valid pre-canceled token directly before kernel_checkpoint can
    // invoke any user checkpoint callback.
    if (owned_control.cancellation && owned_control.cancellation->canceled())
      throw OperationCancelled();
    scope.emplace(owned_control);
    kernel_checkpoint();
    display_dependency = captured_source.revisions().display;
    navigation = select_projection(*state, metadata.shape, position.c, action);
    version = state->version;
    validate_channel_display(*state, metadata.shape, metadata.type, metadata.policy);
  } else if (action == ChannelDisplayAction::Disable) {
    if (request.state || !request.reset_channels.empty())
      throw std::invalid_argument("Disable request accepts neither state nor reset channels");
  } else {
    if (!request.state)
      throw std::invalid_argument("Set/reset_range request requires complete state");
    if (action == ChannelDisplayAction::Set && !request.reset_channels.empty())
      throw std::invalid_argument("Set request does not accept reset channels");
    state = normalize_channel_display(*request.state, metadata.shape, metadata.type, metadata.policy);
    version = state->version;
    if (action == ChannelDisplayAction::ResetRange) {
      channels = explicit_channels(request.reset_channels, metadata.shape);
      auto reset = reset_channel_display_ranges(captured_source.data().dataset, std::move(*state),
          position.t, position.z, channels, owned_limits, owned_control);
      state = std::move(reset.first);
      if (reset.second.size() != channels.size())
        throw std::invalid_argument("Live reset returned an inconsistent observation count");
      empty_finite.reserve(reset.second.size());
      for (std::size_t index = 0; index < reset.second.size(); ++index) {
        const auto &observed = reset.second[index].range;
        const auto &resolved = state->channels[channels[index]].range;
        for (unsigned endpoint = 0; endpoint < 2; ++endpoint)
          if (!(observed[endpoint] == resolved[endpoint]) &&
              !(std::isnan(observed[endpoint]) && std::isnan(resolved[endpoint])))
            throw std::invalid_argument("Live reset observation differs from its resolved channel range");
        empty_finite.push_back(reset.second[index].empty_finite);
      }
    }
  }
  validate_reset(action, metadata, state, channels, empty_finite);
  MetadataValue encoded_state;
  if (state)
    encoded_state = metadata_object(encode_channel_display(*state, metadata.shape, metadata.type, metadata.policy));
  MetadataObject record{
      {"schema", std::string(command_schema)},
      {"version", static_cast<std::int64_t>(version)},
      {"action", std::string(action_name(action))},
      {"source", metadata_object(encode_source(metadata))},
      {"position", metadata_object(encode_position(position))},
      {"state", std::move(encoded_state)},
      {"reset", metadata_object(encode_reset(channels, empty_finite))}};
  if (version != ChannelDisplayVersion::V1) {
    MetadataValue dependency, effect;
    if (display_dependency)
      dependency = metadata_object({{"revision", *display_dependency}});
    if (navigation)
      effect = metadata_object({{"c", static_cast<std::int64_t>(*navigation)}});
    record.emplace("display_dependency", std::move(dependency));
    record.emplace("navigation", std::move(effect));
  }
  if (projection_intent(action)) {
    kernel_checkpoint();
    // A final checkpoint may cancel its own token; do not return a prepared
    // intent merely because that cancellation occurred inside the callback.
    if (owned_control.cancellation && owned_control.cancellation->canceled())
      throw OperationCancelled();
  }
  return {std::move(captured_source), action, std::move(state), std::move(record),
          version, navigation, display_dependency,
          projection_intent(action)};
}

void validate_channel_display_command_record(const MetadataObject &record) {
  (void)decode_record(record);
}

ResolvedChannelDisplayCommand channel_display_command_from_record(
    const DocumentSnapshot &source, const MetadataObject &record) {
  auto decoded = decode_record(record);
  require_binding(decoded.source, source_metadata(source));
  return {source, decoded.action, std::move(decoded.state), record,
          decoded.version, decoded.navigation, decoded.display_dependency, false};
}

ResolvedChannelDisplayCommand rebind_channel_display_command(
    const DocumentSnapshot &new_source, const MetadataObject &record,
    const ChannelDisplayRequest &explicit_request,
    const ChannelDisplayLimits &limits, const ExecutionControl &control) {
  (void)decode_record(record);
  return resolve_channel_display_command(new_source, explicit_request, limits, control);
}

DisplayState command_display_state(
    const ResolvedChannelDisplayCommand &command,
    const DocumentSnapshot &current_snapshot) {
  if (!command.source().same_document(current_snapshot))
    throw std::invalid_argument("Channel display command differs from its retained source owner");
  require_binding(source_metadata(command.source()), source_metadata(current_snapshot));
  if (command.requires_captured_display() &&
      command.display_dependency_revision() !=
          std::optional<std::uint64_t>(current_snapshot.revisions().display))
    throw RevisionConflict();
  auto display = with_channel_display(current_snapshot.display(),
                                      current_snapshot.data().dataset, command.state());
  if (const auto c = command.navigation_channel()) {
    display.c = *c;
    display.validate(current_snapshot.data().dataset.shape());
  }
  return display;
}

} // namespace imagejxx
