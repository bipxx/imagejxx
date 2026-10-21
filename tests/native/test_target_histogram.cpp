#include <imagejxx/target_histogram.hpp>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace imagejxx;
void require(bool ok){if(!ok)throw std::runtime_error("Pooled histogram contract failed");}
struct Source final:PlaneSource {
  bool malformed=false;mutable std::vector<int> seen;
  DatasetShape shape() const override{return {1,1,2,1,1,1};}
  PixelType pixel_type() const override{return PixelType::UInt16;}
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override{seen.push_back(p.z);Image v(1,1,1,PixelType::UInt16);v.pixels[0]=malformed?1.5:p.z+1;return std::make_shared<const Image>(std::move(v));}
};
int main(){
  Stack planes;for(int i=0;i<4;++i){Image v(4,2,1,PixelType::UInt16);for(int p=0;p<8;++p)v.pixels[p]=8*i+p;planes.push_back(std::move(v));}
  DatasetReadObserver reads;Document owner{Dataset::from_planes({1,2,2,2,4,1},std::move(planes)).observe_reads(reads)};
  TargetRequest request;request.scope=TargetScope::Ranges;request.c=AxisRange{0,2,1};request.z=AxisRange{0,2,1};request.roi=RoiPolicy::WholePlane;
  const auto target=resolve_target(owner.snapshot(),request);const auto estimate=estimate_target_histogram(target);
  require(estimate.total_pixels==32 && estimate.plane_count==4 && reads.counts().plane_reads==0);
  DatasetHistogramLimits limits;limits.max_pixels=31;bool refused=false;
  try{accumulate_target_histogram(target,{},limits);}catch(const std::invalid_argument&){refused=true;}
  require(refused && reads.counts().plane_reads==0);
  auto raw=accumulate_target_histogram(target);require(raw.finite_count==32 && raw.mean==15.5 && raw.sum==496 && reads.counts().plane_reads==8);
  require(std::abs(*raw.stddev-std::sqrt(85.25))<1e-12);
  for(int i=0;i<256;++i)require(raw.histogram.counts[i]==(i%8==0?1:0));
  TargetHistogramOptions options;options.binning=HistogramBinning::UniformRange;options.bins=4;options.range=std::array<double,2>{0,32};
  const auto uniform=accumulate_target_histogram(target,options);require(uniform.histogram.counts==std::vector<std::uint64_t>({8,8,8,8}) && reads.counts().plane_reads==12);
  require(target_histogram_csv(uniform)=="bin,lower,upper,count\n0,0,8,8\n1,8,16,8\n2,16,24,8\n3,24,32,8\n");
  auto token=std::make_shared<CancellationToken>();ExecutionControl control;control.cancellation=token;control.progress=[&](std::size_t n,std::size_t){if(n==0)token->cancel();};
  bool canceled=false;try{accumulate_target_histogram(target,options,{},control);}catch(const OperationCancelled&){canceled=true;}require(canceled && reads.counts().plane_reads==12 && owner.undo_steps()==0);
  std::weak_ptr<Source> weak;TargetHistogramResult detached;
  {auto provider=std::make_shared<Source>();weak=provider;Document temporary{Dataset(provider)};TargetRequest all;all.scope=TargetScope::AllZ;all.roi=RoiPolicy::WholePlane;detached=accumulate_target_histogram(resolve_target(temporary.snapshot(),all));}
  require(weak.expired() && detached.finite_count==2 && detached.histogram.counts[0]==1 && detached.histogram.counts[128]==1);
  {
    auto provider=std::make_shared<Source>();provider->malformed=true;DatasetReadObserver boundary;
    Document invalid{Dataset(provider).observe_reads(boundary)};TargetRequest empty;empty.scope=TargetScope::AllZ;empty.roi=RoiPolicy::PositionedRois;
    const auto selected=resolve_target(invalid.snapshot(),empty);estimate_target_histogram(selected);require(boundary.counts().plane_reads==0);
    bool rejected=false;try{accumulate_target_histogram(selected);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected && boundary.counts().plane_reads==1);
  }
  {
    auto provider=std::make_shared<Source>();Document reversed{Dataset(provider)};TargetRequest positions;positions.scope=TargetScope::Positions;positions.roi=RoiPolicy::WholePlane;positions.positions={{0,0,1},{0,0,0}};
    const auto result=accumulate_target_histogram(resolve_target(reversed.snapshot(),positions));
    require(provider->seen==std::vector<int>({1,0,1,0}) && result.mean==1.5);
  }
  std::cout<<"pooled histogram counts, statistics, limits, reads, cancellation and detached ownership passed\n";
}
