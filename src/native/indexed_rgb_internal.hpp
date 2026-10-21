#pragma once
#include <imagejxx/indexed_rgb_command.hpp>
#include <imagejxx/axis_remap.hpp>
namespace imagejxx::detail {
IndexedRgbRequest indexed_rgb_workflow_request(const OperationParameters &,const MetadataObject &);
OperationParameters indexed_rgb_workflow_parameters(const IndexedRgbRequest &);
DocumentSnapshot indexed_rgb_metadata_preview(const ResolvedIndexedRgbCommand &);
PreparedAxisRemap prepare_indexed_rgb_edit(const ResolvedIndexedRgbCommand &,const ExecutionControl &,std::size_t);
}
