#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
class Window;
Json scientificOperationTargetsUiCase(Window &window, const std::string &caseId,
                                     const std::string &context,
                                     const std::string &directory);
int scientificOperationTargetsUiConsumerTest(Window &window,
                                            const std::string &directory);
} // namespace imagejxx::desktop
