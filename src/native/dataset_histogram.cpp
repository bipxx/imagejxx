#include <imagejxx/dataset_histogram.hpp>
#include "regional_geometry.hpp"
#include <imagejxx/target_histogram.hpp>
#include "dataset_histogram_internal.hpp"
#include <imagejxx/operations.hpp>

#include "kernel_control.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace imagejxx {
namespace {
[[noreturn]] void invalid(const char *message) {
  throw std::invalid_argument(message);
}
std::size_t add(std::size_t a, std::size_t b) {
  if (b > std::numeric_limits<std::size_t>::max() - a)
    invalid("Dataset histogram storage size overflows");
  return a + b;
}
std::size_t multiply(std::size_t a, std::size_t b) {
  if (a && b > std::numeric_limits<std::size_t>::max() / a)
    invalid("Dataset histogram storage size overflows");
  return a * b;
}
void within(std::size_t bytes, const DatasetHistogramLimits &limits) {
  if (bytes > limits.max_working_bytes)
    invalid("Dataset histogram exceeds its working byte limit");
}

// Attribute nodes are immutable and can share storage; charging each retained
// occurrence is deliberately conservative. No allocator/node bookkeeping is
// claimed to be a measured heap allocation.
struct Charge {
  const DatasetHistogramLimits &limits;
  std::size_t bytes = 0;
  void take(std::size_t amount) {
    bytes = add(bytes, amount);
    within(bytes, limits);
  }
  void text(const std::string &value) { take(add(value.size(), 1)); }
  void object(const MetadataObject &value, unsigned depth = 0) {
    take(sizeof(MetadataObject));
    constexpr auto node = sizeof(MetadataObject::value_type) + 8 * sizeof(void *);
    for (const auto &[key, entry] : value) {
      take(std::max<std::size_t>(node, 160));
      text(key);
      metadata(entry, depth);
    }
  }
  void metadata(const MetadataValue &value, unsigned depth) {
    take(sizeof(MetadataValue));
    if (const auto number = std::get_if<double>(&value)) {
      if (!std::isfinite(*number)) invalid("Metadata values must be finite");
    } else if (const auto string = std::get_if<std::string>(&value)) {
      text(*string);
    } else if (const auto strings = std::get_if<std::vector<std::string>>(&value)) {
      take(multiply(strings->size(), sizeof(std::string)));
      for (const auto &item : *strings) text(item);
    } else if (const auto tree =
                   std::get_if<std::shared_ptr<const MetadataTree>>(&value)) {
      if (!*tree) invalid("Metadata container must not be null");
      if (depth >= 128) invalid("Metadata nesting exceeds 128 containers");
      take(sizeof(MetadataTree));
      if (const auto array = std::get_if<MetadataArray>(&(*tree)->value)) {
        take(multiply(array->size(), sizeof(MetadataValue)));
        for (const auto &item : *array) metadata(item, depth + 1);
      } else object(std::get<MetadataObject>((*tree)->value), depth + 1);
    }
  }
};

bool area_kind(const std::string &kind) {
  return kind == "rounded_rectangle" || kind == "rotated_rounded_rectangle" || kind == "rectangle" || kind == "ellipse" || kind == "polygon" ||
         kind == "freehand" || kind == "rotated_rectangle" ||
         kind == "rotated_ellipse" || kind == "mask" || kind == "region";
}

// Selection::mask uses float-relative polygon coordinates. Refuse its known
// unrepresentable domain during metadata-only admission, before any callback.
void polygon_domain(const Selection &selection) {
  if (selection.kind != "polygon" && selection.kind != "freehand") return;
  std::array<float, 2> base{std::numeric_limits<float>::max(),
                            std::numeric_limits<float>::max()};
  for (const auto &point : selection.points)
    for (int axis = 0; axis < 2; ++axis) {
      if (std::abs(point[axis]) > std::numeric_limits<float>::max())
        invalid("Area polygon exceeds float coordinates");
      base[axis] = std::min(base[axis], static_cast<float>(point[axis]));
    }
  for (const auto &point : selection.points)
    for (int axis = 0; axis < 2; ++axis) {
      const double span = double(static_cast<float>(point[axis])) - double(base[axis]);
      if (span > std::numeric_limits<float>::max())
        invalid("Area polygon exceeds float coordinate span");
      const float relative = static_cast<float>(span);
      const float absolute = relative + base[axis];
      const float local = absolute - static_cast<float>(std::floor(double(base[axis])));
      if (!std::isfinite(local)) invalid("Area polygon has unrepresentable float span");
    }
}

std::size_t selection_charge(const Selection &selection, DatasetShape shape,
                             const DatasetHistogramLimits &limits) {
  if (!area_kind(selection.kind))
    invalid("Dataset histogram requires an area ROI");
  if (!selection.is_mask() &&
      (selection.mask_width() != 0 || selection.mask_height() != 0 ||
       !selection.packed_mask().empty()))
    invalid("Selection geometry contains hidden mask storage");
  Charge charge{limits};
  charge.take(sizeof(Selection));
  charge.text(selection.kind);
  charge.take(multiply(selection.points.size(), sizeof(selection.points[0])));
  charge.object(selection.attributes);
  charge.take(selection.packed_mask().size());
  selection.validate(shape.x, shape.y);
  detail::validate_region_storage(selection);
  polygon_domain(selection);
  if (selection.kind == "rotated_rectangle") {
    const auto frame = *selection.box_frame();
    const auto bounds = frame.bounds();
    if (!std::isfinite(bounds[2] - bounds[0]) ||
        !std::isfinite(bounds[3] - bounds[1]))
      invalid("Rotated area extent exceeds finite raster coordinates");
    const std::array<std::array<double, 2>, 4> corners{
        frame.image_point({-1, -1}), frame.image_point({1, -1}),
        frame.image_point({1, 1}), frame.image_point({-1, 1})};
    for (std::size_t i = 0; i < corners.size(); ++i) {
      const auto &a = corners[i], &b = corners[(i + 1) % corners.size()];
      const double dx = b[0] - a[0], dy = b[1] - a[1];
      if (dy != 0 && !std::isfinite(dx / dy))
        invalid("Rotated area edge exceeds finite raster slope");
    }
  }
  return charge.bytes;
}

std::size_t raster_scratch(const std::optional<Selection> &selection) {
  if (!selection) return 0;
  if (selection->is_rounded_box()) {
    // Four cubics at subdivision depth ten: <=4101 edges, vector growth
    // capacity <=8192, scan intersections, path and bounded recursion slack.
    return 512 * 1024;
  }
  if (selection->kind == "polygon" || selection->kind == "freehand")
    return add(multiply(selection->points.size(), 3 * sizeof(double)),
               sizeof(std::vector<std::array<double, 2>>) + sizeof(std::vector<double>));
  if (selection->kind == "rotated_rectangle")
    return sizeof(Selection) + 64 + 4 * 3 * sizeof(double) +
           sizeof(std::vector<std::array<double, 2>>) + sizeof(std::vector<double>);
  return 0;
}

struct Layout {
  DatasetShape shape;
  PixelType type;
  FloatingSamplePolicy policy;
  DatasetHistogramEstimate estimate;
};
Layout layout(const Dataset &source, const DatasetHistogramRequest &request,
              const DatasetHistogramLimits &limits) {
  if (!limits.max_pixels || !limits.max_working_bytes)
    invalid("Dataset histogram limits must be positive");
  Layout out{source.shape(), source.pixel_type(), source.sample_policy(), {}};
  out.shape.validate();
  source.calibration().validate();
  if (out.shape.s != 1 || (out.type != PixelType::UInt8 &&
      out.type != PixelType::UInt16 && out.type != PixelType::Float32))
    invalid("Dataset histogram requires scalar UInt8, UInt16 or Float32 storage");
  if (out.policy != FloatingSamplePolicy::FiniteOnly &&
      out.policy != FloatingSamplePolicy::PreserveQuietNaNFloat32)
    invalid("Dataset histogram has an invalid floating sample policy");
  if (out.policy == FloatingSamplePolicy::PreserveQuietNaNFloat32 &&
      out.type != PixelType::Float32)
    invalid("Quiet-NaN histogram policy requires scalar Float32 storage");
  const auto &p = request.position;
  if (p.t < 0 || p.t >= out.shape.t || p.c < 0 || p.c >= out.shape.c ||
      p.z < 0 || p.z >= out.shape.z)
    throw std::out_of_range("Dataset histogram T/C/Z is outside the source");
  if (request.binning == HistogramBinning::ProcessorRaw) {
    if (request.bins != 256 || request.range)
      invalid("ProcessorRaw histogram requires 256 bins and no explicit range");
  } else if (request.binning == HistogramBinning::UniformRange) {
    if (request.bins < 1 || request.bins > 65536 || !request.range)
      invalid("UniformRange histogram requires 1..65536 bins and a range");
    const auto &range = *request.range;
    if (!std::isfinite(range[0]) || !std::isfinite(range[1]) || range[0] >= range[1])
      invalid("UniformRange histogram requires finite low < high");
  } else invalid("Unknown dataset histogram binning");

  auto &estimate = out.estimate;
  estimate.pixel_count = multiply(std::size_t(out.shape.x), std::size_t(out.shape.y));
  if (estimate.pixel_count > limits.max_pixels ||
      estimate.pixel_count > std::vector<double>().max_size())
    invalid("Dataset histogram exceeds its pixel/storage limit");
  estimate.plane_bytes = multiply(estimate.pixel_count, sizeof(double));
  estimate.mask_bytes = request.area_roi ? estimate.plane_bytes : 0;
  estimate.histogram_bytes = add(multiply(std::size_t(request.bins), sizeof(std::uint64_t)),
      multiply(add(std::size_t(request.bins), 1), sizeof(double)));
  // Admit the unavoidable output/source canvases before traversing attributes.
  auto bytes = add(estimate.plane_bytes, add(estimate.mask_bytes, estimate.histogram_bytes));
  within(bytes, limits);
  if (request.area_roi)
    estimate.selection_bytes = selection_charge(*request.area_roi, out.shape, limits);
  bytes = add(bytes, estimate.selection_bytes);
  bytes = add(bytes, raster_scratch(request.area_roi));
  if (request.area_roi && request.area_roi->is_rounded_box())
    bytes = add(bytes, add(estimate.pixel_count / 8, (estimate.pixel_count % 8 != 0)));
  bytes = add(bytes, sizeof(DatasetHistogramResult) + sizeof(DatasetHistogramRequest) +
      sizeof(Dataset) + sizeof(ExecutionControl) + sizeof(DatasetHistogramLimits) +
      sizeof(Layout) + sizeof(std::optional<Image>) + sizeof(std::shared_ptr<const Image>) +
      16 * sizeof(double) + 16 * sizeof(std::uint64_t));
  // Both captures and final provenance can temporarily coexist. Inline value
  // objects are charged above; these are the actual owned string lengths.
  bytes = add(bytes, add(source.calibration().unit.size(), 1));
  bytes = add(bytes, add(source.calibration().time_unit.size(), 1));
  within(bytes, limits);
  estimate.working_bytes = bytes;
  return out;
}

bool finite_sample(double value, PixelType type, FloatingSamplePolicy policy) {
  if (type == PixelType::Float32) {
    // Inspect the reviewed bit encoding before any nonfinite FP operation.
    const auto bits = float32_source_sample_bits(value, policy);
    return (bits & 0x7f800000u) != 0x7f800000u;
  }
  if (!std::isfinite(value) || value < 0 || value > type_max(type) ||
      std::trunc(value) != value)
    invalid("Dataset histogram source integer sample is not canonical");
  return true;
}
void extrema(double value, std::optional<double> &low, std::optional<double> &high) {
  if (!low) { low = high = value; return; }
  if (value < *low) low = value;
  if (value > *high) high = value;
}

// Positive-span arithmetic is kept separate from ProcessorRaw's pinned order.
// A crossing interval whose full span overflows can still have a finite width.
std::optional<double> uniform_width(double low, double high, int bins) {
  const double span = high - low;
  const double width = std::isfinite(span) ? span / bins : high / bins - low / bins;
  if (!std::isfinite(width) || width <= 0) return {};
  return width;
}
double uniform_edge(double low, double high, double fraction) {
  const double value = low <= 0 && high >= 0
      ? low * (1 - fraction) + high * fraction
      : low + (high - low) * fraction;
  return std::clamp(value, low, high);
}
double uniform_fraction(double value, double low, double high) {
  const double span = high - low;
  if (std::isfinite(span)) return (value - low) / span;
  const double scale = std::max(std::abs(low), std::abs(high));
  return (value / scale - low / scale) / (high / scale - low / scale);
}

int mapped_index(double value,double mapped,const Histogram &h,bool uniform,double low,double high) {
  const int bins=static_cast<int>(h.counts.size());
      auto index = mapped <= 0 ? 0 : mapped >= bins - 1
          ? bins - 1 : static_cast<int>(mapped);
      if (uniform && value == low) index = 0;
      else if (uniform && value != high &&
          (value < h.edges[std::size_t(index)] ||
           value >= h.edges[std::size_t(index) + 1])) {
        // Normalization can lose the side of a tiny interior displacement
        // across extreme bounds, or land on collapsed rounded edges. Locate
        // the actual half-open interval with bounded logarithmic work; equal
        // interior edges belong to the rightmost interval.
        const auto edge = std::upper_bound(h.edges.begin(),
                                           h.edges.end(), value);
        index = std::clamp(int(edge - h.edges.begin()) - 1,
                           0, bins - 1);
      }
  return index;
}
} // namespace

