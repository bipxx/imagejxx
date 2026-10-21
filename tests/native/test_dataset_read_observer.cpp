#include <imagejxx/application.hpp>
#include <imagejxx/dataset.hpp>
#include <imagejxx/gaussian_command.hpp>
#include <imagejxx/session.hpp>
#include "dataset_read_counter_internal.hpp" // Production saturation helper only.

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using namespace imagejxx;
using namespace std::chrono_literals;

namespace {
std::size_t checks = 0, groups = 0;
const char *current_group = "before_first_group";
void check(bool condition, const char *message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
template <class Error, class Function>
void rejects(Function function, const char *message) {
  bool rejected = false;
  try { function(); } catch (const Error &) { rejected = true; }
  check(rejected, message);
}
void counts(const DatasetReadObserver &observer, std::uint64_t planes,
            std::uint64_t regions) {
  const auto actual = observer.counts();
  const bool matches = actual.plane_reads == planes &&
                       actual.region_reads == regions && !actual.overflowed;
  std::string diagnostic;
  if (!matches)
    diagnostic = std::string("Quiescent observer counters differ in ") + current_group +
        ": actual plane=" + std::to_string(actual.plane_reads) +
        " region=" + std::to_string(actual.region_reads) +
        " overflowed=" + (actual.overflowed ? "true" : "false") +
        "; expected plane=" + std::to_string(planes) +
        " region=" + std::to_string(regions) + " overflowed=false";
  check(matches, matches ? "Quiescent observer counters differ from actual boundary entries"
                         : diagnostic.c_str());
}
Image analytic(PlanePosition p = {}) {
  Image result(5, 4, 1, PixelType::Float64);
  for (int y = 0; y < 4; ++y)
    for (int x = 0; x < 5; ++x)
      result.at(x, y) = 10000*p.t + 1000*p.c + 100*p.z + 10*y + x;
  return result;
}
Image constant(int width, int height, double value, PixelType type = PixelType::Float64) {
  Image result(width, height, 1, type);
  for (auto &sample : result.pixels) sample = value;
  return result;
}
bool same_image(const Image &a, const Image &b) {
  return a.width == b.width && a.height == b.height && a.channels == b.channels &&
         a.type == b.type && a.pixels.size() == b.pixels.size() &&
         std::memcmp(a.pixels.data(), b.pixels.data(), a.pixels.size()*sizeof(double)) == 0;
}
class Provider final : public PlaneSource {
public:
  DatasetShape shape() const override { return {2, 2, 3, 4, 5, 1}; }
  PixelType pixel_type() const override { return PixelType::Float64; }
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    plane_entries.fetch_add(1, std::memory_order_relaxed);
    if (throw_plane.load(std::memory_order_relaxed)) throw std::runtime_error("provider plane refusal");
    return std::make_shared<const Image>(analytic(p));
  }
  Image read_region(PlanePosition p, int x, int y, int width, int height) const override {
    region_entries.fetch_add(1, std::memory_order_relaxed);
    if (throw_region.load(std::memory_order_relaxed)) throw std::runtime_error("provider region refusal");
    Image result(width, height, 1, PixelType::Float64);
    for (int j = 0; j < height; ++j)
      for (int i = 0; i < width; ++i)
        result.at(i, j) = 10000*p.t + 1000*p.c + 100*p.z + 10*(y+j) + x+i;
    return result;
  }
  mutable std::atomic<std::uint64_t> plane_entries{0}, region_entries{0};
  std::atomic<bool> throw_plane{false}, throw_region{false};
};
class DefaultRegionProvider final : public PlaneSource {
public:
  DatasetShape shape() const override { return {1, 1, 3, 4, 5, 1}; }
  PixelType pixel_type() const override { return PixelType::Float64; }
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    ++plane_entries;
    return std::make_shared<const Image>(analytic(p));
  }
  mutable std::uint64_t plane_entries = 0;
};
SpatialCalibration calibration() {
  SpatialCalibration value;
  value.spacing = {.5, 2, 3}; value.origin = {10, 20, 30};
  value.direction = {0, -1, 0, 1, 0, 0, 0, 0, 1}; value.unit = "um";
  value.frame_interval = .25; value.time_origin = 2;
  return value;
}

