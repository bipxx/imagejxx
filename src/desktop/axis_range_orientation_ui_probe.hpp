#pragma once
// Explicit creator-thread observation only; absent in ordinary product use.
#include <imagejxx/document.hpp>
#include <imagejxx/execution.hpp>
#include <functional>
#include <memory>
namespace imagejxx::desktop {
class Window;
using AxisRangeOrientationUiPublicationProbe = std::function<void(
    const char *phase, const DocumentSnapshot &captured_source,
    const AxisRangeOrientationRequest &resolved_request,
    const std::shared_ptr<imagejxx::Document> &candidate)>;
class AxisRangeOrientationUiProbeAccess {
public:
  static void setPublicationProbe(Window &, AxisRangeOrientationUiPublicationProbe);
};
} // namespace imagejxx::desktop
