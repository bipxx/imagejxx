#include <imagejxx/selection_transform.hpp>
#include <imagejxx/selection_interpolation.hpp>
#include <imagejxx/selection_spline.hpp>
#include <imagejxx/point_annotations.hpp>
#include <imagejxx/point_presentation.hpp>
#include "selection_region_rotation.hpp"
#include "kernel_control.hpp"
#include "imagej_shape_plan.hpp"
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
std::vector<Point> ordered_hull(std::vector<Point> points,bool float_profile) {
  if(float_profile)for(auto &point:points)for(auto &value:point)value=static_cast<float>(value);
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
  return hull;
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
bool circle_area(const Selection &s) {
  return s.kind!="point" && s.kind!="multipoint" && s.kind!="line" && s.kind!="polyline" && s.kind!="angle";
}
std::size_t area_fit_admission(const Selection &s,int width,int height,
    const SelectionEditLimits &limits,const ExecutionControl &control,std::size_t raster_bytes_per_pixel=16) {
  auto bytes=rectangle_admission(s,width,height,limits,control);
  const auto pixels=std::size_t(width)*height;
  // Dense geometry mask plus packed/output staging; source geometry, polygon
  // vertices/intersections and fixed slack are already charged by admission.
  if(pixels>(limits.max_working_bytes-bytes)/raster_bytes_per_pixel)
    throw std::length_error("Selection fit geometry raster exceeds working limit");
  bytes+=pixels*raster_bytes_per_pixel;
  if(s.is_path() || s.is_rounded_box()) {
    ExecutionScope scope(control);
    auto charge=[&](const std::vector<double>& path) {
      const auto plan=detail::plan_imagej_shape_mask(path,width,height,{},limits.max_working_bytes-bytes);
      bytes+=plan.packed_bytes+plan.temporary_bytes;
    };
    if(s.is_path())charge(s.path_commands());
    else {const auto path=s.rounded_path(true);charge(std::vector<double>(path.begin(),path.end()));}
  }
  return bytes;
}
std::size_t circle_admission(const Selection &s,int width,int height,
    const SelectionEditLimits &limits,const ExecutionControl &control) {
  if(circle_area(s))return area_fit_admission(s,width,height,limits,control);
  auto bytes=rectangle_admission(s,width,height,limits,control);
  if(!circle_area(s)) {
    if(s.kind=="line" || s.points.size()<3)
      throw std::invalid_argument("Fit Circle requires at least three non-collinear point/segmented-line vertices");
    return bytes;
  }
  return bytes;
}
struct AreaMoments {std::size_t count=0;double sx=0,sy=0,cxx=0,cyy=0,cxy=0;};
AreaMoments mask_moments(const Image& mask,int width,bool second,std::array<int,2> origin={0,0}) {
  AreaMoments m;double lx=0,ly=0;
  for(int y=0;y<mask.height;++y) {
    double lineSum=0;std::size_t lineCount=0;
    for(int x=0;x<width;++x) {
      kernel_checkpoint_periodic();if(mask.pixels[std::size_t(y)*width+x]==0)continue;
      ++m.count;m.sx+=x;m.sy+=y;
      if(second){const double local=x-double(origin[0]);lineSum+=local;++lineCount;m.cxx+=local*local;}
    }
    if(second){const double local=y-double(origin[1]);lx+=lineSum;ly+=double(lineCount)*local;m.cxy+=lineSum*local;m.cyy+=local*local*double(lineCount);}
  }
  if(second && m.count) {
    const double n=double(m.count),mx=lx/n,my=ly/n;
    m.cxx=(m.cxx+.08333333*n)/n-mx*mx;m.cyy=(m.cyy+.08333333*n)/n-my*my;m.cxy=m.cxy/n-mx*my;
  }
  return m;
}
AreaMoments area_moments(const Selection& s,int width,int height,bool second,std::array<int,2> origin={0,0}) {
  const auto mask=s.mask(width,height);return mask_moments(mask,width,second,origin);
}
// Match the processor ROI's clipped local origin so floating moment ties are
// evaluated in the same coordinate frame as the pinned statistics method.
std::array<int,2> moment_origin(const Selection& s,int width,int height) {
  std::array<double,2> low;
  if(s.is_mask()){const auto o=s.mask_origin();low={double(o[0]),double(o[1])};}
  else if(s.kind=="point" || s.kind=="multipoint" || s.kind=="polygon" || s.kind=="freehand") {
    low={std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};
    for(const auto& p:s.points)for(int a=0;a<2;++a)low[a]=std::min(low[a],double(float(p[a])));
    for(auto& v:low)v=std::floor(v+((s.kind=="point" || s.kind=="multipoint")?.5:0));
  } else if(s.kind=="ellipse" || s.kind=="rounded_rectangle") {
    for(int a=0;a<2;++a)low[a]=std::trunc(std::min(s.points[0][a],s.points[1][a]));
  } else {const auto bounds=SelectionEditor(s,width,height).bounds();low={std::floor(bounds[0]),std::floor(bounds[1])};}
  return {int(std::clamp(low[0],0.,double(width))),int(std::clamp(low[1],0.,double(height)))};
}
bool point_selection(const Selection& s){return s.kind=="point" || s.kind=="multipoint";}
bool line_selection(const Selection& s){return s.kind=="line" || s.kind=="polyline" || s.kind=="angle";}
std::size_t ellipse_admission(const Selection& s,int width,int height,const SelectionEditLimits& limits,const ExecutionControl& control) {
  if(line_selection(s))throw std::invalid_argument("Fit Ellipse does not support line selections");
  if(limits.max_vertices<3)throw std::length_error("Fit Ellipse exceeds output vertex limit");
  if(s.kind=="rectangle" || (s.kind=="rounded_rectangle" && s.corner_diameter()==0))
    return rectangle_admission(s,width,height,limits,control);
  return area_fit_admission(s,width,height,limits,control,point_selection(s)?sizeof(double):16);
}
bool fit_rectangle_curve(const Selection& s) {
  return s.is_path() || s.is_rounded_box() || s.kind=="ellipse" || s.kind=="rotated_ellipse";
}
std::vector<double> fit_rectangle_path(const Selection& s) {
  if(s.is_path())return s.path_commands();
  if(s.is_rounded_box()){const auto p=s.rounded_path(true);return {p.begin(),p.end()};}
  auto frame=*s.box_frame();
  if(s.kind=="ellipse") {
    // OvalRoi's processing contour is based on its integer box, expanded by
    // .0004 before GeneralPath normalization (not its subpixel owner bounds).
    const double x=std::trunc(std::min(s.points[0][0],s.points[1][0]))-.0004;
    const double y=std::trunc(std::min(s.points[0][1],s.points[1][1]))-.0004;
    const double w=std::ceil(std::abs(s.points[1][0]-s.points[0][0]))+.0008;
    const double h=std::ceil(std::abs(s.points[1][1]-s.points[0][1]))+.0008;
    frame={{x+w/2,y+h/2},{w/2,0},{0,h/2}};
  }
  constexpr double k=.5522847498307933;
  std::vector<double> path;
  auto emit=[&](int op,std::initializer_list<Point> pts){path.push_back(op);for(auto q:pts){const auto p=frame.image_point(q);path.push_back(p[0]);path.push_back(p[1]);}};
  emit(0,{{1,0}});emit(3,{{1,k},{k,1},{0,1}});emit(3,{{-k,1},{-1,k},{-1,0}});
  emit(3,{{-1,-k},{-k,-1},{0,-1}});emit(3,{{k,-1},{1,-k},{1,0}});path.push_back(4);
  if(s.kind=="ellipse") {
    const Point origin{std::floor(frame.center[0]-frame.u[0]),std::floor(frame.center[1]-frame.v[1])};
    for(std::size_t i=0;i<path.size();) {const int op=int(path[i++]);const std::size_t count=op==4?0:op==3?6:2;for(std::size_t j=0;j<count;++j)path[i+j]-=origin[j%2];i+=count;}
  }
  return path;
}
std::size_t fit_rectangle_admission(const Selection& s,int w,int h,const SelectionEditLimits& limits,const ExecutionControl& control) {
  if(s.kind=="line")throw std::invalid_argument("Fit Rectangle does not support straight line selections");
  auto bytes=detail::admit_selection_geometry(s,w,h,limits,control);
  for(const auto& point:s.points)for(double v:point)if(std::abs(v)>std::numeric_limits<float>::max())throw std::invalid_argument("Fit Rectangle coordinate exceeds Float32 profile");
  if(limits.max_vertices<3)throw std::length_error("Fit Rectangle exceeds output vertex limit");
  std::size_t count=s.points.size();
  if(s.is_mask())count=std::size_t(s.mask_height())*4;
  else if(fit_rectangle_curve(s)) {
    ExecutionScope scope(control);const auto commands=fit_rectangle_path(s);
    auto roi=ImageJRoiLimits{};roi.max_vertices=limits.max_vertices;roi.max_mask_pixels=limits.max_xy_pixels;
    const auto plan=detail::plan_imagej_shape_outline(commands,w,h,roi,limits.max_working_bytes-bytes);
    count=plan.flattened_edges;
    const auto extra=plan.packed_bytes+plan.temporary_bytes;
    if(extra>limits.max_working_bytes-bytes)throw std::length_error("Fit Rectangle contour exceeds working limit");
    bytes+=extra;
  } else if(s.kind=="rectangle" || s.kind=="rotated_rectangle")count=4;
  if(count<3)throw std::invalid_argument("Fit Rectangle needs three non-collinear points");
  if(count>limits.max_vertices || count>(limits.max_working_bytes-bytes)/128)
    throw std::length_error("Fit Rectangle hull exceeds geometry limits");
  return bytes+count*128;
}
std::vector<Point> fit_rectangle_vertices(const Selection& s,int w,int h,const SelectionEditLimits& limits) {
  if(fit_rectangle_curve(s)) {
    auto roi=ImageJRoiLimits{};roi.max_vertices=limits.max_vertices;roi.max_mask_pixels=limits.max_xy_pixels;
    auto points=detail::imagej_shape_outline(fit_rectangle_path(s),w,h,roi,limits.max_working_bytes);
    if(s.kind=="ellipse") {
      const Point origin{std::floor(std::trunc(std::min(s.points[0][0],s.points[1][0]))-.0004),std::floor(std::trunc(std::min(s.points[0][1],s.points[1][1]))-.0004)};
      for(auto& p:points)for(int a=0;a<2;++a)p[a]=double(float(float(p[a])+float(origin[a])));
    }
    return points;
  }
  if(s.is_mask()) {
    std::vector<Point> points;points.reserve(std::size_t(s.mask_height())*4);
    const auto origin=s.mask_origin();const auto width=s.mask_width();const auto& packed=s.packed_mask();
    for(int y=0;y<s.mask_height();++y) {
      int left=width,right=-1;
      for(int x=0;x<width;++x){kernel_checkpoint_periodic();const auto i=std::size_t(y)*width+x;if(packed[i/8]&(0x80u>>(i%8))){left=std::min(left,x);right=x;}}
      if(right>=left){const double x0=double(origin[0])+left,x1=double(origin[0])+right+1,y0=double(origin[1])+y;points.insert(points.end(),{{x0,y0},{x1,y0},{x1,y0+1},{x0,y0+1}});}
    }return points;
  }
  if(s.kind=="rectangle" || s.kind=="rotated_rectangle") {
    const auto frame=*s.box_frame();return {frame.image_point({-1,-1}),frame.image_point({1,-1}),frame.image_point({1,1}),frame.image_point({-1,1})};
  }
  auto points=s.points;
  Point base{std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};
  for(const auto& p:points)for(int a=0;a<2;++a)base[a]=std::min(base[a],double(float(p[a])));
  for(auto& p:points)for(int a=0;a<2;++a){const float local=float(p[a])-float(base[a]);p[a]=float(local+float(base[a]));}
  return points;
}
std::array<double,3> circle_from_points(std::vector<Point>& points) {
  Point base{std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};
  for(const auto& p:points)for(int a=0;a<2;++a)base[a]=std::min(base[a],double(float(p[a])));
  Point origin{std::floor(base[0]+.5),std::floor(base[1]+.5)},mean{};
  for(auto& p:points) {
    kernel_checkpoint_periodic();
    for(int a=0;a<2;++a){const float relative=float(p[a])-float(base[a]);p[a]=std::floor(double(relative)+.5)+origin[a];mean[a]+=p[a];}
  }
  const double count=double(points.size());for(auto& v:mean)v/=count;
  double xx=0,yy=0,xy=0,xz=0,yz=0,zz=0;
  for(const auto& p:points) {
    kernel_checkpoint_periodic();const auto x=p[0]-mean[0],y=p[1]-mean[1],z=x*x+y*y;
    xx+=x*x;yy+=y*y;xy+=x*y;xz+=x*z;yz+=y*z;zz+=z*z;
  }
  xx/=count;yy/=count;xy/=count;xz/=count;yz/=count;zz/=count;
  const double trace=xx+yy,det=xx*yy-xy*xy;
  if(!(det>0) || !std::isfinite(det))throw std::invalid_argument("Fit Circle points are collinear or singular");
  // Pratt's constrained moment equation. Solve its root nearest zero with a
  // finite iteration bound; unlike legacy fallback, nonconvergence is explicit.
  const double c2=4*det-3*trace*trace-zz;
  const double c1=zz*trace+4*det*trace-xz*xz-yz*yz-trace*trace*trace;
  const double c0=xz*xz*yy+yz*yz*xx-zz*det-2*xz*yz*xy+trace*trace*det;
  double root=0;bool converged=false;
  for(int step=0;step<20;++step) {
    kernel_checkpoint();const double value=c0+root*(c1+root*(c2+4*root*root));
    if(value==0){converged=true;break;}
    const double derivative=c1+root*(2*c2+16*root*root);
    const double next=root-value/derivative;
    if(!std::isfinite(next) || next<0)break;
    if(next==root || (next!=0 && std::abs((next-root)/next)<1e-12)){root=next;converged=true;break;}
    root=next;
  }
  if(!converged)throw std::invalid_argument("Fit Circle did not converge");
  const double determinant=root*root-root*trace+det;
  const double x=(xz*(yy-root)-yz*xy)/(2*determinant),y=(yz*(xx-root)-xz*xy)/(2*determinant);
  const double radius=std::sqrt(x*x+y*y+trace+2*root);
  if(!std::isfinite(radius) || radius<=0)throw std::invalid_argument("Fit Circle is singular");
  return {x+mean[0],y+mean[1],radius};
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
  auto hull=ordered_hull(std::move(points),true);
  Selection result("polygon",std::move(hull));result.width=frozen.width;
  result.attributes=frozen.attributes;
  result.attributes.erase(point_annotations_attribute);result.attributes.erase(point_style_attribute);result.attributes.erase(spline_fit_attribute);
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
  result.attributes.erase(point_annotations_attribute);result.attributes.erase(point_style_attribute);result.attributes.erase(spline_fit_attribute);
  result.validate(width,height);kernel_checkpoint();execution_progress(1,1);kernel_checkpoint();return result;
}
std::size_t estimate_fit_circle_selection(const Selection &source,int width,int height,
    const SelectionEditLimits &limits) {return circle_admission(source,width,height,limits,{});}
Selection fit_circle_selection(const Selection &source,int width,int height,bool discard_point_metadata,
    const SelectionEditLimits &limits,const ExecutionControl &control) {
  const auto frozen_limits=limits;const auto frozen_control=control;
  circle_admission(source,width,height,frozen_limits,frozen_control);
  if(!discard_point_metadata && (source.attributes.count(point_annotations_attribute) || source.attributes.count(point_style_attribute)))
    throw std::invalid_argument("Fit Circle requires explicit discard of per-point metadata");
  Selection frozen=source;ExecutionScope scope(frozen_control);kernel_checkpoint();execution_progress(0,1);kernel_checkpoint();
  std::array<double,3> circle;
  if(circle_area(frozen)) {
    const auto m=area_moments(frozen,width,height,false);
    if(!m.count)throw std::invalid_argument("Fit Circle requires nonempty area on the canvas");
    circle={m.sx/double(m.count)+.5,m.sy/double(m.count)+.5,std::sqrt(double(m.count)/3.14159265358979323846)};
  } else circle=circle_from_points(frozen.points);
  const double x=std::floor(circle[0]-circle[2]+.5),y=std::floor(circle[1]-circle[2]+.5),d=std::max(1.,std::floor(2*circle[2]+.5));
  for(double v:{x,y,d,x+d,y+d})if(!std::isfinite(v) || v<std::numeric_limits<int>::min() || v>std::numeric_limits<int>::max())
    throw std::invalid_argument("Fit Circle exceeds integer output profile");
  Selection result("ellipse",{{x,y},{x+d,y+d}});result.width=frozen.width;result.attributes=frozen.attributes;
  result.attributes.erase(point_annotations_attribute);result.attributes.erase(point_style_attribute);result.attributes.erase(spline_fit_attribute);
  result.validate(width,height);kernel_checkpoint();execution_progress(1,1);kernel_checkpoint();return result;
}
std::size_t estimate_fit_ellipse_selection(const Selection& s,int width,int height,const SelectionEditLimits& limits) {
  return ellipse_admission(s,width,height,limits,{});
}
Selection fit_ellipse_selection(const Selection& source,int width,int height,bool discard_point_metadata,
    const SelectionEditLimits& limits,const ExecutionControl& control) {
  const auto frozen_limits=limits;const auto frozen_control=control;
  ellipse_admission(source,width,height,frozen_limits,frozen_control);
  if(!discard_point_metadata && (source.attributes.count(point_annotations_attribute) || source.attributes.count(point_style_attribute)))
    throw std::invalid_argument("Fit Ellipse requires explicit discard of per-point metadata");
  const Selection frozen=source;ExecutionScope scope(frozen_control);kernel_checkpoint();execution_progress(0,1);kernel_checkpoint();
  constexpr double pi=3.14159265358979323846;double cx,cy,major,minor,angle=0;
  const bool rectangle=frozen.kind=="rectangle" || (frozen.kind=="rounded_rectangle" && frozen.corner_diameter()==0);
  if(rectangle) {
    const auto& p=frozen.points;
    const double x=std::trunc(std::min(p[0][0],p[1][0])),y=std::trunc(std::min(p[0][1],p[1][1]));
    const double left=std::max(0.,x),top=std::max(0.,y),right=std::min(double(width),x+std::ceil(std::abs(p[1][0]-p[0][0]))),bottom=std::min(double(height),y+std::ceil(std::abs(p[1][1]-p[0][1])));
    if(right<=left || bottom<=top)throw std::invalid_argument("Fit Ellipse requires nonempty area on the canvas");
    cx=(left+right)/2;cy=(top+bottom)/2;major=2*(right-left)/1.772453851;minor=2*(bottom-top)/1.772453851;
    if(major<minor){std::swap(major,minor);angle=pi/2;}
  } else {
    AreaMoments m;const auto origin=moment_origin(frozen,width,height);
    if(point_selection(frozen)) {
      Point base{std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};
      for(const auto& p:frozen.points)for(int a=0;a<2;++a)base[a]=std::min(base[a],double(float(p[a])));
      // Sparse cell marks, not draw_line's per-point full-image copy or styled
      // glyph footprint. This is the PointRoi processing-mask convention.
      Image mask(width,height,1,PixelType::UInt8);kernel_checkpoint();
      for(const auto& p:frozen.points) {
        kernel_checkpoint_periodic();Point q;
        for(int a=0;a<2;++a){const float relative=float(p[a])-float(base[a]);q[a]=std::floor(double(relative)+base[a]+.5);}
        if(q[0]>=0 && q[0]<width && q[1]>=0 && q[1]<height)mask.pixels[std::size_t(q[1])*width+std::size_t(q[0])]=255;
      }
      m=mask_moments(mask,width,true,origin);
    } else m=area_moments(frozen,width,height,true,origin);
    if(!m.count)throw std::invalid_argument("Fit Ellipse requires nonempty area on the canvas");
    const double n=double(m.count),xx=m.cxx,yy=m.cyy,xy=m.cxy;
    const double root=std::hypot(xx-yy,2*xy),large=(xx+yy+root)/2,small=(xx*yy-xy*xy)/large;
    if(!(small>0) || !std::isfinite(large))throw std::invalid_argument("Fit Ellipse moments are singular");
    cx=m.sx/n+.5;cy=m.sy/n+.5;
    // Inverse concentration coefficients retain the legacy zero-difference
    // regularization in its original units. General orientation/conditioning
    // follows the symmetric moment matrix; projected ratio preserves tie output.
    const double scale=std::max(4*std::abs(xx*yy-xy*xy),.000001),a=yy/scale,c=xx/scale,cross=xy/scale;
    double difference=a-c;if(difference==0)difference=.000001;
    constexpr double halfPi=1.5707963267949;
    angle=.5*std::atan(2*cross/difference);if(angle<0)angle+=halfPi;if(cross>0)angle+=halfPi;
    if(cross==0)angle=c>a?0:a!=c?halfPi:0;
    double sine=std::sin(angle);if(sine==0)sine=.000001;
    const double projection=cross*std::cos(angle)/sine,quotient=std::abs((a-projection)/(c+projection));
    const double ratio=std::pow(std::max(quotient,1/quotient),.25),diameter=2*std::sqrt(n/pi);
    major=diameter*ratio;minor=diameter/ratio;
    if(180*angle/pi==180)angle=0;
    if(point_selection(frozen) && frozen.points.size()==1)major=minor=2/1.772453851;
  }
  const double ux=std::cos(angle)*major/2,uy=-std::sin(angle)*major/2,vx=std::sin(angle)*minor/2,vy=std::cos(angle)*minor/2;
  Selection result("rotated_ellipse",{{cx,cy},{cx+ux,cy+uy},{cx+vx,cy+vy}});result.width=frozen.width;result.attributes=frozen.attributes;
  result.attributes.erase(point_annotations_attribute);result.attributes.erase(point_style_attribute);result.attributes.erase(spline_fit_attribute);
  result.validate(width,height);kernel_checkpoint();execution_progress(1,1);kernel_checkpoint();return result;
}

std::size_t estimate_fit_rectangle_selection(const Selection& s,int w,int h,const SelectionEditLimits& limits) {
  return fit_rectangle_admission(s,w,h,limits,{});
}
Selection fit_rectangle_selection(const Selection& source,int w,int h,bool discard_point_metadata,
    const SelectionEditLimits& limits,const ExecutionControl& control) {
  const auto frozen_limits=limits;const auto frozen_control=control;
  fit_rectangle_admission(source,w,h,frozen_limits,frozen_control);
  if(!discard_point_metadata && (source.attributes.count(point_annotations_attribute) || source.attributes.count(point_style_attribute)))
    throw std::invalid_argument("Fit Rectangle requires explicit discard of per-point metadata");
  const Selection frozen=source;ExecutionScope scope(frozen_control);kernel_checkpoint();execution_progress(0,1);kernel_checkpoint();
  const bool legacy=frozen.kind!="rotated_rectangle" && frozen.kind!="rotated_ellipse" && !frozen.is_mask();
  auto hull=ordered_hull(fit_rectangle_vertices(frozen,w,h,frozen_limits),legacy);
  if(hull.size()>ImageJRoiLimits{}.max_path_work/hull.size())throw std::length_error("Fit Rectangle hull projection work limit");
  // Examine each edge in the established topmost/leftmost hull order. Strict
  // improvement preserves first-edge ties; Float32 differences/products retain
  // the processing profile while native masks/frames keep binary64 arithmetic.
  double best=std::numeric_limits<double>::infinity(),bestAngle=0,bestSigned=0,bestLo=0,bestHi=0;Point anchor{};
  auto sub=[&](double a,double b){return legacy?double(float(float(a)-float(b))):a-b;};
  auto product=[&](double a,double b){return legacy?double(float(float(a)*float(b))):a*b;};
  auto sum=[&](double a,double b){return legacy?double(float(float(a)+float(b))):a+b;};
  for(std::size_t i=0;i<hull.size();++i) {
    kernel_checkpoint();const auto a=hull[i],b=hull[(i+1)%hull.size()];
    const double dx=sub(b[0],a[0]),dy=sub(b[1],a[1]),length=legacy?std::sqrt(dx*dx+dy*dy):std::hypot(dx,dy);
    if(!(length>0) || !std::isfinite(length))throw std::invalid_argument("Fit Rectangle hull edge is singular");
    double low=0,high=0,far=0;
    for(const auto& p:hull) {
      kernel_checkpoint_periodic();const double x=sub(p[0],a[0]),y=sub(p[1],a[1]);
      const double parallel=sum(product(dx,x),product(dy,y))/length;
      const double normal=sub(product(dx,y),product(x,dy))/length;
      low=std::min(low,parallel);high=std::max(high,parallel);if(std::abs(normal)>std::abs(far))far=normal;
    }
    const double area=std::abs(far)*(high-low);
    if(area<best){best=area;bestAngle=std::atan2(dy,dx);bestSigned=far;bestLo=low;bestHi=high;anchor=a;}
  }
  if(!(best>0) || !std::isfinite(best))throw std::invalid_argument("Fit Rectangle points are collinear or singular");
  Point center{anchor[0]+std::cos(bestAngle)*(bestHi+bestLo)/2-std::sin(bestAngle)*bestSigned/2,
               anchor[1]+std::sin(bestAngle)*(bestHi+bestLo)/2+std::cos(bestAngle)*bestSigned/2};
  double axis=bestHi-bestLo,span=std::abs(bestSigned);
  if(span>std::abs(axis)){bestAngle-=std::acos(-1.)/2;span=std::abs(axis);axis=bestSigned;}
  if(bestAngle*axis>0)axis=-axis;
  // u points to the first/top control endpoint, v to its perpendicular side.
  const Point u{std::cos(bestAngle)*axis/2,std::sin(bestAngle)*axis/2};
  const double length=std::hypot(u[0],u[1]);const Point v{-u[1]*span/(2*length),u[0]*span/(2*length)};
  Selection result("rotated_rectangle",{center,{center[0]+u[0],center[1]+u[1]},{center[0]+v[0],center[1]+v[1]}});
  result.width=frozen.width;result.attributes=frozen.attributes;result.attributes.erase(point_annotations_attribute);result.attributes.erase(point_style_attribute);result.attributes.erase(spline_fit_attribute);
  result.validate(w,h);kernel_checkpoint();execution_progress(1,1);kernel_checkpoint();return result;
}

}


