#pragma once
#include <imagejxx/image.hpp>
#include <imagejxx/metadata.hpp>

namespace imagejxx {
struct DocumentMetadata;
struct ImageJDatasetProjectionRequest;
class Document;
class DocumentSnapshot;
enum class ImageJIntensityMode { None, Signed16, Lookup };
inline constexpr const char* imagej_intensity_property = "imagejxx.intensity_calibration";

// Versioned immutable property record. Lookup words use eight lowercase hex
// digits per IEEE Float32 word (most-significant nibble first), never JSON NaN.
// Nested metadata nodes are shared across snapshots/history; reading a record
// does not acquire pixels. Function generation/fitting is a separate producer.
class ImageJIntensityCalibration {
public:
  static ImageJIntensityCalibration uncalibrated(std::string value_unit = "Gray Value");
  static ImageJIntensityCalibration signed16(std::string value_unit = "Gray Value");
  static ImageJIntensityCalibration lookup(PixelType type,
      const std::vector<std::uint32_t>& words, std::string value_unit = "Gray Value",
      MetadataObject source_parameters = {}, bool signed16_sum_correction = false);
  // ImageJ1.54r built-in function -> bounded integer Float32 table. Coefficients
  // are finite doubles; generated NaNs use canonical0x7fc00000.8-bit zero_clip
  // and OD inversion follow ImageJ;16-bit table generation ignores zero_clip.
  static ImageJIntensityCalibration function(PixelType type,
      const std::string& name, const std::vector<double>& coefficients,
      std::string value_unit = "Gray Value", bool zero_clip = false,
      bool inverted_lut = false);
  static ImageJIntensityCalibration from_record(const MetadataValue& value);
  const MetadataValue& record() const { return record_; }
  ImageJIntensityMode mode() const { return mode_; }
  int bit_depth() const { return depth_; }
  bool has_density() const { return mode_ != ImageJIntensityMode::None; }
  bool signed16_density() const { return signed16_; }
  const std::string& value_unit() const;
  const MetadataObject& source_parameters() const;
  // Materializes one bounded caller-owned table only for Lookup mode.
  std::vector<std::uint32_t> table_words() const;
  void validate_for(PixelType type, int samples) const;
  bool shares_record(const ImageJIntensityCalibration& other) const;
private:
  ImageJIntensityCalibration(MetadataValue record, ImageJIntensityMode mode,
                            int depth, bool signed16);
  MetadataValue record_;
  ImageJIntensityMode mode_;
  int depth_;
  bool signed16_;
};
ImageJIntensityCalibration read_imagej_intensity_calibration(
    const DocumentMetadata&, PixelType type, int samples);
ImageJIntensityCalibration read_imagej_intensity_calibration(const DocumentSnapshot&);
// Explicit replacement of this reserved property only, with one atomic
// metadata edit and ordinary Undo/Redo. A default uncalibrated value removes it.
// Unsupported/malformed reads never normalize or overwrite stored data.
void set_imagej_intensity_calibration(Document&, const ImageJIntensityCalibration&);
// Request bridge. No pixel access; validates before replacing density fields.
// Caller-owned table materialization is configuration, separate from the
// executor's subsequently admitted/charged immutable execution copy.
void apply_imagej_intensity_calibration(const ImageJIntensityCalibration&,
    PixelType type, int samples, ImageJDatasetProjectionRequest&);
// Projection output policy, not a generic type-conversion policy. RGB resets
// value units. Float output clears density but retains the source unit. A
// preserve request requires compatible scalar integer storage and source density.
ImageJIntensityCalibration projected_imagej_intensity_calibration(
    const ImageJIntensityCalibration&, PixelType output_type, int samples,
    bool preserve_density);
} // namespace imagejxx
