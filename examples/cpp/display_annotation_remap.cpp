#include <imagejxx/axis_remap.hpp>
#include <imagejxx/planar_display_annotation_command.hpp>
#include <imagejxx/session.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace imagejxx;
namespace {
std::size_t checks = 0;
void check(bool value, const char *message) {
  ++checks;
  if (!value)
    throw std::runtime_error(message);
}
template <class F> void refuses(F action, const char *message) {
  bool bad = false;
  try {
    action();
  } catch (const std::exception &) {
    bad = true;
  }
  check(bad, message);
}
} // namespace
int main() {
  try {
    TextAnnotationPayload text;
    text.text = "Affine label";
    text.bounds = {-1, -2, 7, 3};
    text.font_size = 3;
    text.angle = 23;
    text.font_style = 3;
    PlanarDisplayAnnotation row{
        "text", text, Plane::XZ, 1, 0, {}, {{"opaque", std::uint64_t(-1)}}};
    check(!row.has_transform() && row.map_point({2, 3}) == std::array<double, 2>{2, 3},
          "Identity point mapping");
    row.transform = {1, .25, 2, .5, 1, -2};
    const auto point = row.map_point({2, 3});
    check(point == std::array<double, 2>{4.75, 2}, "Affine point mapping");
    check(row.unmap_point(point) == std::array<double, 2>{2, 3}, "Affine inverse");
    auto invalid = row;
    invalid.transform = {1, 2, 3, 2, 4, 5};
    refuses([&] { invalid.validate_transform(); }, "Singular affine accepted");
    invalid = row;
    invalid.transform[0] = 1e10;
    refuses([&] { invalid.validate_transform(); }, "Unbounded coefficient accepted");
    Stack planes;
    for (int z = 0; z < 5; ++z) {
      Image p(4, 3, 1, PixelType::UInt16);
      p.at(0, 0) = z;
      planes.push_back(std::move(p));
    }
    DatasetReadObserver reads;
    Document owner(Dataset::from_stack(std::move(planes)).observe_reads(reads));
    auto command = resolve_planar_display_annotation_command(
        owner.snapshot(), {PlanarDisplayAnnotationOperation::Store, {}, row});
    check(std::get<std::int64_t>(command.record().at("version")) == 2,
          "Affine command did not use V2");
    validate_planar_display_annotation_command_record(command.record());
    auto stored =
        prepare_planar_display_annotation(planar_display_annotation_command_from_record(
            owner.snapshot(), command.record()));
    owner.commit(stored);
    ImageAnnotationPayload image;
    image.width = 2;
    image.pixels = {255, 0, 0, 128, 0, 0, 0, 255};
    image.opacity = 123;
    image.zero_transparent = true;
    auto rows = planar_display_annotations(owner.snapshot().data());
    rows.push_back({"image", image, Plane::YZ, 2, {}, {}, {}});
    auto xy = row;
    xy.plane = Plane::XY;
    xy.coordinate = 3;
    rows.push_back(xy);
    xy.coordinate = 0;
    rows.push_back(xy);
    auto edit = owner.begin_edit();
    edit.set_extension(planar_display_annotations_extension,
                       encode_planar_display_annotations(
                           owner.snapshot().data().dataset.shape(), rows));
    owner.commit(edit);
    const auto before = owner.snapshot();
    auto result = prepare_z_axis_remap(before, ZSubsetRequest{1, 4, 2});
    const auto mapped = planar_display_annotations(result.transaction.preview().data());
    check(mapped.size() == 3, "Fixed XY positions were not remapped/dropped");
    check(mapped[0].transform == std::array<double, 6>{1, .25, 2, .25, .5, -1.25},
          "Text affine composition");
    check(mapped[1].transform == std::array<double, 6>{1, 0, 0, 0, .5, -.25},
          "Image affine composition");
    check(mapped[0].payload == row.payload &&
              mapped[1].payload == DisplayAnnotationPayload(image),
          "Payloads were destructively changed");
    check(mapped[2].coordinate == 1 && mapped[2].transform == row.transform,
          "XY transform changed");
    check(reads.counts().plane_reads == 0 && reads.counts().region_reads == 0,
          "Display remap read source samples");
    check(owner.snapshot().shares_data(before), "Preparation changed owner");
    owner.commit(result.transaction);
    owner.undo();
    check(owner.snapshot().shares_data(before), "Undo lost source");
    owner.redo();
    auto sequential = prepare_z_axis_remap(owner.snapshot(), ZSubsetRequest{0, 1, 2});
    auto direct = prepare_z_axis_remap(before, ZSubsetRequest{1, 4, 4});
    check(planar_display_annotations(sequential.transaction.preview().data()) ==
              planar_display_annotations(direct.transaction.preview().data()),
          "Repeated mapping changed retained affine intent");
    auto restored = decode_session(encode_session(SessionCapture{{owner.snapshot()}}));
    check(planar_display_annotations(restored.documents[0]->snapshot().data()) ==
              mapped,
          "Session lost affine geometry");
    auto source = before;
    ZSubsetRequest request{1, 4, 2};
    ExecutionControl control;
    std::size_t calls = 0;
    control.checkpoint = [&] {
      ++calls;
      source = owner.snapshot();
      request.first = -1;
      control.checkpoint = {};
    };
    auto frozen = prepare_z_axis_remap(source, request, control);
    check(calls > 0 &&
              planar_display_annotations(frozen.transaction.preview().data()) == mapped,
          "Callback redirected inputs");
    control.checkpoint = [] { throw OperationCancelled(); };
    refuses([&] { prepare_z_axis_remap(before, ZSubsetRequest{1, 4, 2}, control); },
            "Cancellation accepted");
    std::cout
        << "PASS affine display V2/composition/payload/no-IO/capture/undo/session: "
        << checks << " checks\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