void DR01_positive_boundaries_and_metadata() {
  static_assert(std::is_same_v<decltype(DatasetReadCounts{}.plane_reads), std::uint64_t>);
  static_assert(std::is_same_v<decltype(DatasetReadCounts{}.region_reads), std::uint64_t>);
  static_assert(std::is_same_v<decltype(DatasetReadCounts{}.overflowed), bool>);
  DatasetReadObserver observer;
  auto provider = std::make_shared<Provider>();
  Dataset ordinary(provider, calibration());
  auto observed = ordinary.observe_reads(observer);
  check(observed.shape().dimensions() == std::array<int, 6>{2,2,3,4,5,1} &&
        observed.pixel_type() == PixelType::Float64 &&
        observed.sample_policy() == FloatingSamplePolicy::FiniteOnly && !observed.shared_planes(),
        "Observation changed lazy metadata/admission");
  check(observed.shares_pixels(ordinary) && ordinary.shares_pixels(observed),
        "Observation changed pixel-source identity");
  check(observed.calibration().voxel_to_world({3,2,1}) == std::array<double,3>{6,21.5,33} &&
        observed.calibration().time(4) == 3, "Observation changed calibrated coordinates");
  (void)observed.plane_geometry(Plane::XZ, 2);
  counts(observer, 0, 0);
  check(provider->plane_entries == 0 && provider->region_entries == 0,
        "Construction/metadata/identity queries acquired provider pixels");
  check(ordinary.plane({1,1,2})->at(3,2) == 11223,
        "Unobserved positive control did not enter real provider");
  counts(observer, 0, 0);
  check(observed.plane({1,1,2})->at(3,2) == 11223,
        "Observed real plane returned wrong fixture samples");
  counts(observer, 1, 0);
  const auto region = observed.region({1,1,2}, 2, 1, 2, 2);
  check(region.width == 2 && region.height == 2 && region.at(1,1) == 11223,
        "Observed real region returned wrong fixture samples");
  counts(observer, 1, 1);
  check(provider->plane_entries == 2 && provider->region_entries == 1,
        "Provider positive controls differ from exact delegate entry counts");
  auto detached = observer.counts();
  detached.plane_reads = 99; detached.region_reads = 88; detached.overflowed = true;
  counts(observer, 1, 1);
}

void DR02_throw_and_pre_provider_refusal() {
  auto provider = std::make_shared<Provider>();
  DatasetReadObserver observer;
  auto observed = Dataset(provider).observe_reads(observer);
  provider->throw_plane = true;
  rejects<std::runtime_error>([&] { (void)observed.plane({1,1,2}); }, "Provider plane exception vanished");
  provider->throw_region = true;
  rejects<std::runtime_error>([&] { (void)observed.region({}, 1, 1, 2, 2); }, "Provider region exception vanished");
  counts(observer, 1, 1);
  provider->throw_plane = false; provider->throw_region = false;
  for (const auto p : std::array<PlanePosition,6>{{{-1,0,0},{2,0,0},{0,-1,0},{0,2,0},{0,0,-1},{0,0,3}}}) {
    rejects<std::out_of_range>([&] { (void)observed.plane(p); }, "Invalid plane reached provider");
    rejects<std::out_of_range>([&] { (void)observed.region(p, 0, 0, 1, 1); }, "Invalid region TCZ reached provider");
  }
  for (const auto r : std::array<std::array<int,4>,8>{{
      {{-1,0,1,1}},{{0,-1,1,1}},{{5,0,1,1}},{{0,4,1,1}},
      {{0,0,0,1}},{{0,0,1,0}},{{4,0,2,1}},{{0,0,std::numeric_limits<int>::max(),1}}}}) {
    rejects<std::out_of_range>([&] { (void)observed.region({},r[0],r[1],r[2],r[3]); },
                               "Invalid XY extent reached provider");
  }
  counts(observer, 1, 1);
  check(provider->plane_entries == 1 && provider->region_entries == 1,
        "Coordinate refusal was counted as a provider entry");
  (void)observed.plane(); (void)observed.region({}, 0, 0, 1, 1);
  counts(observer, 2, 2);
}

