#pragma once
#include <imagejxx/rgb_dataset_command.hpp>
#include <imagejxx/axis_remap.hpp>
namespace imagejxx::detail {
RgbDatasetRequest rgb_dataset_request_from_record(const MetadataObject &);
DocumentSnapshot rgb_dataset_metadata_preview(const ResolvedRgbDatasetCommand &);
PreparedAxisRemap prepare_rgb_dataset_edit(const ResolvedRgbDatasetCommand &,
                                          const ExecutionControl &, std::size_t);
}
