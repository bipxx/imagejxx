// Independent P06c4 histogram assertions, adopted contract 46b332807d197682.
// Adoption002 supplies UniformRange rounded-edge membership and cap exclusions.
// Expected bins/membership are literal or elementary test-owned fixtures. No
// product rasterizer, source-sample helper or histogram computes expectations.
#include <imagejxx/dataset_histogram.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace imagejxx;
namespace {
std::size_t checks = 0;
std::vector<std::string> cases;
void named(const char *name) { cases.emplace_back(name); }
void check(bool value, const char *message) {
  ++checks;
  if (!value)
    throw std::runtime_error(cases.back() + ": " + message);
}
template <class Error, class Function>
void rejects(Function f, const char *message) {
  bool caught = false;
  try { f(); } catch (const Error &) { caught = true; }
  check(caught, message);
}
// Descriptor/cap errors have no mandated exception subclass. Do not treat a
// bad_alloc, cancellation or callback failure as a successful refusal.
template <class Function> void refuses(Function f, const char *message) {
  bool caught = false;
  try { f(); }
  catch (const std::invalid_argument &) { caught = true; }
  catch (const std::out_of_range &) { caught = true; }
  catch (const std::overflow_error &) { caught = true; }
  catch (const std::length_error &) { caught = true; }
  check(caught, message);
}
std::uint64_t bits(double v) {
  std::uint64_t b;
  std::memcpy(&b, &v, sizeof b);
  return b;
}
double double_bits(std::uint64_t b) {
  double v;
  std::memcpy(&v, &b, sizeof v);
  return v;
}
double float_bits(std::uint32_t b) {
  float v;
  std::memcpy(&v, &b, sizeof v);
  return static_cast<double>(v);
}
std::vector<std::uint64_t> image_bits(const Image &p) {
  std::vector<std::uint64_t> out;
  for (double v : p.pixels) out.push_back(bits(v));
  return out;
}
std::vector<std::uint64_t> eager_bits(const Dataset &d) {
  const auto table = d.shared_planes();
  check(table.has_value(), "Fixture must use reviewed eager storage");
  std::vector<std::uint64_t> out;
  for (const auto &p : *table)
    for (double v : p.pixels) out.push_back(bits(v));
  return out;
}
Image image(PixelType type, int width, int height,
            std::vector<double> values, int samples = 1) {
  Image p(width, height, samples, type);
  p.pixels = std::move(values);
  return p;
}
Dataset eager(PixelType type, std::vector<double> values,
              FloatingSamplePolicy policy = FloatingSamplePolicy::FiniteOnly) {
  const int n = static_cast<int>(values.size());
  Stack p;
  p.push_back(image(type, n, 1, std::move(values)));
  return Dataset::from_planes({1, 1, 1, 1, n, 1}, std::move(p), {}, policy);
}
struct Source final : PlaneSource {
  DatasetShape descriptor;
  PixelType type;
  std::shared_ptr<const Image> buffer;
  std::function<std::shared_ptr<const Image>(PlanePosition)> factory;
  std::function<void()> on_read;
  mutable std::size_t reads = 0, regions = 0;
  mutable std::vector<PlanePosition> positions;
  bool single_read = false;
  Source(DatasetShape s, PixelType t) : descriptor(s), type(t) {}
  DatasetShape shape() const override { return descriptor; }
  PixelType pixel_type() const override { return type; }
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    ++reads;
    positions.push_back(p);
    if (single_read && reads > 1)
      throw std::runtime_error("A second source acquisition is forbidden");
    if (on_read) on_read();
    if (factory) return factory(p);
    if (buffer) return buffer;
    throw std::runtime_error("Metadata-only fixture was read");
  }
  Image read_region(PlanePosition, int, int, int, int) const override {
    ++regions;
    throw std::runtime_error("Histogram must not read a second region");
  }
};
std::shared_ptr<Source> source(Image p) {
  auto s = std::make_shared<Source>(
      DatasetShape{1, 1, 1, p.height, p.width, p.channels}, p.type);
  s->buffer = std::make_shared<const Image>(std::move(p));
  return s;
}
std::shared_ptr<Source> formula_source() {
  auto s = std::make_shared<Source>(DatasetShape{2, 3, 4, 3, 5, 1},
                                  PixelType::UInt16);
  s->factory = [](PlanePosition pos) {
    Image p(5, 3, 1, PixelType::UInt16);
    for (int y = 0; y < 3; ++y)
      for (int x = 0; x < 5; ++x)
        p.at(x, y) = 64 * pos.t + 24 * pos.c + 8 * pos.z + 3 * y + x;
    return std::make_shared<const Image>(std::move(p));
  };
  return s;
}
SpatialCalibration calibration() {
  SpatialCalibration c;
  c.spacing = {.5, 1.25, 2.5};
  c.origin = {11, -7, 19};
  c.direction = {-1, .25, .5, 0, 1, .5, .5, 0, 1};
  c.unit = "um";
  c.time_unit = "ms";
  c.frame_interval = .75;
  c.time_origin = 2.25;
  return c;
}
void same_calibration(const SpatialCalibration &a,
                      const SpatialCalibration &b) {
  check(a.spacing == b.spacing && a.origin == b.origin &&
            a.direction == b.direction && a.unit == b.unit &&
            a.time_unit == b.time_unit && a.frame_interval == b.frame_interval &&
            a.time_origin == b.time_origin,
        "Complete source calibration must remain unchanged");
}
std::vector<std::uint64_t> sparse(
    int n, std::initializer_list<std::pair<int, std::uint64_t>> entries) {
  std::vector<std::uint64_t> out(static_cast<std::size_t>(n), 0);
  for (const auto &entry : entries) out.at(entry.first) = entry.second;
  return out;
}
std::vector<std::uint64_t> occupied(int n,
                                   std::initializer_list<int> indices) {
  std::vector<std::uint64_t> out(static_cast<std::size_t>(n), 0);
  for (int index : indices) ++out.at(index);
  return out;
}
void conserved(const DatasetHistogramResult &r, int bins) {
  check(r.histogram.counts.size() == static_cast<std::size_t>(bins) &&
            r.histogram.edges.size() == static_cast<std::size_t>(bins + 1),
        "Exact returned histogram cardinality");
  check(r.visited_count == r.finite_count + r.nan_count,
        "Area visited count conserves finite/NaN membership");
  check(r.finite_count == r.included_count + r.underflow_count + r.overflow_count,
        "Finite membership conserves underflow/included/overflow");
  check(r.included_count == std::accumulate(r.histogram.counts.begin(),
                    r.histogram.counts.end(), std::uint64_t{0}),
        "Included count equals all histogram bins");
  check(r.empty_finite == (r.finite_count == 0), "Explicit empty-finite flag");
  check(std::all_of(r.histogram.edges.begin(), r.histogram.edges.end(),
                    [](double v) { return std::isfinite(v); }) &&
            std::is_sorted(r.histogram.edges.begin(), r.histogram.edges.end()),
        "All edges finite and nondecreasing");
}
void counts(const DatasetHistogramResult &r,
            const std::vector<std::uint64_t> &expected,
            std::uint64_t visited, std::uint64_t finite,
            std::uint64_t nan = 0, std::uint64_t under = 0,
            std::uint64_t over = 0) {
  conserved(r, static_cast<int>(expected.size()));
  check(r.histogram.counts == expected, "Independent literal bin counts");
  check(r.visited_count == visited && r.finite_count == finite &&
            r.nan_count == nan && r.underflow_count == under &&
            r.overflow_count == over, "Exact area accounting");
}
void extrema(const DatasetHistogramResult &r, double lo, double hi,
             double plane_lo, double plane_hi, std::uint64_t plane_finite,
             std::uint64_t plane_nan = 0) {
  check(r.minimum && r.maximum && *r.minimum == lo && *r.maximum == hi,
        "Exact finite area extrema");
  check(r.plane_minimum && r.plane_maximum &&
            *r.plane_minimum == plane_lo && *r.plane_maximum == plane_hi &&
            r.plane_finite_count == plane_finite && r.plane_nan_count == plane_nan,
        "Independent whole-plane extrema/counts");
}
DatasetHistogramRequest uniform(int bins, double lo, double hi) {
  DatasetHistogramRequest q;
  q.binning = HistogramBinning::UniformRange;
  q.bins = bins;
  q.range = std::array<double, 2>{lo, hi};
  return q;
}
void no_reads(const Source &s, std::size_t callbacks) {
  check(s.reads == 0 && s.regions == 0 && callbacks == 0,
        "Admission must precede every provider read and user callback");
}

