#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
class Window;
Json channelDisplayUiCase(Window &window, const std::string &caseId,
                          const std::string &context, const std::string &directory);
int channelDisplayUiConsumerTest(Window &window, const std::string &directory);
} // namespace imagejxx::desktop
