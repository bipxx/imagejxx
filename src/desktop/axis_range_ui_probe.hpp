#pragma once
// Private creator-thread observation seam, absent during ordinary UI use.
// Observers never supply expected output or processing/calibration behavior;
// exceptions cannot escape publication or attachment. Actual consumers remain
// separately declared root-owned work.
#include <imagejxx/document.hpp>
#include <imagejxx/execution.hpp>
#include <functional>
#include <memory>

namespace imagejxx::desktop {
class Window;
using AxisRangeUiPublicationProbe = std::function<void(
    const char *phase, const DocumentSnapshot &captured_source,
    const AxisRangeResliceRequest &resolved_request,
    const std::shared_ptr<imagejxx::Document> &candidate)>;
class AxisRangeUiProbeAccess {
public:
  static void setPublicationProbe(Window &window,
                                  AxisRangeUiPublicationProbe probe);
};
} // namespace imagejxx::desktop
