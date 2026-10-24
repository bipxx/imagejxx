#pragma once
// Core-private shared metadata codec. This header is not an installed SDK API.
#include <imagejxx/document.hpp>
#include <nlohmann/json.hpp>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace imagejxx::detail::session_metadata {
using Json = nlohmann::json;

bool calibration_field(const std::string &key);
bool document_field(const std::string &key);
Json initial_metadata(std::size_t plane_count, const std::string &title);

MetadataValue from_json(const Json &value);
MetadataObject object_from_json(const Json &value);
Json to_json(const MetadataValue &value);
Json to_json(const MetadataObject &value);

// Callers retain their existing geometry/image validation. Both prior pointers
// must be supplied together for source-aware edit inference; neither for load.
DocumentMetadata infer_metadata(const Json &metadata,
                               const DocumentMetadata *prior = nullptr,
                               const Json *prior_serialized = nullptr);
MetadataObject display_properties(const Json &metadata);

// Scalar codec records only: no dataset, image, document owner or pixel storage.
// Selection packed bits are owned annotation metadata; they are never rasterized.
struct ScalarRecord {
  DocumentMetadata metadata;
  SpatialCalibration calibration;
  int plane_count = 1;
  std::optional<Selection> selection, previous_selection;
  std::vector<Roi> rois;
  std::vector<MetadataObject> overlays;
};
std::string encode_base64(const std::vector<unsigned char> &data);
std::vector<unsigned char> decode_base64(const std::string &data);
Selection selection_from_json(const Json &value);
Json selection_to_json(const Selection &value);
SpatialCalibration calibration_from_json(const Json &value);
// Explicit native-path bridge for desktop State projections; legacy callers stay closed.
Json wire_metadata(const ScalarRecord &data, bool allow_paths = false);
Json view_json(const DisplayState &value);
DisplayState parse_view(const Json &parameters, DisplayState view,
                        int width, int height, int plane_count, int slice_index);

// Strict private inverse used for marked-record validation and scalar-only
// canonicalization. metadata_json is exactly {title,properties,extensions,
// record_layout}; scalar_wire_json excludes all seven pixel/spectrum transport
// keys. Geometry is validated against descriptors without allocating a raster.
std::string session_wire_metadata_json(const std::string &metadata_json,
                                      const std::string &scalar_wire_json,
                                      int width, int height, int plane_count, bool allow_paths = false);

// Strict scalar-only first-load inference. Positive int32 plane count is the
// only image descriptor needed. Pixel/spectrum transport fields are forbidden.
// Returns {metadata:{title,properties,extensions,record_layout},
//          display_properties:{...}}. No geometry/view/session validity claim.
std::string legacy_session_state_json(const std::string &metadata_json,
                                      int plane_count);
} // namespace imagejxx::detail::session_metadata
