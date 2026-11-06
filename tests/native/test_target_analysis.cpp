#include <imagejxx/target_analysis.hpp>
#include <imagejxx/manual_threshold.hpp>
#include <cstring>
#include <algorithm>
#include <imagejxx/line_measurement.hpp>
#include <imagejxx/application.hpp>
#include <imagejxx/point_annotations.hpp>
#include <imagejxx/selection_edit.hpp>
#include <imagejxx/selection_interpolation.hpp>
#include <imagejxx/selection_spline.hpp>
#include <imagejxx/imagej_roi_projection.hpp>
#include <imagejxx/roi_manager.hpp>
#include <imagejxx/planar_annotation_command.hpp>
#include <cmath>
#include <limits>
#include <iostream>
using namespace imagejxx;
namespace {
void check_at(bool ok,int line){if(!ok){std::cerr<<"Target analysis contract failed at line "<<line<<std::endl;throw std::runtime_error("Target analysis contract failed");}}
#define check(...) check_at((__VA_ARGS__),__LINE__)
template<class E,class F> void refuses(F f){bool caught=false;try{f();}catch(const E&){caught=true;}check(caught);}
struct Source final:PlaneSource {
  mutable std::vector<PlanePosition> reads;
  DatasetShape shape()const override{return {2,2,3,2,3,1};}
  PixelType pixel_type()const override{return PixelType::UInt8;}
  std::shared_ptr<const Image> read_plane(PlanePosition p)const override{
    reads.push_back(p);Image im(3,2,1,PixelType::UInt8);
    for(int i=0;i<6;++i)im.pixels[i]=60*p.t+20*p.c+5*p.z+i;
    return std::make_shared<const Image>(std::move(im));
  }
};
}
int main() try {
  {
    // Original INVERT_Y observations precede expensive binding/UI links.
    struct Case {int kind,cal,local,mode,fit,count;double y[2],ym,by;};
    const Case cases[]={
      {4,3,0,0,0,1,{0},0,0}, // Original exact-origin positive zero.
      {0,0,0,0,0,1,{2},1.3859649122807016,5},
      {0,0,0,0,1,1,{2},1.3859649122807016,5},
      {0,0,0,1,0,1,{2},1.3859649122807016,5},
      {0,0,0,1,1,1,{2},1.3859649122807016,5},
      {0,0,0,2,0,1,{-2.25},-2.3488700564971752,-0.75},
      {0,0,0,2,1,1,{-2.25},-2.3488700564971752,-0.75},
      {0,0,1,0,0,1,{2},1.3859649122807016,5},
      {0,0,1,0,1,1,{2},1.3859649122807016,5},
      {0,0,1,1,0,1,{2},1.3859649122807016,5},
      {0,0,1,1,1,1,{2},1.3859649122807016,5},
      {0,0,1,2,0,1,{-2.25},-2.3488700564971752,-0.75},
      {0,0,1,2,1,1,{-2.25},-2.3488700564971752,-0.75},
      {0,1,0,0,0,1,{-2.25},-2.557017543859649,-0.75},
      {0,1,0,0,1,1,{-2.25},-2.557017543859649,-0.75},
      {0,1,0,1,0,1,{-2.25},-2.557017543859649,-0.75},
      {0,1,0,1,1,1,{-2.25},-2.557017543859649,-0.75},
      {0,1,0,2,0,1,{2.5},2.1045197740112993,8.5},
      {0,1,0,2,1,1,{2.5},2.1045197740112993,8.5},
      {0,1,1,0,0,1,{-2.25},-2.557017543859649,-0.75},
      {0,1,1,0,1,1,{-2.25},-2.557017543859649,-0.75},
      {0,1,1,1,0,1,{-2.25},-2.557017543859649,-0.75},
      {0,1,1,1,1,1,{-2.25},-2.557017543859649,-0.75},
      {0,1,1,2,0,1,{2.5},2.1045197740112993,8.5},
      {0,1,1,2,1,1,{2.5},2.1045197740112993,8.5},
      {0,2,0,0,0,1,{2.5},1.2719298245614032,8.5},
      {0,2,0,0,1,1,{2.5},1.2719298245614032,8.5},
      {0,2,0,1,0,1,{2.5},1.2719298245614032,8.5},
      {0,2,0,1,1,1,{2.5},1.2719298245614032,8.5},
      {0,2,0,2,0,1,{2},1.8022598870056497,5},
      {0,2,0,2,1,1,{2},1.8022598870056497,5},
      {0,2,1,0,0,1,{2.5},1.2719298245614032,8.5},
      {0,2,1,0,1,1,{2.5},1.2719298245614032,8.5},
      {0,2,1,1,0,1,{2.5},1.2719298245614032,8.5},
      {0,2,1,1,1,1,{2.5},1.2719298245614032,8.5},
      {0,2,1,2,0,1,{2},1.8022598870056497,5},
      {0,2,1,2,1,1,{2},1.8022598870056497,5},
      {1,0,0,0,0,1,{2},1.736842105263158,4},
      {1,0,0,0,1,1,{2},1.736842105263158,4},
      {1,0,0,1,0,1,{2},1.736842105263158,4},
      {1,0,0,1,1,1,{2},1.736842105263158,4},
      {1,0,0,2,0,1,{-2.25},-2.2923728813559325,-1.25},
      {1,0,0,2,1,1,{-2.25},-2.2923728813559325,-1.25},
      {1,0,1,0,0,1,{2},1.736842105263158,4},
      {1,0,1,0,1,1,{2},1.736842105263158,4},
      {1,0,1,1,0,1,{2},1.736842105263158,4},
      {1,0,1,1,1,1,{2},1.736842105263158,4},
      {1,0,1,2,0,1,{-2.25},-2.2923728813559325,-1.25},
      {1,0,1,2,1,1,{-2.25},-2.2923728813559325,-1.25},
      {1,1,0,0,0,1,{-2.25},-2.3815789473684212,-1.25},
      {1,1,0,0,1,1,{-2.25},-2.3815789473684212,-1.25},
      {1,1,0,1,0,1,{-2.25},-2.3815789473684212,-1.25},
      {1,1,0,1,1,1,{-2.25},-2.3815789473684212,-1.25},
      {1,1,0,2,0,1,{2.5},2.3305084745762707,6.5},
      {1,1,0,2,1,1,{2.5},2.3305084745762707,6.5},
      {1,1,1,0,0,1,{-2.25},-2.3815789473684212,-1.25},
      {1,1,1,0,1,1,{-2.25},-2.3815789473684212,-1.25},
      {1,1,1,1,0,1,{-2.25},-2.3815789473684212,-1.25},
      {1,1,1,1,1,1,{-2.25},-2.3815789473684212,-1.25},
      {1,1,1,2,0,1,{2.5},2.3305084745762707,6.5},
      {1,1,1,2,1,1,{2.5},2.3305084745762707,6.5},
      {1,2,0,0,0,1,{2.5},1.9736842105263159,6.5},
      {1,2,0,0,1,1,{2.5},1.9736842105263159,6.5},
      {1,2,0,1,0,1,{2.5},1.9736842105263159,6.5},
      {1,2,0,1,1,1,{2.5},1.9736842105263159,6.5},
      {1,2,0,2,0,1,{2},1.9152542372881354,4},
      {1,2,0,2,1,1,{2},1.9152542372881354,4},
      {1,2,1,0,0,1,{2.5},1.9736842105263159,6.5},
      {1,2,1,0,1,1,{2.5},1.9736842105263159,6.5},
      {1,2,1,1,0,1,{2.5},1.9736842105263159,6.5},
      {1,2,1,1,1,1,{2.5},1.9736842105263159,6.5},
      {1,2,1,2,0,1,{2},1.9152542372881354,4},
      {1,2,1,2,1,1,{2},1.9152542372881354,4},
      {2,0,0,0,0,1,{2},1.7789473684210528,4},
      {2,0,0,0,1,1,{2},1.7789473684210528,4},
      {2,0,0,1,0,1,{2},1.7789473684210528,4},
      {2,0,0,1,1,1,{2},1.7789473684210528,4},
      {2,0,0,2,0,1,{-2.25},-2.285593220338983,-1.25},
      {2,0,0,2,1,1,{-2.25},-2.285593220338983,-1.25},
      {2,0,1,0,0,1,{2},1.7789473684210528,4},
      {2,0,1,0,1,1,{2},1.7789473684210528,4},
      {2,0,1,1,0,1,{2},1.7789473684210528,4},
      {2,0,1,1,1,1,{2},1.7789473684210528,4},
      {2,0,1,2,0,1,{-2.25},-2.285593220338983,-1.25},
      {2,0,1,2,1,1,{-2.25},-2.285593220338983,-1.25},
      {2,1,0,0,0,1,{-2.25},-2.3605263157894738,-1.25},
      {2,1,0,0,1,1,{-2.25},-2.3605263157894738,-1.25},
      {2,1,0,1,0,1,{-2.25},-2.3605263157894738,-1.25},
      {2,1,0,1,1,1,{-2.25},-2.3605263157894738,-1.25},
      {2,1,0,2,0,1,{2.5},2.3576271186440678,6.5},
      {2,1,0,2,1,1,{2.5},2.3576271186440678,6.5},
      {2,1,1,0,0,1,{-2.25},-2.3605263157894738,-1.25},
      {2,1,1,0,1,1,{-2.25},-2.3605263157894738,-1.25},
      {2,1,1,1,0,1,{-2.25},-2.3605263157894738,-1.25},
      {2,1,1,1,1,1,{-2.25},-2.3605263157894738,-1.25},
      {2,1,1,2,0,1,{2.5},2.3576271186440678,6.5},
      {2,1,1,2,1,1,{2.5},2.3576271186440678,6.5},
      {2,2,0,0,0,1,{2.5},2.0578947368421057,6.5},
      {2,2,0,0,1,1,{2.5},2.0578947368421057,6.5},
      {2,2,0,1,0,1,{2.5},2.0578947368421057,6.5},
      {2,2,0,1,1,1,{2.5},2.0578947368421057,6.5},
      {2,2,0,2,0,1,{2},1.9288135593220339,4},
      {2,2,0,2,1,1,{2},1.9288135593220339,4},
      {2,2,1,0,0,1,{2.5},2.0578947368421057,6.5},
      {2,2,1,0,1,1,{2.5},2.0578947368421057,6.5},
      {2,2,1,1,0,1,{2.5},2.0578947368421057,6.5},
      {2,2,1,1,1,1,{2.5},2.0578947368421057,6.5},
      {2,2,1,2,0,1,{2},1.9288135593220339,4},
      {2,2,1,2,1,1,{2},1.9288135593220339,4},
      {3,0,0,0,0,1,{2.5714285714285712},2.3695652173913042,4},
      {3,0,0,0,1,1,{2.5714285714285712},2.3695652173913042,4},
      {3,0,0,1,0,1,{2.5714285714285712},2.3695652173913042,4},
      {3,0,0,1,1,1,{2.5714285714285712},2.3695652173913042,4},
      {3,0,0,2,0,1,{-1.9642857142857144},-1.9936708860759493,-1.25},
      {3,0,0,2,1,1,{-1.9642857142857144},-1.9936708860759493,-1.25},
      {3,0,1,0,0,1,{2.5714285714285712},2.3695652173913042,4},
      {3,0,1,0,1,1,{2.5714285714285712},2.3695652173913042,4},
      {3,0,1,1,0,1,{2.5714285714285712},2.3695652173913042,4},
      {3,0,1,1,1,1,{2.5714285714285712},2.3695652173913042,4},
      {3,0,1,2,0,1,{-1.9642857142857144},-1.9936708860759493,-1.25},
      {3,0,1,2,1,1,{-1.9642857142857144},-1.9936708860759493,-1.25},
      {3,1,0,0,0,1,{-1.9642857142857144},-2.0652173913043477,-1.25},
      {3,1,0,0,1,1,{-1.9642857142857144},-2.0652173913043477,-1.25},
      {3,1,0,1,0,1,{-1.9642857142857144},-2.0652173913043477,-1.25},
      {3,1,0,1,1,1,{-1.9642857142857144},-2.0652173913043477,-1.25},
      {3,1,0,2,0,1,{3.6428571428571423},3.5253164556962027,6.5},
      {3,1,0,2,1,1,{3.6428571428571423},3.5253164556962027,6.5},
      {3,1,1,0,0,1,{-1.9642857142857144},-2.0652173913043477,-1.25},
      {3,1,1,0,1,1,{-1.9642857142857144},-2.0652173913043477,-1.25},
      {3,1,1,1,0,1,{-1.9642857142857144},-2.0652173913043477,-1.25},
      {3,1,1,1,1,1,{-1.9642857142857144},-2.0652173913043477,-1.25},
      {3,1,1,2,0,1,{3.6428571428571423},3.5253164556962027,6.5},
      {3,1,1,2,1,1,{3.6428571428571423},3.5253164556962027,6.5},
      {3,2,0,0,0,1,{3.6428571428571423},3.2391304347826084,6.5},
      {3,2,0,0,1,1,{3.6428571428571423},3.2391304347826084,6.5},
      {3,2,0,1,0,1,{3.6428571428571423},3.2391304347826084,6.5},
      {3,2,0,1,1,1,{3.6428571428571423},3.2391304347826084,6.5},
      {3,2,0,2,0,1,{2.5714285714285712},2.5126582278481013,4},
      {3,2,0,2,1,1,{2.5714285714285712},2.5126582278481013,4},
      {3,2,1,0,0,1,{3.6428571428571423},3.2391304347826084,6.5},
      {3,2,1,0,1,1,{3.6428571428571423},3.2391304347826084,6.5},
      {3,2,1,1,0,1,{3.6428571428571423},3.2391304347826084,6.5},
      {3,2,1,1,1,1,{3.6428571428571423},3.2391304347826084,6.5},
      {3,2,1,2,0,1,{2.5714285714285712},2.5126582278481013,4},
      {3,2,1,2,1,1,{2.5714285714285712},2.5126582278481013,4},
      {4,0,0,0,0,1,{3.75},0,0},
      {4,0,0,0,1,1,{3.75},0,0},
      {4,0,0,1,0,1,{3.75},0,0},
      {4,0,0,1,1,1,{3.75},0,0},
      {4,0,0,2,0,1,{3.75},0,0},
      {4,0,0,2,1,1,{3.75},0,0},
      {4,0,1,0,0,1,{3.75},0,0},
      {4,0,1,0,1,1,{3.75},0,0},
      {4,0,1,1,0,1,{3.75},0,0},
      {4,0,1,1,1,1,{3.75},0,0},
      {4,0,1,2,0,1,{3.75},0,0},
      {4,0,1,2,1,1,{3.75},0,0},
      {4,1,0,0,0,1,{-1.375},0,0},
      {4,1,0,0,1,1,{-1.375},0,0},
      {4,1,0,1,0,1,{-1.375},0,0},
      {4,1,0,1,1,1,{-1.375},0,0},
      {4,1,0,2,0,1,{-1.375},0,0},
      {4,1,0,2,1,1,{-1.375},0,0},
      {4,1,1,0,0,1,{-1.375},0,0},
      {4,1,1,0,1,1,{-1.375},0,0},
      {4,1,1,1,0,1,{-1.375},0,0},
      {4,1,1,1,1,1,{-1.375},0,0},
      {4,1,1,2,0,1,{-1.375},0,0},
      {4,1,1,2,1,1,{-1.375},0,0},
      {4,2,0,0,0,1,{6},0,0},
      {4,2,0,0,1,1,{6},0,0},
      {4,2,0,1,0,1,{6},0,0},
      {4,2,0,1,1,1,{6},0,0},
      {4,2,0,2,0,1,{6},0,0},
      {4,2,0,2,1,1,{6},0,0},
      {4,2,1,0,0,1,{6},0,0},
      {4,2,1,0,1,1,{6},0,0},
      {4,2,1,1,0,1,{6},0,0},
      {4,2,1,1,1,1,{6},0,0},
      {4,2,1,2,0,1,{6},0,0},
      {4,2,1,2,1,1,{6},0,0},
      {5,0,0,0,0,2,{3.5,1.75},0,0},
      {5,0,0,0,1,2,{3.5,1.75},0,0},
      {5,0,0,1,0,2,{3.5,1.75},0,0},
      {5,0,0,1,1,2,{3.5,1.75},0,0},
      {5,0,0,2,0,2,{3.5,1.75},0,0},
      {5,0,0,2,1,2,{3.5,1.75},0,0},
      {5,0,1,0,0,2,{3.5,1.75},0,0},
      {5,0,1,0,1,2,{3.5,1.75},0,0},
      {5,0,1,1,0,2,{3.5,1.75},0,0},
      {5,0,1,1,1,2,{3.5,1.75},0,0},
      {5,0,1,2,0,2,{3.5,1.75},0,0},
      {5,0,1,2,1,2,{3.5,1.75},0,0},
      {5,1,0,0,0,2,{-1.5,-2.375},0,0},
      {5,1,0,0,1,2,{-1.5,-2.375},0,0},
      {5,1,0,1,0,2,{-1.5,-2.375},0,0},
      {5,1,0,1,1,2,{-1.5,-2.375},0,0},
      {5,1,0,2,0,2,{-1.5,-2.375},0,0},
      {5,1,0,2,1,2,{-1.5,-2.375},0,0},
      {5,1,1,0,0,2,{-1.5,-2.375},0,0},
      {5,1,1,0,1,2,{-1.5,-2.375},0,0},
      {5,1,1,1,0,2,{-1.5,-2.375},0,0},
      {5,1,1,1,1,2,{-1.5,-2.375},0,0},
      {5,1,1,2,0,2,{-1.5,-2.375},0,0},
      {5,1,1,2,1,2,{-1.5,-2.375},0,0},
      {5,2,0,0,0,2,{5.5,2},0,0},
      {5,2,0,0,1,2,{5.5,2},0,0},
      {5,2,0,1,0,2,{5.5,2},0,0},
      {5,2,0,1,1,2,{5.5,2},0,0},
      {5,2,0,2,0,2,{5.5,2},0,0},
      {5,2,0,2,1,2,{5.5,2},0,0},
      {5,2,1,0,0,2,{5.5,2},0,0},
      {5,2,1,0,1,2,{5.5,2},0,0},
      {5,2,1,1,0,2,{5.5,2},0,0},
      {5,2,1,1,1,2,{5.5,2},0,0},
      {5,2,1,2,0,2,{5.5,2},0,0},
      {5,2,1,2,1,2,{5.5,2},0,0},
      {6,0,0,0,0,1,{2.625},0,0},
      {6,0,0,0,1,1,{2.625},0,0},
      {6,0,0,1,0,1,{2.625},0,0},
      {6,0,0,1,1,1,{2.625},0,0},
      {6,0,0,2,0,1,{2.625},0,0},
      {6,0,0,2,1,1,{2.625},0,0},
      {6,0,1,0,0,1,{2.625},0,0},
      {6,0,1,0,1,1,{2.625},0,0},
      {6,0,1,1,0,1,{2.625},0,0},
      {6,0,1,1,1,1,{2.625},0,0},
      {6,0,1,2,0,1,{2.625},0,0},
      {6,0,1,2,1,1,{2.625},0,0},
      {6,1,0,0,0,1,{-1.9375},0,0},
      {6,1,0,0,1,1,{-1.9375},0,0},
      {6,1,0,1,0,1,{-1.9375},0,0},
      {6,1,0,1,1,1,{-1.9375},0,0},
      {6,1,0,2,0,1,{-1.9375},0,0},
      {6,1,0,2,1,1,{-1.9375},0,0},
      {6,1,1,0,0,1,{-1.9375},0,0},
      {6,1,1,0,1,1,{-1.9375},0,0},
      {6,1,1,1,0,1,{-1.9375},0,0},
      {6,1,1,1,1,1,{-1.9375},0,0},
      {6,1,1,2,0,1,{-1.9375},0,0},
      {6,1,1,2,1,1,{-1.9375},0,0},
      {6,2,0,0,0,1,{3.75},0,0},
      {6,2,0,0,1,1,{3.75},0,0},
      {6,2,0,1,0,1,{3.75},0,0},
      {6,2,0,1,1,1,{3.75},0,0},
      {6,2,0,2,0,1,{3.75},0,0},
      {6,2,0,2,1,1,{3.75},0,0},
      {6,2,1,0,0,1,{3.75},0,0},
      {6,2,1,0,1,1,{3.75},0,0},
      {6,2,1,1,0,1,{3.75},0,0},
      {6,2,1,1,1,1,{3.75},0,0},
      {6,2,1,2,0,1,{3.75},0,0},
      {6,2,1,2,1,1,{3.75},0,0},
      {7,0,0,0,0,1,{0},0,0},
      {7,0,0,0,1,1,{0},0,0},
      {7,0,0,1,0,1,{0},0,0},
      {7,0,0,1,1,1,{0},0,0},
      {7,0,0,2,0,1,{0},0,0},
      {7,0,0,2,1,1,{0},0,0},
      {7,0,1,0,0,1,{0},0,0},
      {7,0,1,0,1,1,{0},0,0},
      {7,0,1,1,0,1,{0},0,0},
      {7,0,1,1,1,1,{0},0,0},
      {7,0,1,2,0,1,{0},0,0},
      {7,0,1,2,1,1,{0},0,0},
      {7,1,0,0,0,1,{0},0,0},
      {7,1,0,0,1,1,{0},0,0},
      {7,1,0,1,0,1,{0},0,0},
      {7,1,0,1,1,1,{0},0,0},
      {7,1,0,2,0,1,{0},0,0},
      {7,1,0,2,1,1,{0},0,0},
      {7,1,1,0,0,1,{0},0,0},
      {7,1,1,0,1,1,{0},0,0},
      {7,1,1,1,0,1,{0},0,0},
      {7,1,1,1,1,1,{0},0,0},
      {7,1,1,2,0,1,{0},0,0},
      {7,1,1,2,1,1,{0},0,0},
      {7,2,0,0,0,1,{0},0,0},
      {7,2,0,0,1,1,{0},0,0},
      {7,2,0,1,0,1,{0},0,0},
      {7,2,0,1,1,1,{0},0,0},
      {7,2,0,2,0,1,{0},0,0},
      {7,2,0,2,1,1,{0},0,0},
      {7,2,1,0,0,1,{0},0,0},
      {7,2,1,0,1,1,{0},0,0},
      {7,2,1,1,0,1,{0},0,0},
      {7,2,1,1,1,1,{0},0,0},
      {7,2,1,2,0,1,{0},0,0},
      {7,2,1,2,1,1,{0},0,0},
      {8,0,0,0,0,1,{0},0,0},
      {8,0,0,0,1,1,{0},0,0},
      {8,0,0,1,0,1,{0},0,0},
      {8,0,0,1,1,1,{0},0,0},
      {8,0,0,2,0,1,{0},0,0},
      {8,0,0,2,1,1,{0},0,0},
      {8,0,1,0,0,1,{0},0,0},
      {8,0,1,0,1,1,{0},0,0},
      {8,0,1,1,0,1,{0},0,0},
      {8,0,1,1,1,1,{0},0,0},
      {8,0,1,2,0,1,{0},0,0},
      {8,0,1,2,1,1,{0},0,0},
      {8,1,0,0,0,1,{0},0,0},
      {8,1,0,0,1,1,{0},0,0},
      {8,1,0,1,0,1,{0},0,0},
      {8,1,0,1,1,1,{0},0,0},
      {8,1,0,2,0,1,{0},0,0},
      {8,1,0,2,1,1,{0},0,0},
      {8,1,1,0,0,1,{0},0,0},
      {8,1,1,0,1,1,{0},0,0},
      {8,1,1,1,0,1,{0},0,0},
      {8,1,1,1,1,1,{0},0,0},
      {8,1,1,2,0,1,{0},0,0},
      {8,1,1,2,1,1,{0},0,0},
      {8,2,0,0,0,1,{0},0,0},
      {8,2,0,0,1,1,{0},0,0},
      {8,2,0,1,0,1,{0},0,0},
      {8,2,0,1,1,1,{0},0,0},
      {8,2,0,2,0,1,{0},0,0},
      {8,2,0,2,1,1,{0},0,0},
      {8,2,1,0,0,1,{0},0,0},
      {8,2,1,0,1,1,{0},0,0},
      {8,2,1,1,0,1,{0},0,0},
      {8,2,1,1,1,1,{0},0,0},
      {8,2,1,2,0,1,{0},0,0},
      {8,2,1,2,1,1,{0},0,0},
    };
    auto spatial=[](int calibration,bool inverted) {
      SpatialCalibration s;s.unit="um";double ox=0,oy=0;
      if(calibration==1){s.spacing={2,.5,1};ox=3.25;oy=-1.5;}
      if(calibration==2){s.spacing={.75,2,1};oy=4.25;}
      if(calibration==3){s.spacing={1,2,1};oy=1.25;}
      s.origin={-ox*s.spacing[0],(inverted?(oy!=0?oy:5):-oy)*s.spacing[1],0};
      s.direction[4]=inverted?-1:1;return s;
    };
    for(const auto& test:cases) {
      Image pixels(8,6,1,PixelType::UInt8),other=pixels;
      for(int y=0;y<6;++y)for(int x=0;x<8;++x){pixels.at(x,y)=x+2*y+1;other.at(x,y)=x+2*y+21;}
      DatasetReadObserver reads;
      auto owner=std::make_shared<Document>(Dataset::from_stack({pixels},spatial(test.cal,test.local)).observe_reads(reads));
      auto sampled=std::make_shared<Document>(Dataset::from_stack({other},spatial((test.cal+1)%3,!test.local)));
      if(test.kind==1 || test.kind==2)owner->select(Selection(test.kind==1?"rectangle":"ellipse",{{1,1},{7,5}}));
      if(test.kind==3)owner->select(Selection("polygon",{{1,1},{7,1},{3,5}}));
      if(test.kind==4)owner->select(Selection("point",{{2.5,1.25}}));
      if(test.kind==5)owner->select(Selection("multipoint",{{1.25,1.5},{5.5,3.25}}));
      if(test.kind==6)owner->select(Selection("line",{{1.25,1.5},{5.5,3.25}}));
      if(test.kind==7 || test.kind==8)owner->select(Selection(test.kind==7?"polyline":"angle",{{1,1},{5,3},{6,2}}));
      Application app;app.adopt(owner);app.adopt(sampled);app.activate(owner->id);
      TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;options.invert_y=true;
      options.ellipse=test.fit && test.kind<4;options.bounding_rectangle=options.feret=test.kind<4;
      TargetRequest capture;capture.roi=RoiPolicy::WholePlane;
      if(test.mode)options.redirect=app.resolve_target(capture,test.mode==1?owner->id:sampled->id);
      const auto target=app.resolve_target();const auto before=reads.counts().plane_reads;
      const auto command=resolve_target_analysis(target,options);check(reads.counts().plane_reads==before);
      auto result=execute_target_analysis(command);check(result.planes.size()==std::size_t(test.count));
      options.invert_y=false;const auto baseline=execute_target_analysis(resolve_target_analysis(target,options));
      for(std::size_t n=0;n<result.planes.size();++n) {
        const auto& row=result.planes[n];const auto& old=baseline.planes[n];
        if(test.kind<7)check(row.reported_y && std::abs(*row.reported_y-test.y[n])<1e-10);
        else check(!row.reported_y);
        if(row.reported_y && test.y[n]==0)check(!std::signbit(*row.reported_y));
        if(test.kind<4){check(row.reported_center_of_mass_y && std::abs(*row.reported_center_of_mass_y-test.ym)<1e-10);check(row.reported_bounding_y && std::abs(*row.reported_bounding_y-test.by)<1e-10);}
        check(!old.reported_y && !old.reported_center_of_mass_y && !old.reported_bounding_y);
        check(row.point_world==old.point_world && row.midpoint_world==old.midpoint_world && row.centroid_world==old.centroid_world && row.center_of_mass_world==old.center_of_mass_world && row.bounding_origin_world==old.bounding_origin_world);
        check(row.feret_y==old.feret_y && row.feret_angle==old.feret_angle && row.statistics->mean==old.statistics->mean && row.statistics->area==old.statistics->area);
      }
    }
    auto source=std::make_shared<Source>();Application app;auto owner=std::make_shared<Document>(Dataset(source));app.adopt(owner);
    TargetAnalysisOptions options;check(!options.invert_y);options.invert_y=true;
    refuses<std::invalid_argument>([&]{resolve_target_analysis(app.resolve_target(),options);});check(source->reads.empty());
  }
  {
    // Frozen original labels: Unicode/code-unit tails precede binding/UI links.
    struct Case {int layout,mode,kind,rows;const char *title,*other,*name,*slice,*expected;};
    const Case cases[]={
      {2,2,0,1,"\x73\x6f\x75\x72\x63\x65\x22\x74\x69\x74\x6c\x65\x2c\xce\xbb\x0a\x6c\x69\x6e\x65","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65","\x72\x6f\x69",nullptr,"\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65\x3a\x72\x6f\x69\x3a\x38"},
      {2,3,0,1,"\x73\x6f\x75\x72\x63\x65\x22\x74\x69\x74\x6c\x65\x2c\xce\xbb\x0a\x6c\x69\x6e\x65","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65","\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\xf0\x9f\xa7\xaa\x7a\x7a\x7a","\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\xf0\x9f\xa7\xaa","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65\x3a\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x3f\x2e\x2e\x2e\x3a\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x3f"},
      {1,0,4,2,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x68\x65\x6c\x6c\x6f\x2e\x74\x69\x66","\x68\x65\x6c\x6c\x6f"},
      {2,0,4,2,"\x73\x6f\x75\x72\x63\x65\x22\x74\x69\x74\x6c\x65\x2c\xce\xbb\x0a\x6c\x69\x6e\x65","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65","\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\xf0\x9f\xa7\xaa\x7a\x7a\x7a","\x68\x65\x6c\x6c\x6f\x2e\x74\x69\x66","\x73\x6f\x75\x72\x63\x65\x22\x74\x69\x74\x6c\x65\x2c\xce\xbb\x0a\x6c\x69\x6e\x65\x3a\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x3f\x2e\x2e\x2e\x3a\x68\x65\x6c\x6c\x6f"},
      {0,0,0,1,"\x73\x6f\x75\x72\x63\x65\x2e\x74\x69\x66","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65","",nullptr,"\x73\x6f\x75\x72\x63\x65\x2e\x74\x69\x66\x3a"},
      {0,2,0,1,"\x73\x6f\x75\x72\x63\x65\x2e\x74\x69\x66","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65","\x68\x61\x73\x2e\x64\x6f\x74",nullptr,"\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65"},
      {1,0,1,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x68\x65\x6c\x6c\x6f\x2e\x74\x69\x66","\x68\x65\x6c\x6c\x6f"},
      {2,2,0,1,"\x73\x6f\x75\x72\x63\x65\x22\x74\x69\x74\x6c\x65\x2c\xce\xbb\x0a\x6c\x69\x6e\x65","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65","\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89","\x68\x65\x6c\x6c\x6f\x2e\x74\x69\x66","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65\x3a\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\xe6\xb1\x89\x2e\x2e\x2e\x3a\x68\x65\x6c\x6c\x6f"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\x30","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\x30"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xd9\xa0","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xd9\xa0"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xdb\xb0","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xdb\xb0"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xdf\x80","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xdf\x80"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe0\xa5\xa6","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe0\xa5\xa6"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe0\xa7\xa6","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe0\xa7\xa6"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe0\xa9\xa6","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe0\xa9\xa6"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe0\xab\xa6","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe0\xab\xa6"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe0\xad\xa6","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe0\xad\xa6"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe0\xaf\xa6","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe0\xaf\xa6"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe0\xb1\xa6","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe0\xb1\xa6"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe0\xb3\xa6","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe0\xb3\xa6"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe0\xb5\xa6","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe0\xb5\xa6"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe0\xb7\xa6","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe0\xb7\xa6"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe0\xb9\x90","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe0\xb9\x90"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe0\xbb\x90","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe0\xbb\x90"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe0\xbc\xa0","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe0\xbc\xa0"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe1\x81\x80","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe1\x81\x80"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe1\x82\x90","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe1\x82\x90"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe1\x9f\xa0","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe1\x9f\xa0"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe1\xa0\x90","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe1\xa0\x90"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe1\xa5\x86","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe1\xa5\x86"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe1\xa7\x90","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe1\xa7\x90"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe1\xaa\x80","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe1\xaa\x80"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe1\xaa\x90","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe1\xaa\x90"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe1\xad\x90","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe1\xad\x90"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe1\xae\xb0","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe1\xae\xb0"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe1\xb1\x80","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe1\xb1\x80"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe1\xb1\x90","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xe1\xb1\x90"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xea\x98\xa0","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xea\x98\xa0"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xea\xa3\x90","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xea\xa3\x90"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xea\xa4\x80","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xea\xa4\x80"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xea\xa7\x90","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xea\xa7\x90"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xea\xa7\xb0","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xea\xa7\xb0"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xea\xa9\x90","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xea\xa9\x90"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xea\xaf\xb0","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xea\xaf\xb0"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xef\xbc\x90","\x64\x69\x67\x69\x74\x73\x2e\x78\x78\xef\xbc\x90"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x75\x6e\x69\x63\x6f\x64\x65\x2e\xc3\xa4\x62\x63","\x75\x6e\x69\x63\x6f\x64\x65"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x75\x6e\x69\x63\x6f\x64\x65\x2e\xc2\xb2\x62\x63","\x75\x6e\x69\x63\x6f\x64\x65"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x75\x6e\x69\x63\x6f\x64\x65\x2e\x78\x78\xc2\xb2","\x75\x6e\x69\x63\x6f\x64\x65"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x75\x6e\x69\x63\x6f\x64\x65\x2e\x78\xf0\x9d\x9f\x8e","\x75\x6e\x69\x63\x6f\x64\x65"},
      {1,0,0,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x75\x6e\x69\x63\x6f\x64\x65\x2e\xf0\x9d\x9f\x8e\x61","\x75\x6e\x69\x63\x6f\x64\x65"},
      {0,0,5,1,"\x73\x6f\x75\x72\x63\x65\x2e\x74\x69\x66","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x68\x65\x6c\x6c\x6f\x2e\x74\x69\x66","\x73\x6f\x75\x72\x63\x65\x2e\x74\x69\x66"},
      {0,1,5,1,"\x73\x6f\x75\x72\x63\x65\x2e\x74\x69\x66","\x73\x6f\x75\x72\x63\x65\x2e\x74\x69\x66",nullptr,"\x68\x65\x6c\x6c\x6f\x2e\x74\x69\x66","\x73\x6f\x75\x72\x63\x65\x2e\x74\x69\x66"},
      {0,2,5,1,"\x73\x6f\x75\x72\x63\x65\x2e\x74\x69\x66","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x68\x65\x6c\x6c\x6f\x2e\x74\x69\x66","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65"},
      {0,3,5,1,"\x73\x6f\x75\x72\x63\x65\x2e\x74\x69\x66","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x68\x65\x6c\x6c\x6f\x2e\x74\x69\x66","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65"},
      {1,0,5,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x68\x65\x6c\x6c\x6f\x2e\x74\x69\x66","\x68\x65\x6c\x6c\x6f"},
      {1,1,5,1,"","",nullptr,"\x68\x65\x6c\x6c\x6f\x2e\x74\x69\x66","\x68\x65\x6c\x6c\x6f"},
      {1,2,5,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x68\x65\x6c\x6c\x6f\x2e\x74\x69\x66","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65\x3a\x68\x65\x6c\x6c\x6f"},
      {1,3,5,1,"","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x68\x65\x6c\x6c\x6f\x2e\x74\x69\x66","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65\x3a\x68\x65\x6c\x6c\x6f"},
      {2,0,5,1,"\x73\x6f\x75\x72\x63\x65\x22\x74\x69\x74\x6c\x65\x2c\xce\xbb\x0a\x6c\x69\x6e\x65","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x68\x65\x6c\x6c\x6f\x2e\x74\x69\x66","\x73\x6f\x75\x72\x63\x65\x22\x74\x69\x74\x6c\x65\x2c\xce\xbb\x0a\x6c\x69\x6e\x65\x3a\x68\x65\x6c\x6c\x6f"},
      {2,1,5,1,"\x73\x6f\x75\x72\x63\x65\x22\x74\x69\x74\x6c\x65\x2c\xce\xbb\x0a\x6c\x69\x6e\x65","\x73\x6f\x75\x72\x63\x65\x22\x74\x69\x74\x6c\x65\x2c\xce\xbb\x0a\x6c\x69\x6e\x65",nullptr,"\x68\x65\x6c\x6c\x6f\x2e\x74\x69\x66","\x73\x6f\x75\x72\x63\x65\x22\x74\x69\x74\x6c\x65\x2c\xce\xbb\x0a\x6c\x69\x6e\x65\x3a\x68\x65\x6c\x6c\x6f"},
      {2,2,5,1,"\x73\x6f\x75\x72\x63\x65\x22\x74\x69\x74\x6c\x65\x2c\xce\xbb\x0a\x6c\x69\x6e\x65","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x68\x65\x6c\x6c\x6f\x2e\x74\x69\x66","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65\x3a\x68\x65\x6c\x6c\x6f"},
      {2,3,5,1,"\x73\x6f\x75\x72\x63\x65\x22\x74\x69\x74\x6c\x65\x2c\xce\xbb\x0a\x6c\x69\x6e\x65","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65",nullptr,"\x68\x65\x6c\x6c\x6f\x2e\x74\x69\x66","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65\x3a\x68\x65\x6c\x6c\x6f"},
      {3,3,4,2,"\x73\x6f\x75\x72\x63\x65\x22\x74\x69\x74\x6c\x65\x2c\xce\xbb\x0a\x6c\x69\x6e\x65","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65","\x72\x6f\x69","\x68\x65\x6c\x6c\x6f\x2e\x74\x69\x66","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65\x3a\x72\x6f\x69\x3a\x68\x65\x6c\x6c\x6f"},
      {3,2,0,1,"\x73\x6f\x75\x72\x63\x65\x22\x74\x69\x74\x6c\x65\x2c\xce\xbb\x0a\x6c\x69\x6e\x65","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65","\x72\x6f\x69",nullptr,"\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65\x3a\x72\x6f\x69\x3a\x32"},
      {3,0,4,2,"\x73\x6f\x75\x72\x63\x65\x22\x74\x69\x74\x6c\x65\x2c\xce\xbb\x0a\x6c\x69\x6e\x65","\x74\x61\x72\x67\x65\x74\x22\x74\x69\x74\x6c\x65\x2c\xce\xbc\x0a\x6c\x69\x6e\x65","\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\xf0\x9f\xa7\xaa\x7a\x7a\x7a","\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\xf0\x9f\xa7\xaa","\x73\x6f\x75\x72\x63\x65\x22\x74\x69\x74\x6c\x65\x2c\xce\xbb\x0a\x6c\x69\x6e\x65\x3a\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x61\x3f\x2e\x2e\x2e\x3a\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x3f"},
    };
    for(const auto& c:cases)for(int provider=0;provider<5;++provider) {
      const DatasetShape shape{c.layout>=2?2:1,c.layout>=2?2:1,c.layout==0?1:c.layout==1?3:2,3,4,1};
      const PlanePosition position=c.layout==3?PlanePosition{0,1,0}:PlanePosition{shape.t-1,shape.c-1,shape.z-1};DatasetReadObserver reads,otherReads;
      auto create=[](DatasetShape dimensions,const std::string& title,DatasetReadObserver observer) {
        Stack planes;for(std::size_t n=0;n<dimensions.plane_count();++n){Image image(4,3,1,PixelType::UInt8);std::fill(image.pixels.begin(),image.pixels.end(),1);planes.push_back(std::move(image));}
        return std::make_shared<Document>(Dataset::from_planes(dimensions,std::move(planes)).observe_reads(observer),title);
      };
      auto owner=create(shape,c.title,reads);const DatasetShape otherShape=c.mode==3?DatasetShape{1,1,1,3,4,1}:shape;
      auto other=create(otherShape,c.other,otherReads);auto view=owner->snapshot().display();view.t=position.t;view.c=position.c;view.crosshair[2]=position.z;owner->set_display(view);
      auto sampledView=other->snapshot().display();sampledView.t=otherShape.t-1;sampledView.c=otherShape.c-1;sampledView.crosshair[2]=otherShape.z-1;other->set_display(sampledView);
      MetadataArray labels;const bool planes=provider<2 || provider==4;const auto count=planes?shape.plane_count():std::size_t(shape.z);
      for(std::size_t n=0;n<count;++n)labels.emplace_back(std::string("unselected.tif"));
      const auto index=planes?(std::size_t(position.t)*shape.c+position.c)*shape.z+position.z:std::size_t(position.z);
      labels[index]=c.slice?MetadataValue(std::string(c.slice)):MetadataValue(std::monostate{});
      auto edit=owner->begin_edit();const auto key=planes?"plane_labels":"slice_labels";
      if(provider==4){std::vector<std::string> legacy;for(const auto& value:labels)legacy.push_back(std::holds_alternative<std::monostate>(value)?"":std::get<std::string>(value));edit.set_property(key,legacy);}
      else if(provider==1 || provider==3)edit.set_extension(key,metadata_array(std::move(labels)));else edit.set_property(key,metadata_array(std::move(labels)));owner->commit(edit);
      if(c.kind!=5) {
        Selection selection=c.kind==1?Selection("line",{{0,0},{2,1}}):c.kind==2?Selection("angle",{{0,0},{1,1},{2,0}}):c.kind==3?Selection("point",{{1,1}}):c.kind==4?Selection("multipoint",{{1,1},{2,1}}):Selection("rectangle",{{0,0},{3,2}});
        if(c.kind==4)selection=with_point_annotations(selection,{{0,PlanePosition{0,0,0}},{0,PlanePosition{shape.t-1,shape.c-1,shape.z-1}}});
        if(c.name)selection.attributes["imagej_roi_name"]=std::string(c.name);owner->select(selection);
      }
      owner->mark_saved();const auto before=owner->snapshot();const auto history=owner->undo_steps();Application app;app.adopt(owner);app.adopt(other);app.activate(owner->id);
      TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;options.display_label=true;
      TargetRequest capture;capture.roi=RoiPolicy::WholePlane;
      if(c.mode)options.redirect=app.resolve_target(capture,c.mode==1?owner->id:other->id);
      const auto command=resolve_target_analysis(app.resolve_target(),options);check(reads.counts().plane_reads==0 && otherReads.counts().plane_reads==0);
      const auto result=execute_target_analysis(command);check(result.planes.size()==std::size_t(c.rows));
      for(const auto& row:result.planes)check(row.label && *row.label==c.expected);
      check(owner->snapshot().shares_data(before) && owner->undo_steps()==history);
      auto renamed=owner->begin_edit();renamed.set_title("changed");owner->commit(renamed);refuses<RevisionConflict>([&]{command.validate_source(owner->snapshot());});
    }
    // Public installed-client coverage for named transient ROI restore.
    Image image(4,3,1,PixelType::UInt8);auto owner=std::make_shared<Document>(Dataset::from_stack({image}),"image");
    Selection geometry("rectangle",{{0,0},{3,2}});geometry.attributes["keep"]=std::string("metadata");owner->select(geometry);
    auto stored=prepare_store_roi(owner->snapshot(),"stored");owner->commit(stored.transaction,stored.display);owner->mark_saved();const auto history=owner->undo_steps();
    auto adoption=prepare_select_roi(owner->snapshot(),0);owner->commit(adoption.transaction,adoption.display);
    check(std::get<std::string>(owner->snapshot().data().selection->attributes.at("imagej_roi_name"))=="stored");
    check(std::get<std::string>(owner->snapshot().data().selection->attributes.at("keep"))=="metadata" && owner->undo_steps()==history && !owner->snapshot().dirty());
    const auto named=owner->snapshot();auto again=prepare_select_roi(named,0);owner->commit(again.transaction,again.display);check(owner->snapshot().shares_data(named));
    Application app;app.adopt(owner);TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;
    check(!execute_target_analysis(resolve_target_analysis(app.resolve_target(),options)).planes[0].label);options.display_label=true;
    const auto command=resolve_target_analysis(app.resolve_target(),options);check(*execute_target_analysis(command).planes[0].label=="image:stored");
    auto small=command.limits();small.max_measurement_work=1;refuses<std::invalid_argument>([&]{resolve_target_analysis(app.resolve_target(),options,small);});
    options.measurement=TargetMeasurementMode::Area;refuses<std::invalid_argument>([&]{resolve_target_analysis(app.resolve_target(),options);});
  }

  {
    // Independently observed ImageJ vectors: ordered powers, not a stable fit.
    auto run=[](PixelType type,const std::vector<double>& values,bool point=false) {
      Image image(int(values.size()),1,1,type);image.pixels=values;
      auto owner=std::make_shared<Document>(Dataset::from_stack({image}));if(point)owner->select(Selection("point",{{0,0}}));
      Application app;app.adopt(owner);TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;
      check(!options.moments);check(!execute_target_analysis(resolve_target_analysis(app.resolve_target(),options)).planes.front().skewness);
      options.moments=true;return execute_target_analysis(resolve_target_analysis(app.resolve_target(),options)).planes.front();
    };
    const auto byte=run(PixelType::UInt8,{0,1,1,4});check(std::abs(*byte.skewness-0.8888888888888888)<1e-12 && std::abs(*byte.kurtosis+0.8148148148148149)<1e-12);
    const auto near=run(PixelType::Float32,{1,1,1.0000001192092896,1});check(*near.skewness==0 && *near.kurtosis==-3);
    const auto huge=run(PixelType::Float32,{1.0000000200408773e20,1.0000000200408773e20,1.0000001080018076e20,1.0000000200408773e20});
    check(*huge.skewness==0 && std::abs((*huge.kurtosis+250199979298363.88)/250199979298363.88)<1e-12);
    const auto single=run(PixelType::Float32,{1.0000000200408773e20},true);check(std::isinf(*single.skewness) && *single.skewness>0 && std::isinf(*single.kurtosis) && *single.kurtosis<0);
    for(bool tiny:{false,true}) {
      auto owner=std::make_shared<Document>(Dataset::from_stack({Image(4,1,1,PixelType::Float32)}));Application app;app.adopt(owner);
      auto view=owner->snapshot().display();const double bound=tiny?std::numeric_limits<double>::denorm_min():0;
      set_manual_threshold_state(view,owner->snapshot().data().dataset.shape(),{{bound,bound},{}});owner->set_display(view);
      TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;options.limit_to_threshold=true;options.moments=true;
      auto row=execute_target_analysis(resolve_target_analysis(app.resolve_target(),options)).planes.front();
      check(std::isnan(*row.skewness) && std::isnan(*row.kurtosis));
      if(tiny)check((*row.center_of_mass_world)[0]==2 && (*row.center_of_mass_world)[1]==.5);else check(std::isnan((*row.center_of_mass_world)[0]));
      options.moments=false;row=execute_target_analysis(resolve_target_analysis(app.resolve_target(),options)).planes.front();check(!row.skewness && !row.kurtosis);
      if(tiny)check((*row.center_of_mass_world)[0]==2);else check(std::isnan((*row.center_of_mass_world)[0]));
      owner->select(Selection("line",{{0,0},{3,0}}));options.moments=true;options.limit_to_threshold=false;
      row=execute_target_analysis(resolve_target_analysis(app.resolve_target(),options)).planes.front();check(*row.skewness==0 && *row.kurtosis==0);
      options.measurement=TargetMeasurementMode::Area;refuses<std::invalid_argument>([&]{resolve_target_analysis(app.resolve_target(),options);});
    }
  }
  {
    Image pixels(5,1,1,PixelType::UInt8);pixels.pixels={0,6,6,7,10};
    auto owner=std::make_shared<Document>(Dataset::from_stack({pixels}));Application app;app.adopt(owner);
    auto view=owner->snapshot().display();set_manual_threshold_state(view,owner->snapshot().data().dataset.shape(),{{6.6,10.6},{}});owner->set_display(view);
    TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;options.area_fraction=true;
    auto frozen=resolve_target_analysis(app.resolve_target(),options);
    auto ordinary=execute_target_analysis(frozen).planes.front();check(ordinary.area_fraction && *ordinary.area_fraction==40 && ordinary.statistics->pixel_count==5);
    options.limit_to_threshold=true;auto limited=execute_target_analysis(resolve_target_analysis(app.resolve_target(),options)).planes.front();
    check(*limited.area_fraction==40 && limited.statistics->pixel_count==4);
    options.area_fraction=false;check(!execute_target_analysis(resolve_target_analysis(app.resolve_target(),options)).planes.front().area_fraction);
    auto changed=owner->snapshot().display();set_manual_threshold_state(changed,owner->snapshot().data().dataset.shape(),{{1,2},{}});owner->set_display(changed);
    refuses<RevisionConflict>([&]{frozen.validate_source(owner->snapshot());});
    owner->select(Selection("point",{{1,0}}));options.area_fraction=true;options.limit_to_threshold=false;
    auto point=execute_target_analysis(resolve_target_analysis(app.resolve_target(),options)).planes.front();check(*point.area_fraction==0 && point.statistics->area==0);
    owner->select(Selection("line",{{0,0},{4,0}}));auto line=resolve_target_analysis(app.resolve_target(),options);
    check(*execute_target_analysis(line).planes.front().area_fraction==0);
    owner->set_display(view);line.validate_source(owner->snapshot());
    options.measurement=TargetMeasurementMode::Area;refuses<std::invalid_argument>([&]{resolve_target_analysis(app.resolve_target(),options);});
  }
  for(double sign:{1.,-1.}) {
    const double max=std::numeric_limits<float>::max();
    // Exact Double inputs independently observed by ImageJ1.54r.
    Image pixels(2,1,1,PixelType::Float32);pixels.pixels={0,sign*max};
    auto owner=std::make_shared<Document>(Dataset::from_stack({pixels}));Application app;app.adopt(owner);
    TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;options.area_fraction=true;
    const double ranges[]={3.402823466385289e38,3.4028235677973362e38,3.4028235677973366e38};
    for(int i=0;i<3;++i) {
      auto view=owner->snapshot().display();set_manual_threshold_state(view,owner->snapshot().data().dataset.shape(),{{sign*ranges[i],sign*ranges[i]},{}});owner->set_display(view);
      const auto row=execute_target_analysis(resolve_target_analysis(app.resolve_target(),options)).planes.front();
      check(row.area_fraction && *row.area_fraction==(i==2?0:50));
    }
  }
  for(int depth:{1,3}) {
    // Source Z belongs to the reported midpoint, sampled Z to the sampler.
    // Valid finite native calibration must not fail due to an unrelated plane.
    auto source=std::make_shared<Document>(Dataset::from_stack(Stack(3,Image(8,6,1,PixelType::UInt8))));
    source->select(Selection("line",{{1,1},{6,4}}));auto view=source->snapshot().display();view.crosshair[2]=2;source->set_display(view);
    SpatialCalibration cal;cal.spacing={1,1,1e308};DatasetReadObserver reads;
    auto sampled=std::make_shared<Document>(Dataset::from_planes({1,1,depth,6,8,1},Stack(depth,Image(8,6,1,PixelType::UInt8)),cal).observe_reads(reads));
    Application app;app.adopt(source);app.adopt(sampled);TargetRequest request;request.roi=RoiPolicy::WholePlane;
    TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;options.redirect=app.resolve_target(request,sampled->id);
    if(depth==3) {
      refuses<std::invalid_argument>([&]{resolve_target_analysis(app.resolve_target({},source->id),options);});check(reads.counts().plane_reads==0);
    }else {
      const auto command=resolve_target_analysis(app.resolve_target({},source->id),options);check(reads.counts().plane_reads==0);
      const auto row=execute_target_analysis(command).planes[0];check(row.sampling_position && row.position.z==2 && row.sampling_position->z==0);
      check(row.midpoint_world && *row.midpoint_world==std::array<double,3>{3.5,2.5,2});
      check(row.statistics->mean==0 && reads.counts().plane_reads==1);
    }
  }
  for(const auto type:{PixelType::UInt8,PixelType::UInt16}) {
    // A sampled lookup belongs once in provenance. Dense UInt16 functions
    // must fit the admitted record without duplicating table_f32_hex.
    const double scale=type==PixelType::UInt16?257:1;Image a(8,6,1,PixelType::UInt8),b(8,6,1,type);
    for(int y=0;y<6;++y)for(int x=0;x<8;++x){a.at(x,y)=x+2*y;b.at(x,y)=scale*(30+3*x+y);}
    auto source=std::make_shared<Document>(Dataset::from_stack({a}));source->select(Selection("rectangle",{{1,1},{6,5}}));
    DatasetReadObserver reads;auto sampled=std::make_shared<Document>(Dataset::from_stack({b}).observe_reads(reads));
    set_imagej_intensity_calibration(*sampled,ImageJIntensityCalibration::function(type,"straight_line",{5,.25},"sampled"));
    auto view=sampled->snapshot().display();set_manual_threshold_state(view,sampled->snapshot().data().dataset.shape(),{{scale*40.4,scale*46.4},{}});sampled->set_display(view);
    Application app;app.adopt(source);app.adopt(sampled);TargetRequest request;request.roi=RoiPolicy::WholePlane;
    TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;options.redirect=app.resolve_target(request,sampled->id);
    options.limit_to_threshold=true;options.shape_descriptors=true;options.bounding_rectangle=true;options.feret=true;
    const auto command=resolve_target_analysis(app.resolve_target({},source->id),options);check(reads.counts().plane_reads==0);
    const auto result=execute_target_analysis(command);check(result.planes[0].statistics->pixel_count==(type==PixelType::UInt16?8:10));
    check(std::abs(result.planes[0].statistics->mean-(type==PixelType::UInt16?2815.9375:15.75))<1e-10 && reads.counts().plane_reads==1);
  }
  {
    // Redirect statistics/calibration/thresholds belong to the sampled owner;
    // area perimeter and captured row identity remain with the source ROI.
    Image a(8,6,1,PixelType::UInt8),b(8,6,1,PixelType::UInt8);
    for(int y=0;y<6;++y)for(int x=0;x<8;++x){a.at(x,y)=x+2*y;b.at(x,y)=30+3*x+y;}
    SpatialCalibration source_cal,sample_cal;source_cal.spacing={2,3,1};source_cal.origin={-1,-3,0};
    sample_cal.spacing={.5,1.25,1};sample_cal.origin={-1,.625,0};
    DatasetReadObserver ar,br;
    auto source=std::make_shared<Document>(Dataset::from_planes({1,1,1,6,8,1},{a},source_cal).observe_reads(ar));
    auto sampled=std::make_shared<Document>(Dataset::from_planes({1,1,1,6,8,1},{b},sample_cal).observe_reads(br));
    source->select(Selection("rectangle",{{1,1},{6,5}}));
    auto av=source->snapshot().display(),bv=sampled->snapshot().display();
    set_manual_threshold_state(av,source->snapshot().data().dataset.shape(),{{6.4,10.4},{}});source->set_display(av);
    set_manual_threshold_state(bv,sampled->snapshot().data().dataset.shape(),{{40.4,46.4},{}});sampled->set_display(bv);
    Application app;app.adopt(source);app.adopt(sampled);TargetRequest capture;capture.roi=RoiPolicy::WholePlane;
    TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;options.limit_to_threshold=true;options.bounding_rectangle=true;
    options.redirect=app.resolve_target(capture,sampled->id);
    check(options.redirect->scope()==TargetScope::Ranges && options.redirect->position_count()==1);
    const auto command=resolve_target_analysis(app.resolve_target({},source->id),options);
    check(ar.counts().plane_reads==0 && br.counts().plane_reads==0);
    const auto before=source->snapshot(),other=sampled->snapshot();const auto result=execute_target_analysis(command);const auto& row=result.planes[0];
    check(row.statistics->pixel_count==10 && row.statistics->area==6.25 && row.statistics->mean==43);
    check(row.perimeter==44 && row.bounding_width==2.5 && row.bounding_height==5);
    check(row.centroid_world && (*row.centroid_world)[0]==1 && (*row.centroid_world)[1]==4.375);
    check(row.threshold_lower==40 && row.threshold_upper==46 && row.sampling_position.has_value());
    check(ar.counts().plane_reads==0 && br.counts().plane_reads==1);
    check(source->snapshot().shares_data(before) && sampled->snapshot().shares_data(other));
    av.zoom=2;av.pan[0]={3,4};set_manual_threshold_state(av,source->snapshot().data().dataset.shape(),{{1,2},{}});source->set_display(av);
    command.validate_source(source->snapshot());bv.zoom=2;sampled->set_display(bv);command.validate_redirect(sampled->snapshot());
    set_manual_threshold_state(bv,sampled->snapshot().data().dataset.shape(),{{1,2},{}});sampled->set_display(bv);
    refuses<RevisionConflict>([&]{command.validate_redirect(sampled->snapshot());});
    check(execute_target_analysis(command).planes[0].statistics->mean==43);
    const auto reads=br.counts().plane_reads;
    refuses<std::invalid_argument>([&]{execute_target_analysis(command,{},command.estimate().working_bytes-1);});
    ExecutionControl cancelled;cancelled.cancellation=std::make_shared<CancellationToken>();cancelled.cancellation->cancel();
    refuses<OperationCancelled>([&]{execute_target_analysis(command,cancelled);});check(br.counts().plane_reads==reads);
    options.measurement=TargetMeasurementMode::Area;
    refuses<std::invalid_argument>([&]{resolve_target_analysis(app.resolve_target({},source->id),options);});
    options.measurement=TargetMeasurementMode::Selection;options.redirect=app.resolve_target({},sampled->id);
    refuses<std::invalid_argument>([&]{resolve_target_analysis(app.resolve_target({},source->id),options);});
    auto small=std::make_shared<Document>(Dataset::from_stack({Image(2,2,1,PixelType::UInt8)}));app.adopt(small);options.redirect=app.resolve_target(capture,small->id);
    refuses<std::invalid_argument>([&]{resolve_target_analysis(app.resolve_target({},source->id),options);});
  }
  {
    // Concrete failing consumer: Short LIMIT compaction uses the processor
    // rectangle, even when the input mask is a full-canvas native allocation.
    Image pixels(8,6,1,PixelType::UInt16);for(int y=0;y<6;++y)for(int x=0;x<8;++x)pixels.at(x,y)=257*(x+2*y);
    DatasetReadObserver reads;auto owner=std::make_shared<Document>(Dataset::from_planes({1,1,1,6,8,1},{pixels}).observe_reads(reads));owner->select(Selection("rectangle",{{1,1},{6,5}}));
    auto display=owner->snapshot().display();set_manual_threshold_state(display,owner->snapshot().data().dataset.shape(),{{25700,28270},{}});owner->set_display(display);Application app;app.adopt(owner);
    TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;options.limit_to_threshold=true;
    const auto row=execute_target_analysis(resolve_target_analysis(app.resolve_target(),options)).planes.front();
    check(row.statistics->histogram16.size()==3342 && row.statistics->pixel_count==0 && row.statistics->minimum==25700 && row.statistics->maximum==3341 && !row.bounding_x && reads.counts().plane_reads==1);
    options.limit_to_threshold=false;const auto original=execute_target_analysis(resolve_target_analysis(app.resolve_target(),options)).planes.front();
    check(original.statistics->histogram16.size()==4370 && original.statistics->pixel_count==20 && !original.threshold_lower && reads.counts().plane_reads==2);
  }
  {
    // Original LIMIT observations, including byte fractional scalar/ellipse
    // membership, Short empty extrema and density-independent fitted centers.
    const double nan=std::numeric_limits<double>::quiet_NaN(),inf=std::numeric_limits<double>::infinity();
    struct ThresholdCase {const char* id;const char* kind;std::vector<std::array<double,2>> points;int type,density,variant;bool limit,active,fitted;std::array<double,2> range;std::array<double,12> expected;int histogram_count;};
    const std::vector<ThresholdCase> cases={
      {"threshold-shape0-type0-range1-density0-variant0-limittrue","whole_image",{},0,0,0,true,true,false,{6.0,10.0},{20.0,8.0,6.0,10.0,8.0,6.0,10.0,3.9,2.8,0.0,0.0,0.0},48},
      {"threshold-shape0-type0-range1-density0-variant1-limittrue","whole_image",{},0,0,1,true,true,false,{6.0,10.0},{20.0,8.0,6.0,10.0,8.0,6.0,10.0,3.9,2.8,9.783961223313058,2.60270766752695,28.396018964837893},48},
      {"threshold-shape1-type0-range1-density0-variant0-limittrue","rectangle",{{1.0,1.0},{6.0,5.0}},0,0,0,true,true,false,{6.0,10.0},{12.0,8.0,6.0,10.0,8.0,6.0,10.0,3.5,3.0,0.0,0.0,0.0},20},
      {"threshold-shape1-type0-range1-density0-variant1-limittrue","rectangle",{{1.0,1.0},{6.0,5.0}},0,0,1,true,true,false,{6.0,10.0},{12.0,8.0,6.0,10.0,8.0,6.0,10.0,3.5,3.0,6.047265563772314,2.526575751584972,31.717474411460998},20},
      {"threshold-shape2-type0-range1-density0-variant0-limittrue","ellipse",{{1.0,1.0},{6.0,5.0}},0,0,0,true,true,false,{6.0,10.0},{10.0,8.0,6.0,10.0,8.0,6.0,10.0,3.5,3.0,0.0,0.0,0.0},16},
      {"threshold-shape2-type0-range1-density0-variant1-limittrue","ellipse",{{1.0,1.0},{6.0,5.0}},0,0,1,true,true,false,{6.0,10.0},{10.0,8.0,6.0,10.0,8.0,6.0,10.0,3.5,3.0,4.910366352979232,2.5929624252224253,26.56505117707799},16},
      {"threshold-shape3-type0-range1-density0-variant0-limittrue","polygon",{{1.0,1.0},{7.0,1.0},{3.0,5.0}},0,0,0,true,true,false,{6.0,10.0},{10.0,7.8,6.0,10.0,8.0,6.0,10.0,4.3,2.5,0.0,0.0,0.0},14},
      {"threshold-shape3-type0-range1-density0-variant1-limittrue","polygon",{{1.0,1.0},{7.0,1.0},{3.0,5.0}},0,0,1,true,true,false,{6.0,10.0},{10.0,7.8,6.0,10.0,8.0,6.0,10.0,4.3,2.5,5.306431931824574,2.399426886264363,25.670095872954917},14},
      {"threshold-shape4-type0-range1-density0-variant0-limittrue","polygon",{{1.0,1.0},{6.0,1.0},{6.0,5.0},{1.0,5.0}},0,0,0,true,true,true,{6.0,10.0},{14.0,8.0,6.0,10.0,8.0,6.0,10.0,3.5,3.0,0.0,0.0,0.0},30},
      {"threshold-shape4-type0-range1-density0-variant1-limittrue","polygon",{{1.0,1.0},{6.0,1.0},{6.0,5.0},{1.0,5.0}},0,0,1,true,true,true,{6.0,10.0},{14.0,8.0,6.0,10.0,8.0,6.0,10.0,3.5,3.0,6.806986176549218,2.6186851513967366,22.24648546954261},30},
      {"threshold-shape5-type0-range1-density0-variant0-limittrue","polygon",{{20.0,20.0},{26.0,20.0},{22.0,24.0}},0,0,0,true,true,false,{6.0,10.0},{0.0,nan,nan,nan,nan,6.0,10.0,nan,nan,0.0,0.0,0.0},0},
      {"threshold-shape5-type0-range1-density0-variant1-limittrue","polygon",{{20.0,20.0},{26.0,20.0},{22.0,24.0}},0,0,1,true,true,false,{6.0,10.0},{0.0,nan,nan,nan,nan,6.0,10.0,0.5,0.5,nan,nan,0.0},0},
      {"threshold-shape6-type0-range1-density0-variant0-limittrue","polygon",{{2.0,2.0},{3.0,2.0},{4.0,2.0}},0,0,0,true,true,false,{6.0,10.0},{0.0,nan,nan,nan,nan,6.0,10.0,nan,nan,0.0,0.0,0.0},0},
      {"threshold-shape6-type0-range1-density0-variant1-limittrue","polygon",{{2.0,2.0},{3.0,2.0},{4.0,2.0}},0,0,1,true,true,false,{6.0,10.0},{0.0,nan,nan,nan,nan,6.0,10.0,2.5,2.5,nan,nan,0.0},0},
      {"threshold-shape0-type0-range0-density0-variant1-limittrue","whole_image",{},0,0,1,true,false,false,{-10.0,100.0},{48.0,8.5,0.0,17.0,9.0,0.0,255.0,4.0,3.0,9.027033336282898,6.770275002212173,0.0},48},
      {"threshold-shape0-type0-range2-density0-variant1-limittrue","whole_image",{},0,0,1,true,true,false,{6.4,10.4},{20.0,8.0,6.0,10.0,8.0,6.0,10.0,4.0,3.0,9.677856909016395,2.1049942055645756,28.76440385457576},48},
      {"threshold-shape0-type0-range3-density0-variant1-limittrue","whole_image",{},0,0,1,true,true,false,{100.0,110.0},{0.0,nan,nan,nan,nan,100.0,110.0,0.5,0.5,nan,nan,0.0},48},
      {"threshold-shape0-type1-range0-density0-variant1-limittrue","whole_image",{},1,0,1,true,false,false,{-2570.0,25700.0},{48.0,2184.5,0.0,4369.0,2313.0,0.0,65535.0,4.0,3.0,9.027033336282898,6.770275002212173,0.0},48},
      {"threshold-shape0-type1-range2-density0-variant1-limittrue","whole_image",{},1,0,1,true,true,false,{1644.8000000000002,2672.8},{16.0,2184.5,1799.0,2570.0,2313.0,1645.0,2673.0,4.0,3.0,9.677856909016395,2.1049942055645756,28.76440385457576},16},
      {"threshold-shape0-type1-range3-density0-variant1-limittrue","whole_image",{},1,0,1,true,true,false,{25700.0,28270.0},{0.0,nan,25700.0,4369.0,nan,25700.0,28270.0,0.5,0.5,nan,nan,0.0},0},
      {"threshold-shape0-type2-range0-density0-variant1-limittrue","whole_image",{},2,0,1,true,false,false,{-4.5,23.0},{48.0,0.125,-2.0,2.25,0.125,-3.4028234663852886e+38,3.4028234663852886e+38,4.0,3.0,9.027033336282898,6.770275002212173,0.0},48},
      {"threshold-shape0-type2-range2-density0-variant1-limittrue","whole_image",{},2,0,1,true,true,false,{-0.3999999999999999,0.6000000000000001},{16.0,0.125,-0.25,0.5,0.125,-0.3999999999999999,0.6000000000000001,4.0,3.0,9.677856909016395,2.1049942055645756,28.76440385457576},16},
      {"threshold-shape0-type2-range3-density0-variant1-limittrue","whole_image",{},2,0,1,true,true,false,{23.0,25.5},{0.0,nan,1.7976931348623157e+308,-1.7976931348623157e+308,nan,23.0,25.5,0.5,0.5,nan,nan,0.0},0},
      {"threshold-shape0-type0-range1-density1-variant1-limittrue","whole_image",{},0,1,1,true,true,false,{6.0,10.0},{20.0,2.0,1.0,3.0,2.0,1.0,3.0,3.9,2.8,9.783961223313058,2.60270766752695,28.396018964837893},48},
      {"threshold-shape0-type0-range1-density2-variant1-limittrue","whole_image",{},0,2,1,true,true,false,{6.0,10.0},{0.0,nan,nan,nan,nan,nan,nan,3.9,2.8,9.783961223313058,2.60270766752695,28.396018964837893},48},
      {"threshold-shape0-type0-range1-density3-variant1-limittrue","whole_image",{},0,3,1,true,true,false,{6.0,10.0},{16.0,8.25,6.0,10.0,8.0,6.0,10.0,3.9,2.8,9.783961223313058,2.60270766752695,28.396018964837893},48},
      {"threshold-shape0-type1-range1-density1-variant1-limittrue","whole_image",{},1,1,1,true,true,false,{1542.0,2570.0},{20.0,1026.0,769.0,1283.0,1026.0,769.0,1283.0,3.9,2.8,9.783961223313058,2.60270766752695,28.396018964837893},20},
      {"threshold-shape0-type1-range1-density2-variant1-limittrue","whole_image",{},1,2,1,true,true,false,{1542.0,2570.0},{20.0,nan,1.7976931348623157e+308,-1.7976931348623157e+308,nan,nan,nan,3.9,2.8,9.783961223313058,2.60270766752695,28.396018964837893},20},
      {"threshold-shape0-type1-range1-density3-variant1-limittrue","whole_image",{},1,3,1,true,true,false,{1542.0,2570.0},{20.0,nan,1542.0,2570.0,2056.0,1542.0,2570.0,3.9,2.8,9.783961223313058,2.60270766752695,28.396018964837893},20},
      {"threshold-shape7-type0-range1-density0-variant0-limittrue","line",{{1.0,1.0},{6.0,4.0}},0,0,0,true,true,false,{6.0,10.0},{nan,8.666666666666666,7.0,10.0,9.0,6.0,10.0,3.5,2.5,nan,nan,nan},-1},
      {"threshold-shape7-type0-range0-density0-variant0-limittrue","line",{{1.0,1.0},{6.0,4.0}},0,0,0,true,false,false,{-10.0,100.0},{nan,8.571428571428571,3.0,14.0,9.0,0.0,255.0,3.5,2.5,nan,nan,nan},-1},
      {"threshold-shape8-type0-range1-density0-variant0-limittrue","polyline",{{1.0,1.0},{6.0,1.0},{6.0,4.0}},0,0,0,true,true,false,{6.0,10.0},{nan,7.75,6.0,10.0,8.0,6.0,10.0,0.0,0.0,nan,nan,nan},-1},
      {"threshold-shape8-type0-range0-density0-variant0-limittrue","polyline",{{1.0,1.0},{6.0,1.0},{6.0,4.0}},0,0,0,true,false,false,{-10.0,100.0},{nan,6.875,3.0,12.0,7.0,0.0,255.0,0.0,0.0,nan,nan,nan},-1},
      {"threshold-shape7-type1-range1-density0-variant0-limittrue","line",{{1.0,1.0},{6.0,4.0}},1,0,0,true,true,false,{1542.0,2570.0},{nan,1949.0,1713.0,2185.0,2185.0,1542.0,2570.0,3.5,2.5,nan,nan,nan},-1},
      {"threshold-shape7-type1-range0-density0-variant0-limittrue","line",{{1.0,1.0},{6.0,4.0}},1,0,0,true,false,false,{-2570.0,25700.0},{nan,2184.5714285714284,771.0,3598.0,2185.0,0.0,65535.0,3.5,2.5,nan,nan,nan},-1},
      {"threshold-shape8-type1-range1-density0-variant0-limittrue","polyline",{{1.0,1.0},{6.0,1.0},{6.0,4.0}},1,0,0,true,true,false,{1542.0,2570.0},{nan,1991.75,1542.0,2570.0,2056.0,1542.0,2570.0,0.0,0.0,nan,nan,nan},-1},
      {"threshold-shape8-type1-range0-density0-variant0-limittrue","polyline",{{1.0,1.0},{6.0,1.0},{6.0,4.0}},1,0,0,true,false,false,{-2570.0,25700.0},{nan,1766.875,771.0,3084.0,1799.0,0.0,65535.0,0.0,0.0,nan,nan,nan},-1},
      {"threshold-shape7-type2-range1-density0-variant0-limittrue","line",{{1.0,1.0},{6.0,4.0}},2,0,0,true,true,false,{-0.5,0.5},{nan,-0.1041666716337204,-0.3333333432674408,0.125,-0.1041666716337204,-0.5,0.5,3.5,2.5,nan,nan,nan},-1},
      {"threshold-shape7-type2-range0-density0-variant0-limittrue","line",{{1.0,1.0},{6.0,4.0}},2,0,0,true,false,false,{-4.5,23.0},{nan,0.12499998722757612,-1.25,1.5,0.125,-3.4028234663852886e+38,3.4028234663852886e+38,3.5,2.5,nan,nan,nan},-1},
      {"threshold-shape8-type2-range1-density0-variant0-limittrue","polyline",{{1.0,1.0},{6.0,1.0},{6.0,4.0}},2,0,0,true,true,false,{-0.5,0.5},{nan,-0.0625,-0.5,0.5,-0.125,-0.5,0.5,0.0,0.0,nan,nan,nan},-1},
      {"threshold-shape8-type2-range0-density0-variant0-limittrue","polyline",{{1.0,1.0},{6.0,1.0},{6.0,4.0}},2,0,0,true,false,false,{-4.5,23.0},{nan,-0.28125,-1.25,1.0,-0.375,-3.4028234663852886e+38,3.4028234663852886e+38,0.0,0.0,nan,nan,nan},-1},
      {"threshold-shape9-type0-range1-density0-variant0-limittrue","angle",{{1.0,1.0},{4.0,4.0},{6.0,1.0}},0,0,0,true,true,false,{6.0,10.0},{nan,0.0,0.0,0.0,0.0,nan,nan,0.0,0.0,nan,nan,nan},-1},
      {"threshold-shape10-type0-range1-density0-variant0-limittrue","point",{{2.0,2.0}},0,0,0,true,true,false,{6.0,10.0},{nan,6.0,6.0,6.0,6.0,6.0,10.0,2.0,2.0,nan,nan,nan},-1},
      {"threshold-shape11-type0-range1-density0-variant0-limittrue","multipoint",{{2.0,2.0},{6.0,4.0}},0,0,0,true,true,false,{6.0,10.0},{nan,6.0,6.0,6.0,6.0,6.0,10.0,2.0,2.0,nan,nan,nan},-1},
      {"threshold-shape0-type0-range1-density0-variant0-limitfalse","whole_image",{},0,0,0,false,true,false,{6.0,10.0},{48.0,8.5,0.0,17.0,9.0,nan,nan,4.0,3.0,0.0,0.0,0.0},48}
    };
    for(const auto& c:cases) {
      const auto type=c.type==0?PixelType::UInt8:c.type==1?PixelType::UInt16:PixelType::Float32;
      Image pixels(8,6,1,type);for(int y=0;y<6;++y)for(int x=0;x<8;++x){const double raw=x+2*y;pixels.at(x,y)=c.type==1?257*raw:c.type==2?double(float(raw*.25-2)):raw;}
      SpatialCalibration spatial;spatial.unit="um";DatasetReadObserver reads;
      auto owner=std::make_shared<Document>(Dataset::from_planes({1,1,1,6,8,1},{pixels},spatial).observe_reads(reads));
      if(std::string(c.kind)!="whole_image"){Selection s(c.kind,c.points);if(c.fitted)s=fit_spline_selection(s,8,6);owner->select(s);}
      auto display=owner->snapshot().display();if(c.active){set_manual_threshold_state(display,{1,1,1,6,8,1},{{c.range[0],c.range[1]},{}});owner->set_display(display);}
      Application app;app.adopt(owner);
      if(c.density) {
        auto density=ImageJIntensityCalibration::uncalibrated();
        if(c.density==1)density=ImageJIntensityCalibration::function(type,"straight_line",{-2,.5},"linear");
        else {
          std::vector<std::uint32_t> words(c.type==0?256:65536,0x7fc00000u);
          if(c.density==3){for(std::size_t i=0;i<words.size();++i){float value=float(i);std::memcpy(&words[i],&value,sizeof(value));}words[c.type==0?7:7*257]=0x7fc00000u;}
          density=ImageJIntensityCalibration::lookup(type,words,c.density==2?"all_nan":"mixed_nan");
        }
        app.set_global_calibration(GlobalCalibration(spatial,density,type));
      }
      TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;check(!options.limit_to_threshold);options.limit_to_threshold=c.limit;options.ellipse=c.variant==1;options.shape_descriptors=c.variant==2;
      const auto command=resolve_target_analysis(app.resolve_target(),options);check(reads.counts().plane_reads==0);command.validate_source(owner->snapshot());
      if(c.limit)check(command.record().at("version")==MetadataValue(std::int64_t(16)));
      options.limit_to_threshold=!c.limit;const auto snapshot=owner->snapshot();const auto result=execute_target_analysis(command);const auto& row=result.planes.front();check(bool(row.statistics));const auto& s=*row.statistics;
      auto same=[&](double a,double b){const bool ok=std::isnan(b)?std::isnan(a):std::isinf(b)?a==b:std::abs(a-b)<=1e-10*std::max(1.,std::abs(b));if(!ok){std::cerr<<c.id<<" actual="<<a<<" expected="<<b<<std::endl;check(false);}};
      if(!std::isnan(c.expected[0]))same(double(s.pixel_count),c.expected[0]);same(s.mean,c.expected[1]);same(s.minimum,c.expected[2]);same(s.maximum,c.expected[3]);same(s.median,c.expected[4]);
      if(c.limit){check(row.threshold_lower && row.threshold_upper);same(*row.threshold_lower,c.expected[5]);same(*row.threshold_upper,c.expected[6]);}else check(!row.threshold_lower && !row.threshold_upper);
      if(row.centroid_world){same((*row.centroid_world)[0],c.expected[7]);same((*row.centroid_world)[1],c.expected[8]);}
      if(c.variant==1){check(row.ellipse_major && row.ellipse_minor && row.ellipse_angle_degrees);same(*row.ellipse_major,c.expected[9]);same(*row.ellipse_minor,c.expected[10]);same(*row.ellipse_angle_degrees,c.expected[11]);}
      if(c.histogram_count>=0){std::uint64_t total=0;for(auto v:s.histogram)total+=v;check(total==std::uint64_t(c.histogram_count));}
      check(owner->snapshot().shares_data(snapshot) && owner->undo_steps()==0 && reads.counts().plane_reads==(std::string(c.kind)=="angle"?0:1));
      const auto before=reads.counts().plane_reads;
      refuses<std::invalid_argument>([&]{execute_target_analysis(command,{},command.estimate().working_bytes-1);});check(reads.counts().plane_reads==before);
      auto token=std::make_shared<CancellationToken>();token->cancel();ExecutionControl control;control.cancellation=token;refuses<OperationCancelled>([&]{execute_target_analysis(command,control);});check(reads.counts().plane_reads==before);
      auto changed=owner->snapshot().display();set_manual_threshold_state(changed,{1,1,1,6,8,1},{{1,2},{}});owner->set_display(changed);
      if(c.limit)refuses<RevisionConflict>([&]{command.validate_source(owner->snapshot());});else command.validate_source(owner->snapshot());
    }
    {
      DatasetReadObserver reads;auto owner=std::make_shared<Document>(Dataset::from_planes({1,1,1,6,8,1},{Image(8,6,1,PixelType::UInt8)}).observe_reads(reads));owner->select(Selection("rectangle",{{0,0},{1e100,10}}));
      auto display=owner->snapshot().display();set_manual_threshold_state(display,{1,1,1,6,8,1},{{0,0},{}});owner->set_display(display);Application app;app.adopt(owner);
      TargetAnalysisOptions options;options.limit_to_threshold=true;refuses<std::invalid_argument>([&]{resolve_target_analysis(app.resolve_target(),options);});check(reads.counts().plane_reads==0);
      options.measurement=TargetMeasurementMode::Selection;options.bounding_rectangle=true;const auto row=execute_target_analysis(resolve_target_analysis(app.resolve_target(),options)).planes.front();check(*row.bounding_width==8 && *row.bounding_height==6 && row.statistics->pixel_count==48 && *row.threshold_lower==0 && reads.counts().plane_reads==1);
    }
  }
  {
    // Pinned original ImageJ observations: ordinary fits, constructor branches,
    // empty processor reset versus retained zero-height mask and one-pixel ties.
    const double nan=std::numeric_limits<double>::quiet_NaN();
    struct EllipseCase {const char* id;const char* kind;std::vector<std::array<double,2>> points;bool fitted,traced;std::array<double,3> axes;std::array<double,2> center;};
    const std::vector<EllipseCase> cases={
      {"shape0-type0-cal0","whole_image",{},false,false,{45.13516668141449,45.13516668141449,0.0},{nan,nan}},
      {"shape1-type0-cal0","rectangle",{{3.0,3.0},{13.0,11.0}},false,false,{11.283791670353622,9.027033336282898,0.0},{nan,nan}},
      {"shape2-type0-cal0","ellipse",{{3.0,3.0},{13.0,11.0}},false,false,{10.299606058404963,7.91169394256132,0.0},{nan,nan}},
      {"shape3-type0-cal0","polygon",{{3.0,3.0},{13.0,3.0},{9.0,11.0}},false,false,{7.855171788226362,6.483573263889882,160.61047496058015},{nan,nan}},
      {"shape4-type0-cal0","polygon",{{3.0,3.0},{13.0,3.0},{13.0,7.0},{8.0,7.0},{8.0,11.0},{3.0,11.0}},false,false,{10.914536113038471,6.999323827684189,29.12642477130444},{nan,nan}},
      {"shape5-type0-cal0","rectangle",{{3.2,4.7},{13.399999999999999,13.0}},false,false,{12.412170837388985,10.15541250331826,0.0},{nan,nan}},
      {"shape6-type0-cal0","ellipse",{{3.2,4.7},{13.399999999999999,13.0}},false,false,{10.932218240412775,9.200870474964098,0.0},{nan,nan}},
      {"shape7-type0-cal0","rectangle",{{-2.7,-1.2},{2.3999999999999995,3.2}},false,false,{4.513516668141449,4.513516668141449,0.0},{nan,nan}},
      {"shape8-type0-cal0","rectangle",{{60.0,60.0},{64.0,64.0}},false,false,{0.0,0.0,0.0},{nan,nan}},
      {"shape9-type0-cal0","ellipse",{{60.0,60.0},{64.0,64.0}},false,false,{0.0,0.0,0.0},{nan,nan}},
      {"shape10-type0-cal0","rectangle",{{3.0,3.0},{4.0,4.0}},false,false,{1.1283791670353622,1.1283791670353622,0.0},{nan,nan}},
      {"shape11-type0-cal0","rectangle",{{3.0,3.0},{4.0,17.0}},false,false,{15.79730833849507,1.1283791670353622,90.0},{nan,nan}},
      {"shape12-type0-cal0","polygon",{{3.0,3.0},{4.0,3.0},{5.0,3.0}},false,false,{nan,nan,0.0},{nan,nan}},
      {"shape13-type0-cal0","polygon",{{3.0,3.0},{13.0,3.0},{13.0,13.0},{3.0,13.0}},true,false,{13.727325035155397,13.727325035155397,0.0},{nan,nan}},
      {"shape14-type0-cal0","freehand",{{3.200000047683716,4.699999809265137},{13.800000190734863,3.0999999046325684},{9.399999618530273,14.199999809265137}},false,true,{9.112088199735224,7.685195031635977,73.21130419733625},{nan,nan}},
      {"extra0-type0-shiftfalse-nanfalse-implicitfalse","whole_image",{},false,false,{45.13516668141449,45.13516668141449,0.0},{20.0,20.0}},
      {"extra1-type0-shiftfalse-nanfalse-implicitfalse","rectangle",{{60.0,60.0},{64.0,64.0}},false,false,{0.0,0.0,0.0},{0.0,0.0}},
      {"extra2-type0-shiftfalse-nanfalse-implicitfalse","ellipse",{{60.0,60.0},{64.0,64.0}},false,false,{0.0,0.0,0.0},{0.0,0.0}},
      {"extra3-type0-shiftfalse-nanfalse-implicitfalse","polygon",{{60.0,60.0},{70.0,60.0},{65.0,68.0}},false,false,{0.0,0.0,0.0},{0.0,0.0}},
      {"extra4-type0-shiftfalse-nanfalse-implicitfalse","polygon",{{3.0,3.0},{4.0,3.0},{5.0,3.0}},false,false,{nan,nan,0.0},{3.5,3.5}},
      {"extra5-type0-shiftfalse-nanfalse-implicitfalse","polygon",{{3.0,3.0},{13.0,3.0},{9.0,11.0}},false,false,{7.855171788226362,6.483573263889882,160.61047496058015},{8.3,5.6}},
      {"extra6-type0-shiftfalse-nanfalse-implicitfalse","ellipse",{{3.0,3.0},{4.0,4.0}},false,false,{1.1283791670955126,1.1283791670955126,0.0},{3.5,3.5}},
      {"extra7-type0-shiftfalse-nanfalse-implicitfalse","polygon",{{3.0999999046325684,3.0999999046325684},{3.200000047683716,3.0999999046325684},{3.1500000953674316,3.200000047683716}},false,false,{nan,nan,0.0},{3.5,3.5}},
      {"extra8-type0-shiftfalse-nanfalse-implicitfalse","rectangle",{{3.0,3.0},{4.0,17.0}},false,false,{15.79730833849507,1.1283791670353622,90.0},{3.5,10.0}}
    };
    for(const auto& value:cases)for(bool implicit:{false,true}) {
      DatasetReadObserver reads;auto owner=std::make_shared<Document>(Dataset::from_planes({1,1,1,40,40,1},{Image(40,40,1,PixelType::UInt8)}).observe_reads(reads));
      if(std::string(value.kind)!="whole_image") {
        Selection s(value.kind,value.points);if(value.traced)s=with_selection_polygon_type(s,SelectionPolygonType::Traced);if(value.fitted)s=fit_spline_selection(s,40,40);owner->select(s);
      }
      Application app;app.adopt(owner);TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;check(!options.ellipse);
      options.ellipse=!implicit;options.shape_descriptors=implicit;
      const auto command=resolve_target_analysis(app.resolve_target(),options);check(reads.counts().plane_reads==0 && command.record().at("version")==MetadataValue(std::int64_t(15)));
      owner->select(Selection("rectangle",{{0,0},{1,1}}));const auto snapshot=owner->snapshot();const auto result=execute_target_analysis(command);const auto& row=result.planes.front();
      auto same=[&](double a,double b){if(!(std::isnan(b)?std::isnan(a):std::abs(a-b)<1e-10)){std::cerr<<value.id<<" implicit="<<implicit<<" actual="<<a<<" expected="<<b<<std::endl;check(false);}};
      if(!implicit){check(row.ellipse_major && row.ellipse_minor && row.ellipse_angle_degrees);same(*row.ellipse_major,value.axes[0]);same(*row.ellipse_minor,value.axes[1]);same(*row.ellipse_angle_degrees,value.axes[2]);check(!row.circularity);}
      else check(!row.ellipse_major && !row.ellipse_minor && !row.ellipse_angle_degrees && row.circularity);
      check(bool(row.centroid_world));if(!std::isnan(value.center[0])){same((*row.centroid_world)[0],value.center[0]);same((*row.centroid_world)[1],value.center[1]);}
      check(reads.counts().plane_reads==1 && owner->snapshot().shares_data(snapshot) && owner->undo_steps()==0);
      TargetAnalysisLimits limits;limits.max_measurement_work=1;refuses<std::invalid_argument>([&]{resolve_target_analysis(command.target(),options,limits);});check(reads.counts().plane_reads==1);
      auto control=ExecutionControl{};control.checkpoint=[] {throw OperationCancelled();};refuses<OperationCancelled>([&]{execute_target_analysis(command,control);});check(reads.counts().plane_reads==1);
    }
    for(const auto* kind:{"rectangle","ellipse","polygon"}) {
      DatasetReadObserver reads;auto owner=std::make_shared<Document>(Dataset::from_planes({1,1,1,40,40,1},{Image(40,40,1,PixelType::UInt8)}).observe_reads(reads));
      owner->select(Selection(kind,std::string(kind)=="polygon"?std::vector<std::array<double,2>>{{0,0},{1e100,0},{0,10}}:std::vector<std::array<double,2>>{{0,0},{1e100,10}}));Application app;app.adopt(owner);
      TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;options.ellipse=true;
      refuses<std::invalid_argument>([&]{resolve_target_analysis(app.resolve_target(),options);});check(reads.counts().plane_reads==0);
    }
    {
      // BBox native coordinates remain wider than the legacy ellipse frame.
      DatasetReadObserver reads;auto owner=std::make_shared<Document>(Dataset::from_planes({1,1,1,40,40,1},{Image(40,40,1,PixelType::UInt8)}).observe_reads(reads));owner->select(Selection("rectangle",{{0,0},{1e100,10}}));Application app;app.adopt(owner);
      TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;options.bounding_rectangle=true;
      const auto row=execute_target_analysis(resolve_target_analysis(app.resolve_target(),options)).planes.front();
      check(*row.bounding_x==0 && *row.bounding_y==0 && *row.bounding_width==40 && *row.bounding_height==10 && reads.counts().plane_reads==1);
      options.ellipse=true;refuses<std::invalid_argument>([&]{resolve_target_analysis(app.resolve_target(),options);});check(reads.counts().plane_reads==1);
    }
    {
      DatasetReadObserver reads;SpatialCalibration spatial;spatial.spacing={2,.5,3};spatial.origin={7,-4,9};spatial.direction={1,1,0,0,1,0,.5,0,1};
      auto owner=std::make_shared<Document>(Dataset::from_planes({1,1,1,40,40,1},{Image(40,40,1,PixelType::UInt8)},spatial).observe_reads(reads));owner->select(Selection("polygon",{{3,3},{13,3},{9,11}}));Application app;app.adopt(owner);
      TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;options.ellipse=true;options.bounding_rectangle=true;options.feret=true;options.shape_descriptors=true;
      const auto row=execute_target_analysis(resolve_target_analysis(app.resolve_target(),options)).planes.front();
      check(*row.ellipse_major==0 && *row.ellipse_minor==0 && std::abs(*row.ellipse_angle_degrees-160.61047496058015)<1e-10);
      const auto center=spatial.voxel_to_world({8.3,5.6,0});for(int a=0;a<3;++a)check(std::abs((*row.centroid_world)[a]-center[a])<1e-12);
      check(row.bounding_x && row.feret && row.circularity && reads.counts().plane_reads==1);
    }
  }
  {
    struct RectangleCase {const char* kind;std::vector<std::array<double,2>> points;bool fitted,traced,invert;std::array<double,2> spacing,origin;std::array<double,4> expected;};
    const std::vector<RectangleCase> cases={
      {"whole_image",{},false,false,false,{1,1},{-0,-0},{0,0,40,40}},
      {"whole_image",{},false,false,false,{2,0.5},{-0,-0},{0,0,80,20}},
      {"whole_image",{},false,false,true,{2,0.5},{-14.5,-1.75},{-14.5,-1.75,80,20}},
      {"whole_image",{},false,false,true,{2,0.5},{-0,19.5},{0,19.5,80,20}},
      {"rectangle",{{3,3},{13,11}},false,false,false,{1,1},{-0,-0},{3,3,10,8}},
      {"rectangle",{{3,3},{13,11}},false,false,false,{2,0.5},{-0,-0},{6,1.5,20,4}},
      {"rectangle",{{3,3},{13,11}},false,false,true,{2,0.5},{-14.5,-1.75},{-8.5,-3.25,20,4}},
      {"rectangle",{{3,3},{13,11}},false,false,true,{2,0.5},{-0,19.5},{6,18,20,4}},
      {"ellipse",{{3,3},{13,11}},false,false,false,{1,1},{-0,-0},{3,3,10,8}},
      {"ellipse",{{3,3},{13,11}},false,false,false,{2,0.5},{-0,-0},{6,1.5,20,4}},
      {"ellipse",{{3,3},{13,11}},false,false,true,{2,0.5},{-14.5,-1.75},{-8.5,-3.25,20,4}},
      {"ellipse",{{3,3},{13,11}},false,false,true,{2,0.5},{-0,19.5},{6,18,20,4}},
      {"polygon",{{3,3},{13,3},{9,11}},false,false,false,{1,1},{-0,-0},{3,3,10,8}},
      {"polygon",{{3,3},{13,3},{9,11}},false,false,false,{2,0.5},{-0,-0},{6,1.5,20,4}},
      {"polygon",{{3,3},{13,3},{9,11}},false,false,true,{2,0.5},{-14.5,-1.75},{-8.5,-3.25,20,4}},
      {"polygon",{{3,3},{13,3},{9,11}},false,false,true,{2,0.5},{-0,19.5},{6,18,20,4}},
      {"polygon",{{3,3},{13,3},{13,7},{8,7},{8,11},{3,11}},false,false,false,{1,1},{-0,-0},{3,3,10,8}},
      {"polygon",{{3,3},{13,3},{13,7},{8,7},{8,11},{3,11}},false,false,false,{2,0.5},{-0,-0},{6,1.5,20,4}},
      {"polygon",{{3,3},{13,3},{13,7},{8,7},{8,11},{3,11}},false,false,true,{2,0.5},{-14.5,-1.75},{-8.5,-3.25,20,4}},
      {"polygon",{{3,3},{13,3},{13,7},{8,7},{8,11},{3,11}},false,false,true,{2,0.5},{-0,19.5},{6,18,20,4}},
      {"rectangle",{{3.2000000000000002,4.7000000000000002},{13.399999999999999,13}},false,false,false,{1,1},{-0,-0},{3,4,11,9}},
      {"rectangle",{{3.2000000000000002,4.7000000000000002},{13.399999999999999,13}},false,false,false,{2,0.5},{-0,-0},{6,2,22,4.5}},
      {"rectangle",{{3.2000000000000002,4.7000000000000002},{13.399999999999999,13}},false,false,true,{2,0.5},{-14.5,-1.75},{-8.5,-3.75,22,4.5}},
      {"rectangle",{{3.2000000000000002,4.7000000000000002},{13.399999999999999,13}},false,false,true,{2,0.5},{-0,19.5},{6,17.5,22,4.5}},
      {"ellipse",{{3.2000000000000002,4.7000000000000002},{13.399999999999999,13}},false,false,false,{1,1},{-0,-0},{3,4,11,9}},
      {"ellipse",{{3.2000000000000002,4.7000000000000002},{13.399999999999999,13}},false,false,false,{2,0.5},{-0,-0},{6,2,22,4.5}},
      {"ellipse",{{3.2000000000000002,4.7000000000000002},{13.399999999999999,13}},false,false,true,{2,0.5},{-14.5,-1.75},{-8.5,-3.75,22,4.5}},
      {"ellipse",{{3.2000000000000002,4.7000000000000002},{13.399999999999999,13}},false,false,true,{2,0.5},{-0,19.5},{6,17.5,22,4.5}},
      {"rectangle",{{-2.7000000000000002,-1.2},{2.3999999999999995,3.2000000000000002}},false,false,false,{1,1},{-0,-0},{0,0,4,4}},
      {"rectangle",{{-2.7000000000000002,-1.2},{2.3999999999999995,3.2000000000000002}},false,false,false,{2,0.5},{-0,-0},{0,0,8,2}},
      {"rectangle",{{-2.7000000000000002,-1.2},{2.3999999999999995,3.2000000000000002}},false,false,true,{2,0.5},{-14.5,-1.75},{-14.5,-1.75,8,2}},
      {"rectangle",{{-2.7000000000000002,-1.2},{2.3999999999999995,3.2000000000000002}},false,false,true,{2,0.5},{-0,19.5},{0,19.5,8,2}},
      {"rectangle",{{60,60},{64,64}},false,false,false,{1,1},{-0,-0},{0,0,0,0}},
      {"rectangle",{{60,60},{64,64}},false,false,false,{2,0.5},{-0,-0},{0,0,0,0}},
      {"rectangle",{{60,60},{64,64}},false,false,true,{2,0.5},{-14.5,-1.75},{-14.5,-1.75,0,0}},
      {"rectangle",{{60,60},{64,64}},false,false,true,{2,0.5},{-0,19.5},{0,19.5,0,0}},
      {"ellipse",{{60,60},{64,64}},false,false,false,{1,1},{-0,-0},{0,0,0,0}},
      {"ellipse",{{60,60},{64,64}},false,false,false,{2,0.5},{-0,-0},{0,0,0,0}},
      {"ellipse",{{60,60},{64,64}},false,false,true,{2,0.5},{-14.5,-1.75},{-14.5,-1.75,0,0}},
      {"ellipse",{{60,60},{64,64}},false,false,true,{2,0.5},{-0,19.5},{0,19.5,0,0}},
      {"rectangle",{{3,3},{4,4}},false,false,false,{1,1},{-0,-0},{3,3,1,1}},
      {"rectangle",{{3,3},{4,4}},false,false,false,{2,0.5},{-0,-0},{6,1.5,2,0.5}},
      {"rectangle",{{3,3},{4,4}},false,false,true,{2,0.5},{-14.5,-1.75},{-8.5,-3.25,2,0.5}},
      {"rectangle",{{3,3},{4,4}},false,false,true,{2,0.5},{-0,19.5},{6,18,2,0.5}},
      {"rectangle",{{3,3},{4,17}},false,false,false,{1,1},{-0,-0},{3,3,1,14}},
      {"rectangle",{{3,3},{4,17}},false,false,false,{2,0.5},{-0,-0},{6,1.5,2,7}},
      {"rectangle",{{3,3},{4,17}},false,false,true,{2,0.5},{-14.5,-1.75},{-8.5,-3.25,2,7}},
      {"rectangle",{{3,3},{4,17}},false,false,true,{2,0.5},{-0,19.5},{6,18,2,7}},
      {"polygon",{{3,3},{4,3},{5,3}},false,false,false,{1,1},{-0,-0},{3,3,2,0}},
      {"polygon",{{3,3},{4,3},{5,3}},false,false,false,{2,0.5},{-0,-0},{6,1.5,4,0}},
      {"polygon",{{3,3},{4,3},{5,3}},false,false,true,{2,0.5},{-14.5,-1.75},{-8.5,-3.25,4,0}},
      {"polygon",{{3,3},{4,3},{5,3}},false,false,true,{2,0.5},{-0,19.5},{6,18,4,0}},
      {"polygon",{{3,3},{13,3},{13,13},{3,13}},true,false,false,{1,1},{-0,-0},{1,1,14,14}},
      {"polygon",{{3,3},{13,3},{13,13},{3,13}},true,false,false,{2,0.5},{-0,-0},{2,0.5,28,7}},
      {"polygon",{{3,3},{13,3},{13,13},{3,13}},true,false,true,{2,0.5},{-14.5,-1.75},{-12.5,-2.25,28,7}},
      {"polygon",{{3,3},{13,3},{13,13},{3,13}},true,false,true,{2,0.5},{-0,19.5},{2,19,28,7}},
      {"freehand",{{3.2000000476837158,4.6999998092651367},{13.800000190734863,3.0999999046325684},{9.3999996185302734,14.199999809265137}},false,true,false,{1,1},{-0,-0},{3,3,11,12}},
      {"freehand",{{3.2000000476837158,4.6999998092651367},{13.800000190734863,3.0999999046325684},{9.3999996185302734,14.199999809265137}},false,true,false,{2,0.5},{-0,-0},{6,1.5,22,6}},
      {"freehand",{{3.2000000476837158,4.6999998092651367},{13.800000190734863,3.0999999046325684},{9.3999996185302734,14.199999809265137}},false,true,true,{2,0.5},{-14.5,-1.75},{-8.5,-3.25,22,6}},
      {"freehand",{{3.2000000476837158,4.6999998092651367},{13.800000190734863,3.0999999046325684},{9.3999996185302734,14.199999809265137}},false,true,true,{2,0.5},{-0,19.5},{6,18,22,6}}
    };
    for(const auto& value:cases) {
      SpatialCalibration spatial;spatial.spacing={value.spacing[0],value.spacing[1],1};spatial.origin={value.origin[0],value.origin[1],0};if(value.invert)spatial.direction[4]=-1;
      DatasetReadObserver reads;auto owner=std::make_shared<Document>(Dataset::from_planes({1,1,1,40,40,1},{Image(40,40,1,PixelType::UInt8)},spatial).observe_reads(reads));
      if(std::string(value.kind)!="whole_image"){Selection selection(value.kind,value.points);if(value.traced)selection=with_selection_polygon_type(selection,SelectionPolygonType::Traced);if(value.fitted)selection=fit_spline_selection(selection,40,40);owner->select(selection);}
      Application app;app.adopt(owner);TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;check(!options.bounding_rectangle);options.bounding_rectangle=true;
      const auto command=resolve_target_analysis(app.resolve_target(),options);check(reads.counts().plane_reads==0 && command.options().bounding_rectangle);
      check(command.record().at("version")==MetadataValue(std::int64_t(14)));
      owner->select(Selection("rectangle",{{0,0},{1,1}}));const auto frozen=owner->snapshot();const auto result=execute_target_analysis(command);const auto& row=result.planes.front();
      check(row.bounding_x && row.bounding_y && row.bounding_width && row.bounding_height && row.bounding_origin_world && row.bounding_horizontal_world && row.bounding_vertical_world);
      const std::array<double,4> actual{*row.bounding_x,*row.bounding_y,*row.bounding_width,*row.bounding_height};
      for(std::size_t i=0;i<4;++i){if(std::abs(actual[i]-value.expected[i])>1e-11)std::cerr<<value.kind<<" rectangle column "<<i<<" got "<<actual[i]<<" expected "<<value.expected[i]<<std::endl;check(std::abs(actual[i]-value.expected[i])<1e-11);}
      if(std::string(value.kind)=="polygon" && value.points.size()==3 && value.points[0][1]==value.points[1][1] && value.points[0][1]==value.points[2][1])check(!row.bounds && *row.bounding_width>0 && *row.bounding_height==0);
      check(!row.feret && !row.circularity && reads.counts().plane_reads==1 && owner->snapshot().shares_data(frozen) && owner->undo_steps()==0);
      TargetAnalysisLimits limits;limits.max_measurement_work=1;refuses<std::invalid_argument>([&]{resolve_target_analysis(command.target(),options,limits);});check(reads.counts().plane_reads==1);
      options.shape_descriptors=true;options.feret=true;const auto combined=execute_target_analysis(resolve_target_analysis(command.target(),options));check(combined.planes.front().bounding_x==row.bounding_x && combined.planes.front().feret && combined.planes.front().circularity);
    }
    {
      SpatialCalibration spatial;spatial.spacing={2,.5,3};spatial.origin={7,-4,9};spatial.direction={1,1,0,0,1,0,.5,0,1};
      DatasetReadObserver reads;auto owner=std::make_shared<Document>(Dataset::from_planes({1,1,2,40,40,1},{Image(40,40,1,PixelType::UInt8),Image(40,40,1,PixelType::UInt8)},spatial).observe_reads(reads));owner->select(Selection("rectangle",{{3,3},{13,11}}));auto display=owner->snapshot().display();display.crosshair[2]=1;owner->set_display(display);Application app;app.adopt(owner);
      TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;options.bounding_rectangle=true;const auto result=execute_target_analysis(resolve_target_analysis(app.resolve_target(),options));const auto& row=result.planes.front();
      check(*row.bounding_origin_world==std::array<double,3>{14.5,-2.5,15} && *row.bounding_horizontal_world==std::array<double,3>{20,0,10} && *row.bounding_vertical_world==std::array<double,3>{4,4,0});
      check(std::abs(*row.bounding_width-std::sqrt(500.))<1e-11 && std::abs(*row.bounding_height-std::sqrt(32.))<1e-11 && row.position.z==1 && reads.counts().plane_reads==1);
    }
    {
      DatasetReadObserver reads;auto owner=std::make_shared<Document>(Dataset::from_stack({Image(40,40,1,PixelType::UInt8)}).observe_reads(reads));owner->select(Selection("polygon",{{0,0},{1e100,0},{0,10}}));Application app;app.adopt(owner);TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;options.bounding_rectangle=true;
      refuses<std::invalid_argument>([&]{resolve_target_analysis(app.resolve_target(),options);});check(reads.counts().plane_reads==0);
    }
  }
  {
    struct FeretCase {const char* kind;std::vector<std::array<double,2>> points;bool fitted,traced;std::array<double,2> spacing;std::array<double,5> expected;};
    const std::vector<FeretCase> cases={
      {"whole_image",{},false,false,{1,1},{56.568542494923804,40,135,0,0}},
      {"whole_image",{},false,false,{2,0.5},{56.568542494923804,40,135,0,0}},
      {"rectangle",{{3,3},{13,11}},false,false,{1,1},{12.806248474865697,8,141.34019174590992,3,3}},
      {"rectangle",{{3,3},{13,11}},false,false,{2,0.5},{20.396078054371138,4,168.69006752597977,3,3}},
      {"ellipse",{{3,3},{13,11}},false,false,{1,1},{10,8,0,3,7}},
      {"ellipse",{{3,3},{13,11}},false,false,{2,0.5},{20,4,0,6,3.5}},
      {"polygon",{{3,3},{13,3},{9,11}},false,false,{1,1},{10,8,126.86989764584402,3,3}},
      {"polygon",{{3,3},{13,3},{9,11}},false,false,{2,0.5},{20,4,0,3,3}},
      {"polygon",{{3,3},{13,3},{13,7},{8,7},{8,11},{3,11}},false,false,{1,1},{12.806248474865697,8,38.659808254090095,3,11}},
      {"polygon",{{3,3},{13,3},{13,7},{8,7},{8,11},{3,11}},false,false,{2,0.5},{20.396078054371138,4,11.309932474020215,3,11}},
      {"rectangle",{{3.2000000000000002,4.7000000000000002},{13.399999999999999,13}},false,false,{1,1},{13.150285140452567,8.3000001907348633,140.86387018601474,3.2000000476837158,4.6999998092651367}},
      {"rectangle",{{3.2000000000000002,4.7000000000000002},{13.399999999999999,13}},false,false,{2,0.5},{20.81784055149777,4.1500000953674316,168.50115063998516,3.2000000476837158,4.6999998092651367}},
      {"ellipse",{{3.2000000000000002,4.7000000000000002},{13.399999999999999,13}},false,false,{1,1},{11,9,0,3,8.5}},
      {"ellipse",{{3.2000000000000002,4.7000000000000002},{13.399999999999999,13}},false,false,{2,0.5},{22,4.5,0,6,4.25}},
      {"rectangle",{{-2.7000000000000002,-1.2},{2.3999999999999995,3.2000000000000002}},false,false,{1,1},{6.7357259987491149,4.4000000953674316,139.21417870338706,-2.7000000476837158,-1.2000000476837158}},
      {"rectangle",{{-2.7000000000000002,-1.2},{2.3999999999999995,3.2000000000000002}},false,false,{2,0.5},{10.43455872443937,2.2000000476837158,167.82854186669013,-2.7000000476837158,-1.2000000476837158}},
      {"rectangle",{{60,60},{64,64}},false,false,{1,1},{5.6568542494923806,4,135,60,60}},
      {"rectangle",{{60,60},{64,64}},false,false,{2,0.5},{8.2462112512353212,2,165.96375653207352,60,60}},
      {"ellipse",{{60,60},{64,64}},false,false,{1,1},{4,4,0,60,62}},
      {"ellipse",{{60,60},{64,64}},false,false,{2,0.5},{8,2,0,120,31}},
      {"rectangle",{{3,3},{4,4}},false,false,{1,1},{1.4142135623730951,1,135,3,3}},
      {"rectangle",{{3,3},{4,4}},false,false,{2,0.5},{2.0615528128088303,0.5,165.96375653207352,3,3}},
      {"rectangle",{{3,3},{4,17}},false,false,{1,1},{14.035668847618199,1,94.08561677997487,3,3}},
      {"rectangle",{{3,3},{4,17}},false,false,{2,0.5},{7.2801098892805181,2,105.94539590092285,3,3}},
      {"polygon",{{3,3},{4,3},{5,3}},false,false,{1,1},{2,0,0,3,3}},
      {"polygon",{{3,3},{4,3},{5,3}},false,false,{2,0.5},{4,0,0,3,3}},
      {"polygon",{{3,3},{13,3},{13,13},{3,13}},true,false,{1,1},{14.139979053786409,13.742882792372066,45.000006148107872,3.0764639377593994,13.074938774108887}},
      {"polygon",{{3,3},{13,3},{13,13},{3,13}},true,false,{2,0.5},{27.487765367547677,6.8715723147404244,179.88599433003395,1.1302516460418701,7.8330926895141602}},
      {"freehand",{{3.2000000476837158,4.6999998092651367},{13.800000190734863,3.0999999046325684},{9.3999996185302734,14.199999809265137}},false,true,{1,1},{11.940268569175016,9.264448738517812,68.376802837290896,9.3999996185302734,14.200000762939453}},
      {"freehand",{{3.2000000476837158,4.6999998092651367},{13.800000190734863,3.0999999046325684},{9.3999996185302734,14.199999809265137}},false,true,{2,0.5},{21.215089730480507,5.214213754450487,2.1610793304011708,3.2000000476837158,4.6999998092651367}}
    };
    for(const auto& value:cases) {
      SpatialCalibration spatial;spatial.spacing={value.spacing[0],value.spacing[1],1};spatial.origin={-14.5,3.5,0};spatial.unit="um";
      DatasetReadObserver reads;auto owner=std::make_shared<Document>(Dataset::from_planes({1,1,1,40,40,1},{Image(40,40,1,PixelType::UInt8)},spatial).observe_reads(reads));
      const bool whole=std::string(value.kind)=="whole_image";
      if(!whole){Selection selection(value.kind,value.points);if(value.traced)selection=with_selection_polygon_type(selection,SelectionPolygonType::Traced);if(value.fitted)selection=fit_spline_selection(selection,40,40);owner->select(selection);}
      Application app;app.adopt(owner);TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;options.feret=true;
      const auto command=resolve_target_analysis(app.resolve_target(),options);check(reads.counts().plane_reads==0 && command.options().feret);
      check(command.record().at("version")==MetadataValue(std::int64_t(13)));
      owner->select(Selection("rectangle",{{0,0},{1,1}}));const auto frozen=owner->snapshot();const auto result=execute_target_analysis(command);const auto& row=result.planes.front();
      check(row.feret && row.min_feret && row.feret_angle && row.feret_x && row.feret_y);
      const std::array<double,5> actual{*row.feret,*row.min_feret,*row.feret_angle,*row.feret_x,*row.feret_y};
      for(std::size_t i=0;i<5;++i){if(std::abs(actual[i]-value.expected[i])>1e-10)std::cerr<<value.kind<<" Feret column "<<i<<" got "<<actual[i]<<" expected "<<value.expected[i]<<std::endl;check(std::abs(actual[i]-value.expected[i])<1e-10);}
      check(row.feret_in_pixels==whole && row.feret_coordinates_scaled==(std::string(value.kind)=="ellipse"));
      check(!row.circularity && reads.counts().plane_reads==1 && owner->snapshot().shares_data(frozen) && owner->undo_steps()==0);
      TargetAnalysisLimits limits;limits.max_measurement_work=1;refuses<std::invalid_argument>([&]{resolve_target_analysis(command.target(),options,limits);});check(reads.counts().plane_reads==1);
      options.shape_descriptors=true;const auto combined=execute_target_analysis(resolve_target_analysis(command.target(),options));
      check(combined.planes.front().feret==row.feret && combined.planes.front().circularity.has_value());
    }
    for(const char* kind:{"rectangle","ellipse"}) {
      DatasetReadObserver reads;auto owner=std::make_shared<Document>(Dataset::from_stack({Image(40,40,1,PixelType::UInt8)}).observe_reads(reads));owner->select(Selection(kind,{{0,0},{1e100,10}}));Application app;app.adopt(owner);
      TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;options.feret=true;
      refuses<std::invalid_argument>([&]{resolve_target_analysis(app.resolve_target(),options);});check(reads.counts().plane_reads==0);
    }
    for(const char* kind:{"rectangle","ellipse"}) {
      SpatialCalibration spatial;spatial.direction={1,1,0,0,1,0,0,0,1};
      DatasetReadObserver reads;auto owner=std::make_shared<Document>(Dataset::from_planes({1,1,1,40,40,1},{Image(40,40,1,PixelType::UInt8)},spatial).observe_reads(reads));owner->select(Selection(kind,{{3,3},{13,11}}));Application app;app.adopt(owner);
      TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;options.feret=true;
      const auto result=execute_target_analysis(resolve_target_analysis(app.resolve_target(),options));const auto& row=result.planes.front();
      if(std::string(kind)=="rectangle"){check(std::abs(*row.feret-std::sqrt(388.))<1e-11);check(std::abs(*row.min_feret-std::sqrt(50.))<1e-11);}
      else {check(std::abs((*row.feret)*(*row.min_feret)-80.)<1e-10);check(*row.feret>10 && *row.min_feret>0);}
      check(reads.counts().plane_reads==1);
    }
  }
  {
    for(const char* kind:{"rectangle","ellipse"}) {
      DatasetReadObserver reads;auto owner=std::make_shared<Document>(Dataset::from_stack({Image(40,40,1,PixelType::UInt8)}).observe_reads(reads));
      owner->select(Selection(kind,{{0,0},{1e100,10}}));Application app;app.adopt(owner);TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;options.shape_descriptors=true;
      refuses<std::invalid_argument>([&]{resolve_target_analysis(app.resolve_target(),options);});check(reads.counts().plane_reads==0);
    }
  }
  {
    const double expected[][4]={{.7757018897752576,1.25,.8000000000852909,1},{.9998303296042818,1.3018205877502111,.7681550049290482,.9696969696969697},{.7144763353520187,1.559370073701773,.6412845910439401,.8571428571428571},{.4363323129985824,0,0,1},{0,0,0,0},{0,0,0,0},{.7775441817634738,1.2222222222222223,.8181818182690476,1.2375},{1,1,1,.9548387096774194}};
    for(int i=0;i<8;++i) {
      SpatialCalibration spatial;spatial.unit="um";if(i==3)spatial.spacing={2,.5,1};
      Selection selection(i==1 || i==4?"ellipse":i==2 || i==5 || i==7?"polygon":"rectangle",{{3,3},{13,11}});
      if(i==2)selection.points={{3,3},{13,3},{13,7},{8,7},{8,11},{3,11}};
      if(i==4)selection.points={{60,60},{64,64}};
      if(i==5)selection.points={{3,3},{4,3},{5,3}};
      if(i==6)selection.points={{3.2,4.7},{13.4,13}};
      if(i==7)selection=fit_spline_selection(Selection("polygon",{{3,3},{13,3},{13,13},{3,13}}),40,40);
      DatasetReadObserver reads;auto owner=std::make_shared<Document>(Dataset::from_planes({1,1,1,40,40,1},{Image(40,40,1,PixelType::UInt8)},spatial).observe_reads(reads));owner->select(selection);Application app;app.adopt(owner);
      TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;options.shape_descriptors=true;
      const auto before=reads.counts().plane_reads;const auto command=resolve_target_analysis(app.resolve_target(),options);check(command.record().at("version")==MetadataValue(std::int64_t(15)) && reads.counts().plane_reads==before);
      owner->select(Selection("rectangle",{{0,0},{1,1}}));const auto frozen=owner->snapshot();const auto result=execute_target_analysis(command);const auto& row=result.planes.front();
      check(row.circularity && row.aspect_ratio && row.roundness && row.solidity);
      check(std::abs(*row.circularity-expected[i][0])<1e-11);
      if(i==3)check(std::isnan(*row.aspect_ratio) && std::isinf(*row.roundness));
      else if(i==4 || i==5)check(std::isnan(*row.aspect_ratio) && std::isnan(*row.roundness));
      else check(std::abs(*row.aspect_ratio-expected[i][1])<1e-11 && std::abs(*row.roundness-expected[i][2])<1e-11);
      if(i==5)check(std::isnan(*row.solidity));else check(std::abs(*row.solidity-expected[i][3])<1e-11);
      check(reads.counts().plane_reads==before+1 && owner->snapshot().shares_data(frozen) && owner->undo_steps()==0);
      TargetAnalysisLimits limits;limits.max_measurement_work=1;refuses<std::invalid_argument>([&]{resolve_target_analysis(command.target(),options,limits);});check(reads.counts().plane_reads==before+1);
    }
  }
  {
    SpatialCalibration spatial;spatial.spacing={2,.5,1};spatial.origin={-6.5,.75,0};spatial.unit="um";
    const PixelType types[]={PixelType::UInt8,PixelType::UInt16,PixelType::Float32,PixelType::Float32,PixelType::UInt8};
    for(int i=0;i<5;++i) {
      Image pixels(8,6,1,types[i]);
      for(int y=0;y<6;++y)for(int x=0;x<8;++x)pixels.at(x,y)=i==0?x+2*y+1:i==3?2*(x%2)-1:i==4?x%2:0;
      DatasetReadObserver reads;auto owner=std::make_shared<Document>(Dataset::from_planes({1,1,1,6,8,1},{pixels},spatial).observe_reads(reads));Application app;app.adopt(owner);
      if(i==4)app.set_global_calibration(GlobalCalibration(spatial,ImageJIntensityCalibration::function(PixelType::UInt8,"straight_line",{-1,2},"OD"),PixelType::UInt8));
      TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;const auto before=reads.counts().plane_reads;
      const auto command=resolve_target_analysis(app.resolve_target(),options);check(command.record().at("version")==MetadataValue(std::int64_t(11)) && reads.counts().plane_reads==before);
      owner->select(Selection("rectangle",{{0,0},{1,1}}));const auto frozen=owner->snapshot();const auto result=execute_target_analysis(command);const auto& row=result.planes.front();
      check(row.center_of_mass_world.has_value());const auto mass=*row.center_of_mass_world;
      if(i==0)check(std::abs(mass[0]-2.6052631578947363)<1e-12 && std::abs(mass[1]-2.557017543859649)<1e-12);
      else if(i==1)check(std::isnan(mass[0]) && std::isnan(mass[1]));
      else if(i==2)check(mass[0]==1.5 && mass[1]==2.25);
      else check(std::isinf(mass[0]) && !std::signbit(mass[0]) && std::isnan(mass[1]));
      check(mass[2]==0 && row.centroid_world && *row.centroid_world==std::array<double,3>{1.5,2.25,0});
      check(reads.counts().plane_reads==before+1 && owner->snapshot().shares_data(frozen) && owner->undo_steps()==0);
    }
    refuses<std::invalid_argument>([&]{spatial.voxel_to_world({std::numeric_limits<double>::infinity(),0,0});});
  }
  {
    SpatialCalibration spatial;spatial.spacing={2,.5,1};spatial.origin={-6.5,.75,0};spatial.unit="um";
    for(int kind=0;kind<5;++kind) {
      DatasetReadObserver reads;DocumentData data{Dataset::from_planes({1,1,1,40,40,1},{Image(40,40,1,PixelType::UInt8)},spatial).observe_reads(reads)};
      if(kind==1 || kind==2)data.selection=Selection(kind==1?"rectangle":"ellipse",{{3,3},{13,11}});
      if(kind==3)data.selection=Selection("polygon",{{3,3},{13,3},{9,11}});
      if(kind==4)data.selection=Selection("rectangle",{{60,60},{64,64}});
      auto owner=std::make_shared<Document>(std::move(data));Application app;app.adopt(owner);TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;
      const auto before=reads.counts().plane_reads;const auto command=resolve_target_analysis(app.resolve_target(),options);
      check(reads.counts().plane_reads==before && command.record().at("version")==MetadataValue(std::int64_t(11)));
      owner->select(Selection("rectangle",{{0,0},{1,1}}));const auto frozen=owner->snapshot();const auto result=execute_target_analysis(command);const auto& row=result.planes.front();
      check(row.centroid_world.has_value());const auto point=*row.centroid_world;
      if(kind==4)check(std::isnan(point[0]) && std::isnan(point[1]) && std::isnan(point[2]));
      else {const double x=kind==0?33.5:kind==3?10.1:9.5,y=kind==0?10.75:kind==3?3.55:4.25;check(std::abs(point[0]-x)<1e-12 && std::abs(point[1]-y)<1e-12 && point[2]==0);}
      check(reads.counts().plane_reads==before+1 && owner->snapshot().shares_data(frozen) && owner->undo_steps()==0);
    }
  }
  {
    // Original Roi/OvalRoi/Analyzer observations,2/.5 um; run this maintained
    // native consumer before expensive Python module links.
    const char* kinds[]={"rectangle","ellipse","rectangle","ellipse","ellipse","whole_image"};
    const double expected[]={48.,42.01120851869869,53.,46.295306614080765,4.288751483104749,160.};
    SpatialCalibration spatial;spatial.spacing={2,.5,1};spatial.unit="um";
    for(int i=0;i<6;++i) {
      DatasetReadObserver reads;
      DocumentData data{Dataset::from_planes({1,1,1,40,40,1},{Image(40,40,1,PixelType::UInt8)},spatial).observe_reads(reads)};
      if(i<5)data.selection=Selection(kinds[i],i==4?std::vector<std::array<double,2>>{{12.3,12.9},{12.5,13.7}}:i>=2?std::vector<std::array<double,2>>{{3.2,4.7},{13.4,13.0}}:std::vector<std::array<double,2>>{{3,3},{13,11}});
      auto owner=std::make_shared<Document>(std::move(data));Application application;application.adopt(owner);
      TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;
      const auto before=reads.counts().plane_reads;const auto command=resolve_target_analysis(application.resolve_target(),options);
      check(reads.counts().plane_reads==before && command.record().at("version")==MetadataValue(std::int64_t(11)));
      owner->select(Selection("rectangle",{{0,0},{1,1}}));const auto frozen=owner->snapshot();
      const auto result=execute_target_analysis(command);const auto& row=result.planes.front();
      check(row.perimeter && std::abs(*row.perimeter-expected[i])<1e-11);
      check(row.perimeter_in_pixels==(i==5) && row.length==std::nullopt);
      check(reads.counts().plane_reads==before+1 && owner->snapshot().shares_data(frozen) && owner->undo_steps()==0);
    }
    DatasetReadObserver reads;auto huge=spatial;huge.spacing={1e308,1,1};
    DocumentData data{Dataset::from_planes({1,1,1,40,40,1},{Image(40,40,1,PixelType::UInt8)},huge).observe_reads(reads)};
    data.selection=Selection("rectangle",{{3,3},{13,11}});auto owner=std::make_shared<Document>(std::move(data));Application application;application.adopt(owner);
    TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;
    const auto before=reads.counts().plane_reads;refuses<std::invalid_argument>([&]{resolve_target_analysis(application.resolve_target(),options);});check(reads.counts().plane_reads==before);
  }
  {
    // Actual original Fit Spline command rows, shape1, Float constructor, 2/.5 um.
    const SelectionPolygonType types[]={SelectionPolygonType::Polygon,SelectionPolygonType::Freehand,SelectionPolygonType::Traced,SelectionPolygonType::Freeline};
    const double expected[]={52.65164241897764,41.50618389325088,0,20.89065609122347};
    SpatialCalibration calibration;calibration.spacing={2,.5,1};
    for(int i=0;i<4;++i) {
      auto source=with_selection_polygon_type(Selection(i==0?"polygon":i==3?"polyline":"freehand",{{3,3},{13,3},{13,13}}),types[i]);
      const auto fitted=fit_spline_selection(source,40,40);
      check(selection_spline_fit(fitted).has_value());
      check(std::abs(imagej_calibrated_polygon_length(fitted,calibration)-expected[i])<1e-6);
      DocumentData data{Dataset::from_planes({1,1,1,40,40,1},{Image(40,40,1,PixelType::UInt8)},calibration)};
      data.selection=fitted;auto document=std::make_shared<Document>(std::move(data));Application application;application.adopt(document);
      TargetAnalysisOptions settings;settings.measurement=TargetMeasurementMode::Selection;
      const auto command=resolve_target_analysis(application.resolve_target(),settings);
      document->select(source);const auto result=execute_target_analysis(command);
      check(result.planes.front().perimeter && std::abs(*result.planes.front().perimeter-expected[i])<1e-6);
      check(command.record().at("version")==MetadataValue(std::int64_t(i==3?8:11)));
    }
  }
  {
    SpatialCalibration calibration;calibration.spacing={2,.5,1};
    const Selection path("polyline",{{3,3},{13,3},{13,13},{3,13}});
    auto free=with_selection_polygon_type(path,SelectionPolygonType::Freeline);
    check(std::abs(imagej_calibrated_polygon_length(free,calibration)-28.54085916099517)<1e-11);
    check(imagej_calibrated_polygon_length(path,calibration)==45);
    const auto traced=with_selection_polygon_type(Selection("freehand",path.points),SelectionPolygonType::Traced);
    check(std::isnan(imagej_calibrated_polygon_length(traced,calibration)));
    check(std::abs(imagej_calibrated_polygon_length(traced,calibration,true)-48.24621125123532)<1e-11);
    refuses<std::invalid_argument>([&]{imagej_calibrated_polygon_length(free,calibration,false,8255);});
    const auto integer=with_selection_polygon_coordinates(traced,true);
    check(selection_polygon_integer_coordinates(integer));
    check(!selection_polygon_integer_coordinates(SelectionEditor(integer,40,40).moved_vertex(0,{3.25,3})));
    DocumentData data{Dataset::from_planes({1,1,1,40,40,1},{Image(40,40,1,PixelType::UInt8)},calibration)};
    data.selection=integer;auto document=std::make_shared<Document>(std::move(data));Application application;application.adopt(document);
    TargetAnalysisOptions settings;settings.measurement=TargetMeasurementMode::Selection;
    const auto command=resolve_target_analysis(application.resolve_target(),settings);
    document->select(with_selection_polygon_coordinates(integer,false));
    const auto result=execute_target_analysis(command);
    check(result.planes.front().perimeter && std::abs(*result.planes.front().perimeter-48.24621125123532)<1e-11);
    check(command.record().at("version")==MetadataValue(std::int64_t(11)));
  }
  {
    // Original lifecycle rows: fractional repeated closed fit, traced straighten,
    // and segmented direct-count refit; the same source is an installed SDK client.
    SpatialCalibration calibration;calibration.spacing={2,.5,1};
    const std::vector<std::array<double,2>> fractional={{3.2,4.7},{13.8,3.1},{19.4,14.2},{11.1,20.6},{4.3,13.8}};
    const auto first=fit_spline_selection(Selection("polygon",fractional),40,40);
    const auto repeated=fit_spline_selection(first,40,40);
    check(std::abs(imagej_calibrated_polygon_length(repeated,calibration)-74.14868185876702)<1e-7*74.14868185876702);
    SplineFitOptions straighten;straighten.mode=SplineFitMode::Straighten;
    const auto square=fit_spline_selection(Selection("polygon",{{3,3},{13,3},{13,13},{3,13}}),40,40);
    const auto straight=fit_spline_selection(square,40,40,straighten);
    check(std::abs(imagej_calibrated_polygon_length(straight,calibration)-48.24621125123532)<1e-7*48.24621125123532);
    SplineFitOptions seven;seven.evaluation_points=7;
    const auto path=fit_spline_selection(Selection("polyline",{{3,3},{13,3},{13,13}}),40,40);
    const auto counted=fit_spline_selection(path,40,40,seven);
    check(std::abs(imagej_calibrated_polygon_length(counted,calibration)-26.556521351852417)<1e-7*26.556521351852417);
  }
  auto provider=std::make_shared<Source>();auto owner=std::make_shared<Document>(Dataset(provider));
  Application app;app.adopt(owner);
  SpatialCalibration spatial;spatial.spacing={.5,2,3};spatial.unit="um";
  app.set_global_calibration(GlobalCalibration(spatial,
      ImageJIntensityCalibration::function(PixelType::UInt8,"straight_line",{10,2},"OD"),PixelType::UInt8));
  TargetRequest request;request.scope=TargetScope::Positions;request.roi=RoiPolicy::WholePlane;
  request.positions={{1,1,2},{0,0,0}};
  TargetAnalysisOptions options;options.line=std::array<double,4>{0,0,2,1};
  const auto target=app.resolve_target(request);const auto estimate=estimate_target_analysis(target,options);
  check(estimate.plane_count==2 && estimate.total_pixels==12 && estimate.profile_samples==3 && provider->reads.empty());
  TargetAnalysisLimits limits;limits.max_working_bytes=estimate.working_bytes-1;
  refuses<std::invalid_argument>([&]{resolve_target_analysis(target,options,limits);});check(provider->reads.empty());
  limits.max_working_bytes=estimate.working_bytes;
  const auto command=resolve_target_analysis(target,options,limits);check(provider->reads.empty());
  options.line=std::array<double,4>{1,0,1,0}; // Cannot mutate the preparation.
  int calls=0;ExecutionControl control;control.checkpoint=[&]{++calls;};
  refuses<std::invalid_argument>([&]{execute_target_analysis(command,control,estimate.working_bytes-1);});
  check(!calls && provider->reads.empty());
  const auto result=execute_target_analysis(command,control);
  check(calls>0 && provider->reads.size()==2 && provider->reads[0].t==1 && provider->reads[0].z==2);
  check(result.planes.size()==2 && result.planes[0].statistics->mean==195 && result.planes[1].statistics->mean==15);
  check(result.planes[1].profile_values==std::vector<double>({10,15,20}));
  check(result.profile_distances.size()==3 && std::abs(result.profile_distances.back()-std::sqrt(5.))<1e-12);
  check(owner->undo_steps()==0);app.validate_target(command.target());
  spatial.spacing[0]=2;app.set_global_calibration(GlobalCalibration(spatial,ImageJIntensityCalibration::uncalibrated(),PixelType::UInt8));
  refuses<RevisionConflict>([&]{app.validate_target(command.target());});
  check(execute_target_analysis(command).planes[1].statistics->mean==15); // Frozen value remains usable.
  auto token=std::make_shared<CancellationToken>();token->cancel();control.cancellation=token;calls=0;
  auto count=provider->reads.size();refuses<OperationCancelled>([&]{execute_target_analysis(command,control);});
  check(!calls && provider->reads.size()==count);
  token=std::make_shared<CancellationToken>();control.cancellation=token;
  control.progress=[&](std::size_t done,std::size_t){if(done==1)token->cancel();};
  refuses<OperationCancelled>([&]{execute_target_analysis(command,control);});check(provider->reads.size()==count+1);
  std::weak_ptr<Source> weak;TargetAnalysisResult detached;
  {auto source=std::make_shared<Source>();weak=source;Document temporary{Dataset(source)};
    TargetRequest whole;whole.roi=RoiPolicy::WholePlane;
    detached=execute_target_analysis(resolve_target_analysis(resolve_target(temporary.snapshot(),whole)));}
  check(weak.expired() && detached.planes[0].statistics->mean==2.5);
  check(detached.planes[0].bounds && *detached.planes[0].bounds==std::array<int,4>{0,0,3,2});
  {
    auto source=std::make_shared<Source>();Document temporary{Dataset(source)};
    TargetRequest whole;whole.roi=RoiPolicy::WholePlane;
    TargetAnalysisOptions configured;configured.statistics=false;
    configured.path={{0,0},{2,0},{2,1}};configured.samples_per_segment=3;
    const auto prepared=resolve_target_analysis(resolve_target(temporary.snapshot(),whole),configured);
    check(source->reads.empty() && prepared.estimate().profile_samples==5);
    const auto result=execute_target_analysis(prepared);
    check(source->reads.size()==1 && !result.planes[0].bounds);
    check(result.planes[0].profile_values==std::vector<double>({0,1,2,3.5,5}));
    check(result.profile_distances==std::vector<double>({0,1,2,2.5,3}));
    auto edit=temporary.begin_edit();edit.select(Selection("rectangle",{{1,0},{3,1}}));temporary.commit(edit);
    const auto area=execute_target_analysis(resolve_target_analysis(resolve_target(temporary.snapshot())));
    check(area.planes[0].bounds && *area.planes[0].bounds==std::array<int,4>{1,0,2,1});
    check(area.planes[0].statistics->mean==1.5);
    const auto measured=imagej_calibrated_line_measurement(*source->read_plane({0,0,0}),
        SpatialCalibration{},ImageJIntensityCalibration::uncalibrated(),0,0,2,1);
    check(measured.statistics.mean==2.5 && measured.statistics.stddev==2.5 && measured.statistics.area==3);
    check(std::abs(measured.length-std::sqrt(5.))<1e-12);
    check(measured.midpoint_world==std::array<double,3>{1,.5,0});
    refuses<std::invalid_argument>([&]{imagej_calibrated_line_measurement(*source->read_plane({0,0,0}),
        SpatialCalibration{},ImageJIntensityCalibration::uncalibrated(),0,0,2,1,true,1);});
    auto manager=temporary.begin_edit();
    manager.set_rois({Roi{"area",Selection("rectangle",{{0,0},{2,1}}),0,0,0,{}},
                      Roi{"line",Selection("line",{{0,0},{2,1}}),0,0,0,{}}});temporary.commit(manager);
    const auto request=roi_measurement_target_request(temporary.snapshot(),{1,0});
    TargetAnalysisOptions batch;batch.measurement=TargetMeasurementMode::RoiManager;batch.roi_indices={1,0};
    source->reads.clear();const auto prepared_rows=resolve_target_analysis(resolve_target(temporary.snapshot(),request),batch);
    check(source->reads.empty() && prepared_rows.estimate().plane_count==1 && prepared_rows.estimate().row_count==2);
    const auto rows=execute_target_analysis(prepared_rows);
    check(source->reads.size()==1 && rows.planes.size()==2);
    check(rows.planes[0].roi_index==1 && rows.planes[1].roi_index==0);
    check(rows.planes[0].statistics->mean==2.5 && rows.planes[1].statistics->mean==.5);
    check(rows.planes[0].length && rows.planes[0].midpoint_world==measured.midpoint_world);
    check(rows.record.at("version")==MetadataValue(std::int64_t(11)));
    const auto irregular=imagej_calibrated_polyline_measurement(*source->read_plane({0,0,0}),
        SpatialCalibration{},ImageJIntensityCalibration::uncalibrated(),{{0,0},{2,0},{2,1}});
    check(irregular.statistics.pixel_count==2 && irregular.statistics.mean==1.5 && irregular.length==3);
    check(imagej_line_bounds({0,0,2,1})==std::array<int,4>{0,0,3,2});
    check(std::abs(imagej_calibrated_angle({{{0,0},{1,1},{2,0}}},SpatialCalibration{},true)-270)<1e-12);
    auto angles=temporary.begin_edit();angles.set_rois({Roi{"angle",Selection("angle",{{0,0},{1,1},{2,0}}),0,0,0,{}}});temporary.commit(angles);
    batch.roi_indices={};source->reads.clear();
    const auto geometry=resolve_target_analysis(resolve_target(temporary.snapshot(),roi_measurement_target_request(temporary.snapshot())),batch);
    check(geometry.estimate().plane_count==0 && geometry.estimate().total_pixels==0);
    const auto angle_rows=execute_target_analysis(geometry);
    check(source->reads.empty() && angle_rows.planes[0].angle_degrees && std::abs(*angle_rows.planes[0].angle_degrees-90)<1e-12);
    auto points=temporary.begin_edit();points.set_rois({Roi{"points",Selection("multipoint",{{.5,.5},{0,0},{.5,.5}}),0,0,0,{}}});temporary.commit(points);
    const auto point_target=resolve_target(temporary.snapshot(),roi_measurement_target_request(temporary.snapshot()));
    TargetAnalysisLimits point_limits;point_limits.max_measurement_rows=2;
    refuses<std::invalid_argument>([&]{resolve_target_analysis(point_target,batch,point_limits);});
    check(source->reads.empty());
    const auto point_command=resolve_target_analysis(point_target,batch);
    check(point_command.estimate().row_count==3 && point_command.estimate().plane_count==1);
    const auto point_rows=execute_target_analysis(point_command);
    check(source->reads.size()==1 && point_rows.planes.size()==3);
    check(point_rows.planes[0].point_index==0 && point_rows.planes[2].point_index==2);
    check(point_rows.planes[0].point_world==std::array<double,3>{.5,.5,0});
    check(point_rows.planes[0].statistics->mean==4 && point_rows.planes[0].statistics->area==0 && point_rows.planes[0].statistics->integrated_density==4);
    const auto created=point_creation_annotation(source->shape(),{1,1,2},99,true);
    check(created.counter==99 && created.position->z==2);
    check(!point_creation_annotation(source->shape(),{1,1,2},99,false).position);
    PlanarAnnotation plane_points{"new",Selection("multipoint",{{1,1},{2,2}}),Plane::XZ,0,0,1,{}};
    const auto plane_created=planar_point_creation_selection(temporary.snapshot(),plane_points,7,true);
    const auto plane_entries=point_annotations(plane_created);
    check(plane_entries.size()==2 && plane_entries[0].counter==7 && plane_entries[0].position->t==0);
    check(plane_entries[0].position->c==1 && plane_entries[0].position->z==1 && plane_entries[1].position->z==2);
    check(!point_annotations(planar_point_creation_selection(temporary.snapshot(),plane_points,7,false))[0].position);
    check(!point_creation_annotation({1,1,1,2,2,1},{0,0,0}).position);
    refuses<std::invalid_argument>([&]{point_creation_annotation(source->shape(),{2,0,0},0,false);});
    const std::vector<std::uint32_t> words{256,3074,258};
    const auto annotated=with_imagej_point_counters(Selection("multipoint",{{0,0},{.5,.5},{0,0}}),words,source->shape());
    check(imagej_point_counters(annotated,source->shape())==words);
    check(point_annotations(annotated)[1].position->t==1 && point_annotations(annotated)[1].position->z==2);
    auto annotations=temporary.begin_edit();annotations.set_rois({Roi{"annotated",annotated,0,0,0,{}}});temporary.commit(annotations);
    source->reads.clear();const auto annotated_request=roi_measurement_target_request(temporary.snapshot());
    check(annotated_request.positions.size()==2);
    const auto annotation_command=resolve_target_analysis(resolve_target(temporary.snapshot(),annotated_request),batch);
    check(source->reads.empty());const auto annotation_rows=execute_target_analysis(annotation_command);
    check(source->reads.size()==2 && annotation_rows.planes[1].statistics->mean==94);
    check(annotation_rows.planes[1].point_counter==2 && annotation_rows.planes[1].point_counter_count==2);
    check(annotation_rows.record.at("version")==MetadataValue(std::int64_t(6)));
    const SelectionEditor point_editor(annotated,3,2);
    const auto inserted=point_editor.inserted_vertex(1,{2,1});
    check(imagej_point_counters(inserted,source->shape())==std::vector<std::uint32_t>({256,0,3074,258}));
    const auto removed=SelectionEditor(inserted,3,2).removed_vertex(1);
    check(removed==annotated && point_editor.selection()==annotated);
    SelectionEditLimits edit_limits;edit_limits.max_working_bytes=8192;
    refuses<std::length_error>([&]{point_editor.inserted_vertex(1,{2,1},edit_limits);});
    ImageJRoi raw;raw.roi={"points",without_point_annotations(annotated),{},{},0,{}};
    raw.point_counters=words;
    ImageJRoiPlaneMapping mapping;mapping.extent=source->shape();
    const auto bare=map_imagej_roi(raw,mapping);
    check(!bare.annotation().selection.attributes.count(point_annotations_attribute));
    const auto promoted=annotated_imagej_point_mapping(bare);
    check(promoted.annotation().selection==annotated);
    auto projected=imagej_roi_annotation_projection(promoted.annotation(),source->shape());
    check(projected.point_counters==words && !projected.roi.selection.attributes.count(point_annotations_attribute));
    const auto restored=decode_imagej_roi_envelope(encode_imagej_roi_native_envelope(promoted,projected));
    check(restored.annotation()==promoted.annotation() && restored.source_record().point_counters==words);
    auto imported_annotation=prepare_store_planar_annotation(temporary.snapshot(),promoted.annotation());
    temporary.commit(imported_annotation);
    const auto before_transfer=temporary.snapshot();
    auto adoption=prepare_select_planar_annotation(before_transfer,0);
    temporary.commit(adoption.transaction,std::move(adoption.display));
    auto named_annotated=annotated;named_annotated.attributes["imagej_roi_name"]=promoted.annotation().name;
    check(temporary.snapshot().data().selection==named_annotated && source->reads.size()==2);
    auto stored=prepare_store_planar_annotation_roi(temporary.snapshot(),0);
    const auto new_index=*stored.row_index;temporary.commit(stored.transaction);
    check(temporary.snapshot().data().rois.at(new_index).selection==annotated);
    const auto copyback=resolve_planar_annotation_from_selection(temporary.snapshot(),0);
    validate_planar_annotation_command_record(copyback.record());
    refuses<std::invalid_argument>([&]{prepare_select_planar_annotation(temporary.snapshot(),0,8192);});
  }
  std::cout<<"target analysis: metadata admission, installed API, captured order/calibration, cancellation, detached results passed\n";
} catch(const std::exception& error) {
  std::cerr<<"Target analysis exception: "<<error.what()<<std::endl;return 1;
}