DatasetHistogramEstimate estimate_dataset_histogram(
    const Dataset &source, const DatasetHistogramRequest &request,
    const DatasetHistogramLimits &limits) {
  return layout(source, request, limits).estimate;
}

static DatasetHistogramResult accumulate_histogram(
    const Dataset &source, const DatasetHistogramRequest &request,
    const DatasetHistogramLimits &limits, const ExecutionControl &control,
    bool plane_progress) {
  if (control.cancellation && control.cancellation->canceled()) throw OperationCancelled();
  const auto admitted = layout(source, request, limits);
  const Dataset captured_source = source;
  const DatasetHistogramLimits captured_limits = limits;
  const ExecutionControl captured_control = control;
  DatasetHistogramResult result;
  result.request = request;
  result.estimate = admitted.estimate;
  if (captured_control.cancellation && captured_control.cancellation->canceled())
    throw OperationCancelled();
  // No reference to caller request/limits/control/source is used below here.
  (void)captured_limits;
  ExecutionScope scope(captured_control);
  kernel_checkpoint();
  if (plane_progress) execution_progress(0, 1);
  kernel_checkpoint();
  std::optional<Image> mask;
  if (result.request.area_roi)
    mask.emplace(result.request.area_roi->mask(admitted.shape.x, admitted.shape.y));
  kernel_checkpoint();
  const auto plane = captured_source.plane(result.request.position);
  kernel_checkpoint();
  if (!plane || plane->width != admitted.shape.x || plane->height != admitted.shape.y ||
      plane->channels != 1 || plane->type != admitted.type ||
      plane->pixels.size() != admitted.estimate.pixel_count)
    invalid("Dataset histogram acquired plane has a different shape/type/storage");

  double mean = 0, m2 = 0;
  kernel_blocks("Dataset histogram validation/extrema", plane->pixels.size(),
      [&](std::size_t first, std::size_t last) {
    for (auto i = first; i < last; ++i) {
      const double value = plane->pixels[i];
      const bool finite = finite_sample(value, admitted.type, admitted.policy);
      if (finite) {
        ++result.plane_finite_count;
        extrema(value, result.plane_minimum, result.plane_maximum);
      } else ++result.plane_nan_count;
      if (mask && mask->pixels[i] == 0) continue;
      ++result.visited_count;
      if (!finite) { ++result.nan_count; continue; }
      ++result.finite_count;
      result.sum += value;
      const double delta = value - mean;
      mean += delta / double(result.finite_count);
      m2 += delta * (value - mean);
      extrema(value, result.minimum, result.maximum);
    }
  });
  result.empty_finite = result.finite_count == 0;
  if (!result.empty_finite) {
    result.mean = mean;
    result.stddev = std::sqrt(std::max(0.0, m2 / double(result.finite_count)));
  }
  const int bins = result.request.bins;
  kernel_checkpoint();
  result.histogram.counts.assign(std::size_t(bins), 0);
  result.histogram.edges.resize(std::size_t(bins) + 1);
  kernel_checkpoint();

  double low = 0, high = 1, scale = 0;
  const bool uniform = result.request.binning == HistogramBinning::UniformRange;
  if (uniform) {
    low = (*result.request.range)[0]; high = (*result.request.range)[1];
    result.bin_size = uniform_width(low, high, bins);
  } else if (result.empty_finite) {
    result.bin_size = 1.0 / 256;
  } else if (admitted.type == PixelType::UInt8) {
    high = 256; scale = 1; result.bin_size = 1;
  } else if (admitted.type == PixelType::UInt16) {
    low = *result.minimum; high = *result.maximum + 1;
    result.bin_size = (*result.maximum - *result.minimum + 1) / 256;
    scale = 1 / *result.bin_size;
  } else {
    low = *result.minimum; high = *result.maximum;
    const double span = high - low;
    result.bin_size = span / 256;
    scale = span == 0 ? 0 : 256 / span;
  }
  result.histogram_origin = low;
  const bool constant_float = !uniform && !result.empty_finite &&
      admitted.type == PixelType::Float32 && low == high;
  kernel_blocks("Dataset histogram edges", result.histogram.edges.size(),
      [&](std::size_t first, std::size_t last) {
    for (auto i = first; i < last; ++i) {
      const double value = constant_float ? low : uniform
          ? uniform_edge(low, high, double(i) / bins)
          : low + double(i) * *result.bin_size;
      result.histogram.edges[i] = value;
    }
  });
  result.histogram.edges.front() = low;
  result.histogram.edges.back() = high;

  kernel_blocks("Dataset histogram bin accumulation", plane->pixels.size(),
      [&](std::size_t first, std::size_t last) {
    for (auto i = first; i < last; ++i) {
      if (mask && mask->pixels[i] == 0) continue;
      const double value = plane->pixels[i];
      if (!finite_sample(value, admitted.type, admitted.policy)) continue;
      double mapped;
      if (uniform) {
        if (value < low) { ++result.underflow_count; continue; }
        if (value > high) { ++result.overflow_count; continue; }
        mapped = value == high ? double(bins - 1)
                              : uniform_fraction(value, low, high) * bins;
      } else if (admitted.type == PixelType::UInt8) mapped = value;
      else if (admitted.type == PixelType::UInt16)
        mapped = scale * (value - int(low));
      else mapped = scale == 0 ? 0 : scale * (value - low);
      const auto index=mapped_index(value,mapped,result.histogram,uniform,low,high);
      ++result.histogram.counts[std::size_t(index)];
      ++result.included_count;
    }
  });
  kernel_checkpoint();
  if (plane_progress) execution_progress(1, 1);
  kernel_checkpoint();
  return result;
}
DatasetHistogramResult accumulate_dataset_histogram(
    const Dataset &source, const DatasetHistogramRequest &request,
    const DatasetHistogramLimits &limits, const ExecutionControl &control) {
  return accumulate_histogram(source, request, limits, control, true);
}
namespace detail {
DatasetHistogramResult accumulate_dataset_histogram_stages(
    const Dataset &source, const DatasetHistogramRequest &request,
    const DatasetHistogramLimits &limits, const ExecutionControl &control) {
  return accumulate_histogram(source, request, limits, control, false);
}
} // namespace detail
namespace {
bool histogram_roi_matches(const ResolvedTarget &target,const Roi &roi) {
  const std::array<std::optional<int>,3> fixed{roi.t,roi.c,roi.z};
  if(target.scope()==TargetScope::Ranges) {
    for(int a=0;a<3;++a)if(fixed[a]){const auto &r=target.ranges()[a];const auto v=*fixed[a];if(v<r.start || v>=*r.stop || (v-r.start)%r.step)return false;}
    return true;
  }
  for(auto p:target.explicit_positions())if((!roi.t||*roi.t==p.t)&&(!roi.c||*roi.c==p.c)&&(!roi.z||*roi.z==p.z))return true;
  return false;
}
}
TargetHistogramEstimate estimate_target_histogram(const ResolvedTarget &target,
    const TargetHistogramOptions &options,const DatasetHistogramLimits &limits) {
  if(target.execution_mode()!=ExecutionMode::Planes || target.destination()!=OutputDestination::InPlace)
    invalid("Target histogram requires a read-only plane target with in-place destination");
  const auto &data=target.source().data();const auto shape=data.dataset.shape();
  DatasetHistogramRequest request;request.position=target.position(0);request.binning=options.binning;request.bins=options.bins;request.range=options.range;
  const auto base=layout(data.dataset,request,limits);
  TargetHistogramEstimate out;out.plane_count=target.position_count();out.plane_pixels=base.estimate.pixel_count;
  out.total_pixels=multiply(out.plane_pixels,out.plane_count);
  if(out.total_pixels>limits.max_pixels)invalid("Target histogram exceeds its total target pixel limit");
  out.plane_bytes=base.estimate.plane_bytes;out.histogram_bytes=base.estimate.histogram_bytes;
  const bool positioned=target.roi_policy()==RoiPolicy::PositionedRois;
  const bool active=target.roi_policy()==RoiPolicy::ActiveSelection && bool(data.selection);
  out.mask_bytes=multiply(out.plane_bytes,positioned?2:active?1:0);
  auto bytes=add(base.estimate.working_bytes,out.mask_bytes);within(bytes,limits);
  std::size_t scratch=0;
  auto area=[&](const Selection &selection) {
    const auto retained=selection_charge(selection,shape,limits);
    out.geometry_bytes=add(out.geometry_bytes,retained);
    auto temporary=raster_scratch(selection);
    if(selection.is_rounded_box())temporary=add(temporary,add(out.plane_pixels/8,out.plane_pixels%8!=0));
    scratch=std::max(scratch,temporary);
    within(add(bytes,add(out.geometry_bytes,scratch)),limits);
  };
  if(active)area(*data.selection);
  if(positioned)for(const auto &roi:data.rois)if(histogram_roi_matches(target,roi))area(roi.selection);
  out.geometry_bytes=add(out.geometry_bytes,scratch);bytes=add(bytes,out.geometry_bytes);
  // Fixed map/value holders plus compact range records. Explicit position
  // arrays have bounded nested metadata/temporary growth and a target copy.
  out.record_bytes=16384;
  if(target.scope()==TargetScope::Positions)out.record_bytes=add(out.record_bytes,multiply(out.plane_count,1024+sizeof(PlanePosition)));
  bytes=add(bytes,out.record_bytes);
  bytes=add(bytes,sizeof(TargetHistogramResult)+sizeof(ResolvedTarget)+sizeof(TargetHistogramOptions));
  within(bytes,limits);out.working_bytes=bytes;return out;
}
TargetHistogramResult accumulate_target_histogram(const ResolvedTarget &requested,
    const TargetHistogramOptions &requested_options,const DatasetHistogramLimits &limits,
    const ExecutionControl &control) {
  if(control.cancellation && control.cancellation->canceled())throw OperationCancelled();
  const auto admitted=estimate_target_histogram(requested,requested_options,limits);
  const auto target=requested;const auto options=requested_options;const auto captured=control;
  const auto &dataset=target.source().data().dataset;const auto shape=dataset.shape();
  const auto type=dataset.pixel_type();const auto policy=dataset.sample_policy();
  TargetHistogramResult result;result.options=options;result.estimate=admitted;result.target_record=target.record();
  ExecutionScope scope(captured);kernel_checkpoint();execution_progress(0,1);kernel_checkpoint();
  const int bins=options.bins;const bool uniform=options.binning==HistogramBinning::UniformRange;
  const bool observed=!uniform && type!=PixelType::UInt8;
  double low=0,high=1,scale=0,mean=0,m2=0;
  auto edges=[&] {
    kernel_checkpoint();result.histogram.counts.assign(std::size_t(bins),0);result.histogram.edges.resize(std::size_t(bins)+1);kernel_checkpoint();
    if(uniform){low=(*options.range)[0];high=(*options.range)[1];result.bin_size=uniform_width(low,high,bins);}
    else if(result.empty_finite && observed){low=0;high=1;result.bin_size=1./256;scale=0;}
    else if(type==PixelType::UInt8){low=0;high=256;scale=1;result.bin_size=1;}
    else if(type==PixelType::UInt16){low=*result.minimum;high=*result.maximum+1;result.bin_size=(high-low)/256;scale=1/ *result.bin_size;}
    else {low=*result.minimum;high=*result.maximum;result.bin_size=(high-low)/256;scale=high==low?0:256/(high-low);}
    result.histogram_origin=low;
    kernel_blocks("Target histogram edges",result.histogram.edges.size(),[&](std::size_t a,std::size_t b){
      for(auto i=a;i<b;++i)result.histogram.edges[i]=uniform?uniform_edge(low,high,double(i)/bins):low+double(i)* *result.bin_size;
    });result.histogram.edges.front()=low;result.histogram.edges.back()=high;
  };
  auto count=[&](double value) {
    if(uniform && value<low){++result.underflow_count;return;}
    if(uniform && value>high){++result.overflow_count;return;}
    double mapped;
    if(uniform)mapped=value==high?double(bins-1):uniform_fraction(value,low,high)*bins;
    else if(type==PixelType::UInt8)mapped=value;
    else if(type==PixelType::UInt16)mapped=scale*(value-int(low));
    else mapped=scale==0?0:scale*(value-low);
    const auto bin=mapped_index(value,mapped,result.histogram,uniform,low,high);
    ++result.histogram.counts[std::size_t(bin)];++result.included_count;
  };
  if(!observed)edges();
  auto scan=[&](bool statistics) {
    KernelStage planes(statistics?"Target histogram statistics":"Target histogram accumulation",admitted.plane_count);
    std::uint64_t members=0;
    for(std::size_t ordinal=0;ordinal<admitted.plane_count;++ordinal) {
      planes.checkpoint(ordinal);auto mask=target.mask(ordinal);kernel_checkpoint();
      const auto plane=dataset.plane(target.position(ordinal));kernel_checkpoint();
      if(!plane || plane->width!=shape.x || plane->height!=shape.y || plane->channels!=1 || plane->type!=type || plane->pixels.size()!=admitted.plane_pixels)
        invalid("Target histogram acquired plane has a different shape/type/storage");
      kernel_blocks("Target histogram samples",plane->pixels.size(),[&](std::size_t a,std::size_t b){
        for(auto i=a;i<b;++i) {
          const double value=plane->pixels[i];const bool finite=finite_sample(value,type,policy);
          if(statistics){if(finite){++result.plane_finite_count;extrema(value,result.plane_minimum,result.plane_maximum);}else ++result.plane_nan_count;}
          if(mask && mask->pixels[i]==0)continue;
          if(statistics)++result.visited_count;
          if(!finite){if(statistics)++result.nan_count;continue;}
          ++members;
          if(statistics) {
            ++result.finite_count;result.sum+=value;const double delta=value-mean;mean+=delta/double(result.finite_count);m2+=delta*(value-mean);
            extrema(value,result.minimum,result.maximum);
          } else if(value<*result.minimum || value>*result.maximum)invalid("Target histogram source changed between passes");
          if(!observed || !statistics)count(value);
        }
      });
    }
    planes.finish();return members;
  };
  scan(true);result.empty_finite=result.finite_count==0;
  if(!result.empty_finite){result.mean=mean;result.stddev=std::sqrt(std::max(0.,m2/double(result.finite_count)));}
  if(observed) {
    edges();
    if(result.finite_count && result.minimum==result.maximum){result.histogram.counts[0]=result.finite_count;result.included_count=result.finite_count;}
    else if(result.finite_count && scan(false)!=result.finite_count)invalid("Target histogram source membership changed between passes");
  } else if(!uniform && result.empty_finite) {
    // The single-plane no-finite presentation uses finite 0..1 edges even for
    // UInt8; it is not the full-type range or an automatic-threshold fallback.
    result.bin_size=1./256;result.histogram_origin=0;
    for(std::size_t i=0;i<result.histogram.edges.size();++i){kernel_checkpoint_periodic();result.histogram.edges[i]=double(i)/256;}
  }
  kernel_checkpoint();execution_progress(1,1);kernel_checkpoint();return result;
}

static std::string histogram_csv(const Histogram &h) {
  if (h.counts.empty() || h.counts.size() > 65536 ||
      h.edges.size() != h.counts.size() + 1)
    invalid("Histogram CSV requires 1..65536 bins and one more edge");
  for (std::size_t i = 0; i < h.edges.size(); ++i)
    if (!std::isfinite(h.edges[i]) || (i && h.edges[i] < h.edges[i - 1]))
      invalid("Histogram CSV requires finite nondecreasing edges");
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::setprecision(std::numeric_limits<double>::max_digits10);
  stream << "bin,lower,upper,count\n";
  for (std::size_t i = 0; i < h.counts.size(); ++i)
    stream << i << ',' << h.edges[i] << ',' << h.edges[i + 1]
           << ',' << h.counts[i] << '\n';
  return stream.str();
}
std::string dataset_histogram_csv(const DatasetHistogramResult &result) { return histogram_csv(result.histogram); }
std::string target_histogram_csv(const TargetHistogramResult &result) { return histogram_csv(result.histogram); }
} // namespace imagejxx
