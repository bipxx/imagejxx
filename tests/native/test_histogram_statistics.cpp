#include <imagejxx/dataset_histogram.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <locale>
#include <stdexcept>
using namespace imagejxx;
void require(bool ok) { if (!ok) throw std::runtime_error("histogram statistics/CSV check failed"); }
struct Comma : std::numpunct<char> { char do_decimal_point() const override { return ','; } };
int main() {
  Image plane(4,1,1,PixelType::UInt16); plane.pixels={0,1,2,9};
  auto source=Dataset::from_planes({1,1,1,1,4,1},Stack{plane});
  DatasetHistogramRequest request; request.binning=HistogramBinning::UniformRange;
  request.bins=2; request.range=std::array<double,2>{1,3};
  auto result=accumulate_dataset_histogram(source,request);
  require(result.mean==3 && result.sum==12 && std::abs(*result.stddev-std::sqrt(12.5))<1e-14);
  require(result.included_count==2 && result.underflow_count==1 && result.overflow_count==1);
  require(dataset_histogram_csv(result)=="bin,lower,upper,count\n0,1,2,1\n1,2,3,1\n");
  // Caller-owned C++ results may contain arbitrary bins. Formatting must neither
  // silently round large counts nor accept malformed arrays/nonfinite edges.
  result.histogram.counts={9007199254740993ULL};result.histogram.edges={0.125,0.25};
  const auto prior=std::locale();std::locale::global(std::locale(prior,new Comma));
  const auto csv=dataset_histogram_csv(result);std::locale::global(prior);
  require(csv=="bin,lower,upper,count\n0,0.125,0.25,9007199254740993\n");
  for (int variant=0;variant<5;++variant) {
    auto invalid=result;
    if(variant==0)invalid.histogram.counts.clear();
    if(variant==1)invalid.histogram.edges.pop_back();
    if(variant==2)invalid.histogram.edges[1]=-1;
    if(variant==3)invalid.histogram.edges[0]=std::numeric_limits<double>::quiet_NaN();
    if(variant==4)invalid.histogram.edges[1]=std::numeric_limits<double>::infinity();
    bool refused=false;try{dataset_histogram_csv(invalid);}catch(const std::invalid_argument&){refused=true;}
    require(refused);
  }
  request.area_roi=Selection("rectangle",{{0,0},{2,1}});
  result=accumulate_dataset_histogram(source,request);
  require(result.mean==0.5 && result.sum==1 && result.stddev==0.5);
  std::cout<<"histogram statistics and exact CSV passed\n";
}
