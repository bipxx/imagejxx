#include <imagejxx/imagej_dataset_projection.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace imagejxx;
namespace {
void check(bool condition, const char* message = "ImageJ dataset projection assertion") {
  if (!condition) throw std::runtime_error(message);
}
template<class Error = std::invalid_argument, class F> void reject(F call) {
  try { call(); } catch (const Error&) { return; }
  throw std::runtime_error("Invalid ImageJ dataset request was accepted");
}
struct Case {
  const char* id;
  DatasetShape input_shape, output_shape;
  PixelType input_type, output_type;
  const char* method;
  int first, last, group, active_t, active_c;
  bool all_time, signed16;
  std::array<double, 6> input_geometry, output_geometry;
  std::vector<std::vector<std::uint32_t>> input, expected;
};
const Case cases[] = {
#include "imagej_dataset_projection_reference.inc"
};
bool nan(std::uint32_t bits) { return (bits & 0x7fffffffU) > 0x7f800000U; }
std::uint32_t output_word(double sample, PixelType type) {
  return type == PixelType::Float32
      ? float32_source_sample_bits(sample, FloatingSamplePolicy::PreserveIEEEFloat32)
      : static_cast<std::uint32_t>(sample);
}
Stack inputs(const Case& c) {
  Stack result;
  for (const auto& codes : c.input) {
    Image plane(c.input_shape.x, c.input_shape.y, 1, c.input_type);
    check(codes.size() == plane.pixels.size());
    for (std::size_t i = 0; i < codes.size(); ++i)
      plane.pixels[i] = c.input_type == PixelType::Float32
          ? float32_source_sample(codes[i], FloatingSamplePolicy::PreserveIEEEFloat32)
          : double(codes[i]);
    result.push_back(std::move(plane));
  }
  return result;
}
class Source final : public PlaneSource {
public:
  mutable DatasetShape dimensions{3,2,6,3,5,1};
  mutable PixelType type = PixelType::UInt16;
  mutable int planes = 0, regions = 0;
  mutable std::vector<std::weak_ptr<const Image>> leases;
  mutable std::size_t peak = 0;
  bool tiled = false, fail = false, bad_code = false, drift = false;
  std::size_t live() const {
    return std::count_if(leases.begin(), leases.end(), [](const auto& p){return !p.expired();});
  }
  DatasetShape shape() const override { return dimensions; }
  PixelType pixel_type() const override { return type; }
  static double sample(PlanePosition p, int x, int y) {
    return 1000*p.t + 100*p.c + 10*y + x + p.z*p.z;
  }
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    ++planes;
    if (fail) throw std::runtime_error("Owned reader failure");
    auto image = std::make_shared<Image>(dimensions.x, dimensions.y, 1, type);
    for (int y=0;y<dimensions.y;++y) for (int x=0;x<dimensions.x;++x)
      image->at(x,y,0) = sample(p,x,y);
    if (bad_code) image->pixels[0] = .5;
    leases.push_back(image);
    peak = std::max(peak,live());
    if (drift) type = PixelType::Float64;
    return image;
  }
  Image read_region(PlanePosition p,int x,int y,int width,int height) const override {
    ++regions;
    if (!tiled) return PlaneSource::read_region(p,x,y,width,height);
    if (fail) throw std::runtime_error("Owned region failure");
    Image region(width,height,1,type);
    for (int yy=0;yy<height;++yy) for (int xx=0;xx<width;++xx)
      region.at(xx,yy,0) = sample(p,x+xx,y+yy);
    if (bad_code) region.pixels[0] = .5;
    if (drift) type = PixelType::Float64;
    return region;
  }
};
}