void DR03_default_region_is_one_outer_entry() {
  auto provider = std::make_shared<DefaultRegionProvider>();
  DatasetReadObserver observer;
  auto observed = Dataset(provider).observe_reads(observer);
  check(observed.region({0,0,1}, 1, 2, 2, 1).at(0,0) == 121,
        "Default regional fallback changed actual samples");
  counts(observer, 0, 1);
  check(provider->plane_entries == 1, "Delegate default read_region did not perform its inner plane read");
  check(observed.plane({0,0,2})->at(3,2) == 223, "Direct fallback provider plane changed");
  counts(observer, 1, 1);
  check(provider->plane_entries == 2, "Outer observer substituted for real provider work");
}

void DR04_shared_copies_and_distinct_observers() {
  auto provider = std::make_shared<Provider>();
  Dataset base(provider);
  DatasetReadObserver first, second, independent;
  const auto same_first = first;
  const auto once = base.observe_reads(first);
  const auto twice = once.observe_reads(first).observe_reads(same_first);
  const auto interleaved = twice.observe_reads(second).observe_reads(same_first);
  check(once.shares_pixels(interleaved) && interleaved.shares_pixels(base),
        "Repeated observer identity changed the provider");
  counts(first, 0, 0); counts(second, 0, 0);
  (void)interleaved.plane(); (void)interleaved.region({},0,0,1,1);
  counts(first, 1, 1); counts(same_first, 1, 1); counts(second, 1, 1);
  (void)once.plane();
  counts(first, 2, 1); counts(second, 1, 1);
  (void)base.observe_reads(independent).plane();
  counts(independent, 1, 0); counts(first, 2, 1); counts(second, 1, 1);
  check(provider->plane_entries == 3 && provider->region_entries == 1,
        "Repeated A/B/A observation double-delegated a read");
}

void DR05_counter_and_provider_lifetimes() {
  DatasetReadObserver survivor;
  std::weak_ptr<Provider> lifetime;
  std::optional<Dataset> observed;
  {
    auto provider = std::make_shared<Provider>(); lifetime = provider;
    const auto temporary_observer = survivor;
    observed = Dataset(provider).observe_reads(temporary_observer);
  }
  check(!lifetime.expired(), "Observed Dataset failed to retain its ordinary provider ownership");
  (void)observed->plane(); counts(survivor, 1, 0);
  const auto detached_counts = survivor.counts();
  const auto copied_observer = survivor;
  observed.reset();
  check(lifetime.expired(), "Observer handle retained Dataset/provider/pixel ownership");
  counts(survivor, 1, 0); counts(copied_observer, 1, 0);
  check(detached_counts.plane_reads == 1 && detached_counts.region_reads == 0 && !detached_counts.overflowed,
        "Detached count snapshot depended on provider lifetime");
}

struct Joined {
  std::atomic<bool> &start;
  std::vector<std::thread> &threads;
  ~Joined() {
    start.store(true, std::memory_order_release);
    for (auto &thread : threads) if (thread.joinable()) thread.join();
  }
};
void DR06_joined_concurrent_entries() {
  constexpr std::size_t workers = 4, iterations = 256;
  auto provider = std::make_shared<Provider>();
  DatasetReadObserver observer;
  const auto observed = Dataset(provider).observe_reads(observer);
  std::array<std::exception_ptr, workers> errors{};
  std::atomic<bool> start{false};
  std::vector<std::thread> threads;
  {
    Joined join{start, threads};
    for (std::size_t worker = 0; worker < workers; ++worker)
      threads.emplace_back([&, worker, observed] {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        try {
          for (std::size_t i = 0; i < iterations; ++i) {
            if (observed.plane({1,1,2})->at(3,2) != 11223 ||
                observed.region({1,1,2},2,1,2,2).at(1,1) != 11223)
              throw std::runtime_error("Concurrent provider returned wrong samples");
          }
        } catch (...) { errors[worker] = std::current_exception(); }
      });
    start.store(true, std::memory_order_release);
  }
  for (const auto &error : errors) if (error) std::rethrow_exception(error);
  counts(observer, workers*iterations, workers*iterations);
  check(provider->plane_entries == workers*iterations && provider->region_entries == workers*iterations,
        "Joined provider/observer entry totals disagree");
  // No concurrently read counter pair is asserted to be transactional.
}

