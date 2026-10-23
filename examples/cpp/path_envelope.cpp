#include <imagejxx/composite_path.hpp>
#include <imagejxx/imagej_roi_export.hpp>
#include <imagejxx/roi_style.hpp>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
namespace {
int checks = 0;
void check(bool value) {
  ++checks;
  if (!value)
    throw std::runtime_error("Path envelope check " + std::to_string(checks));
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
class UnreadSource final : public PlaneSource {
public:
  DatasetShape shape() const override { return {1, 1, 3, 10, 12, 1}; }
  PixelType pixel_type() const override { return PixelType::UInt16; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++reads;
    throw std::runtime_error("Unexpected plane read");
  }
  Image read_region(PlanePosition, int, int, int, int) const override {
    ++reads;
    throw std::runtime_error("Unexpected region read");
  }
  mutable int reads = 0;
};
} // namespace
int main() {
  try {
    const auto provider = std::make_shared<UnreadSource>();
    const auto shape = provider->shape();
    const std::vector<double> commands{
        0, -.123456789, -3, 2, 3, 8, 8, -3, 4,
        0, 10, 2, 3, 11, 7, 13, -2, 14, 2, 4};
    auto area = Selection::from_path(commands);
    area.width = 2.5;
    ImageJRoi original;
    original.roi = {"source", area, 0, 0, 0, {}};
    original.properties = "original: curves\n";
    original.group = 7;
    ImageJRoiPlaneMapping mapping;
    mapping.extent = shape;
    auto mapped = map_imagej_roi(original, mapping);
    ImageJRoiLimits pure;
    pure.max_mask_pixels = pure.max_path_work = 0;
    const auto wire = encode_imagej_roi_native_envelope(mapped, original, pure);
    check(decode_imagej_composite_roi(wire, pure).properties.find("imagejxx.roi.v3: ") == 0);
    auto decoded = decode_imagej_roi_envelope(wire, pure);
    check(decoded.annotation().selection.path_commands() == commands);
    check(decoded.source_record().roi.selection.path_commands() == commands);
    check(decoded.source_record().properties == original.properties &&
          decoded.source_record().group == 7);
    check(decoded.source_record().composite_path.empty());
    check(encode_imagej_roi_native_envelope(decoded, original, pure) == wire);
    refuses([&] { encode_imagej_roi_envelope(mapped); });
    auto current = mapped.annotation();
    current.plane = Plane::YZ;
    current.coordinate = 1;
    current.attributes = {{"row", std::string("opaque")}};
    current.selection.attributes = {{"selection", std::string("opaque")}};
    auto rebound = rebind_imagej_roi_mapping(mapped, shape, current);
    ImageJRoi projection;
    projection.roi = {current.name, area, 0, 0, 1, {}};
    const auto output = encode_imagej_roi_native_envelope(rebound, projection, pure);
    auto retained = decode_imagej_roi_native_envelope(output, pure);
    check(retained.annotation() == current);
    check(retained.source_record().roi.selection.path_commands() == commands);
    Document owner{DocumentData{Dataset(provider)}};
    const auto before = owner.snapshot();
    ImageJRoiImportItem item{"curves.roi", output, {}, {}};
    auto imported = resolve_imagej_roi_import(before, {item});
    validate_imagej_roi_import_record(imported.record());
    check(imported.annotations().size() == 1 && imported.annotations()[0] == current);
    auto replay = imagej_roi_import_from_record(before, imported.record());
    auto edit = prepare_imagej_roi_import(replay);
    owner.commit(edit);
    check(planar_annotations(owner.snapshot().data())[0] == current);
    auto pair = resolve_imagej_roi_pairing(inspect_imagej_roi_sources(owner.snapshot()), 0, 0);
    validate_imagej_roi_pairing_record(pair.record());
    auto exported = resolve_imagej_roi_export(owner.snapshot(), pair, projection);
    validate_imagej_roi_export_record(exported.record());
    check(decode_imagej_roi_envelope(exported.data()).annotation() == current);
    check(imagej_roi_export_from_record(owner.snapshot(), exported.record()).data() ==
          exported.data());
    owner.undo();
    check(planar_annotations(owner.snapshot().data()).empty());
    owner.redo();
    check(planar_annotations(owner.snapshot().data())[0] == current);
    check(provider->reads == 0);
    auto small = pure;
    small.max_text_bytes = 128;
    refuses([&] { encode_imagej_roi_native_envelope(rebound, projection, small); });
    std::cout << "PASS " << checks << " path envelope checks\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
