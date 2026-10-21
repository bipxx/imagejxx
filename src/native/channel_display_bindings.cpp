#include "binding_arrays.hpp"
#include "binding_metadata.hpp"
#include <imagejxx/channel_display_command.hpp>
#include <imagejxx/operations.hpp>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
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
    throw py::type_error("Display indices and limits require integers, not Booleans");
  auto indexed = py::reinterpret_steal<py::object>(PyNumber_Index(value.ptr()));
  if (!indexed) throw py::error_already_set();
  try { return indexed.cast<Integer>(); }
  catch (const py::cast_error &) {
    throw std::invalid_argument("Display integer is outside its native range");
  }
}
std::vector<int> indices(py::iterable values) {
  std::vector<int> result;
  for (auto value : values) result.push_back(integer<int>(value));
  return result;
}
ChannelDisplayMode mode(const std::string &value) {
  if (value == "color") return ChannelDisplayMode::Color;
  if (value == "grayscale") return ChannelDisplayMode::Grayscale;
  if (value == "composite") return ChannelDisplayMode::Composite;
  throw std::invalid_argument("Unknown channel display mode");
}
ChannelDisplayAction action(const std::string &value) {
  if (value == "set") return ChannelDisplayAction::Set;
  if (value == "reset_range") return ChannelDisplayAction::ResetRange;
  if (value == "disable") return ChannelDisplayAction::Disable;
  if (value == "select_max") return ChannelDisplayAction::SelectMax;
  if (value == "select_min") return ChannelDisplayAction::SelectMin;
  throw std::invalid_argument("Unknown channel display action");
}
ChannelDisplayState state(py::handle value, const Dataset &source,
                          bool requested = false) {
  const auto object = bindings::metadata_object_value(value);
  return requested
      ? parse_requested_channel_display(object, source.shape(), source.pixel_type(),
                                        source.sample_policy())
      : parse_channel_display(object, source.shape(), source.pixel_type(),
                              source.sample_policy());
}
py::dict encoded(const ChannelDisplayState &value, const Dataset &source) {
  return bindings::metadata_python(encode_channel_display(
      value, source.shape(), source.pixel_type(), source.sample_policy()));
}
py::dict observation(const ChannelDisplayRange &value) {
  py::dict result;
  if (std::isfinite(value.range[0]) && std::isfinite(value.range[1]))
    result["range"] = value.range;
  else {
    py::dict encoded;
    encoded["float32_bits"] = std::array<std::uint32_t,2>{
        float32_source_sample_bits(value.range[0],FloatingSamplePolicy::PreserveIEEEFloat32),
        float32_source_sample_bits(value.range[1],FloatingSamplePolicy::PreserveIEEEFloat32)};
    result["range"] = std::move(encoded);
  }
  result["empty_finite"] = value.empty_finite;
  return result;
}
ChannelDisplayRequest request(const DocumentSnapshot &source, py::handle value,
                              const std::string &kind, py::iterable selected) {
  ChannelDisplayRequest result;
  result.action = action(kind);
  if (!value.is_none()) result.state = state(value, source.data().dataset, true);
  result.reset_channels = indices(selected);
  return result;
}
std::optional<std::array<double, 2>> legacy_range(py::handle value) {
  if (value.is_none()) return {};
  auto converted = bindings::metadata_value(value);
  const auto tree = std::get_if<std::shared_ptr<const MetadataTree>>(&converted);
  if (!tree || !*tree || !std::holds_alternative<MetadataArray>((*tree)->value))
    throw std::invalid_argument("Legacy display range requires two finite numbers");
  const auto &array = std::get<MetadataArray>((*tree)->value);
  if (array.size() != 2)
    throw std::invalid_argument("Legacy display range requires two finite numbers");
  std::array<double, 2> result;
  for (std::size_t i = 0; i < 2; ++i) {
    if (const auto v = std::get_if<double>(&array[i])) result[i] = *v;
    else if (const auto v = std::get_if<std::int64_t>(&array[i])) {
      result[i] = static_cast<double>(*v);
      if (result[i] >= std::ldexp(1.0, 63) ||
          static_cast<std::int64_t>(result[i]) != *v)
        throw std::invalid_argument("Legacy display endpoint loses precision");
    } else if (const auto v = std::get_if<std::uint64_t>(&array[i])) {
      result[i] = static_cast<double>(*v);
      if (result[i] >= std::ldexp(1.0, 64) ||
          static_cast<std::uint64_t>(result[i]) != *v)
        throw std::invalid_argument("Legacy display endpoint loses precision");
    } else throw std::invalid_argument("Legacy display endpoints must be numbers");
  }
  return result;
}
} // namespace

