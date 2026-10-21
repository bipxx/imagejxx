#pragma once
#include <imagejxx/channel_display.hpp>
#include <imagejxx/rgb_conversion.hpp>
#include <limits>

namespace imagejxx {
enum class RgbDatasetMode { PerPlane, Composite };
struct RgbDatasetRequest {
  RgbDatasetMode mode = RgbDatasetMode::PerPlane;
  OutputDestination destination = OutputDestination::NewDocument;
  std::optional<ChannelDisplayState> channels;
  std::optional<std::array<double, 2>> display_range;
  // PerPlane: scalar processor palette. Composite: replace every contributing
  // channel palette; ranges/visibility/projection remain captured, or default
  // ranges are computed on the preparation worker at the captured T/Z.
  // A palette override colorizes Grayscale and single-channel Composite via
  // Color mode. Without an override existing channel-renderer rules apply.
  std::optional<RgbLookupTable> lut;
  std::size_t max_working_bytes = 512ULL * 1024 * 1024;
};
struct RgbDatasetLayout {
  DatasetShape shape;
  std::size_t output_bytes, working_bytes;
};
// Metadata-only descriptors. PerPlane preserves TCZ and converts S to RGB;
// Composite collapses C to one using the native channel renderer's mode,
// visibility, range, LUT and projection. Neither mode flattens C into spatial Z.
RgbDatasetLayout describe_rgb_dataset(DatasetShape, PixelType,
    FloatingSamplePolicy, const RgbDatasetRequest &);
class ResolvedRgbDatasetCommand {
public:
  const DocumentSnapshot &source() const { return source_; }
  const RgbDatasetRequest &request() const { return request_; }
  const RgbDatasetLayout &layout() const { return layout_; }
  const MetadataObject &record() const { return record_; }
private:
  friend ResolvedRgbDatasetCommand resolve_rgb_dataset_command(
      const DocumentSnapshot &, const RgbDatasetRequest &);
  ResolvedRgbDatasetCommand(DocumentSnapshot, RgbDatasetRequest,
                           RgbDatasetLayout, MetadataObject);
  DocumentSnapshot source_;
  RgbDatasetRequest request_;
  RgbDatasetLayout layout_;
  MetadataObject record_;
};
ResolvedRgbDatasetCommand resolve_rgb_dataset_command(
    const DocumentSnapshot &, const RgbDatasetRequest & = {});
void validate_rgb_dataset_command_record(const MetadataObject &);
ResolvedRgbDatasetCommand rgb_dataset_command_from_record(
    const DocumentSnapshot &, const MetadataObject &);
ResolvedRgbDatasetCommand rebind_rgb_dataset_command(
    const DocumentSnapshot &, const MetadataObject &, const RgbDatasetRequest &);
// Source is retained; preparation never publishes. In-place conversion pairs
// numeric display reset/metadata remapping with pixels and guards all revisions.
// NewDocument is an isolated RGB result with empty history. All C-positioned
// annotations map to output C0 in Composite, irrespective of pixel visibility;
// originals of changed channel/plane label fields are retained as provenance.
PreparedOperation prepare_rgb_dataset(const ResolvedRgbDatasetCommand &,
    const ExecutionControl & = {},
    std::size_t execution_max_working_bytes = std::numeric_limits<std::size_t>::max());
} // namespace imagejxx
