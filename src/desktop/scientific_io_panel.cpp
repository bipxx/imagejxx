#include "scientific_io_panel.hpp"
#include "../io/internal.hpp"
#include "../io/staged.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <mutex>
#include <numeric>

namespace imagejxx::desktop {
namespace {
const char *policy_name(FloatingSamplePolicy policy) {
  switch (policy) {
  case FloatingSamplePolicy::FiniteOnly: return "finite_only";
  case FloatingSamplePolicy::PreserveQuietNaNFloat32: return "preserve_quiet_nan_float32";
  case FloatingSamplePolicy::PreserveIEEEFloat32: return "preserve_ieee_float32";
  }
  throw std::invalid_argument("Invalid TIFF sample policy");
}
FloatingSamplePolicy sample_policy(const Json &value) {
  if (!value.is_string()) throw std::invalid_argument("TIFF sample policy requires text");
  if (value == "finite_only") return FloatingSamplePolicy::FiniteOnly;
  if (value == "preserve_quiet_nan_float32") return FloatingSamplePolicy::PreserveQuietNaNFloat32;
  if (value == "preserve_ieee_float32") return FloatingSamplePolicy::PreserveIEEEFloat32;
  throw std::invalid_argument("Invalid TIFF sample policy");
}
using LimitMember = std::size_t ImageFileLimits::*;
const std::array<std::pair<const char *, LimitMember>, 9> fields{
    {{"max_file_bytes", &ImageFileLimits::max_file_bytes},
     {"max_ifds", &ImageFileLimits::max_ifds},
     {"max_ifd_entries_per_page", &ImageFileLimits::max_ifd_entries_per_page},
     {"max_metadata_bytes", &ImageFileLimits::max_metadata_bytes},
     {"max_total_samples", &ImageFileLimits::max_total_samples},
     {"max_plane_samples", &ImageFileLimits::max_plane_samples},
     {"max_codec_bytes", &ImageFileLimits::max_codec_bytes},
     {"max_working_bytes", &ImageFileLimits::max_working_bytes},
     {"max_output_file_bytes", &ImageFileLimits::max_output_file_bytes}}};
void require(bool value, const char *message) {
  if (!value)
    throw std::invalid_argument(message);
}
bool same(const DocumentSnapshot &a, const DocumentSnapshot &b) {
  return a.same_document(b) && a.shares_data(b) &&
         a.revisions().matches(b.revisions(), Change::Data);
}
Json limits_json(const ImageFileLimits &limits) {
  Json out = Json::object();
  for (auto [name, member] : fields)
    out[name] = limits.*member;
  return out;
}
Json calibration_json(const SpatialCalibration &c) {
  return {{"spacing", c.spacing},
          {"unit", c.unit},
          {"frame_interval", c.frame_interval},
          {"time_unit", c.time_unit}};
}
Json info_json(const ImageSeriesInfo &s) {
  return {{"series_index", s.index},
          {"shape", s.shape.dimensions()},
          {"dtype", io_detail::image_profile(s.pixel_type,s.shape.s).dtype},
          {"calibration", calibration_json(s.calibration)},
          {"metadata", metadataJson(s.metadata)}};
}
MetadataObject export_metadata(const DocumentSnapshot &source,
                               const ImageFileLimits &limits) {
  const auto &p = source.data().metadata.properties;
  const auto it = p.find("format_id");
  if (it == p.end() || !std::holds_alternative<std::string>(it->second) ||
      !io_detail::known_format_id(std::get<std::string>(it->second)))
    return {};
  std::size_t bytes = 512;
  auto charge = [&](std::size_t n) {
    bytes = io_detail::add(bytes, n);
    io_detail::cap(bytes, limits.max_metadata_bytes,
                   "Scientific export provenance metadata limit");
  };
  auto text = [&](const std::string &value) {
    charge(io_detail::add(64, value.size()));
  };
  MetadataObject out;
  for (const auto *key :
       {"format_id", "source_description", "source_imagej_version",
        "source_software", "defaulted_fields", "document_name", "artist",
        "date_time"})
    if (p.count(key)) {
      const auto &value = p.at(key);
      charge(64);
      if (const auto s = std::get_if<std::string>(&value))
        text(*s);
      else if (const auto a = std::get_if<std::vector<std::string>>(&value)) {
        charge(io_detail::mul(64, a->size()));
        for (const auto &item_text : *a)
          text(item_text);
      } else if (const auto tree =
                     std::get_if<std::shared_ptr<const MetadataTree>>(&value)) {
        require(*tree != nullptr, "Invalid scientific export provenance");
        const auto array = std::get_if<MetadataArray>(&(*tree)->value);
        require(array != nullptr, "Scientific provenance requires strings");
        charge(io_detail::mul(64, array->size()));
        for (const auto &item : *array) {
          const auto item_text = std::get_if<std::string>(&item);
          require(item_text != nullptr,
                  "Scientific provenance requires strings");
          text(*item_text);
        }
      } else
        throw std::invalid_argument(
            "Scientific provenance requires text/string arrays");
      out[key] = value;
    }
  return out;
}
std::string display_path(const std::string &path,
                         const ImageFileLimits &limits) {
  io_detail::native_path(path, limits.max_metadata_bytes);
  return std::filesystem::absolute(std::filesystem::u8path(path))
      .lexically_normal()
      .u8string();
}
bool matches_review(const ImageReadResult &result,
                    const ImageSeriesInfo &info) {
  return result.dataset.pixel_type() == info.pixel_type &&
         result.dataset.shape().dimensions() == info.shape.dimensions() &&
         calibration_json(result.dataset.calibration()) ==
             calibration_json(info.calibration) &&
         result.metadata == info.metadata;
}
void closed_object(const Json &value,
                   std::initializer_list<const char *> required,
                   std::initializer_list<const char *> optional = {}) {
  require(value.is_object(), "Scientific IO descriptor requires an object");
  for (auto key : required)
    require(value.contains(key), "Incomplete scientific IO descriptor");
  for (const auto &item : value.items())
    require(std::find(required.begin(), required.end(), item.key()) !=
                    required.end() ||
                std::find(optional.begin(), optional.end(), item.key()) !=
                    optional.end(),
            "Unknown scientific IO descriptor field");
}
std::uint64_t record_integer(const Json &value, std::uint64_t minimum,
                             std::uint64_t maximum) {
  require(value.is_number_integer() &&
              (value.is_number_unsigned() || value.get<std::int64_t>() >= 0),
          "Scientific IO descriptor requires an unsigned integer");
  const auto n = value.get<std::uint64_t>();
  require(n >= minimum && n <= maximum,
          "Scientific IO descriptor integer range");
  return n;
}
double positive_number(const Json &value) {
  require(value.is_number(), "Scientific IO calibration requires a number");
  const auto n = value.get<double>();
  require(std::isfinite(n) && n > 0,
          "Scientific IO calibration must be finite and positive");
  return n;
}
void record_shape(const Json &value, const ImageFileLimits &limits, std::size_t components=1) {
  require(value.is_array() && value.size() == 6,
          "Scientific IO shape requires TCZYXS");
  std::array<std::size_t, 6> dimensions;
  for (std::size_t i = 0; i < dimensions.size(); ++i)
    dimensions[i] = std::size_t(record_integer(value[i], 1, INT32_MAX));
  require(dimensions[5] == components, "Scientific IO shape/component mismatch");
  const auto planes = io_detail::mul(
      io_detail::mul(dimensions[0], dimensions[1]), dimensions[2]);
  const auto area = io_detail::mul(io_detail::mul(dimensions[3], dimensions[4]),components);
  io_detail::cap(planes, limits.max_ifds, "Scientific IO recorded IFD limit");
  io_detail::cap(area, limits.max_plane_samples,
                 "Scientific IO recorded plane limit");
  io_detail::cap(io_detail::mul(planes, area), limits.max_total_samples,
                 "Scientific IO recorded sample limit");
}
void record_calibration(const Json &value) {
  closed_object(value, {"spacing", "unit", "frame_interval", "time_unit"});
  const auto &spacing = value.at("spacing");
  require(spacing.is_array() && spacing.size() == 3,
          "Scientific IO spacing requires XYZ");
  for (const auto &n : spacing)
    positive_number(n);
  positive_number(value.at("frame_interval"));
  require(value.at("unit") == "px" || value.at("unit") == "um" ||
              value.at("unit") == "mm",
          "Scientific IO recorded spatial unit must be canonical");
  require(value.at("time_unit") == "s" || value.at("time_unit") == "ms",
          "Scientific IO recorded time unit must be canonical");
}
void record_names(const Json &value,
                  std::initializer_list<const char *> allowed) {
  require(value.is_array(), "Scientific IO descriptor requires a string array");
  for (auto it = value.begin(); it != value.end(); ++it) {
    require(it->is_string() &&
                std::find(allowed.begin(), allowed.end(), *it) != allowed.end(),
            "Unknown scientific IO descriptor name");
    require(std::find(value.begin(), it, *it) == it,
            "Duplicate scientific IO descriptor name");
  }
}
void record_metadata(const Json &value) {
  closed_object(value,
                {"format_id", "source_description", "source_imagej_version",
                 "defaulted_fields"},
                {"source_software", "document_name", "artist", "date_time"});
  require(value.at("format_id").is_string() && io_detail::known_format_id(value.at("format_id").get<std::string>()),
          "Scientific IO metadata format mismatch");
  for (const auto &item : value.items()) {
    if (item.key() == "defaulted_fields")
      continue;
    require(item.value().is_string(), "Scientific IO provenance requires text");
    for (unsigned char c : item.value().get_ref<const std::string &>())
      require(c > 0 && c < 128,
              "Scientific IO provenance requires ASCII without NUL");
  }
  require(
      !value.at("source_description").get_ref<const std::string &>().empty() &&
          !value.at("source_imagej_version")
               .get_ref<const std::string &>()
               .empty(),
      "Scientific IO source provenance is incomplete");
  record_names(value.at("defaulted_fields"),
               {"legacy_z_from_ifds", "channels", "slices", "frames",
                "hyperstack", "unit", "spacing_x", "spacing_y", "spacing",
                "finterval", "tunit", "xorigin", "yorigin", "zorigin"});
}
void record_path(const std::string &path, const ImageFileLimits &limits) {
  require(!path.empty(), "Scientific IO record path is empty");
  io_detail::cap(path.size(), limits.max_metadata_bytes,
                 "Scientific IO record path metadata limit");
  // Validate syntax only. Do not query the filesystem for an historical path.
  for (std::size_t i = 0; i < path.size();) {
    const auto lead = static_cast<unsigned char>(path[i++]);
    if (lead < 128)
      continue;
    unsigned count;
    std::uint32_t code, minimum;
    if (lead >= 0xc2 && lead <= 0xdf) {
      count = 1;
      code = lead & 31;
      minimum = 128;
    } else if (lead >= 0xe0 && lead <= 0xef) {
      count = 2;
      code = lead & 15;
      minimum = 2048;
    } else {
      require(lead >= 0xf0 && lead <= 0xf4, "Invalid UTF-8 scientific IO path");
      count = 3;
      code = lead & 7;
      minimum = 65536;
    }
    require(count <= path.size() - i, "Truncated UTF-8 scientific IO path");
    while (count--) {
      const auto byte = static_cast<unsigned char>(path[i++]);
      require((byte & 0xc0) == 0x80, "Invalid UTF-8 scientific IO path");
      code = (code << 6) | (byte & 63);
    }
    require(code >= minimum && code <= 0x10ffff &&
                !(code >= 0xd800 && code <= 0xdfff),
            "Invalid UTF-8 scientific IO code point");
  }
}
} // namespace
Json scientific_io_replay_draft(const Json &record) {
  require(record.is_object(), "Scientific IO record must be an object");
  const std::vector<std::string> allowed{"container", "storage", "predictor", "compression", "components", "dtype", "sample_policy", "schema",
                                         "version",
                                         "operation",
                                         "format_id",
                                         "path",
                                         "series_index",
                                         "limits",
                                         "replace_existing",
                                         "pixels_and_calibration_only",
                                         "replay_policy",
                                         "file_identity_policy",
                                         "source_document_id",
                                         "source_data_identity",
                                         "source_shape",
                                         "inspected_file_bytes",
                                         "reviewed_series",
                                         "encoded_calibration",
                                         "canonicalized_fields",
                                         "xy_resolution"};
  for (const auto &field : record.items())
    require(std::find(allowed.begin(), allowed.end(), field.key()) !=
                allowed.end(),
            "Unknown scientific IO record field");
  require(record.at("schema") == "imagejxx.scientific-image-io" &&
              record.at("version").is_number_integer() &&
              !record.at("version").is_boolean() && (record.at("version") == 1 || record.at("version") == 2 || record.at("version") == 3 || record.at("version") == 4 || record.at("version") == 5 || record.at("version") == 6 || record.at("version") == 7 || record.at("version") == 8 || record.at("version") == 9 || record.at("version") == 10 || record.at("version") == 11 || record.at("version") == 12 || record.at("version") == 13 || record.at("version") == 14 || record.at("version") == 15),
          "Unsupported scientific IO record");
  const bool bigtiff=record.at("version")==15;
  require(record.contains("container")==bigtiff,"TIFF record container/version mismatch");
  if(bigtiff)require(record.at("container")=="bigtiff","Version 15 requires BigTIFF");
  const bool storage=bigtiff || record.at("version")==14 || record.at("version")==10 || record.at("version")==11 || record.at("version")==12 || record.at("version")==13;
  require(record.contains("storage")==storage,"TIFF record storage/version mismatch");
  if(bigtiff)require(record.at("storage")=="tiled" || record.at("storage")=="strips","Unknown BigTIFF storage");
  const bool tiled=bigtiff?record.at("storage")=="tiled":record.at("version")==14 || record.at("version")==10 || record.at("version")==11 || record.at("version")==12 || record.at("version")==13;
  if(tiled)require(record.at("storage")=="tiled","Tiled records require tiled storage");
  const bool compressed = bigtiff || record.at("version") == 14 || record.at("version") == 13 || record.at("version") == 12 || record.at("version") == 5 || record.at("version") == 6 || record.at("version") == 7 || record.at("version") == 8 || record.at("version") == 9;
  require(record.contains("compression")==compressed,"TIFF record compression/version mismatch");
  const bool predicted=bigtiff || record.at("version")==14 || record.at("version")==8 || record.at("version")==9;
  const unsigned prediction=bigtiff?(record.value("predictor",std::string())=="none"?1:record.value("predictor",std::string())=="floating"?3:2):record.at("version")==14?(record.value("predictor",std::string())=="floating"?3:2):record.at("version")==9?3:predicted?2:1;
  require(record.contains("predictor")==predicted,"TIFF record predictor/version mismatch");
  if(predicted)require(record.at("predictor")== (prediction==3?"floating":prediction==2?"horizontal":"none"),"TIFF record predictor/version mismatch");
  unsigned codec=1;
  if(compressed) {
    require(record.at("compression").is_string(),"TIFF record compression requires text");
    codec=io_detail::compression_value(record.at("compression").get<std::string>());
    if(record.at("version")==5)require(codec==8,"Version 5 requires Deflate");
    if(record.at("version")==6)require(codec!=5,"Version 6 predates LZW");
    if(record.at("version")==7)require(codec==5,"Version 7 requires LZW");
  }
  if(record.at("version")==12)require(codec==8,"Version 12 requires Deflate tiled storage");
  if(record.at("version")==13)require(codec==32773 || codec==5,"Version 13 requires PackBits/LZW tiled storage");
  const bool layout = record.at("version") == 4 || compressed || tiled;
  const bool scalar = record.at("version") == 3 || layout;
  require(record.contains("components") == layout, "TIFF record component/version mismatch");
  const auto components=layout?record_integer(record.at("components"),1,3):1;
  require(record.contains("dtype") == scalar, "TIFF record dtype/version mismatch");
  PixelType type = record.at("version") == 2 ? PixelType::Float32 : PixelType::UInt16;
  if (scalar) {
    const auto &dtype = record.at("dtype");
    require(dtype == "uint8" || dtype == "uint16" || dtype == "float32", "Unknown scalar TIFF record dtype");
    type = dtype == "uint8" ? PixelType::UInt8 : dtype == "uint16" ? PixelType::UInt16 : PixelType::Float32;
  }
  if(record.at("version")==10)require(type==PixelType::UInt16 && components==1,"Version 10 requires scalar UInt16 tiles");
  if(record.at("version")==11)require(type!=PixelType::UInt16,"Version 11 requires UInt8/RGB8/Float32 tiles");
  const auto profile = io_detail::image_profile(type,components,codec,prediction,tiled);
  const bool floating = type == PixelType::Float32;
  const auto extra_fields = bigtiff ? 7 : record.at("version")==14 ? 6 : (record.at("version")==12 || record.at("version")==13) ? 5 : tiled ? 4 : predicted ? 5 : compressed ? 4 : layout ? 3 : scalar ? 2 : floating ? 1 : 0;
  require(record.contains("sample_policy") == (floating || scalar), "TIFF record sample policy/version mismatch");
  if (floating || scalar) require(floating || sample_policy(record.at("sample_policy")) == FloatingSamplePolicy::FiniteOnly, "Unsigned TIFF records require finite-only samples");
  if (floating) (void)sample_policy(record.at("sample_policy"));
  require(record.at("format_id") == io_detail::container_format_id(profile.id,bigtiff) &&
              record.at("replay_policy") == "explicit_review_required" &&
              record.at("file_identity_policy") ==
                  "reopen_and_review_then_hold_identity_size_change_tokens",
          "Unsupported scientific IO record policy");
  require(record.at("operation") == "open" ||
              record.at("operation") == "export",
          "Unknown scientific IO operation");
  require(record.at("series_index").is_number_integer() &&
              !record.at("series_index").is_boolean() &&
              record.at("series_index") == 0,
          "Scientific IO series index must be zero");
  require(record.at("replace_existing").is_boolean() &&
              record.at("pixels_and_calibration_only").is_boolean(),
          "Scientific IO record choices must be boolean");
  require(record.at("path").is_string() &&
              record.at("path").get_ref<const std::string &>().size() <=
                  131064 &&
              record.at("path").get_ref<const std::string &>().find('\0') ==
                  std::string::npos,
          "Invalid scientific IO record path");
  const auto &limits = record.at("limits");
  require(limits.is_object() && limits.size() == fields.size(),
          "Scientific IO record limits are incomplete");
  ImageFileLimits parsed;
  for (auto [name, member] : fields) {
    const auto &value = limits.at(name);
    require(value.is_number_integer() && !value.is_boolean(),
            "Scientific IO limits require positive integers");
    require(value.is_number_unsigned() ? value.get<std::uint64_t>() > 0
                                       : value.get<std::int64_t>() > 0,
            "Scientific IO limits must be positive");
    auto n = value.get<std::uint64_t>();
    require(n <= SIZE_MAX, "Scientific IO record limit overflow");
    parsed.*member = std::size_t(n);
  }
  io_detail::valid_limits(parsed);
  std::size_t bytes = 0, nodes = 0;
  const auto bound =
      io_detail::add(io_detail::mul(4, parsed.max_metadata_bytes), 8192);
  std::function<void(const Json &, std::size_t)> visit =
      [&](const Json &value, std::size_t depth) {
        require(depth <= 8 && ++nodes <= 512,
                "Scientific IO record structure limit");
        bytes = io_detail::add(bytes, 64);
        if (value.is_string())
          bytes = io_detail::add(bytes,
                                 value.get_ref<const std::string &>().size());
        if (value.is_number_float())
          require(std::isfinite(value.get<double>()),
                  "Scientific IO record numbers must be finite");
        io_detail::cap(bytes, bound, "Scientific IO record byte limit");
        if (value.is_array())
          for (const auto &child : value)
            visit(child, depth + 1);
        if (value.is_object())
          for (const auto &child : value.items()) {
            bytes = io_detail::add(bytes, child.key().size());
            visit(child.value(), depth + 1);
          }
      };
  visit(record, 0);
  // Bound the tree before inspecting nested historical descriptors. These are
  // provenance only: never reuse them to authorize a read or publication.
  record_path(record.at("path").get_ref<const std::string &>(), parsed);
  if (record.at("operation") == "open") {
    require(record.size() == 13 + extra_fields,
            "Scientific open record fields are incomplete or mixed");
    require(!record.at("replace_existing").get<bool>() &&
                !record.at("pixels_and_calibration_only").get<bool>(),
            "Scientific open record has export choices");
    record_integer(record.at("inspected_file_bytes"), 1, parsed.max_file_bytes);
    const auto &series = record.at("reviewed_series");
    closed_object(
        series, {"series_index", "shape", "dtype", "calibration", "metadata"});
    record_integer(series.at("series_index"), 0, 0);
    require(series.at("dtype") == profile.dtype,
            "Scientific IO recorded pixel type mismatch");
    record_shape(series.at("shape"), parsed,components);
    record_calibration(series.at("calibration"));
    record_metadata(series.at("metadata"));
    require(series.at("metadata").at("format_id") == record.at("format_id"), "TIFF record metadata/profile mismatch");
  } else {
    require(record.size() == 17 + extra_fields,
            "Scientific export record fields are incomplete or mixed");
    require(record.at("pixels_and_calibration_only").get<bool>(),
            "Scientific export record lacks loss consent");
    record_integer(record.at("source_document_id"), 1, UINT64_MAX);
    const auto &identity = record.at("source_data_identity");
    if (!identity.is_null())
      record_integer(identity, 0, UINT64_MAX);
    record_shape(record.at("source_shape"), parsed,components);
    const auto &cal = record.at("encoded_calibration");
    record_calibration(cal);
    record_names(record.at("canonicalized_fields"),
                 {"unit", "time_unit", "defaulted_fields", "source_description",
                  "source_imagej_version", "source_software", "format_id"});
    const auto &resolution = record.at("xy_resolution");
    require(resolution.is_array() && resolution.size() == 2,
            "Scientific IO resolution requires XY");
    for (std::size_t axis = 0; axis != 2; ++axis) {
      const auto &pair = resolution[axis];
      require(pair.is_array() && pair.size() == 2,
              "Scientific IO rational requires numerator/denominator");
      const auto n = record_integer(pair[0], 1, UINT32_MAX),
                 d = record_integer(pair[1], 1, UINT32_MAX);
      require(std::gcd(n, d) == 1 && double(d) / double(n) ==
                                         cal.at("spacing")[axis].get<double>(),
              "Scientific IO rational disagrees with encoded calibration");
    }
  }
  Json draft = {{"exporting", record.at("operation") == "export"},
          {"path", record.at("path")}, {"limits", limits}};
  if ((floating || scalar) && record.at("operation") == "open") draft["sample_policy"] = record.at("sample_policy");
  if(compressed && record.at("operation")=="export")draft["compression"]=io_detail::compression_name(codec);
  if(storage && record.at("operation")=="export")draft["storage"]=tiled?"tiled":"strips";
  if(bigtiff && record.at("operation")=="export")draft["container"]="bigtiff";
  if(predicted && record.at("operation")=="export")draft["predictor"]=prediction==3?"floating":prediction==2?"horizontal":"none";
  return draft;
}
struct ScientificIoPanel::State {
  struct Result {
    std::optional<ImageFileInspection> inspection;
    std::shared_ptr<io_detail::Source> input_guard;
    std::optional<ImageWriteEstimate> estimate;
    std::shared_ptr<imagejxx::Document> document;
    std::unique_ptr<io_detail::StagedImageWrite> staged;
    std::shared_ptr<const void> reservation;
  };
  struct Cell {
    std::mutex mutex;
    std::optional<Result> result;
    bool discard = false;
  };
  std::shared_ptr<JobScheduler> scheduler;
  std::atomic<std::uint64_t> cancel_id{0}, cancel_epoch{0};
  Job job;
  std::shared_ptr<Cell> cell;
  bool open = false, focus = false, exporting = false, recording = false,
       axes = false, loss = false, replace = false, flat = false,
       fixed_path = false, job_canceled = false;
  bool pending = false;
  int phase = 0;
  std::uint64_t generation = 0, worker_generation = 0;
  std::optional<DocumentSnapshot> source;
  ImageFileLimits limits;
  FloatingSamplePolicy sample_policy = FloatingSamplePolicy::FiniteOnly;
  unsigned compression = 1, predictor = 1;
  bool tiled = false, bigtiff = false;
  std::size_t cap = 0;
  std::string path, error, message, reason;
  Json controls = Json::object();
  std::optional<ImageFileInspection> inspection;
  std::shared_ptr<io_detail::Source> input_guard;
  std::optional<ImageWriteEstimate> estimate;
  explicit State(std::shared_ptr<JobScheduler> s) : scheduler(std::move(s)) {}
  ~State() {
    if (job.valid()) {
      job.cancel();
      clear_cell();
      job.wait();
    }
  }
  void clear_cell() {
    if (!cell)
      return;
    std::optional<Result> retired;
    {
      std::lock_guard<std::mutex> lock(cell->mutex);
      cell->discard = true;
      retired = std::move(cell->result);
      cell->result.reset();
    }
    cell.reset();
  }
  void invalidate() {
    if (job.valid())
      job.cancel();
    clear_cell();
    inspection.reset();
    input_guard.reset();
    estimate.reset();
    axes = loss = flat = false;
    ++generation;
    error.clear();
    message.clear();
  }
  bool working() const { return pending && job.valid() && !job.ready(); }
  template <class Work> void start(int next, Work work) {
    require(!pending, "Wait for the scientific IO worker");
    auto target = std::make_shared<Cell>();
    JobOptions options;
    options.label = next == 1   ? "Inspect scientific TIFF"
                    : next == 2 ? "Read scientific TIFF"
                    : next == 3 ? "Estimate scientific TIFF"
                                : "Stage scientific TIFF export";
    options.document_id = source ? source->id() : 0;
    options.supersedes = "imagejxx.scientific-io";
    options.resource_bytes = limits.max_working_bytes;
    options.retain_result_resources = next == 2 || next == 4;
    const auto epoch = cancel_epoch.load();
    auto next_job = scheduler->submit(
        [target, work = std::move(work),
         next](const JobContext &context) mutable {
          context.checkpoint();
          auto value = work(context);
          context.checkpoint();
          if (next == 2 || next == 4)
            value.reservation = context.result_resource_guard();
          {
            std::lock_guard<std::mutex> lock(target->mutex);
            if (!target->discard)
              target->result = std::move(value);
          }
        },
        options);
    job = std::move(next_job);
    cancel_id.store(job.info().id);
    if (epoch != cancel_epoch.load())
      job.cancel();
    cell = std::move(target);
    phase = next;
    job_canceled = false;
    pending = true;
    worker_generation = generation;
    error.clear();
    message.clear();
  }
  Json record() const {
    const auto type = source ? source->data().dataset.pixel_type() : inspection->series.at(0).pixel_type;
    const auto components=source?source->data().dataset.shape().s:inspection->series.at(0).shape.s;
    const unsigned codec=exporting?compression:inspection?io_detail::compression_of(inspection->format):1;
    const unsigned prediction=exporting?predictor:inspection?io_detail::predictor_of(inspection->format):1;
    const bool is_tiled=exporting?tiled:inspection?io_detail::image_profile(inspection->format).tiled:false;
    const bool is_big=exporting?bigtiff:inspection && inspection->container==TiffContainer::BigTiff;
    const bool compressed=codec!=1;
    const auto profile = io_detail::image_profile(type,components,codec,prediction,is_tiled);
    const bool floating = type == PixelType::Float32;
    const bool layout = is_big || components!=1 || compressed || is_tiled;
    const bool scalar = type == PixelType::UInt8 || layout;
    Json out = {{"schema", "imagejxx.scientific-image-io"},
                {"version", is_big ? 15 : is_tiled ? (prediction!=1?14:compressed?(codec==8?12:13):type==PixelType::UInt16?10:11) : prediction==3 ? 9 : prediction==2 ? 8 : codec==5 ? 7 : codec==32773 ? 6 : compressed ? 5 : layout ? 4 : scalar ? 3 : floating ? 2 : 1},
                {"operation", exporting ? "export" : "open"},
                {"format_id", io_detail::container_format_id(profile.id,is_big)},
                {"path", path},
                {"series_index", 0},
                {"limits", limits_json(limits)},
                {"replace_existing", replace},
                {"pixels_and_calibration_only", exporting && loss},
                {"replay_policy", "explicit_review_required"},
                {"file_identity_policy",
                 "reopen_and_review_then_hold_identity_size_change_tokens"}};
    if(is_big){out["container"]="bigtiff";out["storage"]=is_tiled?"tiled":"strips";}
    else if(is_tiled)out["storage"]="tiled";
    if(is_big || prediction!=1)out["predictor"]=prediction==3?"floating":prediction==2?"horizontal":"none";
    if(is_big || compressed)out["compression"]=io_detail::compression_name(codec);
    if (layout) out["components"] = components;
    if (scalar) out["dtype"] = profile.dtype;
    if (floating || scalar) out["sample_policy"] = policy_name(source ? source->data().dataset.sample_policy() : sample_policy);
    if (source) {
      out["source_document_id"] = source->id();
      out["source_data_identity"] =
          source->data_identity() ? Json(*source->data_identity()) : Json();
      out["source_shape"] = source->data().dataset.shape().dimensions();
    }
    if (inspection) {
      out["inspected_file_bytes"] = inspection->file_bytes;
      out["reviewed_series"] = info_json(inspection->series.at(0));
    }
    if (estimate) {
      out["encoded_calibration"] =
          calibration_json(estimate->encoded_calibration);
      out["canonicalized_fields"] = estimate->canonicalized_fields;
      out["xy_resolution"] = estimate->xy_resolution;
    }
    return out;
  }
  void observe(const char *name) {
    const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    controls[name] = {
        {"id", ImGui::GetItemID()},
        {"enabled", !(ImGui::GetCurrentContext()->LastItemData.ItemFlags &
                      ImGuiItemFlags_Disabled)},
        {"point", {(a.x + b.x) / 2, (a.y + b.y) / 2}},
        {"rect", {a.x, a.y, b.x, b.y}}};
  }
};
ScientificIoPanel::ScientificIoPanel(std::shared_ptr<JobScheduler> s)
    : state_(std::make_unique<State>(std::move(s))) {}
ScientificIoPanel::~ScientificIoPanel() = default;
void ScientificIoPanel::show(bool exporting,
                             const std::optional<DocumentSnapshot> &source,
                             std::size_t cap, std::string path, bool recording,
                             bool fixed_path) {
  auto &s = *state_;
  require(!s.pending, "Wait for the previous scientific IO worker");
  require(!exporting || source.has_value(), "Export requires an active source");
  require(cap > 65536,
          "Scientific IO requires a positive processing memory budget");
  cancel("reopened");
  s.exporting = exporting;
  s.source = exporting ? source : std::optional<DocumentSnapshot>{};
  s.cap = cap;
  s.recording = recording;
  s.fixed_path = fixed_path;
  s.job_canceled = false;
  s.limits = {};
  s.limits.max_working_bytes = std::min(s.limits.max_working_bytes, cap);
  s.limits.max_codec_bytes =
      std::min(s.limits.max_codec_bytes, std::max(std::size_t(1), cap / 8));
  s.path = std::move(path);
  s.replace = false;
  s.sample_policy = FloatingSamplePolicy::FiniteOnly;
  s.compression=1;s.predictor=1;s.tiled=false;s.bigtiff=false;
  s.open = s.focus = true;
  s.reason.clear();
}
void ScientificIoPanel::patch(const Json &patch) {
  auto &s = *state_;
  require(s.open && !s.pending, "Scientific IO form must be idle");
  require(patch.is_object(), "Scientific IO options require an object");
  auto path = s.path;
  auto limits = s.limits;
  auto policy = s.sample_policy;
  auto compression=s.compression,predictor=s.predictor;
  bool tiled=s.tiled,bigtiff=s.bigtiff;
  bool axes = s.axes, loss = s.loss, replace = s.replace, flat = s.flat;
  bool changed = false;
  for (const auto &field : patch.items()) {
    if (field.key() == "path") {
      require(field.value().is_string(), "Scientific IO path requires text");
      path = field.value().get<std::string>();
      require(path.size() <= 131064 && path.find('\0') == std::string::npos,
              "Scientific IO path length/NUL");
      require(!s.fixed_path || path == s.path,
              "A startup review must keep its planned file path");
      changed |= path != s.path;
    } else if (field.key() == "container") {
      require(s.exporting && field.value().is_string() && (field.value()=="classic" || field.value()=="bigtiff"),"Container is an export choice: classic or bigtiff");
      bigtiff=field.value()=="bigtiff";changed|=bigtiff!=s.bigtiff;
    } else if (field.key() == "storage") {
      require(s.exporting && field.value().is_string() && (field.value()=="strips" || field.value()=="tiled"),"Storage is an export choice: strips or tiled");
      tiled=field.value()=="tiled";changed|=tiled!=s.tiled;
    } else if (field.key() == "compression") {
      require(s.exporting && field.value().is_string(),"Compression is an export choice requiring text");
      compression=io_detail::compression_value(field.value().get<std::string>());changed|=compression!=s.compression;
    } else if (field.key() == "predictor") {
      require(s.exporting && field.value().is_string(),"Prediction is an export choice requiring text");
      require(field.value()=="none" || field.value()=="horizontal" || field.value()=="floating","Unknown TIFF predictor");
      predictor=field.value()=="floating"?3:field.value()=="horizontal"?2:1;changed|=predictor!=s.predictor;
    } else if (field.key() == "sample_policy") {
      require(!s.exporting, "Sample policy is an import choice");
      policy = sample_policy(field.value());

    } else if (field.key() == "confirm_axes") {
      require(field.value().is_boolean() && !s.exporting && bool(s.inspection),
              "Inspect axes before confirming");
      axes = field.value().get<bool>();
    } else if (field.key() == "accept_pixels_and_calibration_only") {
      require(field.value().is_boolean() && s.exporting && bool(s.estimate),
              "Review export before accepting losses");
      loss = field.value().get<bool>();
    } else if (field.key() == "accept_flat_pixels_only") {
      require(field.value().is_boolean() && !s.exporting,
              "Flat pixels are an import choice");
      flat = field.value().get<bool>();
    } else if (field.key() == "replace_existing") {
      require(field.value().is_boolean() && s.exporting,
              "Replacement is an export choice");
      replace = field.value().get<bool>();
    } else if (field.key() == "limits") {
      require(field.value().is_object(),
              "Scientific IO limits require an object");
      for (const auto &value : field.value().items()) {
        auto at = std::find_if(fields.begin(), fields.end(),
                               [&](auto f) { return value.key() == f.first; });
        require(at != fields.end() && value.value().is_number_integer() &&
                    !value.value().is_boolean(),
                "Unknown/noninteger scientific IO limit");
        require(!value.value().is_number_unsigned()
                    ? value.value().get<std::int64_t>() > 0
                    : value.value().get<std::uint64_t>() > 0,
                "Scientific IO limits must be positive");
        auto n = value.value().get<std::uint64_t>();
        require(n <= SIZE_MAX, "Scientific IO limit overflow");
        limits.*(at->second) = std::size_t(n);
      }
      io_detail::valid_limits(limits);
      require(limits.max_working_bytes <= s.cap,
              "Working bytes exceed processing memory setting");
      changed = true;
    } else
      throw std::invalid_argument("Unknown scientific IO option");
  }
  if(compression!=8 && compression!=5 && predictor!=1) {
    require(!patch.contains("predictor"),"Prediction requires Deflate or LZW");predictor=1;changed=true;
  }
  if(predictor==3)require(s.source && s.source->data().dataset.pixel_type()==PixelType::Float32,"Floating prediction requires Float32 samples");
  if(predictor==2)require(s.source && s.source->data().dataset.pixel_type()!=PixelType::Float32,"Horizontal prediction requires unsigned integer samples");
  if(tiled) {
    require(s.source.has_value(),"Tiled export requires a source");
    (void)io_detail::image_profile(s.source->data().dataset.pixel_type(),s.source->data().dataset.shape().s,compression,predictor,true);
  }
  if (changed) {
    s.invalidate();
    axes = loss = flat = false;
    replace = false;
  } else if (policy != s.sample_policy) {
    // Sample admission does not change inspected layout/calibration or identity.
    // Keep the held descriptor and require renewed consent for this read policy.
    ++s.generation;
    axes = flat = false;
    s.error.clear();
    s.message.clear();
  }
  s.path = std::move(path);
  s.limits = limits;
  s.sample_policy = policy;
  s.compression=compression;s.predictor=predictor;s.tiled=tiled;s.bigtiff=bigtiff;
  s.axes = axes;
  s.loss = loss;
  s.replace = replace;
  s.flat = flat;
}
void ScientificIoPanel::inspect() {
  auto &s = *state_;
  require(s.open && !s.pending, "Scientific IO form must be idle");
  s.axes = s.loss = s.flat = false;
  s.inspection.reset();
  s.input_guard.reset();
  s.estimate.reset();
  const auto limits = s.limits;
  if (s.exporting) {
    const auto source = *s.source;
    auto metadata = export_metadata(source, limits);
    ImageWriteRequest request;
    request.format = io_detail::image_profile(source.data().dataset.pixel_type(),source.data().dataset.shape().s,s.compression,s.predictor,s.tiled).format;
    request.container=s.bigtiff?TiffContainer::BigTiff:TiffContainer::Classic;
    s.start(3, [source, metadata, limits, request](const JobContext &context) {
      State::Result result;
      context.checkpoint();
      result.estimate =
          estimate_image_write(source.data().dataset, metadata, request, limits);
      return result;
    });
  } else {
    const auto path = display_path(s.path, limits);
    s.path = path;
    s.start(1, [path, limits](const JobContext &context) {
      State::Result result;
      auto control = context.execution_control();
      result.input_guard = std::make_shared<io_detail::Source>(
          io_detail::native_path(path, limits.max_metadata_bytes),
          limits.max_file_bytes, control);
      result.inspection = inspect_image_file(path, limits, control);
      result.input_guard->unchanged();
      return result;
    });
  }
}
void ScientificIoPanel::apply() {
  auto &s = *state_;
  require(s.open && !s.pending, "Scientific IO form must be idle");
  const auto limits = s.limits;
  const auto path = display_path(s.path, limits);
  s.path = path;
  if (s.exporting) {
    require(s.estimate.has_value() && s.loss,
            "Review and accept pixels/calibration-only export");
    auto extension = std::filesystem::u8path(path).extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    require(extension == ".tif" || extension == ".tiff",
            "Use .tif or .tiff for a scientific TIFF export");
    const auto source = *s.source;
    auto metadata = export_metadata(source, limits);
    ImageWriteRequest request;
    request.format = io_detail::image_profile(source.data().dataset.pixel_type(),source.data().dataset.shape().s,s.compression,s.predictor,s.tiled).format;
    request.container=s.bigtiff?TiffContainer::BigTiff:TiffContainer::Classic;
    request.replace = s.replace ? FileReplacePolicy::ReplaceExisting
                                : FileReplacePolicy::FailIfExists;
    s.start(4, [path, source, metadata, request,
                limits](const JobContext &context) {
      State::Result result;
      result.staged = io_detail::stage_image_series(
          path, source.data().dataset, metadata, request, limits,
          context.execution_control());
      return result;
    });
  } else {
    require(s.inspection.has_value() && s.axes && bool(s.input_guard),
            "Inspect and confirm file axes/calibration");
    const auto review = s.inspection->series.at(0);
    auto held = s.input_guard;
    ImageReadRequest request; request.sample_policy = s.sample_policy;
    s.start(2, [path, review, held, limits, request](const JobContext &context) {
      State::Result result;
      held->unchanged();
      auto decoded =
          read_image_series(path, request, limits, context.execution_control());
      held->unchanged();
      require(matches_review(decoded, review),
              "File descriptors changed after review");
      DocumentData data{std::move(decoded.dataset)};
      data.metadata.title = filename(path);
      data.metadata.properties = std::move(decoded.metadata);
      result.document = std::make_shared<imagejxx::Document>(std::move(data));
      return result;
    });
  }
}
void ScientificIoPanel::cancel(const char *reason) {
  auto &s = *state_;
  s.invalidate();
  s.open = false;
  s.source.reset();
  s.reason = reason;
  s.controls = Json::object();
}
void ScientificIoPanel::request_cancel() const {
  state_->cancel_epoch.fetch_add(1);
  if (auto id = state_->cancel_id.load())
    state_->scheduler->cancel(id);
}
void ScientificIoPanel::wait() const {
  if (state_->job.valid())
    state_->job.wait();
}
bool ScientificIoPanel::wait_for(std::chrono::milliseconds t) const {
  return !state_->job.valid() ||
         state_->job.wait_for(std::max(t, std::chrono::milliseconds(0)));
}
bool ScientificIoPanel::working() const { return state_->pending; }
bool ScientificIoPanel::visible() const { return state_->open; }
std::optional<JobInfo> ScientificIoPanel::job_info() const {
  return state_->pending && state_->job.valid()
             ? std::optional<JobInfo>{state_->job.info()}
             : std::optional<JobInfo>{};
}
std::uint64_t ScientificIoPanel::generation() const {
  return state_->generation;
}
bool ScientificIoPanel::browse_current(
    std::uint64_t generation,
    const std::optional<DocumentSnapshot> &current) const {
  const auto &s = *state_;
  return s.open && !s.pending && s.generation == generation &&
         (!s.source || (current && same(*s.source, *current)));
}
void ScientificIoPanel::tick(const std::optional<DocumentSnapshot> &current,
                             bool busy, std::size_t cap,
                             const OpenPublication &publish_open,
                             const ExportPublication &publish_export) {
  auto &s = *state_;
  if (s.open && (busy || cap != s.cap ||
                 (s.source && (!current || !same(*s.source, *current)))))
    cancel(busy ? "busy" : "source_or_limits_changed");
  if (!s.pending || !s.job.ready())
    return;
  s.pending = false;
  s.cancel_id.store(0);
  try {
    s.job.rethrow();
    require(s.open && s.worker_generation == s.generation,
            "Scientific IO request canceled");
    State::Result result;
    {
      std::lock_guard<std::mutex> lock(s.cell->mutex);
      require(s.cell->result.has_value(), "Scientific IO result unavailable");
      result = std::move(*s.cell->result);
      s.cell->result.reset();
    }
    s.clear_cell();
    if (s.phase == 1)
      s.job.complete([&] {
        s.inspection = std::move(result.inspection);
        s.input_guard = std::move(result.input_guard);
        s.message =
            "Review explicit C/Z/T axes and calibration before opening.";
      });
    else if (s.phase == 3)
      s.job.complete([&] {
        s.estimate = std::move(result.estimate);
        s.message = "Review calibration precision and excluded document state.";
      });
    else {
      auto record = s.record();
      s.job.complete([&] {
        if (s.phase == 2) {
          require(bool(s.input_guard), "Reviewed input guard is unavailable");
          s.input_guard->unchanged();
          publish_open(std::move(result.document), record, s.recording);
        } else
          publish_export(*s.source, record, s.recording,
                         [&] { result.staged->publish(); });
      });
      s.open = false;
      s.inspection.reset();
      s.input_guard.reset();
      s.estimate.reset();
      s.source.reset();
      s.message = s.phase == 2 ? "Scientific image opened"
                               : "Scientific image exported";
    }
  } catch (const std::exception &e) {
    s.clear_cell();
    s.job_canceled = dynamic_cast<const OperationCancelled *>(&e) != nullptr;
    if (s.open) {
      s.error = e.what();
      s.axes = s.loss = false;
    }
  }
  s.phase = 0;
}
std::string ScientificIoPanel::flat_import_path() const {
  const auto &s = *state_;
  require(s.open && !s.pending && !s.exporting && s.flat,
          "Confirm flat pixels on an idle TIFF import form");
  require(!s.path.empty() && s.path.find('\0') == std::string::npos,
          "Choose a bounded TIFF path without NUL");
  std::string suffix = std::filesystem::u8path(s.path).extension().u8string();
  std::transform(suffix.begin(), suffix.end(), suffix.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  require(suffix == ".tif" || suffix == ".tiff", "Choose a TIFF filename");
  return s.path;
}
std::optional<ScientificIoPanel::FlatPixels> ScientificIoPanel::flat_tiled_pixels() const {
  const auto &s=*state_;
  if(!s.inspection || !io_detail::image_profile(s.inspection->format).tiled)return {};
  (void)flat_import_path();const auto revalidate=import_revalidation();revalidate();
  const auto path_bytes=s.path.size();
  auto layout=io_detail::inspect(*s.input_guard,path_bytes,s.limits,{});
  layout.info.sample_policy=layout.info.pixel_type==PixelType::Float32?s.sample_policy:FloatingSamplePolicy::FiniteOnly;
  require(layout.tiled,"Flat tiled import requires the reviewed tile layout");
  // The flattened Stack temporarily coexists with the decoded Dataset. Reserve
  // its entire sample storage before decoding or copying any plane.
  const auto extra=io_detail::add(io_detail::mul(8,layout.info.shape.pixel_count()),io_detail::mul(256,layout.pages.size()));
  io_detail::cap(io_detail::add(io_detail::read_working(layout,s.limits),extra),s.limits.max_working_bytes,"Flat tiled import working-byte limit");
  const auto dataset=io_detail::decode(*s.input_guard,layout,s.limits,{});const auto shape=dataset.shape();
  Stack planes;planes.reserve(layout.pages.size());
  for(int t=0;t<shape.t;++t)for(int z=0;z<shape.z;++z)for(int c=0;c<shape.c;++c)planes.push_back(*dataset.plane({t,c,z}));
  revalidate();return FlatPixels{std::move(planes),layout.info.sample_policy};
}
std::function<void()> ScientificIoPanel::import_revalidation() const {
  const auto &s = *state_;
  require(s.open && !s.exporting && s.inspection && bool(s.input_guard),
          "An inspected import is required for input revalidation");
  auto held = s.input_guard;
  held->unchanged();
  return [held] { held->unchanged(); };
}
void ScientificIoPanel::draw(const std::function<void()> &browse,
                             const std::function<void()> &flat_import,
                             const std::function<void()> &clear_queued,
                             std::size_t queued, const std::string &context) {
  auto &s = *state_;
  s.controls = Json::object();
  if (!s.open)
    return;
  ImGui::SetNextWindowSize({580, 0}, ImGuiCond_FirstUseEver);
  if (s.focus) {
    ImGui::SetNextWindowFocus();
    s.focus = false;
  }
  bool open = true;
  if (ImGui::Begin(s.exporting ? "Export scientific TIFF"
                               : "Open scientific TIFF",
                   &open, ImGuiWindowFlags_AlwaysAutoResize)) {
    auto safe = [&](auto operation) {
      try {
        operation();
      } catch (const std::exception &e) {
        s.error = e.what();
      }
    };
    if (!context.empty())
      ImGui::TextWrapped("%s", context.c_str());
    ImGui::TextUnformatted("Scientific grayscale / RGB TIFF (lossless strips)");
    if (s.source) {
      const auto &title = s.source->data().metadata.title;
      auto n = std::min(title.size(), std::size_t(256));
      while (n < title.size() && n &&
             (static_cast<unsigned char>(title[n]) & 0xc0) == 0x80)
        --n;
      ImGui::TextUnformatted("Source:");
      ImGui::SameLine();
      ImGui::TextUnformatted(title.data(), title.data() + n);
      const auto shape = s.source->data().dataset.shape();
      ImGui::Text("T=%d C=%d Z=%d Y=%d X=%d S=%d", shape.t, shape.c, shape.z,
                  shape.y, shape.x, shape.s);
    }
    ImGui::BeginDisabled(s.pending);
    ImGui::BeginDisabled(s.fixed_path);
    std::string path = s.path;
    ImGui::SetNextItemWidth(430);
    if (ImGui::InputText("File", &path))
      safe([&] { patch({{"path", path}}); });
    s.observe("path");
    ImGui::SameLine();
    if (ImGui::Button("Browse"))
      safe(browse);
    s.observe("browse");
    ImGui::EndDisabled();
    if (ImGui::CollapsingHeader("Resource limits"))
      for (auto [name, member] : fields) {
        std::uint64_t value = s.limits.*member;
        if (ImGui::InputScalar(name, ImGuiDataType_U64, &value))
          safe([&] { patch({{"limits", {{name, value}}}}); });
        s.observe(name);
      }
    if(s.exporting) {
      ImGui::SetNextItemWidth(130);
      const bool choosing_container=ImGui::BeginCombo("Container",s.bigtiff?"BigTIFF":"Classic TIFF");s.observe("container");int chosen_container=-1;
      if(choosing_container) {
        if(ImGui::Selectable("Classic TIFF",!s.bigtiff))chosen_container=0;s.observe("container.classic");
        if(ImGui::Selectable("BigTIFF",s.bigtiff))chosen_container=1;s.observe("container.bigtiff");
        ImGui::EndCombo();
      }
      if(chosen_container>=0)safe([&]{patch({{"container",chosen_container?"bigtiff":"classic"}});});
      const bool choosing=ImGui::BeginCombo("Storage",s.tiled?"Tiles (64x64)":"Strips");s.observe("storage");int chosen=-1;
      if(choosing) {
        if(ImGui::Selectable("Strips",!s.tiled))chosen=0;s.observe("storage.strips");
        if(ImGui::Selectable("Tiles",s.tiled))chosen=1;s.observe("storage.tiled");
        ImGui::EndCombo();
      }
      if(chosen>=0)safe([&]{if(chosen)patch({{"storage","tiled"},{"compression","uncompressed"},{"predictor","none"}});else patch({{"storage","strips"}});});
      ImGui::SameLine();ImGui::SetNextItemWidth(200);
    }
    if(s.exporting) {
      const bool choosing=ImGui::BeginCombo("Compression",s.compression==5?"LZW (lossless)":s.compression==32773?"PackBits (lossless)":s.compression==8?"Deflate (lossless)":"Uncompressed");
      s.observe("compression");int chosen=-1;
      if(choosing) {
        if(ImGui::Selectable("Uncompressed",s.compression==1))chosen=1;
        s.observe("compression.uncompressed");
        if(ImGui::Selectable("Deflate (lossless)",s.compression==8))chosen=8;
        s.observe("compression.deflate");
        if(ImGui::Selectable("PackBits (lossless)",s.compression==32773))chosen=32773;
        s.observe("compression.packbits");
        if(ImGui::Selectable("LZW (lossless)",s.compression==5))chosen=5;
        s.observe("compression.lzw");
        ImGui::EndCombo();
      }
      if(chosen>=0)safe([&]{patch({{"compression",io_detail::compression_name(unsigned(chosen))}});});
    }
    if(s.exporting && (s.compression==8 || s.compression==5)) {
      const bool choosing=ImGui::BeginCombo("Prediction",s.predictor==3?"Floating (Float32)":s.predictor==2?"Horizontal (integer)":"None");s.observe("predictor");int chosen=-1;
      if(choosing) {
        if(ImGui::Selectable("None",s.predictor==1))chosen=1;s.observe("predictor.none");
        if(s.source->data().dataset.pixel_type()==PixelType::Float32) {
          if(ImGui::Selectable("Floating (Float32)",s.predictor==3))chosen=3;s.observe("predictor.floating");
        } else {
          if(ImGui::Selectable("Horizontal (integer)",s.predictor==2))chosen=2;s.observe("predictor.horizontal");
        }
        ImGui::EndCombo();
      }
      if(chosen>=0)safe([&]{patch({{"predictor",chosen==3?"floating":chosen==2?"horizontal":"none"}});});
    }
    if (ImGui::Button(s.exporting ? "Review export" : "Inspect file"))
      safe([&] { inspect(); });
    s.observe("inspect");
    if (s.inspection) {
      const auto &series = s.inspection->series.at(0);
      ImGui::TextUnformatted(s.inspection->container==TiffContainer::BigTiff?"Container: BigTIFF":"Container: classic TIFF");
      if(io_detail::image_profile(s.inspection->format).tiled)ImGui::TextUnformatted("Storage: tiles");
      ImGui::TextUnformatted(io_detail::compression_of(s.inspection->format)==5?"Compression: LZW (lossless)":io_detail::compression_of(s.inspection->format)==32773?"Compression: PackBits (lossless)":io_detail::compression_of(s.inspection->format)==8?"Compression: Deflate (lossless)":"Compression: uncompressed");
      ImGui::TextUnformatted(io_detail::predictor_of(s.inspection->format)==3?"Prediction: floating (Float32)":io_detail::predictor_of(s.inspection->format)==2?"Prediction: horizontal (integer)":"Prediction: none");
      const auto shape = series.shape;
      ImGui::Text("Series 1: T=%d C=%d Z=%d Y=%d X=%d S=%d", shape.t, shape.c,
                  shape.z, shape.y, shape.x, shape.s);
      ImGui::Text("Spacing %.9g, %.9g, %.9g %s; frame %.9g %s",
                  series.calibration.spacing[0], series.calibration.spacing[1],
                  series.calibration.spacing[2],
                  series.calibration.unit.c_str(),
                  series.calibration.frame_interval,
                  series.calibration.time_unit.c_str());
      const auto defaults = series.metadata.find("defaulted_fields");
      if (defaults != series.metadata.end())
        ImGui::TextWrapped("Defaulted fields: %s",
                           metadataJson(defaults->second).dump().c_str());
      if (series.pixel_type == PixelType::Float32) {
        int policy = int(s.sample_policy);
        const char *labels[]{"Finite only", "Preserve quiet NaNs", "Preserve all IEEE words"};
        const bool choosing = ImGui::BeginCombo("Float32 samples", labels[policy]);
        s.observe("sample_policy");
        int chosen = -1;
        if (choosing) {
          for (int i=0; i<3; ++i) {
            if (ImGui::Selectable(labels[i], i==policy)) chosen = i;
            const auto key = std::string("sample_policy.") + policy_name(static_cast<FloatingSamplePolicy>(i));
            s.observe(key.c_str());
          }
          ImGui::EndCombo();
        }
        if (chosen >= 0)
          safe([&] { patch({{"sample_policy", policy_name(static_cast<FloatingSamplePolicy>(chosen))}}); });
      }
      bool axes = s.axes;
      if (ImGui::Checkbox("Use reviewed file axes and calibration", &axes))
        safe([&] { patch({{"confirm_axes", axes}}); });
      s.observe("confirm_axes");
    }
    if (s.estimate) {
      const auto &e = *s.estimate;
      ImGui::Text("%zu planes; %zu samples; output <= %zu bytes", e.planes,
                  e.samples, e.encoded_upper_bound);
      ImGui::Text("Encoded XY spacing %.12g, %.12g; relative error %.3g, %.3g",
                  e.encoded_calibration.spacing[0],
                  e.encoded_calibration.spacing[1], e.xy_relative_error[0],
                  e.xy_relative_error[1]);
      ImGui::Text("Z spacing %.12g %s; frame interval %.12g %s",
                  e.encoded_calibration.spacing[2],
                  e.encoded_calibration.unit.c_str(),
                  e.encoded_calibration.frame_interval,
                  e.encoded_calibration.time_unit.c_str());
      ImGui::TextWrapped(
          "Only pixels and calibration are exported. Display, selections, "
          "annotations, history and extra document metadata are excluded.");
      if (!e.canonicalized_fields.empty())
        ImGui::TextWrapped("Canonicalized provenance: %s",
                           Json(e.canonicalized_fields).dump().c_str());
      bool loss = s.loss;
      if (ImGui::Checkbox("Accept pixels and calibration only", &loss))
        safe([&] { patch({{"accept_pixels_and_calibration_only", loss}}); });
      s.observe("accept_loss");
      bool replace = s.replace;
      if (ImGui::Checkbox("Replace an existing file", &replace))
        safe([&] { patch({{"replace_existing", replace}}); });
      s.observe("replace_existing");
    }
    ImGui::BeginDisabled(s.exporting ? !(s.estimate && s.loss)
                                     : !(s.inspection && s.axes));
    if (ImGui::Button(s.exporting ? "Export" : "Open"))
      safe([&] { apply(); });
    s.observe("apply");
    ImGui::EndDisabled();
    if (!s.exporting && flat_import) {
      ImGui::Separator();
      ImGui::TextWrapped(
          "Flat-pixel import uses one Z plane per page and pixel units.");
      bool flat = s.flat;
      if (ImGui::Checkbox("Accept flat pixels without file axes or calibration",
                          &flat))
        safe([&] { patch({{"accept_flat_pixels_only", flat}}); });
      s.observe("accept_flat");
      ImGui::BeginDisabled(!s.flat);
      if (ImGui::Button("Import flat pixels"))
        safe(flat_import);
      s.observe("flat_open");
      ImGui::EndDisabled();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(s.pending ? "Cancel IO" : "Close")) {
      cancel("explicit_cancel");
      if (clear_queued)
        clear_queued();
    }
    s.observe("cancel");
    if (queued && clear_queued) {
      ImGui::Text("%zu more files waiting to open", queued);
      if (ImGui::Button("Cancel queued files"))
        clear_queued();
      s.observe("cancel_queued");
    }
    if (s.pending)
      ImGui::TextUnformatted(
          "Working; cancellation is checked between bounded transfers.");
    if (!s.error.empty())
      ImGui::TextWrapped("%s", s.error.c_str());
    if (!s.message.empty())
      ImGui::TextWrapped("%s", s.message.c_str());
  }
  ImGui::End();
  if (!open) {
    cancel("window_closed");
    if (clear_queued)
      clear_queued();
  }
}
Json ScientificIoPanel::diagnostics() const {
  const auto &s = *state_;
  Json out = {{"open", s.open},
              {"working", s.pending},
              {"exporting", s.exporting},
              {"predictor", (s.exporting?s.predictor:s.inspection?io_detail::predictor_of(s.inspection->format):1)==3?"floating":(s.exporting?s.predictor:s.inspection?io_detail::predictor_of(s.inspection->format):1)==2?"horizontal":"none"},
              {"container",(s.exporting?s.bigtiff:s.inspection && s.inspection->container==TiffContainer::BigTiff)?"bigtiff":"classic"},
              {"storage",(s.exporting?s.tiled:s.inspection?io_detail::image_profile(s.inspection->format).tiled:false)?"tiled":"strips"},
              {"compression", io_detail::compression_name(s.exporting?s.compression:s.inspection?io_detail::compression_of(s.inspection->format):1)},
              {"fixed_path", s.fixed_path},
              {"job_canceled", s.job_canceled},
              {"generation", s.generation},
              {"path", s.path},
              {"confirm_axes", s.axes},
              {"sample_policy", policy_name(s.sample_policy)},
              {"accept_flat_pixels_only", s.flat},
              {"accept_loss", s.loss},
              {"replace_existing", s.replace},
              {"limits", limits_json(s.limits)},
              {"error", s.error},
              {"message", s.message},
              {"cancel_reason", s.reason},
              {"controls", s.controls}};
  if (s.inspection)
    out["inspection"] = info_json(s.inspection->series.at(0));
  if (s.estimate)
    out["estimate"] = {
        {"planes", s.estimate->planes},
        {"samples", s.estimate->samples},
        {"encoded_upper_bound", s.estimate->encoded_upper_bound},
        {"xy_resolution", s.estimate->xy_resolution},
        {"calibration", calibration_json(s.estimate->encoded_calibration)}};
  return out;
}
} // namespace imagejxx::desktop
