#pragma once

#include <imagejxx/image.hpp>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <utility>

namespace imagejxx {
class PreparedOperation;
class DocumentTransaction;
namespace detail { struct ResourceAccess; struct DatasetReadCounterState; }

// Canonical storage axes: time, scientific channel, Z, Y, X, interleaved
// sample. A scientific channel is independent of an RGB/RGBA sample component.
struct DatasetShape {
  int t = 1, c = 1, z = 1, y = 1, x = 1, s = 1;
  void validate() const;
  int size(char axis) const;
  std::array<int, 6> dimensions() const;
  std::size_t plane_count() const;
  std::size_t pixel_count() const;
};

struct PlanePosition {
  int t = 0, c = 0, z = 0;
};

// Explicit immutable-source admission. Typed Float32 policies require reviewed
// owned grayscale storage. Processing/display admission is separately qualified.
// PreserveIEEEFloat32 transports every binary32 pattern without quieting a
// signaling NaN or changing its sign/payload; it never implies an operation can
// process those values. The two established policies retain their contracts.
enum class FloatingSamplePolicy {
  FiniteOnly, PreserveQuietNaNFloat32, PreserveIEEEFloat32
};

enum class Plane { XY, XZ, YZ };
Plane parse_plane(const std::string &name);

// World coordinate of voxel (0,0,0) is origin. Integer voxel coordinates refer
// to voxel centers; axis-aligned ROI cell edges lie at half-integer
// coordinates. direction is row-major and maps spacing-scaled XYZ vectors into
// world space.
struct SpatialCalibration {
  std::array<double, 3> spacing{1, 1, 1}, origin{0, 0, 0};
  std::array<double, 9> direction{1, 0, 0, 0, 1, 0, 0, 0, 1};
  std::string unit = "px", time_unit = "s";
  double frame_interval = 1, time_origin = 0;
  void validate() const;
  std::array<double, 3>
  voxel_to_world(const std::array<double, 3> &voxel) const;
  std::array<double, 3>
  world_to_voxel(const std::array<double, 3> &world) const;
  double time(int frame) const;
};

struct PlaneGeometry {
  std::array<double, 3> origin, horizontal_step, vertical_step;
  std::string horizontal_axis, vertical_axis, unit;
};

// Editable plane table with immutable pixel buffers. Copying the table shares
// pixels; replacing one entry never changes readers or earlier history states.
// materialize() is the explicit adapter for legacy eager Stack algorithms.
class SharedPlanes {
public:
  SharedPlanes() = default;
  SharedPlanes(Stack planes,
               FloatingSamplePolicy sample_policy = FloatingSamplePolicy::FiniteOnly);
  FloatingSamplePolicy sample_policy() const { return sample_policy_; }
  std::size_t size() const { return planes_.size(); }
  bool empty() const { return planes_.empty(); }
  const Image &operator[](std::size_t index) const;
  std::shared_ptr<const Image> shared(std::size_t index) const;
  void replace(std::size_t index, Image plane);
  void replace_shared(std::size_t index, std::shared_ptr<const Image> plane);
  void insert(std::size_t index, Image plane);
  void insert_shared(std::size_t index, const SharedPlanes &source,
                     std::size_t sourceIndex);
  void erase(std::size_t index);
  SharedPlanes subset(const std::vector<std::size_t> &indices) const;
  Stack materialize() const;

  class const_iterator {
  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = Image;
    using difference_type = std::ptrdiff_t;
    using pointer = const Image *;
    using reference = const Image &;
    const_iterator() = default;
    reference operator*() const { return **position_; }
    pointer operator->() const { return position_->get(); }
    const_iterator &operator++() {
      ++position_;
      return *this;
    }
    const_iterator operator++(int) {
      auto old = *this;
      ++*this;
      return old;
    }
    bool operator==(const const_iterator &other) const {
      return position_ == other.position_;
    }
    bool operator!=(const const_iterator &other) const {
      return !(*this == other);
    }

