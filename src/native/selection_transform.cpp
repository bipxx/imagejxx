#include <imagejxx/selection_transform.hpp>
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
  result.attributes.erase(point_annotations_attribute);result.attributes.erase(point_style_attribute);
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
  result.attributes.erase(point_annotations_attribute);result.attributes.erase(point_style_attribute);
  result.validate(width,height);kernel_checkpoint();execution_progress(1,1);kernel_checkpoint();return result;
}

}
