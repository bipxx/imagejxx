#include <imagejxx/planar_display_annotation_command.hpp>
#include <imagejxx/session.hpp>
#include <iostream>
using namespace imagejxx;
namespace {
void check(bool value) {
  if (!value)
    throw std::runtime_error("Display annotation assertion failed");
}
template <class F> void refuses(F f) {
  bool failed = false;
  try {
    f();
  } catch (const std::exception &) {
    failed = true;
  }
  check(failed);
}
} // namespace
int main() {
  try {
    DatasetShape shape{2, 3, 4, 6, 8, 1};
    Stack planes;
    for (int i = 0; i < int(shape.plane_count()); ++i)
      planes.emplace_back(shape.x, shape.y, 1, PixelType::UInt16);
    DatasetReadObserver reads;
    Document doc(
        Dataset::from_planes(shape, std::move(planes)).observe_reads(reads));
    auto before = doc.snapshot();
    TextAnnotationPayload text;
    text.text = "label \xce\xbb\xf0\x9f\x98\x80\nsecond line";
    text.bounds = {-1.250000000001, 2.5, 6.25, 3.75};
    text.angle = 17.250000000001;
    text.font_size = 13.500000000001;
    text.background = 0x20445566;
    PlanarDisplayAnnotation a{
        "text", text, Plane::XZ, 2, 0, 2, {{"opaque", std::uint64_t(-1)}}};
    auto command = resolve_planar_display_annotation_command(
        before, {PlanarDisplayAnnotationOperation::Store, {}, a});
    validate_planar_display_annotation_command_record(command.record());
    auto replay =
        planar_display_annotation_command_from_record(before, command.record());
    auto prepared_transaction_1 = prepare_planar_display_annotation(replay); doc.commit(prepared_transaction_1);
    ImageAnnotationPayload image;
    image.width = 2;
    image.height = 1;
    image.channels = 4;
    image.pixels = {255, 0, 0, 128, 0, 0, 0, 255};
    image.opacity = 123;
    image.zero_transparent = true;
    PlanarDisplayAnnotation b{"image", image, Plane::YZ, 3, {}, 0, {}};
    auto prepared_transaction_2 = prepare_store_planar_display_annotation(doc.snapshot(), b); doc.commit(prepared_transaction_2);
    auto rows = planar_display_annotations(doc.snapshot().data());
    check(rows.size() == 2 && rows[0] == a && rows[1] == b);
    check(planar_annotations(doc.snapshot().data()).empty() &&
          !doc.snapshot().data().selection);
    check(planar_display_annotation_visible(a, Plane::XZ, 2, 0, 2) &&
          !planar_display_annotation_visible(a, Plane::XZ, 2, 1, 2));
    check(doc.snapshot().data().dataset.shares_pixels(before.data().dataset) &&
          reads.counts().plane_reads == 0 && reads.counts().region_reads == 0);
    const auto encoded = encode_planar_display_annotations(shape, rows);
    check(decode_planar_display_annotations(shape, encoded) == rows);
    auto other_shape = shape;
    other_shape.s = 3;
    refuses([&] { decode_planar_display_annotations(other_shape, encoded); });
    Document clone(before);
    refuses([&] {
      planar_display_annotation_command_from_record(clone.snapshot(),
                                                    command.record());
    });
    auto rebound = rebind_planar_display_annotation_command(
        clone.snapshot(), command.record(),
        {PlanarDisplayAnnotationOperation::Store, {}, b});
    auto prepared_transaction_3 = prepare_planar_display_annotation(rebound); clone.commit(prepared_transaction_3);
    check(planar_display_annotations(clone.snapshot().data()).at(0) == b);
    auto bad = command.record();
    bad["version"] = 2;
    refuses([&] { validate_planar_display_annotation_command_record(bad); });
    auto changed = b;
    std::get<ImageAnnotationPayload>(changed.payload).pixels.pop_back();
    refuses([&] {
      prepare_store_planar_display_annotation(doc.snapshot(), changed);
    });
    const auto steps = doc.undo_steps();
    auto prepared_transaction_4 = prepare_update_planar_display_annotation(doc.snapshot(), 0, a); doc.commit(prepared_transaction_4);
    check(doc.undo_steps() == steps);
    auto prepared_transaction_5 = prepare_delete_planar_display_annotation(doc.snapshot(), 1); doc.commit(prepared_transaction_5);
    check(planar_display_annotations(doc.snapshot().data()).size() == 1);
    doc.undo();
    check(planar_display_annotations(doc.snapshot().data()) == rows);
    doc.undo();
    doc.undo();
    check(planar_display_annotations(doc.snapshot().data()).empty());
    std::cout << "PASS bounded planar text/image display annotations\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
