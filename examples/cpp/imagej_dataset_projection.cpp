#include <imagejxx/imagej_dataset_projection.hpp>
#include <stdexcept>

int main() {
  using namespace imagejxx;
  Stack frames;
  for (int t=0;t<3;++t) {
    Image plane(2,1,1,PixelType::UInt16);
    plane.pixels={double(32768+t),double(32770+t)};
    frames.push_back(std::move(plane));
  }
  const auto source=Dataset::from_planes({3,1,1,1,2,1},std::move(frames));
  ImageJDatasetProjectionRequest request;
  request.last=2;
  request.method="sum";
  request.signed16_density=true;
  const auto layout=describe_imagej_dataset_projection(source,request);
  if(layout.axis!=ImageJProjectionAxis::Time || layout.shape.t!=1)
    throw std::runtime_error("ImageJ one-Z time dispatch failed");
  request.max_working_bytes=layout.working_bytes;
  request.max_output_pixel_bytes=layout.output_pixel_bytes;
  const auto result=project_imagej_dataset(source,request);
  if(result.signed16_density || result.dataset.pixel_type()!=PixelType::Float32 ||
      result.dataset.plane()->pixels!=std::vector<double>{3,9})
    throw std::runtime_error("Signed16 ImageJ time projection failed");
}
