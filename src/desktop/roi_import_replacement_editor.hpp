#pragma once
#include "desktop.hpp"
#include <imagejxx/imagej_roi_import.hpp>
#include <imgui.h>
namespace imagejxx::desktop {
class RoiImportReplacementEditor {
public:
  RoiImportReplacementEditor(std::size_t index, DatasetShape recorded,
                             PlanarAnnotation);
  // 0: editing, 1: stage complete replacement, 2: discard editor changes.
  int draw(DatasetShape target, const std::vector<PlanarAnnotation> &current);
  ImageJRoiNativeReplacement replacement(DatasetShape target) const;
  std::size_t index() const { return index_; }
  Json diagnostics() const;

private:
  void seed(const PlanarAnnotation &);
  void load_vertex();
  Selection vertex_geometry() const;
  void change_geometry(int, DatasetShape);
  void observe(const std::string &, bool enabled = true, ImGuiID id = 0);
  bool text(const char *, std::string &, const char *, float width = 110);
  bool combo(const char *, int &, const char *const *, int, const char *);
  std::size_t index_;
  DatasetShape recorded_;
  PlanarAnnotation base_;
  bool open_ = true, focus_ = true, confirmed_ = false, all_t_ = true,
       all_c_ = true;
  bool geometry_dirty_ = false, point_dirty_ = false, style_dirty_ = false;
  int plane_ = 0, slice_ = 1, t_ = 1, c_ = 1, vertex_ = 1;
  std::string name_, stroke_, fill_, stroke_width_, corner_diameter_, error_;
  std::array<std::string, 5> box_;
  std::array<std::string, 2> point_;
  Json controls_ = Json::object();
  Json bounds_=Json::array();
};
} // namespace imagejxx::desktop
