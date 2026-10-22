#include "binding_arrays.hpp"
#include <imagejxx/dataset.hpp>
#include <imagejxx/operations.hpp>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <type_traits>

namespace py = pybind11;
using namespace imagejxx;
namespace {
const std::string canonical_axes = "TCZYXS";
std::vector<int> axis_indices(const std::string &axes) {
  if (axes.empty() || axes.size() > 6)
    throw std::invalid_argument("Specify unique axes from TCZYXS");
  std::vector<int> indices;
  for (char axis : axes) {
    auto i = canonical_axes.find(axis);
    if (i == std::string::npos ||
        std::find(indices.begin(), indices.end(), static_cast<int>(i)) !=
            indices.end())
      throw std::invalid_argument("Specify unique uppercase axes from TCZYXS");
    indices.push_back(static_cast<int>(i));
  }
  if (axes.find('X') == std::string::npos ||
      axes.find('Y') == std::string::npos)
    throw std::invalid_argument("Dataset axes must contain X and Y");
  return indices;
}
PixelType array_type(const py::array &array) {
  auto kind = py::str(array.dtype().attr("kind")).cast<std::string>();
  auto size = array.itemsize();
  if (kind == "u" && size == 1)
    return PixelType::UInt8;
  if (kind == "u" && size == 2)
    return PixelType::UInt16;
  if (kind == "f" && size == 4)
    return PixelType::Float32;
  if (kind == "f" && size == 8)
    return PixelType::Float64;
  throw std::invalid_argument(
      "Dataset pixels require uint8, uint16, float32 or float64");
}
std::string type_name(PixelType type) {
  if (type == PixelType::UInt8)
    return "uint8";
  if (type == PixelType::UInt16)
    return "uint16";
  if (type == PixelType::Float32)
    return "float32";
  return "float64";
}
// Inspect original ndarray storage, never a float-to-double forcecast. NumPy
// views may have negative/zero strides, unaligned data, or opposite byte order.
std::uint32_t source_bits(const char *address, bool native_byte_order) {
  std::uint32_t bits;
  std::memcpy(&bits, address, sizeof(bits));
  if (!native_byte_order)
    bits = ((bits & 0x000000ffu) << 24) | ((bits & 0x0000ff00u) << 8) |
           ((bits & 0x00ff0000u) >> 8) | ((bits & 0xff000000u) >> 24);
  return bits;
}
void validate_byte_offsets(const py::buffer_info &buffer) {
  py::ssize_t minimum = 0, maximum = 0;
  constexpr auto lo = std::numeric_limits<py::ssize_t>::min();
  constexpr auto hi = std::numeric_limits<py::ssize_t>::max();
  for (std::size_t i = 0; i < buffer.shape.size(); ++i) {
    const auto count = buffer.shape[i] - 1, stride = buffer.strides[i];
    if (count == 0 || stride == 0)
      continue;
    if (stride > 0) {
      if (count > hi / stride || maximum > hi - count * stride)
        throw std::invalid_argument("Dataset byte offsets exceed addressable storage");
      maximum += count * stride;
    } else {
      if (stride < lo / count || minimum < lo - count * stride)
        throw std::invalid_argument("Dataset byte offsets exceed addressable storage");
      minimum += count * stride;
    }
  }
  if (maximum > hi - static_cast<py::ssize_t>(sizeof(std::uint32_t) - 1))
    throw std::invalid_argument("Dataset sample bytes exceed addressable storage");
}
Dataset read_dataset(const py::array &array, const std::string &axes,
                     SpatialCalibration calibration,
                     FloatingSamplePolicy sample_policy = FloatingSamplePolicy::FiniteOnly) {
  if (sample_policy != FloatingSamplePolicy::FiniteOnly &&
      sample_policy != FloatingSamplePolicy::PreserveQuietNaNFloat32 &&
      sample_policy != FloatingSamplePolicy::PreserveIEEEFloat32)
    throw std::invalid_argument("Invalid floating sample policy");
  auto indices = axis_indices(axes);
  if (static_cast<std::size_t>(array.ndim()) != axes.size())
    throw std::invalid_argument(
        "Axis labels must match the array dimensionality");
  auto type = array_type(array);
  std::array<int, 6> sizes{1, 1, 1, 1, 1, 1};
  std::array<std::size_t, 6> strides{};
  std::size_t stride = 1;
  for (int a = array.ndim() - 1; a >= 0; --a) {
    auto size = array.shape(a);
    if (size <= 0 || size > std::numeric_limits<int>::max())
      throw std::invalid_argument(
          "Dataset axes must be nonempty and fit int32");
    sizes[indices[a]] = static_cast<int>(size);
    strides[indices[a]] = stride;
    if (stride > std::numeric_limits<std::size_t>::max() /
                     static_cast<std::size_t>(size))
      throw std::invalid_argument("Dataset shape exceeds addressable storage");
    stride *= static_cast<std::size_t>(size);
  }
  DatasetShape shape{sizes[0], sizes[1], sizes[2],
                     sizes[3], sizes[4], sizes[5]};
  shape.validate();
  calibration.validate();
  if ((sample_policy == FloatingSamplePolicy::PreserveQuietNaNFloat32 || sample_policy == FloatingSamplePolicy::PreserveIEEEFloat32) &&
      (type != PixelType::Float32 || shape.s != 1))
    throw std::invalid_argument(sample_policy == FloatingSamplePolicy::PreserveIEEEFloat32
        ? "IEEE source policy requires grayscale Float32"
        : "Quiet-NaN source policy requires grayscale Float32");
  std::optional<py::buffer_info> buffer;
  std::array<py::ssize_t, 6> byte_strides{};
  bool native_byte_order = true;
  std::optional<py::array_t<double, py::array::c_style | py::array::forcecast>> values;
  if (type == PixelType::Float32) {
    buffer.emplace(array.request());
    validate_byte_offsets(*buffer);
    for (std::size_t a = 0; a < indices.size(); ++a)
      byte_strides[indices[a]] = buffer->strides[a];
    native_byte_order = array.dtype().attr("isnative").cast<bool>();
  } else {
    values.emplace(py::array_t<double, py::array::c_style | py::array::forcecast>::ensure(array));
    if (!*values)
      throw std::invalid_argument("Could not convert dataset pixels");
  }
  py::gil_scoped_release release;
  Stack planes;
  planes.reserve(shape.plane_count());
  for (int t = 0; t < shape.t; ++t)
    for (int c = 0; c < shape.c; ++c)
      for (int z = 0; z < shape.z; ++z) {
        Image plane(shape.x, shape.y, shape.s, type);
        std::size_t base = t * strides[0] + c * strides[1] + z * strides[2];
        const auto byte_base = t * byte_strides[0] + c * byte_strides[1] + z * byte_strides[2];
        for (int y = 0; y < shape.y; ++y)
          for (int x = 0; x < shape.x; ++x)
            for (int s = 0; s < shape.s; ++s) {
              if (type == PixelType::Float32) {
                const auto offset = byte_base + y * byte_strides[3] +
                                    x * byte_strides[4] + s * byte_strides[5];
                const auto address = static_cast<const char *>(buffer->ptr) + offset;
                plane.at(x, y, s) = float32_source_sample(
                    source_bits(address, native_byte_order), sample_policy);
              } else {
                plane.at(x, y, s) = values->data()[
                    base + y * strides[3] + x * strides[4] + s * strides[5]];
              }
            }
        planes.push_back(std::move(plane));
      }
  return Dataset::from_planes(shape, std::move(planes), std::move(calibration), sample_policy);
}
template <class T> T output_value(double value) {
  if constexpr (std::is_integral_v<T>)
    value = std::floor(
        std::clamp(value, 0.0,
                   static_cast<double>(std::numeric_limits<T>::max())) +
        0.5);
  return static_cast<T>(value);
}
template <class T>
void write_source_output(T *destination, double value, FloatingSamplePolicy policy) {
  if constexpr (std::is_same_v<T, float>) {
    if ((policy == FloatingSamplePolicy::PreserveQuietNaNFloat32 || policy == FloatingSamplePolicy::PreserveIEEEFloat32)) {
      const auto bits = float32_source_sample_bits(value, policy);
      std::memcpy(destination, &bits, sizeof(bits));
      return;
    }
  }
  // Legacy finite-only double-backed Float32 intermediates still round here.
  *destination = output_value<T>(value);
}
template <class T> py::array typed_plane(const Image &image, FloatingSamplePolicy policy) {
  std::vector<py::ssize_t> shape{image.height, image.width};
  if (image.channels > 1)
    shape.push_back(image.channels);
  py::array_t<T> result(shape);
  {
    py::gil_scoped_release release;
    for (std::size_t i = 0; i < image.pixels.size(); ++i)
      write_source_output(result.mutable_data() + i, image.pixels[i], policy);
  }
  return result;
}
py::array plane_array(const Image &image,
                      FloatingSamplePolicy policy = FloatingSamplePolicy::FiniteOnly) {
  if (image.type == PixelType::UInt8)
    return typed_plane<std::uint8_t>(image, policy);
  if (image.type == PixelType::UInt16)
    return typed_plane<std::uint16_t>(image, policy);
  if (image.type == PixelType::Float32)
    return typed_plane<float>(image, policy);
  return typed_plane<double>(image, policy);
}
template <class T>
py::array typed_dataset(const Dataset &dataset, const std::string &axes) {
  auto indices = axis_indices(axes);
  auto sizes = dataset.shape().dimensions();
  std::array<std::size_t, 6> strides{};
  std::vector<py::ssize_t> output_shape;
  std::size_t stride = 1;
  for (int i = static_cast<int>(indices.size()) - 1; i >= 0; --i) {
    strides[indices[i]] = stride;
    stride *= static_cast<std::size_t>(sizes[indices[i]]);
  }
  for (int a = 0; a < 6; ++a)
    if (sizes[a] > 1 && axes.find(canonical_axes[a]) == std::string::npos)
      throw std::invalid_argument(
          "Cannot omit a nonsingleton dataset axis from numpy output");
  for (int i : indices)
    output_shape.push_back(sizes[i]);
  py::array_t<T> output(output_shape);
  {
    py::gil_scoped_release release;
    for (int t = 0; t < sizes[0]; ++t)
      for (int c = 0; c < sizes[1]; ++c)
        for (int z = 0; z < sizes[2]; ++z) {
          auto image = dataset.plane({t, c, z});
          std::size_t base = t * strides[0] + c * strides[1] + z * strides[2];
          for (int y = 0; y < sizes[3]; ++y)
            for (int x = 0; x < sizes[4]; ++x)
              for (int s = 0; s < sizes[5]; ++s)
                write_source_output(
                    output.mutable_data() + base + y * strides[3] + x * strides[4] + s * strides[5],
                    image->at(x, y, s), dataset.sample_policy());
        }
  }
  return output;
}
py::array dataset_array(const Dataset &dataset, const std::string &axes) {
  if (dataset.pixel_type() == PixelType::UInt8)
    return typed_dataset<std::uint8_t>(dataset, axes);
  if (dataset.pixel_type() == PixelType::UInt16)
    return typed_dataset<std::uint16_t>(dataset, axes);
  if (dataset.pixel_type() == PixelType::Float32)
    return typed_dataset<float>(dataset, axes);
  return typed_dataset<double>(dataset, axes);
}
} // namespace

