#pragma once
// Private observation seam for the bounded, actual-input export consumer.
// The frontend calls these phases on its creator thread. A producer must catch
// every callback exception and defer work after adoption until outside publish.
#include <imagejxx/document.hpp>
#include <imagejxx/execution.hpp>
#include <functional>
#include <memory>

namespace imagejxx::desktop {
class Window;
using AxisPlaneUiPublicationProbe = std::function<void(
    const char *phase, const DocumentSnapshot &captured_source,
    const AxisPlaneExportRequest &resolved_request,
    const std::shared_ptr<imagejxx::Document> &candidate)>;
class AxisPlaneUiProbeAccess {
public:
  static void setPublicationProbe(Window &window,
                                  AxisPlaneUiPublicationProbe probe);
};
} // namespace imagejxx::desktop
