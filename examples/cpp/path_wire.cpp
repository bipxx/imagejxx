#include <imagejxx/composite_path.hpp>
#include <imagejxx/imagej_roi_mapping.hpp>
#include <imagejxx/roi_style.hpp>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
namespace {
int checks = 0;
void check(bool value) {
  ++checks;
  if (!value)
    throw std::runtime_error("Path wire check " + std::to_string(checks));
}
template <class F> void refuses(F f) {
  bool caught = false;
  try {
    f();
  } catch (const std::invalid_argument &) {
    caught = true;
  }
  check(caught);
}
} // namespace
int main() {
  try {
    const std::vector<double> commands{
        0, -4, -3, 2, 3, 8, 8, -3, 4, 0, 10, 2, 3, 11, 7, 13, -2, 14, 2, 4};
    auto path = Selection::from_path(commands);
    path.width = 2.5;
    ImageJRoi row;
    row.roi = Roi{"curves", path, 0, 1, 2, {}};
    row.properties = "owned: curves\n";
    row.group = 7;
    // Wire transport is independent of raster canvas/work admission.
    ImageJRoiLimits limits;
    limits.max_mask_pixels = limits.max_path_work = 0;
    const auto bytes = encode_imagej_roi(row, limits);
    check(bytes[6] == 1 && bytes[36] == 0 && bytes[39] == commands.size());
    const auto decoded = decode_imagej_composite_roi(bytes, limits);
    check(decoded.roi.selection.is_path());
    check(decoded.roi.selection.path_commands() == commands);
    check(decoded.composite_path == commands);
    check(decoded.roi.name == row.roi.name && decoded.properties == row.properties &&
          decoded.group == 7);
    check(decoded.roi.t == row.roi.t && decoded.roi.c == row.roi.c &&
          decoded.roi.z == row.roi.z);
    check(roi_style(decoded.roi.selection).stroke_width == 2.5);
    check(encode_imagej_roi(decoded, limits) == bytes);
    refuses([&] { decode_imagej_roi(bytes); });
    const auto mask = decode_imagej_roi(bytes, {}, 16, 16);
    check(mask.roi.selection.kind == "mask");
    check(mask.roi.selection.packed_mask() ==
          Selection::from_mask(path.mask(16, 16)).packed_mask());
    ImageJRoiPlaneMapping mapping;
    mapping.extent = {2, 2, 5, 8, 16, 1};
    mapping.plane = Plane::XZ;
    mapping.z_position = ImageJZPositionMapping::FixedAxis;
    const auto mapped = decode_imagej_composite_roi_mapped(bytes, mapping, limits);
    check(mapped.annotation().selection.path_commands() == commands &&
          mapped.annotation().coordinate == 2 && mapped.annotation().plane == Plane::XZ);
    validate_imagej_roi_mapping(mapped, limits);
    auto stale = decoded;
    stale.composite_path[1] += 1;
    refuses([&] { encode_imagej_roi(stale, limits); });
    stale.composite_path.clear();
    check(encode_imagej_roi(stale, limits) == bytes);
    auto small = limits;
    small.max_vertices = 2;
    refuses([&] { decode_imagej_composite_roi(bytes, small); });
    refuses([&] { encode_imagej_roi(row, small); });
    ImageJRoi rectangle;
    rectangle.roi.selection = Selection("rectangle", {{0, 0}, {1, 1}});
    refuses([&] { decode_imagej_composite_roi(encode_imagej_roi(rectangle)); });
    row.roi.selection.attributes["opaque"] = std::string("explicit mapping");
    refuses([&] { encode_imagej_roi(row); });
    std::cout << "PASS " << checks << " native path wire checks\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
