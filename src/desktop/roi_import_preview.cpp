#include "roi_import_preview.hpp"
#include "../native/kernel_control.hpp"
#include "display_annotation_renderer.hpp"
#include "planar_annotation_visual.hpp"
#include <imgui.h>
#include <set>
namespace imagejxx::desktop {
RoiImportPreview::RoiImportPreview(const ResolvedImageJAnnotationExport &command,
                                   std::size_t cap, bool standard)
    : source_(command.source()), replace_(false), cap_(cap), export_(true) {
  std::vector<PlanarAnnotation> geometry;
  std::vector<PlanarDisplayAnnotation> display;
  std::set<std::pair<ImageJAnnotationKind,std::size_t>> seen;
  for(const auto &member:command.members()) {
    kernel_checkpoint();
    const auto &request=member.request;
    if(!seen.insert({request.annotation_kind,request.annotation_index}).second)continue;
    if(const auto g=std::get_if<MappedImageJRoi>(&member.mapped)) {
      auto row=g->annotation();
      if(standard) {
        ImageJRoiPlaneMapping mapping;mapping.extent=g->target_extent();
        mapping.plane=row.plane;mapping.coordinate=row.coordinate;
        mapping.z_position=ImageJZPositionMapping::FixedAxis;
        row=decode_imagej_roi_mapped(encode_imagej_roi(std::get<ImageJRoi>(request.standard_projection),command.limits().output.roi),mapping,command.limits().output.roi).annotation();
      }
      geometry.push_back(std::move(row));
    } else {
      const auto &mapped=std::get<MappedImageJDisplayRoi>(member.mapped);
      auto row=mapped.annotation();
      if(standard) {
        ImageJDisplayRoiMapping mapping;
        mapping.plane.extent=mapped.target_extent();mapping.plane.plane=row.plane;
        mapping.plane.coordinate=row.coordinate;mapping.plane.z_position=ImageJZPositionMapping::FixedAxis;
        std::vector<unsigned char> bytes;
        if(const auto text=std::get_if<ImageJTextRoi>(&request.standard_projection)) {
          bytes=encode_imagej_text_roi(*text,command.limits().output.roi);
          mapping.text_default_color=request.options.text_default_color;
        } else bytes=encode_imagej_image_roi(std::get<ImageJImageRoi>(request.standard_projection),command.limits().output.roi);
        row=decode_imagej_display_roi_mapped(bytes,mapping,{command.limits().output.roi,command.limits().max_display_retained_bytes}).annotation();
      }
      display.push_back(std::move(row));
    }
  }
  admit_planar_annotation_visuals(source_.data().dataset.shape(),geometry,cap);
  admit_display_annotation_visuals(display,cap);
  for(auto &row:geometry) {
    kernel_checkpoint();const auto shape=planar_annotation_geometry(source_.data().dataset.shape(),row.plane);
    auto visual=roiVisual(selectionJson(row.selection),shape.width,shape.height,{255,213,75,255});
    layers_.push_back({std::move(row),std::move(visual)});
  }
  auto edit=source_.begin_edit(Change::Pixels|Change::Metadata);
  edit.set_extension(planar_display_annotations_extension,encode_planar_display_annotations(source_.data().dataset.shape(),display));
  display_preview_=edit.preview();
}
RoiImportPreview::RoiImportPreview(
    const ResolvedImageJAnnotationImport &command, std::size_t cap)
    : source_(command.source()),
      replace_(std::get<std::string>(command.record().at("mode")) == "replace"),
      cap_(cap) {
  admit_planar_annotation_visuals(source_.data().dataset.shape(),
                                  command.annotations(), cap);
  admit_display_annotation_visuals(command.display_annotations(), cap);
  std::vector<PlanarDisplayAnnotation> display;
  for (const auto &record : command.records()) {
    kernel_checkpoint();
    if (auto g = std::get_if<MappedImageJRoi>(&record)) {
      const auto &row = g->annotation();
      const auto geometry =
          planar_annotation_geometry(source_.data().dataset.shape(), row.plane);
      layers_.push_back(
          {row, roiVisual(selectionJson(row.selection), geometry.width,
                          geometry.height, {64, 210, 240, 255})});
    } else
      display.push_back(std::get<MappedImageJDisplayRoi>(record).annotation());
  }
  auto edit = source_.begin_edit(Change::Pixels | Change::Metadata);
  edit.set_extension(planar_display_annotations_extension,
                     encode_planar_display_annotations(
                         source_.data().dataset.shape(), display));
  display_preview_ = edit.preview();
}
std::optional<DocumentSnapshot>
RoiImportPreview::display_preview(const DocumentSnapshot &current) const {
  if (!display_preview_ || !matches(current, cap_))
    return {};
  return display_preview_->with_display(current.display());
}
RoiImportPreview::RoiImportPreview(const ResolvedImageJRoiImport &command,
                                   std::size_t cap)
    : source_(command.source()),
      replace_(std::get<std::string>(command.record().at("mode")) == "replace"),
      cap_(cap) {
  admit_planar_annotation_visuals(source_.data().dataset.shape(),
                                  command.annotations(), cap);
  layers_.reserve(command.records().size());
  execution_stage_progress("Prepare ROI visuals", 0, command.records().size());
  for (const auto &record : command.records()) {
    kernel_checkpoint();
    const auto &row = record.annotation();
    const auto geometry =
        planar_annotation_geometry(source_.data().dataset.shape(), row.plane);
    auto visual = roiVisual(selectionJson(row.selection), geometry.width,
                            geometry.height, {64, 210, 240, 255});
    layers_.push_back({row, std::move(visual)});
    execution_stage_progress("Prepare ROI visuals", layers_.size(),
                             command.records().size());
    kernel_checkpoint();
  }
}
bool RoiImportPreview::matches(const DocumentSnapshot &current,
                               std::size_t cap) const {
  return cap == cap_ && source_.same_document(current) &&
         current.revisions().matches(source_.revisions(),
                                     Change::Pixels | Change::Metadata);
}
RoiImportPreview::RoiImportPreview(const ResolvedImageJRoiPairing &pair,
                                   std::size_t cap)
    : source_(pair.source()), replace_(false), cap_(cap), export_(true) {
  const auto &row = pair.mapped().annotation();
  admit_planar_annotation_visuals(source_.data().dataset.shape(), {row}, cap);
  kernel_checkpoint();
  const auto geometry =
      planar_annotation_geometry(source_.data().dataset.shape(), row.plane);
  layers_.push_back(
      {row, roiVisual(selectionJson(row.selection), geometry.width,
                      geometry.height, {255, 213, 75, 255})});
  kernel_checkpoint();
}
RoiImportPreview::RoiImportPreview(const ResolvedImageJRoiZipExport &batch,
                                   std::size_t cap)
    : source_(batch.source()), replace_(false), cap_(cap), export_(true) {
  std::vector<PlanarAnnotation> rows;
  for (const auto &member : batch.members())
    rows.push_back(member.mapped.annotation());
  admit_planar_annotation_visuals(source_.data().dataset.shape(), rows, cap);
  for (auto &row : rows) {
    kernel_checkpoint();
    const auto geometry =
        planar_annotation_geometry(source_.data().dataset.shape(), row.plane);
    auto visual = roiVisual(selectionJson(row.selection), geometry.width,
                            geometry.height, {255, 213, 75, 255});
    layers_.push_back({std::move(row), std::move(visual)});
  }
}
Json RoiImportPreview::draw(const DocumentSnapshot &current,
                            const PlanarCanvas &canvas, ImDrawList *draw,
                            bool diagnostics) const {
  Json result = Json::array();
  if (!matches(current, cap_))
    return result;
  struct Clip {
    ImDrawList *draw;
    ~Clip() { draw->PopClipRect(); }
  };
  draw->PushClipRect({canvas.origin[0], canvas.origin[1]},
                     {canvas.origin[0] + canvas.width * canvas.scale[0],
                      canvas.origin[1] + canvas.height * canvas.scale[1]},
                     true);
  Clip clip{draw};
  bool visible = false;
  for (std::size_t i = 0; i < layers_.size(); ++i) {
    const auto &layer = layers_[i];
    const auto &row = layer.row;
    if (!planar_annotation_visible(row, canvas.plane, canvas.coordinate,
                                   current.display().t, current.display().c))
      continue;
    const auto draw_begin = draw->IdxBuffer.Size;
    drawRoiVisual(layer.visual, draw, canvas.origin, canvas.scale);
    visible = true;
    if (diagnostics)
      result.push_back(
          {{"row", i},
           {"draw_index_begin", draw_begin},
           {"draw_index_end", draw->IdxBuffer.Size},
           {"plane", std::array<const char *, 3>{"XY", "XZ", "YZ"}.at(
                         std::size_t(row.plane))},
           {"coordinate", canvas.coordinate},
           {"canvas_id", canvas.id},
           {"preview", true},
           {"geometry", selectionJson(row.selection)},
           {"screen_builds", layer.visual.screenBuilds}});
  }
  if (visible)
    draw->AddText({canvas.origin[0] + 4, canvas.origin[1] + 4},
                  IM_COL32(255, 213, 75, 255),
                  export_ ? "ROI export preview" : "ROI import preview");
  return result;
}
} // namespace imagejxx::desktop
