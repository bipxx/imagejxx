#include <imagejxx/imagej_rgb_projection.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace imagejxx;
namespace {
void check(bool v,const char* message="RGB projection assertion") {
  if(!v) throw std::runtime_error(message);
}
template<class Error=std::invalid_argument,class F> void reject(F call) {
  try {call();} catch(const Error&) {return;}
  throw std::runtime_error("Invalid RGB projection request accepted");
}
struct Case {
  const char* id;
  DatasetShape input_shape,output_shape;
  const char* method;
  int first,last,group,active_t,active_c;
  bool all_time,clip,accepted;
  std::array<double,6> input_geometry,output_geometry;
  std::vector<std::vector<std::uint32_t>> input,expected;
};
const Case cases[]={
#include "imagej_rgb_projection_reference.inc"
};
class Unreadable final : public PlaneSource {
public:
  DatasetShape dimensions;
  mutable int reads=0;
  explicit Unreadable(DatasetShape value):dimensions(value){}
  DatasetShape shape() const override {return dimensions;}
  PixelType pixel_type() const override {return PixelType::UInt8;}
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++reads;throw std::runtime_error("RGB rejection read pixels");
  }
};
std::uint32_t packed(const Image& image,std::size_t i) {
  std::uint32_t word=0xff000000U;
  for(int c=0;c<3;++c) {
    const double v=image.pixels[3*i+c];
    check(std::isfinite(v) && v>=0 && v<=255 && std::trunc(v)==v);
    word|=static_cast<std::uint32_t>(v)<<(16-8*c);
  }
  return word;
}
Stack input_planes(const Case& c) {
  Stack result;
  for(const auto& words:c.input) {
    Image image(c.input_shape.x,c.input_shape.y,3,PixelType::UInt8);
    check(words.size()*3==image.pixels.size());
    for(std::size_t i=0;i<words.size();++i) for(int s=0;s<3;++s)
      image.pixels[3*i+s]=(words[i]>>(16-8*s))&255;
    result.push_back(std::move(image));
  }
  return result;
}
class Source final : public PlaneSource {
public:
  DatasetShape dimensions{2,1,4,3,5,3};
  bool tiled=false,bad=false;
  mutable int planes=0,regions=0;
  mutable std::vector<std::weak_ptr<const Image>> leases;
  mutable std::size_t peak=0;
  DatasetShape shape() const override {return dimensions;}
  PixelType pixel_type() const override {return PixelType::UInt8;}
  std::size_t live() const {
    return std::count_if(leases.begin(),leases.end(),[](const auto& p){return !p.expired();});
  }
  Image region_data(PlanePosition p,int x,int y,int width,int height) const {
    Image image(width,height,3,PixelType::UInt8);
    for(int yy=0;yy<height;++yy) for(int xx=0;xx<width;++xx) for(int c=0;c<3;++c)
      image.at(xx,yy,c)=40*p.t+3*p.z+2*(y+yy)+(x+xx)+30*c;
    if(bad) image.pixels[2]=.25;
    return image;
  }
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    ++planes;
    auto image=std::make_shared<const Image>(region_data(p,0,0,dimensions.x,dimensions.y));
    leases.push_back(image);peak=std::max(peak,live());return image;
  }
  Image read_region(PlanePosition p,int x,int y,int width,int height) const override {
    ++regions;
    return tiled?region_data(p,x,y,width,height):PlaneSource::read_region(p,x,y,width,height);
  }
};
}

