#include <imagejxx/imagej_rgb_projection.hpp>
#include <stdexcept>

int main() {
  using namespace imagejxx;
  Image a(1,1,3,PixelType::UInt8),b(1,1,3,PixelType::UInt8);
  a.pixels={200,10,0};b.pixels={100,20,0};
  const auto source=Dataset::from_planes({1,1,2,1,1,3},{a,b});
  ImageJRGBProjectionRequest request;
  request.last=1;request.method="sum";request.clip_when_summing=true;
  const auto layout=describe_imagej_rgb_projection(source,request);
  request.max_output_pixel_bytes=layout.output_pixel_bytes;
  request.max_working_bytes=layout.working_bytes;
  const auto clipped=project_imagej_rgb_dataset(source,request);
  if(clipped.dataset.pixel_type()!=PixelType::UInt8 || clipped.signed16_density ||
      clipped.dataset.plane()->pixels!=std::vector<double>{255,30,0})
    throw std::runtime_error("ImageJ RGB clipped sum failed");
  request.clip_when_summing=false;
  const auto normalized=project_imagej_rgb_dataset(source,request);
  // Each channel has a constant range for a one-pixel projection. ImageJ's
  // Float32 channel-to-byte normalization maps those constant ranges to zero.
  if(normalized.dataset.plane()->pixels!=std::vector<double>{0,0,0})
    throw std::runtime_error("ImageJ RGB constant-range normalization failed");
}
