#pragma once
#include <imagejxx/native_workflow.hpp>
namespace imagejxx::detail {
const std::string &native_workflow_original_schema(const MetadataObject &);
bool native_workflow_bound_operation_original(const MetadataObject &);
void validate_native_workflow_operation_original(const std::string &,
                                                 const OperationParameters &,
                                                 const MetadataObject &);
void validate_native_workflow_operation_intent(const NativeWorkflowOperation &);
void validate_native_workflow_import_original(const MetadataObject &);
void validate_native_workflow_export_original(const MetadataObject &);
using NativeWorkflowStepRef =
    std::variant<const NativeWorkflowOperation *, const NativeWorkflowImport *,
                 const NativeWorkflowExport *, const NativeWorkflowHistogram *>;
void validate_native_workflow_histogram_intent(const NativeWorkflowHistogram &);
// Metadata-only admission of source, targets, parameters and retained/output
// budgets. Acquires no source samples; execution performs this admission again.
// Owning callers supply the captured-input floor; it shares the finite working
// allowance with the maximum serial target-mask scratch, never a second cap.
std::size_t admit_native_workflow(
    const DocumentSnapshot &, const std::vector<NativeWorkflowStepRef> &,
    const NativeWorkflowLimits &, const ExecutionControl & = {},
    std::size_t minimum_retained_bytes = 0);
// Synchronous only. Caller retains immutable requests until this returns.
PreparedNativeWorkflow prepare_native_workflow_borrowed(
    const DocumentSnapshot &, const std::vector<NativeWorkflowStepRef> &,
    const NativeWorkflowLimits &, const ExecutionControl &,
    std::size_t minimum_retained_bytes = 0);
} // namespace imagejxx::detail
