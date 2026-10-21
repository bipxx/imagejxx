#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
class Window;
Json scientificSlicingUiCase(Window &window, const std::string &caseId,
                             const std::string &context,
                             const std::string &directory);
int scientificSlicingUiConsumerTest(Window &window, const std::string &directory);
} // namespace imagejxx::desktop
