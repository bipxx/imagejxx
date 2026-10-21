#pragma once
#include <imagejxx/color_stack_command.hpp>
#include <imagejxx/axis_remap.hpp>
namespace imagejxx::detail {
// Empty parameters retain captured intent/pose. A complete seven-field map
// authors new choices on the current stage; original stays historical.
ColorStackRequest color_stack_workflow_request(const OperationParameters &,const MetadataObject &);
OperationParameters color_stack_workflow_parameters(const ColorStackRequest &);
std::optional<RgbDatasetRequest> color_stack_rendering_options(const MetadataObject &);
DocumentSnapshot color_stack_metadata_preview(const ResolvedColorStackCommand &);
PreparedAxisRemap prepare_color_stack_edit(const ResolvedColorStackCommand &,const ExecutionControl &,std::size_t);
}
