#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
class Window;
Json roiImportLimitsUiCase(Window &,const std::string &,const std::string &);
Json roiMixedExportUiCase(Window &,const std::string &,const std::string &);
Json nativeWorkflowBatchUiCase(Window &,const std::string &,const std::string &);
Json nativeWorkflowUiCase(Window &,const std::string &,const std::string &);
Json scientificProcessUiCase(Window &,const std::string &,const std::string &);
Json roiBareEditUiCase(Window &,const std::string &,const std::string &);
Json displayAnnotationUiCase(Window &,const std::string &context,const std::string &fresh_directory);
Json roiMixedImportUiCase(Window &,const std::string &context,const std::string &fresh_directory);
Json roiZipExportUiCase(Window &,const std::string &context,const std::string &fresh_directory);
Json roiExportUiCase(Window &,const std::string &context,const std::string &fresh_directory);
Json roiImportReplacementUiCase(Window &, const std::string &context,
                               const std::string &fresh_directory);
Json roiImportQueueUiCase(Window &, const std::string &context,
                          const std::string &fresh_directory);
Json roiImportBrowseUiCase(Window &, const std::string &context,
                           const std::string &fresh_directory);
Json roiImportJobsUiCase(Window &, const std::string &context,
                         const std::string &fresh_directory);
Json roiImportRebindUiCase(Window &, const std::string &context,
                           const std::string &fresh_directory);
Json roiImportUiCase(Window &, const std::string &context,
                     const std::string &fresh_directory);
} // namespace imagejxx::desktop
