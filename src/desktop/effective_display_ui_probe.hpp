#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
class Window;
Json effectiveDisplayUiCase(Window&, const std::string& context,
                            const std::string& fresh_directory);
Json capturedMeasurementUiCase(Window&, const std::string& context,
                              const std::string& fresh_directory);
Json capturedProfileUiCase(Window&, const std::string& context,const std::string& fresh_directory);
Json capturedRoiMeasurementUiCase(Window&,const std::string& context,const std::string& fresh_directory);
Json capturedPathMeasurementUiCase(Window&,const std::string& context,const std::string& fresh_directory);
Json pointAnnotationEditingUiCase(Window&,const std::string& context,const std::string& fresh_directory);
Json convexHullWorkflowUiCase(Window&,const std::string& context,const std::string& fresh_directory);
Json boundingRectangleWorkflowUiCase(Window&,const std::string& context,const std::string& fresh_directory);
Json fitCircleWorkflowUiCase(Window&,const std::string& context,const std::string& fresh_directory,bool area);
}
