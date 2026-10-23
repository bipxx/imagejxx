#pragma once
#include <imagejxx/native_workflow.hpp>

namespace imagejxx::detail {
// Closed, source-independent choices; bounds admission precedes owned codecs.
std::size_t selection_creation_request_bytes(const SelectionCreationRequest &,
                                             std::size_t cap);
MetadataObject selection_creation_request_value(const SelectionCreationRequest &,
                                                std::size_t cap);
SelectionCreationRequest selection_creation_request_from_value(const MetadataObject &,
                                                               std::size_t cap);
SelectionCreationRequest selection_creation_record_request(const MetadataObject &,
                                                           std::size_t cap);
void validate_selection_creation_workflow_intent(const NativeWorkflowOperation &,
                                                 std::size_t cap);
SelectionCreationRequest
selection_creation_workflow_request(const NativeWorkflowOperation &, std::size_t cap);
ResolvedSelectionCreationCommand
workflow_selection_creation(const DocumentSnapshot &, const NativeWorkflowOperation &,
                            std::size_t cap, const ExecutionControl & = {});
} // namespace imagejxx::detail