// Interpolate uses circle/chord spacing. Spline straightening deliberately uses
// a different, accumulated arc-length rule and must not share that algorithm.
namespace imagejxx {
namespace {
struct InterpolationPlan {
  Selection source;
  std::vector<Point> points;
  SelectionInterpolationOptions options;
  std::string source_kind;
  std::size_t capacity,bytes;
  bool open;
};
void interpolation_float_bounds(std::vector<Point>& points) {
  Point base{std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};
  for(auto& p:points)for(int a=0;a<2;++a){p[a]=float(p[a]);base[a]=std::min(base[a],p[a]);}
  for(auto& p:points)for(int a=0;a<2;++a)p[a]=float(float(float(p[a])-float(base[a]))+float(base[a]));
}
std::size_t prepare_interpolation_points(std::vector<Point>& points,bool open,
    bool normalize,bool smooth,const SelectionInterpolationOptions& options,
    const SelectionEditLimits& limits,std::size_t& bytes) {
  if(points.size()<(open?2u:3u))throw std::invalid_argument("Interpolation contour has too few vertices");
    for(const auto& p:points)for(double v:p)if(std::abs(v)>1e9)
      throw std::invalid_argument("Interpolation coordinate range");
    if(normalize)interpolation_float_bounds(points);
    else for(auto& p:points)for(auto& v:p)v=float(v);
    if(smooth) {
      for(std::size_t i=1;i+2<points.size();++i)
        for(int a=0;a<2;++a)points[i][a]=float(float(float(points[i-1][a])+float(points[i][a])+float(points[i+1][a]))/3.f);
      for(int a=0;a<2;++a)points[0][a]=float(float(float(points.back()[a])+float(points[0][a])+float(points[1][a]))/3.f);
      for(int a=0;a<2;++a)points.back()[a]=float(float(float(points[points.size()-2][a])+float(points.back()[a])+float(points[0][a]))/3.f);
    }
    double length=0;
    for(std::size_t i=1;i<points.size()+std::size_t(!open);++i) {
      kernel_checkpoint_periodic();const auto& a=points[i-1];const auto& b=points[i%points.size()];
      const double dx=float(b[0])-float(a[0]),dy=float(b[1])-float(a[1]);length+=std::sqrt(dx*dx+dy*dy);
    }
    const double required=10+length*1.5/std::abs(options.interval);
    const auto limit=std::min(limits.max_vertices,std::size_t(65536));
    if(!std::isfinite(required) || required>double(limit)+10)
      throw std::length_error("Interpolation output vertex limit");
    const auto capacity=std::size_t(required);
    if(capacity>(limits.max_working_bytes-bytes)/64)throw std::length_error("Interpolation output workspace limit");
    bytes+=capacity*64;
    if(!open)points.push_back(points.front());
  return capacity;
}
InterpolationPlan interpolation_plan(const Selection& source,int width,int height,
    const SelectionInterpolationOptions& options,const SelectionEditLimits& limits,
    const ExecutionControl& control) {
  if(!std::isfinite(options.interval) || std::abs(options.interval)<.01)
    throw std::invalid_argument("Interpolation interval magnitude must be at least0.01 pixels");
  if(source.kind!="polygon" && source.kind!="polyline" && source.kind!="freehand" &&
      source.kind!="line" && source.kind!="rectangle")
    throw std::invalid_argument("Interpolate requires supported continuous or mask geometry");
  auto bytes=detail::admit_selection_geometry(source,width,height,limits,control);
  const auto count=source.kind=="rectangle"?std::size_t(4):source.points.size();
  if(count>65536 || count>limits.max_vertices)throw std::length_error("Interpolation input vertex limit");
  if(bytes>limits.max_working_bytes || limits.max_working_bytes-bytes<4096 ||
      count>(limits.max_working_bytes-bytes-4096)/64)
    throw std::length_error("Interpolation source workspace limit");
  bytes+=4096+count*64;
  ExecutionScope planning(control);
  return detail::without_execution_callbacks([&] {
    validate_selection_spline_fit(source);
    const bool fitted=source.attributes.count(spline_fit_attribute)!=0;
    auto kind=source.kind;
    if(fitted)kind=selection_spline_fit(source)->controls.kind;
    std::vector<Point> points;
    if(source.kind=="rectangle") {
      const auto& a=source.points[0];const auto& b=source.points[1];
      const double x=std::min(a[0],b[0]),y=std::min(a[1],b[1]),right=std::max(a[0],b[0]),bottom=std::max(a[1],b[1]);
      points={{x,y},{right,y},{right,bottom},{x,bottom}};
    } else points=source.points;
    const bool open=source.kind=="polyline" || source.kind=="line";
    const auto capacity=prepare_interpolation_points(points,open,
        !fitted && source.kind!="line" && source.kind!="rectangle",
        options.smooth && kind=="freehand",options,limits,bytes);
    return InterpolationPlan{source,std::move(points),options,std::move(kind),capacity,bytes,open};
  });
}
std::optional<Point> interpolation_intersection(Point a,Point b,Point center,double radius) {
  const double dxAC=center[0]-a[0],dyAC=center[1]-a[1];
  const double lenAC=std::sqrt(dxAC*dxAC+dyAC*dyAC),dx=b[0]-a[0],dy=b[1]-a[1];
  const double extent=std::sqrt(dx*dx+dy*dy),angle=std::atan2(dy,dx);
  const double turn=angle-std::atan2(dyAC,dxAC),x=lenAC*std::cos(turn),y=lenAC*std::sin(turn);
  if(std::abs(y)>radius)return {};
  const double half=std::sqrt(radius*radius-y*y);
  for(double offset:{x-half,x+half})if(offset>=0 && offset<=extent)
    return Point{std::cos(angle)*offset+a[0],std::sin(angle)*offset+a[1]};
  // An endpoint lying on the circle can miss the segment by a few libm ULPs.
  // Retain that endpoint, without extending the contour or changing spacing.
  const double ex=b[0]-center[0],ey=b[1]-center[1];
  const double squared=ex*ex+ey*ey,rr=radius*radius;
  if(std::abs(squared-rr)<=8*std::numeric_limits<double>::epsilon()*std::max(squared,rr))return b;
  return {};
}
Selection interpolate_planned(InterpolationPlan plan,int width,int height,const SelectionEditLimits& limits,std::size_t* shared_work=nullptr) {
  const double interval=std::abs(plan.options.interval);
  const bool adjust=(plan.options.interval<0)!=plan.options.adjust_to_match;
  double trial_interval=interval,best_interval=0,min_diff=1e9;
  std::vector<Point> output;output.reserve(plan.capacity);
  std::size_t local_work=0;auto& work=shared_work?*shared_work:local_work;
  for(int trial=0;trial<=50;) {
    output.clear();output.push_back(plan.points.front());std::size_t segment=0;auto a=plan.points.front();
    while(segment+1<plan.points.size()) {
      kernel_checkpoint_periodic();
      if(++work>64u*1024u*1024u)throw std::length_error("Interpolation work limit");
      const auto next=interpolation_intersection(a,plan.points[segment+1],output.back(),trial_interval);
      if(next) {
        if(output.size()>=plan.capacity || output.size()>=limits.max_vertices || output.size()>=65536)
          throw std::length_error("Interpolation output vertex limit");
        a=*next;output.push_back(a);
      } else a=plan.points[++segment];
    }
    if(output.size()+1>plan.capacity)throw std::length_error("Interpolation endpoint workspace limit");
    output.push_back(plan.points.back());
    if(!adjust){if(plan.open)output.pop_back();break;}
    const auto& lastFrom=output[output.size()-2];const auto& lastTo=output.back();
    const double dx=lastFrom[0]-lastTo[0],dy=lastFrom[1]-lastTo[1],diff=std::sqrt(dx*dx+dy*dy)-trial_interval;
    if(std::abs(diff)<min_diff){min_diff=std::abs(diff);best_interval=trial_interval;}
    trial_interval+=.66*diff/double(output.size()-1);
    if((trial_interval<.8*interval || std::abs(diff)<.05 || trial==49) && trial<50) {
      trial=50;trial_interval=best_interval;
    } else ++trial;
  }
  if(!plan.open)output.pop_back();
  if(output.size()<(plan.open?2u:3u))
    throw std::invalid_argument("Interpolated geometry has too few vertices for a native editable selection");
  interpolation_float_bounds(output);
  Selection result=std::move(plan.source);
  const bool polygon=(plan.source_kind=="polygon" && plan.options.interval>1) ||
      ((plan.source_kind=="rectangle" || plan.source_kind=="freehand") && plan.options.interval>=8);
  result.kind=plan.open?"polyline":polygon?"polygon":"freehand";
  result.points=std::move(output);result.attributes.erase(spline_fit_attribute);
  result.validate(width,height);return result;
}

bool compound_interpolation_source(const Selection& s) {
  return s.is_path() || s.is_mask() || s.is_rounded_box() || s.kind=="ellipse" ||
      s.kind=="rotated_ellipse" || s.kind=="rotated_rectangle";
}
void interpolation_charge(std::size_t& bytes,std::size_t count,std::size_t unit,const SelectionEditLimits& limits) {
  if(bytes>limits.max_working_bytes || count>(limits.max_working_bytes-bytes)/unit)
    throw std::length_error("Compound interpolation workspace limit");
  bytes+=count*unit;
}
void interpolation_work(std::size_t& work) {
  if(++work>64u*1024u*1024u)throw std::length_error("Compound interpolation work limit");
  kernel_checkpoint_periodic();
}
// The existing mask wire exporter uses row rectangles, not ordered boundaries.
// Extract the first actual pixel-cell boundary owner here, then use the same
// contour/sampling/path owners as continuous selections. Four-connected islands
// remain distinct even at a diagonal contact; even-odd fill retains holes.
detail::ImageJShapeContours interpolation_mask_contours(const Selection& source,
    const SelectionEditLimits& limits,std::size_t& bytes,std::size_t& work) {
  const int width=source.mask_width(),height=source.mask_height();const auto origin=source.mask_origin();
  for(double v:{double(origin[0]),double(origin[1]),double(origin[0])+width,double(origin[1])+height})
    if(std::abs(v)>1e9)throw std::invalid_argument("Mask contour coordinate range");
  const auto& packed=source.packed_mask();
  auto member=[&](int x,int y) {
    if(x<0 || y<0 || x>=width || y>=height)return false;
    const auto at=std::size_t(y)*width+x;return bool(packed[at/8]&(0x80u>>(at%8)));
  };
  auto visit=[&](auto emit) {
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
      interpolation_work(work);if(!member(x,y))continue;
      if(!member(x,y-1))emit(x,y,x+1,y);
      if(!member(x+1,y))emit(x+1,y,x+1,y+1);
      if(!member(x,y+1))emit(x+1,y+1,x,y+1);
      if(!member(x-1,y))emit(x,y+1,x,y);
    }
  };
  std::size_t count=0;const auto maximum=std::min(limits.max_vertices,std::size_t(65536));
  visit([&](int,int,int,int){if(++count>maximum)throw std::length_error("Mask boundary vertex limit");});
  if(!count)throw std::invalid_argument("Interpolate requires a nonempty mask boundary");
  interpolation_charge(bytes,count,192,limits);
  struct GridEdge {std::array<int,2> a,b;};
  std::vector<GridEdge> edges;edges.reserve(count);
  visit([&](int x,int y,int r,int b){edges.push_back({{x,y},{r,b}});});
  std::sort(edges.begin(),edges.end(),[&](const auto& a,const auto& b){interpolation_work(work);return a.a<b.a || (a.a==b.a && a.b<b.b);});
  std::vector<unsigned char> used(count,0);
  detail::ImageJShapeContours out;out.points.reserve(count);out.offsets.reserve(count/4+1);
  auto direction=[](const GridEdge& e){return e.b[0]>e.a[0]?0:e.b[1]>e.a[1]?1:e.b[0]<e.a[0]?2:3;};
  for(std::size_t first=0;first<count;++first)if(!used[first]) {
    std::vector<Point> loop;std::size_t current=first;
    for(;;) {
      interpolation_work(work);if(used[current])throw std::invalid_argument("Mask boundary cycle mismatch");
      const auto edge=edges[current];used[current]=1;
      loop.push_back({double(origin[0])+edge.a[0],double(origin[1])+edge.a[1]});
      if(edge.b==edges[first].a)break;
      auto next=std::lower_bound(edges.begin(),edges.end(),edge.b,[&](const GridEdge& e,const std::array<int,2>& point){interpolation_work(work);return e.a<point;});
      std::size_t selected=count;int best=5;
      for(;next!=edges.end() && next->a==edge.b;++next) {
        const auto index=std::size_t(next-edges.begin());if(used[index])continue;
        const int turn=(direction(*next)-direction(edge)+4)%4;
        const int rank=turn==1?0:turn==0?1:turn==3?2:3;
        if(rank<best){best=rank;selected=index;}
      }
      if(selected==count)throw std::invalid_argument("Mask boundary is not closed");current=selected;
    }
    out.offsets.push_back(out.points.size());
    // Remove only collinear grid vertices, not corners or diagonal contacts.
    for(std::size_t i=0;i<loop.size();++i) {
      const auto& a=loop[(i+loop.size()-1)%loop.size()];const auto& b=loop[i];const auto& c=loop[(i+1)%loop.size()];
      if((b[0]-a[0])*(c[1]-b[1])!=(b[1]-a[1])*(c[0]-b[0]))out.points.push_back(b);
    }
    const auto begin=out.offsets.back();double area=0;
    for(std::size_t i=begin;i<out.points.size();++i){const auto& a=out.points[i];const auto& b=out.points[i+1==out.points.size()?begin:i+1];area+=(a[0]-out.points[begin][0])*(b[1]-out.points[begin][1])-(b[0]-out.points[begin][0])*(a[1]-out.points[begin][1]);}
    if(area<0)std::reverse(out.points.begin()+begin+1,out.points.end());
  }
  out.offsets.push_back(out.points.size());return out;
}
bool interpolation_smooth_component(const std::vector<Point>& points,std::size_t& work) {
  if(points.size()<3)throw std::invalid_argument("Interpolation contour has too few vertices");
  bool grid=true;double length=0;
  for(std::size_t i=0;i<points.size();++i) {
    interpolation_work(work);const auto a=points[i],b=points[(i+1)%points.size()];const double dx=b[0]-a[0],dy=b[1]-a[1];
    length+=std::sqrt(dx*dx+dy*dy);
    grid=grid && (dx==0 || dy==0) && a[0]==std::floor(a[0]) && a[1]==std::floor(a[1]);
  }
  const auto n=points.size();const bool long_edges=length/(double(n)*std::sqrt(double(n)))>=2;
  return (grid && !long_edges) || (n>10 && !long_edges);
}
struct CompoundInterpolationPlan {Selection result;std::size_t bytes;};
CompoundInterpolationPlan compound_interpolation_plan(const Selection& source,int width,int height,
    const SelectionInterpolationOptions& options,const SelectionEditLimits& limits,const ExecutionControl& control) {
  if(!std::isfinite(options.interval) || std::abs(options.interval)<.01)
    throw std::invalid_argument("Interpolation interval magnitude must be at least0.01 pixels");
  auto bytes=detail::admit_selection_geometry(source,width,height,limits,control);
  interpolation_charge(bytes,1,4096,limits);
  ExecutionScope planning(control);
  return detail::without_execution_callbacks([&] {
    std::size_t work=0;detail::ImageJShapeContours contours;
    if(source.is_mask())contours=interpolation_mask_contours(source,limits,bytes,work);
    else {
      std::size_t scalars=source.is_path()?source.path_commands().size():source.is_rounded_box()?44:source.kind=="rotated_rectangle"?13:32;
      interpolation_charge(bytes,scalars,2*sizeof(double),limits);
      std::vector<double> commands;
      if(source.kind=="rotated_rectangle") {
        const auto frame=*source.box_frame();
        for(const auto& q:std::vector<Point>{{-1,-1},{1,-1},{1,1},{-1,1}}) {
          const auto p=frame.image_point(q);commands.insert(commands.end(),{commands.empty()?0.:1.,p[0],p[1]});
        }commands.push_back(4);
      } else commands=fit_rectangle_path(source);
      auto roi=ImageJRoiLimits{};roi.max_vertices=std::min(limits.max_vertices,std::size_t(65536));roi.max_mask_pixels=limits.max_xy_pixels;
      const auto budget=limits.max_working_bytes-bytes;
      const auto shape=detail::plan_imagej_shape_contours(commands,width,height,roi,budget);
      if(shape.work_units>(64u*1024u*1024u-work)/2)throw std::length_error("Compound interpolation shape work limit");
      work+=shape.work_units*2;
      interpolation_charge(bytes,shape.packed_bytes+shape.temporary_bytes,1,limits);
      contours=detail::imagej_shape_contours(commands,width,height,roi,budget);
      interpolation_charge(bytes,contours.points.size(),192,limits);
      if(source.kind=="ellipse") {
        const Point origin{std::floor(std::trunc(std::min(source.points[0][0],source.points[1][0]))-.0004),std::floor(std::trunc(std::min(source.points[0][1],source.points[1][1]))-.0004)};
        for(auto& p:contours.points)for(int axis=0;axis<2;++axis)p[axis]=double(float(float(p[axis])+float(origin[axis])));
      }
    }
    if(contours.offsets.size()<2 || contours.points.empty())throw std::invalid_argument("Interpolate requires nonempty contours");
    const auto components=contours.offsets.size()-1;interpolation_charge(bytes,components,128,limits);
    std::vector<InterpolationPlan> curves;curves.reserve(components);std::size_t capacities=0;
    for(std::size_t i=0;i<components;++i) {
      std::vector<Point> points(contours.points.begin()+contours.offsets[i],contours.points.begin()+contours.offsets[i+1]);
      const bool analytic=source.is_rounded_box() || source.kind=="ellipse" || source.kind=="rotated_ellipse";
      // A single ShapeRoi uses its own direct flattened polygon, not the
      // inferred PolygonRoi produced by getRois() in the multi-component branch.
      const bool direct_shape=source.is_path() && components==1;
      const bool smooth=!analytic && !direct_shape && interpolation_smooth_component(points,work);
      const auto capacity=prepare_interpolation_points(points,false,!analytic && !direct_shape,options.smooth && smooth,options,limits,bytes);
      capacities+=capacity;
      curves.push_back({Selection("polygon"),std::move(points),options,smooth?"freehand":"polygon",capacity,0,false});
    }
    std::vector<double> commands;commands.reserve(3*std::min({capacities,limits.max_vertices,std::size_t(65536)})+components);
    std::size_t segments=0;
    for(auto& curve:curves) {
      const auto sampled=interpolate_planned(std::move(curve),width,height,limits,&work);
      const auto n=sampled.points.size();
      if(segments>=limits.max_vertices || n>limits.max_vertices-segments-1 || segments+n+1>65536)
        throw std::length_error("Compound interpolation aggregate vertex limit");
      segments+=n+1;
      for(std::size_t i=0;i<n;++i)commands.insert(commands.end(),{i?1.:0.,sampled.points[i][0],sampled.points[i][1]});
      commands.push_back(4);
    }
    auto result=Selection::from_path(commands);result.width=source.width;result.attributes=source.attributes;
    result.attributes.erase(spline_fit_attribute);if(source.is_rounded_box())result.attributes.erase("corner_diameter");
    result.validate(width,height);return CompoundInterpolationPlan{std::move(result),bytes};
  });
}

}
std::size_t estimate_interpolated_selection(const Selection& source,int width,int height,
    const SelectionInterpolationOptions& options,const SelectionEditLimits& limits) {
  if(source.kind=="point" || source.kind=="multipoint")
    return detail::admit_selection_geometry(source,width,height,limits,{});
  return compound_interpolation_source(source)?compound_interpolation_plan(source,width,height,options,limits,{}).bytes:
      interpolation_plan(source,width,height,options,limits,{}).bytes;
}
Selection interpolate_selection(const Selection& source,int width,int height,
    const SelectionInterpolationOptions& options,const SelectionEditLimits& limits,const ExecutionControl& control) {
  const auto frozen_options=options;const auto frozen_limits=limits;const auto frozen_control=control;
  // ImageJ returns POINT before options/dialog processing. Preserve all point
  // metadata, but retain native admission, frozen input and cancellation.
  if(source.kind=="point" || source.kind=="multipoint") {
    detail::admit_selection_geometry(source,width,height,frozen_limits,frozen_control);
    Selection result=source;
    ExecutionScope scope(frozen_control);kernel_checkpoint();execution_progress(0,1);
    kernel_checkpoint();execution_progress(1,1);kernel_checkpoint();return result;
  }
  if(compound_interpolation_source(source)) {
    auto plan=compound_interpolation_plan(source,width,height,frozen_options,frozen_limits,frozen_control);
    ExecutionScope scope(frozen_control);kernel_checkpoint();execution_progress(0,1);kernel_checkpoint();
    auto result=std::move(plan.result);kernel_checkpoint();execution_progress(1,1);kernel_checkpoint();return result;
  }
  auto plan=interpolation_plan(source,width,height,frozen_options,frozen_limits,frozen_control);
  ExecutionScope scope(frozen_control);kernel_checkpoint();execution_progress(0,1);kernel_checkpoint();
  auto result=interpolate_planned(std::move(plan),width,height,frozen_limits);
  kernel_checkpoint();execution_progress(1,1);kernel_checkpoint();return result;
}
} // namespace imagejxx
