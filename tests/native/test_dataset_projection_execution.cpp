#include <imagejxx/dataset_projection_execution.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace imagejxx;
namespace {
void check(bool value) {
  if (!value) throw std::runtime_error("Scientific projection execution assertion failed");
}
template<class Error, class F> void refuses(F function) {
  bool refused = false;
  try { function(); } catch (const Error &) { refused = true; }
  check(refused);
}
class Source final : public PlaneSource {
public:
  DatasetShape dimensions{3, 2, 9, 5, 7, 3};
  mutable std::vector<PlanePosition> reads;
  mutable std::vector<std::weak_ptr<const Image>> leases;
  mutable std::size_t peak_live = 0;
  int fail_at = -1;
  bool bad_sample = false;
  bool mutate_type = false;
  mutable PixelType advertised_type = PixelType::Float32;
  DatasetShape shape() const override { return dimensions; }
  PixelType pixel_type() const override { return advertised_type; }
  std::size_t live() const {
    std::size_t count = 0;
    for (const auto &lease : leases) count += !lease.expired();
    return count;
  }
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    reads.push_back(p);
    if (int(reads.size()) == fail_at) throw std::runtime_error("Owned provider failure");
    auto plane = std::make_shared<Image>(dimensions.x, dimensions.y, dimensions.s, PixelType::Float32);
    for (int y = 0; y < dimensions.y; ++y) for (int x = 0; x < dimensions.x; ++x)
      for (int s = 0; s < dimensions.s; ++s)
        plane->at(x, y, s) = 10000*p.t + 1000*p.c + 100*y + 10*x + s*.25 + p.z*p.z;
    if (bad_sample) plane->pixels[0] = std::numeric_limits<double>::quiet_NaN();
    if (mutate_type) plane->type = advertised_type = PixelType::Float64;
    leases.push_back(plane);
    peak_live = std::max(peak_live, live());
    return plane;
  }
};
}

