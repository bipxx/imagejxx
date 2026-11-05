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
}
