#pragma once
#include "native_workflow_internal.hpp"
#include <imagejxx/native_workflow_batch.hpp>
namespace imagejxx::detail {
PreparedNativeWorkflowBatch prepare_native_workflow_batch_borrowed(
    const std::vector<NativeWorkflowBatchInput> &,
    const std::vector<NativeWorkflowStepRef> &, const std::string &,
    const NativeWorkflowBatchOptions &, const ExecutionControl &,
    const NativeWorkflowBatchProgress &);
}
