#include <imagejxx/composite_path.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
namespace {
int checks = 0;
void check(bool value) {
  ++checks;
  if (!value)
    throw std::runtime_error("Composite path assertion " + std::to_string(checks));
}
template <class F> void refuses(F f) {
  try {
    f();
  } catch (const std::invalid_argument &) {
    ++checks;
    return;
  }
  throw std::runtime_error("Missing composite path refusal");
}
} // namespace
int main() {
  try {
    std::vector<double> commands{0, 0, 0, 2, 2, 4, 4, 0, 4};
    const ImageJCompositePath quadratic(commands);
    check(quadratic.bounds() == std::array<double, 4>{0, 0, 4, 2});
    commands[1] = 9;
    check(quadratic.commands()[1] == 0 && quadratic.segment_count() == 3);
    const ImageJCompositePath cubic({0, 0, 0, 3, 0, 4, 4, 4, 4, 0, 4});
    check(cubic.bounds() == std::array<double, 4>{0, 0, 4, 3});
    const auto reflected = cubic.transformed({-1, 0, 8, 0, 1, -2});
    check(reflected.bounds() == std::array<double, 4>{4, -2, 8, 1});
    check(reflected.transformed({-1, 0, 8, 0, 1, 2}).commands() == cubic.commands());
    const ImageJCompositePath negative_zero({0, -0., 0, 4});
    check(std::signbit(negative_zero.transformed({1, 0, 0, 0, 1, 0}).commands()[1]));
    refuses([] { ImageJCompositePath({1, 1, 1}); });
    refuses([] { ImageJCompositePath({0, 1, 1, 3, 1, 2}); });
    refuses([] { ImageJCompositePath({0, 1e10, 0}); });
    refuses([&] { cubic.transformed({1, 2, 0, 2, 4, 0}); });
    ImageJRoiLimits limits;
    limits.max_bytes = cubic.commands().size() * sizeof(double);
    limits.max_vertices = 3;
    check(ImageJCompositePath(cubic.commands(), limits).commands() == cubic.commands());
    --limits.max_bytes;
    refuses([&] { cubic.transformed({1, 0, 0, 0, 1, 0}, limits); });
    limits = {};
    limits.max_vertices = 2;
    refuses([&] { ImageJCompositePath(cubic.commands(), limits); });
    ImageJRoi row;
    row.composite_path = cubic.commands();
    row.roi.name = "curve";
    row.roi.z = 2;
    row.properties = "retained";
    row.roi.selection = cubic.mask(10, 10);
    const auto moved = transform_imagej_composite_roi(row, {1, 0, 2, 0, 1, 1}, 12, 11);
    check(row.composite_path == cubic.commands() && moved.roi.name == "curve" &&
          moved.roi.z == 2 && moved.properties == "retained");
    const auto back = decode_imagej_roi(encode_imagej_roi(moved), {}, 12, 11);
    check(back.composite_path == moved.composite_path &&
          back.roi.selection.packed_mask() == moved.roi.selection.packed_mask());
    row.composite_path = {0, 0, 0, 4};
    refuses([&] { transform_imagej_composite_roi(row, {1, 0, 0, 0, 1, 0}, 10, 10); });
    std::cout << "PASS " << checks << " composite path checks\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
