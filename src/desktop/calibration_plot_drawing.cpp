#include "calibration_plot_drawing.hpp"
#include "calibration_plot_ticks.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace imagejxx::desktop {
CalibrationPlotDrawing drawCalibrationPlotGeometry(ImDrawList* draw,const ImageJIntensityFitPlot& data,
    const CalibrationPlotAnnotation* fitPlotAnnotation,const CalibrationPlotView& fitPlotView,
    const CalibrationPlotStyle& style,const CalibrationPlotCanvas& canvas,bool headlessSessionActive,const CalibrationPlotPayload* payload) {
  CalibrationPlotDrawing result;
  const auto origin=canvas.origin,size=canvas.size,widgetOrigin=canvas.widget_origin,widgetSize=canvas.widget_size;
  const auto guiScale=canvas.scale;const ImVec2 end{origin.x+size.x,origin.y+size.y};
  const auto viewLimits=fitPlotView.limits(data);
  const auto xAxis=calibration_axis_ticks(viewLimits[0],viewLimits[1],fitPlotView.log_x,fitPlotView.nice_ticks,fitPlotView.minor_ticks);
  const auto yAxis=calibration_axis_ticks(viewLimits[2],viewLimits[3],fitPlotView.log_y,fitPlotView.nice_ticks,fitPlotView.minor_ticks);
  const auto packedColor=[](std::uint32_t rgb){return IM_COL32((rgb>>16)&255,(rgb>>8)&255,rgb&255,255);};
  const auto measureText=[&](const char* label){auto size=canvas.font->CalcTextSizeA(canvas.font_size,std::numeric_limits<float>::max(),0,label);size.x=std::floor(size.x+.99999f);return size;};
    draw->AddRect(origin,end,canvas.border);draw->PushClipRect(origin,end,true);
    if(fitPlotView.grid && !fitPlotView.nice_ticks)for(int i=1;i<4;++i) {
      const auto f=float(i)/4;result.grid_lines+=2;
      draw->AddLine({origin.x+f*size.x,origin.y},{origin.x+f*size.x,end.y},canvas.grid);
      draw->AddLine({origin.x,origin.y+f*size.y},{end.x,origin.y+f*size.y},canvas.grid);
    }
    if(fitPlotView.grid && fitPlotView.nice_ticks) {
      for(const auto& tick:xAxis)if(tick.major && tick.fraction>0 && tick.fraction<1){const auto x=origin.x+float(tick.fraction)*size.x;draw->AddLine({x,origin.y},{x,end.y},canvas.grid);++result.grid_lines;}
      for(const auto& tick:yAxis)if(tick.major && tick.fraction>0 && tick.fraction<1){const auto y=end.y-float(tick.fraction)*size.y;draw->AddLine({origin.x,y},{end.x,y},canvas.grid);++result.grid_lines;}
    }
    const auto position=[&](double x,double y) {
      const auto nx=CalibrationPlotView::fraction(x,viewLimits[0],viewLimits[1],fitPlotView.log_x),
          ny=CalibrationPlotView::fraction(y,viewLimits[2],viewLimits[3],fitPlotView.log_y);
      return ImVec2{origin.x+float(nx*size.x),origin.y+size.y-float(ny*size.y)};
    };
    const auto representable=[&](double x,double y){return std::isfinite(x) && std::isfinite(y) && (!fitPlotView.log_x || x>0) && (!fitPlotView.log_y || y>0);};
    auto& drawnSegments=result.drawn_segments;auto& maskedSegments=result.masked_segments;auto& finiteGeometry=result.finite_geometry;
    const bool generic=payload && !payload->calibration();
    const auto count=payload?payload->curveSize():data.curve_x.size();
    const auto cx=[&](std::size_t i){return payload?payload->curveX(i):double(data.curve_x[i]);};
    const auto cy=[&](std::size_t i){return payload?payload->curveY(i):double(data.curve_y[i]);};
    if(payload && payload->histogram) {
      const auto& edges=payload->histogram->histogram.edges;
      for(std::size_t i=0;i<count;++i) {
        auto left=edges[i];const auto right=edges[i+1],value=cy(i);
        if(fitPlotView.log_x)left=std::max(left,std::min(viewLimits[0],viewLimits[1]));
        const auto base=fitPlotView.log_y?std::min(viewLimits[2],viewLimits[3]):0.;
        if(!representable(left,value) || !representable(right,value)){++maskedSegments;continue;}
        const auto a=position(left,base),b=position(right,value);
        if(std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(b.x) && std::isfinite(b.y)) {
          draw->AddRectFilled({std::min(a.x,b.x),std::min(a.y,b.y)},{std::max(a.x,b.x),std::max(a.y,b.y)},packedColor(style.curve_color));++result.drawn_bars;
        }else finiteGeometry=false;
      }
    } else for(std::size_t i=1;i<count;++i) {
      if(!representable(cx(i-1),cy(i-1)) || !representable(cx(i),cy(i))){++maskedSegments;continue;}
      const auto a=position(cx(i-1),cy(i-1)),b=position(cx(i),cy(i));
      if(std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(b.x) && std::isfinite(b.y)) {
        draw->AddLine(a,b,packedColor(style.curve_color),float(style.line_width)*guiScale);++drawnSegments;
      } else finiteGeometry=false;
    }
    auto& points=result.points;
    for(std::size_t i=0;!generic && i<data.measured.size();++i) {
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
    if(fitPlotView.ticks)for(int axis=0;axis<2;++axis) {
      const auto& ticks=axis==0?xAxis:yAxis;
      auto& observed=axis==0?result.x_ticks:result.y_ticks;
      auto& minorObserved=axis==0?result.x_minor_ticks:result.y_minor_ticks;
      int precision=fitPlotView.nice_ticks?6:4;
      if(fitPlotView.nice_ticks) {
        char previous[48]{};
        for(const auto& tick:ticks)if(tick.major){char label[48];std::snprintf(label,sizeof(label),"%.6g",tick.value);if(std::string(previous)==label){precision=17;break;}std::snprintf(previous,sizeof(previous),"%s",label);}
      }
      double lastLabel=axis==0?-std::numeric_limits<double>::infinity():std::numeric_limits<double>::infinity();
      double lastMinor=-std::numeric_limits<double>::infinity();
      for(const auto& tick:ticks) {
        const ImVec2 point=axis==0?ImVec2{origin.x+float(tick.fraction)*size.x,end.y}:ImVec2{origin.x,end.y-float(tick.fraction)*size.y};
        if(!tick.major) {
          const auto coordinate=tick.fraction*(axis==0?size.x:size.y);
          if(coordinate-lastMinor<3*guiScale)continue;
          lastMinor=coordinate;
          draw->AddLine(point,axis==0?ImVec2{point.x,point.y+2}:ImVec2{point.x-2,point.y},canvas.text);
          if(headlessSessionActive)minorObserved.push_back({{"value",tick.value},{"point",{point.x,point.y}}});
          continue;
        }
        char label[48];std::snprintf(label,sizeof(label),"%.*g",precision,tick.value);const auto textSize=measureText(label);
        const ImVec2 text=axis==0?ImVec2{std::clamp(point.x-textSize.x/2,widgetOrigin.x,std::max(widgetOrigin.x,widgetOrigin.x+widgetSize.x-textSize.x)),point.y+4}:
          ImVec2{std::max(widgetOrigin.x,point.x-textSize.x-5),std::clamp(point.y-textSize.y/2,widgetOrigin.y,end.y-textSize.y)};
        const bool labelVisible=!fitPlotView.nice_ticks || (axis==0?text.x>=lastLabel+4*guiScale:text.y+textSize.y<=lastLabel-4*guiScale);
        draw->AddLine(point,axis==0?ImVec2{point.x,point.y+3}:ImVec2{point.x-3,point.y},canvas.text);
        if(labelVisible){draw->AddText(canvas.font,canvas.font_size,text,canvas.text,label);lastLabel=axis==0?text.x+textSize.x:text.y;}
        if(headlessSessionActive)observed.push_back({{"value",tick.value},{"label",label},{"point",{point.x,point.y}},
          {"label_visible",labelVisible},{"label_bounds",{text.x,text.y,text.x+textSize.x,text.y+textSize.y}}});
      }
    }
  return result;
}
} // namespace imagejxx::desktop
