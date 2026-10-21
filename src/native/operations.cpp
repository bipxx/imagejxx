#include "kernel_control.hpp"
#include "operation_internal.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <imagejxx/auto_threshold.hpp>
#include <imagejxx/binary.hpp>
#include <imagejxx/conversion.hpp>
#include <imagejxx/gaussian.hpp>
#include <imagejxx/operations.hpp>
#include <imagejxx/rank.hpp>
#include <imagejxx/rank_command.hpp>
#include <limits>
#include <set>
#include <stdexcept>

namespace imagejxx {
void OperationDefaultContext::validate() const {
  if (width <= 0 || height <= 0 ||
      (samples != 1 && samples != 3 && samples != 4))
    throw std::invalid_argument("Invalid operation default dimensions");
  if (type != PixelType::UInt8 && type != PixelType::UInt16 &&
      type != PixelType::Float32 && type != PixelType::Float64)
    throw std::invalid_argument("Invalid operation default dtype");
  if (observedMaximum)
    require_finite(*observedMaximum, "Observed maximum");
}
void NumericArray::validate() const {
  std::size_t count = 1;
  if (shape.size() > 4)
    throw std::invalid_argument(
        "Numeric parameters support at most four dimensions");
  for (auto size : shape) {
    if (size > std::size_t(INT32_MAX) || (size && count > SIZE_MAX / size))
      throw std::invalid_argument("Numeric parameter shape is too large");
    count *= size;
  }
  if (count != values.size())
    throw std::invalid_argument("Numeric parameter shape and values disagree");
  kernel_blocks("Numeric arguments", values.size(),
                [&](std::size_t first, std::size_t last) {
                  for (auto i = first; i < last; ++i)
                    require_finite(values[i], "Numeric parameter");
                });
}
namespace {
static_assert(sizeof(float) == sizeof(std::uint32_t) &&
                  std::numeric_limits<float>::is_iec559 &&
                  sizeof(double) == sizeof(std::uint64_t) &&
                  std::numeric_limits<double>::is_iec559,
              "Float32 source transport requires IEEE binary32 and binary64");
void validate_sample_policy(FloatingSamplePolicy policy) {
  if (policy != FloatingSamplePolicy::FiniteOnly &&
      policy != FloatingSamplePolicy::PreserveQuietNaNFloat32 &&
      policy != FloatingSamplePolicy::PreserveIEEEFloat32)
    throw std::invalid_argument("Invalid floating sample policy");
}
std::uint64_t double_sample_bits(double value) {
  std::uint64_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}
double sample_from_double_bits(std::uint64_t bits) {
  double value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}
void validate_float32_range(double value) {
  const double maximum = std::numeric_limits<float>::max();
  if (value < -maximum || value > maximum)
    throw std::invalid_argument("Float32 source sample exceeds finite range");
}
const OperationValue &get_parameter(const OperationParameters &p,
                                    const char *name) {
  static const OperationValue empty = std::monostate{};
  auto found = p.find(name);
  return found == p.end() ? empty : found->second;
}
bool empty(const OperationValue &v) {
  return std::holds_alternative<std::monostate>(v);
}
double parameterNumber(const OperationParameters &p, const char *name,
                       double fallback = 0) {
  const auto &v = get_parameter(p, name);
  if (empty(v))
    return fallback;
  double result;
  if (auto value = std::get_if<double>(&v))
    result = *value;
  else if (auto value = std::get_if<std::int64_t>(&v))
    result = double(*value);
  else if (auto value = std::get_if<std::uint64_t>(&v))
    result = double(*value);
  else
    throw std::invalid_argument(std::string(name) +
                                " must be finite numeric data");
  require_finite(result, name);
  return result;
}
int parameterInteger(const OperationParameters &p, const char *name,
                     int fallback = 0) {
  auto number = parameterNumber(p, name, fallback);
  if (number != std::floor(number) || number < INT32_MIN || number > INT32_MAX)
    throw std::invalid_argument(std::string(name) +
                                " must be an int32 integer");
  return int(number);
}
std::uint64_t parameterUnsigned(const OperationParameters &p,
                                const char *name) {
  const auto &value = get_parameter(p, name);
  if (auto u = std::get_if<std::uint64_t>(&value))
    return *u;
  if (auto i = std::get_if<std::int64_t>(&value); i && *i >= 0)
    return std::uint64_t(*i);
  throw std::invalid_argument(std::string(name) +
                              " must be a nonnegative uint64 integer");
}
bool parameterBoolean(const OperationParameters &p, const char *name,
                      bool fallback = false) {
  const auto &value = get_parameter(p, name);
  if (empty(value))
    return fallback;
  if (auto result = std::get_if<bool>(&value))
    return *result;
  throw std::invalid_argument(std::string(name) + " must be boolean");
}
std::string parameterText(const OperationParameters &p, const char *name,
                          const char *fallback = "") {
  const auto &value = get_parameter(p, name);
  if (empty(value))
    return fallback;
  if (auto result = std::get_if<std::string>(&value))
    return *result;
  throw std::invalid_argument(std::string(name) + " must be text");
}
const std::vector<double> &vector(const OperationValue &value) {
  if (auto array = std::get_if<NumericArray>(&value))
    return array->values;
  throw std::invalid_argument("Expected a numeric parameter array");
}
std::array<double, 2> pair(const OperationValue &value, double fallback = 1) {
  if (empty(value))
    return {fallback, fallback};
  const auto &array = std::get<NumericArray>(value);
  if (array.shape.empty())
    return {array.values[0], array.values[0]};
  if (array.values.size() != 2)
    throw std::invalid_argument("Expected a scalar or two values");
  return {array.values[0], array.values[1]};
}
int array_dimension(const OperationValue &value, std::size_t axis) {
  const auto &shape = std::get<NumericArray>(value).shape;
  if (axis >= shape.size())
    throw std::invalid_argument("Invalid numeric parameter dimensions");
  return int(shape[axis]);
}
const Image &parameterImage(const OperationParameters &p, const char *name) {
  return *std::get<std::shared_ptr<const Image>>(get_parameter(p, name));
}
PixelType operationPixelType(const std::string &name) {
  if (name == "uint8")
    return PixelType::UInt8;
  if (name == "uint16")
    return PixelType::UInt16;
  if (name == "float32")
    return PixelType::Float32;
  if (name == "float64")
    return PixelType::Float64;
  throw std::invalid_argument("Unknown pixel type");
}
Image operationLabels(const Labels &labels) {
  Image out(labels.width, labels.height, 1, PixelType::Float64);
  std::copy(labels.pixels.begin(), labels.pixels.end(), out.pixels.begin());
  return out;
}
void validate_image_storage(const Image &image) {
  if (image.width <= 0 || image.height <= 0 ||
      (image.channels != 1 && image.channels != 3 && image.channels != 4) ||
      std::size_t(image.width) >
          SIZE_MAX / std::size_t(image.height) / std::size_t(image.channels) ||
      std::size_t(image.width) * image.height * image.channels !=
          image.pixels.size())
    throw std::invalid_argument("Invalid operation image storage");
  if (image.type != PixelType::UInt8 && image.type != PixelType::UInt16 &&
      image.type != PixelType::Float32 && image.type != PixelType::Float64)
    throw std::invalid_argument("Invalid operation image type");
}
void validate_image(const Image &image) {
  validate_image_storage(image);
  kernel_blocks("Image validation", image.pixels.size(),
                [&](std::size_t first, std::size_t last) {
                  for (auto i = first; i < last; ++i)
                    require_finite(image.pixels[i], "Image pixel");
                });
}
void validate_operation_image(const std::string &id, const Image &image) {
  if (id != "remove_nans_imagej") { validate_image(image); return; }
  validate_image_storage(image);
  if (image.type != PixelType::Float32 || image.channels != 1)
    throw std::invalid_argument("Remove NaNs requires Float32 grayscale");
  kernel_blocks("Remove NaNs operation source validation", image.pixels.size(),
      [&](std::size_t first, std::size_t last) {
        for (auto i=first; i<last; ++i)
          (void)float32_source_sample_bits(image.pixels[i], FloatingSamplePolicy::PreserveIEEEFloat32);
      });
}
OperationValue resolved(DefaultRule rule, const OperationValue &literal,
                        const OperationDefaultContext &context,
                        const OperationParameters &params) {
  const int w = context.width, h = context.height, channels = context.samples;
  const double maximum = type_max(context.type);
  switch (rule) {
  case DefaultRule::Required:
    return std::monostate{};
  case DefaultRule::Width:
    return std::int64_t(w);
  case DefaultRule::Height:
    return std::int64_t(h);
  case DefaultRule::TypeMaximum:
    return maximum;
  case DefaultRule::HalfRange:
    return maximum / 2;
  case DefaultRule::LocalContrast:
    return maximum * 15 / 255;
  case DefaultRule::LocalK: {
    auto method = parameterText(params, "method", "sauvola");
    return method == "niblack" ? -.2 : method == "phansalkar" ? .25 : .2;
  }
  case DefaultRule::ChannelLower:
    return NumericArray{{std::size_t(channels)},
                        std::vector<double>(channels, 0)};
  case DefaultRule::ChannelUpper:
    return NumericArray{{std::size_t(channels)},
                        std::vector<double>(channels, maximum)};
  case DefaultRule::LineStart:
    return NumericArray{{2}, {w / 4., h / 2.}};
  case DefaultRule::LineEnd:
    return NumericArray{{2}, {w * 3 / 4., h / 2.}};
  case DefaultRule::StrokePoints:
    return NumericArray{{2, 2}, {0, 0, w / 2., h / 2.}};
  case DefaultRule::IdentityLut: {
    const std::size_t size = context.type == PixelType::UInt16 ? 65536 : 256;
    NumericArray lut{{size}, std::vector<double>(size)};
    for (std::size_t i = 0; i < size; ++i)
      lut.values[i] = double(i);
    return lut;
  }
  default:
    return literal;
  }
}
OperationValue normalized(const OperationParameter &spec, OperationValue value,
                          std::optional<PixelType> imageType) {
  OperationParameters one{{spec.name, value}};
  if (empty(value)) {
    if (spec.optional)
      return value;
    throw std::invalid_argument(spec.name + " cannot be null");
  }
  switch (spec.kind) {
  case ParameterKind::Boolean:
    value = parameterBoolean(one, spec.name.c_str());
    break;
  case ParameterKind::Integer:
    value = std::int64_t(parameterInteger(one, spec.name.c_str()));
    break;
  case ParameterKind::Unsigned:
    value = parameterUnsigned(one, spec.name.c_str());
    break;
  case ParameterKind::Real:
    value = parameterNumber(one, spec.name.c_str());
    break;
  case ParameterKind::Text:
    value = parameterText(one, spec.name.c_str());
    if (spec.name == "color")
      parse_color(std::get<std::string>(value));
    break;
  case ParameterKind::Numeric:
    if (!std::holds_alternative<NumericArray>(value))
      value = NumericArray{{}, {parameterNumber(one, spec.name.c_str())}};
    std::get<NumericArray>(value).validate();
    if (spec.name == "points" &&
        std::get<NumericArray>(value).shape == std::vector<std::size_t>{0})
      value = NumericArray{{0, 2}, {}};
    break;
  case ParameterKind::Image: {
    std::shared_ptr<const Image> result;
    if (auto stored = std::get_if<std::shared_ptr<const Image>>(&value)) {
      if (!*stored)
        throw std::invalid_argument(spec.name + " image is null");
      validate_image(**stored);
      if (imageType) {
        auto converted = **stored;
        converted.type = *imageType;
        result = std::make_shared<const Image>(
            canonical_image(std::move(converted)));
      } else
        result = *stored;
    } else if (auto array = std::get_if<NumericArray>(&value)) {
      array->validate();
      if ((array->shape.size() != 2 && array->shape.size() != 3) ||
          !array->shape[0] || !array->shape[1])
        throw std::invalid_argument(spec.name + " requires a YX/YXS image");
      int channels = array->shape.size() == 3 ? int(array->shape[2]) : 1;
      if (channels != 1 && channels != 3 && channels != 4)
        throw std::invalid_argument(
            "Parameter images require one, three or four samples");
      Image converted(int(array->shape[1]), int(array->shape[0]), channels,
                      imageType.value_or(PixelType::Float64));
      converted.pixels = array->values;
      result =
          std::make_shared<const Image>(canonical_image(std::move(converted)));
    } else
      throw std::invalid_argument(spec.name + " requires an image");
    value = std::move(result);
    break;
  }
  }
  if (!spec.choices.empty()) {
    bool found = false;
    for (const auto &choice : spec.choices) {
      if (spec.kind == ParameterKind::Integer)
        found = found ||
                std::get<std::int64_t>(value) ==
                    parameterInteger({{spec.name, choice}}, spec.name.c_str());
      else
        found = found || value == choice;
    }
    if (!found)
      throw std::invalid_argument("Unknown " + spec.name + " choice");
  }
  return value;
}
OperationParameters explicit_parameters(const OperationSpec &spec,
                                        const OperationParameters &parameters,
                                        std::optional<PixelType> imageType) {
  OperationParameters result;
  for (const auto &[name, value] : parameters) {
    auto found = std::find_if(spec.parameters.begin(), spec.parameters.end(),
                              [&](const auto &p) { return p.name == name; });
    if (found == spec.parameters.end())
      throw std::invalid_argument("Unknown parameter: " + name);
    result[name] = normalized(*found, value, imageType);
    if (spec.id == "binary_erode" || spec.id == "binary_dilate" ||
        spec.id == "binary_open" || spec.id == "binary_close") {
      if (name == "iterations" || name == "neighbors") {
        const auto number = std::get<std::int64_t>(result[name]);
        if (number < 1 || number > (name == "iterations" ? 100 : 8))
          throw std::invalid_argument("Binary " + name +
                                      " is outside its allowed range");
      }
    }
  }
  return result;
}
void validate_structures(const OperationParameters &parameters) {
  for (const auto &[name, value] : parameters) {
    auto array = std::get_if<NumericArray>(&value);
    if (!array)
      continue;
    if (name == "kernel" &&
        (array->shape.size() != 2 || !array->shape[0] || !array->shape[1]))
      throw std::invalid_argument(
          "Kernel requires a nonempty rectangular matrix");
    if ((name == "stains" && array->values.size() != 9) ||
        (name == "inverse_matrix" && array->values.size() != 6))
      throw std::invalid_argument("Invalid matrix size: " + name);
    if (name == "points" && (array->shape.size() != 2 || array->shape[1] != 2))
      throw std::invalid_argument("Stroke points require Nx2 coordinates");
    if (name == "sigma" || name == "tile_size" || name == "factor" ||
        name == "offset" || name == "start" || name == "end" ||
        name == "spacing" || name == "output_shape") {
      if (!array->shape.empty() &&
          (array->shape.size() != 1 || array->values.size() != 2))
        throw std::invalid_argument(name + " requires a scalar or two values");
      if (name == "tile_size" || name == "factor" || name == "offset" ||
          name == "output_shape")
        for (auto number : array->values)
          if (number != std::floor(number) || number < INT32_MIN ||
              number > INT32_MAX)
            throw std::invalid_argument(name +
                                        " requires int32 coordinates/sizes");
    }
  }
}
ConversionOptions conversionOptions(const OperationParameters &p) {
  ConversionOptions options;
  options.scale = parameterBoolean(p, "scale", true);
  options.weighted_rgb = parameterBoolean(p, "weighted_rgb", false);
  options.rgb_weights = {parameterNumber(p, "red_weight", .299),
                         parameterNumber(p, "green_weight", .587),
                         parameterNumber(p, "blue_weight", .114)};
  options.range_policy = parameterText(p, "range_policy", "data");
  options.input_range = std::array<double, 2>{
      parameterNumber(p, "range_min", 0), parameterNumber(p, "range_max", 1)};
  options.full_range_inversions =
      parameterBoolean(p, "full_range_inversions", false);
  options.unsigned16_bits = parameterInteger(p, "unsigned16_bits", 0);
  options.validate();
  return options;
}
void validate_imagej_rank_parameters(const OperationParameters &parameters) {
  const auto method = parse_rank_filter_method(
      parameterText(parameters, "method", "median"));
  const auto radius = parameterNumber(parameters, "radius", 2.0);
  if (radius < 0 || radius > 1000 ||
      (method == RankFilterMethod::Median && radius > 100))
    throw std::invalid_argument("ImageJ rank radius is outside the method's supported range");
}
void validate_imagej_remove_nans_parameters(const OperationParameters &parameters) {
  validate_remove_nans_command_options({parameterNumber(parameters, "radius", 2.0)});
}
void validate_imagej_top_hat_parameters(const OperationParameters &parameters) {
  validate_top_hat_command_options({parameterNumber(parameters, "radius", 2.0),
      parameterBoolean(parameters, "light_background", false), parameterBoolean(parameters, "dont_subtract", false)});
  (void)parameterBoolean(parameters, "inverted_lut", false);
}
void validate_imagej_outlier_parameters(const OperationParameters &parameters) {
  validate_remove_outliers_command_options({parameterNumber(parameters, "radius", 2.0),
      parameterNumber(parameters, "threshold", 50.0), parameterText(parameters, "which", "bright")});
  (void)parameterBoolean(parameters, "inverted_lut", false);
}
#include "operation_catalog.inc"
#include "operation_kernel.inc"
} // namespace
const std::vector<OperationSpec> &operation_registry() {
  static const auto registry = [] {
    auto registry = make_registry();
    std::set<std::string> names;
    for (const auto &entry : registry) {
      if (!names.insert(entry.id).second)
        throw std::logic_error("Duplicate operation ID");
      std::set<std::string> fields;
      for (const auto &p : entry.parameters)
        if (!fields.insert(p.name).second)
          throw std::logic_error("Duplicate operation parameter");
    }
    return registry;
  }();
  return registry;
}
const OperationSpec &operation_spec(const std::string &id) {
  const auto &registry = operation_registry();
  auto found = std::find_if(registry.begin(), registry.end(),
                            [&](const auto &spec) { return spec.id == id; });
  if (found == registry.end())
    throw std::invalid_argument("Unknown native command: " + id);
  return *found;
}
std::size_t operation_extra_workspace(const std::string &id,
                                      const OperationDefaultContext &context,
                                      const OperationParameters &parameters) {
  context.validate();
  auto multiply = [](std::size_t a, std::size_t b) {
    if (b && a > std::numeric_limits<std::size_t>::max() / b)
      throw std::invalid_argument(
          "Job reservation overflows addressable memory");
    return a * b;
  };
  auto checked_sum = [](std::size_t a, std::size_t b) {
    if (b > std::numeric_limits<std::size_t>::max() - a)
      throw std::invalid_argument(
          "Job reservation overflows addressable memory");
    return a + b;
  };
  std::size_t bytes = 0;
  auto add = [&](std::size_t amount) {
    if (amount > std::numeric_limits<std::size_t>::max() - bytes)
      throw std::invalid_argument(
          "Job reservation overflows addressable memory");
    bytes += amount;
  };
  if(id=="auto_mask" && (is_imagej_threshold_method(parameterText(parameters,"method","otsu")) || parameterBoolean(parameters,"ignore_black",false) || parameterBoolean(parameters,"ignore_white",false) || parameterBoolean(parameters,"stack_histogram",false))) {
    const auto requested=parameterInteger(parameters,"bins",0);
    if(requested!=0 && (requested<2 || requested>65536))throw std::invalid_argument("ImageJ threshold requires 2..65536 bins");
    const auto bins=std::size_t(requested==0?256:requested);
    // Histogram counts/edges, count copy, entropy/mean tables and allocator overhead.
    add(multiply(bins+1,24*sizeof(double)));add(65536);
  }
  if(id=="convert_image"&&context.samples==1)add(1024*1024); // Lookup, palette/provenance and paired metadata copies.
  // Admission, normalization during execution, and retained preparation can
  // overlap copies of these arguments. Count their double-backed storage.
  for (const auto &[name, value] : parameters) {
    if (auto array = std::get_if<NumericArray>(&value))
      add(multiply(array->values.size(), 4 * sizeof(double)));
    else if (auto image = std::get_if<std::shared_ptr<const Image>>(&value))
      add(multiply((*image)->pixels.size(), 4 * sizeof(double)));
  }
  auto rank = [&](int radius) {
    if (radius < 0 || radius > 4096)
      return; // Keep invalid pixel-kernel requests on their existing error
              // path.
    const auto side = std::size_t(radius) * 2 + 1;
    // Square bounds disk footprints too. Include vector growth/reallocation and
    // the per-neighborhood values buffer; the footprint is reused per plane.
    add(multiply(multiply(side, side),
                 3 * sizeof(std::array<int, 2>) + sizeof(double)));
  };
  auto prefix = [&](int radius) {
    if (radius >= 0 && radius <= 4096)
      add(multiply(std::size_t(std::max(context.width, context.height)) +
                       std::size_t(radius) * 2 + 1,
                   sizeof(double)));
  };
  auto gaussian = [&](double sigma) {
    if (sigma >= 0 && sigma <= 1024)
      add(multiply(std::size_t(std::ceil(4 * sigma)) * 2 + 1, sizeof(double)));
  };
  if (id == "rank_filter" || id == "median_filter" || id == "minimum_filter" ||
      id == "maximum_filter" || id == "variance_filter" || id == "morphology" ||
      id == "erode" || id == "dilate" || id == "opening" || id == "closing" ||
      id == "top_hat" || id == "black_hat" || id == "subtract_background" ||
      id == "remove_outliers" || id == "local_maxima")
    rank(parameterInteger(parameters, "radius"));
  if (id == "binary_watershed")
    rank(parameterInteger(parameters, "maxima_radius"));
  if (id == "rank_filter_imagej" || id == "remove_outliers_imagej" || id == "top_hat_imagej" || id == "remove_nans_imagej") {
    if (id == "remove_nans_imagej") validate_imagej_remove_nans_parameters(parameters);
    else if (id == "rank_filter_imagej") validate_imagej_rank_parameters(parameters);
    else if (id == "top_hat_imagej") validate_imagej_top_hat_parameters(parameters);
    else validate_imagej_outlier_parameters(parameters);
    const auto footprint = rank_filter_kernel_imagej(
        parameterNumber(parameters, "radius", 2.0));
    add(multiply(footprint.half_widths.size(), (id == "top_hat_imagej" ? 2 : 1) * sizeof(int)));
    if (id != "top_hat_imagej" && parse_rank_filter_method(parameterText(parameters, "method", "median")) ==
        RankFilterMethod::Median)
      add(multiply(footprint.point_count, 2 * sizeof(float)));
    if (((id == "top_hat_imagej" && !parameterBoolean(parameters,"dont_subtract",false)) ||
         parameterText(parameters, "method", "median") == "variance") &&
        context.type != PixelType::UInt8) {
      // Nonbyte final processing owns counts and edges for a whole active-plane
      // histogram. The generic plane workspace covers its captured XY plane;
      // reserve the extra arrays/control storage before scheduler admission.
      const auto bins = context.type == PixelType::UInt16 ? std::size_t(65536) : std::size_t(256);
      add(multiply(bins, sizeof(std::uint64_t)));
      add(multiply(bins + 1, sizeof(double)));
      add(64 * 1024);
    }
    if (id == "top_hat_imagej")
      add(multiply(multiply(std::size_t(context.width), std::size_t(context.height)), sizeof(double)));
    // Top Hat retains its outer footprint while each rank pass allocates its
    // own footprint. Its second-pass output is generic; reserve the first image.
    // The generic plane workspace already includes the detached output.
    // Count vector objects and conservative allocation/control overhead too.
    add(sizeof(RankFilterKernel) + 2 * sizeof(std::vector<float>) + 3 * 256);
  }
  if (id == "smooth")
    prefix(parameterInteger(parameters, "radius"));
  if (id == "local_threshold") {
    const auto radius = parameterInteger(parameters, "radius");
    prefix(radius);
    const auto method = parameterText(parameters, "method");
    if (method == "median" || method == "midgrey" || method == "bernsen" ||
        method == "contrast")
      rank(radius);
  }
  if (id == "gaussian_blur") {
    const auto sigma = pair(get_parameter(parameters, "sigma"));
    gaussian(sigma[0]);
    gaussian(sigma[1]);
  }
  if (id == "gaussian_blur_imagej") {
    const auto sigma = pair(get_parameter(parameters, "sigma"));
    for (double value : sigma)
      if (!std::isfinite(value) || value < 0 ||
          !std::isfinite(std::ceil(5.0 * value)) ||
          std::ceil(5.0 * value) > double(std::numeric_limits<int>::max()))
        throw std::invalid_argument("Gaussian sigma exceeds the safe finite coordinate range");
    const double accuracy = context.type == PixelType::UInt8 ? .002 : .0002;
    std::size_t plans = 0, peak_line = 0;
    for (std::size_t axis = 0; axis < 2; ++axis) {
      if (sigma[axis] == 0)
        continue;
      const auto length = std::size_t(axis == 0 ? context.height : context.width);
      const bool resample = sigma[axis] > 8.5;
      const auto rate = resample
          ? std::min(length, std::size_t(std::floor(sigma[axis] / 4.0)))
          : std::size_t(1);
      const auto reduced = checked_sum((length - 1) / rate, 7);
      const auto maximum = std::max(std::size_t(50), resample ? reduced : length);
      const double adjusted = resample
          ? std::sqrt(std::pow(sigma[axis] / double(rate), 2) - 1.0 / 3.0 - 1.0 / 4.0)
          : sigma[axis];
      const double requested = std::ceil(adjusted * std::sqrt(-2.0 * std::log(accuracy))) + 1.0;
      const auto coefficients = requested >= double(maximum) ? maximum : std::size_t(requested);
      const auto plan_values = checked_sum(multiply(coefficients, 2),
          resample ? multiply(rate, 7) : 0);
      plans = checked_sum(plans, plan_values);
      const auto line_values = checked_sum(length, resample ? multiply(reduced, 2) : 0);
      peak_line = std::max(peak_line, line_values);
    }
    if (sigma[0] != 0 || sigma[1] != 0) {
      add(multiply(multiply(std::size_t(context.width), context.height), sizeof(float)));
      add(multiply(plans, sizeof(float)));
      add(multiply(peak_line, sizeof(float)));
    }
  }
  if (id == "unsharp_mask") {
    gaussian(parameterNumber(parameters, "sigma"));
    gaussian(parameterNumber(parameters, "sigma"));
  }
  if (id == "difference_of_gaussians")
    for (const auto *name : {"sigma_small", "sigma_large"}) {
      gaussian(parameterNumber(parameters, name));
      gaussian(parameterNumber(parameters, name));
    }
  if (id == "rolling_ball_background" || id == "subtract_rolling_ball") {
    const auto radius = parameterNumber(parameters, "radius");
    if (radius >= 0 && radius <= 4096) {
      const auto side = std::size_t(std::ceil(radius)) * 2 + 1;
      struct Offset {
        int x, y;
        double height;
      };
      add(multiply(multiply(side, side), 3 * sizeof(Offset)));
    }
  }
  if (id == "clahe") {
    const auto tile = pair(get_parameter(parameters, "tile_size"));
    const auto tw = int(tile[1]), th = int(tile[0]);
    if (tw > 0 && th > 0) {
      const auto nx = std::size_t((context.width - 1) / tw + 1);
      const auto ny = std::size_t((context.height - 1) / th + 1);
      add(multiply(multiply(nx, ny), 256 * sizeof(double)));
      add(multiply(nx + ny, sizeof(double)));
    }
  }
  return bytes;
}
OperationParameters operation_defaults(const std::string &id,
                                       const Image *image,
                                       DefaultProfile profile) {
  OperationDefaultContext context;
  if (image) {
    context.width = image->width;
    context.height = image->height;
    context.samples = image->channels;
    context.type = image->type;
    if (profile == DefaultProfile::LegacyPythonUI && id == "in_range") {
      validate_image(*image);
      context.observedMaximum =
          *std::max_element(image->pixels.begin(), image->pixels.end());
    }
  }
  return operation_defaults(id, context, profile);
}
OperationParameters operation_defaults(const std::string &id,
                                       const OperationDefaultContext &context,
                                       DefaultProfile profile) {
  context.validate();
  const bool ui = profile != DefaultProfile::Execution;
  OperationParameters result;
  for (const auto &p : operation_spec(id).parameters) {
    result[p.name] =
        resolved(ui ? p.uiRule : p.defaultRule,
                 ui ? p.uiDefault : p.defaultValue, context, result);
    if (profile == DefaultProfile::LegacyPythonUI && context.observedMaximum &&
        p.uiRule == DefaultRule::ChannelUpper)
      result[p.name] = NumericArray{
          {std::size_t(context.samples)},
          std::vector<double>(context.samples, *context.observedMaximum)};
  }
  return result;
}
OperationParameters validate_operation(const std::string &id,
                                       const Image &image,
                                       const OperationParameters &parameters) {
  validate_operation_image(id, image);
  const OperationDefaultContext context{
      image.width, image.height, image.channels, image.type, {}};
  return validate_operation(id, context, parameters);
}
OperationParameters validate_operation(const std::string &id,
                                       const OperationDefaultContext &context,
                                       const OperationParameters &parameters) {
  context.validate();
  const auto &spec = operation_spec(id);
  auto result = explicit_parameters(spec, parameters, context.type);
  for (const auto &p : spec.parameters) {
    if (!result.count(p.name) ||
        (empty(result[p.name]) && p.defaultRule != DefaultRule::Literal)) {
      if (p.defaultRule == DefaultRule::Required)
        throw std::invalid_argument("Missing parameter: " + p.name);
      result[p.name] = normalized(
          p, resolved(p.defaultRule, p.defaultValue, context, result),
          context.type);
    }
  }
  validate_structures(result);
  if (id == "rank_filter_imagej" || id == "remove_outliers_imagej" || id == "top_hat_imagej" || id == "remove_nans_imagej") {
    if (context.samples != 1 ||
        (context.type != PixelType::UInt8 && context.type != PixelType::UInt16 &&
         context.type != PixelType::Float32))
      throw std::invalid_argument("ImageJ rank filters support UInt8, UInt16 and Float32 grayscale");
    if (id == "remove_nans_imagej") {
      if (context.type != PixelType::Float32) throw std::invalid_argument("Remove NaNs requires Float32 grayscale");
      validate_imagej_remove_nans_parameters(result);
    } else if (id == "rank_filter_imagej") validate_imagej_rank_parameters(result);
    else if (id == "top_hat_imagej") validate_imagej_top_hat_parameters(result);
    else validate_imagej_outlier_parameters(result);
  }
  if (id == "shadow_imagej") {
    if (!((context.type == PixelType::UInt8 && (context.samples == 1 || context.samples == 3)) ||
          ((context.type == PixelType::UInt16 || context.type == PixelType::Float32) && context.samples == 1)))
      throw std::invalid_argument("ImageJ shadows support UInt8 gray/RGB, UInt16 gray and Float32 gray");
  }
  if (id == "gaussian_blur_imagej") {
    if (!((context.type == PixelType::UInt8 && (context.samples == 1 || context.samples == 3)) ||
          ((context.type == PixelType::UInt16 || context.type == PixelType::Float32) && context.samples == 1)))
      throw std::invalid_argument("ImageJ Gaussian supports UInt8 gray/RGB, UInt16 gray and Float32 gray");
    const auto sigma = pair(get_parameter(result, "sigma"));
    for (double value : sigma)
      if (!std::isfinite(value) || value < 0 || !std::isfinite(std::ceil(5.0 * value)) ||
          std::ceil(5.0 * value) > double(std::numeric_limits<int>::max()))
        throw std::invalid_argument("Gaussian sigma exceeds the safe finite coordinate range");
    result["sigma"] = NumericArray{{2}, {sigma[0], sigma[1]}};
  }
  if (id == "convert_image" || id == "invert_uint16")
    (void)conversionOptions(result);
  return result;
}
void validate_operation_record(const std::string &id,
                               const OperationParameters &parameters) {
  const auto &spec = operation_spec(id);
  auto values = explicit_parameters(spec, parameters, std::nullopt);
  for (const auto &p : spec.parameters)
    if (p.defaultRule == DefaultRule::Required && !values.count(p.name))
      throw std::invalid_argument("Missing parameter: " + p.name);
  validate_structures(values);
  if (id == "rank_filter_imagej")
    validate_imagej_rank_parameters(values);
  if (id == "remove_outliers_imagej")
    validate_imagej_outlier_parameters(values);
  if (id == "top_hat_imagej")
    validate_imagej_top_hat_parameters(values);
  if (id == "remove_nans_imagej")
    validate_imagej_remove_nans_parameters(values);
  if (id == "gaussian_blur_imagej" && values.count("sigma"))
    for (double value : pair(get_parameter(values, "sigma")))
      if (!std::isfinite(value) || value < 0 || !std::isfinite(std::ceil(5.0 * value)) ||
          std::ceil(5.0 * value) > double(std::numeric_limits<int>::max()))
        throw std::invalid_argument("Gaussian recorded sigma exceeds the safe finite coordinate range");
  if (id == "convert_image" || id == "invert_uint16")
    (void)conversionOptions(values);
}
Image canonical_image(Image image) {
  validate_image(image);
  kernel_blocks(
      "Canonical pixels", image.pixels.size(),
      [&](std::size_t first, std::size_t last) {
        for (auto i = first; i < last; ++i) {
          auto &value = image.pixels[i];
          if (image.type == PixelType::UInt8 || image.type == PixelType::UInt16)
            value =
                std::floor(std::clamp(value, 0., type_max(image.type)) + .5);
          else if (image.type == PixelType::Float32) {
            value = float(value);
            require_finite(value, "float32 overflow");
          }
        }
      });
  return image;
}
double float32_source_sample(std::uint32_t bits,
                             FloatingSamplePolicy sample_policy) {
  validate_sample_policy(sample_policy);
  constexpr std::uint32_t exponent = 0x7f800000u, fraction = 0x007fffffu;
  if ((bits & exponent) == exponent) {
    if (sample_policy != FloatingSamplePolicy::PreserveIEEEFloat32 &&
        (sample_policy != FloatingSamplePolicy::PreserveQuietNaNFloat32 ||
         (bits & 0x00400000u) == 0))
      throw std::invalid_argument("Float32 source requires finite or opted quiet-NaN samples");
    // Position the binary32 sign/payload in their binary64 fields without any
    // NaN arithmetic or floating conversion that might canonicalize a payload.
    const auto expanded =
        (std::uint64_t(bits & 0x80000000u) << 32) |
        0x7ff0000000000000ULL | (std::uint64_t(bits & fraction) << 29);
    return sample_from_double_bits(expanded);
  }
  float value;
  std::memcpy(&value, &bits, sizeof(value));
  return static_cast<double>(value);
}
std::uint32_t float32_source_sample_bits(double sample,
                                       FloatingSamplePolicy sample_policy) {
  validate_sample_policy(sample_policy);
  const auto bits = double_sample_bits(sample);
  constexpr std::uint64_t exponent = 0x7ff0000000000000ULL;
  if ((bits & exponent) == exponent) {
    // Binary32 payloads occupy the high 23 binary64 fraction bits. Test fields
    // before any FP conversion so an admitted signaling NaN remains signaling.
    // The established quiet policy still rejects infinity/signaling patterns.
    if ((bits & 0x000000001fffffffULL) != 0 ||
        (sample_policy != FloatingSamplePolicy::PreserveIEEEFloat32 &&
         (sample_policy != FloatingSamplePolicy::PreserveQuietNaNFloat32 ||
          (bits & 0x0008000000000000ULL) == 0)))
      throw std::invalid_argument("Float32 source has an unsupported nonfinite payload");
    return std::uint32_t((bits >> 32) & 0x80000000ULL) | 0x7f800000u |
           std::uint32_t((bits & 0x000fffffffffffffULL) >> 29);
  }
  validate_float32_range(sample);
  const float converted = static_cast<float>(sample);
  if (double_sample_bits(static_cast<double>(converted)) != bits)
    throw std::invalid_argument("Float32 source sample is not canonically represented");
  std::uint32_t result;
  std::memcpy(&result, &converted, sizeof(result));
  return result;
}
Image canonical_float32_source(Image image,
                               FloatingSamplePolicy sample_policy) {
  validate_sample_policy(sample_policy);
  validate_image_storage(image);
  if (image.type != PixelType::Float32 || image.channels != 1)
    throw std::invalid_argument("Typed Float32 source requires grayscale Float32 storage");
  kernel_blocks("Float32 source canonicalization", image.pixels.size(),
                [&](std::size_t first, std::size_t last) {
                  for (auto i = first; i < last; ++i) {
                    auto &value = image.pixels[i];
                    const auto bits = double_sample_bits(value);
                    if ((bits & 0x7ff0000000000000ULL) == 0x7ff0000000000000ULL) {
                      // Validation uses only integer bit fields; leave admitted
                      // quiet NaNs unmodified rather than running a cast on them.
                      (void)float32_source_sample_bits(value, sample_policy);
                    } else {
                      validate_float32_range(value);
                      value = static_cast<double>(static_cast<float>(value));
                    }
                  }
                });
  return image;
}
std::array<unsigned char, 4> parse_color(const std::string &name) {
  static const std::map<std::string, std::string> names{
      {"white", "#ffffff"}, {"black", "#000000"},   {"red", "#ff0000"},
      {"green", "#008000"}, {"blue", "#0000ff"},    {"yellow", "#ffff00"},
      {"cyan", "#00ffff"},  {"magenta", "#ff00ff"}, {"gray", "#808080"}};
  auto found = names.find(name);
  std::string v = found == names.end() ? name : found->second;
  if (v.size() == 4 && v[0] == '#')
    v = std::string("#") + v[1] + v[1] + v[2] + v[2] + v[3] + v[3];
  if ((v.size() != 7 && v.size() != 9) || v[0] != '#')
    throw std::invalid_argument("Use a #RRGGBB or #RRGGBBAA color");
  std::array<unsigned char, 4> result{0, 0, 0, 255};
  auto digit = [](char c) {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    throw std::invalid_argument("Invalid color");
  };
  for (std::size_t i = 0; i < (v.size() - 1) / 2; ++i)
    result[i] = static_cast<unsigned char>(digit(v[2 * i + 1]) * 16 +
                                           digit(v[2 * i + 2]));
  return result;
}
OperationResult execute_operation(const std::string &id, const Image &image,
                                  const OperationParameters &parameters) {
  auto validated = validate_operation(id, image, parameters);
  return execute_normalized_operation(id, image, validated);
}
OperationResult
execute_normalized_operation(const std::string &id, const Image &image,
                             const OperationParameters &parameters) {
  validate_operation_image(id, image);
  std::optional<int> count;
  auto output = execute_kernel(id, image, parameters, count);
  if (id == "remove_nans_imagej")
    return {std::move(output), count, false};
  return {canonical_image(std::move(output)), count,
          id == "label" || id == "binary_watershed"};
}
} // namespace imagejxx
