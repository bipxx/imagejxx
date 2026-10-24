#include "axis_command_record.hpp"
#include "imagej_annotation_export_internal.hpp"
#include "imagej_annotation_import_internal.hpp"
#include "imagej_roi_import_internal.hpp"
#include "kernel_control.hpp"
#include "native_workflow_program_internal.hpp"
#include "session_metadata.hpp"
#include "selection_creation_workflow.hpp"
#include "annotation_workflow.hpp"
#include "regional_geometry.hpp"
#include <algorithm>
#include <cmath>
#include <imagejxx/gaussian_command.hpp>
#include <imagejxx/rank_command.hpp>
#include <set>
#include <tuple>
namespace imagejxx {
namespace {
using namespace detail::axis_command_record;
namespace codec = detail::session_metadata;
void require(bool b, const char *s) {
  if (!b)
    throw std::invalid_argument(s);
}
void valid_limits(const NativeWorkflowProgramLimits &l) {
  require(l.max_steps && l.max_steps <= 4096 && l.max_record_bytes &&
              l.max_record_bytes <=
                  NativeWorkflowProgramLimits::hard_max_record_bytes,
          "Native program limits out of range");
}
bool flag(const MetadataValue &v) {
  const auto p = std::get_if<bool>(&v);
  require(p, "Program flag requires bool");
  return *p;
}
bool null(const MetadataValue &v) {
  return std::holds_alternative<std::monostate>(v);
}
const MetadataArray &array(const MetadataValue &v) {
  if (const auto p = std::get_if<std::vector<std::string>>(&v);
      p && p->empty()) {
    static const MetadataArray empty;
    return empty;
  }
  return array_value(v);
}
struct Budget {
  std::size_t cap, used = 0;
  void add(std::size_t n) {
    require(n <= cap - used, "Native program record-byte limit");
    used += n;
  }
  void product(std::size_t n, std::size_t k) {
    require(!k || n <= (cap - used) / k, "Native program record-byte limit");
    add(n * k);
  }
};
void record_budget(const MetadataObject &record, std::size_t cap) {
  Budget b{cap};
  std::size_t nodes = 0;
  auto charge = [&](std::size_t depth, std::size_t bytes) {
    require(depth <= 128 && ++nodes <= 4 * 1024 * 1024,
            "Program record structure limit");
    b.add(64);
    b.add(bytes);
  };
  std::function<void(const MetadataValue &, std::size_t)> visit;
  std::function<void(const MetadataObject &, std::size_t)> object;
  object = [&](const MetadataObject &value, std::size_t depth) {
    charge(depth, 0);
    for (const auto &[key, child] : value) {
      charge(depth + 1, key.size());
      visit(child, depth + 1);
    }
  };
  visit = [&](const MetadataValue &value, std::size_t depth) {
    kernel_checkpoint_periodic();
    if (const auto node =
            std::get_if<std::shared_ptr<const MetadataTree>>(&value)) {
      require(bool(*node), "Null program metadata node");
      if (const auto o = std::get_if<MetadataObject>(&(*node)->value))
        object(*o, depth);
      else {
        charge(depth, 0);
        for (const auto &child : std::get<MetadataArray>((*node)->value))
          visit(child, depth + 1);
      }
    } else if (const auto strings =
                   std::get_if<std::vector<std::string>>(&value)) {
      charge(depth, 0);
      for (const auto &text : *strings)
        charge(depth + 1, text.size());
    } else {
      const auto text = std::get_if<std::string>(&value);
      charge(depth, text ? text->size() : 0);
    }
  };
  object(record, 0);
}
void json_budget(const MetadataObject &record, std::size_t cap) {
  Budget budget{cap};
  auto text = [&](const std::string &value) {
    budget.add(2);
    for (unsigned char c : value) {
      kernel_checkpoint_periodic();
      budget.add(c == '"' || c == '\\' || c == '\b' || c == '\f' || c == '\n' ||
                         c == '\r' || c == '\t'
                     ? 2
                 : c < 32 ? 6
                          : 1);
    }
  };
  std::function<void(const MetadataValue &)> visit;
  std::function<void(const MetadataObject &)> object;
  object = [&](const MetadataObject &value) {
    budget.add(2);
    if (!value.empty())
      budget.add(value.size() - 1);
    for (const auto &[key, child] : value) {
      text(key);
      budget.add(1);
      visit(child);
    }
  };
  visit = [&](const MetadataValue &value) {
    kernel_checkpoint_periodic();
    if (const auto node =
            std::get_if<std::shared_ptr<const MetadataTree>>(&value)) {
      if (const auto o = std::get_if<MetadataObject>(&(*node)->value))
        object(*o);
      else {
        const auto &a = std::get<MetadataArray>((*node)->value);
        budget.add(2);
        if (!a.empty())
          budget.add(a.size() - 1);
        for (const auto &child : a)
          visit(child);
      }
    } else if (const auto strings =
                   std::get_if<std::vector<std::string>>(&value)) {
      budget.add(2);
      if (!strings->empty())
        budget.add(strings->size() - 1);
      for (const auto &v : *strings)
        text(v);
    } else if (const auto str = std::get_if<std::string>(&value))
      text(*str);
    else if (std::holds_alternative<std::monostate>(value))
      budget.add(4);
    else if (const auto b = std::get_if<bool>(&value))
      budget.add(*b ? 4 : 5);
    else
      budget.add(32); // Includes every finite integer/double JSON spelling.
  };
  try {
    object(record);
  } catch (const std::invalid_argument &) {
    throw std::invalid_argument("Program JSON transport estimate limit");
  }
}
MetadataValue range_value(const std::optional<AxisRange> &r) {
  if (!r)
    return {};
  return metadata_object(
      {{"start", r->start},
       {"stop", r->stop ? MetadataValue(*r->stop) : MetadataValue{}},
       {"step", r->step}});
}
std::optional<AxisRange> range_from(const MetadataValue &v) {
  if (null(v))
    return {};
  const auto &r = object_value(v);
  exact_fields(r, {"start", "stop", "step"});
  AxisRange out{
      index_value(r.at("start")), {}, index_value(r.at("step"), true)};
  if (!null(r.at("stop")))
    out.stop = index_value(r.at("stop"), true);
  require(!out.stop || *out.stop > out.start,
          "Program range stop must exceed start");
  return out;
}
void valid_target(const TargetRequest &t) {
  require(unsigned(t.scope) <= unsigned(TargetScope::Positions) &&
              unsigned(t.roi) <= unsigned(RoiPolicy::PositionedRois) &&
              unsigned(t.mode) <= unsigned(ExecutionMode::Volume) &&
              t.destination == OutputDestination::InPlace,
          "Program target enum/destination");
  const bool ranges = t.t || t.c || t.z;
  require((t.scope == TargetScope::Ranges || !ranges) &&
              (t.scope == TargetScope::Positions || t.positions.empty()),
          "Program target carries unused ranges/positions");
  for (const auto *r : {&t.t, &t.c, &t.z})
    if (*r)
      require((*r)->start >= 0 && (*r)->step > 0 &&
                  (!(*r)->stop || *(*r)->stop > (*r)->start),
              "Program target range");
  if (t.scope == TargetScope::Positions)
    require(!t.positions.empty(), "Program explicit positions are empty");
  std::set<std::tuple<int, int, int>> positions;
  for (const auto &p : t.positions)
    require(
        p.t >= 0 && p.c >= 0 && p.z >= 0 &&
            positions.emplace(p.t, p.c, p.z).second,
        "Program explicit positions require distinct nonnegative coordinates");
}
MetadataValue target_value(const TargetRequest &t) {
  MetadataArray positions;
  for (const auto &p : t.positions)
    positions.push_back(metadata_array({p.t, p.c, p.z}));
  return metadata_object({{"scope", int(t.scope)},
                          {"roi", int(t.roi)},
                          {"mode", int(t.mode)},
                          {"destination", std::string("in_place")},
                          {"t", range_value(t.t)},
                          {"c", range_value(t.c)},
                          {"z", range_value(t.z)},
                          {"positions", metadata_array(std::move(positions))}});
}
TargetRequest target_from(const MetadataValue &v) {
  const auto &r = object_value(v);
  exact_fields(
      r, {"scope", "roi", "mode", "destination", "t", "c", "z", "positions"});
  TargetRequest t;
  auto scope = unsigned_value(r.at("scope")), roi = unsigned_value(r.at("roi")),
       mode = unsigned_value(r.at("mode"));
  require(scope <= 3 && roi <= 2 && mode <= 1 &&
              text_value(r.at("destination")) == "in_place",
          "Program target enum/destination");
  t.scope = TargetScope(scope);
  t.roi = RoiPolicy(roi);
  t.mode = ExecutionMode(mode);
  t.t = range_from(r.at("t"));
  t.c = range_from(r.at("c"));
  t.z = range_from(r.at("z"));
  for (const auto &position : array(r.at("positions"))) {
    const auto &p = array(position);
    require(p.size() == 3, "Program position requires T/C/Z");
    t.positions.push_back(
        {index_value(p[0]), index_value(p[1]), index_value(p[2])});
  }
  valid_target(t);
  return t;
}
void valid_parameter(const OperationValue &v, Budget &b) {
  b.add(256);
  std::visit(
      [&](const auto &p) {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, double>)
          require(std::isfinite(p), "Program parameter must be finite");
        else if constexpr (std::is_same_v<T, std::string>)
          b.add(p.size());
        else if constexpr (std::is_same_v<T, NumericArray>) {
          b.product(p.shape.size(), 64);
          b.product(p.values.size(), 128);
          p.validate();
          for (auto n : p.values)
            require(std::isfinite(n), "Program array must be finite");
        } else if constexpr (std::is_same_v<T, std::shared_ptr<const Image>>) {
          require(bool(p) && p->width > 0 && p->height > 0 && p->channels > 0 &&
                      unsigned(p->type) <= 3,
                  "Invalid program image parameter");
          b.product(p->pixels.size(), 128);
          std::size_t count = std::size_t(p->width);
          for (int n : {p->height, p->channels}) {
            require(std::size_t(n) <= SIZE_MAX / count,
                    "Program parameter image overflow");
            count *= std::size_t(n);
          }
          require(count == p->pixels.size(), "Program parameter image backing");
          for (auto n : p->pixels)
            require(std::isfinite(n), "Program image parameter must be finite");
        }
      },
      v);
}
MetadataValue parameter_value(const OperationValue &v) {
  static const char *names[]{"null", "bool", "int64", "uint64",
                             "real", "text", "array", "image"};
  auto value = std::visit(
      [](const auto &p) -> MetadataValue {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, NumericArray>) {
          MetadataArray shape, values;
          for (auto n : p.shape)
            shape.push_back(std::uint64_t(n));
          for (auto n : p.values) {
            kernel_checkpoint_periodic();
            values.push_back(n);
          }
          return metadata_object(
              {{"shape", metadata_array(std::move(shape))},
               {"values", metadata_array(std::move(values))}});
        } else if constexpr (std::is_same_v<T, std::shared_ptr<const Image>>) {
          MetadataArray values;
          for (auto n : p->pixels) {
            kernel_checkpoint_periodic();
            values.push_back(n);
          }
          return metadata_object(
              {{"width", p->width},
               {"height", p->height},
               {"channels", p->channels},
               {"type", int(p->type)},
               {"values", metadata_array(std::move(values))}});
        } else
          return p;
      },
      v);
  return metadata_object(
      {{"kind", std::string(names[v.index()])}, {"value", std::move(value)}});
}
OperationValue parameter_from(const MetadataValue &v) {
  const auto &o = object_value(v);
  exact_fields(o, {"kind", "value"});
  const auto &kind = text_value(o.at("kind"));
  const auto &value = o.at("value");
  if (kind == "null") {
    require(null(value), "Program null parameter");
    return std::monostate{};
  }
  if (kind == "bool")
    return flag(value);
  if (kind == "int64") {
    if (const auto p = std::get_if<std::int64_t>(&value))
      return *p;
    const auto n = unsigned_value(value);
    require(n <= INT64_MAX, "Program signed parameter overflow");
    return std::int64_t(n);
  }
  if (kind == "uint64")
    return unsigned_value(value);
  if (kind == "real")
    return number_value(value);
  if (kind == "text")
    return text_value(value);
  if (kind == "array") {
    const auto &a = object_value(value);
    exact_fields(a, {"shape", "values"});
    NumericArray result;
    for (const auto &n : array(a.at("shape")))
      result.shape.push_back(size_value(n));
    for (const auto &n : array(a.at("values")))
      result.values.push_back(number_value(n));
    result.validate();
    return result;
  }
  require(kind == "image", "Unknown program parameter kind");
  const auto &a = object_value(value);
  exact_fields(a, {"width", "height", "channels", "type", "values"});
  int width = index_value(a.at("width"), true),
      height = index_value(a.at("height"), true),
      channels = index_value(a.at("channels"), true);
  auto type = unsigned_value(a.at("type"));
  require(type <= 3, "Program image pixel type");
  const auto &values = array(a.at("values"));
  std::size_t count = std::size_t(width);
  for (int n : {height, channels}) {
    require(std::size_t(n) <= SIZE_MAX / count,
            "Program image dimensions overflow");
    count *= std::size_t(n);
  }
  require(count == values.size(), "Program image backing count");
  auto image =
      std::make_shared<Image>(width, height, channels, PixelType(type));
  for (std::size_t i = 0; i < values.size(); ++i)
    image->pixels[i] = number_value(values[i]);
  return std::shared_ptr<const Image>(std::move(image));
}
std::optional<MetadataObject> original_from(const MetadataValue &v) {
  return null(v) ? std::optional<MetadataObject>{} : object_value(v);
}
MetadataValue histogram_options_value(const std::optional<NativeWorkflowHistogramOptions> &value) {
  if(!value)return {};
  const auto &o=value->histogram;
  return metadata_object({{"binning",std::string(o.binning==HistogramBinning::ProcessorRaw?"processor_raw":"uniform_range")},
    {"bins",o.bins},{"range",o.range?metadata_array({(*o.range)[0],(*o.range)[1]}):MetadataValue{}},
    {"log_counts",value->log_counts}});
}
std::optional<NativeWorkflowHistogramOptions> histogram_options_from(const MetadataValue &value) {
  if(null(value))return {};
  const auto &o=object_value(value);exact_fields(o,{"binning","bins","range","log_counts"});
  NativeWorkflowHistogramOptions out;const auto &binning=text_value(o.at("binning"));
  require(binning=="processor_raw" || binning=="uniform_range","Unknown workflow histogram binning");
  out.histogram.binning=binning=="processor_raw"?HistogramBinning::ProcessorRaw:HistogramBinning::UniformRange;
  out.histogram.bins=index_value(o.at("bins"),true);
  if(!null(o.at("range"))) {
    const auto &r=array(o.at("range"));require(r.size()==2,"Workflow histogram range requires two endpoints");
    out.histogram.range=std::array<double,2>{number_value(r[0]),number_value(r[1])};
  }
  out.log_counts=flag(o.at("log_counts"));return out;
}
void charge_step(const detail::NativeWorkflowStepRef &step, Budget &b) {
  std::visit(
      [&](const auto *p) {
        require(p, "Null program step");
        const auto &v = *p;
        using T = std::decay_t<decltype(v)>;
        b.add(2048);
        if (v.original)
          b.add(detail::imagej_roi_record_bytes(*v.original, b.cap - b.used));
        if constexpr (std::is_same_v<T, NativeWorkflowOperation>) {
          b.add(v.operation.size());
          if(v.selection_creation)
            b.add(detail::selection_creation_request_bytes(*v.selection_creation,b.cap-b.used));
          if(detail::annotation_workflow_operation(v.operation) && v.original)
            detail::validate_annotation_workflow_original(v.operation,*v.original,b.cap-b.used);
          if(v.annotation_intent)b.add(detail::annotation_workflow_intent_bytes(*v.annotation_intent,b.cap-b.used));
          if (v.filter_options)
            b.add(512);
          b.product(v.target.positions.size(), 256);
          for (const auto &[name, value] : v.parameters) {
            b.add(name.size());
            valid_parameter(value, b);
          }
        } else if constexpr (std::is_same_v<T, NativeWorkflowHistogram>) {
          b.add(v.name.size());
          if(v.authored_options)b.add(sizeof(NativeWorkflowHistogramOptions));
          b.product(v.target.positions.size(), 256);
        } else if constexpr (std::is_same_v<T, NativeWorkflowImport>) {
          require(v.items.size() <= v.limits.batch.max_entries,
                  "Program import count");
          for (const auto &item : v.items)
            std::visit(
                [&](const auto &i) {
                  b.add(512 + i.name.size());
                  b.product(i.data.size(), 2);
                },
                item);
          std::size_t bytes = 0, nodes = 0;
          for (const auto &item : v.items) {
            auto m =
                detail::admit_imagej_annotation_import_item(item, v.limits);
            detail::charge_imagej_annotation_mapping(m, bytes, nodes,
                                                     b.cap - b.used);
          }
          b.add(bytes);
        } else {
          b.add(v.name.size());
          require(v.items.size() <= v.limits.zip.max_entries,
                  "Program export count");
          std::size_t bytes = 0, nodes = 0;
          for (const auto &i : v.items) {
            b.add(512 + i.name.size());
            detail::preflight_imagej_annotation_projection(
                i.standard_projection, v.limits, bytes, nodes);
          }
          b.product(bytes, 2);
        }
      },
      step);
}
void preflight(const std::vector<detail::NativeWorkflowStepRef> &steps,
               const NativeWorkflowProgramLimits &limits) {
  valid_limits(limits);
  require(!steps.empty() && steps.size() <= limits.max_steps,
          "Program requires bounded nonempty steps");
  Budget b{limits.max_record_bytes};
  b.add(512);
  // Charge all caller-owned inputs before serializing/copying any step.
  for (const auto &step : steps)
    charge_step(step, b);
  for (const auto &step : steps)
    std::visit(
        [&](const auto *p) {
          const auto &v = *p;
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<T, NativeWorkflowOperation>) {
            valid_target(v.target);
            require(detail::annotation_workflow_operation(v.operation) || v.operation=="selection_creation" || v.operation=="selection_enlargement" || v.operation=="rgb_contrast_session" || v.operation=="rgb_contrast_stack" || v.operation=="rgb_contrast_apply" || v.operation=="contrast_apply" || v.operation=="rgb_dataset" || v.operation=="color_stack" || v.operation=="indexed_dataset" || v.operation=="indexed_rgb" || operation_spec(v.operation).geometry ==
                        GeometryEffect::Preserve || v.operation == "convert_image",
                    "Program geometry requires explicit remap");
            detail::validate_native_workflow_operation_intent(v);
          } else if constexpr (std::is_same_v<T, NativeWorkflowHistogram>) {
            valid_target(v.target);
            detail::validate_native_workflow_histogram_intent(v);
          } else if constexpr (std::is_same_v<T, NativeWorkflowImport>) {
            detail::validate_imagej_annotation_import_limits(v.limits);
            if (v.original)
              detail::validate_native_workflow_import_original(*v.original);
          } else {
            detail::validate_imagej_annotation_export_limits(v.limits);
            if (v.original)
              detail::validate_native_workflow_export_original(*v.original);
          }
        },
        step);
}
MetadataValue filter_options_value(
    const std::optional<NativeWorkflowFilterOptions> &options) {
  if (!options)
    return {};
  return std::visit(
      [](const auto &v) -> MetadataValue {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, GaussianBlurCommandOptions>)
          return metadata_object({{"kind", std::string("gaussian")},
                                  {"sigma", v.sigma},
                                  {"scaled_units", v.scaled_units}});
        else
          return metadata_object({{"kind", std::string("rank")},
                                  {"method", rank_filter_method_name(v.method)},
                                  {"radius", v.radius}});
      },
      *options);
}
std::optional<NativeWorkflowFilterOptions>
filter_options_from(const MetadataValue &value) {
  if (null(value))
    return {};
  const auto &o = object_value(value);
  const auto &kind = text_value(o.at("kind"));
  if (kind == "gaussian") {
    exact_fields(o, {"kind", "sigma", "scaled_units"});
    return GaussianBlurCommandOptions{number_value(o.at("sigma")),
                                      flag(o.at("scaled_units"))};
  }
  require(kind == "rank", "Unsupported typed workflow filter kind");
  exact_fields(o, {"kind", "method", "radius"});
  return RankFilterCommandOptions{
      parse_rank_filter_method(text_value(o.at("method"))),
      number_value(o.at("radius"))};
}
MetadataObject encode(const std::vector<detail::NativeWorkflowStepRef> &steps,
                      const NativeWorkflowProgramLimits &limits) {
  preflight(steps, limits);
  const bool v2 = std::any_of(steps.begin(), steps.end(), [](const auto &ref) {
    return std::visit(
        [](const auto *p) {
          using T = std::decay_t<decltype(*p)>;
          if constexpr (std::is_same_v<T, NativeWorkflowOperation>)
            return bool(p->filter_options);
          else
            return false;
        },
        ref);
  });
  const bool v3 = std::any_of(steps.begin(), steps.end(), [](const auto &ref) {
    return std::visit([](const auto *p) { return !p->enabled; }, ref);
  });
  const bool v4 = std::any_of(steps.begin(), steps.end(), [](const auto &ref) {
    return std::holds_alternative<const NativeWorkflowHistogram *>(ref);
  });
  const bool v5=std::any_of(steps.begin(),steps.end(),[](const auto &ref) {
    const auto p=std::get_if<const NativeWorkflowHistogram *>(&ref);
    return p && bool((*p)->authored_options);
  });
  const bool v6=std::any_of(steps.begin(),steps.end(),[](const auto &ref) {
    const auto p=std::get_if<const NativeWorkflowOperation *>(&ref);
    return p && (*p)->operation=="selection_creation";
  });
  const bool v7=std::any_of(steps.begin(),steps.end(),[](const auto &ref) {
    const auto p=std::get_if<const NativeWorkflowOperation *>(&ref);
    return p && detail::annotation_workflow_operation((*p)->operation);
  });
  MetadataArray records;
  std::set<std::string> outputs;
  for (const auto &step : steps)
    std::visit(
        [&](const auto *p) {
          const auto &v = *p;
          using T = std::decay_t<decltype(v)>;
          MetadataObject r{{"original", v.original
                                            ? metadata_object(*v.original)
                                            : MetadataValue{}}};
          if constexpr (std::is_same_v<T, NativeWorkflowOperation>) {
            MetadataObject params;
            for (const auto &[name, value] : v.parameters)
              params[name] = parameter_value(value);
            r["kind"] = std::string("operation");
            r["operation"] = v.operation;
            r["parameters"] = metadata_object(std::move(params));
            r["target"] = target_value(v.target);
            r["use_destination_calibration"] = v.use_destination_calibration;
            if (v2 || v3 || v4 || v6 || v7)
              r["filter_options"] = filter_options_value(v.filter_options);
            if(v6 || v7) r["selection_creation"] = v.selection_creation
                ? metadata_object(detail::selection_creation_request_value(*v.selection_creation,limits.max_record_bytes))
                : MetadataValue{};
            if(v7)r["annotation_intent"]=v.annotation_intent
                ? metadata_object(detail::annotation_workflow_intent_value(*v.annotation_intent,limits.max_record_bytes))
                : MetadataValue{};
          } else if constexpr (std::is_same_v<T, NativeWorkflowHistogram>) {
            require(outputs.insert(v.name).second, "Duplicate workflow output name");
            r["kind"] = std::string("histogram");
            r["name"] = v.name;
            r["target"] = target_value(v.target);
            if(v5 || v6 || v7)r["options"]=histogram_options_value(v.authored_options);
            r["limits"] = metadata_object({
                {"max_pixels", std::uint64_t(v.limits.processing.max_pixels)},
                {"max_working_bytes", std::uint64_t(v.limits.processing.max_working_bytes)},
                {"max_record_bytes", std::uint64_t(v.limits.max_record_bytes)}});
          } else if constexpr (std::is_same_v<T, NativeWorkflowImport>) {
            require(v.mode == ImageJRoiImportMode::Append ||
                        v.mode == ImageJRoiImportMode::Replace,
                    "Program import mode");
            r["kind"] = std::string("import");
            r["mode"] = std::string(
                v.mode == ImageJRoiImportMode::Append ? "append" : "replace");
            r["limits"] =
                detail::imagej_annotation_import_limits_value(v.limits);
            r["inputs"] =
                detail::imagej_annotation_import_items_value(v.items, v.limits);
          } else {
            require(!v.name.empty() && v.name.size() <= 180 && v.name != "." &&
                        v.name != ".." &&
                        v.name.find_first_of("/\\:\0", 0, 4) ==
                            std::string::npos &&
                        outputs.insert(v.name).second,
                    "Program outputs require unique flat tokens");
            r["kind"] = std::string("export");
            r["name"] = v.name;
            r["choices"] = detail::imagej_annotation_export_choices_value(
                {v.items, v.limits, v.zip, v.zip && v.compress});
          }
          if (v3 || v4 || v6 || v7)
            r["enabled"] = v.enabled;
          records.push_back(metadata_object(std::move(r)));
        },
        step);
  MetadataObject result{
      {"schema", std::string("imagejxx.native-workflow-program")},
      {"version", v7   ? 7
                  : v6 ? 6
                  : v5 ? 5
                  : v4 ? 4
                  : v3 ? 3
                  : v2 ? 2
                       : 1},
      {"steps", metadata_array(std::move(records))}};
  record_budget(result, limits.max_record_bytes);
  if(v6 || v7)detail::regional_record_depth(result);
  validate_metadata(result);
  return result;
}
std::vector<detail::NativeWorkflowStepRef>
refs(const std::vector<NativeWorkflowStep> &steps) {
  std::vector<detail::NativeWorkflowStepRef> result;
  result.reserve(steps.size());
  for (const auto &step : steps)
    std::visit([&](const auto &v) { result.emplace_back(&v); }, step);
  return result;
}
} // namespace
struct detail::NativeWorkflowProgramAccess {
  static NativeWorkflowProgram make(std::vector<NativeWorkflowStep> steps,
                                    NativeWorkflowProgramLimits limits) {
    return NativeWorkflowProgram(std::move(steps), limits);
  }
};
MetadataObject NativeWorkflowProgram::record() const {
  return encode(refs(*steps_), limits_);
}
std::size_t
detail::native_workflow_program_retained_bytes(const NativeWorkflowProgram &p,
                                               std::size_t cap) {
  Budget b{cap};
  b.add(512);
  for (const auto &step : refs(p.steps()))
    charge_step(step, b);
  return b.used;
}
MetadataObject detail::native_workflow_program_record_bounded(
    const NativeWorkflowProgram &p, const NativeWorkflowProgramLimits &limits) {
  return encode(refs(p.steps()), limits);
}
void detail::native_workflow_record_budget(const MetadataObject &r,
                                           std::size_t cap) {
  record_budget(r, cap);
}
void detail::native_workflow_json_budget(const MetadataObject &r,
                                         std::size_t cap) {
  json_budget(r, cap);
}
std::string NativeWorkflowProgram::json() const {
  auto value = record();
  json_budget(value, limits_.max_record_bytes);
  auto text = codec::to_json(value).dump();
  require(text.size() <= limits_.max_record_bytes,
          "Program JSON transport limit");
  return text;
}
NativeWorkflowProgram
make_native_workflow_program(const std::vector<NativeWorkflowStep> &steps,
                             const NativeWorkflowProgramLimits &limits) {
  require(steps.size() <= limits.max_steps, "Program step count limit");
  return detail::make_native_workflow_program_borrowed(refs(steps), limits);
}
NativeWorkflowProgram detail::make_native_workflow_program_borrowed(
    const std::vector<NativeWorkflowStepRef> &steps,
    const NativeWorkflowProgramLimits &limits) {
  return native_workflow_program_from_record(encode(steps, limits), limits);
}
NativeWorkflowProgram
native_workflow_program_from_record(const MetadataObject &r,
                                    const NativeWorkflowProgramLimits &limits) {
  valid_limits(limits);
  record_budget(r, limits.max_record_bytes);
  validate_metadata(r);
  exact_fields(r, {"schema", "version", "steps"});
  const auto version = unsigned_value(r.at("version"));
  require(text_value(r.at("schema")) == "imagejxx.native-workflow-program" &&
              (version >= 1 && version <= 7),
          "Unsupported native program record");
  if(version>=6)detail::regional_record_depth(r);
  const auto &records = array(r.at("steps"));
  require(!records.empty() && records.size() <= limits.max_steps,
          "Program step count limit");
  if(version==6) {
    const bool creation=std::any_of(records.begin(),records.end(),[](const auto &value) {
      const auto &row=object_value(value);
      const auto kind=row.find("kind"),operation=row.find("operation");
      return kind!=row.end() && operation!=row.end() &&
          text_value(kind->second)=="operation" && text_value(operation->second)=="selection_creation";
    });
    require(creation,"Native program V6 requires selection creation intent");
  }
  if(version==7) {
    const bool annotation=std::any_of(records.begin(),records.end(),[](const auto &value) {
      const auto &row=object_value(value);
      const auto kind=row.find("kind"),operation=row.find("operation");
      return kind!=row.end() && operation!=row.end() && text_value(kind->second)=="operation" &&
          detail::annotation_workflow_operation(text_value(operation->second));
    });
    require(annotation,"Native program V7 requires annotation intent");
  }
  std::vector<NativeWorkflowStep> steps;
  steps.reserve(records.size());
  Budget decoded{limits.max_record_bytes};
  decoded.add(512);
  for (const auto &v : records) {
    kernel_checkpoint();
    const auto &s = object_value(v);
    const auto &kind = text_value(s.at("kind"));
    if (kind == "operation") {
      if (version == 1)
        exact_fields(s, {"kind", "original", "operation", "parameters",
                         "target", "use_destination_calibration"});
      else if (version == 2)
        exact_fields(s,
                     {"kind", "original", "operation", "parameters", "target",
                      "use_destination_calibration", "filter_options"});
      else if(version<6)
        exact_fields(s, {"kind", "original", "operation", "parameters",
                         "target", "use_destination_calibration",
                         "filter_options", "enabled"});
      else if(version==6)exact_fields(s, {"kind", "original", "operation", "parameters",
                         "target", "use_destination_calibration",
                         "filter_options", "enabled", "selection_creation"});
      else exact_fields(s, {"kind", "original", "operation", "parameters",
                         "target", "use_destination_calibration",
                         "filter_options", "enabled", "selection_creation", "annotation_intent"});
      NativeWorkflowOperation op;
      op.operation = text_value(s.at("operation"));
      require(version>=7 || !detail::annotation_workflow_operation(op.operation),"Annotation intent requires native program V7");
      require(version>=6 || op.operation!="selection_creation", "Selection creation requires program V6");
      if(version>=6 && !null(s.at("selection_creation")))
        op.selection_creation=detail::selection_creation_request_from_value(
            object_value(s.at("selection_creation")),limits.max_record_bytes);
      op.original = original_from(s.at("original"));
      if(version>=7 && detail::annotation_workflow_operation(op.operation) && op.original)
        detail::validate_annotation_workflow_original(op.operation,*op.original,limits.max_record_bytes);
      if(version>=7 && !null(s.at("annotation_intent")))
        op.annotation_intent=detail::annotation_workflow_intent_from_value(object_value(s.at("annotation_intent")),limits.max_record_bytes);
      for (const auto &[name, value] : object_value(s.at("parameters")))
        op.parameters[name] = parameter_from(value);
      op.target = target_from(s.at("target"));
      op.use_destination_calibration =
          flag(s.at("use_destination_calibration"));
      if (version >= 2)
        op.filter_options = filter_options_from(s.at("filter_options"));
      if (version >= 3)
        op.enabled = flag(s.at("enabled"));
      steps.emplace_back(std::move(op));
    } else if (kind == "histogram") {
      require(version >= 4, "Histogram step requires native program V4 or V5");
      if(version==4)exact_fields(s, {"kind", "original", "name", "target", "limits", "enabled"});
      else exact_fields(s, {"kind", "original", "name", "target", "limits", "enabled", "options"});
      const auto &l = object_value(s.at("limits"));
      exact_fields(l, {"max_pixels", "max_working_bytes", "max_record_bytes"});
      NativeWorkflowHistogram step;
      step.name = text_value(s.at("name"));
      step.target = target_from(s.at("target"));
      step.original = original_from(s.at("original"));
      step.limits.processing.max_pixels = size_value(l.at("max_pixels"));
      step.limits.processing.max_working_bytes = size_value(l.at("max_working_bytes"));
      step.limits.max_record_bytes = size_value(l.at("max_record_bytes"));
      step.enabled = flag(s.at("enabled"));
      if(version>=5)step.authored_options=histogram_options_from(s.at("options"));
      steps.emplace_back(std::move(step));
    } else if (kind == "import") {
      if (version >= 3)
        exact_fields(
            s, {"kind", "original", "mode", "limits", "inputs", "enabled"});
      else
        exact_fields(s, {"kind", "original", "mode", "limits", "inputs"});
      auto l =
          detail::imagej_annotation_import_limits_from_value(s.at("limits"));
      const auto &mode = text_value(s.at("mode"));
      require(mode == "append" || mode == "replace", "Program import mode");
      auto items = detail::admit_imagej_annotation_archive(s.at("inputs"), l);
      steps.emplace_back(
          NativeWorkflowImport{std::move(items),
                               mode == "append" ? ImageJRoiImportMode::Append
                                                : ImageJRoiImportMode::Replace,
                               l, original_from(s.at("original")),
                               version >= 3 ? flag(s.at("enabled")) : true});
    } else {
      require(kind == "export", "Unknown native program step kind");
      if (version >= 3)
        exact_fields(s, {"kind", "original", "name", "choices", "enabled"});
      else
        exact_fields(s, {"kind", "original", "name", "choices"});
      auto request =
          detail::imagej_annotation_export_choices_from_value(s.at("choices"));
      steps.emplace_back(NativeWorkflowExport{
          text_value(s.at("name")), std::move(request.items), request.limits,
          request.is_zip, request.compress, original_from(s.at("original")),
          version >= 3 ? flag(s.at("enabled")) : true});
    }
    std::visit(
        [&](const auto &step) {
          charge_step(detail::NativeWorkflowStepRef{&step}, decoded);
        },
        steps.back());
  }
  // Entire decoded intent is admitted without a dataset/provider or execution.
  (void)encode(refs(steps), limits);
  return detail::NativeWorkflowProgramAccess::make(std::move(steps), limits);
}
std::size_t detail::native_workflow_inputs_bytes(
    const std::vector<NativeWorkflowStepRef> &steps, std::size_t cap) {
  Budget b{cap};b.add(512);
  for(const auto &step:steps)charge_step(step,b);
  return b.used;
}
std::size_t detail::native_workflow_intent_bytes(
    const std::vector<NativeWorkflowStepRef> &steps, std::size_t max_steps,
    std::size_t cap) {
  const auto limit =
      std::min(cap, NativeWorkflowProgramLimits::hard_max_record_bytes);
  const auto record = encode(steps, {max_steps, limit});
  return detail::imagej_roi_record_bytes(record, cap);
}
std::size_t detail::validate_disabled_native_workflow_step(
    const NativeWorkflowStepRef &step, std::size_t cap) {
  // Reuse the canonical retained-intent grammar without cloning the DTO or
  // resolving an export on a destination. Projection wire validation belongs
  // to retained intent; no workflow output object/data is constructed here.
  const auto limit =
      std::min(cap, NativeWorkflowProgramLimits::hard_max_record_bytes);
  const auto record = encode({step}, {1, limit});
  return detail::imagej_roi_record_bytes(record, cap);
}
codec::Json detail::bounded_native_workflow_json(
    const std::string &text, const NativeWorkflowProgramLimits &limits) {
  valid_limits(limits);
  require(text.size() <= limits.max_record_bytes,
          "Program JSON transport limit");
  std::size_t bytes = 0, nodes = 0;
  std::vector<std::set<std::string>> objects;
  auto callback = [&](int depth, codec::Json::parse_event_t event,
                      codec::Json &value) {
    using E = codec::Json::parse_event_t;
    kernel_checkpoint_periodic();
    require(depth <= 128, "Program JSON depth limit");
    if (event != E::object_end && event != E::array_end) {
      require(++nodes <= 4 * 1024 * 1024, "Program JSON node limit");
      const auto n =
          std::size_t(64) +
          (value.is_string() ? value.get_ref<const std::string &>().size() : 0);
      require(n <= limits.max_record_bytes - bytes,
              "Program JSON structure limit");
      bytes += n;
    }
    if (event == E::object_start)
      objects.emplace_back();
    else if (event == E::key)
      require(!objects.empty() &&
                  objects.back().insert(value.get<std::string>()).second,
              "Duplicate program JSON key");
    else if (event == E::object_end)
      objects.pop_back();
    return true;
  };
  auto parsed = codec::Json::parse(text, callback);
  require(parsed.is_object(), "Workflow JSON root must be an object");
  return parsed;
}
NativeWorkflowProgram
native_workflow_program_from_json(const std::string &text,
                                  const NativeWorkflowProgramLimits &limits) {
  return native_workflow_program_from_record(
      codec::object_from_json(
          detail::bounded_native_workflow_json(text, limits)),
      limits);
}
} // namespace imagejxx
