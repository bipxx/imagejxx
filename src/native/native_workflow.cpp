#include "imagej_annotation_export_internal.hpp"
#include "imagej_annotation_import_internal.hpp"
#include "imagej_roi_import_internal.hpp"
#include "kernel_control.hpp"
#include "operation_internal.hpp"
#include "native_workflow_internal.hpp"
#include "contrast_apply_workflow.hpp"
#include "selection_enlargement_workflow.hpp"
#include "selection_creation_workflow.hpp"
#include "annotation_workflow.hpp"
#include "native_workflow_program_internal.hpp"
#include "rgb_dataset_internal.hpp"
#include "color_stack_internal.hpp"
#include "indexed_dataset_internal.hpp"
#include "indexed_rgb_internal.hpp"
#include "workflow_stage_metadata.hpp"
#include "session_metadata.hpp"
#include <imagejxx/gaussian_command.hpp>
#include <imagejxx/rank_command.hpp>
#include <algorithm>
#include <set>
#include <type_traits>

namespace imagejxx {
namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::invalid_argument(message);
}
struct Budget {
  std::size_t cap, used = 0;
  void add(std::size_t n) {
    require(n <= cap - used, "Native workflow retained-byte limit");
    used += n;
  }
  void product(std::size_t a, std::size_t b) {
    require(!a || b <= (cap - used) / a, "Native workflow retained-byte limit");
    add(a * b);
  }
  void record(const MetadataObject &r) {
    add(detail::imagej_roi_record_bytes(r, cap - used));
  }
  void selection(const Selection &value) {
    add(sizeof(Selection));add(value.kind.size());
    product(value.points.size(),3*sizeof(std::array<double,2>));
    product(value.packed_mask().size(),3);product(value.path_commands().size(),3*sizeof(double));
    record(value.attributes);
  }
  void data(const DocumentSnapshot &s) {
    const auto shape = s.data().dataset.shape();
    product(shape.pixel_count(), sizeof(double));
    record(s.data().metadata.extensions);
    record(s.data().metadata.properties);
    record(s.data().metadata.record_layout);
    add(s.data().metadata.title.size());
    if(s.data().selection)selection(*s.data().selection);
    if(s.data().previous_selection)selection(*s.data().previous_selection);
    product(s.data().rois.size(),3*sizeof(Roi));
    for(const auto &row:s.data().rois) {
      add(row.name.size());selection(row.selection);record(row.attributes);
    }
    for(const auto &overlay:s.data().overlays)record(overlay);
  }
};
void checkpoint(const ExecutionControl &c) {
  if (c.cancellation && c.cancellation->canceled())
    throw OperationCancelled();
  if(current_execution_scope && &current_execution_scope->control==&c)
    kernel_checkpoint();
  else {
    ExecutionScope scope(c);kernel_checkpoint();
  }
  if (c.cancellation && c.cancellation->canceled())
    throw OperationCancelled();
}
void workflow_progress(const ExecutionControl &c,std::size_t done,std::size_t total) {
  if(current_execution_scope && &current_execution_scope->control==&c)
    execution_stage_progress("workflow",done,total);
  else {
    ExecutionScope scope(c);execution_stage_progress("workflow",done,total);
  }
}
bool calibration_equal(const SpatialCalibration &a,
                       const SpatialCalibration &b) {
  return a.spacing == b.spacing && a.origin == b.origin &&
         a.direction == b.direction && a.unit == b.unit &&
         a.frame_interval == b.frame_interval &&
         a.time_origin == b.time_origin && a.time_unit == b.time_unit;
}
void parameters_budget(Budget &b, const OperationParameters &parameters) {
  for (const auto &[key, value] : parameters) {
    b.add(key.size() + sizeof(OperationValue));
    std::visit(
        [&](const auto &v) {
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<T, std::string>)
            b.add(v.size());
          else if constexpr (std::is_same_v<T, NumericArray>) {
            b.product(v.values.size(), sizeof(double));
            b.product(v.shape.size(), sizeof(std::size_t));
          } else if constexpr (std::is_same_v<T,
                                              std::shared_ptr<const Image>>) {
            require(bool(v), "Null workflow image parameter");
            b.product(v->pixels.size(), sizeof(double));
          }
        },
        value);
  }
}
ResolvedRgbDatasetCommand workflow_rgb(const DocumentSnapshot &source,
    const NativeWorkflowOperation &operation,std::size_t cap) {
  detail::validate_native_workflow_operation_intent(operation);
  const auto shape=source.data().dataset.shape();
  require(*operation.target.t->stop==shape.t && *operation.target.c->stop==shape.c &&
          *operation.target.z->stop==shape.z,"RGB workflow requires complete current-stage ranges");
  (void)resolve_target(source,operation.target);
  auto request=detail::rgb_dataset_request_from_record(*operation.original);
  request.destination=OutputDestination::InPlace;
  request.max_working_bytes=std::min(request.max_working_bytes,cap);
  const auto position=detail::session_metadata::to_json(operation.original->at("view_position")).get<std::array<int,3>>();
  auto view=source.display();view.t=position[0];view.c=position[1];view.crosshair[2]=position[2];
  return resolve_rgb_dataset_command(source.with_display(std::move(view)),request);
}
ResolvedColorStackCommand workflow_color_stack(const DocumentSnapshot &source,
    const NativeWorkflowOperation &operation,std::size_t cap) {
  detail::validate_native_workflow_operation_intent(operation);
  const auto shape=source.data().dataset.shape();
  require(*operation.target.t->stop==shape.t && *operation.target.c->stop==shape.c &&
          *operation.target.z->stop==shape.z,"Color stack workflow requires complete current-stage ranges");
  (void)resolve_target(source,operation.target);
  auto request=detail::color_stack_workflow_request(operation.parameters,*operation.original);
  request.destination=OutputDestination::InPlace;
  request.max_working_bytes=std::min(request.max_working_bytes,cap);
  auto rendering=request.action==ColorStackAction::FlattenRgb && shape.s==1?detail::color_stack_rendering_options(*operation.original):std::nullopt;
  if(!operation.parameters.empty())return resolve_color_stack_command(source,request,rendering);
  const auto position=detail::session_metadata::to_json(operation.original->at("view_position")).get<std::array<int,3>>();
  auto view=source.display();view.t=position[0];view.c=position[1];view.crosshair[2]=position[2];
  return resolve_color_stack_command(source.with_display(std::move(view)),request,rendering);
}
template<class Resolve,class Request>
auto workflow_indexed(const DocumentSnapshot &source,const NativeWorkflowOperation &operation,std::size_t cap,Request read,Resolve resolve) {
  detail::validate_native_workflow_operation_intent(operation);
  const auto shape=source.data().dataset.shape();
  require(*operation.target.t->stop==shape.t && *operation.target.c->stop==shape.c && *operation.target.z->stop==shape.z,"Indexed workflow requires complete current-stage ranges");
  (void)resolve_target(source,operation.target);
  auto request=read(operation.parameters,*operation.original);
  request.destination=OutputDestination::InPlace;request.max_working_bytes=std::min(request.max_working_bytes,cap);
  if(!operation.parameters.empty())return resolve(source,request);
  const auto position=detail::session_metadata::to_json(operation.original->at("view_position")).get<std::array<int,3>>();
  auto view=source.display();view.t=position[0];view.c=position[1];view.crosshair[2]=position[2];
  return resolve(source.with_display(std::move(view)),request);
}
HistogramCommandLimits histogram_limits(const NativeWorkflowHistogram &v, std::size_t cap) {
  auto limits = v.limits;
  limits.processing.max_working_bytes = std::min(limits.processing.max_working_bytes, cap);
  limits.max_record_bytes = std::min(limits.max_record_bytes, cap);
  return limits;
}
ResolvedHistogramCommand workflow_histogram(const DocumentSnapshot &source,
    const NativeWorkflowHistogram &step,const HistogramCommandLimits &limits) {
  // Historical evidence is validated even when explicit choices replace it.
  detail::validate_native_workflow_histogram_intent(step);
  if(step.authored_options)return resolve_histogram_command(source,
      step.authored_options->histogram,step.target,step.authored_options->log_counts,limits);
  return rebind_histogram_command(source,*step.original,step.target,limits);
}
ResolvedImageJAnnotationExport workflow_export(const DocumentSnapshot &source,
                                               const NativeWorkflowExport &v) {
  if(v.original && detail::native_workflow_original_schema(*v.original)=="imagejxx.imagej-annotation-export")
    return v.zip?rebind_imagej_annotation_zip_export(source,*v.original,v.items,v.limits,v.compress)
                :rebind_imagej_annotation_export(source,*v.original,v.items.at(0),v.limits);
  return v.zip?resolve_imagej_annotation_zip_export(source,v.items,v.limits,v.compress)
              :resolve_imagej_annotation_export(source,v.items.at(0),v.limits);
}
ResolvedImageJAnnotationImport workflow_import(const DocumentSnapshot &source,
                                               const NativeWorkflowImport &v) {
  return v.original && detail::native_workflow_original_schema(*v.original)=="imagejxx.imagej-annotation-import-command"
      ?rebind_imagej_annotation_import(source,*v.original,v.items,v.mode,v.limits)
      :resolve_imagej_annotation_import(source,v.items,v.mode,v.limits);
}
PixelType conversion_type(const OperationParameters &parameters) {
  const auto &name=std::get<std::string>(parameters.at("dtype"));
  if(name=="uint8")return PixelType::UInt8;
  if(name=="uint16")return PixelType::UInt16;
  if(name=="float32")return PixelType::Float32;
  if(name=="float64")return PixelType::Float64;
  throw std::invalid_argument("Invalid workflow conversion dtype");
}
// Preflight owns a parent execution scope. Delegated command APIs provide
// their own callback scope; silence this parent until that step returns so a
// callback's native work cannot recursively redispatch the same callable.
struct PreflightStepDispatch {
  ExecutionScope &scope;bool previous;
  explicit PreflightStepDispatch(ExecutionScope &value)
      :scope(value),previous(value.dispatching_callback) {scope.dispatching_callback=true;}
  ~PreflightStepDispatch(){scope.dispatching_callback=previous;}
};
// A V4/V8 workflow admits every stage before calling user code. Kernel
// dispatch walks all inherited scopes, so a cancellation-only child alone is
// insufficient. Guard the existing chain without allocating another container;
// each guard restores its prior dispatch state on success or exception. The
// scopes stay linked, preserving every inherited cancellation token.
template<class Function>
auto without_inherited_callbacks(ExecutionScope *scope, Function &function)
    -> decltype(function()) {
  if(!scope)return function();
  PreflightStepDispatch dispatch(*scope);
  return without_inherited_callbacks(scope->previous,function);
}
template<class Function>
auto workflow_admission_callbacks(
    const std::vector<detail::NativeWorkflowStepRef> &steps,
    const ExecutionControl &control, Function function) -> decltype(function(control)) {
  const bool path_intent=std::any_of(steps.begin(),steps.end(),[](const auto &step) {
    const auto p=std::get_if<const NativeWorkflowOperation *>(&step);
    return p && detail::native_path_creation_intent(**p);
  });
  if(!path_intent)return function(control); // Preserve legacy callback timing.
  ExecutionControl quiet;quiet.cancellation=control.cancellation;
  ExecutionScope scope(quiet);
  auto admit=[&] { kernel_checkpoint();return function(quiet); };
  return without_inherited_callbacks(current_execution_scope,admit);
}
void path_grid_preflight(const std::vector<detail::NativeWorkflowStepRef> &steps) {
  const bool paths=std::any_of(steps.begin(),steps.end(),[](const auto &step){
    const auto p=std::get_if<const NativeWorkflowOperation *>(&step);
    return p && (*p)->enabled && detail::native_path_creation_intent(**p);
  });
  if(!paths)return;
  for(const auto &step:steps)if(const auto p=std::get_if<const NativeWorkflowOperation *>(&step)) {
    const auto &v=**p;if(!v.enabled)continue;
    // These dataset conversions alter axis/sample interpretation. An authored
    // history remapping contract must be supplied before combining them in V8.
    if(v.operation=="rgb_dataset" || v.operation=="color_stack" ||
       v.operation=="indexed_dataset" || v.operation=="indexed_rgb")
      throw std::invalid_argument("Path workflow coordinate remapping requires an explicit history policy");
    if(detail::annotation_workflow_operation(v.operation) || v.operation=="selection_creation" ||
       v.operation=="selection_enlargement" || v.operation=="rgb_contrast_session" ||
       v.operation=="rgb_contrast_stack" || v.operation=="rgb_contrast_apply" ||
       v.operation=="contrast_apply" || v.operation=="convert_image")continue;
    require(operation_spec(v.operation).geometry==GeometryEffect::Preserve,
            "Path workflow coordinate remapping requires an explicit history policy");
  }
}
void preflight(const DocumentSnapshot &initial,
               const std::vector<detail::NativeWorkflowStepRef> &steps,
               Budget &budget, const ExecutionControl &control) {
  path_grid_preflight(steps);
  ExecutionScope scope(control);
  auto source=initial;
  std::set<std::string> names;
  for (const auto &step : steps) {
    checkpoint(control);
    PreflightStepDispatch dispatch(scope);
    std::visit(
        [&](const auto *pointer) {
          const auto &v = *pointer;
          using T = std::decay_t<decltype(v)>;
          if constexpr(std::is_same_v<T,NativeWorkflowOperation>)
            if(v.selection_creation_extent)
              (void)detail::selection_creation_workflow_extent(v,budget.cap-budget.used);
          if (!v.enabled) {
            if constexpr (std::is_same_v<T, NativeWorkflowExport> || std::is_same_v<T, NativeWorkflowHistogram>) {
              require(!v.name.empty() && v.name.size() <= 240 &&
                          v.name != "." && v.name != ".." &&
                          v.name.find_first_of("/\\:\0", 0, 4) ==
                              std::string::npos,
                      "Workflow output requires a flat name");
              require(names.insert(v.name).second,
                      "Duplicate workflow output name");
            }
            budget.add(detail::validate_disabled_native_workflow_step(
                step, budget.cap - budget.used));
            return;
          }
          budget.add(sizeof(T));
          if (v.original)
            budget.record(*v.original);
          if constexpr (std::is_same_v<T, NativeWorkflowOperation>) {
            budget.add(v.operation.size());parameters_budget(budget,v.parameters);
            budget.product(v.target.positions.size(),sizeof(PlanePosition));
            if(v.selection_creation)
              budget.add(detail::selection_creation_request_bytes(*v.selection_creation,budget.cap-budget.used));
            if(v.annotation_intent)budget.add(detail::annotation_workflow_intent_bytes(*v.annotation_intent,budget.cap-budget.used));
            if(detail::annotation_workflow_operation(v.operation)) {
              std::size_t working=0;
              auto edit=detail::prepare_annotation_workflow(source,v,budget.cap-budget.used,control,&working);
              budget.add(working);source=edit.preview();return;
            }
            if(v.operation=="selection_creation") {
              const auto cap=budget.cap-budget.used;
              const auto command=detail::workflow_selection_creation(source,v,cap,control);
              budget.add(command.estimate().working_bytes);budget.record(command.record());
              auto edit=prepare_selection_creation(command,control,cap);
              source=edit.preview();return;
            }
            if(v.operation=="selection_enlargement") {
              const auto limits=detail::selection_enlargement_workflow_limits(budget.cap-budget.used);
              const auto command=detail::workflow_selection_enlargement(source,v,limits,control);
              budget.add(command.estimate().working_bytes);budget.record(command.record());
              auto edit=prepare_selection_enlargement(command,control,limits.max_working_bytes);
              source=edit.preview();return;
            }
            if(v.operation=="rgb_contrast_session") {
              const auto limits=detail::contrast_apply_workflow_limits(budget.cap-budget.used);
              const auto command=detail::workflow_rgb_contrast_session(source,v,limits);
              budget.add(estimate_rgb_contrast_session_command(command,limits).working_bytes);
              budget.data(source);budget.record(command.record());
              source=detail::rgb_contrast_session_metadata_preview(command);return;
            }
            if(v.operation=="rgb_contrast_stack") {
              const auto limits=detail::contrast_apply_workflow_limits(budget.cap-budget.used);
              const auto command=detail::workflow_rgb_contrast_stack(source,v,limits);
              budget.add(estimate_rgb_contrast_stack_command(command,limits).working_bytes);
              budget.data(source);budget.record(command.record());
              source=detail::rgb_contrast_stack_metadata_preview(command,source.display());return;
            }
            if(v.operation=="rgb_contrast_apply") {
              const auto limits=detail::contrast_apply_workflow_limits(budget.cap-budget.used);
              const auto command=detail::workflow_rgb_contrast_apply(source,v,limits);
              budget.add(estimate_rgb_contrast_apply_command(command,limits).working_bytes);
              budget.data(source);budget.record(command.record());
              source=detail::contrast_apply_metadata_preview(command);return;
            }
            if(v.operation=="contrast_apply") {
              const auto limits=detail::contrast_apply_workflow_limits(budget.cap-budget.used);
              const auto command=detail::workflow_contrast_apply(source,v,limits);
              budget.add(estimate_contrast_apply_command(command,limits).working_bytes);
              budget.data(source);budget.record(command.record());
              source=detail::contrast_apply_metadata_preview(command);return;
            }
            if(v.operation=="rgb_dataset") {
              auto command=workflow_rgb(source,v,budget.cap-budget.used);
              budget.add(command.layout().working_bytes);budget.record(command.record());
              source=detail::rgb_dataset_metadata_preview(command);return;
            }
            if(v.operation=="color_stack") {
              auto command=workflow_color_stack(source,v,budget.cap-budget.used);
              budget.add(command.layout().working_bytes);budget.record(command.record());
              source=detail::color_stack_metadata_preview(command);return;
            }
            if(v.operation=="indexed_dataset") {
              auto command=workflow_indexed(source,v,budget.cap-budget.used,detail::indexed_dataset_workflow_request,resolve_indexed_dataset_command);
              budget.add(command.layout().working_bytes);budget.record(command.record());
              source=detail::indexed_dataset_metadata_preview(command);return;
            }
            if(v.operation=="indexed_rgb") {
              auto command=workflow_indexed(source,v,budget.cap-budget.used,detail::indexed_rgb_workflow_request,resolve_indexed_rgb_command);
              budget.add(command.layout().working_bytes);budget.record(command.record());
              source=detail::indexed_rgb_metadata_preview(command);return;
            }
            require(operation_spec(v.operation).geometry ==
                        GeometryEffect::Preserve || v.operation == "convert_image",
                    "Workflow geometry requires an explicit native remap step");
            require(v.target.destination == OutputDestination::InPlace,
                    "Workflow step requires in-place destination");
            if (v.operation == "convert_image")
              budget.record(source.display().properties);
            const auto resolved=resolve_target(source, v.target, v.operation);
            std::optional<OperationParameters> normalized;
            if(!v.filter_options && !(v.original && detail::native_workflow_bound_operation_original(*v.original)))
              normalized=validate_execution_parameters(v.operation,resolved,v.parameters,{});
            // Reserve a conservative complete output backing before any kernel.
            budget.data(source);
            detail::validate_native_workflow_operation_intent(v);
            if (v.operation == "convert_image") {
              budget.add(1024*1024); // Palette/type metadata and fixed lookup workspace.
              const auto shape = source.data().dataset.shape();
              require(shape.s == 1 || (source.data().dataset.pixel_type() == PixelType::UInt8 &&
                          (shape.s == 3 || shape.s == 4)),
                      "Workflow conversion requires scalar or UInt8 RGB/RGBA input");
              // Conversion retains scalar results and intermediate plane tables.
              // Charge conservative double backing and per-plane bookkeeping
              // before any processing reads, including for one-pixel planes.
              budget.product(source.data().dataset.shape().pixel_count(),
                             5 * sizeof(double));
              budget.product(source.data().dataset.shape().plane_count(), 512);
              const auto target = resolve_target(source, v.target, v.operation);
              (void)validate_execution_parameters(v.operation, target, v.parameters, {});
            }
            if (v.filter_options)
              std::visit(
                  [&](const auto &options) {
                    using O = std::decay_t<decltype(options)>;
                    if constexpr (std::is_same_v<O, GaussianBlurCommandOptions>)
                      (void)resolve_gaussian_command(source, v.target, options);
                    else
                      (void)resolve_rank_command(source, v.target, options);
                  },
                  *v.filter_options);
            if (v.original) {
              if (detail::native_workflow_bound_operation_original(
                      *v.original) &&
                  v.operation == "gaussian_blur_imagej") {
                auto rebound = v.filter_options
                                   ? resolve_gaussian_command(
                                         source, v.target,
                                         std::get<GaussianBlurCommandOptions>(
                                             *v.filter_options))
                                   : rebind_gaussian_command(
                                         source, *v.original, v.target);
                require(v.use_destination_calibration ||
                            metadata_equal(v.original->at("calibration"),
                                           rebound.record().at("calibration")),
                        "Workflow Gaussian requires destination calibration "
                        "consent");
              } else if(detail::native_workflow_bound_operation_original(*v.original) && (v.operation=="rank_filter_imagej" || v.operation=="remove_outliers_imagej" || v.operation=="top_hat_imagej" || v.operation=="remove_nans_imagej"))
                (void)rebind_rank_command(source,*v.original,v.target);
            }
            if(v.operation=="convert_image")source=detail::scalar_conversion_metadata_preview(source,conversion_type(*normalized));
            else source=detail::workflow_filter_metadata_preview(source,v.operation,normalized?*normalized:v.parameters,resolved);
          } else if constexpr (std::is_same_v<T, NativeWorkflowHistogram>) {
            detail::validate_native_workflow_histogram_intent(v);
            require(names.insert(v.name).second, "Duplicate workflow output name");
            budget.add(v.name.size());
            budget.product(v.target.positions.size(), sizeof(PlanePosition));
            const auto limits = histogram_limits(v, budget.cap - budget.used);
            const auto command = workflow_histogram(source, v, limits);
            budget.add(estimate_histogram_command(command, limits).working_bytes);
            budget.record(command.record());
            // Retained output DTO and conservative CSV text + byte-copy scratch.
            budget.add(sizeof(NativeWorkflowHistogramOutput) + 4096);
            budget.product(std::size_t(command.options().bins), 512);
          } else if constexpr (std::is_same_v<T, NativeWorkflowImport>) {
            if (v.original)
              detail::validate_native_workflow_import_original(*v.original);
            detail::validate_imagej_annotation_import_limits(v.limits);
            require(v.mode == ImageJRoiImportMode::Append ||
                        v.mode == ImageJRoiImportMode::Replace,
                    "Workflow import mode");
            require(!v.items.empty() &&
                        v.items.size() <= v.limits.batch.max_entries,
                    "Workflow import entry count");
            std::size_t wire = 0;
            for (const auto &item : v.items) {
              std::visit(
                  [&](const auto &i) {
                    require(i.data.size() <=
                                v.limits.batch.max_total_bytes - wire,
                            "Workflow import total byte limit");
                    wire += i.data.size();
                    budget.add(i.data.size());
                    budget.add(i.name.size());
                  },
                  item);
              // Admit each original, mapping and native replacement before
              // kernels.
              auto admitted =
                  detail::admit_imagej_annotation_import_item(item, v.limits);
              std::visit(
                  [&](const auto &m) {
                    using M = std::decay_t<decltype(m)>;
                    if constexpr (std::is_same_v<M, MappedImageJRoi>)
                      budget.record(
                          {{"row", encode_planar_annotations(
                                       m.target_extent(), {m.annotation()})}});
                    else
                      budget.record(
                          {{"row", encode_planar_display_annotations(
                                       m.target_extent(), {m.annotation()})}});
                  },
                  admitted);
            }
            const auto command=workflow_import(source,v);budget.record(command.record());
            auto edit=prepare_imagej_annotation_import(command);source=edit.preview();
          } else {
            require(!v.name.empty() && v.name.size() <= 240 && v.name != "." &&
                        v.name != ".." &&
                        v.name.find_first_of("/\\:\0", 0, 4) ==
                            std::string::npos,
                    "Workflow output requires a flat name");
            require(names.insert(v.name).second,
                    "Duplicate workflow output name");
            budget.add(v.name.size());
            require(!v.items.empty() && (v.zip || v.items.size() == 1),
                    "Workflow export member count");
            if (v.original)
              detail::validate_native_workflow_export_original(*v.original);
            detail::validate_imagej_annotation_export_limits(v.limits);
            std::size_t bytes = 0, nodes = 0;
            for (const auto &i : v.items) {
              budget.add(i.name.size());
              detail::preflight_imagej_annotation_projection(
                  i.standard_projection, v.limits, bytes, nodes);
            }
            budget.add(bytes);
            const auto command=workflow_export(source,v);budget.record(command.record());
            budget.add(command.data().size());for(const auto &member:command.members())budget.add(member.data.size());
          }
        },
        step);
  }
}
void extensions(DocumentTransaction &tx, const MetadataObject &before,
                const MetadataObject &after, bool properties) {
  for (const auto &[key, value] : before)
    if (!after.count(key)) {
      if (properties)
        tx.remove_property(key);
      else
        tx.remove_extension(key);
    }
  for (const auto &[key, value] : after) {
    auto found = before.find(key);
    if (found == before.end() || !metadata_equal(value, found->second)) {
      if (properties)
        tx.set_property(key, value);
      else
        tx.set_extension(key, value);
    }
  }
}
} // namespace
DocumentTransaction PreparedNativeWorkflow::begin_edit() const {
  auto tx = source_.begin_edit(dependencies());
  const auto &before = source_.data(), &after = preview_.data();
  if (!after.dataset.shares_pixels(before.dataset))
    tx.replace_dataset(after.dataset);
  if (!calibration_equal(after.dataset.calibration(),
                         before.dataset.calibration()))
    tx.set_calibration(after.dataset.calibration());
  if (after.metadata.title != before.metadata.title)
    tx.set_title(after.metadata.title);
  extensions(tx, before.metadata.properties, after.metadata.properties, true);
  extensions(tx, before.metadata.extensions, after.metadata.extensions, false);
  if (!metadata_equal(before.metadata.record_layout,
                      after.metadata.record_layout))
    tx.set_record_layout(after.metadata.record_layout);
  if (before.selection != after.selection) {
    tx.select(after.selection);
    if(selection_history_)tx.retain_selection_history();
  }
  if (before.previous_selection != after.previous_selection)
    tx.set_previous_selection(after.previous_selection);
  if (before.rois != after.rois)
    tx.set_rois(after.rois);
  bool overlays = before.overlays.size() != after.overlays.size();
  for (std::size_t i = 0; !overlays && i < before.overlays.size(); ++i)
    overlays = !metadata_equal(before.overlays[i], after.overlays[i]);
  if (overlays)
    tx.set_overlays(after.overlays);
  if (before.spectrum != after.spectrum)
    tx.set_spectrum(after.spectrum);
  if (paired_display_)
    tx.set_axis_remap_display(preview_.display());
  return tx;
}
struct detail::NativeWorkflowAccess {
  static PreparedNativeWorkflow with_retention(PreparedNativeWorkflow result,
                                               std::size_t minimum) {
    result.retained_bytes_=std::max(result.retained_bytes_,minimum);
    return result;
  }
  static PreparedNativeWorkflow make(DocumentSnapshot source,
                                     DocumentSnapshot preview,
                                     std::vector<NativeWorkflowOutput> outputs,
                                     std::vector<NativeWorkflowHistogramOutput> histograms,
                                     std::size_t completed, std::size_t bytes,
                                     bool paired_display, bool guard_display, bool selection_history) {
    return PreparedNativeWorkflow(std::move(source), std::move(preview),
                                  std::move(outputs), std::move(histograms), completed, bytes, paired_display, guard_display, selection_history);
  }
};
PreparedNativeWorkflow
prepare_native_workflow(const DocumentSnapshot &source,
                        const std::vector<NativeWorkflowStep> &steps,
                        const NativeWorkflowLimits &limits,
                        const ExecutionControl &control) {
  const auto owned_limits=limits;
  const auto owned_control=control;
  require(owned_limits.max_steps>0 && owned_limits.max_steps<=4096 &&
          steps.size()<=owned_limits.max_steps && owned_limits.max_retained_bytes>0,
          "Native workflow step limit");
  std::vector<detail::NativeWorkflowStepRef> refs;
  refs.reserve(steps.size());
  for (const auto &step : steps)
    std::visit([&](const auto &v) { refs.emplace_back(&v); }, step);
  const auto captured_bytes=workflow_admission_callbacks(refs,owned_control,
      [&](const ExecutionControl &admission) {
    ExecutionControl quiet;quiet.cancellation=admission.cancellation;
    ExecutionScope scope(quiet);
    Budget captured{owned_limits.max_retained_bytes};
    captured.data(source);captured.record(source.display().properties);
    captured.add(sizeof(DocumentSnapshot));
    captured.add(detail::native_workflow_inputs_bytes(refs,captured.cap-captured.used));
    return captured.used;
  });
  const auto owned_source=source;
  const auto owned_steps=steps;
  refs.clear();
  for(const auto &step:owned_steps)
    std::visit([&](const auto &v){refs.emplace_back(&v);},step);
  auto prepared=detail::prepare_native_workflow_borrowed(owned_source,refs,owned_limits,owned_control);
  return detail::NativeWorkflowAccess::with_retention(std::move(prepared),captured_bytes);
}
std::size_t detail::admit_native_workflow(
    const DocumentSnapshot &source,
    const std::vector<NativeWorkflowStepRef> &steps,
    const NativeWorkflowLimits &limits, const ExecutionControl &control) {
  require(limits.max_steps > 0 && limits.max_steps <= 4096 &&
              steps.size() <= limits.max_steps,
          "Native workflow step limit");
  require(limits.max_retained_bytes > 0, "Native workflow retained-byte limit");
  return workflow_admission_callbacks(steps,control,[&](const ExecutionControl &admission) {
    Budget budget{limits.max_retained_bytes};
    budget.data(source);
    preflight(source, steps, budget, admission);
    return budget.used;
  });
}
PreparedNativeWorkflow detail::prepare_native_workflow_borrowed(
    const DocumentSnapshot &source,
    const std::vector<NativeWorkflowStepRef> &steps,
    const NativeWorkflowLimits &limits, const ExecutionControl &control) {
  const auto admitted = admit_native_workflow(source, steps, limits, control);
  std::vector<NativeWorkflowOutput> outputs;
  std::vector<NativeWorkflowHistogramOutput> histograms;
  std::size_t completed = 0;
  bool paired_display = false, guard_display = false, selection_history = false;
  Document working(source);
  working.set_history_policy(1);
  for (std::size_t index = 0; index < steps.size(); ++index) {
    checkpoint(control);
    if (!std::visit([](const auto *v) { return v->enabled; }, steps[index])) {
      workflow_progress(control,index+1,steps.size());
      checkpoint(control);
      continue;
    }
    const auto current = working.snapshot();
    std::visit(
        [&](const auto *pointer) {
          const auto &v = *pointer;
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<T, NativeWorkflowOperation>) {
            auto execute = [&](const ResolvedTarget &target,
                               const OperationParameters &parameters) {
              auto prepared = prepare_operation(v.operation, target, parameters,
                                                {}, control);

              auto edit = prepared.begin_edit();
              paired_display = paired_display || bool(edit.axis_remap_display());
              checkpoint(control);
              working.commit(edit);
            };
            if(detail::annotation_workflow_operation(v.operation)) {
              auto edit=detail::prepare_annotation_workflow(current,v,limits.max_retained_bytes,control);
              guard_display=guard_display || v.operation=="saved_roi_geometry";
              checkpoint(control);working.commit(edit);
            } else if(v.operation=="selection_creation") {
              const auto command=detail::workflow_selection_creation(current,v,limits.max_retained_bytes,control);
              auto edit=prepare_selection_creation(command,control,limits.max_retained_bytes);
              selection_history=selection_history ||
                  (command.changes_selection() && command.request().geometry==SelectionCreationGeometry::NativePathV4);
              guard_display=true;checkpoint(control);working.commit(edit);
            } else if(v.operation=="selection_enlargement") {
              const auto step_limits=detail::selection_enlargement_workflow_limits(limits.max_retained_bytes);
              const auto command=detail::workflow_selection_enlargement(current,v,step_limits,control);
              auto edit=prepare_selection_enlargement(command,control,step_limits.max_working_bytes);
              guard_display=true;checkpoint(control);working.commit(edit);
            } else if(v.operation=="rgb_contrast_session") {
              const auto limits_for_step=detail::contrast_apply_workflow_limits(limits.max_retained_bytes);
              const auto command=detail::workflow_rgb_contrast_session(current,v,limits_for_step);
              auto edit=execute_rgb_contrast_session_command(command,limits_for_step,control);
              paired_display=true;checkpoint(control);working.commit(edit);
            } else if(v.operation=="rgb_contrast_stack") {
              const auto limits_for_step=detail::contrast_apply_workflow_limits(limits.max_retained_bytes);
              auto edit=detail::execute_workflow_rgb_contrast_stack(current,v,limits_for_step,control);
              paired_display=true;checkpoint(control);working.commit(edit);
            } else if(v.operation=="rgb_contrast_apply") {
              const auto limits_for_step=detail::contrast_apply_workflow_limits(limits.max_retained_bytes);
              const auto command=detail::workflow_rgb_contrast_apply(current,v,limits_for_step);
              auto edit=execute_rgb_contrast_apply_command(command,limits_for_step,control);
              paired_display=paired_display || bool(edit.axis_remap_display());checkpoint(control);working.commit(edit);
            } else if(v.operation=="contrast_apply") {
              const auto limits_for_step=detail::contrast_apply_workflow_limits(limits.max_retained_bytes);
              const auto command=detail::workflow_contrast_apply(current,v,limits_for_step);
              auto edit=execute_contrast_apply_command(command,limits_for_step,control);
              paired_display=paired_display || bool(edit.axis_remap_display());checkpoint(control);working.commit(edit);
            } else if(v.operation=="rgb_dataset") {
              auto command=workflow_rgb(current,v,limits.max_retained_bytes);
              auto prepared=prepare_rgb_dataset(command,control,limits.max_retained_bytes);
              auto edit=prepared.begin_edit();
              if(!edit.axis_remap_display())edit.set_axis_remap_display(prepared.preview().display());
              paired_display=true;checkpoint(control);working.commit(edit);
            } else if(v.operation=="color_stack") {
              auto command=workflow_color_stack(current,v,limits.max_retained_bytes);
              auto prepared=prepare_color_stack(command,control,limits.max_retained_bytes);
              auto edit=prepared.begin_edit();paired_display=true;
              checkpoint(control);working.commit(edit);
            } else if(v.operation=="indexed_dataset") {
              auto command=workflow_indexed(current,v,limits.max_retained_bytes,detail::indexed_dataset_workflow_request,resolve_indexed_dataset_command);
              auto prepared=prepare_indexed_dataset(command,control,limits.max_retained_bytes);
              auto edit=prepared.begin_edit();paired_display=true;checkpoint(control);working.commit(edit);
            } else if(v.operation=="indexed_rgb") {
              auto command=workflow_indexed(current,v,limits.max_retained_bytes,detail::indexed_rgb_workflow_request,resolve_indexed_rgb_command);
              auto prepared=prepare_indexed_rgb(command,control,limits.max_retained_bytes);
              auto edit=prepared.begin_edit();paired_display=true;checkpoint(control);working.commit(edit);
            } else if (v.filter_options) {
              std::visit(
                  [&](const auto &options) {
                    using O = std::decay_t<decltype(options)>;
                    if constexpr (std::is_same_v<O,
                                                 GaussianBlurCommandOptions>) {
                      const auto command =
                          resolve_gaussian_command(current, v.target, options);
                      execute(command.target(), command.parameters());
                    } else {
                      const auto command =
                          resolve_rank_command(current, v.target, options);
                      execute(command.target(), command.parameters());
                    }
                  },
                  *v.filter_options);
            } else if (v.original &&
                       detail::native_workflow_bound_operation_original(
                           *v.original)) {
              if (v.operation == "gaussian_blur_imagej") {
                auto command =
                    rebind_gaussian_command(current, *v.original, v.target);
                execute(command.target(), command.parameters());
              } else {
                auto command =
                    rebind_rank_command(current, *v.original, v.target);
                execute(command.target(), command.parameters());
              }
            } else
              execute(resolve_target(current, v.target, v.operation),
                      v.parameters);
          } else if constexpr (std::is_same_v<T, NativeWorkflowHistogram>) {
            const auto limits_for_step = histogram_limits(v, limits.max_retained_bytes);
            const auto command = workflow_histogram(current, v, limits_for_step);
            auto value = execute_histogram_command(command, limits_for_step, control);
            checkpoint(control);
            histograms.push_back({index, v.name, command.record(), std::move(value)});
          } else if constexpr (std::is_same_v<T, NativeWorkflowImport>) {
            auto command=workflow_import(current,v);
            auto edit = prepare_imagej_annotation_import(command);
            checkpoint(control);
            working.commit(edit);
          } else {
            auto command=workflow_export(current,v);
            checkpoint(control);
            outputs.push_back({index, v.name, std::move(command)});
          }
        },
        steps[index]);
    ++completed;
    checkpoint(control);
    workflow_progress(control,index+1,steps.size());
  }
  checkpoint(control);
  if(selection_history) {
    const auto final=working.snapshot();
    const auto &previous=final.data().selection==source.data().selection?
        source.data().previous_selection:source.data().selection;
    if(final.data().previous_selection!=previous) {
      auto edit=final.begin_edit(Change::Data);edit.set_previous_selection(previous);working.commit(edit);
    }
  }
  checkpoint(control);
  return NativeWorkflowAccess::make(source, working.snapshot(),
                                    std::move(outputs), std::move(histograms), completed, admitted, paired_display, guard_display, selection_history);
}
} // namespace imagejxx
