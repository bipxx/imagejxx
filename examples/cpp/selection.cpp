#include <imagejxx/roi_style.hpp>
#include <imagejxx/selection_edit.hpp>
#include <iostream>
int main() {
  using namespace imagejxx;
  Document document(Dataset::from_stack({Image(32, 24)}));
  document.select(Selection("rectangle", {{3, 4}, {13, 14}}));
  auto source = document.snapshot();
  SelectionEditor geometry(*source.data().selection, 32, 24);
  auto edit = source.begin_edit(Change::Pixels | Change::Selection);
  edit.select(geometry.resized(4, {18, 19}, true));
  document.commit(edit);
  if (document.snapshot().dirty() || document.undo_steps() != 0 ||
      document.snapshot().data().selection->points[1] !=
          std::array<double, 2>{18, 19})
    return 1;
  SelectionEditor resized(*document.snapshot().data().selection, 32, 24);
  auto rotated = resized.box_geometry({16, 12}, {12, 8}, 90);
  if (rotated.kind != "rotated_rectangle" || !rotated.box_frame() ||
      rotated.box_frame()->u != std::array<double, 2>{0, 6} ||
      rotated.box_frame()->v != std::array<double, 2>{-4, 0})
    return 2;
  SelectionEditor rotation(rotated, 32, 24);
  auto styled = rotation.properties(4, {{"label", std::string("Rotated")}});
  if (styled.width != 4 ||
      std::get<std::string>(styled.attributes.at("label")) != "Rotated" ||
      !rotation.rotation_handle(5))
    return 3;
  auto tx = document.begin_edit(Change::Selection);
  tx.select(styled);
  document.commit(tx);
  if (document.snapshot().dirty() || document.undo_steps() != 0)
    return 4;
  const auto roiStyled =
      rotation.roi_style({{"stroke_color", std::string("green")},
                          {"fill_color", std::string("#80123456")},
                          {"stroke_width", 0.},
                          {"vendor", metadata_object({{"retained", true}})}});
  const auto roiStyle = roi_style(roiStyled);
  const auto resolved = resolve_roi_style(roiStyled);
  if (!roiStyle.present || !roiStyle.has_stroke || !roiStyle.has_fill ||
      roiStyle.stroke_color != RoiColor{0, 255, 0, 255} ||
      roiStyle.fill_color != RoiColor{18, 52, 86, 128} || !resolved.cosmetic ||
      resolved.stroke_width != 0 || roiStyled.width != rotated.width ||
      roiStyled.mask(32, 24).pixels != rotated.mask(32, 24).pixels ||
      rotation.selection() != rotated)
    return 5;
  const auto filled =
      with_roi_style(roiStyled, {{"fill_color", std::string("#80123456")}});
  if (roi_style(filled).stroke_color ||
      resolve_roi_style(filled).stroke_color ||
      std::get<std::string>(styled.attributes.at("label")) != "Rotated")
    return 6;
  const auto display =
      render_roi_style(Image(32, 24, 3, PixelType::UInt8), filled);
  if (display.at(16, 12, 0) != 9 || display.at(16, 12, 1) != 26 ||
      display.at(16, 12, 2) != 43 || display.at(0, 0, 0) != 0)
    return 7;
  Selection line("line", {{3, 4}, {13, 4}}, 3);
  const auto cosmetic = with_roi_style(line, {{"stroke_width", 0.}});
  if (selection_processing_width(cosmetic) != 1 || cosmetic.width != 3 ||
      cosmetic.mask(32, 24).pixels !=
          Selection("line", line.points, 1).mask(32, 24).pixels)
    return 8;
  std::cout
      << "Installed selection geometry/rotation/properties/ROI styles passed\n";
}