void DR07_eager_storage_and_edits() {
  auto base = Dataset::from_stack({constant(4,3,1),constant(4,3,2),constant(4,3,3)},calibration());
  const auto p0 = base.plane(), p1 = base.plane({0,0,1}), p2 = base.plane({0,0,2});
  DatasetReadObserver observer;
  const auto observed = base.observe_reads(observer);
  auto table = observed.shared_planes();
  check(table && table->size() == 3 && table->shared(0) == p0 && table->shared(1) == p1 && table->shared(2) == p2,
        "Observation hid or copied eager shared-plane storage");
  auto updated_calibration = calibration(); updated_calibration.origin[0] += 4;
  auto calibrated = observed.with_calibration(updated_calibration);
  check(calibrated.shares_pixels(observed) && calibrated.calibration().origin[0] == 14,
        "Observed calibration wrapper changed source identity");
  auto changed = calibrated.with_plane({0,0,1},constant(4,3,77));
  auto batch = changed.with_planes({{{0,0,0},constant(4,3,88)},{{0,0,2},constant(4,3,99)}});
  auto empty = observed.with_planes({});
  check(empty.shares_pixels(observed) && changed.shared_planes() && batch.shared_planes(),
        "Observed edit/empty batch degraded eager storage");
  check(changed.shared_planes()->shared(0) == p0 && changed.shared_planes()->shared(2) == p2 &&
        batch.shared_planes()->shared(1) == changed.shared_planes()->shared(1),
        "Observed edits copied unchanged pixel buffers");
  counts(observer, 0, 0);
  check(changed.plane({0,0,1})->at(0,0) == 77 && observed.plane({0,0,1}) == p1 &&
        batch.plane()->at(0,0) == 88 && batch.plane({0,0,2})->at(0,0) == 99,
        "Observed immutable replacements altered the wrong state");
  counts(observer, 4, 0);
  rejects<std::invalid_argument>([&] { (void)observed.with_plane({},Image(3,3)); }, "Bad replacement accepted");
  rejects<std::invalid_argument>([&] { (void)observed.with_planes({{{},constant(4,3,7)},{{},constant(4,3,8)}}); },
                                 "Duplicate replacement positions accepted");
  counts(observer, 4, 0);
  check(p0->at(0,0) == 1 && p1->at(0,0) == 2 && p2->at(0,0) == 3,
        "Observed edits mutated retained original planes");
}

void DR08_virtual_edit_boundaries() {
  auto provider = std::make_shared<Provider>();
  DatasetReadObserver observer;
  auto observed = Dataset(provider,calibration()).observe_reads(observer);
  auto changed = observed.with_plane({0,0,1},constant(5,4,77));
  auto batch = changed.with_planes({{{0,0,0},constant(5,4,88)}});
  check(!changed.shared_planes() && !batch.shared_planes(), "Observed lazy edits materialized eager source tables");
  counts(observer, 0, 0);
  check(provider->plane_entries == 0 && provider->region_entries == 0, "Observed edit preparation read provider data");
  check(batch.plane({0,0,1})->at(0,0) == 77 && batch.plane({0,0,2})->at(3,2) == 223,
        "Observed lazy changed/unchanged planes returned wrong data");
  counts(observer, 2, 0);
  check(provider->plane_entries == 1, "Edited plane unexpectedly delegated to original provider");
  check(batch.region({0,0,0},1,1,2,2).at(1,1) == 88 &&
        batch.region({0,0,2},2,1,2,2).at(1,1) == 223,
        "Observed lazy changed/unchanged regions returned wrong data");
  counts(observer, 2, 2);
  check(provider->region_entries == 1, "Edited region unexpectedly delegated to original provider");
  // Counters measure the new logical observed boundary, not original disk/cache I/O.
}

