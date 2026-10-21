#pragma once

#include <imagejxx/dataset.hpp>
#include <map>
#include <memory>
#include <optional>
#include <variant>

namespace imagejxx {
// Dense numeric parameter data, retaining dimensions (e.g. a kernel/matrix).
struct NumericArray {
  std::vector<std::size_t> shape;
  std::vector<double> values;
  void validate() const;
  bool operator==(const NumericArray &other) const {
    return shape == other.shape && values == other.values;
  }
};
using OperationValue =
    std::variant<std::monostate, bool, std::int64_t, std::uint64_t, double,
                 std::string, NumericArray, std::shared_ptr<const Image>>;
using OperationParameters = std::map<std::string, OperationValue>;
enum class ParameterKind {
  Boolean,
  Integer,
  Unsigned,
  Real,
  Text,
  Numeric,
  Image
};
enum class DefaultRule {
  Literal,
  Required,
  Width,
  Height,
  TypeMaximum,
  HalfRange,
  LocalK,
  LocalContrast,
  ChannelLower,
  ChannelUpper,
  LineStart,
  LineEnd,
  StrokePoints,
  IdentityLut
};
enum class GeometryEffect {
  Preserve,
  Crop,
  Flip,
  Rotate90,
  Rotate,
  Resize,
  Affine,
  Bin,
  Canvas,
  Samples
};
enum class DefaultProfile { Execution, DesktopUI, LegacyPythonUI };
struct OperationDefaultContext {
  int width = 256, height = 256, samples = 1;
  PixelType type = PixelType::UInt8;
  std::optional<double> observedMaximum;
  void validate() const;
};
struct OperationParameter {
  std::string name, label;
  ParameterKind kind;
  OperationValue defaultValue, uiDefault;
  std::string uiKind;
  std::vector<OperationValue> choices;
  bool optional = false;
  DefaultRule defaultRule = DefaultRule::Literal, uiRule = DefaultRule::Literal;
};
struct OperationSpec {
  std::string id, title;
  std::vector<std::string> menu;
  std::string description;
  std::vector<OperationParameter> parameters;
  GeometryEffect geometry = GeometryEffect::Preserve;
  // These entries process individual XY planes, not scientific channels or
  // volumes as one neighborhood. Dataset targeting is a separate contract.
  bool planeOnly = true, cancellationInsideKernel = false;
};
struct OperationResult {
  Image image;
  std::optional<int> labelCount;
  // Image keeps the legacy desktop's Float64 storage for labels; Python
  // exports their logical int32 type, matching the public label kernels.
  bool labelImage = false;
};
const std::vector<OperationSpec> &operation_registry();
const OperationSpec &operation_spec(const std::string &id);
// GUI defaults preserve the legacy forms' sample values. Execution defaults
// preserve the native dispatcher's numerical behavior and nullable auto values.
OperationParameters
operation_defaults(const std::string &id, const Image *image = nullptr,
                   DefaultProfile profile = DefaultProfile::Execution);
OperationParameters operation_defaults(const std::string &id,
                                       const OperationDefaultContext &context,
                                       DefaultProfile profile);
OperationParameters validate_operation(const std::string &id,
                                       const Image &image,
                                       const OperationParameters &parameters);
// Resolves parameters and validates structures from metadata, before reading
// any source pixels. Image-dependent kernel constraints still run at execution.
OperationParameters validate_operation(const std::string &id,
                                       const OperationDefaultContext &context,
                                       const OperationParameters &parameters);
// Validate a recorded argument list without loading a source image. Checks
// names, required arguments, types, choices and array structure.
// Image-dependent defaults and kernel/target constraints are resolved when the
// record executes.
void validate_operation_record(const std::string &id,
                               const OperationParameters &parameters = {});
OperationResult execute_operation(const std::string &id, const Image &image,
                                  const OperationParameters &parameters = {});
Image canonical_image(Image image);
// Typed source ingestion, separate from finite-only operation canonicalization.
// Finite values round to Float32 after a checked range test. Opted quiet NaNs
// retain their represented payload. PreserveIEEEFloat32 also preserves infinity
// and signaling NaNs by bits. Other types and non-binary32 payloads are rejected.
Image canonical_float32_source(
    Image image,
    FloatingSamplePolicy sample_policy = FloatingSamplePolicy::FiniteOnly);
// Lossless Float32 bit transport through double-backed source storage. The
// encoder requires an already represented Float32 sample (no implicit rounding).
// Inspect original Float32 bits with the decoder before any floating conversion
// to validate the selected policy without inadvertently quieting signaling NaNs.
double float32_source_sample(
    std::uint32_t bits,
    FloatingSamplePolicy sample_policy = FloatingSamplePolicy::FiniteOnly);
std::uint32_t float32_source_sample_bits(
    double sample,
    FloatingSamplePolicy sample_policy = FloatingSamplePolicy::FiniteOnly);
std::array<unsigned char, 4> parse_color(const std::string &name);
} // namespace imagejxx
