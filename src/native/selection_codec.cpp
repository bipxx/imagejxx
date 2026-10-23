#include "selection_codec.hpp"
#include "session_metadata.hpp"
#include <imagejxx/operations.hpp>
#include <limits>
#include <utility>

namespace imagejxx::detail::selection_codec {
namespace {
namespace scalar = session_metadata;
[[noreturn]] void fail(const char *message) { throw std::invalid_argument(message); }
std::size_t add(std::size_t a, std::size_t b) {
  if (b > std::numeric_limits<std::size_t>::max() - a)
    fail("Session size arithmetic overflow");
  return a + b;
}
std::size_t mul(std::size_t a, std::size_t b) {
  if (b && a > std::numeric_limits<std::size_t>::max() / b)
    fail("Session size arithmetic overflow");
  return a * b;
}
std::size_t b64size(std::size_t bytes) { return mul(add(bytes, 2) / 3, 4); }
void within(std::size_t actual, std::size_t limit, const char *message) {
  if (actual > limit) fail(message);
}
const Json &field(const Json &object, const char *key) {
  static const Json null;
  const auto found = object.is_object() ? object.find(key) : object.end();
  return found == object.end() ? null : *found;
}
void keys(const Json &object, std::initializer_list<const char *> required) {
  if (!object.is_object() || object.size() != required.size())
    fail("Invalid or unknown session structural fields");
  for (const auto *key : required)
    if (!object.contains(key)) fail("Missing session structural field");
}
std::size_t size_integer(const Json &value, bool positive = false) {
  std::uint64_t number;
  if (value.is_number_unsigned()) number = value.get<std::uint64_t>();
  else if (value.is_number_integer() && value.get<std::int64_t>() >= 0)
    number = std::uint64_t(value.get<std::int64_t>());
  else fail("Session dimensions and positions must be integers");
  if (number > std::numeric_limits<std::size_t>::max() || (positive && !number))
    fail("Session integer is outside supported range");
  return std::size_t(number);
}
int integer(const Json &value, bool positive = false) {
  const auto result = size_integer(value, positive);
  if (result > std::size_t(std::numeric_limits<int>::max()))
    fail("Session dimension or position exceeds int32");
  return int(result);
}
double number(const Json &value) {
  if (!value.is_number()) fail("Session field must be numeric");
  const auto result = value.get<double>();
  require_finite(result, "Session number");
  return result;
}
std::string text(const Json &value) {
  if (!value.is_string()) fail("Session field must be text");
  return value.get<std::string>();
}
template <std::size_t N> std::array<double, N> numbers(const Json &value) {
  if (!value.is_array() || value.size() != N) fail("Invalid session numeric array");
  std::array<double, N> result{};
  for (std::size_t i = 0; i < N; ++i) result[i] = number(value[i]);
  return result;
}
std::optional<int> optional_index(const Json &value) {
  return value.is_null() ? std::nullopt : std::optional<int>(integer(value));
}
} // namespace
Json encode_selection(const Selection &value,bool allow_regions,bool allow_paths) {
  if(value.is_path() && !allow_paths)fail("Path geometry requires an explicit codec version");
  if(value.is_path())value.validate(1,1);
  if(value.is_region() && !allow_regions)fail("Regional geometry requires an explicit codec version");
  if(value.is_region())value.validate(1,1);
  Json result{{"kind", value.kind}, {"points", value.points}, {"width", value.width},
               {"attributes", scalar::to_json(value.attributes)}};
  if(value.is_path())result["path"]=value.path_commands();
  if (value.is_mask()) {
    if(value.is_region())result["origin"]=value.mask_origin();
    result["shape"] = {value.mask_height(), value.mask_width()};
    result["mask"] = scalar::encode_base64(value.packed_mask());
  }
  return result;
}
Selection decode_selection(const Json &value, const SessionLimits &limits,bool allow_regions,bool allow_paths) {
  const auto kind = text(field(value, "kind"));
  if(kind=="path") {
    if(!allow_paths)fail("Path geometry requires session version 5");
    keys(value,{"kind","points","width","attributes","path"});
  } else if(kind=="region") {
    if(!allow_regions)fail("Regional geometry requires session version 4");
    keys(value,{"kind","points","width","attributes","shape","mask","origin"});
  } else if (kind == "mask") keys(value, {"kind", "points", "width", "attributes", "shape", "mask"});
  else keys(value, {"kind", "points", "width", "attributes"});
  Selection result(kind);
  const auto &points = value.at("points");
  if (!points.is_array()) fail("Selection points must be an array");
  within(points.size(), limits.max_nodes, "Selection point limit exceeded");
  result.points.reserve(points.size());
  for (const auto &point : points) result.points.push_back(numbers<2>(point));
  result.width = number(value.at("width"));
  result.attributes = scalar::object_from_json(value.at("attributes"));
  if(kind=="path") {
    if(!result.points.empty())fail("Path selection must not contain vector points");
    const auto &path=value.at("path");
    if(!path.is_array())fail("Selection path must be an array");
    within(path.size(),limits.max_nodes,"Selection path node limit exceeded");
    within(path.size(),16*1024*1024/sizeof(double),"Selection path byte limit exceeded");
    std::vector<double> commands;commands.reserve(path.size());
    for(const auto &n:path)commands.push_back(number(n));
    auto restored=Selection::from_path(commands);
    restored.width=result.width;restored.attributes=std::move(result.attributes);
    restored.validate(1,1);return restored;
  }
  if (kind == "mask" || kind=="region") {
    const auto &shape = value.at("shape");
    if (!shape.is_array() || shape.size() != 2) fail("Invalid packed selection shape");
    const int h = integer(shape[0], true), w = integer(shape[1], true);
    const auto count = mul(std::size_t(w), std::size_t(h));
    within(count, limits.max_samples, "Packed selection sample limit exceeded");
    const auto bytes = add(count / 8, count % 8 != 0);
    const auto &encoded = value.at("mask");
    if (!encoded.is_string() || encoded.get_ref<const std::string &>().size() != b64size(bytes))
      fail("Packed selection payload length mismatch");
    std::array<int,2> origin{0,0};
    if(kind=="region") {
      const auto &xy=value.at("origin");
      if(!xy.is_array() || xy.size()!=2)fail("Region origin requires two signed integers");
      for(int axis=0;axis<2;++axis) {
        const auto &n=xy[axis];
        if(n.is_number_unsigned()) {
          if(n.get<std::uint64_t>()>std::uint64_t(std::numeric_limits<int>::max()))fail("Region origin exceeds int32");
          origin[axis]=static_cast<int>(n.get<std::uint64_t>());
        } else if(n.is_number_integer()) {
          const auto number=n.get<std::int64_t>();
          if(number<std::numeric_limits<int>::min() || number>std::numeric_limits<int>::max())fail("Region origin exceeds int32");
          origin[axis]=static_cast<int>(number);
        } else fail("Region origin requires exact signed integers");
      }
      if(!result.points.empty())fail("Packed region must not contain vector points");
    }
    auto packed = scalar::decode_base64(encoded.get_ref<const std::string &>());
    if (packed.size() != bytes) fail("Packed selection payload size mismatch");
    auto restored = kind=="region"?Selection::from_packed_region(w,h,std::move(packed),origin):Selection::from_packed_mask(w, h, std::move(packed));
    restored.points = std::move(result.points); restored.width = result.width;
    restored.attributes = std::move(result.attributes);
    if(kind=="region")restored.validate(1,1);
    return restored;
  }
  return result;
}

Json encode_roi(const Roi &roi,bool allow_regions,bool allow_paths) {
  return {{"name", roi.name}, {"selection", encode_selection(roi.selection,allow_regions,allow_paths)},
      {"t", roi.t ? Json(*roi.t) : Json()}, {"c", roi.c ? Json(*roi.c) : Json()},
      {"z", roi.z ? Json(*roi.z) : Json()},
      {"attributes", scalar::to_json(roi.attributes)}};
}
Roi decode_roi(const Json &item, const SessionLimits &limits,bool allow_regions,bool allow_paths) {
  keys(item, {"name", "selection", "t", "c", "z", "attributes"});
  return {text(item.at("name")), decode_selection(item.at("selection"), limits,allow_regions,allow_paths),
      optional_index(item.at("t")), optional_index(item.at("c")),
      optional_index(item.at("z")),
      scalar::object_from_json(item.at("attributes"))};
}
} // namespace imagejxx::detail::selection_codec
