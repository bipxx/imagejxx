// Private assertion fixture: public Dataset/PlaneSource only. Not a product API.
#include <imagejxx/dataset.hpp>
#include <imagejxx/document.hpp>
#include <pybind11/pybind11.h>

#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace py = pybind11;
namespace {
using namespace imagejxx;

struct ReadObserver {
  std::atomic<std::size_t> plane{0}, strip{0}, window{0};
};

struct ConstraintCounts {
  std::atomic<std::size_t> calls{0}, rejections{0};
};
struct ConstraintObserver {
  std::shared_ptr<ConstraintCounts> counts;
  std::shared_ptr<const DocumentValidator> lease;
};

class UnreadSource final : public PlaneSource {
public:
  UnreadSource(PixelType type, int samples, std::shared_ptr<ReadObserver> observer)
      : shape_{2, 3, 3, 10, 12, samples}, type_(type),
        observer_(std::move(observer)) {}

  DatasetShape shape() const override { return shape_; }
  PixelType pixel_type() const override { return type_; }

  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    observer_->plane.fetch_add(1, std::memory_order_relaxed);
    throw std::runtime_error("saved-roi-observer.plane acquisition");
  }

  Image read_region(PlanePosition, int, int, int width, int height) const override {
    // PlaneSource has one public region acquisition seam. Classify actual
    // requested strips separately from rectangular windows at that seam.
    if (width == 1 || height == 1) {
      observer_->strip.fetch_add(1, std::memory_order_relaxed);
      throw std::runtime_error("saved-roi-observer.strip acquisition");
    }
    observer_->window.fetch_add(1, std::memory_order_relaxed);
    throw std::runtime_error("saved-roi-observer.window acquisition");
  }

private:
  DatasetShape shape_;
  PixelType type_;
  std::shared_ptr<ReadObserver> observer_;
};

PixelType pixel_type(const std::string &name) {
  if (name == "uint8") return PixelType::UInt8;
  if (name == "uint16") return PixelType::UInt16;
  if (name == "float32") return PixelType::Float32;
  if (name == "float64") return PixelType::Float64;
  throw py::value_error("Fixture pixel_type must be uint8, uint16, float32 or float64");
}

int samples_value(py::handle input) {
  if (!PyLong_CheckExact(input.ptr()) || PyBool_Check(input.ptr()))
    throw py::type_error("Fixture samples must be an exact nonbool integer");
  const auto value = PyLong_AsLong(input.ptr());
  if (PyErr_Occurred()) {
    PyErr_Clear();
    throw py::value_error("Fixture samples exceed its bounded range");
  }
  if (value != 1 && value != 3 && value != 4)
    throw py::value_error("Fixture samples must be 1, 3 or 4");
  return static_cast<int>(value);
}

py::dict counts(const ReadObserver &observer) {
  py::dict result;
  result["plane"] = observer.plane.load(std::memory_order_relaxed);
  result["strip"] = observer.strip.load(std::memory_order_relaxed);
  result["window"] = observer.window.load(std::memory_order_relaxed);
  return result;
}
} // namespace

PYBIND11_MODULE(_imagejxx_test_saved_roi_source, module) {
  // Use the already registered public Dataset class; do not register a second
  // Dataset or impersonate an imagejxx package/module.
  py::module_::import("imagejxx._native");
  module.attr("fixture_contract") = "p06g.saved-roi-provider.001";
  // Independent compiler measurements for portable working-set assertions.
  // Never derive these from an estimate supplied by the operation under test.
  module.def("abi_sizes", [] {
    py::dict sizes;
    sizes["image"] = sizeof(Image);
    sizes["shared_const_image"] = sizeof(std::shared_ptr<const Image>);
    return sizes;
  });
  py::class_<ReadObserver, std::shared_ptr<ReadObserver>>(module, "ReadObserver")
      .def_property_readonly("counts", &counts);
  module.attr("constraint_contract") = "p06g.saved-roi-constraint.001";
  py::class_<ConstraintObserver, std::shared_ptr<ConstraintObserver>>(module, "ConstraintObserver")
      .def_property_readonly("calls", [](const ConstraintObserver &value) {
        return value.counts->calls.load(std::memory_order_relaxed);
      })
      .def_property_readonly("rejections", [](const ConstraintObserver &value) {
        return value.counts->rejections.load(std::memory_order_relaxed);
      })
      .def_property_readonly("active", [](const ConstraintObserver &value) { return bool(value.lease); });
  module.def("reject_replacement", [](const std::shared_ptr<Document> &owner, Selection replacement) {
    if (!owner) throw py::value_error("Constraint fixture requires a public Document owner");
    const auto source = owner->snapshot();
    if (source.data().rois.empty()) throw py::value_error("Constraint fixture requires saved row zero");
    replacement.validate(source.data().dataset.shape().x, source.data().dataset.shape().y);
    auto observed = std::make_shared<ConstraintObserver>();
    observed->counts = std::make_shared<ConstraintCounts>();
    // C++ values only: no Python callback, owner/controller read or Python
    // object destruction occurs in the retained under-lock native validator.
    observed->lease = owner->constrain([state = observed->counts,
                                      blocked = std::move(replacement)](const DocumentData &data,
                                                                       const DisplayState &) {
      state->calls.fetch_add(1, std::memory_order_relaxed);
      if (!data.rois.empty() && data.rois[0].selection == blocked) {
        state->rejections.fetch_add(1, std::memory_order_relaxed);
        throw std::runtime_error("saved-roi-observer.constraint rejection");
      }
    });
    return observed;
  }, py::arg("owner"), py::arg("replacement"));
  module.def("make_source", [](const std::string &name, py::object sample_count) {
    const auto type = pixel_type(name);
    const auto samples = samples_value(sample_count);
    const auto observer = std::make_shared<ReadObserver>();
    SpatialCalibration calibration;
    calibration.spacing = {.5, 1.25, 2.5};
    calibration.origin = {11, -7, 19};
    calibration.unit = "um";
    calibration.frame_interval = .75;
    calibration.time_origin = 2.25;
    calibration.time_unit = "s";
    Dataset dataset(std::make_shared<UnreadSource>(type, samples, observer),
                    std::move(calibration), FloatingSamplePolicy::FiniteOnly);
    return py::make_tuple(std::move(dataset), observer);
  }, py::arg("pixel_type") = "uint16", py::arg("samples") = 1);
}