int main() {
  std::size_t compared = 0, nan_results = 0;
  for (const auto& c : cases) {
    SpatialCalibration calibration;
    std::copy_n(c.input_geometry.begin(),3,calibration.spacing.begin());
    std::copy_n(c.input_geometry.begin()+3,3,calibration.origin.begin());
    calibration.frame_interval = 2.5;
    calibration.time_origin = 7;
    const auto policy = c.input_type == PixelType::Float32
        ? FloatingSamplePolicy::PreserveIEEEFloat32 : FloatingSamplePolicy::FiniteOnly;
    auto source = Dataset::from_planes(c.input_shape,inputs(c),calibration,policy);
    ImageJDatasetProjectionRequest request;
    request.first=c.first;request.last=c.last;request.group_size=c.group;
    request.active_t=c.active_t;request.active_c=c.active_c;
    request.all_time_frames=c.all_time;request.method=c.method;
    request.signed16_density=c.signed16;
    for (int tile : {1,3}) {
      request.tile_width = tile;
      request.tile_height = tile == 1 ? 1 : 2;
      auto layout = describe_imagej_dataset_projection(source,request);
      request.max_output_pixel_bytes = layout.output_pixel_bytes;
      request.max_working_bytes = layout.working_bytes;
      const auto result = project_imagej_dataset(source,request);
      check(result.dataset.shape().dimensions()==c.output_shape.dimensions(),c.id);
      check(result.dataset.pixel_type()==c.output_type,c.id);
      check(result.signed16_density==(c.signed16 && c.output_type==PixelType::UInt16),c.id);
      const auto expected_policy = c.output_type==PixelType::Float32
          ? FloatingSamplePolicy::PreserveIEEEFloat32 : FloatingSamplePolicy::FiniteOnly;
      check(result.dataset.sample_policy()==expected_policy,c.id);
      for (int i=0;i<3;++i) {
        check(result.dataset.calibration().spacing[i]==c.output_geometry[i],c.id);
        check(result.dataset.calibration().origin[i]==c.output_geometry[i+3],c.id);
      }
      check(result.dataset.calibration().time_origin==7,c.id);
      check(result.dataset.calibration().frame_interval==2.5,c.id);
      std::size_t index=0;
      for(int t=0;t<c.output_shape.t;++t) for(int ch=0;ch<c.output_shape.c;++ch)
        for(int z=0;z<c.output_shape.z;++z,++index) {
          const auto actual=result.dataset.plane({t,ch,z});
          check(actual->pixels.size()==c.expected[index].size(),c.id);
          for(std::size_t i=0;i<actual->pixels.size();++i) {
            const auto bits=output_word(actual->pixels[i],c.output_type);
            const bool both_nan=c.output_type==PixelType::Float32 &&
                nan(bits) && nan(c.expected[index][i]);
            if(!both_nan && bits!=c.expected[index][i]) {
              std::cerr<<c.id<<" plane "<<index<<" pixel "<<i<<" expected "
                       <<std::hex<<c.expected[index][i]<<" actual "<<bits<<'\n';
              return 1;
            }
            ++compared;
            nan_results+=both_nan;
          }
        }
      check(!result.dataset.shares_pixels(source));
      // Every source plane remains bit-identical, including signaling NaNs.
      index=0;
      for(int t=0;t<c.input_shape.t;++t) for(int ch=0;ch<c.input_shape.c;++ch)
        for(int z=0;z<c.input_shape.z;++z,++index) {
          const auto plane=source.plane({t,ch,z});
          for(std::size_t i=0;i<plane->pixels.size();++i)
            check(output_word(plane->pixels[i],c.input_type)==c.input[index][i],c.id);
        }
      request.max_output_pixel_bytes=std::numeric_limits<std::size_t>::max();
      request.max_working_bytes=std::numeric_limits<std::size_t>::max();
    }
  }
  for (bool tiled : {false,true}) for (auto method : {"mean","sum","maximum","minimum","stddev","median"}) {
    auto provider=std::make_shared<Source>();provider->tiled=tiled;
    Dataset source(provider);
    DatasetReadObserver observer;
    ImageJDatasetProjectionRequest request;
    request.first=1;request.last=4;request.group_size=2;
    request.active_t=2;request.active_c=1;
    request.all_time_frames=false;request.all_channels=false;
    request.method=method;request.tile_width=2;request.tile_height=1;
    const auto result=project_imagej_dataset(source.observe_reads(observer),request);
    check(result.dataset.shape().dimensions()==std::array<int,6>{1,1,2,3,5,1});
    check(provider->regions==36 && provider->planes==(tiled?0:36));
    check(provider->peak<2 && provider->live()==0);
    check(observer.counts().region_reads==36 && observer.counts().plane_reads==0);
    for(int z=0;z<2;++z) {
      auto plane=result.dataset.plane({0,0,z});
      for(int y=0;y<3;++y) for(int x=0;x<5;++x) {
        const double a=Source::sample({2,1,1+2*z},x,y);
        const double b=Source::sample({2,1,2+2*z},x,y);
        const double expected=request.method=="mean" ? std::floor((a+b)/2+.5) :
            request.method=="sum" ? a+b :request.method=="maximum" ? b :
            request.method=="minimum" ? a :request.method=="median" ? (a+b)/2 :
            double(float((b-a)/std::sqrt(2.0)));
        check(plane->at(x,y,0)==expected);
      }
    }
  }
  auto provider=std::make_shared<Source>();
  Dataset source(provider);
  ImageJDatasetProjectionRequest request;request.last=5;request.tile_width=2;
  const auto layout=describe_imagej_dataset_projection(source,request);
  int callbacks=0;
  ExecutionControl count;count.checkpoint=[&]{++callbacks;};
  auto bad=request;bad.max_working_bytes=layout.working_bytes-1;
  reject([&]{project_imagej_dataset(source,bad,count);});
  bad=request;bad.max_output_pixel_bytes=layout.output_pixel_bytes-1;
  reject([&]{project_imagej_dataset(source,bad,count);});
  check(callbacks==0 && provider->regions==0 && provider->planes==0);
  for (auto invalid : {0,-1}) {
    bad=request;bad.tile_width=invalid;
    reject([&]{project_imagej_dataset(source,bad,count);});
    bad=request;bad.tile_height=invalid;
    reject([&]{project_imagej_dataset(source,bad,count);});
  }
  bad=request;bad.group_size=4;reject([&]{project_imagej_dataset(source,bad,count);});
  bad=request;bad.method="avg";reject([&]{project_imagej_dataset(source,bad,count);});
  bad=request;bad.active_c=2;
  reject<std::out_of_range>([&]{project_imagej_dataset(source,bad,count);});
  bad=request;bad.axis=static_cast<ImageJProjectionAxis>(99);
  reject([&]{project_imagej_dataset(source,bad,count);});
  reject([&]{describe_imagej_dataset_projection({1,1,1,1,1,3},PixelType::UInt8,{},{});});
  reject([&]{describe_imagej_dataset_projection({1,1,1,1,1,1},PixelType::Float64,{},{});});
  bad={};bad.signed16_density=true;
  reject([&]{describe_imagej_dataset_projection({1,1,1,1,1,1},PixelType::Float32,{},bad);});
  check(callbacks==0 && provider->regions==0);
  auto token=std::make_shared<CancellationToken>();token->cancel();
  count.cancellation=token;
  reject<OperationCancelled>([&]{project_imagej_dataset(source,request,count);});
  check(callbacks==0 && provider->regions==0);
  count={};count.cancellation=std::make_shared<CancellationToken>();
  count.checkpoint=[&]{if(provider->regions>2) count.cancellation->cancel();};
  reject<OperationCancelled>([&]{project_imagej_dataset(source,request,count);});
  check(provider->regions<10 && provider->live()==0);
  count={};count.checkpoint=[] {throw std::runtime_error("Owned callback failure");};
  const auto reads_before=provider->regions;
  reject<std::runtime_error>([&]{project_imagej_dataset(source,request,count);});
  check(provider->regions==reads_before && provider->live()==0);
  provider->fail=true;
  reject<std::runtime_error>([&]{project_imagej_dataset(source,request);});
  provider->fail=false;provider->bad_code=true;
  reject([&]{project_imagej_dataset(source,request);});
  check(provider->live()==0);
  provider->bad_code=false;provider->drift=true;
  reject([&]{project_imagej_dataset(source,request);});
  provider->drift=false;provider->type=PixelType::UInt16;
  ExecutionControl mutate;
  mutate.progress=[&](std::size_t,std::size_t){request.method="invalid";request.last=99;};
  const auto captured=project_imagej_dataset(source,request,mutate);
  check(captured.dataset.shape().z==1 && captured.dataset.pixel_type()==PixelType::UInt16);
  check(request.method=="invalid");
  // Explicit Time preserves Z; default uses Z for a multi-Z source.
  request={};request.axis=ImageJProjectionAxis::Time;request.last=2;
  request.method="maximum";
  const auto time=project_imagej_dataset(source,request);
  check(time.dataset.shape().dimensions()==std::array<int,6>{1,2,6,3,5,1});
  check(time.dataset.plane({0,1,5})->at(4,2,0)==Source::sample({2,1,5},4,2));
  // Long median cancellation must be observable after all contributors were
  // read, inside its bounded scratch processing (not only at plane boundaries).
  auto long_source=std::make_shared<Source>();
  long_source->dimensions={1,1,2050,1,1,1};long_source->type=PixelType::Float32;
  Dataset median_source(long_source);
  request={};request.last=2049;request.method="median";
  count={};count.cancellation=std::make_shared<CancellationToken>();
  int inner_polls=0;
  count.checkpoint=[&]{
    if(long_source->regions==2050 && ++inner_polls==8) count.cancellation->cancel();
  };
  reject<OperationCancelled>([&]{project_imagej_dataset(median_source,request,count);});
  check(inner_polls==8 && long_source->regions==2050 && long_source->live()==0);
  // Geometry preserves the voxel index of physical zero, including rotations;
  // it does not rebase the origin to the first contributing voxel.
  SpatialCalibration affine;
  affine.spacing={.5,2,3};affine.origin={4,-6,9};
  affine.direction={0,0,1,1,0,0,0,1,0};
  const auto zero=affine.world_to_voxel({0,0,0});
  request={};request.first=1;request.last=4;request.group_size=2;
  const auto grouped=describe_imagej_dataset_projection(source.shape(),source.pixel_type(),affine,request);
  const auto grouped_zero=grouped.calibration.world_to_voxel({0,0,0});
  for(int i=0;i<3;++i) check(std::abs(zero[i]-grouped_zero[i])<1e-12);
  // Finite double-backed intermediates round to the declared Float32 source
  // type exactly once at this profile's typed ingestion boundary.
  Image fractional(1,1,1,PixelType::Float32);fractional.pixels={1.0/3};
  const auto fractional_source=Dataset::from_planes({1,1,2,1,1,1},{fractional,fractional});
  request={};request.last=1;request.method="sum";
  const auto quantized=project_imagej_dataset(fractional_source,request);
  check(quantized.dataset.plane()->pixels[0]==double(float(float(1.0/3)+float(1.0/3))));
  Image old_plane(1,1,1,PixelType::UInt16);old_plane.pixels={2};
  Image new_plane(1,1,1,PixelType::UInt16);new_plane.pixels={100};
  auto aliased=Dataset::from_planes({1,1,2,1,1,1},{old_plane,old_plane});
  const auto replacement=Dataset::from_planes({1,1,2,1,1,1},{new_plane,new_plane});
  count={};count.progress=[&](std::size_t,std::size_t){aliased=replacement;};
  const auto original_snapshot=project_imagej_dataset(aliased,request,count);
  check(original_snapshot.dataset.plane()->pixels[0]==4);
  check(aliased.plane()->pixels[0]==100);
  auto forged=layout;forged.source_first=std::numeric_limits<int>::max();
  reject<std::out_of_range>([&]{forged.contributor({0,0,0},1);});
  std::cout<<std::size(cases)<<" pinned dataset cases x2 tiles: "<<compared
           <<" samples, "<<nan_results<<" NaNs by class; bounded provider/control checks passed\n";
}
