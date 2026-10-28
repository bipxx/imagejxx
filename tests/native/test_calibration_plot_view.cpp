#include "../../src/desktop/calibration_plot_view.hpp"
#include "../../src/desktop/calibration_plot_annotation.hpp"
#include "../../src/desktop/calibration_plot_payload.hpp"
#include "../../src/desktop/calibration_plot_style.hpp"
#include "../../src/desktop/calibration_plot_ticks.hpp"
#include "../../src/desktop/generic_plot_payload.hpp"
#include "../../src/desktop/plot_context.hpp"
#include "../../src/desktop/plot_hover.hpp"
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
  const auto panView=view;
  check(view.rectangle(data,{.8,.8,.2,.2}));
  bounds=view.limits(data);
  const auto original=panView.limits(data);
  check(std::abs(bounds[0]-(original[0]+.2*(original[1]-original[0])))<1e-12);
  const auto selected=view;
  check(!view.rectangle(data,{.1,.1,.1,.9}));check(view.same_view(selected));
  check(!view.rectangle(data,{0,0,std::numeric_limits<double>::quiet_NaN(),1}));check(view.same_view(selected));
  check(view.rectangle(data,{-2,-2,2,2}));check(view.same_view(selected));
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
  const auto logBefore=view.limits(data);
  check(view.rectangle(data,{.25,.25,.75,.75}));bounds=view.limits(data);
  check(std::abs(std::log10(bounds[0])-(std::log10(logBefore[0])+.25*(std::log10(logBefore[1])-std::log10(logBefore[0]))))<1e-13);
  check(bounds[2]>bounds[3]);
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
  using imagejxx::desktop::calibration_axis_ticks;
  auto axisTicks=calibration_axis_ticks(0,100,false,true,true);
  check(axisTicks.majors==5 && axisTicks.minors==16);
  int majorIndex=0;for(const auto& tick:axisTicks)if(tick.major)check(tick.value==25*majorIndex++);
  axisTicks=calibration_axis_ticks(1,10000,true,true,true);check(axisTicks.majors==5 && axisTicks.minors==32);
  axisTicks=calibration_axis_ticks(0,0,false,true,true);check(axisTicks.size==1 && axisTicks.begin()->fraction==.5);
  check(calibration_axis_ticks(0,1,true,true,true).size==0);
  const std::array<std::array<double,2>,8> tickRanges{{{{-largest,largest}},{{1e300,std::nextafter(1e300,largest)}},
    {{std::numeric_limits<double>::denorm_min(),4*std::numeric_limits<double>::denorm_min()}},{{1e-300,1e300}},
    {{3,97}},{{-97,-3}},{{2,8}},{{1,10000}}}};
  for(const auto& range:tickRanges)for(bool logarithmic:{false,true})for(bool reverse:{false,true}) {
    if(logarithmic && range[0]<=0)continue;
    const auto ticks=calibration_axis_ticks(range[reverse?1:0],range[reverse?0:1],logarithmic,true,true);
    check(ticks.size<=ticks.capacity && ticks.majors<=ticks.major_limit && ticks.minors<=ticks.minor_limit && ticks.size>0);
    double previous=-1;
    for(const auto& tick:ticks){check(std::isfinite(tick.value) && tick.fraction>=0 && tick.fraction<=1 && tick.fraction>previous);previous=tick.fraction;}
  }
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

  imagejxx::desktop::CalibrationPlotStyle style;
  check(style.valid() && !style.annotation_visible);
  check(style.parse_color("#00Af09")==0x00af09 && style.color_text(0x00af09)=="#00af09");
  check(!style.parse_color("red") && !style.parse_color("#12345678") && !style.parse_color("#zz0000"));
  style.annotation_x=.75;style.annotation_y=.25;
  check(style.box(100,80,20,10,4)==std::array<double,4>{54,15.5,28,18});
  check(style.box(10,8,200,100,4)==std::array<double,4>{0,0,10,8});
  style.line_width=std::numeric_limits<double>::quiet_NaN();check(!style.valid());
  style.line_width=1.5;style.font_scale=2.1;check(!style.valid());
  style.font_scale=1;style.annotation_x=-.1;check(!style.valid());
  check(imagejxx::desktop::calibration_plot_canvas_text(annotation)==annotation.formula);

  imagejxx::desktop::RenderMemory memory(1024*1024);
  ImageJIntensityFitPlot payloadData;payloadData.curve_x={0,1};payloadData.curve_y={1,2};
  auto payload=imagejxx::desktop::retain_calibration_plot(memory,std::move(payloadData),annotation,true,77,1,"Source \xc2\xb5");
  const auto retained=memory.retained();check(retained>0 && memory.working()==0 && payload->source_id==77 && payload->source_title=="Source \xc2\xb5");
  auto shared=payload;payload.reset();check(memory.retained()==retained && shared->data.curve_y[1]==2);
  shared.reset();check(memory.used()==0);
  auto counts=std::make_shared<DatasetHistogramResult>();counts->histogram.counts={9007199254740993ULL,2};counts->histogram.edges={0,1,2};
  auto histogramPlot=imagejxx::desktop::retain_histogram_plot(memory,counts,77,2,"histogram source");
  check(!histogramPlot->calibration() && histogramPlot->histogram.get()==counts.get() && histogramPlot->curveSize()==2);
  check(histogramPlot->genericCsv()==dataset_histogram_csv(*counts) && histogramPlot->genericCsv().find("9007199254740993")!=std::string::npos);
  check(histogramPlot->annotation.canvas_text.empty() && histogramPlot->yLabel()=="Count");
  using imagejxx::desktop::plot_hover_sample;
  auto hovered=plot_hover_sample(*histogramPlot,0);
  check(hovered && hovered->index==0 && hovered->count==9007199254740993ULL && !hovered->upper_inclusive);
  hovered=plot_hover_sample(*histogramPlot,1);check(hovered && hovered->index==1 && hovered->count==2);
  hovered=plot_hover_sample(*histogramPlot,2);check(hovered && hovered->index==1 && hovered->upper_inclusive);
  check(!plot_hover_sample(*histogramPlot,-1) && !plot_hover_sample(*histogramPlot,3));
  check(!plot_hover_sample(*histogramPlot,std::numeric_limits<double>::quiet_NaN()));
  histogramPlot.reset();check(memory.used()==0);
  auto profilePlot=imagejxx::desktop::retain_profile_plot(memory,{0,2,4},{0,10,20},"Line profile","Distance (um)",77,3,"profile source");
  check(!profilePlot->calibration() && profilePlot->curveX(1)==2 && profilePlot->curveY(1)==10);
  check(profilePlot->csv()=="x,y\n0,0\n2,10\n4,20\n" && profilePlot->data.x_max==4 && profilePlot->data.y_max==20);
  hovered=plot_hover_sample(*profilePlot,1);check(hovered && hovered->index==0 && hovered->x==0);
  hovered=plot_hover_sample(*profilePlot,1.1);check(hovered && hovered->index==1 && hovered->y==10);
  profilePlot.reset();check(memory.used()==0);
  profilePlot=imagejxx::desktop::retain_profile_plot(memory,{0,1,1,2},{10,20,30,40},"Profile","Distance",77,4,"source","captured context");
  check(profilePlot->context_caption=="captured context");
  check(plot_hover_sample(*profilePlot,1)->index==1 && plot_hover_sample(*profilePlot,1.5)->index==1);
  check(plot_hover_sample(*profilePlot,largest)->index==3 && plot_hover_sample(*profilePlot,-largest)->index==0);
  profilePlot.reset();
  profilePlot=imagejxx::desktop::retain_profile_plot(memory,{-largest,-largest/2},{0,1},"Profile","Distance",77,5,"source");
  check(plot_hover_sample(*profilePlot,largest)->index==1);profilePlot.reset();check(memory.used()==0);
  bool unordered=false;try{imagejxx::desktop::retain_profile_plot(memory,{2,1},{1,2},"Profile","Distance",77,6,"source");}catch(const std::invalid_argument&){unordered=true;}
  check(unordered && memory.used()==0);
  imagejxx::desktop::RenderMemory contextBudget(4096);bool contextRefused=false;
  try{imagejxx::desktop::retain_profile_plot(contextBudget,{0,1},{0,1},"Profile","Distance",77,7,"source",std::string(16384,'x'));}catch(const std::invalid_argument&){contextRefused=true;}
  check(contextRefused && contextBudget.used()==0);
  Stack contextPlanes(12,Image(1,1,1,PixelType::UInt8));
  imagejxx::Document contextSource(Dataset::from_planes({2,2,3,1,1,1},std::move(contextPlanes)));
  auto display=contextSource.snapshot().display();display.t=display.c=1;display.crosshair[2]=2;contextSource.set_display(display);
  check(imagejxx::desktop::plot_plane_caption(contextSource.snapshot(),"XY profile")=="XY profile | T 2, C 2, Z 3");
  TargetRequest target;target.scope=TargetScope::Ranges;target.t=AxisRange{0,2,1};target.c=AxisRange{1,2,1};target.z=AxisRange{0,3,2};target.roi=RoiPolicy::PositionedRois;
  check(imagejxx::desktop::plot_target_caption(resolve_target(contextSource.snapshot(),target))=="4 XY planes | T 1..2 step 1, C 2..2 step 1, Z 1..3 step 2 | positioned ROI unions");
  target={};target.scope=TargetScope::Positions;target.positions={{1,1,2},{0,0,0}};target.roi=RoiPolicy::WholePlane;
  check(imagejxx::desktop::plot_target_caption(resolve_target(contextSource.snapshot(),target))=="2 XY planes | explicit order; first T 2, C 2, Z 3 | whole plane");
  imagejxx::desktop::RenderMemory denied(1);bool refused=false;
  try{imagejxx::desktop::retain_calibration_plot(denied,ImageJIntensityFitPlot{},imagejxx::desktop::CalibrationPlotAnnotation{},true,0);}
  catch(const std::invalid_argument&){refused=true;}
  check(refused && denied.used()==0);

}

int main() {
  try {verify_contracts();return 0;}
  catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
}