void raw() {
  named("raw.u8-fixed-endpoints");
  auto d = eager(PixelType::UInt8, {0, 1, 2, 2, 127, 128, 254, 255});
  auto r = accumulate_dataset_histogram(d);
  counts(r, sparse(256, {{0,1},{1,1},{2,2},{127,1},{128,1},{254,1},{255,1}}), 8, 8);
  extrema(r, 0, 255, 0, 255, 8);
  check(r.histogram_origin == 0 && r.bin_size == 1, "U8 fixed origin/width");
  for (int i = 0; i <= 256; ++i)
    check(r.histogram.edges[i] == i, "Literal U8 integer edges");

  named("raw.u16-asymmetric-reference");
  d = eager(PixelType::UInt16, {1000,1257,1514,1771,2028,2285,2542,2799,
                              3056,3313,3570,3827,4084,4341,4598,4855});
  r = accumulate_dataset_histogram(d);
  counts(r, occupied(256, {0,17,34,51,68,85,102,119,136,153,170,187,204,221,238,255}),16,16);
  extrema(r,1000,4855,1000,4855,16);
  check(r.histogram_origin == 1000 && r.bin_size == 15.0625 &&
        r.histogram.edges.front() == 1000 && r.histogram.edges[128] == 2928 &&
        r.histogram.edges.back() == 4856, "U16 min..max+1 edges and width");

  named("raw.u16-full-unsigned-range");
  r = accumulate_dataset_histogram(eager(PixelType::UInt16,
      {0,255,256,257,32767,32768,65534,65535}));
  counts(r,sparse(256,{{0,2},{1,2},{127,1},{128,1},{255,2}}),8,8);
  extrema(r,0,65535,0,65535,8);
  check(r.bin_size == 256 && r.histogram.edges.back() == 65536,
        "Unsigned endpoint is not signed or truncated");

  named("raw.integer-constant-typed-domains");
  r = accumulate_dataset_histogram(eager(PixelType::UInt8,{7,7,7}));
  counts(r,sparse(256,{{7,3}}),3,3);
  check(r.histogram_origin == 0 && r.bin_size == 1 &&
        r.histogram.edges.front() == 0 && r.histogram.edges.back() == 256,
        "Finite constant U8 retains its fixed integer domain");
  r = accumulate_dataset_histogram(eager(PixelType::UInt16,{1000,1000,1000}));
  counts(r,sparse(256,{{0,3}}),3,3);
  check(r.histogram_origin == 1000 && r.bin_size == 1./256 &&
        r.histogram.edges.front() == 1000 && r.histogram.edges.back() == 1001,
        "Finite constant U16 retains minimum..minimum+1 domain");

  named("raw.f32-asymmetric-reference");
  r = accumulate_dataset_histogram(eager(PixelType::Float32,
      {-2.5,-2,-1.5,-1,-.5,0,.5,1,1.5,2,2.5,3,3.5,4,4.5,5}));
  counts(r,occupied(256,{0,17,34,51,68,85,102,119,136,153,170,187,204,221,238,255}),16,16);
  extrema(r,-2.5,5,-2.5,5,16);
  check(r.histogram_origin == -2.5 && r.bin_size == .029296875 &&
        r.histogram.edges[128] == 1.25 && r.histogram.edges.back() == 5,
        "F32 observed range and inclusive final bin");

  named("raw.f32-canonical-subnormal");
  const double small = std::numeric_limits<float>::min();
  const double subnormal = std::numeric_limits<float>::denorm_min();
  d = eager(PixelType::Float32,{0,subnormal,small});
  const auto small_before = eager_bits(d);
  r = accumulate_dataset_histogram(d);
  counts(r,sparse(256,{{0,2},{255,1}}),3,3);
  extrema(r,0,small,0,small,3);
  check(r.bin_size == std::ldexp(1.,-134) && eager_bits(d) == small_before,
        "Canonical finite binary32 subnormal is admitted without rounding source");

  named("raw.f32-negative-zero-constant");
  r = accumulate_dataset_histogram(eager(PixelType::Float32, {-0.,-0.,-0.}));
  counts(r,sparse(256,{{0,3}}),3,3);
  check(r.bin_size && *r.bin_size == 0 && bits(r.histogram_origin) == bits(-0.),
        "Constant Float32 origin retains signed zero and has zero width");
  for (double edge : r.histogram.edges)
    check(bits(edge) == bits(-0.), "Constant edge retains the exact source constant");

  named("raw.quiet-nan-payload-and-source-preservation");
  d = eager(PixelType::Float32,
      {float_bits(0x7fc12345),float_bits(0xffc23456),-0.,0.,-5,-4,-3,-2,-1,1,2,3,4,5,6,7},
      FloatingSamplePolicy::PreserveQuietNaNFloat32);
  auto before = eager_bits(d);
  r = accumulate_dataset_histogram(d);
  counts(r,sparse(256,{{0,1},{21,1},{42,1},{64,1},{85,1},{106,2},{128,1},
          {149,1},{170,1},{192,1},{213,1},{234,1},{255,1}}),16,14,2);
  extrema(r,-5,7,-5,7,14,2);
  check(r.bin_size == .046875 && eager_bits(d) == before,
        "Exclude NaNs without canonicalizing payloads or signed zero");

  named("raw.all-nan-owned-empty-presentation");
  d = eager(PixelType::Float32,
      {float_bits(0x7fc12345),float_bits(0xffc23456),float_bits(0x7fc00001),float_bits(0x7fffffff)},
      FloatingSamplePolicy::PreserveQuietNaNFloat32);
  before = eager_bits(d);
  r = accumulate_dataset_histogram(d);
  counts(r,sparse(256,{}),4,0,4);
  check(!r.minimum && !r.maximum && !r.plane_minimum && !r.plane_maximum &&
        r.plane_finite_count == 0 && r.plane_nan_count == 4,
        "No fabricated finite extrema for all-NaN plane");
  check(r.histogram_origin == 0 && r.bin_size == 1./256 &&
        r.histogram.edges.front() == 0 && r.histogram.edges[128] == .5 &&
        r.histogram.edges.back() == 1 && eager_bits(d) == before,
        "Finite owned empty presentation preserves source");

  named("raw.one-captured-scientific-tcz");
  auto s = formula_source();
  s->single_read = true;
  const auto cal = calibration();
  d = Dataset(s,cal);
  DatasetHistogramRequest q;
  q.position = {1,2,3};
  check(estimate_dataset_histogram(d,q).pixel_count == 15 && s->reads == 0,
        "Scientific estimator describes one XY plane without acquisition");
  r = accumulate_dataset_histogram(d,q);
  counts(r,sparse(256,{{0,1},{23,1},{46,1},{69,2},{93,2},{116,1},{139,2},
         {162,2},{186,1},{209,1},{232,1}}),15,15);
  extrema(r,136,146,136,146,15);
  check(s->reads == 1 && s->regions == 0 && s->positions.size() == 1 &&
        s->positions[0].t == 1 && s->positions[0].c == 2 && s->positions[0].z == 3 &&
        r.request.position.t == 1 && r.request.position.c == 2 && r.request.position.z == 3,
        "Exactly captured T/C/Z, not neighboring scientific channels");
  same_calibration(d.calibration(),cal);
}

