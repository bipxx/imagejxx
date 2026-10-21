#pragma once
#include "native_workflow_internal.hpp"
#include "session_metadata.hpp"
#include <imagejxx/native_workflow_program.hpp>
namespace imagejxx::detail {
std::size_t
native_workflow_program_retained_bytes(const NativeWorkflowProgram &,
                                       std::size_t);
MetadataObject
native_workflow_program_record_bounded(const NativeWorkflowProgram &,
                                       const NativeWorkflowProgramLimits &);
void native_workflow_record_budget(const MetadataObject &, std::size_t);
void native_workflow_json_budget(const MetadataObject &, std::size_t);
std::size_t
native_workflow_intent_bytes(const std::vector<NativeWorkflowStepRef> &,
                             std::size_t max_steps, std::size_t cap);
// Source-independent retained intent admission; no export resolution or
// datasets.
std::size_t
validate_disabled_native_workflow_step(const NativeWorkflowStepRef &,
                                       std::size_t);
session_metadata::Json
bounded_native_workflow_json(const std::string &,
                             const NativeWorkflowProgramLimits &);
NativeWorkflowProgram make_native_workflow_program_borrowed(
    const std::vector<NativeWorkflowStepRef> &,
    const NativeWorkflowProgramLimits &);
} // namespace imagejxx::detail
