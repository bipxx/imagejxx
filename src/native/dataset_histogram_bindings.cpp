#include <imagejxx/dataset_histogram.hpp>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace py = pybind11;
using namespace imagejxx;

ExecutionControl axis_range_python_control(std::shared_ptr<CancellationToken>,
                                           py::object, py::object);

namespace {
bool boolean(py::handle value) {
  return PyBool_Check(value.ptr()) ||
      py::isinstance(value, py::module_::import("numpy").attr("bool_"));
}

template <class Integer> Integer integer(py::handle value) {
  if (boolean(value))
    throw py::type_error("Histogram bins and limits require integers, not Booleans");
  auto indexed = py::reinterpret_steal<py::object>(PyNumber_Index(value.ptr()));
  if (!indexed) throw py::error_already_set();
  try { return indexed.cast<Integer>(); }
  catch (const py::cast_error &) {
    throw std::invalid_argument("Histogram integer is outside its native range");
  }
}

double endpoint(py::handle value) {
  if (PyComplex_Check(value.ptr()) ||
      py::isinstance(value, py::module_::import("numpy").attr("complexfloating")))
    throw py::type_error("Histogram range endpoints require real finite numbers");
  if (boolean(value) || !PyNumber_Check(value.ptr()))
    throw py::type_error("Histogram range endpoints require finite numbers");
  double result;
  if (PyIndex_Check(value.ptr())) {
    auto indexed = py::reinterpret_steal<py::object>(PyNumber_Index(value.ptr()));
    if (!indexed) throw py::error_already_set();
    result = PyLong_AsDouble(indexed.ptr());
    if (PyErr_Occurred()) throw py::error_already_set();
    auto restored = py::reinterpret_steal<py::object>(PyLong_FromDouble(result));
    if (!restored) throw py::error_already_set();
    const int exact = PyObject_RichCompareBool(indexed.ptr(), restored.ptr(), Py_EQ);
    if (exact < 0) throw py::error_already_set();
    if (!exact)
      throw std::invalid_argument("Histogram range endpoint loses integer precision");
  } else {
    auto number = py::reinterpret_steal<py::object>(PyNumber_Float(value.ptr()));
    if (!number) throw py::error_already_set();
    result = PyFloat_AsDouble(number.ptr());
  }
  if (!std::isfinite(result))
    throw std::invalid_argument("Histogram range endpoints must be finite");
  return result;
}

std::optional<std::array<double, 2>> optional_range(py::handle value) {
  if (value.is_none()) return {};
  if (!PySequence_Check(value.ptr()) || py::isinstance<py::str>(value) ||
      py::isinstance<py::bytes>(value))
    throw py::type_error("Histogram range requires two finite numbers or None");
  const auto values = py::reinterpret_borrow<py::sequence>(value);
  if (values.size() != 2)
    throw std::invalid_argument("Histogram range requires exactly two endpoints");
  return std::array<double, 2>{endpoint(values[0]), endpoint(values[1])};
}

py::dict histogram_dictionary(const DatasetHistogramResult &value) {
  py::array_t<std::uint64_t> counts(
      static_cast<py::ssize_t>(value.histogram.counts.size()));
  py::array_t<double> edges(
      static_cast<py::ssize_t>(value.histogram.edges.size()));
  std::copy(value.histogram.counts.begin(), value.histogram.counts.end(),
            counts.mutable_data());
  std::copy(value.histogram.edges.begin(), value.histogram.edges.end(),
            edges.mutable_data());
  py::dict result;
  result["counts"] = std::move(counts);
  result["edges"] = std::move(edges);
  return result;
}
} // namespace

