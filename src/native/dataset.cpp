#include <imagejxx/dataset.hpp>
#include <imagejxx/operations.hpp>
#include "kernel_control.hpp"
#include "retained_resource_internal.hpp"
#include "dataset_read_counter_internal.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace imagejxx {
namespace detail {
struct DatasetReadCounterState {
  std::atomic<std::uint64_t> plane_reads{0}, region_reads{0};
  std::atomic<bool> overflowed{false};
};
} // namespace detail
namespace {
std::size_t checked_product(const std::array<int, 6> &sizes) {
  std::size_t result = 1;
  for (int size : sizes) {
    if (size <= 0 || result > std::numeric_limits<std::size_t>::max() / size)
      throw std::invalid_argument(
          "Dataset dimensions must be positive and fit addressable storage");
    result *= static_cast<std::size_t>(size);
  }
  return result;
}
void validate_position(DatasetShape shape, PlanePosition p) {
  if (p.t < 0 || p.t >= shape.t || p.c < 0 || p.c >= shape.c || p.z < 0 ||
      p.z >= shape.z)
    throw std::out_of_range("Dataset T/C/Z position is out of range");
}
std::size_t plane_index(DatasetShape shape, PlanePosition p) {
  validate_position(shape, p);
  return (static_cast<std::size_t>(p.t) * shape.c + p.c) * shape.z + p.z;
}
void validate_sample_policy(FloatingSamplePolicy policy) {
  if (policy != FloatingSamplePolicy::FiniteOnly &&
      policy != FloatingSamplePolicy::PreserveQuietNaNFloat32 &&
      policy != FloatingSamplePolicy::PreserveIEEEFloat32)
    throw std::invalid_argument("Invalid floating sample policy");
}
void validate_source_samples(const Image &image, FloatingSamplePolicy policy) {
  validate_sample_policy(policy);
  if ((policy == FloatingSamplePolicy::PreserveQuietNaNFloat32 || policy == FloatingSamplePolicy::PreserveIEEEFloat32)) {
    if (image.type != PixelType::Float32 || image.channels != 1)
      throw std::invalid_argument(policy == FloatingSamplePolicy::PreserveIEEEFloat32
          ? "IEEE source policy requires grayscale Float32 storage"
          : "Quiet-NaN source policy requires grayscale Float32 storage");
    kernel_blocks("Immutable plane sample validation", image.pixels.size(),
                  [&](std::size_t first, std::size_t last) {
                    for (auto i = first; i < last; ++i) {
                      const double value = image.pixels[i];
                      (void)float32_source_sample_bits(value, policy);
                    }
                  });
  } else {
    kernel_blocks("Immutable plane sample validation", image.pixels.size(),
                  [&](std::size_t first, std::size_t last) {
                    for (auto i = first; i < last; ++i) {
                      const double value = image.pixels[i];
                      require_finite(value, "Immutable plane pixel");
                      // An immutable Float32 source must not retain a finite double that would
                      // overflow when exported in its declared type. Keep legacy finite integer
                      // and within-range intermediate values unchanged under the default policy.
                      if (image.type == PixelType::Float32 &&
                          (value < -double(std::numeric_limits<float>::max()) ||
                           value > double(std::numeric_limits<float>::max())))
                        throw std::invalid_argument("Float32 source sample exceeds finite range");
                    }
                  });
  }
}
void validate_plane(const Image &image, DatasetShape shape, PixelType type,
                    bool pixels,
                    FloatingSamplePolicy policy = FloatingSamplePolicy::FiniteOnly) {
  if (image.width != shape.x || image.height != shape.y ||
      image.channels != shape.s || image.type != type ||
      image.pixels.size() !=
          static_cast<std::size_t>(shape.x) * shape.y * shape.s)
    throw std::invalid_argument("Dataset plane shape/type mismatch");
  if (pixels)
    validate_source_samples(image, policy);
}
void validate_storage(const Image &image, FloatingSamplePolicy policy) {
  const auto count =
      checked_product({1, 1, 1, image.height, image.width, image.channels});
  if (image.width > std::numeric_limits<int>::max() - 8194 ||
      image.height > std::numeric_limits<int>::max() - 8194 ||
      count != image.pixels.size())
    throw std::invalid_argument("Invalid immutable plane storage");
  switch (image.type) {
  case PixelType::UInt8:
  case PixelType::UInt16:
  case PixelType::Float32:
  case PixelType::Float64:
    break;
  default:
    throw std::invalid_argument("Invalid immutable plane pixel type");
  }
  validate_source_samples(image, policy);
}
Image prepare_owned_storage(Image image, FloatingSamplePolicy policy) {
  validate_sample_policy(policy);
  if ((policy == FloatingSamplePolicy::PreserveQuietNaNFloat32 || policy == FloatingSamplePolicy::PreserveIEEEFloat32))
    image = canonical_float32_source(std::move(image), policy);
  validate_storage(image, policy);
  return image;
}
double determinant(const std::array<double, 9> &m) {
  return m[0] * (m[4] * m[8] - m[5] * m[7]) -
         m[1] * (m[3] * m[8] - m[5] * m[6]) +
         m[2] * (m[3] * m[7] - m[4] * m[6]);
}
std::array<double, 3> multiply(const std::array<double, 9> &m,
                               const std::array<double, 3> &v) {
  return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2],
          m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
          m[6] * v[0] + m[7] * v[1] + m[8] * v[2]};
}
} // namespace
// These private implementation types cross the independently linked Python IO,
// NumPy and desktop modules. External type linkage is required for RTTI to
// recognize the same reviewed provider graph across shared-library boundaries.
namespace detail {
class MemorySource final : public PlaneSource, public detail::ResourceSource {
public:
  MemorySource(DatasetShape shape, SharedPlanes planes)
      : shape_(shape), planes_(std::move(planes)) {
    shape_.validate();
    if (planes_.size() != shape_.plane_count())
      throw std::invalid_argument("Dataset plane count mismatch");
    type_ = planes_[0].type;
    for (const auto &image : planes_)
      validate_plane(image, shape_, type_, false);
  }
  DatasetShape shape() const override { return shape_; }
  PixelType pixel_type() const override { return type_; }
  SharedPlanes shared_planes() const { return planes_; }
  FloatingSamplePolicy sample_policy() const { return planes_.sample_policy(); }
  const PlaneSource *resource_identity() const override { return this; }
  std::shared_ptr<const PlaneSource> detach_resources() const override {
    std::shared_ptr<MemorySource> result;
    for (std::size_t i = 0; i < planes_.size(); ++i) {
      auto original = planes_.shared(i);
      auto detached = detail::detach_image(original);
      if (detached.owner_before(original) || original.owner_before(detached)) {
        if (!result)
          result = std::make_shared<MemorySource>(*this);
        detail::ResourceAccess::rebind(result->planes_, i, std::move(detached));
      }
    }
    return result;
  }
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    return planes_.shared(plane_index(shape_, p));
  }
  std::shared_ptr<const PlaneSource> replace(PlanePosition p,
                                             Image image) const {
    auto updated = std::make_shared<MemorySource>(*this);
    updated->planes_.replace(plane_index(shape_, p), std::move(image));
    return updated;
  }
  std::shared_ptr<const PlaneSource>
  replace(std::vector<std::pair<PlanePosition, Image>> edits) const {
    auto updated = std::make_shared<MemorySource>(*this);
    for (auto &edit : edits)
      updated->planes_.replace(plane_index(shape_, edit.first),
                               std::move(edit.second));
    return updated;
  }

private:
  DatasetShape shape_;
  PixelType type_;
  SharedPlanes planes_;
};
class OverlaySource final : public PlaneSource, public detail::ResourceSource {
public:
  OverlaySource(std::shared_ptr<const PlaneSource> source, PlanePosition p,
                Image replacement) {
    // Flatten consecutive edits: discarding history can release overwritten
    // pixels instead of retaining an ever-growing chain of prior sources.
    if (auto old = std::dynamic_pointer_cast<const OverlaySource>(source)) {
      source_ = old->source_;
      replacements_ = old->replacements_;
    } else
      source_ = std::move(source);
    replacements_[plane_index(shape(), p)] =
        std::make_shared<const Image>(std::move(replacement));
  }
  OverlaySource(std::shared_ptr<const PlaneSource> source,
                std::vector<std::pair<PlanePosition, Image>> edits) {
    if (auto old = std::dynamic_pointer_cast<const OverlaySource>(source)) {
      source_ = old->source_;
      replacements_ = old->replacements_;
    } else
      source_ = std::move(source);
    for (auto &edit : edits)
      replacements_[plane_index(shape(), edit.first)] =
          std::make_shared<const Image>(std::move(edit.second));
  }
  DatasetShape shape() const override { return source_->shape(); }
  PixelType pixel_type() const override { return source_->pixel_type(); }
  const std::shared_ptr<const PlaneSource> &source() const { return source_; }
  const PlaneSource *resource_identity() const override { return this; }
  std::shared_ptr<const PlaneSource> detach_resources() const override {
    auto source = detail::detach_source(source_);
    std::shared_ptr<OverlaySource> result;
    if (source != source_) {
      result = std::make_shared<OverlaySource>(*this);
      result->source_ = std::move(source);
    }
    for (const auto &[index, original] : replacements_) {
      auto detached = detail::detach_image(original);
      if (detached.owner_before(original) || original.owner_before(detached)) {
        if (!result)
          result = std::make_shared<OverlaySource>(*this);
        result->replacements_[index] = std::move(detached);
      }
    }
    return result;
  }
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    auto found = replacements_.find(plane_index(shape(), p));
    return found == replacements_.end() ? source_->read_plane(p)
                                        : found->second;
  }
  Image read_region(PlanePosition p, int x, int y, int width,
                    int height) const override {
    auto found = replacements_.find(plane_index(shape(), p));
    return found == replacements_.end()
               ? source_->read_region(p, x, y, width, height)
               : crop(*found->second, x, y, width, height);
  }

