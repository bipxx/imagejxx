#include <imagejxx/global_calibration.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
namespace py=pybind11;
using namespace imagejxx;
namespace {
PixelType storage(const std::string& name) {
  if(name=="uint8")return PixelType::UInt8;
  if(name=="uint16")return PixelType::UInt16;
  if(name=="float32")return PixelType::Float32;
  if(name=="float64")return PixelType::Float64;
  throw std::invalid_argument("Global calibration source type must be uint8/uint16/float32/float64");
}
const char* storage(PixelType type) {
  switch(type) {
    case PixelType::UInt8:return "uint8";
    case PixelType::UInt16:return "uint16";
    case PixelType::Float32:return "float32";
    case PixelType::Float64:return "float64";
    default:throw std::invalid_argument("Invalid global calibration source type");
  }
}
}
void bind_global_calibration(py::module_& m) {
  py::class_<GlobalCalibration>(m,"GlobalCalibration")
    .def(py::init([](SpatialCalibration spatial,ImageJIntensityCalibration intensity,
        const std::string& type,py::object samples) {
      if(!PyLong_CheckExact(samples.ptr()))throw py::type_error("source_samples must be an integer");
      const int count=py::cast<int>(samples);const auto pixelType=storage(type);
      py::gil_scoped_release release;return GlobalCalibration(std::move(spatial),std::move(intensity),pixelType,count);
    }),py::arg("spatial"),py::arg("intensity"),py::arg("source_type"),py::arg("source_samples")=1)
    .def_static("from_snapshot",&GlobalCalibration::from_snapshot,py::arg("source"),py::call_guard<py::gil_scoped_release>())
    // Return values, not mutable Python aliases into immutable native state.
    .def_property_readonly("spatial",[](const GlobalCalibration& value){return value.spatial();})
    .def_property_readonly("intensity",[](const GlobalCalibration& value){return value.intensity();})
    .def_property_readonly("source_type",[](const GlobalCalibration& value){return storage(value.source_type());})
    .def_property_readonly("source_samples",&GlobalCalibration::source_samples)
    .def_property_readonly("source_bit_depth",&GlobalCalibration::source_bit_depth);
  py::class_<EffectiveCalibration>(m,"EffectiveCalibration")
    .def_property_readonly("spatial",[](const EffectiveCalibration& value){return value.spatial();})
    .def_property_readonly("intensity",[](const EffectiveCalibration& value){return value.intensity();})
    .def_property_readonly("uses_global",&EffectiveCalibration::uses_global)
    .def_property_readonly("ignores_global",&EffectiveCalibration::ignores_global)
    .def_property_readonly("document_id",&EffectiveCalibration::document_id)
    .def_property_readonly("source_revisions",&EffectiveCalibration::source_revisions)
    .def_property_readonly("global_generation",&EffectiveCalibration::global_generation)
    .def_property_readonly("bypass_generation",&EffectiveCalibration::bypass_generation);
  m.def("resolve_effective_calibration",[](const DocumentSnapshot& source,
      std::optional<GlobalCalibration> global,py::bool_ ignore) {
    const bool bypass=ignore.cast<bool>();py::gil_scoped_release release;
    return resolve_effective_calibration(source,global?&*global:nullptr,bypass);
  },py::arg("source"),py::arg("global_calibration")=py::none(),py::kw_only(),py::arg("ignore_global").noconvert()=false);
}
