#include <imagejxx/imagej_display_roi.hpp>
#include <iostream>
using namespace imagejxx;
namespace {
void check(bool v) {
  if (!v)
    throw std::runtime_error("Display ROI assertion failed");
}
template <class F> void rejects(F f) {
  try {
    f();
  } catch (const std::exception &) {
    return;
  }
  throw std::runtime_error("Expected display ROI refusal");
}
} // namespace
int main() {
  try {
    ImageJDisplayRoiMapping mapping;
    mapping.plane.extent = {2, 2, 4, 12, 16, 1};
    mapping.plane.plane = Plane::XZ;
    mapping.plane.coordinate = 0;
    mapping.plane.z_position = ImageJZPositionMapping::FixedAxis;
    ImageJTextRoi text;
    text.name = "original";
    text.text = "Text";
    text.bounds = {1.25, 2.5, 8, 4};
    text.stroke_color = 0xff112233;
    text.group = 7;
    text.properties = "source\n";
    text.t = 0;
    text.c = 1;
    text.z = 0;
    const auto original = encode_imagej_text_roi(text);
    auto mapped = decode_imagej_display_roi_mapped(original, mapping);
    check(mapped.source_bytes() == original &&
          mapped.annotation().plane == Plane::XZ && mapped.annotation().t == 0);
    auto row = mapped.annotation();
    row.name = "edited";
    auto &p = std::get<TextAnnotationPayload>(row.payload);
    p.font_size = 13.500000000001;
    p.bounds[0] = 1.250000000001;
    p.text = "Current";
    row.attributes = {{"opaque", std::uint64_t(-1)}};
    auto rebound =
        rebind_imagej_display_roi_mapping(mapped, mapping.plane.extent, row);
    check(&mapped.source_record() == &rebound.source_record() &&
          mapped.source_bytes().data() == rebound.source_bytes().data());
    auto standard = std::get<ImageJTextRoi>(mapped.source_record());
    standard.name = row.name;
    standard.text = p.text;
    standard.bounds = p.bounds;
    standard.font_size = 14;
    standard.group = 29;
    standard.properties = "projected\n";
    rejects([&] { encode_imagej_display_roi_envelope(rebound, standard); });
    ImageJDisplayProjectionOptions options;
    options.round_font_size = true;
    const auto wire =
        encode_imagej_display_roi_envelope(rebound, standard, options);
    auto decoded = decode_imagej_display_roi_envelope_full(wire);
    check(decoded.mapped.annotation() == row &&
          decoded.mapped.source_bytes() == original &&
          decoded.options.round_font_size);
    check(std::get<ImageJTextRoi>(decoded.standard_projection).properties ==
              "projected\n" &&
          std::get<ImageJTextRoi>(decoded.mapped.source_record()).group == 7);
    check(encode_imagej_display_roi_envelope(decoded.mapped,
                                             decoded.standard_projection,
                                             decoded.options) == wire);
    auto damaged = wire;
    damaged.back() ^= 1;
    rejects([&] { decode_imagej_display_roi_envelope(damaged); });
    text.stroke_color = 0;
    auto no_color = encode_imagej_text_roi(text);
    rejects([&] { decode_imagej_display_roi_mapped(no_color, mapping); });
    mapping.text_default_color = 0xff334455;
    auto resolved = decode_imagej_display_roi_mapped(no_color, mapping);
    check(
        std::get<TextAnnotationPayload>(resolved.annotation().payload).color ==
        0xff334455);
    ImageJImageRoi image;
    image.pixels = Image(2, 1, 3, PixelType::UInt8);
    image.pixels.pixels = {1, 2, 3, 4, 5, 6};
    image.origin = {1, 2};
    image.z = 0;
    mapping.text_default_color.reset();
    auto image_wire = encode_imagej_image_roi(image);
    auto im = decode_imagej_display_roi_mapped(image_wire, mapping);
    auto native = im.annotation();
    ImageAnnotationPayload rgba;
    rgba.width = 2;
    rgba.height = 1;
    rgba.channels = 4;
    rgba.pixels = {255, 0, 0, 128, 0, 0, 0, 255};
    rgba.bounds = {1.25, 2.5, 4.5, 2.25};
    rgba.opacity = 77;
    rgba.zero_transparent = true;
    native.payload = rgba;
    auto edited =
        rebind_imagej_display_roi_mapping(im, mapping.plane.extent, native);
    auto projection = std::get<ImageJImageRoi>(im.source_record());
    projection.serialized_image.clear();
    projection.pixels.pixels = {255, 0, 0, 0, 0, 0};
    projection.origin = {1, 3};
    projection.opacity = 77;
    projection.zero_transparent = true;
    rejects([&] { encode_imagej_display_roi_envelope(edited, projection); });
    options = {};
    options.drop_image_alpha = options.reset_image_scale =
        options.round_image_origin = true;
    const auto output =
        encode_imagej_display_roi_envelope(edited, projection, options);
    const auto back = decode_imagej_display_roi_envelope_full(output);
    check(back.mapped.annotation() == native &&
          back.mapped.source_bytes() == image_wire &&
          back.options.drop_image_alpha && back.options.reset_image_scale &&
          back.options.round_image_origin);
    check(encode_imagej_display_roi_envelope(
              back.mapped, back.standard_projection, back.options) == output);
    std::cout << "PASS typed ImageJ display ROI mapping and envelopes\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
