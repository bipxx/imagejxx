#include <imagejxx/imagej_annotation_export.hpp>
#include <iostream>
using namespace imagejxx;
namespace {
void check(bool ok) {
  if (!ok)
    throw std::runtime_error("Mixed export assertion failed");
}
template <class F> void refuses(F f) {
  try {
    f();
  } catch (const std::exception &) {
    return;
  }
  throw std::runtime_error("Expected mixed export refusal");
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
    auto prepared_transaction_1 = prepare_imagej_annotation_import(
        resolve_imagej_annotation_import(doc.snapshot(), {a, b, c})); doc.commit(prepared_transaction_1);
    const auto before = doc.snapshot();
    const auto catalog = inspect_imagej_annotation_catalog(before);
    check(catalog.entries().size() == 3 &&
          catalog.entries()[1].kind == ImageJAnnotationKind::Display);
    const auto pairing = resolve_imagej_annotation_pairing(
        catalog, 1, ImageJAnnotationKind::Display, 0);
    validate_imagej_annotation_pairing_record(pairing.record());
    check(std::get<MappedImageJDisplayRoi>(pairing.mapped()).source_bytes() ==
          b.data);
    geometric.roi.z = 0;
    std::vector<ImageJAnnotationExportItem> items{
        {"geometry.roi", 0, ImageJAnnotationKind::Selection, 0, geometric, {}},
        {"text.roi", 1, ImageJAnnotationKind::Display, 0, text, {}},
        {"image.roi", 2, ImageJAnnotationKind::Display, 1, image, {}}};
    auto single = resolve_imagej_annotation_export(before, items[1]);
    check(!single.is_zip());
    check(single.data() ==
          encode_imagej_annotation_pairing_envelope(before, pairing, text));
    for (bool compressed : {false, true}) {
      auto zipped =
          resolve_imagej_annotation_zip_export(before, items, {}, compressed);
      validate_imagej_annotation_export_record(zipped.record());
      auto request = decode_imagej_annotation_export_request(zipped.record());
      check(request.items.size() == 3 && request.compress == compressed);
      auto decoded = decode_imagej_roi_zip(zipped.data());
      check(decoded.size() == 3);
      check(
          decode_imagej_display_roi_envelope(decoded[2].data).source_bytes() ==
          c.data);
      check(imagej_annotation_export_from_record(before, zipped.record())
                .data() == zipped.data());
    }
    auto bad = items;
    bad.back().source_index = 999;
    refuses([&] { resolve_imagej_annotation_zip_export(before, bad); });
    check(doc.snapshot().shares_data(before) &&
          reads.counts().plane_reads == 0 && reads.counts().region_reads == 0);
    auto tx = doc.begin_edit();
    tx.set_title("changed");
    doc.commit(tx);
    refuses([&] {
      imagej_annotation_export_from_record(doc.snapshot(), single.record());
    });
    check(rebind_imagej_annotation_export(doc.snapshot(), single.record(),
                                          items[1])
              .data() == single.data());
    Document created(before.data().dataset);
    auto g = prepare_store_planar_annotation(
        created.snapshot(), planar_annotations(before.data()).at(0));
    created.commit(g);
    for (const auto &row : planar_display_annotations(before.data())) {
      auto edit =
          prepare_store_planar_display_annotation(created.snapshot(), row);
      created.commit(edit);
    }
    const auto created_before = created.snapshot();
    auto fresh = items;
    for (auto &item : fresh) {
      item.fresh_source = true;
      item.source_index = std::numeric_limits<std::size_t>::max();
    }
    auto generated =
        resolve_imagej_annotation_zip_export(created_before, fresh);
    check(std::get<std::int64_t>(generated.record().at("version")) == 2);
    validate_imagej_annotation_export_record(generated.record());
    auto fresh_request =
        decode_imagej_annotation_export_request(generated.record());
    check(fresh_request.items.size() == 3 &&
          fresh_request.items[0].fresh_source);
    check(std::get<MappedImageJDisplayRoi>(generated.members()[1].mapped)
              .source_bytes() == encode_imagej_text_roi(text));
    check(created.snapshot().shares_data(created_before) &&
          !created_before.data().metadata.extensions.count(
              imagej_roi_import_sources_extension));
    check(
        imagej_annotation_export_from_record(created_before, generated.record())
            .data() == generated.data());
    check(resolve_imagej_annotation_export(created_before, fresh[1])
              .record()
              .count("sources") == 1);
    fresh[0].source_index = 0;
    refuses(
        [&] { resolve_imagej_annotation_export(created_before, fresh[0]); });
    check(reads.counts().plane_reads == 0 && reads.counts().region_reads == 0);
    auto edited_a=a;edited_a.native_replacement=ImageJRoiNativeReplacement{shape,planar_annotations(before.data())[0]};
    auto edited_b=b;edited_b.native_replacement=ImageJDisplayRoiNativeReplacement{shape,planar_display_annotations(before.data())[0]};
    auto edited_c=c;edited_c.native_replacement=ImageJDisplayRoiNativeReplacement{shape,planar_display_annotations(before.data())[1]};
    Document bare_edited(before.data().dataset);
    auto bare_import=prepare_imagej_annotation_import(resolve_imagej_annotation_import(bare_edited.snapshot(),{edited_a,edited_b,edited_c}));bare_edited.commit(bare_import);
    auto bare_export=resolve_imagej_annotation_zip_export(bare_edited.snapshot(),items);
    validate_imagej_annotation_export_record(bare_export.record());
    check(std::get<MappedImageJDisplayRoi>(bare_export.members()[1].mapped).source_bytes()==b.data);
    std::cout << "PASS mixed ImageJ annotation pairing and export\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