void DR09_document_history_and_quiet_nan() {
  DatasetReadObserver observer;
  auto base = Dataset::from_stack({constant(4,3,0),constant(4,3,0)});
  Document document(base.observe_reads(observer));
  auto before = document.snapshot();
  auto edit = document.begin_edit();
  counts(observer, 0, 0);
  edit.replace_plane({0,0,1},constant(4,3,5));
  // DocumentTransaction retains the actual immutable replacement pointer by
  // calling replacement.plane(position). Dataset.with_plane itself reads zero;
  // this real document-transaction call enters the new observed boundary once.
  counts(observer, 1, 0);
  document.commit(edit);
  counts(observer, 1, 0);
  const auto history = document.history();
  check(history.undo_steps == 1 && history.redo_steps == 0 && history.pixels.exact &&
        history.pixels.current_bytes == 192 && history.pixels.retained_bytes == 288 &&
        history.pixels.obsolete_bytes == 96 && history.pixels.retained_planes == 3,
        "Observation changed deduplicated eager history accounting");
  check(before.data().dataset.shared_planes() && document.snapshot().data().dataset.shared_planes(),
        "Observed Document history became virtual");
  document.undo(); document.redo();
  counts(observer, 1, 0);
  check(before.data().dataset.plane({0,0,1})->at(0,0) == 0 &&
        document.snapshot().data().dataset.plane({0,0,1})->at(0,0) == 5,
        "Observed Document edit/Undo/Redo lost immutable states");
  counts(observer, 3, 0);
  static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
  const auto floating = [](std::uint32_t bits) {
    float value; std::memcpy(&value,&bits,sizeof(value)); return static_cast<double>(value);
  };
  auto image = constant(3,2,0,PixelType::Float32);
  image.pixels = {floating(0x7fc12345),floating(0xffc23456),-0.0,0.0,-2.5,7.0};
  constexpr auto quiet = FloatingSamplePolicy::PreserveQuietNaNFloat32;
  auto qbase = Dataset::from_stack({image},calibration(),quiet);
  const auto original = qbase.plane();
  const auto expected = *original;
  DatasetReadObserver quiet_observer;
  auto observed = qbase.observe_reads(quiet_observer);
  auto calibrated = observed.with_calibration(calibration());
  auto replaced = calibrated.with_plane({},expected);
  check(observed.sample_policy() == quiet && replaced.sample_policy() == quiet &&
        observed.shared_planes() && replaced.shared_planes() && observed.shares_pixels(qbase),
        "Observer/edit wrapper changed quiet-NaN eager admission");
  counts(quiet_observer, 0, 0);
  check(observed.plane() == original && same_image(*replaced.plane(),expected),
        "Observer wrapper changed canonical NaN payload/sign or signed zero bits");
  counts(quiet_observer, 2, 0);
  Document quiet_document(replaced);
  auto title = quiet_document.begin_edit(); title.set_title("Quiet observed"); quiet_document.commit(title);
  check(quiet_document.history().pixels.exact && quiet_document.history().pixels.current_bytes == 48 &&
        quiet_document.history().pixels.obsolete_bytes == 0,
        "Quiet observed metadata history copied pixels or lost eager accounting");
  quiet_document.undo(); quiet_document.redo();
  counts(quiet_observer, 2, 0);
  check(same_image(*quiet_document.snapshot().data().dataset.plane(),expected),
        "Quiet observed Document history changed payload storage");
  counts(quiet_observer, 3, 0);
}

