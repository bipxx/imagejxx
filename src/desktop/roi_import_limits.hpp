#pragma once
#include "desktop.hpp"
#include <imagejxx/imagej_annotation_import.hpp>
namespace imagejxx::desktop {
struct RoiImportLimitValues {
  ImageJAnnotationImportLimits annotations;
  ImageJRoiZipLimits zip;
};
class RoiImportLimitEditor {
public:
  explicit RoiImportLimitEditor(const RoiImportLimitValues &);
  // 0 pending, 1 accepted, -1 canceled. Accepted output is fully validated.
  int draw(RoiImportLimitValues &);
  Json diagnostics() const;

private:
  std::array<std::string, 17> fields_;
  Json controls_ = Json::object(), bounds_ = Json::array();
  std::string error_;
  bool focus_ = true;
  void load(const RoiImportLimitValues &);
};
} // namespace imagejxx::desktop
