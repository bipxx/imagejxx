#include <imagejxx/float32_display.hpp>
#include <imagejxx/operations.hpp>
#include <nlohmann/json.hpp>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace imagejxx;
using Json = nlohmann::json;
namespace {
std::size_t checks = 0;
void check(bool value) {
  ++checks;
  if (!value) throw std::runtime_error("IEEE display check " + std::to_string(checks));
}
std::vector<unsigned char> hex(const std::string &text) {
  std::vector<unsigned char> result;
  for (std::size_t i = 0; i < text.size(); i += 2)
    result.push_back(static_cast<unsigned char>(std::stoul(text.substr(i, 2), nullptr, 16)));
  return result;
}
std::uint32_t word(const std::vector<unsigned char> &bytes, std::size_t offset) {
  std::uint32_t result = 0;
  for (unsigned i = 0; i < 4; ++i) result |= std::uint32_t(bytes.at(offset + i)) << (8 * i);
  return result;
}
template<class Action> void refused(Action action) {
  bool caught = false;
  try { action(); } catch (const std::invalid_argument &) { caught = true; }
  check(caught);
}
}
int main(int argc, char **argv) {
  try {
    if (argc != 2) throw std::runtime_error("Expected pinned fixture path");
    std::ifstream stream(argv[1]); Json fixture; stream >> fixture;
    check(fixture.at("imagej_commit") == "62db90a269cccc1ac79f658e4602043214ed055b");
    check(fixture.at("cases").size() == 240);
    const auto policy = FloatingSamplePolicy::PreserveIEEEFloat32;
    for (const auto &row : fixture.at("cases")) {
      const auto raw = hex(row.at("source_before_hex"));
      Image source(8, 4, 1, PixelType::Float32);
      for (std::size_t i = 0; i < source.pixels.size(); ++i)
        source.pixels[i] = float32_source_sample(word(raw, 4 * i), policy);
      Float32DisplayOptions options;
      const int range = row.at("range_mode");
      if (range == 1) options.range = {{0, 1}};
      if (range == 2) options.range = {{-16, 16}};
      if (range == 3) options.range = {{7, 7}};
      options.inverted = row.at("inverted_lut");
      const int threshold = row.at("threshold_mode");
      options.threshold_mode = threshold == 1 ? Float32ThresholdDisplay::Red :
          threshold == 2 ? Float32ThresholdDisplay::BlackAndWhite : Float32ThresholdDisplay::None;
      if (threshold) options.threshold = {{-1, 1}};
      const auto result = render_float32_display(source, options, policy);
      check(result.rgb.type == PixelType::UInt8 && result.rgb.channels == 3);
      const auto expected = hex(row.at("rgb_hex"));
      check(result.rgb.pixels.size() == expected.size());
      for (std::size_t i = 0; i < expected.size(); ++i) {
        if (result.rgb.pixels[i] != expected[i])
          std::cerr << "signal=" << row.at("signal") << " range=" << range
                    << " inverted=" << options.inverted << " threshold=" << threshold
                    << " byte=" << i << " expected=" << int(expected[i])
                    << " actual=" << result.rgb.pixels[i] << '\n';
        check(result.rgb.pixels[i] == expected[i]);
      }
      for (unsigned j = 0; j < 2; ++j) {
        const auto bytes = hex(row.at(j ? "max_f64le" : "min_f64le"));
        const double endpoint = float32_source_sample(result.range_bits[j], policy);
        std::uint64_t bits; std::memcpy(&bits, &endpoint, sizeof bits);
        for (unsigned k = 0; k < 8; ++k) check(((bits >> (8 * k)) & 255) == bytes[k]);
      }
      for (std::size_t i = 0; i < source.pixels.size(); ++i)
        check(float32_source_sample_bits(source.pixels[i], policy) == word(raw, 4 * i));
      check(row.at("source_before_hex") == row.at("source_after_hex"));
    }
    Image one(1, 1, 1, PixelType::Float32);
    Float32DisplayOptions options;
    one.pixels[0] = float32_source_sample(0x7f800001u, policy);
    refused([&] { render_float32_display(one); });
    refused([&] { render_float32_display(one, {}, FloatingSamplePolicy::PreserveQuietNaNFloat32); });
    check(render_float32_display(one, {}, policy).rgb.pixels[0] == 0);
    one.pixels[0] = 1;
    options.range = {{0, 0}};
    check(render_float32_display(one, options).range_bits[0] == 0x3f800000u);
    options.range = {{2, 1}};
    refused([&] { render_float32_display(one, options); });
    options.range = {{0, 1e100}};
    refused([&] { render_float32_display(one, options); });
    options = {}; options.threshold = {{0, 1}};
    refused([&] { render_float32_display(one, options); });
    options = {}; options.threshold_mode = Float32ThresholdDisplay::Red;
    refused([&] { render_float32_display(one, options); });
    options.threshold = {{1, 0}};
    refused([&] { render_float32_display(one, options); });
    one.type = PixelType::Float64;
    refused([&] { render_float32_display(one); });
    one.type = PixelType::Float32; one.pixels.clear();
    refused([&] { render_float32_display(one); });
    std::cout << checks << " IEEE Float32 display checks passed across 240 ImageJ cases\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