private:
  std::shared_ptr<const PlaneSource> source_;
  std::map<std::size_t, std::shared_ptr<const Image>> replacements_;
};
class BorrowedStackSource final : public PlaneSource {
public:
  explicit BorrowedStackSource(const Stack &frames) : frames_(frames) {
    if (frames.empty() || frames.size() > static_cast<std::size_t>(
                                              std::numeric_limits<int>::max()))
      throw std::invalid_argument(
          "Stack must contain an addressable number of planes");
    shape_ = {1,
              1,
              static_cast<int>(frames.size()),
              frames[0].height,
              frames[0].width,
              frames[0].channels};
  }
  DatasetShape shape() const override { return shape_; }
  PixelType pixel_type() const override { return frames_[0].type; }
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    validate_position(shape_, p);
    return std::shared_ptr<const Image>(&frames_[p.z], [](const Image *) {});
  }

private:
  const Stack &frames_;
  DatasetShape shape_;
};

class GuardedSource final : public PlaneSource, public detail::ResourceSource {
public:
  GuardedSource(std::shared_ptr<const PlaneSource> source,
                std::shared_ptr<const void> guard)
      : source_(std::move(source)), guard_(std::move(guard)) {}
  DatasetShape shape() const override { return source_->shape(); }
  PixelType pixel_type() const override { return source_->pixel_type(); }
  const PlaneSource *resource_identity() const override {
    return detail::source_identity(source_.get());
  }
  std::shared_ptr<const PlaneSource> detach_resources() const override {
    return detail::detach_source(source_);
  }
  std::shared_ptr<const Image> read_plane(PlanePosition position) const override {
    return detail::guard_image(source_->read_plane(position), guard_);
  }
  Image read_region(PlanePosition position, int x, int y, int width,
                    int height) const override {
    return source_->read_region(position, x, y, width, height);
  }
  const std::shared_ptr<const PlaneSource> &source() const { return source_; }
  const std::shared_ptr<const void> &guard() const { return guard_; }
private:
  std::shared_ptr<const PlaneSource> source_;
  std::shared_ptr<const void> guard_;
};
class ObservedSource final : public PlaneSource, public detail::ResourceSource {
public:
  ObservedSource(std::shared_ptr<const PlaneSource> source,
                 std::shared_ptr<detail::DatasetReadCounterState> state)
      : source_(std::move(source)), state_(std::move(state)) {}
  DatasetShape shape() const override { return source_->shape(); }
  PixelType pixel_type() const override { return source_->pixel_type(); }
  const std::shared_ptr<const PlaneSource> &source() const { return source_; }
  const std::shared_ptr<detail::DatasetReadCounterState> &state() const {
    return state_;
  }
  const PlaneSource *resource_identity() const override {
    return detail::source_identity(source_.get());
  }
  std::shared_ptr<const PlaneSource> detach_resources() const override {
    auto detached = detail::detach_source(source_);
    if (detached == source_)
      return {};
    return std::make_shared<ObservedSource>(std::move(detached), state_);
  }
  std::shared_ptr<const Image> read_plane(PlanePosition position) const override {
    detail::increment_saturated_read_count(state_->plane_reads, state_->overflowed);
    return source_->read_plane(position);
  }
  Image read_region(PlanePosition position, int x, int y, int width,
                    int height) const override {
    detail::increment_saturated_read_count(state_->region_reads, state_->overflowed);
    return source_->read_region(position, x, y, width, height);
  }
private:
  std::shared_ptr<const PlaneSource> source_;
  std::shared_ptr<detail::DatasetReadCounterState> state_;
};
} // namespace detail
namespace {
using detail::MemorySource;
using detail::OverlaySource;
using detail::BorrowedStackSource;
using detail::GuardedSource;
using detail::ObservedSource;
std::optional<FloatingSamplePolicy>
eager_sample_policy(const std::shared_ptr<const PlaneSource> &source) {
  // No virtual-source extension, pixel reads, or claimed admission policy for
  // arbitrary plugins. Only our reviewed immutable table/wrapper graph qualifies.
  if (auto memory = std::dynamic_pointer_cast<const MemorySource>(source))
    return memory->sample_policy();
  if (auto guarded = std::dynamic_pointer_cast<const GuardedSource>(source))
    return eager_sample_policy(guarded->source());
  if (auto observed = std::dynamic_pointer_cast<const ObservedSource>(source))
    return eager_sample_policy(observed->source());
  if (auto overlay = std::dynamic_pointer_cast<const OverlaySource>(source))
    return eager_sample_policy(overlay->source());
  return std::nullopt;
}
void validate_coordinate(DatasetShape shape, Plane view, int coordinate) {
  if (view != Plane::XY && view != Plane::XZ && view != Plane::YZ)
    throw std::invalid_argument("Invalid plane orientation");
  int size = view == Plane::XY   ? shape.z
             : view == Plane::XZ ? shape.y
                                 : shape.x;
  if (coordinate < 0 || coordinate >= size)
    throw std::out_of_range("Plane coordinate is out of range");
}
} // namespace

