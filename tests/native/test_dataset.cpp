#include <cmath>
#include <imagejxx/dataset.hpp>
#include <iostream>
#include <stdexcept>

using namespace imagejxx;
namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class F> void rejects(F function) {
  bool rejected = false;
  try {
    function();
  } catch (const std::exception &) {
    rejected = true;
  }
  check(rejected, "Invalid dataset input was accepted");
}
class CountingSource : public PlaneSource {
public:
  mutable int reads = 0;
  DatasetShape shape() const override { return {2, 2, 3, 4, 5, 1}; }
  PixelType pixel_type() const override { return PixelType::Float64; }
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    ++reads;
    auto image = std::make_shared<Image>(5, 4);
    for (int y = 0; y < 4; ++y)
      for (int x = 0; x < 5; ++x)
        image->at(x, y) = 10000 * p.t + 1000 * p.c + 100 * p.z + 10 * y + x;
    return image;
  }
};
class RegionalSource : public PlaneSource {
public:
  mutable int regionReads = 0, requestedPixels = 0;
  DatasetShape shape() const override { return {1, 1, 3, 40000, 5, 1}; }
  PixelType pixel_type() const override { return PixelType::Float64; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    throw std::runtime_error("This source must be queried by region");
  }
  Image read_region(PlanePosition p, int x, int y, int width,
                    int height) const override {
    ++regionReads;
    requestedPixels += width * height;
    Image image(width, height);
    for (int j = 0; j < height; ++j)
      for (int i = 0; i < width; ++i)
        image.at(i, j) = 100 * p.z + 10 * (y + j) + x + i;
    return image;
  }
};
} // namespace
int main() {
  try {
    auto source = std::make_shared<CountingSource>();
    Dataset dataset(source);
    check(!dataset.has_eager_planes() && !dataset.observe_reads(DatasetReadObserver{}).has_eager_planes(),"Virtual/observed discovery must stay lazy");
    check(source->reads == 0, "Metadata inspection must not load pixels");
    check(dataset.shape().pixel_count() == 240, "Explicit axis sizes");
    auto xy = dataset.extract_plane(Plane::XY, 2, 1, 1);
    check(xy.width == 5 && xy.height == 4 && xy.at(3, 2) == 11223,
          "XY coordinates");
    check(source->reads == 1, "XY should request exactly one source plane");
    auto xz = dataset.extract_plane(Plane::XZ, 2, 1, 1);
    check(xz.width == 5 && xz.height == 3 && xz.at(3, 2) == 11223,
          "XZ coordinates");
    check(source->reads == 4, "XZ should request only Z planes");
    auto regional = std::make_shared<RegionalSource>();
    Dataset tiled(regional);
    auto strip = tiled.extract_plane(Plane::XZ, 30000);
    check(strip.at(4, 2) == 300204 && regional->regionReads == 3 &&
              regional->requestedPixels == 15,
          "Orthogonal extraction should request only bounded rows");
    auto patched = tiled.with_plane({0, 0, 0}, Image(5, 40000));
    auto partial = patched.region({0, 0, 2}, 1, 30000, 2, 1);
    check(partial.at(1, 0) == 300202 && regional->requestedPixels == 17,
          "Immutable edits retain lazy region reads for unedited planes");
    auto yz = dataset.extract_plane(Plane::YZ, 3, 1, 1);
    check(yz.width == 4 && yz.height == 3 && yz.at(2, 2) == 11223,
          "YZ coordinates");
    rejects([&] { dataset.extract_plane(Plane::XY, 3); });
    rejects([&] { dataset.extract_plane(Plane::XZ, -1); });
    rejects([&] { dataset.extract_plane(static_cast<Plane>(99), 0); });
    rejects([&] { dataset.plane({0, 2, 0}); });
    rejects([&] { dataset.region({}, 4, 2, 2, 1); });
    rejects([&] { dataset.region({}, 0, 0, 0, 1); });
    rejects([] { DatasetShape{1, 1, 1, 2, 3, 2}.validate(); });
    rejects([] { DatasetShape{0, 1, 1, 2, 3, 1}.validate(); });
    rejects([] { Dataset(nullptr); });
    SharedPlanes shared(Stack{Image(5, 4), Image(5, 4), Image(5, 4)});
    auto sharedCopy = shared;
    auto sourceView = Dataset::from_shared_planes({1, 1, 3, 4, 5, 1}, shared);
    check(sourceView.plane({0, 0, 1}) == shared.shared(1),
          "Dataset and editable plane tables share the same immutable storage");
    sharedCopy.replace(1, fill(Image(5, 4), 7));
    check(sharedCopy.shared(0) == shared.shared(0) &&
              sharedCopy[1].at(0, 0) == 7 && shared[1].at(0, 0) == 0,
          "Plane table edits never mutate shared buffers");
    auto subset = shared.subset({2, 0, 2});
    subset.insert_shared(1, subset, 2);
    subset.erase(0);
    check(subset.shared(0) == shared.shared(2) &&
              subset.shared(1) == shared.shared(0),
          "Subset/insertion/deletion share referenced planes");
    auto detached = shared.materialize();
    detached[0].at(0, 0) = 88;
    check(shared[0].at(0, 0) == 0, "Legacy stack materialization is detached");
    rejects([&] { shared.replace(8, Image(5, 4)); });
    rejects([&] { shared.insert(8, Image(5, 4)); });
    rejects([&] { shared.subset({3}); });
    auto invalid = Image(5, 4);
    invalid.pixels.pop_back();
    rejects([&] { shared.replace(0, invalid); });
    check(shared.shared(0) == sourceView.plane(),
          "Failed plane admission is atomic");
    Stack frames{Image(5, 4), Image(5, 4)};
    frames[1].at(3, 2) = 42;
    auto eager = Dataset::from_stack(std::move(frames));
    check(eager.has_eager_planes() && eager.observe_reads(DatasetReadObserver{}).has_eager_planes(),"Eager/observed storage discovery");
    auto old = eager.plane({0, 0, 1});
    Image replacement(5, 4);
    replacement.at(3, 2) = 77;
    auto changed = eager.with_plane({0, 0, 1}, replacement);
    check(changed.plane({0, 0, 0}).get() == eager.plane({0, 0, 0}).get(),
          "Unedited planes share storage");
    check(eager.plane({0, 0, 1})->at(3, 2) == 42 && old->at(3, 2) == 42,
          "History retains immutable pixels");
    check(changed.plane({0, 0, 1})->at(3, 2) == 77, "Plane replacement");
    rejects([&] { eager.with_plane({0, 0, 0}, Image(4, 4)); });
    SpatialCalibration cal;
    cal.spacing = {.5, 2, 3};
    cal.origin = {10, 20, 30};
    cal.unit = "um";
    cal.direction = {0, -1, 0, 1, 0, 0, 0, 0, 1};
    cal.frame_interval = .25;
    cal.time_origin = 2;
    auto world = cal.voxel_to_world({3, 2, 1});
    check(world == std::array<double, 3>{6, 21.5, 33},
          "Calibrated world coordinates");
    auto voxel = cal.world_to_voxel(world);
    check(voxel == std::array<double, 3>{3, 2, 1}, "World coordinate inverse");
    check(cal.time(4) == 3, "Calibrated time");
    SpatialCalibration small;
    small.direction = {1e-5, 0, 0, 0, 1e-5, 0, 0, 0, 1e-5};
    auto tinyRoundtrip = small.world_to_voxel(small.voxel_to_world({3, 2, 1}));
    check(std::abs(tinyRoundtrip[0] - 3) < 1e-12,
          "Small invertible spatial transforms");
    auto geometry = dataset.with_calibration(cal).plane_geometry(Plane::XZ, 2);
    check(geometry.origin == std::array<double, 3>{6, 20, 30},
          "Plane physical origin");
    check(geometry.horizontal_step == std::array<double, 3>{0, .5, 0} &&
              geometry.vertical_step == std::array<double, 3>{0, 0, 3},
          "Plane physical basis");
    rejects([] {
      SpatialCalibration c;
      c.direction.fill(0);
      c.validate();
    });
    rejects([] {
      SpatialCalibration c;
      c.spacing[0] = 0;
      c.validate();
    });
    auto calibrated = eager.with_calibration(cal);
    check(calibrated.plane().get() == eager.plane().get(),
          "Calibration changes share pixels");
    std::weak_ptr<const Image> retired;
    auto independent = [&] {
      auto initial = Dataset::from_stack(Stack{Image(2, 2)});
      retired = initial.plane();
      return initial.with_plane({}, Image(2, 2));
    }();
    check(retired.expired() && independent.plane()->width == 2,
          "Dropping history releases replaced eager planes");
    auto borrowed = stack_plane(Stack{xy, xy}, Plane::YZ, 3);
    check(borrowed.at(2, 1) == 11223,
          "Borrowed stack plane survives its input lifetime");
    std::cout << "Axis-aware dataset checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
