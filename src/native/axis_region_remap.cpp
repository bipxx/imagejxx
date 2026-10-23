#include "axis_region_remap.hpp"
#include "kernel_control.hpp"
#include <cstdint>
#include <limits>

namespace imagejxx::detail {
namespace {
struct Frame {
  std::array<int, 2> origin;
  int width, height;
  std::size_t bits;
};
std::int64_t floor_div(std::int64_t value, int step) {
  const auto quotient = value / step;
  return quotient - (value % step < 0);
}
Frame frame(const Selection &s, const ZSubsetRequest &r) {
  if (!s.is_region() || r.first < 0 || r.step <= 0)
    throw std::invalid_argument(
        "Regional Z remap requires a positive sampling lattice");
  s.validate(1, 1);
  const auto origin = s.mask_origin();
  const auto begin = std::int64_t(origin[1]) - r.first;
  const auto end = begin + s.mask_height() - 1;
  const auto first = -floor_div(-begin, r.step), last = floor_div(end, r.step);
  if (last < first)
    return {{0, 0}, 1, 1, 1};
  const auto height = last - first + 1;
  if (first < std::numeric_limits<int>::min() ||
      first > std::numeric_limits<int>::max() ||
      height > std::numeric_limits<int>::max())
    throw std::length_error("Mapped regional Z frame exceeds integer storage range");
  const auto width = s.mask_width();
  constexpr std::size_t max_bits = 16u * 1024u * 1024u * 8u;
  if (std::size_t(width) > max_bits / std::size_t(height))
    throw std::length_error("Mapped regional Z mask exceeds its metadata cap");
  return {{origin[0], int(first)},
          width,
          int(height),
          std::size_t(width) * std::size_t(height)};
}
} // namespace
void admit_region_z_subset(const Selection &s, const ZSubsetRequest &r) {
  (void)frame(s, r);
}

Selection region_z_subset(const Selection &s, const ZSubsetRequest &r) {
  const auto output = frame(s, r);
  const auto origin = s.mask_origin();
  std::vector<unsigned char> packed(output.bits / 8 + (output.bits % 8 != 0), 0);
  for (int y = 0; y < output.height; ++y) {
    const auto source_y = std::int64_t(r.first) +
                          (std::int64_t(output.origin[1]) + y) * r.step - origin[1];
    if (source_y < 0 || source_y >= s.mask_height())
      continue;
    for (int x = 0; x < output.width; ++x) {
      kernel_checkpoint_periodic();
      const auto input = std::size_t(source_y) * s.mask_width() + x;
      const auto target = std::size_t(y) * output.width + x;
      if (s.packed_mask()[input / 8] & (0x80u >> (input % 8)))
        packed[target / 8] |= static_cast<unsigned char>(0x80u >> (target % 8));
    }
  }
  auto result = Selection::from_packed_region(output.width, output.height,
                                              std::move(packed), output.origin);
  result.width = s.width;
  result.attributes = s.attributes;
  return result;
}
} // namespace imagejxx::detail
