// Public installed-SDK C++17 example. Physical spacing is owned policy;
// this consumer does not assert ImageJ/Slicer interpolation equivalence.
#include <imagejxx/application.hpp>
#include <imagejxx/metadata.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace imagejxx;
using Clock = std::chrono::steady_clock;
const auto started = Clock::now();
std::size_t emitted_rows = 0, emitted_bytes = 0;
constexpr std::size_t expected_rows = 44, maximum_output = 1024 * 1024;
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
void deadline() {
  require(Clock::now() - started < std::chrono::seconds(30),
          "Spacing SDK consumer exceeded its 30-second bound");
}
bool exact(double a, double b) { return std::memcmp(&a, &b, sizeof(double)) == 0; }
void near(double a, double b) {
  require(std::abs(a - b) <= 1e-12 * std::max({1., std::abs(a), std::abs(b)}),
          "Output world coordinates differ from the literal source mapping");
}
template<class Error, class Function>
std::string rejection(Function function) {
  try { function(); }
  catch (const Error &error) { return error.what(); }
  throw std::runtime_error("Expected public API rejection was not delivered");
}
std::string quote(const std::string &value) {
  constexpr char hex[] = "0123456789abcdef";
  std::string result = "\"";
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') { result += '\\'; result += char(c); }
    else if (c < 32) {
      result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15];
    } else result += char(c);
  }
  return result + '"';
}
template<class Values> std::string array(const Values &values) {
  std::ostringstream stream; stream << '[' << std::setprecision(17);
  bool first = true;
  for (const auto &value : values) {
    if (!first) stream << ',';
    first = false; stream << value;
  }
  stream << ']'; return stream.str();
}
std::string number(double value) {
  require(std::isfinite(value), "Observation metadata must be finite");
  std::ostringstream stream; stream << std::setprecision(17) << value;
  return stream.str();
}
void emit(const std::string &id, const std::string &fields) {
  deadline(); const auto line = "{\"case_id\":" + quote(id) + ',' + fields + "}\n";
  emitted_bytes += line.size(); ++emitted_rows;
  require(emitted_bytes <= maximum_output && emitted_rows <= expected_rows,
          "Spacing SDK observation inventory or byte bound exceeded");
  std::cout << line;
  require(bool(std::cout), "Could not write spacing SDK observations");
}
const char *type_name(PixelType type) {
  switch (type) {
  case PixelType::UInt8: return "UInt8";
  case PixelType::UInt16: return "UInt16";
  case PixelType::Float32: return "Float32";
  case PixelType::Float64: return "Float64";
  }
  throw std::runtime_error("Unexpected pixel type");
}
std::string stored_hex(const Dataset &dataset) {
  static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559,
                "This observation encoding requires IEEE binary64 doubles");
  constexpr char digits[] = "0123456789abcdef";
  std::string result; const auto shape = dataset.shape();
  for (int t = 0; t < shape.t; ++t) for (int c = 0; c < shape.c; ++c)
    for (int z = 0; z < shape.z; ++z) {
      deadline();
      for (double value : dataset.plane({t,c,z})->pixels) {
        std::uint64_t bits = 0; std::memcpy(&bits, &value, sizeof(bits));
        for (int byte = 0; byte < 8; ++byte) {
          const auto v = static_cast<unsigned char>(bits >> (8 * byte));
          result += digits[v >> 4]; result += digits[v & 15];
        }
      }
    }
  return result;
}
AxisRangeSpacingRequest request(Plane plane = Plane::XZ,
                                AxisRangeSampling sampling = AxisRangeSampling::Nearest) {
  AxisRangeSpacingRequest result;
  result.orientation.range.plane = plane;
  result.orientation.range.first = result.orientation.range.last = 1;
  result.physical_vertical_spacing = .5; result.sampling = sampling;
  return result;
}
double literal(int t, int c, int z, int y, int x, int sample) {
  return 1000. * t + 100. * c + 10. * z + 2. * y + x + sample;
}
Image source_plane(DatasetShape shape, PixelType type, PlanePosition position) {
  Image plane(shape.x, shape.y, shape.s, type);
  for (int y = 0; y < shape.y; ++y) for (int x = 0; x < shape.x; ++x)
    for (int s = 0; s < shape.s; ++s)
      plane.at(x,y,s) = literal(position.t, position.c, position.z, y, x, s);
  return plane;
}
Dataset source(DatasetShape shape = {1,1,3,2,2,1},
               PixelType type = PixelType::UInt16, SpatialCalibration calibration = {}) {
  Stack planes;
  for (int t = 0; t < shape.t; ++t) for (int c = 0; c < shape.c; ++c)
    for (int z = 0; z < shape.z; ++z)
      planes.push_back(source_plane(shape,type,{t,c,z}));
  return Dataset::from_planes(shape, std::move(planes), std::move(calibration));
}
class CountedSource final : public PlaneSource {
public:
  DatasetShape dimensions{1,1,3,2,2,1};
  PixelType type = PixelType::UInt16;
  mutable std::size_t plane_reads = 0, region_reads = 0;
  DatasetShape shape() const override { return dimensions; }
  PixelType pixel_type() const override { return type; }
  std::shared_ptr<const Image> read_plane(PlanePosition position) const override {
    ++plane_reads; return std::make_shared<const Image>(source_plane(dimensions,type,position));
  }
  Image read_region(PlanePosition position, int x, int y, int width, int height) const override {
    ++region_reads; Image result(width,height,dimensions.s,type);
    for (int v = 0; v < height; ++v) for (int u = 0; u < width; ++u)
      for (int s = 0; s < dimensions.s; ++s)
        result.at(u,v,s) = literal(position.t,position.c,position.z,y+v,x+u,s);
    return result;
  }
};
std::string observation(const AxisRangeSpacingResult &result) {
  const auto &cal = result.dataset.calibration();
  const auto &r = result.request.orientation.range;
  const auto &o = result.request.orientation;
  return "\"shape_TCZYXS\":" + array(result.dataset.shape().dimensions()) +
      ",\"pixel_type\":" + quote(type_name(result.dataset.pixel_type())) +
      ",\"stored_encoding\":\"ieee754_binary64_le_TCZYXS\",\"stored_hex\":" + quote(stored_hex(result.dataset)) +
      ",\"spacing\":" + array(cal.spacing) + ",\"origin\":" + array(cal.origin) +
      ",\"direction\":" + array(cal.direction) + ",\"time_origin\":" + number(cal.time_origin) +
      ",\"frame_interval\":" + number(cal.frame_interval) + ",\"unit\":" + quote(cal.unit) +
      ",\"time_unit\":" + quote(cal.time_unit) + ",\"vertical_count\":" + std::to_string(result.vertical_count) +
      ",\"source_vertical_spacing\":" + number(result.source_vertical_spacing) +
      ",\"voxel_step\":" + number(result.voxel_step) + ",\"working_bytes\":" + std::to_string(result.working_bytes) +
      ",\"request\":{\"plane\":" + quote(r.plane == Plane::XZ ? "XZ" : "YZ") +
      ",\"first\":" + std::to_string(r.first) + ",\"last\":" + std::to_string(r.last) +
      ",\"step\":" + std::to_string(r.step) + ",\"t\":" + std::to_string(r.t) + ",\"c\":" + std::to_string(r.c) +
      ",\"reverse_order\":" + std::to_string(int(o.reverse_order)) +
      ",\"flip_vertical\":" + std::to_string(int(o.flip_vertical)) +
      ",\"transpose\":" + std::to_string(int(o.transpose)) +
      ",\"physical_vertical_spacing\":" + number(result.request.physical_vertical_spacing) +
      ",\"sampling\":" + quote(result.request.sampling == AxisRangeSampling::Nearest ? "nearest" : "linear") + '}';
}
void estimate_agrees(const AxisRangeSpacingEstimate &estimate, const AxisRangeSpacingResult &result) {
  const auto shape = result.dataset.shape();
  require(estimate.width == shape.x && estimate.height == shape.y && estimate.plane_count == shape.z &&
          estimate.samples == shape.s && estimate.vertical_count == result.vertical_count &&
          estimate.voxel_step == result.voxel_step && estimate.source_vertical_spacing == result.source_vertical_spacing &&
          estimate.working_bytes == result.working_bytes, "Estimate and execution disagree");
  auto copy = estimate; copy.output_axes[0] = "changed"; ++copy.width;
  require(estimate.output_axes[0] != "changed" && estimate.width == shape.x, "Estimate copy aliases values");
  auto copied = result; copied.request.orientation.range.step = 99; copied.request.physical_vertical_spacing = 99;
  copied.first_plane_geometry.horizontal_axis = "changed";
  require(result.request.orientation.range.step != 99 && result.request.physical_vertical_spacing != 99,
          "Result/request copy aliases mutable values");
  require(result.first_plane_geometry.horizontal_axis != "changed", "Result geometry copy aliases mutable values");
}
void literal_samples() {
  const std::array<PixelType,4> types{PixelType::UInt8,PixelType::UInt16,PixelType::Float32,PixelType::Float64};
  for (int profile = 0; profile < 6; ++profile) for (int mode = 0; mode < 2; ++mode) {
    const auto type = profile < 4 ? types[profile] : PixelType::UInt8;
    const int samples = profile < 4 ? 1 : (profile == 4 ? 3 : 4);
    const auto plane = profile < 4 ? Plane::XZ : Plane::YZ;
    const auto req = request(plane,mode ? AxisRangeSampling::Linear : AxisRangeSampling::Nearest);
    const auto input = source({1,1,3,2,2,samples},type); const auto before = stored_hex(input);
    const auto estimate = estimate_axis_range_spacing(input,req);
    const auto result = reslice_spaced_axis_range(input,req); estimate_agrees(estimate,result);
    require(result.dataset.shape().dimensions() == std::array<int,6>{1,1,1,5,2,samples} &&
            result.dataset.pixel_type() == type, "Literal output shape/type differs");
    const std::array<double,5> levels = mode ? std::array<double,5>{0,5,10,15,20} : std::array<double,5>{0,10,10,20,20};
    const auto output = result.dataset.plane();
    for (int j = 0; j < 5; ++j) for (int a = 0; a < 2; ++a) for (int s = 0; s < samples; ++s)
      require(exact(output->at(a,j,s),levels[j] + (plane == Plane::XZ ? 2 + a : 1 + 2 * a) + s),
              "Literal nearest/linear output pixels differ");
    require(stored_hex(input) == before, "Spacing changed literal input pixels");
    emit("literal." + std::string(type_name(type)) + ".s" + std::to_string(samples) +
         (plane == Plane::XZ ? ".xz." : ".yz.") + (mode ? "linear" : "nearest"),
         observation(result) + ",\"source_stored_hex\":" + quote(before));
  }
}
void scientific_geometry() {
  SpatialCalibration cal; cal.spacing = {.5,1.25,2}; cal.origin = {11,-7,19};
  cal.direction = {-1,.25,0,0,1,-.6,.25,0,.8}; cal.unit = "um";
  cal.time_unit = "s"; cal.time_origin = 2.25; cal.frame_interval = .75;
  const auto input = source({2,3,3,3,4,1},PixelType::Float64,cal); const auto before = stored_hex(input);
  const std::array<std::array<double,3>,3> basis{{{-.5,0,.125},{.3125,1.25,0},{0,-1.2,1.6}}};
  for (auto plane : {Plane::XZ,Plane::YZ}) for (int mode = 0; mode < 2; ++mode) {
    auto req = request(plane,mode ? AxisRangeSampling::Linear : AxisRangeSampling::Nearest);
    req.orientation.range = {plane,0,2,2,1,2}; req.physical_vertical_spacing = 1;
    req.orientation.reverse_order = req.orientation.flip_vertical = req.orientation.transpose = true;
    const auto result = reslice_spaced_axis_range(input,req);
    estimate_agrees(estimate_axis_range_spacing(input,req),result);
    const int horizontal = plane == Plane::XZ ? 0 : 1, fixed_axis = 1 - horizontal;
    const int a_count = plane == Plane::XZ ? 4 : 3;
    require(result.dataset.shape().dimensions() == std::array<int,6>{1,1,2,a_count,5,1} &&
            result.source_vertical_spacing == 2 && result.voxel_step == .5 && result.vertical_count == 5,
            "Scientific physical grid dimensions/spacing differ");
    const std::array<double,5> values = mode ? std::array<double,5>{2,1.5,1,.5,0} : std::array<double,5>{2,2,1,1,0};
    for (int k = 0; k < 2; ++k) {
      const int fixed = k == 0 ? 2 : 0; const auto output = result.dataset.plane({0,0,k});
      for (int a = 0; a < a_count; ++a) for (int j = 0; j < 5; ++j) {
        const auto expected = 1200 + 10 * values[j] + (plane == Plane::XZ ? 2 * fixed + a : 2 * a + fixed);
        require(exact(output->at(j,a),expected), "Scientific selected T/C pixels differ");
        // Geometry follows fractional sample positions even for Nearest.
        std::array<double,3> point{}; point[horizontal] = a; point[fixed_axis] = fixed; point[2] = 2 - .5 * j;
        const auto world = result.dataset.calibration().voxel_to_world({double(j),double(a),double(k)});
        for (int row = 0; row < 3; ++row)
          near(world[row],cal.origin[row] + basis[0][row] * point[0] + basis[1][row] * point[1] + basis[2][row] * point[2]);
      }
    }
    const auto &out = result.dataset.calibration();
    require(out.unit == "um" && out.time_unit == "s" && out.time_origin == 3 && out.frame_interval == .75 &&
            result.first_plane_geometry.origin == out.origin && result.first_plane_geometry.horizontal_axis == "Z" &&
            result.first_plane_geometry.vertical_axis == (plane == Plane::XZ ? "X" : "Y"),
            "Scientific first-output geometry/time metadata differ");
    for (int row = 0; row < 3; ++row) {
      near(result.first_plane_geometry.horizontal_step[row],-.5 * basis[2][row]);
      near(result.first_plane_geometry.vertical_step[row],basis[horizontal][row]);
      near(out.direction[row * 3 + 2] * out.spacing[2],-2 * basis[fixed_axis][row]);
    }
    require(stored_hex(input) == before, "Scientific source pixels changed");
    emit(std::string("scientific.") + (plane == Plane::XZ ? "xz." : "yz.") + (mode ? "linear" : "nearest"),
         observation(result) + ",\"source_stored_hex\":" + quote(before));
  }
}
void stored_values() {
  const std::array<PixelType,4> types{PixelType::UInt8,PixelType::UInt16,PixelType::Float32,PixelType::Float64};
  const std::array<std::array<double,2>,4> values{{{-.25,255.75},{65535.75,65536.25},{0x1.0000000001000p+0,-0.},{-0.,.25}}};
  for (int profile = 0; profile < 4; ++profile) for (int mode = 0; mode < 2; ++mode) {
    Stack planes;
    for (int z = 0; z < 3; ++z) {
      Image image(2,2,1,types[profile]);
      for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) image.at(x,y) = values[profile][x];
      planes.push_back(std::move(image));
    }
    const auto input = Dataset::from_stack(std::move(planes)); const auto before = stored_hex(input);
    const auto result = reslice_spaced_axis_range(input,request(Plane::XZ,mode ? AxisRangeSampling::Linear : AxisRangeSampling::Nearest));
    for (int j = 0; j < 5; ++j) for (int x = 0; x < 2; ++x)
      require(exact(result.dataset.plane()->at(x,j),values[profile][x]),
              "Copy/endpoints/equal-value branch canonicalized stored doubles");
    require(stored_hex(input) == before, "Stored-value source was changed");
    emit("stored." + std::string(type_name(types[profile])) + (mode ? ".linear" : ".nearest"),
         observation(result) + ",\"source_stored_hex\":" + quote(before));
  }
}
Dataset quiet_source() {
  Stack planes; constexpr std::array<std::uint32_t,3> bits{0x80000000u,0x7fc12345u,0xffc23456u};
  for (int z = 0; z < 3; ++z) {
    Image image(2,2,1,PixelType::Float32);
    std::fill(image.pixels.begin(),image.pixels.end(),float32_source_sample(bits[z],FloatingSamplePolicy::PreserveQuietNaNFloat32));
    planes.push_back(std::move(image));
  }
  return Dataset::from_stack(std::move(planes),{},FloatingSamplePolicy::PreserveQuietNaNFloat32);
}
void quiet_nearest() {
  const auto input = quiet_source(); const auto before = stored_hex(input);
  const auto result = reslice_spaced_axis_range(input,request());
  constexpr std::array<std::uint32_t,5> expected{0x80000000u,0x7fc12345u,0x7fc12345u,0xffc23456u,0xffc23456u};
  for (int j = 0; j < 5; ++j) for (int x = 0; x < 2; ++x)
    require(float32_source_sample_bits(result.dataset.plane()->at(x,j),result.dataset.sample_policy()) == expected[j],
            "Quiet nearest changed payload/sign/zero bits");
  require(result.dataset.sample_policy() == FloatingSamplePolicy::PreserveQuietNaNFloat32 && stored_hex(input) == before,
          "Quiet nearest changed policy/source");
  emit("quiet.eager.nearest",observation(result) + ",\"source_stored_hex\":" + quote(before));
}
std::string counters(const CountedSource &provider, std::size_t callbacks, const std::string &error) {
  return "\"error\":" + quote(error) + ",\"plane_reads\":" + std::to_string(provider.plane_reads) +
      ",\"region_reads\":" + std::to_string(provider.region_reads) + ",\"callback_count\":" + std::to_string(callbacks);
}
void admission() {
  for (int scenario = 0; scenario < 6; ++scenario) {
    auto provider = std::make_shared<CountedSource>(); const Dataset input(provider); auto req = request();
    const auto valid = estimate_axis_range_spacing(input,req);
    require(provider->plane_reads == 0 && provider->region_reads == 0, "Metadata estimate read finite provider");
    const std::array<std::string,6> labels{"xy","zero_spacing","zero_step","negative_first","memory_minus_one","nan_spacing"};
    if (scenario == 0) req.orientation.range.plane = Plane::XY;
    if (scenario == 1) req.physical_vertical_spacing = 0;
    if (scenario == 2) req.orientation.range.step = 0;
    if (scenario == 3) req.orientation.range.first = -1;
    if (scenario == 4) req.orientation.range.max_working_bytes = valid.working_bytes - 1;
    if (scenario == 5) req.physical_vertical_spacing = std::numeric_limits<double>::quiet_NaN();
    std::size_t callbacks = 0; ExecutionControl control;
    control.checkpoint = [&] { ++callbacks; }; control.progress = [&](std::size_t,std::size_t) { ++callbacks; };
    std::string error;
    if (scenario == 3) {
      rejection<std::out_of_range>([&] { estimate_axis_range_spacing(input,req); });
      error = rejection<std::out_of_range>([&] { reslice_spaced_axis_range(input,req,control); });
    } else {
      rejection<std::invalid_argument>([&] { estimate_axis_range_spacing(input,req); });
      error = rejection<std::invalid_argument>([&] { reslice_spaced_axis_range(input,req,control); });
    }
    require(provider->plane_reads == 0 && provider->region_reads == 0 && callbacks == 0,
            "Invalid admission read provider or called processing callback");
    const auto refusal_plane_reads = provider->plane_reads;
    const auto refusal_region_reads = provider->region_reads;
    if (scenario == 4) {
      ++req.orientation.range.max_working_bytes;
      const auto output = reslice_spaced_axis_range(input,req);
      require(output.working_bytes == req.orientation.range.max_working_bytes &&
              provider->plane_reads + provider->region_reads > 0, "Exact working-byte cap was not processed");
      emit("admission." + labels[scenario],counters(*provider,callbacks,error) +
           ",\"refusal_plane_reads\":" + std::to_string(refusal_plane_reads) +
           ",\"refusal_region_reads\":" + std::to_string(refusal_region_reads) +
           ",\"exact_cap_output\":{" + observation(output) + '}');
    } else emit("admission." + labels[scenario],counters(*provider,callbacks,error));
  }
  for (int samples : {2,5}) {
    auto provider = std::make_shared<CountedSource>(); provider->dimensions.s = samples;
    const auto error = rejection<std::invalid_argument>([&] { Dataset input(provider); (void)input; });
    require(provider->plane_reads == 0 && provider->region_reads == 0, "Invalid sample admission read provider");
    emit("admission.s" + std::to_string(samples),counters(*provider,0,error));
  }
  auto provider = std::make_shared<CountedSource>(); provider->type = PixelType::Float32;
  const auto error = rejection<std::invalid_argument>([&] {
    Dataset input(provider,{},FloatingSamplePolicy::PreserveQuietNaNFloat32); (void)input;
  });
  require(provider->plane_reads == 0 && provider->region_reads == 0, "Lazy quiet admission read pixels");
  emit("admission.quiet.lazy",counters(*provider,0,error));
  const auto quiet = quiet_source(); const auto before = stored_hex(quiet);
  const auto req = request(Plane::XZ,AxisRangeSampling::Linear);
  std::size_t callbacks = 0; ExecutionControl control; control.checkpoint = [&] { ++callbacks; };
  rejection<std::invalid_argument>([&] { estimate_axis_range_spacing(quiet,req); });
  const auto quiet_error = rejection<std::invalid_argument>([&] { reslice_spaced_axis_range(quiet,req,control); });
  require(callbacks == 0 && stored_hex(quiet) == before, "Quiet Linear admission ran callback/changed source");
  emit("admission.quiet.linear","\"error\":" + quote(quiet_error) + ",\"callback_count\":" + std::to_string(callbacks) +
       ",\"source_stored_hex\":" + quote(before));
}
std::shared_ptr<Document> owner() {
  DocumentData data{source(),{"SDK source",{{"retained",true}}},{},{}};
  data.selection = Selection("point",{{1,1}}); data.previous_selection = Selection("point",{{0,0}});
  data.rois = {{"saved",Selection("point",{{0,0}}),0,0,0}}; data.overlays = {{{"marker",true}}};
  data.metadata.extensions["opaque"] = true; data.metadata.record_layout["old"] = true;
  data.spectrum = FourierState(Spectrum{2,2,std::vector<std::complex<double>>(4)},std::array<int,2>{2,2});
  auto result = std::make_shared<Document>(std::move(data));
  auto edit = result->begin_edit(); edit.set_property("undoable",true); result->commit(edit); result->undo();
  return result;
}
void unchanged(const Document &document, const DocumentSnapshot &before, const HistoryInfo &history) {
  const auto after = document.snapshot();
  require(after.same_document(before) && after.shares_data(before) && after.data_identity() == before.data_identity() &&
          after.revisions().matches(before.revisions(),Change::All) && after.dirty() == before.dirty() &&
          document.history().undo_steps == history.undo_steps && document.history().redo_steps == history.redo_steps &&
          after.display().crosshair == before.display().crosshair && after.display().revision == before.display().revision,
          "Operation changed retained owner/data/history/display");
}
std::string document_state(const Document &document) {
  const auto snap = document.snapshot(); const auto rev = snap.revisions(); const auto history = document.history();
  return "{\"id\":" + std::to_string(document.id) + ",\"data_identity\":" +
      (snap.data_identity() ? std::to_string(*snap.data_identity()) : "null") +
      ",\"revisions\":" + array(std::array<std::uint64_t,4>{rev.pixels,rev.metadata,rev.selection,rev.display}) +
      ",\"dirty\":" + std::to_string(int(snap.dirty())) + ",\"undo_steps\":" + std::to_string(history.undo_steps) +
      ",\"redo_steps\":" + std::to_string(history.redo_steps) + ",\"roi_count\":" + std::to_string(snap.data().rois.size()) +
      ",\"overlay_count\":" + std::to_string(snap.data().overlays.size()) +
      ",\"selection_present\":" + std::to_string(int(bool(snap.data().selection))) +
      ",\"previous_selection_present\":" + std::to_string(int(bool(snap.data().previous_selection))) +
      ",\"spectrum_present\":" + std::to_string(int(bool(snap.data().spectrum))) + '}';
}
void fresh(const PreparedOperation &prepared, std::uint64_t source_id) {
  const auto candidate = prepared.prepared_new_document(); require(bool(candidate), "Fresh candidate missing");
  const auto snap = candidate->snapshot(); const auto &data = snap.data();
  require(prepared.operation() == "axis_range_spacing" && prepared.target().dependencies() == (Change::Pixels | Change::Metadata) &&
          candidate->id != source_id && !snap.dirty() && snap.data_identity() == 0 &&
          candidate->history().undo_steps == 0 && candidate->history().redo_steps == 0 && !data.selection && !data.previous_selection &&
          data.rois.empty() && data.overlays.empty() && !data.spectrum && data.metadata.properties.empty() && data.metadata.record_layout.empty() &&
          data.metadata.extensions.size() == 1 && data.metadata.extensions.count("axis_range_spacing") == 1,
          "Fresh ordinary output retained source annotations/history/state");
  const auto node = std::get<std::shared_ptr<const MetadataTree>>(data.metadata.extensions.at("axis_range_spacing"));
  const auto &provenance = std::get<MetadataObject>(node->value);
  require(std::get<std::uint64_t>(provenance.at("source_document_id")) == source_id &&
          std::get<std::string>(provenance.at("sampling")) == "nearest", "Captured source provenance differs");
}
void publication() {
  for (int scenario = 0; scenario < 3; ++scenario) {
    Application app; const auto input = owner(), other = std::make_shared<Document>(source(),"Other");
    app.adopt(input); app.adopt(other); app.activate(input->id);
    const auto before = input->snapshot(); const auto history = input->history();
    const auto other_before = other->snapshot(); const auto other_history = other->history();
    auto req = request(); const auto original = req; std::size_t callbacks = 0;
    ExecutionControl control; control.checkpoint = [&] {
      if (++callbacks == 1) { app.activate(other->id); req.physical_vertical_spacing = 99; req.orientation.range.step = 99; }
    };
    auto prepared = scenario == 1 ? prepare_axis_range_spacing(before,req) :
        (scenario == 0 ? app.prepare_axis_range_spacing(req,0,control) : app.prepare_axis_range_spacing(req,input->id));
    fresh(prepared,input->id); const auto candidate = prepared.prepared_new_document();
    require(!app.contains(candidate->id) && app.document_ids().size() == 2 &&
            prepared.target().source().same_document(before), "Preparation registered/retargeted the captured owner");
    const auto expected = reslice_spaced_axis_range(before.data().dataset,original);
    require(stored_hex(candidate->snapshot().data().dataset) == stored_hex(expected.dataset), "Captured callback retargeted request/source");
    unchanged(*input,before,history); unchanged(*other,other_before,other_history);
    if (scenario == 2) {
      input->select(Selection("point",{{0,1}})); auto display = input->snapshot().display(); display.zoom = 2; input->set_display(display);
    }
    const auto at_publish = input->snapshot(); const auto at_history = input->history();
    auto display = candidate->snapshot().display(); display.zoom = 2; candidate->set_display(display); candidate->set_history_limit(7);
    const auto published = app.publish(prepared);
    require(published.created && published.document == candidate && published.document_id == candidate->id && !prepared.active() &&
            app.contains(candidate->id) && app.active_document_id() == candidate->id && app.document_ids().size() == 3,
            "Publication did not attach exact staged candidate once");
    unchanged(*input,at_publish,at_history); unchanged(*other,other_before,other_history);
    const auto ids = app.document_ids(); const auto active = app.active_document_id();
    const auto error = rejection<std::invalid_argument>([&] { app.publish(prepared); });
    require(app.document_ids() == ids && app.active_document_id() == active, "Repeat publication changed membership");
    auto published_result = expected;
    published_result.dataset = candidate->snapshot().data().dataset;
    emit(scenario == 0 ? "publication.request_first_captured_default" : (scenario == 1 ? "publication.free_snapshot" : "publication.selection_display_allowed"),
         "\"callback_count\":" + std::to_string(callbacks) + ",\"source\":" + document_state(*input) +
         ",\"output\":" + document_state(*candidate) + ",\"document_ids\":" + array(ids) +
         ",\"active_document_id\":" + std::to_string(active) + ",\"repeat_error\":" + quote(error) +
         ",\"result\":{" + observation(published_result) + '}');
  }
}
void cancellation() {
  for (int scenario = 0; scenario < 2; ++scenario) {
    Application app; const auto input = owner(); app.adopt(input);
    const auto before = input->snapshot(); const auto history = input->history();
    const auto ids = app.document_ids(); const auto active = app.active_document_id();
    auto token = std::make_shared<CancellationToken>(); ExecutionControl control; control.cancellation = token;
    auto provider = std::make_shared<CountedSource>();
    std::size_t callbacks = 0, sample_work = 0, final_progress = 0;
    if (scenario == 0) token->cancel();
    control.stage_progress = [&](std::string_view stage,std::size_t done,std::size_t) {
      ++callbacks; if (stage == "axis_range_spacing_sample" && done > 0) { ++sample_work; token->cancel(); }
    };
    control.progress = [&](std::size_t done,std::size_t total) { if (done == total) ++final_progress; };
    if (scenario == 0) {
      const Dataset lazy(provider);
      rejection<OperationCancelled>([&] { reslice_spaced_axis_range(lazy,request(),control); });
      require(provider->plane_reads == 0 && provider->region_reads == 0,
              "Pre-canceled public reslice read provider pixels");
    }
    const auto error = rejection<OperationCancelled>([&] { app.prepare_axis_range_spacing(request(),input->id,control); });
    unchanged(*input,before,history);
    require(app.document_ids() == ids && app.active_document_id() == active && final_progress == 0 &&
            (scenario == 0 ? callbacks == 0 : sample_work > 0), "Cancellation published/completed output or lacked stimulus");
    emit(scenario == 0 ? "cancel.pre_canceled" : "cancel.after_sample_work",
         "\"error\":" + quote(error) + ",\"callback_count\":" + std::to_string(callbacks) +
         ",\"sample_work_callbacks\":" + std::to_string(sample_work) + ",\"final_progress_callbacks\":" + std::to_string(final_progress) +
         ",\"counted_provider_role\":" + quote(scenario == 0 ? "pre_canceled_direct_reslice" : "not_exercised_eager_application_case") +
         ",\"finite_provider_plane_reads\":" + std::to_string(provider->plane_reads) +
         ",\"finite_provider_region_reads\":" + std::to_string(provider->region_reads) +
         ",\"source\":" + document_state(*input) + ",\"document_ids\":" + array(ids) +
         ",\"active_document_id\":" + std::to_string(active));
  }
}
void stale_publication() {
  const std::array<std::string,4> names{"pixels","calibration","candidate_pixels","closed_source"};
  for (int scenario = 0; scenario < 4; ++scenario) {
    Application app; const auto input = owner(), other = std::make_shared<Document>(source(),"Other");
    app.adopt(input); app.adopt(other);
    auto prepared = app.prepare_axis_range_spacing(request(),input->id); const auto candidate = prepared.prepared_new_document();
    if (scenario == 0) { auto tx = input->begin_edit(); tx.replace_plane({},Image(2,2,1,PixelType::UInt16)); input->commit(tx); }
    if (scenario == 1) { auto tx = input->begin_edit(); auto cal = input->snapshot().data().dataset.calibration(); cal.origin[0] = 7; tx.set_calibration(cal); input->commit(tx); }
    if (scenario == 2) { auto tx = candidate->begin_edit(); tx.replace_plane({},Image(2,5,1,PixelType::UInt16)); candidate->commit(tx); }
    if (scenario == 3) app.close(input->id,true);
    const auto before = input->snapshot(); const auto history = input->history();
    const auto ids = app.document_ids(); const auto active = app.active_document_id();
    const auto error = scenario == 3 ? rejection<DocumentUnavailable>([&] { app.publish(prepared); }) :
        rejection<RevisionConflict>([&] { app.publish(prepared); });
    unchanged(*input,before,history);
    require(prepared.active() && !app.contains(candidate->id) && app.document_ids() == ids && app.active_document_id() == active,
            "Stale/closed/tampered publication changed app or consumed candidate");
    emit("publish_rejection." + names[scenario],"\"error\":" + quote(error) + ",\"source\":" + document_state(*input) +
         ",\"candidate\":" + document_state(*candidate) + ",\"document_ids\":" + array(ids) +
         ",\"active_document_id\":" + std::to_string(active));
  }
}
void defaults() {
  const AxisRangeSpacingRequest spacing;
  const auto &r = spacing.orientation.range;
  require(spacing.physical_vertical_spacing == 1 && spacing.sampling == AxisRangeSampling::Nearest &&
          r.plane == Plane::XZ && r.first == 0 && r.last == 0 && r.step == 1 && r.t == 0 && r.c == 0 &&
          r.max_working_bytes == 512ULL * 1024 * 1024 && !spacing.orientation.reverse_order &&
          !spacing.orientation.flip_vertical && !spacing.orientation.transpose &&
          AxisPlaneExportRequest{}.plane == Plane::XY && AxisRangeResliceRequest{}.plane == Plane::XZ,
          "Existing public request defaults changed");
}
} // namespace
int main(int argc, char **) {
  try {
    require(argc == 1, "Spacing SDK example accepts no arguments"); defaults();
    literal_samples(); scientific_geometry(); stored_values(); quiet_nearest(); admission();
    publication(); cancellation(); stale_publication();
    require(emitted_rows == expected_rows, "Spacing SDK finite case inventory differs"); deadline(); return 0;
  } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