Image retained_fixture() {
  Image result(7,5,1,PixelType::Float32);
  for (int y = 0; y < 5; ++y)
    for (int x = 0; x < 7; ++x) result.at(x,y) = (37*x+61*y+13*x*y)/8.0-.125;
  return result;
}
void DR10_retained_guard_forwarding_and_detachment() {
  Application app;
  auto source = std::make_shared<Document>(Dataset::from_stack({retained_fixture()}));
  app.adopt(source);
  auto scheduler = app.job_scheduler();
  TargetRequest request; request.roi = RoiPolicy::WholePlane;
  const auto command = resolve_gaussian_command(source->snapshot(),request,{.5,false});
  JobOptions options; options.retain_result_resources = true;
  auto job = app.submit("gaussian_blur_imagej",command.target(),command.parameters(),{},options);
  check(job.wait_for(5s) && job.info().state == JobState::Ready, "Retained observer fixture job did not reach Ready");
  const auto charged = job.info().resource_bytes;
  check(charged > 0, "Retained observer fixture has no real declared charge");
  std::optional<PreparedOperation> prepared = job.prepared();
  const auto expected = *prepared->preview().data().dataset.plane();
  DatasetReadObserver first, second;
  const auto first_alias = first;
  std::optional<Dataset> observed = prepared->preview().data().dataset.observe_reads(first)
      .observe_reads(second).observe_reads(first_alias);
  // Exercise reapplication across both the observer and retained-resource graph.
  observed = observed->with_plane({},expected).with_calibration(calibration());
  std::optional<SharedPlanes> table = observed->shared_planes();
  check(table && table->size() == 1, "Observed retained result lost its eager guarded table");
  counts(first,0,0); counts(second,0,0);
  check(same_image(*observed->plane(),expected), "Observed retained result changed pixels");
  counts(first,1,0); counts(second,1,0);
  auto receiver = std::make_shared<Document>(Dataset::from_stack({constant(7,5,0,PixelType::Float32)}));
  {
    auto edit = receiver->begin_edit(); edit.replace_dataset(*observed); receiver->commit(edit);
  }
  check(receiver->history().pixels.exact && receiver->history().pixels.current_bytes == 280 &&
        receiver->history().pixels.obsolete_bytes == 280,
        "Observer wrapper broke retained-result Document/history detachment accounting");
  counts(first,1,0); counts(second,1,0);
  check(job.cancel(), "Ready retained observer fixture could not be canceled");
  scheduler->shutdown(); // Join the worker before exact quiescent ledger checks.
  prepared.reset();
  auto usage = scheduler->usage();
  check(usage.running_bytes == 0 && usage.retained_bytes == charged && usage.reserved_bytes == charged,
        "Observed payload/table lost or duplicated the single retained charge");
  observed.reset();
  check(scheduler->usage().retained_bytes == charged, "SharedPlanes export lost its retained guard");
  table.reset();
  usage = scheduler->usage();
  check(usage.running_bytes == 0 && usage.retained_bytes == 0 && usage.reserved_bytes == 0,
        "Observer counters or detached published/history storage retained a result lease");
  check(same_image(*receiver->snapshot().data().dataset.plane(),expected),
        "Published detached observed data became invalid after guard release");
  counts(first,2,0); counts(second,2,0);
  receiver->undo(); receiver->redo();
  check(same_image(*receiver->snapshot().data().dataset.plane(),expected) && scheduler->usage().retained_bytes == 0,
        "Undo/Redo reattached a released retained lease or lost observed storage");
  counts(first,3,0); counts(second,3,0);
  check(source->undo_steps() == 0 && !source->snapshot().dirty(),
        "Resource observation fixture published into its unrelated source owner");
}

void DR11_internal_saturation_without_public_seeding() {
  const auto maximum = std::numeric_limits<std::uint64_t>::max();
  std::atomic<std::uint64_t> local_count{maximum-1};
  std::atomic<bool> local_overflow{false};
  detail::increment_saturated_read_count(local_count,local_overflow);
  check(local_count == maximum && !local_overflow, "Exact maximum increment incorrectly reported overflow");
  detail::increment_saturated_read_count(local_count,local_overflow);
  check(local_count == maximum && local_overflow, "Saturating increment wrapped or failed to report overflow");
  detail::increment_saturated_read_count(local_count,local_overflow);
  check(local_count == maximum && local_overflow, "Repeated saturated increment cleared sticky overflow");
  // Production helper/local atomics only: no public observer seed, reset or hook.
}

