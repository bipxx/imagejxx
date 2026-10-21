#include "axis_command_record.hpp"
#include "imagej_roi_import_internal.hpp"
#include "kernel_control.hpp"
#include "native_workflow_internal.hpp"
#include "rgb_dataset_internal.hpp"
#include "color_stack_internal.hpp"
#include "indexed_dataset_internal.hpp"
#include "indexed_rgb_internal.hpp"
#include "session_metadata.hpp"
#include <cmath>
#include <imagejxx/gaussian_command.hpp>
#include <imagejxx/imagej_roi_zip_export.hpp>
#include <imagejxx/rank_command.hpp>
namespace imagejxx::detail {
namespace {
using namespace axis_command_record;
void require(bool b, const char *s) {
  if (!b)
    throw std::invalid_argument(s);
}
void numeric(const session_metadata::Json &node, NumericArray &out,
             std::size_t depth) {
  kernel_checkpoint_periodic();
  if (node.is_array()) {
    if (out.shape.size() == depth)
      out.shape.push_back(node.size());
    else
      require(out.shape.at(depth) == node.size(),
              "Captured registry array is ragged");
    for (const auto &child : node)
      numeric(child, out, depth + 1);
  } else {
    require(node.is_number() && depth == out.shape.size(),
            "Captured registry parameter requires numeric data");
    out.values.push_back(node.get<double>());
  }
}
OperationValue parameter(const MetadataValue &value) {
  return std::visit(
      [](const auto &v) -> OperationValue {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::shared_ptr<const MetadataTree>> ||
                      std::is_same_v<T, std::vector<std::string>>) {
          NumericArray result;
          numeric(session_metadata::to_json(MetadataValue(v)), result, 0);
          result.validate();
          return result;
        } else
          return v;
      },
      value);
}
void registry_original(const MetadataObject &r) {
  preflight_imagej_roi_import_record(r);
  for (const auto &[key, value] : r)
    require(key == "operation" || key == "parameters" || key == "selection" ||
                key == "all_slices" || key == "slice_index" ||
                key == "dataset_policy",
            "Unsupported captured registry field");
  const auto &id = text_value(r.at("operation"));
  OperationParameters parameters;
  if (auto it = r.find("parameters"); it != r.end())
    for (const auto &[key, value] : object_value(it->second))
      parameters[key] = parameter(value);
  validate_operation_record(id, parameters);
  if (auto it = r.find("all_slices"); it != r.end())
    require(std::holds_alternative<bool>(it->second),
            "Captured all_slices requires bool");
  if (auto it = r.find("slice_index"); it != r.end())
    (void)index_value(it->second);
  if (auto it = r.find("dataset_policy"); it != r.end())
    require(id == "threshold" &&
                text_value(it->second) == "preserve_untargeted",
            "Unsupported captured dataset policy");
  if (auto it = r.find("selection");
      it != r.end() && !std::holds_alternative<std::monostate>(it->second)) {
    const auto selected = session_metadata::to_json(it->second);
    if (!selected.empty())
      (void)session_metadata::selection_from_json(selected);
  }
}
} // namespace
const std::string &native_workflow_original_schema(const MetadataObject &r) {
  return axis_command_record::text_value(r.at("schema"));
}
bool native_workflow_bound_operation_original(const MetadataObject &r) {
  return r.count("schema_version") != 0;
}
void validate_native_workflow_operation_original(
    const std::string &id, const OperationParameters &parameters,
    const MetadataObject &r) {
  if(id=="rgb_dataset") {
    require(parameters.empty(),"RGB workflow intent requires empty registry parameters");
    validate_rgb_dataset_command_record(r);return;
  }
  if(id=="color_stack") {
    (void)color_stack_workflow_request(parameters,r);return;
  }
  if(id=="indexed_dataset") {(void)indexed_dataset_workflow_request(parameters,r);return;}
  if(id=="indexed_rgb") {(void)indexed_rgb_workflow_request(parameters,r);return;}
  if (!native_workflow_bound_operation_original(r)) {
    registry_original(r);
    validate_operation_record(id, parameters);
    return;
  }
  require(parameters.empty(),
          "Captured workflow filters cannot override parameters");
  if (id == "gaussian_blur_imagej")
    validate_gaussian_command_record(r);
  else if (id == "rank_filter_imagej" || id == "remove_outliers_imagej" || id == "top_hat_imagej" || id == "remove_nans_imagej") {
    require(text_value(r.at("operation")) == id, "Captured rank operation mismatch");
    validate_rank_command_record(r);
  }
  else
    throw std::invalid_argument("Unsupported captured workflow filter");
}
void validate_native_workflow_operation_intent(
    const NativeWorkflowOperation &v) {
  if(v.operation=="rgb_dataset" || v.operation=="color_stack" || v.operation=="indexed_dataset" || v.operation=="indexed_rgb")
    require(v.original && (v.operation!="rgb_dataset" || v.parameters.empty()) && !v.filter_options && !v.use_destination_calibration,
            "Dataset conversion workflow requires a complete capture and whole-dataset target");
  if (v.operation == "convert_image" || v.operation=="rgb_dataset" || v.operation=="color_stack" || v.operation=="indexed_dataset" || v.operation=="indexed_rgb") {
    require(v.target.scope == TargetScope::Ranges &&
                v.target.roi == RoiPolicy::WholePlane &&
                v.target.mode == ExecutionMode::Planes &&
                v.target.destination == OutputDestination::InPlace,
            "Workflow conversion requires explicit whole-dataset ranges");
    for (const auto &range : {v.target.t, v.target.c, v.target.z})
      require(range && range->start == 0 && range->stop && *range->stop > 0 && range->step == 1,
              "Workflow conversion requires complete unit-stride axis ranges");
  }
  if (v.original)
    validate_native_workflow_operation_original(v.operation, v.parameters,
                                                *v.original);
  else if (!v.filter_options)
    validate_operation_record(v.operation, v.parameters);
  if (v.filter_options) {
    require(v.parameters.empty(),
            "Typed workflow filter options require empty registry parameters");
    require(v.target.mode == ExecutionMode::Planes,
            "Typed workflow filter requires plane execution");
    std::visit(
        [&](const auto &options) {
          using T = std::decay_t<decltype(options)>;
          if constexpr (std::is_same_v<T, GaussianBlurCommandOptions>) {
            require(v.operation == "gaussian_blur_imagej",
                    "Gaussian options require Gaussian operation");
            validate_gaussian_command_options(options);
          } else {
            require(v.operation == "rank_filter_imagej",
                    "Rank options require rank operation");
            validate_rank_command_options(options);
          }
        },
        *v.filter_options);
  }
}
void validate_native_workflow_import_original(const MetadataObject &r) {
  const auto &schema = native_workflow_original_schema(r);
  if (schema == "imagejxx.imagej-annotation-import-command")
    validate_imagej_annotation_import_record(r);
  else if (schema == "imagejxx.imagej-roi-import-command")
    validate_imagej_roi_import_record(r);
  else
    throw std::invalid_argument("Unsupported captured workflow import");
}
void validate_native_workflow_export_original(const MetadataObject &r) {
  const auto &schema = native_workflow_original_schema(r);
  if (schema == "imagejxx.imagej-annotation-export")
    validate_imagej_annotation_export_record(r);
  else if (schema == "imagejxx.imagej-roi-export")
    validate_imagej_roi_export_record(r);
  else if (schema == "imagejxx.imagej-roi-zip-export")
    validate_imagej_roi_zip_export_record(r);
  else
    throw std::invalid_argument("Unsupported captured workflow export");
}
} // namespace imagejxx::detail
