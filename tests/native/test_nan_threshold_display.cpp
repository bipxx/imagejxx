#include "manual_threshold_display_internal.hpp"
#include <imagejxx/operations.hpp>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace imagejxx;
namespace {
std::size_t checks=0;
void check(bool v,const char*m){++checks;if(!v)throw std::runtime_error(m);}
constexpr auto policy=FloatingSamplePolicy::PreserveQuietNaNFloat32;
template<class F>void reject_unchanged(Image &rgb,F f){auto before=rgb.pixels;bool caught=false;try{f();}catch(const std::invalid_argument&){caught=true;}check(caught,"Invalid threshold display source admitted");check(rgb.pixels==before,"Invalid threshold overlay partially mutated display");}
void overlays(){
 Image source(6,1,1,PixelType::Float32),rgb(6,1,3,PixelType::UInt8);rgb.pixels.assign(18,40.);
 source.pixels={float32_source_sample(0xffc12345u,policy),5.,7.,float32_source_sample(0x7fc00001u,policy),10.,-0.};
 const auto before=source.pixels;
 detail::blend_manual_threshold_source_overlay(source,rgb,{4.,8.},policy);
 for(std::size_t i=0;i<6;++i){bool selected=i==1||i==2;check(rgb.pixels[i*3]==(selected?255.:40.),"NaN or finite threshold membership differs");check(rgb.pixels[i*3+1]==(selected?20.:40.) && rgb.pixels[i*3+2]==(selected?20.:40.),"Threshold tint differs");check(float32_source_sample_bits(source.pixels[i],policy)==float32_source_sample_bits(before[i],policy),"Read-only overlay changed source bits");}
 rgb.pixels.assign(18,40.);detail::blend_manual_threshold_source_overlay(source,rgb,{0.,0.},policy);
 check(rgb.pixels[15]==255 && rgb.pixels[0]==40 && rgb.pixels[9]==40,"Signed-zero selection or NaN exclusion differs");
 reject_unchanged(rgb,[&]{blend_manual_threshold_overlay(source,rgb,{4.,8.});});
 reject_unchanged(rgb,[&]{manual_threshold_mask(source,{4.,8.});});
 reject_unchanged(rgb,[&]{analyze_manual_threshold(source,{4.,8.});});
 reject_unchanged(rgb,[&]{manual_threshold_selection(source,{4.,8.});});
 for(double value:{std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),0.1}){
  auto invalid=source;invalid.pixels[0]=value;reject_unchanged(rgb,[&]{detail::blend_manual_threshold_source_overlay(invalid,rgb,{4.,8.},policy);});
 }
 auto invalid=source;std::uint64_t signaling=0x7ff0000000000001ULL;std::memcpy(&invalid.pixels[0],&signaling,8);
 reject_unchanged(rgb,[&]{detail::blend_manual_threshold_source_overlay(invalid,rgb,{4.,8.},policy);});
 reject_unchanged(rgb,[&]{detail::blend_manual_threshold_source_overlay(source,rgb,{4.,8.},static_cast<FloatingSamplePolicy>(99));});
 for(auto type:{PixelType::UInt8,PixelType::Float64}){Image typed(6,1,1,type);reject_unchanged(rgb,[&]{detail::blend_manual_threshold_source_overlay(typed,rgb,{4.,8.},policy);});}
 auto bad_rgb=rgb;bad_rgb.pixels[0]=0.5;reject_unchanged(bad_rgb,[&]{detail::blend_manual_threshold_source_overlay(source,bad_rgb,{4.,8.},policy);});
 ExecutionControl control;control.cancellation=std::make_shared<CancellationToken>();control.cancellation->cancel();bool cancelled=false;
 try{detail::blend_manual_threshold_source_overlay(source,rgb,{4.,8.},policy,{},control);}catch(const OperationCancelled&){cancelled=true;}check(cancelled,"Quiet-NaN overlay ignored cancellation");
}
}
int main(){try{overlays();std::cout<<checks<<" quiet-NaN threshold display checks passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
