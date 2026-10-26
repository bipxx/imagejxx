#pragma once
#include "axis_command_record.hpp"

// Reuse the existing checked definitions without introducing more unqualified
// names into every historical axis-record consumer's using-directive scope.
namespace imagejxx::detail::axis_command_record {
DatasetShape read_shape(const MetadataValue &);
MetadataObject calibration_record(const SpatialCalibration &);
SpatialCalibration read_calibration(const MetadataObject &);
const char *pixel_type_name(PixelType);
PixelType read_pixel_type(const MetadataValue &);
}
