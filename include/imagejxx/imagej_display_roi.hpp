#pragma once
#include <imagejxx/imagej_roi_mapping.hpp>
#include <imagejxx/planar_display_annotations.hpp>
namespace imagejxx {
using ImageJDisplayRoiRecord = std::variant<ImageJTextRoi, ImageJImageRoi>;
struct ImageJDisplayRoiMapping {
  ImageJRoiPlaneMapping plane;
  // Required when a text wire record has unspecified stroke color0.
  std::optional<std::uint32_t> text_default_color;
};
struct ImageJDisplayRoiLimits {
  ImageJRoiLimits wire;
  std::size_t max_retained_bytes = 128 * 1024 * 1024;
};
struct ImageJDisplayProjectionOptions {
  bool round_font_size = false, drop_image_alpha = false,
       reset_image_scale = false, round_image_origin = false;
  // Explicit interpretation when the chosen text projection uses color0.
  std::optional<std::uint32_t> text_default_color;
};
namespace detail {
struct ImageJDisplayRoiAccess;
}
class MappedImageJDisplayRoi {
public:
  const std::vector<unsigned char> &source_bytes() const {
    return source_->bytes;
  }
  const ImageJDisplayRoiRecord &source_record() const {
    return source_->record;
  }
  const ImageJDisplayRoiMapping &source_mapping() const {
    return source_->mapping;
  }
  const DatasetShape &target_extent() const { return target_; }
  const PlanarDisplayAnnotation &annotation() const { return annotation_; }

private:
  friend struct detail::ImageJDisplayRoiAccess;
  struct Source {
    std::vector<unsigned char> bytes;
    ImageJDisplayRoiRecord record;
    ImageJDisplayRoiMapping mapping;
  };
  std::shared_ptr<const Source> source_;
  DatasetShape target_;
  PlanarDisplayAnnotation annotation_;
  MappedImageJDisplayRoi(std::shared_ptr<const Source> source,
                         DatasetShape target,
                         PlanarDisplayAnnotation annotation)
      : source_(std::move(source)), target_(target),
        annotation_(std::move(annotation)) {}
};
struct DecodedImageJDisplayRoiEnvelope {
  MappedImageJDisplayRoi mapped;
  ImageJDisplayRoiRecord standard_projection;
  ImageJDisplayProjectionOptions options;
};
// Bare typed wire admission only. No document samples, selections, fonts or IO.
MappedImageJDisplayRoi
decode_imagej_display_roi_mapped(const std::vector<unsigned char> &,
                                 const ImageJDisplayRoiMapping &,
                                 const ImageJDisplayRoiLimits & = {});
void validate_imagej_display_roi_mapping(const MappedImageJDisplayRoi &,
                                         const ImageJDisplayRoiLimits & = {});
MappedImageJDisplayRoi
rebind_imagej_display_roi_mapping(const MappedImageJDisplayRoi &, DatasetShape,
                                  const PlanarDisplayAnnotation &,
                                  const ImageJDisplayRoiLimits & = {});
// Closed self-describing display V1 envelope. Binds unchanged original bytes /
// mapping, current native row, explicit projection and every lossy choice.
std::vector<unsigned char>
encode_imagej_display_roi_envelope(const MappedImageJDisplayRoi &,
                                   const ImageJDisplayRoiRecord &,
                                   const ImageJDisplayProjectionOptions & = {},
                                   const ImageJDisplayRoiLimits & = {});
MappedImageJDisplayRoi
decode_imagej_display_roi_envelope(const std::vector<unsigned char> &,
                                   const ImageJDisplayRoiLimits & = {});
DecodedImageJDisplayRoiEnvelope
decode_imagej_display_roi_envelope_full(const std::vector<unsigned char> &,
                                        const ImageJDisplayRoiLimits & = {});
} // namespace imagejxx