namespace imagejxx::bindings {
Image read_numpy_image(const py::array &pixels) {
  return read_numpy_image(pixels,FloatingSamplePolicy::FiniteOnly);
}
Image read_numpy_image(const py::array &pixels, FloatingSamplePolicy policy) {
  auto dataset = read_dataset(pixels, pixels.ndim() == 2 ? "YX" : "YXS", {},policy);
  return *dataset.plane();
}
py::array numpy_image(const Image &image) { return plane_array(image); }
py::array numpy_image(const Image &image, FloatingSamplePolicy policy) { return plane_array(image,policy); }
Image read_quiet_nan_numpy_image(const py::array &pixels) {
  auto dataset = read_dataset(pixels, pixels.ndim() == 2 ? "YX" : "YXS", {},
                             FloatingSamplePolicy::PreserveQuietNaNFloat32);
  return *dataset.plane();
}
py::array quiet_nan_numpy_image(const Image &image) {
  return plane_array(image, FloatingSamplePolicy::PreserveQuietNaNFloat32);
}
} // namespace imagejxx::bindings

void bind_dataset(py::module_ &m) {
  py::class_<DatasetReadCounts>(m, "DatasetReadCounts",
      "Detached provider-boundary call counts; not physical I/O or cache measurements.")
      .def_readonly("plane_reads", &DatasetReadCounts::plane_reads)
      .def_readonly("region_reads", &DatasetReadCounts::region_reads)
      .def_readonly("overflowed", &DatasetReadCounts::overflowed);
  py::class_<DatasetReadObserver>(m, "DatasetReadObserver",
      "Opt-in native call observer. Copies share counters, not datasets or leases.")
      .def(py::init<>())
      .def(py::init<const DatasetReadObserver &>(), py::arg("other"))
      .def("__copy__", [](const DatasetReadObserver &value) { return value; })
      .def("counts", &DatasetReadObserver::counts,
           "Saturating counts; exact after synchronous completion or worker join.");
  py::enum_<FloatingSamplePolicy>(m, "FloatingSamplePolicy",
                                 "Immutable source acceptance policy; processing remains finite-only.")
      .value("FINITE_ONLY", FloatingSamplePolicy::FiniteOnly)
      .value("PRESERVE_QUIET_NAN_FLOAT32", FloatingSamplePolicy::PreserveQuietNaNFloat32)
      .value("PRESERVE_IEEE_FLOAT32", FloatingSamplePolicy::PreserveIEEEFloat32);
  py::class_<SpatialCalibration>(m, "SpatialCalibration")
      .def(py::init([](std::array<double, 3> spacing,
                       std::array<double, 3> origin,
                       std::array<double, 9> direction, std::string unit,
                       double frame_interval, double time_origin,
                       std::string time_unit) {
             SpatialCalibration value;
             value.spacing = spacing;
             value.origin = origin;
             value.direction = direction;
             value.unit = std::move(unit);
             value.frame_interval = frame_interval;
             value.time_origin = time_origin;
             value.time_unit = std::move(time_unit);
             value.validate();
             return value;
           }),
           py::kw_only(), py::arg("spacing") = std::array<double, 3>{1, 1, 1},
           py::arg("origin") = std::array<double, 3>{0, 0, 0},
           py::arg("direction") =
               std::array<double, 9>{1, 0, 0, 0, 1, 0, 0, 0, 1},
           py::arg("unit") = "px", py::arg("frame_interval") = 1,
           py::arg("time_origin") = 0, py::arg("time_unit") = "s")
      .def_readonly("spacing", &SpatialCalibration::spacing)
      .def_readonly("origin", &SpatialCalibration::origin)
      .def_readonly("direction", &SpatialCalibration::direction)
      .def_readonly("unit", &SpatialCalibration::unit)
      .def_readonly("frame_interval", &SpatialCalibration::frame_interval)
      .def_readonly("time_origin", &SpatialCalibration::time_origin)
      .def_readonly("time_unit", &SpatialCalibration::time_unit)
      .def("voxel_to_world", &SpatialCalibration::voxel_to_world)
      .def("world_to_voxel", &SpatialCalibration::world_to_voxel)
      .def("time", &SpatialCalibration::time);
  py::class_<Dataset>(m, "Dataset")
      .def(py::init(&read_dataset), py::arg("data"), py::kw_only(),
           py::arg("axes"), py::arg("calibration") = SpatialCalibration{},
           py::arg("sample_policy") = FloatingSamplePolicy::FiniteOnly)
      .def_property_readonly("sample_policy", &Dataset::sample_policy)
      .def_property_readonly("has_eager_planes", &Dataset::has_eager_planes)
      .def_property_readonly("axes",
                             [](const Dataset &) { return canonical_axes; })
      .def_property_readonly("shape",
                             [](const Dataset &value) {
                               auto sizes = value.shape().dimensions();
                               py::tuple shape(6);
                               for (std::size_t i = 0; i < sizes.size(); ++i)
                                 shape[i] = py::int_(sizes[i]);
                               return shape;
                             })
      .def_property_readonly("axis_sizes",
                             [](const Dataset &value) {
                               py::dict result;
                               for (char axis : canonical_axes)
                                 result[py::str(std::string(1, axis))] =
                                     value.shape().size(axis);
                               return result;
                             })
      .def_property_readonly("dtype",
                             [](const Dataset &value) {
                               return py::dtype(type_name(value.pixel_type()));
                             })
      .def_property_readonly("calibration", &Dataset::calibration,
                             py::return_value_policy::copy)
      .def(
          "plane",
          [](const Dataset &value, const std::string &view, int coordinate,
             int t, int c) {
            auto image = [&] {
              py::gil_scoped_release release;
              return value.extract_plane(parse_plane(view), coordinate, t, c);
            }();
            return plane_array(image, value.sample_policy());
          },
          py::arg("view") = "xy", py::arg("coordinate") = 0, py::kw_only(),
          py::arg("t") = 0, py::arg("c") = 0)
      .def(
          "region",
          [](const Dataset &value, int x, int y, int width, int height, int t,
             int c, int z) {
            auto image = [&] {
              py::gil_scoped_release release;
              return value.region({t, c, z}, x, y, width, height);
            }();
            return plane_array(image, value.sample_policy());
          },
          py::arg("x"), py::arg("y"), py::arg("width"), py::arg("height"),
          py::kw_only(), py::arg("t") = 0, py::arg("c") = 0, py::arg("z") = 0)
      .def(
          "plane_geometry",
          [](const Dataset &value, const std::string &view, int coordinate) {
            auto geometry = value.plane_geometry(parse_plane(view), coordinate);
            py::dict result;
            result["origin"] = py::cast(geometry.origin);
            result["horizontal_step"] = py::cast(geometry.horizontal_step);
            result["vertical_step"] = py::cast(geometry.vertical_step);
            result["horizontal_axis"] = geometry.horizontal_axis;
            result["vertical_axis"] = geometry.vertical_axis;
            result["unit"] = geometry.unit;
            return result;
          },
          py::arg("view") = "xy", py::arg("coordinate") = 0)
      .def("shares_pixels", &Dataset::shares_pixels, py::arg("other"))
      .def("observe_reads", &Dataset::observe_reads, py::arg("observer"),
           "Share this dataset with an opt-in native provider-boundary observer. "
           "Metadata and construction acquire no pixels; invalid coordinates "
           "refused before provider entry are not counted.")
      .def("with_calibration", &Dataset::with_calibration,
           py::arg("calibration"))
      .def(
          "with_plane",
          [](const Dataset &value, const py::array &pixels, int t, int c,
             int z) {
            auto shape = value.shape();
            std::string axes = pixels.ndim() == 2 ? "YX" : "YXS";
            auto replacement = read_dataset(pixels, axes, value.calibration(), value.sample_policy());
            auto image = replacement.plane();
            if (image->width != shape.x || image->height != shape.y ||
                image->channels != shape.s)
              throw std::invalid_argument("Replacement plane shape mismatch");
            return value.with_plane({t, c, z}, *image);
          },
          py::arg("pixels"), py::kw_only(), py::arg("t") = 0, py::arg("c") = 0,
          py::arg("z") = 0)
      .def("numpy", &dataset_array, py::arg("axes") = "TCZYXS",
           "Materialize pixels in explicitly specified axis order; plane() "
           "copies only one plane.");
}

