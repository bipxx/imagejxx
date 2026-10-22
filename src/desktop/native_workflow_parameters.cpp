#include "native_workflow_parameters.hpp"
#include "../native/native_workflow_internal.hpp"
#include "../native/contrast_apply_workflow.hpp"
#include "../native/native_workflow_program_internal.hpp"
#include "../native/color_stack_internal.hpp"
#include "../native/indexed_dataset_internal.hpp"
#include "../native/indexed_rgb_internal.hpp"
#include <cstring>
#include <imagejxx/native_workflow_program.hpp>
#include <imgui_internal.h>
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
  bool open = true, focus = true, gaussian = false, rank = false, color_stack = false;
  std::string error;
  Json bounds = Json::array();
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
      if (gaussian || rank)
        step["filter_options"][field.name] = field.value(cap);
      else
        step["parameters"][field.name] = {{"kind", field.kind},
                                          {"value", field.value(cap)}};
      admit();
    }
    auto program = native_workflow_program_from_record(
        metadataObjectFromJson(record), {1, cap});
    (void)
        program.json(); // Escaped transport admission before retaining a draft.
    auto result = std::get<NativeWorkflowOperation>(program.steps()[0]);
    const auto shape = source.data().dataset.shape();
    if (!result.enabled)
      return result;
    if(result.operation=="contrast_apply") {
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
NativeWorkflowParameters::NativeWorkflowParameters(
    const DocumentSnapshot &source,
    std::shared_ptr<const NativeWorkflowOperation> original, std::size_t cap) {
  require(original && cap >= 16384,
          "Parameter editor requires a processing budget");
  // Borrowed admission precedes copying parameter arrays/images or originals.
  auto admitted = detail::make_native_workflow_program_borrowed(
      {detail::NativeWorkflowStepRef{original.get()}}, {1, cap / 8});
  auto operation = std::get<NativeWorkflowOperation>(admitted.steps()[0]);
  if (operation.operation!="contrast_apply" && !operation.filter_options && operation.original &&
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
  if(operation.operation=="contrast_apply") {
    operation.parameters=detail::contrast_apply_workflow_parameters(detail::contrast_apply_workflow_options(operation));
  } else if(operation.operation=="color_stack") {
    operation.parameters=detail::color_stack_workflow_parameters(
        detail::color_stack_workflow_request(operation.parameters,*operation.original));
  } else if(operation.operation=="indexed_dataset") {
    operation.parameters=detail::indexed_dataset_workflow_parameters(detail::indexed_dataset_workflow_request(operation.parameters,*operation.original));
  } else if(operation.operation=="indexed_rgb") {
    operation.parameters=detail::indexed_rgb_workflow_parameters(detail::indexed_rgb_workflow_request(operation.parameters,*operation.original));
  } else if (!operation.filter_options) {
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
                       field.kind == "text" || field.optional ||
                       field.array_allowed;
    const auto count =
        operation.filter_options
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
  if(operation.operation=="contrast_apply") {
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
void NativeWorkflowParameters::cancel() { state_->open = false; }
void NativeWorkflowParameters::draw(
    bool enabled,
    const std::function<void(const NativeWorkflowOperation &)> &keep) {
  auto &s = *state_;
  if (!s.open)
    return;
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
               field.kind == "text") {
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
          {"gaussian", s.gaussian}, {"rank", s.rank}, {"color_stack",s.color_stack}};
}
} // namespace imagejxx::desktop
