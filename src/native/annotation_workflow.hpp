#pragma once
#include <imagejxx/native_workflow.hpp>
namespace imagejxx::detail {
bool annotation_workflow_operation(const std::string &);
std::size_t annotation_workflow_request_bytes(DatasetShape,const PlanarAnnotationRequest &,std::size_t);
std::size_t annotation_workflow_request_bytes(DatasetShape,const SavedRoiGeometryRequest &,std::size_t);
std::size_t annotation_workflow_intent_bytes(const NativeWorkflowAnnotationIntent &,std::size_t);
MetadataObject annotation_workflow_intent_value(const NativeWorkflowAnnotationIntent &,std::size_t);
NativeWorkflowAnnotationIntent annotation_workflow_intent_from_value(const MetadataObject &,std::size_t);
NativeWorkflowAnnotationIntent annotation_workflow_request(const NativeWorkflowOperation &,std::size_t);
void validate_annotation_workflow_original(const std::string &,const MetadataObject &,std::size_t);
void validate_annotation_workflow_intent(const NativeWorkflowOperation &,std::size_t);
// Fresh, metadata-only stage transaction. Original captures stay historical;
// the typed complete request explicitly binds this stage and extent.
DocumentTransaction prepare_annotation_workflow(const DocumentSnapshot &,
    const NativeWorkflowOperation &,std::size_t,const ExecutionControl & = {},
    std::size_t *working_bytes=nullptr);
}
