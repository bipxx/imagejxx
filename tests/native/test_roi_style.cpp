#include <algorithm>
#include <cmath>
#include <imagejxx/operations.hpp>
#include <imagejxx/roi_style.hpp>
#include <imagejxx/selection_edit.hpp>
#include <imagejxx/targets.hpp>
#include <iostream>
#include <limits>

using namespace imagejxx;
namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class Function> void invalid(Function work) {
  bool rejected = false;
  try {
    work();
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  check(rejected, "Invalid ROI style was accepted");
}
MetadataValue color(std::initializer_list<std::int64_t> components) {
  MetadataArray values;
  for (auto component : components)
    values.emplace_back(component);
  return metadata_array(std::move(values));
}
const MetadataObject &style_record(const Selection &selection) {
  return std::get<MetadataObject>(
      std::get<std::shared_ptr<const MetadataTree>>(
          selection.attributes.at(roi_style_attribute))
          ->value);
}
Selection area() {
  Selection result("rectangle", {{2, 2}, {7, 7}}, 3);
  result.attributes = {
      {"color", std::string("#12345678")},
      {"label", std::string("Retained")},
      {"vendor", metadata_object({{"id", std::uint64_t(-1)}})}};
  return result;
}
double blend(double background, unsigned char foreground, unsigned char alpha) {
  return std::floor(background * (1 - alpha / 255.) +
                    foreground * (alpha / 255.) + .5);
}
void colors() {
  const std::vector<std::pair<std::string, RoiColor>> cases{
      {"red", {255, 0, 0, 255}},
      {"green", {0, 255, 0, 255}},
      {"blue", {0, 0, 255, 255}},
      {"magenta", {255, 0, 255, 255}},
      {"cyan", {0, 255, 255, 255}},
      {"yellow", {255, 255, 0, 255}},
      {"orange", {255, 200, 0, 255}},
      {"black", {0, 0, 0, 255}},
      {"white", {255, 255, 255, 255}},
      {"gray", {128, 128, 128, 255}},
      {"lightgray", {192, 192, 192, 255}},
      {"darkgray", {64, 64, 64, 255}},
      {"pink", {255, 175, 175, 255}},
      {"grey", {128, 128, 128, 255}},
      {"Light Grey", {192, 192, 192, 255}},
      {"#123456", {18, 52, 86, 255}},
      {"#80123456", {18, 52, 86, 128}},
      {"#00123456", {18, 52, 86, 0}},
      {"#FFABCDEF", {171, 205, 239, 255}}};
  for (const auto &item : cases) {
    const auto parsed = parse_roi_color(item.first);
    check(parsed && *parsed == item.second, "Incorrect ImageJ ROI color");
    check(parse_roi_color(roi_color_string(parsed)) == parsed,
          "Canonical alpha-first color did not round-trip");
  }
  check(!parse_roi_color("none") && !parse_roi_color(" NULL ") &&
            roi_color_string({}) == "none",
        "Null ROI colors are distinct from transparent RGBA");
  check(parse_color("#80123456") == RoiColor{128, 18, 52, 86} &&
            parse_color("green") == RoiColor{0, 128, 0, 255},
        "Legacy alpha-last colors or named green changed");
  for (const auto &text : {"", "#12345", "#1234567", "#gg123456", "unknown"})
    invalid([&] { (void)parse_roi_color(text); });
}
void setters_and_resolution() {
  const auto source = area();
  const RoiColor fallback{9, 8, 7, 6};
  check(!roi_style(source).present &&
            resolve_roi_style(source, fallback).stroke_color ==
                std::optional<RoiColor>({18, 52, 86, 120}),
        "Absent style must preserve legacy color interpretation");
  const auto stroke =
      with_roi_style(source, {{"stroke_color", std::string("green")}});
  const auto filled =
      with_roi_style(stroke, {{"fill_color", std::string("#80123456")}});
  const auto fill = roi_style(filled);
  check(fill.present && fill.has_stroke && !fill.stroke_color &&
            fill.has_fill &&
            fill.fill_color == std::optional<RoiColor>({18, 52, 86, 128}) &&
            !resolve_roi_style(filled, fallback).stroke_color,
        "Area fill-only setter did not clear stroke");
  const auto both =
      with_roi_style(source, {{"stroke_color", std::string("#80403020")},
                              {"fill_color", std::string("#80123456")},
                              {"stroke_width", 0.}});
  const auto resolved = resolve_roi_style(both, fallback);
  check(resolved.stroke_color == std::optional<RoiColor>({64, 48, 32, 128}) &&
            resolved.fill_color == fill.fill_color && resolved.cosmetic &&
            resolved.stroke_width == 0 && both.width == 3,
        "Simultaneous setters must apply fill before explicit stroke");
  const auto cleared =
      with_roi_style(filled, {{"fill_color", std::monostate{}}});
  check(!resolve_roi_style(cleared, fallback).fill_color &&
            resolve_roi_style(cleared, fallback).stroke_color == fallback,
        "Null stroke without fill must resolve the fallback outline");
  const auto transparent =
      with_roi_style(source, {{"fill_color", color({18, 52, 86, 0})}});
  check(roi_style(transparent).fill_color &&
            !resolve_roi_style(transparent, fallback).stroke_color,
        "Alpha-zero fill must retain its value and suppress fallback stroke");
  const auto invisible =
      with_roi_style(source, {{"stroke_color", color({18, 52, 86, 0})}});
  check(resolve_roi_style(invisible, fallback).stroke_color ==
            std::optional<RoiColor>({18, 52, 86, 0}),
        "Explicit transparent stroke was replaced by a fallback");
  const auto extended = with_roi_style(
      both,
      {{"vendor",
        metadata_object({{"values", metadata_array({false, std::monostate{},
                                                    std::uint64_t(-1)})}})}});
  const auto updated = with_roi_style(extended, {{"stroke_width", 2.5}});
  check(metadata_equal(updated.attributes.at("vendor"),
                       source.attributes.at("vendor")) &&
            std::get<std::string>(updated.attributes.at("color")) ==
                "#12345678" &&
            metadata_equal(style_record(updated).at("vendor"),
                           style_record(extended).at("vendor")) &&
            updated.points == source.points && source == area(),
        "Style patches must retain geometry, legacy and nested extensions");
  SelectionEditor editor(source, 9, 9);
  check(
      editor.roi_style({{"stroke_color", std::string("red")}}) ==
              with_roi_style(source, {{"stroke_color", std::string("red")}}) &&
          editor.selection() == source,
      "SelectionEditor style edits must remain detached");
}
void invalid_records_are_atomic() {
  const auto source =
      with_roi_style(area(), {{"stroke_color", std::string("blue")}});
  const std::vector<MetadataObject> patches{
      {{"schema_version", std::int64_t(2)}},
      {{"schema_version", true}},
      {{"schema_version", 1.}},
      {{"schema_version", std::string("1")}},
      {{"stroke_width", -1.}},
      {{"stroke_width", true}},
      {{"stroke_width", std::monostate{}}},
      {{"stroke_width", std::string("2")}},
      {{"stroke_color", color({1, 2, 3})}},
      {{"stroke_color", color({1, 2, 3, 256})}},
      {{"fill_color", color({1, 2, -1, 4})}},
      {{"stroke_color", metadata_array({1., std::int64_t(2), std::int64_t(3),
                                        std::int64_t(4)})}},
      {{"stroke_color", metadata_array({true, std::int64_t(2), std::int64_t(3),
                                        std::int64_t(4)})}},
      {{"stroke_color", std::int64_t(12)}},
      {{"fill_color", std::string("#invalid")}}};
  for (auto patch : patches) {
    patch["label"] = std::string("Must not publish");
    invalid([&] { (void)with_roi_style(source, patch); });
    check(source ==
              with_roi_style(area(), {{"stroke_color", std::string("blue")}}),
          "Rejected style patch changed its source");
  }
  for (const auto value : {std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()}) {
    invalid([&] { (void)with_roi_style(source, {{"stroke_width", value}}); });
    invalid([&] {
      (void)with_roi_style(
          source, {{"extension", metadata_object({{"nonfinite", value}})}});
    });
    check(source ==
              with_roi_style(area(), {{"stroke_color", std::string("blue")}}),
          "Eager nonfinite metadata rejection changed the source style");
  }
  for (auto record :
       {MetadataValue(std::monostate{}), MetadataValue(std::string("wrong")),
        metadata_object({}),
        metadata_object({{"schema_version", std::int64_t(1)},
                         {"stroke_color", std::string("red")}})}) {
    auto malformed = area();
    malformed.attributes[roi_style_attribute] = record;
    invalid([&] { malformed.validate(9, 9); });
    invalid([&] { (void)with_roi_style(malformed, {}); });
  }
  auto zeroLegacy = area();
  zeroLegacy.width = 0;
  invalid([&] { zeroLegacy.mask(9, 9); });
}
void masks_and_alpha() {
  const auto source = area();
  const auto styled =
      with_roi_style(source, {{"stroke_color", std::string("red")},
                              {"fill_color", std::string("blue")},
                              {"stroke_width", 6.}});
  check(styled.mask(9, 9).pixels == source.mask(9, 9).pixels &&
            selection_processing_width(styled) == source.width,
        "Area display styling changed the processing region");
  for (const auto &kind : {"line", "polyline", "angle"}) {
    Selection line(kind, {{2, 4}, {6, 4}}, 3);
    const auto thin = with_roi_style(line, {{"stroke_width", 0.}});
    const auto wide = with_roi_style(line, {{"stroke_width", 5.}});
    check(line.width == thin.width && thin.width == wide.width &&
              selection_processing_width(thin) == 1 &&
              selection_processing_width(wide) == 5 &&
              thin.mask(9, 9).pixels ==
                  Selection(kind, line.points, 1).mask(9, 9).pixels &&
              wide.mask(9, 9).pixels ==
                  Selection(kind, line.points, 5).mask(9, 9).pixels &&
              wide.mask(9, 9).pixels != thin.mask(9, 9).pixels,
          "Styled line width did not determine processing thickness");
    check(!resolve_roi_style(
               with_roi_style(line, {{"fill_color", std::string("red")}}))
               .fill_color,
          "Line fill must not render an area interior");
    const auto huge =
        with_roi_style(line, {{"stroke_width", 1e300}}).mask(9, 9);
    check(std::all_of(huge.pixels.begin(), huge.pixels.end(),
                      [](double v) { return v == 255; }),
          "Huge finite line width was not bounded to the image");
  }
  Selection points("multipoint", {{2, 2}, {7, 7}}, 3);
  check(with_roi_style(points, {{"stroke_width", 1e300}}).mask(9, 9).pixels ==
            points.mask(9, 9).pixels,
        "New stroke width changed point-marker processing size");
  const auto hugeOutline =
      roi_stroke_mask(with_roi_style(source, {{"stroke_width", 1e300}}), 9, 9);
  check(std::all_of(hugeOutline.pixels.begin(), hugeOutline.pixels.end(),
                    [](double v) { return v == 255; }),
        "Huge finite area outline overflowed rather than clipping");
  Image region(9, 9, 1, PixelType::UInt8);
  for (int y = 1; y < 6; ++y)
    for (int x = 1; x < 6; ++x)
      region.at(x, y) = 255;
  region.at(3, 3) = 0;
  region.at(7, 7) = 255;
  const auto holed =
      with_roi_style(Selection::from_mask(region),
                     {{"fill_color", color({10, 200, 30, 128})}});
  Image rgb(9, 9, 3, PixelType::UInt8);
  for (std::size_t i = 0; i < 81; ++i) {
    rgb.pixels[i * 3] = 100;
    rgb.pixels[i * 3 + 1] = 30;
    rgb.pixels[i * 3 + 2] = 210;
  }
  const auto rendered = render_roi_style(rgb, holed);
  const RoiColor fill{10, 200, 30, 128}, stroke{200, 10, 60, 64};
  for (std::size_t i = 0; i < 81; ++i)
    for (int c = 0; c < 3; ++c)
      check(rendered.pixels[i * 3 + c] ==
                (region.pixels[i]
                     ? blend(rgb.pixels[i * 3 + c], fill[c], fill[3])
                     : rgb.pixels[i * 3 + c]),
            "Fill alpha must round once and preserve holes/disconnected "
            "components");
  check(holed.mask(9, 9).pixels == region.pixels && rgb.at(3, 3, 0) == 100,
        "Rendering mutated source or fill region");
  const auto both =
      with_roi_style(source, {{"fill_color", color({10, 200, 30, 128})},
                              {"stroke_color", color({200, 10, 60, 64})},
                              {"stroke_width", 0.}});
  const auto combined = render_roi_style(rgb, both);
  for (int c = 0; c < 3; ++c) {
    check(combined.at(4, 4, c) == blend(rgb.at(4, 4, c), fill[c], fill[3]),
          "Interior was painted as an outline");
    check(combined.at(2, 2, c) ==
              blend(blend(rgb.at(2, 2, c), fill[c], fill[3]), stroke[c],
                    stroke[3]),
          "Outline must blend after the area fill");
  }
  check(render_roi_style(rgb, with_roi_style(source, {{"stroke_color",
                                                       color({1, 2, 3, 0})}}))
                .pixels == rgb.pixels,
        "Alpha-zero stroke painted pixels");
  invalid([&] { (void)render_roi_style(Image(9, 9), both); });
  invalid(
      [&] { (void)render_roi_style(Image(9, 9, 3, PixelType::UInt16), both); });
}
void document_history_and_frozen_target() {
  Document owner(Dataset::from_stack({Image(9, 9, 1, PixelType::UInt16)}));
  const auto initial = area();
  const auto styled = with_roi_style(
      initial,
      {{"stroke_color", std::string("red")},
       {"fill_color", std::string("#80123456")},
       {"stroke_width", 0.},
       {"vendor", metadata_object(
                      {{"flags", metadata_array({true, std::monostate{}})}})}});
  owner.select(initial);
  owner.select(styled);
  const auto source = owner.snapshot();
  const auto plane = source.data().dataset.plane({0, 0, 0});
  check(source.data().previous_selection == initial && !source.dirty() &&
            owner.undo_steps() == 0,
        "Active ROI styling must preserve previous selection without pixel "
        "history");
  auto tx = owner.begin_edit();
  tx.set_rois({{"Stored",
                styled,
                0,
                0,
                0,
                {{"vendor", std::string("ROI extension")}}}});
  tx.set_overlays(
      {{{"selection", metadata_object({{"style", styled.attributes.at(
                                                     roi_style_attribute)}})},
        {"vendor", std::string("Overlay extension")}}});
  owner.commit(tx);
  const auto stored = owner.snapshot();
  auto edit = owner.begin_edit();
  auto rois = stored.data().rois;
  rois[0].selection = with_roi_style(rois[0].selection,
                                     {{"stroke_color", std::string("green")}});
  edit.set_rois(rois);
  owner.commit(edit);
  check(owner.undo_steps() == 2 &&
            owner.snapshot().data().dataset.plane({0, 0, 0}) == plane &&
            owner.history().pixels.obsolete_bytes == 0 &&
            stored.data().rois[0].selection == styled,
        "Stored style edit must create one metadata step and share pixel "
        "storage");
  owner.undo();
  check(owner.snapshot().data().rois == stored.data().rois &&
            metadata_equal(owner.snapshot().data().overlays[0],
                           stored.data().overlays[0]),
        "Undo lost stored styles or overlay records");
  owner.redo();
  check(roi_style(owner.snapshot().data().rois[0].selection).stroke_color ==
            std::optional<RoiColor>({0, 255, 0, 255}),
        "Redo lost typed ROI style");
  auto frozen = resolve_target(owner.snapshot(), {}, "add");
  const auto frozenMask = frozen.mask(0)->pixels;
  owner.select(Selection("point", {{0, 0}}));
  check(frozen.source().data().selection == styled &&
            frozen.mask(0)->pixels == frozenMask &&
            owner.snapshot().data().previous_selection == styled,
        "Frozen processing target lost complete selection style");
  Document clone(stored);
  check(clone.snapshot().data().selection == styled &&
            clone.snapshot().data().previous_selection == initial &&
            clone.snapshot().data().rois == stored.data().rois &&
            clone.snapshot().data().dataset.plane({0, 0, 0}) == plane,
        "Snapshot copy lost native styled records or pixel sharing");
}
void inherited_area_width() {
  const Selection source("rectangle", {{2, 2}, {7, 7}}, 3);
  const auto styled =
      with_roi_style(source, {{"stroke_color", std::string("red")}});
  check(!roi_style(styled).stroke_width &&
            resolve_roi_style(styled).stroke_width == 3,
        "Color-only style must inherit the raw positive width");
  const Image rgb(9, 9, 3, PixelType::UInt8);
  const auto rendered = render_roi_style(rgb, styled);
  check(rendered.at(1, 4, 0) == 255 && rendered.at(1, 4, 1) == 0 &&
            rendered.at(2, 4, 0) == 255 && rendered.at(4, 4, 0) == 0 &&
            rendered.at(0, 4, 0) == 0 &&
            rendered.pixels ==
                render_roi_style(rgb,
                                 with_roi_style(styled, {{"stroke_width", 3.}}))
                    .pixels &&
            styled.mask(9, 9).pixels == source.mask(9, 9).pixels,
        "Styled area export ignored its inherited width or changed its "
        "processing mask");
}
MetadataObject selection_record(const Selection &selection) {
  auto record = selection.attributes;
  record["kind"] = selection.kind;
  record["width"] = selection.width;
  MetadataArray points;
  for (const auto &point : selection.points)
    points.push_back(metadata_array({point[0], point[1]}));
  record["points"] = metadata_array(std::move(points));
  return record;
}
void nested_overlay_validation_is_atomic() {
  Document owner(Dataset::from_stack({Image(9, 9, 1, PixelType::UInt16)}),
                 "Original");
  const auto styled =
      with_roi_style(area(), {{"stroke_color", std::string("green")},
                              {"fill_color", std::string("#80123456")},
                              {"stroke_width", 0.}});
  const MetadataObject valid{
      {"kind", std::string("roi")},
      {"selection", metadata_object(selection_record(styled))},
      {"vendor",
       metadata_object({{"flags", metadata_array({std::monostate{}, true,
                                                  std::uint64_t(-1)})}})}};
  auto initial = owner.begin_edit();
  initial.set_overlays({valid});
  owner.commit(initial);
  const auto source = owner.snapshot();
  const std::vector<MetadataObject> invalidStyles{
      {{"schema_version", std::int64_t(2)},
       {"stroke_color", color({1, 2, 3, 4})}},
      {{"schema_version", std::int64_t(1)},
       {"stroke_color", metadata_array({1., std::int64_t(2), std::int64_t(3),
                                        std::int64_t(4)})}},
      {{"schema_version", std::int64_t(1)}, {"stroke_width", -1.}}};
  for (const auto &invalidStyle : invalidStyles) {
    auto nested = selection_record(styled);
    nested[roi_style_attribute] = metadata_object(invalidStyle);
    auto malformed = valid;
    malformed["selection"] = metadata_object(std::move(nested));
    auto tx = owner.begin_edit();
    tx.set_title("Prepared");
    invalid([&] { tx.set_overlays({valid, malformed}); });
    const auto preview = tx.preview();
    check(tx.active() && preview.data().metadata.title == "Prepared" &&
              preview.data().overlays.size() == 1 &&
              metadata_equal(preview.data().overlays[0], valid) &&
              owner.snapshot().shares_data(source) && owner.undo_steps() == 1,
          "Rejected late nested style changed prepared overlays, document or "
          "history");
    auto malformedData = source.data();
    malformedData.overlays = {valid, malformed};
    invalid([&] { (void)Document(malformedData); });
  }
  auto rename = owner.begin_edit();
  rename.set_title("Prepared");
  owner.commit(rename);
  owner.undo();
  check(owner.snapshot().data().metadata.title == "Original" &&
            metadata_equal(owner.snapshot().data().overlays[0], valid),
        "Undo lost a valid nested styled ROI overlay");
  owner.redo();
  Document clone(owner.snapshot());
  check(metadata_equal(clone.snapshot().data().overlays[0], valid) &&
            clone.snapshot().data().dataset.plane({0, 0, 0}) ==
                source.data().dataset.plane({0, 0, 0}),
        "Nested styled overlay copy lost metadata or shared pixel storage");
}
} // namespace
int main() {
  try {
    colors();
    setters_and_resolution();
    invalid_records_are_atomic();
    masks_and_alpha();
    document_history_and_frozen_target();
    inherited_area_width();
    nested_overlay_validation_is_atomic();
    std::cout << "ROI color/style/setter/mask/alpha/history checks passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
