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
Json fitEllipseWorkflowUiCase(Window&,const std::string& context,const std::string& fresh_directory,bool area);
Json fitRectangleWorkflowUiCase(Window&,const std::string&,const std::string&,bool area);
Json splineWorkflowUiCase(Window&,const std::string&,const std::string&,bool opened,bool straighten);
Json interpolationWorkflowUiCase(Window&,const std::string&,const std::string&,bool opened,bool configured);
Json interpolationComponentsUiCase(Window&,const std::string&,const std::string&,int profile,bool configured);
Json interpolationPointNoopUiCase(Window&,const std::string&,const std::string&,bool multiple);
Json interpolationRepeatUiCase(Window&,const std::string&,const std::string&,bool opened,bool high_zoom);
}