void roi() {
  const std::vector<double> fixture{90,1,2,3,91,92,4,5,6,93,94,7,8,9,95};
  auto s = source(image(PixelType::UInt16,5,3,fixture));
  const auto before = image_bits(*s->buffer);
  Dataset d(s,calibration());
  DatasetHistogramRequest q;
  named("roi.rectangle-versus-whole-plane-extrema");
  q.area_roi = Selection("rectangle",{{1,0},{4,3}});
  auto r = accumulate_dataset_histogram(d,q);
  counts(r,occupied(256,{0,28,56,85,113,142,170,199,227}),9,9);
  extrema(r,1,9,1,95,15);
  check(r.bin_size == 9./256 && r.request.area_roi == q.area_roi,
        "Rectangle provenance and unsigned max+1 width");

  named("roi.packed-holes-and-owned-attributes");
  q = uniform(10,0,100);
  q.area_roi = Selection::from_packed_mask(5,3,{0xfc,0x7e});
  q.area_roi->attributes["label"] = std::string("hole ring");
  q.area_roi->attributes["retained"] = metadata_object({
      {"nested",metadata_array({true,std::int64_t{7},std::string("unchanged")})}});
  const auto original = *q.area_roi;
  r = accumulate_dataset_histogram(d,q);
  counts(r,sparse(10,{{0,6},{9,6}}),12,12);
  extrema(r,1,95,1,95,15);
  q.area_roi->attributes["label"] = std::string("caller changed after return");
  check(r.request.area_roi && *r.request.area_roi == original &&
        r.request.area_roi->packed_mask() == std::vector<unsigned char>({0xfc,0x7e}),
        "Returned packed mask and recursive metadata are owned provenance");

  named("roi.explicit-empty-still-observes-one-plane");
  auto empty_source = source(image(PixelType::UInt16,5,3,fixture));
  empty_source->single_read = true;
  q = {};
  q.area_roi = Selection::from_packed_mask(5,3,{0,0});
  r = accumulate_dataset_histogram(Dataset(empty_source),q);
  counts(r,sparse(256,{}),0,0);
  check(!r.minimum && !r.maximum && r.plane_minimum == 1 && r.plane_maximum == 95 &&
        r.plane_finite_count == 15 && r.plane_nan_count == 0 &&
        empty_source->reads == 1 && empty_source->regions == 0,
        "Empty ROI does not erase independent whole-plane fallback facts");
  check(r.bin_size == 1./256 && r.histogram.edges.front() == 0 &&
        r.histogram.edges.back() == 1, "Empty area owns finite presentation");

  named("roi.empty-u8-overrides-typed-default");
  auto empty_u8 = source(image(PixelType::UInt8,2,1,{7,9}));
  q.area_roi = Selection::from_packed_mask(2,1,{0});
  r = accumulate_dataset_histogram(Dataset(empty_u8),q);
  counts(r,sparse(256,{}),0,0);
  check(!r.minimum && !r.maximum && r.plane_minimum == 7 && r.plane_maximum == 9 &&
        r.histogram_origin == 0 && r.bin_size == 1./256 &&
        r.histogram.edges.front() == 0 && r.histogram.edges.back() == 1,
        "No-finite U8 presentation takes precedence over ordinary fixed256 edges");

  named("roi.quiet-nan-area-and-plane-counts-are-distinct");
  Stack quiet_planes;
  quiet_planes.push_back(image(PixelType::Float32,2,2,
      {float_bits(0x7fc12345),1,float_bits(0xffc23456),2}));
  const auto quiet = Dataset::from_planes({1,1,1,2,2,1},std::move(quiet_planes),{},
      FloatingSamplePolicy::PreserveQuietNaNFloat32);
  const auto quiet_before = eager_bits(quiet);
  q = {}; q.area_roi = Selection("rectangle",{{1,0},{2,2}});
  r = accumulate_dataset_histogram(quiet,q);
  counts(r,occupied(256,{0,255}),2,2);
  extrema(r,1,2,1,2,2,2);
  q.area_roi = Selection("rectangle",{{0,0},{1,2}});
  r = accumulate_dataset_histogram(quiet,q);
  counts(r,sparse(256,{}),2,0,2);
  check(!r.minimum && !r.maximum && r.plane_minimum == 1 && r.plane_maximum == 2 &&
        r.plane_finite_count == 2 && r.plane_nan_count == 2 &&
        r.histogram.edges.front() == 0 && r.histogram.edges.back() == 1 &&
        eager_bits(quiet) == quiet_before,
        "An all-NaN area keeps detached finite whole-plane facts without changing payloads");

  named("roi.ellipse-literal-cell-membership");
  auto grid = source(image(PixelType::UInt8,4,4,{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}));
  q = {};
  q.area_roi = Selection("ellipse",{{0,0},{4,4}});
  r = accumulate_dataset_histogram(Dataset(grid),q);
  counts(r,occupied(256,{1,2,4,5,6,7,8,9,10,11,13,14}),12,12);
  extrema(r,1,14,0,15,16);

  named("roi.polygon-and-freehand-literal-cells");
  auto polygon_source = source(image(PixelType::UInt8,5,3,{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14}));
  for (const char *kind : {"polygon","freehand"}) {
    q.area_roi = Selection(kind,{{1,0},{4,0},{4,3},{1,3}});
    r = accumulate_dataset_histogram(Dataset(polygon_source),q);
    counts(r,occupied(256,{1,2,3,6,7,8,11,12,13}),9,9);
    extrema(r,1,13,0,14,15);
  }

  named("roi.rotated-area-literal-cells");
  for (const char *kind : {"rotated_rectangle","rotated_ellipse"}) {
    q.area_roi = Selection(kind,{{2,2},{2,3},{0,2}});
    r = accumulate_dataset_histogram(Dataset(grid),q);
    counts(r,occupied(256,{4,5,6,7,8,9,10,11}),8,8);
    extrema(r,4,11,0,15,16);
  }
  check(image_bits(*s->buffer) == before, "ROI operations never alter source bytes");
  same_calibration(d.calibration(),calibration());
}

