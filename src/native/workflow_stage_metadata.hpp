#pragma once
#include <imagejxx/document.hpp>
#include <imagejxx/operations.hpp>
namespace imagejxx {class ResolvedTarget;}
namespace imagejxx::detail {
// A descriptor for preflight only. Any accidental sample access fails.
Dataset workflow_metadata_dataset(DatasetShape, PixelType,
                                  const SpatialCalibration &,bool pending_indexed_palette=false);
bool workflow_pending_indexed_palette(const Dataset &);
DisplayState pair_numeric_conversion_metadata(const DocumentSnapshot &,
                                              DatasetShape,
                                              DocumentTransaction &,PixelType);
DocumentSnapshot scalar_conversion_metadata_preview(const DocumentSnapshot &,
                                                     PixelType);
// Geometry-preserving registry steps can still change dtype. Parameters are
// normalized; masks come only from captured selection metadata, never samples.
DocumentSnapshot workflow_filter_metadata_preview(const DocumentSnapshot &,
    const std::string &,const OperationParameters &,const ResolvedTarget &);
}
