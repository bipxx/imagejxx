#include <imagejxx/imagej_rgb_projection.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
namespace {
void check(bool value,const char* message="Projection policy assertion") {
  if(!value) throw std::runtime_error(message);
}
template<class Error=std::invalid_argument,class F> void reject(F call) {
  try {call();} catch(const Error&) {return;}
  throw std::runtime_error("Invalid projection policy accepted");
}
struct Case {
  const char* id;
  DatasetShape input_shape,output_shape;
  PixelType input_type,output_type;
  const char* method;
  int first,last,group,active_t,active_c;
  bool rgb,signed16,keep_density;
  std::array<double,6> input_geometry,output_geometry;
  std::vector<std::array<std::uint32_t,2>> entries;
  std::vector<std::vector<std::uint32_t>> input,expected;
};
const Case cases[]={
#include "imagej_projection_policy_reference.inc"
};
bool nan(std::uint32_t bits) {return (bits&0x7fffffffU)>0x7f800000U;}
std::uint32_t float_word(float value) {
  std::uint32_t bits;std::memcpy(&bits,&value,sizeof bits);return bits;
}
Stack inputs(const Case& c) {
  Stack output;
  for(const auto& words:c.input) {
    Image plane(c.input_shape.x,c.input_shape.y,c.input_shape.s,c.input_type);
    for(std::size_t i=0;i<words.size();++i) {
      if(c.rgb) for(int s=0;s<3;++s) plane.pixels[3*i+s]=(words[i]>>(16-8*s))&255;
      else plane.pixels[i]=c.input_type==PixelType::Float32
          ?float32_source_sample(words[i],FloatingSamplePolicy::PreserveIEEEFloat32):double(words[i]);
    }
    output.push_back(std::move(plane));
  }
  return output;
}
std::uint32_t word(const Image& plane,std::size_t i,bool rgb) {
  if(rgb) {
    std::uint32_t result=0xff000000U;
    for(int s=0;s<3;++s) {
      const auto v=plane.pixels[3*i+s];
      check(std::isfinite(v) && v>=0 && v<=255 && std::trunc(v)==v);
      result|=std::uint32_t(v)<<(16-8*s);
    }
    return result;
  }
  return plane.type==PixelType::Float32
      ?float32_source_sample_bits(plane.pixels[i],FloatingSamplePolicy::PreserveIEEEFloat32)
      :std::uint32_t(plane.pixels[i]);
}
class Provider final : public PlaneSource {
public:
  DatasetShape dimensions{2,4,3,1,1,1};
  mutable std::vector<PlanePosition> reads;
  bool unreadable=false;
  DatasetShape shape() const override {return dimensions;}
  PixelType pixel_type() const override {return PixelType::UInt16;}
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    reads.push_back(p);
    if(unreadable) throw std::runtime_error("Policy preflight read pixels");
    auto image=std::make_shared<Image>(dimensions.x,dimensions.y,1,PixelType::UInt16);
    image->pixels[0]=100*p.t+10*p.c+p.z;return image;
  }
};
}
int main() {
  std::size_t compared=0,nans=0;
  for(const auto& c:cases) {
    SpatialCalibration calibration;
    std::copy_n(c.input_geometry.begin(),3,calibration.spacing.begin());
    std::copy_n(c.input_geometry.begin()+3,3,calibration.origin.begin());
    const auto policy=c.input_type==PixelType::Float32
        ?FloatingSamplePolicy::PreserveIEEEFloat32:FloatingSamplePolicy::FiniteOnly;
    const auto source=Dataset::from_planes(c.input_shape,inputs(c),calibration,policy);
    ImageJRGBProjectionRequest request;
    request.first=c.first;request.last=c.last;request.group_size=c.group;
    request.method=c.method;request.active_t=c.active_t;request.active_c=c.active_c;
    request.signed16_density=c.signed16;
    if(!c.entries.empty()) {
      request.calibration_table.resize(c.input_type==PixelType::UInt8?256:65536);
      for(const auto& e:c.entries) request.calibration_table[e[0]]=e[1];
    }
    const auto original_table=request.calibration_table;
    for(int tile:{1,3}) {
      request.tile_width=tile;request.tile_height=tile==1?1:2;
      auto layout=c.rgb?describe_imagej_rgb_projection(source,request):describe_imagej_dataset_projection(source,request);
      check(layout.axis==(c.input_shape.c>1?ImageJProjectionAxis::Channels:ImageJProjectionAxis::Z),c.id);
      request.max_working_bytes=layout.working_bytes;request.max_output_pixel_bytes=layout.output_pixel_bytes;
      const auto result=c.rgb?project_imagej_rgb_dataset(source,request):project_imagej_dataset(source,request);
      check(result.dataset.shape().dimensions()==c.output_shape.dimensions(),c.id);
      check(result.dataset.pixel_type()==c.output_type,c.id);
      check(result.preserve_density_calibration==c.keep_density,c.id);
      check(result.signed16_density==(c.signed16&&c.output_type==PixelType::UInt16),c.id);
      check(layout.preserve_density_calibration==result.preserve_density_calibration,c.id);
      for(int i=0;i<3;++i) {
        check(result.dataset.calibration().spacing[i]==c.output_geometry[i],c.id);
        check(result.dataset.calibration().origin[i]==c.output_geometry[i+3],c.id);
      }
      std::size_t index=0;
      for(int t=0;t<c.output_shape.t;++t) for(int ch=0;ch<c.output_shape.c;++ch)
        for(int z=0;z<c.output_shape.z;++z,++index) {
          const auto plane=result.dataset.plane({t,ch,z});
          check(plane->pixels.size()==c.expected[index].size()*c.output_shape.s,c.id);
          for(std::size_t i=0;i<c.expected[index].size();++i) {
            const auto actual=word(*plane,i,c.rgb),expected=c.expected[index][i];
            const bool both_nan=!c.rgb && c.output_type==PixelType::Float32 && nan(actual)&&nan(expected);
            if(!both_nan && actual!=expected) {
              std::cerr<<c.id<<" plane "<<index<<" pixel "<<i<<" expected "<<std::hex<<expected<<" actual "<<actual<<'\n';return 1;
            }
            ++compared;nans+=both_nan;
          }
        }
      index=0;
      for(int t=0;t<c.input_shape.t;++t) for(int ch=0;ch<c.input_shape.c;++ch)
        for(int z=0;z<c.input_shape.z;++z,++index) {
          const auto plane=source.plane({t,ch,z});
          for(std::size_t i=0;i<c.input[index].size();++i) check(word(*plane,i,c.rgb)==c.input[index][i],c.id);
        }
      check(request.calibration_table==original_table,c.id);
      request.max_working_bytes=request.max_output_pixel_bytes=std::numeric_limits<std::size_t>::max();
    }
  }
  auto provider=std::make_shared<Provider>();Dataset source(provider);
  ImageJDatasetProjectionRequest request;
  request.axis=ImageJProjectionAxis::Channels;request.first=1;request.last=2;
  request.all_time_frames=false;request.active_t=1;request.all_channels=false;request.active_c=3;
  request.method="maximum";
  auto result=project_imagej_dataset(source,request);
  check(result.dataset.shape().dimensions()==std::array<int,6>{1,1,3,1,1,1});
  for(int z=0;z<3;++z) check(result.dataset.plane({0,0,z})->pixels[0]==120+z);
  check(provider->reads.size()==6);
  for(const auto p:provider->reads) check(p.t==1 && (p.c==1||p.c==2));
  provider->unreadable=true;provider->reads.clear();
  request.calibration_table.resize(65536);
  auto layout=describe_imagej_dataset_projection(source,request);
  auto without=request;without.calibration_table.clear();
  check(layout.working_bytes==describe_imagej_dataset_projection(source,without).working_bytes+65536U*4);
  int callbacks=0;ExecutionControl control;control.checkpoint=[&]{++callbacks;};
  auto bad=request;bad.max_working_bytes=layout.working_bytes-1;
  reject([&]{project_imagej_dataset(source,bad,control);});
  bad=request;bad.calibration_table.resize(65535);
  reject([&]{project_imagej_dataset(source,bad,control);});
  check(callbacks==0&&provider->reads.empty());
  bad={};bad.calibration_table.resize(256);
  reject([&]{describe_imagej_dataset_projection({1,1,1,1,1,1},PixelType::Float32,{},bad);});
  ImageJRGBProjectionRequest bad_rgb;bad_rgb.calibration_table.resize(256);
  reject([&]{describe_imagej_rgb_projection({1,1,1,1,1,3},PixelType::UInt8,{},bad_rgb);});
  // Odd calibrated medians, NaNs/infinities, no second signed16 subtraction,
  // and mutation of caller metadata after owned admission.
  Stack pixels;
  for(int i=0;i<3;++i) {Image p(1,1,1,PixelType::UInt16);p.pixels={double(i)};pixels.push_back(std::move(p));}
  const auto short_source=Dataset::from_planes({1,1,3,1,1,1},pixels);
  request={};request.last=2;request.method="median";request.calibration_table.resize(65536);
  request.calibration_table[0]=float_word(10);request.calibration_table[1]=float_word(-4);request.calibration_table[2]=float_word(2);
  const auto captured_table=request.calibration_table;
  control={};control.progress=[&](std::size_t,std::size_t){request.calibration_table.clear();request.method="invalid";};
  result=project_imagej_dataset(short_source,request,control);
  check(result.dataset.plane()->pixels[0]==2 && !result.preserve_density_calibration);
  request={};request.last=2;request.method="median";request.calibration_table=captured_table;request.signed16_density=true;
  check(project_imagej_dataset(short_source,request).dataset.plane()->pixels[0]==2);
  request.signed16_density=false;
  for(int i=0;i<3;++i) request.calibration_table[i]=0x7fa12345U;
  result=project_imagej_dataset(short_source,request);
  check(nan(float32_source_sample_bits(result.dataset.plane()->pixels[0],FloatingSamplePolicy::PreserveIEEEFloat32)));
  Stack bytes;for(const auto& p:pixels) {Image b=p;b.type=PixelType::UInt8;bytes.push_back(std::move(b));}
  const auto byte_source=Dataset::from_planes({1,1,3,1,1,1},bytes);
  request.calibration_table.resize(256);
  result=project_imagej_dataset(byte_source,request);
  check(result.dataset.plane()->pixels[0]==0 && result.preserve_density_calibration);
  for(int i=0;i<3;++i) request.calibration_table[i]=float_word(-1000);
  check(project_imagej_dataset(byte_source,request).dataset.plane()->pixels[0]==0);
  for(int i=0;i<3;++i) request.calibration_table[i]=0x7f800000U;
  check(project_imagej_dataset(byte_source,request).dataset.plane()->pixels[0]==255);
  auto forged=layout;forged.source_first=std::numeric_limits<int>::max();
  reject<std::out_of_range>([&]{forged.contributor({0,0,0},1);});
  std::cout<<std::size(cases)<<" policy cases x2 tiles: "<<compared<<" words ("<<nans
           <<" NaNs by class); table admission/capture and channel contributor checks passed\n";
}
