#pragma once
#include <imagejxx/document.hpp>
#include <imagejxx/imagej_intensity_calibration.hpp>

namespace imagejxx {
// Immutable metadata value; stores no source document, provider or pixels.
// Source storage is retained even for None density: ImageJ uses it to adapt
// the effective density/unit when another image has a different bit depth.
class GlobalCalibration {
public:
  GlobalCalibration(SpatialCalibration spatial, ImageJIntensityCalibration intensity,
                    PixelType source_type, int source_samples = 1);
  static GlobalCalibration from_snapshot(const DocumentSnapshot& source);
  const SpatialCalibration& spatial() const { return spatial_; }
  const ImageJIntensityCalibration& intensity() const { return intensity_; }
  PixelType source_type() const { return source_type_; }
  int source_samples() const { return source_samples_; }
  int source_bit_depth() const;
private:
  SpatialCalibration spatial_;
  ImageJIntensityCalibration intensity_;
  PixelType source_type_;
  int source_samples_;
};

class Application;
class EffectiveCalibration {
public:
  const SpatialCalibration& spatial() const { return spatial_; }
  const ImageJIntensityCalibration& intensity() const { return intensity_; }
  bool uses_global() const { return uses_global_; }
  bool ignores_global() const { return ignores_global_; }
  std::uint64_t document_id() const { return document_id_; }
  Revisions source_revisions() const { return source_revisions_; }
  std::uint64_t global_generation() const { return global_generation_; }
  std::uint64_t bypass_generation() const { return bypass_generation_; }
private:
  friend class Application;
  friend EffectiveCalibration resolve_effective_calibration(
      const DocumentSnapshot&, const GlobalCalibration*, bool);
  EffectiveCalibration(SpatialCalibration spatial, ImageJIntensityCalibration intensity,
                       const DocumentSnapshot& source, bool global, bool ignore);
  SpatialCalibration spatial_;
  ImageJIntensityCalibration intensity_;
  bool uses_global_, ignores_global_;
  std::uint64_t document_id_;
  Revisions source_revisions_;
  std::uint64_t global_generation_ = 0, bypass_generation_ = 0;
  std::weak_ptr<const int> application_;
};

// Pure metadata resolver; does not replace local document state. Application
// captures additionally carry ownership/generation checks. Native Float64 and
// RGBA follow Float32/RGB adaptation respectively; these are native extensions.
EffectiveCalibration resolve_effective_calibration(const DocumentSnapshot& source,
    const GlobalCalibration* global = nullptr, bool ignore_global = false);
} // namespace imagejxx