  private:
    friend class SharedPlanes;
    explicit const_iterator(
        std::vector<std::shared_ptr<const Image>>::const_iterator position)
        : position_(position) {}
    std::vector<std::shared_ptr<const Image>>::const_iterator position_;
  };
  const_iterator begin() const { return const_iterator(planes_.begin()); }
  const_iterator end() const { return const_iterator(planes_.end()); }

private:
  friend struct detail::ResourceAccess;
  std::vector<std::shared_ptr<const Image>> planes_;
  FloatingSamplePolicy sample_policy_ = FloatingSamplePolicy::FiniteOnly;
};

// Sources can be eager, virtual, cached or tiled. A returned plane is immutable
// and retains its storage lifetime even if the source evicts it from a cache.
class PlaneSource {
public:
  virtual ~PlaneSource() = default;
  virtual DatasetShape shape() const = 0;
  virtual PixelType pixel_type() const = 0;
  virtual std::shared_ptr<const Image>
  read_plane(PlanePosition position) const = 0;
  // Virtual/tiled readers can serve a bounded region without materializing the
  // full XY plane. The default adapts existing immutable plane sources.
  virtual Image read_region(PlanePosition position, int x, int y, int width,
                            int height) const;
};

struct DatasetReadCounts {
  std::uint64_t plane_reads = 0, region_reads = 0;
  bool overflowed = false;
};

// Opt-in counts of calls entering an observed provider boundary, including
// provider failures. These are not disk/cache/allocator measurements. Copies
// observe the same counters and never retain a dataset or its resource leases.
class DatasetReadObserver {
public:
  DatasetReadObserver();
  DatasetReadObserver(const DatasetReadObserver &) = default;
  DatasetReadObserver &operator=(const DatasetReadObserver &) = default;
  // Counters saturate rather than wrap. Read after synchronous completion or
  // worker join for an exact pair; concurrent snapshots are not transactional.
  DatasetReadCounts counts() const noexcept;

private:
  friend class Dataset;
  std::shared_ptr<detail::DatasetReadCounterState> state_;
};

class Dataset {
public:
  // The quiet-NaN policy is available only for reviewed eager sources and their
  // internal wrappers. Unknown virtual sources cannot opt in through this API.
  explicit Dataset(std::shared_ptr<const PlaneSource> source,
                   SpatialCalibration calibration = {},
                   FloatingSamplePolicy sample_policy = FloatingSamplePolicy::FiniteOnly);
  static Dataset from_planes(DatasetShape shape, Stack planes,
                             SpatialCalibration calibration = {},
                             FloatingSamplePolicy sample_policy = FloatingSamplePolicy::FiniteOnly);
  // A shared table already carries its explicit admission policy; importing it
  // preserves that policy without scanning or silently downgrading its samples.
  static Dataset from_shared_planes(DatasetShape shape, SharedPlanes planes,
                                    SpatialCalibration calibration = {});
  static Dataset from_stack(Stack frames, SpatialCalibration calibration = {},
                            FloatingSamplePolicy sample_policy = FloatingSamplePolicy::FiniteOnly);
  FloatingSamplePolicy sample_policy() const { return sample_policy_; }
  DatasetShape shape() const;
  PixelType pixel_type() const;
  const SpatialCalibration &calibration() const;
  std::shared_ptr<const Image> plane(PlanePosition position = {}) const;
  Image region(PlanePosition position, int x, int y, int width,
               int height) const;
  Image extract_plane(Plane view, int coordinate, int t = 0, int c = 0) const;
  PlaneGeometry plane_geometry(Plane view, int coordinate) const;
  // Shares storage/calibration and preserves source policy. Reapplying the
  // same observer through our transparent wrappers does not count twice.
  // Construction, metadata and edits acquire no pixels. A provider's internal
  // fallback reads are not separate entries into this observed boundary.
  Dataset observe_reads(const DatasetReadObserver &observer) const;
  Dataset with_calibration(SpatialCalibration calibration) const;
  Dataset with_plane(PlanePosition position, Image replacement) const;
  // Validate all edits first, then copy one plane table/overlay map. Duplicate
  // positions are rejected; unchanged buffers and virtual sources stay shared.
  Dataset
  with_planes(std::vector<std::pair<PlanePosition, Image>> replacements) const;
  bool shares_pixels(const Dataset &other) const;
  // Eager adapter only. A virtual source returns nullopt without reading
  // pixels.
  std::optional<SharedPlanes> shared_planes() const;

private:
  friend class PreparedOperation;
  friend class DocumentTransaction;
  friend struct detail::ResourceAccess;
  Dataset retain_resources(std::shared_ptr<const void> guard) const;
  Dataset detach_resources() const;
  std::shared_ptr<const PlaneSource> source_;
  SpatialCalibration calibration_;
  FloatingSamplePolicy sample_policy_ = FloatingSamplePolicy::FiniteOnly;
};

// Samples only the requested plane; it does not copy the input stack. The
// borrowed source is confined to this synchronous call and never escapes it.
Image stack_plane(const Stack &frames, Plane view, int coordinate);

} // namespace imagejxx