int main() {
  std::size_t accepted=0,rejected=0,words=0;
  for(const auto& c:cases) {
    ImageJRGBProjectionRequest request;
    request.first=c.first;request.last=c.last;request.group_size=c.group;
    request.active_t=c.active_t;request.active_c=c.active_c;
    request.all_time_frames=c.all_time;request.method=c.method;request.clip_when_summing=c.clip;
    SpatialCalibration calibration;
    std::copy_n(c.input_geometry.begin(),3,calibration.spacing.begin());
    std::copy_n(c.input_geometry.begin()+3,3,calibration.origin.begin());
    if(!c.accepted) {
      auto provider=std::make_shared<Unreadable>(c.input_shape);
      Dataset source(provider,calibration);
      int callbacks=0;ExecutionControl control;control.checkpoint=[&]{++callbacks;};
      reject([&]{describe_imagej_rgb_projection(source,request);});
      try {project_imagej_rgb_dataset(source,request,control);return 1;}
      catch(const std::invalid_argument& error) {check(std::string(error.what())=="RGB stacks not supported",c.id);}
      check(provider->reads==0 && callbacks==0,c.id);
      ++rejected;continue;
    }
    auto source=Dataset::from_planes(c.input_shape,input_planes(c),calibration);
    for(int tile:{1,3}) {
      request.tile_width=tile;request.tile_height=tile==1?1:2;
      auto layout=describe_imagej_rgb_projection(source,request);
      request.max_output_pixel_bytes=layout.output_pixel_bytes;
      request.max_working_bytes=layout.working_bytes;
      DatasetReadObserver observer;
      auto result=project_imagej_rgb_dataset(source.observe_reads(observer),request);
      check(result.dataset.shape().dimensions()==c.output_shape.dimensions(),c.id);
      check(result.dataset.pixel_type()==PixelType::UInt8 && !result.signed16_density,c.id);
      check(result.dataset.sample_policy()==FloatingSamplePolicy::FiniteOnly,c.id);
      for(int i=0;i<3;++i) {
        check(result.dataset.calibration().spacing[i]==c.output_geometry[i],c.id);
        check(result.dataset.calibration().origin[i]==c.output_geometry[i+3],c.id);
      }
      std::size_t index=0;
      for(int t=0;t<c.output_shape.t;++t) for(int ch=0;ch<c.output_shape.c;++ch)
        for(int z=0;z<c.output_shape.z;++z,++index) {
          auto actual=result.dataset.plane({t,ch,z});
          check(actual->pixels.size()==3*c.expected[index].size(),c.id);
          for(std::size_t i=0;i<c.expected[index].size();++i) {
            const auto value=packed(*actual,i);
            if(value!=c.expected[index][i]) {
              std::cerr<<c.id<<" plane "<<index<<" pixel "<<i<<" expected "
                       <<std::hex<<c.expected[index][i]<<" actual "<<value<<'\n';return 1;
            }
            ++words;
          }
        }
      const auto tiles_x=(c.input_shape.x+layout.tile_width-1)/layout.tile_width;
      const auto tiles_y=(c.input_shape.y+layout.tile_height-1)/layout.tile_height;
      const auto expected_reads=c.output_shape.plane_count()*layout.contributors_per_group*tiles_x*tiles_y;
      check(observer.counts().region_reads==expected_reads && observer.counts().plane_reads==0,c.id);
      index=0;
      for(int t=0;t<c.input_shape.t;++t) for(int ch=0;ch<c.input_shape.c;++ch)
        for(int z=0;z<c.input_shape.z;++z,++index) {
          auto original=source.plane({t,ch,z});
          for(std::size_t i=0;i<c.input[index].size();++i) check(packed(*original,i)==c.input[index][i],c.id);
        }
      request.max_output_pixel_bytes=request.max_working_bytes=std::numeric_limits<std::size_t>::max();
    }
    ++accepted;
  }
  for(bool tiled:{false,true}) for(auto method:{"mean","maximum","minimum","sum","stddev","median"})
    for(bool clip:{false,true}) {
      auto provider=std::make_shared<Source>();provider->tiled=tiled;
      Dataset source(provider);
      ImageJRGBProjectionRequest request;
      request.last=3;request.group_size=2;request.active_t=1;request.all_time_frames=false;
      request.method=method;request.clip_when_summing=clip;request.tile_width=2;request.tile_height=2;
      const auto result=project_imagej_rgb_dataset(source,request);
      check(provider->regions==24 && provider->planes==(tiled?0:24));
      check(provider->peak<=1 && provider->live()==0);
      Stack eager;
      for(int t=0;t<2;++t) for(int z=0;z<4;++z) eager.push_back(provider->region_data({t,0,z},0,0,5,3));
      const auto expected=project_imagej_rgb_dataset(Dataset::from_planes(provider->dimensions,std::move(eager)),request);
      for(int z=0;z<2;++z)
        check(result.dataset.plane({0,0,z})->pixels==expected.dataset.plane({0,0,z})->pixels);
    }
  auto provider=std::make_shared<Source>();Dataset source(provider);
  ImageJRGBProjectionRequest request;request.last=3;request.method="sum";
  const auto layout=describe_imagej_rgb_projection(source,request);
  auto bad=request;bad.max_working_bytes=layout.working_bytes-1;
  int callbacks=0;ExecutionControl control;control.checkpoint=[&]{++callbacks;};
  reject([&]{project_imagej_rgb_dataset(source,bad,control);});
  bad=request;bad.max_output_pixel_bytes=layout.output_pixel_bytes-1;
  reject([&]{project_imagej_rgb_dataset(source,bad,control);});
  check(callbacks==0 && provider->regions==0);
  reject([&]{describe_imagej_rgb_projection({1,1,1,1,1,3},PixelType::UInt16,{},{});});
  reject([&]{describe_imagej_rgb_projection({1,1,1,1,1,4},PixelType::UInt8,{},{});});
  provider->bad=true;reject([&]{project_imagej_rgb_dataset(source,request);});
  check(provider->live()==0);provider->bad=false;
  request.clip_when_summing=true;
  auto expected=project_imagej_rgb_dataset(source,request);
  control={};control.progress=[&](std::size_t,std::size_t){request.clip_when_summing=false;};
  auto captured=project_imagej_rgb_dataset(source,request,control);
  check(captured.dataset.plane()->pixels==expected.dataset.plane()->pixels);
  // All numerical reductions finish before these cancellable RGB merge passes.
  auto large=std::make_shared<Source>();large->dimensions={1,1,2,2,1025,3};
  // Keep this synthetic provider's codes within UInt8 for the larger geometry.
  Stack large_planes;
  for(int z=0;z<2;++z) {
    Image image(1025,2,3,PixelType::UInt8);
    for(std::size_t i=0;i<image.pixels.size();++i) image.pixels[i]=static_cast<double>((i*7+z*31)%256);
    large_planes.push_back(std::move(image));
  }
  auto merge_source=Dataset::from_planes(large->dimensions,std::move(large_planes));
  for(auto stage:{"ImageJ RGB projection scaling","ImageJ RGB channel conversion","ImageJ RGB sum clipping"}) {
    request={};request.last=1;request.method="sum";
    request.clip_when_summing=std::string(stage)=="ImageJ RGB sum clipping";
    control={};control.cancellation=std::make_shared<CancellationToken>();
    bool entered=false;
    control.stage_progress=[&](std::string_view name,std::size_t done,std::size_t){
      if(name==stage && done>0) {entered=true;control.cancellation->cancel();}
    };
    reject<OperationCancelled>([&]{project_imagej_rgb_dataset(merge_source,request,control);});
    check(entered);
  }
  std::cout<<accepted<<" pinned RGB successes x2 tile layouts: "<<words
           <<" packed words exact; "<<rejected<<" preflight rejections; provider/merge cancellation checks passed\n";
}
