#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
class Window;
Json selectionCreationUiCase(Window &window, const std::string &caseId,
                            const std::string &context, const std::string &directory);
int selectionCreationUiConsumerTest(Window &window, const std::string &directory);
} // namespace imagejxx::desktop

