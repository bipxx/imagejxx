#pragma once

#include <imagejxx/execution.hpp>
#include <initializer_list>

// Shared private checked wire primitives for the independently closed V1
// extraction and V2 orientation grammars. This header is not installed.
namespace imagejxx::detail::axis_command_record {

inline constexpr const char *schema_name = "imagejxx.axis-extraction-command";

void exact_fields(const MetadataObject &object,
                  std::initializer_list<const char *> names);
const MetadataObject &object_value(const MetadataValue &value);
const MetadataArray &array_value(const MetadataValue &value);
double number_value(const MetadataValue &value);
const std::string &text_value(const MetadataValue &value);
std::uint64_t unsigned_value(const MetadataValue &value);
int index_value(const MetadataValue &value, bool positive = false);
std::size_t size_value(const MetadataValue &value);
const char *plane_name(Plane plane);
Plane read_plane(const MetadataValue &value);
bool same_calibration(const SpatialCalibration &a,
                      const SpatialCalibration &b);

struct SourceRecord {
  std::uint64_t document_id;
  DatasetShape shape;
  PixelType pixel_type;
  FloatingSamplePolicy sample_policy;
  Revisions revisions;
  SpatialCalibration calibration;
};

SourceRecord source_metadata(const DocumentSnapshot &source);
MetadataObject source_record(const SourceRecord &source);
SourceRecord read_source(const MetadataObject &record);
MetadataObject layout_record(const AxisExtractionLayout &layout);
void validate_layout(const MetadataObject &record,
                     const AxisExtractionLayout &expected);

} // namespace imagejxx::detail::axis_command_record