void bind_channel_display(py::module_ &m) {
  py::class_<ChannelDisplayLimits>(m, "ChannelDisplayLimits")
      .def(py::init<>())
      .def_property("max_pixels", [](const ChannelDisplayLimits &x) { return x.max_pixels; },
          [](ChannelDisplayLimits &x, py::handle v) { x.max_pixels = integer<std::size_t>(v); })
      .def_property("max_working_bytes", [](const ChannelDisplayLimits &x) { return x.max_working_bytes; },
          [](ChannelDisplayLimits &x, py::handle v) { x.max_working_bytes = integer<std::size_t>(v); });
  py::class_<ChannelDisplayEstimate>(m, "ChannelDisplayEstimate")
      .def_readonly("width", &ChannelDisplayEstimate::width)
      .def_readonly("height", &ChannelDisplayEstimate::height)
      .def_readonly("pixels", &ChannelDisplayEstimate::pixels)
      .def_readonly("working_bytes", &ChannelDisplayEstimate::working_bytes)
      .def_readonly("contributing_channels", &ChannelDisplayEstimate::contributing_channels);
  m.def("channel_display_lut", [](const std::string &name) {
    const auto table = channel_display_lut(name);
    py::array_t<std::uint8_t> result({256, 3});
    auto *p = result.mutable_data();
    for (const auto &row : table) for (auto value : row) *p++ = value;
    return result;
  }, py::arg("name"));
  m.def("validate_channel_display", [](py::dict value, Dataset source) {
    (void)state(value, source);
  }, py::arg("state"), py::arg("source"));
  m.def("normalize_channel_display", [](py::dict value, Dataset source) {
    return encoded(state(value, source, true), source);
  }, py::arg("state"), py::arg("source"));
  m.def("channel_display_state", [](DisplayState display, Dataset source) -> py::object {
    const auto value = recognized_channel_display(display, source);
    return value ? py::object(encoded(*value, source)) : py::none();
  }, py::arg("display"), py::arg("source"));
  m.def("with_channel_display", [](DisplayState display, Dataset source, py::object value) {
    std::optional<ChannelDisplayState> captured;
    if (!value.is_none()) captured = state(value, source, true);
    return with_channel_display(std::move(display), source, std::move(captured));
  }, py::arg("display"), py::arg("source"), py::arg("state") = py::none());
  m.def("default_channel_display", [](Dataset source, py::object t, py::object z,
      const std::string &view_mode, ChannelDisplayLimits limits,
      std::shared_ptr<CancellationToken> cancellation, py::object checkpoint) {
    const auto tc = integer<int>(t), zc = integer<int>(z);
    const auto selected_mode = mode(view_mode);
    const auto control = axis_range_python_control(std::move(cancellation), py::none(), checkpoint);
    const auto result = [&] { py::gil_scoped_release release;
      return default_channel_display(source, tc, zc, selected_mode, limits, control); }();
    return encoded(result, source);
  }, py::arg("source"), py::arg("t") = 0, py::arg("z") = 0,
     py::arg("mode") = "composite", py::arg("limits") = ChannelDisplayLimits{},
     py::kw_only(), py::arg("cancellation") = nullptr, py::arg("checkpoint") = py::none());
  m.def("reset_channel_display_ranges", [](Dataset source, py::dict value,
      py::object t, py::object z, py::iterable selected, ChannelDisplayLimits limits,
      std::shared_ptr<CancellationToken> cancellation, py::object checkpoint) {
    auto captured = state(value, source, true);
    const auto tc = integer<int>(t), zc = integer<int>(z);
    const auto chosen = indices(selected);
    const auto control = axis_range_python_control(std::move(cancellation), py::none(), checkpoint);
    const auto result = [&] { py::gil_scoped_release release;
      return reset_channel_display_ranges(source, captured, tc, zc, chosen, limits, control); }();
    py::list ranges;
    for (const auto &range : result.second) ranges.append(observation(range));
    return py::make_tuple(encoded(result.first, source), ranges);
  }, py::arg("source"), py::arg("state"), py::arg("t") = 0, py::arg("z") = 0,
     py::arg("channels") = py::tuple(), py::arg("limits") = ChannelDisplayLimits{},
     py::kw_only(), py::arg("cancellation") = nullptr, py::arg("checkpoint") = py::none());
  m.def("effective_channel_display_range", [](Dataset source, py::object value,
      py::object t, py::object c, py::object z, py::object legacy,
      ChannelDisplayLimits limits, std::shared_ptr<CancellationToken> cancellation,
      py::object checkpoint) {
    std::optional<ChannelDisplayState> captured;
    if (!value.is_none()) captured = state(value, source);
    const auto tc = integer<int>(t), cc = integer<int>(c), zc = integer<int>(z);
    const auto flat = legacy_range(legacy);
    const auto control = axis_range_python_control(std::move(cancellation), py::none(), checkpoint);
    const auto result = [&] { py::gil_scoped_release release;
      return effective_channel_display_range(source, captured, tc, cc, zc, flat, limits, control); }();
    return observation(result);
  }, py::arg("source"), py::arg("state") = py::none(), py::arg("t") = 0,
     py::arg("c") = 0, py::arg("z") = 0, py::arg("legacy_range") = py::none(),
     py::arg("limits") = ChannelDisplayLimits{}, py::kw_only(),
     py::arg("cancellation") = nullptr, py::arg("checkpoint") = py::none());
  m.def("estimate_channel_display", [](Dataset source, py::dict value,
      const std::string &plane, py::object coordinate, py::object t,
      py::object active_c, ChannelDisplayLimits limits) {
    const auto captured = state(value, source);
    const auto view = parse_plane(plane);
    const auto fixed = integer<int>(coordinate), tc = integer<int>(t), cc = integer<int>(active_c);
    py::gil_scoped_release release;
    return estimate_channel_display(source, captured, view, fixed, tc, cc, limits);
  }, py::arg("source"), py::arg("state"), py::arg("plane") = "XY",
     py::arg("coordinate") = 0, py::arg("t") = 0, py::arg("active_c") = 0,
     py::arg("limits") = ChannelDisplayLimits{});
  m.def("compose_channel_display", [](Dataset source, py::dict value,
      const std::string &plane, py::object coordinate, py::object t,
      py::object active_c, ChannelDisplayLimits limits,
      std::shared_ptr<CancellationToken> cancellation, py::object checkpoint) {
    const auto captured = state(value, source);
    const auto view = parse_plane(plane);
    const auto fixed = integer<int>(coordinate), tc = integer<int>(t), cc = integer<int>(active_c);
    const auto control = axis_range_python_control(std::move(cancellation), py::none(), checkpoint);
    const auto result = [&] { py::gil_scoped_release release;
      return compose_channel_display(source, captured, view, fixed, tc, cc, {}, limits, control); }();
    return bindings::numpy_image(result);
  }, py::arg("source"), py::arg("state"), py::arg("plane") = "XY",
     py::arg("coordinate") = 0, py::arg("t") = 0, py::arg("active_c") = 0,
     py::arg("limits") = ChannelDisplayLimits{}, py::kw_only(),
     py::arg("cancellation") = nullptr, py::arg("checkpoint") = py::none());
  py::class_<ResolvedChannelDisplayCommand>(m, "ResolvedChannelDisplayCommand")
      .def_property_readonly("source", [](const ResolvedChannelDisplayCommand &x) { return x.source(); })
      .def_property_readonly("action", [](const ResolvedChannelDisplayCommand &x) {
        switch (x.action()) {
        case ChannelDisplayAction::Set: return "set";
        case ChannelDisplayAction::ResetRange: return "reset_range";
        case ChannelDisplayAction::Disable: return "disable";
        case ChannelDisplayAction::SelectMax: return "select_max";
        case ChannelDisplayAction::SelectMin: return "select_min";
        } throw std::invalid_argument("Unknown channel display action");
      })
      .def_property_readonly("state", [](const ResolvedChannelDisplayCommand &x) -> py::object {
        return x.state() ? py::object(encoded(*x.state(), x.source().data().dataset)) : py::none();
      })
      .def_property_readonly("version", [](const ResolvedChannelDisplayCommand &x) {
        return static_cast<int>(x.version());
      })
      .def_property_readonly("navigation_channel", &ResolvedChannelDisplayCommand::navigation_channel)
      .def_property_readonly("display_dependency_revision", &ResolvedChannelDisplayCommand::display_dependency_revision)
      .def_property_readonly("requires_captured_display", &ResolvedChannelDisplayCommand::requires_captured_display)
      .def("record", [](const ResolvedChannelDisplayCommand &x) { return bindings::metadata_python(x.record()); });
  m.def("resolve_channel_display_command", [](DocumentSnapshot source, py::object value,
      const std::string &kind, py::iterable selected, ChannelDisplayLimits limits,
      std::shared_ptr<CancellationToken> cancellation, py::object checkpoint) {
    const auto captured = request(source, value, kind, selected);
    const auto control = axis_range_python_control(std::move(cancellation), py::none(), checkpoint);
    py::gil_scoped_release release;
    return resolve_channel_display_command(source, captured, limits, control);
  }, py::arg("source"), py::arg("state") = py::none(), py::arg("action") = "set",
     py::arg("channels") = py::tuple(), py::arg("limits") = ChannelDisplayLimits{},
     py::kw_only(), py::arg("cancellation") = nullptr, py::arg("checkpoint") = py::none());
  m.def("validate_channel_display_command_record", [](py::dict value) {
    const auto captured = bindings::metadata_object_value(value);
    py::gil_scoped_release release;
    validate_channel_display_command_record(captured);
  }, py::arg("record"));
  m.def("channel_display_command_from_record", [](DocumentSnapshot source, py::dict value) {
    const auto captured = bindings::metadata_object_value(value);
    py::gil_scoped_release release;
    return channel_display_command_from_record(source, captured);
  }, py::arg("source"), py::arg("record"));
  m.def("rebind_channel_display_command", [](DocumentSnapshot source, py::dict value,
      py::object new_state, const std::string &kind, py::iterable selected,
      ChannelDisplayLimits limits, std::shared_ptr<CancellationToken> cancellation,
      py::object checkpoint) {
    const auto original = bindings::metadata_object_value(value);
    { py::gil_scoped_release release;
      validate_channel_display_command_record(original); }
    const auto captured = request(source, new_state, kind, selected);
    const auto control = axis_range_python_control(std::move(cancellation), py::none(), checkpoint);
    py::gil_scoped_release release;
    return rebind_channel_display_command(source, original, captured, limits, control);
  }, py::arg("source"), py::arg("record"), py::arg("state") = py::none(),
     py::arg("action") = "set", py::arg("channels") = py::tuple(),
     py::arg("limits") = ChannelDisplayLimits{}, py::kw_only(),
     py::arg("cancellation") = nullptr, py::arg("checkpoint") = py::none());
  m.def("command_display_state", [](ResolvedChannelDisplayCommand command, DocumentSnapshot current) {
    py::gil_scoped_release release;
    return command_display_state(command, current);
  }, py::arg("command"), py::arg("current"));
}
