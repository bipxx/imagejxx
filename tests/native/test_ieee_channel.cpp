#include <imagejxx/channel_display_command.hpp>
#include <imagejxx/operations.hpp>
#include <imagejxx/session.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace imagejxx;
namespace {
constexpr auto ieee = FloatingSamplePolicy::PreserveIEEEFloat32;
std::size_t checks = 0;
void check(bool value) {
  ++checks;
  if (!value) throw std::runtime_error("IEEE channel check " + std::to_string(checks));
}
template<class Action> void refused(Action action) {
  bool caught = false;
  try { action(); } catch (const std::invalid_argument &) { caught = true; }
  check(caught);
}
std::vector<unsigned char> hex(const std::string &s) {
  std::vector<unsigned char> result;
  for (std::size_t i = 0; i < s.size(); i += 2)
    result.push_back(static_cast<unsigned char>(std::stoul(s.substr(i, 2), nullptr, 16)));
  return result;
}
std::uint32_t word(const std::vector<unsigned char> &bytes, std::size_t offset) {
  std::uint32_t value = 0;
  for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t(bytes.at(offset + i)) << (8 * i);
  return value;
}
void captured(const char *path) {
  std::ifstream stream(path); nlohmann::json fixture; stream >> fixture;
  check(fixture.at("imagej_commit") == "62db90a269cccc1ac79f658e4602043214ed055b");
  unsigned cases = 0;
  for (const auto &row : fixture.at("cases")) {
    if (row.at("threshold_mode") != 0) continue;
    ++cases;
    const auto raw = hex(row.at("source_before_hex"));
    Image source(8, 4, 1, PixelType::Float32);
    for (std::size_t i = 0; i < source.pixels.size(); ++i)
      source.pixels[i] = float32_source_sample(word(raw, i * 4), ieee);
    const auto data = Dataset::from_stack({source}, {}, ieee);
    auto state = default_channel_display(data, 0, 0, ChannelDisplayMode::Color);
    check(state.version == ChannelDisplayVersion::V3);
    const int range = row.at("range_mode");
    if (range == 1) state.channels[0].range = {0, 1};
    if (range == 2) state.channels[0].range = {-16, 16};
    if (range == 3) state.channels[0].range = {7, 7};
    state.channels[0].lut.name = "Grays";
    if (row.at("inverted_lut").get<bool>()) {
      auto table = channel_display_lut("Grays");
      std::reverse(table.begin(), table.end());
      state.channels[0].lut.name.clear(); state.channels[0].lut.table = table;
    }
    const auto encoded = encode_channel_display(state, data.shape(), data.pixel_type(), ieee);
    validate_metadata(encoded);
    const auto parsed = parse_channel_display(encoded, data.shape(), data.pixel_type(), ieee);
    const auto output = compose_channel_display(data, parsed, Plane::XY, 0, 0, 0);
    const auto expected = hex(row.at("rgb_hex"));
    for (std::size_t i = 0; i < expected.size(); ++i) check(output.pixels[i] == expected[i]);
    if (range == 0) for (unsigned end = 0; end < 2; ++end) {
      const auto bytes = hex(row.at(end ? "max_f64le" : "min_f64le"));
      std::uint64_t bits; std::memcpy(&bits, &parsed.channels[0].range[end], sizeof bits);
      for (unsigned k = 0; k < 8; ++k) check(((bits >> (8 * k)) & 255) == bytes[k]);
    }
    Document owner(data);
    const auto before = owner.snapshot();
    const auto command = resolve_channel_display_command(before, {ChannelDisplayAction::Set, parsed, {}});
    check(command.version() == ChannelDisplayVersion::V3);
    validate_channel_display_command_record(command.record());
    const auto replay = channel_display_command_from_record(before, command.record());
    owner.set_display(command_display_state(replay, owner.snapshot()));
    check(owner.snapshot().shares_data(before) && owner.history().undo_steps == 0);
    const auto saved = encode_session({{owner.snapshot()}, {metadata_object(command.record())}, {}, {}});
    const auto restored = decode_session(saved);
    const auto view = recognized_channel_display(restored.documents[0]->snapshot().display(), data);
    check(view.has_value() && metadata_equal(metadata_object(encoded),
        metadata_object(encode_channel_display(*view, data.shape(), data.pixel_type(), ieee))));
    for (std::size_t i = 0; i < source.pixels.size(); ++i)
      check(float32_source_sample_bits(data.plane()->pixels[i], ieee) == word(raw, 4 * i));
    auto reset = resolve_channel_display_command(owner.snapshot(), {ChannelDisplayAction::ResetRange, parsed, {0}});
    validate_channel_display_command_record(reset.record());
  }
  check(cases == 80);
}
constexpr std::uint32_t words[]{0xff800000u, 0u, 0x3f800000u, 0x7f800000u,
    0x7fc12345u, 0xff800001u, 0x80000000u, 0x3f000000u};
constexpr unsigned indices[]{0, 0, 255, 255, 0, 0, 0, 128};
unsigned signal(int t, int c, int z, int y, int x) {
  return unsigned(t * 3 + c * 5 + z * 2 + y * 4 + x) % 8;
}
Dataset scientific() {
  Stack planes;
  for (int t = 0; t < 2; ++t) for (int c = 0; c < 2; ++c) for (int z = 0; z < 2; ++z) {
    Image image(4, 2, 1, PixelType::Float32);
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 4; ++x)
      image.at(x, y) = float32_source_sample(words[signal(t,c,z,y,x)], ieee);
    planes.push_back(std::move(image));
  }
  return Dataset::from_planes({2,2,2,2,4,1}, std::move(planes), {}, ieee);
}
void orthogonal() {
  const auto data = scientific();
  auto state = default_channel_display(data, 0, 0);
  for (int c = 0; c < 2; ++c) {
    auto &channel = state.channels[c]; channel.range = {0, 1};
    channel.lut.name.clear(); channel.lut.table.emplace();
    for (unsigned i = 0; i < 256; ++i) (*channel.lut.table)[i] = c == 0
        ? std::array<std::uint8_t,3>{std::uint8_t(i),std::uint8_t(255-i),17}
        : std::array<std::uint8_t,3>{std::uint8_t(255-i),std::uint8_t(i),231};
  }
  for (auto projection : {ChannelDisplayProjection::Sum, ChannelDisplayProjection::Max, ChannelDisplayProjection::Min}) {
    state.projection = projection;
    for (int t = 0; t < 2; ++t) for (auto plane : {Plane::XY, Plane::XZ, Plane::YZ}) {
      const auto output = compose_channel_display(data,state,plane,1,t,1);
      for (int y = 0; y < output.height; ++y) for (int x = 0; x < output.width; ++x) {
        const int px = plane == Plane::YZ ? 1 : x;
        const int py = plane == Plane::XY ? y : plane == Plane::XZ ? 1 : x;
        const int pz = plane == Plane::XY ? 1 : y;
        const auto a = indices[signal(t,0,pz,py,px)], b = indices[signal(t,1,pz,py,px)];
        const unsigned first[]{a,255-a,17}, second[]{255-b,b,231};
        for (int k = 0; k < 3; ++k) {
          const auto expected = projection == ChannelDisplayProjection::Sum ? std::min(255u,first[k]+second[k])
              : projection == ChannelDisplayProjection::Max ? std::max(first[k],second[k]) : std::min(first[k],second[k]);
          check(output.at(x,y,k) == expected);
        }
      }
    }
  }
  DatasetReadObserver reads; Document owner(data.observe_reads(reads));
  const auto set = resolve_channel_display_command(owner.snapshot(), {ChannelDisplayAction::Set,state,{}});
  owner.set_display(command_display_state(set,owner.snapshot()));
  const auto selected = resolve_channel_display_command(owner.snapshot(), {ChannelDisplayAction::SelectMin,{},{}});
  check(selected.version() == ChannelDisplayVersion::V3);
  check(selected.state()->version == ChannelDisplayVersion::V3);
  check(reads.counts().plane_reads == 0 && reads.counts().region_reads == 0);
  const auto replay = channel_display_command_from_record(owner.snapshot(),selected.record());
  owner.set_display(command_display_state(replay,owner.snapshot()));
  check(reads.counts().plane_reads == 0);
  auto bad = state; bad.version = ChannelDisplayVersion::V2;
  refused([&] { validate_channel_display(bad,data.shape(),PixelType::Float32,ieee); });
  refused([&] { validate_channel_display(state,data.shape(),PixelType::Float32,FloatingSamplePolicy::FiniteOnly); });
  auto malformed = selected.record(); malformed["version"] = std::int64_t(2);
  refused([&] { validate_channel_display_command_record(malformed); });
  malformed = selected.record(); malformed["version"] = std::int64_t(4);
  refused([&] { validate_channel_display_command_record(malformed); });
  ChannelDisplayLimits tiny; tiny.max_pixels=1;
  refused([&] { compose_channel_display(owner.snapshot().data().dataset,state,Plane::XY,0,0,0,{},tiny); });
  check(reads.counts().plane_reads == 0);
}
}
int main(int argc, char **argv) {
  try {
    if (argc != 2) throw std::runtime_error("Expected pinned display fixture");
    captured(argv[1]); orthogonal();
    std::cout << checks << " IEEE channel checks passed (80 pinned display cases plus scientific composition/records)\n";
    return 0;
  } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