void bind_float32_sources(py::module_ &m) {
  const auto numpy = py::module_::import("numpy");
  const py::object numpy_bool = numpy.attr("bool_");
  const py::object numpy_float32 = numpy.attr("float32");
  const py::object numpy_float64 = numpy.attr("float64");
  m.def("float32_source_sample",
        [numpy_bool](const py::object &bits, FloatingSamplePolicy policy) {
          if (PyBool_Check(bits.ptr()) || py::isinstance(bits, numpy_bool) ||
              !PyIndex_Check(bits.ptr()))
            throw py::type_error("Float32 bits must be an integer index, excluding Boolean values");
          auto integer = py::reinterpret_steal<py::object>(PyNumber_Index(bits.ptr()));
          if (!integer)
            throw py::error_already_set();
          const auto raw = PyLong_AsUnsignedLongLong(integer.ptr());
          if (PyErr_Occurred())
            throw py::error_already_set();
          if (raw > std::numeric_limits<std::uint32_t>::max())
            throw std::invalid_argument("Float32 bits must fit uint32");
          return float32_source_sample(static_cast<std::uint32_t>(raw), policy);
        }, py::arg("bits"), py::kw_only(),
        py::arg("sample_policy") = FloatingSamplePolicy::FiniteOnly,
        "Decode checked uint32 Float32 bits without quieting signaling NaNs.");
  m.def("float32_source_sample_bits",
        [numpy_float32, numpy_float64](const py::object &sample, FloatingSamplePolicy policy) {
          double value;
          if (py::isinstance(sample, numpy_float32)) {
            const auto raw = sample.attr("tobytes")().cast<std::string>();
            if (raw.size() != sizeof(std::uint32_t))
              throw std::invalid_argument("Invalid Float32 scalar storage");
            value = float32_source_sample(source_bits(raw.data(), true), policy);
          } else if (py::isinstance(sample, numpy_float64)) {
            const auto raw = sample.attr("tobytes")().cast<std::string>();
            if (raw.size() != sizeof(double))
              throw std::invalid_argument("Invalid Float64 scalar storage");
            std::memcpy(&value, raw.data(), sizeof(value));
          } else {
            value = sample.cast<double>();
          }
          return float32_source_sample_bits(value, policy);
        }, py::arg("sample"), py::kw_only(),
        py::arg("sample_policy") = FloatingSamplePolicy::FiniteOnly,
        "Encode an exactly represented Float32 sample without rounding or payload loss.");
  m.def("canonical_float32_source",
        [](const py::array &array, FloatingSamplePolicy policy) {
          if (array.ndim() != 2 || array_type(array) != PixelType::Float32)
            throw std::invalid_argument("Typed Float32 source requires a 2D grayscale Float32 array");
          const auto data = read_dataset(array, "YX", {}, policy);
          auto image = [&] {
            py::gil_scoped_release release;
            return canonical_float32_source(*data.plane(), policy);
          }();
          return plane_array(image, policy);
        }, py::arg("array"), py::kw_only(),
        py::arg("sample_policy") = FloatingSamplePolicy::FiniteOnly,
        "Copy a 2D Float32 source, admitting quiet NaNs only with explicit policy.");
}
