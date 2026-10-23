#include "axis_command_record.hpp"

#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace imagejxx::detail::axis_command_record {

void exact_fields(const MetadataObject &object,
                  std::initializer_list<const char *> names) {
  std::set<std::string> expected;
  for (const char *name : names) {
    expected.emplace(name);
    if (!object.count(name))
      throw std::invalid_argument(
          std::string("Missing axis extraction record field: ") + name);
  }
  for (const auto &[name, value] : object) {
    (void)value;
    if (!expected.count(name))
      throw std::invalid_argument("Unknown axis extraction record field: " + name);
  }
}

const MetadataObject &object_value(const MetadataValue &value) {
  if (const auto node = std::get_if<std::shared_ptr<const MetadataTree>>(&value);
      node && *node)
    if (const auto object = std::get_if<MetadataObject>(&(*node)->value))
      return *object;
  throw std::invalid_argument("Axis extraction record requires an object");
}

const MetadataArray &array_value(const MetadataValue &value) {
  if (const auto node = std::get_if<std::shared_ptr<const MetadataTree>>(&value);
      node && *node)
    if (const auto array = std::get_if<MetadataArray>(&(*node)->value))
      return *array;
  throw std::invalid_argument("Axis extraction record requires an array");
}

const std::string &text_value(const MetadataValue &value) {
  if (const auto text = std::get_if<std::string>(&value))
    return *text;
  throw std::invalid_argument("Axis extraction record requires text");
}

// Integer fields accept their exact signed/unsigned alternatives, never a
// Boolean or a floating-point approximation (even for otherwise small values).
std::uint64_t unsigned_value(const MetadataValue &value) {
  if (const auto integer = std::get_if<std::uint64_t>(&value))
    return *integer;
  if (const auto integer = std::get_if<std::int64_t>(&value);
      integer && *integer >= 0)
    return static_cast<std::uint64_t>(*integer);
  throw std::invalid_argument(
      "Axis extraction record requires an exact nonnegative integer");
}

