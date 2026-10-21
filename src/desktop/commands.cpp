#include "../native/operation_internal.hpp"
#include "desktop.hpp"
#include <algorithm>
#include <type_traits>

namespace imagejxx::desktop {
namespace {
void numericNode(const Json &node, NumericArray &out, std::size_t depth) {
  if (node.is_array()) {
    if (out.shape.size() == depth)
      out.shape.push_back(node.size());
    else if (out.shape.at(depth) != node.size())
      throw std::invalid_argument("Ragged numeric parameter array");
    for (const auto &part : node)
      numericNode(part, out, depth + 1);
  } else {
    if (!node.is_number() || depth != out.shape.size())
      throw std::invalid_argument("Expected rectangular numeric data");
    out.values.push_back(node.get<double>());
  }
}
OperationValue parameterValue(const Json &value) {
  if (value.is_null())
    return std::monostate{};
  if (value.is_boolean())
    return value.get<bool>();
  if (value.is_number_unsigned())
    return value.get<std::uint64_t>();
  if (value.is_number_integer())
    return value.get<std::int64_t>();
  if (value.is_number_float())
    return value.get<double>();
  if (value.is_string())
    return value.get<std::string>();
  if (value.is_array()) {
    NumericArray out;
    numericNode(value, out, 0);
    out.validate();
    return out;
  }
  throw std::invalid_argument("Unsupported operation parameter");
}
OperationParameters parametersFromJson(const Json &p) {
  if (!p.is_object())
    throw std::invalid_argument("Operation parameters must be an object");
  OperationParameters parameters;
  for (auto it = p.begin(); it != p.end(); ++it)
    parameters[it.key()] = parameterValue(it.value());
  return parameters;
}
Json arrayJson(const NumericArray &array, std::size_t depth,
               std::size_t &offset) {
  if (depth == array.shape.size())
    return array.values[offset++];
  Json result = Json::array();
  for (std::size_t i = 0; i < array.shape[depth]; ++i)
    result.push_back(arrayJson(array, depth + 1, offset));
  return result;
}
Json valueJson(const OperationValue &value) {
  return std::visit(
      [](const auto &v) -> Json {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::monostate>)
          return nullptr;
        else if constexpr (std::is_same_v<T, NumericArray>) {
          std::size_t offset = 0;
          return arrayJson(v, 0, offset);
        } else if constexpr (std::is_same_v<T, std::shared_ptr<const Image>>)
          throw std::logic_error("Images are not catalog literals");
        else
          return v;
      },
      value);
}
} // namespace
OperationParameters nativeOperationParameters(const Json &value) { return parametersFromJson(value); }
PixelType pixelType(const std::string &name) {
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
std::string typeName(PixelType type) {
  switch (type) {
  case PixelType::UInt8:
    return "uint8";
  case PixelType::UInt16:
    return "uint16";
  case PixelType::Float32:
    return "float32";
  default:
    return "float64";
  }
}
Image canonical(Image image) { return canonical_image(std::move(image)); }
Image gray(const Image &image) {
  return image.channels == 1 ? image : canonical(grayscale(image));
}
Image labelsImage(const Labels &labels) {
  Image result(labels.width, labels.height, 1, PixelType::Float64);
  std::copy(labels.pixels.begin(), labels.pixels.end(), result.pixels.begin());
  return result;
}
Json catalog() {
  Json result = Json::array();
  for (const auto &spec : operation_registry()) {
    auto values =
        operation_defaults(spec.id, nullptr, DefaultProfile::DesktopUI);
    Json parameters = Json::array();
    for (const auto &p : spec.parameters) {
      Json choices = Json::array();
      for (const auto &v : p.choices)
        choices.push_back(valueJson(v));
      parameters.push_back({{"name", p.name},
                            {"label", p.label},
                            {"default", valueJson(values.at(p.name))},
                            {"kind", p.uiKind},
                            {"choices", choices},
                            {"optional", p.optional}});
    }
    result.push_back({{"id", spec.id},
                      {"title", spec.title},
                      {"menu", spec.menu},
                      {"description", spec.description},
                      {"parameters", parameters}});
  }
  return result;
}
Json defaults(const Json &command, const Image &image) {
  Json result = Json::object();
  for (const auto &[key, value] : operation_defaults(
           string(get(command, "id")), &image, DefaultProfile::DesktopUI))
    result[key] = valueJson(value);
  return result;
}
Image execute(const std::string &id, const Image &image, const Json &p,
              std::optional<int> *labelCount) {
  auto result = execute_operation(id, image, parametersFromJson(p));
  if (labelCount)
    *labelCount = result.labelCount;
  return std::move(result.image);
}
void validateRecord(const std::string &id, const Json &p) {
  validate_operation_record(id, parametersFromJson(p));
}
std::size_t extraWorkspace(const std::string &id, const Image &image,
                           const Json &p) {
  const OperationDefaultContext context{
      image.width, image.height, image.channels, image.type, {}};
  OperationParameters normalized;
  try {
    normalized = validate_operation(id, context, parametersFromJson(p));
  } catch (const std::invalid_argument &) {
    // Preserve legacy asynchronous argument errors as failed job diagnostics.
    // The worker rejects these arguments before their parameter-sized kernels.
    return 0;
  }
  return operation_extra_workspace(id, context, normalized);
}
PreparedOperation prepare(const std::string &name, const ResolvedTarget &target,
                          const Json &p) {
  return prepare_operation(
      name, target, parametersFromJson(p),
      {DatasetPolicy::LegacyDesktop, GeometryPolicy::LegacyDesktop});
}
} // namespace imagejxx::desktop
