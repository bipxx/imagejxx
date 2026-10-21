#include <imagejxx/application.hpp>
#include <imagejxx/dataset.hpp>
#include <imagejxx/jobs.hpp>
#include <imagejxx/operations.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using namespace imagejxx;
using namespace std::chrono_literals;
constexpr auto opted = FloatingSamplePolicy::PreserveQuietNaNFloat32;
constexpr auto finite = FloatingSamplePolicy::FiniteOnly;
static_assert(sizeof(float) == 4 && sizeof(double) == 8,
              "This consumer requires IEEE binary32/binary64 storage");
static_assert(std::numeric_limits<float>::is_iec559 &&
                  std::numeric_limits<double>::is_iec559,
              "This consumer requires IEEE floating samples");

constexpr std::array<std::uint32_t, 6> finite_bits{
    0x00000000u, 0x80000000u, 0x3f800000u,
    0xc0200000u, 0x00000001u, 0x40400000u};
constexpr std::array<std::uint64_t, 6> finite_double_bits{
    0x0000000000000000ULL, 0x8000000000000000ULL, 0x3ff0000000000000ULL,
    0xc004000000000000ULL, 0x36a0000000000000ULL, 0x4008000000000000ULL};
constexpr std::array<std::uint32_t, 6> quiet_bits{
    0x00000000u, 0x80000000u, 0x3f800000u,
    0xc0200000u, 0x00000001u, 0x7fc00001u};
constexpr std::array<std::uint64_t, 6> quiet_double_bits{
    0x0000000000000000ULL, 0x8000000000000000ULL, 0x3ff0000000000000ULL,
    0xc004000000000000ULL, 0x36a0000000000000ULL, 0x7ff8000020000000ULL};
constexpr std::array<std::uint32_t, 6> added_bits{
    0x3f800000u, 0x3f800000u, 0x40000000u,
    0xbfc00000u, 0x3f800000u, 0x40800000u};
constexpr std::array<std::uint64_t, 6> added_double_bits{
    0x3ff0000000000000ULL, 0x3ff0000000000000ULL, 0x4000000000000000ULL,
    0xbff8000000000000ULL, 0x3ff0000000000000ULL, 0x4010000000000000ULL};

