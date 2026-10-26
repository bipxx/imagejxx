#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
class Window;
// Fixed actual-input observer. The caller constructs the host with the paired
// preferences path before invocation; no setting or clock implementation here.
Json settingsPlaybackUiCase(Window &, const std::string &caseId,
                            const std::string &context,
                            const std::string &freshDirectory);
}
