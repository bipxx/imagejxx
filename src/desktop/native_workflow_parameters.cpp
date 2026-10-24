#include "native_workflow_parameters.hpp"
#include "../native/native_workflow_internal.hpp"
#include "../native/contrast_apply_workflow.hpp"
#include "../native/native_workflow_program_internal.hpp"
#include "../native/color_stack_internal.hpp"
#include "../native/selection_enlargement_workflow.hpp"
#include "../native/selection_creation_workflow.hpp"
#include "../native/annotation_workflow.hpp"
#include "planar_annotation_rebind.hpp"
#include "path_visual.hpp"
#include "geometry_number.hpp"
#include <imagejxx/selection_edit.hpp>
#include <charconv>
#include <cmath>
#include "../native/indexed_dataset_internal.hpp"
#include "../native/indexed_rgb_internal.hpp"
#include <cstring>
#include <imagejxx/native_workflow_program.hpp>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
namespace imagejxx::desktop {
namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::invalid_argument(message);
}
Json literal(const OperationValue &value) {
  return std::visit(
      [](const auto &v) -> Json {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::monostate>)
          return nullptr;
        else if constexpr (std::is_same_v<T, NumericArray> ||
                           std::is_same_v<T, std::shared_ptr<const Image>>)
          throw std::invalid_argument(
              "Structured parameter is not a catalog choice");
        else
          return v;
      },
      value);
}
struct Field {
  std::string name, label, kind;
  std::vector<char> text;
  std::vector<Json> choices;
  std::vector<std::string> choice_labels;
  bool boolean = false, optional = false, numeric = false, escaped = false,
       array_allowed = false;
  std::string default_kind;
  void write(const std::string &value) {
    require(value.size() + 1 < text.size(),
            "Parameter draft exceeds the processing memory setting");
    std::copy(value.begin(), value.end(), text.begin());
    text[value.size()] = 0;
  }
  Json value(std::size_t cap) const {
    if (kind == "null")
      return nullptr;
    if (kind == "bool")
      return boolean;
    if (kind == "text" && !escaped)
      return std::string(text.data());
    const auto parsed = detail::bounded_native_workflow_json(
        std::string("{\"value\":") + text.data() + "}", {1, cap});
    return parsed.at("value");
  }
};
} // namespace
struct NativeWorkflowParameters::State {
  DocumentSnapshot source;
  Json root, controls = Json::object();
  std::vector<Field> fields;
  std::size_t cap, processing_cap;
  bool open = true, focus = true, gaussian = false, rank = false, color_stack = false, creation = false;
  std::string error;
  Json bounds = Json::array();
  std::shared_ptr<const NativeWorkflowOperation> annotation_step;
  std::unique_ptr<PlanarAnnotationRebind> planar_editor;
  std::optional<Selection> saved_geometry;
  std::vector<std::array<double,2>> saved_points;
  std::string saved_row,saved_u,saved_v,move_u="0",move_v="0";
  std::array<std::string,5> saved_box;
  int saved_vertex=1;bool saved_confirm=false,saved_dirty=false;
  void refresh_saved() {
    saved_points=planarControlPoints(*saved_geometry);
    if(saved_vertex>=1 && std::size_t(saved_vertex)<=saved_points.size()) {
      saved_u=geometry_number_text(saved_points[std::size_t(saved_vertex-1)][0]);
      saved_v=geometry_number_text(saved_points[std::size_t(saved_vertex-1)][1]);
    }
    if(const auto box=saved_geometry->box_frame()) {
      const std::array<double,5> values{box->center[0],box->center[1],2*std::hypot(box->u[0],box->u[1]),
          2*std::hypot(box->v[0],box->v[1]),std::atan2(box->u[1],box->u[0])*180/std::acos(-1.)};
      for(int i=0;i<5;++i)saved_box[i]=geometry_number_text(values[i]);
    }
    saved_dirty=false;
  }
  std::size_t row_index() const {
    std::size_t value=0;const auto parsed=std::from_chars(saved_row.data(),saved_row.data()+saved_row.size(),value);
    require(parsed.ec==std::errc{} && parsed.ptr==saved_row.data()+saved_row.size() && value>0,"Choose a positive 1-based saved row");
    return value-1;
  }
  void stage_saved() {
    if(!saved_dirty)return;
    const auto shape=source.data().dataset.shape();
    if(saved_geometry->box_frame()) {
      std::array<double,5> v{};for(int i=0;i<5;++i)v[i]=parse_geometry_number(saved_box[i]);
      const bool ellipse=saved_geometry->kind=="ellipse" || saved_geometry->kind=="rotated_ellipse";
      Selection unit(saved_geometry->is_rounded_box()?"rounded_rectangle":ellipse?"ellipse":"rectangle",{{0,0},{1,1}},saved_geometry->width);
      unit.attributes=saved_geometry->attributes;
      saved_geometry=SelectionEditor(unit,shape.x,shape.y).box_geometry({v[0],v[1]},{v[2],v[3]},v[4]);
      refresh_saved();return;
    }
    if(saved_points.empty())return;
    require(saved_vertex>=1 && std::size_t(saved_vertex)<=saved_points.size(),"Choose a valid endpoint/control point");
    saved_geometry=SelectionEditor(*saved_geometry,shape.x,shape.y).moved_vertex(std::size_t(saved_vertex-1),{parse_geometry_number(saved_u),parse_geometry_number(saved_v)});
    refresh_saved();
  }
  void draw_saved(bool,const std::function<void(const NativeWorkflowOperation &)> &);
  State(const DocumentSnapshot &s, std::size_t limit, std::size_t processing) : source(s), cap(limit), processing_cap(processing) {}
  void observe(const std::string &name) {
    const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    controls[name] = {
        {"id", ImGui::GetItemID()},
        {"enabled", !(ImGui::GetCurrentContext()->LastItemData.ItemFlags &
                      ImGuiItemFlags_Disabled)},
        {"visible", ImGui::IsItemVisible()},
        {"point", {a.x + std::min(70.f, (b.x - a.x) / 2), (a.y + b.y) / 2}},
        {"rect", {a.x, a.y, b.x, b.y}}};
  }
  NativeWorkflowOperation proposal() const {
    auto record = root;
    auto &step = record["steps"][0];
    auto admit = [&] {
      std::size_t remaining = cap;
      std::function<void(const Json &, std::size_t)> visit =
          [&](const Json &v, std::size_t depth) {
            require(depth <= 128 && remaining >= 64,
                    "Parameter draft structure/memory setting");
            remaining -= 64;
            if (v.is_string()) {
              const auto n = v.get_ref<const std::string &>().size();
              require(n <= remaining, "Parameter text memory setting");
              remaining -= n;
            } else if (v.is_array())
              for (const auto &child : v)
                visit(child, depth + 1);
            else if (v.is_object())
              for (const auto &child : v.items()) {
                require(child.key().size() <= remaining,
                        "Parameter key memory setting");
                remaining -= child.key().size();
                visit(child.value(), depth + 1);
              }
          };
      visit(record, 0);
    };
    for (const auto &field : fields) {
      require(std::strlen(field.text.data()) + 1 < field.text.size(),
              "Parameter input reached its memory limit; shorten it or "
              "increase the processing setting");
      if (creation) {
        if(field.name=="corner_diameter")continue;
        if(field.name=="operation" || field.name=="geometry")
          step["selection_creation"][field.name]=field.value(cap);
        else step["selection_creation"]["candidate"][field.name]=field.value(cap);
      } else if (gaussian || rank)
        step["filter_options"][field.name] = field.value(cap);
      else
        step["parameters"][field.name] = {{"kind", field.kind},
                                          {"value", field.value(cap)}};
      admit();
    }
    if(creation && step["selection_creation"]["candidate"]["kind"]=="rounded_rectangle") {
      auto &attributes=step["selection_creation"]["candidate"]["attributes"];
      require(!attributes.contains("corner_diameter"), "Use the Corner diameter field for rounded boxes");
      for(const auto &field:fields)if(field.name=="corner_diameter")
        attributes["corner_diameter"]=field.value(cap);
    }
    auto program = native_workflow_program_from_record(
        metadataObjectFromJson(record), {1, cap});
    (void)
        program.json(); // Escaped transport admission before retaining a draft.
    auto result = std::get<NativeWorkflowOperation>(program.steps()[0]);
    const auto shape = source.data().dataset.shape();
    if (!result.enabled)
      return result;
    if(result.operation=="selection_creation") {
      (void)detail::workflow_selection_creation(source,result,processing_cap);
    } else if(result.operation=="selection_enlargement") {
      (void)detail::workflow_selection_enlargement(source,result,detail::selection_enlargement_workflow_limits(processing_cap));
    } else if(result.operation=="rgb_contrast_apply") {
      (void)detail::workflow_rgb_contrast_apply(source,result,detail::contrast_apply_workflow_limits(processing_cap));
    } else if(result.operation=="contrast_apply") {
      (void)detail::workflow_contrast_apply(source,result,detail::contrast_apply_workflow_limits(processing_cap));
    } else if(result.operation=="color_stack") {
      auto request=detail::color_stack_workflow_request(result.parameters,*result.original);
      request.max_working_bytes=std::min(request.max_working_bytes,processing_cap);
      (void)resolve_color_stack_command(source,request,request.action==ColorStackAction::FlattenRgb && shape.s==1?detail::color_stack_rendering_options(*result.original):std::nullopt);
    } else if(result.operation=="indexed_dataset") {
      auto request=detail::indexed_dataset_workflow_request(result.parameters,*result.original);
      request.max_working_bytes=std::min(request.max_working_bytes,processing_cap);
      (void)resolve_indexed_dataset_command(source,request);
    } else if(result.operation=="indexed_rgb") {
      auto request=detail::indexed_rgb_workflow_request(result.parameters,*result.original);
      request.max_working_bytes=std::min(request.max_working_bytes,processing_cap);
      (void)resolve_indexed_rgb_command(source,request);
    } else if (result.filter_options) {
      std::visit(
          [&](const auto &options) {
            using T = std::decay_t<decltype(options)>;
            if constexpr (std::is_same_v<T, GaussianBlurCommandOptions>)
              (void)resolve_gaussian_command(source, result.target, options);
            else
              (void)resolve_rank_command(source, result.target, options);
          },
          *result.filter_options);
    } else {
      OperationDefaultContext context;
      context.width = shape.x;
      context.height = shape.y;
      context.samples = shape.s;
      context.type = source.data().dataset.pixel_type();
      (void)validate_operation(result.operation, context, result.parameters);
    }
    return result;
  }
};
void NativeWorkflowParameters::State::draw_saved(bool enabled,
    const std::function<void(const NativeWorkflowOperation &)> &keep) {
  controls=Json::object();
  if(focus)ImGui::SetNextWindowFocus();focus=false;
  ImGui::SetNextWindowSize({650,560},ImGuiCond_FirstUseEver);
  ImGui::Begin("Edit saved ROI workflow geometry",&open);
  const auto a=ImGui::GetCurrentWindow()->InnerRect.Min,b=ImGui::GetCurrentWindow()->InnerRect.Max;
  bounds={a.x,a.y,b.x,b.y};
  ImGui::TextWrapped("Edit saved-row geometry on this workflow stage. The image stays unchanged until the workflow is published; row names, positions, width and attributes are preserved.");
  const auto shape=source.data().dataset.shape();
  ImGui::Text("Stage extent T/C/Z/Y/X/S: %d/%d/%d/%d/%d/%d",shape.t,shape.c,shape.z,shape.y,shape.x,shape.s);
  ImGui::BeginDisabled(!enabled);
  ImGui::Checkbox("Use this stage's extent",&saved_confirm);observe("confirm_extent");
  ImGui::SetNextItemWidth(120);ImGui::InputText("Saved row (1-based)",&saved_row);observe("row");
  const auto &rows=source.data().rois;
  if(ImGui::BeginCombo("Choose saved row","Select a destination row")) {
    ImGuiListClipper clip;clip.Begin(int(std::min<std::size_t>(rows.size(),INT32_MAX)));
    while(clip.Step())for(int i=clip.DisplayStart;i<clip.DisplayEnd;++i) {
      ImGui::PushID(i);if(ImGui::Selectable(rows[std::size_t(i)].name.c_str()))saved_row=std::to_string(i+1);
      observe("row."+std::to_string(i));ImGui::PopID();
    }
    ImGui::EndCombo();
  }observe("row_choice");
  if(ImGui::Button("Use selected row geometry"))try {
    const auto index=row_index();require(index<rows.size(),"Choose an existing saved row");
    saved_geometry=rows[index].selection;saved_vertex=1;refresh_saved();error.clear();
  }catch(const std::exception &e){error=e.what();}
  observe("load_geometry");
  ImGui::Text("Geometry: %s; %zu endpoints/control points",saved_geometry->kind.c_str(),saved_points.size());
  if(saved_geometry->box_frame()) {
    const char *labels[]{"Center U","Center V","Width","Height","Angle"};
    const char *keys[]{"center_u","center_v","width","height","angle"};
    for(int i=0;i<5;++i){ImGui::SetNextItemWidth(130);saved_dirty|=ImGui::InputText(labels[i],&saved_box[i]);observe(keys[i]);}
  } else if(!saved_points.empty()) {
    ImGui::SetNextItemWidth(90);
    if(ImGui::InputInt("Endpoint/control (1-based)",&saved_vertex,0,0))refresh_saved();observe("vertex");
    ImGui::SetNextItemWidth(150);saved_dirty|=ImGui::InputText("U",&saved_u);observe("vertex_u");ImGui::SameLine();
    ImGui::SetNextItemWidth(150);saved_dirty|=ImGui::InputText("V",&saved_v);observe("vertex_v");
    if(ImGui::Button("Stage endpoint/control"))try {stage_saved();error.clear();}catch(const std::exception &e){error=e.what();}
    observe("stage_vertex");
    ImGui::TextWrapped("Stage each control before choosing another. Keep includes the current control; curve commands are retained.");
  }
  ImGui::SetNextItemWidth(120);ImGui::InputText("Translate U",&move_u);observe("move_u");ImGui::SameLine();
  ImGui::SetNextItemWidth(120);ImGui::InputText("Translate V",&move_v);observe("move_v");
  if(ImGui::Button("Stage translation"))try {
    stage_saved();saved_geometry=SelectionEditor(*saved_geometry,shape.x,shape.y).moved({parse_geometry_number(move_u),parse_geometry_number(move_v)});
    refresh_saved();move_u=move_v="0";error.clear();
  }catch(const std::exception &e){error=e.what();}
  observe("translate");
  const bool accepted=ImGui::Button("Keep geometry choices");observe("keep_parameters");
  ImGui::EndDisabled();ImGui::SameLine();
  const bool discard=ImGui::Button("Discard geometry edits");observe("discard");
  const bool escape=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape);
  if(!error.empty())ImGui::TextWrapped("%s",error.c_str());
  ImGui::End();if(!open || discard || escape){open=false;return;}
  if(accepted)try {
    require(saved_confirm,"Confirm the current stage extent");stage_saved();
    auto next=*annotation_step;
    next.annotation_intent=NativeWorkflowAnnotationIntent{shape,SavedRoiGeometryRequest{row_index(),*saved_geometry}};
    (void)detail::prepare_annotation_workflow(source,next,processing_cap);
    keep(next);open=false;
  }catch(const std::exception &e){error=e.what();}
}
NativeWorkflowParameters::NativeWorkflowParameters(
    const DocumentSnapshot &source,
    std::shared_ptr<const NativeWorkflowOperation> original, std::size_t cap) {
  require(original && cap >= 16384,
          "Parameter editor requires a processing budget");
  // Borrowed admission precedes copying parameter arrays/images or originals.
  auto admitted = detail::make_native_workflow_program_borrowed(
      {detail::NativeWorkflowStepRef{original.get()}}, {1, cap / 8});
  auto operation = std::get<NativeWorkflowOperation>(admitted.steps()[0]);
  if(detail::annotation_workflow_operation(operation.operation)) {
    state_=std::make_unique<State>(source,cap,cap);auto &s=*state_;
    s.annotation_step=std::make_shared<const NativeWorkflowOperation>(operation);
    const auto intent=detail::annotation_workflow_request(operation,cap/8);
    if(const auto request=std::get_if<PlanarAnnotationRequest>(&intent.request)) {
      s.planar_editor=std::make_unique<PlanarAnnotationRebind>();s.planar_editor->show_request(source,*request);
    } else {
      const auto &saved_request=std::get<SavedRoiGeometryRequest>(intent.request);s.saved_geometry=saved_request.replacement;
      s.saved_row=saved_request.row_index==SIZE_MAX?"0":std::to_string(saved_request.row_index+1);s.refresh_saved();
    }
    return;
  }
  if (operation.operation!="rgb_contrast_apply" && operation.operation!="contrast_apply" && !operation.filter_options && operation.original &&
      detail::native_workflow_bound_operation_original(*operation.original)) {
    const auto raw = metadataJson(operation.original->at("options"));
    if (operation.operation == "gaussian_blur_imagej")
      operation.filter_options = GaussianBlurCommandOptions{
          raw.at("sigma").get<double>(), raw.at("scaled_units").get<bool>()};
    else
      operation.filter_options = RankFilterCommandOptions{
          parse_rank_filter_method(raw.at("method").get<std::string>()),
          raw.at("radius").get<double>()};
  }
  if(operation.operation=="selection_creation")
    operation.selection_creation=detail::selection_creation_workflow_request(operation,cap/8);
  if(operation.operation=="rgb_contrast_apply") {
    operation.parameters=detail::rgb_contrast_apply_workflow_parameters(detail::rgb_contrast_apply_workflow_options(operation));
  } else if(operation.operation=="contrast_apply") {
    operation.parameters=detail::contrast_apply_workflow_parameters(detail::contrast_apply_workflow_options(operation));
  } else if(operation.operation=="color_stack") {
    operation.parameters=detail::color_stack_workflow_parameters(
        detail::color_stack_workflow_request(operation.parameters,*operation.original));
  } else if(operation.operation=="indexed_dataset") {
    operation.parameters=detail::indexed_dataset_workflow_parameters(detail::indexed_dataset_workflow_request(operation.parameters,*operation.original));
  } else if(operation.operation=="indexed_rgb") {
    operation.parameters=detail::indexed_rgb_workflow_parameters(detail::indexed_rgb_workflow_request(operation.parameters,*operation.original));
  } else if (!operation.filter_options && operation.operation!="selection_creation" && operation.operation!="selection_enlargement") {
    OperationDefaultContext context;
    const auto shape = source.data().dataset.shape();
    context.width = shape.x;
    context.height = shape.y;
    context.samples = shape.s;
    context.type = source.data().dataset.pixel_type();
    auto defaults = operation_defaults(operation.operation, context,
                                       DefaultProfile::Execution);
    for (const auto &[name, value] : operation.parameters)
      defaults[name] = value;
    operation.parameters = std::move(defaults);
  }
  auto program = make_native_workflow_program({operation}, {1, cap / 8});
  (void)
      program.json(); // Admit escaped field literals before materializing them.
  state_ = std::make_unique<State>(source, cap / 8, cap);
  auto &s = *state_;
  s.root = metadataJson(program.record());
  auto &step = s.root["steps"][0];
  auto add = [&](std::string name, std::string label, std::string kind,
                 Json value, const OperationParameter *descriptor = nullptr) {
    Field field;
    field.name = std::move(name);
    field.label = std::move(label);
    field.kind = std::move(kind);
    if (descriptor) {
      field.optional = descriptor->optional;
      field.array_allowed = descriptor->kind == ParameterKind::Numeric;
      field.numeric =
          field.array_allowed || descriptor->kind == ParameterKind::Real;
      static const char *kinds[]{"bool", "int64", "uint64", "real",
                                 "text", "real",  "image"};
      field.default_kind = kinds[int(descriptor->kind)];
      for (const auto &choice : descriptor->choices)
        field.choices.push_back(literal(choice));
    }
    const auto text =
        field.kind == "text" ? value.get<std::string>() : value.dump();
    field.escaped =
        field.kind == "text" && text.find('\0') != std::string::npos;
    // Scalar edits have small fixed buffers. Structured/text fields share one
    // bounded allowance; ImGui never receives an unbounded resize callback.
    const bool large = field.kind == "array" || field.kind == "image" ||
                       field.kind == "text" || field.kind == "structured" || field.optional ||
                       field.array_allowed;
    const auto count =
        operation.selection_creation ? std::size_t(8) : operation.filter_options
            ? std::size_t(2)
            : std::max(std::size_t(1), operation.parameters.size());
    const auto bytes =
        large ? std::max(std::size_t(128), cap / 16 / count) : std::size_t(128);
    field.text.resize(bytes);
    field.write(field.escaped ? value.dump() : text);
    if (field.kind == "bool")
      field.boolean = value.get<bool>();
    s.fields.push_back(std::move(field));
  };
  s.color_stack=operation.operation=="color_stack";
  s.creation=operation.operation=="selection_creation";
  if(operation.operation=="selection_creation") {
    const auto &choice=step["selection_creation"];
    const auto &candidate=choice["candidate"];
    add("operation","Selection mode","text",choice["operation"]);
    s.fields.back().choices={"replace","add","subtract","intersect","xor"};
    s.fields.back().choice_labels={"Replace","Add","Subtract","Intersect","XOR"};
    add("geometry","Geometry","text",choice["geometry"]);
    s.fields.back().choices={"automatic","canvas_v1","full_region_v3"};
    s.fields.back().choice_labels={"Automatic","Canvas","Preserve off-image geometry"};
    add("kind","Shape","text",candidate["kind"]);
    s.fields.back().choices={"rectangle","ellipse","rounded_rectangle","polygon","freehand"};
    s.fields.back().choice_labels={"Rectangle","Ellipse","Rounded rectangle","Polygon","Freehand"};
    add("points","Vertices (x, y pairs)","structured",candidate["points"]);
    add("width","Stroke width","real",candidate["width"]);
    auto attributes=candidate["attributes"];Json corner=0;
    if(candidate["kind"]=="rounded_rectangle") {
      corner=attributes.at("corner_diameter");attributes.erase("corner_diameter");
    }
    add("corner_diameter","Corner diameter (pixels)","int64",corner);
    add("attributes","Additional attributes","structured",std::move(attributes));
  } else if(operation.operation=="selection_enlargement") {
    const auto value=[&](const char *name){return step["parameters"].at(name).at("value");};
    add("distance","Signed distance (negative shrinks)","real",value("distance"));
    add("units","Distance units","text",value("units"));s.fields.back().choices={"pixels","calibrated"};
    s.fields.back().choice_labels={"Pixels","Input X calibration"};
  } else if(operation.operation=="rgb_contrast_apply") {
    const auto value=[&](const char *name){return step["parameters"].at(name).at("value");};
    add("minimum","Minimum","real",value("minimum"));
    add("maximum","Maximum","real",value("maximum"));
    add("channels","RGB component bits (Red 4 / Green 2 / Blue 1)","int64",value("channels"));
  } else if(operation.operation=="contrast_apply") {
    const auto value=[&](const char *name){return step["parameters"].at(name).at("value");};
    add("minimum","Minimum","real",value("minimum"));
    add("maximum","Maximum","real",value("maximum"));
    add("unsigned16_bits","Unsigned 16-bit output (0/8/10/12/14/15/16)","int64",value("unsigned16_bits"));
  } else if(s.color_stack) {
    auto value=[&](const char *name){return step["parameters"].at(name).at("value");};
    add("action","Conversion","text",value("action"));s.fields.back().choices={"split","merge","flatten_rgb"};
    s.fields.back().choice_labels={"Split into component channels","Merge component channels to RGB","Flatten RGB channels to Z"};
    add("space","Components","text",value("space"));s.fields.back().choices={"rgb","hsb8","hsb32"};
    s.fields.back().choice_labels={"RGB (8-bit)","HSB (8-bit)","HSB (32-bit)"};
    add("layout","Dimensions","text",value("layout"));s.fields.back().choices={"preserve_axes","imagej_legacy"};
    s.fields.back().choice_labels={"Preserve Z and time","ImageJ stack layout"};
    add("invert_rgb","Complement RGB components","bool",value("invert_rgb"));
    add("allow_calibration_reset","Allow reset of all spatial/time calibration","bool",value("allow_calibration_reset"));
    add("allow_nonxy_annotation_drop","Allow removal of XZ/YZ annotations","bool",value("allow_nonxy_annotation_drop"));
    add("max_working_bytes","Working memory limit (bytes)","uint64",value("max_working_bytes"));
  } else if(operation.operation=="indexed_dataset" || operation.operation=="indexed_rgb") {
    auto value=[&](const char *name){return step["parameters"].at(name).at("value");};
    if(operation.operation=="indexed_dataset")add("max_colors","Maximum colors (2..256)","int64",value("max_colors"));
    else {
      add("palette_source","Palette","text",value("palette_source"));s.fields.back().choices={"display","stored"};
      s.fields.back().choice_labels={"Current input display","Stored indexed palette (full byte range)"};
    }
    add("max_working_bytes","Working memory limit (bytes)","uint64",value("max_working_bytes"));
  } else if (operation.filter_options) {
    s.gaussian = std::holds_alternative<GaussianBlurCommandOptions>(
        *operation.filter_options);
    s.rank = !s.gaussian;
    const auto &options = step["filter_options"];
    if (s.gaussian) {
      add("sigma", "Sigma", "real", options["sigma"]);
      add("scaled_units", "Use calibrated units", "bool",
          options["scaled_units"]);
    } else {
      add("method", "Method", "text", options["method"]);
      s.fields.back().choices = {"minimum", "maximum", "median", "mean", "variance"};
      add("radius", "Radius (pixels)", "real", options["radius"]);
    }
  } else {
    const auto &spec = operation_spec(operation.operation);
    for (const auto &descriptor : spec.parameters) {
      const auto &p = step["parameters"].at(descriptor.name);
      add(descriptor.name, descriptor.label, p["kind"], p["value"],
          &descriptor);
    }
  }
}
NativeWorkflowParameters::~NativeWorkflowParameters() = default;
bool NativeWorkflowParameters::visible() const { return state_->open; }
void NativeWorkflowParameters::cancel() { state_->open = false;if(state_->planar_editor)state_->planar_editor->cancel("canceled"); }
void NativeWorkflowParameters::draw(
    bool enabled,
    const std::function<void(const NativeWorkflowOperation &)> &keep) {
  auto &s = *state_;
  if (!s.open)
    return;
  if(s.planar_editor) {
    s.planar_editor->draw(s.source,!enabled,[&](const ResolvedPlanarAnnotationCommand &command) {
      auto next=*s.annotation_step;next.annotation_intent=NativeWorkflowAnnotationIntent{s.source.data().dataset.shape(),command.request()};
      keep(next);s.open=false;
    });
    if(!s.planar_editor->visible())s.open=false;return;
  }
  if(s.saved_geometry){s.draw_saved(enabled,keep);return;}
  s.controls = Json::object();
  ImGui::SetNextWindowSize({700, 590}, ImGuiCond_FirstUseEver);
  if (s.focus) {
    ImGui::SetNextWindowFocus();
    s.focus = false;
  }
  ImGui::Begin("Edit workflow parameters", &s.open);
  const auto a = ImGui::GetCurrentWindow()->InnerRect.Min,
             b = ImGui::GetCurrentWindow()->InnerRect.Max;
  s.bounds = {a.x, a.y, b.x, b.y};
  ImGui::TextWrapped(
      "Edit this step on its private input. Keep replaces its parameters; "
      "the original capture and live image stay unchanged. Confirm this and "
      "later steps again.");
  if(s.color_stack)ImGui::TextWrapped("Preserving Z and time retains physical calibration. ImageJ layouts can turn time/channels into Z or consume Z as components; they require explicit calibration reset and, when needed, XZ/YZ annotation removal. Originals remain as provenance. Authored choices use the current stage view.");
  ImGui::BeginChild("Parameter fields",
                    {0, std::max(80.f, ImGui::GetContentRegionAvail().y - 95)},
                    false);
  ImGui::BeginDisabled(!enabled);
  for (auto &field : s.fields) {
    if(s.creation && field.name=="corner_diameter") {
      const auto kind=std::find_if(s.fields.begin(),s.fields.end(),[](const auto &v){return v.name=="kind";});
      if(kind==s.fields.end() || std::string(kind->text.data())!="rounded_rectangle")continue;
    }
    ImGui::PushID(field.name.c_str());
    if (field.optional) {
      bool automatic = field.kind == "null";
      if (ImGui::Checkbox("Use default / automatic", &automatic)) {
        field.kind = automatic ? "null" : field.default_kind;
        field.boolean = false;
        field.escaped = false;
        field.write(field.kind == "text" ? ""
                    : field.kind == "image"
                        ? "{\"width\":1,\"height\":1,\"channels\":1,\"type\":0,"
                          "\"values\":[0]}"
                        : "0");
      }
      s.observe(field.name + ".automatic");
    }
    if (field.numeric && field.kind != "null") {
      if (ImGui::BeginCombo("Value type", field.kind.c_str())) {
        for (const auto *kind : {"real", "int64", "uint64", "array"}) {
          if (std::string(kind) == "array" && !field.array_allowed)
            continue;
          if (ImGui::Selectable(kind, field.kind == kind) &&
              field.kind != kind) {
            field.kind = kind;
            field.write(field.kind == "array" ? "{\"shape\":[1],\"values\":[0]}"
                                              : "0");
          }
          s.observe(field.name + ".type." + kind);
        }
        ImGui::EndCombo();
      }
      s.observe(field.name + ".type");
    }
    ImGui::SetNextItemWidth(420);
    if (field.kind == "null")
      ImGui::TextDisabled("%s: automatic", field.label.c_str());
    else if (field.kind == "bool") {
      const bool unavailable = s.gaussian && field.name == "scaled_units" &&
                               !field.boolean &&
                               !gaussian_scaled_units_available(
                                   s.source.data().dataset.calibration());
      ImGui::BeginDisabled(unavailable);
      ImGui::Checkbox(field.label.c_str(), &field.boolean);
      s.observe(field.name);
      ImGui::EndDisabled();
      if (unavailable)
        ImGui::TextDisabled(
            "Calibrated units are unavailable for pixel-unit calibration");
    } else if (!field.choices.empty()) {
      const std::string current(field.text.data());
      std::string preview=current;
      for(std::size_t i=0;i<field.choice_labels.size();++i)if(field.choices[i]==current)preview=field.choice_labels[i];
      if (ImGui::BeginCombo(field.label.c_str(), preview.c_str())) {
        for (std::size_t i = 0; i < field.choices.size(); ++i) {
          const auto &choice = field.choices[i];
          const auto value =
              choice.is_string() ? choice.get<std::string>() : choice.dump();
          const auto label=i<field.choice_labels.size()?field.choice_labels[i]:value;
          if (ImGui::Selectable(label.c_str(), value == current))
            field.write(value);
          s.observe(field.name + ".choice." + std::to_string(i));
        }
        ImGui::EndCombo();
      }
    } else if (field.kind == "array" || field.kind == "image" ||
               field.kind == "text" || field.kind == "structured") {
      if (field.kind == "text" &&
          ImGui::Checkbox("Escaped JSON text", &field.escaped)) {
        try {
          if (field.escaped)
            field.write(Json(std::string(field.text.data())).dump());
          else {
            const auto value = detail::bounded_native_workflow_json(
                std::string("{\"value\":") + field.text.data() + "}",
                {1, s.cap});
            const auto text = value.at("value").get<std::string>();
            require(text.find('\0') == std::string::npos,
                    "Embedded NUL requires escaped text mode");
            field.write(text);
          }
        } catch (const std::exception &e) {
          field.escaped = !field.escaped;
          s.error = e.what();
        }
      }
      if (field.kind == "array")
        ImGui::TextUnformatted("Shaped numeric data: shape and flat values");
      if (field.kind == "image")
        ImGui::TextUnformatted(
            "Image data: width, height, channels, type and flat values");
      ImGui::InputTextMultiline(field.label.c_str(), field.text.data(),
                                field.text.size(), {420, 100});
    } else
      ImGui::InputText(field.label.c_str(), field.text.data(),
                       field.text.size());
    if (field.kind != "bool")
      s.observe(field.name);
    ImGui::PopID();
  }
  ImGui::EndDisabled();
  ImGui::EndChild();
  if (!s.error.empty())
    ImGui::TextWrapped("%s", s.error.c_str());
  ImGui::BeginDisabled(!enabled);
  if (ImGui::Button("Keep workflow parameters")) {
    try {
      const auto proposal = s.proposal();
      keep(proposal);
      s.open = false;
    } catch (const std::exception &e) {
      s.error = e.what();
    }
  }
  s.observe("keep_parameters");
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Discard parameter edits"))
    s.open = false;
  s.observe("discard");
  ImGui::End();
}
Json NativeWorkflowParameters::diagnostics() const {
  const auto &s = *state_;
  if(s.planar_editor){auto result=s.planar_editor->diagnostics();result["parameters"]=true;result["annotation"]=true;return result;}
  Json fields = Json::array();
  for (const auto &f : s.fields)
    fields.push_back(
        {{"name", f.name},
         {"kind", f.kind},
         {"bytes", std::strlen(f.text.data())},
         {"text",
          std::strlen(f.text.data()) <= 256 ? Json(f.text.data()) : Json()},
         {"boolean", f.boolean}});
  return {{"visible", s.open},      {"parameters", true}, {"fields", fields},
          {"controls", s.controls}, {"window", s.bounds}, {"error", s.error},
          {"gaussian", s.gaussian}, {"rank", s.rank}, {"color_stack",s.color_stack},{"annotation",bool(s.annotation_step)}};
}
} // namespace imagejxx::desktop