void uniform_range() {
  named("uniform.endpoints-underflow-overflow");
  auto q = uniform(4,0,1);
  auto r = accumulate_dataset_histogram(eager(PixelType::Float32,
      {-2,-1,0,.25,.5,.75,1,2}),q);
  counts(r,{1,1,1,2},8,8,0,2,1);
  extrema(r,-2,2,-2,2,8);
  check(r.histogram.edges == std::vector<double>({0,.25,.5,.75,1}) &&
        r.histogram_origin == 0 && r.bin_size == .25,
        "Explicit endpoints and width are independent of observed extrema");

  named("uniform.single-bin-inclusive-bounds");
  r = accumulate_dataset_histogram(eager(PixelType::Float32,{-2,-1,-0.,1,2}),uniform(1,-1,1));
  counts(r,{3},5,5,0,1,1);
  check(r.histogram.edges == std::vector<double>({-1,1}) && r.bin_size == 2,
        "Both endpoints belong to one bin");

  named("uniform.empty-keeps-requested-domain");
  q = uniform(4,-8,8);
  q.area_roi = Selection::from_packed_mask(2,1,{0});
  r = accumulate_dataset_histogram(eager(PixelType::UInt8,{7,9}),q);
  counts(r,{0,0,0,0},0,0);
  check(!r.minimum && !r.maximum && r.plane_minimum == 7 && r.plane_maximum == 9 &&
        r.histogram.edges == std::vector<double>({-8,-4,0,4,8}) &&
        r.histogram_origin == -8 && r.bin_size == 4,
        "Empty UniformRange retains explicit bounds instead of raw no-finite domain");

  named("uniform.double-extremes-finite-edges");
  const double maximum = std::numeric_limits<double>::max();
  const double fmaximum = std::numeric_limits<float>::max();
  r = accumulate_dataset_histogram(eager(PixelType::Float32,
      {-fmaximum,-1,0,1,fmaximum}),uniform(1,-maximum,maximum));
  counts(r,{5},5,5);
  check(!r.bin_size && r.histogram.edges == std::vector<double>({-maximum,maximum}),
        "Overflowing positive width is absent, not infinity");
  r = accumulate_dataset_histogram(eager(PixelType::Float32,{-1,0,1}),uniform(2,-maximum,maximum));
  counts(r,{1,2},3,3);
  check(r.bin_size == maximum &&
        r.histogram.edges == std::vector<double>({-maximum,0,maximum}),
        "Rounded finite edges decide half-open membership even when normalized fractions collapse");

  named("uniform.underflow-width-collapsed-edge");
  const double tiny = std::numeric_limits<double>::denorm_min();
  r = accumulate_dataset_histogram(eager(PixelType::Float32,{0}),uniform(2,0,tiny));
  counts(r,{1,0},1,1);
  check(!r.bin_size && r.histogram.edges == std::vector<double>({0,0,tiny}),
        "Rounded zero width is absent and collapsed edges are permitted");

  named("uniform.maximum-bin-cardinality");
  r = accumulate_dataset_histogram(eager(PixelType::UInt8,{0,1,255}),uniform(65536,0,65536));
  counts(r,occupied(65536,{0,1,255}),3,3);
  std::vector<double> expected_edges(65537);
  std::iota(expected_edges.begin(),expected_edges.end(),0.);
  check(r.histogram.edges == expected_edges && r.bin_size == 1,
        "Complete maximum-size returned arrays");
}

