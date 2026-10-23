#pragma once
#include "planar_annotation_panel.hpp"
#include <imagejxx/imagej_annotation_import.hpp>
#include <imagejxx/imagej_annotation_export.hpp>
#include <imagejxx/imagej_roi_import.hpp>
#include <imagejxx/imagej_roi_pairing.hpp>
#include <imagejxx/imagej_roi_zip_export.hpp>
namespace imagejxx::desktop {
// Construct on the preparation worker. After transfer, draw/cache mutation is
// confined to the creator UI thread, using the same RoiVisual as live layers.
class RoiImportPreview {
public:
  RoiImportPreview(const ResolvedImageJRoiImport &, std::size_t cap);
  RoiImportPreview(const ResolvedImageJAnnotationImport &, std::size_t cap);
  RoiImportPreview(const ResolvedImageJRoiPairing &, std::size_t cap);
  RoiImportPreview(const ResolvedImageJRoiZipExport &, std::size_t cap);
  RoiImportPreview(const ResolvedImageJAnnotationExport &, std::size_t cap,
                   bool standard);
  bool matches(const DocumentSnapshot &, std::size_t cap) const;
  bool replaces() const { return replace_; }
  bool replaces_display() const {
    return (replace_ || export_) && bool(display_preview_);
  }
  std::optional<DocumentSnapshot>
  display_preview(const DocumentSnapshot &) const;
  std::size_t cap() const { return cap_; }
  Json draw(const DocumentSnapshot &, const PlanarCanvas &, ImDrawList *,
            bool diagnostics) const;

private:
  struct Layer {
    PlanarAnnotation row;
    mutable RoiVisual visual;
  };
  DocumentSnapshot source_;
  bool replace_;
  std::size_t cap_;
  std::vector<Layer> layers_;
  bool export_ = false;
  std::optional<DocumentSnapshot> display_preview_;
};
} // namespace imagejxx::desktop
