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
                 const NativeWorkflowExport *>;
// Metadata-only admission of source, targets, parameters and retained/output
// budgets. Acquires no source samples; execution performs this admission again.
std::size_t admit_native_workflow(
    const DocumentSnapshot &, const std::vector<NativeWorkflowStepRef> &,
    const NativeWorkflowLimits &, const ExecutionControl & = {});
// Synchronous only. Caller retains immutable requests until this returns.
PreparedNativeWorkflow prepare_native_workflow_borrowed(
    const DocumentSnapshot &, const std::vector<NativeWorkflowStepRef> &,
    const NativeWorkflowLimits &, const ExecutionControl &);
} // namespace imagejxx::detail