void admission() {
  named("admission.request-fields-before-read-or-callback");
  auto s = std::make_shared<Source>(DatasetShape{2,3,4,3,5,1},PixelType::UInt16);
  Dataset d(s);
  std::size_t callbacks = 0;
  ExecutionControl control;
  control.checkpoint = [&] { ++callbacks; };
  control.progress = [&](std::size_t,std::size_t) { ++callbacks; };
  control.stage_progress = [&](std::string_view,std::size_t,std::size_t) { ++callbacks; };
  const auto invalid = [&](const DatasetHistogramRequest &q) {
    refuses([&] { estimate_dataset_histogram(d,q); },"Invalid estimate request must refuse");
    refuses([&] { accumulate_dataset_histogram(d,q,{},control); },"Invalid accumulation request must refuse");
    no_reads(*s,callbacks);
  };
  for (PlanePosition p : {PlanePosition{-1,0,0},PlanePosition{2,0,0},
       PlanePosition{0,-1,0},PlanePosition{0,3,0},PlanePosition{0,0,-1},PlanePosition{0,0,4}}) {
    DatasetHistogramRequest q;
    q.position = p;
    invalid(q);
  }
  for (int n : {0,255,257}) { DatasetHistogramRequest q; q.bins=n; invalid(q); }
  DatasetHistogramRequest q;
  q.range = std::array<double,2>{0,1}; invalid(q);
  q = {}; q.binning = static_cast<HistogramBinning>(77); invalid(q);
  q = {}; q.binning = HistogramBinning::UniformRange; invalid(q);
  for (int n : {0,65537}) invalid(uniform(n,0,1));
  for (std::array<double,2> bounds : {
       std::array<double,2>{0,0},std::array<double,2>{1,0},
       std::array<double,2>{0,std::numeric_limits<double>::infinity()},
       std::array<double,2>{std::numeric_limits<double>::quiet_NaN(),1}})
    invalid(uniform(4,bounds[0],bounds[1]));

  named("admission.nonarea-and-malformed-area");
  for (const char *kind : {"line","polyline","angle","point","multipoint","unknown"}) {
    q = {};
    q.area_roi = Selection(kind,{{1,1},{3,2},{4,2}});
    invalid(q);
  }
  q = {}; q.area_roi = Selection("rectangle",{{1,1},{1,2}}); invalid(q);
  q.area_roi = Selection("polygon",{{1,1},{3,2}}); invalid(q);
  q.area_roi = Selection("ellipse",{{0,0},{std::numeric_limits<double>::infinity(),2}}); invalid(q);
  q.area_roi = Selection("rotated_rectangle",{{2,2},{3,2},{4,2}}); invalid(q);
  q.area_roi = Selection::from_packed_mask(4,3,{0xff,0xf0}); invalid(q);
  q.area_roi = Selection("rectangle",{{0,0},{3,2}});
  q.area_roi->attributes["bad"] = std::numeric_limits<double>::infinity(); invalid(q);

  named("admission.unsupported-type-sample-policy");
  for (auto pair : {std::make_pair(PixelType::Float64,1),
                    std::make_pair(PixelType::UInt8,3),
                    std::make_pair(PixelType::Float32,4),
                    std::make_pair(static_cast<PixelType>(97),1)}) {
    auto bad = std::make_shared<Source>(DatasetShape{1,1,1,1,2,pair.second},pair.first);
    Dataset unsupported(bad);
    refuses([&] { estimate_dataset_histogram(unsupported); },"Unsupported descriptor must refuse before reading");
    refuses([&] { accumulate_dataset_histogram(unsupported,{}, {},control); },"Unsupported scalar service source must refuse");
    no_reads(*bad,callbacks);
  }
  refuses([&] { Dataset bad(s,{},static_cast<FloatingSamplePolicy>(71)); },"Unknown source policy refuses admission");
  auto virtual_float = std::make_shared<Source>(DatasetShape{1,1,1,1,2,1},PixelType::Float32);
  refuses([&] { Dataset bad(virtual_float,{},FloatingSamplePolicy::PreserveQuietNaNFloat32); },
          "Unreviewed virtual source cannot opt into quiet-NaN storage");
  no_reads(*virtual_float,callbacks);

  named("admission.canonical-samples-even-outside-roi");
  for (const auto &entry : std::vector<std::pair<PixelType,std::vector<double>>>{
       {PixelType::UInt8,{-1,.5,256}},
       {PixelType::UInt16,{-1,.5,65536}},
       {PixelType::Float32,{.1,std::numeric_limits<double>::max(),
          std::numeric_limits<double>::denorm_min(),
          std::numeric_limits<double>::quiet_NaN(),
          std::numeric_limits<double>::infinity(),
          -std::numeric_limits<double>::infinity(),double_bits(0x7ff0000000000001ULL)}}}) {
    for (double bad_sample : entry.second) {
      auto bad = source(image(entry.first,2,1,{1,bad_sample}));
      const auto original = image_bits(*bad->buffer);
      q = {}; q.area_roi = Selection("rectangle",{{0,0},{1,1}});
      rejects<std::invalid_argument>([&] { accumulate_dataset_histogram(Dataset(bad),q); },
          "Canonicality validation includes excluded ROI pixels");
      check(bad->reads == 1 && bad->regions == 0 && image_bits(*bad->buffer) == original,
            "Refusal validates one captured plane and preserves its bits");
    }
  }

  named("admission.reviewed-quiet-source-rejects-noncanonical-replacement");
  SharedPlanes owned(Stack{image(PixelType::Float32,1,1,{float_bits(0x7fc12345)})},
                     FloatingSamplePolicy::PreserveQuietNaNFloat32);
  const auto original = image_bits(owned[0]);
  for (double bad_sample : {.1,std::numeric_limits<double>::infinity(),
       double_bits(0x7ff0000000000001ULL),double_bits(0x7ff82468a0000001ULL)}) {
    auto bad = std::make_shared<const Image>(image(PixelType::Float32,1,1,{bad_sample}));
    rejects<std::invalid_argument>([&] { owned.replace_shared(0,bad); },
        "Reviewed immutable-source seam rejects malformed quiet encoding without normalization");
    check(image_bits(owned[0]) == original,"Failed shared replacement retains original payload");
  }

  named("admission.provider-shape-storage-type-null");
  for (int variant = 0; variant < 4; ++variant) {
    auto bad = std::make_shared<Source>(DatasetShape{1,1,1,1,2,1},PixelType::UInt8);
    bad->factory = [variant](PlanePosition) -> std::shared_ptr<const Image> {
      if (variant == 3) return nullptr;
      Image p(variant == 0 ? 3 : 2,1,1,variant == 1 ? PixelType::UInt16 : PixelType::UInt8);
      if (variant == 2) p.pixels.pop_back();
      return std::make_shared<const Image>(std::move(p));
    };
    rejects<std::exception>([&] { accumulate_dataset_histogram(Dataset(bad)); },
                            "Bad provider return must not yield detached counts");
    check(bad->reads == 1 && bad->regions == 0,"Provider validation uses only one acquisition");
  }
}

