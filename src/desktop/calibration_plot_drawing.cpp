#include "calibration_plot_drawing.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace imagejxx::desktop {
CalibrationPlotDrawing drawCalibrationPlotGeometry(ImDrawList* draw,const ImageJIntensityFitPlot& data,
    const CalibrationPlotAnnotation* fitPlotAnnotation,const CalibrationPlotView& fitPlotView,
    const CalibrationPlotStyle& style,const CalibrationPlotCanvas& canvas,bool headlessSessionActive) {
  CalibrationPlotDrawing result;
  const auto origin=canvas.origin,size=canvas.size,widgetOrigin=canvas.widget_origin,widgetSize=canvas.widget_size;
  const auto guiScale=canvas.scale;const ImVec2 end{origin.x+size.x,origin.y+size.y};
  const auto viewLimits=fitPlotView.limits(data);
  const auto packedColor=[](std::uint32_t rgb){return IM_COL32((rgb>>16)&255,(rgb>>8)&255,rgb&255,255);};
  const auto measureText=[&](const char* label){auto size=canvas.font->CalcTextSizeA(canvas.font_size,std::numeric_limits<float>::max(),0,label);size.x=std::floor(size.x+.99999f);return size;};
    draw->AddRect(origin,end,canvas.border);draw->PushClipRect(origin,end,true);
    if(fitPlotView.grid)for(int i=1;i<4;++i) {
      const auto f=float(i)/4;
      draw->AddLine({origin.x+f*size.x,origin.y},{origin.x+f*size.x,end.y},canvas.grid);
      draw->AddLine({origin.x,origin.y+f*size.y},{end.x,origin.y+f*size.y},canvas.grid);
    }
    const auto position=[&](double x,double y) {
      const auto nx=CalibrationPlotView::fraction(x,viewLimits[0],viewLimits[1],fitPlotView.log_x),
          ny=CalibrationPlotView::fraction(y,viewLimits[2],viewLimits[3],fitPlotView.log_y);
      return ImVec2{origin.x+float(nx*size.x),origin.y+size.y-float(ny*size.y)};
    };
    const auto representable=[&](double x,double y){return std::isfinite(x) && std::isfinite(y) && (!fitPlotView.log_x || x>0) && (!fitPlotView.log_y || y>0);};
    auto& drawnSegments=result.drawn_segments;auto& maskedSegments=result.masked_segments;auto& finiteGeometry=result.finite_geometry;
    for(std::size_t i=1;i<data.curve_x.size();++i) {
      if(!representable(data.curve_x[i-1],data.curve_y[i-1]) || !representable(data.curve_x[i],data.curve_y[i])){++maskedSegments;continue;}
      const auto a=position(data.curve_x[i-1],data.curve_y[i-1]),b=position(data.curve_x[i],data.curve_y[i]);
      if(std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(b.x) && std::isfinite(b.y)) {
        draw->AddLine(a,b,packedColor(style.curve_color),float(style.line_width)*guiScale);++drawnSegments;
      } else finiteGeometry=false;
    }
    auto& points=result.points;
    for(std::size_t i=0;i<data.measured.size();++i) {
      if(!representable(data.measured[i],data.known[i])){if(headlessSessionActive)points.push_back(nullptr);continue;}
      const auto point=position(data.measured[i],data.known[i]);
      if(std::isfinite(point.x) && std::isfinite(point.y))draw->AddCircleFilled(point,float(style.point_radius)*guiScale,packedColor(style.point_color));
      if(headlessSessionActive)points.push_back({point.x,point.y});
    }
    auto& modelOverlay=result.model_overlay;
    if(style.annotation_visible && fitPlotAnnotation && !fitPlotAnnotation->canvas_text.empty()) {
      const auto& text=fitPlotAnnotation->canvas_text;
      const auto fontSize=canvas.font_size*float(style.font_scale),padding=4.f*guiScale;
      const auto wrap=std::max(1.f,size.x-2*padding);
      const auto measured=canvas.font->CalcTextSizeA(fontSize,std::numeric_limits<float>::max(),wrap,text.c_str());
      const auto box=style.box(size.x,size.y,measured.x,measured.y,padding);
      const ImVec2 a{origin.x+float(box[0]),origin.y+float(box[1])},b{a.x+float(box[2]),a.y+float(box[3])};
      draw->AddRectFilled(a,b,IM_COL32(20,20,20,235));draw->AddRect(a,b,IM_COL32(210,210,210,255));
      draw->PushClipRect(a,b,true);
      draw->AddText(canvas.font,fontSize,{a.x+padding,a.y+padding},IM_COL32(245,245,245,255),text.c_str(),nullptr,wrap);
      draw->PopClipRect();
      if(headlessSessionActive)modelOverlay={{"text",text},{"origin",{a.x,a.y}},{"size",{box[2],box[3]}},
        {"text_size",{measured.x,measured.y}},{"font_size",fontSize},{"padding",padding},{"wrap_width",wrap}};
    }
    draw->PopClipRect();
    auto& xTicks=result.x_ticks;auto& yTicks=result.y_ticks;
    if(fitPlotView.ticks)for(int i=0;i<=4;++i) {
      const double f=double(i)/4;
      const auto x=CalibrationPlotView::axis_value(viewLimits[0],viewLimits[1],f,fitPlotView.log_x),
          y=CalibrationPlotView::axis_value(viewLimits[2],viewLimits[3],f,fitPlotView.log_y);
      const ImVec2 xp{origin.x+float(f)*size.x,end.y},yp{origin.x,end.y-float(f)*size.y};
      char label[48];ImVec2 textSize;
      if(viewLimits[0]!=viewLimits[1] || i==2) {
      std::snprintf(label,sizeof(label),"%.4g",x);textSize=measureText(label);
      draw->AddLine(xp,{xp.x,xp.y+3},canvas.text);
      draw->AddText(canvas.font,canvas.font_size,{std::clamp(xp.x-textSize.x/2,widgetOrigin.x,widgetOrigin.x+widgetSize.x-textSize.x),xp.y+4},canvas.text,label);
      if(headlessSessionActive)xTicks.push_back({{"value",x},{"label",label},{"point",{xp.x,xp.y}}});
      }
      if(viewLimits[2]!=viewLimits[3] || i==2) {
      std::snprintf(label,sizeof(label),"%.4g",y);textSize=measureText(label);
      draw->AddLine(yp,{yp.x-3,yp.y},canvas.text);
      draw->AddText(canvas.font,canvas.font_size,{std::max(widgetOrigin.x,yp.x-textSize.x-5),std::clamp(yp.y-textSize.y/2,widgetOrigin.y,end.y-textSize.y)},canvas.text,label);
      if(headlessSessionActive)yTicks.push_back({{"value",y},{"label",label},{"point",{yp.x,yp.y}}});
      }
    }
  return result;
}
} // namespace imagejxx::desktop
