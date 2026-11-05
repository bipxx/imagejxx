#include <imagejxx/selection_transform.hpp>
#include <imagejxx/point_annotations.hpp>
#include <imagejxx/point_presentation.hpp>
#include "selection_region_rotation.hpp"
#include "kernel_control.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace imagejxx {
namespace {
using Point=std::array<double,2>;
std::size_t admission(const Selection &source,int width,int height,
                     const SelectionEditLimits &limits,const ExecutionControl &control) {
  const auto &kind=source.kind;
  if(kind!="point" && kind!="multipoint" && kind!="polygon" && kind!="freehand" &&
      kind!="polyline" && kind!="angle" && kind!="rectangle")
    throw std::invalid_argument("Convex Hull requires vertex geometry or an axis-aligned rectangle");
  const auto bytes=detail::admit_selection_geometry(source,width,height,limits,control);
  const auto count=kind=="rectangle"?4:source.points.size();
  if(count<3)throw std::invalid_argument("Convex Hull needs three non-collinear points");
  if(count>limits.max_vertices)throw std::length_error("Convex Hull exceeds vertex limit");
  for(const auto &point:source.points)for(double value:point)
    if(value>std::numeric_limits<float>::max() || value<std::numeric_limits<float>::lowest())
      throw std::invalid_argument("Convex Hull coordinate exceeds Float32 range");
  return bytes;
}
double cross(Point a,Point b,Point c) {
  return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);
}
bool vertex_bounds(const Selection &s) {
  return s.kind=="point" || s.kind=="multipoint" || s.kind=="polygon" ||
      s.kind=="freehand" || s.kind=="polyline" || s.kind=="angle";
}
std::size_t rectangle_admission(const Selection &s,int width,int height,
    const SelectionEditLimits &limits,const ExecutionControl &control) {
  const auto bytes=detail::admit_selection_geometry(s,width,height,limits,control);
  if(limits.max_vertices<2)throw std::length_error("Bounding Rectangle exceeds vertex limit");
  if(!s.is_mask() && !s.is_path() && s.points.empty())
    throw std::invalid_argument("Bounding Rectangle requires nonempty geometry");
  // Keep the coordinate conversion and inclusive endpoint arithmetic inside the
  // portable signed-32-bit ROI profile; do not inherit Java saturating casts.
  for(const auto &point:s.points)for(double value:point)
    if(value<std::numeric_limits<int>::min()+2.0 || value>std::numeric_limits<int>::max()-2.0)
      throw std::invalid_argument("Bounding Rectangle coordinate exceeds integer profile");
  return bytes;
}
}
std::size_t estimate_convex_hull_selection(const Selection &source,int width,int height,
                                         const SelectionEditLimits &limits) {
  return admission(source,width,height,limits,{});
}
Selection convex_hull_selection(const Selection &source,int width,int height,
    bool discard_point_metadata,const SelectionEditLimits &limits,const ExecutionControl &control) {
  const auto frozen_limits=limits;const auto frozen_control=control;
  admission(source,width,height,frozen_limits,frozen_control);
  if(!discard_point_metadata && (source.attributes.count(point_annotations_attribute) ||
      source.attributes.count(point_style_attribute)))
    throw std::invalid_argument("Convex Hull requires explicit discard of per-point metadata");
  const Selection frozen=source;
  ExecutionScope scope(frozen_control);kernel_checkpoint();execution_progress(0,1);kernel_checkpoint();
  std::vector<Point> points;
  if(frozen.kind=="rectangle") {
    const auto a=frozen.points[0],b=frozen.points[1];
    points={a,{a[0],b[1]},b,{b[0],a[1]}};
  } else points=frozen.points;
  for(auto &point:points)for(auto &value:point)value=static_cast<float>(value);
  std::size_t comparisons=0;
  std::sort(points.begin(),points.end(),[&](const Point &a,const Point &b) {
    if(!(++comparisons%1024))kernel_checkpoint();
    return a[0]<b[0] || (a[0]==b[0] && a[1]<b[1]);
  });
  points.erase(std::unique(points.begin(),points.end()),points.end());
  std::vector<Point> hull;hull.reserve(points.size()*2);
  for(const auto &point:points) {
    kernel_checkpoint();
    while(hull.size()>1 && cross(hull[hull.size()-2],hull.back(),point)<=0)hull.pop_back();
    hull.push_back(point);
  }
  const auto lower=hull.size();
  for(std::size_t i=points.size();i-->0;) {
    kernel_checkpoint();
    if(i+1==points.size())continue;
    while(hull.size()>lower && cross(hull[hull.size()-2],hull.back(),points[i])<=0)hull.pop_back();
    hull.push_back(points[i]);
  }
  if(hull.size()>1)hull.pop_back();
  if(hull.size()<3)throw std::invalid_argument("Convex Hull needs three non-collinear points");
  std::reverse(hull.begin(),hull.end());
  const auto first=std::min_element(hull.begin(),hull.end(),[](const Point &a,const Point &b) {
    return a[1]<b[1] || (a[1]==b[1] && a[0]<b[0]);
  });
  std::rotate(hull.begin(),first,hull.end());
  Selection result("polygon",std::move(hull));result.width=frozen.width;
  result.attributes=frozen.attributes;
  result.attributes.erase(point_annotations_attribute);result.attributes.erase(point_style_attribute);
  result.validate(width,height);kernel_checkpoint();execution_progress(1,1);kernel_checkpoint();
  return result;
}
std::size_t estimate_bounding_rectangle_selection(const Selection &source,int width,int height,
    const SelectionEditLimits &limits) {
  return rectangle_admission(source,width,height,limits,{});
}
Selection bounding_rectangle_selection(const Selection &source,int width,int height,
    bool discard_point_metadata,const SelectionEditLimits &limits,const ExecutionControl &control) {
  const auto frozen_limits=limits;const auto frozen_control=control;
  rectangle_admission(source,width,height,frozen_limits,frozen_control);
  if(!discard_point_metadata && (source.attributes.count(point_annotations_attribute) ||
      source.attributes.count(point_style_attribute)))
    throw std::invalid_argument("Bounding Rectangle requires explicit discard of per-point metadata");
  Selection frozen=source;
  ExecutionScope scope(frozen_control);kernel_checkpoint();execution_progress(0,1);kernel_checkpoint();
  if(vertex_bounds(frozen))for(auto &point:frozen.points) {
    kernel_checkpoint_periodic();for(auto &value:point)value=static_cast<float>(value);
  }
  // Reuse the editor's bounded traversal/analytic geometry, without an image
  // raster. Its own geometry copy remains within the existing admission bound.
  const SelectionEditor editor(frozen,width,height);
  auto b=editor.bounds();const auto &kind=frozen.kind;
  if(frozen.is_mask() && (b[2]<=b[0] || b[3]<=b[1]))
    throw std::invalid_argument("Bounding Rectangle requires nonempty geometry");
  const bool box=kind=="rectangle" || kind=="ellipse" || kind=="rounded_rectangle";
  const bool open=kind=="point" || kind=="multipoint" || kind=="line" || kind=="polyline" || kind=="angle";
  if(vertex_bounds(frozen)) {
    // Pinned FloatPolygon initializes its upper bound to Float.MIN_VALUE. Its
    // subtraction is Float32 before promotion into the ROI's binary64 bounds.
    for(int axis=0;axis<2;++axis) {
      const float hi=std::max(static_cast<float>(b[axis+2]),std::numeric_limits<float>::denorm_min());
      const float extent=hi-static_cast<float>(b[axis]);
      b[axis+2]=b[axis]+static_cast<double>(extent);
    }
  }
  for(int axis=0;axis<2;++axis) {
    double low,extent;
    if(box) {low=std::trunc(b[axis]);extent=std::ceil(b[axis+2]-b[axis]);}
    else if(open) {low=std::floor(b[axis]+.5);extent=std::floor(b[axis+2]+1.5)-low;}
    else {low=std::floor(b[axis]);extent=std::ceil(b[axis+2])-low;}
    extent=std::max(1.0,extent);
    if(!std::isfinite(low) || !std::isfinite(extent) || low<std::numeric_limits<int>::min() ||
        low>std::numeric_limits<int>::max() || extent>std::numeric_limits<int>::max() ||
        low+extent>std::numeric_limits<int>::max())
      throw std::invalid_argument("Bounding Rectangle exceeds integer profile");
    b[axis]=low;b[axis+2]=low+extent;
  }
  Selection result("rectangle",{{b[0],b[1]},{b[2],b[3]}});result.width=frozen.width;
  result.attributes=frozen.attributes;
  result.attributes.erase(point_annotations_attribute);result.attributes.erase(point_style_attribute);
  result.validate(width,height);kernel_checkpoint();execution_progress(1,1);kernel_checkpoint();return result;
}
}
