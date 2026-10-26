#include <imagejxx/dataset_projection.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace imagejxx;
namespace {
void check(bool value) {
  if (!value) throw std::runtime_error("Dataset projection layout assertion failed");
}
template<class F> void refuses(F function) {
  bool refused = false;
  try { function(); } catch (const std::logic_error &) { refused = true; }
  check(refused);
}
class UnreadableSource final : public PlaneSource {
public:
  DatasetShape shape() const override { return {3, 2, 9, 5, 7, 3}; }
  PixelType pixel_type() const override { return PixelType::UInt16; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    throw std::runtime_error("Layout must not read source planes");
  }
};
} // namespace

int main() {
  refuses([] { DatasetZProjectionLayout{}.contributor({}, 0); });
  SpatialCalibration calibration;
  calibration.spacing = {.5, 2, 3};
  calibration.origin = {10, -4, 8};
  calibration.direction = {1, .25, .5, 0, 1, .25, 0, 0, -1};
  calibration.time_origin = -3;
  calibration.frame_interval = 2.5;
  Dataset source(std::make_shared<UnreadableSource>(), calibration);
  DatasetReadObserver observer;
  auto observed = source.observe_reads(observer);
  DatasetZProjectionRequest request;
  request.first = 2; request.last = 7; request.group_size = 3;
  request.active_t = 2; request.active_c = 1;
  auto layout = describe_dataset_z_projection(observed, request);
  check(layout.shape.dimensions() == std::array<int, 6>{3, 2, 2, 5, 7, 3});
  check(layout.pixel_type == PixelType::Float64);
  check(layout.output_pixel_bytes == 1260 * sizeof(double));
  check(layout.reduction_scratch_bytes == 3 * sizeof(double));
  check(layout.calibration.origin == std::array<double, 3>{13, -2.5, 2});
  check(layout.calibration.spacing == std::array<double, 3>{.5, 2, 9});
  check(layout.calibration.direction == calibration.direction);
  check(layout.calibration.time_origin == -3 && layout.calibration.frame_interval == 2.5);
  for (int t = 0; t < 3; ++t) for (int c = 0; c < 2; ++c)
    for (int z = 0; z < 2; ++z) for (int k = 0; k < 3; ++k) {
      auto position = layout.contributor({t, c, z}, k);
      check(position.t == t && position.c == c && position.z == 2 + 3*z + k);
      // An asymmetric independent coordinate encoding exposes axis mixing.
      check(10000*position.t + 1000*position.c + position.z == 10000*t + 1000*c + 2 + 3*z + k);
    }
  request.all_time_frames = false; request.all_channels = false;
  layout = describe_dataset_z_projection(observed, request);
  check(layout.shape.dimensions() == std::array<int, 6>{1, 1, 2, 5, 7, 3});
  check(layout.calibration.time_origin == 2 && layout.calibration.frame_interval == 2.5);
  auto position = layout.contributor({0, 0, 1}, 2);
  check(position.t == 2 && position.c == 1 && position.z == 7);
  for (bool all_t : {false, true}) for (bool all_c : {false, true}) {
    request.all_time_frames = all_t; request.all_channels = all_c;
    auto selected = describe_dataset_z_projection(observed, request);
    check(selected.shape.t == (all_t ? 3 : 1) && selected.shape.c == (all_c ? 2 : 1));
    auto first = selected.contributor({0, 0, 0}, 0);
    check(first.t == (all_t ? 0 : 2) && first.c == (all_c ? 0 : 1) && first.z == 2);
  }
  for (auto method : {"maximum", "minimum", "mean", "sum", "median", "stddev"}) {
    request.method = method; request.group_size = 0;
    layout = describe_dataset_z_projection(observed, request);
    check(layout.shape.z == 1 && layout.contributors_per_group == 6);
    check(layout.calibration.spacing[2] == 3);
    check(layout.pixel_type == ((request.method == "maximum" || request.method == "minimum")
          ? PixelType::UInt16 : PixelType::Float64));
  }
  request.group_size = 1;
  check(describe_dataset_z_projection(observed, request).shape.z == 6);
  auto valid = request;
  for (int group : {-1, 4, 7}) {
    request = valid; request.group_size = group;
    refuses([&] { describe_dataset_z_projection(observed, request); });
  }
  for (int index : {-1, 9}) {
    request = valid; request.first = index;
    refuses([&] { describe_dataset_z_projection(observed, request); });
    request = valid; request.last = index;
    refuses([&] { describe_dataset_z_projection(observed, request); });
  }
  for (bool all : {false, true}) {
    request = valid; request.all_time_frames = all; request.active_t = 3;
    refuses([&] { describe_dataset_z_projection(observed, request); });
    request = valid; request.all_channels = all; request.active_c = 2;
    refuses([&] { describe_dataset_z_projection(observed, request); });
  }
  request = valid; request.method = "Maximum";
  refuses([&] { describe_dataset_z_projection(observed, request); });
  request = valid; layout = describe_dataset_z_projection(observed, request);
  request.max_output_pixel_bytes = layout.output_pixel_bytes;
  describe_dataset_z_projection(observed, request);
  --request.max_output_pixel_bytes;
  refuses([&] { describe_dataset_z_projection(observed, request); });
  for (auto bad : {PlanePosition{-1, 0, 0}, PlanePosition{0, 2, 0}, PlanePosition{0, 0, 6}})
    refuses([&] { layout.contributor(bad, 0); });
  refuses([&] { layout.contributor({0, 0, 0}, -1); });
  refuses([&] { layout.contributor({0, 0, 0}, 1); });
  refuses([&] { describe_dataset_z_projection({3, 2, 9, 5, 7, 3}, static_cast<PixelType>(99), calibration, valid); });
  auto huge_calibration = calibration;
  huge_calibration.spacing[2] = std::numeric_limits<double>::max();
  request = valid; request.first = 0; request.last = 1; request.group_size = 2;
  refuses([&] { describe_dataset_z_projection({3, 2, 9, 5, 7, 3}, PixelType::UInt16, huge_calibration, request); });
  request = {}; // A one-Z time series remains T, never an implicit Z stack.
  layout = describe_dataset_z_projection({8, 2, 1, 5, 7, 1}, PixelType::Float32, calibration, request);
  check(layout.shape.t == 8 && layout.shape.c == 2 && layout.shape.z == 1);
  for (int samples : {1, 3, 4}) for (auto type : {PixelType::UInt8, PixelType::UInt16, PixelType::Float32, PixelType::Float64}) {
    request.method = "minimum";
    layout = describe_dataset_z_projection({3, 2, 1, 5, 7, samples}, type, calibration, request);
    check(layout.shape.s == samples && layout.shape.c == 2 && layout.pixel_type == type);
  }
  refuses([&] { describe_dataset_z_projection({2147483647, 2147483647, 1, 1, 1, 1}, PixelType::UInt8, calibration, request); });
  refuses([&] { describe_dataset_z_projection({3, 2, 1, 5, 7, 2}, PixelType::UInt8, calibration, request); });
  SpatialCalibration zero;
  zero.origin[0] = -0.0; zero.time_origin = -0.0;
  layout = describe_dataset_z_projection({3, 2, 1, 5, 7, 1}, PixelType::UInt8, zero, request);
  check(std::signbit(layout.calibration.origin[0]) && std::signbit(layout.calibration.time_origin));
  layout.source_first_z = std::numeric_limits<int>::max();
  layout.shape.z = 2;
  refuses([&] { layout.contributor({0, 0, 1}, 0); });
  check(observer.counts().plane_reads == 0 && observer.counts().region_reads == 0);
  std::cout << "Dataset Z projection metadata layout passed; no pixel reads\n";
}