DatasetReadObserver::DatasetReadObserver()
    : state_(std::make_shared<detail::DatasetReadCounterState>()) {}
DatasetReadCounts DatasetReadObserver::counts() const noexcept {
  return {state_->plane_reads.load(std::memory_order_relaxed),
          state_->region_reads.load(std::memory_order_relaxed),
          state_->overflowed.load(std::memory_order_relaxed)};
}

SharedPlanes::SharedPlanes(Stack planes, FloatingSamplePolicy sample_policy)
    : sample_policy_(sample_policy) {
  validate_sample_policy(sample_policy_);
  planes_.reserve(planes.size());
  for (auto &plane : planes) {
    plane = prepare_owned_storage(std::move(plane), sample_policy_);
    planes_.push_back(std::make_shared<const Image>(std::move(plane)));
  }
}
const Image &SharedPlanes::operator[](std::size_t index) const {
  return *planes_.at(index);
}
std::shared_ptr<const Image> SharedPlanes::shared(std::size_t index) const {
  return planes_.at(index);
}
void SharedPlanes::replace(std::size_t index, Image plane) {
  (void)planes_.at(index); // Reject an invalid index before examining pixels.
  plane = prepare_owned_storage(std::move(plane), sample_policy_);
  auto replacement = std::make_shared<const Image>(std::move(plane));
  planes_[index] = std::move(replacement);
}
void SharedPlanes::replace_shared(std::size_t index,
                                  std::shared_ptr<const Image> plane) {
  (void)planes_.at(index);
  if (!plane)
    throw std::invalid_argument("Replacement plane is null");
  validate_storage(*plane, sample_policy_);
  planes_[index] = std::move(plane);
}
void SharedPlanes::insert(std::size_t index, Image plane) {
  if (index > planes_.size())
    throw std::out_of_range("Plane insertion index is out of range");
  plane = prepare_owned_storage(std::move(plane), sample_policy_);
  auto replacement = std::make_shared<const Image>(std::move(plane));
  planes_.insert(planes_.begin() + index, std::move(replacement));
}
void SharedPlanes::insert_shared(std::size_t index, const SharedPlanes &source,
                                 std::size_t sourceIndex) {
  if (index > planes_.size())
    throw std::out_of_range("Plane insertion index is out of range");
  auto plane = source.shared(sourceIndex);
  // The destination policy governs imported aliases; a preserved NaN cannot
  // bypass finite-only admission by entering through a shared source table.
  validate_storage(*plane, sample_policy_);
  planes_.insert(planes_.begin() + index, std::move(plane));
}
void SharedPlanes::erase(std::size_t index) {
  (void)planes_.at(index);
  planes_.erase(planes_.begin() + index);
}
SharedPlanes
SharedPlanes::subset(const std::vector<std::size_t> &indices) const {
  SharedPlanes result;
  result.sample_policy_ = sample_policy_;
  result.planes_.reserve(indices.size());
  for (auto index : indices)
    result.planes_.push_back(shared(index));
  return result;
}
Stack SharedPlanes::materialize() const {
  Stack result;
  result.reserve(size());
  for (const auto &plane : *this)
    result.push_back(plane);
  return result;
}