void DR12_session_transparency_and_ephemeral_counters() {
  Stack planes;
  for (int t = 0; t < 2; ++t)
    for (int z = 0; z < 2; ++z) planes.push_back(analytic({t,0,z}));
  const DatasetShape finite_shape{2,1,2,4,5,1};
  const auto finite = Dataset::from_planes(finite_shape,std::move(planes),calibration());
  auto quiet_image = constant(3,2,0,PixelType::Float32);
  const auto floating = [](std::uint32_t bits) {
    float value; std::memcpy(&value,&bits,sizeof(value)); return static_cast<double>(value);
  };
  quiet_image.pixels = {floating(0x7fc12345),floating(0xffc23456),-0.0,0.0,-2.5,7.0};
  constexpr auto quiet_policy = FloatingSamplePolicy::PreserveQuietNaNFloat32;
  const auto quiet = Dataset::from_stack(
      {quiet_image,constant(3,2,4,PixelType::Float32)},calibration(),quiet_policy);
  // Capture the owned canonical backing before adding an observer. This is a
  // storage-transparency assertion, not a new numerical float-conversion oracle.
  const auto quiet_untargeted = *quiet.plane();
  DatasetReadObserver finite_observer, quiet_observer;
  const auto ordinary_finite = finite.with_plane({1,0,1},constant(5,4,66));
  const auto ordinary_quiet = quiet.with_plane({0,0,1},constant(3,2,9,PixelType::Float32));
  const auto observed_finite = finite.observe_reads(finite_observer)
      .with_plane({1,0,1},constant(5,4,66));
  const auto observed_quiet = quiet.observe_reads(quiet_observer)
      .with_plane({0,0,1},constant(3,2,9,PixelType::Float32));
  Document plain_finite(ordinary_finite,"finite observed session");
  Document plain_quiet(ordinary_quiet,"quiet observed session");
  Document watched_finite(observed_finite,"finite observed session");
  Document watched_quiet(observed_quiet,"quiet observed session");
  (void)observed_finite.plane();
  (void)observed_quiet.region({},0,0,1,1);
  counts(finite_observer,1,0); counts(quiet_observer,0,1);
  const SessionCapture plain_capture{{plain_finite.snapshot(),plain_quiet.snapshot()}, {}, {}, {}};
  const SessionCapture watched_capture{{watched_finite.snapshot(),watched_quiet.snapshot()}, {}, {}, {}};
  SessionLimits limits;
  limits.max_documents = 2; limits.max_planes = 6; limits.max_samples = 92;
  limits.max_json_bytes = limits.max_encoded_bytes = 1024*1024;
  limits.max_working_bytes = 64ULL*1024*1024;
  limits.max_nodes = 4096; limits.max_depth = 32;
  const auto estimate = estimate_session(watched_capture,limits);
  check(estimate.documents == 2 && estimate.planes == 6 && estimate.samples == 92 &&
        estimate.source_bytes == 736,
        "Observed eager source failed session admission or changed explicit sample axes");
  counts(finite_observer,1,0); counts(quiet_observer,0,1);
  const auto ordinary_bytes = encode_session(plain_capture,limits);
  const auto observed_bytes = encode_session(watched_capture,limits);
  check(!observed_bytes.empty() && observed_bytes == ordinary_bytes,
        "Observer/counter state entered session bytes or changed native eager serialization");
  // This codec reads the public shared table, rather than entering the observed
  // provider boundary. No claim is made about zero I/O or codec pixel work.
  counts(finite_observer,1,0); counts(quiet_observer,0,1);
  const auto decoded = decode_session(observed_bytes,limits);
  check(decoded.source_version == 3 && decoded.documents.size() == 2,
        "Observed session roundtrip lost an owner or changed wire version");
  const auto restored_finite = decoded.documents[0]->snapshot();
  const auto restored_quiet = decoded.documents[1]->snapshot();
  check(restored_finite.data().dataset.shape().dimensions() == finite_shape.dimensions() &&
        restored_quiet.data().dataset.shape().dimensions() == std::array<int,6>{1,1,2,2,3,1} &&
        restored_finite.data().dataset.pixel_type() == PixelType::Float64 &&
        restored_quiet.data().dataset.pixel_type() == PixelType::Float32 &&
        restored_finite.data().dataset.sample_policy() == FloatingSamplePolicy::FiniteOnly &&
        restored_quiet.data().dataset.sample_policy() == quiet_policy &&
        restored_finite.data().dataset.shared_planes() && restored_quiet.data().dataset.shared_planes(),
        "Session flattened observed eager axes/type/policy/storage");
  for (const auto &owner : decoded.documents) {
    const auto snapshot = owner->snapshot();
    const auto &value = snapshot.data().dataset.calibration();
    const auto expected = calibration();
    check(value.spacing == expected.spacing && value.origin == expected.origin &&
          value.direction == expected.direction && value.unit == expected.unit &&
          value.time_unit == expected.time_unit && value.frame_interval == expected.frame_interval &&
          value.time_origin == expected.time_origin,
          "Observed session lost spatial/temporal calibration");
    check(!snapshot.dirty() && owner->undo_steps() == 0 && owner->redo_steps() == 0 &&
          snapshot.revisions().pixels == 0 && snapshot.revisions().metadata == 0 &&
          snapshot.revisions().selection == 0 && snapshot.revisions().display == 0,
          "Session restored observation as dirty edits/history/revisions");
  }
  check(!restored_finite.same_document(watched_finite.snapshot()) &&
        !restored_quiet.same_document(watched_quiet.snapshot()),
        "Session reused captured owner identity");
  for (int t = 0; t < 2; ++t) for (int z = 0; z < 2; ++z) {
    const auto expected = t == 1 && z == 1 ? constant(5,4,66) : analytic({t,0,z});
    check(same_image(*restored_finite.data().dataset.plane({t,0,z}),expected),
          "Session changed observed edited or untargeted scientific planes");
  }
  check(same_image(*restored_quiet.data().dataset.plane(),quiet_untargeted) &&
        same_image(*restored_quiet.data().dataset.plane({0,0,1}),constant(3,2,9,PixelType::Float32)),
        "Session changed untargeted quiet-NaN payload/sign/signed-zero or edited plane bits");
  // Reads of freshly decoded owners cannot update the source observer. The
  // restored Dataset opts in only when an independent fresh observer is added.
  DatasetReadObserver decoded_observer;
  const auto decoded_watched = restored_quiet.data().dataset.observe_reads(decoded_observer);
  counts(decoded_observer,0,0);
  check(same_image(*decoded_watched.plane(),quiet_untargeted),
        "Freshly observed restored Dataset lost its eager payload");
  counts(decoded_observer,1,0);
  counts(finite_observer,1,0); counts(quiet_observer,0,1);
  check(watched_finite.undo_steps() == 0 && watched_quiet.undo_steps() == 0 &&
        !watched_finite.snapshot().dirty() && !watched_quiet.snapshot().dirty(),
        "Session codec mutated a captured live owner");
}

