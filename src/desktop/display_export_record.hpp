#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <imagejxx/document.hpp>
#include "display_export_io.hpp"
#include "point_raster_export.hpp"

namespace imagejxx::desktop {
struct DisplayExportRecord {
  std::string action,path;
  std::uint64_t source_id=0;
  Revisions source_revisions;
  DatasetShape shape;
  PixelType pixel_type=PixelType::UInt8;
  DisplayState display;
  bool point_show_all=false,show_rois=false;
  DisplayExportFileOptions file;
  DisplayExportFontPolicy font;
};
bool isDisplayExportRecord(const Json&);
// Bounded allocation-free structural/string preflight, before decoding/copying.
std::size_t displayExportRecordWorkingBytes(const Json&);
std::size_t displayExportRecordWorkingBytes(const DocumentSnapshot&,
    const std::string& path,const DisplayExportFontPolicy&);
DisplayExportRecord decodeDisplayExportRecord(const Json&);
Json captureDisplayExportRecord(const DocumentSnapshot&,const std::string& action,
    const std::string& path,bool point_show_all,bool show_rois,
    const DisplayExportFileOptions&,const DisplayExportFontPolicy&);
void validateDisplayExportReplaySource(const DisplayExportRecord&,
    const DocumentSnapshot&,bool rebind);
} // namespace imagejxx::desktop
