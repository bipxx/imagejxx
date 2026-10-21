#pragma once
#include "desktop.hpp"
#include <imagejxx/imagej_annotation_import.hpp>
namespace imagejxx::desktop {
class DisplayAnnotationPanel;
class RoiDisplayReplacementEditor {
public:
  RoiDisplayReplacementEditor(std::size_t, DatasetShape,
                              PlanarDisplayAnnotation,
                              std::optional<DocumentSnapshot> source = {});
  ~RoiDisplayReplacementEditor();
  RoiDisplayReplacementEditor(RoiDisplayReplacementEditor &&) noexcept;
  RoiDisplayReplacementEditor &
  operator=(RoiDisplayReplacementEditor &&) noexcept;
  int draw(DatasetShape, const std::vector<PlanarDisplayAnnotation> &,
           std::size_t cap = 256 * 1048576);
  ImageJDisplayRoiNativeReplacement replacement(DatasetShape) const;
  std::size_t index() const { return index_; }
  Json diagnostics() const;

private:
  void seed(const PlanarDisplayAnnotation &);
  void observe(const char *, bool = true);
  PlanarDisplayAnnotation value(DatasetShape) const;
  std::size_t index_;
  DatasetShape recorded_;
  PlanarDisplayAnnotation base_;
  std::optional<DocumentSnapshot> source_;
  std::unique_ptr<Document> payload_document_;
  std::unique_ptr<DisplayAnnotationPanel> payload_editor_;
  std::string name_, error_;
  bool open_ = true, focus_ = true, confirmed_ = false, all_t_ = true,
       all_c_ = true;
  int plane_ = 0, slice_ = 1, t_ = 1, c_ = 1;
  Json controls_ = Json::object();
  Json bounds_ = Json::array();
};
} // namespace imagejxx::desktop