std::array<int, 6> DatasetShape::dimensions() const {
  return {t, c, z, y, x, s};
}
void DatasetShape::validate() const {
  checked_product(dimensions());
  if (s != 1 && s != 3 && s != 4)
    throw std::invalid_argument("Sample axis S must contain one, three or four "
                                "components; use C for scientific channels");
  if (x > std::numeric_limits<int>::max() - 8194 ||
      y > std::numeric_limits<int>::max() - 8194 ||
      z > std::numeric_limits<int>::max() - 8194)
    throw std::invalid_argument(
        "Spatial dimensions exceed supported image coordinates");
}
std::size_t DatasetShape::pixel_count() const {
  validate();
  return checked_product(dimensions());
}
std::size_t DatasetShape::plane_count() const {
  validate();
  return checked_product({t, c, z, 1, 1, 1});
}
int DatasetShape::size(char axis) const {
  const std::string axes = "TCZYXS";
  auto index = axes.find(axis);
  if (index == std::string::npos)
    throw std::invalid_argument("Unknown dataset axis; expected T,C,Z,Y,X,S");
  return dimensions()[index];
}
Plane parse_plane(const std::string &name) {
  if (name == "xy" || name == "XY")
    return Plane::XY;
  if (name == "xz" || name == "XZ")
    return Plane::XZ;
  if (name == "yz" || name == "YZ")
    return Plane::YZ;
  throw std::invalid_argument("Plane must be xy, xz or yz");
}
void SpatialCalibration::validate() const {
  for (double value : spacing) {
    require_finite(value, "Voxel spacing");
    if (value <= 0)
      throw std::invalid_argument("Voxel spacing must be positive");
  }
  for (double value : origin)
    require_finite(value, "Spatial origin");
  for (double value : direction)
    require_finite(value, "Spatial direction");
  double det = determinant(direction);
  if (!std::isfinite(det) || det == 0)
    throw std::invalid_argument("Spatial direction matrix must be invertible");
  require_finite(frame_interval, "Frame interval");
  require_finite(time_origin, "Time origin");
  if (frame_interval <= 0)
    throw std::invalid_argument("Frame interval must be positive");
  if (unit.find_first_not_of(" \t\r\n") == std::string::npos ||
      time_unit.find_first_not_of(" \t\r\n") == std::string::npos)
    throw std::invalid_argument("Calibration units must be nonempty");
}
std::array<double, 3>
SpatialCalibration::voxel_to_world(const std::array<double, 3> &voxel) const {
  validate();
  for (double value : voxel)
    require_finite(value, "Voxel coordinate");
  auto result =
      multiply(direction, {voxel[0] * spacing[0], voxel[1] * spacing[1],
                           voxel[2] * spacing[2]});
  for (int i = 0; i < 3; ++i) {
    result[i] += origin[i];
    require_finite(result[i], "World coordinate");
  }
  return result;
}
std::array<double, 3>
SpatialCalibration::world_to_voxel(const std::array<double, 3> &world) const {
  validate();
  for (double value : world)
    require_finite(value, "World coordinate");
  const auto &m = direction;
  std::array<double, 9> inverse{
      m[4] * m[8] - m[5] * m[7], m[2] * m[7] - m[1] * m[8],
      m[1] * m[5] - m[2] * m[4], m[5] * m[6] - m[3] * m[8],
      m[0] * m[8] - m[2] * m[6], m[2] * m[3] - m[0] * m[5],
      m[3] * m[7] - m[4] * m[6], m[1] * m[6] - m[0] * m[7],
      m[0] * m[4] - m[1] * m[3]};
  double det = determinant(m);
  for (double &value : inverse)
    value /= det;
  auto result = multiply(inverse, {world[0] - origin[0], world[1] - origin[1],
                                   world[2] - origin[2]});
  for (int i = 0; i < 3; ++i) {
    result[i] /= spacing[i];
    require_finite(result[i], "Voxel coordinate");
  }
  return result;
}
double SpatialCalibration::time(int frame) const {
  validate();
  double value = time_origin + frame * frame_interval;
  require_finite(value, "Calibrated time");
  return value;
}