int main() {
  DatasetZProjectionRequest request;
  request.first = 2; request.last = 7; request.group_size = 3;
  request.active_t = 2; request.active_c = 1;
  SpatialCalibration calibration;
  calibration.spacing = {.5, 2, 3}; calibration.origin = {10, -4, 8};
  calibration.frame_interval = 2.5; calibration.time_origin = -3;
  for (auto method : {"maximum", "minimum", "mean", "sum", "median", "stddev"}) {
    for (bool all_t : {false, true}) for (bool all_c : {false, true}) {
      auto provider = std::make_shared<Source>();
      Dataset source(provider, calibration);
      DatasetReadObserver observer;
      request.method = method; request.all_time_frames = all_t; request.all_channels = all_c;
      auto result = project_dataset_z(source.observe_reads(observer), request);
      check(result.shape().dimensions() == std::array<int, 6>{all_t ? 3 : 1, all_c ? 2 : 1, 2, 5, 7, 3});
      check(result.pixel_type() == ((request.method == "maximum" || request.method == "minimum")
          ? PixelType::Float32 : PixelType::Float64));
      check(result.calibration().origin == std::array<double, 3>{10, -4, 14});
      check(result.calibration().spacing[2] == 9);
      check(result.calibration().time_origin == (all_t ? -3 : 2));
      check(result.calibration().frame_interval == 2.5);
      for (int t = 0; t < result.shape().t; ++t) for (int c = 0; c < result.shape().c; ++c)
        for (int z = 0; z < 2; ++z) {
          auto plane = result.plane({t, c, z});
          for (int y = 0; y < 5; ++y) for (int x = 0; x < 7; ++x) for (int s = 0; s < 3; ++s) {
            double base = 10000*(all_t ? t : 2) + 1000*(all_c ? c : 1) + 100*y + 10*x + s*.25;
            double expected;
            if (request.method == "maximum") expected = base + (z ? 49 : 16);
            else if (request.method == "minimum") expected = base + (z ? 25 : 4);
            else if (request.method == "median") expected = base + (z ? 36 : 9);
            else if (request.method == "sum") expected = 3*base + (z ? 110 : 29);
            else if (request.method == "mean") expected = base + (z ? 110.0 : 29.0)/3;
            else expected = std::sqrt((z ? 866.0 : 218.0)/9);
            check(std::abs(plane->at(x, y, s) - expected) < 1e-8);
          }
        }
      check(observer.counts().plane_reads == std::uint64_t(result.shape().t * result.shape().c * 6));
      check(observer.counts().region_reads == 0 && provider->peak_live <= 3 && provider->live() == 0);
      for (std::size_t i = 0; i < provider->reads.size(); ++i) {
        const auto p = provider->reads[i];
        check(p.t == (all_t ? int(i / (result.shape().c*6)) : 2));
        check(p.c == (all_c ? int(i/6) % 2 : 1) && p.z == 2 + int(i%6));
      }
      check(source.calibration().origin == calibration.origin && source.shape().z == 9);
      check(!result.shares_pixels(source));
    }
  }
  for (auto type : {PixelType::UInt8, PixelType::UInt16, PixelType::Float32, PixelType::Float64})
    for (int samples : {1, 3, 4})
      for (auto method : {"maximum", "minimum", "mean", "sum", "median", "stddev"}) {
        Image low(2, 1, samples, type), high(2, 1, samples, type);
        for (std::size_t i = 0; i < low.pixels.size(); ++i) {
          low.pixels[i] = 10*double(i) + 1; high.pixels[i] = 10*double(i) + 3;
        }
        auto input = Dataset::from_planes({1, 1, 2, 1, 2, samples}, {low, high});
        DatasetZProjectionRequest pair; pair.last = 1; pair.method = method;
        int repeated_input_scans = 0;
        ExecutionControl admitted;
        admitted.stage_progress = [&](std::string_view name, std::size_t, std::size_t) {
          if (name == "Projection source samples") ++repeated_input_scans;
        };
        auto reduced = project_dataset_z(input, pair, admitted);
        check(repeated_input_scans == 0);
        check(reduced.shape().s == samples && reduced.shape().c == 1 && reduced.shape().z == 1);
        check(reduced.pixel_type() == ((pair.method == "maximum" || pair.method == "minimum") ? type : PixelType::Float64));
        auto pixels = reduced.plane();
        for (std::size_t i = 0; i < low.pixels.size(); ++i) {
          double expected = pair.method == "maximum" ? 10*double(i)+3 :
              pair.method == "minimum" ? 10*double(i)+1 : pair.method == "sum" ? 20*double(i)+4 :
              pair.method == "stddev" ? 1 : 10*double(i)+2;
          check(pixels->pixels[i] == expected);
        }
        check(input.plane()->pixels == low.pixels && input.plane({0, 0, 1})->pixels == high.pixels);
      }
  auto provider = std::make_shared<Source>(); Dataset source(provider);
  request.all_time_frames = request.all_channels = true;
  request.method = "mean";
  auto layout = describe_dataset_z_projection(source, request);
  auto bounded = request; bounded.max_working_bytes = layout.working_bytes - 1;
  int callbacks = 0; ExecutionControl control; control.checkpoint = [&] { ++callbacks; };
  refuses<std::invalid_argument>([&] { project_dataset_z(source, bounded, control); });
  check(provider->reads.empty() && callbacks == 0);
  bounded.max_working_bytes = layout.working_bytes;
  std::vector<std::size_t> progress;
  control.progress = [&](std::size_t done, std::size_t total) {
    check(total == 12); progress.push_back(done);
  };
  auto output = project_dataset_z(source, bounded, control);
  check(output.shape().z == 2);
  check(progress.size() == 13);
  for (std::size_t i = 0; i < progress.size(); ++i) check(progress[i] == i);
  request.group_size = 2; request.method = "median";
  output = project_dataset_z(source, request);
  check(output.shape().z == 3 && output.plane({0, 0, 0})->pixels[0] == 6.5);
  request.group_size = 0; request.method = "sum";
  output = project_dataset_z(source, request);
  check(output.shape().z == 1 && output.plane()->pixels[0] == 139);
  check(output.calibration().spacing[2] == 1);
  request.group_size = 3;
  for (bool bad : {false, true}) {
    auto broken = std::make_shared<Source>(); broken->fail_at = bad ? -1 : 4; broken->bad_sample = bad;
    Dataset dataset(broken);
    if (bad) refuses<std::invalid_argument>([&] { project_dataset_z(dataset, request); });
    else refuses<std::runtime_error>([&] { project_dataset_z(dataset, request); });
    check(broken->live() == 0 && broken->peak_live <= 3);
  }
  for (auto policy : {FloatingSamplePolicy::PreserveQuietNaNFloat32, FloatingSamplePolicy::PreserveIEEEFloat32}) {
    Image plane(1, 1, 1, PixelType::Float32); plane.pixels[0] = 1;
    Dataset dataset = Dataset::from_planes({1, 1, 1, 1, 1, 1}, {plane}, {}, policy);
    DatasetReadObserver observer;
    refuses<std::invalid_argument>([&] { project_dataset_z(dataset.observe_reads(observer), {}); });
    check(observer.counts().plane_reads == 0);
  }
  auto unstable = std::make_shared<Source>(); unstable->mutate_type = true;
  refuses<std::invalid_argument>([&] { project_dataset_z(Dataset(unstable), request); });
  check(unstable->reads.size() == 1 && unstable->live() == 0);
  auto intent = request; intent.method = "minimum";
  ExecutionControl mutate_intent;
  mutate_intent.checkpoint = [&] { intent.method = "sum"; };
  auto captured = project_dataset_z(source, intent, mutate_intent);
  check(captured.pixel_type() == PixelType::Float32 && captured.plane()->pixels[0] == 4);
  auto canceled = std::make_shared<CancellationToken>(); canceled->cancel();
  control = {}; control.cancellation = canceled;
  auto unread = std::make_shared<Source>(); Dataset unread_source(unread);
  refuses<OperationCancelled>([&] { project_dataset_z(unread_source, request, control); });
  check(unread->reads.empty());
  auto large = std::make_shared<Source>(); large->dimensions = {1, 1, 3, 33, 35, 1};
  canceled = std::make_shared<CancellationToken>();
  control = {}; control.cancellation = canceled;
  control.stage_progress = [&](std::string_view name, std::size_t done, std::size_t) {
    if (name == "Z projection" && done == 1024) canceled->cancel();
  };
  DatasetZProjectionRequest whole; whole.last = 2;
  refuses<OperationCancelled>([&] { project_dataset_z(Dataset(large), whole, control); });
  check(large->reads.size() == 3 && large->live() == 0);
  auto progress_cancel = std::make_shared<Source>();
  canceled = std::make_shared<CancellationToken>();
  control = {}; control.cancellation = canceled;
  control.progress = [&](std::size_t done, std::size_t) {
    if (done == 1) canceled->cancel();
  };
  refuses<OperationCancelled>([&] { project_dataset_z(Dataset(progress_cancel), request, control); });
  check(progress_cancel->reads.size() == 3 && progress_cancel->live() == 0);
  struct CallbackFailure : std::runtime_error { CallbackFailure() : std::runtime_error("callback failure") {} };
  auto interrupted = std::make_shared<Source>();
  control = {};
  control.stage_progress = [](std::string_view name, std::size_t done, std::size_t) {
    if (name == "Scientific Z projection" && done == 1) throw CallbackFailure();
  };
  refuses<CallbackFailure>([&] { project_dataset_z(Dataset(interrupted), request, control); });
  check(interrupted->reads.size() == 3 && interrupted->live() == 0);
  auto times = std::make_shared<Source>(); times->dimensions.z = 1;
  auto time_series = project_dataset_z(Dataset(times), {});
  check(time_series.shape().t == 3 && time_series.shape().c == 2 && time_series.shape().z == 1);
  check(time_series.plane({2, 1, 0})->pixels[0] == 21000);
  Image huge(1, 1); huge.pixels[0] = std::numeric_limits<double>::max();
  auto finite = Dataset::from_stack({huge, huge});
  whole.last = 1; whole.method = "sum";
  refuses<std::invalid_argument>([&] { project_dataset_z(finite, whole); });
  check(finite.plane()->pixels[0] == std::numeric_limits<double>::max());
  std::cout << "Scientific projection finite profile, leases, admission and cancellation passed\n";
}
