#include "../../src/desktop/calibration_plot_view.hpp"
#include "../../src/desktop/calibration_plot_annotation.hpp"
#include <limits>
#include <stdexcept>
#include <cstdio>

using namespace imagejxx;
using imagejxx::desktop::CalibrationPlotView;
void check_impl(bool value,int line) { if(!value)throw std::runtime_error("Calibration plot view contract at line "+std::to_string(line)); }
#define check(...) check_impl((__VA_ARGS__),__LINE__)
void verify_contracts() {
  ImageJIntensityFitPlot data;data.x_min=0;data.x_max=100;data.y_min=-10;data.y_max=10;
  CalibrationPlotView view;
  check(view.zoom(data,.25,.75,.5));
  auto bounds=view.limits(data);
  check(bounds==std::array<double,4>{12.5,62.5,-2.5,7.5});
  check(view.pan(data,-.1,.1));
  bounds=view.limits(data);const std::array<double,4> panned{7.5,57.5,-1.5,8.5};
  for(int i=0;i<4;++i)check(std::abs(bounds[i]-panned[i])<1e-12);
  // Panning beyond the original range is permitted, without mutating data.
  check(view.pan(data,2,2));check(view.limits(data)[0]>data.x_max);
  check(data.x_min==0 && data.x_max==100 && data.y_min==-10 && data.y_max==10);
  const auto previous=view.fractions;
  check(!view.zoom(data,.5,.5,std::numeric_limits<double>::infinity()));
  check(view.fractions==previous);
  const auto largest=std::numeric_limits<double>::max();
  data.x_min=-largest;data.x_max=largest;view={};
  check(!view.zoom(data,.5,.5,1.2));check(view.fractions==CalibrationPlotView{}.fractions);
  check(view.zoom(data,.5,.5,.5));
  for(auto v:view.limits(data))check(std::isfinite(v));
  const auto finite=view.fractions;check(!view.pan(data,2,0));check(view.fractions==finite);
  // A viewport must not collapse when interpolating adjacent large doubles.
  data.x_min=1e300;data.x_max=std::nextafter(data.x_min,largest);view={};
  check(!view.zoom(data,0,.5,.1));check(view.fractions==CalibrationPlotView{}.fractions);
  // Constant axes retain their value while the other axis remains usable.
  data.x_min=data.x_max=7;view={};check(view.zoom(data,.5,.5,.5));
  bounds=view.limits(data);check(bounds[0]==7 && bounds[1]==7 && bounds[2]<bounds[3]);
  // Explicit limits expand constant axes, accept reversed directions and remain
  // the basis for later pan/zoom without modifying the plotted data bounds.
  check(view.set_limits({200,100,1000,-1000}));
  check(view.limits(data)==std::array<double,4>{200,100,1000,-1000});
  check(view.zoom(data,.5,.5,.5));
  check(view.limits(data)==std::array<double,4>{175,125,500,-500});
  const auto reversed=view;
  check(!view.set_limits({1,1,0,1}));
  check(!view.set_limits({0,1,0,std::numeric_limits<double>::infinity()}));
  check(view.fractions==reversed.fractions && view.manual_limits==reversed.manual_limits);
  view={};check(!view.manual_limits);check(view.limits(data)[0]==7);
  view.log_x=view.log_y=true;
  check(view.set_limits({1,10000,10000,1}));
  check(std::abs(CalibrationPlotView::fraction(100,1,10000,true)-.5)<1e-14);
  check(std::abs(CalibrationPlotView::fraction(100,10000,1,true)-.5)<1e-14);
  check(std::isnan(CalibrationPlotView::fraction(0,1,10000,true)));
  check(view.zoom(data,.5,.5,.5));bounds=view.limits(data);
  const std::array<double,4> logarithmic{10,1000,1000,10};
  for(int i=0;i<4;++i)check(std::abs(bounds[i]/logarithmic[i]-1)<1e-14);
  const auto logged=view;
  check(!view.set_limits({0,1,1,10}));
  check(!view.set_limits({1,10,-1,1}));
  check(!view.set_limits({1e300,std::nextafter(1e300,largest),1,10}));
  check(view.fractions==logged.fractions && view.manual_limits==logged.manual_limits);
  check(view.set_limits({1e-300,1e300,1e300,1e-300}));
  const auto wide=view;
  check(!view.zoom(data,.5,.5,2));
  check(view.fractions==wide.fractions && view.manual_limits==wide.manual_limits);
  check(data.x_min==7 && data.x_max==7);
  const auto nearMaximum=CalibrationPlotView::axis_value(largest/2,largest,1-1e-15,true);
  check(std::isfinite(nearMaximum) && nearMaximum>=largest/2 && nearMaximum<=largest);
  using imagejxx::desktop::calibration_plot_annotation;
  auto annotation=calibration_plot_annotation(ImageJIntensityCalibration::signed16("HU"),true);
  check(annotation.model && !annotation.fit && annotation.formula=="y = x - 32768");
  auto calibration=ImageJIntensityCalibration::fit_polynomial(PixelType::UInt8,"straight_line",{0,1},{2,2});
  annotation=calibration_plot_annotation(calibration,false);
  check(annotation.model && annotation.fit && annotation.r_squared_undefined && !annotation.r_squared);
  check(annotation.rmse && *annotation.rmse==0 && annotation.objective=="Fit objective: known(measured)");
  calibration=ImageJIntensityCalibration::function(PixelType::UInt8,"straight_line",{-0.,1},"unit",true);
  annotation=calibration_plot_annotation(calibration,true);
  check(annotation.coefficients=="a=-0; b=1" && annotation.detail.find("clipped")!=std::string::npos);
  std::vector<std::uint32_t> words(256,0);
  auto arbitrary=ImageJIntensityCalibration::lookup(PixelType::UInt8,words,"unit",{{"arbitrary",metadata_array({metadata_object({{"nested",std::string(1024,'x')}})})}});
  annotation=calibration_plot_annotation(arbitrary,true);check(!annotation.model && !annotation.fit);
  arbitrary=ImageJIntensityCalibration::lookup(PixelType::UInt8,words,"unit",{{"producer",std::string("imagej154r.function")},{"version",1},{"function",std::string("straight_line")},{"coefficients",metadata_array({true,1})}});
  annotation=calibration_plot_annotation(arbitrary,true);check(!annotation.model && !annotation.fit);



  auto od=ImageJIntensityCalibration::function(PixelType::UInt8,"uncalibrated_od",{},"OD",false,true);
  auto odParameters=od.source_parameters();odParameters["coefficients"]=std::vector<std::string>{};
  auto legacyOd=ImageJIntensityCalibration::lookup(PixelType::UInt8,od.table_words(),"OD",odParameters);
  annotation=calibration_plot_annotation(legacyOd,true);
  check(annotation.model && annotation.coefficients.empty() && annotation.formula=="y = 0.434294481*ln(255/(255-x))");

}

int main() {
  try {verify_contracts();return 0;}
  catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
}
