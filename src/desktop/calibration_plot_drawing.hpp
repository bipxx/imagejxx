#pragma once
#include "desktop.hpp"
#include "calibration_plot_payload.hpp"
#include "calibration_plot_style.hpp"
#include "calibration_plot_view.hpp"
#include <imgui.h>

namespace imagejxx::desktop {
struct CalibrationPlotCanvas {
  ImVec2 origin,size,widget_origin,widget_size;
  ImFont* font=nullptr;
  float font_size=13,scale=1;
  ImU32 border=0,grid=0,text=0;
};
struct CalibrationPlotDrawing {
  Json points=Json::array(),x_ticks=Json::array(),y_ticks=Json::array(),model_overlay=nullptr;
  Json x_minor_ticks=Json::array(),y_minor_ticks=Json::array();
  std::size_t drawn_segments=0,masked_segments=0,grid_lines=0;
  bool finite_geometry=true;
};
CalibrationPlotDrawing drawCalibrationPlotGeometry(ImDrawList*,const ImageJIntensityFitPlot&,
    const CalibrationPlotAnnotation*,const CalibrationPlotView&,const CalibrationPlotStyle&,
    const CalibrationPlotCanvas&,bool observe);
std::size_t calibrationPlotImageBytes(const CalibrationPlotPayload&,int width,int height);
Json exportCalibrationPlotPng(const std::string&,const CalibrationPlotPayload&,
    const CalibrationPlotView&,const CalibrationPlotStyle&,int width,int height,RenderMemory&);
} // namespace imagejxx::desktop