void bind_dataset_histogram(py::module_ &m) {
  py::enum_<HistogramBinning>(m, "HistogramBinning")
      .value("ProcessorRaw", HistogramBinning::ProcessorRaw)
      .value("UniformRange", HistogramBinning::UniformRange);

  py::class_<DatasetHistogramRequest>(m, "DatasetHistogramRequest")
      .def(py::init<>())
      .def(py::init<const DatasetHistogramRequest &>())
      .def_property("position",
          [](const DatasetHistogramRequest &value) { return value.position; },
          [](DatasetHistogramRequest &value, PlanePosition position) {
            value.position = position;
          })
      .def_property("area_roi",
          [](const DatasetHistogramRequest &value) { return value.area_roi; },
          [](DatasetHistogramRequest &value, std::optional<Selection> area) {
            value.area_roi = std::move(area);
          })
      .def_readwrite("binning", &DatasetHistogramRequest::binning)
      .def_property("bins",
          [](const DatasetHistogramRequest &value) { return value.bins; },
          [](DatasetHistogramRequest &value, py::handle bins) {
            value.bins = integer<int>(bins);
          })
      .def_property("range",
          [](const DatasetHistogramRequest &value) { return value.range; },
          [](DatasetHistogramRequest &value, py::handle range) {
            value.range = optional_range(range);
          });

  py::class_<DatasetHistogramLimits>(m, "DatasetHistogramLimits")
      .def(py::init<>())
      .def(py::init<const DatasetHistogramLimits &>())
      .def_property("max_pixels",
          [](const DatasetHistogramLimits &value) { return value.max_pixels; },
          [](DatasetHistogramLimits &value, py::handle cap) {
            value.max_pixels = integer<std::size_t>(cap);
          })
      .def_property("max_working_bytes",
          [](const DatasetHistogramLimits &value) { return value.max_working_bytes; },
          [](DatasetHistogramLimits &value, py::handle cap) {
            value.max_working_bytes = integer<std::size_t>(cap);
          });

  py::class_<DatasetHistogramEstimate>(m, "DatasetHistogramEstimate")
      .def_readonly("pixel_count", &DatasetHistogramEstimate::pixel_count)
      .def_readonly("working_bytes", &DatasetHistogramEstimate::working_bytes)
      .def_readonly("selection_bytes", &DatasetHistogramEstimate::selection_bytes)
      .def_readonly("mask_bytes", &DatasetHistogramEstimate::mask_bytes)
      .def_readonly("plane_bytes", &DatasetHistogramEstimate::plane_bytes)
      .def_readonly("histogram_bytes", &DatasetHistogramEstimate::histogram_bytes);

  py::class_<DatasetHistogramResult>(m, "DatasetHistogramResult")
      .def_property_readonly("histogram", &histogram_dictionary,
          "Detached uint64 counts and float64 edges; every access returns copies.")
      .def_readonly("histogram_origin", &DatasetHistogramResult::histogram_origin)
      .def_readonly("bin_size", &DatasetHistogramResult::bin_size)
      .def_readonly("minimum", &DatasetHistogramResult::minimum)
      .def_readonly("maximum", &DatasetHistogramResult::maximum)
      .def_readonly("visited_count", &DatasetHistogramResult::visited_count)
      .def_readonly("finite_count", &DatasetHistogramResult::finite_count)
      .def_readonly("included_count", &DatasetHistogramResult::included_count)
      .def_readonly("nan_count", &DatasetHistogramResult::nan_count)
      .def_readonly("underflow_count", &DatasetHistogramResult::underflow_count)
      .def_readonly("overflow_count", &DatasetHistogramResult::overflow_count)
      .def_readonly("plane_minimum", &DatasetHistogramResult::plane_minimum)
      .def_readonly("plane_maximum", &DatasetHistogramResult::plane_maximum)
      .def_readonly("plane_finite_count", &DatasetHistogramResult::plane_finite_count)
      .def_readonly("plane_nan_count", &DatasetHistogramResult::plane_nan_count)
      .def_readonly("mean", &DatasetHistogramResult::mean)
      .def_readonly("stddev", &DatasetHistogramResult::stddev)
      .def_readonly("sum", &DatasetHistogramResult::sum)
      .def_readonly("empty_finite", &DatasetHistogramResult::empty_finite)
      .def_property_readonly("request",
          [](const DatasetHistogramResult &value) { return value.request; })
      .def_property_readonly("estimate",
          [](const DatasetHistogramResult &value) { return value.estimate; });

  m.def("dataset_histogram_csv", [](const DatasetHistogramResult &result) {
    py::gil_scoped_release release;
    return dataset_histogram_csv(result);
  }, py::arg("result"), "Lossless raw-bin CSV; no source reads or file writes.");

  m.def("estimate_dataset_histogram",
      [](Dataset source, DatasetHistogramRequest request,
         DatasetHistogramLimits limits) {
        py::gil_scoped_release release;
        return estimate_dataset_histogram(source, request, limits);
      }, py::arg("source"), py::arg("request") = DatasetHistogramRequest{},
      py::arg("limits") = DatasetHistogramLimits{},
      "Metadata-only native working estimate. Python argument/value-conversion "
      "captures and later getter/representation copies are frontend storage "
      "outside this estimate, as are opaque callable-target internals; native "
      "callable holders are charged. Core preflight precedes pixel/provider "
      "callbacks and reads.");

  m.def("accumulate_dataset_histogram",
      [](Dataset source, DatasetHistogramRequest request,
         DatasetHistogramLimits limits,
         std::shared_ptr<CancellationToken> cancellation, py::object progress,
         py::object checkpoint) {
        // All typed inputs are value captures while the GIL is held. The
        // wrapper-owned control outlives release, including exception unwind.
        const auto control = axis_range_python_control(std::move(cancellation),
            std::move(progress), std::move(checkpoint));
        py::gil_scoped_release release;
        return accumulate_dataset_histogram(source, request, limits, control);
      }, py::arg("source"), py::arg("request") = DatasetHistogramRequest{},
      py::arg("limits") = DatasetHistogramLimits{}, py::kw_only(),
      py::arg("cancellation") = nullptr, py::arg("progress") = py::none(),
      py::arg("checkpoint") = py::none(),
      "Accumulate one captured native TCZ plane and optional area ROI. Returned "
      "histogram/provenance copies are detached. Python argument/value-conversion "
      "captures and later getter copies are frontend storage outside the native "
      "working estimate; core preflight precedes pixel/provider callbacks and reads.");
}
