#include <imagejxx/indexed_color.hpp>
#include "kernel_control.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace imagejxx {
namespace {
constexpr int bins_count=32768;
int bin(int r,int g,int b){return (r>>3)|((g>>3)<<5)|((b>>3)<<10);}
int origin(int value,int component){return ((value>>(5*component))&31)*8;}
int rounded(double value){return int(std::floor(value+.5));}
struct Cube {
  int lo=0,hi=0,level=0;
  std::uint64_t count=0;
  std::array<int,3> low{},high{};
};
struct Quantizer {
  std::vector<std::uint64_t> counts,sum_r,sum_g,sum_b;
  std::vector<int> bins;
  std::vector<std::uint8_t> inverse;
  std::array<Cube,256> cubes{};
  RgbLookupTable palette{};
  int colors=1;
  Quantizer():counts(bins_count),sum_r(bins_count),sum_g(bins_count),sum_b(bins_count),inverse(bins_count){}
  void shrink(Cube &cube){
    cube.low={255,255,255};cube.high={0,0,0};
    for(int i=cube.lo;i<=cube.hi;++i){kernel_checkpoint_periodic();for(int c=0;c<3;++c){const int v=origin(bins[i],c);cube.low[c]=std::min(cube.low[c],v);cube.high[c]=std::max(cube.high[c],v);}}
  }
  void histogram(const Stack &input){
    for(const auto &image:input)for(std::size_t p=0;p<image.pixels.size();p+=3){
      kernel_checkpoint_periodic();int rgb[3];for(int c=0;c<3;++c){const double value=image.pixels[p+c];if(!std::isfinite(value)||value<0||value>255||value!=std::floor(value))throw std::invalid_argument("Indexed color requires canonical finite UInt8 samples");rgb[c]=int(value);}
      const int i=bin(rgb[0],rgb[1],rgb[2]);++counts[i];sum_r[i]+=rgb[0]&7;sum_g[i]+=rgb[1]&7;sum_b[i]+=rgb[2]&7;
    }
    bins.reserve(bins_count);auto &first=cubes[0];
    for(int i=0;i<bins_count;++i){kernel_checkpoint_periodic();if(counts[i]){bins.push_back(i);first.count+=counts[i];}}
    first.hi=int(bins.size())-1;shrink(first);
  }
  void partition(int maximum){
    while(colors<maximum){kernel_checkpoint();int selected=-1,level=255;
      for(int i=0;i<colors;++i)if(cubes[i].lo!=cubes[i].hi && cubes[i].level<level){level=cubes[i].level;selected=i;}
      if(selected<0)break;
      const auto cube=cubes[selected];int dimension=0;
      for(int c=1;c<3;++c)if(cube.high[c]-cube.low[c]>=cube.high[dimension]-cube.low[dimension])dimension=c;
      const auto key=[dimension](int b){if(dimension==0)return ((b&31)<<10)|(b>>5);if(dimension==1)return (((b>>5)&31)<<10)|((b>>10)<<5)|(b&31);return b;};
      std::sort(bins.begin()+cube.lo,bins.begin()+cube.hi+1,[&](int a,int b){return key(a)<key(b);});
      int median=cube.lo;std::uint64_t count=0;
      while(median<cube.hi && count<cube.count/2){kernel_checkpoint_periodic();count+=counts[bins[median++]];}
      Cube left;left.lo=cube.lo;left.hi=median-1;left.level=cube.level+1;left.count=count;shrink(left);
      Cube right;right.lo=median;right.hi=cube.hi;right.level=cube.level+1;right.count=cube.count-count;shrink(right);
      cubes[selected]=left;cubes[colors++]=right;
    }
  }
  void initial_map(){
    for(int k=0;k<colors;++k){kernel_checkpoint();std::array<std::uint64_t,3> sum{};const auto &cube=cubes[k];
      for(int j=cube.lo;j<=cube.hi;++j){const int i=bins[j];kernel_checkpoint_periodic();sum[0]+=origin(i,0)*counts[i]+sum_r[i];sum[1]+=origin(i,1)*counts[i]+sum_g[i];sum[2]+=origin(i,2)*counts[i]+sum_b[i];inverse[i]=std::uint8_t(k);}
      for(int c=0;c<3;++c)palette[k][c]=std::uint8_t(rounded(double(sum[c])/double(cube.count)));
    }
  }
  bool refine(){
    bool changed=false;std::array<bool,256> color_changed{},unused{};std::vector<double> distance(bins_count);
    for(int i=0;i<bins_count;++i){kernel_checkpoint_periodic();if(!counts[i])continue;
      // The reference uses integer division for offsets at this stage, unlike
      // the fractional centroid used when replacing an unused palette row.
      const double r=origin(i,0)+sum_r[i]/counts[i],g=origin(i,1)+sum_g[i]/counts[i],b=origin(i,2)+sum_b[i]/counts[i];
      int nearest=-1;double best=std::numeric_limits<double>::max();
      for(int k=0;k<colors;++k){const auto &rgb=palette[k];if(i==bin(rgb[0],rgb[1],rgb[2])){best=0;nearest=k;inverse[i]=std::uint8_t(k);break;}
        const double dr=r-rgb[0],dg=g-rgb[1],db=b-rgb[2],value=dr*dr+dg*dg+db*db;if(value<best){best=value;nearest=k;}
      }
      distance[i]=best;
      if(nearest!=inverse[i]){changed=true;color_changed[nearest]=true;color_changed[inverse[i]]=true;inverse[i]=std::uint8_t(nearest);}
    }
    if(!changed)return false;
    std::array<std::array<std::uint64_t,3>,256> sums{};std::array<std::uint64_t,256> total{};
    for(int i=0;i<bins_count;++i){kernel_checkpoint_periodic();const auto k=inverse[i];if(color_changed[k]){sums[k][0]+=origin(i,0)*counts[i]+sum_r[i];sums[k][1]+=origin(i,1)*counts[i]+sum_g[i];sums[k][2]+=origin(i,2)*counts[i]+sum_b[i];total[k]+=counts[i];}}
    for(int k=0;k<colors;++k)if(color_changed[k]){if(!total[k])unused[k]=true;else for(int c=0;c<3;++c)palette[k][c]=std::uint8_t(rounded(double(sums[k][c])/double(total[k])));}
    for(int k=0;k<colors;++k)if(unused[k]){
      double worst=0;int selected=-1;for(int i=0;i<bins_count;++i){kernel_checkpoint_periodic();const double badness=distance[i]*double(counts[i]);if(badness>worst){worst=badness;selected=i;}}
      if(selected<0)throw std::invalid_argument("Indexed refinement has no replacement bin");
      const double reciprocal=1./double(counts[selected]);
      palette[k]={std::uint8_t(rounded(origin(selected,0)+double(sum_r[selected])*reciprocal)),std::uint8_t(rounded(origin(selected,1)+double(sum_g[selected])*reciprocal)),std::uint8_t(rounded(origin(selected,2)+double(sum_b[selected])*reciprocal))};
      inverse[selected]=std::uint8_t(k);
    }
    return true;
  }
};
}
std::size_t indexed_color_working_bytes(int width,int height,std::size_t planes){
  constexpr std::size_t fixed=4*1024*1024;const auto limit=std::numeric_limits<std::size_t>::max();
  if(width<=0||height<=0||!planes||std::size_t(width)>std::size_t(std::numeric_limits<int>::max())/std::size_t(height))throw std::invalid_argument("Indexed color dimensions exceed int32 pixel count");
  const auto plane=std::size_t(width)*height;if(planes>std::size_t(std::numeric_limits<int>::max())/plane)throw std::invalid_argument("Indexed global histogram exceeds int32 pixel count");
  const auto pixels=plane*planes;if(pixels>(limit-fixed)/64 || planes>(limit-fixed-pixels*64)/1024)throw std::invalid_argument("Indexed color working-byte overflow");
  return fixed+pixels*64+planes*1024;
}
IndexedColorResult quantize_indexed_color(const Stack &input,int maximum,const ExecutionControl &control,std::size_t cap){
  if(maximum<2||maximum>256||input.empty())throw std::invalid_argument("Indexed color requires planes and 2..256 colors");
  if(control.cancellation && control.cancellation->canceled())throw OperationCancelled();
  const auto width=input[0].width,height=input[0].height;
  if(indexed_color_working_bytes(width,height,input.size())>cap)throw std::invalid_argument("Indexed color working-byte limit");
  const auto samples=std::size_t(width)*height*3;
  for(const auto &image:input)if(image.width!=width||image.height!=height||image.channels!=3||image.type!=PixelType::UInt8||image.pixels.size()!=samples)throw std::invalid_argument("Indexed color requires matching UInt8 RGB planes");
  const auto captured=control;const Stack snapshot=input;ExecutionScope scope(captured);kernel_checkpoint();
  Quantizer q;q.histogram(snapshot);execution_progress(1,8);q.partition(maximum);execution_progress(2,8);q.initial_map();
  for(int i=0;i<5;++i){kernel_checkpoint();const bool changed=q.refine();execution_progress(3+i,8);if(!changed)break;}
  IndexedColorResult result;result.palette=q.palette;result.color_count=q.colors;result.indices.reserve(snapshot.size());
  for(const auto &image:snapshot){Image indices(width,height,1,PixelType::UInt8);for(std::size_t p=0;p<indices.pixels.size();++p){kernel_checkpoint_periodic();indices.pixels[p]=q.inverse[bin(int(image.pixels[3*p]),int(image.pixels[3*p+1]),int(image.pixels[3*p+2]))];}result.indices.push_back(std::move(indices));}
  execution_progress(8,8);kernel_checkpoint();return result;
}
} // namespace imagejxx