int index_value(const MetadataValue &value, bool positive) {
  const auto number = unsigned_value(value);
  if ((positive && number == 0) ||
      number > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
    throw std::invalid_argument("Axis extraction integer is out of native range");
  return static_cast<int>(number);
}

std::size_t size_value(const MetadataValue &value) {
  const auto number = unsigned_value(value);
  if (!number || number > std::numeric_limits<std::size_t>::max())
    throw std::invalid_argument("Axis extraction size must be positive and addressable");
  return static_cast<std::size_t>(number);
}

double number_value(const MetadataValue &value) {
  double result;
  if (const auto real = std::get_if<double>(&value))
    result = *real;
  else if (const auto integer = std::get_if<std::int64_t>(&value)) {
    result = static_cast<double>(*integer);
    // Test representable bounds before converting back; INT64_MAX rounds up.
    if (result >= std::ldexp(1.0, 63) || result < -std::ldexp(1.0, 63) ||
        static_cast<std::int64_t>(result) != *integer)
      throw std::invalid_argument("Axis extraction integer loses numeric precision");
  } else if (const auto integer = std::get_if<std::uint64_t>(&value)) {
    result = static_cast<double>(*integer);
    if (result >= std::ldexp(1.0, 64) ||
        static_cast<std::uint64_t>(result) != *integer)
      throw std::invalid_argument("Axis extraction integer loses numeric precision");
  } else
    throw std::invalid_argument("Axis extraction record requires a number");
  if (!std::isfinite(result))
    throw std::invalid_argument("Axis extraction record numbers must be finite");
  return result;
}

template <std::size_t N>
MetadataValue number_array(const std::array<double, N> &values) {
  MetadataArray result;
  result.reserve(N);
  for (double value : values)
    result.emplace_back(value);
  return metadata_array(std::move(result));
}

template <std::size_t N>
MetadataValue integer_array(const std::array<int, N> &values) {
  MetadataArray result;
  result.reserve(N);
  for (int value : values)
    result.emplace_back(static_cast<std::int64_t>(value));
  return metadata_array(std::move(result));
}

template <std::size_t N>
std::array<double, N> read_numbers(const MetadataValue &value) {
  const auto &array = array_value(value);
  if (array.size() != N)
    throw std::invalid_argument("Axis extraction numeric array has the wrong length");
  std::array<double, N> result;
  for (std::size_t i = 0; i < N; ++i)
    result[i] = number_value(array[i]);
  return result;
}

template <std::size_t N>
std::array<int, N> read_integers(const MetadataValue &value,
                                 bool positive = false) {
  const auto &array = array_value(value);
  if (array.size() != N)
    throw std::invalid_argument("Axis extraction integer array has the wrong length");
  std::array<int, N> result;
  for (std::size_t i = 0; i < N; ++i)
    result[i] = index_value(array[i], positive);
  return result;
}

DatasetShape read_shape(const MetadataValue &value) {
  const auto dimensions = read_integers<6>(value, true);
  DatasetShape shape{dimensions[0], dimensions[1], dimensions[2],
                     dimensions[3], dimensions[4], dimensions[5]};
  shape.validate();
  return shape;
}

MetadataObject calibration_record(const SpatialCalibration &calibration) {
  return {{"spacing", number_array(calibration.spacing)},
          {"origin", number_array(calibration.origin)},
          {"direction", number_array(calibration.direction)},
          {"unit", calibration.unit},
          {"frame_interval", calibration.frame_interval},
          {"time_origin", calibration.time_origin},
          {"time_unit", calibration.time_unit}};
}

SpatialCalibration read_calibration(const MetadataObject &record) {
  exact_fields(record, {"spacing", "origin", "direction", "unit",
                        "frame_interval", "time_origin", "time_unit"});
  SpatialCalibration calibration;
  calibration.spacing = read_numbers<3>(record.at("spacing"));
  calibration.origin = read_numbers<3>(record.at("origin"));
  calibration.direction = read_numbers<9>(record.at("direction"));
  calibration.unit = text_value(record.at("unit"));
  calibration.frame_interval = number_value(record.at("frame_interval"));
  calibration.time_origin = number_value(record.at("time_origin"));
  calibration.time_unit = text_value(record.at("time_unit"));
  calibration.validate();
  return calibration;
}

bool same_calibration(const SpatialCalibration &a, const SpatialCalibration &b) {
  return a.spacing == b.spacing && a.origin == b.origin &&
         a.direction == b.direction && a.unit == b.unit &&
         a.frame_interval == b.frame_interval && a.time_origin == b.time_origin &&
         a.time_unit == b.time_unit;
}

MetadataObject geometry_record(const PlaneGeometry &geometry) {
  return {{"origin", number_array(geometry.origin)},
          {"horizontal_step", number_array(geometry.horizontal_step)},
          {"vertical_step", number_array(geometry.vertical_step)},
          {"horizontal_axis", geometry.horizontal_axis},
          {"vertical_axis", geometry.vertical_axis},
          {"unit", geometry.unit}};
}

PlaneGeometry read_geometry(const MetadataObject &record) {
  exact_fields(record, {"origin", "horizontal_step", "vertical_step",
                        "horizontal_axis", "vertical_axis", "unit"});
  return {read_numbers<3>(record.at("origin")),
          read_numbers<3>(record.at("horizontal_step")),
          read_numbers<3>(record.at("vertical_step")),
          text_value(record.at("horizontal_axis")),
          text_value(record.at("vertical_axis")),
          text_value(record.at("unit"))};
}

bool same_geometry(const PlaneGeometry &a, const PlaneGeometry &b) {
  return a.origin == b.origin && a.horizontal_step == b.horizontal_step &&
         a.vertical_step == b.vertical_step &&
         a.horizontal_axis == b.horizontal_axis &&
         a.vertical_axis == b.vertical_axis && a.unit == b.unit;
}

const char *pixel_type_name(PixelType type) {
  switch (type) {
  case PixelType::UInt8: return "uint8";
  case PixelType::UInt16: return "uint16";
  case PixelType::Float32: return "float32";
  case PixelType::Float64: return "float64";
  }
  throw std::invalid_argument("Invalid axis extraction source pixel type");
}

PixelType read_pixel_type(const MetadataValue &value) {
  const auto &name = text_value(value);
  if (name == "uint8") return PixelType::UInt8;
  if (name == "uint16") return PixelType::UInt16;
  if (name == "float32") return PixelType::Float32;
  if (name == "float64") return PixelType::Float64;
  throw std::invalid_argument("Unknown axis extraction source pixel type");
}

const char *sample_policy_name(FloatingSamplePolicy policy) {
  switch (policy) {
  case FloatingSamplePolicy::FiniteOnly: return "finite_only";
  case FloatingSamplePolicy::PreserveQuietNaNFloat32:
    return "preserve_quiet_nan_float32";
  case FloatingSamplePolicy::PreserveIEEEFloat32:
    return "preserve_ieee_float32";
  }
  throw std::invalid_argument("Invalid axis extraction source sample policy");
}

FloatingSamplePolicy read_sample_policy(const MetadataValue &value) {
  const auto &name = text_value(value);
  if (name == "finite_only") return FloatingSamplePolicy::FiniteOnly;
  if (name == "preserve_quiet_nan_float32")
    return FloatingSamplePolicy::PreserveQuietNaNFloat32;
  if (name == "preserve_ieee_float32")
    return FloatingSamplePolicy::PreserveIEEEFloat32;
  throw std::invalid_argument("Unknown axis extraction source sample policy");
}

void validate_sample_policy(DatasetShape shape, PixelType type,
                            FloatingSamplePolicy policy) {
  (void)sample_policy_name(policy);
  if (policy == FloatingSamplePolicy::PreserveQuietNaNFloat32 &&
      (type != PixelType::Float32 || shape.s != 1))
    throw std::invalid_argument(
        "Axis extraction quiet-NaN policy requires grayscale Float32");
  if (policy == FloatingSamplePolicy::PreserveIEEEFloat32 &&
      (type != PixelType::Float32 || shape.s != 1))
    throw std::invalid_argument("Axis extraction IEEE policy requires grayscale Float32");
}

const char *plane_name(Plane plane) {
  switch (plane) {
  case Plane::XY: return "XY";
  case Plane::XZ: return "XZ";
  case Plane::YZ: return "YZ";
  }
  throw std::invalid_argument("Invalid axis extraction plane");
}

Plane read_plane(const MetadataValue &value) {
  const auto &name = text_value(value);
  if (name == "XY") return Plane::XY;
  if (name == "XZ") return Plane::XZ;
  if (name == "YZ") return Plane::YZ;
  throw std::invalid_argument("Axis extraction plane must be XY, XZ or YZ");
}

SourceRecord source_metadata(const DocumentSnapshot &source) {
  const auto &dataset = source.data().dataset;
  SourceRecord result{source.id(), dataset.shape(), dataset.pixel_type(),
                      dataset.sample_policy(), source.revisions(),
                      dataset.calibration()};
  if (!result.document_id)
    throw std::invalid_argument("Axis extraction source requires a positive owner ID");
  result.shape.validate();
  (void)pixel_type_name(result.pixel_type);
  validate_sample_policy(result.shape, result.pixel_type, result.sample_policy);
  result.calibration.validate();
  return result;
}

MetadataObject source_record(const SourceRecord &source) {
  return {{"document_id", source.document_id},
          {"shape", integer_array(source.shape.dimensions())},
          {"pixel_type", std::string(pixel_type_name(source.pixel_type))},
          {"sample_policy", std::string(sample_policy_name(source.sample_policy))},
          {"revisions", metadata_object({{"pixels", source.revisions.pixels},
                                          {"metadata", source.revisions.metadata}})},
          {"calibration", metadata_object(calibration_record(source.calibration))}};
}

SourceRecord read_source(const MetadataObject &record) {
  exact_fields(record, {"document_id", "shape", "pixel_type", "sample_policy",
                        "revisions", "calibration"});
  const auto id = unsigned_value(record.at("document_id"));
  if (!id)
    throw std::invalid_argument("Axis extraction source requires a positive owner ID");
  const auto shape = read_shape(record.at("shape"));
  const auto type = read_pixel_type(record.at("pixel_type"));
  const auto policy = read_sample_policy(record.at("sample_policy"));
  validate_sample_policy(shape, type, policy);
  const auto &revisions = object_value(record.at("revisions"));
  exact_fields(revisions, {"pixels", "metadata"});
  Revisions revision_values;
  revision_values.pixels = unsigned_value(revisions.at("pixels"));
  revision_values.metadata = unsigned_value(revisions.at("metadata"));
  return {id, shape, type, policy, revision_values,
          read_calibration(object_value(record.at("calibration")))};
}

MetadataObject layout_record(const AxisExtractionLayout &layout) {
  return {{"shape", integer_array(layout.shape.dimensions())},
          {"calibration", metadata_object(calibration_record(layout.calibration))},
          {"geometry", metadata_object(geometry_record(layout.geometry))},
          {"source_axes", integer_array(layout.source_axes)},
          {"plane_name", layout.plane_name},
          {"working_bytes", static_cast<std::uint64_t>(layout.working_bytes)}};
}

void validate_layout(const MetadataObject &record,
                     const AxisExtractionLayout &expected) {
  exact_fields(record, {"shape", "calibration", "geometry", "source_axes",
                        "plane_name", "working_bytes"});
  const auto shape = read_shape(record.at("shape"));
  const auto calibration = read_calibration(object_value(record.at("calibration")));
  const auto geometry = read_geometry(object_value(record.at("geometry")));
  const auto axes = read_integers<3>(record.at("source_axes"));
  const auto &name = text_value(record.at("plane_name"));
  const auto working = size_value(record.at("working_bytes"));
  if (shape.dimensions() != expected.shape.dimensions() ||
      !same_calibration(calibration, expected.calibration) ||
      !same_geometry(geometry, expected.geometry) || axes != expected.source_axes ||
      name != expected.plane_name || working != expected.working_bytes)
    throw std::invalid_argument(
        "Axis extraction derived claims contradict the source and request");
}

} // namespace imagejxx::detail::axis_command_record
