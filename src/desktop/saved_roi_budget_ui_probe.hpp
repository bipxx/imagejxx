#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
class Window;
// Bounded shared-ImGui input observer, not an OS/GPU presentation verdict.
Json savedRoiBudgetUiCase(Window &,const std::string &caseId,
                         const std::string &context,const std::string &freshDirectory);
}
