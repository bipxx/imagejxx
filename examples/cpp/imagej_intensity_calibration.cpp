#include <imagejxx/imagej_intensity_calibration.hpp>
#include <imagejxx/imagej_dataset_projection.hpp>
#include <imagejxx/document.hpp>
#include <stdexcept>
int main() {
  using namespace imagejxx;
  Stack frames;
  for(int i=0;i<3;++i) {
    Image frame(1,1,1,PixelType::UInt16);frame.pixels={double(32768+i)};
    frames.push_back(std::move(frame));
  }
  Document source(Dataset::from_planes({1,1,3,1,1,1},std::move(frames)),"signed source");
  set_imagej_intensity_calibration(source,ImageJIntensityCalibration::signed16("mV"));
  const auto snapshot=source.snapshot();
  const auto intensity=read_imagej_intensity_calibration(snapshot);
  ImageJDatasetProjectionRequest request;request.last=2;request.method="median";
  apply_imagej_intensity_calibration(intensity,PixelType::UInt16,1,request);
  const auto result=project_imagej_dataset(snapshot.data().dataset,request);
  const auto output_intensity=projected_imagej_intensity_calibration(
      intensity,result.dataset.pixel_type(),1,result.preserve_density_calibration);
  if(result.dataset.plane()->pixels[0]!=1 || output_intensity.has_density() ||
      output_intensity.value_unit()!="mV")
    throw std::runtime_error("Intensity-to-projection metadata bridge failed");
  // Actual new-document publication must also apply the prepared source guards.
}
