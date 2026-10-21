#include <imagejxx/auto_threshold.hpp>
#include "kernel_control.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace imagejxx {
bool is_imagej_threshold_method(const std::string &m) {
  return m=="default" || m=="ij_isodata" || m=="huang" || m=="huang2" ||
         m=="renyi_entropy" || m=="shanbhag";
}
namespace {
using Counts=std::vector<double>;
int ij_isodata(Counts data,bool modified) {
  const int n=static_cast<int>(data.size());
  if(modified) {
    const auto mode=std::max_element(data.begin(),data.end())-data.begin();
    double runner=0;
    for(int i=0;i<n;++i) {kernel_checkpoint_periodic();if(i!=mode)runner=std::max(runner,data[i]);}
    if(runner && data[mode]>2*runner)data[mode]=std::floor(1.5*runner);
  }
  data.front()=data.back()=0;
  int first=0,last=n-1;
  while(first<n-1 && !data[first])++first;
  while(last>0 && !data[last])--last;
  if(first>=last)return n/2;
  // The original moving split, including its round-to-nearest final threshold.
  // Direct accumulation keeps upstream's floating-point evaluation order.
  double result=0;
  int split=first;
  do {
    double low=0,lowCount=0,high=0,highCount=0;
    for(int i=first;i<=split;++i) {kernel_checkpoint_periodic();low+=i*data[i];lowCount+=data[i];}
    for(int i=split+1;i<=last;++i) {kernel_checkpoint_periodic();high+=i*data[i];highCount+=data[i];}
    result=(low/lowCount+high/highCount)/2;
    ++split;
  }while(split+1<=result && split<last-1);
  return static_cast<int>(std::floor(result+.5));
}
int huang(const Counts &data,bool rounded) {
  const int n=static_cast<int>(data.size());
  int first=0,last=n-1;
  while(!data[first])++first;
  while(!data[last])--last;
  if(first==last)return 0;
  Counts low(n),high(n);
  double sum=0,count=0;
  for(int i=first;i<n;++i) {kernel_checkpoint_periodic();sum+=i*data[i];count+=data[i];low[i]=sum/count;}
  sum=count=0;
  for(int i=last;i>0;--i) {kernel_checkpoint_periodic();sum+=i*data[i];count+=data[i];high[i-1]=sum/count;}
  const double span=last-first,term=1/span;
  Counts lookup;
  if(rounded) {
    lookup.resize(last-first+1);
    for(int i=1;i<=last-first;++i) {kernel_checkpoint_periodic();const double u=1/(1+i/span);lookup[i]=-u*std::log(u)-(1-u)*std::log(1-u);}
  }
  double best=std::numeric_limits<double>::max();int threshold=rounded?0:-1;
  KernelStage stage(rounded?"Huang2 thresholds":"Huang thresholds",n);
  for(int t=rounded?first:0;t<(rounded?last+1:n);++t) {
    stage.periodic(t);
    if(t>0 && !data[t])continue; // Same class distributions; strict ties keep the earlier split.
    double entropy=0;
    const double a=rounded?std::floor(low[t]+.5):low[t];
    // At the final occupied bin the upper class is empty and never sampled.
    const double b=rounded?std::floor(high[t]+.5):high[t];
    for(int i=0;i<n;++i) {
      stage.poll();
      const double distance=std::abs(i-(i<=t?a:b));
      if(rounded) {
        if(data[i])entropy+=lookup.at(static_cast<std::size_t>(distance))*data[i];
      }else {
        const double u=1/(1+term*distance);
        if(!(u<1e-6 || u>.999999))entropy+=data[i]*(-u*std::log(u)-(1-u)*std::log(1-u));
      }
    }
    if(entropy<best) {best=entropy;threshold=t;}
  }
  stage.finish();return threshold;
}
int entropy_threshold(const Counts &data,bool shanbhag) {
  const int n=static_cast<int>(data.size());
  const double total=std::accumulate(data.begin(),data.end(),0.0);
  Counts p(n),p1(n),p2(n);
  for(int i=0;i<n;++i) {kernel_checkpoint_periodic();p[i]=data[i]/total;p1[i]=p[i]+(i?p1[i-1]:0);p2[i]=1-p1[i];}
  constexpr double epsilon=2.220446049250313e-16;
  int first=0,last=n-1;
  while(first<n-1 && std::abs(p1[first])<epsilon)++first;
  while(last>first && std::abs(p2[last])<epsilon)--last;
  KernelStage stage(shanbhag?"Shanbhag thresholds":"Renyi thresholds",n);
  if(shanbhag) {
    double best=std::numeric_limits<double>::max();int threshold=-1;
    for(int t=first;t<=last;++t) {
      stage.periodic(t);double back=0,obj=0,term=.5/p1[t];
      for(int i=1;i<=t;++i) {stage.poll();back-=p[i]*std::log(1-term*p1[i-1]);}
      back*=term;term=.5/p2[t];
      for(int i=t+1;i<n;++i) {stage.poll();obj-=p[i]*std::log(1-term*p2[i]);}
      const double score=std::abs(back-obj*term);
      if(score<best) {best=score;threshold=t;}
    }
    stage.finish();return threshold;
  }
  std::array<int,3> stars{};
  // alpha=1, 1/2 and 2; preserve strict maxima and ascending-bin tie behavior.
  for(int pass=0;pass<3;++pass) {
    double best=0;int threshold=0;
    for(int t=first;t<=last;++t) {
      stage.periodic(t);double back=0,obj=0;
      for(int i=0;i<n;++i) {
        stage.poll();const double classP=i<=t?p1[t]:p2[t];double value=0;
        if(pass==0) {if(data[i]) {const double q=p[i]/classP;value=-q*std::log(q);}}
        else if(pass==1)value=std::sqrt(p[i]/classP);
        else value=p[i]*p[i]/(classP*classP);
        (i<=t?back:obj)+=value;
      }
      const double score=pass==0?back+obj:(back*obj>0?std::log(back*obj):0)*(pass==1?2:-1);
      if(score>best) {best=score;threshold=t;}
    }
    stars[pass]=threshold;
  }
  std::sort(stars.begin(),stars.end());
  const auto a=stars[0],b=stars[1],c=stars[2];
  int beta1=1,beta2=2,beta3=1;
  if(b-a<=5 && c-b>5) {beta1=0;beta2=1;beta3=3;}
  else if(b-a>5 && c-b<=5) {beta1=3;beta2=1;beta3=0;}
  const double omega=p1[c]-p1[a];stage.finish();
  return static_cast<int>(a*(p1[a]+.25*omega*beta1)+.25*b*omega*beta2+c*(p2[c]+.25*omega*beta3));
}
}
int auto_threshold_histogram_imagej(const std::vector<std::uint64_t> &counts,const std::string &method) {
  kernel_checkpoint();
  execution_progress(0,1);
  const auto finish=[](int result) {kernel_checkpoint();execution_progress(1,1);kernel_checkpoint();return result;};
  if(!is_imagej_threshold_method(method))throw std::invalid_argument("Unknown ImageJ histogram threshold method");
  if(counts.size()<2 || counts.size()>65536)throw std::invalid_argument("ImageJ threshold requires 2..65536 bins");
  std::uint64_t total=0;int first=-1,last=-1,occupied=0;
  for(std::size_t i=0;i<counts.size();++i) {
    kernel_checkpoint_periodic();
    if(counts[i]>std::uint64_t(INT32_MAX)-total)throw std::invalid_argument("ImageJ histogram total exceeds INT32_MAX");
    total+=counts[i];if(counts[i]) {if(first<0)first=static_cast<int>(i);last=static_cast<int>(i);++occupied;}
  }
  if(!total)throw std::invalid_argument("ImageJ histogram contains no samples");
  const int bilevel=occupied==1?first-1:occupied==2?last-1:-1;
  if(bilevel>=0)return finish(bilevel);
  const int offset=counts.size()>256?first:0;
  const int end=counts.size()>256?last+1:static_cast<int>(counts.size());
  Counts data(counts.begin()+offset,counts.begin()+end);
  int result=method=="default" || method=="ij_isodata"?ij_isodata(std::move(data),method=="default"):
    method=="huang" || method=="huang2"?huang(data,method=="huang2"):entropy_threshold(data,method=="shanbhag");
  return finish((result==-1?0:result)+offset);
}
} // namespace imagejxx
