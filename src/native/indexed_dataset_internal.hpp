#pragma once
#include <imagejxx/indexed_dataset_command.hpp>
#include <imagejxx/axis_remap.hpp>
namespace imagejxx::detail {
IndexedDatasetRequest indexed_dataset_workflow_request(const OperationParameters &,const MetadataObject &);
OperationParameters indexed_dataset_workflow_parameters(const IndexedDatasetRequest &);
DocumentSnapshot indexed_dataset_metadata_preview(const ResolvedIndexedDatasetCommand &);
PreparedAxisRemap prepare_indexed_dataset_edit(const ResolvedIndexedDatasetCommand &,const ExecutionControl &,std::size_t);
}