template <class Function> void run(const char *name, Function function) {
  current_group = name;
  function(); ++groups; std::cout << name << '\n';
}
} // namespace

int main() {
  try {
    run("DR01.positive_boundaries_and_metadata",DR01_positive_boundaries_and_metadata);
    run("DR02.throw_and_pre_provider_refusal",DR02_throw_and_pre_provider_refusal);
    run("DR03.default_region_is_one_outer_entry",DR03_default_region_is_one_outer_entry);
    run("DR04.shared_copies_and_distinct_observers",DR04_shared_copies_and_distinct_observers);
    run("DR05.counter_and_provider_lifetimes",DR05_counter_and_provider_lifetimes);
    run("DR06.joined_concurrent_entries",DR06_joined_concurrent_entries);
    run("DR07.eager_storage_and_edits",DR07_eager_storage_and_edits);
    run("DR08.virtual_edit_boundaries",DR08_virtual_edit_boundaries);
    run("DR09.document_history_and_quiet_nan",DR09_document_history_and_quiet_nan);
    run("DR10.retained_guard_forwarding_and_detachment",DR10_retained_guard_forwarding_and_detachment);
    run("DR11.internal_saturation_without_public_seeding",DR11_internal_saturation_without_public_seeding);
    run("DR12.session_transparency_and_ephemeral_counters",DR12_session_transparency_and_ephemeral_counters);
    std::cout << "Dataset read observer: " << groups << " groups / " << checks << " checks\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Dataset read observer failure: " << error.what() << '\n';
    return 1;
  }
}
