#include "path_segment_controls.hpp"
#include "geometry_number.hpp"
#include <imgui.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <cmath>

namespace imagejxx::desktop {
namespace {
std::size_t arity(int op) { return op == 4 ? 0 : op == 3 ? 6 : op == 2 ? 4 : 2; }
void require(bool condition, const char *message) {
  if (!condition) throw std::invalid_argument(message);
}
}
Selection editPathSegments(const Selection &selection, const Json &request,
                          const PathEditLimits &limits, const ExecutionControl &control) {
  require(selection.is_path(), "Segment editing requires a native path");
  require(request.is_object() && request.contains("operation") && request.at("operation").is_string() &&
      request.contains("index") && request.at("index").is_number_integer(), "Path edit requires an operation and command index");
  const auto &index = request.at("index");
  require(index.is_number_unsigned() || index.get<std::int64_t>() >= 0, "Path command index must be nonnegative");
  const auto ordinal = index.get<std::uint64_t>();
  require(ordinal <= selection.path_geometry().segment_count(), "Path command index outside geometry");
  const auto operation = request.at("operation").get<std::string>();
  const bool split = operation == "split", remove = operation == "remove";
  require(split || remove || operation == "replace" || operation == "insert", "Unknown path segment operation");
  const auto field = split ? "t" : "command";
  require(request.size() == (remove ? 2u : 3u) && (remove || request.contains(field)), "Unexpected path edit fields");
  const auto frozen = selection;
  auto geometry = frozen.path_geometry();
  if (split) {
    require(request.at("t").is_number(), "Path split parameter must be numeric");
    geometry = split_composite_path(geometry, std::size_t(ordinal), request.at("t").get<double>(), limits, control);
  } else {
    std::vector<double> command;
    if (!remove) {
      const auto &value = request.at("command");
      require(value.is_array() && value.size() <= 7, "Path edit requires one native command");
      for (const auto &v : value) {
        require(v.is_number(), "Path command operands must be numeric");
        command.push_back(v.get<double>());
      }
    }
    geometry = edit_composite_path(geometry, remove ? PathSegmentEdit::Remove : operation == "insert" ? PathSegmentEdit::InsertBefore : PathSegmentEdit::Replace,
                                   std::size_t(ordinal), command, limits, control);
  }
  auto result = Selection::from_path(geometry);
  result.width = frozen.width; result.attributes = frozen.attributes;
  return result;
}
Json PathSegmentControls::diagnostics() const {
  return {{"index",index_},{"opcode",opcode_},{"operands",numbers_},{"t",parameter_},{"error",error_}};
}
void PathSegmentControls::reset() { seen_.reset(); index_ = 0; parameter_ = "0.5"; error_.clear(); }
void PathSegmentControls::seed(const ImageJCompositePath &path) {
  seen_ = path;
  index_ = std::clamp(index_, 0, int(path.segment_count()));
  opcode_ = 1;
  for (auto &n : numbers_) n = "0";
  const auto &c = path.commands(); std::size_t ordinal = 0;
  for (std::size_t at = 0; at < c.size();) {
    const int op = int(c[at++]); const auto n = arity(op);
    if (ordinal++ == std::size_t(index_)) {
      opcode_ = op;
      for (std::size_t i = 0; i < n; ++i) numbers_[i] = geometry_number_text(c[at + i]);
      break;
    }
    at += n;
  }
}
void PathSegmentControls::draw(const Selection &selection, bool enabled,
    const std::function<void(const Json &)> &stage, const Observe &observe) {
  if (!selection.is_path()) { reset(); return; }
  const auto path = selection.path_geometry();
  if (!seen_ || seen_->commands().data() != path.commands().data()) seed(path);
  ImGui::TextUnformatted("Path segments");
  ImGui::TextWrapped("Stage a whole command, then Apply. Indices count moves and closes. Index %zu appends.", path.segment_count());
  ImGui::SetNextItemWidth(100);
  if (ImGui::InputInt("Command index", &index_, 0, 0)) seed(path);
  observe("Path command index", enabled, 100);
  ImGui::SetNextItemWidth(180);
  ImGui::Combo("Command", &opcode_, "Move (M)\0Line (L)\0Quadratic (Q)\0Cubic (C)\0Close (Z)\0");
  observe("Path command type", enabled, 180);
  const char *labels[] = {"X1", "Y1", "X2", "Y2", "X3", "Y3"};
  for (std::size_t i = 0; i < arity(opcode_); ++i) {
    if (i % 2) ImGui::SameLine();
    ImGui::SetNextItemWidth(100); ImGui::InputText(labels[i], &numbers_[i]);
    observe(std::string("Path ") + labels[i], enabled, 100);
  }
  ImGui::TextWrapped("Q: control, endpoint. C: two controls, endpoint. M/L: endpoint.");
  auto action = [&](const char *name, const char *operation, bool available) {
    ImGui::BeginDisabled(!available);
    if (ImGui::Button(name)) {
      try {
        Json request{{"operation", operation}, {"index", index_}};
        if (std::string(operation) == "split") request["t"] = parse_geometry_number(parameter_);
        else if (std::string(operation) != "remove") {
          Json command = Json::array({opcode_});
          for (std::size_t i = 0; i < arity(opcode_); ++i) command.push_back(parse_geometry_number(numbers_[i]));
          request["command"] = std::move(command);
        }
        stage(request); error_.clear();
      } catch (const std::exception &e) { error_ = e.what(); }
    }
    observe(name, enabled && available, 0); ImGui::EndDisabled();
  };
  action("Stage replace", "replace", std::size_t(index_) < path.segment_count()); ImGui::SameLine();
  action("Stage insert", "insert", true); ImGui::SameLine();
  action("Stage remove", "remove", std::size_t(index_) < path.segment_count());
  ImGui::SetNextItemWidth(100); ImGui::InputText("Split parameter", &parameter_); observe("Path split parameter", enabled, 100);
  action("Stage split", "split", std::size_t(index_) < path.segment_count());
  if (!error_.empty()) ImGui::TextWrapped("%s", error_.c_str());
}
} // namespace imagejxx::desktop
