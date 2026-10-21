#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
class Window;
Json axisRangeSlicerResizeUiCase(Window &window,const std::string &caseId,
                                const std::string &context,const std::string &directory);
int axisRangeSlicerResizeUiConsumerTest(Window &window,const std::string &directory);
}
