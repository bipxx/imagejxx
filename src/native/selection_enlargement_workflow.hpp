#pragma once
#include <imagejxx/native_workflow.hpp>
#include <imagejxx/selection_enlargement.hpp>

namespace imagejxx::detail {
// The capture is immutable provenance. Authored choices always apply to the
// current stage selection and X calibration, without moving its view.
SelectionEnlargementRequest selection_enlargement_workflow_request(
    const NativeWorkflowOperation &);
SelectionEnlargementCommandLimits selection_enlargement_workflow_limits(std::size_t);
ResolvedSelectionEnlargementCommand workflow_selection_enlargement(
    const DocumentSnapshot &, const NativeWorkflowOperation &,
    const SelectionEnlargementCommandLimits &, const ExecutionControl & = {});
}
