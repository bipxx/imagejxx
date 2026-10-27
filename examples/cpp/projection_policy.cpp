#include <imagejxx/imagej_dataset_projection.hpp>
#include <stdexcept>
int main() {
  using namespace imagejxx;
  Stack planes;
  for(int i=1;i<=3;++i) {
    Image p(1,1,1,PixelType::UInt8);p.pixels={double(i)};planes.push_back(std::move(p));
  }
  const auto source=Dataset::from_planes({1,3,1,1,1,1},std::move(planes));
  ImageJDatasetProjectionRequest request;
  request.last=2;request.method="median";request.calibration_table.resize(256);
  request.calibration_table[1]=0x42c80000U; //100
  request.calibration_table[2]=0;           //0
  request.calibration_table[3]=0x43480000U; //200
  const auto layout=describe_imagej_dataset_projection(source,request);
  if(layout.axis!=ImageJProjectionAxis::Channels) throw std::runtime_error("Legacy C-only axis missing");
  request.max_working_bytes=layout.working_bytes;request.max_output_pixel_bytes=layout.output_pixel_bytes;
  const auto result=project_imagej_dataset(source,request);
  if(result.dataset.shape().c!=1 || result.dataset.plane()->pixels[0]!=100 ||
      !result.preserve_density_calibration || result.signed16_density)
    throw std::runtime_error("Calibrated channel projection failed");
}