void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
template <class Function> void rejects(Function function, const char *message) {
  bool rejected = false;
  try {
    function();
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  require(rejected, message);
}
double represented(std::uint64_t bits) {
  double value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}
std::uint64_t stored_bits(double value) {
  std::uint64_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}
Image literal_plane(bool quiet = true) {
  Image image(3, 2, 1, PixelType::Float32);
  const auto &bits = quiet ? quiet_double_bits : finite_double_bits;
  for (std::size_t i = 0; i < bits.size(); ++i)
    image.pixels[i] = represented(bits[i]);
  return image;
}
void exact_plane(const Image &image,
                 const std::array<std::uint32_t, 6> &expected32,
                 const std::array<std::uint64_t, 6> &expected64) {
  require(image.width == 3 && image.height == 2 && image.channels == 1 &&
              image.type == PixelType::Float32 && image.pixels.size() == 6,
          "Typed source shape/type changed");
  for (std::size_t i = 0; i < expected32.size(); ++i) {
    require(stored_bits(image.pixels[i]) == expected64[i],
            "Actual immutable double storage changed IEEE sample bits");
    require(float32_source_sample_bits(image.pixels[i], opted) == expected32[i],
            "Actual Float32 source bit export changed a payload or signed zero");
  }
}
void exact_quiet(const Image &image) {
  exact_plane(image, quiet_bits, quiet_double_bits);
}
void exact_finite(const Image &image) {
  exact_plane(image, finite_bits, finite_double_bits);
}
void exact_added(const Image &image) {
  exact_plane(image, added_bits, added_double_bits);
}
Dataset owned_stack() {
  return Dataset::from_stack({literal_plane(), literal_plane(false)}, {}, opted);
}

void exact_ieee_transport() {
  constexpr std::array<std::pair<std::uint32_t, std::uint64_t>, 10> cases{{
      {0x00000000u, 0x0000000000000000ULL},
      {0x80000000u, 0x8000000000000000ULL},
      {0x3f800000u, 0x3ff0000000000000ULL},
      {0xc0200000u, 0xc004000000000000ULL},
      {0x00000001u, 0x36a0000000000000ULL},
      {0x00800000u, 0x3810000000000000ULL},
      {0x7f7fffffu, 0x47efffffe0000000ULL},
      {0xff7fffffu, 0xc7efffffe0000000ULL},
      {0x7fc00001u, 0x7ff8000020000000ULL},
      {0xffc12345u, 0xfff82468a0000000ULL},
  }};
  for (const auto &item : cases) {
    require(stored_bits(float32_source_sample(item.first, opted)) == item.second,
            "Public Float32 decoder disagrees with literal IEEE double encoding");
    require(float32_source_sample_bits(represented(item.second), opted) == item.first,
            "Public represented-sample encoder disagrees with literal Float32 bits");
  }
}

void owned_gray_source() {
  auto caller = literal_plane();
  const auto canonical = canonical_float32_source(caller, opted);
  exact_quiet(canonical);
  exact_quiet(caller);
  auto source = Dataset::from_planes({1, 1, 1, 2, 3, 1}, {caller}, {}, opted);
  require(source.sample_policy() == opted &&
              source.pixel_type() == PixelType::Float32,
          "Owned grayscale source did not retain explicit admission policy");
  for (auto &sample : caller.pixels)
    sample = 99;
  exact_quiet(*source.plane());
  auto copied = *source.plane();
  copied.pixels.back() = 77;
  exact_quiet(*source.plane());
}

void finite_default_compatibility() {
  const auto source = Dataset::from_stack({literal_plane(false)});
  require(source.sample_policy() == finite &&
              SharedPlanes{}.sample_policy() == finite,
          "Public source admission default changed");
  exact_finite(*source.plane());
  exact_finite(canonical_float32_source(literal_plane(false)));
  require(stored_bits(float32_source_sample(0x80000000u)) ==
              0x8000000000000000ULL &&
              float32_source_sample_bits(-0.0) == 0x80000000u,
          "Finite default transport lost signed zero");
  // The legacy default still retains finite double-backed intermediates; the
  // explicit typed canonicalizer is a separate API and may round them.
  auto intermediate = literal_plane(false);
  intermediate.pixels[2] = 0.1;
  const auto legacy = Dataset::from_stack({intermediate});
  require(stored_bits(legacy.plane()->pixels[2]) == stored_bits(0.1),
          "Default source silently canonicalized a legacy finite intermediate");
  rejects([] { (void)Dataset::from_stack({literal_plane()}); },
          "Default eager source admitted a quiet NaN");
  rejects([] { (void)canonical_float32_source(literal_plane()); },
          "Default typed canonicalization admitted a quiet NaN");
}

void nonquiet_and_lossy_rejection() {
  for (auto bits : {0x7f800000u, 0xff800000u, 0x7f800001u, 0xff800001u}) {
    rejects([&] { (void)float32_source_sample(bits, opted); },
            "Opted Float32 decoder admitted an infinity or signaling NaN");
    rejects([&] { (void)float32_source_sample(bits); },
            "Default decoder admitted an infinity or signaling NaN");
  }
  for (auto bits : {0x7ff0000000000000ULL, 0xfff0000000000000ULL,
                   0x7ff0000020000000ULL, 0xfff0000020000000ULL,
                   0x7ff8000020000001ULL}) {
    rejects([&] { (void)float32_source_sample_bits(represented(bits), opted); },
            "Encoder admitted nonquiet or nonrepresentable NaN payload bits");
    auto invalid = literal_plane(false);
    invalid.pixels.back() = represented(bits);
    const auto before = stored_bits(invalid.pixels.back());
    rejects([&] { (void)Dataset::from_stack({invalid}, {}, opted); },
            "Owned opted storage admitted invalid represented sample bits");
    require(stored_bits(invalid.pixels.back()) == before,
            "Failed sample admission changed caller storage");
  }
  rejects([] { (void)float32_source_sample(0x7fc00001u); },
          "Default decoder admitted a quiet NaN payload");
  rejects([] { (void)float32_source_sample_bits(represented(0x7ff8000020000000ULL)); },
          "Default encoder admitted a quiet NaN payload");
  rejects([] { (void)float32_source_sample_bits(0.1, opted); },
          "Exact source encoder silently rounded an unrepresented finite sample");
}

void declared_storage_and_policy_rejection() {
  for (auto type : {PixelType::UInt8, PixelType::UInt16, PixelType::Float64}) {
    Image image(3, 2, 1, type);
    rejects([&] { (void)Dataset::from_stack({image}, {}, opted); },
            "Quiet-NaN policy admitted a different declared dtype");
    rejects([&] { (void)canonical_float32_source(image, opted); },
            "Typed canonicalizer admitted a different declared dtype");
  }
  for (int samples : {3, 4}) {
    Image color(3, 2, samples, PixelType::Float32);
    rejects([&] { (void)Dataset::from_stack({color}, {}, opted); },
            "Quiet-NaN policy admitted interleaved color samples");
  }
  auto overflow = literal_plane(false);
  overflow.pixels[2] = 1e40;
  rejects([&] { (void)Dataset::from_stack({overflow}, {}, opted); },
          "Opted Float32 storage admitted a finite-to-Float32 overflow");
  rejects([&] { (void)Dataset::from_stack({overflow}); },
          "Default Float32 storage admitted a finite-to-Float32 overflow");
  const auto unknown = static_cast<FloatingSamplePolicy>(73);
  rejects([&] { (void)Dataset::from_stack({literal_plane(false)}, {}, unknown); },
          "Source factory accepted an unknown admission policy");
  rejects([&] { (void)canonical_float32_source(literal_plane(false), unknown); },
          "Typed canonicalizer accepted an unknown admission policy");
  rejects([&] { (void)float32_source_sample(0, unknown); },
          "Decoder accepted an unknown admission policy");
  rejects([&] { (void)float32_source_sample_bits(0, unknown); },
          "Encoder accepted an unknown admission policy");
}

class CountedVirtual final : public PlaneSource {
public:
  mutable std::size_t reads = 0;
  DatasetShape shape() const override { return {1, 1, 1, 2, 3, 1}; }
  PixelType pixel_type() const override { return PixelType::Float32; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++reads;
    return std::make_shared<const Image>(literal_plane(false));
  }
};
void virtual_preflight_without_reads() {
  auto provider = std::make_shared<CountedVirtual>();
  rejects([&] { (void)Dataset(provider, {}, opted); },
          "Unreviewed virtual provider was given quiet-NaN source admission");
  require(provider->reads == 0, "Opt-in policy rejection read virtual pixels");
  const Dataset source(provider);
  require(source.sample_policy() == finite && !source.shared_planes() &&
              provider->reads == 0,
          "Legacy virtual metadata path changed");
  exact_finite(*source.plane());
  require(provider->reads == 1, "Ordinary virtual plane read changed");
}

void table_subsets_and_cross_policy() {
  SharedPlanes table({literal_plane(), literal_plane(false)}, opted);
  auto copy = table;
  auto reordered = table.subset({1, 0, 1});
  require(copy.sample_policy() == opted && reordered.sample_policy() == opted &&
              reordered.size() == 3 &&
              reordered.shared(1).get() == table.shared(0).get(),
          "Subset lost immutable aliases or source policy");
  const auto imported = Dataset::from_shared_planes({1, 1, 3, 2, 3, 1}, reordered);
  require(imported.sample_policy() == opted,
          "Public shared-table import silently changed source policy");
  exact_finite(*imported.plane({0, 0, 0}));
  exact_quiet(*imported.plane({0, 0, 1}));
  exact_finite(*imported.plane({0, 0, 2}));
  table.replace(0, literal_plane(false));
  exact_quiet(copy[0]);
  exact_quiet(*imported.plane({0, 0, 1}));
  auto materialized = copy.materialize();
  materialized.front().pixels.back() = 17;
  exact_quiet(copy[0]);
  SharedPlanes default_table({literal_plane(false)});
  const auto original_alias = default_table.shared(0);
  rejects([&] { default_table.replace_shared(0, copy.shared(0)); },
          "Shared replacement bypassed finite-only destination admission");
  rejects([&] { default_table.insert_shared(1, copy, 0); },
          "Shared insertion bypassed finite-only destination admission");
  require(default_table.size() == 1 &&
              default_table.shared(0).get() == original_alias.get(),
          "Rejected shared import changed table contents");
}

void immutable_edits_and_calibration() {
  const auto source = owned_stack();
  const auto z0 = source.plane();
  const auto changed = source.with_plane({0, 0, 1}, literal_plane());
  require(changed.sample_policy() == opted && changed.plane().get() == z0.get(),
          "Immutable replacement changed policy or untouched plane alias");
  exact_quiet(*changed.plane({0, 0, 1}));
  exact_finite(*source.plane({0, 0, 1}));
  auto calibration = source.calibration();
  calibration.spacing = {.5, 2, 3};
  calibration.origin = {11, -7, 19};
  calibration.unit = "um";
  const auto calibrated = changed.with_calibration(calibration);
  require(calibrated.sample_policy() == opted && calibrated.shares_pixels(changed) &&
              calibrated.calibration().spacing == calibration.spacing &&
              calibrated.calibration().origin == calibration.origin &&
              calibrated.calibration().unit == "um",
          "Calibration replacement lost source policy or shared pixel identity");
  exact_quiet(*calibrated.plane());
  const auto region = calibrated.region({0, 0, 0}, 2, 1, 1, 1);
  require(region.width == 1 && region.height == 1 &&
              stored_bits(region.pixels[0]) == 0x7ff8000020000000ULL,
          "Region extraction changed actual quiet-NaN payload bits");
  const auto xz = calibrated.extract_plane(Plane::XZ, 1);
  const auto yz = calibrated.extract_plane(Plane::YZ, 2);
  require(xz.width == 3 && xz.height == 2 && yz.width == 2 && yz.height == 2 &&
              stored_bits(xz.at(2, 0)) == 0x7ff8000020000000ULL &&
              stored_bits(xz.at(2, 1)) == 0x7ff8000020000000ULL &&
              stored_bits(yz.at(1, 0)) == 0x7ff8000020000000ULL &&
              stored_bits(yz.at(1, 1)) == 0x7ff8000020000000ULL,
          "Orthogonal extraction changed actual selected payload samples");
  auto invalid = literal_plane(false);
  invalid.pixels.back() = represented(0x7ff0000020000000ULL);
  rejects([&] { (void)source.with_plane({0, 0, 1}, invalid); },
          "Immutable replacement admitted signaling NaN storage");
  exact_quiet(*source.plane());
  exact_finite(*source.plane({0, 0, 1}));
}

void gaussian_guards_and_finite_z1() {
  const auto source = owned_stack();
  rejects([&] { (void)canonical_image(*source.plane()); },
          "Generic finite canonicalization admitted an opted quiet NaN");
  rejects([&] { (void)execute_operation("gaussian_blur", *source.plane()); },
          "Public generic Gaussian admitted quiet-NaN image storage");
  rejects([&] { (void)execute_operation("gaussian_blur_imagej", *source.plane()); },
          "Public ImageJ Gaussian admitted quiet-NaN image storage");
  rejects([&] {
    (void)execute_operation("gaussian_blur_imagej", *source.plane(),
                            {{"sigma", NumericArray{{2}, {0, 0}}}});
  }, "Sigma-zero ImageJ Gaussian bypassed the finite image guard");
  const auto output = execute_operation("add", *source.plane({0, 0, 1}),
                                        {{"value", 1.0}}).image;
  exact_added(output);
  const auto changed = source.with_plane({0, 0, 1}, output);
  require(changed.sample_policy() == opted,
          "Finite-plane processing replacement lost source policy");
  exact_quiet(*changed.plane());
  exact_added(*changed.plane({0, 0, 1}));
  exact_quiet(*source.plane());
  exact_finite(*source.plane({0, 0, 1}));
}

void ledger(const std::shared_ptr<JobScheduler> &scheduler, std::size_t retained) {
  const auto usage = scheduler->usage();
  require(usage.running == 0 && usage.queued == 0 && usage.running_bytes == 0 &&
              usage.retained_bytes == retained && usage.reserved_bytes == retained,
          "Actual retained-resource ledger disagrees with exported ownership");
}
void retained_job_policy_export_and_drop() {
  // Document is the existing public job container only. This SDK observation
  // does not qualify NaN sessions, desktop rendering or Gaussian NaN numerics.
  const auto source = owned_stack();
  Application application;
  const auto id = application.create(source, "Owned NaN source SDK fixture");
  const auto original = application.snapshot(id);
  auto scheduler = application.job_scheduler();
  scheduler->set_limits({1, 0, 8});
  TargetRequest request;
  request.scope = TargetScope::Positions;
  request.positions = {{0, 0, 1}};
  request.roi = RoiPolicy::WholePlane;
  const auto target = application.resolve_target(request, id, "add");
  JobOptions options;
  options.retain_result_resources = true;
  auto job = application.submit("add", target, {{"value", 1.0}}, {}, options);
  require(job.wait_for(5s) && job.ready() && job.info().state == JobState::Ready,
          "Actual finite-Z1 native job did not become READY within five seconds");
  const auto charge = job.info().resource_bytes;
  require(charge > 0, "Real retained operation has no resource reservation");
  ledger(scheduler, charge);
  require(application.snapshot(id).shares_data(original) &&
              application.history(id).undo_steps == 0,
          "Preparing finite Z1 changed source data/history");
  std::optional<PreparedOperation> prepared = job.prepared();
  std::optional<DocumentSnapshot> snapshot = prepared->preview();
  std::optional<Dataset> exported = snapshot->data().dataset;
  auto table = exported->shared_planes();
  auto plane = exported->plane();
  require(exported->sample_policy() == opted && table && table->sample_policy() == opted,
          "Actual retained Dataset/table wrapper lost admission policy");
  exact_quiet(*plane);
  exact_added(*exported->plane({0, 0, 1}));
  auto calibration = exported->calibration();
  calibration.unit = "um";
  std::optional<Dataset> calibrated = exported->with_calibration(calibration);
  std::optional<Dataset> replaced = exported->with_plane({0, 0, 1}, literal_plane(false));
  require(calibrated->sample_policy() == opted && replaced->sample_policy() == opted,
          "Actual guarded derivation lost source policy");
  exact_quiet(*calibrated->plane());
  exact_quiet(*replaced->plane());
  exact_finite(*replaced->plane({0, 0, 1}));
  const auto published = application.publish(job);
  require(published.document_id == id && job.info().state == JobState::Completed &&
              application.history(id).undo_steps == 1,
          "Actual finite-Z1 publication did not change the selected document once");
  scheduler->shutdown(); // Join before last-owner assertions: no worker alias remains.
  const auto live = application.snapshot(id).data().dataset;
  require(live.sample_policy() == opted,
          "Published detached storage lost explicit source policy");
  exact_quiet(*live.plane());
  exact_added(*live.plane({0, 0, 1}));
  exact_quiet(*original.data().dataset.plane());
  exact_finite(*original.data().dataset.plane({0, 0, 1}));
  prepared.reset();
  snapshot.reset();
  exported.reset();
  calibrated.reset();
  replaced.reset();
  table.reset();
  ledger(scheduler, charge); // The actual exported plane is the final guarded owner.
  exact_quiet(*plane);
  plane.reset();
  ledger(scheduler, 0);
  exact_quiet(*live.plane());
  exact_added(*live.plane({0, 0, 1}));
  exact_quiet(*source.plane());
  exact_finite(*source.plane({0, 0, 1}));
}

struct Observation {
  const char *name;
  void (*run)();
};
constexpr std::array<Observation, 10> observations{{
    {"SDK_NAN01_exact_ieee_transport", exact_ieee_transport},
    {"SDK_NAN02_owned_gray_source", owned_gray_source},
    {"SDK_NAN03_finite_default_compatibility", finite_default_compatibility},
    {"SDK_NAN04_nonquiet_and_lossy_rejection", nonquiet_and_lossy_rejection},
    {"SDK_NAN05_declared_storage_and_policy_rejection", declared_storage_and_policy_rejection},
    {"SDK_NAN06_virtual_preflight_without_reads", virtual_preflight_without_reads},
    {"SDK_NAN07_table_subsets_and_cross_policy", table_subsets_and_cross_policy},
    {"SDK_NAN08_immutable_edits_and_calibration", immutable_edits_and_calibration},
    {"SDK_NAN09_gaussian_guards_and_finite_z1", gaussian_guards_and_finite_z1},
    {"SDK_NAN10_retained_job_policy_export_and_drop", retained_job_policy_export_and_drop},
}};
} // namespace

int main() {
  for (const auto &observation : observations) {
    try {
      observation.run();
      std::cout << "PASS " << observation.name << '\n';
    } catch (const std::exception &error) {
      std::cerr << "FAIL " << observation.name << ": " << error.what() << '\n';
      return 1;
    }
  }
  return 0;
}
