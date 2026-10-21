#include <imagejxx/imagej_annotation_import.hpp>
#include <iostream>
using namespace imagejxx;
namespace {
void check(bool ok) {
  if (!ok)
    throw std::runtime_error("Mixed import assertion failed");
}
template <class F> void refuses(F f) {
  try {
    f();
  } catch (const std::exception &) {
    return;
  }
  throw std::runtime_error("Expected mixed import refusal");
}
} // namespace
int main() {
  try {
    DatasetShape shape{2, 3, 6, 8, 12, 1};
    Stack planes;
    for (std::size_t i = 0; i < shape.plane_count(); ++i)
      planes.emplace_back(shape.x, shape.y, 1, PixelType::UInt16);
    DatasetReadObserver reads;
    Document doc(
        Dataset::from_planes(shape, std::move(planes)).observe_reads(reads));
    const auto before = doc.snapshot();
    ImageJRoiPlaneMapping mapping;
    mapping.extent = shape;
    mapping.coordinate = 0;
    ImageJRoi geometric;
    geometric.roi.name = "geometry";
    geometric.roi.selection = Selection("rectangle", {{1, 1}, {3, 3}});
    ImageJRoiImportItem a{"geometry.roi", encode_imagej_roi(geometric),
                          mapping};
    ImageJTextRoi text;
    text.name = "text";
    text.text = "Native text";
    text.stroke_color = 0xff223344;
    text.z = 0;
    ImageJDisplayRoiMapping dm;
    dm.plane = mapping;
    dm.plane.plane = Plane::XZ;
    dm.plane.z_position = ImageJZPositionMapping::FixedAxis;
    ImageJDisplayRoiImportItem b{"text.roi", encode_imagej_text_roi(text), dm};
    ImageJImageRoi image;
    image.name = "image";
    image.pixels = Image(2, 1, 3, PixelType::UInt8);
    image.pixels.pixels = {255, 0, 0, 0, 0, 0};
    image.z = 0;
    ImageJDisplayRoiImportItem c{"image.roi", encode_imagej_image_roi(image),
                                 dm};
    auto resolved = resolve_imagej_annotation_import(before, {a, b, c});
    check(resolved.records().size() == 3 &&
          resolved.annotations().size() == 1 &&
          resolved.display_annotations().size() == 2);
    validate_imagej_annotation_import_record(resolved.record());
    auto request = decode_imagej_annotation_import_request(resolved.record());
    check(request.items.size() == 3);
    auto replay =
        imagej_annotation_import_from_record(before, resolved.record());
    auto prepared_transaction_1 = prepare_imagej_annotation_import(replay); doc.commit(prepared_transaction_1);
    auto saved = inspect_imagej_annotation_sources(doc.snapshot());
    check(saved.size() == 3 &&
          std::get<ImageJDisplayRoiImportItem>(saved[1]).data == b.data);
    check(planar_annotations(doc.snapshot().data()).size() == 1 &&
          planar_display_annotations(doc.snapshot().data()).size() == 2);
    check(doc.snapshot().data().dataset.shares_pixels(before.data().dataset) &&
          !doc.snapshot().data().selection);
    check(reads.counts().plane_reads == 0 && reads.counts().region_reads == 0);
    refuses([&] { auto prepared_transaction_2 = prepare_imagej_annotation_import(resolved); doc.commit(prepared_transaction_2); });
    doc.undo();
    check(doc.snapshot().shares_data(before));
    doc.redo();
    auto bad = c;
    bad.data = {0};
    const auto current = doc.snapshot();
    refuses([&] {
      resolve_imagej_annotation_import(current, {a, b, bad},
                                       ImageJRoiImportMode::Replace);
    });
    check(doc.snapshot().shares_data(current));
    auto replace = resolve_imagej_annotation_import(
        current, {b}, ImageJRoiImportMode::Replace);
    auto prepared_transaction_3 = prepare_imagej_annotation_import(replace); doc.commit(prepared_transaction_3);
    check(planar_annotations(doc.snapshot().data()).empty() &&
          planar_display_annotations(doc.snapshot().data()).size() == 1);
    auto bare_geometry=a;auto geometry_row=resolved.annotations().front();
    geometry_row.name="bare edited geometry";geometry_row.plane=Plane::YZ;geometry_row.coordinate=2;
    geometry_row.selection=Selection("point",{{1.250000000001,2.5}});
    bare_geometry.native_replacement=ImageJRoiNativeReplacement{shape,geometry_row};
    auto bare_text=b;auto text_row=resolved.display_annotations()[0];
    std::get<TextAnnotationPayload>(text_row.payload).text="Edited before publication";
    text_row.plane=Plane::XY;text_row.coordinate=1;
    bare_text.native_replacement=ImageJDisplayRoiNativeReplacement{shape,text_row};
    Document edited(before.data().dataset);const auto edit_before=edited.snapshot();
    auto edited_command=resolve_imagej_annotation_import(edit_before,{bare_geometry,bare_text,c});
    validate_imagej_annotation_import_record(edited_command.record());
    const auto &archive=std::get<MetadataObject>(std::get<std::shared_ptr<const MetadataTree>>(edited_command.provenance())->value);
    check(std::get<std::int64_t>(archive.at("version"))==4);
    auto edited_request=decode_imagej_annotation_import_request(edited_command.record());
    check(std::get<ImageJRoiImportItem>(edited_request.items[0]).mapping.has_value() && std::get<ImageJRoiImportItem>(edited_request.items[0]).native_replacement.has_value());
    auto prepared_transaction_4 = prepare_imagej_annotation_import(edited_command); edited.commit(prepared_transaction_4);
    auto edited_sources=inspect_imagej_annotation_sources(edited.snapshot());
    check(std::get<ImageJRoiImportItem>(edited_sources[0]).data==a.data && std::get<ImageJDisplayRoiImportItem>(edited_sources[1]).data==b.data);
    check(planar_annotations(edited.snapshot().data())[0].plane==Plane::YZ);
    refuses([&]{resolve_imagej_roi_import(edit_before,{bare_geometry});});
    auto damaged=bare_text;damaged.data={0};
    refuses([&]{resolve_imagej_annotation_import(edit_before,{damaged});});
    check(reads.counts().plane_reads==0 && reads.counts().region_reads==0);
    std::cout << "PASS mixed ImageJ annotation import and archive\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
