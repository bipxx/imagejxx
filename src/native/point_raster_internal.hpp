#pragma once
#include <imagejxx/point_raster.hpp>
namespace imagejxx::detail {
// Shared ingress admission: bindings prepare before reading NumPy pixels.
struct PreparedPointRaster {
  int width=0,height=0;
  PointPresentation layout;
  PointPresentationOptions view;
  std::size_t working_bytes=0,pixel_samples=0;
};
PreparedPointRaster prepare_point_raster(int width,int height,const Selection&,
    const PointPresentationOptions&,const PointRasterFont*,const PointRasterLimits&,
    PointPresentation* captured_layout=nullptr);
Image raster_point_style(const Image&,const PreparedPointRaster&,const PointRasterFont*);
}
