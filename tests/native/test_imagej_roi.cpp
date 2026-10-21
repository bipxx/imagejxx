#include <cmath>
#include <imagejxx/imagej_roi.hpp>
#include <imagejxx/imagej_roi_export.hpp>
#include <imagejxx/imagej_roi_import.hpp>
#include <imagejxx/imagej_roi_mapping.hpp>
#include <imagejxx/imagej_roi_pairing.hpp>
#include <imagejxx/imagej_roi_zip_export.hpp>
#include <imagejxx/roi_style.hpp>
#include <iostream>
using namespace imagejxx;
namespace {
void check(bool v) {
  if (!v)
    throw std::runtime_error("ImageJ ROI assertion");
}
template <class F> void refuses(F f) {
  try {
    f();
  } catch (const std::invalid_argument &) {
    return;
  }
  throw std::runtime_error("Missing ImageJ ROI refusal");
}
} // namespace
int main() {
  try {
    // Independent literal legacy rectangle header: left2/top3/right7/bottom9.
    std::vector<unsigned char> literal(64);
    literal[0] = 'I';
    literal[1] = 'o';
    literal[2] = 'u';
    literal[3] = 't';
    literal[5] = 228;
    literal[6] = 1;
    literal[9] = 3;
    literal[11] = 2;
    literal[13] = 9;
    literal[15] = 7;
    literal[59] = 2;
    auto decoded = decode_imagej_roi(literal);
    check(decoded.roi.selection.points ==
          std::vector<std::array<double, 2>>{{2, 3}, {7, 9}});
    check(decoded.legacy_position == 2 && !decoded.roi.z && !decoded.roi.c &&
          !decoded.roi.t);
    ImageJRoi row;
    row.roi = {"named", Selection("rectangle", {{-1.25, 2.5}, {7.75, 9.5}}),
               0,       2,
               3,       {}};
    row.properties = "key: value\n";
    row.group = 7;
    row.roi.selection = with_roi_style(
        row.roi.selection, {{"stroke_color", std::string("#80112233")},
                            {"fill_color", std::string("#FF445566")},
                            {"stroke_width", 2.5}});
    auto bytes = encode_imagej_roi(row);
    auto restored = decode_imagej_roi(bytes);
    check(restored.roi.name == row.roi.name &&
          restored.properties == row.properties && restored.group == 7);
    check(restored.roi.t == 0 && restored.roi.c == 2 && restored.roi.z == 3);
    check(restored.roi.selection.points == row.roi.selection.points &&
          roi_style(restored.roi.selection).stroke_width == 2.5);
    for (std::size_t n = 0; n < bytes.size(); ++n) {
      auto cut = bytes;
      cut.resize(n);
      refuses([&] { decode_imagej_roi(cut); });
    }
    auto changed = bytes;
    changed[48] = 1;
    refuses([&] { decode_imagej_roi(changed); });
    changed = bytes;
    changed[63] = 8;
    refuses([&] { decode_imagej_roi(changed); });
    refuses([&] { decode_imagej_roi(bytes, {64, 65536, 1024}); });
    ImageJRoi wide_point;
    wide_point.roi = {"wide", Selection("point", {{2, 3}}, 3), {}, {}, {}, {}};
    refuses([&] { encode_imagej_roi(wide_point); });
    for (const auto &kind : {"rotated_rectangle", "rotated_ellipse"}) {
      ImageJRoi rotated;
      rotated.roi = {
          "rotated", Selection(kind, {{6, 4}, {9, 4}, {6, 6}}), 0, 1, 2, {}};
      auto wire = encode_imagej_roi(rotated);
      check(wire[6] == 7 &&
            wire[49] ==
                (rotated.roi.selection.kind == "rotated_rectangle" ? 5 : 3));
      auto back = decode_imagej_roi(wire);
      check(back.roi.selection.kind == rotated.roi.selection.kind &&
            back.roi.t == 0 && back.roi.c == 1 && back.roi.z == 2);
      for (std::size_t i = 0; i < 3; ++i)
        for (std::size_t j = 0; j < 2; ++j)
          check(std::abs(back.roi.selection.points[i][j] -
                         rotated.roi.selection.points[i][j]) < 1e-6);
      // A zero axis width/aspect must fail rather than inventing geometry.
      for (int offset = 52; offset != 56; ++offset)
        wire[offset] = 0;
      refuses([&] { decode_imagej_roi(wire); });
    }
    ImageJRoi tagged;
    tagged.roi = {"fallback", Selection("polygon", {{1, 1}, {6, 1}, {3, 5}}),
                  {},         {},
                  {},         {}};
    tagged.source_subtype = 5;
    tagged.ignored_rotated_parameters = {{100, 200, 300, 400, 5}};
    auto tagged_wire = encode_imagej_roi(tagged);
    auto tagged_back = decode_imagej_roi(tagged_wire);
    check(tagged_back.roi.selection.kind == "polygon" &&
          tagged_back.roi.selection.points == tagged.roi.selection.points &&
          tagged_back.ignored_rotated_parameters ==
              tagged.ignored_rotated_parameters);
    check(encode_imagej_roi(tagged_back) == tagged_wire);
    const std::vector<double> path{0, 1, 1, 1, 8, 1, 1, 8, 8, 1, 1, 8, 4,
                                   0, 3, 3, 1, 6, 3, 1, 6, 6, 1, 3, 6, 4};
    auto area = imagej_shape_mask(path, 10, 10);
    auto raster = area.mask(10, 10);
    for (int y = 0; y < 10; ++y)
      for (int x = 0; x < 10; ++x)
        check(bool(raster.pixels[y * 10 + x]) ==
              (x >= 1 && x < 8 && y >= 1 && y < 8 &&
               !(x >= 3 && x < 6 && y >= 3 && y < 6)));
    ImageJRoi shape;
    shape.roi = {"holes", area, 0, 1, 2, {}};
    shape.composite_path = path;
    auto shape_bytes = encode_imagej_roi(shape);
    refuses([&] { decode_imagej_roi(shape_bytes); });
    auto shape_back = decode_imagej_roi(shape_bytes, {}, 10, 10);
    check(shape_back.composite_path == path &&
          shape_back.roi.selection.packed_mask() == area.packed_mask());
    auto wrong = shape;
    wrong.roi.selection =
        Selection::from_packed_mask(10, 10, std::vector<unsigned char>(13));
    refuses([&] { encode_imagej_roi(wrong); });
    wrong.composite_path.clear();
    auto empty = decode_imagej_roi(encode_imagej_roi(wrong), {}, 10, 10);
    check(empty.roi.selection.packed_mask() ==
          wrong.roi.selection.packed_mask());
    ImageJRoi rounded;
    rounded.rounded_rectangle = {{1.25, 1.75, 8.5, 6.5, 4}};
    rounded.roi = {
        "rounded",
        imagej_rounded_rectangle_mask(*rounded.rounded_rectangle, 12, 10),
        0,
        1,
        2,
        {}};
    rounded.options |= 8 | 16 | 32 | 64 | 2048;
    rounded.overlay_label_color = 0xff123456;
    rounded.overlay_font_size = 14;
    auto rounded_wire = encode_imagej_roi(rounded);
    auto rounded_back = decode_imagej_roi(rounded_wire, {}, 12, 10);
    check(rounded_back.rounded_rectangle == rounded.rounded_rectangle &&
          rounded_back.roi.selection.packed_mask() ==
              rounded.roi.selection.packed_mask());
    check(rounded_back.options == (rounded.options | 128) &&
          rounded_back.overlay_font_size == 14 &&
          rounded_back.overlay_label_color == 0xff123456);
    ImageJRoi arrow;
    arrow.arrow = ImageJArrow{{8.25, 12.5, 48.75, 32.25}, 1, 10};
    arrow.options |= 2 | 4;
    arrow.roi = {"arrow", imagej_arrow_mask(*arrow.arrow, 2.5, 64, 48, true),
                 0,       1,
                 2,       {}};
    auto arrow_bytes = encode_imagej_roi(arrow);
    check(arrow_bytes[6] == 3 && arrow_bytes[49] == 2 && arrow_bytes[52] == 1 &&
          arrow_bytes[53] == 10);
    auto arrow_back = decode_imagej_roi(arrow_bytes, {}, 64, 48);
    check(arrow_back.arrow &&
          arrow_back.arrow->endpoints == arrow.arrow->endpoints &&
          arrow_back.arrow->style == 1 && arrow_back.arrow->head_size == 10 &&
          arrow_back.roi.selection.packed_mask() ==
              arrow.roi.selection.packed_mask() &&
          arrow_back.options == (arrow.options | 128));
    refuses([&] { decode_imagej_roi(arrow_bytes); });
    auto invalid_arrow = *arrow.arrow;
    invalid_arrow.style = 5;
    refuses([&] { imagej_arrow_mask(invalid_arrow, 2.5, 64, 48); });
    ImageJRoi spline;
    spline.spline = ImageJSpline{
        {{2.25, 3.5}, {24.75, 4.25}, {17.5, 18.75}, {4.5, 16.25}}, 0};
    spline.roi = {"spline", imagej_spline_selection(*spline.spline), 0, 1, 2,
                  {}};
    check(spline.roi.selection.kind == "polygon" &&
          spline.roi.selection.points.size() == 100);
    auto spline_back = decode_imagej_roi(encode_imagej_roi(spline));
    check(spline_back.spline &&
          spline_back.spline->control_points == spline.spline->control_points &&
          spline_back.roi.selection.points == spline.roi.selection.points);
    spline.spline =
        ImageJSpline{{{40, 60}, {480, 85}, {350, 375}, {80, 325}}, 8, true};
    spline.roi.selection = imagej_spline_selection(*spline.spline);
    check(spline.roi.selection.points.size() > 100);
    auto integer_wire = encode_imagej_roi(spline);
    check(!(integer_wire[51] & 128));
    auto integer_back = decode_imagej_roi(integer_wire);
    check(integer_back.spline && integer_back.spline->integer_coordinates &&
          integer_back.roi.selection.points == spline.roi.selection.points);
    ImageJTextRoi text;
    text.bounds = {3.25, 4.5, 50, 20};
    text.text = u8"line \u03bb\U0001F600\nsecond\n";
    text.name = "annotation";
    text.font_name = "SansSerif";
    text.font_style = 3;
    text.justification = 2;
    text.draw_string_mode = true;
    text.angle = -30.5;
    text.t = 0;
    text.c = 1;
    text.z = 2;
    text.stroke_color = 0x80112233;
    text.fill_color = 0xff445566;
    text.stroke_width = 2.5;
    auto text_bytes = encode_imagej_text_roi(text);
    auto text_back = decode_imagej_text_roi(text_bytes);
    check(text_back.bounds == text.bounds && text_back.text == text.text &&
          text_back.name == text.name &&
          text_back.font_name == text.font_name && text_back.font_style == 3 &&
          text_back.justification == 2 && text_back.draw_string_mode &&
          text_back.angle == -30.5 &&
          text_back.stroke_color == text.stroke_color &&
          text_back.fill_color == text.fill_color &&
          text_back.stroke_width == 2.5 && text_back.z == 2);
    refuses([&] { decode_imagej_roi(text_bytes); });
    refuses([&] { decode_imagej_text_roi(bytes); });
    ImageJImageRoi image;
    image.pixels = Image(3, 2, 3, PixelType::UInt8);
    for (std::size_t i = 0; i < image.pixels.pixels.size(); ++i)
      image.pixels.pixels[i] = double(i * 13 % 256);
    image.origin = {-2, 5};
    image.opacity = 127;
    image.zero_transparent = true;
    image.t = 0;
    image.c = 1;
    image.z = 2;
    image.name = "pixels";
    auto image_bytes = encode_imagej_image_roi(image);
    auto image_back = decode_imagej_image_roi(image_bytes);
    check(image_back.pixels.pixels == image.pixels.pixels &&
          image_back.origin == image.origin && image_back.opacity == 127 &&
          image_back.zero_transparent && image_back.name == image.name &&
          image_back.z == 2);
    check(encode_imagej_image_roi(image_back) == image_bytes);
    refuses([&] { decode_imagej_roi(image_bytes); });
    image_back.pixels.pixels[0] = 255;
    refuses([&] { encode_imagej_image_roi(image_back); });
    image_back.serialized_image.clear();
    check(decode_imagej_image_roi(encode_imagej_image_roi(image_back))
              .pixels.pixels[0] == 255);
    const std::vector<ImageJRoiZipEntry> zip_members{
        {"region.roi", bytes},
        {"text.roi", text_bytes},
        {"image.roi", image_bytes},
        {"spline.roi", integer_wire}};
    for (bool compressed : {false, true}) {
      auto archive = encode_imagej_roi_zip(zip_members, {}, compressed);
      auto members = decode_imagej_roi_zip(archive);
      check(members.size() == zip_members.size());
      for (std::size_t i = 0; i < members.size(); ++i)
        check(members[i].name == zip_members[i].name &&
              members[i].data == zip_members[i].data);
      auto tiny = ImageJRoiZipLimits{};
      tiny.max_entries = 1;
      refuses([&] { decode_imagej_roi_zip(archive, tiny); });
      archive[archive.size() - 18] = 1;
      refuses([&] { decode_imagej_roi_zip(archive); });
    }
    ImageJRoiPlaneMapping mapping;
    mapping.extent = {3, 4, 5, 7, 11, 3};
    mapping.plane = Plane::XZ;
    mapping.coordinate = 3;
    ImageJRoi unpositioned;
    unpositioned.roi = {
        "mapped", Selection("rectangle", {{1, 1}, {4, 3}}), {}, {}, {}, {}};
    unpositioned.properties = "kept: source\n";
    auto mapped = map_imagej_roi(unpositioned, mapping);
    check(mapped.annotation().plane == Plane::XZ &&
          mapped.annotation().coordinate == 3 &&
          mapped.source_record().properties == unpositioned.properties &&
          !mapped.annotation().t && !mapped.annotation().c);
    check(planar_annotation_voxel(mapped.annotation(), {1.25, 2.5}) ==
          std::array<double, 3>{.75, 3, 2});
    mapping.plane = Plane::XY;
    mapping.coordinate.reset();
    mapping.legacy_position = ImageJLegacyPositionMapping::HyperstackCZT;
    unpositioned.legacy_position = 60;
    auto hyper = map_imagej_roi(unpositioned, mapping);
    check(hyper.annotation().t == 2 && hyper.annotation().c == 3 &&
          hyper.annotation().coordinate == 4);
    PlanarAnnotation replacement{
        "new", Selection("rectangle", {{0, 0}, {2, 2}}), Plane::YZ, 1, {}, 0,
        {}};
    auto rebound =
        rebind_imagej_roi_mapping(hyper, {1, 1, 3, 4, 5, 1}, replacement);
    check(rebound.annotation() == replacement &&
          rebound.source_record().legacy_position == 60 &&
          rebound.source_mapping().extent.t == 3);
    validate_imagej_roi_mapping(rebound);
    auto envelope = encode_imagej_roi_envelope(hyper);
    auto envelope_restored = decode_imagej_roi_envelope(envelope);
    check(envelope_restored.annotation().selection.points ==
              hyper.annotation().selection.points &&
          envelope_restored.annotation().plane == Plane::XY &&
          envelope_restored.annotation().coordinate == 4 &&
          envelope_restored.annotation().t == 2 &&
          envelope_restored.annotation().c == 3 &&
          envelope_restored.source_record().properties == "kept: source\n" &&
          envelope_restored.source_record().legacy_position == 60);
    auto stale = decode_imagej_roi(envelope);
    stale.roi.name = "external edit";
    refuses([&] { decode_imagej_roi_envelope(encode_imagej_roi(stale)); });
    refuses([&] { encode_imagej_roi_envelope(rebound); });
    ImageJRoi projection;
    projection.roi = {"new", replacement.selection, {}, 0, 1, {}};
    auto native_envelope =
        encode_imagej_roi_native_envelope(rebound, projection);
    auto native_restored = decode_imagej_roi_envelope(native_envelope);
    check(native_restored.annotation() == replacement &&
          native_restored.target_extent().dimensions() ==
              rebound.target_extent().dimensions() &&
          native_restored.source_record().legacy_position == 60);
    projection.roi.name = "wrong native projection";
    refuses([&] { encode_imagej_roi_native_envelope(rebound, projection); });
    DatasetReadObserver import_reads;
    Stack import_planes;
    import_planes.emplace_back(11, 7, 1, PixelType::UInt16);
    Document import_owner(
        Dataset::from_planes({1, 1, 1, 7, 11, 1}, std::move(import_planes))
            .observe_reads(import_reads));
    ImageJRoiPlaneMapping import_mapping;
    import_mapping.extent = {1, 1, 1, 7, 11, 1};
    import_mapping.coordinate = 0;
    ImageJRoi import_roi;
    import_roi.roi = {
        "import", Selection("rectangle", {{1, 1}, {3, 3}}), {}, {}, {}, {}};
    import_roi.group = 7;
    import_roi.properties = "retained: source\n";
    auto import_bytes = encode_imagej_roi(import_roi);
    auto import_source = map_imagej_roi(import_roi, import_mapping);
    auto import_envelope = encode_imagej_roi_envelope(import_source);
    std::vector<ImageJRoiImportItem> import_items{
        {"bare.roi", import_bytes, import_mapping},
        {"envelope.roi", import_envelope, {}}};
    const auto import_before = import_owner.snapshot();
    const auto import_resolved =
        resolve_imagej_roi_import(import_before, import_items);
    validate_imagej_roi_import_record(import_resolved.record());
    {
      const DatasetShape target_shape{1, 1, 1, 9, 13, 1};
      Stack target_planes;
      target_planes.emplace_back(13, 9, 1, PixelType::UInt16);
      Document target(
          Dataset::from_planes(target_shape, std::move(target_planes))
              .observe_reads(import_reads));
      PlanarAnnotation native_row{"explicit target",
                                  Selection("point", {{2.250000000001, .5}}),
                                  Plane::XZ,
                                  4,
                                  {},
                                  {},
                                  {{"opaque", std::string("retained")}}};
      const ImageJRoiNativeReplacement native_choice{target_shape, native_row};
      const std::vector<ImageJRoiImportItem> native_items{
          {"original.roi", import_envelope, {}, native_choice}};
      const auto target_before = target.snapshot();
      auto changed = resolve_imagej_roi_import(target_before, native_items);
      check(changed.records()[0].annotation() == native_row &&
            changed.records()[0].source_mapping().extent.dimensions() ==
                import_mapping.extent.dimensions() &&
            changed.records()[0].source_record().group == 7 &&
            std::get<std::int64_t>(changed.record().at("version")) == 2);
      const auto inputs = decode_imagej_roi_import_request(changed.record());
      check(inputs.items.size() == 1 &&
            inputs.items[0].data == import_envelope &&
            inputs.items[0].native_replacement &&
            inputs.items[0].native_replacement->annotation == native_row);
      auto bad = changed.record();
      bad["version"] = 1;
      refuses([&] { validate_imagej_roi_import_record(bad); });
      auto prepared = prepare_imagej_roi_import(
          imagej_roi_import_from_record(target_before, changed.record()));
      target.commit(prepared);
      check(planar_annotations(target.snapshot().data()).at(0) == native_row &&
            target.undo_steps() == 1);
      target.undo();
      check(target.snapshot().shares_data(target_before));
      target.redo();
      const auto repeated = resolve_imagej_roi_import(
          target.snapshot(), native_items, ImageJRoiImportMode::Replace);
      validate_imagej_roi_import_record(repeated.record());
      auto noop = prepare_imagej_roi_import(repeated);
      target.commit(noop);
      check(target.undo_steps() == 1 && import_reads.counts().plane_reads == 0);
    }
    const auto import_request =
        decode_imagej_roi_import_request(import_resolved.record());
    check(import_request.items.size() == 2 &&
          import_request.items[0].data == import_items[0].data &&
          import_request.items[0].mapping.has_value() &&
          !import_request.items[1].mapping &&
          import_request.source_extent.dimensions() ==
              import_before.data().dataset.shape().dimensions() &&
          import_request.mode == ImageJRoiImportMode::Append);
    auto import_replay =
        imagej_roi_import_from_record(import_before, import_resolved.record());
    auto invalid_import_record = import_resolved.record();
    invalid_import_record["after"] = invalid_import_record.at("before");
    refuses([&] { validate_imagej_roi_import_record(invalid_import_record); });
    refuses([&] { decode_imagej_roi_import_request(invalid_import_record); });
    refuses([&] {
      rebind_imagej_roi_import(import_before, invalid_import_record,
                               import_items);
    });
    auto import_edit = prepare_imagej_roi_import(import_replay);
    import_owner.commit(import_edit);
    check(planar_annotations(import_owner.snapshot().data()).size() == 2 &&
          import_owner.snapshot().data().metadata.extensions.count(
              imagej_roi_import_sources_extension) == 1 &&
          import_resolved.records()[0].source_record().group == 7 &&
          import_reads.counts().plane_reads == 0 &&
          import_reads.counts().region_reads == 0);
    import_owner.undo();
    check(planar_annotations(import_owner.snapshot().data()).empty() &&
          !import_owner.snapshot().data().metadata.extensions.count(
              imagej_roi_import_sources_extension));
    import_owner.redo();
    check(planar_annotations(import_owner.snapshot().data()).size() == 2);
    const auto pairing_before = import_owner.snapshot();
    const auto catalog = inspect_imagej_roi_sources(pairing_before);
    check(catalog.entries().size() == 2 && catalog.entries()[0].index == 0 &&
          catalog.entries()[0].data_bytes == import_items[0].data.size());
    const auto pairing = resolve_imagej_roi_pairing(catalog, 0, 1);
    check(pairing.source_index() == 0 && pairing.annotation_index() == 1 &&
          pairing.source_item().data == import_items[0].data &&
          pairing.mapped().annotation() ==
              planar_annotations(pairing_before.data())[1] &&
          pairing.mapped().source_record().group == 7);
    validate_imagej_roi_pairing_record(pairing.record());
    const auto paired_replay =
        imagej_roi_pairing_from_record(pairing_before, pairing.record());
    check(paired_replay.mapped().annotation() ==
              pairing.mapped().annotation() &&
          import_owner.snapshot().shares_data(pairing_before) &&
          import_reads.counts().plane_reads == 0 &&
          import_reads.counts().region_reads == 0);
    refuses([&] { resolve_imagej_roi_pairing(catalog, 2, 0); });
    refuses([&] { resolve_imagej_roi_pairing(catalog, 0, 2); });
    Document pairing_other(pairing_before);
    refuses([&] {
      imagej_roi_pairing_from_record(pairing_other.snapshot(),
                                     pairing.record());
    });
    check(rebind_imagej_roi_pairing(pairing_other.snapshot(), pairing.record(),
                                    1, 0)
              .mapped()
              .annotation() ==
          planar_annotations(pairing_other.snapshot().data())[0]);
    ImageJRoi export_projection;
    const auto &export_row = pairing.mapped().annotation();
    export_projection.roi = {export_row.name,       export_row.selection,
                             export_row.t,          export_row.c,
                             export_row.coordinate, {}};
    export_projection.group = 9;
    const auto exported =
        resolve_imagej_roi_export(pairing_before, pairing, export_projection);
    validate_imagej_roi_export_record(exported.record());
    const auto export_request =
        decode_imagej_roi_export_request(exported.record());
    check(exported.record().size() == 6 &&
          export_request.standard_projection.group == 9 &&
          imagej_roi_export_from_record(pairing_before, exported.record())
                  .data() == exported.data() &&
          decode_imagej_roi_native_envelope(exported.data())
                  .source_record()
                  .group == 7 &&
          import_owner.snapshot().shares_data(pairing_before));
    auto bad_export = exported.record();
    bad_export["output"] = bad_export.at("projection");
    refuses([&] { validate_imagej_roi_export_record(bad_export); });
    refuses([&] {
      imagej_roi_export_from_record(pairing_other.snapshot(),
                                    exported.record());
    });
    const auto other_pair = resolve_imagej_roi_pairing(
        inspect_imagej_roi_sources(pairing_other.snapshot()), 0, 1);
    check(rebind_imagej_roi_export(pairing_other.snapshot(), exported.record(),
                                   other_pair, export_projection)
              .data() == exported.data());
    check(import_reads.counts().plane_reads == 0 &&
          import_reads.counts().region_reads == 0);
    const std::vector<ImageJRoiZipExportItem> zip_export_items{
        {"first.roi", 0, 1, export_projection},
        {"second.roi", 1, 1, export_projection}};
    const auto zip_export =
        resolve_imagej_roi_zip_export(pairing_before, zip_export_items);
    validate_imagej_roi_zip_export_record(zip_export.record());
    const auto zip_export_request =
        decode_imagej_roi_zip_export_request(zip_export.record());
    check(zip_export.members().size() == 2 &&
          zip_export_request.items.size() == 2 &&
          zip_export.record().size() == 9 &&
          imagej_roi_zip_export_from_record(pairing_before, zip_export.record())
                  .data() == zip_export.data() &&
          decode_imagej_roi_zip(zip_export.data()).size() == 2 &&
          import_owner.snapshot().shares_data(pairing_before));
    check(rebind_imagej_roi_zip_export(pairing_other.snapshot(),
                                       zip_export.record(), zip_export_items)
              .data() == zip_export.data());
    refuses([&] {
      imagej_roi_zip_export_from_record(pairing_other.snapshot(),
                                        zip_export.record());
    });
    auto bad_zip_export = zip_export.record();
    bad_zip_export["output"] = std::string("AAAA");
    refuses([&] { validate_imagej_roi_zip_export_record(bad_zip_export); });
    auto bad_zip_items = zip_export_items;
    bad_zip_items.back().name = "first.roi";
    refuses(
        [&] { resolve_imagej_roi_zip_export(pairing_before, bad_zip_items); });
    check(import_reads.counts().plane_reads == 0 &&
          import_reads.counts().region_reads == 0);
    Document refused_owner(import_before);
    auto import_guard = refused_owner.constrain([](const DocumentData &data,
                                                   const DisplayState &) {
      if (data.metadata.extensions.count(imagej_roi_import_sources_extension))
        throw std::invalid_argument("test host refuses source archive");
    });
    import_items.back().data = import_envelope;
    auto refused_import =
        resolve_imagej_roi_import(refused_owner.snapshot(), import_items);
    auto refused_edit = prepare_imagej_roi_import(refused_import);
    refuses([&] { refused_owner.commit(refused_edit); });
    check(refused_edit.active() &&
          planar_annotations(refused_owner.snapshot().data()).empty() &&
          !refused_owner.snapshot().data().metadata.extensions.count(
              imagej_roi_import_sources_extension));
    import_items.back().data[0] = 0;
    refuses([&] {
      resolve_imagej_roi_import(import_owner.snapshot(), import_items);
    });
    check(planar_annotations(import_owner.snapshot().data()).size() == 2);
    unpositioned.properties = "imagejxx.roi.v99: unknown\n";
    refuses([&] { map_imagej_roi(unpositioned, mapping); });
    ImageJRoiLimits tiny;
    tiny.max_mask_pixels = 10;
    refuses([&] { decode_imagej_roi(shape_bytes, tiny, 10, 10); });
    std::cout << "PASS bounded ImageJ ROI codec\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