Dataset::Dataset(std::shared_ptr<const PlaneSource> source,
                 SpatialCalibration calibration,
                 FloatingSamplePolicy sample_policy)
    : source_(std::move(source)), calibration_(std::move(calibration)),
      sample_policy_(sample_policy) {
  if (!source_)
    throw std::invalid_argument("Dataset source must not be null");
  validate_sample_policy(sample_policy_);
  source_->shape().validate();
  calibration_.validate();
  const auto admitted_policy = eager_sample_policy(source_);
  if (admitted_policy && *admitted_policy != sample_policy_)
    throw std::invalid_argument("Dataset policy differs from its immutable source table");
  if ((sample_policy_ == FloatingSamplePolicy::PreserveQuietNaNFloat32 || sample_policy_ == FloatingSamplePolicy::PreserveIEEEFloat32) &&
      (!admitted_policy || source_->pixel_type() != PixelType::Float32 ||
       source_->shape().s != 1))
    throw std::invalid_argument(sample_policy_ == FloatingSamplePolicy::PreserveIEEEFloat32
        ? "IEEE Float32 policy requires a reviewed eager grayscale source"
        : "Quiet-NaN policy requires a reviewed eager Float32 source");
}
Dataset Dataset::from_planes(DatasetShape shape, Stack planes,
                             SpatialCalibration calibration,
                             FloatingSamplePolicy sample_policy) {
  return from_shared_planes(shape, SharedPlanes(std::move(planes), sample_policy),
                            std::move(calibration));
}
Dataset Dataset::from_shared_planes(DatasetShape shape, SharedPlanes planes,
                                    SpatialCalibration calibration) {
  const auto policy = planes.sample_policy();
  return Dataset(std::make_shared<MemorySource>(shape, std::move(planes)),
                 std::move(calibration), policy);
}
Dataset Dataset::from_stack(Stack frames, SpatialCalibration calibration,
                            FloatingSamplePolicy sample_policy) {
  if (frames.empty() ||
      frames.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    throw std::invalid_argument(
        "Stack must contain an addressable number of planes");
  DatasetShape shape{1,
                     1,
                     static_cast<int>(frames.size()),
                     frames[0].height,
                     frames[0].width,
                     frames[0].channels};
  return from_planes(shape, std::move(frames), std::move(calibration),
                     sample_policy);
}
DatasetShape Dataset::shape() const { return source_->shape(); }
PixelType Dataset::pixel_type() const { return source_->pixel_type(); }
const SpatialCalibration &Dataset::calibration() const { return calibration_; }
std::shared_ptr<const Image> Dataset::plane(PlanePosition position) const {
  validate_position(shape(), position);
  auto result = source_->read_plane(position);
  if (!result)
    throw std::runtime_error("Dataset source returned a null plane");
  validate_plane(*result, shape(), pixel_type(), false);
  return result;
}
Image PlaneSource::read_region(PlanePosition position, int x, int y, int width,
                               int height) const {
  validate_position(shape(), position);
  auto image = read_plane(position);
  if (!image)
    throw std::runtime_error("Dataset source returned a null plane");
  validate_plane(*image, shape(), pixel_type(), false);
  return crop(*image, x, y, width, height);
}
Image Dataset::region(PlanePosition position, int x, int y, int width,
                      int height) const {
  auto sizes = shape();
  validate_position(sizes, position);
  if (x < 0 || y < 0 || x >= sizes.x || y >= sizes.y || width <= 0 ||
      height <= 0 || width > sizes.x - x || height > sizes.y - y)
    throw std::out_of_range("Dataset region is outside the XY plane");
  auto result = source_->read_region(position, x, y, width, height);
  sizes.x = width;
  sizes.y = height;
  validate_plane(result, sizes, pixel_type(), false);
  return result;
}
Image Dataset::extract_plane(Plane view, int coordinate, int t, int c) const {
  auto sizes = shape();
  validate_coordinate(sizes, view, coordinate);
  validate_position(sizes, {t, c, 0});
  kernel_checkpoint();
  if (view == Plane::XY) {
    auto input = plane({t, c, coordinate});
    kernel_checkpoint();
    Image result(sizes.x, sizes.y, sizes.s, pixel_type());
    kernel_checkpoint();
    kernel_blocks("plane-extract.copy", result.pixels.size(),
                  [&](std::size_t first, std::size_t last) {
                    std::copy(input->pixels.begin() + first,
                              input->pixels.begin() + last,
                              result.pixels.begin() + first);
                  });
    kernel_checkpoint();
    return result;
  }
  Image result(view == Plane::XZ ? sizes.x : sizes.y, sizes.z, sizes.s,
               pixel_type());
  kernel_checkpoint();
  execution_progress(0, sizes.z);
  for (int z = 0; z < sizes.z; ++z) {
    kernel_checkpoint();
    auto input = view == Plane::XZ
                     ? region({t, c, z}, 0, coordinate, sizes.x, 1)
                     : region({t, c, z}, coordinate, 0, 1, sizes.y);
    kernel_checkpoint();
    const auto strip_samples = std::size_t(result.width) * sizes.s;
    const auto output_offset = std::size_t(z) * strip_samples;
    kernel_blocks("plane-extract.copy", strip_samples,
                  [&](std::size_t first, std::size_t last) {
                    // Both regional layouts flatten to the output row's
                    // horizontal/sample order, including a YZ width-1 strip.
                    std::copy(input.pixels.begin() + first,
                              input.pixels.begin() + last,
                              result.pixels.begin() + output_offset + first);
                  });
    execution_progress(std::size_t(z) + 1, sizes.z);
    kernel_checkpoint();
  }
  return result;
}
PlaneGeometry Dataset::plane_geometry(Plane view, int coordinate) const {
  validate_coordinate(shape(), view, coordinate);
  int horizontal = view == Plane::YZ ? 1 : 0;
  int vertical = view == Plane::XY ? 1 : 2;
  int fixed = view == Plane::XY ? 2 : view == Plane::XZ ? 1 : 0;
  std::array<double, 3> voxel{0, 0, 0};
  voxel[fixed] = coordinate;
  auto origin = calibration_.voxel_to_world(voxel);
  auto h = voxel, v = voxel;
  h[horizontal] += 1;
  v[vertical] += 1;
  auto horizontal_step = calibration_.voxel_to_world(h),
       vertical_step = calibration_.voxel_to_world(v);
  for (int i = 0; i < 3; ++i) {
    horizontal_step[i] -= origin[i];
    vertical_step[i] -= origin[i];
  }
  const std::array<std::string, 3> names{"X", "Y", "Z"};
  return {origin,          horizontal_step,  vertical_step, names[horizontal],
          names[vertical], calibration_.unit};
}
Dataset Dataset::with_calibration(SpatialCalibration calibration) const {
  return Dataset(source_, std::move(calibration), sample_policy_);
}
Dataset Dataset::observe_reads(const DatasetReadObserver &observer) const {
  auto source = source_;
  for (;;) {
    if (auto observed = std::dynamic_pointer_cast<const ObservedSource>(source)) {
      if (observed->state() == observer.state_)
        return *this;
      source = observed->source();
    } else if (auto guarded = std::dynamic_pointer_cast<const GuardedSource>(source))
      source = guarded->source();
    else
      break;
  }
  return Dataset(std::make_shared<ObservedSource>(source_, observer.state_),
                 calibration_, sample_policy_);
}
Dataset Dataset::with_plane(PlanePosition position, Image replacement) const {
  if (auto observed = std::dynamic_pointer_cast<const ObservedSource>(source_)) {
    auto edited = Dataset(observed->source(), calibration_, sample_policy_)
        .with_plane(position, std::move(replacement));
    return Dataset(std::make_shared<ObservedSource>(edited.source_, observed->state()),
                   calibration_, sample_policy_);
  }
  if (auto guarded = std::dynamic_pointer_cast<const GuardedSource>(source_))
    return Dataset(guarded->source(), calibration_, sample_policy_)
        .with_plane(position, std::move(replacement))
        .retain_resources(guarded->guard());
  validate_position(shape(), position);
  validate_plane(replacement, shape(), pixel_type(), false);
  replacement = prepare_owned_storage(std::move(replacement), sample_policy_);
  validate_plane(replacement, shape(), pixel_type(), true, sample_policy_);
  if (auto memory = std::dynamic_pointer_cast<const MemorySource>(source_))
    return Dataset(memory->replace(position, std::move(replacement)),
                   calibration_, sample_policy_);
  return Dataset(std::make_shared<OverlaySource>(source_, position,
                                                 std::move(replacement)),
                 calibration_, sample_policy_);
}
Dataset Dataset::with_planes(
    std::vector<std::pair<PlanePosition, Image>> replacements) const {
  if (auto observed = std::dynamic_pointer_cast<const ObservedSource>(source_)) {
    if (replacements.empty())
      return *this;
    auto edited = Dataset(observed->source(), calibration_, sample_policy_)
        .with_planes(std::move(replacements));
    return Dataset(std::make_shared<ObservedSource>(edited.source_, observed->state()),
                   calibration_, sample_policy_);
  }
  if (auto guarded = std::dynamic_pointer_cast<const GuardedSource>(source_))
    return Dataset(guarded->source(), calibration_, sample_policy_)
        .with_planes(std::move(replacements))
        .retain_resources(guarded->guard());
  if (replacements.empty())
    return *this;
  std::map<std::size_t, bool> seen;
  for (auto &edit : replacements) {
    validate_position(shape(), edit.first);
    validate_plane(edit.second, shape(), pixel_type(), false);
    edit.second = prepare_owned_storage(std::move(edit.second), sample_policy_);
    validate_plane(edit.second, shape(), pixel_type(), true, sample_policy_);
    if (!seen.emplace(plane_index(shape(), edit.first), true).second)
      throw std::invalid_argument("Replacement positions must be unique");
  }
  if (auto memory = std::dynamic_pointer_cast<const MemorySource>(source_))
    return Dataset(memory->replace(std::move(replacements)), calibration_,
                   sample_policy_);
  return Dataset(
      std::make_shared<OverlaySource>(source_, std::move(replacements)),
      calibration_, sample_policy_);
}
Image stack_plane(const Stack &frames, Plane view, int coordinate) {
  return Dataset(std::make_shared<BorrowedStackSource>(frames))
      .extract_plane(view, coordinate);
}
bool Dataset::shares_pixels(const Dataset &other) const {
  return detail::source_identity(source_.get()) ==
         detail::source_identity(other.source_.get());
}
bool Dataset::has_eager_planes() const {
  if (auto observed=std::dynamic_pointer_cast<const ObservedSource>(source_))
    return Dataset(observed->source(),calibration_,sample_policy_).has_eager_planes();
  if (auto guarded=std::dynamic_pointer_cast<const GuardedSource>(source_))
    return Dataset(guarded->source(),calibration_,sample_policy_).has_eager_planes();
  return bool(std::dynamic_pointer_cast<const MemorySource>(source_));
}
std::optional<SharedPlanes> Dataset::shared_planes() const {
  if (auto observed = std::dynamic_pointer_cast<const ObservedSource>(source_))
    return Dataset(observed->source(), calibration_, sample_policy_).shared_planes();
  if (auto guarded = std::dynamic_pointer_cast<const GuardedSource>(source_)) {
    auto table = Dataset(guarded->source(), calibration_, sample_policy_).shared_planes();
    if (table)
      for (std::size_t i = 0; i < table->size(); ++i)
        detail::ResourceAccess::rebind(
            *table, i, detail::guard_image(table->shared(i), guarded->guard()));
    return table;
  }
  if (auto source = std::dynamic_pointer_cast<const MemorySource>(source_))
    return source->shared_planes();
  return std::nullopt;
}
Dataset Dataset::retain_resources(std::shared_ptr<const void> guard) const {
  if (!guard)
    return *this;
  return Dataset(std::make_shared<GuardedSource>(source_, std::move(guard)),
                 calibration_, sample_policy_);
}
Dataset Dataset::detach_resources() const {
  return Dataset(detail::detach_source(source_), calibration_, sample_policy_);
}
} // namespace imagejxx
