#include "selection_polygon_type.hpp"
#include "kernel_control.hpp"
#include "selection_region_rotation.hpp"
#include "selection_spline_detail.hpp"
#include <imagejxx/selection_spline.hpp>
#include <algorithm>
#include <cmath>
#include <imagejxx/imagej_roi.hpp>
#include <limits>
#include <stdexcept>
namespace imagejxx {
namespace {
using P = std::array<float, 2>;
void check(bool ok, const char *message) {
  if (!ok)
    throw std::invalid_argument(message);
}
double distance(P a, P b) {
  return std::hypot(double(a[0] - b[0]), double(a[1] - b[1]));
}
std::vector<double> derivatives(const std::vector<float> &x,
                                const std::vector<float> &y) {
  std::vector<double> result(x.size()), upper(x.size());
  for (std::size_t i = 1; i + 1 < x.size(); ++i) {
    kernel_checkpoint_periodic();
    const double left = x[i] - x[i - 1], right = x[i + 1] - x[i];
    check(left > 0 && right > 0, "ImageJ spline knot precision exhausted");
    const double denominator = 2 * (left + right) - left * upper[i - 1];
    const double slope_left = (double(y[i]) - double(y[i - 1])) / left,
                 slope_right = (double(y[i + 1]) - double(y[i])) / right;
    const double inverse = 1 / denominator;
    result[i] =
        (6 * (slope_right - slope_left) - left * result[i - 1]) * inverse;
    upper[i] = right * inverse;
  }
  for (std::size_t i = x.size() - 1; i-- > 0;) {
    kernel_checkpoint_periodic();
    result[i] -= upper[i] * result[i + 1];
  }
  return result;
}
float evaluate(const std::vector<float> &x, const std::vector<float> &y,
               const std::vector<double> &second, double t) {
  std::size_t lo = 0, hi = x.size() - 1;
  while (hi - lo > 1) {
    auto mid = (lo + hi) / 2;
    if (x[mid] < t)
      lo = mid;
    else
      hi = mid;
  }
  const double span = x[hi] - x[lo], a = t - x[lo], b = x[hi] - t;
  return float((a * y[hi] + b * y[lo] +
                ((a * a - span * span) * a * second[hi] +
                 (b * b - span * span) * b * second[lo]) /
                    6) /
               span);
}
double processing_length(const std::vector<P>& points,unsigned type,bool integer_coordinates) {
  const auto n=points.size();const bool closed=type==0 || type==7 || type==8;
  double length = 0;
  bool axis_integer = closed && type == 0;
  for (std::size_t i = 0; i < n; ++i) {
    kernel_checkpoint_periodic();
    auto a = points[i], b = points[(i + 1) % n];
    if (i + 1 < n || closed)
      length += distance(a, b);
    axis_integer &= (a[0] == b[0] || a[1] == b[1]) &&
                    std::floor(a[0]) == a[0] && std::floor(a[1]) == a[1];
  }
  if (type == 8 && !integer_coordinates)
    length = 0; // float traced constructor has no integer perimeter buffer
  else if (axis_integer || (type == 8 && integer_coordinates)) {
    if (n < 4)
      length = 0;
    else {
      double manhattan = 0;
      std::size_t corners = 0;
      bool previous_corner = false;
      for (std::size_t i = 0; i < n; ++i) {
        kernel_checkpoint_periodic();
        auto a = points[(i + n - 1) % n], b = points[i];
        double side = std::abs(b[0] - a[0]) + std::abs(b[1] - a[1]);
        manhattan += side;
        if (side > 1 || !previous_corner) {
          previous_corner = true;
          ++corners;
        } else
          previous_corner = false;
      }
      length = manhattan - corners * (2 - std::sqrt(2.));
    }
  } else if (n > 2 &&
             (type == 7 || (type == 4 &&
                              std::any_of(points.begin(), points.end(),
                                          [](P p) { return p[0] != 0; }) &&
                              std::any_of(points.begin(), points.end(),
                                          [](P p) { return p[1] != 0; })))) {
    auto mean = [&](std::size_t i) {
      return std::array<double, 2>{
          double(float(points[i][0] + points[i + 1][0] + points[i + 2][0])) / 3,
          double(float(points[i][1] + points[i + 1][1] + points[i + 2][1])) /
              3};
    };
    auto first = mean(0), last = mean(n - 3);
    length = std::hypot(first[0] - points[0][0], first[1] - points[0][1]);
    for (std::size_t i = 1; i + 2 < n; ++i) {
      kernel_checkpoint_periodic();
      length += std::hypot(double(points[i + 2][0] - points[i - 1][0]) / 3,
                           double(points[i + 2][1] - points[i - 1][1]) / 3);
    }
    length +=
        std::hypot(points[n - 1][0] - last[0], points[n - 1][1] - last[1]);
    if (closed)
      length += distance(points.front(), points.back());
  }
  return length;
}
} // namespace
namespace {
Selection sample_spline(const ImageJSpline &s,const ImageJRoiLimits &limits,
    const SplineFitOptions& options,std::size_t resolved_points=0,std::size_t* used_points=nullptr) {
  const auto n = s.control_points.size();
  const bool closed = s.type == 0 || s.type == 7 || s.type == 8;
  check(closed || s.type == 4 || s.type == 5 || s.type == 9,
        "Unsupported ImageJ spline wire type");
  check(n >= (closed ? 3 : 2) && (s.type != 9 || n == 3),
        "Invalid ImageJ spline control count");
  check(n <= limits.max_vertices && n <= limits.max_bytes / 32 &&
            n <= limits.max_path_work / 32,
        "ImageJ spline input resource limit");
  kernel_checkpoint();
  std::vector<P> points;
  points.reserve(n + 1);
  P base{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
  for (auto p : s.control_points) {
    kernel_checkpoint_periodic();
    P q{};
    for (int axis = 0; axis < 2; ++axis) {
      check(std::isfinite(p[axis]) && std::abs(p[axis]) <= 1e9,
            "ImageJ spline coordinate range");
      check(!s.integer_coordinates || p[axis] == std::floor(p[axis]),
            "ImageJ integer spline requires integral control points");
      q[axis] = float(p[axis]);
      base[axis] = std::min(base[axis], q[axis]);
    }
    points.push_back(q);
  }
  for (auto &p : points)
    for (int axis = 0; axis < 2; ++axis)
      p[axis] -= base[axis];
  if (s.integer_coordinates) {
    for (float b : base)
      check(b >= -5000 && b <= 60535, "ImageJ integer spline origin range");
    for (auto p : points)
      for (float v : p)
        check(v <= 60535, "ImageJ integer spline relative coordinate range");
  }
  const double length=processing_length(points,s.type,s.integer_coordinates);
  const bool straighten=options.mode==SplineFitMode::Straighten;
  const double requested=resolved_points?double(resolved_points):straighten?std::floor(length*2)+1:options.evaluation_points?double(options.evaluation_points):std::max(100.,std::floor(std::floor(length/2)*std::min(1.,options.magnification)));
  check(std::isfinite(requested) && requested >= 2 && requested <= limits.max_vertices &&
            requested <= limits.max_bytes / 32 &&
            requested <= (limits.max_path_work - n * 32) / 64,
        "ImageJ spline sample resource limit");
  const auto samples = std::size_t(requested);
  if(used_points)*used_points=samples;
  if (closed)
    points.push_back(points.front());
  std::vector<float> knots(points.size());
  for (std::size_t i = 1; i < points.size(); ++i) {
    kernel_checkpoint_periodic();
    float dx = points[i][0] - points[i - 1][0],
          dy = points[i][1] - points[i - 1][1];
    float step = float(std::sqrt(std::sqrt(double(float(dx * dx + dy * dy)))));
    knots[i] = knots[i - 1] + std::max(step, .001f);
    check(knots[i] > knots[i - 1], "ImageJ spline knot precision exhausted");
  }
  const float extent = knots.back();
  const std::size_t extension =
      closed ? std::min(std::size_t(7), points.size() - 1) : 0;
  std::vector<float> positions, x, y;
  auto append = [&](float t, P p) {
    positions.push_back(t);
    x.push_back(p[0]);
    y.push_back(p[1]);
  };
  for (std::size_t i = 0; i < extension; ++i) {
    auto j = points.size() - extension + i - 1;
    append(knots[j] - extent, points[j]);
  }
  for (std::size_t i = 0; i < points.size(); ++i)
    append(knots[i], points[i]);
  for (std::size_t i = 1; i <= extension; ++i)
    append(knots[i] - knots[0] + extent, points[i]);
  auto sx = derivatives(positions, x), sy = derivatives(positions, y);
  const double scale = double(extent) / (samples - 1);
  std::vector<P> result;
  result.reserve(samples);
  P minimum{std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()};
  for (std::size_t i = 0; i < samples; ++i) {
    kernel_checkpoint_periodic();
    P p{evaluate(positions, x, sx, i * scale) + (straighten?0:base[0]),
        evaluate(positions, y, sy, i * scale) + (straighten?0:base[1])};
    for (int axis = 0; axis < 2; ++axis) {
      check(std::isfinite(p[axis]), "Nonfinite ImageJ spline sample");
      minimum[axis] = std::min(minimum[axis], p[axis]);
    }
    result.push_back(p);
  }
  if(straighten) {
    double arc=0;
    for(std::size_t i=1;i<result.size();++i){kernel_checkpoint_periodic();arc+=std::hypot(double(result[i][0])-result[i-1][0],double(result[i][1])-result[i-1][1]);}
    const double requested_output=std::floor(arc+.5)+1;
    check(std::isfinite(arc) && arc>0 && requested_output>=2 && requested_output<=limits.max_vertices && requested_output<=limits.max_bytes/32,"Spline straightening output resource/length limit");
    const auto count=std::size_t(requested_output);const double step=arc/(count-1);
    std::vector<P> resampled;resampled.reserve(count);resampled.push_back(result.front());
    double traversed=0;
    for(std::size_t i=1;i<result.size();++i) {
      kernel_checkpoint_periodic();const auto a=result[i-1],b=result[i];
      const double dx=double(b[0])-a[0],dy=double(b[1])-a[1],distance=std::hypot(dx,dy);traversed+=distance;
      while(resampled.size()<count && (traversed>=resampled.size()*step || i+1==result.size())) {
        kernel_checkpoint_periodic();const double over=distance==0?0:(traversed-resampled.size()*step)/distance;
        resampled.push_back({float(b[0]-over*dx),float(b[1]-over*dy)});
      }
    }
    result=std::move(resampled);minimum={std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};
    for(auto& p:result)for(int a=0;a<2;++a){p[a]+=base[a];check(std::isfinite(p[a]),"Nonfinite straightened spline");minimum[a]=std::min(minimum[a],p[a]);}
  }
  std::vector<std::array<double, 2>> absolute;
  absolute.reserve(result.size());
  for (auto p : result) {
    kernel_checkpoint_periodic();
    absolute.push_back({double(float(float(p[0] - minimum[0]) + minimum[0])),
                        double(float(float(p[1] - minimum[1]) + minimum[1]))});
  }
  kernel_checkpoint();
  return Selection(closed ? "polygon" : "polyline", std::move(absolute));
}
} // namespace
Selection imagej_spline_selection(const ImageJSpline& s,const ImageJRoiLimits& limits) {
  return sample_spline(s,limits,{});
}

namespace {
template<class T> const T& spline_container(const MetadataValue& value) {
  const auto node=std::get_if<std::shared_ptr<const MetadataTree>>(&value);
  check(node && *node && std::holds_alternative<T>((*node)->value),"Invalid spline state container");
  return std::get<T>((*node)->value);
}
std::size_t spline_integer(const MetadataValue& value) {
  if(const auto p=std::get_if<std::int64_t>(&value)){check(*p>=0,"Negative spline count");return std::size_t(*p);}
  if(const auto p=std::get_if<std::uint64_t>(&value)){check(*p<=std::numeric_limits<std::size_t>::max(),"Spline count exceeds native range");return std::size_t(*p);}
  throw std::invalid_argument("Spline state requires integer counts");
}
double spline_number(const MetadataValue& value) {
  double out;
  if(const auto p=std::get_if<double>(&value))out=*p;
  else if(const auto p=std::get_if<std::int64_t>(&value))out=double(*p);
  else throw std::invalid_argument("Spline state requires numeric coordinates");
  check(std::isfinite(out),"Nonfinite spline state number");return out;
}
void spline_options(const SplineFitOptions& value) {
  check(value.mode==SplineFitMode::Fit || value.mode==SplineFitMode::Straighten,"Unknown spline mode");
  check(std::isfinite(value.magnification) && value.magnification>0,"Spline magnification must be finite and positive");
  check(!value.evaluation_points || value.evaluation_points>=2,"Explicit spline evaluation needs at least two points");
  check(value.mode!=SplineFitMode::Straighten || (!value.evaluation_points && value.magnification==1),"Straightening uses automatic unit-distance evaluation independent of zoom");
  check(!value.evaluation_points || value.magnification==1,"Explicit spline count and zoom-derived evaluation are mutually exclusive");
}
unsigned spline_type(const Selection& source) {
  if(source.kind=="polygon")return 0;
  if(source.kind=="polyline")return 5;
  if(source.kind=="freehand")return 7;
  throw std::invalid_argument("Fit Spline requires polygon,polyline or freehand controls");
}
Selection trim_spline_controls(Selection controls,std::optional<double> original_length = {}) {
  if(controls.kind!="freehand" || controls.points.size()<=3)return controls;
  const auto n=controls.points.size();std::vector<P> relative,absolute;relative.reserve(n);absolute.reserve(n);
  P base{std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};
  for(const auto& p:controls.points)for(int a=0;a<2;++a)base[a]=std::min(base[a],float(p[a]));
  for(const auto& p:controls.points){P q{float(p[0])-base[0],float(p[1])-base[1]};relative.push_back(q);absolute.push_back({q[0]+base[0],q[1]+base[1]});}
  const double length=(original_length?*original_length:processing_length(relative,7,false));
  const double exponent=length==0?5:std::exp(std::log(length/700)*.88),threshold=44-40.1/(1+exponent);
  auto smoothed=absolute;
  for(std::size_t i=1;i+1<n;++i)for(int a=0;a<2;++a){float total=0;for(int k=-1;k<=1;++k)total+=absolute[std::size_t(std::ptrdiff_t(i)+k)][a];smoothed[i][a]=total/3;}
  auto blurred=smoothed;std::vector<float> curvature(n);
  for(std::size_t i=0;i<n;++i) {
    kernel_checkpoint_periodic();
    for(int a=0;a<2;++a){double sum=0;for(int k=-2;k<=2;++k)sum+=smoothed[std::size_t(std::clamp(std::ptrdiff_t(i)+k,std::ptrdiff_t(0),std::ptrdiff_t(n-1)))][a];blurred[i][a]=float(sum/5);}
    const float dx=smoothed[i][0]-blurred[i][0],dy=smoothed[i][1]-blurred[i][1];curvature[i]=float(std::sqrt(double(float(dx*dx+dy*dy))));
  }
  std::vector<std::array<double,2>> knots;knots.reserve(n);knots.push_back({blurred[0][0],blurred[0][1]});
  const float dx=blurred[1][0]-blurred[0][0],dy=blurred[1][1]-blurred[0][1];double accumulated=std::sqrt(double(float(dx*dx+dy*dy)));
  for(std::size_t i=1;i+1<n;++i){kernel_checkpoint_periodic();accumulated+=std::hypot(double(blurred[i+1][0])-blurred[i][0],double(blurred[i+1][1])-blurred[i][1])+1+double(float(curvature[i]*2));if(accumulated>=threshold){knots.push_back({blurred[i+1][0],blurred[i+1][1]});accumulated=0;}}
  check(knots.size()>=3,"Trimmed spline has fewer than three controls");controls.kind="polygon";controls.attributes.erase(polygon_type_attribute);controls.attributes.erase(polygon_integer_coordinates_attribute);controls.points=std::move(knots);return controls;
}
double spline_control_length(const Selection& controls) {
  std::vector<P> relative;relative.reserve(controls.points.size());P base{std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};
  for(const auto& p:controls.points)for(int a=0;a<2;++a)base[a]=std::min(base[a],float(p[a]));
  for(const auto& p:controls.points)relative.push_back({float(p[0])-base[0],float(p[1])-base[1]});
  return processing_length(relative,spline_type(controls),false);
}
Selection spline_command_controls(const Selection& source,Selection controls,bool fitted) {
  if(fitted && controls.kind=="freehand" && source.points.size()>3) {
    const double original_length=spline_control_length(controls);
    Selection curve("freehand",source.points,source.width);curve.attributes=std::move(controls.attributes);
    return trim_spline_controls(std::move(curve),original_length);
  }
  return trim_spline_controls(std::move(controls));
}
bool spline_traced_polygon(const Selection& controls) {
  if(controls.kind!="polygon")return false;
  P base{std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};
  for(const auto& p:controls.points)for(int a=0;a<2;++a)base[a]=std::min(base[a],float(p[a]));
  for(std::size_t i=0;i<controls.points.size();++i){const auto& a=controls.points[i];const auto& b=controls.points[(i+1)%controls.points.size()];P u{float(a[0])-base[0],float(a[1])-base[1]},v{float(b[0])-base[0],float(b[1])-base[1]};if((u[0]!=v[0] && u[1]!=v[1]) || u[0]!=std::floor(u[0]) || u[1]!=std::floor(u[1]))return false;}
  return true;
}
double spline_curve_length(const Selection& source) {
  double length=0;const bool closed=source.kind!="polyline";
  const auto count=source.points.size();for(std::size_t i=1;i<count+std::size_t(closed);++i){kernel_checkpoint_periodic();const auto& a=source.points[i-1];const auto& b=source.points[i%count];length+=std::hypot(double(float(b[0])-float(a[0])),double(float(b[1])-float(a[1])));}
  return length;
}
struct SplinePlan {Selection controls;SplineFitOptions options;ImageJRoiLimits roi;std::size_t bytes,sampling_points;};
SplinePlan spline_plan(const Selection& source,int w,int h,const SplineFitOptions& options,const SelectionEditLimits& limits,const ExecutionControl& control,bool preprocess=true) {
  spline_options(options);auto bytes=detail::admit_selection_geometry(source,w,h,limits,control);
  ExecutionScope planning(control);
  return detail::without_execution_callbacks([&] {
    auto retained=selection_spline_fit(source);Selection controls=retained?retained->controls:source;
    (void)spline_type(controls);const auto n=preprocess && retained && controls.kind=="freehand"?std::max(controls.points.size(),source.points.size()):controls.points.size();
    check(n<=65536 && n<=limits.max_vertices,"Spline control limit exceeded");
    check(bytes<=limits.max_working_bytes && limits.max_working_bytes-bytes>=16384 && n<=(limits.max_working_bytes-bytes-16384)/1024,"Spline control workspace limit");
    bytes+=16384+n*1024;
    ImageJRoiLimits roi;roi.max_vertices=std::min(limits.max_vertices,std::size_t(65536));roi.max_bytes=std::min(roi.max_bytes,n*32+(limits.max_working_bytes-bytes)/8);
    if(preprocess)controls=spline_command_controls(source,std::move(controls),bool(retained));
    std::size_t resolved=0;
    if(preprocess && retained && retained->controls.kind!="freehand" && !spline_traced_polygon(retained->controls) && !options.evaluation_points) {
      const auto length=spline_curve_length(source);
      const double requested=options.mode==SplineFitMode::Straighten?std::floor(length*2)+1:std::max(100.,std::floor(std::floor(length/2)*std::min(1.,options.magnification)));
      check(std::isfinite(requested) && requested>=2 && requested<=roi.max_vertices,"Spline captured evaluation limit");resolved=std::size_t(requested);
    }
    std::size_t sampling_points=0;
    const auto sampled=sample_spline({controls.points,static_cast<unsigned char>(spline_type(controls)),false},roi,options,resolved,&sampling_points);
    check(sampled.points.size()<=(limits.max_working_bytes-bytes)/256,"Spline sample workspace limit");
    const std::size_t peak_samples=std::max(sampled.points.size(),sampling_points);
    check(peak_samples<=(limits.max_working_bytes-bytes)/256,"Spline preliminary workspace limit");
    bytes+=peak_samples*256;
    return SplinePlan{std::move(controls),options,roi,bytes,sampling_points};
  });
}
MetadataValue spline_record(const Selection& controls,const SplineFitOptions& options,std::size_t sampling_points) {
  MetadataArray points;points.reserve(controls.points.size());
  for(const auto& p:controls.points)points.push_back(metadata_array({p[0],p[1]}));
  return metadata_object({{"version",1},{"kind",controls.kind},{"mode",options.mode==SplineFitMode::Fit?"fit":"straighten"},{"evaluation_points",std::uint64_t(options.evaluation_points)},{"magnification",options.magnification},{"sampling_points",std::uint64_t(sampling_points)},{"controls",metadata_array(std::move(points))}});
}
}
namespace {
struct SplineView {const std::string* kind;const MetadataArray* controls;SplineFitOptions options;std::size_t sampling_points;};
std::optional<SplineView> inspect_spline(const Selection& source) {
  const auto it=source.attributes.find(spline_fit_attribute);if(it==source.attributes.end())return {};
  const auto& record=spline_container<MetadataObject>(it->second);
  check(record.size()==7,"Invalid spline state fields");for(const auto* name:{"version","kind","mode","evaluation_points","magnification","sampling_points","controls"})check(record.count(name)!=0,"Incomplete spline state");
  check(spline_integer(record.at("version"))==1,"Unsupported spline state version");
  const auto kind=std::get_if<std::string>(&record.at("kind")),mode=std::get_if<std::string>(&record.at("mode"));check(kind && mode,"Invalid spline state kind/mode");
  check(*kind=="polygon" || *kind=="polyline" || *kind=="freehand","Invalid spline control kind");
  SplineFitOptions options;check(*mode=="fit" || *mode=="straighten","Unknown spline state mode");options.mode=*mode=="fit"?SplineFitMode::Fit:SplineFitMode::Straighten;options.evaluation_points=spline_integer(record.at("evaluation_points"));options.magnification=spline_number(record.at("magnification"));spline_options(options);
  const auto sampling_points=spline_integer(record.at("sampling_points"));check(sampling_points>=2 && sampling_points<=65536,"Spline resolved evaluation count");
  const auto& points=spline_container<MetadataArray>(record.at("controls"));check(points.size()>=(*kind=="polyline"?2u:3u) && points.size()<=65536,"Invalid spline control count");
  for(const auto& value:points){const auto& p=spline_container<MetadataArray>(value);check(p.size()==2,"Invalid spline control coordinate");check(std::abs(spline_number(p[0]))<=1e9 && std::abs(spline_number(p[1]))<=1e9,"Spline control coordinate range");}
  check(source.kind==(*kind=="polyline"?"polyline":"polygon") && source.points.size()>=2 && source.points.size()<=65536,"Spline sample kind/count mismatch");
  if(options.mode==SplineFitMode::Fit)check(source.points.size()==sampling_points && (!options.evaluation_points || sampling_points==options.evaluation_points),"Spline sample count differs from options/context");
  return SplineView{kind,&points,options,sampling_points};
}
}
std::size_t detail::spline_control_count(const Selection& source) {
  const auto view=inspect_spline(source);return view?view->controls->size():0;
}
std::optional<SelectionSplineFit> selection_spline_fit(const Selection& source) {
  const auto view=inspect_spline(source);if(!view)return {};
  Selection controls(*view->kind);controls.points.reserve(view->controls->size());
  for(const auto& value:*view->controls){const auto& p=spline_container<MetadataArray>(value);controls.points.push_back({spline_number(p[0]),spline_number(p[1])});}
  controls.width=source.width;controls.attributes=source.attributes;controls.attributes.erase(spline_fit_attribute);controls.validate(1,1);return SelectionSplineFit{std::move(controls),view->options,view->sampling_points};
}
void validate_selection_spline_fit(const Selection& source) {
  const auto state=selection_spline_fit(source);if(!state)return;
  detail::without_execution_callbacks([&] {
    const auto expected=sample_spline({state->controls.points,static_cast<unsigned char>(spline_type(state->controls)),false},{},state->options,state->sampling_points);
    check(source.kind==expected.kind && source.points.size()==expected.points.size(),"Spline samples differ from retained controls/options");
    for(std::size_t i=0;i<source.points.size();++i)for(int a=0;a<2;++a){const float v=float(expected.points[i][a]);const double tolerance=std::max(1e-6,2*std::abs(double(std::nextafter(v,std::numeric_limits<float>::infinity()))-v));check(std::abs(source.points[i][a]-expected.points[i][a])<=tolerance,"Spline samples differ from retained controls/options");}
    return 0;
  });
}
std::size_t estimate_spline_selection(const Selection& source,int w,int h,const SplineFitOptions& options,const SelectionEditLimits& limits) {
  return spline_plan(source,w,h,options,limits,{}).bytes;
}
namespace {
Selection fit_spline_impl(const Selection& source,int w,int h,const SplineFitOptions& options,const SelectionEditLimits& limits,const ExecutionControl& control,bool preprocess) {
  const auto frozen_limits=limits;const auto frozen_options=options;const auto frozen_control=control;
  auto plan=spline_plan(source,w,h,frozen_options,frozen_limits,frozen_control,preprocess);
  ExecutionScope scope(frozen_control);kernel_checkpoint();execution_progress(0,1);kernel_checkpoint();
  auto result=sample_spline({plan.controls.points,static_cast<unsigned char>(spline_type(plan.controls)),false},plan.roi,plan.options,plan.sampling_points);
  result.width=plan.controls.width;result.attributes=plan.controls.attributes;
  result.attributes.erase(polygon_integer_coordinates_attribute); // New sampled geometry uses Float32 controls.
  result.attributes[spline_fit_attribute]=spline_record(plan.controls,plan.options,plan.sampling_points);
  result.validate(w,h);validate_selection_spline_fit(result);kernel_checkpoint();execution_progress(1,1);kernel_checkpoint();return result;
}
}
Selection fit_spline_selection(const Selection& source,int w,int h,const SplineFitOptions& options,const SelectionEditLimits& limits,const ExecutionControl& control) {
  return fit_spline_impl(source,w,h,options,limits,control,true);
}
Selection detail::refit_spline_controls(const Selection& controls,int w,int h,const SplineFitOptions& options,const SelectionEditLimits& limits,const ExecutionControl& control) {
  return fit_spline_impl(controls,w,h,options,limits,control,false);
}
Selection remove_spline_fit(const Selection& source,int w,int h,const SelectionEditLimits& limits,const ExecutionControl& control) {
  const auto frozen_limits=limits;const auto frozen_control=control;detail::admit_selection_geometry(source,w,h,frozen_limits,frozen_control);
  const Selection frozen=source;
  auto state=[&] {ExecutionScope planning(frozen_control);return detail::without_execution_callbacks([&] {
    auto value=selection_spline_fit(frozen);check(bool(value),"Selection has no retained spline fit");validate_selection_spline_fit(frozen);return value;
  });}();
  ExecutionScope scope(frozen_control);kernel_checkpoint();auto result=std::move(state->controls);
  result=spline_command_controls(frozen,std::move(result),true);
  result.validate(w,h);kernel_checkpoint();return result;
}
} // namespace imagejxx
