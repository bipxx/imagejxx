#include <imagejxx/global_calibration.hpp>

namespace imagejxx {
namespace {
int bit_depth(PixelType type, int samples) {
  if(samples!=1 && samples!=3 && samples!=4)throw std::invalid_argument("Invalid global calibration sample count");
  int bits=0;
  switch(type) {
    case PixelType::UInt8:bits=8;break;
    case PixelType::UInt16:bits=16;break;
    case PixelType::Float32:bits=32;break;
    case PixelType::Float64:bits=64;break;
    default:throw std::invalid_argument("Invalid global calibration storage type");
  }
  return samples==1?bits:24;
}
}
GlobalCalibration::GlobalCalibration(SpatialCalibration spatial,
    ImageJIntensityCalibration intensity, PixelType type, int samples)
    : spatial_(std::move(spatial)), intensity_(std::move(intensity)),
      source_type_(type), source_samples_(samples) {
  spatial_.validate();(void)bit_depth(type,samples);intensity_.validate_for(type,samples);
}
GlobalCalibration GlobalCalibration::from_snapshot(const DocumentSnapshot& source) {
  const auto& dataset=source.data().dataset;
  return {dataset.calibration(),read_imagej_intensity_calibration(source),dataset.pixel_type(),dataset.shape().s};
}
int GlobalCalibration::source_bit_depth() const { return bit_depth(source_type_,source_samples_); }
EffectiveCalibration::EffectiveCalibration(SpatialCalibration spatial,
    ImageJIntensityCalibration intensity, const DocumentSnapshot& source, bool global, bool ignore)
    : spatial_(std::move(spatial)), intensity_(std::move(intensity)), uses_global_(global),
      ignores_global_(ignore), document_id_(source.id()), source_revisions_(source.revisions()) {}
EffectiveCalibration resolve_effective_calibration(const DocumentSnapshot& source,
    const GlobalCalibration* global, bool ignore) {
  const auto& dataset=source.data().dataset;
  auto local=read_imagej_intensity_calibration(source);
  if(!global || ignore)return {dataset.calibration(),std::move(local),source,false,ignore};
  const auto type=dataset.pixel_type();const auto samples=dataset.shape().s;
  // Calibration.setImage gives a locally signed16 destination priority over
  // global density, while retaining the global spatial calibration.
  auto intensity=global->intensity();
  if(samples==1 && type==PixelType::UInt16 && local.signed16_density())
    intensity=ImageJIntensityCalibration::signed16();
  else if(samples!=1)intensity=ImageJIntensityCalibration::uncalibrated();
  else if(type==PixelType::Float32 || type==PixelType::Float64)
    intensity=ImageJIntensityCalibration::uncalibrated(global->intensity().value_unit());
  else if(bit_depth(type,samples)!=global->source_bit_depth())
    intensity=ImageJIntensityCalibration::uncalibrated();
  intensity.validate_for(type,samples);
  return {global->spatial(),std::move(intensity),source,true,ignore};
}
} // namespace imagejxx
