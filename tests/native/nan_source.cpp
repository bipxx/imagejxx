#include <imagejxx/application.hpp>
#include <imagejxx/gaussian.hpp>
#include <imagejxx/operations.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace imagejxx;
using namespace std::chrono_literals;

namespace {
constexpr auto preserve = FloatingSamplePolicy::PreserveQuietNaNFloat32;
constexpr auto finite_only = FloatingSamplePolicy::FiniteOnly;
constexpr std::uint32_t payload32 = 0x7fc00001u;
constexpr std::uint64_t payload64 = 0x7ff8000020000000ULL;
static_assert(sizeof(float) == 4 && sizeof(double) == 8 &&
              std::numeric_limits<float>::is_iec559 &&
              std::numeric_limits<double>::is_iec559,
              "The frozen source contract requires IEEE binary32/binary64");

void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class F> void rejects(F function, const char *message) {
  bool rejected = false;
  try {
    function();
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  check(rejected, message);
}
double from64(std::uint64_t bits) {
  double value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}
std::uint64_t bits64(double value) {
  std::uint64_t result;
  std::memcpy(&result, &value, sizeof(result));
  return result;
}
Image fixture(bool nan = true) {
  // Independent IEEE constants, not a result of either tested transport helper.
  Image image(3, 2, 1, PixelType::Float32);
  image.pixels = {0.0, -0.0, 1.0, -2.5, std::ldexp(1.0, -149),
                  nan ? from64(payload64) : 3.0};
  return image;
}
void same(const Image &actual, const Image &expected) {
  check(actual.width == expected.width && actual.height == expected.height &&
            actual.channels == expected.channels && actual.type == expected.type &&
            actual.pixels.size() == expected.pixels.size(),
        "Immutable source geometry/type changed");
  check(std::memcmp(actual.pixels.data(), expected.pixels.data(),
                    actual.pixels.size() * sizeof(double)) == 0,
        "Source bits changed (including zero sign or quiet-NaN payload)");
}
void has_payload(const Image &image) {
  check(bits64(image.pixels.back()) == payload64 &&
            float32_source_sample_bits(image.pixels.back(), preserve) == payload32,
        "The exact independently declared 0x7fc00001 payload was lost");
}

void N01_lossless_bit_transport() {
  struct Pair { std::uint32_t f32; std::uint64_t f64; };
  // These field encodings are independently frozen IEEE examples. Finite
  // transport is tested along with both NaN signs, a nontrivial payload, and
  // signed zero; no floating NaN equality supplies an assertion.
  const std::array<Pair, 10> examples{{
      {0x00000000u, 0x0000000000000000ULL},
      {0x80000000u, 0x8000000000000000ULL},
      {0x3f800000u, 0x3ff0000000000000ULL},
      {0xc0200000u, 0xc004000000000000ULL},
      {0x00000001u, 0x36a0000000000000ULL},
      {0x00800000u, 0x3810000000000000ULL},
      {0x7f7fffffu, 0x47efffffe0000000ULL},
      {0xff7fffffu, 0xc7efffffe0000000ULL},
      {payload32, payload64},
      {0xffc12345u, 0xfff82468a0000000ULL},
  }};
  for (const auto item : examples) {
    const auto decoded = float32_source_sample(item.f32, preserve);
    check(bits64(decoded) == item.f64,
          "Float32 decoder changed an independently declared double encoding");
    check(float32_source_sample_bits(from64(item.f64), preserve) == item.f32,
          "Float32 encoder changed independently declared source bits");
  }
  for (const auto bits : {0x7f800000u, 0xff800000u, 0x7f800001u, 0xff800001u})
    rejects([&] { (void)float32_source_sample(bits, preserve); },
            "INF or signaling-NaN raw admission succeeded");
  for (const auto bits : {0x7ff0000000000000ULL, 0xfff0000000000000ULL,
                         0x7ff0000020000000ULL, 0xfff0000020000000ULL,
                         0x7ff8000020000001ULL})
    rejects([&] { (void)float32_source_sample_bits(from64(bits), preserve); },
            "Invalid or non-binary32 NaN representation was admitted");
  rejects([] { (void)float32_source_sample(payload32); },
          "Default raw decoder accepted a quiet NaN");
  rejects([] { (void)float32_source_sample_bits(from64(payload64)); },
          "Default sample encoder accepted a quiet NaN");
  rejects([] { (void)float32_source_sample_bits(0.1, preserve); },
          "Encoder silently rounded an unrepresented finite sample");
  rejects([] { (void)float32_source_sample_bits(1e40, preserve); },
          "Encoder admitted a finite Float32 overflow");
}

void N02_owned_admission_and_finite_defaults() {
  const auto source = fixture();
  auto admitted = canonical_float32_source(source, preserve);
  same(admitted, source);
  same(source, fixture());
  has_payload(admitted);
  auto rounding = fixture(false);
  rounding.pixels[0] = 1.0 + std::ldexp(1.0, -25); // below half a Float32 ULP
  const auto rounded = canonical_float32_source(rounding);
  check(bits64(rounded.pixels[0]) == 0x3ff0000000000000ULL &&
            rounding.pixels[0] == 1.0 + std::ldexp(1.0, -25),
        "Typed source canonicalization failed rounding or mutated its caller");
  const auto opted = Dataset::from_stack({rounding}, {}, preserve);
  check(opted.sample_policy() == preserve && opted.plane()->pixels[0] == 1.0,
        "Opted owned ingestion omitted finite canonicalization");
  const auto legacy = Dataset::from_stack({rounding});
  check(legacy.sample_policy() == finite_only &&
            bits64(legacy.plane()->pixels[0]) == bits64(rounding.pixels[0]),
        "Default finite Dataset intermediates changed");
  Image integer(1, 1, 1, PixelType::UInt8);
  integer.pixels[0] = 255.75;
  check(Dataset::from_stack({integer}).plane()->pixels[0] == 255.75,
        "Finite-only integer admission changed existing intermediate semantics");
  check(SharedPlanes{}.sample_policy() == finite_only,
        "An empty shared table changed its admission default");
  rejects([&] { (void)Dataset::from_stack({source}); },
          "Default Dataset accepted NaN without explicit opt-in");
  rejects([&] { (void)SharedPlanes({source}); },
          "Default SharedPlanes accepted NaN without explicit opt-in");
  rejects([&] { (void)canonical_float32_source(source); },
          "Default typed source canonicalizer accepted NaN");
}

void N03_type_range_policy_rejections() {
  for (const auto bits : {0x7ff0000000000000ULL, 0xfff0000000000000ULL,
                         0x7ff0000020000000ULL, 0xfff0000020000000ULL,
                         0x7ff8000020000001ULL}) {
    auto malformed = fixture(false);
    malformed.pixels[0] = from64(bits);
    rejects([&] { (void)Dataset::from_stack({malformed}); },
            "Default source admitted a nonfinite sample");
    rejects([&] { (void)Dataset::from_stack({malformed}, {}, preserve); },
            "Owned opted source admitted infinity, sNaN, or a foreign payload");
    rejects([&] { (void)canonical_float32_source(malformed, preserve); },
            "Typed source admitted infinity, sNaN, or a foreign payload");
    check(bits64(malformed.pixels[0]) == bits,
          "Failed source admission modified the original sample bits");
  }
  for (const auto type : {PixelType::UInt8, PixelType::UInt16, PixelType::Float64}) {
    Image other(3, 2, 1, type);
    rejects([&] { (void)Dataset::from_stack({other}, {}, preserve); },
            "Quiet-NaN policy accepted a non-Float32 type");
    rejects([&] { (void)canonical_float32_source(other, preserve); },
            "Typed source canonicalizer accepted a different declared type");
    other.pixels[0] = from64(payload64);
    rejects([&] { (void)Dataset::from_stack({other}); },
            "Finite-only non-Float32 storage admitted a NaN");
  }
  Image rgb(3, 2, 3, PixelType::Float32);
  rejects([&] { (void)Dataset::from_stack({rgb}, {}, preserve); },
          "Quiet-NaN policy accepted interleaved samples");
  rejects([&] { (void)canonical_float32_source(rgb, preserve); },
          "Typed source canonicalizer accepted RGB");
  for (const auto value : {1e40, -1e40}) {
    auto overflow = fixture(false);
    overflow.pixels[2] = value;
    rejects([&] { (void)Dataset::from_stack({overflow}); },
            "Finite-only Float32 source admitted a finite overflow");
    rejects([&] { (void)Dataset::from_stack({overflow}, {}, preserve); },
            "Opted Float32 source admitted a finite overflow");
    rejects([&] { (void)canonical_float32_source(overflow, preserve); },
            "Typed canonicalizer admitted a finite Float32 overflow");
  }
  const auto unknown = static_cast<FloatingSamplePolicy>(73);
  rejects([&] { (void)SharedPlanes({}, unknown); }, "Unknown table policy accepted");
  rejects([&] { (void)canonical_float32_source(fixture(false), unknown); },
          "Unknown typed source policy accepted");
  rejects([&] { (void)float32_source_sample(0, unknown); },
          "Unknown raw decoder policy accepted");
  rejects([&] { (void)float32_source_sample_bits(0, unknown); },
          "Unknown raw encoder policy accepted");
}

void N04_shared_subset_and_owned_materialization() {
  SharedPlanes original({fixture(), fixture(false)}, preserve);
  auto copy = original;
  check(copy.sample_policy() == preserve &&
            copy.shared(0).get() == original.shared(0).get(),
        "Shared table copy changed policy or pixel ownership");
  auto subset = original.subset({1, 0, 0});
  check(subset.sample_policy() == preserve && subset.size() == 3 &&
            subset.shared(0).get() == original.shared(1).get() &&
            subset.shared(1).get() == original.shared(0).get() &&
            subset.shared(2).get() == original.shared(0).get(),
        "Subset failed exact ordering, duplicates, policy, or shared identity");
  has_payload(subset[1]);
  auto empty = original.subset({});
  check(empty.empty() && empty.sample_policy() == preserve,
        "Empty subset silently downgraded the explicit policy");
  subset.erase(0);
  has_payload(subset[0]);
  check(subset.sample_policy() == preserve, "Erase lost source policy");
  auto stack = original.materialize();
  same(stack[0], fixture());
  stack[0].pixels.back() = 4;
  has_payload(original[0]);
  auto raw_stack = original.materialize();
  rejects([&] { (void)Dataset::from_stack(raw_stack); },
          "Untyped eager materialization silently preserved an opt-in policy");
  auto imported = Dataset::from_stack(raw_stack, {}, preserve);
  has_payload(*imported.plane());
  const auto from_table = Dataset::from_shared_planes({1, 1, 2, 2, 3, 1}, original);
  check(from_table.sample_policy() == preserve &&
            from_table.plane().get() == original.shared(0).get(),
        "Dataset shared-table import lost policy or shared identity");
  const auto scientific = Dataset::from_planes(
      {2, 2, 1, 2, 3, 1}, {fixture(), fixture(false), fixture(false), fixture()}, {}, preserve);
  has_payload(*scientific.plane({0, 0, 0}));
  same(*scientific.plane({0, 1, 0}), fixture(false));
  same(*scientific.plane({1, 0, 0}), fixture(false));
  has_payload(*scientific.plane({1, 1, 0}));
  check(scientific.sample_policy() == preserve,
        "Scientific C/T planes were mistaken for interleaved RGB samples");
}

void N05_replacement_and_cross_policy_atomicity() {
  SharedPlanes table({fixture(false), fixture(false)}, preserve);
  auto before = table;
  table.replace(0, fixture());
  has_payload(table[0]);
  same(before[0], fixture(false));
  table.insert(1, fixture());
  has_payload(table[1]);
  check(table.sample_policy() == preserve && table.size() == 3,
        "Owned mutation changed policy");
  auto alias = std::make_shared<const Image>(fixture());
  table.replace_shared(2, alias);
  check(table.shared(2).get() == alias.get(), "Validated immutable alias was copied");
  SharedPlanes foreign({fixture()}, preserve);
  table.insert_shared(0, foreign, 0);
  check(table.shared(0).get() == foreign.shared(0).get(),
        "Cross-table opted alias lost exact storage identity");
  auto unrepresented = fixture(false);
  unrepresented.pixels[0] = 0.1;
  const auto current = table;
  auto invalid_alias = std::make_shared<const Image>(unrepresented);
  rejects([&] { table.replace_shared(0, invalid_alias); },
          "Immutable shared admission silently rounded unrepresented Float32");
  check(table.size() == current.size() &&
            table.shared(0).get() == current.shared(0).get(),
        "Failed shared replacement changed the table");
  SharedPlanes finite({fixture(false)});
  const auto finite_before = finite.shared(0);
  rejects([&] { finite.replace_shared(0, foreign.shared(0)); },
          "Finite-only table imported an opted NaN alias");
  rejects([&] { finite.insert_shared(1, foreign, 0); },
          "Finite-only table inserted an opted NaN alias");
  check(finite.size() == 1 && finite.shared(0).get() == finite_before.get() &&
            finite.sample_policy() == finite_only,
        "Cross-policy failure mutated the recipient table");
}

void N06_dataset_edits_calibration_and_plane_bits() {
  const auto source = Dataset::from_stack({fixture(), fixture(false)}, {}, preserve);
  const auto p0 = source.plane();
  auto changed = source.with_plane({0, 0, 1}, fixture());
  check(changed.sample_policy() == preserve && changed.plane().get() == p0.get(),
        "Replacing another plane copied or downgraded untouched NaN storage");
  has_payload(*changed.plane({0, 0, 1}));
  same(*source.plane({0, 0, 1}), fixture(false));
  auto calibration = source.calibration();
  calibration.spacing = {.5, 2, 3};
  calibration.origin = {11, -7, 19};
  calibration.direction = {0, -1, 0, 1, 0, 0, 0, 0, 1};
  calibration.unit = "um";
  calibration.frame_interval = .25;
  calibration.time_origin = -2;
  calibration.time_unit = "min";
  auto calibrated = changed.with_calibration(calibration);
  const auto &actual = calibrated.calibration();
  check(calibrated.sample_policy() == preserve && calibrated.shares_pixels(changed) &&
            actual.spacing == calibration.spacing && actual.origin == calibration.origin &&
            actual.direction == calibration.direction && actual.unit == "um" &&
            actual.frame_interval == .25 && actual.time_origin == -2 &&
            actual.time_unit == "min", "Calibration edit changed source bits or policy");
  has_payload(*calibrated.plane());
  const auto region = calibrated.region({}, 2, 1, 1, 1);
  check(bits64(region.pixels[0]) == payload64, "Region copied a normalized NaN payload");
  const auto xz = calibrated.extract_plane(Plane::XZ, 1);
  const auto yz = calibrated.extract_plane(Plane::YZ, 2);
  check(bits64(xz.at(2, 0)) == payload64 && bits64(xz.at(2, 1)) == payload64 &&
            bits64(yz.at(1, 0)) == payload64 && bits64(yz.at(1, 1)) == payload64,
        "Plane extraction changed exact NaN payloads");
  const auto multi = source.with_planes({{{0, 0, 0}, fixture(false)},
                                         {{0, 0, 1}, fixture()}});
  check(multi.sample_policy() == preserve, "Batch edit lost policy");
  same(*multi.plane(), fixture(false));
  has_payload(*multi.plane({0, 0, 1}));
  auto bad = fixture(false);
  bad.pixels[0] = from64(0x7ff0000000000000ULL);
  rejects([&] { (void)source.with_planes({{{0, 0, 0}, fixture(false)},
                                        {{0, 0, 1}, bad}}); },
          "Batch with a later invalid sample succeeded");
  rejects([&] { (void)source.with_planes({{{0, 0, 0}, fixture(false)},
                                        {{0, 0, 0}, fixture()}}); },
          "Duplicate replacement positions succeeded");
  check(source.plane().get() == p0.get(), "Failed batch changed original source storage");
  same(*source.plane(), fixture());
  same(*source.plane({0, 0, 1}), fixture(false));
  const auto finite = Dataset::from_stack({fixture(false)});
  rejects([&] { (void)finite.with_plane({}, fixture()); },
          "Default Dataset replacement accepted a quiet NaN");
}

class CountingVirtual final : public PlaneSource {
public:
  mutable std::size_t reads = 0;
  DatasetShape shape() const override { return {1, 1, 1, 2, 3, 1}; }
  PixelType pixel_type() const override { return PixelType::Float32; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++reads;
    return std::make_shared<const Image>(fixture(false));
  }
};
void N07_unknown_virtual_policy_is_not_inferred() {
  auto source = std::make_shared<CountingVirtual>();
  rejects([&] { (void)Dataset(source, {}, preserve); },
          "Unknown virtual source was granted owned NaN admission");
  check(source->reads == 0, "Policy rejection read virtual pixels");
  const auto ordinary = Dataset(source);
  check(ordinary.sample_policy() == finite_only && source->reads == 0 &&
            !ordinary.shared_planes(), "Default virtual metadata path changed");
  same(*ordinary.plane(), fixture(false));
  check(source->reads == 1, "Ordinary virtual plane read changed");
}

void N08_processing_finite_guards_remain() {
  const auto image = fixture();
  rejects([&] { (void)canonical_image(image); },
          "Generic operation canonicalization admitted a quiet NaN");
  rejects([&] { (void)execute_operation("add", image, {{"value", 1.0}}); },
          "Generic operation dispatch admitted a quiet NaN");
  rejects([&] { (void)execute_operation("gaussian_blur", image); },
          "Generic Gaussian dispatch lost its finite guard");
  rejects([&] { (void)gaussian_blur_imagej(image, 0, 0); },
          "Sigma-zero ImageJ Gaussian bypassed finite source validation");
  rejects([&] { (void)gaussian_blur_imagej(image, 2, 2); },
          "ImageJ Gaussian admitted the opted source without a new numerical policy");
  same(image, fixture());
  Image constant(3, 2, 1, PixelType::Float32);
  for (auto &value : constant.pixels)
    value = 4;
  same(execute_operation("gaussian_blur", constant).image, constant);
  same(gaussian_blur_imagej(constant, 0, 0), constant);
}

void N09_real_retained_result_and_publication_detach() {
  // Document is solely the existing public preparation/publication container.
  // This does not qualify session serialization, display, or Document NaN scope.
  const auto original = Dataset::from_stack({fixture(), fixture(false)}, {}, preserve);
  auto owner = std::make_shared<Document>(original);
  Application app;
  app.adopt(owner);
  auto scheduler = app.job_scheduler();
  scheduler->set_limits({1, 0, 8});
  TargetRequest request;
  request.scope = TargetScope::Positions;
  request.positions = {{0, 0, 1}}; // Generic processing sees only finite Z1.
  request.roi = RoiPolicy::WholePlane;
  const auto target = app.resolve_target(request, owner->id, "add");
  JobOptions options;
  options.retain_result_resources = true;
  auto job = app.submit("add", target, {{"value", 1.0}}, {}, options);
  check(job.wait_for(5s) && job.info().state == JobState::Ready,
        "Real finite-plane retained preparation did not become Ready");
  const auto charge = job.info().resource_bytes;
  check(charge > 0, "Real retained operation has no declared resource estimate");
  auto usage = scheduler->usage();
  check(usage.running_bytes == 0 && usage.retained_bytes == charge &&
            usage.reserved_bytes == charge,
        "Real prepared result omitted its native retained charge");
  std::optional<PreparedOperation> prepared = job.prepared();
  std::optional<DocumentSnapshot> snapshot = prepared->preview();
  std::optional<Dataset> dataset = snapshot->data().dataset;
  auto table = dataset->shared_planes();
  auto plane = dataset->plane();
  auto calibration = dataset->calibration();
  calibration.unit = "um";
  std::optional<Dataset> recalibrated = dataset->with_calibration(calibration);
  std::optional<Dataset> replaced = dataset->with_plane({0, 0, 1}, fixture(false));
  check(dataset->sample_policy() == preserve && table && table->sample_policy() == preserve,
        "Resource tagging lost Dataset/table source admission policy");
  check(recalibrated->sample_policy() == preserve && replaced->sample_policy() == preserve,
        "Resource-wrapped calibration/replacement lost source policy");
  has_payload(*recalibrated->plane());
  has_payload(*replaced->plane());
  same(*replaced->plane({0, 0, 1}), fixture(false));
  has_payload(*plane);
  same(*dataset->plane(), fixture());
  auto expected_finite = fixture(false);
  expected_finite.pixels = {1, 1, 2, -1.5, 1, 4};
  same(*dataset->plane({0, 0, 1}), expected_finite);
  same(*original.plane({0, 0, 1}), fixture(false));
  const auto published = app.publish(job);
  check(published.document_id == owner->id && job.info().state == JobState::Completed,
        "Resource fixture did not publish to its actual selected source");
  scheduler->shutdown(); // Joins workers; no transient worker alias can mask lifetime.
  check(owner->snapshot().data().dataset.sample_policy() == preserve,
        "Actual publication detached guards by dropping source policy");
  has_payload(*owner->snapshot().data().dataset.plane());
  same(*owner->snapshot().data().dataset.plane({0, 0, 1}), expected_finite);
  prepared.reset();
  snapshot.reset();
  dataset.reset();
  recalibrated.reset();
  replaced.reset();
  table.reset();
  check(scheduler->usage().retained_bytes == charge,
        "An actual exported tagged plane stopped owning its retained lease");
  plane.reset();
  usage = scheduler->usage();
  check(usage.running == 0 && usage.running_bytes == 0 && usage.retained_bytes == 0 &&
            usage.reserved_bytes == 0,
        "Detached published storage kept a hidden retained-result owner");
  // Live published storage remains usable after the last exported resource alias.
  has_payload(*owner->snapshot().data().dataset.plane());
  same(*owner->snapshot().data().dataset.plane({0, 0, 1}), expected_finite);
}
} // namespace

int main() {
  try {
    const std::array<std::pair<const char *, void (*)()>, 9> groups{{
        {"N01_lossless_bit_transport", N01_lossless_bit_transport},
        {"N02_owned_admission_and_finite_defaults", N02_owned_admission_and_finite_defaults},
        {"N03_type_range_policy_rejections", N03_type_range_policy_rejections},
        {"N04_shared_subset_and_owned_materialization", N04_shared_subset_and_owned_materialization},
        {"N05_replacement_and_cross_policy_atomicity", N05_replacement_and_cross_policy_atomicity},
        {"N06_dataset_edits_calibration_and_plane_bits", N06_dataset_edits_calibration_and_plane_bits},
        {"N07_unknown_virtual_policy_is_not_inferred", N07_unknown_virtual_policy_is_not_inferred},
        {"N08_processing_finite_guards_remain", N08_processing_finite_guards_remain},
        {"N09_real_retained_result_and_publication_detach", N09_real_retained_result_and_publication_detach},
    }};
    for (const auto &group : groups) {
      group.second();
      std::cout << "PASS " << group.first << '\n';
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