struct CallbackFailure : std::runtime_error {
  CallbackFailure() : std::runtime_error("independent callback sentinel") {}
};
void control() {
  named("control.estimate-components-and-tight-caps");
  auto s = source(image(PixelType::UInt16,5,3,{90,1,2,3,91,92,4,5,6,93,94,7,8,9,95}));
  Dataset d(s,calibration());
  DatasetHistogramRequest q;
  auto e = estimate_dataset_histogram(d,q);
  check(e.pixel_count == 15 && e.plane_bytes == 15*sizeof(double) &&
        e.mask_bytes == 0 && e.selection_bytes == 0 &&
        e.histogram_bytes == 256*sizeof(std::uint64_t)+257*sizeof(double) &&
        e.working_bytes >= e.plane_bytes+e.histogram_bytes,
        "No-ROI estimate charges exact one-plane and histogram array storage");
  q = uniform(10,0,100);
  q.area_roi = Selection::from_packed_mask(5,3,{0xfc,0x7e});
  q.area_roi->attributes["retained"] = std::string(123,'x');
  e = estimate_dataset_histogram(d,q);
  check(e.pixel_count == 15 && e.mask_bytes == 15*sizeof(double) &&
        e.histogram_bytes == 10*sizeof(std::uint64_t)+11*sizeof(double) &&
        e.selection_bytes >= sizeof(Selection)+q.area_roi->packed_mask().size()+123 &&
        e.working_bytes >= e.plane_bytes+e.mask_bytes+e.histogram_bytes+e.selection_bytes,
        "ROI estimate charges full mask and complete retained packed/attribute state");
  std::size_t callbacks = 0;
  ExecutionControl c;
  c.checkpoint = [&] { ++callbacks; };
  c.progress = [&](std::size_t,std::size_t) { ++callbacks; };
  c.stage_progress = [&](std::string_view,std::size_t,std::size_t) { ++callbacks; };
  DatasetHistogramLimits exact{15,e.working_bytes};
  for (auto limits : {DatasetHistogramLimits{14,e.working_bytes},
       DatasetHistogramLimits{15,e.working_bytes-1},DatasetHistogramLimits{0,e.working_bytes},
       DatasetHistogramLimits{15,0}}) {
    refuses([&] { estimate_dataset_histogram(d,q,limits); },"Estimator enforces checked positive caps");
    refuses([&] { accumulate_dataset_histogram(d,q,limits,c); },"Cap refusal before control/source");
    no_reads(*s,callbacks);
  }
  auto r = accumulate_dataset_histogram(d,q,exact,c);
  counts(r,sparse(10,{{0,6},{9,6}}),12,12);
  check(r.estimate.working_bytes == e.working_bytes && s->reads == 1 && callbacks > 0,
        "Exact admitted cap succeeds and result retains same bound");
  auto longer = d.calibration();
  longer.unit = std::string(211,'u'); longer.time_unit = std::string(197,'t');
  const auto bigger = estimate_dataset_histogram(d.with_calibration(longer),q);
  check(bigger.working_bytes >= e.working_bytes+(211-2)+(197-2),
        "Actual captured calibration strings contribute to bound");

  named("control.checked-size-overflow-without-read");
  callbacks = 0;
  const int huge = sizeof(std::size_t) >= 8 ? std::numeric_limits<int>::max() - 8194 : 60000;
  auto enormous = std::make_shared<Source>(DatasetShape{1,1,1,huge,huge,1},PixelType::UInt8);
  Dataset big(enormous);
  DatasetHistogramLimits unlimited{std::numeric_limits<std::size_t>::max(),std::numeric_limits<std::size_t>::max()};
  refuses([&] { estimate_dataset_histogram(big,{},unlimited); },"Checked bytes reject metadata-only multiplication overflow");
  refuses([&] { accumulate_dataset_histogram(big,{},unlimited,c); },"Overflow precedes user code and allocations");
  no_reads(*enormous,callbacks);
  auto many = std::make_shared<Source>(DatasetShape{1024,1024,1,1,1,1},PixelType::UInt16);
  DatasetHistogramRequest last;
  last.position = {1023,1023,0};
  check(estimate_dataset_histogram(Dataset(many),last).pixel_count == 1 && many->reads == 0,
        "Plane estimate never scales with scientific volume size");

  named("control.pre-cancel-before-callback-and-provider");
  auto untouched = source(image(PixelType::UInt8,2,1,{1,2}));
  callbacks = 0;
  c.cancellation = std::make_shared<CancellationToken>();
  c.cancellation->cancel();
  rejects<OperationCancelled>([&] { accumulate_dataset_histogram(Dataset(untouched),{}, {},c); },
                              "Valid pre-cancel has dedicated cancellation result");
  no_reads(*untouched,callbacks);

  named("control.one-acquisition-across-all-passes");
  auto once = source(image(PixelType::Float32,4,2,{-2,-1,0,1,2,3,4,5}));
  once->single_read = true;
  const auto before = image_bits(*once->buffer);
  q = uniform(4,-1,3);
  q.area_roi = Selection("rectangle",{{1,0},{4,2}});
  std::vector<std::pair<std::size_t,std::size_t>> progress;
  c = {};
  c.progress = [&](std::size_t done,std::size_t total) { progress.emplace_back(done,total); };
  r = accumulate_dataset_histogram(Dataset(once),q,{},c);
  counts(r,{1,1,1,1},6,6,0,0,2);
  extrema(r,-1,5,-2,5,8);
  check(once->reads == 1 && once->regions == 0 && image_bits(*once->buffer) == before,
        "Validation/extrema/histogram retain one immutable acquired plane");
  check(progress == std::vector<std::pair<std::size_t,std::size_t>>({{0,1},{1,1}}),
        "Success has the captured one-plane progress pair");

  named("control.reentry-captures-request-source-limits-and-control");
  auto captured = formula_source();
  captured->single_read = true;
  Dataset caller(captured,calibration());
  const Dataset retained = caller;
  q = {}; q.position = {1,2,3};
  q.area_roi = Selection("rectangle",{{1,1},{4,3}});
  q.area_roi->attributes["nested"] = metadata_array({true,std::int64_t{7},std::string("captured")});
  const auto requested = q;
  DatasetHistogramLimits caller_limits;
  const auto captured_estimate = estimate_dataset_histogram(caller,q);
  caller_limits.max_working_bytes = captured_estimate.working_bytes;
  c = {};
  const auto token = std::make_shared<CancellationToken>();
  c.cancellation = token;
  bool entered = false;
  std::size_t old_progress = 0;
  c.progress = [&](std::size_t,std::size_t) { ++old_progress; };
  c.checkpoint = [&] {
    if (entered) return;
    entered = true;
    check(estimate_dataset_histogram(retained,requested).working_bytes == captured_estimate.working_bytes,
          "Metadata-only callback reentry is independent of effective captured work");
    caller = eager(PixelType::UInt8,{255});
    q.position = {0,0,0}; q.area_roi.reset();
    q.binning = HistogramBinning::UniformRange; q.bins = 1; q.range = std::array<double,2>{0,1};
    caller_limits = {0,0};
    c.cancellation = std::make_shared<CancellationToken>(); c.cancellation->cancel();
    c.checkpoint = [] { throw CallbackFailure(); };
    c.progress = [](std::size_t,std::size_t) { throw CallbackFailure(); };
    c.stage_progress = [](std::string_view,std::size_t,std::size_t) { throw CallbackFailure(); };
  };
  r = accumulate_dataset_histogram(caller,q,caller_limits,c);
  counts(r,occupied(256,{0,42,85,128,170,213}),6,6);
  extrema(r,140,145,136,146,15);
  check(entered && old_progress == 2 && !token->canceled() && captured->reads == 1 &&
        captured->positions[0].t == 1 && captured->positions[0].c == 2 &&
        captured->positions[0].z == 3 && r.request.position.t == 1 &&
        r.request.position.c == 2 && r.request.position.z == 3 &&
        r.request.binning == HistogramBinning::ProcessorRaw && r.request.bins == 256 &&
        !r.request.range && r.request.area_roi == requested.area_roi &&
        r.estimate.working_bytes == captured_estimate.working_bytes,
        "Reentrant caller mutation cannot replace any effective captured input");
  same_calibration(retained.calibration(),calibration());

  named("control.callback-exception-before-source");
  untouched = source(image(PixelType::UInt8,2,1,{1,2}));
  c = {};
  c.checkpoint = [] { throw CallbackFailure(); };
  rejects<CallbackFailure>([&] { accumulate_dataset_histogram(Dataset(untouched),{}, {},c); },
                           "Original callback exception propagates unchanged");
  check(untouched->reads == 0,"First checkpoint failure precedes source acquisition");

  named("control.cancel-during-provider-return");
  auto cancel_source = source(image(PixelType::UInt8,2,1,{1,2}));
  c = {}; c.cancellation = std::make_shared<CancellationToken>();
  cancel_source->on_read = [token = c.cancellation] { token->cancel(); };
  rejects<OperationCancelled>([&] { accumulate_dataset_histogram(Dataset(cancel_source),{}, {},c); },
                              "Cancellation after acquisition cannot publish counts");
  check(cancel_source->reads == 1 && image_bits(*cancel_source->buffer) ==
        std::vector<std::uint64_t>({bits(1),bits(2)}), "Acquired source remains unchanged on cancellation");

  named("control.bounded-kernel-cancel-and-exception");
  for (bool cancel : {false,true}) {
    std::vector<double> values(8192);
    for (std::size_t i=0;i<values.size();++i) values[i] = static_cast<double>(i%256);
    auto long_source = source(image(PixelType::UInt8,128,64,std::move(values)));
    const auto original = image_bits(*long_source->buffer);
    c = {}; c.cancellation = std::make_shared<CancellationToken>();
    bool reached = false;
    c.stage_progress = [&](std::string_view,std::size_t done,std::size_t total) {
      if (total == 8192 && done > 0 && done < total) {
        reached = true;
        check(done <= 1024,"Long validation pass polls within its first 1024 units");
        if (cancel) c.cancellation->cancel();
        else throw CallbackFailure();
      }
    };
    if (cancel)
      rejects<OperationCancelled>([&] { accumulate_dataset_histogram(Dataset(long_source),{}, {},c); },
                                  "Mid-kernel cancellation discards detached result");
    else
      rejects<CallbackFailure>([&] { accumulate_dataset_histogram(Dataset(long_source),{}, {},c); },
                               "Mid-kernel callback exception propagates");
    check(reached && long_source->reads == 1 && long_source->regions == 0 &&
          image_bits(*long_source->buffer) == original,
          "Bounded interruption retains one unchanged source and returns no result");
  }
}
} // namespace

int main() {
  try {
    raw(); roi(); uniform_range(); admission(); control();
    std::cout << "dataset_histogram: " << checks << " checks; " << cases.size()
              << " named cases; sections raw/roi/uniform/admission/control\n";
    for (const auto &name : cases) std::cout << "case " << name << '\n';
    return 0;
  } catch (const std::exception &e) {
    const std::string message = e.what();
    if (!cases.empty() && message.rfind(cases.back() + ": ", 0) != 0)
      std::cerr << cases.back() << ": ";
    std::cerr << message << '\n';
    return 1;
  }
}
