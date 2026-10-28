#include "window.hpp"
#include <imagejxx/stack_projection.hpp>
#include <imagejxx/imagej_projection_command.hpp>
#include "path_visual.hpp"
#include <imagejxx/playback.hpp>
#include "shadow_demo.hpp"
#include "legacy_target_resources.hpp"
#include "command_metadata.hpp"
#include "planar_annotation_rebind.hpp"
#include "roi_import_panel.hpp"
#include "native_workflow_panel.hpp"
#include "rgb_dataset_panel.hpp"
#include "color_stack_panel.hpp"
#include "indexed_color_panel.hpp"
#ifdef IMAGEJXX_HAS_SCIENTIFIC_IO
#include "scientific_io_panel.hpp"
#include "../native/startup_admission_internal.hpp"
#include "../io/internal.hpp"
#endif
#include "../native/native_workflow_program_internal.hpp"
#include "roi_export_panel.hpp"
#include "movie_export_panel.hpp"
#include "movie_export_ui_probe.hpp"
#include "histogram_async_ui_probe.hpp"
#include "display_annotation_panel.hpp"
#include "display_annotation_renderer.hpp"
#include "roi_import_preview.hpp"
#include "planar_annotation_visual.hpp"
#include <imagejxx/planar_annotation_command.hpp>
#include <imagejxx/imagej_roi_import.hpp>
#include <imagejxx/imagej_annotation_import.hpp>
#include <imagejxx/imagej_roi_export.hpp>
#include <imagejxx/roi_manager.hpp>
#include "axis_plane_ui_probe.hpp"
#include "axis_range_ui_probe.hpp"
#include "axis_range_orientation_ui_probe.hpp"
#include "../native/kernel_control.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "headless_session_internal.hpp"
#include <imagejxx/selection_region.hpp>
#include "gaussian_jobs.hpp"
#include "gaussian_panel.hpp"
#include "rank_panel.hpp"
#include "rank_runtime_probe.hpp"
#include "settings_appearance_ui_probe.hpp"
#include "threshold_panel.hpp"
#include "histogram_panel.hpp"
#include "channel_display_panel.hpp"
#include "contrast_panel.hpp"
#include <imagejxx/rgb_contrast_apply_job.hpp>
#include <type_traits>
#include "channel_display_sources.hpp"
#include <imagejxx/manual_threshold.hpp>
#include <imagejxx/manual_threshold_command.hpp>
#include "gaussian_replay_policy.hpp"
#include "jobs_panel.hpp"
#include "orthogonal.hpp"
#include "render_memory.hpp"
#include "rendering.hpp"
#include "viewport_renderer.hpp"
#include "selection_panel.hpp"
#include "path_authoring_controls.hpp"
#include "saved_roi_edit.hpp"
#include "settings_io.hpp"
#include "settings_panel.hpp"
#include "startup_panel.hpp"
#include "startup_ui_probe.hpp"
#include <imagejxx/session.hpp>
#include "shortcut_input.hpp"
#include "../native/session_metadata.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <exception>
#include <imagejxx/selection_edit.hpp>
#include <imagejxx/selection_enlargement.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <numeric>
#include <set>
#include <sstream>
#include <thread>
#include <type_traits>
#include <utility>

namespace imagejxx::desktop {
namespace {
std::string rankProbeHex(const std::vector<unsigned char> &bytes) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(bytes.size() * 2);
  for (const auto value : bytes) {
    result.push_back(digits[value >> 4]);
    result.push_back(digits[value & 15]);
  }
  return result;
}
std::vector<unsigned char> rankProbeDeclaredBytes(const Image &image) {
  std::vector<unsigned char> bytes;
  const int width = image.type == PixelType::UInt8 ? 1 :
                    image.type == PixelType::UInt16 ? 2 : 4;
  if (image.type != PixelType::UInt8 && image.type != PixelType::UInt16 &&
      image.type != PixelType::Float32)
    throw std::invalid_argument("Rank runtime probe requires U8, U16 or F32");
  bytes.reserve(image.pixels.size() * width);
  for (const auto sample : image.pixels) {
    std::uint32_t word;
    if (image.type == PixelType::Float32) {
      const auto value = static_cast<float>(sample);
      std::memcpy(&word, &value, sizeof(word));
    } else word = static_cast<std::uint32_t>(sample);
    for (int i = 0; i < width; ++i)
      bytes.push_back(static_cast<unsigned char>(word >> (8 * i)));
  }
  return bytes;
}
const char *creationModeName(SelectionCreationOperation value) {
  switch (value) {
  case SelectionCreationOperation::Replace: return "replace";
  case SelectionCreationOperation::Add: return "add";
  case SelectionCreationOperation::Subtract: return "subtract";
  case SelectionCreationOperation::Intersect: return "intersect";
  case SelectionCreationOperation::Xor: return "xor";
  }
  throw std::invalid_argument("Unknown selection creation mode");
}
bool creationTool(const std::string &value) {
  return value == "rounded_rectangle" || value == "rectangle" || value == "ellipse" ||
         value == "polygon" || value == "freehand";
}
SelectionCreationGeometry creationGeometry(const std::optional<MetadataObject> &original) {
  if (original) {
    const auto found = original->find("version");
    if (found != original->end()) {
      if (const auto v = std::get_if<std::int64_t>(&found->second); v && *v == 4)
        return SelectionCreationGeometry::NativePathV4;
      if (const auto v = std::get_if<std::uint64_t>(&found->second); v && *v == 4)
        return SelectionCreationGeometry::NativePathV4;
      if (const auto v = std::get_if<std::int64_t>(&found->second); v && *v == 3)
        return SelectionCreationGeometry::FullRegionV3;
      if (const auto v = std::get_if<std::uint64_t>(&found->second); v && *v == 3)
        return SelectionCreationGeometry::FullRegionV3;
    }
  }
  return SelectionCreationGeometry::Automatic;
}
bool creationArea(const Selection &value) {
  return creationTool(value.kind) || value.kind == "rotated_rounded_rectangle" || value.is_mask() ||
         value.kind == "rotated_rectangle" || value.kind == "rotated_ellipse";
}
ChannelDisplayRequest channelDisplayRequest(const DocumentSnapshot &source,
                                           const Json &value) {
  if (!value.is_object() || value.size() != 3 || !value.contains("action") ||
      !value.contains("state") || !value.contains("channels"))
    throw std::invalid_argument("Channel display requires action, state and channels");
  ChannelDisplayRequest request;
  const auto &action = value.at("action"), &channels = value.at("channels");
  if (!action.is_string() || !channels.is_array())
    throw std::invalid_argument("Invalid channel display request fields");
  if (action == "set") request.action = ChannelDisplayAction::Set;
  else if (action == "reset_range") request.action = ChannelDisplayAction::ResetRange;
  else if (action == "disable") request.action = ChannelDisplayAction::Disable;
  else if (action == "select_max") request.action = ChannelDisplayAction::SelectMax;
  else if (action == "select_min") request.action = ChannelDisplayAction::SelectMin;
  else throw std::invalid_argument("Unknown channel display action");
  const auto &dataset = source.data().dataset;
  if (!value.at("state").is_null())
    request.state = parse_requested_channel_display(
        metadataObjectFromJson(value.at("state")), dataset.shape(),
        dataset.pixel_type(), dataset.sample_policy());
  for (const auto &c : channels) {
    if (!c.is_number_integer() || c < 0 || c > INT32_MAX)
      throw std::invalid_argument("Channel indices must be nonnegative integers");
    request.reset_channels.push_back(c.get<int>());
  }
  return request;
}
ChannelDisplayLut inheritedDisplayLut(const Json &properties) {
  ChannelDisplayLut result;
  const auto &custom = get(properties, "custom_lut");
  if (!custom.is_null()) {
    if (!custom.is_array() || custom.size() != 256)
      throw std::invalid_argument("LUT needs 256 RGB rows");
    result.name.clear(); result.table.emplace();
    for (std::size_t i = 0; i < 256; ++i) {
      const auto &row = custom[i];
      if (!row.is_array() || row.size() != 3)
        throw std::invalid_argument("LUT needs RGB rows");
      for (std::size_t k = 0; k < 3; ++k) {
        if (!row[k].is_number_integer() || row[k] < 0 || row[k] > 255)
          throw std::invalid_argument("LUT entries must be integer bytes");
        (*result.table)[i][k] = row[k].get<std::uint8_t>();
      }
    }
    return result;
  }
  result.name = string(get(properties, "lut"), "Grays");
  try { channel_display_lut(result.name); return result; }
  catch (const std::invalid_argument &) {
    if (result.name != "Inverted" && result.name != "Fire" &&
        result.name != "Ice" && result.name != "Spectrum")
      throw std::invalid_argument("Unsupported legacy LUT: " + result.name);
    // Convert the existing owned legacy palette, independently of the source
    // range. Unknown names still refuse through the legacy palette validator.
    Image ramp(256, 1, 1, PixelType::UInt8);
    for (int i = 0; i < 256; ++i) ramp.at(i, 0) = i;
    const auto mapped = displayImage(ramp, {{"lut", result.name},
        {"display_range", {0, 255}}, {"labels", false}}, FloatingSamplePolicy::FiniteOnly);
    result.name.clear(); result.table.emplace();
    for (int i = 0; i < 256; ++i)
      for (int k = 0; k < 3; ++k)
        (*result.table)[i][k] = static_cast<std::uint8_t>(mapped.at(i, 0, k));
    return result;
  }
}
enum class AxisExtractionKind { Plane, Range, OrientedRange };
AxisExtractionRequest axisExtractionDialogRequest(
    const DocumentSnapshot &source, bool range, std::size_t cap, const Json &draft) {
  const auto positive = [&](const char *key) {
    const auto &value = get(draft, key);
    if (!value.is_number_integer() || value < 1 || value > INT32_MAX)
      throw std::invalid_argument("Extraction indices and Step must be positive integers");
    return value.get<int>();
  };
  const auto plane = parse_plane(string(get(draft, "plane")));
  if (range) {
    AxisRangeResliceRequest value;
    value.plane = plane; value.first = positive("first") - 1;
    value.last = positive("last") - 1; value.step = positive("step");
    value.t = source.display().t; value.c = source.display().c;
    value.max_working_bytes = cap;
    return value;
  }
  AxisPlaneExportRequest value;
  value.plane = plane; value.coordinate = positive("index") - 1;
  value.t = source.display().t; value.c = source.display().c;
  value.max_working_bytes = cap;
  return value;
}
AxisRangeOrientationRequest axisOrientationDialogRequest(
    const DocumentSnapshot &source, std::size_t cap, const Json &draft) {
  AxisRangeOrientationRequest request;
  request.range = std::get<AxisRangeResliceRequest>(
      axisExtractionDialogRequest(source, true, cap, draft));
  for (const char *flag : {"reverse_order", "flip_vertical", "transpose"})
    if (!get(draft, flag).is_boolean())
      throw std::invalid_argument("Orientation flags must be booleans");
  request.reverse_order = draft.at("reverse_order").get<bool>();
  request.flip_vertical = draft.at("flip_vertical").get<bool>();
  request.transpose = draft.at("transpose").get<bool>();
  return request;
}
AxisExtractionRequest axisExtractionRebindRequest(const Json &value) {
  if (!value.is_object())
    throw std::invalid_argument("Extraction rebind requires a complete request object");
  const auto &operation = get(value, "operation");
  if (!operation.is_string() ||
      (operation != "axis_plane_export" && operation != "axis_range_reslice"))
    throw std::invalid_argument("Unknown extraction rebind operation");
  const bool range = operation == "axis_range_reslice";
  std::set<std::string> fields{"operation", "plane", "t", "c", "max_working_bytes"};
  if (range) fields.insert({"first", "last", "step"});
  else fields.insert("coordinate");
  if (value.size() != fields.size())
    throw std::invalid_argument("Extraction rebind requires every request field");
  for (const auto &entry : value.items())
    if (!fields.count(entry.key()))
      throw std::invalid_argument("Unknown extraction rebind field: " + entry.key());
  const auto index = [&](const char *key, bool positive = false) {
    const auto &v = get(value, key);
    if (!v.is_number_integer() || v < (positive ? 1 : 0) || v > INT32_MAX)
      throw std::invalid_argument(std::string("Invalid extraction rebind integer: ") + key);
    return v.get<int>();
  };
  const auto &plane = get(value, "plane"), &cap = get(value, "max_working_bytes");
  if (!plane.is_string() || (plane != "XY" && plane != "XZ" && plane != "YZ"))
    throw std::invalid_argument("Extraction rebind plane must be XY, XZ or YZ");
  if (!cap.is_number_integer() || cap < 1 ||
      cap > std::numeric_limits<std::size_t>::max())
    throw std::invalid_argument("Extraction rebind cap must be a positive size_t");
  if (range) {
    AxisRangeResliceRequest request;
    request.plane = parse_plane(plane.get<std::string>());
    request.first = index("first"); request.last = index("last");
    request.step = index("step", true); request.t = index("t"); request.c = index("c");
    request.max_working_bytes = cap.get<std::size_t>();
    return request;
  }
  AxisPlaneExportRequest request;
  request.plane = parse_plane(plane.get<std::string>());
  request.coordinate = index("coordinate"); request.t = index("t"); request.c = index("c");
  request.max_working_bytes = cap.get<std::size_t>();
  return request;
}
AxisRangeOrientationRequest axisOrientationRebindRequest(const Json &value) {
  const std::set<std::string> fields{"operation", "plane", "first", "last", "step",
      "t", "c", "max_working_bytes", "reverse_order", "flip_vertical", "transpose"};
  if (!value.is_object() || value.size() != fields.size() ||
      get(value, "operation") != "axis_range_orientation")
    throw std::invalid_argument("Orientation rebind requires a complete oriented request");
  for (const auto &entry : value.items())
    if (!fields.count(entry.key()))
      throw std::invalid_argument("Unknown orientation rebind field: " + entry.key());
  for (const char *flag : {"reverse_order", "flip_vertical", "transpose"})
    if (!value.at(flag).is_boolean())
      throw std::invalid_argument("Orientation flags must be booleans");
  auto range = value;
  range["operation"] = "axis_range_reslice";
  range.erase("reverse_order"); range.erase("flip_vertical"); range.erase("transpose");
  AxisRangeOrientationRequest request;
  request.range = std::get<AxisRangeResliceRequest>(axisExtractionRebindRequest(range));
  request.reverse_order = value.at("reverse_order").get<bool>();
  request.flip_vertical = value.at("flip_vertical").get<bool>();
  request.transpose = value.at("transpose").get<bool>();
  return request;
}
int physicalGridRenderLimit(bool gl, const void *owner) {
  if (!gl) return 4096; // Frontend headless policy; not a native sampling limit.
  const auto context = glfwGetCurrentContext();
  if (!context || glfwGetWindowUserPointer(context) != owner)
    throw std::runtime_error("Rendering is not ready; wait for the image window.");
  GLint limit = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
  if (limit <= 0)
    throw std::runtime_error("The image window cannot determine its rendering limit.");
  return limit;
}
void validatePhysicalGridRendering(const AxisRangeSpacingEstimate &estimate, int limit) {
  if (estimate.width <= 0 || estimate.height <= 0 || estimate.width > limit || estimate.height > limit)
    throw std::invalid_argument("Output exceeds the image window's rendering limit (" +
                               std::to_string(limit) + " pixels per dimension). Use a smaller source or larger physical spacing.");
  const auto width = std::size_t(estimate.width), height = std::size_t(estimate.height);
  if (width > std::numeric_limits<std::size_t>::max() / height ||
      width * height > std::size_t(INT32_MAX) / 3 ||
      width * height > std::vector<unsigned char>().max_size() / 3)
    throw std::invalid_argument("Output exceeds the image window's display buffer limit.");
}
AxisRangeSampling physicalGridSampling(const std::string &value) {
  if (value == "Nearest" || value == "nearest") return AxisRangeSampling::Nearest;
  if (value == "Linear" || value == "linear") return AxisRangeSampling::Linear;
  throw std::invalid_argument("Sampling must be Nearest or Linear.");
}
AxisRangeSpacingRequest physicalGridRequest(const DocumentSnapshot &source, const Json &values, bool oneBased) {
  const auto plane = parse_plane(string(get(values, "plane")));
  if (plane != Plane::XZ && plane != Plane::YZ) throw std::invalid_argument("Physical spacing views must be XZ or YZ.");
  const auto &first = get(values, "first"), &last = get(values, "last"), &step = get(values, "step");
  for (const auto *value : {&first, &last, &step})
    if (!value->is_number_integer() || *value < (oneBased ? 1 : 0) || *value > INT32_MAX)
      throw std::invalid_argument("First, Last and Step must be valid integer indices.");
  for (const char *flag : {"reverse_order", "flip_vertical", "transpose"})
    if (values.contains(flag) && !values.at(flag).is_boolean())
      throw std::invalid_argument("Orientation flags must be booleans.");
  const auto &spacing = get(values, "physical_vertical_spacing");
  if (!spacing.is_number() || !std::isfinite(spacing.get<double>()) || spacing.get<double>() <= 0)
    throw std::invalid_argument("Physical vertical spacing must be finite and positive.");
  if (!get(values, "sampling").is_string()) throw std::invalid_argument("Sampling must be Nearest or Linear.");
  AxisRangeSpacingRequest request;
  request.orientation.range = {plane, first.get<int>() - (oneBased ? 1 : 0), last.get<int>() - (oneBased ? 1 : 0),
                               step.get<int>(), source.display().t, source.display().c};
  request.orientation.reverse_order = values.value("reverse_order", false);
  request.orientation.flip_vertical = values.value("flip_vertical", false);
  request.orientation.transpose = values.value("transpose", false);
  request.physical_vertical_spacing = spacing.get<double>();
  request.sampling = physicalGridSampling(values.at("sampling").get<std::string>());
  return request;
}
std::mutex guiMutex;
const char *slicerResizeBranchName(AxisRangeSlicerResizeBranch branch) {
  switch (branch) {
  case AxisRangeSlicerResizeBranch::NoResize: return "no_resize";
  case AxisRangeSlicerResizeBranch::EqualDimensionCrop: return "equal_dimension_crop";
  case AxisRangeSlicerResizeBranch::Bilinear: return "bilinear_resize";
  }
  throw std::invalid_argument("Invalid Slicer resize branch.");
}
void validateSlicerResizeRendering(const AxisRangeSlicerResizeEstimate &estimate, int limit) {
  if (estimate.width <= 0 || estimate.height <= 0 || estimate.width > limit || estimate.height > limit)
    throw std::invalid_argument("Output exceeds the image window's rendering limit (" +
        std::to_string(limit) + " pixels per dimension). Use a smaller source or change its calibration.");
  const auto width = std::size_t(estimate.width), height = std::size_t(estimate.height);
  if (width > std::numeric_limits<std::size_t>::max() / height ||
      width * height > std::size_t(INT32_MAX) / 3 ||
      width * height > std::vector<unsigned char>().max_size() / 3)
    throw std::invalid_argument("Output exceeds the image window's display buffer limit.");
}
AxisRangeSlicerResizeRequest slicerResizeRequest(const DocumentSnapshot &source, const Json &values, bool oneBased) {
  if (!get(values, "plane").is_string())
    throw std::invalid_argument("Slicer resize plane must be XZ or YZ.");
  const auto plane = parse_plane(values.at("plane").get<std::string>());
  if (plane != Plane::XZ && plane != Plane::YZ)
    throw std::invalid_argument("Slicer resize plane must be XZ or YZ.");
  const auto &first = get(values, "first"), &last = get(values, "last"), &step = get(values, "step");
  for (const auto *value : {&first, &last, &step})
    if (!value->is_number_integer() || *value < (oneBased ? 1 : 0) || *value > INT32_MAX)
      throw std::invalid_argument("First, Last and Step must be valid integer indices.");
  for (const char *flag : {"reverse_order", "flip_vertical", "transpose"})
    if (values.contains(flag) && !values.at(flag).is_boolean())
      throw std::invalid_argument("Orientation flags must be booleans.");
  AxisRangeSlicerResizeRequest request;
  request.orientation.range = {plane, first.get<int>() - (oneBased ? 1 : 0), last.get<int>() - (oneBased ? 1 : 0),
                               step.get<int>(), source.display().t, source.display().c};
  request.orientation.reverse_order = values.value("reverse_order", false);
  request.orientation.flip_vertical = values.value("flip_vertical", false);
  request.orientation.transpose = values.value("transpose", false);
  return request;
}
std::atomic<std::uint64_t> headlessSessionSequence{0};
const std::unordered_map<std::string, ImGuiKey> &headlessKeys() {
  static const auto keys = [] {
    std::unordered_map<std::string, ImGuiKey> result{
      {"Shift", ImGuiMod_Shift},
      {"Ctrl", ImGuiMod_Ctrl},
      {"Alt", ImGuiMod_Alt},
      {"Super", ImGuiMod_Super}};
    for (unsigned value = 0; value <= static_cast<unsigned>(ShortcutKey::Slash); ++value) {
      const auto key = static_cast<ShortcutKey>(value);
      result.emplace(format_shortcut_chord({key, ShortcutModifiers::None}),
                     shortcutImGuiKey(key));
    }
    return result;
  }();
  return keys;
}
const std::unordered_map<std::string, int> &headlessButtons() {
  static const std::unordered_map<std::string, int> buttons{
      {"left", 0}, {"right", 1}, {"middle", 2}};
  return buttons;
}

struct InputJobBarrier {
  std::mutex mutex;
  std::condition_variable changed;
  bool entered = false, released = false;
  void hold(const JobContext &context) {
    std::unique_lock<std::mutex> lock(mutex);
    entered = true;
    changed.notify_all();
    changed.wait(lock, [&] { return released; });
    lock.unlock();
    context.checkpoint();
  }
  void release() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      released = true;
    }
    changed.notify_all();
  }
};
const char *jobStateName(JobState state) {
  switch (state) {
  case JobState::Queued: return "queued";
  case JobState::Running: return "running";
  case JobState::Ready: return "ready";
  case JobState::Publishing: return "publishing";
  case JobState::Completed: return "completed";
  case JobState::Failed: return "failed";
  case JobState::Canceled: return "canceled";
  }
  return "unknown";
}
// Only headless replay diagnostics allocate this scalar/record cell. Worker
// callbacks never access Window, ImGui or result ownership. Its mutex protects
// all creator/worker transfers, including job-ID binding after submit returns.
struct WorkflowReplayLineage {
  mutable std::mutex mutex;
  std::atomic<bool> observationFailed{false};
  std::uint64_t liveDestinationId, jobId = 0, publicationId = 0;
  std::size_t eventLimit = 1;
  Json originalSteps, explicitRequest, events = Json::array();
  Json terminalJob = nullptr;

  WorkflowReplayLineage(std::uint64_t source, Json steps, Json request)
      : liveDestinationId(source), originalSteps(std::move(steps)),
        explicitRequest(std::move(request)) {
    if (originalSteps.size() > (std::numeric_limits<std::size_t>::max() - 1) / 2)
      throw std::length_error("Replay observation bound overflows");
    eventLimit += 2 * originalSteps.size();
  }
  void bindJob(const Job &job) noexcept {
    try {
      const auto id = job.info().id;
      std::lock_guard<std::mutex> lock(mutex);
      jobId = id;
    } catch (...) {
      observationFailed.store(true, std::memory_order_relaxed);
    }
  }
  bool associatedWith(const Job &job) noexcept {
    try {
      const auto id = job.info().id;
      std::lock_guard<std::mutex> lock(mutex);
      return jobId != 0 && jobId == id;
    } catch (...) {
      observationFailed.store(true, std::memory_order_relaxed);
      return false;
    }
  }
  void observe(const GaussianReplayObservation &value) noexcept {
    try {
      Json row{{"phase", value.phase}, {"step_index", value.step_index},
          {"live_destination_id", value.live_destination_id},
          {"captured_snapshot_owner_id", value.captured_snapshot_owner_id},
          {"working_copy_id", value.working_copy_id}};
      if (value.original_record) {
        row["original_record"] = *value.original_record;
        row["original_record_source_id"] = value.original_record->at("target").at("document_id");
        row["original_record_target"] = value.original_record->at("target");
      }
      if (value.resolved_command)
        row["resolved_command_record"] = metadataJson(value.resolved_command->record());
      if (value.resolved_rank_command)
        row["resolved_command_record"] = metadataJson(value.resolved_rank_command->record());
      if (value.execution_parameters) {
        // This is the same map supplied to Application::prepare, borrowed only
        // during this callback. Preserve actual NumericArray shape and YX data.
        const auto &parameters = *value.execution_parameters;
        if (value.resolved_rank_command) {
          if(value.resolved_rank_command->remove_nans_options()) {
            row["resolved_command_parameters"]={{"radius",std::get<double>(parameters.at("radius"))}};
            row["resolved_command_parameter_shapes"]=Json::object();
          } else if(value.resolved_rank_command->top_hat_options()) {
            row["resolved_command_parameters"]={{"radius",std::get<double>(parameters.at("radius"))},
                {"light_background",std::get<bool>(parameters.at("light_background"))},
                {"dont_subtract",std::get<bool>(parameters.at("dont_subtract"))},
                {"inverted_lut",std::get<bool>(parameters.at("inverted_lut"))}};
            row["resolved_command_parameter_shapes"]=Json::object();
          } else if (value.resolved_rank_command->outlier_options()) {
            row["resolved_command_parameters"] = {{"radius", std::get<double>(parameters.at("radius"))},
                {"threshold", std::get<double>(parameters.at("threshold"))},
                {"which", std::get<std::string>(parameters.at("which"))},
                {"inverted_lut", std::get<bool>(parameters.at("inverted_lut"))}};
            row["resolved_command_parameter_shapes"] = Json::object();
          } else {
          if (parameters.size() != 2)
            throw std::logic_error("Unexpected rank replay parameter fields");
          row["resolved_command_parameters"] = {
              {"method", std::get<std::string>(parameters.at("method"))},
              {"radius", std::get<double>(parameters.at("radius"))}};
          row["resolved_command_parameter_shapes"] = Json::object();
          }
        } else {
          if (parameters.size() != 1)
            throw std::logic_error("Unexpected Gaussian replay parameter fields");
          const auto &sigma = std::get<NumericArray>(parameters.at("sigma"));
          row["resolved_command_parameters"] = {{"sigma", sigma.values}};
          row["resolved_command_parameter_shapes"] = {{"sigma", sigma.shape}};
        }
      }
      if (value.scratch_publication_document_id)
        row["scratch_publication_document_id"] = value.scratch_publication_document_id;
      std::lock_guard<std::mutex> lock(mutex);
      if (events.size() >= eventLimit) {
        observationFailed.store(true, std::memory_order_relaxed);
        return;
      }
      row["sequence"] = events.size() + 1;
      events.push_back(std::move(row));
    } catch (...) {
      observationFailed.store(true, std::memory_order_relaxed);
    }
  }
  void published(std::uint64_t id) noexcept {
    try {
      std::lock_guard<std::mutex> lock(mutex);
      publicationId = id; // Set only after the real live commit returns.
    } catch (...) {
      observationFailed.store(true, std::memory_order_relaxed);
    }
  }
  void terminal(const Job &job) noexcept {
    try {
      const auto info = job.info();
      Json value{{"id", info.id}, {"document_id", info.document_id},
          {"label", info.label}, {"state", jobStateName(info.state)},
          {"error", info.error}, {"cancel_requested", info.cancel_requested},
          {"resource_bytes", info.resource_bytes}};
      std::lock_guard<std::mutex> lock(mutex);
      terminalJob = std::move(value);
    } catch (...) {
      observationFailed.store(true, std::memory_order_relaxed);
    }
  }
  Json diagnostics() const {
    std::lock_guard<std::mutex> lock(mutex);
    return {{"schema_version", 1}, {"job_id", jobId ? Json(jobId) : Json(nullptr)},
        {"live_destination_id", liveDestinationId},
        {"original_steps", originalSteps}, {"explicit_request", explicitRequest},
        {"events", events}, {"terminal_job", terminalJob},
        {"final_publication_document_id", publicationId ? Json(publicationId) : Json(nullptr)},
        {"publication_succeeded", publicationId != 0},
        {"observation_failed", observationFailed.load(std::memory_order_relaxed)},
        {"ownership", "scalar_and_record_values_only_no_result_aliases"}};
  }
};
struct OutputPublication {
  Json rows, step;
  Json::array_t *resultArray = nullptr, *workflowArray = nullptr;

  static Json::array_t *reserve(Json &target, std::size_t count,
                                const char *name) {
    if (!target.is_array())
      throw std::invalid_argument(std::string(name) + " must be an array");
    auto &array = target.get_ref<Json::array_t &>();
    if (count > array.max_size() - array.size())
      throw std::length_error(std::string(name) + " exceeds addressable size");
    array.reserve(array.size() + count);
    return &array;
  }
  OutputPublication(Json &results, Json &workflow,
                    const ComponentCounts &counts, const std::string &title,
                    Json recordedStep)
      : step(std::move(recordedStep)) {
    if (!counts.empty()) {
      rows = Json::array();
      rows.get_ref<Json::array_t &>().reserve(counts.size());
      for (const auto &[position, count] : counts)
        rows.push_back({{"Label", title + " (components)"},
                        {"Slice", position.z + 1},
                        {"Count", count}});
      resultArray = reserve(results, rows.size(), "Results");
    }
    if (!step.is_null())
      workflowArray = reserve(workflow, 1, "Workflow");
  }
  void publish() noexcept {
    // Storage and values are prepared before the pixel commit; these moves do
    // not allocate. No UI callback may mutate the arrays between these phases.
    static_assert(std::is_nothrow_move_constructible_v<Json>);
    if (resultArray)
      for (auto &row : rows.get_ref<Json::array_t &>())
        resultArray->push_back(std::move(row));
    if (workflowArray)
      workflowArray->push_back(std::move(step));
  }
};
bool samePath(const std::filesystem::path &a, const std::filesystem::path &b) {
  if (a == b)
    return true;
  std::error_code error;
  return std::filesystem::equivalent(a, b, error) && !error;
}
Json selection(const std::string &kind, const Json &points, double width = 1) {
  return {{"kind", kind}, {"points", points}, {"width", width}};
}
Json imageArray(const Image &image) {
  Json rows = Json::array();
  for (int y = 0; y < image.height; ++y) {
    Json row = Json::array();
    for (int x = 0; x < image.width; ++x) {
      if (image.channels == 1)
        row.push_back(image.at(x, y));
      else {
        Json p = Json::array();
        for (int c = 0; c < image.channels; ++c)
          p.push_back(image.at(x, y, c));
        row.push_back(p);
      }
    }
    rows.push_back(row);
  }
  return rows;
}
Json imageReadback(const Image &image, FloatingSamplePolicy sample_policy) {
  if (sample_policy == FloatingSamplePolicy::PreserveQuietNaNFloat32 ||
      sample_policy == FloatingSamplePolicy::PreserveIEEEFloat32) {
    if (image.type != PixelType::Float32 || image.channels != 1)
      throw std::invalid_argument("Quiet-NaN readback requires grayscale Float32");
    Json bytes = Json::array();
    auto &output = bytes.get_ref<Json::array_t &>();
    if (image.pixels.size() > output.max_size() / 4)
      throw std::length_error("Float32 readback byte array is too large");
    output.reserve(image.pixels.size() * 4);
    for (double sample : image.pixels) {
      const auto bits = float32_source_sample_bits(sample, sample_policy);
      for (unsigned int byte = 0; byte < 4; ++byte)
        output.emplace_back(static_cast<unsigned int>((bits >> (8 * byte)) & 0xffu));
    }
    return {{"shape", {image.height, image.width, image.channels}},
            {"dtype", typeName(image.type)},
            {"encoding", "IEEE754 Float32LE"}, {"bytes", std::move(bytes)}};
  }
  return {{"shape", {image.height, image.width, image.channels}},
          {"dtype", typeName(image.type)}, {"pixels", image.pixels}};
}
TargetRequest gaussianRebindRequest(const Json &value) {
  if (!value.is_object())
    throw std::invalid_argument("Gaussian rebind requires an explicit target object");
  const std::set<std::string> allowed{"scope", "t", "c", "z", "positions", "roi", "mode", "destination"};
  for (const auto &field : value.items())
    if (!allowed.count(field.key()))
      throw std::invalid_argument("Unknown Gaussian target field: " + field.key());
  auto index = [](const Json &item, bool positive = false) {
    if (!item.is_number_integer() || item < 0 ||
        item.get<std::uint64_t>() > std::uint64_t(std::numeric_limits<int>::max()) ||
        (positive && item == 0))
      throw std::invalid_argument("Gaussian target indices require supported nonnegative integers");
    return item.get<int>();
  };
  auto text = [&](const char *key, const char *fallback) {
    if (!value.contains(key)) return std::string(fallback);
    if (!value[key].is_string())
      throw std::invalid_argument("Gaussian target choices require strings");
    return value[key].get<std::string>();
  };
  TargetRequest request;
  const auto scope = text("scope", "current_plane");
  if (scope == "current_plane") request.scope = TargetScope::CurrentPlane;
  else if (scope == "all_z") request.scope = TargetScope::AllZ;
  else if (scope == "ranges") request.scope = TargetScope::Ranges;
  else if (scope == "positions") request.scope = TargetScope::Positions;
  else throw std::invalid_argument("Unknown Gaussian target scope");
  const auto roi = text("roi", "active_selection");
  if (roi == "active_selection") request.roi = RoiPolicy::ActiveSelection;
  else if (roi == "whole_plane") request.roi = RoiPolicy::WholePlane;
  else if (roi == "positioned_rois") request.roi = RoiPolicy::PositionedRois;
  else throw std::invalid_argument("Unknown Gaussian ROI policy");
  const auto mode = text("mode", "planes");
  if (mode == "planes") request.mode = ExecutionMode::Planes;
  else if (mode == "volume") request.mode = ExecutionMode::Volume;
  else throw std::invalid_argument("Unknown Gaussian execution mode");
  const auto destination = text("destination", "in_place");
  if (destination == "in_place") request.destination = OutputDestination::InPlace;
  else if (destination == "new_document") request.destination = OutputDestination::NewDocument;
  else throw std::invalid_argument("Unknown Gaussian output destination");
  auto range = [&](const char *key) -> std::optional<AxisRange> {
    if (!value.contains(key)) return std::nullopt;
    const auto &item = value[key];
    if (!item.is_object()) throw std::invalid_argument("Gaussian axis range must be an object");
    for (const auto &field : item.items())
      if (field.key() != "start" && field.key() != "stop" && field.key() != "step")
        throw std::invalid_argument("Unknown Gaussian axis range field");
    AxisRange result;
    if (item.contains("start")) result.start = index(item["start"]);
    if (item.contains("stop") && !item["stop"].is_null()) result.stop = index(item["stop"]);
    if (item.contains("step")) result.step = index(item["step"], true);
    return result;
  };
  request.t = range("t"); request.c = range("c"); request.z = range("z");
  if (value.contains("positions")) {
    if (!value["positions"].is_array())
      throw std::invalid_argument("Gaussian positions must be an array");
    for (const auto &position : value["positions"]) {
      if (!position.is_array() || position.size() != 3)
        throw std::invalid_argument("Gaussian positions require T/C/Z triples");
      request.positions.push_back({index(position[0]), index(position[1]), index(position[2])});
    }
  }
  return request;
}
std::string configPath() {
  if (const char *dir = std::getenv("IMAGEJXX_CONFIG_DIR"); dir && *dir)
    return (std::filesystem::u8path(dir) / "preferences.json").u8string();
#ifdef _WIN32
  const char *dir = std::getenv("LOCALAPPDATA");
#else
  const char *dir = std::getenv("XDG_CONFIG_HOME");
#endif
  if (dir && *dir)
    return (std::filesystem::u8path(dir) / "imagejxx" / "preferences.json")
        .u8string();
#ifndef _WIN32
  if (const char *homeDirectory = std::getenv("HOME"))
    return (std::filesystem::u8path(homeDirectory) / ".config" / "imagejxx" /
            "preferences.json")
        .u8string();
#endif
  return "imagejxx-preferences.json";
}
std::string colorText(const float *v) {
  char s[10];
  std::snprintf(s, sizeof(s), "#%02x%02x%02x",
                int(std::clamp(v[0], 0.f, 1.f) * 255 + .5),
                int(std::clamp(v[1], 0.f, 1.f) * 255 + .5),
                int(std::clamp(v[2], 0.f, 1.f) * 255 + .5));
  return s;
}
std::vector<std::string> resultColumns(const Json &rows,
                                       const Settings &settings) {
  auto columns = settings.strings("measurements");
  if (!columns.empty())
    return columns;
  for (const auto &row : rows)
    for (auto it = row.begin(); it != row.end(); ++it)
      if (std::find(columns.begin(), columns.end(), it.key()) == columns.end())
        columns.push_back(it.key());
  return columns;
}
std::string resultText(const Json &value, const Settings &settings) {
  if (value.is_null())
    return "";
  if (value.is_string())
    return string(value);
  auto precision = settings.integer("measurement_precision");
  if (value.is_number_float() && precision >= 0) {
    std::ostringstream text;
    text << std::fixed << std::setprecision(static_cast<int>(precision))
         << value.get<double>();
    return text.str();
  }
  return value.dump();
}
} // namespace
struct Window::Impl {
  Window &owner;
  std::shared_ptr<RenderMemory> renderMemory=std::make_shared<RenderMemory>();
  std::unique_ptr<ViewportJobs> viewportJobs;
  ViewportJobs &viewJobs(){if(!viewportJobs)viewportJobs=std::make_unique<ViewportJobs>(renderMemory);return *viewportJobs;}
  std::shared_ptr<VirtualViewCache> viewCache=std::make_shared<VirtualViewCache>();
  struct LaunchAppendItem {
    std::string path;
    std::shared_ptr<Document> document;
  };
  std::deque<LaunchAppendItem> pendingLaunch;
  bool launchStarted=false, launchReview=false, launchPublishing=false;
  std::size_t launchTotal=0, launchCompleted=0;
  std::string launchStatus="idle", launchError;
  StartupRequest startupRequest;
  StartupPlan startupPlan;
  std::vector<StartupDiagnostic> startupDiagnostics;
  std::string startupStatus = "fresh";
  bool startupNoticesDismissed = false;
  bool startupAttempted = false, startupCompleted = false;
  bool startupAutomatic = false, startupUserOpened = false;
  bool startupFramesDrawn = false, suppressSavedLayout = false;
  ImGuiID startupRetryId = 0, startupChooseWorkspaceId = 0;
  ImGuiID startupStartEmptyId = 0, startupDisableRestoreId = 0;
  ImGuiID workspaceDockId = 0;
  bool savedLayoutApplied = false;
  std::size_t loadedLayoutBytes = 0, layoutResetGeneration = 0;
  Json startupControls = Json::object();
  std::string launchDirectory = std::filesystem::current_path().u8string();
  bool startupRetryAllowed(bool drawingBanner = false) const {
    if (!startupAttempted || startupStatus != "failed" || !startupAutomatic ||
        startupUserOpened || !owner.documents_.empty() || owner.busy() ||
        dialog.open || browser.open || pendingClose || closingApp ||
        settingsPanel.visible() || selectionPanel.visible() ||
        gaussianPanel.visible() || rankPanel.visible() || planarAnnotationRebind.visible() || (movieExportPanel.visible() || roiImportPanel.visible() || roiExportPanel.visible() || displayAnnotationPanel.visible()) || thresholdPanel.visible() || histogramPanel.visible() ||
        (channelDisplayPanel.inputActive() || contrastPanel.inputActive()) || savedRoiEdit.active())
      return false;
    if (const auto *gui = ImGui::GetCurrentContext()) {
      const bool ownedButton = gui->ActiveId == startupRetryId ||
          (drawingBanner && (gui->ActiveId == startupChooseWorkspaceId ||
                             gui->ActiveId == startupStartEmptyId ||
                             gui->ActiveId == startupDisableRestoreId));
      if (gui->ActiveId && !ownedButton) return false;
      if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId |
                                    ImGuiPopupFlags_AnyPopupLevel)) return false;
    }
    for (const auto &pane : panes)
      if (pane.dragging || pane.selectionGesture || pane.creation || !pane.points.empty())
        return false;
    return true;
  }
  RankRuntimeProbe *rankProbe = nullptr;
  SettingsAppearanceUiProbe *appearanceProbe = nullptr;
  std::uint64_t appearanceFrame = 0;
  double appearanceActualDpi = 1, appearanceSuppliedDpi = 1;
  std::array<int, 2> appearanceFramebuffer{}, appearanceWindow{};
  std::uint64_t rankFrame = 0, rankUploadSerial = 0;
  Json rankConsumed = {{"valid", false}};
  std::map<std::uint64_t, std::pair<std::uint64_t, std::string>> rankSelectionHashes;
  GaussianJobs gaussianJobs;
  ShadowDemo shadowDemo;
  bool shadowDemoVisible = false;
  GaussianPanel gaussianPanel;
  RankPanel rankPanel;
  ThresholdPanel thresholdPanel;
  HistogramPanel histogramPanel;
  struct HistogramTask {
    HistogramJob job;
    ResolvedHistogramCommand command;
    std::uint64_t generation;
    bool recording,remember;
    std::string error;
  };
  std::shared_ptr<HistogramTask> histogramTask;
  std::shared_ptr<const JobInfo> histogramLast;
  std::atomic<std::uint64_t> histogramCancelId{0},histogramCancelEpoch{0};
  ChannelDisplayPanel channelDisplayPanel;
  ContrastPanel contrastPanel;
  std::optional<MetadataObject> channelDisplayRebind;
  std::optional<std::uint64_t> channelDisplayRebindOwner;
  std::optional<ContrastApplyJob> contrastApplyJob;
  std::optional<DocumentSnapshot> contrastApplySource;
  bool contrastApplyRecording=false;
  std::atomic<std::uint64_t> contrastApplyCancelId{0},contrastApplyCancelEpoch{0};
  std::optional<AutoContrastJob> autoJob;
  std::uint64_t autoOwner = 0;
  std::atomic<std::uint64_t> autoCancelId{0}, autoCancelEpoch{0};
  bool autoRecording = false;
  std::optional<DocumentSnapshot> thresholdSource;
  std::optional<ManualThresholdState> thresholdOpeningState;
  std::optional<OperationJob> thresholdJob;
  std::atomic<std::uint64_t> thresholdCancelId{0};
  std::atomic<std::uint64_t> thresholdCancelEpoch{0};
  Json thresholdStep;
  const std::shared_ptr<JobScheduler> gaussianCancellationScheduler;
  struct Pane {
    std::shared_ptr<Document> doc;
    GLuint texture = 0;
    RenderMemory::Lease textureMemory,readbackMemory,rankMemory;
    uint64_t revision = ~uint64_t(0);
    bool previewActive = false, textureDirty = true;
    bool viewportPending=false,viewportSeen=false;
    RenderMemory::Lease sampleMemory;
    std::shared_ptr<const Image> sampledSurface;
    std::uint64_t previewGeneration = 0;
    std::uintptr_t pixelsIdentity = 0;
    std::size_t pixelBuilds = 0, diagnosticRebuilds = 0;
    std::optional<Image> sourceReadback, renderedReadback;
    Json rankTexture = {{"valid", false}};
    std::vector<unsigned char> rankScalarBytes, rankRgbBytes;
    PlanePosition pixelsPosition{}, rangePosition{};
    std::array<double, 2> effectiveRange{0, 1};
    Json channelDisplayKey = nullptr, channelDisplayReadback = nullptr;
    Json previewDisplayKey = Json::object();
    FloatingSamplePolicy sourceReadbackPolicy = FloatingSamplePolicy::FiniteOnly;
    uint64_t outlineRevision = ~uint64_t(0);
    int outlineT = -1, outlineC = -1;
    std::map<const Json *, std::vector<Rect>> maskOutlines;
    std::map<const Json *, RoiVisual> roiVisuals;
    std::size_t roiVisualCap=0;
    Json transientStyle;
    RoiColor transientFallback{};
    std::array<int, 2> transientShape{};
    std::optional<RoiVisual> transientVisual;
    std::optional<std::vector<Rect>> transientOutline;
    Json styledLayers = Json::array();
    PlanarAnnotationPanel nativePlanar;
    DisplayAnnotationRenderer displayRenderer;
    DisplayAnnotationRenderer importDisplayRenderer;
    Json displayLayers=Json::array();
    Json ordinaryImportLayers=Json::array();
    bool ordinaryImportActive=false,ordinaryImportReplace=false;
    std::string ordinaryImportError;
    Json savedRoiLayers = Json::array(), savedRoiHandles = Json::array();
    Json selectionHandles = Json::array();
    std::string selectionEditError,selectionEditNotice,savedRoiHandleError,savedRoiHandleNotice;
    std::size_t selectionEditorCap=0;
    Json savedRoiInput = Json::object();
    Json overlayLayers = Json::array();
    std::vector<OverlayVisual> overlays;
    std::uint64_t overlayRevision = ~std::uint64_t(0);
    int overlaySlice = -1;
    int overlayT = -1, overlayC = -1;
    std::array<int, 2> overlayShape{};
    bool layeredOverlays = false;
    std::size_t overlayBuilds = 0;
    int slice = -1;
    float zoom = 1;
    ImVec2 pan{0, 0}, origin{0, 0}, extent{0, 0};
    Json points = Json::array();
    bool dragging = false;
    float aspectY = 1;
    OrthogonalViews orthogonal;
    bool orthogonalWasVisible = false, requestOrdinaryFocus = false;
    bool ordinaryCanvasVisible = false;
    Json ordinaryFrameSource = Json::object();
    ImGuiID toggleId = 0, ordinaryCanvasId = 0;
    ImVec2 togglePoint{-1, -1}, ordinaryCanvasPoint{-1, -1};
    std::string keyboardAdmission = "hidden";
    std::unique_ptr<SelectionEditor> selectionEditor;
    std::optional<DocumentSnapshot> selectionSource;
    std::uint64_t selectionRevision = ~std::uint64_t(0);
    std::array<int, 2> selectionShape{};
    std::optional<std::size_t> selectionVertex;
    struct SelectionGesture {
      DocumentSnapshot source;
      SelectionHandle handle;
      std::array<double, 2> down;
      std::optional<Selection> preview;
      bool invalid = false;
      ImVec2 origin{}, extent{};
      float zoom=0, aspect=0;
      PlanePosition position{};
    };
    std::optional<SelectionGesture> selectionGesture;
    struct CreationGesture {
      explicit CreationGesture(DocumentSnapshot value) : source(std::move(value)) {}
      DocumentSnapshot source;
      PlanePosition position{};
      std::string tool;
      SelectionCreationOperation operation = SelectionCreationOperation::Replace;
      SelectionCreationGeometry geometry = SelectionCreationGeometry::Automatic;
      std::array<double, 2> down{};
      std::vector<std::array<double, 2>> points;
      std::optional<SelectionCreationBoxState> box;
      std::optional<Selection> candidate, preview;
      std::optional<MetadataObject> rebind;
      Json previewGeometry = Json::object();
      Json candidateGeometry = Json::object();
      ImVec2 origin{}, extent{};
      float zoom = 1, aspect = 1;
      ImGuiID canvasId = 0;
      double width = 1;
      int cornerDiameter = 8;
      bool dragging = false;
      std::size_t retained = 0;
    };
    std::optional<CreationGesture> creation;
  };
  std::deque<Pane> panes;
  Json documentTabs = Json::array();
  Json hostControls = Json::object(), workflowControls = Json::object();
  Json resultsTableObservation = Json::object();
  std::vector<std::string> resultsColumnNames;
  std::size_t resultsColumnPage = 0;
  // An immutable staged snapshot has a non-throwing pointer swap after commit.
  // DocumentSnapshot's value swap is not noexcept on every supported STL.
  std::shared_ptr<const DocumentSnapshot> roiManagerSource;
  std::uint64_t scientificRoiCreationOwner = 0;
  // A changed saved-row publication stages focus before committing. The next
  // normal manager refresh consumes this exact owner/Metadata revision without
  // clearing the already allocated selection set.
  std::optional<std::pair<std::uint64_t, std::uint64_t>> roiManagerPendingFocus;
  struct SavedRoiHistoryFocus {
    std::uint64_t document_id;
    std::size_t row;
    std::optional<std::uint64_t> before, after;
    std::uint64_t publication_metadata_revision;
  };
  std::optional<SavedRoiHistoryFocus> savedRoiHistoryFocus;
  std::set<std::size_t> roiSelectedIndices;
  std::optional<std::size_t> roiRangeAnchor;
  Json roiManagerControls = Json::object(), roiManagerRows = Json::array();
  bool roiManagerVisible = false, roiManagerFocused = false;
  Json errorModal = Json::object(), closePrompt = Json::object();
  std::shared_ptr<WorkflowReplayLineage> workflowReplayLineage;
  bool workflowReplayObservationFailed = false;
  std::uint64_t dialogGeneration = 0;
  size_t maskOutlineBuilds = 0;
  std::map<std::filesystem::path, size_t> sessionSizes;
  Json sessionExtensions = Json::object();
  Json commands = catalog(), preferences = Json::object();
  PreferencesState preferenceState;
  ShortcutProfile shortcutProfile;
  std::map<std::string, std::string> shortcutLabels;
  Json shortcutProfileState = Json::object();
  bool shortcutsRearmed = false;
  std::uint64_t shortcutOwner = 0;
  ImGuiID shortcutContextOwner = 0;
  ImGuiWindow *shortcutWorkspace = nullptr;
  ImGuiWindow *shortcutRoiManager = nullptr;
  Json shortcutObservation = Json::object();
  SettingsPanel settingsPanel;
  ImVec2 editMenuPoint{-1, -1}, preferencesMenuPoint{-1, -1};
  JobsPanel jobsPanel;
  SelectionPanel selectionPanel;
  PathAuthoringControls pathAuthoring;
  bool pathTransform = false, pathAspect = false, pathCenter = false;
  PlanarAnnotationRebind planarAnnotationRebind;
  RoiImportPanel roiImportPanel;
  NativeWorkflowPanel nativeWorkflowPanel;
  RgbDatasetPanel rgbDatasetPanel;
  ColorStackPanel colorStackPanel;
  IndexedColorPanel indexedColorPanel;
#ifdef IMAGEJXX_HAS_SCIENTIFIC_IO
  ScientificIoPanel scientificIoPanel;
  std::vector<std::string> pendingFileOpens;
  struct StartupTiffEntry {
    std::string path;
    std::shared_ptr<Document> document;
    std::function<void()> revalidate;
  };
  struct StartupTiffBatch {
    std::vector<StartupTiffEntry> entries;
    std::size_t next = 0, cap = 0;
    bool finalizing = false;
  };
  std::unique_ptr<StartupTiffBatch> startupTiffs;
  bool scientificIoPublishing = false;
#endif
  RoiExportPanel roiExportPanel;
  MovieExportPanel movieExportPanel;
  DisplayAnnotationPanel displayAnnotationPanel;
  SavedRoiEdit savedRoiEdit;
  std::string savedRoiCancelReason;
  std::optional<std::size_t> savedRoiVertex;
  bool savedRoiPanelShown = false;
  struct SavedRoiRebind {
    MetadataObject record;
    std::uint64_t owner;
    Revisions revisions;
    PlanePosition position;
    std::optional<std::size_t> row;
    bool choosing = true, focus = true;
  };
  std::optional<SavedRoiRebind> savedRoiRebind;
  Json savedRoiRebindControls = Json::object();
  float guiScale = 0;
  std::string guiTheme;
  std::string preferencesPath, lastError,
      status = "Open an image or create a document";
  std::string tool = "rectangle";
  SelectionCreationOperation creationMode = SelectionCreationOperation::Replace;
  bool creationFullGeometry = false;
  std::optional<MetadataObject> creationRebind;
  std::uint64_t creationRebindOwner = 0;
  std::string creationAdmission = "hidden", creationLastCancel;
  float foreground[3]{1, 1, 1}, background[3]{0, 0, 0};
  float brush = 8;
  bool allSlices = false, showRois = true, animating = false;
  double fps = 8, lastTick = 0;
  std::string animationAxis = "z", animationReason = "stopped";
  std::optional<DocumentSnapshot> animationSource;
  std::uint64_t animationTicks = 0;
  int animationFirst = 0, animationStep = 1;
  std::optional<int> animationLast;
  PlaybackMode animationMode = PlaybackMode::Forward;
  bool animationReverse = false;
  std::optional<PlaybackSequence> animationSequence;
  std::string animationTiming = "fps";
  double animationCalibratedInterval = 1;
  int historyLimit = 8, roiIndex = -1, lastVisible = -1;
  int numericZoomPlane = 0; // Transient Tools choice, never document/session data.
  std::uint64_t navigationDocument = 0;
  int navigationChannel = 1, navigationTime = 1;
  bool navigationChannelEdited = false, navigationTimeEdited = false;
  bool exit = false, gl = false, layout = false;
  std::optional<Image> clipboard, clipboardMask;
  FloatingSamplePolicy clipboardSamplePolicy = FloatingSamplePolicy::FiniteOnly;
  struct JobResult {
    State state;
    ComponentCounts components;
    std::optional<PreparedOperation> prepared;
    MetadataObject record;
    bool recorded = false;
    std::shared_ptr<const void> reservation;
    JobResult(PreparedOperation value, MetadataObject capture, bool recordStep)
        : prepared(std::move(value)), record(std::move(capture)), recorded(recordStep) {}
    JobResult(State value, ComponentCounts counts = {})
        : state(std::move(value)), components(std::move(counts)) {}
  };
  Job job;
  // Used only by the explicit headless diagnostic; copied into one real task.
  std::function<void(const JobContext &)> diagnosticWorkerGate;
  std::mutex jobMutex;
  // Only scalar IDs cross the off-thread cancellation boundary. GaussianJobs,
  // panels and result ownership remain confined to the creator thread.
  std::array<std::uint64_t, 2> gaussianCancelIds{};
  std::uint64_t gaussianCancelEpoch = 0, gaussianDrainedEpoch = 0;
  std::uint64_t gaussianAdmissionEpoch = 0; // Last actual creator-thread admission.
  // Creator-only diagnostic callbacks are absent during ordinary UI use.
  std::shared_ptr<const GaussianHostProbe> gaussianHostProbe;
  AxisPlaneUiPublicationProbe axisPlaneUiPublicationProbe;
  AxisRangeUiPublicationProbe axisRangeUiPublicationProbe;
  AxisRangeOrientationUiPublicationProbe axisRangeOrientationUiPublicationProbe;
  std::uint64_t gaussianApplyAttempt = 0, gaussianReportedApplyAttempt = 0;
  bool headlessDiagnostics = false, gaussianReadbackRequested = false;
  bool headlessSessionReadback = false;
  bool headlessSessionActive = false;
  std::shared_ptr<std::optional<JobResult>> jobResult;
  std::shared_ptr<Document> jobDocument;
  Revisions jobRevisions;
  int jobSlice = 0;
  bool jobNew = false;
  std::string jobTitle;
  Json jobStep;
  bool jobPreview = false, jobSkip = false;
  std::atomic<bool> cancelled{false};
  std::vector<float> plotX, plotY;
  std::string plotTitle;
  struct Dialog {
    std::string title, id;
    std::uint64_t generation = 0, openedDocumentId = 0;
    Json schema = Json::array(), values = Json::object();
    std::map<std::string, std::string> text;
    Json controls = Json::object();
    std::function<void(Json)> submit;
    bool open = false, operation = false;
    std::optional<DocumentSnapshot> enlargementSource;
    std::optional<DocumentSnapshot> axisPlaneSource;
    std::string axisIndexLabel;
    std::optional<DocumentSnapshot> axisRangeSource;
    Json extractionRebindRecord;
    bool scientificExtraction = false;
    bool orientationRangeEdited = false;
    std::size_t extractionCap = AxisPlaneExportRequest{}.max_working_bytes;
    std::optional<DocumentSnapshot> axisRangeOrientationSource;
    std::optional<DocumentSnapshot> axisRangeSpacingSource;
    std::optional<DocumentSnapshot> axisRangeSlicerResizeSource;
    Json axisRangeSlicerResizePreview = Json::object();
    Json axisRangeSpacingPreview = Json::object();
    Json axisRangeOrientationPreview = Json::object();
    std::string axisRangeLabel;
    std::optional<int> axisRangeCount;
    std::optional<Image> preview;
    GLuint texture = 0;
    ImVec2 choicePoint{-1, 0}, alternateChoicePoint{-1, 0}, applyPoint{-1, 0},
        previewPoint{-1, 0};
  };
  Dialog dialog;
  struct Browser {
    std::string title, path, filter;
    std::function<void(std::string)> submit;
    bool open = false, save = false, overwrite = false;
    std::string prompt;
    std::vector<std::string> extensions;
    std::function<bool()> current;
    bool expired=false;
    bool directory=false;
    Json entries=Json::array();
  };
  Browser browser;
  std::shared_ptr<Document> pendingClose;
  bool closingApp = false;
  std::mutex dropMutex;
  std::vector<std::string> dropped;
  void resetInput(Pane &pane, bool newRenderer = false) {
    // Opening the first renderer does not invalidate a metadata-only numeric
    // draft entered through the public action API. A pointer capture always
    // belongs to the old canvas; owner changes and renderer teardown cancel all.
    if (((savedRoiEdit.active() && savedRoiEdit.source().id() == pane.doc->id) ||
         (savedRoiRebind && savedRoiRebind->owner == pane.doc->id)) &&
        (!newRenderer || savedRoiEdit.pointer_active()))
      cancelSavedRoiGeometry("owner_changed");
    cancelCreation(pane, "owner_changed");
    pane.points = Json::array();
    pane.dragging = false;
    pane.selectionGesture.reset();
    if (creationRebindOwner == pane.doc->id) {
      creationRebind.reset(); creationRebindOwner = 0;
    }
    if (auto context = ImGui::GetCurrentContext()) {
      bool owned = context->NavId != 0 &&
                   context->NavId == pane.ordinaryCanvasId;
      for (const auto &row : pane.orthogonal.diagnostics())
        owned |= context->NavId != 0 &&
                 (context->NavId == get(row, "canvas_id").get<ImGuiID>() ||
                  context->NavId == get(row, "slice_id").get<ImGuiID>());
      if (owned)
        ImGui::FocusWindow(nullptr);
    }
    pane.orthogonal.resetInput();
    pane.orthogonalWasVisible = pane.requestOrdinaryFocus = false;
    pane.ordinaryCanvasVisible = false;
    pane.toggleId = pane.ordinaryCanvasId = 0;
    pane.togglePoint = pane.ordinaryCanvasPoint = {-1, -1};
    pane.keyboardAdmission = "hidden";
  }
  void resetInputs(bool newRenderer = false) {
    for (auto &pane : panes)
      resetInput(pane, newRenderer);
  }
  void resetRendererInputs() noexcept {
    cancelSavedRoiGeometry("renderer_reset");
    for (auto &pane : panes) {
      if (pane.creation) {
        pane.transientOutline.reset(); pane.transientVisual.reset();
        pane.transientStyle.clear();
      }
      pane.creation.reset();
      pane.selectionGesture.reset();
      pane.orthogonal.resetInput();
      pane.orthogonalWasVisible = pane.requestOrdinaryFocus = false;
      pane.ordinaryCanvasVisible = false;
      pane.toggleId = pane.ordinaryCanvasId = 0;
      pane.togglePoint = pane.ordinaryCanvasPoint = {-1, -1};
      pane.keyboardAdmission.clear();
    }
  }
  Json navigation(const Pane &pane) const {
    const auto context = ImGui::GetCurrentContext();
    const auto navId = context ? context->NavId : 0;
    const bool mode = pane.doc->view().orthogonal;
    const int focused = mode ? pane.orthogonal.focusedPlane() : -1;
    bool admitted = false;
    for (const auto &row : pane.orthogonal.diagnostics())
      admitted |= boolean(get(row, "focused")) &&
                  boolean(get(row, "keyboard_admitted"));
    auto reason = pane.keyboardAdmission;
    if (!pane.ordinaryCanvasVisible && !pane.orthogonalWasVisible)
      reason = "hidden";
    if (mode && reason == "admitted" && focused < 0)
      reason = "no_canvas_focus";
    return {{"toggle_point", {pane.togglePoint.x, pane.togglePoint.y}},
            {"toggle_id", pane.toggleId},
            {"toggle_focused", pane.toggleId != 0 && navId == pane.toggleId},
            {"ordinary_canvas_point", {pane.ordinaryCanvasPoint.x, pane.ordinaryCanvasPoint.y}},
            {"ordinary_canvas_id", pane.ordinaryCanvasId},
            {"ordinary_canvas_focused", pane.ordinaryCanvasVisible &&
                                         pane.ordinaryCanvasId != 0 && navId == pane.ordinaryCanvasId},
            {"focused_plane", focused}, {"keyboard_admitted", mode && admitted},
            {"admission_reason", reason}, {"mode", mode}};
  }
  explicit Impl(Window &w, std::string path)
      : owner(w), gaussianJobs(*w.controller_), shadowDemo(*w.controller_),
        gaussianCancellationScheduler(w.controller_->job_scheduler()),
        roiImportPanel(w.controller_->job_scheduler()),
        nativeWorkflowPanel(w.controller_->job_scheduler()),
        rgbDatasetPanel(w.controller_->job_scheduler(),inheritedDisplayLut),
        colorStackPanel(w.controller_->job_scheduler()),
        indexedColorPanel(w.controller_->job_scheduler()),
#ifdef IMAGEJXX_HAS_SCIENTIFIC_IO
        scientificIoPanel(w.controller_->job_scheduler()),
#endif
        roiExportPanel(w.controller_->job_scheduler()),
        movieExportPanel(w.controller_->job_scheduler()),
        preferencesPath(std::filesystem::absolute(std::filesystem::u8path(
            path.empty() ? configPath() : path)).lexically_normal().u8string()) {
    preferenceState = PreferencesState::load(preferencesPath);
    const auto *configDirectory = std::getenv("IMAGEJXX_CONFIG_DIR");
    if (path.empty() && (!configDirectory || !*configDirectory) &&
        !std::filesystem::exists(std::filesystem::u8path(preferencesPath))) {
#ifdef _WIN32
      const char *homeDirectory = std::getenv("USERPROFILE");
#else
      const char *homeDirectory = std::getenv("HOME");
#endif
      if (homeDirectory) {
        auto legacy = (std::filesystem::u8path(homeDirectory) / ".imagejxx" /
                       "preferences.json")
                          .u8string();
        if (std::filesystem::exists(std::filesystem::u8path(legacy)))
          preferenceState = PreferencesState::load(legacy);
      }
    }
    preferences = preferenceState.extras;
    consumeSettings();
    for (const auto &warning : preferenceState.warnings) {
      if (!lastError.empty())
        lastError += "\n";
      lastError += warning;
    }
  }
  static std::size_t settingBytes(const Settings &settings, const char *name) {
    constexpr std::size_t unit = 1024 * 1024;
    const auto value = settings.integer(name);
    if (value < 0 || static_cast<std::uint64_t>(value) >
                         std::numeric_limits<std::size_t>::max() / unit)
      throw std::invalid_argument("Configured memory budget exceeds native size_t");
    return static_cast<std::size_t>(value) * unit;
  }
  static HistoryPolicy historyPolicy(const Settings &settings) {
    return {static_cast<std::size_t>(settings.integer("history_limit")),
            settingBytes(settings, "history_memory_mb")};
  }
  static void swapPreferences(PreferencesState &a, PreferencesState &b) noexcept {
    static_assert(std::is_nothrow_swappable_v<Settings>);
    using std::swap;
    swap(a.settings, b.settings);
    a.extras.swap(b.extras);
    a.unknownValues.swap(b.unknownValues);
    a.unknownFields.swap(b.unknownFields);
    a.warnings.swap(b.warnings);
    swap(a.writeBlocked, b.writeBlocked);
  }
  void applyPreferences(PreferencesState proposed, bool persist) {
    // All allocation, parsing and frontend state preparation precede the save.
    const auto &settings = proposed.settings;
    auto profile = compile_shortcuts(settings.strings("shortcut_bindings"));
    std::map<std::string, std::string> labels;
    Json profileState = {{"registry_version", shortcut_registry_version},
                         {"overrides", profile.override_lines()},
                         {"inactive_rows", profile.inactive_rows()},
                         {"bindings", Json::array()}};
    const auto platform = desktopShortcutPlatform();
    for (const auto &command : shortcut_commands()) {
      const auto chord = profile.chord(command.id, platform);
      const auto hint = chord ? format_shortcut_chord(*chord) : std::string{};
      labels.emplace(command.id, hint);
      profileState["bindings"].push_back(
          {{"command_id", command.id}, {"label", command.label},
           {"chord", chord ? Json(hint) : Json()}});
    }
    auto extras = proposed.extras;
    const auto policy = historyPolicy(settings);
    const JobLimits limits{
        static_cast<std::size_t>(settings.integer("processing_workers")),
        settingBytes(settings, "processing_memory_mb"), 64};
    const auto nextCacheBytes=settingBytes(settings,"viewport_cache_mb");
    const auto nextRenderBytes=settingBytes(settings,"render_memory_mb");
    renderMemory->validateLimit(nextRenderBytes);
    const auto nextFps = settings.number("animation_fps");
    const auto nextBrush = static_cast<float>(settings.number("brush_width"));
    const auto nextShowRois = settings.boolean("show_rois");
    const auto fg = color(settings.text("foreground"));
    const auto bg = color(settings.text("background"));
    std::vector<std::uint64_t> ids;
    ids.reserve(owner.documents_.size());
    for (const auto &document : owner.documents_)
      ids.push_back(document->id);
    const auto scheduler = owner.controller_->job_scheduler();
    // This holder outlives set_limits' acceptance callback and scheduler lock.
    // Its old history sources can run custom destructors only after every lock
    // is released and the new Window state is installed.
    PreparedHistoryPolicies histories;
    scheduler->set_limits(limits, [&] {
      histories = owner.controller_->prepare_history_policies(ids, policy);
      if (persist)
        proposed.save(preferencesPath, true);
      histories.commit();
      histories.unlock();
    });
    if (persist) {
      proposed.writeBlocked = false;
      proposed.warnings.clear();
    }
    swapPreferences(preferenceState, proposed);
    preferences.swap(extras);
    shortcutProfile.swap(profile);
    shortcutLabels.swap(labels);
    shortcutProfileState.swap(profileState);
    historyLimit = static_cast<int>(policy.step_limit);
    fps = nextFps;
    brush = nextBrush;
    showRois = nextShowRois;
    for (int c = 0; c < 3; ++c) {
      foreground[c] = fg[c] / 255.f;
      background[c] = bg[c] / 255.f;
    }
    renderMemory->setLimit(nextRenderBytes);
    viewCache->setBudget(nextCacheBytes);
    shortcutsRearmed = false;
  }
  void consumeSettings() { applyPreferences(preferenceState, false); }
  void initializeLayout() {
    savedLayoutApplied = false;
    loadedLayoutBytes = 0;
    const auto &stored = get(preferences, "layout");
    if (!suppressSavedLayout && stored.is_string()) {
      const auto text = stored.get<std::string>();
      validate_preferences_layout(text);
      ImGui::LoadIniSettingsFromMemory(text.data(), text.size());
      savedLayoutApplied = true;
      loadedLayoutBytes = text.size();
    }
    layout = false;
    workspaceDockId = 0;
  }
  DocumentAdoption prepareAdoption(const std::shared_ptr<Document> &document,
                                   bool applyDefaultZoom) const {
    DocumentAdoption result{document->nativeOwner(), std::nullopt};
    const auto zoom = preferenceState.settings.number("default_zoom");
    if (applyDefaultZoom && zoom > 0) {
      auto source = document->nativeSnapshot();
      if (source.revisions().display == 0) {
        const auto shape = source.data().dataset.shape();
        auto display = detail::session_metadata::parse_view(
            {{"zoom", zoom / document->displaySpacing()[0]}}, source.display(),
            shape.x, shape.y, shape.z, source.display().crosshair[2]);
        display.validate(shape);
        result.initial_display.emplace(
            DocumentInitialDisplay{std::move(source), std::move(display)});
      }
    }
    return result;
  }
  std::string paintColor(bool paint = true) const {
    auto text = colorText(paint ? foreground : background);
    const auto &original =
        preferenceState.settings.text(paint ? "foreground" : "background");
    if (original.size() == 9)
      text += original.substr(7);
    return text;
  }
  void appearance(double dpi, bool systemFonts);
  std::shared_ptr<Document> doc() const { return owner.document(); }
  bool gaussianSourceAllowed(const Document &source) const {
    // Opening a draft does not admit work. Submission checks busy separately.
    if (owner.documents_.empty() || doc()->id != source.id)
      return false;
    const auto dataset = source.nativeSnapshot().data().dataset;
    const auto type = dataset.pixel_type();
    const auto samples = dataset.shape().s;
    return (type == PixelType::UInt8 && (samples == 1 || samples == 3)) ||
           ((type == PixelType::UInt16 || type == PixelType::Float32) &&
            samples == 1);
  }
  bool removeNaNsSourceAllowed(const Document &source) const {
    if (owner.documents_.empty() || doc()->id != source.id) return false;
    const auto &dataset=source.nativeSnapshot().data().dataset;
    return dataset.shape().s==1 && dataset.pixel_type()==PixelType::Float32 &&
        (dataset.sample_policy()==FloatingSamplePolicy::FiniteOnly ||
         dataset.sample_policy()==FloatingSamplePolicy::PreserveQuietNaNFloat32 ||
         dataset.sample_policy()==FloatingSamplePolicy::PreserveIEEEFloat32);
  }
  bool rankSourceAllowed(const Document &source) const {
    if (owner.documents_.empty() || doc()->id != source.id)
      return false;
    const auto dataset = source.nativeSnapshot().data().dataset;
    const auto type = dataset.pixel_type();
    return dataset.shape().s == 1 &&
           dataset.sample_policy() == FloatingSamplePolicy::FiniteOnly &&
           (type == PixelType::UInt8 || type == PixelType::UInt16 ||
            type == PixelType::Float32);
  }
  bool axisExtractionSourceAllowed(const Document &source) const {
    if (!source.scientific())
      return true;
    const auto samples = source.nativeSnapshot().data().dataset.shape().s;
    return (samples == 1 || samples == 3 || samples == 4) &&
        !owner.documents_.empty() && doc()->id == source.id;
  }
  template <typename Request>
  void requireAxisExtractionSource(const DocumentSnapshot &source,
                                  const Request &request,
                                  bool requireCapturedPosition) const {
    if (owner.busy())
      throw std::invalid_argument("Wait for the current edit");
    const auto current = owner.controller_->snapshot(source.id());
    const auto facade = owner.documentById(source.id());
    if (!source.same_document(current))
      throw std::invalid_argument("Axis extraction belongs to another document");
    if (!current.revisions().matches(source.revisions(),
                                     Change::Pixels | Change::Metadata))
      throw RevisionConflict();
    if (!axisExtractionSourceAllowed(*facade))
      throw std::invalid_argument("Scientific extraction requires the active document");
    if (facade->scientific() && requireCapturedPosition) {
      const auto check = [&](const auto &value) {
        const auto &position = [&]() -> const auto & {
          if constexpr (std::is_same_v<std::decay_t<decltype(value)>,
                                       AxisRangeOrientationRequest>)
            return value.range;
          else if constexpr ((std::is_same_v<std::decay_t<decltype(value)>, AxisRangeSpacingRequest> || std::is_same_v<std::decay_t<decltype(value)>, AxisRangeSlicerResizeRequest>))
            return value.orientation.range;
          else return value;
        }();
        if (position.t != source.display().t || position.c != source.display().c)
          throw std::invalid_argument("Extraction must use the captured channel and time");
      };
      if constexpr (std::is_same_v<Request, AxisExtractionRequest> ||
                    std::is_same_v<Request, AxisCommandRequest>)
        std::visit(check, request);
      else check(request);
    }
  }
  void requireCreationSource(const DocumentSnapshot &source) const {
    const auto facade = owner.documentById(source.id());
    const auto current = facade->nativeSnapshot();
    if (!source.same_document(current))
      throw std::invalid_argument("Selection creation belongs to another document");
    if (!current.revisions().matches(source.revisions(), Change::Data))
      throw RevisionConflict();
    if (facade->scientific() &&
        (scientificRoiCreationOwner != source.id() ||
         owner.controller_->active_document_id() != source.id() ||
         source.display().t != current.display().t ||
         source.display().c != current.display().c ||
         source.display().crosshair[2] != current.display().crosshair[2]))
      throw std::invalid_argument("Scientific selection requires its captured active T/C/Z position");
  }
  bool actionAllowed(const std::string &action, std::uint64_t id = 0,
                     const std::string &operationId = {}) const {
    static const std::set<std::string> global{
        "new", "open_sequence", "raw", "show_preferences", "preferences",
        "import_preferences", "export_preferences", "reset_preferences",
        "show_jobs", "wait_jobs", "close_document", "save_workspace",
        "save_workflow", "load_workflow", "cancel_native_workflow"};
    if (global.count(action))
      return true;
    if (action == "merge_channels" || action == "images_to_stack")
      for (const auto &source : owner.documents_)
        if (source->scientific())
          return false;
    if (action == "calculate" && owner.documents_.size() > 1 &&
        owner.documents_[(owner.activeIndex() + 1) % owner.documents_.size()]
            ->scientific())
      return false;
    if (owner.documents_.empty() && !id)
      return true; // Preserve the existing no-document validation.
    const auto source = owner.documentById(id);
    if(action=="show_histogram") {
      const auto captured=source->nativeSnapshot();const auto &data=captured.data().dataset;
      return !owner.busy() && data.shape().s==1 &&
        (data.pixel_type()==PixelType::UInt8 || data.pixel_type()==PixelType::UInt16 || data.pixel_type()==PixelType::Float32) &&
        (data.sample_policy()==FloatingSamplePolicy::FiniteOnly || data.sample_policy()==FloatingSamplePolicy::PreserveQuietNaNFloat32);
    }
    if (action == "show_animation_options" || action == "show_movie_export")
      return !owner.busy() && owner.controller_->active_document_id() == source->id;
    if (action == "animation")
      return owner.controller_->active_document_id() == source->id;
    if(ContrastPanel::handles(action))return true;
    if (action == "show_channel_display" || action == "enable_channel_display" ||
        action == "channel_display" || action == "replay_channel_display" ||
        action == "auto_contrast" || action == "replay_auto_contrast")
      return true; // The native request validates type/state and owner atomically.
    if ((action == "lut" || action == "load_lut" || action == "save_lut") &&
        source->nativeSnapshot().display().properties.count("channel_display"))
      return true;
    if(action=="run_workflow"||action=="batch"||action=="native_batch")return owner.controller_->active_document_id()==source->id;
    if (action=="remove_nans_imagej" ||
        ((action=="show_operation" || action=="rank_replay") && operationId=="remove_nans_imagej"))
      return removeNaNsSourceAllowed(*source);
    if (action == "show_shadow_demo")
      return !owner.busy() && owner.controller_->active_document_id() == source->id && ShadowDemo::eligible(source->nativeSnapshot());
    if (action == "shadow_imagej" || (action == "show_operation" && operationId == "shadow_imagej"))
      return owner.controller_->active_document_id() == source->id &&
             scientific_operation_review_available("shadow_imagej", source->nativeSnapshot());
    if (action=="show_imagej_projection" || action=="show_imagej_grouped_projection" ||
        action=="project_imagej" || action=="replay_imagej_projection") {
      const auto captured=source->nativeSnapshot();
      const auto& data=captured.data().dataset;
      const auto shape=data.shape();
      return !owner.busy() && owner.controller_->active_document_id()==source->id &&
          ((shape.s==1 && (data.pixel_type()==PixelType::UInt8 ||
              data.pixel_type()==PixelType::UInt16 || data.pixel_type()==PixelType::Float32)) ||
           (shape.s==3 && data.pixel_type()==PixelType::UInt8 &&
              (shape.c==1 || (shape.t==1 && shape.z==1))));
    }
    if (!source->scientific())
      return true;
    if(action=="show_display_annotations" || action=="replay_display_annotation")return owner.controller_->active_document_id()==source->id;
    if(action=="replay_planar_annotation" || action=="show_planar_annotation_rebind" || action=="replay_imagej_roi_import" || action=="replay_imagej_annotation_import" || action=="show_roi_import" || action=="show_roi_import_record" || action=="show_roi_export" || action=="show_roi_zip_export" || action=="show_roi_export_record")
      return owner.controller_->active_document_id()==source->id;
    if (action == "show_rgb_dataset" || action=="show_color_stack" || action=="show_indexed_dataset" || action=="show_indexed_rgb") return true;
    if (action == "show_z_subset" || action == "substack" || action == "replay_z_subset")
      return source->nativeSnapshot().data().dataset.shared_planes().has_value();
    if (action == "configure_threshold_target" || action == "auto_manual_threshold" || action == "show_manual_threshold" || action == "set_manual_threshold" ||
        action == "reset_manual_threshold" || action == "manual_threshold_to_selection" ||
        action == "apply_manual_threshold" || action == "replay_manual_threshold") {
      const auto &dataset = source->nativeSnapshot().data().dataset;
      return dataset.shape().s == 1 && dataset.shared_planes() &&
             dataset.sample_policy() == FloatingSamplePolicy::FiniteOnly;
    }
    if(action=="enlarge_selection" || action=="grow_selection" || action=="shrink_selection" ||
       action=="show_selection_enlargement" || action=="replay_selection_enlargement")
      return !owner.busy() && owner.controller_->active_document_id()==source->id;
    if((action=="show_selection_properties" || action=="selection_properties_draft" || action=="apply_selection_properties" || action=="cancel_selection_properties") &&
        source->nativeSnapshot().data().selection && source->nativeSnapshot().data().selection->is_path())
      return owner.controller_->active_document_id()==source->id;
    if(action=="select_all" || action=="select_none" || action=="restore_selection" || action=="invert_selection")
      return !owner.busy() && owner.controller_->active_document_id()==source->id;
    if (action == "combine_rois" || action == "create_roi" || action == "add_roi" || action == "select_roi" || action == "update_roi" ||
        action == "rename_roi" || action == "delete_roi" || action == "roi_properties")
      return owner.controller_->active_document_id() == source->id;
    if (action == "gaussian_blur_imagej" || action == "gaussian_replay" ||
        (action == "show_operation" && operationId == "gaussian_blur_imagej"))
      return gaussianSourceAllowed(*source);
    if (action == "rank_filter_imagej" || action == "rank_replay" || action == "despeckle_imagej" || (action == "remove_outliers_imagej" || action == "top_hat_imagej") ||
        (action == "show_operation" && (operationId == "rank_filter_imagej" || (operationId == "remove_outliers_imagej" || operationId == "top_hat_imagej"))))
      return rankSourceAllowed(*source);
    const auto processReviewAvailable = [&](const std::string &operation) {
      return scientific_operation_review_available(operation) &&
          (!(scientific_binary_operation(operation) || operation == "invert_uint16" || operation == "convert_image") ||
           scientific_operation_review_available(operation, source->nativeSnapshot()));
    };
    if ((action == "show_operation" && processReviewAvailable(operationId)) ||
        processReviewAvailable(action))
      return owner.controller_->active_document_id() == source->id;
    if (action == "export_axis_plane" || action == "show_axis_plane_export" ||
        action == "reslice_spaced_axis_range" || action == "show_axis_range_spacing" ||
        action == "replay_axis_spacing" || action == "reslice_slicer_axis_range" ||
        action == "show_axis_range_slicer_resize" || action == "replay_slicer_resize" ||
        action == "reslice_axis_range" || action == "show_axis_range_reslice" ||
        action == "replay_axis_extraction" || action == "replay_axis_orientation" ||
        action == "reslice_oriented_axis_range" || action == "show_axis_range_orientation")
      return axisExtractionSourceAllowed(*source);
    return action == "view" || action == "display_range" ||
           action == "lut" || action == "load_lut" || action == "save_lut" ||
           action == "save_document" || action == "undo" || action == "redo";
  }
  int animationExtent(const DatasetShape &shape, const std::string &axis) const {
    return axis == "z" ? shape.z : axis == "t" ? shape.t : shape.c;
  }
  bool animationCalibratedAxis(const DatasetShape &shape, const std::string &axis) const {
    // Ordinary ImageJ stacks may store a time series along Z. This is an
    // explicit timing choice, never inferred from spatial spacing or units.
    return axis == "t" || (axis == "z" && shape.t == 1 && shape.c == 1);
  }
  void stopAnimation(const char *reason) {
    animating = false;
    animationReason = reason;
    animationSource.reset();
  }
  void configureAnimation(const Json &parameters) {
    if (!parameters.is_object())
      throw std::invalid_argument("Animation parameters must be an object");
    for (const auto &field : parameters.items())
      if (field.key() != "axis" && field.key() != "playing" && field.key() != "document_id" &&
          field.key() != "first" && field.key() != "last" && field.key() != "step" && field.key() != "mode" && field.key() != "timing")
        throw std::invalid_argument("Unknown animation parameter: " + field.key());
    auto axis = animationAxis;
    if (parameters.contains("axis")) {
      if (!parameters.at("axis").is_string())
        throw std::invalid_argument("Animation axis must be Z, C or T");
      axis = parameters.at("axis").get<std::string>();
      if (axis == "Z" || axis == "C" || axis == "T") axis[0] += 'a' - 'A';
      if (axis != "z" && axis != "c" && axis != "t")
        throw std::invalid_argument("Animation axis must be Z, C or T");
    }
    if (parameters.contains("playing") && !parameters.at("playing").is_boolean())
      throw std::invalid_argument("Animation playing must be boolean");
    const bool playing = parameters.value("playing", animating);
    const bool configured = parameters.contains("axis") || parameters.contains("first") ||
        parameters.contains("last") || parameters.contains("step") || parameters.contains("mode") || parameters.contains("timing");
    if (!playing && !configured) { stopAnimation("stopped"); return; }
    const auto index = [&](const char *key, int fallback) {
      if (!parameters.contains(key)) return fallback;
      const auto &value = parameters.at(key);
      if (!value.is_number_integer() || value < 0 || value > INT32_MAX)
        throw std::invalid_argument(std::string("Animation ") + key + " must be a nonnegative integer");
      return value.get<int>();
    };
    const bool axisChanged = axis != animationAxis;
    const int first = index("first", axisChanged ? 0 : animationFirst);
    const int step = index("step", axisChanged ? 1 : animationStep);
    auto last = axisChanged ? std::optional<int>{} : animationLast;
    if (parameters.contains("last")) last = parameters.at("last").is_null() ? std::optional<int>{} : index("last",0);
    auto mode = animationMode;
    if (parameters.contains("mode")) {
      if (!parameters.at("mode").is_string()) throw std::invalid_argument("Animation mode must be text");
      mode = parse_playback_mode(parameters.at("mode").get<std::string>());
    }
    const auto captured = owner.nativeSnapshot();
    if (parameters.contains("document_id") && parameters.at("document_id") != captured.id())
      throw std::invalid_argument("Animation requires the active document");
    PlaybackSequence sequence(animationExtent(captured.data().dataset.shape(),axis),first,last,step,mode);
    auto timing = axisChanged && axis != "t" ? std::string("fps") : animationTiming;
    if(parameters.contains("timing")) {
      if(!parameters.at("timing").is_string())throw std::invalid_argument("Animation timing must be text");
      timing=parameters.at("timing").get<std::string>();
    }
    if(timing=="calibrated_time" && !animationCalibratedAxis(captured.data().dataset.shape(),axis))
      throw std::invalid_argument("Calibrated playback requires T or an ordinary Z time-series stack");
    const auto &calibration=captured.data().dataset.calibration();
    const double interval=playback_interval_seconds(fps,timing,calibration.frame_interval,calibration.time_unit,step);
    std::optional<DocumentSnapshot> source;
    if (playing) {
      if (owner.busy()) throw std::invalid_argument("Wait for the current edit");
      source = captured;
      if (sequence.count() < 2)
        throw std::invalid_argument("Animation axis needs at least two positions");
    }
    // Validate the entire request before changing a running playback session.
    animationAxis = axis;
    animationFirst = first; animationLast = last; animationStep = step;
    animationMode = mode; animationSequence = sequence; animationReverse = mode == PlaybackMode::Reverse;
    animationTiming = timing; animationCalibratedInterval = interval;
    if (!playing) { stopAnimation("stopped"); return; }
    animationSource = std::move(source);
    animationTicks = 0;
    lastTick = -1;
    animationReason = "playing";
    animating = true;
    cancelSavedRoiGeometry("playback");
    cancelCreations("playback");
  }
  void advanceAnimation() {
    if (!animating) {
      if (animationSource) stopAnimation("stopped");
      return;
    }
    if (owner.documents_.empty()) { stopAnimation("source_closed"); return; }
    if (owner.busy()) { stopAnimation("busy"); return; }
    const auto current = owner.nativeSnapshot();
    if (!animationSource || !animationSource->same_document(current)) {
      stopAnimation("source_changed"); return;
    }
    if (!current.revisions().matches(animationSource->revisions(), Change::Pixels | Change::Metadata)) {
      stopAnimation("source_edited"); return;
    }
    const int extent = animationExtent(current.data().dataset.shape(), animationAxis);
    if (extent < 2) { stopAnimation("single_position"); return; }
    const double now = ImGui::GetTime();
    if (lastTick < 0 || now < lastTick) { lastTick = now; return; }
    const double interval=animationTiming=="fps" ? 1/fps : animationCalibratedInterval;
    if (now - lastTick < interval) return;
    auto display = current.display();
    int &position = animationAxis == "z" ? display.crosshair[2] : animationAxis == "t" ? display.t : display.c;
    const auto next = animationSequence->advance(position, animationReverse);
    position = next.position;
    animationReverse = next.reverse;
    cancelSavedRoiGeometry("position_changed");
    cancelCreations("position_changed");
    doc()->setNativeDisplay(std::move(display));
    ++animationTicks;
    lastTick = now;
  }
  Json animationDiagnostics() const {
    return {{"playing", animating}, {"axis", animationAxis}, {"fps", fps},
            {"timing",animationTiming},{"interval_seconds",animationTiming=="fps"?1/fps:animationCalibratedInterval},
            {"ticks", animationTicks}, {"reason", animationReason},
            {"first", animationFirst}, {"last", animationLast ? Json(*animationLast) : Json()},
            {"step", animationStep}, {"mode", playback_mode_name(animationMode)}, {"reverse", animationReverse},
            {"count", animationSequence ? Json(animationSequence->count()) : Json()},
            {"last_selected", animationSequence ? Json(animationSequence->last_selected()) : Json()},
            {"source_id", animationSource ? Json(animationSource->id()) : Json()},
            {"clock", "imgui_monotonic_seconds"}, {"last_tick", lastTick}};
  }
  void requireAction(const std::string &action, std::uint64_t id = 0,
                     const std::string &operationId = {}) const {
    if (!actionAllowed(action, id, operationId))
      throw std::invalid_argument(
          "This command is not supported for scientific desktop documents");
  }
  void showRoiExport(std::optional<MetadataObject> original = {}, bool zip = false) {
    if(owner.busy())throw std::invalid_argument("Wait for the current edit");
    displayAnnotationPanel.cancel("roi_export");roiExportPanel.show(owner.nativeSnapshot(),original.value_or(MetadataObject{}),zip);roiImportPanel.cancel("roi_export");
    planarAnnotationRebind.cancel("roi_export");cancelSavedRoiGeometry("roi_export");
    selectionPanel.cancel();cancelCreations("roi_export");
    for(auto &pane:panes)pane.orthogonal.resetInput();
  }
  std::shared_ptr<const RoiImportPreview> roiPreview(const DocumentSnapshot &source,std::size_t cap) const {
    return roiExportPanel.visible()?roiExportPanel.preview(source,cap):roiImportPanel.preview(source,cap);
  }
  void roiPreviewError(const std::string &error) {
    if(roiExportPanel.visible())roiExportPanel.preview_error(error);else roiImportPanel.preview_error(error);
  }
  void showRoiImport(std::optional<MetadataObject> record = {}) {
    displayAnnotationPanel.cancel("roi_import");
    if(owner.busy())throw std::invalid_argument("Wait for the current edit");
    if(record)roiImportPanel.show_record(owner.nativeSnapshot(),std::move(*record));
    else roiImportPanel.show(owner.nativeSnapshot());
    roiExportPanel.cancel("roi_import");
    planarAnnotationRebind.cancel("roi_import");
    cancelSavedRoiGeometry("roi_import");selectionPanel.cancel();cancelCreations("roi_import");
    for(auto &pane:panes)pane.orthogonal.resetInput();
  }
  void publishImageJRoiExport(const DocumentSnapshot &source,const MetadataObject &record,const std::function<void()> &publishFile) {
    if(owner.busy() || owner.controller_->active_document_id()!=source.id())
      throw std::invalid_argument("ROI export requires the idle active source");
    const auto current=owner.nativeSnapshot();
    if(!source.same_document(current) || !source.shares_data(current))throw RevisionConflict();
    if(!current.revisions().matches(source.revisions(),Change::Pixels|Change::Metadata))throw RevisionConflict();
    OutputPublication outputs(owner.results,owner.workflow,{},"",owner.recording?metadataJson(record):Json(nullptr));
    publishFile();outputs.publish();
  }
  void publishImageJAnnotationImport(const ResolvedImageJAnnotationImport &command) {
    const auto &source=command.source();
    if(owner.busy() || owner.controller_->active_document_id()!=source.id())throw std::invalid_argument("Mixed import publication requires the idle active source");
    auto facade=owner.documentById(source.id());const auto current=facade->nativeSnapshot();
    if(!source.same_document(current) || !current.revisions().matches(source.revisions(),Change::Pixels|Change::Metadata))throw RevisionConflict();
    auto cap=static_cast<std::size_t>(preferenceState.settings.integer("processing_memory_mb"))*1024*1024;if(!cap)cap=256*1048576;
    admit_planar_annotation_visuals(source.data().dataset.shape(),command.annotations(),cap);
    admit_display_annotation_visuals(command.display_annotations(),cap);
    const auto &record=command.record();
    if(metadata_equal(record.at("before"),record.at("after")) && metadata_equal(record.at("before_display"),record.at("after_display")) &&
        metadata_equal(record.at("before_sources"),record.at("after_sources"))) {facade->commitImageJAnnotationImport(command);return;}
    OutputPublication outputs(owner.results,owner.workflow,{},"",owner.recording?metadataJson(record):Json(nullptr));
    facade->commitImageJAnnotationImport(command);outputs.publish();
  }
  void publishImageJRoiImport(const ResolvedImageJRoiImport &command) {
    const auto &source=command.source();
    if(owner.busy() || owner.controller_->active_document_id()!=source.id())
      throw std::invalid_argument("ROI import publication requires the idle active source");
    auto facade=owner.documentById(source.id());
    const auto current=facade->nativeSnapshot();
    if(!source.same_document(current))throw std::invalid_argument("ROI import belongs to another source");
    if(!current.revisions().matches(source.revisions(),Change::Pixels|Change::Metadata))throw RevisionConflict();
    auto cap=static_cast<std::size_t>(preferenceState.settings.integer("processing_memory_mb"))*1024*1024;
    if(!cap)cap=256*1048576;
    admit_planar_annotation_visuals(source.data().dataset.shape(),command.annotations(),cap);
    const auto &record=command.record();
    if(metadata_equal(record.at("before"),record.at("after")) &&
        metadata_equal(record.at("before_sources"),record.at("after_sources"))) {
      facade->commitImageJRoiImport(command);return;
    }
    OutputPublication outputs(owner.results,owner.workflow,{},"",
        owner.recording?metadataJson(record):Json(nullptr));
    facade->commitImageJRoiImport(command);outputs.publish();
  }
  void publishDisplayAnnotation(const ResolvedPlanarDisplayAnnotationCommand &command) {
    const auto &source=command.source();
    if(owner.busy() || owner.controller_->active_document_id()!=source.id())throw std::invalid_argument("Display annotation requires the idle active source");
    auto facade=owner.documentById(source.id());const auto current=facade->nativeSnapshot();
    if(!source.same_document(current) || !current.revisions().matches(source.revisions(),Change::Pixels|Change::Metadata))throw RevisionConflict();
    auto cap=static_cast<std::size_t>(preferenceState.settings.integer("processing_memory_mb"))*1024*1024;
    if(!cap)cap=256*1048576;
    admit_display_annotation_visuals(decode_planar_display_annotations(source.data().dataset.shape(),command.record().at("after")),cap);
    auto edit=prepare_planar_display_annotation(command);
    if(metadata_equal(command.record().at("before"),command.record().at("after"))){facade->commitDisplayAnnotation(edit);return;}
    OutputPublication outputs(owner.results,owner.workflow,{},"",owner.recording?metadataJson(command.record()):Json(nullptr));
    facade->commitDisplayAnnotation(edit);outputs.publish();
  }
  void publishPlanarAnnotation(const ResolvedPlanarAnnotationCommand &command) {
    const auto &source=command.source();
    if(owner.busy() || owner.controller_->active_document_id()!=source.id())
      throw std::invalid_argument("Annotation publication requires the idle active source");
    auto facade=owner.documentById(source.id());
    const auto current=facade->nativeSnapshot();
    if(!source.same_document(current))throw std::invalid_argument("Annotation belongs to another source");
    if(!current.revisions().matches(source.revisions(),Change::Pixels|Change::Metadata))throw RevisionConflict();
    auto cap=static_cast<std::size_t>(preferenceState.settings.integer("processing_memory_mb"))*1024*1024;
    if(!cap)cap=256*1048576;
    admit_planar_annotation_visuals(source.data().dataset.shape(),
        decode_planar_annotations(source.data().dataset.shape(),command.record().at("after")),cap);
    auto edit=prepare_planar_annotation(command);
    if(metadata_equal(command.record().at("before"),command.record().at("after"))) {
      // No-op suppression must preserve all native transaction validation.
      facade->commitPlanarAnnotation(edit);return;
    }
    OutputPublication outputs(owner.results,owner.workflow,{},"",
        owner.recording?metadataJson(command.record()):Json(nullptr));
    facade->commitPlanarAnnotation(edit);outputs.publish();
  }
  void showPlanarAnnotationRebind(const MetadataObject &record) {
    validate_planar_annotation_command_record(record);
    if(owner.busy())throw std::invalid_argument("Wait for the current edit");
    const auto source=owner.nativeSnapshot();
    planarAnnotationRebind.show(source,record);
    cancelSavedRoiGeometry("annotation_rebind");selectionPanel.cancel();cancelCreations("annotation_rebind");
    for(auto &pane:panes)pane.orthogonal.resetInput();
  }
  static const char *shortcutForAction(const std::string &action) noexcept {
    constexpr std::pair<const char *, const char *> actions[] = {
        {"save_document", "file.save"}, {"close_document", "file.close"},
        {"undo", "edit.undo"}, {"redo", "edit.redo"},
        {"copy", "edit.copy"}, {"cut", "edit.cut"}, {"paste", "edit.paste"},
        {"select_all", "edit.select_all"}, {"measure", "analysis.measure"},
        {"show_preferences", "app.preferences"}, {"show_jobs", "app.jobs"},
        {"show_channel_display", "view.channel_display"},
        {"auto_contrast", "image.auto_contrast"},
        {"show_axis_plane_export", "image.extract_plane"},
        {"gaussian_blur_imagej", "filter.gaussian_imagej"},
        {"rank_filter_imagej", "filter.rank_imagej"}};
    for (const auto &entry : actions)
      if (action == entry.first) return entry.second;
    return nullptr;
  }
  const char *shortcutLabel(const std::string &id) const noexcept {
    const auto found = shortcutLabels.find(id);
    return found == shortcutLabels.end() ? "" : found->second.c_str();
  }
  bool shortcutAvailable(const std::string &id) const {
    if (id == "app.preferences" || id == "app.jobs") return true;
    if (owner.busy()) return false;
    if (id == "file.open") return true;
    if (owner.documents_.empty()) return false;
    if (id == "view.orthogonal") return actionAllowed("view");
    if (id == "filter.gaussian_imagej")
      return actionAllowed("show_operation", 0, "gaussian_blur_imagej");
    if (id == "filter.rank_imagej")
      return actionAllowed("show_operation", 0, "rank_filter_imagej");
    if (id == "edit.undo") return actionAllowed("undo") && doc()->undoSteps() > 0;
    if (id == "edit.redo") return actionAllowed("redo") && doc()->redoSteps() > 0;
    for (const auto &command : shortcut_commands()) {
      if (command.id != id) continue;
      constexpr const char *actions[] = {"save_document", "close_document",
          "copy", "cut", "paste", "select_all", "measure",
          "show_channel_display", "auto_contrast", "show_axis_plane_export"};
      for (const auto action : actions)
        if (id == shortcutForAction(action)) return actionAllowed(action);
    }
    return false;
  }
  void invokeShellCommand(const std::string &id) {
    // Both menus and global keys use this closed table and recheck admission.
    if (!shortcutAvailable(id))
      throw std::invalid_argument("Shortcut command is unavailable");
    if (id == "file.open") {
      file("Open image or session", false,
           [this](std::string path) { owner.openFile(path); });
    } else if (id == "file.save") {
      file("Save document", true,
           [this](std::string path) { owner.action("save_document", {{"path", path}}); },
           doc()->path.empty() ? "document.ijx" : doc()->path);
    } else if (id == "file.close") requestClose(doc());
    else if (id == "view.orthogonal")
      owner.action("view", {{"orthogonal", !doc()->view().orthogonal}});
    else if (id == "filter.gaussian_imagej" || id == "filter.rank_imagej")
      owner.action("show_operation", {{"operation", id == "filter.gaussian_imagej"
          ? "gaussian_blur_imagej" : "rank_filter_imagej"}});
    else if (id == "app.jobs") owner.action("show_jobs", {{"visible", true}});
    else {
      constexpr const char *actions[] = {"undo", "redo", "copy", "cut", "paste",
          "select_all", "measure", "show_preferences", "show_channel_display",
          "auto_contrast", "show_axis_plane_export"};
      for (const auto action : actions)
        if (id == shortcutForAction(action)) {
          owner.action(action);
          return;
        }
      throw std::invalid_argument("Unknown registered shortcut command");
    }
  }
  void dispatchShortcuts() {
    const auto &io = ImGui::GetIO();
    const auto &gui = *ImGui::GetCurrentContext();
    ShortcutContext context;
    context.window_focused = !io.AppFocusLost;
    const bool modal = dialog.open || browser.open || pendingClose || closingApp ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    bool canvas = false, activeCanvas = false, workspace = false, manager = false;
    for (auto window = gui.NavWindow; window; window = window->ParentWindow) {
      workspace |= window == shortcutWorkspace;
      manager |= window == shortcutRoiManager;
    }
    // Earlier manager drawing precedes Images and floating panels. Resolve its
    // keyboard ownership from the final current-frame navigation window.
    roiManagerFocused = manager;
    for (const auto &pane : panes) {
      canvas |= pane.ordinaryCanvasVisible && pane.ordinaryCanvasId &&
                gui.NavId == pane.ordinaryCanvasId;
      activeCanvas |= pane.ordinaryCanvasVisible && pane.ordinaryCanvasId &&
                      gui.ActiveId == pane.ordinaryCanvasId;
      if (pane.orthogonalWasVisible)
        for (const auto &view : pane.orthogonal.diagnostics()) {
          canvas |= gui.NavId != 0 && get(view, "canvas_id") == gui.NavId;
          activeCanvas |= gui.ActiveId != 0 && get(view, "canvas_id") == gui.ActiveId;
        }
      context.gesture_active |= pane.dragging || pane.selectionGesture.has_value() ||
                                pane.creation.has_value() || !pane.points.empty();
    }
    context.gesture_active |= pathAuthoring.active() || savedRoiEdit.pointer_active() ||
                              (canvas && ImGui::IsKeyDown(ImGuiKey_Space));
    if (settingsPanel.shortcutCaptureActive()) context.focus = ShortcutFocus::Capture;
    else if (modal) context.focus = ShortcutFocus::Modal;
    else if (io.WantTextInput) context.focus = ShortcutFocus::Text;
    else if (settingsPanel.focused() ||
             (gui.ActiveId != 0 && !activeCanvas) ||
             (!roiManagerFocused && !canvas &&
              ((gui.NavId != 0 && gui.NavIdIsAlive) || (gui.NavWindow && !workspace))))
      context.focus = ShortcutFocus::Control;
    else if (roiManagerFocused) context.focus = ShortcutFocus::ROIManager;
    else if (canvas) context.focus = ShortcutFocus::Canvas;
    else context.focus = workspace ? ShortcutFocus::Workspace : ShortcutFocus::None;
    const auto currentOwner = owner.documents_.empty() ? 0 : doc()->id;
    // A closed canvas or popup can leave a remembered NavId in its window.
    // Only a submitted current-frame navigation item owns shortcut focus.
    const auto currentContext = gui.NavId && gui.NavIdIsAlive ? gui.NavId :
                               gui.NavWindow ? gui.NavWindow->ID : 0;
    const bool blocked = !context.window_focused || context.gesture_active ||
        context.focus == ShortcutFocus::Capture || context.focus == ShortcutFocus::Modal ||
        context.focus == ShortcutFocus::Text || context.focus == ShortcutFocus::Control ||
        context.focus == ShortcutFocus::None;
    if (blocked || currentOwner != shortcutOwner || currentContext != shortcutContextOwner)
      shortcutsRearmed = false;
    shortcutOwner = currentOwner;
    shortcutContextOwner = currentContext;
    if (!blocked && !shortcutsRearmed && shortcutInputsReleased()) shortcutsRearmed = true;
    context.rearmed = shortcutsRearmed;
    const bool primary = desktopShortcutPlatform() == ShortcutPlatform::MacOS
        ? io.KeySuper && !io.KeyCtrl : io.KeyCtrl && !io.KeySuper;
    if (context.focus == ShortcutFocus::ROIManager && context.window_focused &&
        context.rearmed && !context.gesture_active && !owner.busy() &&
        actionAllowed("select_roi") && roiManagerSource &&
        !roiManagerSource->data().rois.empty() && primary && !io.KeyAlt && !io.KeyShift &&
        ImGui::GetKeyOwner(ImGuiKey_A) == ImGuiKeyOwner_NoOwner &&
        ImGui::IsKeyPressed(ImGuiKey_A, false)) {
      ImGui::SetKeyOwner(ImGuiKey_A, ImHashStr("imagejxx.roi_manager_select_all"),
                         ImGuiInputFlags_LockThisFrame);
      selectAllRoiManagerRows();
    }
    for (const auto &command : shortcut_commands())
      context.availability.emplace(command.id, shortcutAvailable(command.id));
    const auto input = shortcutInputEvents();
    std::vector<ShortcutEvent> events;
    Json claimed = Json::array(), raw = Json::array();
    for (const auto &entry : input) {
      const auto keyOwner = ImGui::GetKeyOwner(entry.imgui_key);
      const auto key = format_shortcut_chord({entry.event.key, ShortcutModifiers::None});
      raw.push_back({{"key", key}, {"ctrl", entry.event.ctrl}, {"alt", entry.event.alt},
                     {"shift", entry.event.shift}, {"super", entry.event.super},
                     {"down_edge", entry.event.down_edge}, {"repeat", entry.event.repeat}});
      if (keyOwner != ImGuiKeyOwner_NoOwner) claimed.push_back(key);
      else events.push_back(entry.event);
    }
    const auto decision = evaluate_shortcuts(shortcutProfile, desktopShortcutPlatform(),
                                              events, context);
    constexpr const char *focusNames[] = {"none", "workspace", "canvas", "roi_manager",
                                         "control", "text", "modal", "capture"};
    constexpr const char *reasons[] = {"dispatch", "unfocused", "rearming", "capture",
        "modal", "control", "gesture", "roi_manager_select_all", "local_claim", "no_edge",
        "repeat", "unbound", "unavailable", "ambiguous", "no_workspace"};
    Json observation = {{"focus", focusNames[static_cast<int>(context.focus)]},
        {"window_focused", context.window_focused}, {"rearmed", context.rearmed},
        {"gesture_active", context.gesture_active}, {"owner_id", currentOwner},
        {"context_owner", currentContext}, {"navigation_id_alive", gui.NavIdIsAlive},
        {"events", std::move(raw)},
        {"local_claimed_keys", std::move(claimed)},
        {"reason", reasons[static_cast<int>(decision.reason)]},
        {"command_id", decision.command_id ? Json(*decision.command_id) : Json()},
        {"availability", context.availability}, {"callback_completed", false}};
    shortcutObservation.swap(observation);
    if (decision.disposition == ShortcutDisposition::GlobalCommand && decision.command_id) {
      // Lock only the successful named key; local owners were omitted above.
      for (const auto &entry : input)
        if (entry.event.key == shortcutProfile.chord(*decision.command_id,
                desktopShortcutPlatform())->key)
          ImGui::SetKeyOwner(entry.imgui_key, ImHashStr("imagejxx.global_shortcut"),
                             ImGuiInputFlags_LockUntilRelease);
      invokeShellCommand(*decision.command_id);
      shortcutObservation["callback_completed"] = true;
    }
  }
  std::size_t rgbCap() const {
    auto cap=std::size_t(preferenceState.settings.integer("processing_memory_mb"))*1048576;
    return cap?cap:256*1048576;
  }
  template<class Command>
  std::uint64_t publishRgb(const Command &command, PreparedOperation &prepared, bool recorded) {
  const auto &source = command.source();
  const auto current = owner.nativeSnapshot(source.id());
  if (!source.same_document(current) || !current.revisions().matches(source.revisions(), Change::All))
    throw RevisionConflict();
  std::string completed = "Color conversion created";
  Json emptyControls=Json::object(),emptyRows=Json::array();
  if (command.request().destination == OutputDestination::InPlace) {
    OutputPublication outputs(owner.results, owner.workflow, ComponentCounts{}, "",
        recorded ? metadataJson(command.record()) : Json());
    owner.controller_->publish(prepared);
    outputs.publish();
    roiManagerSource.reset();roiManagerPendingFocus.reset();savedRoiHistoryFocus.reset();
    roiSelectedIndices.clear();roiRangeAnchor.reset();roiIndex=-1;
    roiManagerControls.swap(emptyControls);roiManagerRows.swap(emptyRows);
    status.swap(completed);
    return source.id();
  }
  auto candidate = prepared.prepared_new_document();
  auto facade = std::make_shared<Document>(candidate);
  facade->setHistoryPolicy(historyLimit, static_cast<std::size_t>(
      preferenceState.settings.integer("history_memory_mb"))*1024*1024);
  owner.documents_.reserve(owner.documents_.size()+1);
  if (!owner.documentAdapters_.emplace(candidate->id,facade).second)
    throw std::invalid_argument("RGB conversion output is already attached");
  bool paneStaged=false;
  std::optional<OutputPublication> outputs;
  try {
    panes.emplace_back();paneStaged=true;panes.back().doc=facade;
    outputs.emplace(owner.results,owner.workflow,ComponentCounts{},"",recorded?metadataJson(command.record()):Json());
    owner.controller_->publish(prepared);
  } catch (...) {
    if(paneStaged)panes.pop_back();
    owner.documentAdapters_.erase(candidate->id);throw;
  }
  facade->bindPublishedController(owner.controller_);owner.documents_.push_back(std::move(facade));
  roiManagerSource.reset();roiSelectedIndices.clear();roiRangeAnchor.reset();roiIndex=-1;
  roiManagerControls.swap(emptyControls);roiManagerRows.swap(emptyRows);
  outputs->publish();status.swap(completed);
  return candidate->id;
}

  void pollRgb() {
    rgbDatasetPanel.tick(owner.documents_.empty()?std::optional<DocumentSnapshot>{}:std::optional<DocumentSnapshot>{owner.nativeSnapshot()},rgbCap(),
      [this](const auto &command,auto &prepared,bool recorded){publishRgb(command,prepared,recorded);});
  }
  void pollColorStack() {
    colorStackPanel.tick(owner.documents_.empty()?std::optional<DocumentSnapshot>{}:std::optional<DocumentSnapshot>{owner.nativeSnapshot()},rgbCap(),
      [this](const auto &command,auto &prepared,bool recorded){publishRgb(command,prepared,recorded);});
  }
  void pollIndexedColor() {
    indexedColorPanel.tick(owner.documents_.empty()?std::optional<DocumentSnapshot>{}:std::optional<DocumentSnapshot>{owner.nativeSnapshot()},rgbCap(),
      [this](const auto &command,auto &prepared,bool recorded){std::visit([&](const auto &c){publishRgb(c,prepared,recorded);},command);});
  }
  bool workflowOtherBusy() const { return movieExportPanel.working() || shadowDemo.active() || indexedColorPanel.visible() || indexedColorPanel.working() || colorStackPanel.visible() || colorStackPanel.working() || rgbDatasetPanel.visible() || rgbDatasetPanel.working() || legacyBusy() || gaussianJobs.apply_busy() || bool(thresholdJob) || bool(autoJob) || bool(contrastApplyJob) || bool(histogramTask); }
  bool legacyBusy() const {
    return job.valid() && job.info().state != JobState::Publishing;
  }
  std::optional<DocumentSnapshot> gaussianCurrent() const {
    return owner.documents_.empty()
               ? std::nullopt
               : std::optional<DocumentSnapshot>(doc()->nativeSnapshot());
  }
  void tickContrast(){
    if(!contrastPanel.visible() && !contrastPanel.rebindVisible())return;
    const auto current=gaussianCurrent();contrastPanel.tickRebind(current);
    if(contrastPanel.visible()){
      const auto conversion=owner.settings().conversion_options();contrastPanel.rgbWeights(conversion.weighted_rgb?conversion.rgb_weights:std::array<double,3>{1./3,1./3,1./3});
      contrastPanel.tick(current,owner.busy(),*owner.controller_->job_scheduler(),histogramLimits(),contrastApplyLimits());
    }
  }
  void publishContrast(const DocumentSnapshot &source,std::array<double,2> range){
    if(owner.busy())throw std::invalid_argument("Wait for the current edit");
    const auto current=owner.nativeSnapshot();
    if(!source.same_document(current) || !source.revisions().matches(current.revisions(),Change::All))throw RevisionConflict();
    const auto &data=source.data().dataset;
    if(auto channels=recognized_channel_display(source.display(),data)){
      channels->channels.at(source.display().c).range=range;
      applyChannelPanel(source,{ChannelDisplayAction::Set,std::move(*channels),{}});
    }else{
      auto display=source.display();display.properties["display_range"]=metadataFromJson(Json(range));
      Image carrier(1,1,data.shape().s,data.pixel_type());
      (void)displayImage(carrier,metadataJson(display.properties),data.sample_policy());
      owner.controller_->set_display_if_current(source,std::move(display));
    }
  }
  void contrastAuto(const DocumentSnapshot &source,bool all,bool selection){
    auto options=autoOptions(all);options.roi=selection?AutoContrastRoiPolicy::ActiveSelection:AutoContrastRoiPolicy::WholePlane;
    if(recognized_channel_display(source.display(),source.data().dataset))owner.applyAutoContrast(source,options);
    else{
      const auto current=owner.nativeSnapshot();
      if(owner.busy() || !source.same_document(current) || !source.revisions().matches(current.revisions(),Change::All))throw RevisionConflict();
      if(boolean(get(metadataJson(source.data().metadata.extensions),"labels")))throw std::invalid_argument("Disable label coloring before Auto");
      const auto limit=autoLimits();const auto count=std::size_t(source.data().dataset.shape().c);
      if(count>limit.max_working_bytes/(8*sizeof(ChannelDisplayChannel)))throw std::invalid_argument("Auto channel display exceeds metadata budget");
      const auto properties=metadataJson(source.display().properties);const auto lut=inheritedDisplayLut(properties);
      ChannelDisplayState initial;initial.mode=ChannelDisplayMode::Color;initial.channels.resize(count);
      const auto range=contrastPanel.currentRange();
      for(auto &channel:initial.channels){channel.range=range;channel.lut=lut;}
      const auto epoch=autoCancelEpoch.load();JobOptions jobOptions;jobOptions.label="Auto contrast";
      auto submitted=owner.controller_->submit_seeded_auto_contrast(initial,options,limit,std::move(jobOptions));
      bindAutoJob(std::move(submitted),source.id(),epoch);
    }
  }
  static bool rgbContrastRecord(const MetadataObject &record){
    const auto it=record.find("schema");const auto value=it==record.end()?nullptr:std::get_if<std::string>(&it->second);
    return value && *value=="imagejxx.rgb-contrast-apply-command";
  }
  static bool rgbSessionContrastRecord(const MetadataObject &record){
    const auto it=record.find("schema");const auto value=it==record.end()?nullptr:std::get_if<std::string>(&it->second);return value && *value=="imagejxx.rgb-contrast-session-command";
  }
  static bool rgbStackContrastRecord(const MetadataObject &record){
    const auto it=record.find("schema");const auto value=it==record.end()?nullptr:std::get_if<std::string>(&it->second);
    return value && *value=="imagejxx.rgb-contrast-stack-command";
  }
  static void validateContrastRecord(const MetadataObject &record,std::size_t cap){
    if(rgbSessionContrastRecord(record))validate_rgb_contrast_session_command_record(record,cap);else if(rgbStackContrastRecord(record))validate_rgb_contrast_stack_command_record(record,cap);else if(rgbContrastRecord(record))validate_rgb_contrast_apply_command_record(record,cap);else validate_contrast_apply_command_record(record,cap);
  }
  ContrastApplyCommandLimits contrastApplyLimits() const {
    ContrastApplyCommandLimits limits;
    const auto cap=static_cast<std::size_t>(preferenceState.settings.integer("processing_memory_mb"))*1024*1024;
    if(cap)limits.processing.max_working_bytes=std::min(limits.processing.max_working_bytes,cap);
    return limits;
  }
  template<class Command>void beginContrastApply(const Command &command,std::uint64_t epoch,bool reusePreview=false){
    if(owner.busy())throw std::invalid_argument("Wait for the current edit");
    const auto source=command.target().source();
    const auto current=owner.nativeSnapshot();
    if(!source.same_document(current) || !source.revisions().matches(current.revisions(),Change::All))throw RevisionConflict();
    if(boolean(get(metadataJson(source.data().metadata.extensions),"labels")))throw std::invalid_argument("Disable label coloring before Apply");
    auto capturedSource=std::optional<DocumentSnapshot>(source);
    const bool capturedRecording=owner.recording;
    std::string pendingStatus="Applying brightness/contrast...";
    JobOptions options;options.label="Apply brightness/contrast";
    auto submitted=[&]{
      if constexpr(std::is_same_v<Command,ResolvedRgbContrastSessionCommand>){
        if(reusePreview && !command.plan().entire_stack)return contrastPanel.takeRgbPreview(command);
        (void)estimate_rgb_contrast_session_job(command,contrastApplyLimits(),options);
        if(reusePreview)contrastPanel.releaseRgbPreviewForApply();
        return submit_rgb_contrast_session(*owner.controller_->job_scheduler(),command,contrastApplyLimits(),std::move(options));
      }else if constexpr(std::is_same_v<Command,ResolvedRgbContrastStackCommand>){
        if(reusePreview)return contrastPanel.takeRgbPreview(command);
        return submit_rgb_contrast_stack(*owner.controller_->job_scheduler(),command,contrastApplyLimits(),std::move(options));
      }else if constexpr(std::is_same_v<Command,ResolvedRgbContrastApplyCommand>){
        if(reusePreview)return contrastPanel.takeRgbPreview(command);
        return submit_rgb_contrast_apply(*owner.controller_->job_scheduler(),command,contrastApplyLimits(),std::move(options));
      }else return submit_contrast_apply(*owner.controller_->job_scheduler(),command,contrastApplyLimits(),std::move(options));
    }();
    std::uint64_t id;
    try{id=submitted.info().id;}catch(...){submitted.cancel();throw;}
    contrastApplySource=std::move(capturedSource);contrastApplyRecording=capturedRecording;
    contrastApplyJob.emplace(std::move(submitted));
    contrastApplyCancelId.store(id);
    if(contrastApplyCancelEpoch.load()!=epoch)contrastApplyJob->cancel();
    animating=false;status.swap(pendingStatus);
  }
  void applyContrast(const DocumentSnapshot &source,std::array<double,2> range,const TargetRequest &target){
    if(owner.busy())throw std::invalid_argument("Wait for the current edit");
    const auto epoch=contrastApplyCancelEpoch.load();
    if(source.data().dataset.shape().s==3){
      if(contrastPanel.rgbSessionActive()){beginContrastApply(contrastPanel.currentRgbSessionCommand(source),epoch,true);return;}
      if(contrastPanel.rgbEntireStack()){beginContrastApply(contrastPanel.currentRgbStackCommand(source),epoch,true);return;}
      beginContrastApply(resolve_rgb_contrast_apply_command(source,{range[0],range[1],contrastPanel.rgbChannels()},target,contrastApplyLimits()),epoch,true);return;
    }
    ContrastApplyOptions options;options.minimum=range[0];options.maximum=range[1];
    options.unsigned16_bits=owner.settings().conversion_options().unsigned16_bits;
    beginContrastApply(resolve_contrast_apply_command(source,options,target,contrastApplyLimits()),epoch);
  }
  void applyContrastRebind(const DocumentSnapshot &source,const MetadataObject &record,const TargetRequest &target){
    const auto epoch=contrastApplyCancelEpoch.load();
    const auto current=owner.nativeSnapshot();
    if(!source.same_document(current) || !source.revisions().matches(current.revisions(),Change::All))throw RevisionConflict();
    if(owner.busy())throw std::invalid_argument("Wait for the current edit");
    if(rgbContrastRecord(record))beginContrastApply(rebind_rgb_contrast_apply_command(source,record,target,contrastApplyLimits()),epoch);
    else beginContrastApply(rebind_contrast_apply_command(source,record,target,contrastApplyLimits()),epoch);
  }
  void cancelContrastApply(){if(contrastApplyJob)contrastApplyJob->cancel();}
  void pollContrastApply(){
    if(!contrastApplyJob)return;
    const auto current=gaussianCurrent();
    if(!current || !contrastApplySource || !contrastApplySource->same_document(*current) ||
       !contrastApplySource->revisions().matches(current->revisions(),Change::All))contrastApplyJob->cancel();
    if(!contrastApplyJob->ready())return;
    try{
      const auto record=contrastApplyJob->record();
      OutputPublication outputs(owner.results,owner.workflow,{},"",contrastApplyRecording?metadataJson(record):Json(nullptr));
      std::string completed="Brightness/contrast applied";
      contrastApplyJob->publish(*owner.controller_);
      outputs.publish();if(contrastApplySource && contrastApplySource->data().dataset.shape().s==3)contrastPanel.restartRgbSession();status.swap(completed);
    }catch(const OperationCancelled &){status="Contrast Apply cancelled";}
    catch(const std::exception &e){lastError=e.what();status="Contrast Apply failed";}
    contrastApplyCancelId.store(0);contrastApplyJob.reset();contrastApplySource.reset();contrastApplyRecording=false;
  }
  ChannelDisplayLimits channelLimits() const {
    ChannelDisplayLimits limits;
    const auto cap = static_cast<std::size_t>(
        preferenceState.settings.integer("processing_memory_mb")) * 1024 * 1024;
    if (cap) limits.max_working_bytes = std::min(limits.max_working_bytes, cap);
    return limits;
  }
  AutoContrastLimits autoLimits() const {
    AutoContrastLimits limits;
    const auto cap = static_cast<std::size_t>(
        preferenceState.settings.integer("processing_memory_mb")) * 1024 * 1024;
    if (cap) limits.max_working_bytes = std::min(limits.max_working_bytes, cap);
    return limits;
  }
  AutoContrastOptions autoOptions(bool allChannels = false) const {
    AutoContrastOptions options;
    options.scope = allChannels ? AutoContrastScope::CompositeAll
                               : AutoContrastScope::ActiveChannel;
    const auto &bits = preferenceState.settings.text("conversion_unsigned16_range");
    options.unsigned16_bits = bits == "auto" ? 0 : std::stoi(bits);
    return options;
  }
  void bindAutoJob(AutoContrastJob submitted, std::uint64_t sourceId,
                   std::uint64_t cancelEpoch) {
    autoJob.emplace(std::move(submitted));
    autoOwner = sourceId;
    autoRecording = owner.recording;
    autoCancelId.store(autoJob->info().id);
    if (autoCancelEpoch.load() != cancelEpoch) autoJob->cancel();
    animating = false;
    status = "Adjusting range...";
  }
  void pollAuto() {
    if (!autoJob || !autoJob->ready()) return;
    try {
      const auto command = autoJob->resolved();
      OutputPublication outputs(owner.results, owner.workflow, {}, "",
          autoRecording ? metadataJson(command.record()) : Json(nullptr));
      std::string completedStatus = "Auto range applied";
      owner.controller_->publish_auto_contrast(*autoJob);
      outputs.publish();
      status.swap(completedStatus);
    } catch (const OperationCancelled &) {
      status = "Auto range cancelled";
    } catch (const std::exception &error) {
      lastError = error.what();
      status = "Auto range failed";
    }
    autoCancelId.store(0);
    autoJob.reset();
    autoOwner = 0;
    autoRecording = false;
  }
  SelectionEnlargementRequest enlargementRequest(const Json &values,bool rebind=false) const {
    if(!values.is_object())throw std::invalid_argument("Enlargement parameters must be an object");
    for(const auto &[key,value]:values.items())
      if(key!="distance" && key!="units" && key!="geometry" && !(key=="document_id" && !rebind))throw std::invalid_argument("Unknown enlargement parameter");
    if(!values.contains("distance") || !values.at("distance").is_number())throw std::invalid_argument("Enlargement distance must be numeric");
    const auto distance=values.at("distance").get<double>();require_finite(distance,"Enlargement distance");
    if(rebind && !values.contains("units"))throw std::invalid_argument("Rebind requires explicit distance and units");
    const auto units=values.value("units",std::string("pixels"));
    if(units!="pixels" && units!="calibrated")throw std::invalid_argument("Unknown enlargement units");
    const auto geometry=values.value("geometry",std::string("canvas_v1"));
    if(geometry!="canvas_v1" && geometry!="full_region_v2")throw std::invalid_argument("Unknown enlargement geometry mode");
    return {distance,units=="pixels"?SelectionEnlargementUnits::Pixels:SelectionEnlargementUnits::Calibrated,
        geometry=="full_region_v2"?SelectionEnlargementGeometry::FullRegionV2:SelectionEnlargementGeometry::CanvasV1};
  }
  void publishEnlargement(const ResolvedSelectionEnlargementCommand &command) {
    const auto current=owner.nativeSnapshot(command.source().id());
    if(owner.busy() || owner.controller_->active_document_id()!=current.id())throw std::invalid_argument("Enlargement requires the idle active document");
    if(!command.source().same_document(current) || !command.source().revisions().matches(current.revisions(),Change::All))throw RevisionConflict();
    if(!command.changes_selection()){status="Selection unchanged";return;}
    auto edit=prepare_selection_enlargement(command,{},selectionCreationLimits().max_working_bytes);
    OutputPublication outputs(owner.results,owner.workflow,ComponentCounts{},"",owner.recording?metadataJson(command.record()):Json(nullptr));
    cancelCreations("enlargement");cancelSavedRoiGeometry("enlargement");
    const auto facade=owner.documentById(current.id());
    if(facade->scientific())owner.controller_->commit(edit);else facade->commitNative(edit);
    outputs.publish();tickContrast();status="Selection enlarged";
  }
  SelectionCreationLimits selectionCreationLimits() const {
    SelectionCreationLimits limits;
    const auto cap = static_cast<std::size_t>(
        preferenceState.settings.integer("processing_memory_mb")) * 1024 * 1024;
    if (cap) limits.max_working_bytes = std::min(limits.max_working_bytes, cap);
    return limits;
  }
  SavedRoiGeometryLimits savedRoiGeometryLimits() const {
    SavedRoiGeometryLimits limits;
    const auto configured = preferenceState.settings.integer("processing_memory_mb");
    if (configured < 0 || static_cast<std::uint64_t>(configured) >
        std::numeric_limits<std::size_t>::max() / (1024u * 1024u))
      throw std::invalid_argument("Saved ROI memory setting exceeds addressable size");
    const auto cap = static_cast<std::size_t>(configured) * 1024u * 1024u;
    if (cap) limits.max_working_bytes = std::min(limits.max_working_bytes, cap);
    return limits;
  }
  void cancelSavedRoiGeometry(const char *reason) noexcept {
    if (selectionPanel.geometry_only()) selectionPanel.cancel();
    savedRoiEdit.cancel();
    savedRoiPanelShown = false;
    savedRoiVertex.reset();
    savedRoiRebind.reset();
    // Reasons are diagnostics only; cancellation itself cannot fail.
    try { savedRoiCancelReason = reason; } catch (...) {}
    for (auto &pane : panes) {
      pane.transientOutline.reset(); pane.transientVisual.reset();
      pane.transientStyle.clear();
    }
  }
  bool savedRoiMatches(const DocumentSnapshot &current) const {
    if (!savedRoiEdit.active()) return false;
    const auto &source = savedRoiEdit.source();
    return source.same_document(current) &&
        source.revisions().matches(current.revisions(), Change::Data) &&
        source.display().t == current.display().t &&
        source.display().c == current.display().c &&
        source.display().crosshair[2] == current.display().crosshair[2];
  }
  void beginSavedRoiGeometryEdit(std::size_t row) {
    if (owner.busy()) throw std::invalid_argument("Wait for the current edit");
    cancelSavedRoiGeometry("new_edit");
    cancelCreations("saved_area_edit");
    for (auto &pane : panes) {
      pane.points.clear(); pane.dragging = false; pane.selectionGesture.reset();
    }
    const auto source = owner.nativeSnapshot();
    if (row >= source.data().rois.size())
      throw std::invalid_argument("Select a saved ROI");
    const auto &roi = source.data().rois[row];
    auto display = source.display();
    if (roi.t) display.t = *roi.t;
    if (roi.c) display.c = *roi.c;
    if (roi.z) display.crosshair[2] = *roi.z;
    const auto limits = savedRoiGeometryLimits();
    // Admit the complete row/cap before explicit position navigation. Wildcard
    // axes retain the actual current position; an explicit zero stays zero.
    estimate_saved_roi_geometry(source.with_display(display),
        SavedRoiGeometryRequest(row, roi.selection), limits);
    if (display.t != source.display().t || display.c != source.display().c ||
        display.crosshair[2] != source.display().crosshair[2])
      owner.controller_->set_display(std::move(display));
    const auto capture = owner.controller_->capture_saved_roi_geometry();
    if (!capture.source().revisions().matches(source.revisions(), Change::Data) ||
        !source.same_document(capture.source()))
      throw RevisionConflict();
    try {
      savedRoiEdit.begin(capture, row, limits);
      savedRoiEdit.show_numeric(selectionPanel);
      savedRoiPanelShown = true;
      focusRoiManagerRow(row, true);
      savedRoiCancelReason.clear();
    } catch (...) { cancelSavedRoiGeometry("entry_failed"); throw; }
  }
  void applySavedRoiNumeric(const Selection &value) {
    try {
      if (savedRoiRebind)
        validate_saved_roi_geometry_record(savedRoiRebind->record, savedRoiGeometryLimits());
      if (!savedRoiEdit.active() || !savedRoiMatches(owner.nativeSnapshot()))
        throw RevisionConflict();
      savedRoiEdit.working_limit(savedRoiGeometryLimits().max_working_bytes);
      savedRoiEdit.stage_numeric(value);
      const auto capture = savedRoiEdit.capture();
      const auto command = savedRoiCommand();
      owner.publishSavedRoiGeometry(capture, command, SavedRoiGeometryInteraction::Numeric);
      cancelSavedRoiGeometry("accepted");
    } catch (const std::exception &error) {
      // Cancellation closes the numeric panel before its local error can be
      // drawn. Preserve the failure in the root modal as well, so a fresh
      // publication-limit refusal remains visible after releasing the draft.
      lastError = error.what();
      cancelSavedRoiGeometry("publication_failed");
      throw;
    } catch (...) { cancelSavedRoiGeometry("publication_failed"); throw; }
  }
  ResolvedSavedRoiGeometryCommand savedRoiCommand() {
    savedRoiEdit.working_limit(savedRoiGeometryLimits().max_working_bytes);
    if (!savedRoiRebind) return savedRoiEdit.command();
    validate_saved_roi_geometry_record(savedRoiRebind->record, savedRoiGeometryLimits());
    if (!savedRoiEdit.candidate())
      throw std::invalid_argument("Enter a complete saved area replacement");
    return rebind_saved_roi_geometry(savedRoiEdit.source(), savedRoiRebind->record,
        SavedRoiGeometryRequest(savedRoiEdit.row_index(), *savedRoiEdit.candidate()),
        savedRoiEdit.limits());
  }
  void showSavedRoiRebind(const MetadataObject &record) {
    validate_saved_roi_geometry_record(record, savedRoiGeometryLimits());
    if (owner.busy()) throw std::invalid_argument("Wait for the current edit");
    const auto source = owner.nativeSnapshot();
    SavedRoiRebind next{record, source.id(), source.revisions(),
        {source.display().t, source.display().c, source.display().crosshair[2]}, {}};
    cancelSavedRoiGeometry("rebind_started");
    savedRoiRebind.emplace(std::move(next));
  }
  void drawSavedRoiRebind() {
    savedRoiRebindControls = Json::object();
    if (!savedRoiRebind || !savedRoiRebind->choosing) return;
    const auto source = owner.nativeSnapshot();
    if (owner.busy() || source.id() != savedRoiRebind->owner ||
        !savedRoiRebind->revisions.matches(source.revisions(), Change::Data)) {
      cancelSavedRoiGeometry("rebind_source_changed"); return;
    }
    if (savedRoiRebind->focus) ImGui::SetNextWindowFocus();
    ImGui::SetNextWindowSize({440, 410}, ImGuiCond_FirstUseEver);
    bool open = true, edit = false, canceled = false;
    ImGui::Begin("Rebind saved area", &open);
    savedRoiRebind->focus = false;
    ImGui::TextWrapped("Choose a destination saved area, then enter its complete replacement geometry.");
    ImGui::TextWrapped("Navigate to a positioned row before opening Rebind.");
    auto observe = [&](const std::string &key, bool enabled = true) {
      observeControl("saved_roi_geometry_rebind." + key, enabled);
      if (headlessDiagnostics)
        savedRoiRebindControls[key] = hostControls["saved_roi_geometry_rebind." + key];
    };
    if (ImGui::BeginChild("rows", {0, 230}, ImGuiChildFlags_Borders)) {
      for (std::size_t i = 0; i < source.data().rois.size(); ++i) {
        const auto &row = source.data().rois[i];
        const auto &position = savedRoiRebind->position;
        const bool enabled = selection_is_area(row.selection) &&
            (!row.t || *row.t == position.t) && (!row.c || *row.c == position.c) &&
            (!row.z || *row.z == position.z);
        const auto axis = [](const std::optional<int> &value) {
          return value ? std::to_string(*value + 1) : std::string("*");
        };
        const auto label = std::to_string(i + 1) + ". " + row.name + "  T:" + axis(row.t) +
                           " C:" + axis(row.c) + " Z:" + axis(row.z);
        ImGui::PushID(static_cast<int>(i));
        ImGui::BeginDisabled(!enabled);
        if (ImGui::Selectable(label.c_str(), savedRoiRebind->row == i)) savedRoiRebind->row = i;
        observe("row." + std::to_string(i), enabled);
        ImGui::EndDisabled(); ImGui::PopID();
      }
    }
    ImGui::EndChild();
    const bool canEdit = savedRoiRebind->row.has_value();
    ImGui::BeginDisabled(!canEdit);
    edit = ImGui::Button("Edit replacement"); observe("edit", canEdit);
    ImGui::EndDisabled(); ImGui::SameLine();
    canceled = ImGui::Button("Cancel"); observe("cancel");
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::IsKeyPressed(ImGuiKey_Escape)) canceled = true;
    ImGui::End();
    if (!open || canceled) { cancelSavedRoiGeometry("rebind_canceled"); return; }
    if (edit && savedRoiRebind->row) {
      // Preserve the original before begin() clears old frontend state. The
      // explicitly selected visible row causes no native navigation on entry.
      auto pending = *savedRoiRebind;
      const auto row = *pending.row;
      guard([&] {
        beginSavedRoiGeometryEdit(row);
        pending.choosing = pending.focus = false;
        savedRoiRebind.emplace(std::move(pending));
      });
    }
  }
  SavedRoiCanvasHooks savedRoiHooks(Pane &pane) {
    SavedRoiCanvasHooks hooks;
    if (!savedRoiEdit.active() || savedRoiEdit.source().id() != pane.doc->id)
      return hooks;
    hooks.edit = &savedRoiEdit;
    hooks.selected_vertex = &savedRoiVertex;
    hooks.accept_numeric = [this](const Selection &value) { applySavedRoiNumeric(value); };
    hooks.pointer_enabled = !owner.busy() &&
        owner.controller_->active_document_id() == pane.doc->id &&
        savedRoiMatches(pane.doc->nativeSnapshot());
    hooks.begin = [this](const SavedRoiCanvasTransform &transform,
                        SelectionHandle hit, std::array<double, 2> down) {
      try {
        if (!savedRoiMatches(owner.nativeSnapshot())) throw RevisionConflict();
        const auto row = savedRoiEdit.row_index();
        // This is the actual mouse-down capture. All Window activation changes
        // cancel the prior row draft; no release-time capture is permitted.
        const auto capture = owner.controller_->capture_saved_roi_geometry();
        const auto limits = savedRoiGeometryLimits();
        selectionPanel.cancel();
        savedRoiPanelShown = false;
        savedRoiEdit.begin_pointer(capture, row, transform, hit, down, limits);
        savedRoiVertex = hit.kind == SelectionHandleKind::Vertex
            ? std::optional<std::size_t>(hit.index) : std::nullopt;
      } catch (...) { cancelSavedRoiGeometry("pointer_begin_failed"); throw; }
    };
    hooks.update = [this](std::array<double, 2> point, SavedRoiEditModifiers modifiers,
                         const SavedRoiCanvasTransform &transform) {
      try {
        if (!savedRoiMatches(owner.nativeSnapshot())) throw RevisionConflict();
        savedRoiEdit.working_limit(savedRoiGeometryLimits().max_working_bytes);
        savedRoiEdit.update_pointer(point, modifiers, transform);
      } catch (...) { cancelSavedRoiGeometry("pointer_update_failed"); throw; }
    };
    hooks.release = [this](const SavedRoiCanvasTransform &transform) {
      try {
        if (!savedRoiEdit.pointer_active() || !savedRoiEdit.transform() ||
            !(*savedRoiEdit.transform() == transform)) throw RevisionConflict();
        if (!savedRoiEdit.pointer_valid()) {cancelSavedRoiGeometry("invalid_resize");return;}
        if (savedRoiEdit.candidate()) {
          const auto capture = savedRoiEdit.capture();
          const auto command = savedRoiCommand();
          owner.publishSavedRoiGeometry(capture, command, SavedRoiGeometryInteraction::Pointer);
          cancelSavedRoiGeometry("accepted");
        } else {
          // Clicking a vertex without a delivered drag selects that vertex for
          // Delete/nudge. It does not accept geometry or mint a new owner token.
          const auto capture = savedRoiEdit.capture();
          const auto row = savedRoiEdit.row_index();
          const auto limits = savedRoiGeometryLimits();
          savedRoiEdit.begin(capture, row, limits);
        }
      } catch (...) { cancelSavedRoiGeometry("pointer_publication_failed"); throw; }
    };
    hooks.cancel = [this] { cancelSavedRoiGeometry("canvas_canceled"); };
    return hooks;
  }
  void savedRoiOrdinaryInput(Pane &pane, const SavedRoiCanvasTransform &transform,
                             bool hovered) {
    auto hooks = savedRoiHooks(pane);
    if (!hooks.edit) return;
    auto &io = ImGui::GetIO();
    const std::array<double, 2> point{
        (double(io.MousePos.x) - transform.origin[0]) / transform.scale[0],
        (double(io.MousePos.y) - transform.origin[1]) / transform.scale[1]};
    const auto &context = *ImGui::GetCurrentContext();
    std::string blocked;
    if (!hooks.pointer_enabled) blocked = owner.busy() ? "busy" : "source_changed";
    else if (!pane.ordinaryCanvasVisible) blocked = "hidden";
    else if (io.AppFocusLost || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
      blocked = "focus_lost";
    else if (io.WantTextInput) blocked = "text_input";
    else if (browser.open || dialog.open || pendingClose || settingsPanel.visible() ||
             !lastError.empty() || ImGui::IsPopupOpen(nullptr,
                 ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) blocked = "modal";
    else if (context.CurrentItemFlags & ImGuiItemFlags_Disabled) blocked = "disabled";
    else if (context.ActiveId && context.ActiveId != pane.ordinaryCanvasId) blocked = "other_widget";
    const bool enabled = blocked.empty();
    const bool focused = pane.ordinaryCanvasVisible && context.NavId == pane.ordinaryCanvasId;
    const bool keyboard = enabled && focused && !io.KeyCtrl && !io.KeyAlt && !io.KeySuper;
    const std::array<ImGuiKey, 6> keys{ImGuiKey_LeftArrow, ImGuiKey_RightArrow,
        ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_Delete, ImGuiKey_Escape};
    const std::array<const char *, 6> names{"left", "right", "up", "down", "delete", "escape"};
    for (const auto key : keys) {
      const auto keyOwner = ImGui::GetKeyOwner(key);
      if (keyboard && (keyOwner == ImGuiKeyOwner_NoOwner || keyOwner == pane.ordinaryCanvasId))
        ImGui::SetKeyOwner(key, pane.ordinaryCanvasId, ImGuiInputFlags_LockThisFrame);
      else if (!keyboard && keyOwner == pane.ordinaryCanvasId)
        ImGui::SetKeyOwner(key, ImGuiKeyOwner_NoOwner);
    }
    auto &observed = pane.savedRoiInput;
    observed = {{"phase", enabled ? "idle" : "blocked"}, {"frame", ImGui::GetFrameCount()},
        {"canvas_id", pane.ordinaryCanvasId}, {"kind", "ordinary_xy"},
        {"row_index", savedRoiEdit.row_index()}, {"point", point},
        {"origin", transform.origin}, {"extent", transform.extent}, {"scale", transform.scale},
        {"admitted", enabled}, {"keyboard_admitted", keyboard}, {"focused", focused},
        {"blocked_reason", blocked}, {"active_id", context.ActiveId}, {"nav_id", context.NavId},
        {"shift", io.KeyShift}, {"ctrl", io.KeyCtrl}, {"alt", io.KeyAlt},
        {"mouse_down", ImGui::IsMouseDown(ImGuiMouseButton_Left)},
        {"mouse_pressed", ImGui::IsMouseClicked(ImGuiMouseButton_Left)},
        {"mouse_released", ImGui::IsMouseReleased(ImGuiMouseButton_Left)},
        {"mouse_delta", {io.MouseDelta.x, io.MouseDelta.y}},
        {"nudge", {0., 0.}}, {"delete", false}, {"key_owners", Json::object()}};
    for (std::size_t i = 0; i < keys.size(); ++i)
      observed["key_owners"][names[i]] = ImGui::GetKeyOwner(keys[i]);
    const auto pressed = [&](ImGuiKey key, bool repeat = true) {
      return keyboard && ImGui::IsKeyPressed(key,
          repeat ? ImGuiInputFlags_Repeat : ImGuiInputFlags_None, pane.ordinaryCanvasId);
    };
    try {
      if (pressed(ImGuiKey_Escape, false)) {
        observed["phase"] = "escape";
        cancelSavedRoiGeometry("escape"); return;
      }
      if (savedRoiEdit.pointer_active()) {
        if (!enabled || !savedRoiEdit.transform() ||
            !(*savedRoiEdit.transform() == transform) || io.MouseWheel ||
            ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
          observed["phase"] = "canceled";
          cancelSavedRoiGeometry("transform_changed"); return;
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
          observed["phase"] = "released"; hooks.release(transform);
        }
        else if (ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                 (io.MouseDelta.x || io.MouseDelta.y || savedRoiEdit.path_transform())) {
          observed["phase"] = "updated";
          hooks.update(point, {io.KeyShift, io.KeyCtrl, io.KeyAlt}, transform);
        } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
          observed["phase"] = "canceled"; cancelSavedRoiGeometry("capture_lost");
        }
        return;
      }
      if (hovered && io.MouseWheel && !io.KeyCtrl) {
        cancelSavedRoiGeometry("position_changed"); return;
      }
      if (!enabled || (!hovered && !keyboard)) return;
      const auto shape = savedRoiEdit.source().data().dataset.shape();
      const auto &selection = savedRoiEdit.candidate()
          ? *savedRoiEdit.candidate() : savedRoiEdit.original();
      const bool pointerRequest=hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
      const bool keyRequest=keyboard && !context.ActiveId &&
          (pressed(ImGuiKey_LeftArrow) || pressed(ImGuiKey_RightArrow) ||
           pressed(ImGuiKey_UpArrow) || pressed(ImGuiKey_DownArrow) || pressed(ImGuiKey_Delete));
      if(selection.is_path() && !pointerRequest && !keyRequest)return;
      const SelectionEditLimits limits{savedRoiEdit.limits().max_xy_pixels,
          savedRoiEdit.limits().max_vertices, savedRoiEdit.limits().max_working_bytes};
      SelectionEditor editor(selection, shape.x, shape.y, limits);
      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (io.KeyAlt && (selection.kind == "polygon" || selection.kind == "freehand")) {
          const auto index = editor.insertion_index(point,
              {1, transform.scale[1] / transform.scale[0]}, limits);
          observed["phase"] = "inserted_vertex"; observed["vertex_index"] = index;
          applySavedRoiNumeric(editor.inserted_vertex(index, point, limits));
          return;
        }
        const auto rotation = savedRoiEdit.rotation_handle(24 * guiScale, transform.scale);
        const auto hit = savedRoiEdit.hit_test(point,
            {6 * guiScale / transform.scale[0], 6 * guiScale / transform.scale[1]}, rotation);
        if (hit) {
          observed["phase"] = "began";
          observed["handle_kind"] = int(hit->kind); observed["handle_index"] = hit->index;
          hooks.begin(transform, *hit, point);
        }
      } else if (keyboard && !context.ActiveId) {
        const double step = io.KeyShift ? 10 : 1;
        const auto direction = [&](ImGuiKey increase, ImGuiKey decrease) {
          if (ImGui::IsKeyDown(increase, pane.ordinaryCanvasId) &&
              ImGui::IsKeyDown(decrease, pane.ordinaryCanvasId)) return 0.;
          return (pressed(increase) ? step : 0) - (pressed(decrease) ? step : 0);
        };
        const std::array<double, 2> delta{direction(ImGuiKey_RightArrow, ImGuiKey_LeftArrow),
                                         direction(ImGuiKey_DownArrow, ImGuiKey_UpArrow)};
        const bool remove = pressed(ImGuiKey_Delete);
        observed["nudge"] = delta; observed["delete"] = remove;
        observed["selected_vertex"] = savedRoiVertex ? Json(*savedRoiVertex) : Json();
        if (delta[0] || delta[1]) {
          if (savedRoiVertex) {
            observed["phase"] = "nudged_vertex";
            auto target = selectionControlPoint(selection,*savedRoiVertex);
            target[0] += delta[0]; target[1] += delta[1];
            applySavedRoiNumeric(editor.moved_vertex(*savedRoiVertex, target, limits));
          } else { observed["phase"] = "nudged"; applySavedRoiNumeric(editor.moved(delta, true, limits)); }
        } else if (remove && savedRoiVertex && !selection.is_path()) {
          observed["phase"] = "removed_vertex";
          applySavedRoiNumeric(editor.removed_vertex(*savedRoiVertex, limits));
        }
      }
    } catch (...) { cancelSavedRoiGeometry("input_failed"); throw; }
  }
  void drawSavedRoiHandles(Pane &pane, ImDrawList *draw) {
    pane.savedRoiHandleError.clear();
    if (!savedRoiEdit.active() || savedRoiEdit.source().id() != pane.doc->id) return;
    try {
    auto handles = savedRoiEdit.handles();
    if (const auto rotate = savedRoiEdit.rotation_handle(
            24 * guiScale, {pane.zoom, pane.zoom * pane.aspectY})) handles.push_back(*rotate);
    for (const auto &handle : handles) {
      const ImVec2 p{pane.origin.x + float(handle.position[0]) * pane.zoom,
                     pane.origin.y + float(handle.position[1]) * pane.zoom * pane.aspectY};
      const float radius = 3 * guiScale;
      const bool selected = savedRoiVertex && handle.kind == SelectionHandleKind::Vertex &&
                            *savedRoiVertex == handle.index;
      const auto tint = selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 213, 75, 255);
      if (handle.kind == SelectionHandleKind::Rotate) {
        draw->AddCircleFilled(p, radius + 1, IM_COL32(20, 20, 20, 255));
        draw->AddCircle(p, radius + 1, tint);
      } else {
        draw->AddRectFilled({p.x - radius, p.y - radius}, {p.x + radius, p.y + radius},
                            IM_COL32(20, 20, 20, 255));
        draw->AddRect({p.x - radius, p.y - radius}, {p.x + radius, p.y + radius},
                            tint);
      }
      if (headlessDiagnostics)
        pane.savedRoiHandles.push_back({{"kind", int(handle.kind)}, {"index", handle.index},
            {"image_point", handle.position}, {"screen_point", {p.x, p.y}},
            {"row_index", savedRoiEdit.row_index()}, {"canvas_id", pane.ordinaryCanvasId},
            {"gui_scale",guiScale},{"radius_device",radius+(handle.kind==SelectionHandleKind::Rotate?1:0)},{"shape",handle.kind==SelectionHandleKind::Rotate?"circle":"square"},{"bounds_hit_tolerance_device",6*guiScale},
            {"selected", selected}});
    }
    } catch(const std::exception &error) {
      if(!savedRoiEdit.active() || !savedRoiEdit.original().is_path())throw;
      pane.savedRoiHandleError=error.what();
    }
  }
  void cancelCreation(Pane &pane, const char *reason) {
    if (!pane.creation) return;
    pane.creation.reset();
    pane.transientOutline.reset(); pane.transientVisual.reset();
    pane.transientStyle = Json::object();
    creationLastCancel = reason;
  }
  void cancelCreations(const char *reason) {
    pathAuthoring.cancel(reason);
    for (auto &pane : panes) cancelCreation(pane, reason);
  }
  void publishAuthoredPath(const DocumentSnapshot &source,const Selection &selection) {
    const auto current=owner.nativeSnapshot();
    if(owner.busy() || !source.same_document(current) || !source.revisions().matches(current.revisions(),Change::Data))
      throw RevisionConflict();
    if(owner.documentById(source.id())->scientific())scientificRoiCreationOwner=source.id();
    const SelectionCreationRequest request{SelectionCreationOperation::Replace,selection,SelectionCreationGeometry::NativePathV4};
    if(creationRebind) {
      if(creationRebindOwner!=source.id())throw RevisionConflict();
      owner.replaySelectionCreation(*creationRebind,request,source.id());creationRebind.reset();creationRebindOwner=0;
    } else owner.applySelectionCreation(source,request);
  }
  void updateCreationPreview(Pane &pane) {
    auto &draft = *pane.creation;
    std::optional<Selection> candidate;
    if (draft.box) candidate = selection_creation_box_candidate(*draft.box);
    else if (draft.points.size() >= 3) {
      std::array<double, 2> low = draft.points.front(), high = low;
      for (const auto &p : draft.points)
        for (int axis = 0; axis < 2; ++axis) {
          low[axis] = std::min(low[axis], p[axis]);
          high[axis] = std::max(high[axis], p[axis]);
        }
      if (high[0] > low[0] && high[1] > low[1])
        candidate.emplace(draft.tool, draft.points, draft.width);
    }
    if (!candidate) {
      draft.candidate.reset(); draft.preview.reset();
      draft.previewGeometry = Json::object();
      draft.candidateGeometry = Json::object(); draft.retained = 0;
      return;
    }
    candidate->width = draft.width;
    if (draft.tool == "rounded_rectangle") {
      candidate->kind = "rounded_rectangle";
      candidate->attributes["corner_diameter"] = std::int64_t(draft.cornerDiameter);
    }
    SelectionCreationRequest request{draft.operation, std::move(*candidate), draft.geometry};
    const auto limits = selectionCreationLimits();
    const auto estimate = estimate_selection_creation(draft.source, request, limits);
    if (draft.retained > limits.max_working_bytes ||
        estimate.working_bytes > limits.max_working_bytes - draft.retained)
      throw std::invalid_argument("Selection preview exceeds the working memory limit");
    auto preview = compose_selection_creation(draft.source, request, limits);
    auto rendered = selectionJson(preview);
    auto candidateRendered = selectionJson(request.candidate);
    draft.candidate = std::move(request.candidate);
    draft.preview = std::move(preview);
    draft.previewGeometry = std::move(rendered);
    draft.candidateGeometry = std::move(candidateRendered);
    draft.retained = estimate.retained_bytes;
  }
  void beginCreation(Pane &pane, std::array<double, 2> point,
                     bool shift, bool alt) {
    const auto source = pane.doc->nativeSnapshot();
    const auto shape = source.data().dataset.shape();
    const auto limits = selectionCreationLimits();
    if (static_cast<std::size_t>(shape.x) > limits.max_pixels /
                                                   static_cast<std::size_t>(shape.y))
      throw std::invalid_argument("Selection canvas exceeds the pixel limit");
    auto mode = creationMode;
    const auto &base = source.data().selection;
    if (base) {
      if (shift) mode = SelectionCreationOperation::Add;
      else if (alt) mode = SelectionCreationOperation::Subtract;
    }
    if (mode != SelectionCreationOperation::Replace && base && !creationArea(*base))
      throw std::invalid_argument("Selection combinations require an area base");
    if (!base && mode != SelectionCreationOperation::Replace &&
        mode != SelectionCreationOperation::Add)
      throw std::invalid_argument("This selection mode requires an area base");
    if (creationRebind && creationRebindOwner != source.id())
      throw std::invalid_argument("Selection rebind belongs to another document");
    Pane::CreationGesture draft(source);
    draft.position = pane.doc->position(); draft.tool = tool;
    draft.operation = mode; draft.down = point;
    draft.geometry = base && base->is_path()?SelectionCreationGeometry::NativePathV4:
        creationFullGeometry ? SelectionCreationGeometry::FullRegionV3 : creationGeometry(creationRebind);
    draft.width = preferenceState.settings.number("line_width");
    draft.cornerDiameter = int(preferenceState.settings.integer("rounded_corner_diameter"));
    draft.origin = pane.origin; draft.extent = pane.extent;
    draft.zoom = pane.zoom; draft.aspect = pane.aspectY;
    draft.canvasId = pane.ordinaryCanvasId;
    draft.rebind = creationRebind;
    if (tool == "rounded_rectangle" || tool == "rectangle" || tool == "ellipse") {
      draft.box = begin_selection_creation_box(
          tool == "ellipse" ? SelectionCreationBoxKind::Ellipse
                            : SelectionCreationBoxKind::Rectangle,
          shape.x, shape.y, {static_cast<int>(point[0]), static_cast<int>(point[1])});
      draft.dragging = true;
    } else {
      draft.points.push_back(point);
      draft.dragging = tool == "freehand";
    }
    cancelCreation(pane, "new_gesture");
    pane.creation.emplace(std::move(draft));
    creationLastCancel.clear();
  }
  void finishCreation(Pane &pane) {
    if (!pane.creation) return;
    auto draft = std::move(*pane.creation);
    cancelCreation(pane, "");
    if (!draft.candidate) return;
    SelectionCreationRequest request{draft.operation, std::move(*draft.candidate), draft.geometry};
    // Release the old preview before resolving/preparing the final action.
    draft.preview.reset(); draft.previewGeometry = Json::object();
    draft.candidateGeometry = Json::object();
    std::vector<std::array<double, 2>>().swap(draft.points);
    const auto current = pane.doc->nativeSnapshot();
    if (!draft.source.same_document(current) ||
        !draft.source.revisions().matches(current.revisions(), Change::Data))
      throw RevisionConflict();
    if (draft.rebind)
      owner.replaySelectionCreation(*draft.rebind, request, draft.source.id());
    else owner.applySelectionCreation(draft.source, request);
    creationRebind.reset(); creationRebindOwner = 0;
  }
  Json creationDiagnostics() const {
    Json result{{"mode", creationModeName(creationMode)},
                {"admission", creationAdmission}, {"last_cancel", creationLastCancel},
                {"rebind", creationRebind ? Json{{"record", metadataJson(*creationRebind)},
                    {"document_id", creationRebindOwner}} : Json(nullptr)},
                {"draft", nullptr}};
    Json retainedOwners = Json::array();
    std::size_t retainedBytes = 0;
    for (const auto &pane : panes)
      if (pane.creation) {
        retainedOwners.push_back(pane.creation->source.id());
        retainedBytes += pane.creation->retained;
      }
    result["retained_draft_count"] = retainedOwners.size();
    result["total_retained_bytes"] = retainedBytes;
    result["retained_owner_ids"] = std::move(retainedOwners);
    if (owner.documents_.empty()) return result;
    const auto &pane = panes[owner.activeIndex()];
    if (!pane.creation) return result;
    const auto &d = *pane.creation;
    const auto r = d.source.revisions();
    result["draft"] = {{"document_id", d.source.id()}, {"tool", d.tool},
        {"operation", creationModeName(d.operation)},
        {"position", {{"t", d.position.t}, {"c", d.position.c}, {"z", d.position.z}}},
        {"source_revisions", {{"pixels", r.pixels}, {"metadata", r.metadata},
                               {"selection", r.selection}, {"display", r.display}}},
        {"source_selection", d.source.data().selection
            ? selectionJson(*d.source.data().selection) : Json(nullptr)},
        {"starting_pointer", d.down}, {"points", d.points},
        {"candidate", d.candidate ? selectionJson(*d.candidate) : Json(nullptr)},
        {"preview", d.preview ? d.previewGeometry : Json(nullptr)},
        {"canvas_origin", {d.origin.x, d.origin.y}},
        {"canvas_scale", {d.zoom, d.zoom * d.aspect}},
        {"canvas_extent", {d.extent.x, d.extent.y}}, {"canvas_id", d.canvasId},
        {"dragging", d.dragging}, {"retained_bytes", d.retained}};
    return result;
  }
  void applyChannelPanel(const DocumentSnapshot &source,
                         const ChannelDisplayRequest &request) {
    if (owner.documents_.empty() || doc()->id != source.id())
      throw std::invalid_argument("Channel controls require their captured active document");
    const auto current = owner.nativeSnapshot(source.id());
    if (!source.same_document(current) ||
        !source.revisions().matches(current.revisions(), Change::Pixels | Change::Metadata))
      throw RevisionConflict();
    if (channelDisplayRebind) {
      if (!channelDisplayRebindOwner || *channelDisplayRebindOwner != source.id())
        throw std::invalid_argument("Rebind belongs to the document selected when it opened");
      owner.replayChannelDisplay(*channelDisplayRebind, request, source.id());
      channelDisplayRebind.reset(); channelDisplayRebindOwner.reset();
    } else owner.applyChannelDisplay(source, request);
  }
  std::optional<DocumentSnapshot>
  planeOperationPreview(const Document &document, bool captured_only = true) const {
    const auto rgbLive=document.nativeSnapshot();
    if(auto rgb=contrastPanel.rgbPreview(rgbLive)){
      const auto &v=rgbLive.display();
      if(!captured_only || contrastPanel.rgbPreviewAffects(rgbLive,{v.t,v.c,v.crosshair[2]}))return rgb;
      return std::nullopt; // The active RGB tool owns preview selection, including excluded planes.
    }
    if (shadowDemo.active()) return shadowDemo.preview(document.nativeSnapshot());
    auto preview = gaussianJobs.preview_snapshot(document.id);
    const auto *target = gaussianJobs.preview_target(document.id);
    if (!preview || !target)
      return std::nullopt;
    const auto live = document.nativeSnapshot();
    if (!preview->same_document(live) ||
        !live.revisions().matches(preview->revisions(), Change::Data) ||
        preview->data().dataset.shape().dimensions() != live.data().dataset.shape().dimensions() ||
        preview->data().dataset.pixel_type() != live.data().dataset.pixel_type())
      return std::nullopt;
    const auto &display = live.display();
    if (captured_only &&
        !target->contains({display.t, display.c, display.crosshair[2]}))
      return std::nullopt;
    return preview;
  }
  std::uint64_t operationPreviewGeneration(const Document &document) const {
    if(contrastPanel.rgbPreview(document.nativeSnapshot()))return contrastPanel.rgbPreviewGeneration();
    return shadowDemo.preview(document.nativeSnapshot()) ? shadowDemo.generation() : gaussianJobs.preview_generation(document.id).value_or(0);
  }
  const ResolvedTarget *operationPreviewTarget(const Document &document) const {
    if(const auto *target=contrastPanel.rgbPreviewTarget(document.nativeSnapshot()))return target;
    return shadowDemo.preview(document.nativeSnapshot()) ? shadowDemo.target() : gaussianJobs.preview_target(document.id);
  }
  void showPlaneOperationPreview(PlanePosition position) {
    auto display = doc()->view();
    display.t = position.t;
    display.c = position.c;
    display.crosshair[2] = position.z;
    doc()->setNativeDisplay(std::move(display));
  }
  bool planeOperationPreviewVisible(PlanePosition position) const {
    if (owner.documents_.empty())
      return false;
    const auto canvas = doc()->position();
    return canvas.t == position.t && canvas.c == position.c &&
           canvas.z == position.z && planeOperationPreview(*doc()).has_value();
  }
  bool captureGaussianReadback() const {
    return headlessDiagnostics &&
           (gaussianReadbackRequested || headlessSessionReadback || channelDisplayPanel.visible());
  }
  void clearGaussianReadbacks() {
    if(viewportJobs)viewportJobs->cancelAll();
    for(auto &pane:panes){pane.sampledSurface.reset();pane.sampleMemory.reset();pane.viewportPending=false;}
    headlessDiagnostics = false;
    for (auto &pane : panes) {
      pane.sourceReadback.reset();
      pane.renderedReadback.reset();
      pane.readbackMemory.reset();
      pane.sourceReadbackPolicy = FloatingSamplePolicy::FiniteOnly;
      pane.orthogonal.clearReadback();
    }
  }
  Json ordinaryPixels(const Pane &pane, bool readback = false) const {
    Json result{{"pixels_source", pane.previewActive ? "preview" : "live"},
                {"preview_generation", pane.previewGeneration},
                {"source_document_id", pane.doc->id},
                {"pixel_builds", pane.pixelBuilds},
                {"diagnostic_rebuilds", pane.diagnosticRebuilds},
                {"current_tcz", {pane.pixelsPosition.t, pane.pixelsPosition.c,
                                  pane.pixelsPosition.z}},
                {"range", displayRangeValue(pane.effectiveRange)},
                {"range_tcz", {pane.rangePosition.t, pane.rangePosition.c,
                                pane.rangePosition.z}},
                {"contrast_source", pane.previewDisplayKey.value("contrast_source", "live")},
                {"threshold_source", pane.previewDisplayKey.value("threshold_source", "live")},
                {"preview_display_effects", pane.previewDisplayKey.value("effects", Json::array())}};
    if (!pane.channelDisplayReadback.is_null())
      result["channel_display"] = pane.channelDisplayReadback;
    if (readback)
      result["readback"] = {
          {"source_tcz", {pane.pixelsPosition.t, pane.pixelsPosition.c,
                           pane.pixelsPosition.z}},
          {"available", pane.sourceReadback.has_value() && pane.renderedReadback.has_value()},
          {"source", pane.sourceReadback ? imageReadback(*pane.sourceReadback, pane.sourceReadbackPolicy) : Json()},
          {"rendered_rgb", pane.renderedReadback ? imageReadback(*pane.renderedReadback, FloatingSamplePolicy::FiniteOnly) : Json()}};
    return result;
  }
  void mirrorGaussianIds() {
    const auto ids = gaussianJobs.active_job_ids();
    std::lock_guard<std::mutex> lock(jobMutex);
    gaussianCancelIds = ids;
  }
  void drainGaussianCancellation() {
    bool requested;
    {
      std::lock_guard<std::mutex> lock(jobMutex);
      requested = gaussianDrainedEpoch != gaussianCancelEpoch;
      gaussianDrainedEpoch = gaussianCancelEpoch;
    }
    if (requested)
      gaussianJobs.cancel_all();
    mirrorGaussianIds();
  }
  std::uint64_t beginGaussianAdmission() {
    for (;;) {
      drainGaussianCancellation();
      std::lock_guard<std::mutex> lock(jobMutex);
      if (gaussianDrainedEpoch == gaussianCancelEpoch)
        return gaussianCancelEpoch;
    }
  }
  void finishGaussianAdmission(std::uint64_t epoch) {
    const auto ids = gaussianJobs.active_job_ids();
    bool canceled;
    {
      std::lock_guard<std::mutex> lock(jobMutex);
      gaussianCancelIds = ids;
      canceled = epoch != gaussianCancelEpoch;
    }
    // A cancellation during submission can precede publication of these IDs.
    // Native cancellation is safe off-thread; host payload cleanup is not.
    if (canceled) {
      for (const auto id : ids)
        if (id)
          gaussianCancellationScheduler->cancel(id);
      drainGaussianCancellation();
    }
  }
  void cancelGaussianPreview() {
    gaussianJobs.cancel_preview();
    mirrorGaussianIds();
  }
  void probeGaussian(const char *phase,
                     std::optional<std::uint64_t> admission = std::nullopt) {
    const auto probe = gaussianHostProbe;
    if (!probe)
      return;
    GaussianHostProbeEvent event;
    event.phase = phase;
    {
      std::lock_guard<std::mutex> lock(jobMutex);
      event.current_epoch = gaussianCancelEpoch;
      event.drained_epoch = gaussianDrainedEpoch;
      event.admission_epoch = admission.value_or(gaussianCancelEpoch);
      event.mirrored_ids = gaussianCancelIds;
    }
    event.actual_lane_ids = gaussianJobs.active_job_ids();
    // A diagnostic may wait here; no host, application or job lock is held.
    (*probe)(event);
  }
  void requestGaussian(const ResolvedGaussianBlurCommand &command,
                       std::optional<std::uint64_t> generation = std::nullopt) {
    const auto source = owner.documentById(command.target().document_id());
    if (!gaussianSourceAllowed(*source) || owner.busy())
      throw std::invalid_argument(
          "Gaussian requires an idle active UInt8 gray/RGB, UInt16 gray or Float32 gray document");
    requestPlaneFilter(command, generation, "Gaussian", "Processing Gaussian blur...");
  }
  void requestRank(const ResolvedRankFilterCommand &command,
                   std::optional<std::uint64_t> generation = std::nullopt) {
    const auto source = owner.documentById(command.target().document_id());
    if (!(command.remove_nans_options() ? removeNaNsSourceAllowed(*source) : rankSourceAllowed(*source)) || owner.busy())
      throw std::invalid_argument(
          "Rank filtering requires an idle active finite UInt8, UInt16 or Float32 scalar document");
    requestPlaneFilter(command, generation, "Rank", "Processing rank filter...");
  }
  void rankCallback(const char *endpoint, std::uint64_t source) {
    if (!rankProbe) return;
    const auto stamp = rankRuntimeClockStamp();
    rankProbe->event("callback", {{"endpoint", endpoint},
        {"source_id", source}, {"steady_ns", stamp.steady_ns},
        {"clock", rankRuntimeClockJson(stamp)}});
  }
  Json rankCancelFields(std::uint64_t id, const RankRuntimeClockStamp &stamp) {
    const auto diagnostics = metadataJson(gaussianJobs.diagnostics());
    Json result{{"job_id", id}, {"steady_ns", stamp.steady_ns},
                {"clock", rankRuntimeClockJson(stamp)},
                {"usage", diagnostics.at("resource_usage")}};
    for (const auto *role : {"preview", "apply"}) {
      const auto &lane = diagnostics.at(role);
      const auto &job = lane.at("job");
      if (job.is_object() && job.at("id") == id) {
        result["job"] = job;
        result["source_id"] = lane.at("source_id");
        result["role"] = role;
        return result;
      }
    }
    result["job"] = nullptr;
    result["source_id"] = 0;
    return result;
  }
  template <class Command>
  void requestPlaneFilter(const Command &command,
                           std::optional<std::uint64_t> generation,
                           const char *name, const char *applyStatus) {
    if (command.target().execution_mode() != ExecutionMode::Planes)
      throw std::invalid_argument(std::string(name) + " desktop targets require XY-plane execution");
    if constexpr (std::is_same_v<Command, ResolvedGaussianBlurCommand>) {
      if (command.target().destination() != OutputDestination::InPlace)
        throw std::invalid_argument("Gaussian desktop Apply requires an in-place target");
    } else if (generation && command.target().destination() != OutputDestination::InPlace) {
      throw std::invalid_argument("Rank preview requires an in-place target");
    }
    owner.controller_->validate_target(command.target());
    if (!generation && gaussianApplyAttempt == std::numeric_limits<std::uint64_t>::max())
      throw std::overflow_error(std::string(name) + " Apply request counter exhausted");
    const auto epoch = beginGaussianAdmission();
    gaussianAdmissionEpoch = epoch;
    probeGaussian("admission_before_submit", epoch);
    if (!generation) ++gaussianApplyAttempt;
    bool admitted;
    try {
      admitted = generation ? gaussianJobs.request_preview(command, *generation)
                            : gaussianJobs.request_apply(command);
      if constexpr (std::is_same_v<Command, ResolvedRankFilterCommand>) {
        if (rankProbe && admitted) {
          const auto stamp = rankRuntimeSteadyNanoseconds();
          const auto ids = gaussianJobs.active_job_ids();
          rankProbe->event("submitted", {{"role", generation ? "preview" : "apply"},
              {"job_id", ids[generation ? 0 : 1]},
              {"generation", generation ? Json(*generation) : Json()},
              {"source_id", command.target().document_id()}, {"steady_ns", stamp}});
        }
      }
    } catch (...) {
      if (!generation) gaussianReportedApplyAttempt = gaussianApplyAttempt;
      throw;
    }
    probeGaussian("admission_after_submit", epoch);
    finishGaussianAdmission(epoch);
    if (!admitted) {
      // The caller observes this exception now. A later preview must not
      // resurrect the prior Apply failure after its error has been cleared.
      if (!generation) gaussianReportedApplyAttempt = gaussianApplyAttempt;
      const auto info = metadataJson(gaussianJobs.diagnostics());
      throw std::runtime_error(string(get(info, "request_error")));
    }
    animating = false;
    status = generation ? std::string("Preparing ") + name + " preview..." : applyStatus;
  }
  ManualThresholdLimits thresholdLimits() const {
    ManualThresholdLimits limits;
    const auto configured = static_cast<std::size_t>(
        preferenceState.settings.integer("processing_memory_mb")) * 1024 * 1024;
    if (configured)
      limits.max_working_bytes = std::min(limits.max_working_bytes, configured);
    return limits;
  }
  std::optional<DocumentSnapshot> thresholdCurrent() const {
    if (!thresholdSource)
      return std::nullopt;
    try {
      return owner.nativeSnapshot(thresholdSource->id());
    } catch (const DocumentUnavailable &) {
      return std::nullopt;
    } catch (const std::out_of_range &) {
      return std::nullopt;
    }
  }
  DocumentSnapshot checkedThresholdSource() const {
    if (!thresholdSource)
      throw std::invalid_argument("Open a manual threshold source first");
    const auto current = owner.nativeSnapshot(thresholdSource->id());
    if (!current.same_document(*thresholdSource) ||
        !current.revisions().matches(thresholdSource->revisions(), Change::Data))
      throw RevisionConflict();
    return *thresholdSource;
  }
  PlanePosition thresholdPosition() const {
    const auto &view = thresholdSource->display();
    return {view.t, view.c, view.crosshair[2]};
  }
  void thresholdSet(ManualThresholdRange range) {
    if (owner.busy())
      throw std::invalid_argument("Wait for the current edit");
    const auto source = checkedThresholdSource();
    auto display = owner.nativeSnapshot(source.id()).display();
    set_manual_threshold_state(display, source.data().dataset.shape(),
                                thresholdPanel.displayState(range));
    owner.setNativeDisplay(std::move(display), source.id());
  }
  DatasetHistogramLimits histogramLimits() const {
    DatasetHistogramLimits limits;const auto cap=static_cast<std::size_t>(preferenceState.settings.integer("processing_memory_mb"))*1024*1024;
    if(cap)limits.max_working_bytes=std::min(limits.max_working_bytes,cap);return limits;
  }
  void beginHistogram(bool remember=true,std::optional<HistogramPanel> replacement={}) {
    if(owner.busy())throw std::invalid_argument("Wait for the current edit");
    const auto epoch=histogramCancelEpoch.load();const bool recording=owner.recording;
    const auto &draft=replacement?*replacement:histogramPanel;
    const auto captured=draft.capture(owner.nativeSnapshot(),false);
    HistogramCommandLimits cap;cap.processing=histogramLimits();
    JobOptions options;options.label="Histogram";options.supersedes="histogram.panel";
    auto submitted=submit_histogram(*owner.controller_->job_scheduler(),captured,cap,std::move(options));
    const auto id=submitted.info().id;
    auto pending=std::make_shared<HistogramTask>(HistogramTask{std::move(submitted),captured,draft.generation(),recording,remember,{}});
    static_assert(std::is_nothrow_move_assignable_v<HistogramPanel>);
    if(replacement)histogramPanel=std::move(*replacement);
    histogramPanel.actionError({});histogramTask=std::move(pending);
    histogramCancelId.store(id);
    if(histogramCancelEpoch.load()!=epoch)histogramTask->job.cancel();
    animating=false;
  }
  void cancelHistogram(){if(histogramTask){histogramTask->job.cancel();if(histogramTask->job.ready())pollHistogram();}}
  void pollHistogram() {
    if(!histogramTask)return;
    const auto pending=histogramTask;
    bool valid=histogramPanel.generation()==pending->generation;
    try {
      const auto current=owner.nativeSnapshot();
      valid=valid && current.id()==pending->command.target().document_id() &&
          current.revisions().matches(pending->command.target().source_revisions(),Change::Data);
    }catch(const std::exception &){valid=false;}
    if(!valid)pending->job.cancel();
    if(!pending->job.ready())return;
    auto terminal=std::make_shared<JobInfo>(pending->job.info());
    try {
      const auto result=pending->job.result();
      // All result copies/formatting are staged before preference/workflow effects.
      pending->job.complete([&](const TargetHistogramResult &){
        if(histogramPanel.generation()!=pending->generation)throw std::invalid_argument("Histogram draft changed before publication");
        histogramPanel.accept(pending->command,*result,[&](const SettingsPatch &values,const MetadataObject &record){
          publishHistogram(values,record,pending->remember,pending->recording);
        });
      });
      terminal->state=JobState::Completed;
    } catch(const OperationCancelled &) {
      terminal->state=JobState::Canceled;
    } catch(const std::exception &error) {
      pending->error=error.what();lastError=pending->error;histogramPanel.actionError(pending->error);
      *terminal=pending->job.info();
    }
    histogramLast=std::move(terminal);histogramCancelId.store(0);histogramTask.reset();
  }
  Json histogramDiagnostics() const {
    const auto info=histogramTask?std::optional<JobInfo>{histogramTask->job.info()}:histogramLast?std::optional<JobInfo>{*histogramLast}:std::nullopt;
    Json out={{"active",bool(histogramTask)}};
    if(info)out["job"]={{"id",info->id},{"document_id",info->document_id},{"state",jobStateName(info->state)},
      {"completed",info->completed},{"total",info->total},{"stage",info->stage},{"stage_completed",info->stage_completed},{"stage_total",info->stage_total},
      {"cancel_requested",info->cancel_requested},{"resource_bytes",info->resource_bytes},{"error",info->error}};
    else out["job"]=nullptr;
    return out;
  }
  void publishHistogram(const SettingsPatch &values,const MetadataObject &record,bool remember=true,std::optional<bool> recorded={}) {
    HistogramCommandLimits cap;cap.processing=histogramLimits();
    (void)histogram_command_from_record(owner.nativeSnapshot(),record,cap);
    OutputPublication outputs(owner.results,owner.workflow,{},"",recorded.value_or(owner.recording)?metadataJson(record):Json(nullptr));
    if(remember)rememberHistogram(values);
    outputs.publish();
  }
  void rememberHistogram(const SettingsPatch &values) {
    auto remembered=preferenceState;remembered.settings=owner.settings();remembered.extras=preferences;
    remembered.settings.update(values);remembered.save(preferencesPath);swapPreferences(preferenceState,remembered);
  }
  AutomaticThresholdDisplayResult thresholdAutomatic(const std::string &method,bool bright,bool black,bool white,bool pooled) {
    if(owner.busy())throw std::invalid_argument("Wait for the current edit");
    const auto source=checkedThresholdSource();const auto live=owner.nativeSnapshot(source.id());
    if(!live.revisions().matches(source.revisions(),Change::Data))throw RevisionConflict();
    const auto result=[&] {
      if(!pooled && !thresholdPanel.scientificTargets() && !thresholdPanel.histogramUsesRoi())return automatic_threshold_display(source.data().dataset,thresholdPosition(),method,0,bright,black,white,thresholdLimits());
      auto request=thresholdPanel.targetRequest();
      if(!thresholdPanel.scientificTargets()){request.scope=pooled?TargetScope::AllZ:TargetScope::Positions;request.positions=pooled?std::vector<PlanePosition>{}:std::vector<PlanePosition>{thresholdPosition()};}
      if(!thresholdPanel.histogramUsesRoi())request.roi=RoiPolicy::WholePlane;
      const auto target=resolve_target(source,request,"threshold");
      auto result=pooled_automatic_threshold_display(target,method,0,bright,black,white,thresholdLimits());
      result.stack_histogram=pooled;return result;
    }();
    auto display=live.display();set_manual_threshold_state(display,source.data().dataset.shape(),thresholdPanel.scientificTargets()?thresholdPanel.displayState(result.range):ManualThresholdState{result.range,thresholdPosition(),pooled});
    auto remembered = preferenceState;
    remembered.settings = owner.settings();
    remembered.extras = preferences;
    remembered.settings.update({{"threshold_method", method},
        {"threshold_bright", bright}, {"threshold_ignore_black", black},
        {"threshold_ignore_white", white},{"threshold_stack_histogram",pooled},{"threshold_histogram_roi",thresholdPanel.histogramUsesRoi()}});
    if(thresholdPanel.scientificTargets()){const auto roi=thresholdPanel.targetRequest().roi;remembered.settings.set("threshold_apply_roi",std::string(roi==RoiPolicy::WholePlane?"whole_plane":roi==RoiPolicy::ActiveSelection?"active_selection":"positioned_rois"));}
    // Implicit saves preserve newer/corrupt-file write protection. A disk
    // failure leaves both the display and remembered settings unchanged.
    remembered.save(preferencesPath);
    owner.controller_->set_display_if_current(live,std::move(display));
    swapPreferences(preferenceState, remembered);
    return result;
  }
  void thresholdReset() {
    if (owner.busy())
      throw std::invalid_argument("Wait for the current edit");
    const auto source = checkedThresholdSource();
    auto display = owner.nativeSnapshot(source.id()).display();
    if (display.properties.erase(manual_threshold_property))
      owner.setNativeDisplay(std::move(display), source.id());
  }
  void thresholdCancel() {
    const auto current = thresholdCurrent();
    if (!current) {
      thresholdSource.reset();
      thresholdOpeningState.reset();
      return;
    }
    auto display = current->display();
    if (current->revisions().matches(thresholdSource->revisions(),
                                      Change::Pixels | Change::Metadata) &&
        thresholdOpeningState)
      set_manual_threshold_state(display, current->data().dataset.shape(),
                                  *thresholdOpeningState);
    else
      reset_manual_threshold_state(display);
    owner.setNativeDisplay(std::move(display), current->id());
    thresholdSource.reset();
    thresholdOpeningState.reset();
  }
  void thresholdApplyPreview() {
    const auto source = checkedThresholdSource();
    const auto shape = source.data().dataset.shape();
    const auto request=thresholdPanel.targetRequest();const auto target=resolve_target(source,request,"threshold");
    bool complete=target.position_count()==shape.plane_count();
    const bool conditional=complete && request.roi==RoiPolicy::PositionedRois && source.data().dataset.pixel_type()!=PixelType::UInt8;
    if(conditional)complete=false;
    if(complete && request.roi==RoiPolicy::ActiveSelection && source.data().selection){const auto mask=source.data().selection->mask(shape.x,shape.y);complete=std::all_of(mask.pixels.begin(),mask.pixels.end(),[](double v){return v!=0;});}
    const std::string roi=request.roi==RoiPolicy::WholePlane?"whole plane":request.roi==RoiPolicy::PositionedRois?"positioned ROIs":source.data().selection?"active selection":"whole plane (no selection)";
    const auto extent=thresholdPanel.scientificTargets()?std::to_string(target.position_count())+" frozen XY planes; "+roi:
        "Captured T "+std::to_string(thresholdPosition().t+1)+", C "+std::to_string(thresholdPosition().c+1)+(thresholdPanel.stackHistogram()?", all Z":", Z "+std::to_string(thresholdPosition().z+1))+"; "+roi;
    thresholdPanel.setApplyPreview(complete?PixelType::UInt8:source.data().dataset.pixel_type(),extent,conditional);
  }
  void showThreshold(std::uint64_t id = 0) {
    if (owner.busy())
      throw std::invalid_argument("Wait for the current edit");
    auto source = owner.nativeSnapshot(id);
    const auto shape = source.data().dataset.shape();
    (void)resolve_manual_threshold_command(source, {0, 0});
    const PlanePosition position{source.display().t, source.display().c,
                                  source.display().crosshair[2]};
    estimate_manual_threshold(shape, source.data().dataset.pixel_type(),
                               position, thresholdLimits());
    const auto plane = source.data().dataset.plane(position);
    auto summary = analyze_manual_threshold(*plane,
        {-std::numeric_limits<double>::max(), std::numeric_limits<double>::max()},
        thresholdLimits());
    thresholdPanel.cancel([this] { thresholdCancel(); });
    source = owner.nativeSnapshot(id);
    thresholdSource = source;
    thresholdOpeningState = manual_threshold_state(source.display(), shape);
    thresholdPanel.show(source, std::move(summary));
    thresholdPanel.setAutomaticOptions(owner.settings());
    thresholdApplyPreview();
    headlessSessionReadback = true;
    animating = false;
  }
  void thresholdToSelection(ManualThresholdRange range) {
    if (owner.busy())
      throw std::invalid_argument("Wait for the current edit");
    const auto source = checkedThresholdSource();
    const auto plane = source.data().dataset.plane(thresholdPosition());
    auto selection = manual_threshold_selection(*plane, range, thresholdLimits());
    if (!source.data().selection || selection != *source.data().selection) {
      auto edit = source.begin_edit(Change::Data);
      edit.select(std::move(selection));
      if (owner.documentById(source.id())->scientific()) owner.controller_->commit(edit);
      else owner.commitEdit(edit);
      thresholdSource = owner.nativeSnapshot(source.id()).with_display(source.display());
      thresholdPanel.rebase(*thresholdSource);
      thresholdApplyPreview();
    }
  }
  void thresholdApply(ManualThresholdRange range) {
    if (owner.busy())
      throw std::invalid_argument("Wait for the current edit");
    const auto cancelEpoch = thresholdCancelEpoch.load();
    const auto source = checkedThresholdSource();
    range.validate();
    const auto request=thresholdPanel.targetRequest();
    const auto command = resolve_manual_threshold_command(source, range, request);
    Json step = owner.recording ? Json{{"operation", "threshold"},
        {"parameters", {{"lower", range.lower}, {"upper", range.upper}}},
        {"all_slices", thresholdPanel.stackHistogram()}, {"slice_index", thresholdPosition().z},
        {"selection", source.data().selection ? selectionJson(*source.data().selection) : Json()},
        {"dataset_policy", "preserve_untargeted"}} : Json();
    const bool nativePath = command.target().roi_policy() == RoiPolicy::ActiveSelection &&
                            source.data().selection && source.data().selection->is_path();
    if (owner.recording && (nativePath || thresholdPanel.scientificTargets() || thresholdPanel.stackHistogram() || owner.documentById(source.id())->scientific()))
      step = metadataJson(command.record());
    submitThreshold(command, std::move(step), cancelEpoch);
  }
  void submitThreshold(const ResolvedManualThresholdCommand &command, Json step,
                       std::uint64_t cancelEpoch) {
    if (owner.busy()) throw std::invalid_argument("Wait for the current edit");
    command.target().validate_source(owner.nativeSnapshot(command.target().document_id()));
    JobOptions options;
    options.label = "Apply manual threshold";
    ExecutionOptions execution;
    execution.reset_manual_threshold = true;
    auto submitted = owner.controller_->submit("threshold", command.target(),
        command.parameters(), execution, options);
    thresholdJob.emplace(std::move(submitted));
    thresholdCancelId.store(thresholdJob->info().id);
    if (thresholdCancelEpoch.load() != cancelEpoch)
      thresholdJob->cancel();
    thresholdStep = std::move(step);
    animating = false;
    status = "Applying threshold...";
  }
  void pollThreshold() {
    if (!thresholdJob || !thresholdJob->ready())
      return;
    try {
      OutputPublication outputs(owner.results, owner.workflow, {}, "", thresholdStep);
      std::string completedStatus = "Threshold applied";
      owner.controller_->publish(*thresholdJob);
      outputs.publish();
      status.swap(completedStatus);
    } catch (const OperationCancelled &) {
      status = "Threshold cancelled";
    } catch (const std::exception &e) {
      lastError = e.what();
      status = "Threshold failed";
    }
    thresholdCancelId.store(0);
    thresholdJob.reset();
    thresholdStep = Json();
    if (!thresholdPanel.visible()) {
      thresholdSource.reset();
      thresholdOpeningState.reset();
    }
  }
  void pollGaussian() {
    const auto previousIds = gaussianJobs.active_job_ids();
    const auto probeSource = rankProbe && !owner.documents_.empty() ? doc()->id : 0;
    const auto previousPreview = probeSource ? gaussianJobs.preview_generation(probeSource) : std::nullopt;
    drainGaussianCancellation();
    if (previousIds == std::array<std::uint64_t, 2>{})
      return;
    gaussianJobs.poll(gaussianCurrent(),
        [this](const ResolvedGaussianBlurCommand &command,
               const OperationJob &completed) {
          // Stage every allocating output/record operation before native commit.
          Json step = owner.recording ? metadataJson(command.record()) : Json();
          OutputPublication outputs(owner.results, owner.workflow, {}, "", std::move(step));
          std::string completedStatus = "Edit complete";
          const auto publicationJobId = completed.info().id;
          probeGaussian("before_native_publish");
          auto publishJob = completed;
          const auto published = owner.controller_->publish(publishJob);
          gaussianJobs.record_publication(publicationJobId,
                                          published.document_id);
          outputs.publish();
          status.swap(completedStatus);
        },
        [this](const ResolvedRankFilterCommand &command,
               const OperationJob &completed) {
          auto prepared = completed.prepared();
          const bool changed = !prepared.preview().data().dataset.shares_pixels(
              command.target().source().data().dataset);
          Json step = owner.recording && changed ? metadataJson(command.record()) : Json();
          OutputPublication outputs(owner.results, owner.workflow, {}, "", std::move(step));
          std::string completedStatus = changed ? "Edit complete" : "Image unchanged";
          const auto publicationJobId = completed.info().id;
          std::uint64_t publishedStamp = 0;
          std::uint64_t publishedId = command.target().document_id();
          if (command.target().destination() == OutputDestination::NewDocument) {
            // The exact owner, facade, map slot and stable Pane all exist
            // before native adoption. Failure leaves no attached partial tab.
            auto candidate = prepared.stage_new_document();
            auto facade = std::make_shared<Document>(candidate);
            facade->setHistoryPolicy(historyLimit, static_cast<std::size_t>(
                preferenceState.settings.integer("history_memory_mb")) * 1024 * 1024);
            const auto zoom = preferenceState.settings.number("default_zoom");
            if (zoom > 0)
              facade->setView({{"zoom", zoom / facade->displaySpacing()[0]}});
            Json emptyControls = Json::object(), emptyRows = Json::array();
            static_assert(std::is_nothrow_move_constructible_v<std::shared_ptr<Document>>);
            owner.documents_.reserve(owner.documents_.size() + 1);
            const auto id = candidate->id;
            if (!owner.documentAdapters_.emplace(id, facade).second)
              throw std::invalid_argument("Rank output is already attached");
            bool paneStaged = false;
            try {
              panes.emplace_back();
              paneStaged = true;
              panes.back().doc = facade;
              probeGaussian("before_native_publish");
              auto publishJob = completed;
              owner.controller_->publish(publishJob);
              if (rankProbe) publishedStamp = rankRuntimeSteadyNanoseconds();
              publishedId = id;
            } catch (...) {
              if (paneStaged)
                panes.pop_back();
              owner.documentAdapters_.erase(id);
              throw;
            }
            facade->bindPublishedController(owner.controller_);
            owner.documents_.push_back(std::move(facade));
            roiManagerSource.reset();
            roiSelectedIndices.clear();
            roiRangeAnchor.reset();
            roiIndex = -1;
            roiManagerControls.swap(emptyControls);
            roiManagerRows.swap(emptyRows);
            gaussianJobs.record_publication(publicationJobId, id);
          } else {
            probeGaussian("before_native_publish");
            auto publishJob = completed;
            const auto published = owner.controller_->publish(publishJob);
            if (rankProbe) publishedStamp = rankRuntimeSteadyNanoseconds();
            publishedId = published.document_id;
            gaussianJobs.record_publication(publicationJobId, published.document_id);
          }
          outputs.publish();
          if (rankProbe) {
            const auto &document = owner.documentById(publishedId);
            rankProbe->event("publication", {{"job_id", publicationJobId},
                {"source_id", command.target().document_id()},
                {"published_document_id", publishedId},
                {"native_publish_return_steady_ns", publishedStamp},
                {"pixel_revision", document->revisions().pixels},
                {"workflow", owner.workflow}, {"undo", document->undoSteps()}});
          }
          status.swap(completedStatus);
        });
    mirrorGaussianIds();
    if (rankProbe && probeSource) {
      const auto generation = gaussianJobs.preview_generation(probeSource);
      if (generation && generation != previousPreview) {
        const auto stamp = rankRuntimeSteadyNanoseconds();
        const auto preview = gaussianJobs.preview_snapshot(probeSource);
        if (preview)
          rankProbe->event("preview_install", {{"job_id", gaussianJobs.active_job_ids()[0]},
              {"source_id", probeSource}, {"generation", *generation},
              {"steady_ns", stamp}, {"timestamp_scope", "first observation immediately after normal poll"},
              {"pixel_revision", preview->revisions().pixels},
              {"data_identity", preview->data_identity() ? Json(*preview->data_identity()) : Json()}});
      }
    }
    const auto info = metadataJson(gaussianJobs.diagnostics());
    gaussianPanel.setPreviewError(string(get(get(info, "preview"), "error")));
    rankPanel.setPreviewError(string(get(get(info, "preview"), "error")));
    const auto &apply = get(info, "apply");
    const auto error = string(get(apply, "error"));
    const auto exit = string(get(apply, "exit"));
    const bool failure = exit == "publication_failed" || exit == "callback_failed_after_publication" ||
        exit == "admission_failed" || exit == "source_invalid" ||
        (exit == "job_rejected" && get(get(apply, "job"), "state") == "failed");
    if (failure && !error.empty() && gaussianReportedApplyAttempt != gaussianApplyAttempt) {
      gaussianReportedApplyAttempt = gaussianApplyAttempt;
      lastError = error;
    }
  }
  void guard(const std::function<void()> &function) {
    try {
      function();
    } catch (const std::exception &e) {
      lastError = e.what();
    }
  }
  Json launchAppendState() const {
    return {{"status",launchStatus},{"total_items",launchTotal},{"completed_items",launchCompleted},
      {"remaining_items",pendingLaunch.size()},{"reviewing",launchReview},{"error",launchError}};
  }
  void cancelLaunchAppend() {
    if(pendingLaunch.empty() && !launchReview)return;
    pendingLaunch.clear();launchReview=false;launchStatus="canceled";
#ifdef IMAGEJXX_HAS_SCIENTIFIC_IO
    scientificIoPanel.cancel("launch_canceled");
#endif
    status="Mixed launch canceled; earlier images remain open";
  }
  void requireLaunchPath(const std::string &path) const {
    if(launchReview && (pendingLaunch.empty() || pendingLaunch.front().document || pendingLaunch.front().path!=path))
      throw std::invalid_argument("Mixed launch input differs from its planned review");
  }
  void completeLaunchReview() {
    if(!launchReview)return;
    pendingLaunch.pop_front();++launchCompleted;launchReview=false;
  }
  void advanceLaunchAppend() {
    if(!launchStarted || launchStatus=="completed" || launchStatus=="canceled" || launchStatus=="failed")return;
#ifdef IMAGEJXX_HAS_SCIENTIFIC_IO
    if(launchReview) {
      if(scientificIoPanel.working())return;
      if(scientificIoPanel.visible() && !scientificIoPanel.diagnostics().at("job_canceled").get<bool>())return;
      cancelLaunchAppend();return;
    }
#endif
    struct Flag {bool &v;explicit Flag(bool &value):v(value){v=true;}~Flag(){v=false;}} flag(launchPublishing);
    if(owner.busy())return;
    try {
      while(!pendingLaunch.empty()) {
        const auto &item=pendingLaunch.front();
        if(item.document)owner.addDocument(item.document);
        else if(extension(item.path)==".tif" || extension(item.path)==".tiff") {
#ifdef IMAGEJXX_HAS_SCIENTIFIC_IO
          scientificIoPanel.show(false,{},scientificIoCap(),item.path,false,true);
          scientificIoPanel.inspect();launchReview=true;launchStatus="reviewing";return;
#else
          throw std::invalid_argument("Reviewed TIFF launch requires IMAGEJXX_BUILD_IO=ON");
#endif
        } else owner.openLegacyFile(item.path);
        pendingLaunch.pop_front();++launchCompleted;
      }
      launchStatus="completed";
    } catch(const std::exception &error) {
      const auto path=pendingLaunch.empty()?std::string():pendingLaunch.front().path;
      const auto failure=StartupError({StartupStage::Reading,StartupReason::ReadFailed,path,error.what(),false}).diagnostic();
      std::string message="Mixed launch stopped: "+failure.detail;
      pendingLaunch.clear();launchReview=false;launchStatus="failed";
      launchError=failure.detail;lastError=launchError;status.swap(message);
#ifdef IMAGEJXX_HAS_SCIENTIFIC_IO
      scientificIoPanel.cancel("launch_failed");
#endif
    }
  }
  void queueLaunchAppend(const Json &parameters) {
    if(launchStarted || startupFramesDrawn || startupUserOpened || !owner.documents_.empty() || owner.busy() || dialog.open || browser.open || settingsPanel.visible())
      throw std::invalid_argument("Mixed launch requires a fresh idle editor before its first frame");
    if(!parameters.is_object() || parameters.size()!=1 || !parameters.contains("items") ||
       !parameters.at("items").is_array() || parameters.at("items").empty() || parameters.at("items").size()>64)
      throw std::invalid_argument("Mixed launch requires one array of one to 64 items");
    std::deque<LaunchAppendItem> prepared;
    SessionCapture captured;
    for(const auto &item:parameters.at("items")) {
      if(!item.is_object())throw std::invalid_argument("Mixed launch item must be an object");
      if(item.contains("path")) {
        if(item.size()!=1 || !item.at("path").is_string())throw std::invalid_argument("A launch path accepts no other fields");
        auto path=item.at("path").get<std::string>();
        if(path.empty() || path.size()>131064 || path.find('\0')!=std::string::npos)throw std::invalid_argument("Mixed launch path must be bounded nonempty text without NUL");
        (void)Json(path).dump(); // Strict UTF-8 before any prefix is attached.
        path=std::filesystem::absolute(std::filesystem::u8path(path)).lexically_normal().u8string();
        if(path.size()>131064)throw std::invalid_argument("Resolved launch path exceeds its byte limit");
        prepared.push_back({std::move(path),{}});
      } else {
        if(!item.contains("document") || item.size()!=(item.contains("native_state")?2:1))throw std::invalid_argument("A launch document accepts only document and optional native_state");
        auto document=item.contains("native_state")?documentFromJson(item.at("document"),item.at("native_state")):documentFromJson(item.at("document"));
        document->checkpoint();
        captured.documents.push_back(document->nativeSnapshot());
        (void)estimate_session(captured);
        prepared.push_back({{},std::move(document)});
      }
    }
    pendingLaunch.swap(prepared);launchTotal=pendingLaunch.size();launchStarted=true;launchStatus="running";
    advanceLaunchAppend();
  }
  std::string launchAppendContext() const {
    if(!launchReview)return {};
    return "Mixed launch item "+std::to_string(launchCompleted+1)+" of "+std::to_string(launchTotal)+
      ". Earlier images stay open. Closing discards this item and the remaining inputs.";
  }
  void recent(const std::string &path) {
    Json files = Json::array();
    files.push_back(path);
    for (const auto &p : get(preferences, "recent"))
      if (p != path && files.size() < 15)
        files.push_back(p);
    preferences["recent"] = files;
  }

#ifdef IMAGEJXX_HAS_SCIENTIFIC_IO
  std::size_t scientificIoCap() const {
    const auto cap=settingBytes(preferenceState.settings,"processing_memory_mb");return cap?cap:256*1048576;
  }
  std::optional<DocumentSnapshot> scientificIoCurrent() const {
    return owner.documents_.empty()?std::optional<DocumentSnapshot>{}:std::optional<DocumentSnapshot>{owner.nativeSnapshot()};
  }
  bool scientificIoOtherBusy() const {return workflowOtherBusy()||nativeWorkflowPanel.visible()||nativeWorkflowPanel.working();}
  bool beginStartupTiffs(const StartupRequest &request,const StartupPlan &plan,
                         const std::vector<StartupDiagnostic> &diagnostics) {
    if(plan.source!=StartupSource::ExplicitPaths)return false;
    if(startupTiffs) {
      if(!startupTiffs->finalizing || plan.paths!=startupPlan.paths)
        throw std::invalid_argument("Startup review plan changed");
      return false;
    }
    auto batch=std::make_unique<StartupTiffBatch>();
    for(const auto &path:plan.paths)if(extension(path)==".tif" || extension(path)==".tiff")batch->entries.push_back({path,{},{}});
    if(batch->entries.empty())return false;
    batch->cap=scientificIoCap();
    auto nextRequest=request;auto nextPlan=plan;auto nextDiagnostics=diagnostics;
    std::string nextStatus="reviewing";
    try {
      scientificIoPanel.show(false,{},batch->cap,batch->entries.front().path,false,true);
      scientificIoPanel.inspect();
    } catch(...) {scientificIoPanel.cancel("startup_initialization_failed");throw;}
    startupRequest=std::move(nextRequest);startupPlan=std::move(nextPlan);
    startupDiagnostics.swap(nextDiagnostics);startupStatus.swap(nextStatus);
    startupAttempted=true;startupCompleted=false;startupAutomatic=false;
    startupNoticesDismissed=false;suppressSavedLayout=plan.reset_layout;
    startupTiffs=std::move(batch);return true;
  }
  void cancelStartupTiffs() {
    if(!startupTiffs)return;
    startupTiffs.reset();startupStatus="canceled";startupCompleted=true;
    scientificIoPanel.cancel("startup_canceled");status="Startup batch canceled";
  }
  void clearPendingImports() {pendingFileOpens.clear();cancelStartupTiffs();cancelLaunchAppend();}
  void keepStartupTiff(std::shared_ptr<Document> candidate,const std::string &path,
                       std::function<void()> revalidate) {
    if(!startupTiffs || startupTiffs->finalizing || startupTiffs->next>=startupTiffs->entries.size() ||
       path!=startupTiffs->entries[startupTiffs->next].path)
      throw std::invalid_argument("Startup input differs from its planned review");
    revalidate();
    SessionCapture aggregate;
    for(const auto &entry:startupTiffs->entries)if(entry.document)aggregate.documents.push_back(entry.document->nativeSnapshot());
    aggregate.documents.push_back(candidate->nativeSnapshot());
    (void)detail::estimate_startup_admission(aggregate);
    auto &entry=startupTiffs->entries[startupTiffs->next];
    entry.document=std::move(candidate);entry.revalidate=std::move(revalidate);
    ++startupTiffs->next;
  }
  void advanceStartupTiffs() {
    if(!startupTiffs || scientificIoPanel.working())return;
    if(startupTiffs->cap!=scientificIoCap()) {cancelStartupTiffs();return;}
    if(scientificIoPanel.diagnostics().at("job_canceled").get<bool>()) {cancelStartupTiffs();return;}
    if(scientificIoPanel.visible())return;
    if(startupTiffs->next<startupTiffs->entries.size() &&
       scientificIoPanel.diagnostics().at("cancel_reason")!="startup_flat_staged") {
      // A completed scientific import closes without a cancellation reason.
      if(scientificIoPanel.diagnostics().at("cancel_reason")!="") {cancelStartupTiffs();return;}
    }
    try {
      if(startupTiffs->next==startupTiffs->entries.size()) {
        startupTiffs->finalizing=true;
        struct Flag {bool &v;explicit Flag(bool &value):v(value){v=true;}~Flag(){v=false;}} flag(scientificIoPublishing);
        owner.executeStartup(startupRequest,false);
        startupTiffs.reset();
      } else {
        scientificIoPanel.show(false,{},startupTiffs->cap,startupTiffs->entries[startupTiffs->next].path,false,true);
        scientificIoPanel.inspect();
      }
    } catch(const std::exception &error) {
      StartupDiagnostic failure{StartupStage::Validation,StartupReason::AdmissionFailed,{},error.what(),false};
      if(const auto *typed=dynamic_cast<const StartupError *>(&error))failure=typed->diagnostic();
      auto diagnostics=startupDiagnostics;diagnostics.push_back(StartupError(std::move(failure)).diagnostic());
      scientificIoPanel.cancel("startup_failed");startupTiffs.reset();
      startupDiagnostics.swap(diagnostics);startupStatus="failed";startupCompleted=false;
      status="Startup batch failed";
    }
  }
  std::string startupTiffContext() const {
    if(!startupTiffs)return launchAppendContext();
    return "Startup TIFF "+std::to_string(std::min(startupTiffs->next+1,startupTiffs->entries.size()))+" of "+std::to_string(startupTiffs->entries.size())+
      ". No images appear until the entire startup batch is accepted. Closing cancels the whole batch.";
  }
  void pollScientificIo() {
    scientificIoPanel.tick(scientificIoCurrent(),scientificIoOtherBusy(),scientificIoCap(),
      [this](std::shared_ptr<imagejxx::Document> candidate,const Json &record,bool capturedRecording){
        if(scientificIoOtherBusy())throw std::invalid_argument("Wait for the current edit");
        requireLaunchPath(record.at("path").get<std::string>());
        if(startupTiffs) {
          auto revalidate=scientificIoPanel.import_revalidation();
          keepStartupTiff(std::make_shared<Document>(std::move(candidate),record.value("loading",std::string())=="virtual",scientificIoCap()),record.at("path").get<std::string>(),std::move(revalidate));return;
        }
        auto nextPreferences=preferences;nextPreferences["scientific_io_last_directory"]=std::filesystem::u8path(record.at("path").get<std::string>()).parent_path().u8string();
        const auto openedPath=record.at("path").get<std::string>();Json recentFiles=Json::array({openedPath});for(const auto &p:get(nextPreferences,"recent"))if(p!=openedPath && recentFiles.size()<15)recentFiles.push_back(p);nextPreferences["recent"]=std::move(recentFiles);
        std::string completed="Scientific image opened";
        OutputPublication outputs(owner.results,owner.workflow,{},"",capturedRecording?record:Json());
        struct Flag {bool &value;explicit Flag(bool &v):value(v){value=true;}~Flag(){value=false;}} flag(scientificIoPublishing);
        struct LaunchFlag {bool &v;explicit LaunchFlag(bool &value):v(value){v=true;}~LaunchFlag(){v=false;}} launchFlag(launchPublishing);
        if(record.value("loading",std::string())=="virtual")owner.addVirtualDocument(std::move(candidate),scientificIoCap());else owner.addNativeDocument(std::move(candidate));
        outputs.publish();preferences.swap(nextPreferences);status.swap(completed);startupUserOpened=true;completeLaunchReview();
      },
      [this](const DocumentSnapshot &source,const Json &record,bool capturedRecording,const std::function<void()> &publishFile){
        if(scientificIoOtherBusy()||owner.controller_->active_document_id()!=source.id())throw std::invalid_argument("Export requires its idle active source");
        const auto current=owner.nativeSnapshot();if(!source.same_document(current)||!source.shares_data(current)||!source.revisions().matches(current.revisions(),Change::Data))throw RevisionConflict();
        auto nextPreferences=preferences;nextPreferences["scientific_io_last_directory"]=std::filesystem::u8path(record.at("path").get<std::string>()).parent_path().u8string();
        std::string completed="Scientific image exported";
        OutputPublication outputs(owner.results,owner.workflow,{},"",capturedRecording?record:Json());
        publishFile();outputs.publish();preferences.swap(nextPreferences);status.swap(completed);
      });
    advanceStartupTiffs();
    if(!startupTiffs && !launchReview && pendingLaunch.empty() && !scientificIoPanel.visible() && !scientificIoPanel.working() && !scientificIoOtherBusy() && !pendingFileOpens.empty()) {
      auto path=std::move(pendingFileOpens.front());pendingFileOpens.erase(pendingFileOpens.begin());
      struct Flag {bool &v;explicit Flag(bool &value):v(value){v=true;}~Flag(){v=false;}} flag(scientificIoPublishing);
      if(extension(path)==".tif" || extension(path)==".tiff") {showScientificIo(false,{{"path",path}});scientificIoPanel.inspect();}
      else owner.openLegacyFile(path);
    }
  }
  void importFlatReview() {
    if(scientificIoOtherBusy())throw std::invalid_argument("Wait for the current edit");
    const auto generation=scientificIoPanel.generation();const auto path=scientificIoPanel.flat_import_path();
    std::shared_ptr<io_detail::Source> startupGuard;
    requireLaunchPath(path);
    if(startupTiffs || launchReview)startupGuard=std::make_shared<io_detail::Source>(std::filesystem::u8path(path),1024ULL*1024*1024,ExecutionControl{});
    auto tiledPixels=scientificIoPanel.flat_tiled_pixels();
    auto candidate=std::make_shared<Document>(tiledPixels?std::move(tiledPixels->planes):readImages(path),filename(path),Json::object(),tiledPixels?tiledPixels->sample_policy:FloatingSamplePolicy::FiniteOnly);
    if(startupGuard)startupGuard->unchanged();
    if(generation!=scientificIoPanel.generation() || path!=scientificIoPanel.flat_import_path())throw std::invalid_argument("TIFF import form changed");
    if(startupTiffs) {
      keepStartupTiff(std::move(candidate),path,[startupGuard]{startupGuard->unchanged();});
      scientificIoPanel.cancel("startup_flat_staged");return;
    }
    auto next=preferences;Json files=Json::array({path});for(const auto &p:get(next,"recent"))if(p!=path && files.size()<15)files.push_back(p);next["recent"]=std::move(files);
    std::string completed="Flat pixels imported";
    struct Flag {bool &v;explicit Flag(bool &value):v(value){v=true;}~Flag(){v=false;}} flag(scientificIoPublishing);
    struct LaunchFlag {bool &v;explicit LaunchFlag(bool &value):v(value){v=true;}~LaunchFlag(){v=false;}} launchFlag(launchPublishing);
    owner.addDocument(std::move(candidate));preferences.swap(next);status.swap(completed);completeLaunchReview();scientificIoPanel.cancel("flat_imported");
  }
  void showScientificIo(bool exporting,const Json &parameters) {
    if(owner.busy())throw std::invalid_argument("Close the current form or wait for its worker");
    if(!parameters.is_object()||parameters.size()>1||(parameters.size()&&!parameters.contains("path")))throw std::invalid_argument("Scientific IO accepts an optional path");
    std::string path;
    if(parameters.contains("path")){
      const auto &value=parameters.at("path");if(!value.is_string()||value.get_ref<const std::string &>().size()>131064)throw std::invalid_argument("Scientific IO path must be bounded text");path=value.get<std::string>();
    }
    if(path.empty()){
      auto directory=string(get(preferences,"scientific_io_last_directory"));if(directory.size()>131064||directory.find('\0')!=std::string::npos)directory.clear();
      if(directory.empty())directory=std::filesystem::current_path().u8string();
      path=exporting?(std::filesystem::u8path(directory)/"image.tif").u8string():directory;
    }
    roiImportPanel.cancel("scientific_io");roiExportPanel.cancel("scientific_io");displayAnnotationPanel.cancel("scientific_io");
    planarAnnotationRebind.cancel("scientific_io");selectionPanel.cancel();cancelSavedRoiGeometry("scientific_io");cancelCreations("scientific_io");
    scientificIoPanel.show(exporting,scientificIoCurrent(),scientificIoCap(),std::move(path),owner.recording);
  }
  void browseScientificIo() {
    const auto generation=scientificIoPanel.generation();const auto draft=scientificIoPanel.diagnostics();const bool save=draft.at("exporting").get<bool>();
    file(save?"Choose scientific TIFF export":"Choose scientific TIFF",save,[this,generation,save](std::string path){
      if(!scientificIoPanel.browse_current(generation,scientificIoCurrent()))throw std::invalid_argument("Scientific IO form changed while browsing");
      scientificIoPanel.patch({{"path",std::move(path)}});
      if(save&&browser.overwrite)scientificIoPanel.patch({{"replace_existing",true}});
    },draft.at("path").get<std::string>());
    browser.extensions={".tif",".tiff"};browser.prompt=save?"Choose a grayscale or RGB scientific TIFF output":"Choose a calibrated grayscale or RGB TIFF with explicit C/Z/T axes";
    browser.current=[this,generation]{return scientificIoPanel.browse_current(generation,scientificIoCurrent());};
  }
#endif

  void savePreferences() {
    preferenceState.settings = owner.settings();
    preferenceState.extras = preferences;
    preferenceState.save(preferencesPath);
  }
  void start(std::function<JobResult()> function,
             std::shared_ptr<Document> target, bool newDocument = false,
             std::string title = "", std::string label = "",
             std::size_t extraWorkspace = 0,
             std::shared_ptr<WorkflowReplayLineage> replayLineage = {},
             bool preparedTask = false) {
    if (target && !preparedTask)
      target->requireLegacyEditable();
    if (owner.busy())
      throw std::invalid_argument("Wait for the current edit to finish");
    animating = false;
    cancelled = false;
    jobDocument = std::move(target);
    jobRevisions = jobDocument ? jobDocument->revisions() : Revisions{};
    jobSlice = jobDocument ? jobDocument->slice() : 0;
    jobNew = newDocument;
    jobPreview = false;
    jobSkip = false;
    jobStep = nullptr;
    jobTitle = std::move(title);
    auto result = std::make_shared<std::optional<JobResult>>();
    JobOptions options;
    options.label = label.empty() ? (jobTitle.empty() ? "Image task" : jobTitle)
                                  : std::move(label);
    options.document_id = jobDocument ? jobDocument->id : 0;
    options.retain_result_resources = preparedTask;
    if (jobDocument && !preparedTask)
      options.resource_bytes = legacy_job_base_workspace(jobDocument->nativeSnapshot());
    if (extraWorkspace >
        std::numeric_limits<std::size_t>::max() - options.resource_bytes)
      throw std::invalid_argument(
          "Job reservation overflows addressable memory");
    options.resource_bytes += extraWorkspace;
    auto gate = std::exchange(diagnosticWorkerGate, {});
    auto submitted = owner.controller_->job_scheduler()->submit(
        [function = std::move(function), result, gate = std::move(gate), preparedTask](const JobContext &context) {
          // Establish the scope in the frontend's core module as well, so an
          // independently linked Python binding does not hide its cancellation.
          ExecutionScope scope(context.execution_control());
          if (gate)
            gate(context);
          auto completed = function();
          if (preparedTask) completed.reservation = context.result_resource_guard();
          result->emplace(std::move(completed));
        },
        std::move(options));
    if (replayLineage) {
      replayLineage->bindJob(submitted);
      workflowReplayLineage = std::move(replayLineage);
    }
    {
      std::lock_guard<std::mutex> lock(jobMutex);
      job = std::move(submitted);
      jobResult = std::move(result);
    }
    status = "Processing...";
  }
  void requestClose(std::shared_ptr<Document> d) {
    if (legacyBusy())
      return;
    if (d->dirty()) {
      pendingClose = d;
      return;
    }
    remove(d);
  }
  void remove(const std::shared_ptr<Document> &d) {
    auto it = std::find(owner.documents_.begin(), owner.documents_.end(), d);
    if (it == owner.documents_.end())
      return;
    size_t index = it - owner.documents_.begin();
    cancelCreation(panes[index], "owner_closed");
    if(histogramTask && histogramTask->command.target().document_id()==d->id)cancelHistogram();
    histogramPanel.sourceClosed(d->id);
    contrastPanel.sourceClosed(d->id);
    if (autoJob && autoOwner == d->id) autoJob->cancel();
    if(contrastApplySource && contrastApplySource->id()==d->id)cancelContrastApply();
    if (thresholdSource && thresholdSource->id() == d->id) {
      thresholdPanel.source_closed();
      thresholdSource.reset();
      thresholdOpeningState.reset();
      if (thresholdJob)
        thresholdJob->cancel();
    }
    if (get(gaussianPanel.diagnostics(), "source_id") == d->id)
      gaussianPanel.cancel([this] { cancelGaussianPreview(); });
    if (get(rankPanel.diagnostics(), "source_id") == d->id)
      rankPanel.cancel([this] { cancelGaussianPreview(); });
    gaussianJobs.document_closed(d->id);
    mirrorGaussianIds();
    resetInput(panes[index]);
    owner.controller_->close(d->id, true);
    if(viewportJobs)viewportJobs->cancelDocument(d->id);
    panes[index].sampledSurface.reset();panes[index].sampleMemory.reset();
    d->releaseFrontend();
    if (gl && panes[index].texture)
      glDeleteTextures(1, &panes[index].texture);
    if (gl)
      panes[index].orthogonal.releaseTextures();
    if(gl){panes[index].displayRenderer.release_textures();panes[index].importDisplayRenderer.release_textures();}
    panes.erase(panes.begin() + index);
    owner.documents_.erase(it);
    owner.documentAdapters_.erase(d->id);
    // The promoted owner can retain the removed tab's numeric index.
    lastVisible = -1;
    documentTabs = Json::array();
    invalidateRoiManager();
  }
  void rememberSession(const std::string &path, size_t count) {
    auto target = std::filesystem::absolute(std::filesystem::u8path(path))
                      .lexically_normal();
    for (auto &[existing, size] : sessionSizes)
      if (samePath(existing, target))
        size = count;
    sessionSizes[target] = count;
  }
  void saveDocument(std::shared_ptr<Document> d, const std::string &path) {
    auto target = std::filesystem::absolute(std::filesystem::u8path(path))
                      .lexically_normal();
    for (const auto &[existing, size] : sessionSizes)
      if (size > 1 && samePath(existing, target))
        throw std::invalid_argument(
            "This file contains multiple documents. Use Save workspace or "
            "choose a different document path.");
    const auto presentation = with_workspace_presentation(
        metadataObjectFromJson(sessionExtensions), std::size_t(0), 1);
    const auto singleExtensions = metadataJson(presentation.extensions);
    auto nextDiagnostics = startupDiagnostics;
    nextDiagnostics.insert(nextDiagnostics.end(), presentation.diagnostics.begin(), presentation.diagnostics.end());
    saveNativeSession(path, {d}, owner.results, owner.workflow, singleExtensions);
    startupDiagnostics.swap(nextDiagnostics);
    startupNoticesDismissed = false;
    rememberSession(path, 1);
    d->path = path;
    recent(path);
    status = "Saved " + filename(path);
  }
  void file(const std::string &title, bool save,
            std::function<void(std::string)> submit, std::string path = "") {
    browser = {title,
               path.empty() ? std::filesystem::current_path().u8string()
                            : std::move(path),
               "",
               std::move(submit),
               true,
               save,
               false};
  }
  bool browserFileAllowed(const std::filesystem::path &path) const {
    if(browser.directory){std::error_code error;return std::filesystem::is_directory(path,error);}
    if(browser.extensions.empty())return true;
    auto extension=path.extension().u8string();
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
    return std::find(browser.extensions.begin(),browser.extensions.end(),extension)!=browser.extensions.end();
  }
  void browseRoiExport() {
    const auto request=roiExportPanel.browse_request();
    auto current=[this]() -> std::optional<DocumentSnapshot> {
      if(owner.documents_.empty() || owner.busy())return {};
      return owner.nativeSnapshot();
    };
    file("Choose ROI export destination",true,[this,request,current](std::string path) {
      roiExportPanel.path(request,current(),std::move(path));
    },request.path);
    browser.prompt=roiExportPanel.zip_mode()?"Choose an output ImageJ ROI ZIP (.zip) file":"Choose an output ImageJ ROI (.roi) file";browser.extensions={roiExportPanel.zip_mode()?".zip":".roi"};
    browser.current=[this,request,current]{return roiExportPanel.browse_current(request,current());};
  }
  void browseNativeWorkflow() {
    const auto request=nativeWorkflowPanel.browse_request();
    auto current=[this]() -> std::optional<DocumentSnapshot>{if(owner.documents_.empty()||workflowOtherBusy()||nativeWorkflowPanel.working())return {};return owner.nativeSnapshot();};
    file("Choose workflow output",true,[this,request,current](std::string path){nativeWorkflowPanel.path(request,current(),std::move(path),browser.overwrite);},request.path);
    browser.prompt="Choose a new CSV (.csv), ROI (.roi) or ZIP (.zip) file";browser.extensions={".csv",".roi",".zip"};
    if(request.route==UINT64_MAX){browser.directory=true;browser.save=false;browser.extensions.clear();browser.prompt="Choose an existing batch output folder";}
    else if(request.route==UINT64_MAX-1){browser.save=false;browser.extensions={".ijx"};browser.prompt="Choose an input native session (.ijx)";}
    else if(request.route==UINT64_MAX-2){browser.extensions={".json"};browser.prompt="Save native Batch setup (.json)";}
    else if(request.route==UINT64_MAX-3){browser.save=false;browser.extensions={".json"};browser.prompt="Load native Batch setup (.json)";}
    browser.current=[this,request,current]{return nativeWorkflowPanel.browse_current(request,current());};
  }
  void browseRoiImport() {
    const auto request=roiImportPanel.browse_request();
    auto current=[this]() -> std::optional<DocumentSnapshot> {
      if(owner.documents_.empty() || owner.busy())return {};
      return owner.nativeSnapshot();
    };
    file("Choose ROI file",false,[this,request,current](std::string path) {
      roiImportPanel.path(request,current(),std::move(path));
    },request.path);
    browser.prompt="Choose an ImageJ ROI (.roi) or ROI archive (.zip)";
    browser.extensions={".roi",".zip"};
    browser.current=[this,request,current]{return roiImportPanel.browse_current(request,current());};
  }
  void ask(std::string title, Json values, std::function<void(Json)> submit,
           Json schema = Json::array(), std::string id = "") {
    if (gl && dialog.texture)
      glDeleteTextures(1, &dialog.texture);
    dialog = Dialog{};
    dialog.generation = ++dialogGeneration;
    dialog.openedDocumentId = owner.documents_.empty() ? 0 : doc()->id;
    dialog.title = std::move(title);
    dialog.id = std::move(id);
    dialog.values = std::move(values);
    dialog.schema = std::move(schema);
    dialog.submit = std::move(submit);
    dialog.open = true;
    if (dialog.schema.empty())
      for (auto it = dialog.values.begin(); it != dialog.values.end(); ++it)
        dialog.schema.push_back(
            {{"name", it.key()},
             {"label", it.key()},
             {"kind", it.value().is_boolean()          ? "bool"
                      : it.value().is_number_integer() ? "int"
                      : it.value().is_number()         ? "float"
                      : it.value().is_string()         ? "text"
                                                       : "json"},
             {"choices", Json::array()},
             {"optional", it.value().is_null()}});
    for (const auto &p : dialog.schema) {
      auto name = string(get(p, "name"));
      auto v = get(dialog.values, name.c_str());
      dialog.text[name] = v.is_null()     ? ""
                          : v.is_string() ? string(v)
                                          : v.dump();
    }
  }
  void showAxisExtraction(const DocumentSnapshot &source, AxisExtractionKind kind,
                          std::optional<MetadataObject> original = {}) {
    const bool oriented = kind == AxisExtractionKind::OrientedRange;
    const bool range = kind != AxisExtractionKind::Plane;
    if (original) {
      if (oriented) validate_axis_orientation_command_record(*original);
      else validate_axis_extraction_command_record(*original);
    }
    AxisPlaneExportRequest admission;
    admission.t = source.display().t;
    admission.c = source.display().c;
    requireAxisExtractionSource(source, admission, true);
    const auto scientific = owner.documentById(source.id())->scientific();
    Json values = range
        ? Json{{"plane", "XZ"}, {"first", 1},
               {"last", source.data().dataset.shape().y}, {"step", 1}}
        : Json{{"plane", "XY"}, {"index", source.display().crosshair[2] + 1}};
    if (oriented)
      values.update({{"reverse_order", false}, {"flip_vertical", false}, {"transpose", false}});
    auto cap = admission.max_working_bytes;
    Json originalJson;
    if (original) {
      originalJson = metadataJson(*original);
      const auto &recordRequest = originalJson.at("request");
      cap = recordRequest.at("max_working_bytes").get<std::size_t>();
      values["plane"] = recordRequest.at("plane");
      if (range) {
        values["first"] = recordRequest.at("first").get<int>() + 1;
        values["last"] = recordRequest.at("last").get<int>() + 1;
        values["step"] = recordRequest.at("step");
      } else values["index"] = recordRequest.at("coordinate").get<int>() + 1;
      if (oriented)
        for (const char *flag : {"reverse_order", "flip_vertical", "transpose"})
          values[flag] = recordRequest.at(flag);
    }
    auto schema = Json::array({{{"name", "plane"}, {"kind", "text"},
        {"label", "Plane"}, {"choices", {"XY", "XZ", "YZ"}}}});
    if (range) {
      schema.push_back({{"name", "first"}, {"kind", "int"}, {"label", "First (1-based)"}});
      schema.push_back({{"name", "last"}, {"kind", "int"}, {"label", "Last (inclusive, 1-based)"}});
      schema.push_back({{"name", "step"}, {"kind", "int"}, {"label", "Step"}});
    } else schema.push_back({{"name", "index"}, {"kind", "int"}, {"label", "Index (1-based)"}});
    if (oriented) {
      schema.push_back({{"name", "reverse_order"}, {"kind", "bool"}, {"label", "Reverse selected planes"}});
      schema.push_back({{"name", "flip_vertical"}, {"kind", "bool"}, {"label", "Flip input vertical axis"}});
      schema.push_back({{"name", "transpose"}, {"kind", "bool"}, {"label", "Transpose axes (ImageJ Rotate90 output)"}});
    }
    const std::string title = oriented ? "Reslice oriented range"
        : range ? "Reslice range" : "Export plane";
    ask(original ? (oriented ? "Rebind oriented range"
                            : range ? "Rebind reslice range" : "Rebind plane export") : title,
        std::move(values), [this, source, kind, cap, original](Json draft) {
          if (kind == AxisExtractionKind::OrientedRange) {
            const auto request = axisOrientationDialogRequest(source, cap, draft);
            const auto command = original
                ? rebind_axis_orientation_command(source, *original, request)
                : resolve_axis_orientation_command(source, request);
            owner.publishAxisOrientation(command, true);
          } else {
            const auto request = axisExtractionDialogRequest(source,
                kind == AxisExtractionKind::Range, cap, draft);
            const auto command = original
                ? rebind_axis_extraction_command(source, *original, request)
                : resolve_axis_extraction_command(source, request);
            owner.publishAxisExtraction(command, true);
          }
        }, std::move(schema), oriented ? "axis_range_orientation"
            : range ? "axis_range_reslice" : "axis_plane_export");
    if (oriented) dialog.axisRangeOrientationSource = source;
    else if (range) dialog.axisRangeSource = source;
    else dialog.axisPlaneSource = source;
    dialog.extractionRebindRecord = std::move(originalJson);
    dialog.scientificExtraction = scientific;
    dialog.extractionCap = cap;
    dialog.openedDocumentId = source.id();
  }
  Json operationDefaults(const Json &command, const Document &document) const {
    const auto source = document.nativeSnapshot();
    const auto shape = source.data().dataset.shape();
    const OperationDefaultContext context{
        shape.x, shape.y, shape.s, source.data().dataset.pixel_type(), {}};
    const auto id = string(get(command, "id"));
    // Ordinary form defaults depend only on the captured descriptor. Preserve
    // the conversion lane's existing sample/display default policy.
    auto values = id == "convert_image" || id == "invert_uint16"
        ? defaults(command, document.frame()) : defaults(command, context);
    if (id == "binary_erode" || id == "binary_dilate" || id == "binary_open" ||
        id == "binary_close") {
      const auto options = owner.settings().binary_options();
      values["iterations"] = options.iterations;
      values["neighbors"] = options.neighbors;
      values["black_background"] = options.black_background;
      values["pad_edges"] = options.pad_edges;
    }
    if (id == "convert_image" || id == "invert_uint16") {
      const auto options = owner.settings().conversion_options();
      if (id == "invert_uint16") {
        values["full_range_inversions"] = options.full_range_inversions;
        values["unsigned16_bits"] = options.unsigned16_bits;
      } else {
        values["scale"] = options.scale;
        values["weighted_rgb"] = options.weighted_rgb;
        values["red_weight"] = options.rgb_weights[0];
        values["green_weight"] = options.rgb_weights[1];
        values["blue_weight"] = options.rgb_weights[2];
        values["range_policy"] = options.range_policy;
        if (options.range_policy == "display") {
          const auto snapshot = document.nativeSnapshot();
          const auto channelState = recognized_channel_display(snapshot.display(), snapshot.data().dataset);
          const auto display =
              get(metadataJson(snapshot.display().properties),
                  "display_range");
          if (channelState) {
            const auto range = effective_channel_display_range(snapshot.data().dataset,
                channelState, snapshot.display().t, snapshot.display().c,
                snapshot.display().crosshair[2]).range;
            values["range_policy"] = "explicit";
            values["range_min"] = range[0];
            values["range_max"] = range[1];
          } else if (display.is_array()) {
            values["range_policy"] = "explicit";
            values["range_min"] = display[0];
            values["range_max"] = display[1];
          } else if (context.type == PixelType::UInt16 &&
                     options.unsigned16_bits) {
            values["range_policy"] = "explicit";
            values["range_min"] = 0.;
            values["range_max"] = double((1u << options.unsigned16_bits) - 1);
          } else
            values["range_policy"] = "data";
        }
      }
    }
    return values;
  }
  void operation(const Json &command, const Json &parameters = Json::object()) {
    auto d = doc();
    auto id = string(get(command, "id"));
    if (id == "gaussian_blur_imagej") {
      if (!gaussianSourceAllowed(*d))
        throw std::invalid_argument("Gaussian is not available for this document");
      if (!parameters.is_object() || !parameters.empty())
        throw std::invalid_argument("Use the Gaussian dialog to choose its parameters");
      rankPanel.cancel([this] { cancelGaussianPreview(); });
      gaussianPanel.cancel([this] { cancelGaussianPreview(); });
      cancelGaussianPreview();
      gaussianPanel.show(d->nativeSnapshot());
      gaussianReadbackRequested = true;
      return;
    }
    if (id == "remove_nans_imagej" || id == "rank_filter_imagej" || id == "remove_outliers_imagej" || id == "top_hat_imagej") {
      if (!(id=="remove_nans_imagej" ? removeNaNsSourceAllowed(*d) : rankSourceAllowed(*d)))
        throw std::invalid_argument("Rank filtering is not available for this document");
      if (!parameters.is_object() || !parameters.empty())
        throw std::invalid_argument("Use the rank dialog to choose its parameters");
      gaussianPanel.cancel([this] { cancelGaussianPreview(); });
      rankPanel.cancel([this] { cancelGaussianPreview(); });
      cancelGaussianPreview();
      if(id=="remove_nans_imagej")rankPanel.show_remove_nans(d->nativeSnapshot());
      else if(id=="top_hat_imagej")rankPanel.show_top_hat(d->nativeSnapshot());
      else if (id == "remove_outliers_imagej") rankPanel.show_outliers(d->nativeSnapshot());
      else rankPanel.show(d->nativeSnapshot());
      gaussianReadbackRequested = true;
      return;
    }
    if (d->scientific() && scientific_operation_review_available(id, d->nativeSnapshot())) {
      if (owner.busy()) throw std::invalid_argument("Wait for the current edit");
      auto cap = std::size_t(preferenceState.settings.integer("processing_memory_mb")) * 1048576;
      if (!cap) cap = 256 * 1048576;
      Json configuredParameters;
      const Json *requested = &parameters;
      if (scientific_binary_operation(id) || id == "invert_uint16" || id == "convert_image") {
        if (!parameters.is_object()) throw std::invalid_argument("Processing parameters must be an object");
        if (scientific_binary_operation(id)) {
          const auto options = owner.settings().binary_options();
          configuredParameters = {{"iterations", options.iterations}, {"neighbors", options.neighbors},
                                  {"black_background", options.black_background}, {"pad_edges", options.pad_edges}};
        } else if (id == "invert_uint16") {
          const auto options = owner.settings().conversion_options();
          configuredParameters = {{"full_range_inversions", options.full_range_inversions},
                                  {"unsigned16_bits", options.unsigned16_bits}};
        } else {
          const auto snapshot = d->nativeSnapshot();
          admit_scientific_conversion_review(snapshot, cap);
          const auto options = owner.settings().conversion_options();
          configuredParameters = {{"dtype", "uint8"}, {"scale", options.scale},
              {"weighted_rgb", options.weighted_rgb}, {"red_weight", options.rgb_weights[0]},
              {"green_weight", options.rgb_weights[1]}, {"blue_weight", options.rgb_weights[2]},
              {"range_policy", options.range_policy}, {"range_min", 0.}, {"range_max", 1.}};
          if (options.range_policy == "display") {
            const auto channels = recognized_channel_display(snapshot.display(), snapshot.data().dataset);
            const auto found = snapshot.display().properties.find("display_range");
            if (channels) {
              const auto range = channels->channels.at(snapshot.display().c).range;
              configuredParameters["range_policy"] = "explicit";
              configuredParameters["range_min"] = range[0];
              configuredParameters["range_max"] = range[1];
            } else if (found != snapshot.display().properties.end()) {
              const auto range = metadataJson(found->second);
              if (!range.is_array() || range.size() != 2 || !range[0].is_number() || !range[1].is_number())
                throw std::invalid_argument("Conversion display range requires two numbers");
              configuredParameters["range_policy"] = "explicit";
              configuredParameters["range_min"] = range[0];
              configuredParameters["range_max"] = range[1];
            } else if (snapshot.data().dataset.pixel_type() == PixelType::UInt16 && options.unsigned16_bits) {
              configuredParameters["range_policy"] = "explicit";
              configuredParameters["range_max"] = double((1u << options.unsigned16_bits) - 1);
            } else configuredParameters["range_policy"] = "data";
          }
        }
        for (const auto &field : parameters.items()) {
          if (!configuredParameters.contains(field.key())) throw std::invalid_argument("Unknown configured processing parameter");
          const auto &expected = configuredParameters.at(field.key());
          const bool valid = expected.is_boolean() ? field.value().is_boolean()
              : expected.is_string() ? field.value().is_string()
              : expected.is_number_float() ? field.value().is_number() && !field.value().is_boolean()
              : field.value().is_number_integer();
          if (!valid) throw std::invalid_argument("Configured processing parameter has the wrong scalar type");
          configuredParameters[field.key()] = field.value();
        }
        requested = &configuredParameters;
      }
      nativeWorkflowPanel.show_operation(d->nativeSnapshot(), id, *requested, owner.workflow, cap, owner.recording);
      return;
    }
    d->requireLegacyEditable();
    auto values = operationDefaults(command, *d);
    if (id == "fill_color" || id == "paint_stroke")
      values["color"] = paintColor();
    if (id == "paint_stroke")
      values["width"] = brush;
    if (id == "calculate" && owner.documents_.size() > 1) {
      owner.documents_[(owner.activeIndex() + 1) % owner.documents_.size()]
          ->requireLegacyEditable();
      values["other"] = imageArray(
          owner.documents_[(owner.activeIndex() + 1) % owner.documents_.size()]
              ->frame());
    }
    if (!parameters.is_object())
      throw std::invalid_argument("Operation parameters must be an object");
    for (auto it = parameters.begin(); it != parameters.end(); ++it) {
      if (!values.contains(it.key()))
        throw std::invalid_argument("Unknown parameter: " + it.key());
      values[it.key()] = it.value();
    }
    ask(
        string(get(command, "title")), values,
        [this, id](Json p) { owner.applyOperation(id, p, allSlices); },
        get(command, "parameters"), id);
    dialog.operation = true;
  }
  Json itemControl(bool enabled = true) const {
    const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    const auto &context = *ImGui::GetCurrentContext();
    return {{"id", ImGui::GetItemID()}, {"rect", {a.x, a.y, b.x, b.y}},
            {"point", {(a.x + b.x) * .5f, (a.y + b.y) * .5f}},
            {"enabled", enabled && !(context.LastItemData.ItemFlags & ImGuiItemFlags_Disabled)},
            {"active", ImGui::IsItemActive()}, {"focused", ImGui::IsItemFocused()}};
  }
  void observeControl(const std::string &name, bool enabled = true,
                      const char *shortcutHint = nullptr,
                      const char *commandId = nullptr,
                      ImGuiWindow *itemWindow = nullptr) {
    if (!headlessDiagnostics)
      return;
    hostControls[name] = itemControl(enabled);
    if (itemWindow) {
      // BeginMenu may switch CurrentWindow to the opened submenu while its
      // last item still belongs to the parent. Capture that parent explicitly.
      const auto clip = itemWindow->InnerClipRect;
      hostControls[name]["window_name"] = itemWindow->Name;
      hostControls[name]["clip_rect"] = {clip.Min.x, clip.Min.y, clip.Max.x, clip.Max.y};
      hostControls[name]["visible"] = ImGui::IsItemVisible();
    }
    if (name.rfind("tools.zoom.", 0) == 0) {
      const auto clipMin = ImGui::GetWindowDrawList()->GetClipRectMin();
      const auto clipMax = ImGui::GetWindowDrawList()->GetClipRectMax();
      hostControls[name]["clip_rect"] = {clipMin.x, clipMin.y, clipMax.x, clipMax.y};
      hostControls[name]["visible"] = ImGui::IsItemVisible();
    }
    if (shortcutHint) hostControls[name]["shortcut_hint"] = shortcutHint;
    if (commandId) hostControls[name]["command_id"] = commandId;
  }
  void invalidateRoiManager() {
    roiManagerSource.reset();
    roiManagerPendingFocus.reset();
    savedRoiHistoryFocus.reset();
    roiSelectedIndices.clear();
    roiRangeAnchor.reset();
    roiIndex = -1;
    roiManagerControls = Json::object();
    roiManagerRows = Json::array();
  }
  void synchronizeRoiManager() {
    if (owner.documents_.empty()) {
      invalidateRoiManager();
      return;
    }
    try {
      const auto current = owner.nativeSnapshot();
      const bool stagedFocus = roiManagerPendingFocus && roiManagerSource &&
          roiManagerSource->same_document(current) &&
          roiManagerPendingFocus->first == current.id() &&
          roiManagerPendingFocus->second == current.revisions().metadata;
      if (stagedFocus && savedRoiHistoryFocus && !savedRoiHistoryFocus->after &&
          savedRoiHistoryFocus->document_id == current.id() &&
          savedRoiHistoryFocus->publication_metadata_revision == current.revisions().metadata)
        savedRoiHistoryFocus->after = current.data_identity();
      if ((!roiManagerSource || !roiManagerSource->same_document(current) ||
           roiManagerSource->revisions().metadata != current.revisions().metadata) &&
          !stagedFocus)
        invalidateRoiManager();
      // Selection and display changes do not change saved-row membership.
      // Retain a fresh snapshot so later preparation captures the current
      // selection once, rather than the selection present at the first row click.
      roiManagerSource = std::make_shared<const DocumentSnapshot>(current);
      roiManagerPendingFocus.reset();
    } catch (...) {
      invalidateRoiManager();
      throw;
    }
  }
  void focusRoiManagerRow(std::size_t index, bool select) {
    synchronizeRoiManager();
    if (!roiManagerSource || index >= roiManagerSource->data().rois.size() ||
        index > static_cast<std::size_t>(std::numeric_limits<int>::max()))
      throw std::invalid_argument("Select a saved ROI");
    roiIndex = static_cast<int>(index);
    if (select) {
      roiSelectedIndices = {index};
      roiRangeAnchor = index;
    }
  }
  void commitRoiManager(const DocumentSnapshot &source, PreparedRoiManagerEdit edit) {
    if (owner.busy()) throw std::invalid_argument("Wait for the current edit");
    if (source.id() != owner.controller_->active_document_id())
      throw std::invalid_argument("ROI manager requires its captured active document");
    if (edit.row_index && *edit.row_index > static_cast<std::size_t>(std::numeric_limits<int>::max()))
      throw std::invalid_argument("ROI row exceeds the desktop range");
    const bool changed = edit.transaction.preview().data().rois != source.data().rois;
    if (changed && source.revisions().metadata == std::numeric_limits<std::uint64_t>::max())
      throw std::overflow_error("ROI manager Metadata revision overflow");
    std::set<std::size_t> selected;
    if (edit.row_index) selected.insert(*edit.row_index);
    auto retained = std::make_shared<const DocumentSnapshot>(source);
    // Everything which allocates is staged before the native atomic commit.
    owner.controller_->commit(edit.transaction, std::move(edit.display));
    roiManagerSource.swap(retained); roiSelectedIndices.swap(selected);
    roiRangeAnchor = edit.row_index;
    roiIndex = edit.row_index ? static_cast<int>(*edit.row_index) : -1;
    roiManagerPendingFocus.emplace(source.id(), source.revisions().metadata + (changed ? 1 : 0));
    savedRoiHistoryFocus.reset();
  }
  void savedRoiHistoryStep(const std::shared_ptr<Document> &document, bool redo) {
    cancelSavedRoiGeometry("history");
    const auto before = document->nativeSnapshot();
    const bool available = redo ? document->redoSteps() != 0 : document->undoSteps() != 0;
    if (!available) { if (redo) document->redo(); else document->undo(); return; }
    if (savedRoiHistoryFocus && !savedRoiHistoryFocus->after &&
        savedRoiHistoryFocus->document_id == before.id() &&
        savedRoiHistoryFocus->publication_metadata_revision == before.revisions().metadata)
      savedRoiHistoryFocus->after = before.data_identity();
    const bool known = savedRoiHistoryFocus &&
        savedRoiHistoryFocus->document_id == before.id() &&
        before.data_identity() &&
        before.data_identity() == (redo ? savedRoiHistoryFocus->before : savedRoiHistoryFocus->after) &&
        savedRoiHistoryFocus->row < before.data().rois.size();
    std::set<std::size_t> selected;
    std::shared_ptr<const DocumentSnapshot> source;
    std::size_t row = 0;
    if (known) {
      if (before.revisions().metadata == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("Saved ROI history Metadata revision overflow");
      row = savedRoiHistoryFocus->row; selected.insert(row);
      source = std::make_shared<const DocumentSnapshot>(before);
    }
    if (redo) document->redo(); else document->undo();
    if (known) {
      roiManagerSource.swap(source); roiSelectedIndices.swap(selected);
      roiRangeAnchor = row; roiIndex = static_cast<int>(row);
      roiManagerPendingFocus.emplace(before.id(), before.revisions().metadata + 1);
    } else invalidateRoiManager();
  }
  void selectAllRoiManagerRows() {
    synchronizeRoiManager();
    roiSelectedIndices.clear();
    roiRangeAnchor.reset();
    if (!roiManagerSource || roiManagerSource->data().rois.empty())
      return;
    const auto count = roiManagerSource->data().rois.size();
    for (std::size_t index = 0; index < count; ++index)
      roiSelectedIndices.insert(index);
    if (roiIndex < 0 || static_cast<std::size_t>(roiIndex) >= count)
      roiIndex = 0;
    roiRangeAnchor = static_cast<std::size_t>(roiIndex);
  }
  bool roiManagerCanCombine() const {
    if (!roiManagerSource || roiSelectedIndices.size() < 2 ||
        !actionAllowed("combine_rois", roiManagerSource->id()))
      return false;
    const auto &saved = roiManagerSource->data().rois;
    return std::all_of(roiSelectedIndices.begin(), roiSelectedIndices.end(),
                       [&](std::size_t index) {
                         return index < saved.size() &&
                                selection_is_area(saved[index].selection);
                       });
  }
  void observeRoiManagerControl(const std::string &name, bool enabled = true) {
    observeControl("roi_manager." + name, enabled);
    if (headlessDiagnostics)
      roiManagerControls[name] = hostControls["roi_manager." + name];
  }
  Json roiManagerDiagnostics() const {
    const auto base = roiSelectedIndices.empty()
                          ? Json(nullptr)
                          : Json(*roiSelectedIndices.begin());
    return {{"visible", roiManagerVisible}, {"focused", roiManagerFocused},
            {"document_id", roiManagerSource ? Json(roiManagerSource->id())
                                             : Json(nullptr)},
            {"metadata_revision",
             roiManagerSource ? Json(roiManagerSource->revisions().metadata)
                              : Json(nullptr)},
            {"selected_indices", roiSelectedIndices},
            {"anchor", roiRangeAnchor ? Json(*roiRangeAnchor) : Json(nullptr)},
            {"focused_index", roiIndex < 0 ? Json(nullptr) : Json(roiIndex)},
            {"subtraction_base_index", base},
            {"area_operands_valid", roiManagerCanCombine()},
            {"can_combine", roiManagerVisible && roiManagerCanCombine() &&
                                !owner.busy()},
            {"controls", roiManagerControls}, {"rows", roiManagerRows}};
  }
  Json headlessReport();
  void menu();
  void tools();
  void properties();
  void rois();
  void resultTable();
  void recorder();
  void plots();
  void dialogs();
  void canvas(Pane &pane);
  void draw();
  void geometry(const Json &o, Pane &pane, ImU32 tint, bool cache = true,
                const char *role = "selection");
  void finish(Pane &pane) {
    if (pane.points.empty())
      return;
    auto points = std::move(pane.points);
    pane.points = Json::array();
    pane.dragging = false;
    guard([&] {
      pane.doc->select(
          selection(tool, points,
                    tool == "point" || tool == "multipoint"
                        ? preferenceState.settings.number("point_size")
                        : preferenceState.settings.number("line_width")));
    });
  }
  bool refreshTexture(Pane &pane) {
    try {
    const auto liveSnapshot = pane.doc->nativeSnapshot();
    const auto position = pane.doc->position();
    auto channelState = recognized_channel_display(liveSnapshot.display(), liveSnapshot.data().dataset);
    const auto preview = planeOperationPreview(*pane.doc, !channelState.has_value());
    const bool rgbContrast=bool(preview) && bool(contrastPanel.rgbPreview(liveSnapshot));
    const auto previewGeneration = preview ? operationPreviewGeneration(*pane.doc) : 0;
    const auto *previewTarget = preview ? operationPreviewTarget(*pane.doc) : nullptr;
    const auto sources = channelState ? std::optional<ChannelDisplaySources>(
        std::in_place, liveSnapshot, preview,
        previewTarget ? std::optional<ResolvedTarget>(*previewTarget) : std::nullopt,
        previewGeneration, position) : std::nullopt;
    const bool activePreview = sources ? sources->usesPreview(position.c) : bool(preview);
    const auto displayEffects = preview && !rgbContrast && !shadowDemo.active()
        ? collectPlanePreviewDisplayEffects(liveSnapshot, position, channelState,
            sources ? &*sources : nullptr, activePreview,
            [this](const DocumentSnapshot &current, PlanePosition candidate) {
              return gaussianJobs.preview_display_effects(current, candidate);
            }, channelLimits())
        : std::vector<PlanePreviewDisplayEffects>{};
    Json displayMetadata{{"display_range", get(pane.doc->state().metadata, "display_range")}};
    if (rgbContrast) displayMetadata["display_range"] = {0., 255.};
    projectPlanePreviewDisplayEffects(displayMetadata, channelState, position, displayEffects);
    const auto displayKey = planePreviewDisplayKey(liveSnapshot, position, displayMetadata, channelState, displayEffects);
    // Shape and overlay geometry do not require a decoded source plane.
    const auto dimensions=(!sources && preview)?preview->data().dataset.shape():liveSnapshot.data().dataset.shape();
    const std::array<int, 2> shape{dimensions.x, dimensions.y};
    if (pane.overlayRevision != pane.doc->revision() ||
        pane.overlayT != position.t || pane.overlayC != position.c ||
        pane.overlaySlice != pane.doc->slice() || pane.overlayShape != shape) {
      const auto &metadata = pane.doc->state().metadata;
      const bool layered = hasStyledOverlays(metadata, position);
      auto layers = layered
                        ? overlayVisuals(metadata, shape[0], shape[1], position)
                        : std::vector<OverlayVisual>{};
      pane.overlays = std::move(layers);
      pane.layeredOverlays = layered;
      pane.overlayRevision = pane.doc->revision();
      pane.overlaySlice = pane.doc->slice();
      pane.overlayT = position.t;
      pane.overlayC = position.c;
      pane.overlayShape = shape;
      ++pane.overlayBuilds;
    }
    const bool capture = captureGaussianReadback();
    const bool background=preferenceState.settings.boolean("async_viewport") && !liveSnapshot.data().dataset.has_eager_planes() && !rankProbe;
    if (!gl && !capture && !background)
      return true;
    bool linear = preferenceState.settings.text("interpolation") == "linear";
    if (gl && pane.texture) {
      glBindTexture(GL_TEXTURE_2D, pane.texture);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                      linear ? GL_LINEAR : GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                      linear ? GL_LINEAR : GL_NEAREST);
    }
    const auto generation = activePreview ? previewGeneration : 0;
    const auto identity = reinterpret_cast<std::uintptr_t>(
        activePreview ? &preview->data() : &liveSnapshot.data());
    const auto channelKey = sources ? channelDisplayDiagnostics(*channelState,
        *sources, Plane::XY, position.z, position.t, position.c, false, channelLimits(), displayEffects) : Json(nullptr);
    bool uncachedPreview = false;
    if (sources && sources->generation() == 0)
      for (const auto &item : channelKey.at("contributors"))
        uncachedPreview = uncachedPreview || item.at("preview").get<bool>();
    const bool rebuild = (background && !pane.sampledSurface) || pane.revision != pane.doc->renderRevision() ||
        pane.pixelsPosition.t != position.t || pane.pixelsPosition.c != position.c ||
        pane.slice != pane.doc->slice() || pane.previewActive != activePreview ||
        pane.previewGeneration != generation || pane.pixelsIdentity != identity ||
        pane.channelDisplayKey != channelKey || pane.previewDisplayKey != displayKey || uncachedPreview ||
        (gl && (pane.textureDirty || !pane.texture));
    const bool missingReadback = capture && (!pane.sourceReadback || !pane.renderedReadback);
    if (!rebuild && !missingReadback)
      return true;
    auto metadata = pane.doc->state().metadata;
    if (rgbContrast) metadata["display_range"] = {0., 255.};
    projectPlanePreviewDisplayEffects(metadata, channelState, position, displayEffects);
    const auto memoryPlan=RenderMemory::plan(shape[0],shape[1],dimensions.s,gl,capture,bool(rankProbe));
    RenderMemory::Lease memory;
    std::optional<ViewportJobs::Frame> asynchronous;
    std::shared_ptr<const Image> framePixels;
    std::optional<Image> rendered;
    auto samplePolicy=liveSnapshot.data().dataset.sample_policy();
    if(background){
      pane.doc->admitView();
      ViewportRender spec{liveSnapshot};spec.preview=preview;spec.previewGeneration=previewGeneration;spec.position=position;spec.coordinate=position.z;
      if(previewTarget)spec.target=*previewTarget;if(rgbContrast)spec.previewTargets=contrastPanel.rgbPreviewTargets(liveSnapshot);
      spec.metadata=metadata;spec.previewDisplayEffects=displayEffects;spec.layered=pane.layeredOverlays;spec.capture=capture;spec.channelLimits=channelLimits();
      const auto key=Json{{"identity",identity},{"live",liveSnapshot.data_identity().value_or(0)},{"revision",pane.doc->renderRevision()},
          {"tcz",{position.t,position.c,position.z}},{"preview",generation},{"channels",channelKey},{"display_effects",displayKey},{"capture",capture},{"gl",gl}}.dump();
      auto ready=viewJobs().request(pane.doc->id,0,key,memoryPlan.peak,[spec=std::move(spec)](const ExecutionControl &){return renderViewport(spec);});
      if(!ready){pane.viewportPending=true;pane.sampledSurface.reset();pane.sampleMemory.reset();pane.sourceReadback.reset();pane.renderedReadback.reset();pane.readbackMemory.reset();status="Loading view...";return false;}
      asynchronous.emplace(std::move(*ready));memory=std::move(asynchronous->reservation);
      auto &surface=*asynchronous->surface;framePixels=surface.source;rendered=std::move(surface.rgb);samplePolicy=surface.policy;pane.effectiveRange=surface.range;
      pane.channelDisplayReadback=surface.channels;
      pane.sampledSurface.reset();pane.sampleMemory.reset();
      pane.sampleMemory=memory.retain(RenderMemory::mul(framePixels->pixels.capacity(),sizeof(double)));
      pane.sampledSurface=framePixels;
      pane.revision=pane.doc->renderRevision();pane.slice=position.z;pane.previewActive=activePreview;pane.channelDisplayKey=channelKey;
      pane.previewGeneration=generation;pane.pixelsIdentity=identity;pane.pixelsPosition=position;pane.rangePosition=position;
    }else{
      pane.sampledSurface.reset();pane.sampleMemory.reset();
    memory=renderMemory->acquire(memoryPlan.peak);
    // Acquire pixels only after the complete texture/readback key misses.
    const auto previewPixels = sources ? sources->provider()(Plane::XY, position.z, position.t, position.c) : preview
        ? preview->data().dataset.plane(position) : nullptr;
    framePixels=previewPixels?previewPixels:pane.doc->sharedFrame();
    const auto &frame=*framePixels;
    samplePolicy=previewPixels && !sources?preview->data().dataset.sample_policy():liveSnapshot.data().dataset.sample_policy();
    if (!rebuild)
      ++pane.diagnosticRebuilds;
    pane.revision = pane.doc->renderRevision();
    pane.slice = pane.doc->slice();
    pane.previewActive = activePreview;
    pane.channelDisplayKey = channelKey;
    pane.channelDisplayReadback = sources ? channelDisplayDiagnostics(*channelState,
        *sources, Plane::XY, position.z, position.t, position.c, capture, channelLimits(), displayEffects) : Json(nullptr);
    pane.previewGeneration = generation;
    pane.pixelsIdentity = identity;
    pane.pixelsPosition = position;
    pane.rangePosition = position;
    // Ordinary filters retain live contrast. Only a matching Top Hat display
    // proposal has changed the render-local range above. Otherwise freeze the
    // ordinary floating auto-range from live pixels when no range is explicit.
    std::optional<std::array<std::uint32_t,2>> computed;
    std::optional<std::array<double,2>> automaticBounds;
    if (!channelState && !get(metadata, "display_range").is_array() &&
        (frame.type == PixelType::Float32 || frame.type == PixelType::Float64)) {
      const auto &live = pane.doc->frame();
      const auto bounds = sourceDisplayRange(live, liveSnapshot.data().dataset.sample_policy(), true);
      automaticBounds=bounds;
      if (samplePolicy==FloatingSamplePolicy::PreserveIEEEFloat32) computed=displayRangeBits(bounds);
      else metadata["display_range"] = {bounds[0], bounds[1]};
    }
    const auto &range = get(metadata, "display_range");
    pane.effectiveRange = channelState ? channelState->channels.at(position.c).range : automaticBounds ? *automaticBounds : range.is_array() && range.size() == 2
        ? std::array<double, 2>{number(range[0]), number(range[1])}
        : std::array<double, 2>{0, type_max(frame.type)};
    auto image = channelState ? compose_channel_display(liveSnapshot.data().dataset,
        *channelState, Plane::XY, position.z, position.t, position.c, sources->provider(), channelLimits())
        : renderImage(frame, metadata, !pane.layeredOverlays, samplePolicy, position,computed);
    if (channelState && !pane.layeredOverlays) renderDisplayOverlays(image, metadata, position);
    const bool activeContributes = !channelState || std::any_of(
        channelKey.at("contributors").begin(), channelKey.at("contributors").end(),
        [&](const Json &item) { return item.at("c") == position.c; });
    const auto *displayEffect = planePreviewDisplayEffect(displayEffects, position);
    if (activeContributes && !(displayEffect && displayEffect->suppress_threshold_overlay))
      renderThresholdOverlay(image, pane.doc->frame(), liveSnapshot,
                               {liveSnapshot.display().t, liveSnapshot.display().c, pane.doc->slice()});
    rendered=std::move(image);
    }
    pane.previewDisplayKey=displayKey;
    pane.viewportPending=false;
    const auto &frame=*framePixels;
    auto &image=*rendered;
    ++pane.pixelBuilds;
    pane.textureDirty = !gl;
    if (capture) {
      // Deep values from the actual cache build, never retained source aliases.
      pane.sourceReadback.reset();pane.renderedReadback.reset();pane.readbackMemory.reset();
      pane.sourceReadback = frame;
      pane.renderedReadback = image;
      pane.readbackMemory=memory.retain(RenderMemory::mul(RenderMemory::add(pane.sourceReadback->pixels.capacity(),pane.renderedReadback->pixels.capacity()),sizeof(double)));
      pane.sourceReadbackPolicy = samplePolicy;
    } else {
      pane.sourceReadback.reset();
      pane.renderedReadback.reset();
      pane.readbackMemory.reset();
      pane.sourceReadbackPolicy = FloatingSamplePolicy::FiniteOnly;
    }
    if (!gl)
      return true;
    std::vector<unsigned char> pixels(image.pixels.size());
    for (size_t i = 0; i < pixels.size(); ++i)
      pixels[i] = static_cast<unsigned char>(image.pixels[i]);
    if (!pane.texture)
      glGenTextures(1, &pane.texture);
    glBindTexture(GL_TEXTURE_2D, pane.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    linear ? GL_LINEAR : GL_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image.width, image.height, 0, GL_RGB,
                 GL_UNSIGNED_BYTE, pixels.data());
    pane.textureMemory=memory.retain(memoryPlan.texture);
    if (rankProbe) {
      const auto &consumed = activePreview ? *preview : liveSnapshot;
      pane.rankTexture = {{"valid", glGetError() == GL_NO_ERROR},
          {"document_id", pane.doc->id},
          {"source_kind", activePreview ? "preview" : "source"},
          {"tcz", {position.t, position.c, position.z}},
          {"pixel_revision", consumed.revisions().pixels},
          {"data_identity", consumed.data_identity() ? Json(*consumed.data_identity()) : Json()},
          {"preview_generation", generation}, {"upload_serial", ++rankUploadSerial}};
      // Own copied bytes only. Hashing and JSON/hex serialization occur after
      // the CPU render-submission timestamp, never while retaining a source.
      std::vector<unsigned char>().swap(pane.rankScalarBytes);std::vector<unsigned char>().swap(pane.rankRgbBytes);pane.rankMemory.reset();
      pane.rankScalarBytes = rankProbeDeclaredBytes(frame);
      pane.rankRgbBytes = std::move(pixels);
      pane.rankMemory=memory.retain(RenderMemory::add(pane.rankScalarBytes.capacity(),pane.rankRgbBytes.capacity()));
    }
    } catch (...) {
      pane.viewportPending=false;pane.sampledSurface.reset();pane.sampleMemory.reset();
      // A failed composition/upload must never mark an older texture or RGB
      // diagnostic as current under the new state, source, or resource limit.
      pane.revision = ~std::uint64_t(0);
      pane.textureDirty = true;
      pane.sourceReadback.reset(); pane.renderedReadback.reset();pane.readbackMemory.reset();
      pane.channelDisplayKey = nullptr; pane.channelDisplayReadback = nullptr;
      pane.rankTexture = {{"valid", false}};
      std::vector<unsigned char>().swap(pane.rankScalarBytes);std::vector<unsigned char>().swap(pane.rankRgbBytes);pane.rankMemory.reset();
      throw;
    }
    return true;
  }
};
Json RankRuntimeProbeAccess::inspect(Window &window, bool viewBytes) {
  window.checkThread();
  auto &state = *window.impl;
  Json result{{"gaussian_jobs", metadataJson(state.gaussianJobs.diagnostics(true))},
      {"rank_panel", state.rankPanel.diagnostics()},
      {"jobs_panel", state.jobsPanel.diagnostics()},
      {"workflow", window.workflow}, {"results", window.results},
      {"recording", window.recording}, {"settings", settingsJson(window.settings())},
      {"error", state.lastError}, {"consumed", state.rankConsumed},
      {"document_info", nullptr}};
  if (window.documents_.empty()) return result;
  const auto document = window.document();
  const auto source = document->nativeSnapshot();
  const auto shape = source.data().dataset.shape();
  const auto &calibration = source.data().dataset.calibration();
  const auto revisions = source.revisions();
  auto found = state.rankSelectionHashes.find(source.id());
  if (found == state.rankSelectionHashes.end() || found->second.first != revisions.selection) {
    const auto text = source.data().selection ? selectionJson(*source.data().selection).dump() : "null";
    state.rankSelectionHashes[source.id()] = {revisions.selection,
        rankRuntimeSha256(std::vector<unsigned char>(text.begin(), text.end()))};
  }
  result["document_info"] = {{"id", source.id()},
      {"dtype", typeName(source.data().dataset.pixel_type())},
      {"shape", shape.dimensions()},
      {"calibration", {{"spacing", calibration.spacing}, {"origin", calibration.origin},
          {"direction", calibration.direction}, {"unit", calibration.unit},
          {"frame_interval", calibration.frame_interval}, {"time_origin", calibration.time_origin},
          {"time_unit", calibration.time_unit}}},
      {"current_tcz", {source.display().t, source.display().c, source.display().crosshair[2]}},
      {"data_identity", source.data_identity() ? Json(*source.data_identity()) : Json()},
      {"revisions", {{"pixels", revisions.pixels}, {"metadata", revisions.metadata},
          {"selection", revisions.selection}, {"display", revisions.display}}},
      {"selection_sha256", state.rankSelectionHashes.at(source.id()).second},
      {"dirty", source.dirty()}, {"undo_steps", document->undoSteps()},
      {"redo_steps", document->redoSteps()}, {"display", document->viewJson()}};
  if (viewBytes)
    result["document_info"]["selection"] = source.data().selection
        ? selectionJson(*source.data().selection) : Json();
  auto &pane = state.panes.at(window.activeIndex());
  if (state.rankConsumed.value("valid", false) &&
      state.rankConsumed.at("upload_serial") == pane.rankTexture.at("upload_serial")) {
    if (!pane.rankTexture.contains("scalar_sha256")) {
      pane.rankTexture["scalar_sha256"] = rankRuntimeSha256(pane.rankScalarBytes);
      pane.rankTexture["rgb_sha256"] = rankRuntimeSha256(pane.rankRgbBytes);
    }
    result["consumed"].update(pane.rankTexture);
    if (viewBytes) {
      result["consumed"]["scalar_declared_hex"] = rankProbeHex(pane.rankScalarBytes);
      result["consumed"]["rgb_hex"] = rankProbeHex(pane.rankRgbBytes);
    }
  }
  return result;
}
Json SettingsAppearanceUiProbeAccess::inspect(Window &window) {
  window.checkThread();
  auto &state = *window.impl;
  if (!state.appearanceProbe || !state.gl || !ImGui::GetCurrentContext())
    throw std::logic_error("Appearance observations require the active OpenGL probe context");
  const auto *draw = ImGui::GetDrawData();
  if (!draw || !draw->Valid)
    throw std::logic_error("Appearance observations require a rendered OpenGL probe frame");
  auto result = state.headlessReport();
  const auto &io = ImGui::GetIO();
  const auto &style = ImGui::GetStyle();
  const auto &background = style.Colors[ImGuiCol_WindowBg];
  std::array<GLint, 4> viewport{};
  std::array<GLfloat, 4> clearColor{};
  glGetIntegerv(GL_VIEWPORT, viewport.data());
  glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor.data());
  const auto glString = [](GLenum name) {
    const auto *value = glGetString(name);
    if (!value)
      throw std::runtime_error("Appearance observations require OpenGL identity strings");
    return std::string(reinterpret_cast<const char *>(value));
  };
  // GL_SHADING_LANGUAGE_VERSION is absent from Windows OpenGL 1.1 headers.
  constexpr GLenum shadingLanguageVersion = 0x8B8C;
  result["appearance_runtime"] = {
      {"frame", state.appearanceFrame},
      {"vendor", glString(GL_VENDOR)},
      {"renderer", glString(GL_RENDERER)},
      {"version", glString(GL_VERSION)},
      {"shading_language_version", glString(shadingLanguageVersion)},
      {"actual_dpi", state.appearanceActualDpi},
      {"supplied_dpi", state.appearanceSuppliedDpi},
      {"framebuffer_size", state.appearanceFramebuffer},
      {"window_size", state.appearanceWindow},
      {"requested_window_size", {2560, 1600}},
      {"viewport", viewport}, {"clear_color", clearColor},
      {"display_pos", {draw->DisplayPos.x, draw->DisplayPos.y}},
      {"display_size", {io.DisplaySize.x, io.DisplaySize.y}},
      {"framebuffer_scale", {io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y}},
      {"frame_padding", {style.FramePadding.x, style.FramePadding.y}},
      {"window_background_rgba", {background.x, background.y, background.z, background.w}}};
  // Query only already-created textures while the owning context is live.
  // The returned values own no texture, document snapshot, lease or GL pointer.
  const auto sampler = [](GLuint texture) -> Json {
    if (!texture || !glIsTexture(texture))
      return {{"available", false}, {"texture_id", texture}};
    struct RestoreBinding {
      GLint value = 0;
      RestoreBinding() { glGetIntegerv(GL_TEXTURE_BINDING_2D, &value); }
      ~RestoreBinding() { glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(value)); }
    } binding;
    glBindTexture(GL_TEXTURE_2D, texture);
    GLint minimum = 0, maximum = 0, wrapS = 0, wrapT = 0, width = 0, height = 0;
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &minimum);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &maximum);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &wrapS);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &wrapT);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    return {{"available", true}, {"texture_id", texture},
            {"min_filter", minimum}, {"mag_filter", maximum},
            {"wrap_s", wrapS}, {"wrap_t", wrapT},
            {"width", width}, {"height", height}};
  };
  const auto imageRect = [](ImVec2 origin, ImVec2 scale, int width, int height) {
    return Json::array({origin.x, origin.y, origin.x + width * scale.x,
                        origin.y + height * scale.y});
  };
  const auto canvasRect = [](ImVec2 center, ImVec2 extent) {
    return Json::array({center.x - extent.x * .5f, center.y - extent.y * .5f,
                        center.x + extent.x * .5f, center.y + extent.y * .5f});
  };
  result["source"] = nullptr;
  if (window.documents_.empty()) return result;
  const auto document = window.document();
  const auto source = document->nativeSnapshot();
  const auto &data = source.data();
  const auto shape = data.dataset.shape();
  const auto revisions = source.revisions();
  result["source"] = {
      {"id", source.id()}, {"data_identity", source.data_identity() ? Json(*source.data_identity()) : Json()},
      {"shape", shape.dimensions()}, {"dtype", typeName(data.dataset.pixel_type())},
      {"revisions", {{"pixels", revisions.pixels}, {"metadata", revisions.metadata},
                     {"selection", revisions.selection}, {"display", revisions.display}}},
      {"display", document->viewJson()}, {"dirty", source.dirty()},
      {"undo_steps", document->undoSteps()}, {"redo_steps", document->redoSteps()},
      {"selection", data.selection ? selectionJson(*data.selection) : Json()},
      {"previous", data.previous_selection ? selectionJson(*data.previous_selection) : Json()},
      {"metadata", {{"title", data.metadata.title},
                    {"properties", metadataJson(data.metadata.properties)},
                    {"extensions", metadataJson(data.metadata.extensions)},
                    {"record_layout", metadataJson(data.metadata.record_layout)}}}};
  const auto &pane = state.panes.at(window.activeIndex());
  auto &ordinary = result["ordinary_canvas"];
  ordinary["width"] = shape.x;
  ordinary["height"] = shape.y;
  ordinary["image_rect"] = imageRect(pane.origin, {pane.zoom, pane.zoom * pane.aspectY}, shape.x, shape.y);
  ordinary["canvas_rect"] = canvasRect(pane.ordinaryCanvasPoint, pane.extent);
  ordinary["sampler"] = sampler(pane.texture);
  auto &planes = result["orthogonal_planes"];
  for (std::size_t i = 0; i < pane.orthogonal.caches_.size(); ++i) {
    const auto &cache = pane.orthogonal.caches_[i];
    planes.at(i)["image_rect"] = imageRect(cache.origin, cache.scale, cache.width, cache.height);
    planes.at(i)["canvas_rect"] = canvasRect(cache.canvasPoint, cache.extent);
    planes.at(i)["sampler"] = sampler(cache.texture);
  }
  return result;
}
void RankRuntimeProbeAccess::openRank(Window &window, RankFilterMethod method) {
  window.checkThread();
  if (!window.impl->rankProbe || !window.impl->gl || !ImGui::GetCurrentContext())
    throw std::logic_error("Rank runtime setup requires its active normal OpenGL loop");
  window.impl->rankPanel.show(window.nativeSnapshot(), method);
}
void RankRuntimeProbeAccess::showJobs(Window &window) {
  window.checkThread();
  window.impl->jobsPanel.show();
}
Job GaussianCancellationAccess::startBusyPreparation(
    Window &window, std::function<void(const JobContext &)> gate) {
  window.checkThread();
  if (!gate || !window.impl->headlessSessionActive ||
      ImGui::GetCurrentContext() == nullptr || window.busy() ||
      window.documents_.empty() || window.impl->diagnosticWorkerGate)
    throw std::invalid_argument("Busy preparation requires an idle live headless source and a fresh gate");
  window.impl->diagnosticWorkerGate = std::move(gate);
  try {
    window.applyOperation("add", {{"value", 0}}, false, true);
    std::lock_guard<std::mutex> lock(window.impl->jobMutex);
    if (!window.impl->job.valid())
      throw std::logic_error("Busy preparation did not submit its actual native job");
    return window.impl->job;
  } catch (...) {
    window.impl->diagnosticWorkerGate = {};
    throw;
  }
}
void GaussianCancellationAccess::showBusyGaussian(Window &window) {
  window.checkThread();
  if (!window.impl->headlessSessionActive || ImGui::GetCurrentContext() == nullptr ||
      !window.busy() || window.documents_.empty())
    throw std::invalid_argument("Busy Gaussian opening is private declared action preparation only");
  window.impl->operation({{"id", "gaussian_blur_imagej"}});
}
void AxisPlaneUiProbeAccess::setPublicationProbe(
    Window &window, AxisPlaneUiPublicationProbe probe) {
  window.checkThread();
  window.impl->axisPlaneUiPublicationProbe = std::move(probe);
}
void AxisRangeUiProbeAccess::setPublicationProbe(
    Window &window, AxisRangeUiPublicationProbe probe) {
  window.checkThread();
  window.impl->axisRangeUiPublicationProbe = std::move(probe);
}
void AxisRangeOrientationUiProbeAccess::setPublicationProbe(
    Window &window, AxisRangeOrientationUiPublicationProbe probe) {
  window.checkThread();
  window.impl->axisRangeOrientationUiPublicationProbe = std::move(probe);
}
Application &GaussianCancellationAccess::application(Window &window) {
  window.checkThread();
  return *window.controller_;
}
std::shared_ptr<JobScheduler>
GaussianCancellationAccess::scheduler(Window &window) {
  window.checkThread();
  return window.impl->gaussianCancellationScheduler;
}
void GaussianCancellationAccess::setHostProbe(Window &window,
                                             GaussianHostProbe probe) {
  window.checkThread();
  auto next = probe ? std::make_shared<const GaussianHostProbe>(std::move(probe))
                    : nullptr;
  window.impl->gaussianHostProbe.swap(next);
}
void GaussianCancellationAccess::request(
    Window &window, const ResolvedGaussianBlurCommand &command,
    std::optional<std::uint64_t> generation) {
  window.checkThread();
  window.impl->requestGaussian(command, generation);
}
std::optional<OperationJob>
GaussianCancellationAccess::laneJob(Window &window, bool preview) {
  window.checkThread();
  return preview ? window.impl->gaussianJobs.preview_job()
                 : window.impl->gaussianJobs.apply_job();
}
Json GaussianCancellationAccess::diagnostics(const Window &window) {
  window.checkThread();
  Json result{{"gaussian_jobs", metadataJson(window.impl->gaussianJobs.diagnostics())},
              {"actual_lane_ids", window.impl->gaussianJobs.active_job_ids()}};
  {
    std::lock_guard<std::mutex> lock(window.impl->jobMutex);
    result["admission_epoch"] = window.impl->gaussianAdmissionEpoch;
    result["current_epoch"] = window.impl->gaussianCancelEpoch;
    result["drained_epoch"] = window.impl->gaussianDrainedEpoch;
    result["mirrored_ids"] = window.impl->gaussianCancelIds;
  }
  return result;
}
Window::Window(std::string path)
    : impl(std::make_unique<Impl>(*this, std::move(path))) {}
Window::~Window() {
  cancel();
  if (impl->autoJob) impl->autoJob->wait();
  if(impl->contrastApplyJob)impl->contrastApplyJob->wait();
  if (impl->histogramTask) impl->histogramTask->job.wait();
  impl->gaussianPanel.cancel([this] { impl->cancelGaussianPreview(); });
  impl->rankPanel.cancel([this] { impl->cancelGaussianPreview(); });
  impl->drainGaussianCancellation();
  if (impl->job.valid())
    impl->job.wait();
  for (const auto &document : documents_) {
    controller_->close(document->id, true);
    document->releaseFrontend();
  }
}
Settings Window::settings() const {
  checkThread();
  auto result = impl->preferenceState.settings;
  result.update(
      {{"history_limit", static_cast<std::int64_t>(impl->historyLimit)},
       {"animation_fps", impl->fps},
       {"foreground", impl->paintColor()},
       {"background", impl->paintColor(false)},
       {"show_rois", impl->showRois}});
  if (impl->brush != static_cast<float>(result.number("brush_width")))
    result.set("brush_width", static_cast<double>(impl->brush));
  return result;
}
Json Window::preferencesJson() const {
  checkThread();
  auto result = impl->preferenceState;
  result.settings = settings();
  result.extras = impl->preferences;
  return result.json();
}
void Window::updateSettings(const Json &values, bool persist) {
  checkThread();
  auto proposed = impl->preferenceState;
  proposed.settings = settings();
  proposed.settings.update(settingsPatch(values));
  proposed.extras = impl->preferences;
  impl->applyPreferences(std::move(proposed), persist);
}
namespace {
const char *startupSourceName(StartupSource source) {
  switch (source) {
  case StartupSource::Empty: return "empty";
  case StartupSource::ExplicitPaths: return "explicit_paths";
  case StartupSource::ExplicitWorkspace: return "explicit_workspace";
  case StartupSource::PersistedWorkspace: return "persisted_workspace";
  }
  throw std::invalid_argument("Unknown startup source");
}
Json startupDiagnosticJson(const StartupDiagnostic &diagnostic) {
  return {{"stage", startup_stage_name(diagnostic.stage)},
          {"reason", startup_reason_name(diagnostic.reason)},
          {"path", diagnostic.path}, {"detail", diagnostic.detail},
          {"recoverable", diagnostic.recoverable}};
}
MetadataArray startupTable(const Json &table) {
  if (!table.is_array()) throw std::invalid_argument("Startup table must be an array");
  MetadataArray result;
  result.reserve(table.size());
  for (const auto &value : table) result.push_back(metadataFromJson(value));
  return result;
}
Json startupTable(const MetadataArray &table) {
  Json result = Json::array();
  for (const auto &value : table) result.push_back(metadataJson(value));
  return result;
}
} // namespace
Json Window::startupState() const {
  checkThread();
  Json diagnostics = Json::array(), ids = Json::array();
  for (const auto &value : impl->startupDiagnostics)
    diagnostics.push_back(startupDiagnosticJson(value));
  for (const auto &document : documents_) ids.push_back(document->id);
  Json output = {{"schema_version", 1}, {"attempted", impl->startupAttempted},
          {"completed", impl->startupCompleted}, {"status", impl->startupStatus},
          {"source", startupSourceName(impl->startupPlan.source)},
          {"paths", impl->startupPlan.paths}, {"automatic", impl->startupAutomatic},
          {"reset_layout", impl->suppressSavedLayout},
          {"retry_allowed", impl->startupRetryAllowed()},
          {"preferences_write_path", impl->preferencesPath},
          {"active_index", documents_.empty() ? Json() : Json(activeIndex())},
          {"document_ids", std::move(ids)}, {"diagnostics", std::move(diagnostics)},
          {"layout", {{"saved_applied", impl->savedLayoutApplied},
                      {"loaded_bytes", impl->loadedLayoutBytes},
                      {"reset_generation", impl->layoutResetGeneration}}}};
  if(impl->launchStarted)output["append"]=impl->launchAppendState();
  return output;
}
Json Window::startupObservation(const std::string &context) const {
  checkThread();
  if (context != "cpp" && context != "python")
    throw std::invalid_argument("Unknown launcher observation context");
  const auto runtimeBefore = startupRuntimeIdentity();
  Json observed = Json::array();
  std::size_t remainingSamples = 4096;
  if (documents_.size() > 4)
    throw std::length_error("Launcher observation supports at most four documents");
  for (const auto &document : documents_) {
    auto info = documentInfo(document->id);
    const auto source = document->nativeSnapshot();
    const auto &dataset = source.data().dataset;
    const auto shape = dataset.shape();
    const auto &calibration = dataset.calibration();
    info["shape"] = shape.dimensions();
    info["display"] = document->viewJson();
    info["calibration"] = {{"spacing", calibration.spacing}, {"origin", calibration.origin},
        {"direction", calibration.direction}, {"unit", calibration.unit},
        {"frame_interval", calibration.frame_interval}, {"time_origin", calibration.time_origin},
        {"time_unit", calibration.time_unit}};
    Json planes = Json::array();
    bool complete = true;
    // Only eager planes are admitted by this startup slice. The observation
    // budget never expands an arbitrary source into an unbounded pixel dump.
    const auto shared = dataset.shared_planes();
    if (!shared || shared->size() > 128) complete = false;
    else {
      std::size_t count = 0;
      for (const auto &plane : *shared) {
        if (plane.pixels.size() > remainingSamples - count) { complete = false; break; }
        count += plane.pixels.size();
      }
      if (complete) {
        for (const auto &plane : *shared) planes.push_back(plane.pixels);
        remainingSamples -= count;
      }
    }
    info["all_samples_observed"] = complete;
    info["planes"] = std::move(planes);
    observed.push_back(std::move(info));
  }
  auto output = Json{{"schema_version", 1}, {"context", context},
      {"observation_kind", "normal_launcher"}, {"startup", startupState()},
      {"document_infos", std::move(observed)}, {"results", results}, {"workflow", workflow},
      {"preferences", preferencesJson()}, {"preference_warnings", impl->preferenceState.warnings},
      {"preferences_write_blocked", impl->preferenceState.writeBlocked},
      {"session_extensions", impl->sessionExtensions}};
  output["runtime_before"] = runtimeBefore;
  output["runtime_after"] = startupRuntimeIdentity();
  if (output.dump().size() > 16 * 1024 * 1024)
    throw std::length_error("Launcher observation exceeds its 16 MiB bound");
  return output;
}
void Window::startStartup(const StartupRequest &request) {
  checkThread();
  if (impl->startupAttempted || impl->startupFramesDrawn ||
      impl->startupUserOpened || !documents_.empty() || busy())
    throw StartupError({StartupStage::Planning, StartupReason::InvalidRequest, {},
                        "Startup requires a fresh, idle Window before its first frame", false});
  executeStartup(request, false);
}
void Window::executeStartup(StartupRequest request, bool retry) {
  if (retry && !impl->startupRetryAllowed())
    throw StartupError({StartupStage::Planning, StartupReason::InvalidRequest, {},
                        "Retry requires an empty idle launch before any user document was opened", false});
  if (request.launch_cwd.empty()) request.launch_cwd = impl->launchDirectory;
  request.preferences_write_path = impl->preferencesPath;
  PreferencesDocument preferences;
  preferences.settings = settings();
  preferences.extras_json = impl->preferences.dump();
  preferences.unknown_values_json = impl->preferenceState.unknownValues.dump();
  preferences.unknown_fields_json = impl->preferenceState.unknownFields.dump();
  preferences.write_blocked = impl->preferenceState.writeBlocked;
  const bool automatic = request.explicit_paths.empty() && !request.explicit_workspace &&
      request.restore_workspace == StartupOverride::Inherit &&
      preferences.settings.text("startup_mode") == "workspace";
  StartupPlan plan;
  plan.source = automatic ? StartupSource::PersistedWorkspace : StartupSource::Empty;
  plan.reset_layout = request.reset_layout;
  std::vector<StartupDiagnostic> diagnostics;
  Documents opened;
  std::vector<bool> defaults;
  auto nextResults = results, nextWorkflow = workflow;
  auto nextExtensions = impl->sessionExtensions;
  auto nextPreferences = impl->preferences;
  auto nextSizes = impl->sessionSizes;
  std::optional<std::size_t> active;
  std::string currentPath;
  StartupStage stage = StartupStage::Planning;
  StartupReason reason = StartupReason::InvalidRequest;
  try {
    plan = plan_startup(preferences, request);
    diagnostics = plan.diagnostics;
#ifdef IMAGEJXX_HAS_SCIENTIFIC_IO
    if(impl->beginStartupTiffs(request,plan,diagnostics))return;
#endif
    SessionCapture aggregate;
    aggregate.results = startupTable(nextResults);
    aggregate.workflow = startupTable(nextWorkflow);
    aggregate.extensions = metadataObjectFromJson(nextExtensions);
    for (const auto &path : plan.paths) {
      currentPath = path;
      stage = StartupStage::Reading;
      reason = StartupReason::ReadFailed;
      const bool session = plan.source != StartupSource::ExplicitPaths || extension(path) == ".ijx";
      if (session) {
        auto decoded = imagejxx::load_session(path);
        stage = StartupStage::Validation;
        reason = StartupReason::UnsupportedDocument;
        const auto offset = opened.size();
        const auto presentation = workspace_presentation(decoded.extensions, decoded.documents.size());
        diagnostics.insert(diagnostics.end(), presentation.diagnostics.begin(), presentation.diagnostics.end());
        for (auto &owner : decoded.documents) {
          auto facade = std::make_shared<Document>(std::move(owner));
          facade->path = decoded.documents.size() == 1 ? path : "";
          aggregate.documents.push_back(facade->nativeSnapshot());
          opened.push_back(std::move(facade));
          defaults.push_back(false);
        }
        if (!decoded.documents.empty())
          active = offset + presentation.active_index.value_or(decoded.documents.size() - 1);
        aggregate.results = decoded.results;
        aggregate.workflow = decoded.workflow;
        aggregate.extensions = decoded.extensions;
        nextResults = startupTable(decoded.results);
        nextWorkflow = startupTable(decoded.workflow);
        nextExtensions = metadataJson(decoded.extensions);
        const auto target = std::filesystem::u8path(path);
        for (auto &[existing, size] : nextSizes)
          if (samePath(existing, target)) size = decoded.documents.size();
        nextSizes[target] = decoded.documents.size();
      } else {
        std::shared_ptr<Document> facade;
#ifdef IMAGEJXX_HAS_SCIENTIFIC_IO
        if(impl->startupTiffs && (extension(path)==".tif" || extension(path)==".tiff")) {
          const auto found=std::find_if(impl->startupTiffs->entries.begin(),impl->startupTiffs->entries.end(),[&](const auto &entry){return entry.path==path;});
          if(found==impl->startupTiffs->entries.end() || !found->document || !found->revalidate)throw std::invalid_argument("Startup TIFF has not been reviewed");
          found->revalidate();facade=found->document;
        } else
#endif
        facade = std::make_shared<Document>(readImages(path), filename(path));
        facade->path = path;
        aggregate.documents.push_back(facade->nativeSnapshot());
        active = opened.size();
        opened.push_back(std::move(facade));
        defaults.push_back(true);
      }
      stage = StartupStage::Validation;
      reason = StartupReason::AdmissionFailed;
      // Conservative logical/session-shaped admission; virtual pixels stay lazy.
      // Existing per-reader limits still
      // own their scratch. This is not a whole-process preallocation guarantee.
      (void)detail::estimate_startup_admission(aggregate);
      Json recent = Json::array({path});
      for (const auto &old : get(nextPreferences, "recent"))
        if (old != path && recent.size() < 15) recent.push_back(old);
      nextPreferences["recent"] = std::move(recent);
    }
    std::string status = opened.empty() ? "empty" : "loaded";
    stage = StartupStage::Publication;
    reason = StartupReason::PublicationFailed;
    // Keep retired providers alive until every facade and workspace table is
    // installed. Their destructors may re-enter public owner-thread APIs.
#ifdef IMAGEJXX_HAS_SCIENTIFIC_IO
    if(impl->startupTiffs)for(const auto &entry:impl->startupTiffs->entries)entry.revalidate();
#endif
    auto retirement = attachStartupDocuments(opened, defaults, active);
    // All fallible staging precedes native publication. The remaining state
    // installation is allocation-free, with retirement outside native locks.
    static_assert(std::is_nothrow_move_assignable_v<StartupRequest>);
    static_assert(std::is_nothrow_move_assignable_v<StartupPlan>);
    static_assert(noexcept(results.swap(nextResults)));
    static_assert(noexcept(impl->sessionSizes.swap(nextSizes)));
    results.swap(nextResults);
    workflow.swap(nextWorkflow);
    impl->sessionExtensions.swap(nextExtensions);
    impl->sessionSizes.swap(nextSizes);
    impl->preferences.swap(nextPreferences);
    impl->startupRequest = std::move(request);
    impl->startupPlan = std::move(plan);
    impl->startupDiagnostics.swap(diagnostics);
    impl->startupStatus.swap(status);
    impl->startupAttempted = impl->startupCompleted = true;
    impl->startupAutomatic = automatic;
    impl->startupNoticesDismissed = false;
    impl->suppressSavedLayout = impl->startupPlan.reset_layout;
  } catch (const std::exception &error) {
    StartupDiagnostic failure{stage, reason, currentPath,
                              std::string(error.what()).substr(0, 2048), automatic};
    if (const auto *typed = dynamic_cast<const StartupError *>(&error))
      failure = typed->diagnostic();
    if (failure.path.empty()) failure.path = currentPath;
    failure.recoverable = automatic;
    if (!automatic) throw StartupError(std::move(failure));
    failure = StartupError(std::move(failure)).diagnostic();
    diagnostics.push_back(std::move(failure));
    std::string status = "failed";
    impl->startupRequest = std::move(request);
    impl->startupPlan = std::move(plan);
    impl->startupDiagnostics.swap(diagnostics);
    impl->startupStatus.swap(status);
    impl->startupAttempted = true;
    impl->startupCompleted = false;
    impl->startupAutomatic = true;
    impl->startupNoticesDismissed = false;
    impl->suppressSavedLayout = impl->startupPlan.reset_layout;
  }
}
void Window::retryStartup() {
  checkThread();
  executeStartup(impl->startupRequest, true);
}
void Window::startEmptyStartup() {
  checkThread();
  if (!impl->startupAttempted || impl->startupStatus != "failed")
    throw StartupError({StartupStage::Planning, StartupReason::InvalidRequest, {},
                        "There is no failed startup to dismiss", false});
  std::string status = "started_empty";
  impl->startupStatus.swap(status);
  impl->startupCompleted = true;
}
void Window::disableStartupRestore() {
  checkThread();
  std::string status = "disabled";
  updateSettings({{"startup_mode", "empty"}}, true);
  impl->startupStatus.swap(status);
  impl->startupCompleted = true;
}
void Window::resetLayout(bool persist) {
  checkThread();
  if (persist) {
    auto proposed = impl->preferenceState;
    proposed.settings = settings();
    proposed.extras = impl->preferences;
    proposed.extras.erase("layout");
    impl->applyPreferences(std::move(proposed), true);
  }
  impl->suppressSavedLayout = true;
  impl->savedLayoutApplied = false;
  impl->loadedLayoutBytes = 0;
  ++impl->layoutResetGeneration;
  if (ImGui::GetCurrentContext()) {
    ImGui::ClearIniSettings();
#ifdef IMGUI_HAS_DOCK
    if (impl->workspaceDockId) ImGui::DockBuilderRemoveNode(impl->workspaceDockId);
#endif
    impl->resetInputs();
  }
  impl->layout = false;
}
ShortcutProfile Window::shortcutProfile() const {
  checkThread();
  return impl->shortcutProfile;
}
void Window::addDocument(std::shared_ptr<Document> d) {
  checkThread();
  if (busy())
    throw std::invalid_argument("Wait for the current edit");
  if (!d)
    throw std::invalid_argument("Document is required");
  if (controller_->contains(d->id))
    throw std::invalid_argument("Document is already open");
  std::vector<DocumentAdoption> adoptions;
  auto viewSlot=impl->viewCache->slot();
  adoptions.push_back(impl->prepareAdoption(d, true));
  const auto policy = Impl::historyPolicy(impl->preferenceState.settings);
  documents_.reserve(documents_.size() + 1);
  Json emptyManagerControls = Json::object(), emptyManagerRows = Json::array();
  const auto inserted = documentAdapters_.emplace(d->id, d).second;
  if (!inserted)
    throw std::invalid_argument("Document is already attached");
  bool paneStaged = false;
  HistoryPolicyAdoption adopted;
  try {
    impl->panes.emplace_back();
    paneStaged = true;
    impl->panes.back().doc = d;
    adopted = controller_->adopt_all_with_history_policy(std::move(adoptions), policy);
  } catch (...) {
    if (paneStaged)
      impl->panes.pop_back();
    documentAdapters_.erase(d->id);
    throw;
  }
  d->viewSlot_=std::move(viewSlot);d->viewCache_=impl->viewCache;
  d->bindPublishedController(controller_);
  documents_.push_back(std::move(d));
  impl->startupUserOpened = true;
  impl->roiManagerSource.reset();
  impl->roiSelectedIndices.clear();
  impl->roiRangeAnchor.reset();
  impl->roiIndex = -1;
  impl->roiManagerControls.swap(emptyManagerControls);
  impl->roiManagerRows.swap(emptyManagerRows);
}
void Window::addSessionDocuments(const Documents &documents) {
  auto retirement = attachStartupDocuments(
      documents, std::vector<bool>(documents.size(), false), {});
  if (!documents.empty()) impl->startupUserOpened = true;
}
HistoryPolicyAdoption Window::attachStartupDocuments(const Documents &documents,
                                    const std::vector<bool> &applyDefaults,
                                    std::optional<std::size_t> active) {
  checkThread();
  if (busy())
    throw std::invalid_argument("Wait for the current edit");
  auto nextAdapters = documentAdapters_;
  std::vector<DocumentAdoption> owners;
  std::vector<std::shared_ptr<VirtualViewCache::Slot>> viewSlots;
  viewSlots.reserve(documents.size());
  owners.reserve(documents.size());
  if (applyDefaults.size() != documents.size())
    throw std::invalid_argument("Startup default policy count differs");
  for (std::size_t i = 0; i < documents.size(); ++i) {
    const auto &document = documents[i];
    if (!document || controller_->contains(document->id) ||
        !nextAdapters.emplace(document->id, document).second)
      throw std::invalid_argument("Session document is already attached or invalid");
    viewSlots.push_back(impl->viewCache->slot());
    owners.push_back(impl->prepareAdoption(document, applyDefaults[i]));
  }
  documents_.reserve(documents_.size() + documents.size());
  Json emptyControls = Json::object(), emptyRows = Json::array();
  std::size_t staged = 0;
  HistoryPolicyAdoption adopted;
  try {
    for (const auto &document : documents) {
      impl->panes.emplace_back();
      ++staged;
      impl->panes.back().doc = document;
    }
    const auto policy = Impl::historyPolicy(impl->preferenceState.settings);
    adopted = active
        ? controller_->adopt_all_with_history_policy(std::move(owners), policy,
                                                     IncomingDocumentActivation{*active})
        : controller_->adopt_all_with_history_policy(std::move(owners), policy);
  } catch (...) {
    while (staged > 0) {
      --staged;
      impl->panes.pop_back();
    }
    throw;
  }
  documentAdapters_.swap(nextAdapters);
  std::size_t viewIndex=0;
  for (const auto &document : documents) {
    document->viewSlot_=std::move(viewSlots[viewIndex++]);document->viewCache_=impl->viewCache;
    document->bindPublishedController(controller_);
    documents_.push_back(document);
  }
  impl->roiManagerSource.reset();
  impl->roiSelectedIndices.clear();
  impl->roiRangeAnchor.reset();
  impl->roiIndex = -1;
  impl->roiManagerControls.swap(emptyControls);
  impl->roiManagerRows.swap(emptyRows);
  return adopted;
}
const Documents &Window::documents() const {
  checkThread();
  return documents_;
}
int Window::activeIndex() const {
  checkThread();
  auto id = controller_->active_document_id();
  auto found =
      std::find_if(documents_.begin(), documents_.end(),
                   [id](const auto &document) { return document->id == id; });
  return found == documents_.end() ? 0 : int(found - documents_.begin());
}
void Window::setActiveIndex(int index) {
  checkThread();
  if (index < 0 || index >= int(documents_.size()))
    throw std::out_of_range("Invalid active document index");
  if (controller_->active_document_id() != documents_[index]->id) {
    impl->stopAnimation("source_changed");
    impl->cancelHistogram();
    impl->rgbDatasetPanel.cancel("activation_changed");
    impl->colorStackPanel.cancel("activation_changed");
    impl->indexedColorPanel.cancel("activation_changed");
    impl->nativeWorkflowPanel.cancel("activation_changed");
    impl->resetInputs();
    impl->invalidateRoiManager();
  }
  controller_->activate(documents_[index]->id);
}
void Window::addNativeDocument(std::shared_ptr<imagejxx::Document> document) {
  checkThread();
  addDocument(std::make_shared<Document>(std::move(document)));
}
Json Window::renderMemoryInfo() const {
  checkThread();const auto &m=*impl->renderMemory;
  return {{"limit_bytes",m.limit()},{"retained_bytes",m.retained()},{"working_bytes",m.working()},{"peak_reserved_bytes",m.peak()},{"denied_requests",m.denied()}};
}
Json Window::virtualViewCacheInfo() const {
  checkThread();const auto &cache=*impl->viewCache;
  return {{"budget_bytes",cache.budget()},{"retained_bytes",cache.bytes()},{"entries",cache.entries()},{"evictions",cache.evictions()}};
}
void Window::addVirtualDocument(std::shared_ptr<imagejxx::Document> document,std::size_t maxViewBytes) {
  checkThread();
  addDocument(std::make_shared<Document>(std::move(document),true,maxViewBytes));
}
std::uint64_t Window::publishZSubset(const ResolvedZSubsetCommand &command) {
  checkThread();
  if (busy()) throw std::invalid_argument("Wait for the current edit");
  const auto &source = command.source();
  const auto current = nativeSnapshot(source.id());
  if (!source.same_document(current) || !current.revisions().matches(source.revisions(), Change::All))
    throw RevisionConflict();
  auto prepared = prepare_z_subset(command);
  std::string completed = "Z subset created";
  Json emptyControls=Json::object(),emptyRows=Json::array();
  if (command.request().destination == OutputDestination::InPlace) {
    OutputPublication outputs(results, workflow, ComponentCounts{}, "",
        recording ? metadataJson(command.record()) : Json());
    controller_->publish(prepared);
    outputs.publish();
    impl->roiManagerSource.reset();impl->roiManagerPendingFocus.reset();impl->savedRoiHistoryFocus.reset();
    impl->roiSelectedIndices.clear();impl->roiRangeAnchor.reset();impl->roiIndex=-1;
    impl->roiManagerControls.swap(emptyControls);impl->roiManagerRows.swap(emptyRows);
    impl->status.swap(completed);
    return source.id();
  }
  auto candidate = prepared.prepared_new_document();
  auto facade = std::make_shared<Document>(candidate);
  facade->setHistoryPolicy(impl->historyLimit, static_cast<std::size_t>(
      impl->preferenceState.settings.integer("history_memory_mb"))*1024*1024);
  documents_.reserve(documents_.size()+1);
  if (!documentAdapters_.emplace(candidate->id,facade).second)
    throw std::invalid_argument("Z subset output is already attached");
  bool paneStaged=false;
  std::optional<OutputPublication> outputs;
  try {
    impl->panes.emplace_back();paneStaged=true;impl->panes.back().doc=facade;
    outputs.emplace(results,workflow,ComponentCounts{},"",recording?metadataJson(command.record()):Json());
    controller_->publish(prepared);
  } catch (...) {
    if(paneStaged)impl->panes.pop_back();
    documentAdapters_.erase(candidate->id);throw;
  }
  facade->bindPublishedController(controller_);documents_.push_back(std::move(facade));
  impl->roiManagerSource.reset();impl->roiSelectedIndices.clear();impl->roiRangeAnchor.reset();impl->roiIndex=-1;
  impl->roiManagerControls.swap(emptyControls);impl->roiManagerRows.swap(emptyRows);
  outputs->publish();impl->status.swap(completed);
  return candidate->id;
}
std::uint64_t Window::exportAxisPlane(const DocumentSnapshot &source,
                                     AxisPlaneExportRequest request) {
  checkThread();
  impl->requireAxisExtractionSource(source, request, true);
  return publishAxisExtraction(resolve_axis_extraction_command(source, request), true);
}
std::uint64_t Window::replayAxisExtraction(
    const MetadataObject &record,
    std::optional<AxisExtractionRequest> rebindRequest,
    std::uint64_t documentId) {
  checkThread();
  // Validate the complete original before considering a different source.
  validate_axis_extraction_command_record(record);
  const auto source = nativeSnapshot(documentId);
  const auto command = rebindRequest
      ? rebind_axis_extraction_command(source, record, *rebindRequest)
      : axis_extraction_command_from_record(source, record);
  return publishAxisExtraction(command, rebindRequest.has_value());
}
std::uint64_t Window::publishAxisExtraction(
    const ResolvedAxisExtractionCommand &command,
    bool requireCapturedPosition) {
  checkThread();
  const auto &source = command.source();
  impl->requireAxisExtractionSource(source, command.request(), requireCapturedPosition);
  const auto configured = static_cast<std::size_t>(
      impl->preferenceState.settings.integer("processing_memory_mb")) *
      1024 * 1024;
  auto prepared = imagejxx::prepare_axis_extraction(command, {},
      configured ? configured : std::numeric_limits<std::size_t>::max());
  return std::visit([&](const auto &request) {
    return publishPreparedAxisExtraction(source, request, command.record(),
                                         prepared, requireCapturedPosition);
  }, command.request());
}
std::uint64_t Window::publishAxisOrientation(
    const ResolvedAxisOrientationCommand &command,
    bool requireCapturedPosition) {
  checkThread();
  impl->requireAxisExtractionSource(command.source(), command.request(), requireCapturedPosition);
  const auto configured = static_cast<std::size_t>(
      impl->preferenceState.settings.integer("processing_memory_mb")) * 1024 * 1024;
  auto prepared = imagejxx::prepare_axis_orientation(command, {},
      configured ? configured : std::numeric_limits<std::size_t>::max());
  return publishPreparedAxisExtraction(command.source(), command.request(),
      command.record(), prepared, requireCapturedPosition);
}
std::uint64_t Window::publishPreparedAxisExtraction(
    const DocumentSnapshot &source, const AxisCommandRequest &request,
    const MetadataObject &record, PreparedOperation &prepared,
    bool requireCapturedPosition) {
  checkThread();
  impl->requireAxisExtractionSource(source, request, requireCapturedPosition);
  // Every throwing frontend allocation/admission precedes native publication.
  auto candidate = prepared.prepared_new_document();
  auto facade = std::make_shared<Document>(candidate);
  facade->setHistoryPolicy(
      impl->historyLimit,
      static_cast<std::size_t>(
          impl->preferenceState.settings.integer("history_memory_mb")) *
          1024 * 1024);
  const auto zoom = impl->preferenceState.settings.number("default_zoom");
  if (zoom > 0)
    facade->setView({{"zoom", zoom / facade->displaySpacing()[0]}});
  Json emptyManagerControls = Json::object(), emptyManagerRows = Json::array();
  static_assert(std::is_nothrow_move_constructible_v<std::shared_ptr<Document>>);
  documents_.reserve(documents_.size() + 1);
  const auto id = candidate->id;
  const auto inserted = documentAdapters_.emplace(id, facade).second;
  if (!inserted)
    throw std::invalid_argument("Extraction output is already attached");
  const auto observe = [&](const char *phase) noexcept {
    try {
      std::visit([&](const auto &request) {
        using Request = std::decay_t<decltype(request)>;
        if constexpr (std::is_same_v<Request, AxisPlaneExportRequest>) {
          if (impl->axisPlaneUiPublicationProbe)
            impl->axisPlaneUiPublicationProbe(phase, source, request, candidate);
        } else if constexpr (std::is_same_v<Request, AxisRangeResliceRequest>) {
          if (impl->axisRangeUiPublicationProbe)
            impl->axisRangeUiPublicationProbe(phase, source, request, candidate);
        } else if constexpr (std::is_same_v<Request, AxisRangeOrientationRequest>) {
          if (impl->axisRangeOrientationUiPublicationProbe)
            impl->axisRangeOrientationUiPublicationProbe(phase, source, request, candidate);
        }
      }, request);
    } catch (...) {
      // Producers retain their own observation failure; never alter adoption.
    }
  };
  std::optional<OutputPublication> outputs;
  bool paneStaged = false;
  try {
    // Construct in stable storage before adoption: Pane's map-containing move
    // constructor can allocate on MSVC. Existing view caches stay in place.
    impl->panes.emplace_back();
    paneStaged = true;
    impl->panes.back().doc = facade;
    observe("before_native_publish");
    impl->requireAxisExtractionSource(source, request, requireCapturedPosition);
    outputs.emplace(results, workflow, ComponentCounts{}, "",
                    recording ? metadataJson(record) : Json(nullptr));
    controller_->publish(prepared);
  } catch (...) {
    if (paneStaged)
      impl->panes.pop_back();
    documentAdapters_.erase(id);
    throw;
  }
  // The controller adopted this exact candidate. Never readopt or retarget it.
  facade->bindPublishedController(controller_);
  documents_.push_back(std::move(facade));
  impl->roiManagerSource.reset();
  impl->roiSelectedIndices.clear();
  impl->roiRangeAnchor.reset();
  impl->roiIndex = -1;
  impl->roiManagerControls.swap(emptyManagerControls);
  impl->roiManagerRows.swap(emptyManagerRows);
  outputs->publish();
  observe("after_attach");
  return id;
}
std::uint64_t Window::publishPreparedProjection(const MetadataObject& record,
    PreparedOperation& prepared,bool recorded) {
  checkThread();
  if(controller_->active_document_id()!=prepared.target().document_id())throw RevisionConflict();
  auto candidate=prepared.prepared_new_document();
  if(!candidate)throw std::invalid_argument("Projection has no prepared output");
  auto facade=std::make_shared<Document>(candidate);
  facade->setHistoryPolicy(impl->historyLimit,static_cast<std::size_t>(
      impl->preferenceState.settings.integer("history_memory_mb"))*1024*1024);
  const auto zoom=impl->preferenceState.settings.number("default_zoom");
  if(zoom>0)facade->setView({{"zoom",zoom/facade->displaySpacing()[0]}});
  Json emptyControls=Json::object(),emptyRows=Json::array();
  documents_.reserve(documents_.size()+1);
  const auto id=candidate->id;
  if(!documentAdapters_.emplace(id,facade).second)
    throw std::invalid_argument("Projection output is already attached");
  std::optional<OutputPublication> outputs;bool pane=false;
  try {
    impl->panes.emplace_back();pane=true;impl->panes.back().doc=facade;
    outputs.emplace(results,workflow,ComponentCounts{},"",recorded?metadataJson(record):Json(nullptr));
    controller_->publish(prepared);
  }catch(...) {if(pane)impl->panes.pop_back();documentAdapters_.erase(id);throw;}
  facade->bindPublishedController(controller_);documents_.push_back(std::move(facade));
  impl->roiManagerSource.reset();impl->roiSelectedIndices.clear();impl->roiRangeAnchor.reset();impl->roiIndex=-1;
  impl->roiManagerControls.swap(emptyControls);impl->roiManagerRows.swap(emptyRows);
  outputs->publish();return id;
}
std::shared_ptr<imagejxx::Document>
Window::nativeDocument(std::uint64_t id) const {
  return documentById(id)->nativeOwner();
}
std::uint64_t Window::resliceSpacedAxisRange(const DocumentSnapshot &source_input,
                                      AxisRangeSpacingRequest request) {
  const auto source = source_input;
  checkThread();
  if (busy())
    throw std::invalid_argument("Wait for the current edit");
  impl->requireAxisExtractionSource(source, request, true);
  const auto configured = static_cast<std::size_t>(
      impl->preferenceState.settings.integer("processing_memory_mb")) * 1024 * 1024;
  if (configured)
    request.orientation.range.max_working_bytes = std::min(request.orientation.range.max_working_bytes, configured);
  return publishAxisSpacing(resolve_axis_spacing_command(source, request));
}
std::uint64_t Window::publishAxisSpacing(const ResolvedAxisSpacingCommand &command,
                                       bool requireCapturedPosition) {
  checkThread();
  impl->requireAxisExtractionSource(command.source(),command.request(),requireCapturedPosition);
  const auto configured=static_cast<std::size_t>(impl->preferenceState.settings.integer("processing_memory_mb"))*1024*1024;
  auto request=command.request();
  if(configured)request.orientation.range.max_working_bytes=std::min(request.orientation.range.max_working_bytes,configured);
  validatePhysicalGridRendering(estimate_axis_range_spacing(command.source().data().dataset,request),
                               physicalGridRenderLimit(impl->gl,impl.get()));
  auto prepared=prepare_axis_spacing(command,{},configured?configured:std::numeric_limits<std::size_t>::max());
  return publishPreparedAxisExtraction(command.source(),command.request(),command.record(),prepared,requireCapturedPosition);
}
std::uint64_t Window::resliceSlicerAxisRange(const DocumentSnapshot &source, AxisRangeSlicerResizeRequest request) {
  checkThread();
  impl->requireAxisExtractionSource(source,request,true);
  const auto configured=static_cast<std::size_t>(impl->preferenceState.settings.integer("processing_memory_mb"))*1024*1024;
  if(configured)request.orientation.range.max_working_bytes=std::min(request.orientation.range.max_working_bytes,configured);
  return publishSlicerResize(resolve_slicer_resize_command(source,request));
}
std::uint64_t Window::publishSlicerResize(const ResolvedSlicerResizeCommand &command,bool requireCapturedPosition) {
  checkThread();
  impl->requireAxisExtractionSource(command.source(),command.request(),requireCapturedPosition);
  const auto configured=static_cast<std::size_t>(impl->preferenceState.settings.integer("processing_memory_mb"))*1024*1024;
  auto request=command.request();
  if(configured)request.orientation.range.max_working_bytes=std::min(request.orientation.range.max_working_bytes,configured);
  validateSlicerResizeRendering(estimate_axis_range_slicer_resize(command.source().data().dataset,request),
                               physicalGridRenderLimit(impl->gl,impl.get()));
  auto prepared=prepare_slicer_resize(command,{},configured?configured:std::numeric_limits<std::size_t>::max());
  return publishPreparedAxisExtraction(command.source(),command.request(),command.record(),prepared,requireCapturedPosition);
}
std::uint64_t Window::resliceOrientedAxisRange(const DocumentSnapshot &source,
                                              AxisRangeOrientationRequest request) {
  checkThread();
  impl->requireAxisExtractionSource(source, request, true);
  return publishAxisOrientation(resolve_axis_orientation_command(source, request), true);
}
std::uint64_t Window::replayAxisOrientation(
    const MetadataObject &record,
    std::optional<AxisRangeOrientationRequest> rebindRequest,
    std::uint64_t documentId) {
  checkThread();
  validate_axis_orientation_command_record(record);
  const auto source = nativeSnapshot(documentId);
  const auto command = rebindRequest
      ? rebind_axis_orientation_command(source, record, *rebindRequest)
      : axis_orientation_command_from_record(source, record);
  return publishAxisOrientation(command, rebindRequest.has_value());
}
std::uint64_t Window::resliceAxisRange(const DocumentSnapshot &source_input,
                                      AxisRangeResliceRequest request) {
  const auto source = source_input;
  checkThread();
  impl->requireAxisExtractionSource(source, request, true);
  return publishAxisExtraction(resolve_axis_extraction_command(source, request), true);
}
std::shared_ptr<Document> Window::document() const {
  checkThread();
  if (documents_.empty() || activeIndex() < 0 ||
      activeIndex() >= int(documents_.size()))
    throw std::invalid_argument("Open an image first");
  return documents_[activeIndex()];
}
std::shared_ptr<Document> Window::documentById(std::uint64_t id) const {
  checkThread();
  if (!id && documents_.empty())
    return document();
  try {
    id = controller_->snapshot(id).id();
  } catch (const DocumentUnavailable &) {
    throw std::out_of_range("Document no longer exists");
  }
  auto found = documentAdapters_.find(id);
  if (found == documentAdapters_.end())
    throw std::out_of_range("Document no longer exists");
  auto document = found->second.lock();
  if (!document)
    throw std::out_of_range("Document no longer exists");
  return document;
}
Json Window::documentInfo(std::uint64_t id) const {
  auto d = documentById(id);
  const auto type=d->nativeSnapshot().data().dataset.pixel_type();
  const auto shape = d->shape();
  const auto position = d->position();
  const auto calibration = d->spatialCalibration();
  return {{"id", d->id},
          {"title", d->title()},
          {"dtype", typeName(type)},
          {"axis_sizes",
           {{"T", shape.t},
            {"C", shape.c},
            {"Z", shape.z},
            {"Y", shape.y},
            {"X", shape.x},
            {"S", shape.s}}},
          {"slice_index", d->slice()},
          {"current_tcz", {position.t, position.c, position.z}},
          {"scientific_read_only", false},
          {"time", calibration.time_origin + position.t * calibration.frame_interval},
          {"time_unit", calibration.time_unit},
          {"calibration", get(d->state().metadata, "calibration")},
          {"selection", get(d->state().metadata, "selection")},
          {"view", d->viewJson()},
          {"revision", d->revision()},
          {"revisions",
           {{"pixels", d->revisions().pixels},
            {"metadata", d->revisions().metadata},
            {"selection", d->revisions().selection},
            {"display", d->revisions().display}}},
          {"view_revision", d->view().revision},
          {"dirty", d->dirty()},
          {"pixel_storage", d->pixelStorage()},
          {"history_pixel_bytes", d->historyPixelBytes()},
          {"history_pixel_budget", d->historyPixelBudget()},
          {"undo_steps", d->undoSteps()},
          {"redo_steps", d->redoSteps()}};
}
DocumentSnapshot Window::nativeSnapshot(std::uint64_t id) const {
  return documentById(id)->nativeSnapshot();
}
void Window::setNativeDisplay(DisplayState display, std::uint64_t id) {
  documentById(id)->setNativeDisplay(std::move(display));
}
ChannelDisplayState Window::initialChannelDisplay(const DocumentSnapshot &source) const {
  const auto &dataset = source.data().dataset;
  if (const auto state = recognized_channel_display(source.display(), dataset)) return *state;
  if (boolean(get(metadataJson(source.data().metadata.extensions), "labels")))
    throw std::invalid_argument("Disable legacy label coloring before enabling channel display");
  const auto limits = impl->channelLimits();
  auto state = default_channel_display(dataset, source.display().t,
      source.display().crosshair[2], ChannelDisplayMode::Composite, limits);
  const auto properties = metadataJson(source.display().properties);
  const auto &range = get(properties, "display_range");
  const bool explicitLut = properties.contains("lut") || properties.contains("custom_lut");
  const auto lut = explicitLut ? std::optional<ChannelDisplayLut>(inheritedDisplayLut(properties)) : std::nullopt;
  for (auto &channel : state.channels) {
    if (!range.is_null()) {
      if (!range.is_array() || range.size() != 2)
        throw std::invalid_argument("Display range needs two endpoints");
      channel.range = {number(range[0]), number(range[1])};
    }
    if (lut) channel.lut = *lut;
  }
  return state;
}
void Window::enableChannelDisplay(std::uint64_t documentId) {
  checkThread();
  if (busy()) throw std::invalid_argument("Wait for the current edit");
  const auto source = nativeSnapshot(documentId);
  if (recognized_channel_display(source.display(), source.data().dataset)) return;
  applyChannelDisplay(source, {ChannelDisplayAction::Set, initialChannelDisplay(source), {}});
}
void Window::applyChannelDisplay(const DocumentSnapshot &source,
                                 const ChannelDisplayRequest &request) {
  checkThread();
  if (busy()) throw std::invalid_argument("Wait for the current edit");
  const auto current = nativeSnapshot(source.id());
  if (!source.same_document(current))
    throw std::invalid_argument("Channel display belongs to another document");
  if (!current.revisions().matches(source.revisions(), Change::Pixels | Change::Metadata))
    throw RevisionConflict();
  const auto limits = impl->channelLimits();
  publishChannelDisplay(resolve_channel_display_command(source, request, limits));
}
void Window::replayChannelDisplay(const MetadataObject &record,
                                  std::optional<ChannelDisplayRequest> rebindRequest,
                                  std::uint64_t documentId) {
  checkThread();
  validate_channel_display_command_record(record);
  if (busy()) throw std::invalid_argument("Wait for the current edit");
  const auto source = nativeSnapshot(documentId);
  const auto limits = impl->channelLimits();
  publishChannelDisplay(rebindRequest
      ? rebind_channel_display_command(source, record, *rebindRequest, limits)
      : channel_display_command_from_record(source, record));
}
void Window::publishChannelDisplay(const ResolvedChannelDisplayCommand &command) {
  checkThread();
  if (busy()) throw std::invalid_argument("Wait for the current edit");
  const auto facade = documentById(command.source().id());
  const auto current = facade->nativeSnapshot();
  auto display = command_display_state(command, current);
  OutputPublication outputs(results, workflow, ComponentCounts{}, "",
      recording ? metadataJson(command.record()) : Json(nullptr));
  // No frontend callback or throwing allocation follows native publication.
  if (command.action() == ChannelDisplayAction::ResetRange)
    controller_->set_display_and_reset_auto_contrast(current, std::move(display));
  else facade->setNativeDisplay(std::move(display));
  outputs.publish();
}
void Window::applyAutoContrast(const DocumentSnapshot &source,
                                const AutoContrastOptions &options) {
  checkThread();
  if (busy()) throw std::invalid_argument("Wait for the current edit");
  const auto current = nativeSnapshot();
  if (!current.same_document(source))
    throw std::invalid_argument("Auto requires its captured active document");
  if (!current.revisions().matches(source.revisions(), Change::All))
    throw RevisionConflict();
  const auto cancelEpoch = impl->autoCancelEpoch.load();
  JobOptions jobOptions;
  jobOptions.label = "Auto contrast";
  auto submitted = controller_->submit_auto_contrast(options, impl->autoLimits(), jobOptions);
  impl->bindAutoJob(std::move(submitted), source.id(), cancelEpoch);
}
void Window::replayAutoContrast(const MetadataObject &record,
    std::optional<AutoContrastRequest> rebindRequest, std::uint64_t documentId) {
  checkThread();
  const auto limits = impl->autoLimits();
  validate_auto_contrast_command_record(record, limits);
  if (busy()) throw std::invalid_argument("Wait for the current edit");
  const auto source = nativeSnapshot(documentId);
  if (source.id() != controller_->active_document_id())
    throw std::invalid_argument("Auto replay requires the active document");
  if (rebindRequest) {
    const auto cancelEpoch = impl->autoCancelEpoch.load();
    JobOptions options;
    options.label = "Rebind Auto contrast";
    auto submitted = controller_->submit_rebound_auto_contrast(
        record, *rebindRequest, limits, options);
    impl->bindAutoJob(std::move(submitted), source.id(), cancelEpoch);
  } else {
    OutputPublication outputs(results, workflow, {}, "",
        recording ? metadataJson(record) : Json(nullptr));
    controller_->replay_auto_contrast(record, limits, source.id());
    outputs.publish();
  }
}
void Window::applySelectionCreation(const DocumentSnapshot &source,
                                    const SelectionCreationRequest &request) {
  checkThread();
  if (busy()) throw std::invalid_argument("Wait for the current edit");
  const auto facade = documentById(source.id());
  impl->requireCreationSource(source);
  const auto current = facade->nativeSnapshot();
  if (!source.same_document(current))
    throw std::invalid_argument("Selection creation belongs to another document");
  if (!current.revisions().matches(source.revisions(), Change::Data))
    throw RevisionConflict();
  publishSelectionCreation(resolve_selection_creation_command(
      source, request, impl->selectionCreationLimits()));
}
void Window::replaySelectionCreation(
    const MetadataObject &record,
    std::optional<SelectionCreationRequest> rebindRequest,
    std::uint64_t documentId) {
  checkThread();
  const auto limits = impl->selectionCreationLimits();
  std::shared_ptr<Document> facade;
  try {
    facade = documentById(documentId);
  } catch (...) {
    // Keep malformed-original rejection ahead of a missing/closed owner.
    validate_selection_creation_command_record(record, limits);
    throw;
  }
  if (busy()) throw std::invalid_argument("Wait for the current edit");
  const auto source = facade->nativeSnapshot();
  impl->requireCreationSource(source);
  const auto command = rebindRequest
      ? rebind_selection_creation_command(source, record, *rebindRequest, limits)
      : selection_creation_command_from_record(source, record, limits);
  if (facade->scientific() && !rebindRequest) {
    const auto position = metadataJson(record).at("position");
    if (position.at("t") != source.display().t || position.at("c") != source.display().c ||
        position.at("z") != source.display().crosshair[2])
      throw std::invalid_argument("Selection replay requires its captured T/C/Z; use explicit rebind");
  }
  publishSelectionCreation(command);
}
void Window::publishSelectionCreation(
    const ResolvedSelectionCreationCommand &command) {
  checkThread();
  if (busy()) throw std::invalid_argument("Wait for the current edit");
  const auto facade = documentById(command.source().id());
  impl->requireCreationSource(command.source());
  const auto current = facade->nativeSnapshot();
  if (!command.source().same_document(current))
    throw std::invalid_argument("Selection creation belongs to another document");
  if (!current.revisions().matches(command.source().revisions(), Change::Data))
    throw RevisionConflict();
  if (!command.changes_selection()) return;
  OutputPublication outputs(results, workflow, ComponentCounts{}, "",
      recording ? metadataJson(command.record()) : Json(nullptr));
  auto edit = prepare_selection_creation(command, {},
      impl->selectionCreationLimits().max_working_bytes);
  std::string completed = "Selection created";
  // Preflight/allocation precedes publication; the recorder append cannot throw.
  if (facade->scientific()) controller_->commit(edit);
  else facade->commitNative(edit);
  outputs.publish();
  impl->status.swap(completed);
}
Json Window::applySavedRoiGeometry(const SavedRoiGeometryRequest &request) {
  return applySavedRoiGeometryStaged(request, {});
}
Json Window::applySavedRoiGeometryStaged(
    const SavedRoiGeometryRequest &request,
    const StageSavedRoiReply &stageReply) {
  checkThread();
  if (busy()) throw std::invalid_argument("Wait for the current edit");
  const auto capture = controller_->capture_saved_roi_geometry();
  const auto command = resolve_saved_roi_geometry(
      capture.source(), request, impl->savedRoiGeometryLimits());
  return publishSavedRoiGeometry(capture, command,
                                 SavedRoiGeometryInteraction::Numeric, stageReply);
}
Json Window::replaySavedRoiGeometry(
    const MetadataObject &record,
    std::optional<SavedRoiGeometryRequest> completeRebindRequest) {
  return replaySavedRoiGeometryStaged(record, std::move(completeRebindRequest), {});
}
void Window::validateSavedRoiGeometryReplayRecord(const MetadataObject &record) const {
  checkThread();
  validate_saved_roi_geometry_record(record, impl->savedRoiGeometryLimits());
}
Json Window::replaySavedRoiGeometryStaged(
    const MetadataObject &record,
    std::optional<SavedRoiGeometryRequest> completeRebindRequest,
    const StageSavedRoiReply &stageReply) {
  checkThread();
  const auto limits = impl->savedRoiGeometryLimits();
  // Even a missing owner or malformed replacement must not bypass validation
  // of the complete original record.
  validate_saved_roi_geometry_record(record, limits);
  if (busy()) throw std::invalid_argument("Wait for the current edit");
  const auto capture = controller_->capture_saved_roi_geometry();
  const auto command = completeRebindRequest
      ? rebind_saved_roi_geometry(capture.source(), record,
                                  *completeRebindRequest, limits)
      : saved_roi_geometry_from_record(capture.source(), record, limits);
  return publishSavedRoiGeometry(capture, command,
                                 SavedRoiGeometryInteraction::Numeric, stageReply);
}
Json Window::publishSavedRoiGeometry(
    const SavedRoiGeometryCapture &capture,
    const ResolvedSavedRoiGeometryCommand &command,
    SavedRoiGeometryInteraction interaction,
    const StageSavedRoiReply &stageReply) {
  checkThread();
  if (busy()) throw std::invalid_argument("Wait for the current edit");
  const auto row = command.row_index();
  if (row > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    throw std::invalid_argument("Saved ROI row exceeds manager addressable range");
  const bool changes = !(command.original_roi().selection == command.replacement());
  const auto metadataRevision = capture.source().revisions().metadata;
  if (changes && metadataRevision == std::numeric_limits<std::uint64_t>::max())
    throw std::overflow_error("Saved ROI Metadata revision overflow");
  auto prepared = controller_->prepare_saved_roi_geometry(
      capture, command, interaction, impl->savedRoiGeometryLimits());
  Json reply{{"document_id", capture.document_id()}, {"changed", changes},
             {"record", metadataJson(prepared.record())}};
  const bool recordChange = recording && changes;
  // Private language transport staging runs before any frontend array pointers
  // are retained, outside Application/Document locks. Python builds its complete
  // detached return object here; conversion failure cannot follow a commit.
  if (stageReply) stageReply(reply);
  OutputPublication outputs(results, workflow, {}, "",
      recordChange ? reply["record"] : Json(nullptr));
  std::set<std::size_t> selected;
  std::shared_ptr<const DocumentSnapshot> managerSource;
  if (changes) {
    selected.insert(row);
    managerSource = std::make_shared<const DocumentSnapshot>(capture.source());
  }
  std::string completed = "Saved area updated";
  static_assert(std::is_nothrow_swappable_v<decltype(managerSource)>);
  // Native consumption performs the final owner/activation/Data/row/TCZ and
  // optional Pointer Display guard atomically. All frontend values and array
  // capacity already exist; no callback/allocation follows changed publication.
  SavedRoiGeometryPublication publication{};
  if(command.replacement().is_path())
    documentById(capture.document_id())->publishNativePaths([&]{publication=controller_->publish_saved_roi_geometry(prepared);});
  else publication=controller_->publish_saved_roi_geometry(prepared);
  if (publication.changed) {
    outputs.publish();
    impl->roiManagerSource.swap(managerSource);
    impl->roiSelectedIndices.swap(selected);
    impl->roiRangeAnchor = row;
    impl->roiIndex = static_cast<int>(row);
    impl->roiManagerPendingFocus.emplace(capture.document_id(), metadataRevision + 1);
    impl->savedRoiHistoryFocus.emplace(Impl::SavedRoiHistoryFocus{
        capture.document_id(), row, capture.source().data_identity(), {}, metadataRevision + 1});
    impl->status.swap(completed);
  }
  return reply;
}
DocumentTransaction Window::beginEdit(std::uint64_t id,
                                      Change dependencies) const {
  documentById(id)->requireLegacyEditable();
  return nativeSnapshot(id).begin_edit(dependencies);
}
void Window::commitEdit(DocumentTransaction &transaction) {
  checkThread();
  if (busy())
    throw std::invalid_argument("Wait for the current edit");
  documentById(transaction.document_id())->commitNative(transaction);
}
void Window::closeDocument(std::uint64_t id, bool discard) {
  checkThread();
  if (impl->legacyBusy())
    throw std::invalid_argument("Wait for the current edit");
  auto d = documentById(id);
  if (d->dirty() && !discard)
    throw std::invalid_argument(
        "Save the document or explicitly discard its changes");
  impl->remove(d);
}
void Window::openFile(const std::string &path) {
  checkThread();
#ifdef IMAGEJXX_HAS_SCIENTIFIC_IO
  if(path.size()>131064 || path.find('\0')!=std::string::npos)throw std::invalid_argument("File path must be bounded text");
  if(impl->startupTiffs || !impl->pendingLaunch.empty())throw std::invalid_argument("Finish or cancel the launch before opening another file");
  const auto resolved=std::filesystem::absolute(std::filesystem::u8path(path)).lexically_normal().u8string();
  if(resolved.size()>131064)throw std::invalid_argument("Resolved file path must be bounded text");
  if(impl->scientificIoPanel.visible() || impl->scientificIoPanel.working() || !impl->pendingFileOpens.empty()) {
    if(impl->scientificIoOtherBusy() || impl->scientificIoPanel.diagnostics().at("exporting").get<bool>())throw std::invalid_argument("Wait for the current edit or export");
    if(impl->pendingFileOpens.size()>=15)throw std::invalid_argument("At most sixteen files may await opening");
    impl->pendingFileOpens.push_back(resolved);return;
  }
  const auto suffix=extension(resolved);
  if(suffix==".tif" || suffix==".tiff") {
    impl->showScientificIo(false,{{"path",resolved}});impl->scientificIoPanel.inspect();return;
  }
#endif
  openLegacyFile(path);
}
void Window::openLegacyFile(const std::string &path) {
  checkThread();
  if(busy())throw std::invalid_argument("Wait for the current edit or close its review");
  if(path.size()>131064 || path.find('\0')!=std::string::npos)throw std::invalid_argument("File path must be bounded text");
  if (extension(path) == ".ijx") {
    Json rows, steps, extensions;
    auto opened = loadSession(path, rows, steps, &extensions);
    for (auto d : opened) {
      d->path = opened.size() == 1 ? path : "";
    }
    auto sizes = impl->sessionSizes;
    auto target = std::filesystem::absolute(std::filesystem::u8path(path)).lexically_normal();
    for (auto &[existing, size] : sizes)
      if (samePath(existing, target)) size = opened.size();
    sizes[target] = opened.size();
    auto preferences = impl->preferences;
    Json recent = Json::array({path});
    for (const auto &old : get(preferences, "recent"))
      if (old != path && recent.size() < 15) recent.push_back(old);
    preferences["recent"] = std::move(recent);
    addSessionDocuments(opened);
    impl->sessionSizes.swap(sizes);
    impl->preferences.swap(preferences);
    results.swap(rows);
    workflow.swap(steps);
    impl->sessionExtensions.swap(extensions);
    return;
  } else {
    auto d = std::make_shared<Document>(readImages(path), filename(path));
    addDocument(d);
  }
  impl->recent(path);
}
bool Window::busy() const {
  checkThread();
  return (!impl->launchPublishing && !impl->pendingLaunch.empty()) || impl->workflowOtherBusy() || impl->nativeWorkflowPanel.visible() || impl->nativeWorkflowPanel.working()
#ifdef IMAGEJXX_HAS_SCIENTIFIC_IO
      || (!impl->scientificIoPublishing && (impl->startupTiffs || impl->scientificIoPanel.visible() || impl->scientificIoPanel.working() || !impl->pendingFileOpens.empty()))
#endif
      ;
}
std::string Window::error() const {
  checkThread();
  return impl->lastError;
}
void Window::checkThread() const {
  if (std::this_thread::get_id() != ownerThread)
    throw std::runtime_error("Use the editor from the thread that created it");
}
void Window::clearError() {
  checkThread();
  impl->lastError.clear();
}
void Window::action(const std::string &name, const Json &p) {
  checkThread();
  if(name=="show_contrast_apply_rebind"){
    if(!p.is_object() || p.size()!=1 || !p.contains("record") || !p.at("record").is_object())throw std::invalid_argument("Rebind review requires only a command record");
    const auto record=metadataObjectFromJson(p.at("record"));const auto limits=impl->contrastApplyLimits();
    Impl::validateContrastRecord(record,limits.max_record_bytes);
    if(busy())throw std::invalid_argument("Wait for the current edit");
    const auto source=nativeSnapshot();
    if(boolean(get(metadataJson(source.data().metadata.extensions),"labels")))throw std::invalid_argument("Disable label coloring before Apply");
    if(Impl::rgbSessionContrastRecord(record))throw std::invalid_argument("Review complete RGB sessions with Run workflow to inspect visits and choose a final anchor.");
    if(Impl::rgbStackContrastRecord(record))throw std::invalid_argument("Review stack captures with Run workflow to choose their active processor.");
    impl->contrastPanel.showRebind(source,record,limits);impl->animating=false;return;
  }
  if(name=="configure_contrast_rebind_target"){
    if(!p.is_object() || p.size()!=1 || !p.contains("target"))throw std::invalid_argument("Rebind target requires only a target object");
    const auto request=gaussianRebindRequest(p.at("target"));impl->tickContrast();
    impl->contrastPanel.configureRebindTarget(request,busy());return;
  }
  if(name=="apply_contrast_rebind" || name=="cancel_contrast_rebind"){
    if(!p.is_object() || !p.empty())throw std::invalid_argument("Rebind control accepts no parameters");
    impl->tickContrast();
    if(name=="cancel_contrast_rebind")impl->contrastPanel.cancelRebind();
    else impl->contrastPanel.applyRebind(busy(),[this](const DocumentSnapshot &source,const MetadataObject &record,const TargetRequest &target){impl->applyContrastRebind(source,record,target);});
    return;
  }
  if(name=="replay_contrast_apply"){
    const auto epoch=impl->contrastApplyCancelEpoch.load();
    if(!p.is_object() || !p.contains("record") || !p.at("record").is_object())throw std::invalid_argument("Contrast Apply replay requires a command record");
    for(const auto &v:p.items())if(v.key()!="record" && v.key()!="contrast_rebind")throw std::invalid_argument("Unknown Contrast Apply replay parameter");
    const auto record=metadataObjectFromJson(p.at("record"));
    const auto limits=impl->contrastApplyLimits();Impl::validateContrastRecord(record,limits.max_record_bytes);
    if(busy())throw std::invalid_argument("Wait for the current edit");
    const auto source=nativeSnapshot();
    if(Impl::rgbSessionContrastRecord(record)){
      if(p.contains("contrast_rebind") && (!p.at("contrast_rebind").is_object() || !p.at("contrast_rebind").empty()))throw std::invalid_argument("Session replay rebinding retains all saved absolute visits and selections; use an empty contrast_rebind object or Run workflow for anchor translation.");
      const auto command=p.contains("contrast_rebind")?rebind_rgb_contrast_session_command(source,record,limits):rgb_contrast_session_command_from_record(source,record,limits);
      impl->beginContrastApply(command,epoch);return;
    }
    if(Impl::rgbStackContrastRecord(record)){
      if(p.contains("contrast_rebind") && (!p.at("contrast_rebind").is_object() || !p.at("contrast_rebind").empty()))throw std::invalid_argument("Stack replay rebinding uses the destination's current processor and selection; use an empty contrast_rebind object or Run workflow for an explicit anchor.");
      const auto command=p.contains("contrast_rebind")?rebind_rgb_contrast_stack_command(source,record,limits):rgb_contrast_stack_command_from_record(source,record,limits);
      impl->beginContrastApply(command,epoch);return;
    }
    if(Impl::rgbContrastRecord(record)){
      const auto command=p.contains("contrast_rebind")
        ?rebind_rgb_contrast_apply_command(source,record,gaussianRebindRequest(p.at("contrast_rebind")),limits)
        :rgb_contrast_apply_command_from_record(source,record,limits);
      impl->beginContrastApply(command,epoch);return;
    }
    const auto command=p.contains("contrast_rebind")
      ?rebind_contrast_apply_command(source,record,gaussianRebindRequest(p.at("contrast_rebind")),limits)
      :contrast_apply_command_from_record(source,record,limits);
    impl->beginContrastApply(command,epoch);return;
  }
  if(name=="show_histogram") {
    if(!p.is_object() || !p.empty())throw std::invalid_argument("Show Histogram accepts no parameters");
    if(busy())throw std::invalid_argument("Wait for the current edit");
    impl->histogramPanel.show(nativeSnapshot(),impl->histogramLimits(),settings());impl->animating=false;return;
  }
  if(name=="replay_histogram" || name=="show_histogram_rebind") {
    if(!p.is_object() || !p.contains("record") || !p.at("record").is_object())throw std::invalid_argument("Histogram replay requires a command record");
    for(const auto &v:p.items())if(v.key()!="record" && !(name=="replay_histogram" && v.key()=="histogram_rebind"))throw std::invalid_argument("Unknown histogram replay parameter");
    const auto record=metadataObjectFromJson(p.at("record"));validate_histogram_command_record(record);
    if(busy())throw std::invalid_argument("Wait for the current edit");
    const auto source=nativeSnapshot();const auto limits=impl->histogramLimits();
    if(name=="show_histogram_rebind")impl->histogramPanel.showRebind(source,record,limits,settings());
    else {
      HistogramCommandLimits cap;cap.processing=limits;
      const auto captured=p.contains("histogram_rebind")
          ?rebind_histogram_command(source,record,gaussianRebindRequest(p.at("histogram_rebind")),cap)
          :histogram_command_from_record(source,record,cap);
      impl->beginHistogram(false,impl->histogramPanel.replayDraft(captured,limits,settings()));
    }
    impl->animating=false;return;
  }
  if(name=="configure_histogram"){impl->histogramPanel.configure(p);return;}
  if(name=="configure_histogram_target") {
    if(!p.is_object() || p.size()!=1 || !p.contains("target"))throw std::invalid_argument("Histogram target requires only a target object");
    const auto request=[&]{try{return gaussianRebindRequest(p.at("target"));}catch(const std::invalid_argument &e){std::string message=e.what();const auto at=message.find("Gaussian");if(at!=std::string::npos)message.replace(at,8,"Histogram");throw std::invalid_argument(message);}}();
    impl->histogramPanel.configureTarget(request);return;
  }

  if(name=="calculate_histogram") {
    if(!p.is_object() || !p.empty())throw std::invalid_argument("Calculate Histogram accepts no parameters");
    impl->beginHistogram();return;
  }
  if(name=="cancel_histogram") {
    if(!p.is_object() || !p.empty())throw std::invalid_argument("Cancel Histogram accepts no parameters");
    impl->cancelHistogram();return;
  }
  if(name=="wait_histogram") {
    if(!p.is_object() || p.size()>1 || (p.size()==1&&!p.contains("timeout_ms")))throw std::invalid_argument("Histogram wait accepts only timeout_ms");
    const auto timeout=p.value("timeout_ms",Json(5000));
    if(!timeout.is_number_integer() || timeout<1 || timeout>60000)throw std::invalid_argument("Histogram wait timeout must be in 1..60000 ms");
    const auto pending=impl->histogramTask;impl->pollHistogram();
    if(pending && !pending->job.wait_for(std::chrono::milliseconds(timeout.get<int>())))throw std::runtime_error("Histogram wait timed out");
    impl->pollHistogram();if(pending && !pending->error.empty())throw std::runtime_error(pending->error);return;
  }
  if(name=="close_histogram") {
    if(!p.is_object() || !p.empty())throw std::invalid_argument("Close Histogram accepts no parameters");
    impl->cancelHistogram();impl->histogramPanel.cancel();return;
  }
  if(name=="export_histogram_csv") {
    if(!p.is_object() || p.size()!=1 || !p.contains("path") || !p.at("path").is_string())throw std::invalid_argument("Histogram CSV export requires only a path");
    const auto text=impl->histogramPanel.csv();writeBytes(p.at("path").get<std::string>(),{text.begin(),text.end()});return;
  }
  if(name=="show_rgb_dataset" || name=="review_rgb_dataset") {
    if(!p.is_object())throw std::invalid_argument("RGB review requires an object");
    for(const auto &v:p.items())if(v.key()!="record" && v.key()!="document_id")throw std::invalid_argument("Unknown RGB review parameter");
    if(p.contains("record"))validate_rgb_dataset_command_record(metadataObjectFromJson(p.at("record")));
    if(busy())throw std::invalid_argument("Wait for the current edit");
    const auto &id=get(p,"document_id");
    if(!id.is_null() && (!id.is_number_unsigned() && (!id.is_number_integer() || id<0)))throw std::invalid_argument("Invalid RGB source ID");
    const auto source=nativeSnapshot(id.is_null()?0:id.get<std::uint64_t>());
    if(source.id()!=nativeSnapshot().id())throw std::invalid_argument("Activate the RGB source first");
    impl->rgbDatasetPanel.show(source,impl->rgbCap(),recording,get(p,"record"));return;
  }
  if(name=="rgb_dataset_draft"){impl->pollRgb();impl->rgbDatasetPanel.patch(p);return;}
  if(name=="rgb_dataset_preview" || name=="rgb_dataset_apply" || name=="cancel_rgb_dataset") {
    if(!p.is_object() || !p.empty())throw std::invalid_argument("RGB buttons accept no parameters");
    if(name=="cancel_rgb_dataset")impl->rgbDatasetPanel.cancel("explicit_cancel");
    else {impl->pollRgb();if(name=="rgb_dataset_preview")impl->rgbDatasetPanel.preview();else impl->rgbDatasetPanel.apply();}
    return;
  }
  if(name=="show_color_stack" || name=="review_color_stack") {
    if(!p.is_object())throw std::invalid_argument("RGB review requires an object");
    for(const auto &v:p.items())if(v.key()!="record" && v.key()!="document_id")throw std::invalid_argument("Unknown RGB review parameter");
    if(p.contains("record"))validate_color_stack_command_record(metadataObjectFromJson(p.at("record")));
    if(busy())throw std::invalid_argument("Wait for the current edit");
    const auto &id=get(p,"document_id");
    if(!id.is_null() && (!id.is_number_unsigned() && (!id.is_number_integer() || id<0)))throw std::invalid_argument("Invalid RGB source ID");
    const auto source=nativeSnapshot(id.is_null()?0:id.get<std::uint64_t>());
    if(source.id()!=nativeSnapshot().id())throw std::invalid_argument("Activate the RGB source first");
    impl->colorStackPanel.show(source,impl->rgbCap(),recording,get(p,"record"));return;
  }
  if(name=="color_stack_draft"){impl->pollColorStack();impl->colorStackPanel.patch(p);return;}
  if(name=="color_stack_preview" || name=="color_stack_apply" || name=="cancel_color_stack") {
    if(!p.is_object() || !p.empty())throw std::invalid_argument("RGB buttons accept no parameters");
    if(name=="cancel_color_stack")impl->colorStackPanel.cancel("explicit_cancel");
    else {impl->pollColorStack();if(name=="color_stack_preview")impl->colorStackPanel.preview();else impl->colorStackPanel.apply();}
    return;
  }
  if(name=="show_indexed_dataset" || name=="show_indexed_rgb" || name=="review_indexed_color") {
    if(!p.is_object())throw std::invalid_argument("Indexed review requires an object");
    for(const auto &v:p.items())if(v.key()!="record" && v.key()!="document_id")throw std::invalid_argument("Unknown indexed review parameter");
    if(p.contains("record")){
      const auto r=metadataObjectFromJson(p.at("record"));
      if(get(p.at("record"),"schema")=="imagejxx.indexed-dataset-command")validate_indexed_dataset_command_record(r);
      else if(get(p.at("record"),"schema")=="imagejxx.indexed-rgb-command")validate_indexed_rgb_command_record(r);
      else throw std::invalid_argument("Unsupported indexed review capture");
    }
    if(busy())throw std::invalid_argument("Wait for the current edit");
    const auto &id=get(p,"document_id");
    if(!id.is_null() && (!id.is_number_unsigned() && (!id.is_number_integer() || id<0)))throw std::invalid_argument("Invalid indexed source ID");
    const auto source=nativeSnapshot(id.is_null()?0:id.get<std::uint64_t>());
    if(source.id()!=nativeSnapshot().id())throw std::invalid_argument("Activate the indexed source first");
    impl->indexedColorPanel.show(source,impl->rgbCap(),recording,get(p,"record"),name=="show_indexed_rgb");return;
  }
  if(name=="indexed_color_draft"){impl->pollIndexedColor();impl->indexedColorPanel.patch(p);return;}
  if(name=="indexed_color_preview" || name=="indexed_color_apply" || name=="cancel_indexed_color") {
    if(!p.is_object() || !p.empty())throw std::invalid_argument("Indexed buttons accept no parameters");
    if(name=="cancel_indexed_color")impl->indexedColorPanel.cancel("explicit_cancel");
    else {impl->pollIndexedColor();if(name=="indexed_color_preview")impl->indexedColorPanel.preview();else impl->indexedColorPanel.apply();}
    return;
  }
  if(name=="queue_launch"){impl->queueLaunchAppend(p);return;}
  if(name=="cancel_launch"){if(!p.is_object()||!p.empty())throw std::invalid_argument("Launch cancel accepts no parameters");impl->cancelLaunchAppend();return;}
#ifdef IMAGEJXX_HAS_SCIENTIFIC_IO
  if(name=="show_scientific_open"||name=="show_scientific_export"){impl->showScientificIo(name=="show_scientific_export",p);return;}
  if(name=="scientific_io_draft"){impl->pollScientificIo();impl->scientificIoPanel.patch(p);return;}
  if(name=="cancel_scientific_io"){
    if(!p.is_object()||!p.empty())throw std::invalid_argument("Scientific IO cancel accepts no parameters");
    impl->clearPendingImports();impl->scientificIoPanel.cancel("explicit_cancel");return;
  }
  if(name=="scientific_io_inspect"||name=="scientific_io_apply"){
    if(!p.is_object()||!p.empty())throw std::invalid_argument("Scientific IO button actions accept no parameters");
    impl->pollScientificIo();if(name=="scientific_io_inspect")impl->scientificIoPanel.inspect();else impl->scientificIoPanel.apply();return;
  }
  if(name=="scientific_io_flat_open"){if(!p.is_object()||!p.empty())throw std::invalid_argument("Flat import accepts no parameters");impl->pollScientificIo();impl->importFlatReview();return;}
  if(name=="review_scientific_io"){
    if(!p.is_object()||p.size()!=1||!p.contains("record"))throw std::invalid_argument("Choose one scientific IO record for explicit review");
    auto draft=scientific_io_replay_draft(p.at("record"));impl->showScientificIo(draft.at("exporting").get<bool>(),{{"path",draft.at("path")}});impl->scientificIoPanel.patch({{"limits",draft.at("limits")}});if(draft.contains("sample_policy"))impl->scientificIoPanel.patch({{"sample_policy",draft.at("sample_policy")}});if(draft.contains("compression"))impl->scientificIoPanel.patch({{"compression",draft.at("compression")}});if(draft.contains("predictor"))impl->scientificIoPanel.patch({{"predictor",draft.at("predictor")}});if(draft.contains("storage"))impl->scientificIoPanel.patch({{"storage",draft.at("storage")}});if(draft.contains("container"))impl->scientificIoPanel.patch({{"container",draft.at("container")}});if(draft.contains("virtual_loading"))impl->scientificIoPanel.patch({{"virtual_loading",draft.at("virtual_loading")}});return;
  }
  if((name=="run_workflow"||name=="batch"||name=="native_batch")&&workflow.is_array())for(const auto &step:workflow)if(get(step,"schema")=="imagejxx.scientific-image-io")throw std::invalid_argument("Review scientific file actions individually; Batch inputs remain native sessions");
#endif

  if(name=="cancel_native_workflow"){if(!p.is_object()||!p.empty())throw std::invalid_argument("Workflow cancel accepts no parameters");impl->nativeWorkflowPanel.cancel("explicit_cancel");return;}
  if (name == "edit_saved_roi_geometry") {
    if (!p.is_object() || p.size() != 1 || !p.contains("index") ||
        !p.at("index").is_number_integer() || p.at("index").is_boolean() ||
        (!p.at("index").is_number_unsigned() && p.at("index").get<std::int64_t>() < 0))
      throw std::invalid_argument("Edit Saved Area requires one nonnegative integer index");
    const auto index = p.at("index").get<std::uint64_t>();
    if (index > std::numeric_limits<std::size_t>::max())
      throw std::invalid_argument("Saved ROI index exceeds addressable range");
    impl->beginSavedRoiGeometryEdit(static_cast<std::size_t>(index));
    return;
  }
  if (impl->savedRoiEdit.active() && name == "show_selection_properties") {
    impl->savedRoiEdit.show_numeric(impl->selectionPanel);
    impl->savedRoiPanelShown = true;
    return;
  }
  if (impl->savedRoiEdit.active() && name == "selection_properties_draft") {
    try {
      impl->selectionPanel.working_limit(impl->selectionCreationLimits().max_working_bytes);
      impl->selectionPanel.patch(p);
      if (impl->selectionPanel.preview())
        impl->savedRoiEdit.stage_numeric(*impl->selectionPanel.preview());
      else impl->savedRoiEdit.invalidate_preview();
    } catch (...) { impl->savedRoiEdit.invalidate_preview(); throw; }
    return;
  }
  if (impl->savedRoiEdit.active() && name == "apply_selection_properties") {
    impl->selectionPanel.working_limit(impl->selectionCreationLimits().max_working_bytes);
    impl->selectionPanel.apply([&](const Selection &value, const DocumentSnapshot &) {
      impl->applySavedRoiNumeric(value);
    });
    return;
  }
  if (impl->savedRoiEdit.active() && name == "cancel_selection_properties") {
    impl->cancelSavedRoiGeometry("canceled");
    return;
  }
  std::optional<MetadataObject> autoRecord;
  if (name == "auto_contrast" || name == "replay_auto_contrast") {
    if (!p.is_object()) throw std::invalid_argument("Auto parameters must be an object");
    if (name == "replay_auto_contrast") {
      autoRecord = metadataObjectFromJson(p.at("record"));
      validate_auto_contrast_command_record(*autoRecord, impl->autoLimits());
    }
  }
  if (name == "rank_replay")
    validate_rank_command_record(metadataObjectFromJson(get(p, "record")));
  if(name=="replay_planar_annotation" || name=="show_planar_annotation_rebind") {
    if(!p.is_object() || !p.contains("record"))throw std::invalid_argument("Annotation replay requires a record");
    validate_planar_annotation_command_record(metadataObjectFromJson(p.at("record")));
  }
  if(name=="replay_imagej_annotation_import") {
    if(!p.is_object() || !p.contains("record"))throw std::invalid_argument("Mixed import replay requires a record");
    validate_imagej_annotation_import_record(metadataObjectFromJson(p.at("record")));
  }
  if(name=="replay_imagej_roi_import") {
    if(!p.is_object() || !p.contains("record"))throw std::invalid_argument("ROI import replay requires a record");
    validate_imagej_roi_import_record(metadataObjectFromJson(p.at("record")));
  }
  if (name == "replay_slicer_resize" ||
      (name == "show_axis_range_slicer_resize" && p.contains("record"))) {
    if (!p.is_object() || !p.contains("record")) throw std::invalid_argument("Slicer replay requires a record");
    validate_slicer_resize_command_record(metadataObjectFromJson(p.at("record")));
  }
  if (name == "replay_axis_spacing" ||
      (name == "show_axis_range_spacing" && p.contains("record"))) {
    if (!p.is_object() || !p.contains("record"))
      throw std::invalid_argument("Physical spacing replay requires a record");
    validate_axis_spacing_command_record(metadataObjectFromJson(p.at("record")));
  }
  if (name == "replay_axis_orientation") {
    if (!p.is_object() || !p.contains("record"))
      throw std::invalid_argument("Orientation replay requires a record");
    validate_axis_orientation_command_record(metadataObjectFromJson(p.at("record")));
  }
  std::uint64_t targetId = 0;
  if (p.is_object() && p.contains("document_id")) {
    const auto &id = p["document_id"];
    if (!id.is_number_integer() || id < 0)
      throw std::invalid_argument("Document ID must be nonnegative");
    targetId = id.get<std::uint64_t>();
    documentById(targetId); // Validate explicit targets before global side effects.
  }
  static const std::set<std::string> targeted{
      "show_movie_export", "view", "fit_selection", "close_document", "combine_rois", "undo", "redo",
      "show_manual_threshold", "configure_threshold_target", "auto_manual_threshold", "set_manual_threshold", "reset_manual_threshold",
      "manual_threshold_to_selection", "apply_manual_threshold",
      "replay_manual_threshold",
      "show_z_subset", "substack", "replay_z_subset",
      "export_axis_plane", "show_axis_plane_export",
      "reslice_axis_range", "show_axis_range_reslice",
      "replay_axis_extraction", "replay_axis_orientation",
      "show_channel_display", "enable_channel_display", "channel_display", "replay_channel_display",
      "auto_contrast", "replay_auto_contrast", "rank_replay",
      "reslice_oriented_axis_range", "show_axis_range_orientation",
      "reslice_spaced_axis_range", "show_axis_range_spacing",
      "replay_axis_spacing", "replay_slicer_resize", "show_display_annotations", "replay_display_annotation", "replay_planar_annotation", "show_planar_annotation_rebind", "replay_imagej_roi_import", "replay_imagej_annotation_import", "show_roi_import", "show_roi_import_record", "show_roi_export", "show_roi_zip_export", "show_roi_export_record",
      "reslice_slicer_axis_range", "show_axis_range_slicer_resize"};
  if (!targetId && impl->thresholdSource &&
      (name == "configure_threshold_target" || name == "auto_manual_threshold" || name == "set_manual_threshold" || name == "reset_manual_threshold" ||
       name == "manual_threshold_to_selection" || name == "apply_manual_threshold"))
    targetId = impl->thresholdSource->id();
  const auto operationId = name == "show_operation"
                               ? string(get(p, "operation")) : name == "rank_replay" ? string(get(get(p,"record"),"operation")) : std::string();
  if (targetId && (name == "show_operation" || name == "gaussian_replay" || name == "rank_replay") &&
      document()->id != targetId)
    throw std::invalid_argument(name == "rank_replay" || operationId == "rank_filter_imagej" || (operationId == "remove_outliers_imagej" || operationId == "top_hat_imagej")
        ? "Rank desktop commands require the active document"
        : "Gaussian desktop commands require the active document");

  impl->requireAction(name, targetId, operationId);
  // Legacy routes still use the active source internally. An explicit ID must
  // not bypass its capability check before clipboard, files or jobs change.
  if (targetId && !targeted.count(name))
    impl->requireAction(name, 0, operationId);
  if(name=="show_imagej_projection" || name=="show_imagej_grouped_projection" ||
      name=="project_imagej" || name=="replay_imagej_projection") {
    if(busy()) throw std::invalid_argument("Wait for the current edit");
    if(!p.is_object()) throw std::invalid_argument("Projection parameters must be an object");
    auto facade=documentById(targetId);const auto source=facade->nativeSnapshot();
    const auto shape=source.data().dataset.shape();
    const auto automatic=shape.t==1 && shape.z==1 && shape.c>1?ImageJProjectionAxis::Channels:
        shape.z==1?ImageJProjectionAxis::Time:ImageJProjectionAxis::Z;
    const auto extent=automatic==ImageJProjectionAxis::Channels?shape.c:
        automatic==ImageJProjectionAxis::Time?shape.t:shape.z;
    if(name=="show_imagej_projection" || name=="show_imagej_grouped_projection") {
      for(const auto& item:p.items()) if(item.key()!="record" && item.key()!="document_id")
        throw std::invalid_argument("Unknown projection dialog parameter: "+item.key());
      std::optional<MetadataObject> original;
      if(p.contains("record")) {original=metadataObjectFromJson(p.at("record"));validate_imagej_projection_command_record(*original);}
      const bool hyper=(shape.c>1 && (shape.z>1 || shape.t>1)) || (shape.z>1 && shape.t>1);
      std::optional<ImageJProjectionDocumentRequest> saved;
      if(original)saved=imagej_projection_request_from_record(
          metadataObjectFromJson(metadataJson(*original).at("request")));
      const bool grouped=saved?saved->projection.group_size>0:
          name=="show_imagej_grouped_projection" && !hyper;
      if(grouped && extent<2) throw std::invalid_argument("Grouped projection requires a stack");
      Json values{{"axis","automatic"},{"first",1},{"last",extent},
        {"method",grouped?"mean":"maximum"},{"all_time_frames",true},{"all_channels",true},
        {"clip_when_summing",false}};
      Json schema=Json::array({
        {{"name","axis"},{"label","Projection axis"},{"kind","text"},{"choices",{"automatic","z","time","channels"}}},
        {{"name","first"},{"label","Start index"},{"kind","int"}},
        {{"name","last"},{"label","Stop index"},{"kind","int"}},
        {{"name","method"},{"label","Projection method"},{"kind","text"},{"choices",{"maximum","minimum","mean","sum","median","stddev"}}},
        {{"name","all_time_frames"},{"label","All time frames"},{"kind","bool"}},
        {{"name","all_channels"},{"label","All channels"},{"kind","bool"}},
        {{"name","clip_when_summing"},{"label","Clip RGB sums"},{"kind","bool"}}});
      if(saved) {
        const auto old=metadataJson(imagej_projection_request_record(*saved));
        for(const auto* key:{"axis","method","all_time_frames","all_channels","clip_when_summing"})values[key]=old.at(key);
        values["first"]=saved->projection.first+1;values["last"]=saved->projection.last+1;
      }
      if(grouped) {values["group_size"]=saved?saved->projection.group_size:extent;schema.push_back({{"name","group_size"},{"label","Group size"},{"kind","int"}});}
      impl->stopAnimation("imagej_projection");
      impl->ask(original?"Rebind ImageJ projection":grouped?"ImageJ Grouped Z Project":"ImageJ Z Project",std::move(values),
          [this,source,original,grouped](Json values) {
            if(grouped && (!values.at("group_size").is_number_integer() || values.at("group_size")<1))
              throw std::invalid_argument("Grouped projection requires a positive group size");
            const auto now=nativeSnapshot();
            if(!source.same_document(now) || !source.revisions().matches(now.revisions(),Change::All))throw RevisionConflict();
            if(original) values["rebind_record"]=metadataJson(*original);
            action("project_imagej",values);
          },std::move(schema),"imagej_projection");
      return;
    }
    const auto configured=static_cast<std::size_t>(impl->preferenceState.settings.integer("processing_memory_mb"))*1024*1024;
    ImageJProjectionDocumentRequest request;
    request.max_working_bytes=configured?configured:std::numeric_limits<std::size_t>::max();
    request.projection.max_working_bytes=request.max_working_bytes;
    request.projection.max_output_pixel_bytes=request.max_working_bytes;
    request.projection.active_t=source.display().t;request.projection.active_c=source.display().c;
    request.projection.last=extent-1;request.desktop_grays=true;
    if(!recognized_channel_display(source.display(),source.data().dataset) && shape.s==1)
      request.source_lut=inheritedDisplayLut(metadataJson(source.display().properties));
    auto command=[&] {
      if(name=="replay_imagej_projection") {
        for(const auto& f:p.items()) if(f.key()!="record" && f.key()!="document_id")
          throw std::invalid_argument("Unknown projection replay field: "+f.key());
        if(!p.contains("record"))throw std::invalid_argument("Projection replay requires a record");
        return imagej_projection_command_from_record(source,metadataObjectFromJson(p.at("record")));
      }
      auto raw=metadataJson(imagej_projection_request_record(request));
      for(const auto& f:p.items()) {
        if(f.key()=="rebind_record" || f.key()=="document_id")continue;
        if(f.key()=="first" || f.key()=="last") {
          if(!f.value().is_number_integer() || f.value()<1 || f.value()>std::numeric_limits<int>::max())throw std::invalid_argument("Projection bounds require positive native integer indices");
          raw[f.key()]=f.value().get<int>()-1;
        } else if(f.key()=="axis" || f.key()=="method" || f.key()=="all_time_frames" || f.key()=="all_channels" ||
            f.key()=="group_size" || f.key()=="clip_when_summing") raw[f.key()]=f.value();
        else throw std::invalid_argument("Unknown projection parameter: "+f.key());
      }
      request=imagej_projection_request_from_record(metadataObjectFromJson(raw));
      return p.contains("rebind_record")?rebind_imagej_projection_command(source,
          metadataObjectFromJson(p.at("rebind_record")),request):resolve_imagej_projection_command(source,request);
    }();
    if(configured && command.estimate().working_bytes>configured)
      throw std::invalid_argument("Projection exceeds current processing memory budget");
    const auto bytes=command.estimate().working_bytes;const auto recorded=recording;
    impl->stopAnimation("imagej_projection");
    impl->start([command,recorded,configured] {
      auto prepared=prepare_imagej_projection(command,{},configured?configured:std::numeric_limits<std::size_t>::max());
      return Impl::JobResult(std::move(prepared),command.record(),recorded);
    },facade,false,"ImageJ projection","ImageJ projection",bytes,{},true);
    return;
  }
  if(ContrastPanel::handles(name)){
    if(targetId && (documents_.empty() || targetId!=document()->id))throw std::invalid_argument("Contrast controls require the active image");
    Json values=p;if(values.is_object())values.erase("document_id");
    if(name=="show_contrast"||name=="close_contrast"){
      if(!values.is_object()||!values.empty())throw std::invalid_argument("Contrast visibility accepts only the active document ID");
      if(name=="show_contrast"){impl->contrastPanel.open();impl->tickContrast();}else {impl->cancelContrastApply();impl->contrastPanel.close();}
    }else{
      impl->tickContrast();
      if(name=="cancel_contrast_apply"){
        if(!values.is_object() || !values.empty())throw std::invalid_argument("Cancel Apply accepts no parameters");
        impl->cancelContrastApply();return;
      }
      if(name=="configure_contrast_target"){
        if(busy())throw std::invalid_argument("Wait for the current edit");
        if(!values.is_object() || values.size()!=1 || !values.contains("target"))throw std::invalid_argument("Contrast target requires only a target object");
        impl->contrastPanel.configureTarget(gaussianRebindRequest(values.at("target")));return;
      }
      impl->contrastPanel.action(name,values,busy(),
        [this](const DocumentSnapshot &source,std::array<double,2> range){impl->publishContrast(source,range);},
        [this](const DocumentSnapshot &source,bool all,bool selection){impl->contrastAuto(source,all,selection);},settings().conversion_options().unsigned16_bits,
        [this](const DocumentSnapshot &source,std::array<double,2> range,const TargetRequest &target){impl->applyContrast(source,range,target);});
    }
    return;
  }
  if (name == "animation") {
    impl->configureAnimation(p);
    return;
  }
  if(name=="show_movie_export") {
    if(busy())throw std::invalid_argument("Wait for the current edit");
    for(const auto &field:p.items())if(field.key()!="document_id")throw std::invalid_argument("Movie export accepts only its source ID");
    impl->stopAnimation("movie_export");
    Json movieDisplay=Json::object();
    const auto &movieMetadata=documentById(targetId)->state().metadata;
    for(const char *key:{"display_range","lut","labels","custom_lut"})if(movieMetadata.contains(key))movieDisplay[key]=movieMetadata[key];
    impl->movieExportPanel.show(nativeSnapshot(),std::move(movieDisplay),
        impl->animationAxis,impl->animationFirst,impl->animationLast,impl->animationStep,
        impl->animationMode,impl->animationTiming,impl->fps);
    return;
  }
  if (name == "show_animation_options") {
    if (busy()) throw std::invalid_argument("Wait for the current edit");
    for (const auto &field : p.items()) if (field.key() != "document_id")
      throw std::invalid_argument("Animation options accepts only its source ID");
    const auto captured = nativeSnapshot();
    const auto sourceId = captured.id();
    const auto revisions = captured.revisions();
    const auto capturedAxis = impl->animationAxis;
    const int extent = impl->animationExtent(captured.data().dataset.shape(),impl->animationAxis);
    impl->stopAnimation("options");
    const auto mode = impl->animationMode == PlaybackMode::Forward ? "Forward" :
        impl->animationMode == PlaybackMode::Reverse ? "Reverse" : "Ping-pong";
    const auto axisLabel = capturedAxis == "z" ? "Z" : capturedAxis == "c" ? "C" : "T";
    impl->ask(std::string("Animation options (")+axisLabel+")", {{"first",impl->animationFirst+1},
        {"last",impl->animationLast ? *impl->animationLast+1 : extent},
        {"step",impl->animationStep},{"mode",mode},
        {"timing",impl->animationTiming=="fps"?"FPS":"Calibrated time"}},
        [this,sourceId,revisions,capturedAxis](Json values) {
          if (impl->animationAxis!=capturedAxis || nativeSnapshot().id()!=sourceId || !nativeSnapshot().revisions().matches(revisions,Change::Pixels|Change::Metadata))
            throw std::invalid_argument("Animation options source or axis changed");
          for(const char *key:{"first","last"})
            if(!values.at(key).is_number_integer() || values.at(key)<1 || values.at(key)>INT32_MAX)
              throw std::invalid_argument("Animation First and Last must be positive integers");
          const auto label=values.at("mode").get<std::string>();
          if(label!="Forward" && label!="Reverse" && label!="Ping-pong")throw std::invalid_argument("Unknown animation mode");
          const auto timing=values.at("timing").get<std::string>();
          if(timing!="FPS" && timing!="Calibrated time")throw std::invalid_argument("Unknown animation timing");
          action("animation",{{"first",values.at("first").get<int>()-1},
              {"last",values.at("last").get<int>()-1},{"step",values.at("step")},
              {"mode",label=="Forward"?"forward":label=="Reverse"?"reverse":"ping_pong"},
              {"timing",timing=="FPS"?"fps":"calibrated_time"}});
        }, Json::array({{{"name","first"},{"label","First (1-based)"},{"kind","int"}},
            {{"name","last"},{"label","Last (inclusive, 1-based)"},{"kind","int"}},
            {{"name","step"},{"label","Step"},{"kind","int"}},
            {{"name","mode"},{"label","Mode"},{"kind","choice"},{"choices",{"Forward","Reverse","Ping-pong"}}},
            {{"name","timing"},{"label","Timing"},{"kind","choice"},
             {"choices",impl->animationCalibratedAxis(captured.data().dataset.shape(),capturedAxis)?
                Json::array({"FPS","Calibrated time"}):Json::array({"FPS"})}}}),"animation_options");
    return;
  }
  if(name=="show_selection_enlargement"){
    for(const auto &[key,value]:p.items())if(key!="document_id" && key!="shrink")throw std::invalid_argument("Unknown enlargement dialog parameter");
    if(p.contains("shrink") && !p.at("shrink").is_boolean())throw std::invalid_argument("Shrink flag must be Boolean");
    const auto source=nativeSnapshot(targetId);
    if(busy() || source.id()!=controller_->active_document_id())throw std::invalid_argument("Enlargement requires the idle active document");
    (void)estimate_selection_enlargement_command(source,{0,SelectionEnlargementUnits::Pixels,SelectionEnlargementGeometry::FullRegionV2},impl->selectionCreationLimits());
    const auto &cal=source.data().dataset.calibration();const auto units=impl->preferenceState.settings.text("selection_enlargement_units");
    double distance=impl->preferenceState.settings.number("selection_enlargement_distance");
    if(p.value("shrink",false))distance=-distance;if(units=="calibrated")distance*=cal.spacing[0];
    require_finite(distance,"Default enlargement distance");
    impl->ask("Enlarge selection",{{"distance",distance},{"units",units}},[this,source](Json values){
      const auto current=nativeSnapshot(source.id());
      if(!source.same_document(current) || !source.revisions().matches(current.revisions(),Change::All))throw RevisionConflict();
      auto request=impl->enlargementRequest(values);request.geometry=SelectionEnlargementGeometry::FullRegionV2;
      impl->publishEnlargement(resolve_selection_enlargement_command(source,request,impl->selectionCreationLimits()));
    },Json::array({{{"name","distance"},{"label","Distance (negative shrinks)"},{"kind","float"}},
        {{"name","units"},{"label","Units"},{"kind","choice"},{"choices",{"pixels","calibrated"}}}}),"selection_enlargement");
    impl->dialog.enlargementSource=source;return;
  }
  if(name=="enlarge_selection" || name=="grow_selection" || name=="shrink_selection"){
    Json values=p;
    if(name!="enlarge_selection"){
      for(const auto &[key,value]:p.items())if(key!="radius" && key!="units" && key!="geometry" && key!="document_id")throw std::invalid_argument("Unknown enlargement parameter");
      const auto radius=p.value("radius",Json(1.));if(!radius.is_number() || radius.get<double>()<0)throw std::invalid_argument("Selection radius must be a nonnegative number");
      values.erase("radius");values["distance"]=(name=="shrink_selection"?-1.:1.)*radius.get<double>();
    }
    const auto request=impl->enlargementRequest(values);const auto source=nativeSnapshot(targetId);
    if(busy() || source.id()!=controller_->active_document_id())throw std::invalid_argument("Enlargement requires the idle active document");
    impl->publishEnlargement(resolve_selection_enlargement_command(source,request,impl->selectionCreationLimits()));return;
  }
  if(name=="replay_selection_enlargement"){
    for(const auto &[key,value]:p.items())if(key!="record" && key!="rebind" && key!="document_id")throw std::invalid_argument("Unknown enlargement replay parameter");
    if(!p.contains("record") || !p.at("record").is_object())throw std::invalid_argument("Supply a complete enlargement record");
    const auto record=metadataObjectFromJson(p.at("record"));const auto source=nativeSnapshot(targetId);
    if(busy() || source.id()!=controller_->active_document_id())throw std::invalid_argument("Enlargement replay requires the idle active document");
    const auto command=p.contains("rebind")?rebind_selection_enlargement_command(source,record,impl->enlargementRequest(p.at("rebind"),true),impl->selectionCreationLimits()):
        selection_enlargement_command_from_record(source,record,impl->selectionCreationLimits());
    impl->publishEnlargement(command);return;
  }
  if(name=="select_all" || name=="select_none" || name=="restore_selection" || name=="invert_selection"){
    const auto facade=documentById(targetId);
    if(facade->scientific()){
      if(busy() || facade->id!=controller_->active_document_id())throw std::invalid_argument("Selection commands require the idle active document");
      if(!p.is_object() || (p.size() && !(p.size()==1 && p.contains("document_id"))))throw std::invalid_argument("Selection commands accept only their document ID");
      const auto source=facade->nativeSnapshot();std::optional<Selection> selected;
      if(name=="select_all"){const auto shape=source.data().dataset.shape();selected=Selection("rectangle",{{0,0},{double(shape.x),double(shape.y)}});}
      else if(name=="restore_selection")selected=source.data().previous_selection;
      else if(name=="invert_selection"){
        const auto shape=source.data().dataset.shape();
        const SelectionCreationRequest request{
            source.data().selection ? SelectionCreationOperation::Xor : SelectionCreationOperation::Replace,
            Selection("rectangle",{{0,0},{double(shape.x),double(shape.y)}})};
        const auto limits=impl->selectionCreationLimits();
        const auto command=resolve_selection_creation_command(source,request,limits);
        OutputPublication outputs(results,workflow,ComponentCounts{},"",
            recording && command.changes_selection() ? metadataJson(command.record()) : Json(nullptr));
        auto edit=prepare_selection_creation(command,{},limits.max_working_bytes);
        impl->cancelCreations("selection_command");impl->cancelSavedRoiGeometry("selection_command");
        controller_->commit(edit);outputs.publish();impl->tickContrast();return;
      }
      impl->cancelCreations("selection_command");impl->cancelSavedRoiGeometry("selection_command");
      controller_->select(std::move(selected),facade->id);impl->tickContrast();return;
    }
  }
  if (name == "create_roi") {
    const auto facade = documentById(targetId);
    if (busy() || facade->id != controller_->active_document_id())
      throw std::invalid_argument("Create ROI requires the idle active document");
    if (!p.is_object() || (p.size() && !(p.size() == 1 && p.contains("document_id"))))
      throw std::invalid_argument("Create ROI accepts only its document ID");
    std::string rectangle = "rectangle";
    if (facade->view().orthogonal) facade->setView({{"orthogonal", false}});
    impl->cancelSavedRoiGeometry("new_manager_area");
    impl->cancelCreations("new_manager_area");
    impl->scientificRoiCreationOwner = facade->id;
    impl->creationMode = SelectionCreationOperation::Replace;
    impl->tool.swap(rectangle);
    return;
  }
  if ((name == "add_roi" || name == "select_roi" || name == "update_roi" ||
       name == "rename_roi" || name == "delete_roi" || name == "roi_properties") &&
      (documentById(targetId)->scientific() || name == "roi_properties")) {
    const auto source = nativeSnapshot(targetId);
    if (source.id() != controller_->active_document_id())
      throw std::invalid_argument("ROI manager requires the active document");
    if (!p.is_object()) throw std::invalid_argument("ROI parameters must be an object");
    for (const auto &field : p.items()) {
      const auto &key = field.key();
      if (key != "document_id" && key != "index" &&
          !((name == "add_roi" || name == "rename_roi" || name == "roi_properties") && key == "name") &&
          !(name == "add_roi" && key == "all_slices") &&
          !(name == "roi_properties" && (key == "style" || key == "t" || key == "c" || key == "z")))
        throw std::invalid_argument("Unknown ROI parameter: " + key);
    }
    const auto index = [&]() -> std::size_t {
      if (!p.contains("index")) {
        if (impl->roiIndex < 0) throw std::invalid_argument("Select a saved ROI");
        return static_cast<std::size_t>(impl->roiIndex);
      }
      const auto &value = p.at("index");
      if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>() < 0))
        throw std::invalid_argument("ROI index must be a nonnegative integer");
      const auto result = value.get<std::uint64_t>();
      if (result > std::numeric_limits<std::size_t>::max()) throw std::invalid_argument("ROI index overflow");
      return static_cast<std::size_t>(result);
    };
    const auto nameValue = [&](const std::string &fallback) {
      if (!p.contains("name")) return fallback;
      if (!p.at("name").is_string()) throw std::invalid_argument("ROI name must be text");
      return p.at("name").get<std::string>();
    };
    const auto prepare = [&]() -> PreparedRoiManagerEdit {
      if (name == "add_roi") {
        if (p.contains("all_slices") && !p.at("all_slices").is_boolean())
          throw std::invalid_argument("all_slices must be Boolean");
        return prepare_store_roi(source, nameValue(""), boolean(get(p, "all_slices")));
      }
      const auto row = index();
      if (name == "select_roi") return prepare_select_roi(source, row);
      if (name == "update_roi") return prepare_update_roi(source, row);
      if (name == "delete_roi") return prepare_delete_roi(source, row);
      if (name == "rename_roi") return prepare_rename_roi(source, row, nameValue(""));
      if (row >= source.data().rois.size()) throw std::out_of_range("Select a saved ROI");
      const auto &original = source.data().rois[row];
      const auto axis = [&](const char *key, std::optional<int> fallback) -> std::optional<int> {
        if (!p.contains(key)) return fallback;
        const auto &value = p.at(key);
        if (value.is_null()) return {};
        if (!value.is_number_integer() || value < 0 || value > std::numeric_limits<int>::max())
          throw std::invalid_argument("ROI position must be null or a zero-based integer");
        return value.get<int>();
      };
      const auto style = p.contains("style") ? metadataObjectFromJson(p.at("style")) : MetadataObject{};
      return prepare_roi_properties(source, row, nameValue(original.name), style,
          axis("t", original.t), axis("c", original.c), axis("z", original.z));
    };
    impl->commitRoiManager(source, prepare());
    return;
  }
  if (name == "auto_contrast" || name == "replay_auto_contrast") {
    for (const auto &field : p.items())
      if (field.key() != "document_id" &&
          !(name == "auto_contrast" && (field.key() == "all_channels" ||
              field.key() == "use_selection" || field.key() == "unsigned16_bits")) &&
          !(name == "replay_auto_contrast" &&
              (field.key() == "record" || field.key() == "rebind_request")))
        throw std::invalid_argument("Unknown Auto parameter: " + field.key());
    if (name == "auto_contrast") {
      auto options = impl->autoOptions();
      for (const auto *key : {"all_channels", "use_selection"})
        if (p.contains(key) && !p.at(key).is_boolean())
          throw std::invalid_argument(std::string(key) + " must be Boolean");
      options.scope = boolean(get(p,"all_channels")) ? AutoContrastScope::CompositeAll
                                                    : AutoContrastScope::ActiveChannel;
      options.roi = boolean(get(p,"use_selection"),true) ? AutoContrastRoiPolicy::ActiveSelection
                                                        : AutoContrastRoiPolicy::WholePlane;
      if (p.contains("unsigned16_bits")) {
        const auto &bits = p.at("unsigned16_bits");
        if (!bits.is_number_integer() || bits < 0 || bits > 16)
          throw std::invalid_argument("Unsigned16 bits must be an integer in 0..16");
        options.unsigned16_bits = bits.get<int>();
      }
      applyAutoContrast(nativeSnapshot(targetId), options);
    } else {
      std::optional<AutoContrastRequest> request;
      if (p.contains("rebind_request")) {
        const auto &value = p.at("rebind_request");
        if (!value.is_object() || value.size() != 4 ||
            !value.contains("previous_divisor") || !value.contains("scope") ||
            !value.contains("roi") || !value.contains("unsigned16_bits"))
          throw std::invalid_argument("Auto rebind requires a complete request");
        for (const auto *key : {"previous_divisor", "unsigned16_bits"})
          if (!value.at(key).is_number_integer() || value.at(key) < 0 || value.at(key) > 5000)
            throw std::invalid_argument("Auto rebind integers are invalid");
        const auto scope = string(value.at("scope"));
        const auto roi = string(value.at("roi"));
        if ((scope != "active_channel" && scope != "composite_all") ||
            (roi != "active_selection" && roi != "whole_plane"))
          throw std::invalid_argument("Auto rebind scope/ROI is invalid");
        request = AutoContrastRequest{value.at("previous_divisor").get<int>(),
          scope == "active_channel" ? AutoContrastScope::ActiveChannel : AutoContrastScope::CompositeAll,
          roi == "active_selection" ? AutoContrastRoiPolicy::ActiveSelection : AutoContrastRoiPolicy::WholePlane,
          value.at("unsigned16_bits").get<int>()};
      }
      replayAutoContrast(*autoRecord, request, targetId);
    }
    return;
  }
  if (name == "show_channel_display" || name == "enable_channel_display" ||
      name == "channel_display" || name == "replay_channel_display") {
    for (const auto &field : p.items())
      if (field.key() != "document_id" &&
          !(name == "channel_display" && field.key() == "request") &&
          !(name == "replay_channel_display" &&
            (field.key() == "record" || field.key() == "rebind_request")))
        throw std::invalid_argument("Unknown channel display parameter: " + field.key());
    if (name == "show_channel_display") {
      if (documentById(targetId)->id != document()->id)
        throw std::invalid_argument("Open the channel panel on the active document");
      impl->channelDisplayRebind.reset(); impl->channelDisplayRebindOwner.reset();
      impl->channelDisplayPanel.open();
    } else if (name == "enable_channel_display") enableChannelDisplay(targetId);
    else if (name == "channel_display")
      applyChannelDisplay(nativeSnapshot(targetId),
          channelDisplayRequest(nativeSnapshot(targetId), p.at("request")));
    else {
      const auto record = metadataObjectFromJson(p.at("record"));
      validate_channel_display_command_record(record);
      const auto rebind = p.contains("rebind_request")
          ? std::optional<ChannelDisplayRequest>(channelDisplayRequest(nativeSnapshot(targetId), p.at("rebind_request")))
          : std::nullopt;
      replayChannelDisplay(record, rebind, targetId);
    }
    return;
  }
  if (name == "display_range" || name == "lut" || name == "load_lut" || name == "save_lut") {
    const auto source = nativeSnapshot(targetId);
    auto state = recognized_channel_display(source.display(), source.data().dataset);
    if (!state && targetId && source.id() != document()->id)
      throw std::invalid_argument("Legacy display actions require the active document");
    if (state) {
      if (busy()) throw std::invalid_argument("Wait for the current edit");
      auto &channel = state->channels.at(source.display().c);
      if (name == "save_lut") {
        const auto table = channel.lut.table ? *channel.lut.table : channel_display_lut(channel.lut.name);
        Json rows = Json::array();
        for (const auto &rgb : table) rows.push_back({rgb[0], rgb[1], rgb[2]});
        writeJson(string(get(p, "path")), {{"format", "imagejxx-lut"}, {"lut", std::move(rows)}});
        return;
      }
      ChannelDisplayRequest request{ChannelDisplayAction::Set, {}, {}};
      if (name == "display_range") {
        if (p.contains("auto") && !p.at("auto").is_boolean())
          throw std::invalid_argument("Display range auto must be boolean");
        if (boolean(get(p, "auto"))) {
          request.action = ChannelDisplayAction::ResetRange;
          request.reset_channels = {source.display().c};
        } else {
          if (!get(p, "minimum").is_number() || !get(p, "maximum").is_number())
            throw std::invalid_argument("Display range requires numeric endpoints");
          channel.range = {p.at("minimum").get<double>(), p.at("maximum").get<double>()};
        }
      } else if (name == "lut")
        channel.lut = inheritedDisplayLut({{"lut", string(get(p, "name"), "Grays")}});
      else {
        const auto file = readJson(string(get(p, "path")));
        channel.lut = inheritedDisplayLut({{"custom_lut", file.at("lut")}});
      }
      request.state = std::move(*state);
      applyChannelDisplay(source, request);
      return;
    }
  }
  if(name=="show_roi_export_record") {
    for(const auto &field:p.items())if(field.key()!="document_id" && field.key()!="record")throw std::invalid_argument("Unknown ROI export review parameter: "+field.key());
    if(targetId && nativeSnapshot().id()!=targetId)throw std::invalid_argument("ROI export requires the active image");
    impl->showRoiExport(metadataObjectFromJson(p.at("record")),(get(p.at("record"),"schema")=="imagejxx.imagej-roi-zip-export" || (get(p.at("record"),"schema")=="imagejxx.imagej-annotation-export" && get(p.at("record"),"packaging")=="zip")));return;
  }
  if(name=="show_roi_export" || name=="show_roi_zip_export") {
    for(const auto &field:p.items())if(field.key()!="document_id")throw std::invalid_argument("Unknown ROI export parameter: "+field.key());
    if(targetId && nativeSnapshot().id()!=targetId)throw std::invalid_argument("ROI export requires the active image");
    impl->showRoiExport({},name=="show_roi_zip_export");return;
  }
  if(name=="show_roi_import_record") {
    for(const auto &field:p.items())if(field.key()!="document_id" && field.key()!="record")throw std::invalid_argument("Unknown ROI import review parameter: "+field.key());
    if(targetId && nativeSnapshot().id()!=targetId)throw std::invalid_argument("ROI import requires the active destination");
    impl->showRoiImport(metadataObjectFromJson(p.at("record")));return;
  }
  if(name=="show_roi_import") {
    for(const auto &field:p.items())if(field.key()!="document_id")throw std::invalid_argument("Unknown ROI import panel parameter: "+field.key());
    if(targetId && nativeSnapshot().id()!=targetId)throw std::invalid_argument("ROI import requires the active destination");
    impl->showRoiImport();return;
  }
  if(name=="show_planar_annotation_rebind") {
    if(targetId && nativeSnapshot().id()!=targetId)throw std::invalid_argument("Annotation rebind requires the active destination");
    for(const auto &field:p.items())
      if(field.key()!="record" && field.key()!="document_id")throw std::invalid_argument("Unknown annotation rebind parameter: "+field.key());
    impl->showPlanarAnnotationRebind(metadataObjectFromJson(p.at("record")));return;
  }
  if(name=="replay_imagej_annotation_import") {
    for(const auto &field:p.items())if(field.key()!="record" && field.key()!="document_id")throw std::invalid_argument("Unknown mixed import replay parameter");
    impl->publishImageJAnnotationImport(imagej_annotation_import_from_record(nativeSnapshot(targetId),metadataObjectFromJson(p.at("record"))));return;
  }
  if(name=="replay_imagej_roi_import") {
    for(const auto &field:p.items())
      if(field.key()!="record" && field.key()!="document_id")
        throw std::invalid_argument("Unknown ROI import replay parameter: "+field.key());
    const auto command=imagej_roi_import_from_record(nativeSnapshot(targetId),metadataObjectFromJson(p.at("record")));
    impl->publishImageJRoiImport(command);return;
  }
  if(name=="show_display_annotations") {
    for(const auto &field:p.items())if(field.key()!="document_id")throw std::invalid_argument("Unknown display annotation parameter");
    if(targetId && nativeSnapshot().id()!=targetId)throw std::invalid_argument("Display annotations require the active image");
    impl->roiImportPanel.cancel("display_annotations");impl->roiExportPanel.cancel("display_annotations");
    impl->selectionPanel.cancel();impl->cancelCreations("display_annotations");
    for(auto &pane:impl->panes)pane.orthogonal.resetInput();
    impl->displayAnnotationPanel.show(nativeSnapshot());return;
  }
  if(name=="replay_display_annotation") {
    for(const auto &field:p.items())if(field.key()!="document_id" && field.key()!="record" && field.key()!="rebind_request")throw std::invalid_argument("Unknown display annotation replay parameter");
    const auto source=nativeSnapshot(targetId);const auto record=metadataObjectFromJson(p.at("record"));
    if(p.contains("rebind_request"))validate_planar_display_annotation_command_record(record);
    const auto command=p.contains("rebind_request")?
      rebind_planar_display_annotation_command(source,record,decode_planar_display_annotation_request(source.data().dataset.shape(),metadataObjectFromJson(p.at("rebind_request")))):
      planar_display_annotation_command_from_record(source,record);
    impl->publishDisplayAnnotation(command);return;
  }
  if(name=="replay_planar_annotation") {
    for(const auto &field:p.items())
      if(field.key()!="record" && field.key()!="document_id" && field.key()!="rebind_request")
        throw std::invalid_argument("Unknown annotation replay parameter: "+field.key());
    const auto source=nativeSnapshot(targetId);
    const auto record=metadataObjectFromJson(p.at("record"));
    const auto command=p.contains("rebind_request")
        ?rebind_planar_annotation_command(source,record,decode_planar_annotation_request(source.data().dataset.shape(),metadataObjectFromJson(p.at("rebind_request"))))
        :planar_annotation_command_from_record(source,record);
    impl->publishPlanarAnnotation(command);return;
  }
  if (name == "replay_axis_spacing") {
    for (const auto &field : p.items())
      if (field.key() != "record" && field.key() != "document_id")
        throw std::invalid_argument("Unknown physical spacing replay parameter: " + field.key());
    publishAxisSpacing(axis_spacing_command_from_record(nativeSnapshot(targetId),
        metadataObjectFromJson(p.at("record"))), false);
    return;
  }
  if (name == "replay_slicer_resize") {
    for (const auto &field : p.items())
      if (field.key() != "record" && field.key() != "document_id")
        throw std::invalid_argument("Unknown bilinear replay parameter: " + field.key());
    publishSlicerResize(slicer_resize_command_from_record(nativeSnapshot(targetId),
        metadataObjectFromJson(p.at("record"))), false);
    return;
  }
  if (name == "replay_axis_orientation") {
    for (const auto &field : p.items())
      if (field.key() != "record" && field.key() != "document_id" &&
          field.key() != "rebind_request")
        throw std::invalid_argument("Unknown orientation replay parameter: " + field.key());
    const auto record = metadataObjectFromJson(p.at("record"));
    const auto rebind = p.contains("rebind_request")
        ? std::optional<AxisRangeOrientationRequest>(axisOrientationRebindRequest(p.at("rebind_request")))
        : std::nullopt;
    replayAxisOrientation(record, rebind, targetId);
    return;
  }
  if (name == "replay_axis_extraction") {
    if (!p.is_object() || !p.contains("record"))
      throw std::invalid_argument("Extraction replay requires a record");
    for (const auto &field : p.items())
      if (field.key() != "record" && field.key() != "document_id" &&
          field.key() != "rebind_request")
        throw std::invalid_argument("Unknown extraction replay parameter: " + field.key());
    const auto record = metadataObjectFromJson(p.at("record"));
    validate_axis_extraction_command_record(record);
    const auto rebind = p.contains("rebind_request")
        ? std::optional<AxisExtractionRequest>(axisExtractionRebindRequest(p.at("rebind_request")))
        : std::nullopt;
    replayAxisExtraction(record, rebind, targetId);
    return;
  }
  if(name=="configure_threshold_target") {
    if(!p.is_object())throw std::invalid_argument("Threshold target parameters require an object");
    if(targetId && impl->thresholdSource && targetId!=impl->thresholdSource->id())throw std::invalid_argument("Threshold target differs from captured owner");
    for(const auto &field:p.items())if(field.key()!="target" && field.key()!="histogram_roi" && field.key()!="document_id")throw std::invalid_argument("Unknown threshold target parameter");
    if(p.contains("histogram_roi") && !p.at("histogram_roi").is_boolean())throw std::invalid_argument("Histogram ROI must be Boolean");
    (void)impl->checkedThresholdSource();
    if(!p.contains("target"))throw std::invalid_argument("Threshold target is required");
    const auto request=[&] {try{return gaussianRebindRequest(p.at("target"));}catch(const std::invalid_argument &e){std::string message=e.what();const auto at=message.find("Gaussian");if(at!=std::string::npos)message.replace(at,8,"Threshold");throw std::invalid_argument(message);}}();
    impl->thresholdPanel.configureTarget(request,p.value("histogram_roi",impl->thresholdPanel.histogramUsesRoi()));
    impl->thresholdApplyPreview();return;
  }
  if(name=="auto_manual_threshold") {
    if(targetId && impl->thresholdSource && targetId!=impl->thresholdSource->id())throw std::invalid_argument("Automatic display target differs from captured owner");
    for(const auto &entry:p.items())if(entry.key()!="method" && entry.key()!="bright" && entry.key()!="ignore_black" && entry.key()!="ignore_white" && entry.key()!="stack_histogram" && entry.key()!="histogram_roi" && entry.key()!="document_id")throw std::invalid_argument("Unknown automatic display parameter");
    for(const auto key:{"bright","ignore_black","ignore_white","stack_histogram","histogram_roi"})if(p.contains(key) && !p.at(key).is_boolean())throw std::invalid_argument("Automatic display flags must be Boolean");
    const auto defaults = settings();
    if(p.contains("histogram_roi"))impl->thresholdPanel.setHistogramUsesRoi(p.at("histogram_roi").get<bool>());
    const auto result=impl->thresholdAutomatic(p.value("method",defaults.text("threshold_method")),p.value("bright",defaults.boolean("threshold_bright")),p.value("ignore_black",defaults.boolean("threshold_ignore_black")),p.value("ignore_white",defaults.boolean("threshold_ignore_white")),p.value("stack_histogram",defaults.boolean("threshold_stack_histogram") && impl->checkedThresholdSource().data().dataset.shape().z>1));
    impl->thresholdPanel.setAutomaticResult(result);return;
  }
  if (name == "replay_manual_threshold") {
    if (!p.is_object()) throw std::invalid_argument("Threshold replay requires an object");
    for (const auto &entry : p.items())
      if (entry.key() != "document_id" && entry.key() != "record" && entry.key() != "threshold_rebind")
        throw std::invalid_argument("Unknown threshold replay parameter: " + entry.key());
    const auto record = metadataObjectFromJson(p.at("record"));
    const auto source = nativeSnapshot(targetId);
    const auto command = p.contains("threshold_rebind")
        ? rebind_manual_threshold_command(source, record, gaussianRebindRequest(p.at("threshold_rebind")))
        : manual_threshold_command_from_record(source, record);
    impl->submitThreshold(command, recording ? metadataJson(command.record()) : Json(),
                          impl->thresholdCancelEpoch.load());
    return;
  }
  if (name == "show_z_subset" || name == "substack" || name == "replay_z_subset") {
    if (!p.is_object()) throw std::invalid_argument("Z subset parameters require an object");
    const auto source = nativeSnapshot(targetId);
    if (busy()) throw std::invalid_argument("Wait for the current edit");
    auto parse = [](const Json &values, int depth) {
      if (!values.is_object()) throw std::invalid_argument("Z subset request must be an object");
      for (const auto &entry : values.items())
        if (entry.key()!="first" && entry.key()!="last" && entry.key()!="step" && entry.key()!="replace" && entry.key()!="document_id")
          throw std::invalid_argument("Unknown Z subset parameter: "+entry.key());
      auto index = [&](const char *key,int fallback) {
        if (!values.contains(key)) return fallback;
        const auto &value=values[key];
        if (!value.is_number_integer() || value<1 || value>std::numeric_limits<int>::max())
          throw std::invalid_argument("Z subset bounds and step must be positive integers");
        return value.get<int>();
      };
      if (values.contains("replace") && !values["replace"].is_boolean())
        throw std::invalid_argument("Replace must be Boolean");
      return ZSubsetRequest{index("first",1)-1,index("last",depth)-1,index("step",1),
          values.value("replace",false)?OutputDestination::InPlace:OutputDestination::NewDocument};
    };
    if (name=="replay_z_subset") {
      for (const auto &entry:p.items())
        if(entry.key()!="record" && entry.key()!="rebind" && entry.key()!="document_id")
          throw std::invalid_argument("Unknown Z subset replay parameter");
      const auto record=metadataObjectFromJson(p.at("record"));
      const auto command=p.contains("rebind")
          ? rebind_z_subset_command(source,record,parse(p.at("rebind"),source.data().dataset.shape().z))
          : z_subset_command_from_record(source,record);
      publishZSubset(command);return;
    }
    if (name=="substack") {publishZSubset(resolve_z_subset_command(source,parse(p,source.data().dataset.shape().z)));return;}
    for(const auto &entry:p.items())if(entry.key()!="document_id" && entry.key()!="record")throw std::invalid_argument("Unknown Show Z subset parameter");
    std::optional<MetadataObject> original;
    Json defaults{{"first",1},{"last",source.data().dataset.shape().z},{"step",1},{"replace",false}};
    if(p.contains("record")) {
      original=metadataObjectFromJson(p.at("record"));validate_z_subset_command_record(*original);
      const auto &r=p.at("record").at("request");
      defaults={{"first",r.at("first").get<int>()+1},{"last",r.at("last").get<int>()+1},
          {"step",r.at("step")},{"replace",r.at("destination")=="in_place"}};
    }
    impl->ask(original?"Rebind Z subset":"Z subset",std::move(defaults),
        [this,source,parse,original](Json values) {
          const auto request=parse(values,source.data().dataset.shape().z);
          publishZSubset(original?rebind_z_subset_command(source,*original,request):resolve_z_subset_command(source,request));
        });
    for(auto &field:impl->dialog.schema) {
      if(field["name"]=="first")field["label"]="First Z (1-based)";
      else if(field["name"]=="last")field["label"]="Last Z (inclusive)";
      else if(field["name"]=="replace")field["label"]="Replace this stack (Undo available)";
    }
    impl->dialog.openedDocumentId=source.id();return;
  }
  if (name == "show_manual_threshold") {
    for (const auto &entry : p.items())
      if (entry.key() != "document_id")
        throw std::invalid_argument("Unknown manual threshold parameter: " + entry.key());
    const auto &id = get(p, "document_id");
    if (!id.is_null() && (!id.is_number_integer() || id < 0))
      throw std::invalid_argument("Threshold document ID must be nonnegative");
    impl->showThreshold(id.is_null() ? 0 : id.get<std::uint64_t>());
    return;
  }
  if (name == "auto_manual_threshold" || name == "set_manual_threshold" || name == "reset_manual_threshold" ||
      name == "manual_threshold_to_selection" || name == "apply_manual_threshold") {
    if (!p.is_object())
      throw std::invalid_argument("Threshold parameters must be an object");
    for (const auto &entry : p.items())
      if (entry.key() != "document_id" && entry.key() != "lower" && entry.key() != "upper")
        throw std::invalid_argument("Unknown threshold parameter: " + entry.key());
    if (!impl->thresholdSource || (p.contains("document_id") &&
        p["document_id"] != impl->thresholdSource->id()))
      throw std::invalid_argument("Open the requested threshold source first");
    if (name == "reset_manual_threshold") {
      impl->thresholdReset();
      return;
    }
    if (!get(p, "lower").is_number() || !get(p, "upper").is_number())
      throw std::invalid_argument("Threshold requires numeric lower and upper bounds");
    const ManualThresholdRange range{p["lower"].get<double>(), p["upper"].get<double>()};
    if (name == "set_manual_threshold") impl->thresholdSet(range);
    else if (name == "manual_threshold_to_selection") impl->thresholdToSelection(range);
    else impl->thresholdApply(range);
    return;
  }
  if (name == "reslice_slicer_axis_range" || name == "show_axis_range_slicer_resize") {
    if (!p.is_object())
      throw std::invalid_argument("ImageJ bilinear parameters must be an object");
    const bool direct = name == "reslice_slicer_axis_range";
    for (auto field = p.begin(); field != p.end(); ++field) {
      const auto &key = field.key();
      if (!(key == "document_id" || (!direct && key == "record") || (direct && (key == "plane" || key == "first" ||
          key == "last" || key == "step" || key == "reverse_order" || key == "flip_vertical" || key == "transpose"))))
        throw std::invalid_argument("Unknown ImageJ bilinear parameter: " + key);
    }
    if (direct)
      for (const char *key : {"document_id", "plane", "first", "last", "step"})
        if (!p.contains(key))
          throw std::invalid_argument(std::string("Required ImageJ bilinear parameter: ") + key);
    const auto &ownerId = get(p, "document_id");
    if (p.contains("document_id") && (!ownerId.is_number_integer() || ownerId < 0 ||
        (direct && ownerId == 0)))
      throw std::invalid_argument("ImageJ bilinear source ID must be a positive integer for a direct action");
    if (busy())
      throw std::invalid_argument("Wait for the current edit");
    const auto source = nativeSnapshot(p.contains("document_id") ? ownerId.get<std::uint64_t>() : 0);
    const auto shape = source.data().dataset.shape();
    impl->requireAxisExtractionSource(source,AxisRangeResliceRequest{Plane::XZ,0,0,1,source.display().t,source.display().c},true);
    if (direct) {
      resliceSlicerAxisRange(source, slicerResizeRequest(source, p, false));
      return;
    }
    const auto original=p.contains("record")?p.at("record"):Json();
    impl->ask("Reslice with ImageJ bilinear", {{"plane", "XZ"}, {"first", 1},
        {"last", shape.y}, {"step", 1}, {"reverse_order", false}, {"flip_vertical", false}, {"transpose", false}},
        [this, source, original](Json values) {
          const auto request=slicerResizeRequest(source,values,true);
          if(original.is_null())resliceSlicerAxisRange(source,request);
          else publishSlicerResize(rebind_slicer_resize_command(source,metadataObjectFromJson(original),request));
        }, Json::array({
          {{"name", "plane"}, {"kind", "text"}, {"label", "Plane"}, {"choices", {"XZ", "YZ"}}},
          {{"name", "first"}, {"kind", "int"}, {"label", "First (1-based)"}},
          {{"name", "last"}, {"kind", "int"}, {"label", "Last (inclusive, 1-based)"}},
          {{"name", "step"}, {"kind", "int"}, {"label", "Step"}},
          {{"name", "reverse_order"}, {"kind", "bool"}, {"label", "Reverse selected planes"}},
          {{"name", "flip_vertical"}, {"kind", "bool"}, {"label", "Flip source Z before transpose"}},
          {{"name", "transpose"}, {"kind", "bool"}, {"label", "Transpose before bilinear sampling"}}}), "axis_range_slicer_resize");
    impl->dialog.axisRangeSlicerResizeSource = source;
    impl->dialog.openedDocumentId = source.id();
    return;
  }
  if (name == "reslice_spaced_axis_range" || name == "show_axis_range_spacing") {
    if (!p.is_object())
      throw std::invalid_argument("Physical spacing parameters must be an object");
    const bool direct = name == "reslice_spaced_axis_range";
    for (auto field = p.begin(); field != p.end(); ++field) {
      const auto &key = field.key();
      if (!(key == "document_id" || (!direct && key == "record") || (direct && (key == "plane" || key == "first" ||
          key == "last" || key == "step" || key == "physical_vertical_spacing" ||
          key == "sampling" || key == "reverse_order" || key == "flip_vertical" || key == "transpose"))))
        throw std::invalid_argument("Unknown physical spacing parameter: " + key);
    }
    if (direct)
      for (const char *key : {"document_id", "plane", "first", "last", "step",
                              "physical_vertical_spacing", "sampling"})
        if (!p.contains(key))
          throw std::invalid_argument(std::string("Required physical spacing parameter: ") + key);
    const auto &ownerId = get(p, "document_id");
    if (p.contains("document_id") && (!ownerId.is_number_integer() || ownerId < 0 ||
        (direct && ownerId == 0)))
      throw std::invalid_argument("Physical spacing source ID must be a positive integer for a direct action");
    if (busy())
      throw std::invalid_argument("Wait for the current edit");
    const auto source = nativeSnapshot(p.contains("document_id") ? ownerId.get<std::uint64_t>() : 0);
    const auto shape = source.data().dataset.shape();
    impl->requireAxisExtractionSource(source,AxisRangeResliceRequest{Plane::XZ,0,0,1,source.display().t,source.display().c},true);
    if (direct) {
      resliceSpacedAxisRange(source, physicalGridRequest(source, p, false));
      return;
    }
    const auto &calibration = source.data().dataset.calibration();
    const auto initialSpacing = std::hypot(calibration.direction[2] * calibration.spacing[2],
        calibration.direction[5] * calibration.spacing[2], calibration.direction[8] * calibration.spacing[2]);
    const auto original = p.contains("record") ? p.at("record") : Json();
    impl->ask("Reslice physical spacing", {{"plane", "XZ"}, {"first", 1},
        {"last", shape.y}, {"step", 1}, {"physical_vertical_spacing", initialSpacing},
        {"sampling", "Nearest"}, {"reverse_order", false}, {"flip_vertical", false}, {"transpose", false}},
        [this, source, original](Json values) {
          const auto request = physicalGridRequest(source, values, true);
          if (original.is_null()) resliceSpacedAxisRange(source, request);
          else publishAxisSpacing(rebind_axis_spacing_command(source,
              metadataObjectFromJson(original), request));
        }, Json::array({
          {{"name", "plane"}, {"kind", "text"}, {"label", "Plane"}, {"choices", {"XZ", "YZ"}}},
          {{"name", "first"}, {"kind", "int"}, {"label", "First (1-based)"}},
          {{"name", "last"}, {"kind", "int"}, {"label", "Last (inclusive, 1-based)"}},
          {{"name", "step"}, {"kind", "int"}, {"label", "Step"}},
          {{"name", "physical_vertical_spacing"}, {"kind", "float"},
           {"label", "Physical Z spacing before transpose (" + calibration.unit + ")"}},
          {{"name", "sampling"}, {"kind", "text"}, {"label", "Sampling"}, {"choices", {"Nearest", "Linear"}}},
          {{"name", "reverse_order"}, {"kind", "bool"}, {"label", "Reverse selected planes"}},
          {{"name", "flip_vertical"}, {"kind", "bool"}, {"label", "Flip source Z extent before sampling"}},
          {{"name", "transpose"}, {"kind", "bool"}, {"label", "Transpose output axes"}}}), "axis_range_spacing");
    impl->dialog.axisRangeSpacingSource = source;
    impl->dialog.openedDocumentId = source.id();
    return;
  }
  if (name == "reslice_oriented_axis_range" || name == "show_axis_range_orientation") {
    if (!p.is_object())
      throw std::invalid_argument("Oriented range parameters must be an object");
    const bool direct = name == "reslice_oriented_axis_range";
    for (auto field = p.begin(); field != p.end(); ++field) {
      const auto &key = field.key();
      if (!(key == "document_id" || (direct && (key == "plane" || key == "first" ||
          key == "last" || key == "step" || key == "reverse_order" ||
          key == "flip_vertical" || key == "transpose"))))
        throw std::invalid_argument("Unknown oriented range parameter: " + key);
    }
    if (direct)
      for (const char *key : {"document_id", "plane", "first", "last", "step"})
        if (!p.contains(key))
          throw std::invalid_argument(std::string("Required oriented range parameter: ") + key);
    const auto &ownerId = get(p, "document_id");
    if (p.contains("document_id") && (!ownerId.is_number_integer() || ownerId < 0 ||
        (direct && ownerId == 0)))
      throw std::invalid_argument("Oriented range source ID must be a positive integer for a direct action");
    if (busy())
      throw std::invalid_argument("Wait for the current edit");
    const auto source = nativeSnapshot(p.contains("document_id") ? ownerId.get<std::uint64_t>() : 0);
    if (!direct) {
      impl->showAxisExtraction(source, AxisExtractionKind::OrientedRange);
      return;
    }
    if (!p.at("plane").is_string())
      throw std::invalid_argument("Plane must be XY, XZ or YZ");
    const auto &first = p.at("first"), &last = p.at("last"), &step = p.at("step");
    for (const auto *value : {&first, &last, &step})
      if (!value->is_number_integer() || *value < 0 || *value > INT32_MAX)
        throw std::invalid_argument("First, Last and Step must be native integer indices");
    for (const char *flag : {"reverse_order", "flip_vertical", "transpose"})
      if (p.contains(flag) && !p.at(flag).is_boolean())
        throw std::invalid_argument("Orientation flags must be booleans");
    AxisRangeOrientationRequest request;
    request.range.plane = parse_plane(p.at("plane").get<std::string>());
    request.range.first = first.get<int>();
    request.range.last = last.get<int>();
    request.range.step = step.get<int>();
    request.range.t = source.display().t;
    request.range.c = source.display().c;
    request.reverse_order = p.value("reverse_order", false);
    request.flip_vertical = p.value("flip_vertical", false);
    request.transpose = p.value("transpose", false);
    resliceOrientedAxisRange(source, request);
    return;
  }
  if (name == "reslice_axis_range" || name == "show_axis_range_reslice") {
    if (!p.is_object())
      throw std::invalid_argument("Range reslice parameters must be an object");
    const bool direct = name == "reslice_axis_range";
    for (auto field = p.begin(); field != p.end(); ++field) {
      const auto &key = field.key();
      if (!(key == "document_id" || (direct &&
              (key == "plane" || key == "first" || key == "last" || key == "step"))))
        throw std::invalid_argument("Unknown range reslice parameter: " + key);
    }
    if (direct)
      for (const char *key : {"document_id", "plane", "first", "last", "step"})
        if (!p.contains(key))
          throw std::invalid_argument(std::string("Required range reslice parameter: ") + key);
    const auto &owner_id = get(p, "document_id");
    if (p.contains("document_id") &&
        (!owner_id.is_number_integer() || owner_id < 0 || (direct && owner_id == 0)))
      throw std::invalid_argument("Range source ID must be a positive integer for a direct action");
    if (busy())
      throw std::invalid_argument("Wait for the current edit");
    const auto source = nativeSnapshot(p.contains("document_id") ? owner_id.get<std::uint64_t>() : 0);
    if (!direct) {
      impl->showAxisExtraction(source, AxisExtractionKind::Range);
      return;
    }
    if (!p.at("plane").is_string())
      throw std::invalid_argument("Plane must be XY, XZ or YZ");
    const auto &first = p.at("first"), &last = p.at("last"), &step = p.at("step");
    for (const auto *value : {&first, &last, &step})
      if (!value->is_number_integer() || *value < 0 || *value > INT32_MAX)
        throw std::invalid_argument("First, Last and Step must be native integer indices");
    AxisRangeResliceRequest request;
    request.plane = parse_plane(p.at("plane").get<std::string>());
    request.first = first.get<int>();
    request.last = last.get<int>();
    request.step = step.get<int>();
    request.t = source.display().t;
    request.c = source.display().c;
    resliceAxisRange(source, request);
    return;
  }
  if (name == "export_axis_plane" || name == "show_axis_plane_export") {
    if (!p.is_object())
      throw std::invalid_argument("Plane export parameters must be an object");
    for (auto field = p.begin(); field != p.end(); ++field) {
      const auto &key = field.key();
      const bool allowed = key == "document_id" ||
          (name == "export_axis_plane" && (key == "plane" || key == "coordinate"));
      if (!allowed)
        throw std::invalid_argument("Unknown plane export parameter: " + key);
    }
    const auto &planeValue = get(p, "plane");
    if (!planeValue.is_null() && !planeValue.is_string())
      throw std::invalid_argument("Plane must be XY, XZ or YZ");
    const auto &ownerId = get(p, "document_id");
    if (!ownerId.is_null() && (!ownerId.is_number_integer() || ownerId < 0))
      throw std::invalid_argument("Document ID must be a nonnegative integer");
    auto source = nativeSnapshot(ownerId.is_null() ? 0 : ownerId.get<std::uint64_t>());
    if (busy())
      throw std::invalid_argument("Wait for the current edit");
    const auto coordinate_for = [](Plane plane, const DisplayState &display) {
      return display.crosshair[plane == Plane::XY ? 2 : plane == Plane::XZ ? 1 : 0];
    };
    if (name == "show_axis_plane_export") {
      impl->showAxisExtraction(source, AxisExtractionKind::Plane);
      return;
    }
    AxisPlaneExportRequest request;
    request.plane = parse_plane(string(get(p, "plane"), "xy"));
    request.coordinate = coordinate_for(request.plane, source.display());
    const auto &coordinate = get(p, "coordinate");
    if (!coordinate.is_null()) {
      if (!coordinate.is_number_integer() || coordinate < 0 || coordinate > INT32_MAX)
        throw std::invalid_argument("Plane coordinate must be a nonnegative integer");
      request.coordinate = coordinate.get<int>();
    }
    request.t = source.display().t;
    request.c = source.display().c;
    exportAxisPlane(source, request);
    return;
  }
  if (name == "wait_jobs") {
    const auto &requested = get(p, "timeout_ms");
    if (!requested.is_null() && (!requested.is_number_integer() || requested < 1 || requested > 60000))
      throw std::invalid_argument("Job wait timeout must be an integer in 1..60000 ms");
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(requested.is_null() ? 5000 : requested.get<int>());
    auto remaining = [&] {
      return std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
    };
    if (impl->legacyBusy() && !impl->job.wait_for(remaining()))
      throw std::runtime_error("Image job wait timed out");
    if (const auto preview = impl->gaussianJobs.preview_job(); preview && !preview->wait_for(remaining()))
      throw std::runtime_error("Gaussian preview wait timed out");
    if (const auto apply = impl->gaussianJobs.apply_job(); apply && !apply->wait_for(remaining()))
      throw std::runtime_error("Gaussian Apply wait timed out");
    if (impl->thresholdJob && !impl->thresholdJob->wait_for(remaining()))
      throw std::runtime_error("Threshold Apply wait timed out");
    impl->pollContrastApply();
    if(impl->contrastApplyJob && !impl->contrastApplyJob->wait_for(remaining()))throw std::runtime_error("Contrast Apply wait timed out");
    impl->pollHistogram();
    if(impl->histogramTask && !impl->histogramTask->job.wait_for(remaining()))throw std::runtime_error("Histogram wait timed out");
    for(;;){
      impl->tickContrast();if(!impl->contrastPanel.pending())break;
      if(remaining().count()<=0 || !impl->contrastPanel.wait_for(remaining()))throw std::runtime_error("Contrast preview/histogram wait timed out");
      poll();
    }
    if (impl->autoJob && !impl->autoJob->wait_for(remaining()))
      throw std::runtime_error("Auto contrast wait timed out");
#ifdef IMAGEJXX_HAS_SCIENTIFIC_IO
    if(!impl->scientificIoPanel.wait_for(remaining()))throw std::runtime_error("Scientific IO wait timed out");
#endif
    if(!impl->indexedColorPanel.wait_for(remaining()))throw std::runtime_error("Indexed conversion wait timed out");
    if(!impl->colorStackPanel.wait_for(remaining()))throw std::runtime_error("Component conversion wait timed out");
    if(!impl->rgbDatasetPanel.wait_for(remaining()))throw std::runtime_error("RGB conversion wait timed out");
    if(!impl->nativeWorkflowPanel.wait_for(remaining()))throw std::runtime_error("Native workflow wait timed out");
    if (!impl->roiImportPanel.wait_for(remaining()))
      throw std::runtime_error("ROI import wait timed out");
    if (!impl->roiExportPanel.wait_for(remaining()))
      throw std::runtime_error("ROI export wait timed out");
    poll();
    return;
  }
  if (name == "close_document") {
    const auto &id = get(p, "document_id");
    if (!id.is_null() && (!id.is_number_integer() || id < 0))
      throw std::invalid_argument("Document ID must be a nonnegative integer");
    closeDocument(id.is_null() ? 0 : id.get<std::uint64_t>(), boolean(get(p, "discard")));
    return;
  }
  if (name == "edit_selection") {
    impl->cancelSavedRoiGeometry("tool_changed");
    impl->cancelCreations("tool_changed");
    impl->tool = "edit_roi";
    if (!documents_.empty() && document()->view().orthogonal)
      document()->setView({{"orthogonal", false}});
    for (auto &pane : impl->panes) {
      pane.points = Json::array();
      pane.dragging = false;
      pane.selectionGesture.reset();
    }
    return;
  }
  if (name == "show_selection_properties") {
    impl->selectionPanel.show(nativeSnapshot());
    return;
  }
  if (name == "selection_properties_draft") {
    impl->selectionPanel.working_limit(impl->selectionCreationLimits().max_working_bytes);
    impl->selectionPanel.patch(p);
    return;
  }
  if (name == "cancel_selection_properties") {
    impl->selectionPanel.cancel();
    return;
  }
  if (name == "apply_selection_properties") {
    if (busy())
      throw std::invalid_argument(
          "Wait for processing before applying selection properties");
    if (!impl->selectionPanel.matches(nativeSnapshot())) {
      impl->selectionPanel.cancel();
      throw RevisionConflict();
    }
    impl->selectionPanel.working_limit(impl->selectionCreationLimits().max_working_bytes);
    impl->selectionPanel.apply(
        [&](const Selection &value, const DocumentSnapshot &source) {
          if (source.data().selection && value == *source.data().selection)
            return;
          if(impl->selectionPanel.path_edited())impl->publishAuthoredPath(source,value);
          else {auto edit=source.begin_edit(Change::Data);edit.select(value);commitEdit(edit);}
        });
    return;
  }
  if (name == "show_jobs") {
    impl->jobsPanel.show(boolean(get(p, "visible"), true));
    return;
  }
  if (name == "fit_selection") {
    auto d = documentById(get(p, "document_id").is_null()
                              ? 0
                              : get(p, "document_id").get<std::uint64_t>());
    auto pane = std::find_if(impl->panes.begin(), impl->panes.end(),
                             [&](const auto &value) { return value.doc == d; });
    if (pane == impl->panes.end())
      throw std::out_of_range("Document has no image canvas");
    std::array<double, 2> extent{pane->extent.x, pane->extent.y};
    if (d->view().orthogonal) {
      const auto info = pane->orthogonal.diagnostics();
      extent = {number(info[0]["extent"][0]), number(info[0]["extent"][1])};
    }
    if (!get(p, "extent").is_null()) {
      const auto &value = get(p, "extent");
      if (!value.is_array() || value.size() != 2)
        throw std::invalid_argument(
            "Canvas extent must contain width and height");
      extent = {number(value[0]), number(value[1])};
    }
    d->fitSelection(extent);
    return;
  }
  if (name == "view") {
    auto d = documentById(get(p, "document_id").is_null()
                              ? 0
                              : get(p, "document_id").get<std::uint64_t>());
    d->setView(p);
    return;
  }
  if (busy() && name != "display_range" && name != "lut" && name != "load_lut" &&
      name != "show_preferences")
    throw std::invalid_argument("Wait for the current edit");
  if (name == "combine_rois") {
    if (!p.is_object())
      throw std::invalid_argument("ROI combination parameters must be an object");
    for (const auto &field : p.items())
      if (field.key() != "indices" && field.key() != "operation" &&
          field.key() != "document_id" && field.key() != "metadata_revision" && field.key() != "geometry")
        throw std::invalid_argument("Unknown ROI combination parameter: " +
                                    field.key());
    if (p.contains("operation") && !p["operation"].is_string())
      throw std::invalid_argument("ROI combination operation must be text");
    const auto operation = string(get(p, "operation"), "union");
    imagejxx::SelectionBooleanOperation kind;
    if (operation == "union")
      kind = imagejxx::SelectionBooleanOperation::Union;
    else if (operation == "intersection")
      kind = imagejxx::SelectionBooleanOperation::Intersection;
    else if (operation == "subtraction")
      kind = imagejxx::SelectionBooleanOperation::Subtraction;
    else if (operation == "xor")
      kind = imagejxx::SelectionBooleanOperation::Xor;
    else
      throw std::invalid_argument("Unknown ROI combination operation: " +
                                  operation);
    std::uint64_t id = 0;
    if (p.contains("document_id")) {
      const auto &value = p["document_id"];
      if (!value.is_number_integer() || value <= 0)
        throw std::invalid_argument("ROI combination document ID must be positive");
      id = value.get<std::uint64_t>();
    }
    const auto target = documentById(id);
    const auto source = target->nativeSnapshot();
    if (p.contains("metadata_revision")) {
      const auto &value = p["metadata_revision"];
      if (!value.is_number_integer() || value < 0)
        throw std::invalid_argument("ROI collection revision must be nonnegative");
      if (value.get<std::uint64_t>() != source.revisions().metadata)
        throw RevisionConflict();
    }
    const auto &requested = get(p, "indices");
    if (!requested.is_array() || requested.size() < 2)
      throw std::invalid_argument("Select at least two distinct saved area ROIs");
    std::vector<std::size_t> indices;
    std::set<std::size_t> distinct;
    indices.reserve(requested.size());
    for (const auto &value : requested) {
      if (!value.is_number_integer() || value < 0)
        throw std::invalid_argument("ROI indices must be nonnegative integers");
      const auto index = value.get<std::uint64_t>();
      if (index > std::numeric_limits<std::size_t>::max() ||
          index >= source.data().rois.size())
        throw std::out_of_range("ROI combination index is outside the manager");
      const auto checked = static_cast<std::size_t>(index);
      if (!distinct.insert(checked).second)
        throw std::invalid_argument("ROI combination indices must be distinct");
      if (!selection_is_area(source.data().rois[checked].selection))
        throw std::invalid_argument("ROI combinations require area ROIs");
      indices.push_back(checked);
    }
    bool full=false;for(auto index:indices)full=full || source.data().rois[index].selection.is_region();
    if(p.contains("geometry")) {
      if(!p["geometry"].is_string() || (p["geometry"]!="canvas_v1" && p["geometry"]!="full_region_v2"))throw std::invalid_argument("Choose canvas_v1 or full_region_v2 Boolean geometry");
      full=p["geometry"]=="full_region_v2";
    }
    const auto admitted=impl->selectionCreationLimits();
    const SelectionEditLimits limits{admitted.max_pixels,admitted.max_candidate_vertices,admitted.max_working_bytes};
    auto edit = full?imagejxx::prepare_roi_region_boolean(source,indices,kind,limits):imagejxx::prepare_roi_boolean(source, indices, kind);
    if (busy())
      throw std::invalid_argument("Wait for the current edit");
    // The retained adapter and transaction target the captured native owner;
    // later active-tab/display changes cannot redirect publication.
    if(target->scientific())controller_->commit(edit);else target->commitNative(edit);
    return;
  }
  if (name == "new") {
    int w = integer(get(p, "width"), 512), h = integer(get(p, "height"), 512),
        z = integer(get(p, "slices"), 1), c = integer(get(p, "channels"), 1);
    if (z <= 0 || double(w) * h * z * c > 1024ULL * 1024 * 1024 / 8)
      throw std::invalid_argument("New image exceeds size limit");
    Stack frames;
    for (int i = 0; i < z; ++i)
      frames.push_back(canonical(
          fill(Image(w, h, c, pixelType(string(get(p, "dtype"), "uint8"))),
               number(get(p, "value")))));
    auto d = std::make_shared<Document>(std::move(frames),
                                        string(get(p, "title"), "Untitled"));
    d->checkpoint();
    addDocument(d);
    return;
  }
  if (name == "open_sequence") {
    Stack frames;
    for (const auto &path : get(p, "paths")) {
      auto images = readImages(string(path));
      frames.insert(frames.end(), images.begin(), images.end());
    }
    addDocument(std::make_shared<Document>(
        std::move(frames), string(get(p, "title"), "Sequence")));
    return;
  }
  if (name == "raw") {
    if (integer(get(p, "offset")) < 0)
      throw std::invalid_argument("Raw offset must be nonnegative");
    addDocument(std::make_shared<Document>(
        readRaw(string(get(p, "path")), integer(get(p, "width")),
                integer(get(p, "height")), integer(get(p, "channels"), 1),
                integer(get(p, "slices"), 1),
                pixelType(string(get(p, "dtype"), "uint16")),
                size_t(std::max(0, integer(get(p, "offset")))),
                boolean(get(p, "big_endian"))),
        filename(string(get(p, "path")))));
    return;
  }
  if (name == "preferences") {
    updateSettings(p);
    return;
  }
  if (name == "show_preferences") {
    impl->settingsPanel.show(settings(), string(get(p, "search")));
    return;
  }
  if (name == "despeckle_imagej") {
    if (!p.is_object() || (p.size() && !(p.size() == 1 && p.contains("document_id"))))
      throw std::invalid_argument("Despeckle accepts only its active document ID");
    if (busy()) throw std::invalid_argument("Wait for the current edit");
    impl->gaussianPanel.cancel([&] { impl->cancelGaussianPreview(); });
    impl->rankPanel.cancel([&] { impl->cancelGaussianPreview(); });
    impl->cancelGaussianPreview();
    impl->rankPanel.show_despeckle(nativeSnapshot());
    impl->gaussianReadbackRequested = true;
    return;
  }
  if (name == "show_shadow_demo") {
    if (impl->dialog.open) throw std::invalid_argument("Close the current form before starting Shadows Demo");
    impl->gaussianPanel.cancel([this]{impl->cancelGaussianPreview();});
    impl->rankPanel.cancel([this]{impl->cancelGaussianPreview();});
    impl->cancelGaussianPreview();
    impl->shadowDemo.start(nativeSnapshot());impl->shadowDemoVisible=true;impl->animating=false;return;
  }
  if (name == "show_operation") {
    const auto id = string(get(p, "operation"));
    const auto &commands = impl->commands;
    auto found = std::find_if(
        commands.begin(), commands.end(),
        [&](const Json &command) { return get(command, "id") == id; });
    if (found == commands.end())
      throw std::invalid_argument("Unknown operation: " + id);
    impl->operation(*found, get(p, "parameters").is_null()
                                ? Json::object()
                                : get(p, "parameters"));
    return;
  }
  if (name == "reset_preferences") {
    auto proposed = settings();
    proposed.reset(string(get(p, "group")));
    auto nextPanel = impl->settingsPanel;
    nextPanel.show(proposed);
    updateSettings(settingsJson(proposed));
    static_assert(std::is_nothrow_swappable_v<SettingsPanel>);
    using std::swap;
    swap(impl->settingsPanel, nextPanel);
    return;
  }
  if (name == "import_preferences") {
    auto imported = PreferencesState::load(string(get(p, "path")), false);
    imported.writeBlocked = impl->preferenceState.writeBlocked;
    auto nextPanel = impl->settingsPanel;
    nextPanel.show(imported.settings);
    impl->applyPreferences(std::move(imported), true);
    static_assert(std::is_nothrow_swappable_v<SettingsPanel>);
    using std::swap;
    swap(impl->settingsPanel, nextPanel);
    return;
  }
  if (name == "export_preferences") {
    auto exported = impl->preferenceState;
    exported.settings = settings();
    exported.extras = impl->preferences;
    exported.writeBlocked = false;
    exported.save(string(get(p, "path")), true);
    return;
  }
  if (name == "save_workspace") {
    auto path = string(get(p, "path"));
    const auto presentation = with_workspace_presentation(
        metadataObjectFromJson(impl->sessionExtensions),
        documents_.empty() ? std::optional<std::size_t>{}
                           : std::optional<std::size_t>(activeIndex()),
        documents_.size());
    auto extensions = metadataJson(presentation.extensions);
    auto diagnostics = impl->startupDiagnostics;
    diagnostics.insert(diagnostics.end(), presentation.diagnostics.begin(),
                       presentation.diagnostics.end());
    saveNativeSession(path, documents_, results, workflow, extensions);
    impl->sessionExtensions.swap(extensions);
    impl->startupDiagnostics.swap(diagnostics);
    impl->startupNoticesDismissed = false;
    impl->rememberSession(path, documents_.size());
    impl->recent(path);
    return;
  }
  if (name == "save_results") {
    auto current = settings();
    auto columns = resultColumns(results, current);
    auto escape = [](std::string s) {
      std::string out = "\"";
      for (char c : s) {
        if (c == '\"')
          out += '\"';
        out += c;
      }
      return out + '\"';
    };
    std::ostringstream data;
    for (size_t k = 0; k < columns.size(); ++k) {
      if (k)
        data << ',';
      data << escape(columns[k]);
    }
    data << '\n';
    for (const auto &r : results) {
      for (size_t k = 0; k < columns.size(); ++k) {
        if (k)
          data << ',';
        const auto &v = get(r, columns[k].c_str());
        if (!v.is_null())
          data << escape(resultText(v, current));
      }
      data << '\n';
    }
    auto text = data.str();
    writeBytes(string(get(p, "path")), {text.begin(), text.end()});
    return;
  }
  if (name == "save_workflow") {
    if(workflow.is_array()&&workflow.size()==1&&workflow[0].value("schema",std::string{})=="imagejxx.native-workflow-program"){
      auto cap=std::size_t(settings().integer("processing_memory_mb"))*1048576;if(!cap)cap=256*1048576;
      auto text=native_workflow_program_from_record(metadataObjectFromJson(workflow[0]),desktop_workflow_program_limits(cap)).json();
      writeBytes(string(get(p,"path")),{text.begin(),text.end()});return;
    }
    writeJson(
        string(get(p, "path")),
        {{"format", "imagejxx-workflow"}, {"version", 1}, {"steps", workflow}});
    return;
  }
  if (name == "load_workflow") {
    auto cap=std::size_t(settings().integer("processing_memory_mb"))*1048576;if(!cap)cap=256*1048576;
    const auto bytes=readBytes(string(get(p,"path")),desktop_workflow_program_limits(cap).max_record_bytes);
    auto root=detail::bounded_native_workflow_json(std::string(bytes.begin(),bytes.end()),desktop_workflow_program_limits(cap));
    if(root.value("schema",std::string{})=="imagejxx.native-workflow-program"){
      auto program=native_workflow_program_from_record(metadataObjectFromJson(root),desktop_workflow_program_limits(cap));
      workflow=Json::array({metadataJson(program.record())});return;
    }
    if (get(root, "format") != "imagejxx-workflow" ||
        get(root, "version") != 1 || !get(root, "steps").is_array())
      throw std::invalid_argument("Invalid workflow file");
    workflow = get(root, "steps");
    return;
  }
  if (name == "paste_new") {
    if (!impl->clipboard && impl->gl) {
      impl->clipboard = pasteSystemImage();
      impl->clipboardSamplePolicy = FloatingSamplePolicy::FiniteOnly;
    }
    if (!impl->clipboard)
      throw std::invalid_argument("Copy pixels first");
    addDocument(
        std::make_shared<Document>(Stack{*impl->clipboard}, "Clipboard",
                                   Json::object(), impl->clipboardSamplePolicy));
    return;
  }
  if(name=="native_batch" || ((name=="run_workflow"||name=="batch") && (document()->scientific() || needs_native_workflow_review(workflow)))){
    if(impl->workflowOtherBusy())throw std::invalid_argument("Wait for the current edit");
    const bool batch=name!="run_workflow";
    if(!p.is_object())throw std::invalid_argument("Workflow review parameters must be an object");
    for(const auto &field:p.items())if(!batch||(field.key()!="paths"&&field.key()!="output_directory"))throw std::invalid_argument("Choose processing targets in workflow review");
    impl->roiImportPanel.cancel("workflow_review");impl->roiExportPanel.cancel("workflow_review");impl->displayAnnotationPanel.cancel("workflow_review");
    impl->planarAnnotationRebind.cancel("workflow_review");impl->selectionPanel.cancel();impl->cancelSavedRoiGeometry("workflow_review");impl->cancelCreations("workflow_review");
    auto cap=std::size_t(settings().integer("processing_memory_mb"))*1048576;if(!cap)cap=256*1048576;
    impl->nativeWorkflowPanel.show(nativeSnapshot(),workflow,cap,recording,batch);
    if(batch)impl->nativeWorkflowPanel.seed_batch(p.value("paths",Json::array()),p.value("output_directory",std::string{}));return;
  }
  auto d = document();
  State edited = d->state();
  auto &meta = edited.metadata;
  auto publishDisplay = [&] {
    auto display = d->view();
    for (auto key : {"display_range", "lut", "custom_lut"})
      if (meta.contains(key))
        display.properties[key] = metadataFromJson(meta[key]);
      else
        display.properties.erase(key);
    d->setNativeDisplay(std::move(display));
  };
  // Display configuration validates metadata and processor options without
  // asking an immutable provider for image pixels. The one-pixel carrier is
  // local and bounded; actual source validity remains the renderer's job.
  auto validateDisplayMapping = [&](const Json &properties) {
    const auto source=d->nativeSnapshot();
    const auto &data=source.data().dataset;
    const auto samples=data.shape().s;
    Image carrier(1,1,samples==4?4:samples>=3?3:1,data.pixel_type());
    (void)displayImage(carrier,properties,data.sample_policy());
  };
  if (name == "display_range") {
    if (boolean(get(p, "auto"))) {
      if (d->samplePolicy()==FloatingSamplePolicy::PreserveIEEEFloat32) {
        meta.erase("display_range"); publishDisplay(); return;
      }
      const auto bounds = finiteDisplayRange(d->frame(), d->samplePolicy(), false);
      meta["display_range"] = {bounds[0], bounds[1]};
      publishDisplay();
      return;
    }
    double lower = number(get(p, "minimum")),
           upper = number(get(p, "maximum"), type_max(d->nativeSnapshot().data().dataset.pixel_type()));
    if (lower > upper || (lower==upper && d->samplePolicy()!=FloatingSamplePolicy::PreserveIEEEFloat32))
      throw std::invalid_argument("Maximum must exceed minimum");
    if (d->samplePolicy()==FloatingSamplePolicy::PreserveIEEEFloat32) {
      auto proposed=meta; proposed["display_range"]={lower,upper};
      validateDisplayMapping(proposed);
    }
    meta["display_range"] = {lower, upper};
    publishDisplay();
    return;
  }
  if (name == "lut") {
    auto lutName = string(get(p, "name"), "Grays");
    auto proposed = meta;
    proposed["lut"] = lutName;
    proposed.erase("custom_lut");
    validateDisplayMapping(proposed);
    meta["lut"] = lutName;
    meta.erase("custom_lut");
    publishDisplay();
    return;
  }
  if (name == "load_lut") {
    auto root = readJson(string(get(p, "path")));
    const auto &lut = get(root, "lut");
    if (!lut.is_array() || lut.size() != 256)
      throw std::invalid_argument("LUT needs 256 RGB rows");
    auto check = meta;
    check["custom_lut"] = lut;
    validateDisplayMapping(check);
    meta["custom_lut"] = lut;
    publishDisplay();
    return;
  }
  if (name == "save_lut") {
    Image ramp(256, 1, 1, PixelType::UInt8);
    for (int x = 0; x < 256; ++x)
      ramp.at(x, 0) = x;
    auto settings = meta;
    settings.erase("display_range");
    settings["labels"] = false;
    auto lut = displayImage(ramp, settings, FloatingSamplePolicy::FiniteOnly);
    writeJson(string(get(p, "path")),
              {{"format", "imagejxx-lut"}, {"lut", imageArray(lut)[0]}});
    return;
  }
  const auto &frame = d->frame();
  auto selected = get(meta, "selection");
  if (name == "undo") {
    impl->savedRoiHistoryStep(documentById(targetId), false);
    return;
  }
  if (name == "redo") {
    impl->savedRoiHistoryStep(documentById(targetId), true);
    return;
  }
  if (name == "save_document") {
    impl->saveDocument(d, string(get(p, "path")));
    return;
  }
  if (name == "save_pixels") {
    writeImages(string(get(p, "path")), d->state().frames.materialize());
    return;
  }
  if (name == "export_display") {
    writeImages(string(get(p, "path")), {renderImage(frame, meta, true, d->samplePolicy())});
    return;
  }
  if (name == "export_pdf") {
    writePdf(
        string(get(p, "path")), renderImage(frame, meta, true, d->samplePolicy()),
        number(get(p, "dpi"),
               impl->preferenceState.settings.number("print_dpi")),
        boolean(get(p, "landscape"),
                impl->preferenceState.settings.boolean("print_landscape")));
    return;
  }
  if (name == "print") {
    printImage(renderImage(frame, meta, true, d->samplePolicy()));
    return;
  }
  if (name == "select_all") {
    d->select(selection("rectangle", {{0, 0}, {frame.width, frame.height}}));
    return;
  }
  if (name == "select_none") {
    d->select(Json::object());
    return;
  }
  if (name == "wand") {
    auto mask = wand_mask(
        gray(frame), integer(get(p, "x")), integer(get(p, "y")),
        number(get(p, "tolerance"),
               impl->preferenceState.settings.number("wand_tolerance")),
        integer(get(p, "connectivity"),
                static_cast<int>(impl->preferenceState.settings.integer(
                    "wand_connectivity"))));
    d->select(maskSelection(mask));
    return;
  }
  if (name == "restore_selection") {
    d->select(get(meta, "previous_selection"));
    return;
  }
  if (name == "invert_selection" || name == "grow_selection" ||
      name == "shrink_selection") {
    auto mask =
        selected.empty()
            ? fill(Image(frame.width, frame.height, 1, PixelType::UInt8), 255)
            : selectionMask(selected, frame.width, frame.height);
    mask = name == "invert_selection"
               ? point(mask, "invert")
               : morphology(mask, name == "grow_selection" ? "dilate" : "erode",
                            integer(get(p, "radius"), 1), true,
                            Border::Constant, 0);
    d->select(maskSelection(mask));
    return;
  }
  if (name == "rotate_selection") {
    if (selected.empty())
      throw std::invalid_argument("Draw a selection first");
    if (!get(p, "degrees").is_number())
      throw std::invalid_argument("Selection rotation needs numeric degrees");
    SelectionEditor editor(selectionFromJson(selected), frame.width,
                           frame.height);
    std::optional<std::array<double, 2>> pivot;
    if (!get(p, "pivot").is_null()) {
      const auto &v = get(p, "pivot");
      if (!v.is_array() || v.size() != 2 || !v[0].is_number() ||
          !v[1].is_number())
        throw std::invalid_argument("Rotation pivot needs X/Y");
      pivot = v.get<std::array<double, 2>>();
    }
    const auto result = editor.rotated(number(get(p, "degrees")), pivot);
    if (result != editor.selection())
      d->select(selectionJson(result));
    return;
  }
  if (name == "move_selection" || name == "resize_selection" ||
      name == "move_selection_vertex" || name == "insert_selection_vertex" ||
      name == "delete_selection_vertex") {
    if (selected.empty())
      throw std::invalid_argument("Draw a selection first");
    SelectionEditor editor(selectionFromJson(selected), frame.width,
                           frame.height);
    Selection result = editor.selection();
    const auto index = [&] {
      const auto &value = get(p, "index");
      if (!value.is_number_integer() ||
          (!value.is_number_unsigned() && value.get<std::int64_t>() < 0))
        throw std::invalid_argument("Selection index must be nonnegative");
      return value.get<std::size_t>();
    };
    if (name != "move_selection" && name != "delete_selection_vertex" &&
        (!get(p, "x").is_number() || !get(p, "y").is_number()))
      throw std::invalid_argument(
          "Selection edit needs numeric X/Y coordinates");
    for (const auto &option : {"keep_inside", "preserve_aspect", "from_center"})
      if (!get(p, option).is_null() && !get(p, option).is_boolean())
        throw std::invalid_argument("Selection edit flags must be boolean");
    const std::array<double, 2> position{
        {number(get(p, "x")), number(get(p, "y"))}};
    if (name == "move_selection")
      result = editor.moved({number(get(p, "dx")), number(get(p, "dy"))},
                            boolean(get(p, "keep_inside")));
    else if (name == "resize_selection")
      result =
          editor.resized(index(), position, boolean(get(p, "preserve_aspect")),
                         boolean(get(p, "from_center")));
    else if (name == "move_selection_vertex")
      result = editor.moved_vertex(index(), position);
    else if (name == "insert_selection_vertex")
      result = editor.inserted_vertex(index(), position);
    else
      result = editor.removed_vertex(index());
    if (result != editor.selection())
      d->select(selectionJson(result));
    return;
  }
  if (name == "selection_mask") {
    if (selected.empty())
      throw std::invalid_argument("Draw a selection first");
    addDocument(std::make_shared<Document>(
        Stack{selectionMask(selected, frame.width, frame.height)}, "ROI mask"));
    return;
  }
  if (name == "copy" || name == "cut") {
    auto r = selected.empty()
                 ? Rect{0, 0, frame.width, frame.height}
                 : selectionBounds(selected, frame.width, frame.height);
    impl->clipboard = crop(frame, r.x, r.y, r.width, r.height);
    impl->clipboardSamplePolicy = d->samplePolicy();
    impl->clipboardMask =
        selected.empty()
            ? fill(Image(r.width, r.height, 1, PixelType::UInt8), 255)
            : crop(selectionMask(selected, frame.width, frame.height), r.x, r.y,
                   r.width, r.height);
    if (impl->gl) {
      if (impl->clipboardSamplePolicy != FloatingSamplePolicy::FiniteOnly)
        copySystemImage(displayImage(*impl->clipboard, meta, impl->clipboardSamplePolicy));
      else
        copySystemImage(*impl->clipboard);
    }
    if (name == "cut")
      applyOperation("fill_color", {{"color", impl->paintColor(false)}}, false);
    return;
  }
  if (name == "paste") {
    if (!impl->clipboard && impl->gl) {
      impl->clipboard = pasteSystemImage();
      impl->clipboardSamplePolicy = FloatingSamplePolicy::FiniteOnly;
      impl->clipboardMask.reset();
    }
    if (!impl->clipboard)
      throw std::invalid_argument("Copy pixels first");
    int x = integer(get(p, "x")), y = integer(get(p, "y"));
    if (!selected.empty()) {
      auto r = selectionBounds(selected, frame.width, frame.height);
      x = r.x;
      y = r.y;
    }
    State proposed = d->state();
    proposed.frames.replace(
        d->slice(),
        canonical(
            paste(frame, canonical(convert(*impl->clipboard, frame.type)), x, y,
                  impl->clipboardMask ? &*impl->clipboardMask : nullptr)));
    d->commit(std::move(proposed));
    return;
  }
  if (name == "fill" || name == "clear") {
    applyOperation("fill_color", {{"color", impl->paintColor(name == "fill")}},
                   impl->allSlices);
    return;
  }
  if (name == "crop_selection") {
    if (selected.empty())
      throw std::invalid_argument("Draw a selection first");
    auto r = selectionBounds(selected, frame.width, frame.height);
    State proposed = d->state();
    for (std::size_t k = 0; k < proposed.frames.size(); ++k)
      proposed.frames.replace(
          k, crop(proposed.frames[k], r.x, r.y, r.width, r.height));
    proposed.metadata["selection"] = nullptr;
    proposed.metadata["rois"] = Json::array();
    proposed.metadata["overlays"] = Json::array();
    proposed.spectrum.reset();
    d->commit(std::move(proposed));
    return;
  }
  if (name == "duplicate") {
    auto snapshot = d->nativeSnapshot();
    auto data = snapshot.data();
    data.metadata.title = d->title() + " copy";
    auto out = std::make_shared<Document>(std::make_shared<imagejxx::Document>(
        std::move(data), snapshot.display()));
    out->checkpoint();
    addDocument(out);
    return;
  }
  if (name == "calibration") {
    auto values = get(meta, "calibration");
    values.update(p);
    for (auto key :
         {"pixel_width", "pixel_height", "pixel_depth", "frame_interval"})
      if (number(get(values, key)) <= 0)
        throw std::invalid_argument("Calibration values must be positive");
    calibrationFromJson(values);
    meta["calibration"] = values;
    d->commit(std::move(edited));
    return;
  }
  if (name == "set_scale") {
    const auto &points = get(selected, "points");
    if (points.size() != 2)
      throw std::invalid_argument("Draw a line with two points");
    double pixels = std::hypot(number(points[1][0]) - number(points[0][0]),
                               number(points[1][1]) - number(points[0][1]));
    double distance = number(get(p, "known_distance"), 1);
    if (pixels <= 0 || distance <= 0)
      throw std::invalid_argument("Distance must be positive");
    action("calibration", {{"pixel_width", distance / pixels},
                           {"pixel_height", distance / pixels},
                           {"unit", string(get(p, "unit"), "um")}});
    return;
  }
  if (name == "add_roi") {
    if (selected.empty())
      throw std::invalid_argument("Draw a selection first");
    auto &rois = meta["rois"];
    rois.push_back(
        {{"name",
          string(get(p, "name"), "ROI " + std::to_string(rois.size() + 1))},
         {"selection", selected},
         {"slice_index",
          boolean(get(p, "all_slices")) ? Json() : Json(d->slice())}});
    d->commit(std::move(edited));
    impl->focusRoiManagerRow(d->nativeSnapshot().data().rois.size() - 1, true);
    return;
  }
  if (name == "select_roi" || name == "update_roi" || name == "rename_roi" ||
      name == "delete_roi") {
    impl->synchronizeRoiManager();
    int index = integer(get(p, "index"), impl->roiIndex);
    auto &rois = meta["rois"];
    if (index < 0 || index >= int(rois.size()))
      throw std::invalid_argument("Select a saved ROI");
    if (name == "select_roi") {
      auto roi = rois[index];
      if (!get(roi, "slice_index").is_null()) {
        auto crosshair = d->view().crosshair;
        crosshair[2] = integer(get(roi, "slice_index"));
        d->setView({{"crosshair", crosshair}});
      }
      d->select(get(roi, "selection"));
      impl->focusRoiManagerRow(static_cast<std::size_t>(index), true);
    } else {
      if (name == "update_roi" && selected.empty())
        throw std::invalid_argument("Draw a selection first");
      if (name == "delete_roi") {
        rois.erase(rois.begin() + index);
        impl->roiIndex = -1;
      } else if (name == "rename_roi")
        rois[index]["name"] = string(get(p, "name"));
      else
        rois[index]["selection"] = selected;
      d->commit(std::move(edited));
      // Metadata-changing single-row updates invalidate prior multi-selection
      // and range anchors, while retaining the explicitly updated focused row.
      if (name == "delete_roi")
        impl->invalidateRoiManager();
      else
        impl->focusRoiManagerRow(static_cast<std::size_t>(index), false);
    }
    return;
  }
  if (name == "save_rois") {
    writeJson(string(get(p, "path")), {{"format", "imagejxx-rois"},
                                       {"version", 1},
                                       {"rois", get(meta, "rois")}});
    return;
  }
  if (name == "load_rois") {
    auto root = readJson(string(get(p, "path")));
    const auto &rois = get(root, "rois");
    if (get(root, "format") != "imagejxx-rois" || get(root, "version") != 1 ||
        !rois.is_array())
      throw std::invalid_argument("Invalid ROI file");
    for (const auto &roi : rois)
      selectionMask(get(roi, "selection"), frame.width, frame.height);
    meta["rois"] = rois;
    d->commit(std::move(edited));
    impl->invalidateRoiManager();
    return;
  }
  if (name == "measure") {
    results.push_back(d->measure());
    return;
  }
  if (name == "measure_rois") {
    for (const auto &roi : get(meta, "rois")) {
      int index = integer(get(roi, "slice_index"), d->slice());
      auto row = d->measure(get(roi, "selection"), index);
      row["ROI"] = get(roi, "name");
      results.push_back(row);
    }
    return;
  }
  if (name == "add_overlay") {
    Json o = selected;
    if (o.empty())
      throw std::invalid_argument("Draw a selection first");
    if (!o.contains(roi_style_attribute) || !o.contains("color"))
      o["color"] = string(get(p, "color"), impl->paintColor());
    o["slice_index"] =
        boolean(get(p, "all_slices")) ? Json() : Json(d->slice());
    meta["overlays"].push_back(o);
    d->commit(std::move(edited));
    return;
  }
  if (name == "text") {
    auto o = selection("text", {{number(get(p, "x")), number(get(p, "y"))}});
    o["text"] = string(get(p, "text"), "Label");
    o["color"] = string(get(p, "color"), impl->paintColor());
    color(string(o["color"]));
    int size = integer(
        get(p, "font_size"),
        static_cast<int>(impl->preferenceState.settings.integer("text_size")));
    if (size < 1 || size > 512)
      throw std::invalid_argument("Font size must be 1..512");
    o["font_size"] = size;
    if (!get(p, "font_path").is_null())
      o["font_path"] = string(get(p, "font_path"));
    o["slice_index"] =
        boolean(get(p, "all_slices")) ? Json() : Json(d->slice());
    // Append to the typed collection without reinterpreting legacy field
    // presence or changing the source display and opaque metadata.
    const auto source = d->nativeSnapshot();
    auto overlays = source.data().overlays;
    overlays.push_back(metadataObjectFromJson(o));
    auto edit = source.begin_edit(Change::Metadata);
    edit.set_overlays(std::move(overlays));
    d->commitNative(edit);
    return;
  }
  if (name == "clear_overlays") {
    meta["overlays"] = Json::array();
    d->commit(std::move(edited));
    return;
  }
  if (name == "flatten") {
    addDocument(std::make_shared<Document>(Stack{renderImage(frame, meta, true, d->samplePolicy())},
                                           d->title() + " rendered"));
    return;
  }
  if (name == "split_channels") {
    std::vector<Stack> channels(frame.channels);
    for (const auto &source : d->state().frames) {
      auto planes = split_channels(source);
      for (size_t c = 0; c < planes.size(); ++c)
        channels[c].push_back(std::move(planes[c]));
    }
    for (size_t c = 0; c < channels.size(); ++c) {
      auto out = std::make_shared<Document>(
          std::move(channels[c]),
          d->title() + " channel " + std::to_string(c + 1),
          Json{{"calibration", get(meta, "calibration")},
               {"slice_index", d->slice()}});
      addDocument(out);
    }
    return;
  }
  if (name == "merge_channels") {
    Stack planes;
    for (const auto &source : documents_)
      if (source->frame().channels == 1)
        planes.push_back(source->frame());
    if (planes.size() != 3 && planes.size() != 4)
      throw std::invalid_argument(
          "Open exactly three or four grayscale documents");
    addDocument(std::make_shared<Document>(Stack{merge_channels(planes)},
                                           "Merged channels"));
    return;
  }
  if (name == "images_to_stack") {
    Stack frames;
    for (const auto &source : documents_) {
      require_same_shape(frame, source->frame());
      frames.push_back(canonical(convert(source->frame(), frame.type)));
    }
    auto out = std::make_shared<Document>(
        frames, "Stack", Json{{"calibration", get(meta, "calibration")}});
    addDocument(out);
    return;
  }
  if (name == "stack_to_images") {
    auto frames = d->state().frames;
    for (size_t i = 0; i < frames.size(); ++i) {
      auto out = std::make_shared<Document>(
          frames.subset({i}), d->title() + " " + std::to_string(i + 1),
          Json{{"calibration", get(meta, "calibration")}});
      addDocument(out);
    }
    return;
  }
  if (name == "add_slice" || name == "delete_slice") {
    State proposed = d->state();
    auto &frames = proposed.frames;
    if (name == "add_slice")
      frames.insert_shared(d->slice() + 1, frames, d->slice());
    else if (name == "delete_slice") {
      if (frames.size() == 1)
        throw std::invalid_argument("Cannot delete the only slice");
      frames.erase(d->slice());
    }
    proposed.metadata["is_stack"] = frames.size() > 1;
    proposed.metadata["slice_index"] =
        std::min(d->slice(), int(frames.size()) - 1);
    proposed.metadata["rois"] = Json::array();
    proposed.metadata["overlays"] = Json::array();
    d->commit(std::move(proposed));
    return;
  }
  if (name == "show_z_projection" || name == "show_grouped_z_projection") {
    if (busy()) throw std::invalid_argument("Wait for the current edit");
    if (!p.is_object() || !p.empty())
      throw std::invalid_argument("Choose projection parameters in the dialog");
    const bool grouped = name == "show_grouped_z_projection";
    if (grouped && d->shape().z < 2)
      throw std::invalid_argument("Grouped Z projection requires a stack");
    const auto captured = d->nativeSnapshot();
    impl->stopAnimation("z_projection");
    Json values{{"method", grouped ? "mean" : "maximum"}, {"first", 1}, {"last", d->shape().z}};
    Json schema = Json::array({
      {{"name", "first"}, {"label", "Start slice"}, {"kind", "int"}},
      {{"name", "last"}, {"label", "Stop slice"}, {"kind", "int"}},
      {{"name", "method"}, {"label", "Projection method"}, {"kind", "text"},
       {"choices", {"maximum", "minimum", "mean", "sum", "median", "stddev"}}}
    });
    if (grouped) {
      values["group_size"] = d->shape().z;
      schema.insert(schema.begin() + 2,
          Json{{"name", "group_size"}, {"label", "Group size"}, {"kind", "int"}});
    }
    impl->ask(grouped ? "Grouped Z Project" : "Z Project", std::move(values),
        [this, captured, grouped](Json values) {
          const auto current = nativeSnapshot();
          if (!captured.same_document(current) ||
              !captured.revisions().matches(current.revisions(), Change::All))
            throw RevisionConflict();
          action(grouped ? "project_groups" : "project", values);
        }, std::move(schema), grouped ? "grouped_z_projection" : "z_projection");
    return;
  }
  if (name == "project" || name == "project_groups") {
    const bool grouped = name == "project_groups";
    if (grouped && d->shape().z < 2)
      throw std::invalid_argument("Grouped Z projection requires a stack");
    if (!p.is_object()) throw std::invalid_argument("Projection parameters must be an object");
    for (const auto &field : p.items())
      if (field.key() != "method" && field.key() != "first" && field.key() != "last" &&
          !(grouped && field.key() == "group_size"))
        throw std::invalid_argument("Unknown projection parameter: " + field.key());
    const auto depth = d->shape().z;
    if (grouped) for (const auto* key : {"first", "last"})
      if (p.contains(key) && !p.at(key).is_number_integer())
        throw std::invalid_argument("Projection slice bounds must be integers");
    const auto first = p.contains("first") ? integer(p.at("first")) : 1;
    const auto last = p.contains("last") ? integer(p.at("last")) : depth;
    if (first < 1 || first > last || last > depth)
      throw std::invalid_argument("Projection requires 1 <= Start slice <= Stop slice <= stack depth");
    int group = 0;
    auto calibration = d->spatialCalibration();
    if (grouped) {
      if (!p.contains("group_size") || !p.at("group_size").is_number_integer())
        throw std::invalid_argument("Projection group size must be an integer");
      group = integer(p.at("group_size"));
      if (group < 1 || group > last - first + 1 || (last - first + 1) % group)
        throw std::invalid_argument("Projection group size must divide the selected slice count exactly");
      if (first != 1) calibration.origin = calibration.voxel_to_world({0, 0, double(first - 1)});
      calibration.spacing[2] *= group;
      calibration.validate();
    }
    std::size_t workspace = 0;
    if (grouped) {
      const auto maximum = std::numeric_limits<std::size_t>::max();
      const auto multiply = [maximum](std::size_t a, std::size_t b) {
        if (b && a > maximum / b) throw std::invalid_argument("Projection workspace exceeds addressable size");
        return a * b;
      };
      const auto groups = std::size_t((last - first + 1) / group);
      const auto output = multiply(multiply(frame.pixels.size(), sizeof(double)), groups);
      const auto scratch = multiply(std::size_t(group), sizeof(double));
      const auto objects = multiply(groups, sizeof(Image));
      if (scratch > maximum - output || objects > maximum - output - scratch)
        throw std::invalid_argument("Projection workspace exceeds addressable size");
      workspace = output + scratch + objects;
    }
    auto source = d->state();
    auto method = string(get(p, "method"), grouped ? "mean" : "maximum");
    impl->start(
        [source = std::move(source), method, first, last, group, calibration] {
          State out;
          const auto stack = source.frames.materialize();
          auto projected = group ? project_groups(stack, method, group, first - 1, last - 1)
                                 : Stack{project(stack, method, first - 1, last - 1)};
          for (auto& image : projected) image = canonical(std::move(image));
          out.frames = std::move(projected);
          out.metadata = source.metadata;
          out.metadata["is_stack"] = out.frames.size() > 1;
          out.metadata["slice_index"] = 0;
          out.metadata["rois"] = Json::array();
          out.metadata["overlays"] = Json::array();
          if (group) {
            out.metadata["calibration"]["pixel_depth"] = calibration.spacing[2];
            if (first != 1) out.metadata["calibration"]["origin"] = calibration.origin;
          }
          return out;
        },
        d, true, d->title() + (grouped ? " grouped " : " ") + method, "", workspace);
    return;
  }
  if (name == "gaussian3d") {
    auto source = d->state();
    auto mask = selected.empty()
                    ? std::optional<Image>{}
                    : selectionMask(selected, frame.width, frame.height);
    double sx = number(get(p, "sigma_x"), 1), sy = number(get(p, "sigma_y"), 1),
           sz = number(get(p, "sigma_z"), 1);
    impl->start(
        [source = std::move(source), mask = std::move(mask), sx, sy,
         sz]() mutable {
          auto filtered = gaussian_blur3d(source.frames.materialize(), sx, sy,
                                          sz, Border::Reflect);
          for (size_t z = 0; z < filtered.size(); ++z) {
            filtered[z] = canonical(std::move(filtered[z]));
            if (mask)
              filtered[z] = paste(source.frames[z], filtered[z], 0, 0, &*mask);
          }
          source.frames = std::move(filtered);
          return source;
        },
        d, false, "", "Gaussian blur 3D");
    return;
  }
  if (name == "reslice") {
    Stack frames;
    bool across = string(get(p, "axis"), "y") == "y";
    int size = across ? frame.height : frame.width;
    for (int k = 0; k < size; ++k) {
      Image out(across ? frame.width : frame.height,
                int(d->state().frames.size()), frame.channels, frame.type);
      for (int z = 0; z < out.height; ++z)
        for (int i = 0; i < out.width; ++i)
          for (int c = 0; c < out.channels; ++c)
            out.at(i, z, c) =
                d->state().frames[z].at(across ? i : k, across ? k : i, c);
      frames.push_back(std::move(out));
    }
    auto cal = get(meta, "calibration");
    double step = number(get(cal, across ? "pixel_height" : "pixel_width"), 1);
    if (!across)
      cal["pixel_width"] = get(cal, "pixel_height");
    cal["pixel_height"] = get(cal, "pixel_depth");
    cal["pixel_depth"] = step;
    auto out = std::make_shared<Document>(
        std::move(frames), d->title() + " reslice", Json{{"calibration", cal}});
    addDocument(out);
    return;
  }
  if (name == "montage") {
    int columns = integer(get(p, "columns"),
                          int(std::ceil(std::sqrt(d->state().frames.size()))));
    if (columns <= 0)
      throw std::invalid_argument("Columns must be positive");
    int rows = (int(d->state().frames.size()) + columns - 1) / columns;
    Image out(frame.width * columns, frame.height * rows, frame.channels,
              frame.type);
    for (size_t i = 0; i < d->state().frames.size(); ++i)
      out = paste(out, d->state().frames[i], int(i % columns) * frame.width,
                  int(i / columns) * frame.height);
    addDocument(std::make_shared<Document>(Stack{std::move(out)},
                                           d->title() + " montage"));
    return;
  }
  if (name == "fft") {
    impl->start(
        [source = d->snapshot()] {
          State out = source.state();
          auto spectrum = fft2(gray(source.frame()), true);
          out.frames = Stack{power_spectrum(spectrum, true, true)};
          out.spectrum = FourierState(
              std::move(spectrum),
              std::array<int, 2>{source.frame().height, source.frame().width});
          out.metadata["is_stack"] = false;
          out.metadata["slice_index"] = 0;
          out.metadata["selection"] = nullptr;
          out.metadata["rois"] = Json::array();
          out.metadata["overlays"] = Json::array();
          out.metadata.erase("display_range");
          out.metadata.erase("spectrum_shape");
          return out;
        },
        d, true, d->title() + " FFT");
    return;
  }
  if (name == "inverse_fft") {
    if (!d->state().spectrum)
      throw std::invalid_argument(
          "This document has no retained complex spectrum");
    const auto &fft = d->state().spectrum->spectrum();
    auto shape = d->state().spectrum->original_shape();
    auto out = ifft2(fft);
    if (shape)
      out = crop(out, 0, 0, (*shape)[1], (*shape)[0]);
    addDocument(
        std::make_shared<Document>(Stack{out}, d->title() + " inverse"));
    return;
  }
  if (name == "watershed") {
    int index = integer(get(p, "markers_document"));
    if (index < 0 || index >= int(documents_.size()))
      throw std::invalid_argument("Marker document is out of range");
    documents_[index]->requireLegacyEditable();
    auto markersImage = gray(documents_[index]->frame());
    require_same_shape(gray(frame), markersImage);
    Labels markers{frame.width, frame.height, 0, {}};
    for (double value : markersImage.pixels) {
      if (value < 0 || value != std::floor(value) || value > 2147483647)
        throw std::invalid_argument("Markers must be nonnegative integers");
      markers.pixels.push_back(int(value));
      markers.count = std::max(markers.count, int(value));
    }
    auto source = gray(frame);
    auto mask = selected.empty()
                    ? std::optional<Image>{}
                    : selectionMask(selected, frame.width, frame.height);
    impl->start(
        [source = std::move(source), markers = std::move(markers),
         mask = std::move(mask)] {
          State out;
          out.frames = Stack{labelsImage(
              watershed(source, markers, mask ? &*mask : nullptr, 8))};
          out.metadata = {{"labels", true}};
          return out;
        },
        d, true, "Watershed");
    return;
  }
  if (name == "histogram") {
    auto image = gray(frame);
    if (!selected.empty()) {
      auto mask = selectionMask(selected, frame.width, frame.height);
      std::vector<double> values;
      for (size_t i = 0; i < image.pixels.size(); ++i)
        if (mask.pixels[i])
          values.push_back(image.pixels[i]);
      if (values.empty())
        throw std::invalid_argument("Empty selection");
      image = Image(int(values.size()), 1, 1, image.type);
      image.pixels = std::move(values);
    }
    auto h = histogram(image, integer(get(p, "bins"), 256));
    impl->plotX.clear();
    impl->plotY.clear();
    for (size_t i = 0; i < h.counts.size(); ++i) {
      impl->plotX.push_back(float((h.edges[i] + h.edges[i + 1]) / 2));
      impl->plotY.push_back(float(h.counts[i]));
    }
    impl->plotTitle = "Histogram";
    return;
  }
  if (name == "profile") {
    const auto &points = get(selected, "points");
    if (points.size() < 2)
      throw std::invalid_argument("Draw a line or polyline");
    impl->plotX.clear();
    impl->plotY.clear();
    double distance = 0;
    auto cal = get(meta, "calibration");
    for (size_t i = 1; i < points.size(); ++i) {
      double x1 = number(points[i - 1][0]), y1 = number(points[i - 1][1]),
             x2 = number(points[i][0]), y2 = number(points[i][1]);
      int samples =
          std::max(2, int(std::ceil(std::hypot(x2 - x1, y2 - y1))) + 1);
      if (impl->preferenceState.settings.integer("profile_samples") != 0)
        samples = static_cast<int>(
            impl->preferenceState.settings.integer("profile_samples"));
      auto profile = line_profile(gray(frame), x1, y1, x2, y2, samples);
      double length =
          std::hypot((x2 - x1) * number(get(cal, "pixel_width"), 1),
                     (y2 - y1) * number(get(cal, "pixel_height"), 1));
      for (size_t k = i == 1 ? 0 : 1; k < profile.size(); ++k) {
        impl->plotX.push_back(float(distance + length * k / (samples - 1)));
        impl->plotY.push_back(float(profile[k]));
      }
      distance += length;
    }
    impl->plotTitle = "Line profile (" + string(get(cal, "unit"), "px") + ")";
    return;
  }
  if (name == "particles") {
    auto source = gray(frame);
    if (!selected.empty()) {
      auto mask = selectionMask(selected, frame.width, frame.height);
      source = paste(fill(source, 0), source, 0, 0, &mask);
    }
    auto labels =
        connected_components(source, integer(get(p, "connectivity"), 8));
    auto cal = get(meta, "calibration");
    auto particles =
        region_properties(labels, &source, number(get(cal, "pixel_width"), 1),
                          number(get(cal, "pixel_height"), 1));
    double minimum = number(get(p, "min_area")),
           maximum = number(get(p, "max_area"),
                            std::numeric_limits<double>::infinity());
    for (const auto &v : particles)
      if (v.area >= minimum && v.area <= maximum)
        results.push_back({{"Image", d->title()},
                           {"Slice", d->slice() + 1},
                           {"label", v.label},
                           {"pixels", v.pixels},
                           {"area", v.area},
                           {"bbox", {v.x, v.y, v.width, v.height}},
                           {"centroid", {v.centroid_y, v.centroid_x}},
                           {"perimeter", v.perimeter},
                           {"mean", v.mean},
                           {"min", v.minimum},
                           {"max", v.maximum},
                           {"integrated_density", v.integrated_density},
                           {"circularity", v.circularity},
                           {"eccentricity", v.eccentricity},
                           {"touches_border", v.touches_border}});
    return;
  }
  if (name == "run_workflow") {
    auto steps = workflow;
    if (std::any_of(steps.begin(), steps.end(), [](const Json &step) {
          return step.is_object() && step.contains("schema_version") &&
                 (get(step, "operation") == "rank_filter_imagej" || (get(step, "operation") == "remove_outliers_imagej" || get(step, "operation") == "top_hat_imagej" || get(step, "operation") == "remove_nans_imagej"));
        }))
      validateGaussianReplayCalibration(steps, d->nativeSnapshot(), true);
    if (p.contains("use_destination_calibration")) {
      if (!p["use_destination_calibration"].is_boolean())
        throw std::invalid_argument("Calibration choice must be boolean");
      validateGaussianReplayCalibration(steps, d->nativeSnapshot(),
                                         boolean(p["use_destination_calibration"]));
    }
    const auto rebind = p.contains("gaussian_rebind")
        ? std::optional<TargetRequest>(gaussianRebindRequest(p["gaussian_rebind"])) : std::nullopt;
    const auto rankRebind = p.contains("rank_rebind")
        ? std::optional<TargetRequest>(gaussianRebindRequest(p["rank_rebind"])) : std::nullopt;
    std::shared_ptr<WorkflowReplayLineage> lineage;
    if (impl->headlessDiagnostics) {
      impl->workflowReplayObservationFailed = false;
      try {
        Json request = p.contains("gaussian_rebind") ? p["gaussian_rebind"]
                       : p.contains("rank_rebind") ? p["rank_rebind"] : Json(nullptr);
        if (rebind && rankRebind)
          request = {{"gaussian_rebind", p["gaussian_rebind"]},
                     {"rank_rebind", p["rank_rebind"]}};
        lineage = std::make_shared<WorkflowReplayLineage>(d->id, steps, std::move(request));
      } catch (...) {
        impl->workflowReplayObservationFailed = true;
      }
    }
    impl->start(
        [this, source = d->snapshot(), steps = std::move(steps), rebind, rankRebind, lineage] {
          GaussianReplayObserver observer;
          if (lineage) {
            try {
              observer = [lineage](const GaussianReplayObservation &value) noexcept {
                lineage->observe(value);
              };
            } catch (...) {
              lineage->observationFailed.store(true, std::memory_order_relaxed);
            }
          }
          return runWorkflow(source, steps,
                             [this] { return impl->cancelled.load(); }, rebind,
                             std::move(observer), rankRebind);
        },
        d, false, "", "Workflow replay", 0, lineage);
    return;
  }
  if (name == "batch") {
    auto inputs = get(p, "paths");
    auto directory =
        std::filesystem::u8path(string(get(p, "output_directory")));
    if (!inputs.is_array() || inputs.empty() || workflow.empty())
      throw std::invalid_argument(
          "Choose inputs and record or load a workflow");
    if (std::any_of(workflow.begin(), workflow.end(), [](const Json &step) {
          return step.is_object() && step.contains("schema_version") &&
                 (get(step, "operation") == "rank_filter_imagej" || (get(step, "operation") == "remove_outliers_imagej" || get(step, "operation") == "top_hat_imagej" || get(step, "operation") == "remove_nans_imagej"));
        }))
      validateGaussianReplayCalibration(workflow, d->nativeSnapshot(), true);
    validate_legacy_batch_workflow(workflow);
    std::filesystem::create_directories(directory);
    auto steps = workflow;
    std::optional<bool> calibrationConsent;
    if (p.contains("use_destination_calibration")) {
      if (!p["use_destination_calibration"].is_boolean())
        throw std::invalid_argument("Calibration choice must be boolean");
      calibrationConsent = boolean(p["use_destination_calibration"]);
    }
    const auto rebind = p.contains("gaussian_rebind")
        ? std::optional<TargetRequest>(gaussianRebindRequest(p["gaussian_rebind"])) : std::nullopt;
    const auto rankRebind = p.contains("rank_rebind")
        ? std::optional<TargetRequest>(gaussianRebindRequest(p["rank_rebind"])) : std::nullopt;
    impl->start(
        [this, inputs, directory, steps, rebind, rankRebind, calibrationConsent] {
          for (const auto &input : inputs) {
            if (impl->cancelled)
              throw std::runtime_error("Batch cancelled");
            auto path = string(input);
            Document source(readImages(path), filename(path));
            if (calibrationConsent)
              validateGaussianReplayCalibration(steps, source.nativeSnapshot(),
                                                 *calibrationConsent);
            auto proposed = runWorkflow(
                source, steps, [this] { return impl->cancelled.load(); }, rebind, {}, rankRebind);
            if (impl->cancelled)
              throw std::runtime_error("Batch cancelled");
            auto output = directory / (filename(path) + ".npy");
            if (std::filesystem::exists(output))
              throw std::invalid_argument("Batch output already exists: " +
                                          output.u8string());
            writeImages(output.u8string(), proposed.frames.materialize());
          }
          return State{};
        },
        d);
    impl->jobSkip = true;
    return;
  }
  if (name == "gaussian_replay") {
    const auto record = metadataObjectFromJson(get(p, "record"));
    const auto source = d->nativeSnapshot();
    const auto command = p.contains("gaussian_rebind")
        ? rebind_gaussian_command(source, record, gaussianRebindRequest(p["gaussian_rebind"]))
        : gaussian_command_from_record(source, record);
    impl->requestGaussian(command);
    return;
  }
  if (name == "rank_replay") {
    const auto record = metadataObjectFromJson(get(p, "record"));
    const auto source = d->nativeSnapshot();
    const auto command = p.contains("rank_rebind")
        ? rebind_rank_command(source, record, gaussianRebindRequest(p["rank_rebind"]))
        : rank_command_from_record(source, record);
    impl->requestRank(command);
    return;
  }
  throw std::invalid_argument("Unknown desktop action: " + name);
}
void Window::cancel() {
  impl->shadowDemo.request_cancel();
#ifdef IMAGEJXX_HAS_SCIENTIFIC_IO
  impl->scientificIoPanel.request_cancel();
#endif
  impl->rgbDatasetPanel.request_cancel();
  impl->colorStackPanel.request_cancel();
  impl->indexedColorPanel.request_cancel();
  impl->nativeWorkflowPanel.request_cancel();
  impl->contrastPanel.request_cancel(*impl->gaussianCancellationScheduler);
  impl->histogramCancelEpoch.fetch_add(1);
  if(const auto id=impl->histogramCancelId.load())impl->gaussianCancellationScheduler->cancel(id);
  impl->contrastApplyCancelEpoch.fetch_add(1);
  if(const auto id=impl->contrastApplyCancelId.load())impl->gaussianCancellationScheduler->cancel(id);
  impl->autoCancelEpoch.fetch_add(1);
  if (const auto id = impl->autoCancelId.load())
    impl->gaussianCancellationScheduler->cancel(id);
  impl->thresholdCancelEpoch.fetch_add(1);
  if (const auto id = impl->thresholdCancelId.load())
    impl->gaussianCancellationScheduler->cancel(id);
  Job legacy;
  std::array<std::uint64_t, 2> ids;
  {
    std::lock_guard<std::mutex> lock(impl->jobMutex);
    legacy = impl->job;
    ids = impl->gaussianCancelIds;
    ++impl->gaussianCancelEpoch;
  }
  if (!legacy.valid() || legacy.cancel())
    impl->cancelled = true;
  for (const auto id : ids)
    if (id)
      impl->gaussianCancellationScheduler->cancel(id);
}
std::optional<JobInfo> Window::jobInfo() const {
  checkThread();
  if(auto indexed=impl->indexedColorPanel.job_info())return indexed;
  if(auto color=impl->colorStackPanel.job_info())return color;
  if(auto rgb=impl->rgbDatasetPanel.job_info())return rgb;
#ifdef IMAGEJXX_HAS_SCIENTIFIC_IO
  if(auto io=impl->scientificIoPanel.job_info())return io;
#endif
  if(auto workflow=impl->nativeWorkflowPanel.job_info())return workflow;
  if (impl->histogramTask)return impl->histogramTask->job.info();
  if(impl->contrastApplyJob)return impl->contrastApplyJob->info();
  if (impl->autoJob) return impl->autoJob->info();
  if (impl->thresholdJob)
    return impl->thresholdJob->info();
  if (const auto apply = impl->gaussianJobs.apply_job())
    return apply->info();
  std::lock_guard<std::mutex> lock(impl->jobMutex);
  return impl->job.valid() ? std::optional<JobInfo>(impl->job.info())
                           : std::nullopt;
}
std::vector<JobInfo> Window::jobs() const {
  checkThread();
  return controller_->job_scheduler()->jobs();
}
bool Window::cancelJob(std::uint64_t jobId) {
  checkThread();
  Job legacy;
  {
    std::lock_guard<std::mutex> lock(impl->jobMutex);
    legacy = impl->job;
  }
  if (legacy.valid() && legacy.info().id == jobId) {
    const bool accepted = legacy.cancel();
    if (accepted)
      impl->cancelled = true;
    return accepted;
  }
  const bool accepted = controller_->job_scheduler()->cancel(jobId);
  if (accepted)
    impl->pollGaussian();
  if (accepted)
    impl->pollAuto();
  if(accepted)impl->pollHistogram();
  if(accepted)impl->pollContrastApply();
  if (accepted)
    impl->roiImportPanel.poll();
  if (accepted) impl->roiExportPanel.poll();
  return accepted;
}
std::shared_ptr<JobScheduler> HistogramAsyncUiProbeAccess::scheduler(Window &w){w.checkThread();return w.controller_->job_scheduler();}
Json HistogramAsyncUiProbeAccess::diagnostics(Window &w){w.checkThread();return {{"panel",w.impl->histogramPanel.diagnostics()},{"processing",w.impl->histogramDiagnostics()}};}
void MovieExportUiProbeAccess::worker_gate(Window &window,std::function<void()> gate){window.checkThread();window.impl->movieExportPanel.worker_gate(std::move(gate));}
void Window::poll() {
  checkThread();
  impl->pollContrastApply();
  impl->tickContrast();
  impl->movieExportPanel.poll(documents_.empty()?std::optional<DocumentSnapshot>{}:std::optional<DocumentSnapshot>{nativeSnapshot()},impl->rgbCap());
  impl->shadowDemo.poll(documents_.empty()?std::optional<DocumentSnapshot>{}:std::optional<DocumentSnapshot>{nativeSnapshot()}, impl->rgbCap());
  impl->pollRgb();
  impl->pollColorStack();
  impl->pollIndexedColor();
#ifdef IMAGEJXX_HAS_SCIENTIFIC_IO
  impl->pollScientificIo();
#endif
  impl->advanceLaunchAppend();
  impl->roiExportPanel.poll();
  impl->nativeWorkflowPanel.poll();
  impl->roiImportPanel.poll();
  impl->pollAuto();
  impl->pollHistogram();
  impl->pollThreshold();
  impl->pollGaussian();
  if (!impl->legacyBusy() || !impl->job.ready())
    return;
  const auto replayLineage = impl->workflowReplayLineage &&
      impl->workflowReplayLineage->associatedWith(impl->job)
          ? impl->workflowReplayLineage : nullptr;
  try {
    impl->job.rethrow();
    impl->job.complete([&] {
      auto &completed = impl->jobResult->value();
      if (completed.prepared) {
        if (impl->cancelled) throw OperationCancelled();
        publishPreparedProjection(completed.record, *completed.prepared, completed.recorded);
        impl->status = "Projection complete";
        return;
      }
      auto &proposed = completed.state;
      if (impl->cancelled) {
        impl->status = "Cancelled";
        return;
      }
      if (impl->jobDocument &&
          !impl->jobDocument->revisions().matches(impl->jobRevisions))
        throw std::runtime_error("Document changed while processing");
      if (impl->jobDocument &&
          impl->jobDocument->revisions().display != impl->jobRevisions.display)
        preserveDisplay(proposed.metadata, impl->jobDocument->state().metadata);
      const bool inPlace = !impl->jobPreview && !impl->jobSkip && !impl->jobNew;
      const ComponentCounts noCounts;
      OutputPublication outputs(
          results, workflow, inPlace ? completed.components : noCounts,
          inPlace && !completed.components.empty() ? impl->jobDocument->title()
                                                   : std::string(),
          std::move(impl->jobStep));
      std::string completedStatus = "Edit complete";
      if (impl->jobPreview) {
        impl->dialog.preview = proposed.frames[std::min(
            impl->jobSlice, int(proposed.frames.size()) - 1)];
      } else if (impl->jobSkip) {
      } else if (impl->jobNew) {
        proposed.metadata["title"] = impl->jobTitle;
        auto d = std::make_shared<Document>(std::move(proposed));
        d->checkpoint();
        addDocument(d);
      } else {
        impl->jobDocument->commit(std::move(proposed));
        if (replayLineage)
          replayLineage->published(impl->jobDocument->id);
      }
      outputs.publish();
      impl->status.swap(completedStatus);
    });
  } catch (const OperationCancelled &) {
    impl->status = "Cancelled";
  } catch (const std::exception &e) {
    impl->lastError = e.what();
    impl->status = "Processing failed";
  }
  if (replayLineage)
    replayLineage->terminal(impl->job);
  {
    std::lock_guard<std::mutex> lock(impl->jobMutex);
    impl->job = {};
    impl->jobResult.reset();
  }
  impl->jobDocument.reset();
}
void Window::wait() {
  checkThread();
  impl->movieExportPanel.wait();
  impl->shadowDemo.wait();
#ifdef IMAGEJXX_HAS_SCIENTIFIC_IO
  impl->scientificIoPanel.wait();
#endif
  impl->rgbDatasetPanel.wait();
  impl->colorStackPanel.wait();
  impl->indexedColorPanel.wait();
  impl->nativeWorkflowPanel.wait();
  impl->roiImportPanel.wait();
  impl->roiExportPanel.wait();
  if (impl->autoJob) impl->autoJob->wait();
  if(impl->contrastApplyJob)impl->contrastApplyJob->wait();
  if (impl->histogramTask) impl->histogramTask->job.wait();
  if (impl->thresholdJob)
    impl->thresholdJob->wait();
  if (impl->legacyBusy())
    impl->job.wait();
  if (const auto preview = impl->gaussianJobs.preview_job())
    preview->wait();
  if (const auto apply = impl->gaussianJobs.apply_job())
    apply->wait();
  poll();
}
void Window::applyGaussian(const TargetRequest &request,
                           const GaussianBlurCommandOptions &options,
                           std::uint64_t documentId) {
  checkThread();
  const auto source = documentById(documentId);
  if (!impl->gaussianSourceAllowed(*source) || busy())
    throw std::invalid_argument(
        "Gaussian requires an idle active UInt8 gray/RGB, UInt16 gray or Float32 gray document");
  // Resolve without touching the current lane. Invalid targets/options cannot
  // discard a valid installed preview or its resource reservation.
  const auto command = resolve_gaussian_command(source->nativeSnapshot(), request, options);
  impl->requestGaussian(command);
}
void Window::applyRank(const TargetRequest &request,
                       const RankFilterCommandOptions &options,
                       std::uint64_t documentId) {
  checkThread();
  const auto source = documentById(documentId);
  if (!impl->rankSourceAllowed(*source) || busy())
    throw std::invalid_argument(
        "Rank filtering requires an idle active finite UInt8, UInt16 or Float32 scalar document");
  const auto command = resolve_rank_command(source->nativeSnapshot(), request, options);
  impl->requestRank(command);
}
void Window::applyOperation(const std::string &id, const Json &params, bool all,
                            bool asynchronous, std::uint64_t documentId) {
  checkThread();
  if (busy())
    throw std::invalid_argument("Wait for the current edit");
  auto d = documentById(documentId);
  d->requireLegacyEditable();
  const auto &commands = impl->commands;
  auto found = std::find_if(commands.begin(), commands.end(),
                            [&](const Json &c) { return get(c, "id") == id; });
  if (found == commands.end())
    throw std::invalid_argument("Unknown operation");
  auto merged = impl->operationDefaults(*found, *d);
  if (id == "convert_image")
    all = true;
  if (id == "fill_color" || id == "paint_stroke")
    merged["color"] = impl->paintColor();
  if (id == "paint_stroke")
    merged["width"] = impl->brush;
  for (auto it = params.begin(); it != params.end(); ++it) {
    if (!merged.contains(it.key()))
      throw std::invalid_argument("Unknown parameter: " + it.key());
    merged[it.key()] = it.value();
  }
  auto step = Json{{"operation", id},
                   {"parameters", merged},
                   {"all_slices", all},
                   {"slice_index", d->slice()},
                   {"selection", get(d->state().metadata, "selection")}};
  // Both exposed execution modes obey the live host's finite workspace cap.
  // Only the asynchronous mode acquires a scheduler reservation.
  const auto source = d->nativeSnapshot();
  const auto memoryBudget = controller_->job_scheduler()->limits().memory_budget;
  const auto shape = source.data().dataset.shape();
  const OperationDefaultContext context{
      shape.x, shape.y, shape.s, source.data().dataset.pixel_type(), {}};
  const auto extra = extraWorkspace(id, context, merged);
  const auto workspace = legacy_operation_workspace(
      id, source, all, source.display().crosshair[2], extra, memoryBudget);
  if (asynchronous) {
    impl->start(
        [source = d->snapshot(), id, merged, all] {
          ComponentCounts counts;
          auto state = source.compute(id, merged, all, &counts);
          return Impl::JobResult(std::move(state), std::move(counts));
        },
        d, false, "", string(get(*found, "title")), workspace);
  } else {
    ComponentCounts counts;
    auto proposed = d->compute(id, merged, all, &counts);
    OutputPublication outputs(results, workflow, counts,
                              counts.empty() ? std::string() : d->title(),
                              recording ? std::move(step) : Json());
    d->commit(std::move(proposed));
    outputs.publish();
  }
  if (recording && asynchronous)
    impl->jobStep = std::move(step);
}
void Window::Impl::menu() {
  editMenuPoint = preferencesMenuPoint = {-1, -1};
  auto item = [&](const char *label, const char *action,
                  const char *shortcut = "", bool additionalEnabled = true) {
    const auto command = shortcutForAction(action);
    const bool enabled = additionalEnabled &&
        (command ? shortcutAvailable(command) : actionAllowed(action));
    const auto hint = command ? shortcutLabel(command) : shortcut;
    const bool selected = ImGui::MenuItem(label, hint, false, enabled);
    observeControl(std::string("menu.action.") + action, enabled, hint, command);
    if (selected)
      guard([&] { if (command) invokeShellCommand(command); else owner.action(action); });
  };
  auto form = [&](const char *label, const char *action, Json values) {
    const bool enabled=actionAllowed(action);
    const bool selected=ImGui::MenuItem(label,nullptr,false,enabled);
    observeControl(std::string("menu.form.")+action,enabled);
    if(selected)guard([&] {
      ask(label,std::move(values),[this,action](Json p){owner.action(action,p);});
    });
  };
  auto path = [&](const char *label, const char *action, bool save,
                  const char *suggested = "") {
    const auto command = shortcutForAction(action);
    const bool enabled = command ? shortcutAvailable(command) : actionAllowed(action);
    const auto hint = command ? shortcutLabel(command) : nullptr;
    if (ImGui::MenuItem(label, hint, false, enabled)) {
      if (command) guard([&] { invokeShellCommand(command); });
      else file(
          label, save,
          [this, action](std::string p) {
            owner.action(action, {{"path", p}});
          },
          suggested);
    }
    observeControl(std::string("menu.path.") + action, enabled, hint, command);
  };
  std::function<void(std::vector<std::string>)> catalogMenu =
      [&](std::vector<std::string> prefix) {
        if (prefix == std::vector<std::string>{"Process", "Noise"})
          item("Despeckle", "despeckle_imagej");
        if (prefix == std::vector<std::string>{"Process", "Shadows"}) {
          constexpr std::pair<const char *,const char *> directions[] = {
              {"north","North"},{"northeast","Northeast"},{"east","East"},{"southeast","Southeast"},
              {"south","South"},{"southwest","Southwest"},{"west","West"},{"northwest","Northwest"}};
          const bool enabled = !owner.documents_.empty() && !owner.busy() &&
                               actionAllowed("show_operation", 0, "shadow_imagej");
          for (const auto &direction : directions) {
            const bool selected = ImGui::MenuItem(direction.second, nullptr, false, enabled);
            observeControl(std::string("menu.shadow.") + direction.first, enabled);
            if (selected) guard([&] {
              const Json parameters{{"direction",direction.first}};
              const auto source = doc();
              if (source->scientific() || source->nativeSnapshot().data().dataset.shape().z > 1)
                owner.action("show_operation", {{"operation","shadow_imagej"},{"parameters",parameters}});
              else owner.applyOperation("shadow_imagej", parameters, false, true);
            });
          }
          const bool demoEnabled = !owner.documents_.empty() && actionAllowed("show_shadow_demo");
          if (ImGui::MenuItem("Demo", nullptr, false, demoEnabled)) guard([&]{owner.action("show_shadow_demo");});
          observeControl("menu.shadow.demo", demoEnabled);
          ImGui::Separator();
        }
        std::set<std::string> groups;
        for (const auto &c : commands) {
          const auto &menu = get(c, "menu");
          if (menu.size() < prefix.size())
            continue;
          bool match = true;
          for (size_t i = 0; i < prefix.size(); ++i)
            if (menu[i] != prefix[i])
              match = false;
          if (!match)
            continue;
          if (menu.size() == prefix.size()) {
            const auto command = shortcutForAction(string(get(c, "id")));
            const bool enabled = command ? shortcutAvailable(command) :
                !owner.documents_.empty() && actionAllowed(string(get(c, "id")));
            const auto hint = command ? shortcutLabel(command) : nullptr;
            const bool selected = ImGui::MenuItem(
                string(get(c, "title")).c_str(), hint, false, enabled);
            observeControl("menu.command." + string(get(c, "id")), enabled, hint, command);
            if (selected)
              guard([&] { if (command) invokeShellCommand(command); else operation(c); });
          } else
            groups.insert(string(menu[prefix.size()]));
        }
        for (const auto &group : groups) {
          const bool open = ImGui::BeginMenu(group.c_str());
          std::string key = "menu";
          for (const auto &part : prefix)
            key += "." + part;
          observeControl(key + "." + group);
          if (open) {
            auto next = prefix;
            next.push_back(group);
            catalogMenu(next);
            ImGui::EndMenu();
          }
        }
        if(prefix==std::vector<std::string>{"Image","Type"}) {
          const bool enabled=!owner.documents_.empty() && !owner.busy() && actionAllowed("show_rgb_dataset");
          const bool selected=ImGui::MenuItem("Convert dataset to RGB...",nullptr,false,enabled);
          observeControl("menu.rgb_dataset",enabled);
          if(selected)guard([&]{owner.action("show_rgb_dataset");});
          const bool components=ImGui::MenuItem("Color components and stack layout...",nullptr,false,enabled);
          observeControl("menu.color_stack",enabled);
          if(components)guard([&]{owner.action("show_color_stack");});
          const bool indexed=ImGui::MenuItem("Indexed color dataset...",nullptr,false,enabled);
          observeControl("menu.indexed_dataset",enabled);
          if(indexed)guard([&]{owner.action("show_indexed_dataset");});
          const bool expand=ImGui::MenuItem("Indexed palette to RGB...",nullptr,false,enabled);
          observeControl("menu.indexed_rgb",enabled);
          if(expand)guard([&]{owner.action("show_indexed_rgb");});
        }
      };
  if (!ImGui::BeginMainMenuBar())
    return;
  ImGui::BeginDisabled(owner.busy());
  const bool fileMenuOpen = ImGui::BeginMenu("File");
  observeControl("menu.File");
  if (fileMenuOpen) {
    form("New image...", "new",
         {{"title", "Untitled"},
          {"width", 512},
          {"height", 512},
          {"slices", 1},
          {"channels", 1},
          {"dtype", "uint8"},
          {"value", 0}});
    if (ImGui::MenuItem("Open...", shortcutLabel("file.open"), false,
                        shortcutAvailable("file.open")))
      guard([&] { invokeShellCommand("file.open"); });
    observeControl("menu.file.open", shortcutAvailable("file.open"),
                   shortcutLabel("file.open"), "file.open");
    if(ImGui::MenuItem("Export movie (AVI)...",nullptr,false,!owner.documents_.empty()))guard([&]{owner.action("show_movie_export");});
    observeControl("menu.file.movie_export",!owner.documents_.empty());
#ifdef IMAGEJXX_HAS_SCIENTIFIC_IO
    if(ImGui::MenuItem("Open scientific TIFF..."))guard([&]{owner.action("show_scientific_open");});
    observeControl("menu.file.scientific_open");
    if(ImGui::MenuItem("Export scientific TIFF...",nullptr,false,!owner.documents_.empty()))guard([&]{owner.action("show_scientific_export");});
    observeControl("menu.file.scientific_export",!owner.documents_.empty());
#endif
    if (ImGui::BeginMenu("Recent")) {
      for (const auto &p : get(preferences, "recent"))
        if (ImGui::MenuItem(string(p).c_str()))
          guard([&] { owner.openFile(string(p)); });
      ImGui::EndMenu();
    }
    form("Open sequence...", "open_sequence",
         {{"title", "Sequence"}, {"paths", Json::array()}});
    form("Import raw...", "raw",
         {{"path", ""},
          {"width", 512},
          {"height", 512},
          {"slices", 1},
          {"channels", 1},
          {"dtype", "uint16"},
          {"offset", 0},
          {"big_endian", false}});
    if(ImGui::MenuItem("Import ImageJ ROIs...",nullptr,false,!owner.documents_.empty() && !owner.busy()))
      guard([&]{owner.action("show_roi_import");});
    observeControl("menu.file.import_rois",!owner.documents_.empty() && !owner.busy());
    if(ImGui::MenuItem("Export paired ImageJ ROI...",nullptr,false,!owner.documents_.empty() && !owner.busy()))
      guard([&]{owner.action("show_roi_export");});
    observeControl("menu.file.export_roi",!owner.documents_.empty() && !owner.busy());
    if(ImGui::MenuItem("Export ImageJ ROI ZIP...",nullptr,false,!owner.documents_.empty() && !owner.busy()))
      guard([&]{owner.action("show_roi_zip_export");});
    observeControl("menu.file.export_roi_zip",!owner.documents_.empty() && !owner.busy());
    if(ImGui::MenuItem("Text and image annotations...",nullptr,false,!owner.documents_.empty() && !owner.busy()))
      guard([&]{owner.action("show_display_annotations");});
    observeControl("menu.file.display_annotations",!owner.documents_.empty() && !owner.busy());
    ImGui::Separator();
    ImGui::BeginDisabled(owner.documents_.empty());
    path("Save document...", "save_document", true, "document.ijx");
    ImGui::EndDisabled();
    path("Save workspace...", "save_workspace", true, "workspace.ijx");
    ImGui::BeginDisabled(owner.documents_.empty());
    path("Save pixels...", "save_pixels", true, "image.tif");
    path("Export display...", "export_display", true, "display.png");
    if (ImGui::MenuItem("PDF preview / export...", nullptr, false, actionAllowed("export_pdf")))
      guard([&] {
        ask("PDF preview / export",
            {{"path", "image.pdf"},
             {"dpi", preferenceState.settings.number("print_dpi")},
             {"landscape",
              preferenceState.settings.boolean("print_landscape")}},
            [this](Json p) { owner.action("export_pdf", p); });
        dialog.preview = renderImage(doc()->frame(), doc()->state().metadata, true, doc()->samplePolicy());
      });
#ifdef _WIN32
    if (ImGui::MenuItem("Print...", nullptr, false, actionAllowed("print")))
      guard([&] {
        ask("Print preview", Json::object(),
            [this](Json) { owner.action("print"); });
        dialog.preview = renderImage(doc()->frame(), doc()->state().metadata, true, doc()->samplePolicy());
      });
#endif
    const auto closeSourceId = owner.documents_.empty() ? 0 : doc()->id;
    const bool closeSelected = ImGui::MenuItem("Close image", shortcutLabel("file.close"),
                                              false, shortcutAvailable("file.close"));
    observeControl("menu.File.Close_image", shortcutAvailable("file.close"),
                   shortcutLabel("file.close"), "file.close");
    if (headlessDiagnostics)
      hostControls["menu.File.Close_image"]["document_id"] = closeSourceId;
    if (closeSelected)
      guard([&] { invokeShellCommand("file.close"); });
    ImGui::EndDisabled();
    ImGui::Separator();
    if (ImGui::MenuItem("Exit")) {
      closingApp = true;
    }
    ImGui::EndMenu();
  }
  ImGui::EndDisabled();
  const bool editMenuOpen = ImGui::BeginMenu("Edit");
  observeControl("menu.Edit");
  {
    const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    editMenuPoint = {(a.x + b.x) * .5f, (a.y + b.y) * .5f};
  }
  if (editMenuOpen) {
    ImGui::BeginDisabled(owner.busy() || owner.documents_.empty());
    bool undo = shortcutAvailable("edit.undo"), redo = shortcutAvailable("edit.redo");
    const bool undoSelected = ImGui::MenuItem("Undo", shortcutLabel("edit.undo"), false, undo);
    observeControl("menu.Edit.Undo", undo, shortcutLabel("edit.undo"), "edit.undo");
    if (undoSelected)
      guard([&] { invokeShellCommand("edit.undo"); });
    const bool redoSelected = ImGui::MenuItem("Redo", shortcutLabel("edit.redo"), false, redo);
    observeControl("menu.Edit.Redo", redo, shortcutLabel("edit.redo"), "edit.redo");
    if (redoSelected)
      guard([&] { invokeShellCommand("edit.redo"); });
    ImGui::Separator();
    item("Copy", "copy", "Ctrl+C");
    item("Cut", "cut", "Ctrl+X");
    item("Paste", "paste", "Ctrl+V");
    item("Paste as new image", "paste_new");
    item("Fill", "fill");
    item("Clear", "clear");
    const bool selectionMenuOpen = ImGui::BeginMenu("Selection");
    observeControl("menu.Edit.Selection");
    if (selectionMenuOpen) {
      item("Select all", "select_all", "Ctrl+A");
      item("Deselect", "select_none");
      item("Restore", "restore_selection");
      item("Invert", "invert_selection");
      for(bool shrink:{false,true}){
        const bool enabled=actionAllowed("enlarge_selection");
        const bool selected=ImGui::MenuItem(shrink?"Shrink...":"Expand...",nullptr,false,enabled);
        observeControl(shrink?"menu.form.shrink_selection":"menu.form.grow_selection",enabled);
        if(selected)guard([&]{owner.action("show_selection_enlargement",{{"shrink",shrink}});});
      }
      form("Move...", "move_selection", {{"dx", 0}, {"dy", 0}});
      item("Geometry and properties...", "show_selection_properties");
      item("Create mask", "selection_mask");
      item("Crop to selection", "crop_selection");
      ImGui::EndMenu();
    }
    catalogMenu({"Edit"});
    ImGui::EndDisabled();
    ImGui::Separator();
    item("Preferences...", "show_preferences");
    observeControl("menu.Edit.Preferences", true, nullptr, nullptr, ImGui::GetCurrentWindow());
    {
      const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
      preferencesMenuPoint = {(a.x + b.x) * .5f, (a.y + b.y) * .5f};
    }
    ImGui::EndMenu();
  }
  ImGui::BeginDisabled(owner.busy());
  ImGui::BeginDisabled(owner.documents_.empty());
  const bool imageMenuOpen = ImGui::BeginMenu("Image");
  observeControl("menu.Image");
  if (imageMenuOpen) {
    catalogMenu({"Image"});
    ImGui::Separator();
    item("Duplicate", "duplicate");
    item("Brightness/Contrast...","show_contrast");
    form("Display range...", "display_range",
         {{"minimum", 0.0},
          {"maximum",
           owner.documents_.empty() ? 255 : type_max(doc()->nativeSnapshot().data().dataset.pixel_type())},
          {"auto", false}});
    item("Channel display...", "show_channel_display");
    item("Auto contrast", "auto_contrast");
    if (ImGui::MenuItem("Reset display range"))
      guard([&] { owner.action("display_range", {{"auto", true}}); });
    const bool lookupTableOpen = ImGui::BeginMenu("Lookup table", actionAllowed("lut"));
    observeControl("menu.Image.Lookup_table");
    if (lookupTableOpen) {
      for (auto lut : {"Grays", "Inverted", "Red", "Green", "Blue", "Fire",
                       "Ice", "Spectrum"}) {
        if (ImGui::MenuItem(lut))
          guard([&] { owner.action("lut", {{"name", lut}}); });
        if (std::string(lut) == "Grays" || std::string(lut) == "Inverted")
          observeControl(std::string("menu.Image.Lookup_table.") + lut);
      }
      path("Import LUT...", "load_lut", false);
      path("Export LUT...", "save_lut", true, "colors.json");
      ImGui::EndMenu();
    }
    item("Split channels", "split_channels");
    item("Merge channels", "merge_channels");
    auto *stacksItemWindow = ImGui::GetCurrentWindow();
    const bool stacksMenuOpen = ImGui::BeginMenu("Stacks");
    observeControl("menu.Image.Stacks", true, nullptr, nullptr, stacksItemWindow);
    if (stacksMenuOpen) {
      item("Images to stack", "images_to_stack");
      item("Stack to images", "stack_to_images");
      item("Insert slice", "add_slice");
      item("Delete slice", "delete_slice");
      if (!owner.documents_.empty()) {
        bool orthogonal = doc()->view().orthogonal;
        if (ImGui::MenuItem("Orthogonal XY/XZ/YZ views", shortcutLabel("view.orthogonal"),
                            orthogonal, shortcutAvailable("view.orthogonal")))
          guard([&] { invokeShellCommand("view.orthogonal"); });
        observeControl("menu.action.view.orthogonal", shortcutAvailable("view.orthogonal"),
                       shortcutLabel("view.orthogonal"), "view.orthogonal", ImGui::GetCurrentWindow());
      }
      item("Substack...", "show_z_subset");
      observeControl("menu.z_subset");
      item("Z Project...", "show_z_projection");
      item("ImageJ Z Project...", "show_imagej_projection");
      item("ImageJ Grouped Z Project...", "show_imagej_grouped_projection");
      item("Grouped Z Project...", "show_grouped_z_projection", "", doc()->shape().z > 1);
      if (ImGui::BeginMenu("Projection", actionAllowed("project"))) {
        for (auto method :
             {"maximum", "minimum", "mean", "sum", "median", "stddev"})
          if (ImGui::MenuItem(method))
            guard([&] { owner.action("project", {{"method", method}}); });
        ImGui::EndMenu();
      }
      form("3D Gaussian blur...", "gaussian3d",
           {{"sigma_x", 1.0}, {"sigma_y", 1.0}, {"sigma_z", 1.0}});
      item("Export plane...", "show_axis_plane_export");
      item("Reslice range...", "show_axis_range_reslice");
      item("Reslice oriented range...", "show_axis_range_orientation");
      item("Reslice physical spacing...", "show_axis_range_spacing");
      item("Reslice with ImageJ bilinear...", "show_axis_range_slicer_resize");
      form("Reslice...", "reslice", {{"axis", "y"}});
      form("Montage...", "montage", {{"columns", 4}});
      const auto animationShape = doc()->shape();
      bool canAnimate = false;
      if (!owner.busy()) try {
        canAnimate = PlaybackSequence(animationExtent(animationShape,animationAxis),animationFirst,
                                      animationLast,animationStep,animationMode).count()>1;
      } catch (const std::invalid_argument &) {}
      item("Animation options...", "show_animation_options");
      const bool animationAxisOpen = ImGui::BeginMenu("Animation axis");
      observeControl("menu.animation.axis");
      if (animationAxisOpen) {
        for (const auto *axis : {"z", "c", "t"}) {
          const bool available = animationExtent(animationShape, axis) > 1 && !owner.busy();
          const char *label = *axis == 'z' ? "Z slices" : *axis == 'c' ? "Channels" : "Time frames";
          if (ImGui::MenuItem(label, nullptr, animationAxis == axis, available))
            guard([&] { owner.action("animation", {{"axis", axis}}); });
          observeControl(std::string("menu.animation.axis.") + axis, available);
        }
        ImGui::EndMenu();
      }
      if (ImGui::MenuItem("Animate", nullptr, animating, canAnimate || animating))
        guard([&] { owner.action("animation", {{"playing", !animating}}); });
      observeControl("menu.action.animation", canAnimate || animating);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Overlays")) {
      item("Add selection", "add_overlay");
      form("Add text...", "text",
           {{"x", 10},
            {"y", 10},
            {"text", "Label"},
            {"font_size", preferenceState.settings.integer("text_size")},
            {"color", paintColor()},
            {"all_slices", false}});
      item("Clear overlays", "clear_overlays");
      item("Flatten to new image", "flatten");
      ImGui::EndMenu();
    }
    ImGui::EndMenu();
  }
  ImGui::EndDisabled();
  const bool processMenuOpen = ImGui::BeginMenu("Process");
  observeControl("menu.Process");
  if (processMenuOpen) {
    item("Threshold display...", "show_manual_threshold");
    observeControl("menu.manual_threshold");
    catalogMenu({"Process"});
    ImGui::Separator();
    ImGui::BeginDisabled(owner.documents_.empty());
    item("Forward FFT", "fft");
    item("Inverse FFT", "inverse_fft");
    form("Marker watershed...", "watershed", {{"markers_document", 0}});
    ImGui::EndDisabled();
    ImGui::EndMenu();
  }
  ImGui::BeginDisabled(owner.documents_.empty());
  const bool analyzeMenuOpen = ImGui::BeginMenu("Analyze");
  observeControl("menu.Analyze");
  if (analyzeMenuOpen) {
    item("Measure", "measure", "M");
    item("Measure ROIs", "measure_rois");
    if(actionAllowed("show_histogram"))item("Histogram...", "show_histogram");
    else form("Histogram...", "histogram", {{"bins",256}});
    observeControl("menu.histogram");
    item("Line profile", "profile");
    form("Analyze particles...", "particles",
         {{"min_area", 0.0}, {"max_area", nullptr}, {"connectivity", 8}});
    form("Set scale from line...", "set_scale",
         {{"known_distance", 1.0}, {"unit", "um"}});
    ImGui::EndMenu();
  }
  ImGui::EndDisabled();
  ImGui::EndDisabled();
  const bool windowMenuOpen = ImGui::BeginMenu("Window");
  observeControl("menu.Window");
  if (windowMenuOpen) {
    const bool jobsVisible = jobsPanel.visible();
    if (ImGui::MenuItem("Jobs and log", shortcutLabel("app.jobs"), jobsVisible,
                        shortcutAvailable("app.jobs")))
      guard([&] { owner.action("show_jobs",{{"visible",!jobsVisible}}); });
    observeControl("menu.action.show_jobs", shortcutAvailable("app.jobs"),
                   shortcutLabel("app.jobs"), "app.jobs");
    ImGui::Separator();
    ImGui::BeginDisabled(owner.busy());
    for (size_t i = 0; i < owner.documents_.size(); ++i)
      if (ImGui::MenuItem(owner.documents_[i]->title().c_str(), nullptr,
                          owner.activeIndex() == int(i)))
        owner.setActiveIndex(int(i));
    ImGui::EndDisabled();
    ImGui::EndMenu();
  }
  ImGui::BeginDisabled(owner.busy());
  const bool helpMenuOpen = ImGui::BeginMenu("Help");
  observeControl("menu.Help");
  if (helpMenuOpen) {
    if (ImGui::MenuItem("Tools and shortcuts"))
      ask("Tools and shortcuts",
          {{"Help", "Drag for rectangles, ellipses and lines. Right-click or "
                    "Enter finishes polygons, polylines and points. Ctrl+wheel "
                    "zooms; Hand pans. Editing uses the current ROI. Deselect "
                    "before changing image dimensions. Save .ijx to retain "
                    "calibration, ROIs and overlays."}},
          [](Json) {});
    if (ImGui::MenuItem("About"))
      ask("About imagejxx",
          {{"Version", "0.2.0"},
           {"UI",
            std::string("Dear ImGui ") + IMGUI_VERSION + " / GLFW / OpenGL"},
           {"License",
            "MIT. Third-party notices are installed in imagejxx/licenses."}},
          [](Json) {});
    ImGui::EndMenu();
  }
  ImGui::EndDisabled();
  ImGui::EndMainMenuBar();
}
void Window::Impl::tools() {
  ImGui::Begin("Tools");
  ImGui::BeginDisabled(owner.busy());
  const bool readOnly = !owner.documents_.empty() && doc()->scientific();
  if (readOnly) {
    const auto source = doc();
    const auto shape = source->shape();
    const auto position = source->position();
    if (navigationDocument != source->id) {
      navigationDocument = source->id;
      navigationChannel = position.c + 1;
      navigationTime = position.t + 1;
      navigationChannelEdited = navigationTimeEdited = false;
    }
    auto axis = [&](const char *label, const char *key, int &draft, bool &edited,
                    int current, int count, bool channel) {
      const auto inputLabel = std::string("##") + key;
      const auto id = ImGui::GetID(inputLabel.c_str());
      if (!edited && ImGui::GetCurrentContext()->ActiveId != id)
        draft = current + 1;
      ImGui::BeginDisabled(count == 1);
      ImGui::TextUnformatted(label);
      ImGui::SetNextItemWidth(-1);
      if (ImGui::InputInt(inputLabel.c_str(), &draft, 0, 0))
        edited = true;
      const bool commit = edited && (!ImGui::IsItemActive() ||
          ImGui::IsItemDeactivatedAfterEdit() || ImGui::IsKeyPressed(ImGuiKey_Enter));
      observeControl(key, count > 1);
      if (commit) {
        guard([&] {
          if (draft < 1 || draft > count)
            throw std::invalid_argument(std::string(label) + " is outside the dataset");
          const auto selected = source->position();
          if ((channel ? selected.c : selected.t) != draft - 1)
            cancelSavedRoiGeometry("position_changed");
          source->setView({{channel ? "c" : "t", draft - 1}});
        });
        edited = false;
      }
      if (headlessDiagnostics) {
        auto &control = hostControls[key];
        control["document_id"] = source->id;
        control["min"] = control["minimum"] = 1;
        control["max"] = control["maximum"] = control["count"] = count;
        const auto selected = source->position();
        control["value"] = (channel ? selected.c : selected.t) + 1;
        control["draft_value"] = draft;
      }
      ImGui::EndDisabled();
    };
    axis("Channel", "navigation.channel", navigationChannel, navigationChannelEdited,
         position.c, shape.c, true);
    axis("Time point", "navigation.time", navigationTime, navigationTimeEdited,
         position.t, shape.t, false);
    const auto selected = source->position();
    const auto calibration = source->spatialCalibration();
    ImGui::Text("Time %.6g %s", calibration.time(selected.t), calibration.time_unit.c_str());
    ImGui::TextWrapped("Use New area in the ROI manager to draw an XY area, then Add it at the current channel, time and slice. Edit stored areas through the manager.");
    ImGui::Separator();
  }
  static const std::pair<const char *, const char *> tools[] = {
      {"rectangle", "Rectangle"},
      {"rounded_rectangle", "Rounded rectangle"},
      {"edit_roi", "Edit ROI"},
      {"path", "Path"},
      {"ellipse", "Ellipse"},
      {"polygon", "Polygon"},
      {"freehand", "Freehand"},
      {"line", "Line"},
      {"polyline", "Polyline"},
      {"angle", "Angle"},
      {"point", "Point"},
      {"multipoint", "Multipoint"},
      {"wand", "Wand"},
      {"brush", "Brush"},
      {"eraser", "Eraser"},
      {"picker", "Color picker"},
      {"text", "Text"},
      {"hand", "Hand"},
      {"zoom", "Zoom"}};
  for (auto [id, label] : tools) {
    const bool creationEnabled = !owner.documents_.empty() && scientificRoiCreationOwner == doc()->id;
    const bool pathEditable=!owner.documents_.empty() && doc()->nativeSnapshot().data().selection && doc()->nativeSnapshot().data().selection->is_path();
    const bool enabled = !readOnly || (creationEnabled && creationTool(id)) || std::string(id)=="path" || (std::string(id)=="edit_roi" && pathEditable) || std::string(id) == "hand" || std::string(id) == "zoom";
    ImGui::BeginDisabled(!enabled);
    const auto displayedTool = readOnly && !(creationEnabled && creationTool(tool)) && tool != "path" && !(tool=="edit_roi" && pathEditable) && tool != "zoom" ? "hand" : tool;
    if (ImGui::Selectable(label, displayedTool == id)) {
      cancelSavedRoiGeometry("tool_changed");
      cancelCreations("tool_changed");
      tool = id;
      if(tool=="path" && !owner.documents_.empty())scientificRoiCreationOwner=doc()->id;
      if (!owner.documents_.empty() && doc()->view().orthogonal)
        doc()->setView({{"orthogonal", false}});
      for (auto &pane : panes) {
        pane.points = Json::array();
        pane.dragging = false;
        pane.selectionGesture.reset();
      }
    }
    observeControl(std::string("tools.tool.") + id, enabled);
    ImGui::EndDisabled();
  }
  ImGui::Separator();
  if (creationTool(tool)) {
    ImGui::TextUnformatted("Selection mode");
    const bool creationEnabled = !readOnly || (!owner.documents_.empty() && scientificRoiCreationOwner == doc()->id);
    ImGui::BeginDisabled(!creationEnabled || owner.busy() || owner.documents_.empty());
    for (const auto mode : {SelectionCreationOperation::Replace,
                           SelectionCreationOperation::Add,
                           SelectionCreationOperation::Subtract,
                           SelectionCreationOperation::Intersect,
                           SelectionCreationOperation::Xor}) {
      const auto name = creationModeName(mode);
      const char *label = mode == SelectionCreationOperation::Replace ? "Replace"
          : mode == SelectionCreationOperation::Add ? "Add"
          : mode == SelectionCreationOperation::Subtract ? "Subtract"
          : mode == SelectionCreationOperation::Intersect ? "Intersect" : "XOR";
      if (ImGui::RadioButton(label, creationMode == mode)) {
        cancelCreations("mode_changed");
        creationMode = mode;
      }
      observeControl(std::string("selection_creation.mode.") + name,
                     creationEnabled && !owner.busy() && !owner.documents_.empty());
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("Shift adds; Alt subtracts from an area base. The mode is captured on the first press. Shift/Ctrl also constrain box geometry.");
    ImGui::BeginDisabled(!creationEnabled || owner.busy() || owner.documents_.empty());
    if (ImGui::Checkbox("Preserve off-image geometry", &creationFullGeometry))
      cancelCreations("geometry_changed");
    observeControl("selection_creation.full_geometry",
                   creationEnabled && !owner.busy() && !owner.documents_.empty());
    ImGui::EndDisabled();
    ImGui::Separator();
  }
  if (creationRebind) {
    ImGui::TextWrapped("Rebind selection: choose a mode and draw a complete new area.");
    if (ImGui::Button("Cancel selection rebind")) {
      cancelCreations("rebind_canceled");
      creationRebind.reset(); creationRebindOwner = 0;
    }
    observeControl("selection_creation.rebind.cancel");
  }
  if(tool=="path" && !owner.documents_.empty()) {
    pathAuthoring.sync(owner.nativeSnapshot(),!owner.busy(),selectionCreationLimits().max_working_bytes);
    pathAuthoring.controls(!owner.busy(),[&](const DocumentSnapshot &source,const Selection &value,const PathAuthoringCanvas &){publishAuthoredPath(source,value);});
  }
  if((tool=="edit_roi" && !owner.documents_.empty() && doc()->nativeSnapshot().data().selection && doc()->nativeSnapshot().data().selection->is_path()) ||
      (savedRoiEdit.active() && savedRoiEdit.original().is_path())) {
    ImGui::BeginDisabled(owner.busy() || savedRoiEdit.pointer_active());
    if(ImGui::RadioButton("Edit points",!pathTransform)){pathTransform=false;savedRoiEdit.path_transform(false);}
    observeControl("path.points");
    const float pathModeWidth=ImGui::CalcTextSize("Transform").x+ImGui::GetFrameHeight()+ImGui::GetStyle().ItemInnerSpacing.x;
    if(ImGui::GetItemRectMax().x+ImGui::GetStyle().ItemSpacing.x+pathModeWidth<=ImGui::GetWindowPos().x+ImGui::GetWindowContentRegionMax().x)ImGui::SameLine();
    if(ImGui::RadioButton("Transform",pathTransform)){pathTransform=true;savedRoiEdit.path_transform(true);}
    observeControl("path.transform");
    ImGui::Checkbox("Preserve aspect",&pathAspect);observeControl("path.aspect");
    ImGui::Checkbox("From center",&pathCenter);observeControl("path.center");
    savedRoiEdit.path_flags(pathAspect,pathCenter);ImGui::EndDisabled();
    bool invalid=savedRoiEdit.pointer_active() && !savedRoiEdit.pointer_valid();
    for(const auto &pane:panes)if(pane.selectionGesture)invalid|=pane.selectionGesture->invalid;
    ImGui::Text("Anchor: %s",pathCenter || ImGui::GetIO().KeyCtrl?"center":"opposite");
    ImGui::TextUnformatted(invalid?"Invalid resize":"Resize ready");
  }
  if (tool == "edit_roi") {
    const bool path=!owner.documents_.empty() && doc()->nativeSnapshot().data().selection && doc()->nativeSnapshot().data().selection->is_path();
    if(path && pathTransform)ImGui::TextWrapped("Drag a bound to resize. Shift preserves aspect; Ctrl resizes from the center. Release while invalid cancels the resize. Escape cancels a drag.");
    else if(path)ImGui::TextWrapped("Drag inside to move; drag endpoints and curve controls. Shift constrains movement. Arrows nudge (Shift: 10 pixels). Escape cancels a drag.");
    else ImGui::TextWrapped(
        "Drag inside to move; drag handles to resize or edit vertices. Shift "
        "constrains movement/aspect; Ctrl resizes from center. Alt-click "
        "inserts a vertex; Delete removes the active vertex. Arrows nudge "
        "(Shift: 10 pixels). Escape cancels a drag.");
    ImGui::TextWrapped(
        "Drag the round handle to rotate; Shift snaps to 15 degrees.");
    ImGui::BeginDisabled(readOnly && !path);
    if (ImGui::Button("Geometry / properties"))
      guard([&] { owner.action("show_selection_properties"); });
    observeControl("path.properties",!readOnly || path);
    ImGui::EndDisabled();
  }
  if (tool == "rounded_rectangle") {
    std::int64_t diameter = preferenceState.settings.integer("rounded_corner_diameter");
    ImGui::BeginDisabled(owner.busy());
    if (ImGui::InputScalar("Corner diameter", ImGuiDataType_S64, &diameter))
      guard([&] { owner.updateSettings({{"rounded_corner_diameter", diameter}}, false); });
    observeControl("tools.rounded_corner_diameter", !owner.busy());
    ImGui::EndDisabled();
  }
  if (tool == "point" || tool == "multipoint" || tool == "line" ||
      tool == "polyline" || tool == "angle") {
    const char *name =
        tool == "point" || tool == "multipoint" ? "point_size" : "line_width";
    float width = static_cast<float>(preferenceState.settings.number(name));
    if (ImGui::SliderFloat("Width", &width, 1, 256, "%.0f"))
      owner.updateSettings({{name, width}}, false);
  } else
    ImGui::SliderFloat("Brush width", &brush, 1, 256, "%.0f");
  ImGui::ColorEdit3("Paint", foreground, ImGuiColorEditFlags_NoInputs);
  ImGui::ColorEdit3("Erase", background, ImGuiColorEditFlags_NoInputs);
  ImGui::BeginDisabled(readOnly);
  ImGui::Checkbox("All slices", &allSlices);
  ImGui::EndDisabled();
  ImGui::Checkbox("Show ROIs", &showRois);
  if (!owner.documents_.empty()) {
    auto &pane = panes[owner.activeIndex()];
    auto d = pane.doc;
    const double physicalX = d->displaySpacing()[0];
    auto fitViews = [&] {
      d->setView({{"zoom", 0},
                  {"plane_zoom", std::array<double, 3>{}},
                  {"pan", std::array<std::array<double, 2>, 3>{}}});
    };
    if (ImGui::Button(d->view().orthogonal ? "Fit views" : "Fit image")) {
      if (d->view().orthogonal)
        fitViews();
      else {
        auto pan = d->view().pan;
        pan[0] = {};
        if (d->view().linkedZoom)
          d->setView({{"zoom", 0}, {"pan", pan}});
        else {
          auto zooms = d->view().planeZoom;
          zooms[0] = 0;
          d->setView({{"plane_zoom", zooms}, {"pan", pan}});
        }
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("1:1")) {
      guard([&] {
        auto pan = d->view().pan;
        pan[0] = {};
        if (d->view().linkedZoom)
          d->setView({{"zoom", 1 / physicalX}, {"pan", pan}});
        else {
          auto zooms = d->view().planeZoom;
          zooms[0] = 1 / physicalX;
          d->setView({{"plane_zoom", zooms}, {"pan", pan}});
        }
      });
    }
    ImGui::BeginDisabled(readOnly || get(d->state().metadata, "selection").empty());
    if (ImGui::Button("Fit selection"))
      guard([&] { owner.action("fit_selection"); });
    ImGui::EndDisabled();
    const bool orthogonal = d->view().orthogonal;
    const std::array<const char *, 3> planeNames{"XY", "XZ", "YZ"};
    if (orthogonal) {
      ImGui::TextUnformatted("Zoom plane");
      ImGui::SetNextItemWidth(-1);
      const int selectedAtDraw = numericZoomPlane;
      const bool planeChoiceOpen =
          ImGui::BeginCombo("##zoom_plane", planeNames[selectedAtDraw]);
      observeControl("tools.zoom.plane");
      if (headlessDiagnostics) {
        auto &control = hostControls["tools.zoom.plane"];
        control["document_id"] = d->id;
        control["selected_plane_at_draw"] = planeNames[selectedAtDraw];
      }
      if (planeChoiceOpen) {
        for (int i = 0; i < 3; ++i) {
          const bool selected = numericZoomPlane == i;
          if (ImGui::Selectable(planeNames[i], selected))
            numericZoomPlane = i;
          observeControl(std::string("tools.zoom.plane.choice.") + planeNames[i]);
          if (headlessDiagnostics)
            hostControls[std::string("tools.zoom.plane.choice.") + planeNames[i]]
                        ["selected_at_draw"] = selected;
          if (selected)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
      if (headlessDiagnostics)
        hostControls["tools.zoom.plane"]["selected_plane_after"] =
            planeNames[numericZoomPlane];
    }
    const int planeIndex = orthogonal ? numericZoomPlane : 0;
    const int horizontalAxis = planeIndex == 2 ? 1 : 0;
    const double horizontalSpacing = d->displaySpacing()[horizontalAxis];
    float percent = pane.zoom * 100;
    if (orthogonal) {
      const auto planes = pane.orthogonal.diagnostics();
      percent = float(number(planes[planeIndex]["scale"][0])) * 100;
      ImGui::Text("%s zoom (%%)", planeNames[planeIndex]);
    } else
      ImGui::TextUnformatted("Zoom (%)");
    ImGui::SetNextItemWidth(-1);
    const bool percentChanged =
        ImGui::InputFloat("##zoom_percent", &percent, 0, 0, "%.2f");
    observeControl("tools.zoom.percent");
    if (headlessDiagnostics) {
      auto &control = hostControls["tools.zoom.percent"];
      control["document_id"] = d->id;
      control["plane_index"] = planeIndex;
      control["plane"] = planeNames[planeIndex];
      control["horizontal_spacing"] = horizontalSpacing;
      control["linked_zoom"] = d->view().linkedZoom;
      control["input_value_finite"] = std::isfinite(percent);
      control["input_value_percent"] =
          std::isfinite(percent) ? Json(percent) : Json(nullptr);
      control["value_changed"] = percentChanged;
    }
    if (percentChanged) {
      guard([&] {
        if (!std::isfinite(percent))
          throw std::invalid_argument("Zoom percentage must be finite");
        // Preserve existing XY percentage/clamp semantics. Native view validation
        // rejects unrepresentable physical zoom before publishing any state.
        const float boundedPercent = std::clamp(percent, .01f, 6400.f);
        const float fraction = boundedPercent / 100.f;
        const double zoom = fraction / horizontalSpacing;
        if (d->view().linkedZoom)
          d->setView({{"zoom", zoom}});
        else {
          auto zooms = d->view().planeZoom;
          zooms[planeIndex] = zoom;
          d->setView({{"plane_zoom", zooms}});
        }
      });
    }
  }
  ImGui::EndDisabled();
  ImGui::End();
}
void Window::Impl::properties() {
  ImGui::Begin("Image properties");
  ImGui::BeginDisabled(owner.busy());
  if (!owner.documents_.empty()) {
    auto d = doc();
    const auto shape=d->shape();const auto type=d->nativeSnapshot().data().dataset.pixel_type();
    ImGui::TextWrapped("%s%s", d->title().c_str(), d->dirty() ? " *" : "");
    ImGui::Text("%d x %d x %d | %s", shape.x, shape.y, shape.s,
                typeName(type).c_str());
    ImGui::Text("Slice %d / %d", d->slice() + 1, d->shape().z);
    if (d->scientific()) {
      const auto position = d->position();
      const auto shape = d->shape();
      ImGui::Text("Channel %d / %d | Time point %d / %d",
                  position.c + 1, shape.c, position.t + 1, shape.t);
    }
    auto memory = d->pixelStorage();
    ImGui::Text("Pixels %.2f MiB",
                number(memory["logical_bytes"]) / (1024 * 1024));
    ImGui::Text("Undo pixels %.2f MiB",
                d->historyPixelBytes() / double(1024 * 1024));
    if (d->historyPixelBudget())
      ImGui::Text("Undo budget %.2f MiB",
                  d->historyPixelBudget() / double(1024 * 1024));
    const auto &c = get(d->state().metadata, "calibration");
    ImGui::Text("Pixel %.4g x %.4g %s", number(get(c, "pixel_width"), 1),
                number(get(c, "pixel_height"), 1),
                string(get(c, "unit"), "px").c_str());
    ImGui::BeginDisabled(d->scientific());
    if (ImGui::Button("Calibration..."))
      ask("Calibration", c,
          [this](Json values) { owner.action("calibration", values); });
    if (ImGui::Button("Rename..."))
      ask("Rename image", {{"title", d->title()}}, [d](Json values) {
        auto edit = d->nativeSnapshot().begin_edit();
        edit.set_title(string(get(values, "title")));
        d->commitNative(edit);
      });
    ImGui::EndDisabled();
    ImGui::Separator();
    ImGui::Text("History %zu / %zu", d->undoSteps(), d->historyLimit());
    ImGui::Text("Saved ROIs: %zu", get(d->state().metadata, "rois").size());
    ImGui::Text("Overlays: %zu", get(d->state().metadata, "overlays").size());
    ImGui::Separator();
    if (ImGui::Button("Auto display range"))
      guard([&] { owner.action("display_range", {{"auto", true}}); });
    ImGui::TextWrapped(d->scientific()
        ? "Use Channel, Time point and Slice to navigate. Save a session (.ijx) to keep data and annotations."
        : "Draw a selection to limit pixel edits. Save a session (.ijx) to keep image data and annotations.");
  } else
    ImGui::TextWrapped("Create or open an image to inspect its properties.");
  ImGui::EndDisabled();
  ImGui::End();
}
void Window::Impl::rois() {
  roiManagerControls = Json::object();
  roiManagerRows = Json::array();
  roiManagerVisible = roiManagerFocused = false;
  guard([&] { synchronizeRoiManager(); });
  const bool visible = ImGui::Begin("ROI manager");
  shortcutRoiManager = visible ? ImGui::GetCurrentWindow() : nullptr;
  roiManagerVisible = visible;
  roiManagerFocused = visible &&
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
#ifdef IMGUI_HAS_DOCK
  if (headlessDiagnostics) {
    const auto window = ImGui::GetCurrentWindow();
    const auto node = window->DockNode;
    const auto bar = node ? node->TabBar : nullptr;
    const auto tab = bar ? ImGui::TabBarFindTabByID(bar, window->TabId) : nullptr;
    if (tab && tab->Width > 0) {
      const auto min = ImVec2(bar->BarRect.Min.x + tab->Offset - bar->ScrollingAnim,
                              bar->BarRect.Min.y);
      const auto max = ImVec2(min.x + tab->Width, bar->BarRect.Max.y);
      roiManagerControls["tab"] = {
          {"id", tab->ID}, {"rect", {min.x, min.y, max.x, max.y}},
          {"point", {(min.x + max.x) * .5f, (min.y + max.y) * .5f}},
          {"enabled", true}, {"selected", bar->SelectedTabId == tab->ID}};
      hostControls["roi_manager.tab"] = roiManagerControls["tab"];
    }
  }
#endif
  if (!visible) {
    ImGui::End();
    return;
  }
  ImGui::BeginDisabled(owner.busy() || !roiManagerSource);
  ImGui::BeginDisabled(!actionAllowed("select_roi"));
  auto act = [&](const char *label, const char *name) {
    ImGui::BeginDisabled(!actionAllowed(name));
    if (ImGui::Button(label))
      guard([&] { owner.action(name); });
    observeRoiManagerControl(name);
    ImGui::EndDisabled();
  };
  act("New area", "create_roi");
  ImGui::SameLine();
  act("Add", "add_roi");
  ImGui::SameLine();
  act("Update", "update_roi");
  ImGui::SameLine();
  act("Delete", "delete_roi");
  if (ImGui::Button("Rename"))
    guard([&] {
      synchronizeRoiManager();
      if (!roiManagerSource || roiIndex < 0 ||
          static_cast<std::size_t>(roiIndex) >= roiManagerSource->data().rois.size())
        throw std::invalid_argument("Select a saved ROI");
      const auto source = *roiManagerSource;
      const auto index = roiIndex;
      ask("Rename ROI", {{"name", source.data().rois[index].name}},
          [this, source, index](Json p) {
            const auto current = owner.nativeSnapshot();
            if (!source.same_document(current) ||
                source.revisions().metadata != current.revisions().metadata)
              throw RevisionConflict();
            p["index"] = index;
            owner.action("rename_roi", p);
          });
    });
  observeRoiManagerControl("rename_roi");
  if (ImGui::Button("Style / position"))
    guard([&] {
      synchronizeRoiManager();
      if (!roiManagerSource || roiIndex < 0 ||
          static_cast<std::size_t>(roiIndex) >= roiManagerSource->data().rois.size())
        throw std::invalid_argument("Select a saved ROI");
      const auto captured = *roiManagerSource;
      const auto index = static_cast<std::size_t>(roiIndex);
      const auto &row = captured.data().rois[index];
      const auto style = resolve_roi_style(row.selection);
      const auto uiAxis = [](std::optional<int> value) { return value ? *value + 1 : 0; };
      ask("ROI properties", {{"name", row.name},
          {"stroke_color", roi_color_string(style.stroke_color)},
          {"fill_color", roi_color_string(style.fill_color)},
          {"stroke_width", style.stroke_width},
          {"time", uiAxis(row.t)}, {"channel", uiAxis(row.c)}, {"slice", uiAxis(row.z)}},
          [this, captured, index](Json p) {
            const auto current = owner.nativeSnapshot();
            if (!captured.same_document(current) ||
                !captured.revisions().matches(current.revisions(), Change::All))
              throw RevisionConflict();
            const auto axis = [&](const char *key) -> Json {
              const auto &value = p.at(key);
              if (!value.is_number_integer() || value < 0 || value > std::numeric_limits<int>::max())
                throw std::invalid_argument("ROI positions use one-based integers; zero means all");
              const auto number = value.get<int>();
              return number ? Json(number - 1) : Json(nullptr);
            };
            owner.action("roi_properties", {{"index", index}, {"name", p.at("name")},
                {"style", {{"stroke_color", p.at("stroke_color")},
                           {"fill_color", p.at("fill_color")}, {"stroke_width", p.at("stroke_width")}}},
                {"t", axis("time")}, {"c", axis("channel")}, {"z", axis("slice")}});
          });
    });
  observeRoiManagerControl("roi_properties");
  ImGui::TextDisabled("Position: 1-based; 0 = all. Update keeps the saved position.");
  ImGui::SameLine();
  act("Measure all", "measure_rois");
  ImGui::BeginDisabled(!actionAllowed("load_rois"));
  if (ImGui::Button("Import"))
    file("Import ROI JSON", false,
         [this](std::string p) { owner.action("load_rois", {{"path", p}}); });
  observeRoiManagerControl("load_rois");
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!actionAllowed("save_rois"));
  if (ImGui::Button("Export"))
    file(
        "Save ROI JSON", true,
        [this](std::string p) { owner.action("save_rois", {{"path", p}}); },
        "rois.json");
  observeRoiManagerControl("save_rois");
  ImGui::EndDisabled();
  // A preceding single-row edit can have replaced the saved collection during
  // this draw. Rebind the row scope before offering any multi-row operation.
  guard([&] { synchronizeRoiManager(); });
  ImGui::Separator();
  const auto source = roiManagerSource;
  const auto count = source ? source->data().rois.size() : 0;
  ImGui::BeginDisabled(count == 0);
  if (ImGui::Button("Select all"))
    guard([&] { selectAllRoiManagerRows(); });
  observeRoiManagerControl("select_all", count != 0);
  ImGui::EndDisabled();
  auto &io = ImGui::GetIO();
  ImGui::SameLine();
  ImGui::Text("%zu selected", roiSelectedIndices.size());
  const bool canCombine = roiManagerCanCombine();
  ImGui::BeginDisabled(!canCombine);
  auto combine = [&](const char *label, const char *operation) {
    if (ImGui::Button(label))
      guard([&] {
        if (!roiManagerSource || !roiManagerCanCombine())
          throw std::invalid_argument("Select at least two saved area ROIs");
        const auto captured = *roiManagerSource;
        const std::vector<std::size_t> indices(roiSelectedIndices.begin(),
                                               roiSelectedIndices.end());
        owner.action("combine_rois", {{"indices", indices},
            {"operation", operation}, {"document_id", captured.id()},
            {"metadata_revision", captured.revisions().metadata}});
      });
    observeRoiManagerControl(operation, canCombine);
  };
  combine("Union", "union");
  ImGui::SameLine();
  combine("Intersect", "intersection");
  ImGui::SameLine();
  combine("Subtract", "subtraction");
  ImGui::SameLine();
  combine("XOR", "xor");
  ImGui::EndDisabled();
  if (source && !roiSelectedIndices.empty()) {
    const auto first = *roiSelectedIndices.begin();
    if (first < source->data().rois.size())
      ImGui::TextWrapped("Subtract starts with %s (first selected row).",
                         source->data().rois[first].name.c_str());
  }
  ImGui::TextWrapped("Ctrl toggles rows; Shift selects a range. "
                     "Update, Delete and Rename use the focused row.");
  if (roiSelectedIndices.size() >= 2 && !canCombine)
    ImGui::TextWrapped("Boolean operations require every selected ROI to be an area.");
  ImGui::EndDisabled();
  if (source) {
    const auto &saved = source->data().rois;
    ImGui::BeginChild("saved_roi_rows", {0, 0}, false);
    for (std::size_t i = 0; i < saved.size(); ++i) {
      ImGui::PushID(int(i));
      const bool selected = roiSelectedIndices.count(i) != 0;
      ImGui::BeginDisabled(!actionAllowed("select_roi"));
      if (ImGui::Selectable(saved[i].name.c_str(), selected)) {
        if (io.KeyShift) {
          const auto anchor = roiRangeAnchor.value_or(i);
          if (!io.KeyCtrl)
            roiSelectedIndices.clear();
          for (auto index = std::min(anchor, i); index <= std::max(anchor, i); ++index)
            roiSelectedIndices.insert(index);
          roiIndex = static_cast<int>(i);
          if (!roiRangeAnchor)
            roiRangeAnchor = i;
        } else if (io.KeyCtrl) {
          if (!roiSelectedIndices.erase(i))
            roiSelectedIndices.insert(i);
          roiRangeAnchor = i;
          roiIndex = static_cast<int>(i);
        } else
          guard([&] { owner.action("select_roi", {{"index", i}}); });
      }
      observeRoiManagerControl("row." + std::to_string(i));
      ImGui::EndDisabled();
      ImGui::BeginDisabled(!selection_is_area(saved[i].selection));
      if (ImGui::Button("Edit Saved Area"))
        guard([&] { owner.action("edit_saved_roi_geometry", {{"index", i}}); });
      observeRoiManagerControl("row." + std::to_string(i) + ".edit_saved_area");
      ImGui::EndDisabled();
      if (headlessDiagnostics) {
        const auto position = [](const std::optional<int> &value) {
          return value ? Json(*value) : Json(nullptr);
        };
        roiManagerRows.push_back({{"index", i}, {"name", saved[i].name},
            {"kind", saved[i].selection.kind},
            {"area", selection_is_area(saved[i].selection)},
            {"drawn_selected", selected},
            {"position", {{"t", position(saved[i].t)},
                           {"c", position(saved[i].c)},
                           {"z", position(saved[i].z)}}},
            {"control", roiManagerControls["row." + std::to_string(i)]}});
      }
      ImGui::PopID();
    }
    ImGui::EndChild();
  }
  for (auto &row : roiManagerRows) {
    const auto index = row["index"].get<std::size_t>();
    row["selected"] = roiSelectedIndices.count(index) != 0;
    row["focused"] = roiIndex >= 0 && index == static_cast<std::size_t>(roiIndex);
  }
  ImGui::EndDisabled();
  ImGui::End();
}
void Window::Impl::resultTable() {
  const bool visible = ImGui::Begin("Results");
  if (headlessDiagnostics)
    resultsTableObservation = {{"visible", visible}, {"row_count", owner.results.size()},
        {"column_count", 0}, {"begin_table", false}, {"headers", Json::array()},
        {"cells", Json::array()}, {"controls", Json::object()}, {"truncated", false}};
  const auto observed = [&](const char *name) {
    observeControl(std::string("results.") + name);
    if (headlessDiagnostics) {
      auto &item = hostControls[std::string("results.") + name];
      const auto *window = ImGui::GetCurrentWindowRead();
      item["window_name"] = window->Name;
      item["visible"] = ImGui::IsItemVisible();
      item["clip_rect"] = {window->ClipRect.Min.x, window->ClipRect.Min.y,
                            window->ClipRect.Max.x, window->ClipRect.Max.y};
      resultsTableObservation["controls"][name] = item;
    }
  };
  ImGui::Text("%zu measurements", owner.results.size());
  const bool save = ImGui::Button("Save CSV");
  observed("save_csv");
  if (save)
    file(
        "Save measurements", true,
        [this](std::string p) { owner.action("save_results", {{"path", p}}); },
        "results.csv");
  ImGui::SameLine();
  const bool clear = ImGui::Button("Clear");
  observed("clear");
  if (clear) {
    owner.results = Json::array();
    resultsColumnPage = 0;
  }
  auto current = owner.settings();
  auto columns = resultColumns(owner.results, current);
  if (columns != resultsColumnNames) {
    resultsColumnNames = columns;
    resultsColumnPage = 0;
  }
  constexpr std::size_t pageSize = 64;
  const std::size_t pages = columns.empty() ? 0 : 1 + (columns.size() - 1) / pageSize;
  resultsColumnPage = pages ? std::min(resultsColumnPage, pages - 1) : 0;
  if (pages > 1) {
    ImGui::BeginDisabled(resultsColumnPage == 0);
    const bool previous = ImGui::Button("Previous columns");
    observed("previous_columns");
    ImGui::EndDisabled();
    if (previous && resultsColumnPage) --resultsColumnPage;
    ImGui::SameLine();
    ImGui::BeginDisabled(resultsColumnPage + 1 >= pages);
    const bool next = ImGui::Button("Next columns");
    observed("next_columns");
    ImGui::EndDisabled();
    if (next && resultsColumnPage + 1 < pages) ++resultsColumnPage;
    ImGui::Text("Columns %zu-%zu of %zu", resultsColumnPage * pageSize + 1,
                std::min(columns.size(), (resultsColumnPage + 1) * pageSize), columns.size());
  }
  const auto firstColumn = resultsColumnPage * pageSize;
  const auto endColumn = std::min(columns.size(), firstColumn + pageSize);
  const auto shownColumns = endColumn - firstColumn;
  const bool opened = shownColumns &&
      ImGui::BeginTable("measurements", static_cast<int>(shownColumns),
                        ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                            ImGuiTableFlags_Resizable);
  if (headlessDiagnostics) {
    resultsTableObservation["row_count"] = owner.results.size();
    resultsTableObservation["column_count"] = columns.size();
    resultsTableObservation["begin_table"] = opened;
    resultsTableObservation["page"] = resultsColumnPage;
    resultsTableObservation["page_count"] = pages;
    resultsTableObservation["page_size"] = pageSize;
    resultsTableObservation["first_column"] = firstColumn;
    resultsTableObservation["end_column_exclusive"] = endColumn;
  }
  if (opened) {
    for (auto c = firstColumn; c < endColumn; ++c)
      ImGui::TableSetupColumn(columns[c].c_str(), ImGuiTableColumnFlags_WidthFixed, 110);
    ImGui::TableSetupScrollFreeze(1, 1);
    const bool literalHeaders = std::any_of(columns.begin() + firstColumn, columns.begin() + endColumn,
        [](const std::string &name) { return name.find("##") != std::string::npos; });
    if (literalHeaders) {
      float headerHeight = ImGui::GetTextLineHeight();
      for (auto c = firstColumn; c < endColumn; ++c)
        headerHeight = std::max(headerHeight, ImGui::CalcTextSize(
            columns[c].data(), columns[c].data() + columns[c].size(), false).y);
      ImGui::TableNextRow(ImGuiTableRowFlags_Headers,
                          headerHeight + 2 * ImGui::GetStyle().CellPadding.y);
      for (auto c = firstColumn; c < endColumn; ++c) {
        ImGui::TableSetColumnIndex(static_cast<int>(c - firstColumn));
        ImGui::PushID(static_cast<int>(c - firstColumn));
        if (columns[c].find("##") != std::string::npos) {
          const auto position = ImGui::GetCursorScreenPos();
          ImGui::TableHeader("##literal_column");
          ImGui::GetWindowDrawList()->AddText(position, ImGui::GetColorU32(ImGuiCol_Text),
              columns[c].data(), columns[c].data() + columns[c].size());
        } else ImGui::TableHeader(columns[c].c_str());
        ImGui::PopID();
      }
    } else ImGui::TableHeadersRow();
    if (headlessDiagnostics) {
      const auto *table = ImGui::GetCurrentTable();
      for (std::size_t column = 0; column < shownColumns; ++column) {
        const auto &geometry = table->Columns[static_cast<int>(column)];
        const auto flags = ImGui::TableGetColumnFlags(static_cast<int>(column));
        const bool shown = (flags & ImGuiTableColumnFlags_IsVisible) &&
            table->RowPosY2 > table->InnerClipRect.Min.y && table->RowPosY1 < table->InnerClipRect.Max.y;
        resultsTableObservation["headers"].push_back({{"column", firstColumn + column},
            {"name", ImGui::TableGetColumnName(static_cast<int>(column))},
            {"text", columns[firstColumn + column]},
            {"text_source", columns[firstColumn + column].find("##") != std::string::npos
                ? "AddText.literal" : literalHeaders ? "TableHeader" : "TableHeadersRow"},
            {"visible", shown},
            {"rect", {geometry.MinX, table->RowPosY1, geometry.MaxX, table->RowPosY2}}});
      }
    }
    ImGuiListClipper clip;
    clip.Begin(int(owner.results.size()));
    while (clip.Step())
      for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row) {
        ImGui::TableNextRow();
        for (auto c = firstColumn; c < endColumn; ++c) {
          ImGui::TableSetColumnIndex(static_cast<int>(c - firstColumn));
          const auto &value = get(owner.results[row], columns[c].c_str());
          auto label = resultText(value, current);
          ImGui::TextUnformatted(label.c_str());
          if (headlessDiagnostics) {
            if (resultsTableObservation["cells"].size() < 256) {
              const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
              resultsTableObservation["cells"].push_back({{"row", row}, {"column", c},
                  {"name", columns[c]}, {"text", label}, {"visible", ImGui::IsItemVisible()},
                  {"rect", {a.x, a.y, b.x, b.y}}});
            } else resultsTableObservation["truncated"] = true;
          }
        }
      }
    ImGui::EndTable();
  }
  ImGui::End();
}
void Window::Impl::recorder() {
  ImGui::Begin("Workflow");
  ImGui::BeginDisabled(owner.busy());
  ImGui::Checkbox("Record actions", &owner.recording);
  observeControl("workflow.record");
  if (headlessDiagnostics) {
    workflowControls["record"] = hostControls["workflow.record"];
    workflowControls["record"]["value"] = owner.recording;
  }
  ImGui::SameLine();
  if (ImGui::Button("Clear"))
    owner.workflow = Json::array();
  observeControl("workflow.clear");
  if (ImGui::Button("Load"))
    file("Load workflow", false, [this](std::string p) {
      owner.action("load_workflow", {{"path", p}});
    });
  observeControl("workflow.load");
  ImGui::SameLine();
  if (ImGui::Button("Save"))
    file(
        "Save workflow", true,
        [this](std::string p) { owner.action("save_workflow", {{"path", p}}); },
        "workflow.json");
  observeControl("workflow.save");
  ImGui::BeginDisabled(owner.documents_.empty() || !owner.workflow.is_array() || owner.workflow.empty() ||
                       !actionAllowed("run_workflow"));
  const auto runWorkflow = [this](bool batch) {
    if(doc()->scientific()||needs_native_workflow_review(owner.workflow)){owner.action(batch?"batch":"run_workflow");return;}
    const bool typed = std::any_of(owner.workflow.begin(), owner.workflow.end(),
        [](const Json &step) { return step.is_object() && step.contains("schema_version"); });
    const auto hasOperation = [this](const char *operation) {
      return std::any_of(owner.workflow.begin(), owner.workflow.end(),
          [operation](const Json &step) {
            return step.is_object() && step.contains("schema_version") &&
                   get(step, "operation") == operation;
          });
    };
    const bool gaussian = hasOperation("gaussian_blur_imagej");
    const bool rank = hasOperation("rank_filter_imagej");
    if (!batch && !typed) {
      owner.action("run_workflow");
      return;
    }
    Json values = batch
        ? Json{{"paths", Json::array()},
               {"output_directory", std::filesystem::current_path().u8string()}}
        : Json::object();
    if (typed) {
      values["processing_scope"] = "Current plane";
      values["region"] = "Active selection";
      if (gaussian)
        values["use_destination_calibration"] = false;
    }
    ask(batch ? "Batch workflow" : "Run workflow", std::move(values),
        [this, batch, typed, gaussian, rank](Json parameters) {
          if (typed) {
            const Json request = {
                {"scope", parameters.at("processing_scope") == "All Z slices"
                              ? "all_z" : "current_plane"},
                {"roi", parameters.at("region") == "Whole plane"
                            ? "whole_plane" : "active_selection"},
                {"mode", "planes"}, {"destination", "in_place"}};
            if (gaussian)
              parameters["gaussian_rebind"] = request;
            if (rank)
              parameters["rank_rebind"] = request;
            parameters.erase("processing_scope");
            parameters.erase("region");
          }
          owner.action(batch ? "batch" : "run_workflow", parameters);
        });
    for (auto &field : dialog.schema) {
      if (field["name"] == "processing_scope") {
        field["label"] = "Process";
        field["choices"] = {"Current plane", "All Z slices"};
      } else if (field["name"] == "region") {
        field["label"] = "Region";
        field["choices"] = {"Active selection", "Whole plane"};
      } else if (field["name"] == "use_destination_calibration") {
        field["label"] = "Use this image's calibration";
      }
    }
  };
  const bool runClicked = ImGui::Button("Run on image");
  observeControl("workflow.run");
  if (headlessDiagnostics)
    workflowControls["run"] = hostControls["workflow.run"];
  if (runClicked)
    guard([&] { runWorkflow(false); });
  ImGui::SameLine();
  const bool batchClicked = ImGui::Button("Batch...");
  observeControl("workflow.batch");
  if (headlessDiagnostics)
    workflowControls["batch"] = hostControls["workflow.batch"];
  if (batchClicked)
    guard([&] { runWorkflow(true); });
  ImGui::SameLine();const bool sessionBatch=ImGui::Button("Session batch...");observeControl("workflow.session_batch");if(sessionBatch)guard([&]{owner.action("native_batch");});
  ImGui::EndDisabled();
  ImGui::Separator();
  if (!owner.workflow.is_array())
    ImGui::TextWrapped("Workflow data is invalid. Load a valid workflow before replaying.");
  const auto rowCount = owner.workflow.is_array() ? owner.workflow.size() : 0;
  for (size_t i = 0; i < rowCount; ++i) {
    if(get(owner.workflow[i],"schema")=="imagejxx.native-workflow-program"){
      ImGui::Text("%zu. Native program (%zu steps)",i+1,get(owner.workflow[i],"steps").size());
      continue;
    }
    const auto step = owner.workflow[i]; // Replay can append and reallocate.
    if(get(step,"schema")=="imagejxx.contrast-apply-command" || get(step,"schema")=="imagejxx.rgb-contrast-apply-command") {
      ImGui::Text("%zu. Apply brightness/contrast",i+1);ImGui::PushID(static_cast<int>(i));
      ImGui::BeginDisabled(owner.busy()||owner.documents_.empty());
      if(ImGui::Button("Replay captured Apply"))guard([&]{owner.action("replay_contrast_apply",{{"record",step}});});
      observeControl("workflow.contrast_apply."+std::to_string(i)+".replay");ImGui::SameLine();
      if(ImGui::Button("Rebind Apply..."))guard([&]{owner.action("show_contrast_apply_rebind",{{"record",step}});});
      observeControl("workflow.contrast_apply."+std::to_string(i)+".rebind");
      ImGui::EndDisabled();ImGui::PopID();continue;
    }
    if(get(step,"schema")=="imagejxx.histogram-command") {
      ImGui::Text("%zu. Captured histogram",i+1);ImGui::PushID(static_cast<int>(i));
      ImGui::BeginDisabled(owner.busy()||owner.documents_.empty());
      if(ImGui::Button("Replay histogram"))guard([&]{owner.action("replay_histogram",{{"record",step}});});
      observeControl("workflow.histogram."+std::to_string(i)+".replay");ImGui::SameLine();
      if(ImGui::Button("Rebind histogram..."))guard([&]{owner.action("show_histogram_rebind",{{"record",step}});});
      observeControl("workflow.histogram."+std::to_string(i)+".rebind");
      ImGui::EndDisabled();ImGui::PopID();continue;
    }
    if(get(step,"schema")=="imagejxx.indexed-dataset-command" || get(step,"schema")=="imagejxx.indexed-rgb-command") {
      ImGui::Text("%zu. Indexed color conversion",i+1);ImGui::PushID(static_cast<int>(i));
      ImGui::BeginDisabled(owner.busy()||owner.documents_.empty());
      if(ImGui::Button("Review indexed color on active source..."))guard([&]{owner.action("review_indexed_color",{{"record",step}});});
      observeControl("workflow.indexed_color."+std::to_string(i));
      ImGui::EndDisabled();ImGui::PopID();continue;
    }
    if(get(step,"schema")=="imagejxx.color-stack-command") {
      ImGui::Text("%zu. Color components and stack layout",i+1);ImGui::PushID(static_cast<int>(i));
      ImGui::BeginDisabled(owner.busy()||owner.documents_.empty());
      if(ImGui::Button("Review components on active source..."))guard([&]{owner.action("review_color_stack",{{"record",step}});});
      observeControl("workflow.color_stack."+std::to_string(i));
      ImGui::EndDisabled();ImGui::PopID();continue;
    }
    if(get(step,"schema")=="imagejxx.rgb-dataset-command") {
      ImGui::Text("%zu. RGB dataset conversion",i+1);ImGui::PushID(static_cast<int>(i));
      ImGui::BeginDisabled(owner.busy()||owner.documents_.empty());
      if(ImGui::Button("Review RGB on active source..."))guard([&]{owner.action("review_rgb_dataset",{{"record",step}});});
      observeControl("workflow.rgb_dataset."+std::to_string(i));
      ImGui::EndDisabled();ImGui::PopID();continue;
    }
#ifdef IMAGEJXX_HAS_SCIENTIFIC_IO
    if(get(step,"schema")=="imagejxx.scientific-image-io"){
      ImGui::Text("%zu. %s scientific TIFF",i+1,get(step,"operation")=="export"?"Export":"Open");
      ImGui::PushID(static_cast<int>(i));ImGui::BeginDisabled(owner.busy()||(get(step,"operation")=="export"&&owner.documents_.empty()));
      if(ImGui::Button("Review file action..."))guard([&]{owner.action("review_scientific_io",{{"record",step}});});observeControl("workflow.scientific_io."+std::to_string(i));
      ImGui::EndDisabled();ImGui::PopID();continue;
    }
#endif
    const bool displayAnnotation=get(step,"schema")=="imagejxx.planar-display-annotation-command";
    const bool roiImport=get(step,"schema")=="imagejxx.imagej-roi-import-command" || get(step,"schema")=="imagejxx.imagej-annotation-import-command";
    const bool roiZip=get(step,"schema")=="imagejxx.imagej-roi-zip-export" || (get(step,"schema")=="imagejxx.imagej-annotation-export" && get(step,"packaging")=="zip");
    const bool roiExport=get(step,"schema")=="imagejxx.imagej-roi-export" || get(step,"schema")=="imagejxx.imagej-annotation-export" || roiZip;
    auto label = std::to_string(i + 1) + ". " + (displayAnnotation?"Display annotation":roiImport?"Import ROIs":roiExport?(roiZip?"Export ROI ZIP":"Export ROI"):string(get(step, "operation"))) +
                 (boolean(get(step, "all_slices")) ? " (all slices)" : "");
    ImGui::TextUnformatted(label.c_str());
    if (ImGui::IsItemHovered() && !roiImport && !roiExport)
      ImGui::SetTooltip("%s", get(step, "parameters").dump(2).c_str());
    if(roiExport) {
      ImGui::PushID(static_cast<int>(i));
      ImGui::BeginDisabled(owner.documents_.empty() || owner.busy() || !actionAllowed("show_roi_export_record"));
      const bool review=ImGui::Button("Review export on active image...");
      observeControl("workflow.roi_export."+std::to_string(i)+".review");
      if(review)guard([&]{owner.action("show_roi_export_record",{{"record",step}});});
      ImGui::EndDisabled();ImGui::PopID();
    }
    if(roiImport) {
      ImGui::PushID(static_cast<int>(i));
      ImGui::BeginDisabled(owner.documents_.empty() || owner.busy() || !actionAllowed("show_roi_import_record"));
      const bool review=ImGui::Button("Review import on active image...");
      observeControl("workflow.roi_import."+std::to_string(i)+".review");
      if(review)guard([&]{owner.action("show_roi_import_record",{{"record",step}});});
      ImGui::EndDisabled();ImGui::PopID();
    }
    if(displayAnnotation) {
      ImGui::PushID(static_cast<int>(i));
      ImGui::BeginDisabled(owner.documents_.empty() || owner.busy() || !actionAllowed("replay_display_annotation"));
      const bool replay=ImGui::Button("Replay captured display annotation");observeControl("workflow.display."+std::to_string(i)+".replay");
      if(replay)guard([&]{owner.action("replay_display_annotation",{{"record",step}});});
      ImGui::EndDisabled();ImGui::PopID();
    }
    if(get(step,"schema")=="imagejxx.planar-annotation-command") {
      ImGui::PushID(static_cast<int>(i));
      ImGui::BeginDisabled(owner.documents_.empty() || owner.busy() || !actionAllowed("replay_planar_annotation"));
      const bool replay=ImGui::Button("Replay captured annotation");
      observeControl("workflow.planar."+std::to_string(i)+".replay");
      if(replay)guard([&]{owner.action("replay_planar_annotation",{{"record",step}});});
      const bool rebind=ImGui::Button("Rebind annotation...");
      observeControl("workflow.planar."+std::to_string(i)+".rebind");
      if(rebind)guard([&]{owner.action("show_planar_annotation_rebind",{{"record",step}});});
      ImGui::EndDisabled();ImGui::PopID();
    }
    if (get(step, "schema") == "imagejxx.saved-roi-geometry-command") {
      ImGui::PushID(static_cast<int>(i));
      ImGui::BeginDisabled(owner.documents_.empty() || owner.busy());
      const bool replay = ImGui::Button("Replay saved area");
      observeControl("workflow.saved_roi_geometry." + std::to_string(i) + ".replay");
      ImGui::SameLine();
      const bool rebind = ImGui::Button("Rebind saved area...");
      observeControl("workflow.saved_roi_geometry." + std::to_string(i) + ".rebind");
      if (replay) guard([&] {
        owner.replaySavedRoiGeometry(metadataObjectFromJson(step));
        cancelSavedRoiGeometry("workflow_replayed");
      });
      if (rebind) guard([&] { showSavedRoiRebind(metadataObjectFromJson(step)); });
      ImGui::EndDisabled(); ImGui::PopID();
    }
    if (get(step, "schema") == "imagejxx.auto-contrast-command") {
      ImGui::PushID(static_cast<int>(i));
      ImGui::BeginDisabled(owner.documents_.empty() || owner.busy());
      const bool replay = ImGui::Button("Replay Auto");
      observeControl("workflow.auto_contrast." + std::to_string(i) + ".replay");
      ImGui::SameLine();
      const bool rebind = ImGui::Button("Rebind Auto...");
      observeControl("workflow.auto_contrast." + std::to_string(i) + ".rebind");
      if (replay) guard([&] { owner.replayAutoContrast(metadataObjectFromJson(step)); });
      if (rebind) guard([&] {
        const auto record = metadataObjectFromJson(step);
        validate_auto_contrast_command_record(record, autoLimits());
        const auto destination = doc()->id;
        ask("Rebind Auto", {{"all_channels", false}, {"use_selection", true},
            {"unsigned16_range", preferenceState.settings.text("conversion_unsigned16_range")}},
            [this, record, destination](Json values) {
              const auto bits = string(values.at("unsigned16_range"));
              const AutoContrastRequest request{
                owner.controller_->auto_contrast_divisor(),
                boolean(values.at("all_channels")) ? AutoContrastScope::CompositeAll
                                                    : AutoContrastScope::ActiveChannel,
                boolean(values.at("use_selection")) ? AutoContrastRoiPolicy::ActiveSelection
                                                     : AutoContrastRoiPolicy::WholePlane,
                bits == "auto" ? 0 : std::stoi(bits)};
              owner.replayAutoContrast(record, request, destination);
            });
        for (auto &field : dialog.schema) {
          if (field["name"] == "all_channels") field["label"] = "Apply to all channels";
          else if (field["name"] == "use_selection") field["label"] = "Use area selection";
          else if (field["name"] == "unsigned16_range") {
            field["label"] = "Unsigned16 fallback range";
            field["choices"] = {"auto", "8", "10", "12", "14", "15", "16"};
          }
        }
      });
      ImGui::EndDisabled(); ImGui::PopID();
    }
    if(get(step,"schema")=="imagejxx.imagej-projection-command") {
      ImGui::PushID(static_cast<int>(i));
      ImGui::BeginDisabled(owner.documents_.empty() || !actionAllowed("replay_imagej_projection"));
      if(ImGui::Button("Replay projection"))guard([&]{owner.action("replay_imagej_projection",{{"record",step}});});
      observeControl("workflow.projection."+std::to_string(i)+".replay");ImGui::SameLine();
      if(ImGui::Button("Rebind projection..."))guard([&]{owner.action("show_imagej_projection",{{"record",step}});});
      observeControl("workflow.projection."+std::to_string(i)+".rebind");
      ImGui::EndDisabled();ImGui::PopID();continue;
    }
    if (get(step, "schema") == "imagejxx.axis-extraction-command") {
      const bool bilinear = get(step, "version") == 4 &&
                          get(step, "operation") == "axis_range_slicer_resize";
      if (bilinear) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::BeginDisabled(owner.documents_.empty() || owner.busy() || !actionAllowed("replay_slicer_resize"));
        const bool replay = ImGui::Button("Replay ImageJ bilinear");
        observeControl("workflow.slicer." + std::to_string(i) + ".replay");
        ImGui::SameLine();
        const bool rebind = ImGui::Button("Rebind ImageJ bilinear...");
        observeControl("workflow.slicer." + std::to_string(i) + ".rebind");
        if (replay) guard([&] { owner.action("replay_slicer_resize", {{"record", step}}); });
        if (rebind) guard([&] { owner.action("show_axis_range_slicer_resize", {{"record", step}}); });
        ImGui::EndDisabled(); ImGui::PopID();
        continue;
      }
      const bool spaced = get(step, "version") == 3 &&
                          get(step, "operation") == "axis_range_spacing";
      if (spaced) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::BeginDisabled(owner.documents_.empty() || owner.busy() || !actionAllowed("replay_axis_spacing"));
        const bool replay = ImGui::Button("Replay physical spacing");
        observeControl("workflow.spacing." + std::to_string(i) + ".replay");
        ImGui::SameLine();
        const bool rebind = ImGui::Button("Rebind physical spacing...");
        observeControl("workflow.spacing." + std::to_string(i) + ".rebind");
        if (replay) guard([&] { owner.action("replay_axis_spacing", {{"record", step}}); });
        if (rebind) guard([&] { owner.action("show_axis_range_spacing", {{"record", step}}); });
        ImGui::EndDisabled(); ImGui::PopID();
        continue;
      }
      const bool oriented = get(step, "version") == 2 &&
                            get(step, "operation") == "axis_range_orientation";
      const std::string control = oriented ? "workflow.orientation." : "workflow.extraction.";
      ImGui::PushID(static_cast<int>(i));
      ImGui::BeginDisabled(owner.documents_.empty() ||
                           !actionAllowed(oriented ? "replay_axis_orientation" : "replay_axis_extraction"));
      const bool replay = ImGui::Button(oriented ? "Replay orientation" : "Replay extraction");
      observeControl(control + std::to_string(i) + ".replay");
      ImGui::SameLine();
      const bool rebind = ImGui::Button(oriented ? "Rebind orientation..." : "Rebind extraction...");
      observeControl(control + std::to_string(i) + ".rebind");
      if (replay) guard([&] {
        if (oriented) owner.replayAxisOrientation(metadataObjectFromJson(step));
        else owner.replayAxisExtraction(metadataObjectFromJson(step));
      });
      if (rebind) guard([&] {
        const auto record = metadataObjectFromJson(step);
        if (oriented) validate_axis_orientation_command_record(record);
        else validate_axis_extraction_command_record(record);
        showAxisExtraction(owner.nativeSnapshot(),
            oriented ? AxisExtractionKind::OrientedRange
                : get(step, "operation") == "axis_range_reslice"
                    ? AxisExtractionKind::Range : AxisExtractionKind::Plane, record);
      });
      ImGui::EndDisabled();
      ImGui::PopID();
    }
    if (get(step, "operation") == "threshold" && step.contains("schema_version") &&
        (get(step, "profile") == "manual_raw_inclusive_v1" || get(step, "profile") == "manual_raw_inclusive_v2" ||
         get(step, "profile") == "manual_raw_inclusive_v3")) {
      ImGui::PushID(static_cast<int>(i));
      ImGui::BeginDisabled(owner.documents_.empty() || owner.busy() ||
                          !actionAllowed("replay_manual_threshold"));
      const bool replay = ImGui::Button("Replay captured threshold");
      observeControl("workflow.manual_threshold." + std::to_string(i) + ".replay");
      const bool rebind = ImGui::Button("Apply bounds to current plane / selection");
      observeControl("workflow.manual_threshold." + std::to_string(i) + ".rebind");
      if (replay) guard([&] {
        owner.action("replay_manual_threshold", {{"record", step}});
      });
      if (rebind) guard([&] {
        owner.action("replay_manual_threshold", {{"record", step},
            {"threshold_rebind", {{"scope", "current_plane"}, {"roi", "active_selection"}}}});
      });
      ImGui::EndDisabled(); ImGui::PopID();
    }
    if (get(step,"schema")=="imagejxx.z-subset-command") {
      ImGui::PushID(static_cast<int>(i));
      ImGui::BeginDisabled(owner.documents_.empty() || owner.busy() || !actionAllowed("replay_z_subset"));
      const bool replay=ImGui::Button("Replay captured Z subset");
      observeControl("workflow.z_subset."+std::to_string(i)+".replay");
      ImGui::SameLine();const bool rebind=ImGui::Button("Rebind Z subset...");
      observeControl("workflow.z_subset."+std::to_string(i)+".rebind");
      if(replay)guard([&]{owner.action("replay_z_subset",{{"record",step}});});
      if(rebind)guard([&]{owner.action("show_z_subset",{{"record",step}});});
      ImGui::EndDisabled();ImGui::PopID();
    }
    if(get(step,"schema")=="imagejxx.selection-enlargement-command"){
      ImGui::PushID(static_cast<int>(i));ImGui::BeginDisabled(owner.documents_.empty() || owner.busy() || !actionAllowed("replay_selection_enlargement"));
      if(ImGui::Button("Replay captured enlargement"))guard([&]{owner.action("replay_selection_enlargement",{{"record",step}});});
      observeControl("workflow.enlargement."+std::to_string(i)+".replay");
      if(ImGui::Button("Rebind distance to current selection"))guard([&]{owner.action("replay_selection_enlargement",{{"record",step},{"rebind",{{"distance",step.at("distance")},{"units",step.at("units")},{"geometry",step.at("version")==2?"full_region_v2":"canvas_v1"}}}});});
      observeControl("workflow.enlargement."+std::to_string(i)+".rebind");ImGui::EndDisabled();ImGui::PopID();
    }
    if (get(step, "schema") == "imagejxx.channel-display-command") {
      ImGui::PushID(static_cast<int>(i));
      ImGui::BeginDisabled(owner.documents_.empty() || owner.busy());
      const bool replay = ImGui::Button("Replay channel display");
      observeControl("workflow.channel_display." + std::to_string(i) + ".replay");
      ImGui::SameLine();
      const bool rebind = ImGui::Button("Rebind channel display...");
      observeControl("workflow.channel_display." + std::to_string(i) + ".rebind");
      if (replay) guard([&] { owner.replayChannelDisplay(metadataObjectFromJson(step)); });
      if (rebind) guard([&] {
        auto record = metadataObjectFromJson(step);
        validate_channel_display_command_record(record);
        channelDisplayRebind = std::move(record);
        channelDisplayRebindOwner = doc()->id;
        channelDisplayPanel.open();
        status = "Rebind channel display: choose the complete display state, then Apply or Reset";
      });
      ImGui::EndDisabled(); ImGui::PopID();
    }
    if (get(step, "schema") == "imagejxx.selection-creation-command") {
      ImGui::PushID(static_cast<int>(i));
      ImGui::BeginDisabled(owner.documents_.empty() || owner.busy() ||
          (doc()->scientific() && scientificRoiCreationOwner != doc()->id));
      const bool replay = ImGui::Button("Replay selection");
      observeControl("workflow.selection_creation." + std::to_string(i) + ".replay");
      ImGui::SameLine();
      const bool rebind = ImGui::Button("Rebind selection...");
      observeControl("workflow.selection_creation." + std::to_string(i) + ".rebind");
      if (replay) guard([&] { owner.replaySelectionCreation(metadataObjectFromJson(step)); });
      if (rebind) guard([&] {
        auto record = metadataObjectFromJson(step);
        validate_selection_creation_command_record(record, selectionCreationLimits());
        cancelCreations("rebind_started");
        creationRebind = std::move(record);
        creationRebindOwner = doc()->id;
        const auto operation = string(get(step, "operation"));
        creationMode = operation == "add" ? SelectionCreationOperation::Add
            : operation == "subtract" ? SelectionCreationOperation::Subtract
            : operation == "intersect" ? SelectionCreationOperation::Intersect
            : operation == "xor" ? SelectionCreationOperation::Xor
            : SelectionCreationOperation::Replace;
        tool = string(get(get(step, "candidate"), "kind"), "rectangle");
        if (doc()->view().orthogonal) doc()->setView({{"orthogonal", false}});
        status = "Rebind selection: choose a mode and draw a complete new area";
      });
      ImGui::EndDisabled(); ImGui::PopID();
    }
  }
  ImGui::EndDisabled();
  ImGui::End();
}
void Window::Impl::plots() {
  ImGui::Begin("Plot");
  if (plotY.empty())
    ImGui::TextWrapped("Use Histogram or draw a line and choose Line profile.");
  else {
    ImGui::TextUnformatted(plotTitle.c_str());
    auto [low, high] = std::minmax_element(plotY.begin(), plotY.end());
    float maximum = *high > *low ? *high : *low + 1;
    ImGui::PlotLines("##plot", plotY.data(), int(plotY.size()), 0, nullptr,
                     *low, maximum, {ImGui::GetContentRegionAvail().x, 140});
    ImGui::Text("X: %.4g to %.4g | Y: %.4g to %.4g", plotX.front(),
                plotX.back(), *low, *high);
    if (ImGui::Button("Export plot CSV"))
      file(
          "Export plot", true,
          [this](std::string p) {
            std::ostringstream stream;
            stream << "x,y\n";
            for (size_t i = 0; i < plotX.size(); ++i)
              stream << plotX[i] << ',' << plotY[i] << '\n';
            auto text = stream.str();
            writeBytes(p, {text.begin(), text.end()});
          },
          "plot.csv");
  }
  ImGui::End();
}
void Window::Impl::geometry(const Json &o, Pane &pane, ImU32 tint, bool cache,
                            const char *role) {
  auto draw = ImGui::GetWindowDrawList();
  const bool nativePath=get(o,"kind")=="path";
  if (o.contains(roi_style_attribute) || nativePath) {
    const auto rgba = ImGui::ColorConvertU32ToFloat4(tint);
    const RoiColor fallback{static_cast<unsigned char>(rgba.x * 255 + .5f),
                            static_cast<unsigned char>(rgba.y * 255 + .5f),
                            static_cast<unsigned char>(rgba.z * 255 + .5f),
                            static_cast<unsigned char>(rgba.w * 255 + .5f)};
    if(nativePath && pane.roiVisualCap!=rgbCap()){pane.roiVisuals.clear();pane.transientVisual.reset();pane.roiVisualCap=rgbCap();}
    auto found = pane.roiVisuals.find(&o);
    const std::array<float,2> pathScale{pane.zoom,pane.zoom*pane.aspectY};
    auto buildVisual=[&](int width,int height,const RoiVisual *replaced=nullptr) {
      if(!nativePath)return roiVisual(o,width,height,fallback);
      const auto cap=rgbCap();std::size_t retained=0;
      auto charge=[&](const RoiVisual &v){if(&v==replaced)return;const auto n=pathVisualBytes(v);if(n>cap-retained)throw std::invalid_argument("Root path visuals exceed processing memory");retained+=n;};
      for(const auto &[key,v]:pane.roiVisuals)charge(v);
      if(pane.transientVisual)charge(*pane.transientVisual);
      return pathVisual(selectionFromJson(o),width,height,pathScale,cap-retained,fallback);
    };
    if (!cache) {
      const std::array<int, 2> shape{pane.doc->shape().x,
                                     pane.doc->shape().y};
      if (!pane.transientVisual || pane.transientStyle != o ||
          pane.transientShape != shape || pane.transientFallback != fallback ||
          (nativePath && (pane.transientVisual->screenKey[2]!=pathScale[0] || pane.transientVisual->screenKey[3]!=pathScale[1]))) {
        auto next=buildVisual(shape[0],shape[1],pane.transientVisual?&*pane.transientVisual:nullptr);
        next.screenKey[2]=pathScale[0];next.screenKey[3]=pathScale[1];
        pane.transientVisual = std::move(next);
        pane.transientOutline.reset();
        pane.transientStyle = o;
        pane.transientShape = shape;
        pane.transientFallback = fallback;
        ++maskOutlineBuilds;
      }
    } else if (found == pane.roiVisuals.end() || (nativePath &&
        (found->second.screenKey[2]!=pathScale[0] || found->second.screenKey[3]!=pathScale[1]))) {
      auto next=buildVisual(pane.doc->shape().x,pane.doc->shape().y,found==pane.roiVisuals.end()?nullptr:&found->second);
      next.screenKey[2]=pathScale[0];next.screenKey[3]=pathScale[1];
      found=pane.roiVisuals.insert_or_assign(&o,std::move(next)).first;
      ++maskOutlineBuilds;
    }
    const auto &visual = cache ? found->second : *pane.transientVisual;
    drawRoiVisual(visual, draw, {pane.origin.x, pane.origin.y},
                  {pane.zoom, pane.zoom * pane.aspectY});
    auto summary = roiVisualJson(visual);
    summary["role"] = role;
    pane.styledLayers.push_back(std::move(summary));
    return;
  }
  const auto &points = get(o, "points");
  auto map = [&](const Json &p) {
    return ImVec2(pane.origin.x + float(number(p[0])) * pane.zoom,
                  pane.origin.y +
                      float(number(p[1])) * pane.zoom * pane.aspectY);
  };
  auto kind = string(get(o, "kind"));
  if (get(o, "color").is_string()) {
    const auto rgba = color(string(get(o, "color")));
    tint = IM_COL32(rgba[0], rgba[1], rgba[2], rgba[3]);
  }
  if (kind == "mask" || kind=="region") {
    auto found = pane.maskOutlines.find(&o);
    if (!cache) {
      const std::array<int, 2> shape{pane.doc->shape().x,
                                     pane.doc->shape().y};
      if (!pane.transientOutline || pane.transientStyle != o ||
          pane.transientShape != shape) {
        pane.transientOutline = selectionOutlineSpans(o, shape[0], shape[1]);
        pane.transientVisual.reset();
        pane.transientStyle = o;
        pane.transientShape = shape;
        ++maskOutlineBuilds;
      }
    } else if (found == pane.maskOutlines.end()) {
      auto spans = selectionOutlineSpans(o, pane.doc->shape().x,
                                         pane.doc->shape().y);
      found = pane.maskOutlines.emplace(&o, std::move(spans)).first;
      ++maskOutlineBuilds;
    }
    const auto &spans = cache ? found->second : *pane.transientOutline;
    const auto clipMin = draw->GetClipRectMin(),
               clipMax = draw->GetClipRectMax();
    const float firstRow =
        (clipMin.y - pane.origin.y) / (pane.zoom * pane.aspectY);
    auto first = std::lower_bound(
        spans.begin(), spans.end(), firstRow,
        [](const Rect &span, float row) { return span.y + 1 <= row; });
    for (auto it = first; it != spans.end(); ++it) {
      const auto &span = *it;
      ImVec2 start{pane.origin.x + span.x * pane.zoom,
                   pane.origin.y + span.y * pane.zoom * pane.aspectY};
      ImVec2 end{start.x + span.width * pane.zoom,
                 start.y + pane.zoom * pane.aspectY};
      if (start.y >= clipMax.y)
        break;
      if (end.x > clipMin.x && start.x < clipMax.x)
        draw->AddRectFilled(start, end, tint);
    }
    return;
  }
  if (points.empty())
    return;
  // Width is a positive finite native property. Cap only the rendering
  // extent at the viewport size before narrowing to float.
  const auto clipMin = draw->GetClipRectMin(), clipMax = draw->GetClipRectMax();
  const ImVec2 viewport{clipMax.x - clipMin.x, clipMax.y - clipMin.y};
  const float stroke =
      1.5f * float(std::min(number(get(o, "width"), 1),
                            2. * std::max({1.f, viewport.x, viewport.y})));
  if (kind == "rounded_rectangle" || kind == "rotated_rounded_rectangle") {
    const auto path = selectionFromJson(o).rounded_path();
    draw->PathClear();
    for (std::size_t at=0; at<path.size();) {
      const int op = int(path[at++]);
      if (op == 4) break;
      const auto a = map({path[at],path[at+1]}); at += 2;
      if (op == 0 || op == 1) draw->PathLineTo(a);
      else {
        const auto b = map({path[at],path[at+1]}), c = map({path[at+2],path[at+3]}); at += 4;
        draw->PathBezierCubicCurveTo(a,b,c);
      }
    }
    draw->PathStroke(tint, ImDrawFlags_Closed, stroke);
  } else if (kind == "rotated_rectangle" || kind == "rotated_ellipse") {
    const auto frame = *selectionFromJson(o).box_frame();
    std::vector<ImVec2> outline;
    if (kind == "rotated_rectangle") {
      for (const auto &p : std::array<std::array<double, 2>, 4>{
               {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}})
        outline.push_back(map(frame.image_point(p)));
    } else
      for (int i = 0; i < 96; ++i) {
        const double angle = 2 * std::acos(-1.) * i / 96;
        outline.push_back(
            map(frame.image_point({std::cos(angle), std::sin(angle)})));
      }
    draw->AddPolyline(outline.data(), int(outline.size()), tint,
                      ImDrawFlags_Closed, stroke);
  } else if ((kind == "rectangle" || kind == "ellipse") && points.size() >= 2) {
    auto a = map(points[0]), b = map(points[1]);
    if (a.x > b.x)
      std::swap(a.x, b.x);
    if (a.y > b.y)
      std::swap(a.y, b.y);
    if (kind == "rectangle")
      draw->AddRect(a, b, tint, 0, 0, stroke);
    else
      draw->AddEllipse({(a.x + b.x) / 2, (a.y + b.y) / 2},
                       {(b.x - a.x) / 2, (b.y - a.y) / 2}, tint, 0, 0, stroke);
  } else if (kind == "point" || kind == "multipoint")
    for (const auto &p : points) {
      auto q = map(p);
      draw->AddLine({q.x - 5, q.y}, {q.x + 5, q.y}, tint, stroke);
      draw->AddLine({q.x, q.y - 5}, {q.x, q.y + 5}, tint, stroke);
    }
  else {
    for (size_t i = 1; i < points.size(); ++i)
      draw->AddLine(map(points[i - 1]), map(points[i]), tint, stroke);
    if ((kind == "polygon" || kind == "freehand") && points.size() > 2)
      draw->AddLine(map(points.back()), map(points.front()), tint, stroke);
  }
}
void Window::Impl::canvas(Pane &pane) {
  pane.ordinaryImportLayers=Json::array();pane.ordinaryImportActive=pane.ordinaryImportReplace=false;pane.ordinaryImportError.clear();
  pane.styledLayers = Json::array();
  pane.savedRoiLayers = Json::array();
  pane.savedRoiHandles = Json::array();
  pane.selectionHandles = Json::array();
  pane.savedRoiInput = Json::object();
  pane.overlayLayers = Json::array();
  pane.viewportSeen=true;
  auto d = pane.doc;
  const bool readOnly = d->scientific();
  const bool creationEnabled = !readOnly || scientificRoiCreationOwner == d->id;
  const bool nativePathTool=this->tool=="path" || (this->tool=="edit_roi" && d->nativeSnapshot().data().selection && d->nativeSnapshot().data().selection->is_path());
  const std::string tool = (roiImportPanel.visible() || roiExportPanel.visible() || displayAnnotationPanel.visible()) || (readOnly && !nativePathTool && !(creationEnabled && creationTool(this->tool)) && this->tool != "zoom") ? "hand" : this->tool;
  if (readOnly && !nativePathTool && !(creationEnabled && creationTool(tool))) {
    cancelCreation(pane, "scientific_read_only");
    pane.points = Json::array();
    pane.dragging = false;
    pane.selectionGesture.reset();
  }
  d->normalizeView();
  const auto cal = d->spatialCalibration();
  const auto spacing = d->displaySpacing();
  pane.aspectY = float(spacing[1] / spacing[0]);
  if (!std::isfinite(pane.aspectY) || pane.aspectY <= 0)
    throw std::invalid_argument("Unsupported display aspect ratio");
  bool orthogonal = d->view().orthogonal;
  pane.ordinaryCanvasVisible = false;
  pane.ordinaryFrameSource.clear();
  if (ImGui::Checkbox("Linked XY/XZ/YZ views", &orthogonal))
  {
    if (savedRoiEdit.active()) cancelSavedRoiGeometry("view_changed");
    d->setView({{"orthogonal", orthogonal}});
  }
  pane.toggleId = ImGui::GetItemID();
  const auto toggleMin = ImGui::GetItemRectMin(), toggleMax = ImGui::GetItemRectMax();
  pane.togglePoint = {(toggleMin.x + toggleMax.x) * .5f, (toggleMin.y + toggleMax.y) * .5f};
  if (d->view().orthogonal) {
    d->admitView(true);
    cancelCreation(pane, "orthogonal_view");
    creationAdmission = "orthogonal_view";
    // Linked-plane inputs can change Z inside draw. Borrow the installed
    // snapshot here; its target is checked after all inputs, before extraction.
    const auto preview = planeOperationPreview(*d, false);
    const auto previewGeneration = preview ? operationPreviewGeneration(*d) : 0;
    const auto *previewTarget = preview ? operationPreviewTarget(*d) : nullptr;
    const bool gesture = pane.selectionGesture.has_value() || pane.dragging;
    const bool requestFocus = !pane.orthogonalWasVisible;
    auto &context = *ImGui::GetCurrentContext();
    auto &io = ImGui::GetIO();
    std::set<ImGuiID> canvasIds;
    for (const auto &row : pane.orthogonal.diagnostics()) {
      const auto id = get(row, "canvas_id").get<ImGuiID>();
      if (id)
        canvasIds.insert(id);
    }
    std::string blocked;
    if (owner.controller_->active_document_id() != d->id) blocked = "inactive_document";
    else if (owner.busy()) blocked = "busy";
    else if (context.CurrentItemFlags & ImGuiItemFlags_Disabled) blocked = "disabled";
    else if (io.WantTextInput) blocked = "text_input";
    else if (dialog.open || pendingClose || (selectionPanel.visible() && !selectionPanel.geometry_only()) || gaussianPanel.visible() || rankPanel.visible() || planarAnnotationRebind.visible() || (roiImportPanel.visible() || roiExportPanel.visible() || displayAnnotationPanel.visible()) || (channelDisplayPanel.inputActive() || contrastPanel.inputActive())) blocked = "dialog";
    else if (browser.open) blocked = "browser";
    else if (settingsPanel.visible()) blocked = "preferences";
    else if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) blocked = "popup";
    else if (gesture) blocked = "gesture";
    else if (context.ActiveId != 0 && !canvasIds.count(context.ActiveId)) blocked = "other_widget";
    else if (io.KeyCtrl || io.KeyAlt || io.KeySuper) blocked = "reserved_modifier";
    if (blocked.empty()) {
      for (auto key : {ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_UpArrow,
                       ImGuiKey_DownArrow, ImGuiKey_PageUp, ImGuiKey_PageDown, ImGuiKey_Escape}) {
        const auto keyOwner = ImGui::GetKeyOwner(key);
        if (ImGui::GetKeyData(key)->Down && keyOwner != ImGuiKeyOwner_NoOwner &&
            !canvasIds.count(keyOwner)) {
          blocked = "key_owner";
          break;
        }
      }
    }
    if (requestFocus && blocked.empty())
      ImGui::FocusWindow(ImGui::GetCurrentWindow());
    if (blocked.empty() && !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
      blocked = "unfocused_window";
    pane.keyboardAdmission = blocked.empty() ? "admitted" : blocked;
    OrthogonalInputContext input;
    if(preview && contrastPanel.rgbPreview(d->nativeSnapshot())){input.preview_display_range=std::array<double,2>{0,255};input.preview_targets=contrastPanel.rgbPreviewTargets(d->nativeSnapshot());}
    else if (preview && !shadowDemo.active())
      input.preview_display_effects = [this](const DocumentSnapshot &current, PlanePosition candidate) {
        return gaussianJobs.preview_display_effects(current, candidate);
      };
    input.active_document = owner.controller_->active_document_id() == d->id;
    input.input_enabled = !owner.busy() && !(context.CurrentItemFlags & ImGuiItemFlags_Disabled);
    input.keyboard_admitted = blocked.empty();
    // The planar editor owns its modifier contract: legacy tools still reject
    // Ctrl; native path Bounds uses it for from-center resize. Navigation keeps
    // its stricter keyboard admission above.
    input.annotation_admitted = blocked.empty() ||
        (blocked == "reserved_modifier" && !io.KeySuper && (io.KeyAlt != io.KeyCtrl));
    input.annotation_gui_scale=guiScale;
    input.annotation_max_working_bytes = static_cast<std::size_t>(
        preferenceState.settings.integer("processing_memory_mb"))*1024*1024;
    if(!input.annotation_max_working_bytes)input.annotation_max_working_bytes=256*1048576;
    input.display_annotations=[this,&pane,d](const PlanarCanvas &canvas,ImDrawList *draw) {
      auto cap=static_cast<std::size_t>(preferenceState.settings.integer("processing_memory_mb"))*1024*1024;if(!cap)cap=256*1048576;
      const auto source=d->nativeSnapshot();const auto preview=roiPreview(source,cap);
      auto result=pane.displayRenderer.draw(source,canvas,draw,gl,showRois && !(preview && preview->replaces_display()),cap,headlessDiagnostics);
      const auto staged=preview?preview->display_preview(source):std::optional<DocumentSnapshot>{};
      if(staged) {auto layers=pane.importDisplayRenderer.draw(*staged,canvas,draw,gl,true,cap,headlessDiagnostics);
        for(auto &layer:layers){layer["preview"]=true;result.push_back(std::move(layer));}}
      else pane.importDisplayRenderer.clear();return result;
    };
    input.planar_publish=[this](const ResolvedPlanarAnnotationCommand &command){publishPlanarAnnotation(command);};
    input.import_preview=roiPreview(d->nativeSnapshot(),input.annotation_max_working_bytes);
    input.import_preview_diagnostics=headlessDiagnostics;
    input.import_preview_error=[this](const std::string &error){roiPreviewError(error);};
    if(!d->nativeSnapshot().data().dataset.has_eager_planes() && !rankProbe){input.viewport_jobs=&viewJobs();input.viewport_jobs->cancelView(d->id,0);pane.sampledSurface.reset();pane.sampleMemory.reset();}
    input.request_xy_focus = requestFocus;
    pane.selectionGesture.reset();
    auto bg = color(preferenceState.settings.text("canvas_background"));
    ImGui::PushStyleColor(
        ImGuiCol_ChildBg,
        ImVec4(bg[0] / 255.f, bg[1] / 255.f, bg[2] / 255.f, bg[3] / 255.f));
    const auto dispatch=[&]{
      struct PopCanvasStyle {~PopCanvasStyle(){ImGui::PopStyleColor();}} pop;
    auto savedHooks = savedRoiHooks(pane);
    pane.orthogonal.setRenderMemory(renderMemory);
    return pane.orthogonal.draw(
        *d, gl, showRois,
        preferenceState.settings.text("interpolation") == "linear", status, input,
        preview && previewTarget ? &*preview : nullptr, previewGeneration,
        captureGaussianReadback(), previewTarget, channelLimits(),
        savedHooks.edit ? &savedHooks : nullptr);

    }();
    pane.orthogonalWasVisible = true;
    if (!dispatch.escape_requested)
      return;
    d->setView({{"orthogonal", false}});
    pane.orthogonal.resetInput();
    pane.orthogonalWasVisible = false;
    pane.requestOrdinaryFocus = true;
    pane.keyboardAdmission = "ordinary_canvas";
  } else {
    if (pane.orthogonalWasVisible)
      pane.orthogonal.resetInput();
    pane.orthogonalWasVisible = false;
    pane.keyboardAdmission = "ordinary_canvas";
  }
  const auto currentPosition = d->position();
  if (pane.outlineRevision != d->revision() ||
      pane.outlineT != currentPosition.t || pane.outlineC != currentPosition.c) {
    pane.maskOutlines.clear();
    pane.roiVisuals.clear();
    pane.outlineRevision = d->revision();
    pane.outlineT = currentPosition.t;
    pane.outlineC = currentPosition.c;
  }
  if(viewportJobs)for(int view=1;view<=3;++view)viewportJobs->cancelView(d->id,view);
  // Re-evaluate after a possible linked-view Escape/navigation handoff.
  const auto preview = planeOperationPreview(*d);
  // Freeze source/position before input publication. Geometry is metadata-only;
  // hover/picker sampling acquires a retained plane only when a pixel is needed.
  const auto canvasSnapshot=d->nativeSnapshot();
  const auto &canvasDataset=preview?preview->data().dataset:canvasSnapshot.data().dataset;
  const auto sizes=canvasDataset.shape();
  struct Geometry {int width,height,channels;PixelType type;};
  const Geometry f{sizes.x,sizes.y,sizes.s,canvasDataset.pixel_type()};
  std::shared_ptr<const Image> sampledPixels;
  auto canvasPixels=[&]() -> const Image & {
    if(!sampledPixels){
      if(preferenceState.settings.boolean("async_viewport") && !canvasDataset.has_eager_planes() && !rankProbe){sampledPixels=pane.sampledSurface;if(!sampledPixels)throw std::logic_error("Viewport sample unavailable");}
      else if(preview || canvasDataset.has_eager_planes())sampledPixels=canvasDataset.plane(currentPosition);
      else {d->admitView();sampledPixels=d->viewCache_->read(d->viewSlot_,canvasDataset,currentPosition);}
    }
    return *sampledPixels;
  };
  if (captureGaussianReadback())
    pane.ordinaryFrameSource = {
        {"frame_source", preview ? "preview" : "live"},
        {"preview_generation", preview
             ? operationPreviewGeneration(*d) : 0},
        {"source_document_id", d->id},
        {"current_tcz", {currentPosition.t, currentPosition.c, currentPosition.z}}};
  auto refreshSelection = [&](bool preserveVertex) {
    const auto source = d->nativeSnapshot();
    const auto shape = source.data().dataset.shape();
    const bool path=source.data().selection && source.data().selection->is_path();
    pane.selectionEditorCap=path?selectionCreationLimits().max_working_bytes:0;
    pane.selectionEditError.clear();
    try {
      if (source.data().selection) {
        SelectionEditLimits editLimits;
        if(path)editLimits.max_working_bytes=pane.selectionEditorCap;
        pane.selectionEditor = std::make_unique<SelectionEditor>(
            *source.data().selection, shape.x, shape.y,editLimits);
      } else pane.selectionEditor.reset();
    } catch(const std::exception &error) {
      if(!path)throw;
      // Display preparation must not reopen an acknowledged edit-refusal modal.
      // Keep the source immutable; retry admission when its budget changes.
      pane.selectionEditor.reset();pane.selectionGesture.reset();
      pane.selectionEditError=error.what();
    }
    pane.selectionSource = source;
    pane.selectionRevision = source.revisions().selection;
    pane.selectionShape = {shape.y, shape.x};
    if (!preserveVertex || !pane.selectionEditor ||
        (pane.selectionVertex &&
         *pane.selectionVertex >= selectionControlPointCount(pane.selectionEditor->selection())))
      pane.selectionVertex.reset();
  };
  if (!pane.selectionSource ||
      !pane.selectionSource->revisions().matches(d->revisions(), Change::Data) ||
      (!pane.selectionEditor && !pane.selectionEditError.empty() &&
       pane.selectionEditorCap!=selectionCreationLimits().max_working_bytes))
    refreshSelection(false);
  if (pane.selectionGesture &&
      (tool != "edit_roi" || owner.busy() ||
       !pane.selectionGesture->source.revisions().matches(d->revisions(),
                                                          Change::Data)))
    pane.selectionGesture.reset();
  auto commitSelection = [&](const Selection &value,
                             const DocumentSnapshot &source) {
    if (source.data().selection && value == *source.data().selection)
      return;
    if(value.is_path() && pathTransform)publishAuthoredPath(source,value);
    else {auto edit = source.begin_edit(Change::Data);edit.select(value);d->commitNative(edit);}
    refreshSelection(true);
  };
  auto available = ImGui::GetContentRegionAvail();
  available.y -= d->shape().z > 1 ? 36 : 0;
  available.x = std::max(32.f, available.x);
  available.y = std::max(32.f, available.y);
  if (pane.requestOrdinaryFocus)
    ImGui::SetKeyboardFocusHere();
  ImGui::InvisibleButton("canvas", available,
                         ImGuiButtonFlags_MouseButtonLeft |
                             ImGuiButtonFlags_MouseButtonRight |
                             ImGuiButtonFlags_MouseButtonMiddle |
                             ImGuiButtonFlags_EnableNav);
  pane.ordinaryCanvasId = ImGui::GetItemID();
  pane.ordinaryCanvasVisible = ImGui::IsItemVisible();
  const auto canvasMin = ImGui::GetItemRectMin(), canvasMax = ImGui::GetItemRectMax();
  pane.ordinaryCanvasPoint = {(canvasMin.x + canvasMax.x) * .5f, (canvasMin.y + canvasMax.y) * .5f};
  if (pane.requestOrdinaryFocus && pane.ordinaryCanvasVisible) {
    ImGui::SetFocusID(pane.ordinaryCanvasId, ImGui::GetCurrentWindow());
    ImGui::FocusWindow(ImGui::GetCurrentWindow());
    pane.requestOrdinaryFocus = false;
  }
  bool hover = ImGui::IsItemHovered();
  auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
  pane.extent = available;
  const auto viewZoom =
      d->view().linkedZoom ? d->view().zoom : d->view().planeZoom[0];
  if (viewZoom == 0) {
    pane.zoom = std::clamp(std::min(available.x / f.width,
                                    available.y / (f.height * pane.aspectY)) *
                               .96f,
                           .01f, 32.f);
  } else
    pane.zoom = float(viewZoom * spacing[0]);
  if (!std::isfinite(pane.zoom) || pane.zoom <= 0)
    throw std::invalid_argument("Unsupported image display zoom");
  pane.pan = {float(d->view().pan[0][0]), float(d->view().pan[0][1])};
  pane.origin = {a.x + (available.x - f.width * pane.zoom) / 2 + pane.pan.x,
                 a.y + (available.y - f.height * pane.zoom * pane.aspectY) / 2 +
                     pane.pan.y};
  for (float value :
       {pane.origin.x, pane.origin.y, pane.origin.x + f.width * pane.zoom,
        pane.origin.y + f.height * pane.zoom * pane.aspectY})
    if (!std::isfinite(value))
      throw std::invalid_argument("Unsupported image display extent");
  auto &io = ImGui::GetIO();
  const auto &context = *ImGui::GetCurrentContext();
  const bool savedMode = savedRoiEdit.active() && savedRoiEdit.source().id() == d->id;
  if (savedMode) {
    const SavedRoiCanvasTransform transform{
        {pane.origin.x, pane.origin.y}, {pane.extent.x, pane.extent.y},
        {pane.zoom, pane.zoom * pane.aspectY}, pane.ordinaryCanvasId,
        SavedRoiCanvasKind::OrdinaryXY};
    guard([&] { savedRoiOrdinaryInput(pane, transform, hover); });
  }
  std::string creationBlocked;
  if (readOnly && !creationEnabled) creationBlocked = "scientific_read_only";
  else if (owner.busy()) creationBlocked = "busy";
  else if (owner.controller_->active_document_id() != d->id) creationBlocked = "inactive_document";
  else if (io.AppFocusLost) creationBlocked = "focus_lost";
  else if (browser.open) creationBlocked = "browser";
  else if (dialog.open || pendingClose || !lastError.empty() ||
           ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
    creationBlocked = "popup";
  else if (io.WantTextInput) creationBlocked = "text_input";
  else if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
    creationBlocked = "focus_lost";
  else if (context.ActiveId && context.ActiveId != pane.ordinaryCanvasId)
    creationBlocked = "other_widget";
  if (pane.creation) {
    const auto &draft = *pane.creation;
    if (!draft.source.same_document(d->nativeSnapshot()) ||
        !draft.source.revisions().matches(d->revisions(), Change::Data))
      cancelCreation(pane, "source_changed");
    else if (draft.position.t != currentPosition.t || draft.position.c != currentPosition.c ||
             draft.position.z != currentPosition.z)
      cancelCreation(pane, "position_changed");
    else if (draft.tool != this->tool) cancelCreation(pane, "tool_changed");
    else if (draft.origin.x != pane.origin.x || draft.origin.y != pane.origin.y ||
             draft.extent.x != pane.extent.x || draft.extent.y != pane.extent.y ||
             draft.zoom != pane.zoom || draft.aspect != pane.aspectY ||
             draft.canvasId != pane.ordinaryCanvasId)
      cancelCreation(pane, "transform_changed");
    else if (!creationBlocked.empty()) cancelCreation(pane, creationBlocked.c_str());
    else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) cancelCreation(pane, "escape");
    else if (draft.dragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
             !ImGui::IsMouseReleased(ImGuiMouseButton_Left))
      cancelCreation(pane, "capture_lost");
  }
  creationAdmission = !creationBlocked.empty() ? creationBlocked
                      : creationTool(tool) ? "admitted" : "other_tool";
  auto draw = ImGui::GetWindowDrawList();
  auto canvasColor = color(preferenceState.settings.text("canvas_background"));
  draw->AddRectFilled(
      a, b,
      IM_COL32(canvasColor[0], canvasColor[1], canvasColor[2], canvasColor[3]));
  draw->PushClipRect(a, b, true);
  const bool viewportReady=refreshTexture(pane);
  if(viewportReady)draw->AddImage(static_cast<ImTextureID>(gl ? pane.texture : 1), pane.origin,
                 {pane.origin.x + f.width * pane.zoom,
                  pane.origin.y + f.height * pane.zoom * pane.aspectY});
  else draw->AddText(a,ImGui::GetColorU32(ImGuiCol_Text),"Loading view...");
  {
    const auto source=d->nativeSnapshot();
    auto cap=static_cast<std::size_t>(preferenceState.settings.integer("processing_memory_mb"))*1024*1024;
    if(!cap)cap=256*1048576;
    const auto preview=roiPreview(source,cap);
    pane.ordinaryImportActive=bool(preview);pane.ordinaryImportReplace=preview && preview->replaces();
    const PlanarCanvas canvas{Plane::XY,currentPosition.z,f.width,f.height,pane.ordinaryCanvasId,
        {pane.origin.x,pane.origin.y},{pane.zoom,pane.zoom*pane.aspectY},{pane.extent.x,pane.extent.y}};
    try {
      pane.nativePlanar.begin_draw(*d,cap);
      pane.nativePlanar.draw(*d,canvas,draw,showRois && !pane.ordinaryImportReplace,false);
      if(preview)pane.ordinaryImportLayers=preview->draw(source,canvas,draw,headlessDiagnostics);
      pane.displayLayers=pane.displayRenderer.draw(source,canvas,draw,gl,showRois && !(preview && preview->replaces_display()),cap,headlessDiagnostics);
      const auto staged=preview?preview->display_preview(source):std::optional<DocumentSnapshot>{};
      if(staged) {auto layers=pane.importDisplayRenderer.draw(*staged,canvas,draw,gl,true,cap,headlessDiagnostics);
        for(auto &layer:layers){layer["preview"]=true;pane.displayLayers.push_back(std::move(layer));}}
      else pane.importDisplayRenderer.clear();
    }catch(const std::exception &error) {
      pane.ordinaryImportError=error.what();if(preview)roiPreviewError(pane.ordinaryImportError);
    }
  }
  if (rankProbe) {
    rankConsumed = pane.rankTexture;
    rankConsumed["frame"] = rankFrame;
  }
  // All annotations share one ordered view path when a styled overlay needs
  // device-space coverage. Clip to the displayed image as the raster path did.
  draw->PushClipRect(pane.origin,
                     {pane.origin.x + f.width * pane.zoom,
                      pane.origin.y + f.height * pane.zoom * pane.aspectY},
                     true);
  for (const auto &overlay : pane.overlays) {
    drawOverlayVisual(overlay, draw, {pane.origin.x, pane.origin.y},
                      {pane.zoom, pane.zoom * pane.aspectY});
    auto summary = overlayVisualJson(overlay);
    pane.overlayLayers.push_back(summary);
    if (overlay.styled)
      pane.styledLayers.push_back(std::move(summary));
  }
  draw->PopClipRect();
  if (showRois) {
    const auto visible = d->visibleRoiIndices();
    const auto &rois = get(d->state().metadata, "rois");
    const auto annotationSource = d->nativeSnapshot();
    const auto annotationRevisions = annotationSource.revisions();
    for (auto index : visible) {
      const bool draft = savedRoiEdit.active() && savedRoiMatches(d->nativeSnapshot()) &&
          savedRoiEdit.row_index() == index && savedRoiEdit.candidate();
      const auto before = pane.styledLayers.size();
      const auto draftGeometry = draft ? selectionJson(*savedRoiEdit.candidate()) : Json();
      const auto &selectedGeometry = draft ? draftGeometry : get(rois[index], "selection");
      geometry(selectedGeometry, pane, IM_COL32(64, 210, 240, 180), !draft, "stored_roi");
      if (headlessDiagnostics)
        pane.savedRoiLayers.push_back({{"row_index", index}, {"preview", draft},
            {"document_id", annotationSource.id()},
            {"data_identity", annotationSource.data_identity().value_or(0)},
            {"metadata_revision", annotationRevisions.metadata},
            {"selection_revision", annotationRevisions.selection},
            {"tcz", {currentPosition.t, currentPosition.c, currentPosition.z}},
            {"geometry", selectedGeometry}, {"drawn", true}, {"frame", ImGui::GetFrameCount()},
            {"canvas_id", pane.ordinaryCanvasId}, {"origin", {pane.origin.x, pane.origin.y}},
            {"scale", {pane.zoom, pane.zoom * pane.aspectY}}});
      for (auto j = before; j < pane.styledLayers.size(); ++j) {
        pane.styledLayers[j]["saved_row_index"] = index;
        pane.styledLayers[j]["preview"] = draft;
      }
    }
  }
  const PathAuthoringCanvas pathCanvas{pane.ordinaryCanvasId,0,currentPosition.z,f.width,f.height,
      {pane.origin.x,pane.origin.y},{pane.zoom,pane.zoom*pane.aspectY},{canvasMin.x,canvasMin.y},{canvasMax.x,canvasMax.y}};
  if(tool=="path" && !savedMode) {
    auto style=Selection("rectangle");style.width=preferenceState.settings.number("line_width");
    guard([&]{pathAuthoring.pointer(d->nativeSnapshot(),pathCanvas,!owner.busy(),hover,style,selectionCreationLimits().max_working_bytes);
      pathAuthoring.draw(pathCanvas,draw,selectionCreationLimits().max_working_bytes);});
  } else if(pathAuthoring.active())pathAuthoring.cancel("tool_changed");
  const auto &selected = get(d->state().metadata, "selection");
  if (pane.creation && pane.creation->preview)
    geometry(pane.creation->previewGeometry, pane,
             IM_COL32(255, 213, 75, 255), false, "creation_preview");
  else if (selectionPanel.visible() && !selectionPanel.geometry_only() && selectionPanel.matches(d->nativeSnapshot()) &&
      selectionPanel.preview())
    geometry(selectionJson(*selectionPanel.preview()), pane,
             IM_COL32(255, 213, 75, 255), false);
  else if (pane.selectionGesture && pane.selectionGesture->preview)
    geometry(selectionJson(*pane.selectionGesture->preview), pane,
             IM_COL32(255, 213, 75, 255), false);
  else if (!selected.empty())
    geometry(selected, pane, IM_COL32(255, 213, 75, 255));
  drawSavedRoiHandles(pane, draw);
  if (!savedMode && tool == "edit_roi" && pane.selectionEditor) {
    pane.selectionEditError.clear();
    try {
    const SelectionEditor *editor = pane.selectionEditor.get();
    std::optional<SelectionEditor> preview;
    SelectionEditLimits editLimits;
    if(editor->selection().is_path())editLimits.max_working_bytes=selectionCreationLimits().max_working_bytes;
    if (pane.selectionGesture && pane.selectionGesture->preview) {
      preview.emplace(*pane.selectionGesture->preview, f.width, f.height,editLimits);
      editor = &*preview;
    }
    auto handles = editor->selection().is_path()?editor->handles(pathTransform?SelectionHandleMode::Bounds:SelectionHandleMode::ControlPoints,editLimits):editor->handles();
    if (!(editor->selection().is_path() && pathTransform))if (const auto rotation = editor->rotation_handle(
            24 * guiScale, {pane.zoom, pane.zoom * pane.aspectY}))
      handles.push_back(*rotation);
    for (const auto &handle : handles) {
      const ImVec2 p{pane.origin.x + float(handle.position[0]) * pane.zoom,
                     pane.origin.y +
                         float(handle.position[1]) * pane.zoom * pane.aspectY};
      const float radius = 3 * guiScale;
      pane.selectionHandles.push_back({{"kind",int(handle.kind)},{"index",handle.index},{"position",handle.position},{"point",{p.x,p.y}},
          {"gui_scale",guiScale},{"radius_device",radius+(handle.kind==SelectionHandleKind::Rotate?1:0)},{"shape",handle.kind==SelectionHandleKind::Rotate?"circle":"square"},{"bounds_hit_tolerance_device",6*guiScale}});
      if (handle.kind == SelectionHandleKind::Rotate) {
        draw->AddCircleFilled(p, radius + 1, IM_COL32(20, 20, 20, 255));
        draw->AddCircle(p, radius + 1, IM_COL32(255, 213, 75, 255));
        continue;
      }
      const auto tint = pane.selectionVertex &&
                                handle.kind == SelectionHandleKind::Vertex &&
                                handle.index == *pane.selectionVertex
                            ? IM_COL32(255, 255, 255, 255)
                            : IM_COL32(255, 213, 75, 255);
      draw->AddRectFilled({p.x - radius, p.y - radius},
                          {p.x + radius, p.y + radius},
                          IM_COL32(20, 20, 20, 255));
      draw->AddRect({p.x - radius, p.y - radius}, {p.x + radius, p.y + radius},
                    tint);
    }
    } catch(const std::exception &error) {
      if(!pane.selectionEditor->selection().is_path())throw;
      pane.selectionHandles=Json::array();pane.selectionEditError=error.what();
    }
  }
  if (!pane.points.empty())
    geometry(selection(tool, pane.points), pane, IM_COL32(255, 255, 255, 255));
  if (pane.creation) {
    const auto &draft = *pane.creation;
    if (draft.candidate)
      geometry(draft.candidateGeometry, pane, IM_COL32(255, 255, 255, 255),
               true, "creation_candidate");
    else for (std::size_t i = 1; i < draft.points.size(); ++i) {
      auto map = [&](const std::array<double, 2> &p) {
        return ImVec2(pane.origin.x + float(p[0]) * pane.zoom,
                      pane.origin.y + float(p[1]) * pane.zoom * pane.aspectY);
      };
      draw->AddLine(map(draft.points[i - 1]), map(draft.points[i]),
                    IM_COL32(255, 255, 255, 255));
    }
  }
  draw->PopClipRect();
  auto mouse = io.MousePos;
  double x = (mouse.x - pane.origin.x) / pane.zoom,
         y = (mouse.y - pane.origin.y) / (pane.zoom * pane.aspectY);
  bool inside = x >= 0 && y >= 0 && x < f.width && y < f.height;
  double snappedX = x, snappedY = y;
  if (tool == "rounded_rectangle" || tool == "rectangle" || tool == "ellipse") {
    snappedX = std::floor(x + .5);
    snappedY = std::floor(y + .5);
  }
  auto point = Json{std::clamp(snappedX, 0.0, double(f.width)),
                    std::clamp(snappedY, 0.0, double(f.height))};
  const std::array<double, 2> position{{x, y}};
  std::optional<SelectionHandle> selectionHit;
  if (!savedMode && tool == "edit_roi" && pane.selectionEditor && pane.selectionEditError.empty() && hover && !owner.busy() &&
      pane.selectionShape == std::array<int, 2>{{f.height, f.width}}) {
    std::array<double, 2> tolerance{
        {6 * guiScale / pane.zoom, 6 * guiScale / (pane.zoom * pane.aspectY)}};
    if (pane.selectionEditor->selection().kind == "rectangle" ||
        pane.selectionEditor->selection().kind == "ellipse") {
      const auto bounds = pane.selectionEditor->bounds();
      tolerance[0] =
          std::min(tolerance[0], std::max((bounds[2] - bounds[0]) / 4,
                                          1.5 * guiScale / pane.zoom));
      tolerance[1] = std::min(
          tolerance[1], std::max((bounds[3] - bounds[1]) / 4,
                                 1.5 * guiScale / (pane.zoom * pane.aspectY)));
    }
    try {
      const auto rotation=pane.selectionEditor->selection().is_path() && pathTransform?std::optional<SelectionHandle>{}:
          pane.selectionEditor->rotation_handle(24 * guiScale,{pane.zoom,pane.zoom*pane.aspectY});
      selectionHit = pane.selectionEditor->selection().is_path()
          ? pane.selectionEditor->hit_test(position,tolerance,rotation,
              pathTransform?SelectionHandleMode::Bounds:SelectionHandleMode::ControlPoints,
              SelectionEditLimits{16777216,65536,selectionCreationLimits().max_working_bytes})
          : pane.selectionEditor->hit_test(position,tolerance,rotation);
    } catch(const std::exception &error) {
      if(!pane.selectionEditor->selection().is_path())throw;
      pane.selectionEditError=error.what();selectionHit.reset();
    }
    if (selectionHit) {
      if (selectionHit->kind == SelectionHandleKind::Move)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
      else if (selectionHit->kind == SelectionHandleKind::Vertex ||
               selectionHit->kind == SelectionHandleKind::Rotate)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
      else {
        const auto i = selectionHit->index;
        ImGui::SetMouseCursor(i == 1 || i == 5   ? ImGuiMouseCursor_ResizeNS
                              : i == 3 || i == 7 ? ImGuiMouseCursor_ResizeEW
                              : i == 0 || i == 4 ? ImGuiMouseCursor_ResizeNWSE
                                                 : ImGuiMouseCursor_ResizeNESW);
      }
    }
  }
  pane.selectionEditNotice=pane.selectionEditError.empty()?std::string():
      "Path editing: "+pane.selectionEditError+". Adjust Processing memory in Preferences.";
  pane.savedRoiHandleNotice=pane.savedRoiHandleError.empty()?std::string():
      "Saved path editing: "+pane.savedRoiHandleError+". Adjust Processing memory in Preferences.";
  const auto &editNotice=savedMode?pane.savedRoiHandleNotice:pane.selectionEditNotice;
  if(!editNotice.empty())draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),
      {canvasMin.x+8*guiScale,canvasMin.y+8*guiScale},IM_COL32(255,213,75,255),
      editNotice.c_str(),nullptr,std::max(1.f,pane.extent.x-16*guiScale));
  if(pane.selectionGesture && pane.selectionEditor && pane.selectionEditor->selection().is_path()) {
    const auto &g=*pane.selectionGesture;
    if(io.AppFocusLost || io.WantTextInput || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel) ||
        io.MouseWheel || ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
        g.origin.x!=pane.origin.x || g.origin.y!=pane.origin.y || g.extent.x!=pane.extent.x || g.extent.y!=pane.extent.y ||
        g.zoom!=pane.zoom || g.aspect!=pane.aspectY || g.position.t!=currentPosition.t || g.position.c!=currentPosition.c || g.position.z!=currentPosition.z ||
        (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseReleased(ImGuiMouseButton_Left)))pane.selectionGesture.reset();
  }
  if (pane.selectionGesture) {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
      pane.selectionGesture.reset();
    else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      guard([&] {
        try {
          const auto &gesture = *pane.selectionGesture;
          const auto &editor = *pane.selectionEditor;
          if (gesture.handle.kind == SelectionHandleKind::Move) {
            std::array<double, 2> delta{
                {x - gesture.down[0], y - gesture.down[1]}};
            if (io.KeyShift) {
              if (std::abs(delta[0]) >= std::abs(delta[1] * pane.aspectY))
                delta[1] = 0;
              else
                delta[0] = 0;
            }
            pane.selectionGesture->preview = editor.moved(delta, !io.KeyAlt);
          } else if (gesture.handle.kind == SelectionHandleKind::Resize) {
            try {
              pane.selectionGesture->preview = editor.selection().is_path()
                  ? editor.resized(SelectionHandleMode::Bounds,gesture.handle.index,position,
                      io.KeyShift || pathAspect,io.KeyCtrl || pathCenter,
                      SelectionEditLimits{16777216,65536,selectionCreationLimits().max_working_bytes})
                  : editor.resized(gesture.handle.index,
                  editor.selection().is_path()?position:std::array<double,2>{std::clamp(x,0.,double(f.width)),std::clamp(y,0.,double(f.height))},
                  io.KeyShift || (editor.selection().is_path() && pathAspect),
                  io.KeyCtrl || (editor.selection().is_path() && pathCenter),
                  SelectionEditLimits{16777216,65536,selectionCreationLimits().max_working_bytes});
              pane.selectionGesture->invalid=false;
            } catch(const CollapsedSelectionResize &) {
              if(!editor.selection().is_path())throw;
              pane.selectionGesture->invalid=true;
            }
          }
          else if (gesture.handle.kind == SelectionHandleKind::Rotate) {
            double angle = editor.rotation_degrees(gesture.down, position);
            if (io.KeyShift)
              angle = std::round(angle / 15) * 15;
            pane.selectionGesture->preview = editor.rotated(angle);
          } else {
            auto target = position;
            if (io.KeyShift) {
              const auto original =
                  selectionControlPoint(editor.selection(),gesture.handle.index);
              if (std::abs(target[0] - original[0]) >=
                  std::abs((target[1] - original[1]) * pane.aspectY))
                target[1] = original[1];
              else
                target[0] = original[0];
            }
            pane.selectionGesture->preview =
                editor.moved_vertex(gesture.handle.index, target);
          }
        } catch (...) {
          pane.selectionGesture.reset();
          throw;
        }
      });
    } else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
      auto gesture = std::move(*pane.selectionGesture);
      pane.selectionGesture.reset();
      if (gesture.preview && !gesture.invalid)
        guard([&] { commitSelection(*gesture.preview, gesture.source); });
    }
  }
  if (!savedMode && tool == "edit_roi" && pane.selectionEditor && pane.selectionEditError.empty() && hover && !owner.busy() &&
      !pane.selectionGesture && !io.WantTextInput &&
      !ImGui::IsAnyItemActive()) {
    const auto keyOwner = ImGui::GetID("selection_edit_keys");
    for (auto key : {ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_UpArrow,
                     ImGuiKey_DownArrow, ImGuiKey_Delete})
      ImGui::SetKeyOwner(key, keyOwner);
    auto pressed = [&](ImGuiKey key) {
      return ImGui::IsKeyPressed(key, ImGuiInputFlags_Repeat, keyOwner);
    };
    guard([&] {
      const double step = io.KeyShift ? 10 : 1;
      std::array<double, 2> delta{{(pressed(ImGuiKey_RightArrow) ? step : 0) -
                                       (pressed(ImGuiKey_LeftArrow) ? step : 0),
                                   (pressed(ImGuiKey_DownArrow) ? step : 0) -
                                       (pressed(ImGuiKey_UpArrow) ? step : 0)}};
      if (delta[0] || delta[1]) {
        if (pane.selectionVertex) {
          auto p =
              selectionControlPoint(pane.selectionEditor->selection(),*pane.selectionVertex);
          p[0] += delta[0];
          p[1] += delta[1];
          commitSelection(
              pane.selectionEditor->moved_vertex(*pane.selectionVertex, p),
              *pane.selectionSource);
        } else
          commitSelection(pane.selectionEditor->moved(delta, true),
                          *pane.selectionSource);
      }
      if (pressed(ImGuiKey_Delete) && pane.selectionVertex && !pane.selectionEditor->selection().is_path()) {
        auto changed =
            pane.selectionEditor->removed_vertex(*pane.selectionVertex);
        commitSelection(changed, *pane.selectionSource);
      }
    });
  }
  if (hover && inside && viewportReady) {
    int ix = int(x), iy = int(y);
    std::ostringstream pixel;
    auto world =
        cal.voxel_to_world({double(ix), double(iy), double(d->slice())});
    pixel << "x=" << ix << " y=" << iy << " z=" << d->slice()
          << " c=" << currentPosition.c << " t=" << currentPosition.t
          << " | world=" << world[0] << ',' << world[1] << ',' << world[2]
          << ' ' << cal.unit << " | time=" << cal.time(currentPosition.t)
          << ' ' << cal.time_unit << " | ";
    for (int c = 0; c < f.channels; ++c) {
      if (c)
        pixel << ", ";
      pixel << canvasPixels().at(ix, iy, c);
    }
    status = pixel.str();
  }
  const auto oldZoom = pane.zoom;
  const auto oldPan = pane.pan;
  bool creationNavigated = false;
  if (hover && !owner.busy()) {
    if (io.MouseWheel != 0 && !io.KeyCtrl && d->shape().z > 1) {
      auto crosshair = d->view().crosshair;
      crosshair[2] = std::clamp(d->slice() + (io.MouseWheel > 0 ? 1 : -1), 0,
                                d->shape().z - 1);
      if (crosshair[2] != d->slice()) {
        if (savedRoiEdit.active()) cancelSavedRoiGeometry("position_changed");
        cancelCreation(pane, "position_changed");
        creationNavigated = true;
      }
      d->setView({{"crosshair", crosshair}});
    }
    if (io.MouseWheel != 0 && io.KeyCtrl) {
      float old = pane.zoom;
      pane.zoom =
          std::clamp(old * float(std::pow(1.2, io.MouseWheel)), .01f, 64.f);
      if (pane.zoom != old) {
        cancelCreation(pane, "transform_changed");
        creationNavigated = true;
      }
      pane.pan.x -= (mouse.x - (a.x + available.x / 2) - pane.pan.x) *
                    (pane.zoom / old - 1);
      pane.pan.y -= (mouse.y - (a.y + available.y / 2) - pane.pan.y) *
                    (pane.zoom / old - 1);
    }
    if ((!savedMode && tool == "hand" && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) ||
        ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
      if (io.MouseDelta.x || io.MouseDelta.y) {
        cancelCreation(pane, "transform_changed");
        creationNavigated = true;
      }
      pane.pan.x += io.MouseDelta.x;
      pane.pan.y += io.MouseDelta.y;
    }
    if (!savedMode && tool!="path" && (inside || (tool == "edit_roi" && selectionHit)) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      if (tool == "edit_roi") {
        const auto selectedKind =
            pane.selectionEditor ? pane.selectionEditor->selection().kind : "";
        const bool canInsert =
            selectedKind == "polygon" || selectedKind == "freehand" ||
            selectedKind == "line" || selectedKind == "polyline" ||
            selectedKind == "point" || selectedKind == "multipoint";
        if (canInsert && io.KeyAlt && !pane.selectionGesture) {
          guard([&] {
            const auto index = pane.selectionEditor->insertion_index(
                position, {1, pane.aspectY});
            auto changed =
                pane.selectionEditor->inserted_vertex(index, position);
            commitSelection(changed, *pane.selectionSource);
            pane.selectionVertex = index;
          });
        } else if (selectionHit && pane.selectionSource) {
          if (selectionHit->kind == SelectionHandleKind::Vertex)
            pane.selectionVertex = selectionHit->index;
          else
            pane.selectionVertex.reset();
          pane.selectionGesture.emplace(Pane::SelectionGesture{*pane.selectionSource, *selectionHit, position, {}});
          pane.selectionGesture->origin=pane.origin;pane.selectionGesture->extent=pane.extent;
          pane.selectionGesture->zoom=pane.zoom;pane.selectionGesture->aspect=pane.aspectY;pane.selectionGesture->position=currentPosition;
        }
      } else if (creationTool(tool)) {
        if (creationBlocked.empty() && !creationNavigated &&
            !ImGui::IsKeyPressed(ImGuiKey_Escape)) guard([&] {
          try {
            const std::array<double, 2> p{point[0].get<double>(), point[1].get<double>()};
            if (pane.creation && pane.creation->tool == "polygon") {
              auto &draft = *pane.creation;
              const auto &first = draft.points.front();
              const bool close = draft.points.size() >= 3 &&
                  std::hypot((p[0] - first[0]) * pane.zoom,
                             (p[1] - first[1]) * pane.zoom * pane.aspectY) <= 6 * guiScale;
              if (close) finishCreation(pane);
              else if (draft.points.back() != p) {
                if (draft.points.size() >= selectionCreationLimits().max_candidate_vertices)
                  throw std::invalid_argument("Selection candidate exceeds the vertex limit");
                draft.points.push_back(p);
                updateCreationPreview(pane);
              }
            } else {
              beginCreation(pane, p, io.KeyShift, io.KeyAlt);
              updateCreationPreview(pane);
            }
          } catch (...) {
            cancelCreation(pane, "invalid_candidate");
            throw;
          }
        });
      } else if (tool == "zoom") {
        pane.zoom =
            std::clamp(pane.zoom * (io.KeyShift ? .5f : 2.f), .01f, 64.f);
      } else if (tool == "picker" && viewportReady) {
        for (int c = 0; c < 3; ++c)
          foreground[c] = float(std::clamp(
              canvasPixels().at(int(x), int(y), f.channels == 1 ? 0 : c) / type_max(f.type),
              0.0, 1.0));
      } else if (tool == "text")
        ask("Text annotation",
            {{"x", int(x)},
             {"y", int(y)},
             {"text", "Label"},
             {"font_size", preferenceState.settings.integer("text_size")},
             {"color", paintColor()},
             {"all_slices", false}},
            [this](Json p) { owner.action("text", p); });
      else if (tool == "wand") {
        owner.action("wand", {{"x", int(x)}, {"y", int(y)}});
      } else if (tool != "hand") {
        if (tool == "polygon" || tool == "polyline" || tool == "multipoint" ||
            tool == "angle") {
          pane.points.push_back(point);
          if (tool == "angle" && pane.points.size() == 3)
            finish(pane);
        } else if (tool == "point") {
          d->select(selection(tool, Json::array({point}),
                              preferenceState.settings.number("point_size")));
        } else {
          pane.points = Json::array({point});
          if (tool == "rounded_rectangle" || tool == "rectangle" || tool == "ellipse" || tool == "line")
            pane.points.push_back(point);
          pane.dragging = true;
        }
      }
    }
    if ((ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
         ImGui::IsKeyPressed(ImGuiKey_Enter) ||
         ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) &&
        !pane.points.empty() && !pane.dragging)
      finish(pane);
  }
  if (pane.creation && creationBlocked.empty() && !creationNavigated) guard([&] {
    try {
      auto &draft = *pane.creation;
      if (draft.dragging) {
        // Only delivered pointer movement grows a candidate. Key-only frames
        // and button release retain the last observed geometry.
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            (io.MouseDelta.x || io.MouseDelta.y)) {
          if (draft.box) {
            bool fromCenter = io.KeyCtrl;
#ifdef __APPLE__
            fromCenter = fromCenter || io.KeySuper;
#endif
            draft.box = update_selection_creation_box(*draft.box,
                {point[0].get<int>(), point[1].get<int>()}, io.KeyShift, fromCenter);
          } else {
            const std::array<double, 2> p{point[0].get<double>(), point[1].get<double>()};
            if (draft.points.back() != p) {
              if (draft.points.size() >= selectionCreationLimits().max_candidate_vertices)
                throw std::invalid_argument("Selection candidate exceeds the vertex limit");
              draft.points.push_back(p);
            }
          }
          updateCreationPreview(pane);
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) finishCreation(pane);
      } else if (ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                 (hover && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
                            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))))
        finishCreation(pane);
    } catch (...) {
      cancelCreation(pane, "invalid_candidate");
      throw;
    }
  });
  if (oldZoom != pane.zoom || oldPan.x != pane.pan.x ||
      oldPan.y != pane.pan.y) {
    auto pan = d->view().pan;
    pan[0] = {pane.pan.x, pane.pan.y};
    if (d->view().linkedZoom)
      d->setView({{"zoom", pane.zoom / spacing[0]}, {"pan", pan}});
    else {
      auto zooms = d->view().planeZoom;
      zooms[0] = pane.zoom / spacing[0];
      d->setView({{"plane_zoom", zooms}, {"pan", pan}});
    }
  }
  if (pane.dragging && !owner.busy()) {
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      if (tool == "rounded_rectangle" || tool == "rectangle" || tool == "ellipse" || tool == "line")
        pane.points[1] = point;
      else if (io.MouseDelta.x || io.MouseDelta.y)
        pane.points.push_back(point);
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
      if (tool == "brush" || tool == "eraser") {
        auto points = pane.points;
        pane.points = Json::array();
        pane.dragging = false;
        guard([&] {
          owner.applyOperation("paint_stroke",
                               {{"points", points},
                                {"width", brush},
                                {"color", paintColor(tool == "brush")}},
                               allSlices);
        });
      } else
        finish(pane);
    }
  }
  if (d->shape().z > 1) {
    int index = d->slice() + 1;
    const bool sliceChanged = ImGui::SliderInt("Slice", &index, 1, d->shape().z);
    observeControl("image.slice");
    if (headlessDiagnostics) {
      auto &control = hostControls["image.slice"];
      control["document_id"] = d->id;
      control["value"] = index;
      control["minimum"] = 1;
      control["maximum"] = d->shape().z;
      control["native_slice"] = index - 1;
    }
    if (sliceChanged) {
      if (savedRoiEdit.active()) cancelSavedRoiGeometry("position_changed");
      cancelCreation(pane, "position_changed");
      auto crosshair = d->view().crosshair;
      crosshair[2] = index - 1;
      d->setView({{"crosshair", crosshair}});
    }
    ImGui::SameLine();
    bool playing = animating;
    if (ImGui::Checkbox("Play", &playing))
      guard([&] { owner.action("animation", {{"playing", playing}, {"axis", "z"}}); });
    observeControl("image.play");
  }
}
void Window::Impl::dialogs() {
  guard([&]{rgbDatasetPanel.draw();});
  guard([&]{colorStackPanel.draw();});
  guard([&]{indexedColorPanel.draw();});
#ifdef IMAGEJXX_HAS_SCIENTIFIC_IO
  guard([&]{scientificIoPanel.draw([this]{browseScientificIo();},[this]{importFlatReview();},[this]{clearPendingImports();},pendingFileOpens.size(),startupTiffContext());});
#endif
  const bool orientationRange = dialog.id == "axis_range_orientation";
  const bool spacingRange = dialog.id == "axis_range_spacing";
  const bool slicerRange = dialog.id == "axis_range_slicer_resize";
  const bool integerRange = dialog.id == "axis_range_reslice" || orientationRange || spacingRange || slicerRange;
  bool spacingRenderReady = true;
  bool slicerApplyReady = false;
  dialog.controls = Json::object();
  dialog.choicePoint = dialog.alternateChoicePoint = {-1, 0};
  dialog.applyPoint = dialog.previewPoint = {-1, 0};
  auto itemPoint = [] {
    const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    return ImVec2((a.x + b.x) / 2, (a.y + b.y) / 2);
  };
  const bool errorBlocking = !lastError.empty();
  if (errorBlocking)
    ImGui::OpenPopup("Operation failed");
  if (ImGui::BeginPopupModal("Operation failed", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextWrapped("%s", lastError.c_str());
    if (headlessDiagnostics)
      errorModal = {{"open", true}, {"message", lastError},
                    {"acknowledged", false}, {"controls", Json::object()}};
    const bool acknowledged = ImGui::Button("OK");
    if (headlessDiagnostics)
      errorModal["controls"]["ok"] = itemControl();
    if (acknowledged) {
      lastError.clear();
      ImGui::CloseCurrentPopup();
      if (headlessDiagnostics) {
        errorModal["acknowledged"] = true;
        errorModal["open"] = false;
      }
    }
    ImGui::EndPopup();
  }
  // A rejected Run retains its draft. Keep the error modal as the current
  // popup until acknowledgement; reopening Run at the same level would replace
  // it. Resume that unchanged draft on the next physical frame after OK.
  if (errorBlocking)
    return;
  if (dialog.open)
    ImGui::OpenPopup(dialog.title.c_str());
  ImGui::SetNextWindowSize({680, 0}, ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal(dialog.title.c_str(), &dialog.open,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::BeginDisabled(owner.busy());
    if (dialog.id == "grouped_z_projection") {
      std::string hint = "Enter valid slice bounds";
      Json factors = Json::array();
      try {
        const auto first = Json::parse(dialog.text.at("first"));
        const auto last = Json::parse(dialog.text.at("last"));
        if (first.is_number_integer() && last.is_number_integer() && first >= 1 &&
            first <= last && last <= INT32_MAX) {
          const auto count = integer(last) - integer(first) + 1;
          std::vector<int> divisors;
          for (int i = 1; i <= count / i; ++i) if (count % i == 0) {
            divisors.push_back(i); if (i != count / i) divisors.push_back(count / i);
          }
          std::sort(divisors.begin(), divisors.end());
          hint.clear();
          for (std::size_t i = 0; i < std::min<std::size_t>(10, divisors.size()); ++i) {
            hint += (i ? ", " : "") + std::to_string(divisors[i]);
            factors.push_back(divisors[i]);
          }
          if (divisors.size() > 10) hint += ", ...";
        }
      } catch (const Json::exception&) {} catch (const std::invalid_argument&) {}
      // A fixed-height, read-only field keeps controls stable while drafts are
      // invalid and lets long divisor lists scroll horizontally at large fonts.
      ImGui::SetNextItemWidth(390);
      ImGui::InputText("Valid group sizes", &hint, ImGuiInputTextFlags_ReadOnly);
      if (headlessDiagnostics) {
        auto observed = itemControl(!owner.busy());
        observed["text"] = hint; observed["values"] = std::move(factors);
        observed["read_only"] = true;
        dialog.controls["group_sizes"] = std::move(observed);
      }
    }
    for (const auto &p : dialog.schema) {
      auto name = string(get(p, "name")), label = string(get(p, "label"), name),
           kind = string(get(p, "kind"));
      const auto &choices = get(p, "choices");
      auto &value = dialog.values[name];
      auto &text = dialog.text[name];
      Json fieldControl;
      ImGui::PushID(name.c_str());
      if (kind == "bool") {
        bool v = boolean(value);
        if (ImGui::Checkbox(label.c_str(), &v)) {
          value = v;
          if(dialog.id=="auto_mask" && name=="stack_histogram" && v)allSlices=true;
        }
      } else if(kind=="float" && dialog.id=="selection_enlargement"){
        ImGui::SetNextItemWidth(390);const auto editorId=ImGui::GetID(label.c_str());
        ImGui::InputText(label.c_str(),&text);
        try{const auto parsed=Json::parse(text);value=parsed.is_number() && std::isfinite(parsed.get<double>())?parsed:Json(nullptr);}
        catch(const Json::exception &){value=nullptr;}
        fieldControl=itemControl();fieldControl["id"]=editorId;fieldControl["draft"]=text;
        fieldControl["identity_source"]="current_scoped_InputText_label_id";
      } else if (kind == "float" && spacingRange && value.is_number()) {
        double v = number(value);
        ImGui::SetNextItemWidth(390);
        const auto editorId = ImGui::GetID(label.c_str());
        const auto *editorWindow = ImGui::GetCurrentWindowRead();
        const bool editorEnabled = !(ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled);
        if (ImGui::InputDouble(label.c_str(), &v, 0, 0, "%.17g")) {
          value = v;
          text = value.dump();
        }
        if (!editorWindow->SkipItems) {
          fieldControl = itemControl(editorEnabled);
          const auto &context = *ImGui::GetCurrentContext();
          fieldControl["id"] = editorId;
          fieldControl["active"] = context.ActiveId == editorId;
          fieldControl["focused"] = context.NavWindow == editorWindow && context.NavId == editorId;
          fieldControl["identity_source"] = "current_scoped_InputDouble_text_label_id";
          fieldControl["geometry_source"] = "actual_InputDouble_group_item_rect";
          fieldControl["rect_kind"] = "group_including_label";
        }
      } else if (kind == "float" && dialog.id == "convert_image" &&
                 value.is_number()) {
        double v = number(value);
        ImGui::SetNextItemWidth(390);
        const bool disabled =
            ((name == "range_min" || name == "range_max") &&
             dialog.values["range_policy"] != "explicit") ||
            ((name == "red_weight" || name == "green_weight" ||
              name == "blue_weight") &&
             !boolean(dialog.values["weighted_rgb"]));
        ImGui::BeginDisabled(disabled);
        if (ImGui::InputDouble(label.c_str(), &v, 0, 0, "%.17g")) {
          value = v;
          text = value.dump();
        }
        fieldControl = itemControl(!disabled);
        ImGui::EndDisabled();
      } else if (kind == "int" && (dialog.scientificExtraction || orientationRange)) {
        // Preserve partial/invalid scientific drafts verbatim across focus loss.
        // InputInt restores an old parsed value, which can silently retarget a
        // draft such as "-". Other eager forms retain their InputInt path.
        ImGui::SetNextItemWidth(390);
        const auto editorId = ImGui::GetID(label.c_str());
        const auto *editorWindow = ImGui::GetCurrentWindowRead();
        if (ImGui::InputText(label.c_str(), &text, ImGuiInputTextFlags_CharsDecimal) &&
            orientationRange)
          dialog.orientationRangeEdited = true;
        try {
          const auto parsed = Json::parse(text);
          if (parsed.is_number_integer()) value = parsed;
        } catch (const Json::exception &) {}
        fieldControl = itemControl();
        const auto &context = *ImGui::GetCurrentContext();
        fieldControl["id"] = editorId;
        fieldControl["active"] = context.ActiveId == editorId;
        fieldControl["focused"] = context.NavWindow == editorWindow && context.NavId == editorId;
        fieldControl["identity_source"] = "current_scoped_InputText_text_label_id";
        fieldControl["geometry_source"] = "actual_InputText_item_rect";
        fieldControl["rect_kind"] = "item_including_label";
      } else if (kind == "int" &&
                 (dialog.id == "axis_plane_export" ||
                  integerRange ||
                  dialog.id == "binary_erode" || dialog.id == "binary_dilate" ||
                  dialog.id == "binary_open" || dialog.id == "binary_close") &&
                 value.is_number_integer() && number(value) >= INT32_MIN &&
                 number(value) <= INT32_MAX) {
        auto v = integer(value);
        ImGui::SetNextItemWidth(390);
        // InputInt ends a group; its editable text uses this scoped label ID.
        const auto editorId = ImGui::GetID(label.c_str());
        const auto *editorWindow = ImGui::GetCurrentWindowRead();
        const bool editorEnabled =
            !(ImGui::GetCurrentContext()->CurrentItemFlags &
              ImGuiItemFlags_Disabled);
        if (ImGui::InputInt(label.c_str(), &v)) {
          value = v;
          text = std::to_string(v);
        }
        if ((dialog.id == "axis_plane_export" || integerRange) &&
            !editorWindow->SkipItems) {
          fieldControl = itemControl(editorEnabled);
          const auto &context = *ImGui::GetCurrentContext();
          fieldControl["id"] = editorId;
          fieldControl["active"] = context.ActiveId == editorId;
          fieldControl["focused"] =
              context.NavWindow == editorWindow && context.NavId == editorId;
          fieldControl["identity_source"] = "current_scoped_InputInt_text_label_id";
          fieldControl["geometry_source"] = "actual_InputInt_group_item_rect";
          fieldControl["rect_kind"] = "group_including_step_buttons_and_label";
        }
      } else if (choices.is_array() && !choices.empty()) {
        const auto choiceLabel = [](const Json &v) {
          return v.is_null()     ? std::string("preserve")
                 : v.is_string() ? string(v)
                                 : v.dump();
        };
        auto current = choiceLabel(value);
        const bool track = dialog.choicePoint.x < 0;
        const bool open = ImGui::BeginCombo(label.c_str(), current.c_str());
        fieldControl = itemControl();
        if (track)
          dialog.choicePoint = itemPoint();
        if (open) {
          for (const auto &choice : choices) {
            auto option = choiceLabel(choice);
            if (ImGui::Selectable(option.c_str(), option == current)) {
              if(option!=current && dialog.id=="selection_enlargement" && name=="units" && dialog.enlargementSource){
                const auto spacing=dialog.enlargementSource->data().dataset.calibration().spacing[0];
                if(dialog.values.at("distance").is_number()){
                  const double converted=option=="calibrated"?number(dialog.values.at("distance"))*spacing:number(dialog.values.at("distance"))/spacing;
                  if(std::isfinite(converted)){dialog.values["distance"]=converted;dialog.text["distance"]=dialog.values["distance"].dump();}
                  else {dialog.values["distance"]=nullptr;dialog.text["distance"]="invalid distance";}
                }
              }
              value = choice;
              text = option;
              if (option != current && dialog.id == "axis_plane_export" && name == "plane" &&
                  dialog.axisPlaneSource) {
                const auto plane = parse_plane(option);
                const auto axis = plane == Plane::XY ? 2 : plane == Plane::XZ ? 1 : 0;
                const auto index = dialog.axisPlaneSource->display().crosshair[axis] + 1;
                dialog.values["index"] = index;
                dialog.text["index"] = std::to_string(index);
              }
              if (option != current && dialog.id == "axis_range_reslice" && name == "plane" &&
                  dialog.axisRangeSource) {
                const auto plane = parse_plane(option);
                const auto shape = dialog.axisRangeSource->data().dataset.shape();
                const auto extent = plane == Plane::XY ? shape.z : plane == Plane::XZ ? shape.y : shape.x;
                dialog.values["first"] = 1;
                dialog.values["last"] = extent;
                dialog.values["step"] = 1;
                dialog.text["first"] = "1";
                dialog.text["last"] = std::to_string(extent);
                dialog.text["step"] = "1";
              }
              if (option != current && spacingRange && name == "plane" && dialog.axisRangeSpacingSource) {
                const auto shape = dialog.axisRangeSpacingSource->data().dataset.shape();
                const auto extent = parse_plane(option) == Plane::XZ ? shape.y : shape.x;
                dialog.values["first"] = 1;
                dialog.values["last"] = extent;
                dialog.values["step"] = 1;
                dialog.text["first"] = "1";
                dialog.text["last"] = std::to_string(extent);
                dialog.text["step"] = "1";
              }
              if (option != current && slicerRange && name == "plane" && dialog.axisRangeSlicerResizeSource) {
                const auto shape = dialog.axisRangeSlicerResizeSource->data().dataset.shape();
                const auto extent = parse_plane(option) == Plane::XZ ? shape.y : shape.x;
                dialog.values["first"] = 1;
                dialog.values["last"] = extent;
                dialog.values["step"] = 1;
                dialog.text["first"] = "1";
                dialog.text["last"] = std::to_string(extent);
                dialog.text["step"] = "1";
              }
              const auto resetOrientationRange = [&] {
                if (!dialog.extractionRebindRecord.is_null()) return false;
                if (!dialog.orientationRangeEdited) return true;
                // An explicit plane change resets a valid range to that axis.
                // Retain partial/invalid text until the user corrects it.
                try {
                  const auto first = Json::parse(dialog.text.at("first"));
                  const auto last = Json::parse(dialog.text.at("last"));
                  const auto step = Json::parse(dialog.text.at("step"));
                  if (!first.is_number_integer() || !last.is_number_integer() || !step.is_number_integer()) return false;
                  const auto shape = dialog.axisRangeOrientationSource->data().dataset.shape();
                  const auto oldPlane = parse_plane(current);
                  const auto extent = oldPlane == Plane::XY ? shape.z : oldPlane == Plane::XZ ? shape.y : shape.x;
                  return first >= 1 && first <= last && last <= extent && step >= 1 && step <= INT32_MAX;
                } catch (const std::exception &) { return false; }
              };
              if (option != current && orientationRange && name == "plane" &&
                  dialog.axisRangeOrientationSource && resetOrientationRange()) {
                const auto plane = parse_plane(option);
                const auto shape = dialog.axisRangeOrientationSource->data().dataset.shape();
                const auto extent = plane == Plane::XY ? shape.z : plane == Plane::XZ ? shape.y : shape.x;
                dialog.values["first"] = 1;
                dialog.values["last"] = extent;
                dialog.values["step"] = 1;
                dialog.text["first"] = "1";
                dialog.text["last"] = std::to_string(extent);
                dialog.text["step"] = "1";
              }
            }
            dialog.controls[name + ".choice." + option] = itemControl();
            if (track && option != current && dialog.alternateChoicePoint.x < 0)
              dialog.alternateChoicePoint = itemPoint();
          }
          ImGui::EndCombo();
          // EndCombo restores the actual parent combo item and its window.
          if (((dialog.id == "axis_plane_export" || integerRange) && name == "plane") ||
              (spacingRange && name == "sampling"))
            fieldControl = itemControl();
        }
      } else {
        ImGui::SetNextItemWidth(390);
        if (kind == "json")
          ImGui::InputTextMultiline(label.c_str(), &text, {390, 64});
        else
          ImGui::InputText(label.c_str(), &text);
        fieldControl = itemControl();
        if (boolean(get(p, "optional"))) {
          ImGui::SameLine();
          ImGui::TextDisabled("optional");
        }
      }
      if (fieldControl.is_null())
        fieldControl = itemControl();
      const auto point =
          kind == "int" || (kind == "float" && (dialog.id == "convert_image" || spacingRange || dialog.id=="selection_enlargement"))
              ? ImVec2(float(number(fieldControl["rect"][0])) + 80,
                       float(number(fieldControl["point"][1])))
              : ImVec2(float(number(fieldControl["point"][0])),
                       float(number(fieldControl["point"][1])));
      fieldControl["kind"] = kind;
      fieldControl["point"] = {point.x, point.y};
      dialog.controls[name] = std::move(fieldControl);
      ImGui::PopID();
    }
    if (dialog.operation && dialog.id == "convert_image")
      ImGui::TextDisabled(
          "Converts every slice; the active selection is preserved.");
    else if (dialog.operation) {
      const bool pooled=dialog.id=="auto_mask" && boolean(get(dialog.values,"stack_histogram"));
      if(pooled)allSlices=true;
      ImGui::BeginDisabled(pooled);
      ImGui::Checkbox("Process all slices", &allSlices);
      dialog.controls["all_slices"] = itemControl(!pooled);
      ImGui::EndDisabled();
      dialog.controls["all_slices"]["value"] = allSlices;
    }
    const auto extractionSource = dialog.axisPlaneSource
        ? &*dialog.axisPlaneSource
        : dialog.axisRangeSource ? &*dialog.axisRangeSource
        : dialog.axisRangeOrientationSource ? &*dialog.axisRangeOrientationSource : nullptr;
    if (extractionSource) {
      const auto shape = extractionSource->data().dataset.shape();
      ImGui::Text("Channel %d / %d", extractionSource->display().c + 1, shape.c);
      ImGui::SameLine();
      ImGui::Text("Time point %d / %d", extractionSource->display().t + 1, shape.t);
    }
    if (dialog.id == "axis_plane_export" && dialog.axisPlaneSource) {
      const auto plane = parse_plane(string(get(dialog.values, "plane")));
      const auto shape = dialog.axisPlaneSource->data().dataset.shape();
      const auto axis = plane == Plane::XY ? "Z" : plane == Plane::XZ ? "Y" : "X";
      const auto extent = plane == Plane::XY ? shape.z : plane == Plane::XZ ? shape.y : shape.x;
      dialog.axisIndexLabel = std::string(axis) + " index: 1 to " +
                              std::to_string(extent);
      ImGui::TextUnformatted(dialog.axisIndexLabel.c_str());
    }
    if (dialog.id == "axis_range_reslice" && dialog.axisRangeSource) {
      const auto plane = parse_plane(string(get(dialog.values, "plane")));
      const auto shape = dialog.axisRangeSource->data().dataset.shape();
      const auto axis = plane == Plane::XY ? "Z" : plane == Plane::XZ ? "Y" : "X";
      const auto extent = plane == Plane::XY ? shape.z : plane == Plane::XZ ? shape.y : shape.x;
      dialog.axisRangeLabel = std::string(axis) + " indices: 1 to " + std::to_string(extent);
      ImGui::TextUnformatted(dialog.axisRangeLabel.c_str());
      dialog.axisRangeCount.reset();
      const auto &first = get(dialog.values, "first"), &last = get(dialog.values, "last"),
                 &step = get(dialog.values, "step");
      if (first.is_number_integer() && last.is_number_integer() && step.is_number_integer() &&
          first >= 1 && first <= last && last <= extent && step >= 1 && step <= INT32_MAX)
        dialog.axisRangeCount = 1 + (last.get<int>() - first.get<int>()) / step.get<int>();
      if (dialog.axisRangeCount)
        ImGui::Text("Emitted planes: %d", *dialog.axisRangeCount);
      else
        ImGui::TextDisabled("Emitted planes: invalid range");
    }
    if (orientationRange && dialog.axisRangeOrientationSource) {
      const auto plane = parse_plane(string(get(dialog.values, "plane")));
      const auto shape = dialog.axisRangeOrientationSource->data().dataset.shape();
      const auto fixed = plane == Plane::XY ? 2 : plane == Plane::XZ ? 1 : 0;
      const auto horizontal = plane == Plane::YZ ? 1 : 0;
      const auto vertical = plane == Plane::XY ? 1 : 2;
      const std::array<const char *, 3> axes{{"X", "Y", "Z"}};
      const std::array<int, 3> extents{{shape.x, shape.y, shape.z}};
      dialog.axisRangeLabel = std::string(axes[fixed]) + " indices: 1 to " + std::to_string(extents[fixed]);
      ImGui::TextUnformatted(dialog.axisRangeLabel.c_str());
      ImGui::TextDisabled("Flip input vertical axis before transpose; transpose exchanges axes.");
      dialog.axisRangeCount.reset();
      dialog.axisRangeOrientationPreview = Json::object();
      const auto &first = get(dialog.values, "first"), &last = get(dialog.values, "last"),
                 &step = get(dialog.values, "step");
      if (first.is_number_integer() && last.is_number_integer() && step.is_number_integer() &&
          first >= 1 && first <= last && last <= extents[fixed] && step >= 1 && step <= INT32_MAX) {
        const auto count = 1 + (last.get<int>() - first.get<int>()) / step.get<int>();
        const auto selectedLast = first.get<int>() + (count - 1) * step.get<int>();
        const bool reverse = boolean(dialog.values["reverse_order"]),
                   flip = boolean(dialog.values["flip_vertical"]),
                   transpose = boolean(dialog.values["transpose"]);
        const auto begin = reverse ? selectedLast : first.get<int>();
        const auto end = reverse ? first.get<int>() : selectedLast;
        dialog.axisRangeCount = count;
        dialog.axisRangeOrientationPreview = {
            {"effective_first_selected_1based", begin}, {"effective_last_selected_1based", end},
            {"last_selected_1based", selectedLast}, {"requested_last_1based", last},
            {"output_axis_order", {axes[transpose ? vertical : horizontal], axes[transpose ? horizontal : vertical], axes[fixed]}},
            {"output_axis_signs", {transpose && flip ? -1 : 1, !transpose && flip ? -1 : 1, reverse ? -1 : 1}},
            {"emitted_plane_count", count}, {"flip_before_transpose", true}};
        ImGui::Text("Emitted planes: %d; selected %d to %d (step %d)", count, begin, end, step.get<int>());
        ImGui::Text("Output axes: %s%s, %s%s, %s%s (stride %d)",
            transpose && flip ? "-" : "+", axes[transpose ? vertical : horizontal],
            !transpose && flip ? "-" : "+", axes[transpose ? horizontal : vertical],
            reverse ? "-" : "+", axes[fixed], step.get<int>());
      } else {
        ImGui::TextDisabled("Emitted planes: invalid range");
      }
    }
    if (spacingRange && dialog.axisRangeSpacingSource) {
      const auto &source = *dialog.axisRangeSpacingSource;
      ImGui::Text("Captured T %d, C %d",source.display().t+1,source.display().c+1);
      const auto shape = source.data().dataset.shape();
      const auto plane = parse_plane(string(get(dialog.values, "plane")));
      const auto extent = plane == Plane::XZ ? shape.y : shape.x;
      dialog.axisRangeLabel = std::string(plane == Plane::XZ ? "Y" : "X") +
                             " indices: 1 to " + std::to_string(extent);
      ImGui::TextUnformatted(dialog.axisRangeLabel.c_str());
      ImGui::TextDisabled("Flip the original source Z extent before sampling; transpose afterward.");
      dialog.axisRangeSpacingPreview = Json::object();
      dialog.axisRangeCount.reset();
      int renderLimit = 0;
      try {
        renderLimit = physicalGridRenderLimit(gl, this);
      } catch (const std::exception &error) {
        spacingRenderReady = false;
        dialog.axisRangeSpacingPreview["render_error"] = error.what();
        ImGui::TextWrapped("%s", error.what());
      }
      try {
        auto request = physicalGridRequest(source, dialog.values, true);
        const auto configured = static_cast<std::size_t>(
            preferenceState.settings.integer("processing_memory_mb")) * 1024 * 1024;
        if (configured)
          request.orientation.range.max_working_bytes = std::min(request.orientation.range.max_working_bytes, configured);
        const auto estimate = imagejxx::estimate_axis_range_spacing(source.data().dataset, request);
        dialog.axisRangeCount = estimate.plane_count;
        dialog.axisRangeSpacingPreview.update({
            {"width", estimate.width}, {"height", estimate.height}, {"samples", estimate.samples},
            {"emitted_plane_count", estimate.plane_count}, {"vertical_count", estimate.vertical_count},
            {"last_selected_1based", estimate.last_selected + 1},
            {"effective_first_selected_1based", estimate.effective_first + 1},
            {"effective_last_selected_1based", estimate.effective_last + 1},
            {"output_axis_order", estimate.output_axes},
            {"source_vertical_spacing", estimate.source_vertical_spacing}, {"voxel_step", estimate.voxel_step},
            {"working_bytes", estimate.working_bytes}, {"render_limit", renderLimit},
            {"render_limit_source", gl ? "current_owned_GL_MAX_TEXTURE_SIZE" : "headless_frontend_4096"}});
        ImGui::Text("Output: %d x %d x %d; %d samples per pixel", estimate.width, estimate.height,
                    estimate.plane_count, estimate.samples);
        ImGui::Text("Selected %d to %d; sampled source Z count: %d", estimate.effective_first + 1,
                    estimate.effective_last + 1, estimate.vertical_count);
        ImGui::Text("Output axes: %s, %s, %s", estimate.output_axes[0].c_str(),
                    estimate.output_axes[1].c_str(), estimate.output_axes[2].c_str());
        ImGui::Text("Source Z spacing: %.17g; voxel step: %.17g", estimate.source_vertical_spacing, estimate.voxel_step);
        ImGui::Text("Working estimate: %.3f MiB; memory limit: %.3f MiB",
                    double(estimate.working_bytes) / (1024 * 1024),
                    double(request.orientation.range.max_working_bytes) / (1024 * 1024));
        if (spacingRenderReady) {
          ImGui::Text("Image window limit: %d pixels per dimension%s", renderLimit,
                      gl ? "" : " (headless frontend)");
          validatePhysicalGridRendering(estimate, renderLimit);
        }
      } catch (const std::exception &error) {
        dialog.axisRangeSpacingPreview["error"] = error.what();
        ImGui::TextWrapped("%s", error.what());
      }
    }
    if (slicerRange && dialog.axisRangeSlicerResizeSource) {
      const auto &source = *dialog.axisRangeSlicerResizeSource;
      ImGui::Text("Captured T %d, C %d",source.display().t+1,source.display().c+1);
      const auto &calibration = source.data().dataset.calibration();
      const bool editBusy = owner.busy();
      dialog.axisRangeCount.reset();
      dialog.axisRangeSlicerResizePreview = {{"source_available", false}, {"source_current", false},
          {"busy", editBusy}, {"can_apply", false}, {"source_unit", calibration.unit},
          {"geometry_policy", "nominal_unclamped_processor_sampling_affine"},
          {"scalar_spacing_policy", "imagej_slicer_positive_scalar_spacing_public_stride_copy"},
          {"canonicality", "checked_on_selected_samples_at_execution"}};
      auto &preview = dialog.axisRangeSlicerResizePreview;
      ImGui::TextDisabled("Flip source Z and transpose before resizing.");
      ImGui::TextWrapped("Finite scalar bilinear pixels; nominal world geometry and ImageJ scalar spacing are separate.");
      ImGui::Text("Source unit: %s", calibration.unit.c_str());
      dialog.controls["source_unit"] = itemControl(false);
      dialog.controls["source_unit"]["read_only"] = true;
      dialog.controls["source_unit"]["value"] = calibration.unit;
      int renderLimit = 0;
      bool renderReady = true;
      try {
        renderLimit = physicalGridRenderLimit(gl, this);
      } catch (const std::exception &error) {
        renderReady = false;
        preview["render_error"] = error.what();
        ImGui::TextWrapped("%s", error.what());
      }
      try {
        const auto current = owner.nativeSnapshot(source.id());
        preview["source_available"] = true;
        const bool sourceCurrent = source.same_document(current) &&
            current.revisions().matches(source.revisions(), Change::Pixels | Change::Metadata);
        preview["source_current"] = sourceCurrent;
        if (!sourceCurrent)
          throw RevisionConflict();
        auto request = slicerResizeRequest(source, dialog.values, true);
        const auto &range = request.orientation.range;
        const auto shape = source.data().dataset.shape();
        const auto extent = range.plane == Plane::XZ ? shape.y : shape.x;
        dialog.axisRangeLabel = std::string(range.plane == Plane::XZ ? "Y" : "X") +
            " indices: 1 to " + std::to_string(extent);
        ImGui::TextUnformatted(dialog.axisRangeLabel.c_str());
        const auto configured = static_cast<std::size_t>(
            preferenceState.settings.integer("processing_memory_mb")) * 1024 * 1024;
        if (configured)
          request.orientation.range.max_working_bytes = std::min(range.max_working_bytes, configured);
        const auto estimate = imagejxx::estimate_axis_range_slicer_resize(source.data().dataset, request);
        dialog.axisRangeCount = estimate.plane_count;
        const int horizontal = range.plane == Plane::XZ ? 0 : 1;
        const int fixed = range.plane == Plane::XZ ? 1 : 0;
        volatile double nominalVertical = calibration.spacing[2] * estimate.processor_vertical_step;
        volatile double nominalFixed = calibration.spacing[fixed] * double(range.step);
        std::array<double, 3> nominalSpacing{calibration.spacing[horizontal], nominalVertical, nominalFixed};
        if (request.orientation.transpose)
          std::swap(nominalSpacing[0], nominalSpacing[1]);
        preview.update({{"width", estimate.width}, {"height", estimate.height}, {"samples", estimate.samples},
            {"emitted_plane_count", estimate.plane_count}, {"source_vertical_count", estimate.source_vertical_count},
            {"vertical_count", estimate.vertical_count}, {"z_spacing_ratio", estimate.z_spacing_ratio},
            {"processor_vertical_origin", estimate.processor_vertical_origin},
            {"processor_vertical_step", estimate.processor_vertical_step},
            {"resize_branch", slicerResizeBranchName(estimate.resize_branch)},
            {"requested_last_1based", range.last + 1}, {"last_selected_1based", estimate.last_selected + 1},
            {"effective_first_selected_1based", estimate.effective_first + 1},
            {"effective_last_selected_1based", estimate.effective_last + 1},
            {"output_axis_order", estimate.output_axes}, {"nominal_spacing", nominalSpacing},
            {"scalar_spacing", estimate.scalar_spacing}, {"working_bytes", estimate.working_bytes},
            {"max_working_bytes", range.max_working_bytes}, {"render_limit", renderLimit},
            {"render_limit_source", gl ? "current_owned_GL_MAX_TEXTURE_SIZE" : "headless_frontend_4096"}});
        ImGui::Text("Z ratio: %.17g; source %d; resized %d; %s", estimate.z_spacing_ratio,
            estimate.source_vertical_count, estimate.vertical_count, slicerResizeBranchName(estimate.resize_branch));
        ImGui::Text("Output: %d x %d x %d; selected %d to %d", estimate.width, estimate.height,
            estimate.plane_count, estimate.effective_first + 1, estimate.effective_last + 1);
        ImGui::Text("Output axes: %s, %s, %s", estimate.output_axes[0].c_str(),
            estimate.output_axes[1].c_str(), estimate.output_axes[2].c_str());
        ImGui::Text("Nominal spacing: %.17g, %.17g, %.17g %s", nominalSpacing[0], nominalSpacing[1],
            nominalSpacing[2], calibration.unit.c_str());
        ImGui::Text("ImageJ scalar spacing: %.17g, %.17g, %.17g %s", estimate.scalar_spacing[0],
            estimate.scalar_spacing[1], estimate.scalar_spacing[2], calibration.unit.c_str());
        ImGui::Text("Working estimate: %.3f MiB; memory limit: %.3f MiB", double(estimate.working_bytes) / (1024 * 1024),
            double(range.max_working_bytes) / (1024 * 1024));
        if (renderReady) {
          ImGui::Text("Image window limit: %d pixels per dimension%s", renderLimit,
              gl ? "" : " (headless frontend)");
          validateSlicerResizeRendering(estimate, renderLimit);
        }
        if (editBusy)
          throw std::invalid_argument("Wait for the current edit");
        slicerApplyReady = renderReady;
        preview["can_apply"] = slicerApplyReady;
      } catch (const std::exception &error) {
        preview["error"] = error.what();
        ImGui::TextWrapped("%s", error.what());
      }
    }
    auto parse = [&] {
      auto values = dialog.values;
      for (const auto &p : dialog.schema) {
        auto name = string(get(p, "name")), kind = string(get(p, "kind"));
        if (kind == "bool" || !get(p, "choices").empty())
          continue;
        const auto &text = dialog.text[name];
        if (boolean(get(p, "optional")) &&
            (text.empty() || text == "auto" || text == "preserve" ||
             text == "none"))
          values[name] = nullptr;
        else if (kind == "text")
          values[name] = text;
        else {
          auto parsed = Json::parse(text);
          if (kind == "int" && !dialog.operation)
            integer(parsed);
          if (kind == "float")
            number(parsed);
          values[name] = std::move(parsed);
        }
      }
      if (dialog.operation)
        validateRecord(dialog.id, values);
      return values;
    };
    bool extractionReady = true;
    std::string extractionDraftError;
    if ((dialog.scientificExtraction || orientationRange) && extractionSource) {
      try {
        if (orientationRange) {
          auto request = axisOrientationDialogRequest(*extractionSource, dialog.extractionCap, parse());
          const auto configured = static_cast<std::size_t>(
              preferenceState.settings.integer("processing_memory_mb")) * 1024 * 1024;
          if (configured) request.range.max_working_bytes =
              std::min(request.range.max_working_bytes, configured);
          const auto layout = describe_axis_range_orientation(extractionSource->data().dataset, request);
          dialog.axisRangeOrientationPreview["working_bytes"] = layout.working_bytes;
          dialog.axisRangeOrientationPreview["max_working_bytes"] = request.range.max_working_bytes;
          dialog.axisRangeOrientationPreview["can_apply"] = true;
        } else {
        auto request = axisExtractionDialogRequest(*extractionSource,
            dialog.axisRangeSource.has_value(), dialog.extractionCap, parse());
        const auto configured = static_cast<std::size_t>(
            preferenceState.settings.integer("processing_memory_mb")) * 1024 * 1024;
        std::visit([&](auto &value) {
          if (configured) value.max_working_bytes = std::min(value.max_working_bytes, configured);
          using Request = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<Request, AxisPlaneExportRequest>)
            (void)describe_axis_plane_export(extractionSource->data().dataset, value);
          else (void)describe_axis_range_reslice(extractionSource->data().dataset, value);
        }, request);
        }
      } catch (const std::exception &error) {
        extractionReady = false;
        extractionDraftError = error.what();
        if (orientationRange) {
          dialog.axisRangeOrientationPreview["can_apply"] = false;
          dialog.axisRangeOrientationPreview["error"] = extractionDraftError;
        }
      }
      if (!extractionReady) ImGui::TextWrapped("%s", extractionDraftError.c_str());
    }
    if (spacingRange || slicerRange || dialog.scientificExtraction || orientationRange)
      ImGui::BeginDisabled(!extractionReady ||
                           (spacingRange && !spacingRenderReady) ||
                           (slicerRange && !slicerApplyReady));
    if(dialog.id=="selection_enlargement" && dialog.enlargementSource){
      const auto &cal=dialog.enlargementSource->data().dataset.calibration();
      ImGui::TextWrapped("Selections retain their geometry beyond the image boundary.");
      ImGui::Text("Calibrated units: %s; X spacing: %.17g.",cal.unit.c_str(),cal.spacing[0]);
      if(dialog.values.value("units",std::string("pixels"))=="calibrated" && cal.spacing[0]!=cal.spacing[1]){
        ImGui::TextWrapped("X and Y scales differ. Calibrated distance uses X spacing: %.17g %s.",cal.spacing[0],cal.unit.c_str());
        dialog.controls["anisotropy_warning"]=itemControl(false);
      }
    }
    if(dialog.id=="selection_enlargement"){
      const bool valid=dialog.values.at("distance").is_number();
      if(!valid)ImGui::TextWrapped("Enter a finite numeric distance.");
      ImGui::BeginDisabled(!valid);
    }
    const bool submitClicked = ImGui::Button(integerRange ? "Submit" :
                                             dialog.operation ? "Apply" : "OK");
    dialog.controls["apply"] = itemControl(extractionReady && (!spacingRange || spacingRenderReady) && (!slicerRange || slicerApplyReady));
    if (spacingRange || slicerRange || dialog.scientificExtraction || orientationRange)
      ImGui::EndDisabled();
    if(dialog.id=="selection_enlargement")ImGui::EndDisabled();
    if (integerRange)
      dialog.controls["submit"] = dialog.controls["apply"];
    if (submitClicked)
      guard([&] {
        auto p = parse();
        dialog.submit(std::move(p));
        dialog.open = false;
        ImGui::CloseCurrentPopup();
      });
    dialog.applyPoint = itemPoint();
    if (dialog.operation) {
      ImGui::SameLine();
      if (ImGui::Button("Preview"))
        guard([&] {
          auto p = parse();
          auto d = doc();
          d->requireLegacyEditable();
          auto id = dialog.id;
          const auto source = d->nativeSnapshot();
          const auto memoryBudget = owner.controller_->job_scheduler()->limits().memory_budget;
          const bool poolPreview=id=="auto_mask" && p.value("stack_histogram",false) && allSlices;
          const auto shape = source.data().dataset.shape();
          const OperationDefaultContext context{
              shape.x, shape.y, shape.s, source.data().dataset.pixel_type(), {}};
          const auto extra = extraWorkspace(id, context, p);
          const auto workspace = legacy_operation_workspace(
              id, source, poolPreview, source.display().crosshair[2], extra, memoryBudget);
          start([source = d->snapshot(), id,
                 p = std::move(p),poolPreview] { return source.compute(id, p, poolPreview); },
                d, false, "", "Preview: " + dialog.title, workspace);
          jobPreview = true;
        });
      dialog.previewPoint = itemPoint();
      dialog.controls["preview"] = itemControl();
    }
    if (dialog.id == "axis_plane_export" || integerRange)
      ImGui::EndDisabled();
    ImGui::SameLine();
    const bool cancelClicked = ImGui::Button("Cancel");
    dialog.controls["cancel"] = itemControl();
    if (cancelClicked) {
      dialog.open = false;
      ImGui::CloseCurrentPopup();
    }
    if (dialog.id != "axis_plane_export" && !integerRange)
      ImGui::EndDisabled();
    if (dialog.preview) {
      ImGui::Separator();
      ImGui::Text("Preview of the current slice");
      const auto &preview = *dialog.preview;
      if (gl) {
        auto image = displayImage(preview, Json::object(), FloatingSamplePolicy::FiniteOnly);
        std::vector<unsigned char> pixels(image.pixels.size());
        for (size_t i = 0; i < pixels.size(); ++i)
          pixels[i] = static_cast<unsigned char>(image.pixels[i]);
        if (!dialog.texture)
          glGenTextures(1, &dialog.texture);
        glBindTexture(GL_TEXTURE_2D, dialog.texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image.width, image.height, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
      }
      float scale = std::min(400.f / preview.width, 240.f / preview.height);
      ImGui::Image(static_cast<ImTextureID>(gl ? dialog.texture : 1),
                   {preview.width * scale, preview.height * scale});
    }
    ImGui::EndPopup();
  }
  if(!dialog.open && dialog.id=="selection_enlargement"){dialog.enlargementSource.reset();dialog.submit={};}
  if (!dialog.open && dialog.id == "axis_plane_export") {
    dialog.axisPlaneSource.reset();
    dialog.submit = {};
  }
  if (!dialog.open && dialog.id == "axis_range_reslice") {
    dialog.axisRangeSource.reset();
    dialog.axisRangeCount.reset();
    dialog.submit = {};
  }
  if (!dialog.open && spacingRange) {
    dialog.axisRangeSpacingSource.reset();
    dialog.axisRangeCount.reset();
    dialog.axisRangeSpacingPreview = Json::object();
    dialog.submit = {};
  }
  if (!dialog.open && slicerRange) {
    dialog.axisRangeSlicerResizeSource.reset();
    dialog.axisRangeCount.reset();
    dialog.axisRangeSlicerResizePreview = Json::object();
    dialog.submit = {};
  }
  if (!dialog.open && orientationRange) {
    dialog.axisRangeOrientationSource.reset();
    dialog.axisRangeCount.reset();
    dialog.axisRangeOrientationPreview = Json::object();
    dialog.submit = {};
  }
  if(browser.open && browser.current && !browser.current()) {
    browser.open=false;browser.expired=true;
    status="ROI import file choice discarded because the destination or draft changed";
  }
  browser.entries=Json::array();
  if (browser.open)
    ImGui::OpenPopup(browser.title.c_str());
  ImGui::SetNextWindowSize({760, 500}, ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal(browser.title.c_str(), &browser.open)) {
    ImGui::TextUnformatted(!browser.prompt.empty()?browser.prompt.c_str():browser.save
                               ? "Choose the output path and file extension"
                               : "Choose an image, session, or JSON file");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##path", &browser.path);
    observeControl("browser.path");
    auto path = std::filesystem::u8path(browser.path);
    std::error_code error;
    auto folder =
        std::filesystem::is_directory(path, error) ? path : path.parent_path();
    if (folder.empty())
      folder = std::filesystem::current_path();
    if (ImGui::Button("Up"))
      browser.path = folder.parent_path().u8string();
    observeControl("browser.up");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220);
    ImGui::InputText("Filter", &browser.filter);
    observeControl("browser.filter");
    ImGui::BeginChild("files", {0, -40}, ImGuiChildFlags_Borders);
    guard([&] {
      std::vector<std::filesystem::directory_entry> entries;
      for (const auto &entry : std::filesystem::directory_iterator(folder)) {
        if(!entry.is_directory() && !browserFileAllowed(entry.path()))continue;
        if (browser.filter.empty() || entry.is_directory() ||
            entry.path().filename().u8string().find(browser.filter) !=
                std::string::npos)
          entries.push_back(entry);
      }
      std::sort(entries.begin(), entries.end(),
                [](const auto &a, const auto &b) {
                  if (a.is_directory() != b.is_directory())
                    return a.is_directory();
                  return a.path().filename() < b.path().filename();
                });
      for (const auto &entry : entries) {
        bool directory = entry.is_directory();
        auto label =
            (directory ? "[Folder] " : "") + entry.path().filename().u8string();
        const auto identity=entry.path().u8string();
        ImGui::PushID(static_cast<int>(ImHashData(identity.data(),identity.size())));
        const auto textPosition=ImGui::GetCursorScreenPos();
        const bool chosen=ImGui::Selectable("##entry",path==entry.path(),ImGuiSelectableFlags_AllowDoubleClick);
        ImGui::GetWindowDrawList()->AddText(textPosition,ImGui::GetColorU32(ImGuiCol_Text),label.c_str());
        if(headlessDiagnostics)browser.entries.push_back({{"name",entry.path().filename().u8string()},
          {"path",entry.path().u8string()},{"directory",directory},{"label",label},{"control",itemControl()}});
        if (chosen) {
          browser.path = entry.path().u8string();
          if (!directory && !browser.save && ImGui::IsMouseDoubleClicked(0)) {
            guard([&] {
              browser.submit(browser.path);
              browser.open = false;
              ImGui::CloseCurrentPopup();
            });
          }
        }
        ImGui::PopID();
      }
    });
    ImGui::EndChild();
    auto submit = [&] {
      if(browser.directory){if(!std::filesystem::is_directory(std::filesystem::u8path(browser.path)))throw std::invalid_argument("Choose an existing folder");browser.submit(browser.path);browser.open=false;ImGui::CloseCurrentPopup();return;}
      if (browser.path.empty() ||
          std::filesystem::is_directory(std::filesystem::u8path(browser.path)))
        throw std::invalid_argument("Choose a file path");
      if(!browserFileAllowed(std::filesystem::u8path(browser.path)))throw std::invalid_argument(browser.prompt);
      if (browser.save &&
          std::filesystem::exists(std::filesystem::u8path(browser.path))) {
        browser.overwrite = true;
        return;
      }
      browser.submit(browser.path);
      browser.open = false;
      ImGui::CloseCurrentPopup();
    };
    const bool allowed=browserFileAllowed(std::filesystem::u8path(browser.path));
    ImGui::BeginDisabled(!allowed);
    const bool submitted = ImGui::Button(browser.directory?"Choose folder":browser.save ? "Save" : "Open");
    observeControl("browser.submit",allowed);
    ImGui::EndDisabled();
    if (submitted)
      guard(submit);
    ImGui::SameLine();
    const bool canceled = ImGui::Button("Cancel");
    observeControl("browser.cancel");
    if (canceled) {
      browser.open = false;
      ImGui::CloseCurrentPopup();
    }
    if (browser.overwrite)
      ImGui::OpenPopup("Replace existing file?");
    if (ImGui::BeginPopupModal("Replace existing file?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextWrapped("%s", browser.path.c_str());
      const bool replaceFile = ImGui::Button("Replace");
      observeControl("browser.replace");
      if (replaceFile)
        guard([&] {
          browser.submit(browser.path);
          browser.overwrite = false;
          browser.open = false;
          ImGui::CloseCurrentPopup();
        });
      ImGui::SameLine();
      const bool cancelReplace = ImGui::Button("Cancel");
      observeControl("browser.replace_cancel");
      if (cancelReplace) {
        browser.overwrite = false;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
    ImGui::EndPopup();
  }
  if(!browser.open && !browser.overwrite) {browser.submit={};browser.current={};}
  if (pendingClose && !browser.open)
    ImGui::OpenPopup("Save changes?");
  if (ImGui::BeginPopupModal("Save changes?", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextWrapped("Save changes to %s?",
                       pendingClose ? pendingClose->title().c_str() : "image");
    if (headlessDiagnostics)
      closePrompt = {{"open", true},
          {"document_id", pendingClose ? pendingClose->id : 0},
          {"controls", Json::object()}};
    const bool saveClose = ImGui::Button("Save...");
    if (headlessDiagnostics)
      closePrompt["controls"]["save"] = itemControl();
    if (saveClose) {
      auto d = pendingClose;
      file(
          "Save document before closing", true,
          [this, d](std::string path) {
            saveDocument(d, path);
            if (!closingApp)
              remove(d);
            pendingClose.reset();
          },
          d->path.empty() ? d->title() + ".ijx" : d->path);
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    const bool discardClose = ImGui::Button("Discard");
    if (headlessDiagnostics)
      closePrompt["controls"]["discard"] = itemControl();
    if (discardClose) {
      if (closingApp)
        pendingClose->markSaved();
      else
        remove(pendingClose);
      pendingClose.reset();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    const bool cancelClose = ImGui::Button("Cancel");
    if (headlessDiagnostics)
      closePrompt["controls"]["cancel"] = itemControl();
    if (cancelClose) {
      closingApp = false;
      pendingClose.reset();
      ImGui::CloseCurrentPopup();
    }
    if (headlessDiagnostics && (saveClose || discardClose || cancelClose)) {
      closePrompt["open"] = false;
      closePrompt["response"] = saveClose ? "save" : (discardClose ? "discard" : "cancel");
    }
    ImGui::EndPopup();
  }
}
void Window::Impl::draw() {
  if(viewportJobs){
    viewportJobs->poll();
    if(!preferenceState.settings.boolean("async_viewport"))for(auto &pane:panes){
      // Virtual XYZ sampling requires workers even when ordinary async rendering
      // is disabled. Cancel ordinary views without canceling those XYZ jobs.
      if(pane.doc){const auto source=pane.doc->nativeSnapshot();const bool virtualXYZ=source.display().orthogonal && !source.data().dataset.has_eager_planes();
        viewportJobs->cancelView(pane.doc->id,0);if(!virtualXYZ)for(int view=1;view<=3;++view)viewportJobs->cancelView(pane.doc->id,view);
      }
      pane.sampledSurface.reset();pane.sampleMemory.reset();pane.viewportPending=false;
    }
  }
  for(auto &pane:panes)pane.viewportSeen=false;
  startupFramesDrawn = true;
  if (headlessDiagnostics) {
    errorModal = {{"open", false}, {"controls", Json::object()}};
    closePrompt = {{"open", false}, {"controls", Json::object()}};
  }
  documentTabs = Json::array();
  hostControls.clear();
  startupControls.clear();
  workflowControls.clear();
  owner.poll();
  advanceAnimation();
  if (shadowDemo.active() && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) shadowDemo.cancel("escape");
  if (savedRoiRebind) {
    try {
      const auto current = owner.nativeSnapshot();
      const auto &position = savedRoiRebind->position;
      if (owner.busy() || current.id() != savedRoiRebind->owner ||
          !savedRoiRebind->revisions.matches(current.revisions(), Change::Data) ||
          current.display().t != position.t || current.display().c != position.c ||
          current.display().crosshair[2] != position.z)
        cancelSavedRoiGeometry("rebind_source_changed");
    } catch (...) { cancelSavedRoiGeometry("owner_unavailable"); }
  }
  if (savedRoiEdit.active()) {
    try {
      savedRoiEdit.working_limit(savedRoiGeometryLimits().max_working_bytes);
      const auto current = owner.nativeSnapshot();
      if (owner.busy() || !savedRoiMatches(current) ||
          (savedRoiEdit.pointer_active() &&
           savedRoiEdit.source().revisions().display != current.revisions().display))
        cancelSavedRoiGeometry(owner.busy() ? "busy" : "source_changed");
    } catch (...) { cancelSavedRoiGeometry("owner_unavailable"); }
  }
  menu();
#ifdef IMGUI_HAS_DOCK
  ImGuiID dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
  workspaceDockId = dock;
  if (!layout && (!ImGui::DockBuilderGetNode(dock) ||
                  !ImGui::DockBuilderGetNode(dock)->IsSplitNode())) {
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    auto viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderSetNodeSize(dock, viewport->WorkSize);
    ImGuiID center = dock, left, right, bottom, rightBottom, bottomRight;
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, .13f, &left, &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, .30f, &right, &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, .28f, &bottom, &center);
    ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, .52f, &rightBottom,
                                &right);
    ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Right, .45f, &bottomRight,
                                &bottom);
    ImGui::DockBuilderDockWindow("Tools", left);
    ImGui::DockBuilderDockWindow("Images", center);
    ImGui::DockBuilderDockWindow("Image properties", right);
    ImGui::DockBuilderDockWindow("ROI manager", rightBottom);
    ImGui::DockBuilderDockWindow("Plot", rightBottom);
    ImGui::DockBuilderDockWindow("Results", bottom);
    ImGui::DockBuilderDockWindow("Workflow", bottomRight);
    ImGui::DockBuilderFinish(dock);
  }
#endif
  layout = true;
  ImGui::BeginDisabled(owner.busy());
  tools();
  properties();
  rois();
  ImGui::EndDisabled();
  resultTable();
  recorder();
  plots();
  const bool imagesVisible = ImGui::Begin("Images");
  shortcutWorkspace = ImGui::GetCurrentWindow();
  const bool imageEditingBlocked = owner.busy();
  // An idle workflow review owns a private draft, not the document tabs.
  // Activation already cancels that draft before switching its source. Keep
  // processing/child jobs and every other existing busy cause blocking tabs.
  const bool imageTabsBlocked = (!launchPublishing && !pendingLaunch.empty()) ||
      workflowOtherBusy() || nativeWorkflowPanel.working()
#ifdef IMAGEJXX_HAS_SCIENTIFIC_IO
      || (!scientificIoPublishing && (startupTiffs || scientificIoPanel.visible() ||
          scientificIoPanel.working() || !pendingFileOpens.empty()))
#endif
      ;
  ImGui::BeginDisabled(imageTabsBlocked);
  if (!imagesVisible) {
    resetInputs();
  } else if (owner.documents_.empty()) {
    ImGui::TextWrapped(
        "imagejxx\n\nOpen an image or create a document from the File "
        "menu.\nSupported files include PNG, TIFF, NPY and saved sessions.");
  } else if (ImGui::BeginTabBar("documents",
                                ImGuiTabBarFlags_Reorderable |
                                    ImGuiTabBarFlags_AutoSelectNewTabs)) {
    std::shared_ptr<Document> close;
    int force = owner.activeIndex() != lastVisible ? owner.activeIndex() : -1;
    for (size_t i = 0; i < panes.size(); ++i) {
      auto &pane = panes[i];
      ImGui::PushID(pane.doc.get());
      bool open = true;
      auto title =
          pane.doc->title() + (pane.doc->dirty() ? " *" : "") + "###document";
      auto flags = force == int(i) ? ImGuiTabItemFlags_SetSelected
                                   : ImGuiTabItemFlags_None;
      const bool visible = ImGui::BeginTabItem(title.c_str(),
          imageEditingBlocked ? nullptr : &open, flags);
      const auto tabMin = ImGui::GetItemRectMin(),
                 tabMax = ImGui::GetItemRectMax();
      documentTabs.push_back({{"document_id", pane.doc->id},
                              {"index", i},
                              {"editing_blocked", imageEditingBlocked},
                              {"close_enabled", !imageEditingBlocked},
                              {"id", ImGui::GetItemID()},
                              {"enabled", !(ImGui::GetCurrentContext()->LastItemData.ItemFlags & ImGuiItemFlags_Disabled)},
                              {"selected", visible}, {"visible", ImGui::IsItemVisible()},
                              {"active", ImGui::IsItemActive()}, {"focused", ImGui::IsItemFocused()},
                              {"rect", {tabMin.x, tabMin.y, tabMax.x, tabMax.y}},
                              {"point",
                               {tabMin.x + (tabMax.x - tabMin.x) / 4,
                                (tabMin.y + tabMax.y) / 2}}});
      if (visible) {
        // ImGui applies a requested tab on the next frame. Preserve the native
        // owner while a previous/default tab is still visible during setup.
        if (force < 0 || force == int(i)) {
          owner.setActiveIndex(int(i));
          ImGui::BeginDisabled(imageEditingBlocked);
          guard([&] { canvas(pane); });
          ImGui::EndDisabled();
        }
        ImGui::EndTabItem();
      } else if (pane.orthogonalWasVisible || pane.ordinaryCanvasVisible)
        resetInput(pane);
      if (!imageEditingBlocked && !open)
        close = pane.doc;
      ImGui::PopID();
    }
    ImGui::EndTabBar();
    lastVisible = owner.activeIndex();
    if (close)
      requestClose(close);
  } else
    resetInputs();
  ImGui::EndDisabled();
  ImGui::End();
  if (shadowDemoVisible) {
    ImGui::SetNextWindowSize({320,0},ImGuiCond_Appearing);
    if(ImGui::Begin("Shadows Demo",&shadowDemoVisible,ImGuiWindowFlags_AlwaysAutoResize)) {
      const auto info=shadowDemo.diagnostics();
      ImGui::TextUnformatted("Original image stays unchanged.");
      ImGui::ProgressBar(float(info.at("shown").get<int>())/160.f);
      ImGui::Text("%d / 20 cycles",info.at("shown").get<int>()/8);
      if(!info.at("direction").is_null())ImGui::Text("Direction: %s",info.at("direction").get<std::string>().c_str());
      if(!info.at("error").get<std::string>().empty())ImGui::TextWrapped("%s",info.at("error").get<std::string>().c_str());
      else if(!shadowDemo.active())ImGui::TextUnformatted(info.at("reason")=="completed"?"Demo finished.":"Demo stopped.");
      if(ImGui::Button(shadowDemo.active()?"Stop":"Close")) {if(shadowDemo.active())shadowDemo.cancel("stopped");shadowDemoVisible=false;}
      observeControl("shadow_demo.stop");
    }
    ImGui::End();
    if(!shadowDemoVisible && shadowDemo.active())shadowDemo.cancel("closed");
  }
  if (const auto processingJob = owner.jobInfo()) {
    ImGui::SetNextWindowSize({360, 0}, ImGuiCond_Appearing);
    ImGui::Begin("Processing", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextUnformatted("Processing image...");
    const auto info = *processingJob;
    ImGui::TextUnformatted(info.state == JobState::Queued ? "Queued"
                                                          : "Running");
    ImGui::ProgressBar(info.total ? float(double(info.completed) / info.total)
                                  : 0.f);
    if (ImGui::Button("Cancel"))
      owner.cancel();
    ImGui::TextWrapped(
        "Cancellation stops at a safe processing checkpoint. "
        "An external reader or codec may finish its current call.");
    ImGui::End();
  }
  if (closingApp && !pendingClose && !owner.busy()) {
    auto found = std::find_if(owner.documents_.begin(), owner.documents_.end(),
                              [](const auto &d) { return d->dirty(); });
    if (found == owner.documents_.end())
      exit = true;
    else
      pendingClose = *found;
  }
  guard([&]{
    tickContrast();
    const bool wasVisible=contrastPanel.visible();
    contrastPanel.draw(owner.busy(),
      [this](const DocumentSnapshot &source,std::array<double,2> range){publishContrast(source,range);},
      [this](const DocumentSnapshot &source,bool all,bool selection){contrastAuto(source,all,selection);},
      [this](int c){owner.action("view",{{"c",c}});},owner.settings().conversion_options().unsigned16_bits,
      [this](const DocumentSnapshot &source,std::array<double,2> range,const TargetRequest &target){applyContrast(source,range,target);},
      bool(contrastApplyJob),[this]{cancelContrastApply();});
    if(wasVisible && !contrastPanel.visible())cancelContrastApply();
    if(wasVisible && !contrastPanel.visible() && !owner.documents_.empty())for(auto &pane:panes)if(pane.doc->id==owner.controller_->active_document_id())pane.requestOrdinaryFocus=true;
  });
  guard([&]{
    const bool wasVisible=contrastPanel.rebindVisible();
    contrastPanel.drawRebind(gaussianCurrent(),owner.busy(),
      [this](const DocumentSnapshot &source,const MetadataObject &record,const TargetRequest &target){applyContrastRebind(source,record,target);});
    if(wasVisible && !contrastPanel.rebindVisible() && !owner.documents_.empty())for(auto &pane:panes)if(pane.doc->id==owner.controller_->active_document_id())pane.requestOrdinaryFocus=true;
  });
  // Draw floating selection controls after document windows so opening the
  // panel gives it keyboard focus. Keep Cancel outside busy document controls.
  guard([&] {
    const auto current = gaussianCurrent();
    channelDisplayPanel.draw(current ? &*current : nullptr, owner.busy(),
        [this](const DocumentSnapshot &source) {
          if (owner.busy()) throw std::invalid_argument("Wait for the current edit");
          applyChannelPanel(source, {ChannelDisplayAction::Set, owner.initialChannelDisplay(source), {}});
        },
        [this](const DocumentSnapshot &source, const ChannelDisplayRequest &request) {
          bool applied = false;
          guard([&] { applyChannelPanel(source, request); applied = true; });
          return applied;
        },
        [this](int c) { owner.action("view", {{"c", c}}); },
        [this] {
          const auto source = owner.nativeSnapshot();
          const auto state = recognized_channel_display(source.display(), source.data().dataset);
          if (!state) throw std::invalid_argument("Enable channel display before importing a LUT");
          file("Import LUT", false, [this, source, state](std::string path) {
            auto next = *state;
            next.channels.at(source.display().c).lut =
                inheritedDisplayLut({{"custom_lut", readJson(path).at("lut")}});
            applyChannelPanel(source, {ChannelDisplayAction::Set, std::move(next), {}});
          });
        },
        [this](const DocumentSnapshot &source, bool allChannels, bool useSelection) {
          auto options = autoOptions(allChannels);
          options.roi = useSelection ? AutoContrastRoiPolicy::ActiveSelection
                                    : AutoContrastRoiPolicy::WholePlane;
          owner.applyAutoContrast(source, options);
        });
  });
  guard([&] {
    const bool visible=histogramPanel.visible();
    histogramPanel.draw(owner.documents_.empty()?std::nullopt:std::optional<DocumentSnapshot>{owner.nativeSnapshot()},owner.busy(),
      [this](std::string text){file("Export histogram CSV",true,[text=std::move(text)](std::string path){writeBytes(path,{text.begin(),text.end()});},"histogram.csv");},[this]{beginHistogram();},histogramTask?std::optional<JobInfo>{histogramTask->job.info()}:std::nullopt,[this]{cancelHistogram();});
    if(visible && !histogramPanel.visible() && !owner.documents_.empty())
      for(auto &pane:panes)if(pane.doc && pane.doc->id==owner.controller_->active_document_id())pane.requestOrdinaryFocus=true;
  });
  guard([&] {
    const bool wasVisible = thresholdPanel.visible();
    thresholdPanel.draw(thresholdCurrent(), owner.busy(),
        {[this](ManualThresholdRange range) { thresholdSet(range); },
         [this] { thresholdReset(); },
         [this](ManualThresholdRange range) { thresholdToSelection(range); },
         [this](ManualThresholdRange range) { thresholdApply(range); },
         [this] { thresholdCancel(); },
           [this](const std::string &method,bool bright,bool black,bool white,bool pooled) { return thresholdAutomatic(method,bright,black,white,pooled); },
           [this] { thresholdApplyPreview(); },
           [this](PlanePosition position) {const auto source=checkedThresholdSource();const auto live=owner.nativeSnapshot(source.id());auto display=live.display();display.t=position.t;display.c=position.c;display.crosshair[2]=position.z;owner.controller_->set_display_if_current(live,std::move(display));}});
    // A closed floating panel can retain NavWindow/ActiveId for a frame and
    // suppress document shortcuts. Hand focus to the current document on the
    // next canvas submission, including when it differs from the capture owner.
    if (wasVisible && !thresholdPanel.visible() && !owner.documents_.empty())
      for (auto &pane : panes)
        if (pane.doc && pane.doc->id == owner.controller_->active_document_id())
          pane.requestOrdinaryFocus = true;
  });
  guard([&] {
    gaussianPanel.draw(gaussianCurrent(), owner.busy(),
        {[this](const ResolvedGaussianBlurCommand &command, std::uint64_t generation) {
           requestGaussian(command, generation);
         },
         [this] { cancelGaussianPreview(); },
         [this](const ResolvedGaussianBlurCommand &command) { requestGaussian(command); },
         [this](PlanePosition position) { showPlaneOperationPreview(position); },
         [this](PlanePosition position) { return planeOperationPreviewVisible(position); }});
  });
  guard([&] {
    rankPanel.draw(gaussianCurrent(), owner.busy(),
        {[this](const ResolvedRankFilterCommand &command, std::uint64_t generation) {
           rankCallback("preview", command.target().document_id());
           requestRank(command, generation);
         },
         [this] { cancelGaussianPreview(); },
         [this](const ResolvedRankFilterCommand &command) {
           rankCallback("apply", command.target().document_id());
           requestRank(command);
         },
         [this](PlanePosition position) { showPlaneOperationPreview(position); },
         [this](PlanePosition position) { return planeOperationPreviewVisible(position); }});
  });
  guard([&] {
    auto cap=static_cast<std::size_t>(preferenceState.settings.integer("processing_memory_mb"))*1024*1024;if(!cap)cap=256*1048576;
    displayAnnotationPanel.draw(owner.documents_.empty()?nullptr:owner.document().get(),owner.busy(),cap,
      [this](const ResolvedPlanarDisplayAnnotationCommand &command){publishDisplayAnnotation(command);},
      [this]{auto metadata=owner.document()->state().metadata;if(owner.nativeSnapshot().display().orthogonal){const auto views=panes[owner.activeIndex()].orthogonal.diagnostics();if(!views.empty())metadata["display_range"]=views.at(0).at("range");}return metadata;});
  });
  guard([&] { drawSavedRoiRebind(); });
  guard([&] { planarAnnotationRebind.draw(owner.documents_.empty()?std::optional<DocumentSnapshot>{}:
      std::optional<DocumentSnapshot>{owner.nativeSnapshot()},owner.busy(),
      [this](const ResolvedPlanarAnnotationCommand &command){publishPlanarAnnotation(command);}); });
  guard([&] {
    auto cap=static_cast<std::size_t>(preferenceState.settings.integer("processing_memory_mb"))*1024*1024;
    if(!cap)cap=256*1048576;
    roiImportPanel.draw(owner.documents_.empty()?std::optional<DocumentSnapshot>{}:std::optional<DocumentSnapshot>{owner.nativeSnapshot()},owner.busy(),cap,
      [this]{browseRoiImport();},
      [this](Plane plane,int coordinate,std::optional<int> t,std::optional<int> c) {
        auto view=owner.nativeSnapshot().display();if(t)view.t=*t;if(c)view.c=*c;
        const auto geometry=planar_annotation_geometry(owner.nativeSnapshot().data().dataset.shape(),plane);
        view.crosshair[geometry.fixed_axis]=coordinate;view.orthogonal=true;owner.setNativeDisplay(std::move(view));
      },
      [this](const ResolvedImageJRoiImport &command){publishImageJRoiImport(command);},
      [this](const ResolvedImageJAnnotationImport &command){publishImageJAnnotationImport(command);});
  });
  guard([&] {
    auto cap=static_cast<std::size_t>(preferenceState.settings.integer("processing_memory_mb"))*1024*1024;
    if(!cap)cap=256*1048576;
    movieExportPanel.draw(owner.documents_.empty()?std::optional<DocumentSnapshot>{}:std::optional<DocumentSnapshot>{owner.nativeSnapshot()},cap);
    roiExportPanel.draw(owner.documents_.empty()?std::optional<DocumentSnapshot>{}:std::optional<DocumentSnapshot>{owner.nativeSnapshot()},owner.busy(),cap,
      [this]{browseRoiExport();},
      [this](const PlanarAnnotation &row){
        auto view=owner.nativeSnapshot().display();
        if(row.t)view.t=*row.t;if(row.c)view.c=*row.c;
        const auto geometry=planar_annotation_geometry(owner.nativeSnapshot().data().dataset.shape(),row.plane);
        view.crosshair[geometry.fixed_axis]=row.coordinate;view.orthogonal=true;owner.setNativeDisplay(std::move(view));
      },
      [this](const ResolvedImageJRoiExport &command,const std::function<void()> &publishFile){publishImageJRoiExport(command.pairing().source(),command.record(),publishFile);},
      [this](const ResolvedImageJRoiZipExport &command,const std::function<void()> &publishFile){publishImageJRoiExport(command.source(),command.record(),publishFile);},
      [this](const PlanarDisplayAnnotation &row){
        auto view=owner.nativeSnapshot().display();if(row.t)view.t=*row.t;if(row.c)view.c=*row.c;
        const auto geometry=planar_annotation_geometry(owner.nativeSnapshot().data().dataset.shape(),row.plane);
        view.crosshair[geometry.fixed_axis]=row.coordinate;view.orthogonal=true;owner.setNativeDisplay(std::move(view));
      },
      [this](const ResolvedImageJAnnotationExport &command,const std::function<void()> &publishFile){publishImageJRoiExport(command.source(),command.record(),publishFile);});
  });
  guard([&]{
    auto cap=std::size_t(preferenceState.settings.integer("processing_memory_mb"))*1048576;if(!cap)cap=256*1048576;
    nativeWorkflowPanel.draw(owner.documents_.empty()?std::optional<DocumentSnapshot>{}:std::optional<DocumentSnapshot>{owner.nativeSnapshot()},workflowOtherBusy(),cap,owner.recording,owner.workflow,
      [this]{browseNativeWorkflow();},[this,cap](const PreparedNativeWorkflow &plan,const std::function<void()> &publishFiles){
        if(workflowOtherBusy()||owner.controller_->active_document_id()!=plan.source().id())throw std::invalid_argument("Workflow publication requires its idle active source");
        const auto current=owner.nativeSnapshot();if(!plan.source().same_document(current)||!current.revisions().matches(plan.source().revisions(),plan.dependencies()))throw RevisionConflict();
        auto edit=plan.begin_edit();const auto preview=edit.preview();
        admit_planar_annotation_visuals(preview.data().dataset.shape(),planar_annotations(preview.data()),cap);
        admit_display_annotation_visuals(planar_display_annotations(preview.data()),cap);
        if(!owner.workflow.is_array()||!owner.results.is_array())throw std::invalid_argument("Workflow and results must remain arrays");
        OutputPublication outputs(owner.results,owner.workflow,{},"",nativeWorkflowPanel.recorded_operation());
        if(!plan.histograms().empty()) {
          outputs.rows=Json::array();
          for(const auto &out:plan.histograms()) {
            const auto &v=out.value;auto number=[](const std::optional<double> &n){return n?Json(*n):Json(nullptr);};
            outputs.rows.push_back({{"Label",out.name},{"WorkflowStep",out.step_index+1},{"Count",v.finite_count},
              {"Mean",number(v.mean)},{"StdDev",number(v.stddev)},{"Min",number(v.minimum)},{"Max",number(v.maximum)},
              {"Sum",v.sum},{"NaN",v.nan_count},{"Underflow",v.underflow_count},{"Overflow",v.overflow_count}});
          }
          outputs.resultArray=OutputPublication::reserve(owner.results,outputs.rows.size(),"Results");
        }
        publishFiles();owner.document()->commitPlanarNative(edit);outputs.publish();
      },[this](const DocumentSnapshot &source,const std::function<void()> &publishFiles){
        if(workflowOtherBusy()||owner.controller_->active_document_id()!=source.id())throw std::invalid_argument("Batch publication requires its idle active anchor");
        const auto current=owner.nativeSnapshot();if(!source.same_document(current)||!current.revisions().matches(source.revisions(),Change::Data))throw RevisionConflict();
        if(!owner.workflow.is_array()||!owner.results.is_array())throw std::invalid_argument("Workflow and results must remain arrays");
        publishFiles();
      },[this](const DocumentSnapshot &source,Json program){
        if(workflowOtherBusy()||owner.controller_->active_document_id()!=source.id())throw std::invalid_argument("Program retention requires its idle active source");
        const auto current=owner.nativeSnapshot();if(!source.same_document(current)||!source.revisions().matches(current.revisions(),Change::Data))throw RevisionConflict();
        if(!owner.workflow.is_array()||!program.is_array()||program.size()!=1)throw std::invalid_argument("Invalid portable workflow proposal");
        owner.workflow.swap(program);
      },gl,headlessDiagnostics);
  });
  selectionPanel.working_limit(selectionCreationLimits().max_working_bytes);
  selectionPanel.draw(
      owner.documents_.empty()
          ? std::optional<DocumentSnapshot>{}
          : std::optional<DocumentSnapshot>{owner.nativeSnapshot()},
      owner.busy(),
      [&](const Selection &value, const DocumentSnapshot &source) {
        if (selectionPanel.geometry_only()) {
          applySavedRoiNumeric(value);
          return;
        }
        if (source.data().selection && value == *source.data().selection)
          return;
        if(selectionPanel.path_edited())publishAuthoredPath(source,value);
        else {auto edit=source.begin_edit(Change::Data);edit.select(value);owner.commitEdit(edit);}
      });
  if (savedRoiEdit.active() && !savedRoiEdit.pointer_active()) {
    if (!selectionPanel.visible()) {
      if (savedRoiPanelShown) cancelSavedRoiGeometry("panel_closed");
    }
    else if (!selectionPanel.preview()) savedRoiEdit.invalidate_preview();
    else if (!savedRoiEdit.candidate() ||
             !(*savedRoiEdit.candidate() == *selectionPanel.preview()))
      guard([&] { savedRoiEdit.stage_numeric(*selectionPanel.preview()); });
  }
  dialogs();
  jobsPanel.draw(*owner.controller_->job_scheduler(),
                 [this](std::uint64_t id) {
                   if (rankProbe) {
                     const auto stamp = rankRuntimeClockStamp();
                     rankProbe->event("cancel_callback", rankCancelFields(id, stamp));
                   }
                   owner.cancelJob(id);
                   if (rankProbe) {
                     const auto stamp = rankRuntimeClockStamp();
                     rankProbe->event("cancel_return", rankCancelFields(id, stamp));
                   }
                 });
  if (startupStatus == "failed") {
    StartupPanelActions actions;
    actions.retry = [this] { guard([&] { owner.retryStartup(); }); };
    actions.choose_workspace = [this] {
      file("Choose startup workspace", false, [this](std::string path) {
        const auto absolute = std::filesystem::absolute(std::filesystem::u8path(path))
                                  .lexically_normal().u8string();
        owner.updateSettings({{"startup_mode", "workspace"},
                              {"startup_workspace", absolute}}, true);
      });
    };
    actions.start_empty = [this] { guard([&] { owner.startEmptyStartup(); }); };
    actions.disable_restore = [this] { guard([&] { owner.disableStartupRestore(); }); };
    // A held banner button must not disable itself or shift the banner before
    // release. Public Retry still rejects every other active interaction.
    drawStartupPanel(startupDiagnostics, startupRetryAllowed(true), actions,
      [this](const std::string &id) {
        observeControl(id);
        if (headlessDiagnostics) startupControls[id] = hostControls[id];
        if (id == "startup.retry") startupRetryId = ImGui::GetItemID();
        if (id == "startup.choose_workspace") startupChooseWorkspaceId = ImGui::GetItemID();
        if (id == "startup.start_empty") startupStartEmptyId = ImGui::GetItemID();
        if (id == "startup.disable_restore") startupDisableRestoreId = ImGui::GetItemID();
      });
  } else if (!startupDiagnostics.empty() && !startupNoticesDismissed &&
             startupStatus != "started_empty" && startupStatus != "disabled") {
    if (ImGui::Begin("Workspace notices###startup.notices")) {
      for (const auto &diagnostic : startupDiagnostics) {
        if (!diagnostic.path.empty()) ImGui::TextWrapped("%s", diagnostic.path.c_str());
        ImGui::TextWrapped("%s", diagnostic.detail.c_str());
      }
      if (ImGui::Button("Dismiss###startup.dismiss")) startupNoticesDismissed = true;
      observeControl("startup.dismiss");
      if (headlessDiagnostics) startupControls["startup.dismiss"] = hostControls["startup.dismiss"];
    }
    ImGui::End();
  }
  settingsPanel.draw(
      owner.settings(),
      [this](const Settings &settings) {
        auto values = settingsJson(settings);
        const auto path = settings.text("startup_workspace");
        if (!path.empty()) {
          // Consume the native lexical policy for manual input too. In
          // particular, a different drive's relative path must be refused
          // rather than persisted as if it were an absolute selection.
          StartupRequest pathRequest;
          pathRequest.explicit_workspace = path;
          pathRequest.launch_cwd = std::filesystem::u8path(preferencesPath)
                                       .parent_path().u8string();
          values["startup_workspace"] =
              plan_startup(PreferencesDocument{}, pathRequest).paths.front();
        }
        owner.updateSettings(values);
      },
      [this](bool save) {
        file(
            save ? "Export preferences" : "Import preferences", save,
            [this, save](std::string path) {
              owner.action(save ? "export_preferences" : "import_preferences",
                           {{"path", path}});
            },
            save ? "preferences.json" : "");
      },
      [this] {
        file("Choose startup workspace", false, [this](std::string path) {
          settingsPanel.accept_startup_workspace(
              std::filesystem::absolute(std::filesystem::u8path(path)).lexically_normal());
        });
      },
      [this] { owner.resetLayout(true); });
  // All current-frame panels, popups and local per-key owners have run.
  guard([&] { dispatchShortcuts(); });
  auto viewport = ImGui::GetMainViewport();
  float statusHeight = ImGui::GetFontSize() + 6 * guiScale;
  ImGui::SetNextWindowPos(
      {viewport->Pos.x, viewport->Pos.y + viewport->Size.y - statusHeight});
  ImGui::SetNextWindowSize({viewport->Size.x, statusHeight});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(6 * guiScale, 2 * guiScale));
  ImGui::Begin("Status", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoInputs |
                   ImGuiWindowFlags_NoFocusOnAppearing);
  ImGui::TextUnformatted(status.c_str());
  ImGui::End();
  ImGui::PopStyleVar();
  if(viewportJobs)for(auto &pane:panes)if(!pane.viewportSeen)viewportJobs->cancelDocument(pane.doc->id);
}
namespace {
void setupGui(bool fonts, const Settings &settings, float scale,
              bool rebuildFonts) {
  auto &io = ImGui::GetIO();
  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
  io.IniFilename = nullptr;
  ImGui::GetStyle() = ImGuiStyle();
  if (settings.text("theme") == "light")
    ImGui::StyleColorsLight();
  else if (settings.text("theme") == "classic")
    ImGui::StyleColorsClassic();
  else
    ImGui::StyleColorsDark();
  auto &style = ImGui::GetStyle();
  style.FrameRounding = 4;
  style.GrabRounding = 4;
  style.WindowRounding = 5;
  style.FramePadding = {7, 5};
  if (settings.text("theme") == "dark") {
    style.Colors[ImGuiCol_WindowBg] = {.09f, .105f, .13f, 1};
    style.Colors[ImGuiCol_TitleBgActive] = {.13f, .19f, .28f, 1};
    style.Colors[ImGuiCol_HeaderActive] = {.13f, .38f, .62f, 1};
  }
  style.ScaleAllSizes(scale);
  if (!rebuildFonts)
    return;
  io.Fonts->Clear();
  bool loaded = false;
#ifdef _WIN32
  if (fonts) {
    const char *windows = std::getenv("WINDIR");
    auto font =
        std::string(windows ? windows : "C:/Windows") + "/Fonts/segoeui.ttf";
    if (std::filesystem::exists(std::filesystem::u8path(font)))
      loaded =
          io.Fonts->AddFontFromFileTTF(font.c_str(), 17 * scale) != nullptr;
  }
#else
  (void)fonts;
#endif
  if (!loaded) {
    ImFontConfig config;
    config.SizePixels = 13 * scale;
    io.Fonts->AddFontDefault(&config);
  }
  unsigned char *pixels;
  int width, height;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  if (!fonts)
    io.Fonts->SetTexID(1);
}
} // namespace
void Window::Impl::appearance(double dpi, bool systemFonts) {
  const auto &settings = preferenceState.settings;
  if (!std::isfinite(dpi) || dpi <= 0)
    dpi = 1;
  float scale = static_cast<float>(settings.number("ui_scale") *
                                   (settings.boolean("auto_dpi") ? dpi : 1));
  if (scale == guiScale && guiTheme == settings.text("theme"))
    return;
  bool fonts = scale != guiScale;
  if (gl && fonts)
    ImGui_ImplOpenGL3_DestroyFontsTexture();
  setupGui(systemFonts, settings, scale, fonts);
  if (gl && fonts)
    ImGui_ImplOpenGL3_CreateFontsTexture();
  guiScale = scale;
  guiTheme = settings.text("theme");
}
Json Window::Impl::headlessReport() {
    auto &io = ImGui::GetIO();
    auto draw = ImGui::GetDrawData();
    Json result = {{"vertices", draw->TotalVtxCount},
              {"indices", draw->TotalIdxCount},
              {"draw_lists", draw->CmdListsCount},
              {"mask_outline_builds", maskOutlineBuilds},
              {"imgui", IMGUI_VERSION}};
    result["document_tabs"] = documentTabs;
    result["host_controls"] = hostControls;
    result["animation"] = animationDiagnostics();
    result["workflow_controls"] = workflowControls;
    result["roi_manager"] = roiManagerDiagnostics();
    result["error_modal"] = errorModal;
    result["close_prompt"] = closePrompt;
    result["window_error"] = lastError;
    result["status"] = status;
    Json browserControls = Json::object();
    for (const auto *name : {"path", "submit", "cancel", "up", "filter", "replace", "replace_cancel"}) {
      const auto key = std::string("browser.") + name;
      if (hostControls.contains(key)) browserControls[name] = hostControls[key];
    }
    result["file_browser"] = {{"open", browser.open}, {"title", browser.title},
        {"save", browser.save}, {"overwrite",browser.overwrite}, {"path", browser.path}, {"controls", browserControls},
        {"filter",browser.filter},{"prompt",browser.prompt},{"extensions",browser.extensions},
        {"expired",browser.expired},{"entries",browser.entries}};
    result["viewport_jobs"]=viewportJobs?viewportJobs->diagnostics():Json(nullptr);
    result["session_extensions"] = sessionExtensions;
    result["startup"] = owner.startupState();
    result["startup_controls"] = startupControls;
    result["dialog"] = {{"open", dialog.open}, {"title", dialog.title},
        {"is_operation", dialog.operation}, {"values", dialog.values},
        {"text", dialog.text}, {"axis_index_label", dialog.axisIndexLabel},
        {"schema", dialog.schema}, {"generation", dialog.generation},
        {"opened_document_id", dialog.openedDocumentId},
        {"opened_document_id_semantics", (dialog.id == "grouped_z_projection" || dialog.id == "z_projection" || dialog.id == "axis_plane_export" || dialog.id == "axis_range_reslice" || dialog.id == "axis_range_orientation" || dialog.id == "axis_range_spacing" || dialog.id == "axis_range_slicer_resize")
             ? "captured_native_execution_source"
             : "observed_at_ask_not_a_captured_execution_source"},
        {"controls", dialog.controls}};
    if(dialog.id=="selection_enlargement"){
      result["dialog"]["opened_document_id_semantics"]="captured_native_execution_source";
      result["dialog"]["retained_source"]=dialog.enlargementSource.has_value();
      if(dialog.enlargementSource){const auto &source=*dialog.enlargementSource;result["dialog"]["captured_t"]=source.display().t;result["dialog"]["captured_c"]=source.display().c;result["dialog"]["captured_z"]=source.display().crosshair[2];}
    }
    if (const auto source = dialog.axisPlaneSource ? &*dialog.axisPlaneSource :
                            dialog.axisRangeSource ? &*dialog.axisRangeSource :
                            dialog.axisRangeOrientationSource ? &*dialog.axisRangeOrientationSource :
                            dialog.axisRangeSpacingSource ? &*dialog.axisRangeSpacingSource :
                            dialog.axisRangeSlicerResizeSource ? &*dialog.axisRangeSlicerResizeSource : nullptr) {
      const auto shape = source->data().dataset.shape();
      result["dialog"]["captured_t"] = source->display().t;
      result["dialog"]["captured_c"] = source->display().c;
      result["dialog"]["captured_channel_label"] = "Channel " +
          std::to_string(source->display().c + 1) + " / " + std::to_string(shape.c);
      result["dialog"]["captured_time_label"] = "Time point " +
          std::to_string(source->display().t + 1) + " / " + std::to_string(shape.t);
      result["dialog"]["is_extraction_rebind"] = !dialog.extractionRebindRecord.is_null();
      result["dialog"]["rebind_record"] = dialog.extractionRebindRecord;
    }
    if (dialog.id == "axis_range_reslice") {
      result["dialog"]["fixed_axis_label"] = dialog.axisRangeLabel;
      result["dialog"]["emitted_plane_count"] = dialog.axisRangeCount
          ? Json(*dialog.axisRangeCount) : Json(nullptr);
      result["dialog"]["retained_source"] = dialog.axisRangeSource.has_value();
    }
    if (dialog.id == "axis_range_spacing") {
      result["dialog"]["fixed_axis_label"] = dialog.axisRangeLabel;
      result["dialog"]["emitted_plane_count"] = dialog.axisRangeCount ? Json(*dialog.axisRangeCount) : Json(nullptr);
      result["dialog"]["retained_source"] = dialog.axisRangeSpacingSource.has_value();
      result["dialog"]["spacing_preview"] = dialog.axisRangeSpacingPreview;
    }
    if (dialog.id == "axis_range_slicer_resize") {
      result["dialog"]["fixed_axis_label"] = dialog.axisRangeLabel;
      result["dialog"]["emitted_plane_count"] = dialog.axisRangeCount ? Json(*dialog.axisRangeCount) : Json(nullptr);
      result["dialog"]["retained_source"] = dialog.axisRangeSlicerResizeSource.has_value();
      result["dialog"]["slicer_resize_preview"] = dialog.axisRangeSlicerResizePreview;
    }
    if (dialog.id == "axis_range_orientation") {
      result["dialog"]["fixed_axis_label"] = dialog.axisRangeLabel;
      result["dialog"]["emitted_plane_count"] = dialog.axisRangeCount ? Json(*dialog.axisRangeCount) : Json(nullptr);
      result["dialog"]["retained_source"] = dialog.axisRangeOrientationSource.has_value();
      result["dialog"]["orientation_preview"] = dialog.axisRangeOrientationPreview;
      for (auto field = dialog.axisRangeOrientationPreview.begin(); field != dialog.axisRangeOrientationPreview.end(); ++field)
        result["dialog"][field.key()] = field.value();
    }
    result["workflow_replay"] = workflowReplayLineage
        ? workflowReplayLineage->diagnostics() : Json(nullptr);
    result["workflow_replay_observation_failed"] = workflowReplayObservationFailed;
    result["settings"] = settingsJson(owner.settings());
    result["shortcuts"] = {{"profile", shortcutProfileState},
                            {"dispatch", shortcutObservation}};
    result["preferences_path"] = preferencesPath;
    result["preferences_controls"] = settingsPanel.visibleNames();
    result["preferences_panel"] = settingsPanel.diagnostics();
    result["jobs_panel"] = jobsPanel.diagnostics();
    result["selection_properties"] = selectionPanel.diagnostics();
    result["saved_roi_geometry"] = savedRoiEdit.diagnostics();
    result["saved_roi_geometry"]["cancel_reason"] = savedRoiCancelReason;
    result["display_annotation_panel"] = displayAnnotationPanel.diagnostics();
    result["planar_annotation_rebind"] = planarAnnotationRebind.diagnostics();
    result["saved_roi_geometry_rebind"] = {
        {"open", savedRoiRebind.has_value()},
        {"choosing_row", savedRoiRebind && savedRoiRebind->choosing},
        {"document_id", savedRoiRebind ? Json(savedRoiRebind->owner) : Json()},
        {"row_index", savedRoiRebind && savedRoiRebind->row ? Json(*savedRoiRebind->row) : Json()},
        {"controls", savedRoiRebindControls}};
    result["gaussian_panel"] = gaussianPanel.diagnostics();
    result["rank_panel"] = rankPanel.diagnostics();
    result["roi_import"] = roiImportPanel.diagnostics();
    result["native_workflow"] = nativeWorkflowPanel.diagnostics();
    result["rgb_dataset"] = rgbDatasetPanel.diagnostics();
    result["color_stack"] = colorStackPanel.diagnostics();
    result["indexed_color"] = indexedColorPanel.diagnostics();
#ifdef IMAGEJXX_HAS_SCIENTIFIC_IO
    result["scientific_io"] = scientificIoPanel.diagnostics();
    result["scientific_io"]["queued_paths"] = pendingFileOpens;
    result["scientific_io"]["startup_batch"] = {{"active",bool(startupTiffs)},
      {"accepted",startupTiffs?startupTiffs->next:0},{"tiff_count",startupTiffs?startupTiffs->entries.size():0}};
#endif
    result["roi_export"] = roiExportPanel.diagnostics();
    result["movie_export"] = movieExportPanel.diagnostics();
    result["threshold_panel"] = thresholdPanel.diagnostics();
    result["histogram_panel"] = histogramPanel.diagnostics();
    result["contrast_panel"]=contrastPanel.diagnostics();
    result["contrast_apply_rebind"]=contrastPanel.rebindDiagnostics();
    result["contrast_apply"]={{"busy",bool(contrastApplyJob)},{"owner_id",contrastApplySource?Json(contrastApplySource->id()):Json(nullptr)},{"recording_captured",contrastApplyRecording}};
    result["histogram_processing"]=histogramDiagnostics();
    result["channel_display_panel"] = channelDisplayPanel.diagnostics();
    result["auto_contrast"] = {{"busy", bool(autoJob)}, {"owner_id", autoOwner},
        {"divisor", owner.controller_->auto_contrast_divisor()},
        {"recording_captured", autoRecording}};
    result["selection_creation"] = creationDiagnostics();
    result["path_authoring"] = pathAuthoring.diagnostics();
    result["path_transform"] = {{"enabled",pathTransform},{"aspect",pathAspect},{"center",pathCenter}};
    result["channel_display_rebind"] = channelDisplayRebind ? Json{
        {"record", metadataJson(*channelDisplayRebind)},
        {"document_id", channelDisplayRebindOwner ? Json(*channelDisplayRebindOwner) : Json(nullptr)}} : Json(nullptr);
    result["gaussian_jobs"] = metadataJson(gaussianJobs.diagnostics(true));
    result["workflow_steps"] = owner.workflow.size();
    result["workflow"] = owner.workflow;
    result["gui_scale"] = guiScale;
    result["font_size"] = io.Fonts->Fonts[0]->FontSize;
    result["window_background"] = {
        ImGui::GetStyle().Colors[ImGuiCol_WindowBg].x,
        ImGui::GetStyle().Colors[ImGuiCol_WindowBg].y,
        ImGui::GetStyle().Colors[ImGuiCol_WindowBg].z};
    result["plot_samples"] = plotX.size();
    result["results_columns"] = resultColumns(owner.results, owner.settings());
    result["results_table"] = resultsTableObservation;
    result["shadow_demo"] = shadowDemo.diagnostics();
    result["operation_dialog"] = {
        {"id", dialog.id},
        {"open", dialog.open && dialog.operation},
        {"parameters", dialog.values},
        {"controls", dialog.controls},
        {"apply_point", {dialog.applyPoint.x, dialog.applyPoint.y}},
        {"preview_point",
         {dialog.previewPoint.x, dialog.previewPoint.y}},
        {"has_preview", dialog.preview.has_value()}};
    if (dialog.preview && dialog.preview->pixels.size() <= 1024) {
      const auto &image = *dialog.preview;
      result["operation_dialog"]["preview_image"] = {{"width",image.width},
          {"height",image.height},{"channels",image.channels},{"pixels",image.pixels}};
    }
    if (!owner.documents_.empty()) {
      result["selection"] = get(owner.document()->state().metadata, "selection");
      result["view"] = owner.document()->viewJson();
      result["ordinary_zoom"] = panes[owner.activeIndex()].zoom;
      const auto &pane = panes[owner.activeIndex()];
      result["ordinary_canvas"] = {
          {"canvas_id", pane.ordinaryCanvasId}, {"visible", pane.ordinaryCanvasVisible},
          {"focused", pane.ordinaryCanvasVisible && pane.ordinaryCanvasId != 0 &&
              ImGui::GetCurrentContext()->NavId == pane.ordinaryCanvasId},
          {"extent", {pane.extent.x, pane.extent.y}},
          {"canvas_point", {pane.ordinaryCanvasPoint.x, pane.ordinaryCanvasPoint.y}},
          {"origin", {pane.origin.x, pane.origin.y}},
          {"scale", {pane.zoom, pane.zoom * pane.aspectY}}};
      result["ordinary_canvas"]["loading"]=pane.viewportPending;
      result["ordinary_canvas"]["selection_handles"]=pane.selectionHandles;
      result["ordinary_canvas"]["selection_edit_error"]=pane.selectionEditError;
      result["ordinary_canvas"]["selection_edit_notice"]=pane.selectionEditNotice;
      result["ordinary_canvas"]["saved_roi_handle_error"]=pane.savedRoiHandleError;
      result["ordinary_canvas"]["saved_roi_handle_notice"]=pane.savedRoiHandleNotice;
      result["ordinary_canvas"]["selection_gesture"]=pane.selectionGesture?Json{
          {"handle",pane.selectionGesture->handle.index},{"kind",int(pane.selectionGesture->handle.kind)},{"invalid",pane.selectionGesture->invalid},
          {"preview",pane.selectionGesture->preview?selectionJson(*pane.selectionGesture->preview):Json()}}:Json();
      result["ordinary_canvas"]["style_layers"] = pane.styledLayers;
      result["ordinary_canvas"]["display_annotations"]=pane.displayLayers;
      result["ordinary_canvas"]["native_planar_annotations"]=pane.nativePlanar.diagnostics();
      result["ordinary_canvas"]["import_preview"]={{"active",pane.ordinaryCanvasVisible && pane.ordinaryImportActive},
          {"replace",pane.ordinaryImportReplace},{"layers",pane.ordinaryImportLayers},{"error",pane.ordinaryImportError}};
      result["ordinary_canvas"]["saved_roi_geometry_layers"] = pane.savedRoiLayers;
      result["ordinary_canvas"]["saved_roi_geometry_handles"] = pane.savedRoiHandles;
      result["ordinary_canvas"]["saved_roi_geometry_input"] = pane.savedRoiInput;
      result["ordinary_canvas"]["overlay_layers"] = pane.overlayLayers;
      result["ordinary_canvas"]["overlay_builds"] = pane.overlayBuilds;
      result["ordinary_canvas"].update(pane.ordinaryFrameSource);
      result["ordinary_pixels"] = ordinaryPixels(pane, captureGaussianReadback());
      result["orthogonal_planes"] =
          panes[owner.activeIndex()].orthogonal.diagnostics(captureGaussianReadback());
      result["orthogonal_navigation"] = navigation(pane);
      result["document_info"] = owner.documentInfo();
    }
    return result;
}

struct HeadlessInputSession::Impl {
  Window &window;
  std::unique_lock<std::mutex> lock;
  ImGuiContext *context = nullptr;
  Probe probe;
  const std::uint64_t id = ++headlessSessionSequence;
  bool frameFocusLost = false;
  int lastFrame = 0;
  double lastTime = 0;
  explicit Impl(Window &value, Probe callback)
      : window(value), lock(guiMutex, std::try_to_lock),
        probe(std::move(callback)) {
    if (!lock.owns_lock())
      throw std::runtime_error("Only one ImGui editor can run at a time");
    if (ImGui::GetCurrentContext())
      throw std::runtime_error("An ImGui context is already current");
  }
  void cleanup(bool releaseLock = true) noexcept {
    if (!context && !lock.owns_lock())
      return;
    // The stack-only session and every caller are creator-thread confined.
    if (std::this_thread::get_id() != window.ownerThread)
      std::terminate();
    window.impl->headlessSessionActive = false;
    if (context) {
      ImGui::SetCurrentContext(context);
      lastFrame = context->FrameCount;
      lastTime = context->Time;
      window.impl->resetRendererInputs();
      window.impl->clearGaussianReadbacks();
      window.impl->headlessSessionReadback = false;
      ImGui::DestroyContext(context);
      context = nullptr;
      window.impl->layout = false;
    }
    if (releaseLock && lock.owns_lock())
      lock.unlock();
  }
  ~Impl() { cleanup(); }
};
HeadlessInputSession::HeadlessInputSession(Window &window, Probe probe) {
  window.checkThread();
  impl_ = std::make_unique<Impl>(window, std::move(probe));
  try {
    IMGUI_CHECKVERSION();
    impl_->context = ImGui::CreateContext();
    if (impl_->probe)
      impl_->probe("after_context_creation");
    window.impl->resetInputs(true);
    window.impl->guiScale = 0;
    window.impl->appearance(1, false);
    window.impl->layout = false;
    window.impl->lastVisible = -1;
    window.impl->exit = false;
    auto &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.DisplaySize = {1250, 820};
    io.DeltaTime = 1.f / 60;
    io.KeyRepeatDelay = .275f;
    io.KeyRepeatRate = .05f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    unsigned char *pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(1);
    window.impl->initializeLayout();
    window.impl->headlessSessionActive = true;
  } catch (...) {
    impl_->cleanup();
    throw;
  }
}
HeadlessInputSession::~HeadlessInputSession() = default;
void HeadlessInputSession::close() {
  impl_->window.checkThread();
  impl_->cleanup();
}
Json HeadlessInputSession::lifecycle() const {
  impl_->window.checkThread();
  const auto identity = [](const ImGuiContext *value) {
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(value));
  };
  return {{"open", impl_->context != nullptr}, {"session_id", impl_->id},
          {"context_id", identity(impl_->context)},
          {"current_context_id", identity(ImGui::GetCurrentContext())},
          {"imgui_frame", impl_->context ? impl_->context->FrameCount : impl_->lastFrame},
          {"imgui_time", impl_->context ? impl_->context->Time : impl_->lastTime}};
}
void HeadlessInputSession::renderFrame(bool atomicKeys) {
  impl_->window.checkThread();
  if (!impl_->context || ImGui::GetCurrentContext() != impl_->context)
    throw std::logic_error("Headless session has no current owned context");
  auto &io = ImGui::GetIO();
  const bool trickle = io.ConfigInputTrickleEventQueue;
  try {
    impl_->window.impl->appearance(1, false);
    if (atomicKeys)
      io.ConfigInputTrickleEventQueue = false;
    ImGui::NewFrame();
    impl_->frameFocusLost=io.AppFocusLost;
    if (impl_->probe)
      impl_->probe("after_new_frame");
    impl_->window.impl->draw();
    ImGui::Render();
    impl_->lastFrame = impl_->context->FrameCount;
    impl_->lastTime = impl_->context->Time;
    io.ConfigInputTrickleEventQueue = trickle;
  } catch (...) {
    io.ConfigInputTrickleEventQueue = trickle;
    // Keep GUI exclusion until the enclosing driver/wrapper has unwound its
    // own worker cleanup. No other editor may borrow this global context yet.
    impl_->cleanup(false);
    throw;
  }
}
Json HeadlessInputSession::frame(const Json &events) {
  impl_->window.checkThread();
  if (!impl_->context || ImGui::GetCurrentContext() != impl_->context)
    throw std::logic_error("Headless session has no current owned context");
  if (!events.is_array() || events.size() > 120)
    throw std::invalid_argument("Session frame needs at most120 physical events");
  const auto &keys = headlessKeys();
  const auto &buttons = headlessButtons();
  std::size_t atoms = 0;
  for (const auto &event : events) {
    if (!event.is_object() || !get(event, "type").is_string())
      throw std::invalid_argument("Session physical input requires a type");
    const auto type = string(event["type"]);
    std::vector<std::string> fields{"type"};
    if (type == "position") {
      fields.insert(fields.end(), {"point", "space"});
      const auto &point = get(event, "point");
      const auto space = string(get(event, "space"), "screen");
      if (!point.is_array() || point.size() != 2 || !point[0].is_number() ||
          !point[1].is_number() || !std::isfinite(number(point[0])) ||
          !std::isfinite(number(point[1])) ||
          std::abs(number(point[0])) > std::numeric_limits<float>::max() / 256. ||
          std::abs(number(point[1])) > std::numeric_limits<float>::max() / 256. ||
          (!get(event, "space").is_null() && !get(event, "space").is_string()) ||
          (space != "screen" && space != "image"))
        throw std::invalid_argument("Session position needs finite X/Y and screen or image space");
      if (space == "image" && (impl_->window.documents_.empty() ||
          impl_->window.impl->settingsPanel.visible() || impl_->window.document()->view().orthogonal))
        throw std::invalid_argument("Image-space input needs the ordinary unobstructed XY canvas");
    } else if(type=="focus") {
      fields.push_back("focused");
      if(!get(event,"focused").is_boolean())throw std::invalid_argument("Session focus needs a boolean");
    } else if (type == "button" || type == "key") {
      fields.insert(fields.end(), {"name", "down"});
      const auto name = string(get(event, "name"));
      if (!get(event, "down").is_boolean() ||
          (type == "key" ? keys.count(name) == 0 : buttons.count(name) == 0))
        throw std::invalid_argument("Invalid session button/key");
    } else if (type == "keys") {
      fields.push_back("keys");
      const auto &batch = get(event, "keys");
      if (!batch.is_array() || batch.empty() || batch.size() > 16)
        throw std::invalid_argument("Session key batch needs1..16 entries");
      std::set<std::string> names;
      for (const auto &atom : batch) {
        if (!atom.is_object() || atom.size() != 2 || !get(atom, "name").is_string() ||
            !get(atom, "down").is_boolean() || keys.count(string(atom["name"])) == 0 ||
            !names.insert(string(atom["name"])).second)
          throw std::invalid_argument("Invalid or duplicate session key atom");
      }
      atoms += batch.size();
    } else if (type == "text") {
      fields.push_back("value");
      if (!get(event, "value").is_string() || string(event["value"]).size() > 65536 ||
          string(event["value"]).find('\0') != std::string::npos)
        throw std::invalid_argument("Session text needs at most64KiB UTF-8 without NUL");
    } else if (type == "wheel") {
      fields.insert(fields.end(), {"x", "y"});
      if (!get(event, "x").is_number() || !get(event, "y").is_number() ||
          !std::isfinite(number(event["x"])) || !std::isfinite(number(event["y"])) ||
          std::abs(number(event["x"])) > 120 || std::abs(number(event["y"])) > 120)
        throw std::invalid_argument("Session wheel needs finite deltas in[-120,120]");
    } else
      throw std::invalid_argument("Unsupported session physical input type");
    for (const auto &field : event.items())
      if (std::find(fields.begin(), fields.end(), field.key()) == fields.end())
        throw std::invalid_argument("Unknown session physical input field");
    if (type != "keys")
      ++atoms;
    if (atoms > 120)
      throw std::invalid_argument("Session frame exceeds120 physical atoms");
  }
  auto &window = impl_->window;
  auto &io = ImGui::GetIO();
  try {
    window.impl->headlessDiagnostics = true;
    window.impl->headlessSessionReadback = true;
    for (const auto &event : events) {
      const auto type = string(event["type"]);
      if (type == "position") {
        const auto &point = event["point"];
        const bool image = string(get(event, "space"), "screen") == "image";
        const auto *pane = image ? &window.impl->panes[window.activeIndex()] : nullptr;
        io.AddMousePosEvent(float(image ? pane->origin.x + number(point[0]) * pane->zoom : number(point[0])),
            float(image ? pane->origin.y + number(point[1]) * pane->zoom * pane->aspectY : number(point[1])));
      } else if(type=="focus")
        io.AddFocusEvent(boolean(event["focused"]));
      else if (type == "button")
        io.AddMouseButtonEvent(buttons.at(string(event["name"])), boolean(event["down"]));
      else if (type == "key")
        io.AddKeyEvent(keys.at(string(event["name"])), boolean(event["down"]));
      else if (type == "keys")
        for (const auto &atom : event["keys"])
          io.AddKeyEvent(keys.at(string(atom["name"])), boolean(atom["down"]));
      else if (type == "text")
        io.AddInputCharactersUTF8(string(event["value"]).c_str());
      else if (type == "wheel")
        io.AddMouseWheelEvent(float(number(event["x"])), float(number(event["y"])));
    }
    renderFrame(true);
    auto result = window.impl->headlessReport();
    result["session"] = lifecycle();
    const auto &context = *impl_->context;
    Json popups = Json::array(), down = Json::array();
    for (const auto &popup : context.OpenPopupStack)
      popups.push_back({{"id", popup.PopupId}, {"opened_frame", popup.OpenFrameCount},
          {"window", popup.Window ? popup.Window->Name : ""}});
    for (const auto &[name, key] : keys)
      if (ImGui::GetKeyData(key)->Down)
        down.push_back(name);
    result["input_frame"] = {{"raw_inputs", events}, {"keys_down", down},
        {"modifiers", {{"ctrl", io.KeyCtrl}, {"shift", io.KeyShift},
            {"alt", io.KeyAlt}, {"super", io.KeySuper}}},
        {"mouse_position", {io.MousePos.x, io.MousePos.y}},
        {"mouse_down", {io.MouseDown[0], io.MouseDown[1], io.MouseDown[2]}},
        {"app_focus_lost",impl_->frameFocusLost},
        {"popup_stack", popups},
        {"focus", {{"nav_id", context.NavId}, {"active_id", context.ActiveId},
            {"window", context.NavWindow ? context.NavWindow->Name : ""}}}};
    const auto &text = context.InputTextState;
    result["input_frame"]["input_text"] = {{"id", text.ID}, {"length", text.TextLen},
        {"cursor", text.Stb ? text.GetCursorPos() : 0},
        {"selection_start", text.Stb ? text.GetSelectionStart() : 0},
        {"selection_end", text.Stb ? text.GetSelectionEnd() : 0}};
    // Reuse the latest CPU value copies while this creator session is alive.
    // Cleanup releases them; camera-only frames need no new plane extraction.
    window.impl->headlessDiagnostics = false;
    window.impl->headlessSessionReadback = false;
    return result;
  } catch (...) {
    impl_->cleanup();
    throw;
  }
}

Json Window::renderHeadless(int frames, bool exercise, bool exerciseCamera,
                            const Json &inputEvents, bool exercisePreferences,
                            bool exerciseOrthogonalBusy) {
  checkThread();
  if (frames < 1 || frames > 120)
    throw std::invalid_argument("Frames must be 1..120");
  if (int(exercise) + int(exerciseCamera) + int(exercisePreferences) > 1)
    throw std::invalid_argument("Choose one headless interaction exercise");
  if (!inputEvents.is_array() || inputEvents.size() > 120)
    throw std::invalid_argument(
        "Headless input must be an array of at most 120 events");
  if (!inputEvents.empty() &&
      (exercise || exerciseCamera || exercisePreferences || documents_.empty() ||
       (impl->dialog.open && !impl->dialog.operation &&
        impl->dialog.id != "axis_plane_export" && impl->dialog.id != "axis_range_reslice" &&
        impl->dialog.id != "animation_options" && impl->dialog.id != "selection_enlargement") ||
       impl->browser.open))
    throw std::invalid_argument(
        "Headless input needs an image canvas, Preferences or operation dialog");
  const auto &keys = headlessKeys();
  const auto &buttons = headlessButtons();
  int inputFrames = 0;
  std::size_t inputAtoms = 0;
  bool possibleModeTransition = false;
  for (const auto &event : inputEvents) {
    if (!event.is_object() || !get(event, "type").is_string())
      throw std::invalid_argument("Each headless input event needs a type");
    const auto type = string(get(event, "type"));
    std::vector<std::string> fields{"type", "frames"};
    if (type == "position") {
      fields.insert(fields.end(), {"point", "space"});
      const auto &point = get(event, "point");
      const auto space = string(get(event, "space"), "image");
      if (!point.is_array() || point.size() != 2 || !point[0].is_number() ||
          !point[1].is_number() ||
          (!get(event, "space").is_null() &&
           !get(event, "space").is_string()) ||
          !std::isfinite(number(point[0])) ||
          !std::isfinite(number(point[1])) ||
          std::abs(number(point[0])) >
              std::numeric_limits<float>::max() / 256. ||
          std::abs(number(point[1])) >
              std::numeric_limits<float>::max() / 256. ||
          (space != "image" && space != "screen"))
        throw std::invalid_argument(
            "Headless position needs finite X/Y and image or screen space");
      if (space == "image" && impl->settingsPanel.visible())
        throw std::invalid_argument(
            "Preferences input requires screen-space coordinates");
      if (space == "image" && !possibleModeTransition && !documents_.empty() &&
          document()->view().orthogonal)
        throw std::invalid_argument("Image-space input needs the ordinary XY canvas");
    } else if (type == "button" || type == "key") {
      fields.insert(fields.end(), {"name", "down"});
      const auto name = string(get(event, "name"));
      if (!get(event, "down").is_boolean() ||
          (type == "key" ? keys.count(name) == 0 : buttons.count(name) == 0))
        throw std::invalid_argument("Invalid headless input button/key");
    } else if (type == "keys") {
      fields.push_back("keys");
      const auto &batch = get(event, "keys");
      if (!batch.is_array() || batch.empty() || batch.size() > 16)
        throw std::invalid_argument("Headless key batch needs 1..16 entries");
      std::set<std::string> distinct;
      for (const auto &atom : batch) {
        if (!atom.is_object() || atom.size() != 2 ||
            !get(atom, "name").is_string() || !get(atom, "down").is_boolean() ||
            keys.count(string(get(atom, "name"))) == 0 ||
            !distinct.insert(string(get(atom, "name"))).second)
          throw std::invalid_argument("Invalid or duplicate headless batch key");
      }
      inputAtoms += batch.size();
    } else if (type == "text") {
      fields.push_back("value");
      if (!get(event, "value").is_string() ||
          string(get(event, "value")).size() > 65536 ||
          string(get(event, "value")).find('\0') != std::string::npos)
        throw std::invalid_argument(
            "Headless text input needs at most 64 KiB UTF-8 text without NUL");
    } else if (type == "action") {
      fields.insert(fields.end(), {"name", "parameters"});
      if (!get(event, "name").is_string() ||
          (!get(event, "parameters").is_null() &&
           !get(event, "parameters").is_object()))
        throw std::invalid_argument(
            "Headless action needs a name and object parameters");
    } else if (type != "wait")
      throw std::invalid_argument("Unknown headless input event type");
    for (const auto &field : event.items())
      if (std::find(fields.begin(), fields.end(), field.key()) == fields.end())
        throw std::invalid_argument("Unknown headless input event field");
    const auto &delay = get(event, "frames");
    if (!delay.is_null() && (!delay.is_number_integer() || integer(delay) < 1 ||
                             integer(delay) > 120))
      throw std::invalid_argument("Headless input event frames must be 1..120");
    inputFrames += integer(delay, 1);
    if (inputFrames > 240)
      throw std::invalid_argument("Headless input exceeds 240 frames");
    if (type != "keys")
      ++inputAtoms;
    if (inputAtoms > 120)
      throw std::invalid_argument("Headless input exceeds 120 logical atoms");
    // Explicit actions or activation inputs can change the mode during the
    // sequence. Keep their dynamic coordinate check; reject an initially
    // incompatible static sequence before its first event changes the view.
    auto activation = [](const Json &atom) {
      const auto name = string(get(atom, "name"));
      return boolean(get(atom, "down")) &&
             (name == "Escape" || name == "Space" || name == "Enter");
    };
    possibleModeTransition |= type == "action" || type == "button" ||
                              (type == "key" && activation(event));
    if (type == "keys")
      for (const auto &atom : get(event, "keys"))
        possibleModeTransition |= activation(atom);
  }
  if (exerciseCamera && (documents_.empty() || impl->settingsPanel.visible() ||
                         impl->dialog.open || impl->browser.open ||
                         impl->gaussianPanel.visible() || impl->rankPanel.visible() || (impl->planarAnnotationRebind.visible() || (impl->roiImportPanel.visible() || impl->roiExportPanel.visible() || impl->displayAnnotationPanel.visible())) || (impl->channelDisplayPanel.inputActive() || impl->contrastPanel.inputActive())))
    throw std::invalid_argument(
        "Camera exercise needs an unobstructed image canvas");
  if (exercisePreferences &&
      (busy() || impl->settingsPanel.visible() || impl->dialog.open ||
       impl->browser.open || impl->selectionPanel.visible() ||
       impl->gaussianPanel.visible() || impl->rankPanel.visible() || (impl->planarAnnotationRebind.visible() || (impl->roiImportPanel.visible() || impl->roiExportPanel.visible() || impl->displayAnnotationPanel.visible())) || (impl->channelDisplayPanel.inputActive() || impl->contrastPanel.inputActive())))
    throw std::invalid_argument(
        "Preferences exercise needs closed dialogs and an idle editor");
  if (exerciseOrthogonalBusy &&
      (exercise || exerciseCamera || exercisePreferences || inputEvents.empty() ||
       documents_.empty() || busy() || impl->dialog.open || impl->browser.open ||
       impl->settingsPanel.visible() || impl->selectionPanel.visible() ||
       impl->gaussianPanel.visible() || impl->rankPanel.visible() || (impl->planarAnnotationRebind.visible() || (impl->roiImportPanel.visible() || impl->roiExportPanel.visible() || impl->displayAnnotationPanel.visible())) || (impl->channelDisplayPanel.inputActive() || impl->contrastPanel.inputActive())))
    throw std::invalid_argument("Orthogonal busy exercise needs input and an idle unobstructed image");
  HeadlessInputSession session(*this);
  auto &io = ImGui::GetIO();
  Json cacheTrace = Json::array();
  Json inputFramesTrace = Json::array();
  Json busyDiagnostic{{"started", false}};
  std::shared_ptr<InputJobBarrier> barrier;
  auto finishBusy = [&] {
    impl->diagnosticWorkerGate = {};
    if (!barrier)
      return;
    auto heldJob = impl->job;
    cancel();
    barrier->release();
    if (heldJob.valid() && !heldJob.wait_for(std::chrono::seconds(5)))
      throw std::runtime_error("Diagnostic job did not stop after barrier release");
    if (heldJob.valid())
      busyDiagnostic["final_job_state"] = jobStateName(heldJob.info().state);
    wait();
    busyDiagnostic["busy_after_cleanup"] = busy();
    barrier.reset();
  };
  std::size_t frameIndex = 0;
  auto frame = [&](int eventIndex = -1, const Json &raw = Json::array(),
                   const char *phase = "warmup", bool atomicKeys = false) {
    session.renderFrame(atomicKeys);
    Json sample{{"frame", ++frameIndex},
                {"mask_outline_builds", impl->maskOutlineBuilds}};
    sample["gaussian_panel"] = impl->gaussianPanel.diagnostics();
    sample["rank_panel"] = impl->rankPanel.diagnostics();
    sample["gaussian_jobs"] = metadataJson(impl->gaussianJobs.diagnostics());
    sample["workflow_steps"] = workflow.size();
    if (!documents_.empty()) {
      const auto &pane = impl->panes[activeIndex()];
      sample["data_revision"] = pane.doc->revision();
      sample["slice_index"] = pane.doc->slice();
      sample["ordinary_canvas"] = {
          {"extent", {pane.extent.x, pane.extent.y}},
          {"origin", {pane.origin.x, pane.origin.y}},
          {"scale", {pane.zoom, pane.zoom * pane.aspectY}},
          {"style_layers", pane.styledLayers},
          {"overlay_layers", pane.overlayLayers},
          {"overlay_builds", pane.overlayBuilds}};
      sample["ordinary_canvas"].update(pane.ordinaryFrameSource);
      sample["orthogonal_planes"] = pane.orthogonal.diagnostics();
      sample["ordinary_pixels"] = impl->ordinaryPixels(pane);
    }
    Json observation = sample;
    observation["event_index"] = eventIndex;
    observation["phase"] = phase;
    observation["delta_time"] = double(io.DeltaTime);
    observation["repeat_delay"] = double(io.KeyRepeatDelay);
    observation["repeat_rate"] = double(io.KeyRepeatRate);
    observation["raw_inputs"] = raw;
    observation["mouse_position"] = {io.MousePos.x, io.MousePos.y};
    std::vector<std::string> down;
    for (const auto &[name, key] : keys)
      if (ImGui::GetKeyData(key)->Down)
        down.push_back(name);
    std::sort(down.begin(), down.end());
    observation["keys_down"] = down;
    observation["modifiers"] = {{"shift", io.KeyShift}, {"ctrl", io.KeyCtrl},
                                {"alt", io.KeyAlt}, {"super", io.KeySuper}};
    const auto &context = *ImGui::GetCurrentContext();
    observation["focus"] = {{"nav_id", context.NavId}, {"active_id", context.ActiveId},
                            {"window", context.NavWindow ? context.NavWindow->Name : ""},
                            {"focused_plane", documents_.empty() ? -1 :
                              impl->panes[activeIndex()].orthogonal.focusedPlane()}};
    observation["busy"] = busy();
    const auto observedJob = jobInfo();
    observation["job_state"] = observedJob ? jobStateName(observedJob->state) : "none";
    if (!documents_.empty()) {
      const auto &pane = impl->panes[activeIndex()];
      const auto xyz = pane.doc->view().crosshair;
      observation["document_info"] = documentInfo();
      observation["crosshair_xyz"] = xyz;
      observation["world_xyz"] = pane.doc->spatialCalibration().voxel_to_world(
          {double(xyz[0]), double(xyz[1]), double(xyz[2])});
      observation["orthogonal_navigation"] = impl->navigation(pane);
    }
    inputFramesTrace.push_back(std::move(observation));
    cacheTrace.push_back(std::move(sample));
    // Each invocation owns a fresh ImGui context. Keep actual consecutive
    // frame evidence bounded instead of comparing counts across context setup.
    if (cacheTrace.size() > 8)
      cacheTrace.erase(cacheTrace.begin());
  };
  Json result;
  Json inputTrace = Json::array();
  Json preferencesInput = Json::object();
  impl->headlessDiagnostics = true;
  try {
    for (int i = 0; i < frames; ++i)
      frame();
    if (exerciseOrthogonalBusy) {
      barrier = std::make_shared<InputJobBarrier>();
      impl->diagnosticWorkerGate = [barrier](const JobContext &context) { barrier->hold(context); };
      applyOperation("add", {{"value", 0}}, false, true);
      std::unique_lock<std::mutex> lock(barrier->mutex);
      if (!barrier->changed.wait_for(lock, std::chrono::seconds(5), [&] { return barrier->entered; }))
        throw std::runtime_error("Diagnostic job did not enter its barrier");
      busyDiagnostic["started"] = true;
    }
    if (!inputEvents.empty()) {
      int eventIndex = 0;
      for (const auto &event : inputEvents) {
        const auto type = string(get(event, "type"));
        if (type == "position") {
          const auto &point = get(event, "point");
          const bool image = string(get(event, "space"), "image") == "image";
          if (image && documents_.empty())
            throw std::invalid_argument("Image-space input needs an open document");
          const auto *pane = documents_.empty() ? nullptr : &impl->panes[activeIndex()];
          if (image && impl->settingsPanel.visible())
            throw std::invalid_argument(
                "Preferences input requires screen-space coordinates");
          if (image && pane->doc->view().orthogonal)
            throw std::invalid_argument(
                "Image-space input needs the ordinary XY canvas");
          io.AddMousePosEvent(
              float(image ? pane->origin.x + number(point[0]) * pane->zoom
                          : number(point[0])),
              float(image ? pane->origin.y +
                                number(point[1]) * pane->zoom * pane->aspectY
                          : number(point[1])));
        } else if (type == "button")
          io.AddMouseButtonEvent(buttons.at(string(get(event, "name"))),
                                 boolean(get(event, "down")));
        else if (type == "key")
          io.AddKeyEvent(keys.at(string(get(event, "name"))),
                         boolean(get(event, "down")));
        else if (type == "keys")
          for (const auto &atom : get(event, "keys"))
            io.AddKeyEvent(keys.at(string(get(atom, "name"))), boolean(get(atom, "down")));
        else if (type == "text")
          io.AddInputCharactersUTF8(string(get(event, "value")).c_str());
        else if (type == "action")
          action(string(get(event, "name")), get(event, "parameters").is_null()
                                                 ? Json::object()
                                                 : get(event, "parameters"));
        for (int i = 0; i < integer(get(event, "frames"), 1); ++i)
          frame(eventIndex, i == 0 ? Json::array({event}) : Json::array(), "input", type == "keys" && i == 0);
        if (documents_.empty()) {
          inputTrace.push_back({{"type", type}, {"document_closed", true},
                                {"gaussian_panel", impl->gaussianPanel.diagnostics()},
                                {"gaussian_jobs", metadataJson(impl->gaussianJobs.diagnostics())}});
          ++eventIndex;
          continue;
        }
        const auto &pane = impl->panes[activeIndex()];
        const auto snapshot = nativeSnapshot();
        inputTrace.push_back(
            {{"type", type},
             {"mouse_screen", {io.MousePos.x, io.MousePos.y}},
             {"canvas_origin", {pane.origin.x, pane.origin.y}},
             {"canvas_scale", {pane.zoom, pane.zoom * pane.aspectY}},
             {"selection", snapshot.data().selection
                               ? selectionJson(*snapshot.data().selection)
                               : Json::object()},
             {"preview", pane.selectionGesture && pane.selectionGesture->preview
                             ? selectionJson(*pane.selectionGesture->preview)
                             : Json::object()},
             {"gesture_active", pane.selectionGesture.has_value()},
             {"selected_vertex", pane.selectionVertex
                                     ? Json(*pane.selectionVertex)
                                     : Json(nullptr)},
             {"selection_revision", snapshot.revisions().selection},
             {"pixel_revision", snapshot.revisions().pixels},
             {"dirty", snapshot.dirty()},
             {"undo_steps", nativeDocument()->undo_steps()}});
        ++eventIndex;
      }
    }
    if (exercisePreferences) {
      Json events = Json::array();
      auto observe = [&] {
        const auto panel = impl->settingsPanel.diagnostics();
        return Json{{"visible", panel["open"]},
                    {"toggle_name", panel["toggle_name"]},
                    {"toggle_value", panel["toggle_value"]},
                    {"controls", panel["controls"]},
                    {"search", panel["search"]},
                    {"error", panel["error"]},
                    {"settings_auto_dpi", settings().boolean("auto_dpi")}};
      };
      auto inject = [&](Json event, const char *target) {
        const auto type = string(get(event, "type"));
        if (type == "position") {
          const auto &point = event["point"];
          io.AddMousePosEvent(float(number(point[0])), float(number(point[1])));
        } else if (type == "button")
          io.AddMouseButtonEvent(0, boolean(event["down"]));
        else if (type == "key")
          io.AddKeyEvent(keys.at(string(event["name"])), boolean(event["down"]));
        else if (type == "text")
          io.AddInputCharactersUTF8(string(event["value"]).c_str());
        frame();
        if (type != "wait") {
          event["target"] = target;
          event["frame"] = frameIndex;
          events.push_back(std::move(event));
        }
      };
      auto click = [&](ImVec2 point, const char *target) {
        if (point.x < 0 || point.y < 0)
          throw std::runtime_error("Preferences exercise has no visible input control");
        inject({{"type", "position"}, {"space", "screen"},
                {"point", {point.x, point.y}}}, target);
        inject({{"type", "button"}, {"name", "left"}, {"down", true}}, target);
        inject({{"type", "button"}, {"name", "left"}, {"down", false}}, target);
        inject({{"type", "wait"}, {"frames", 1}}, target);
      };
      click(impl->editMenuPoint, "Edit");
      click(impl->preferencesMenuPoint, "Preferences");
      auto panel = impl->settingsPanel.diagnostics();
      if (!impl->settingsPanel.visible())
        throw std::runtime_error("Actual Edit menu input did not open Preferences");
      const auto &searchPoint = panel["search_point"];
      click({float(number(searchPoint[0])), float(number(searchPoint[1]))}, "Search");
      inject({{"type", "key"}, {"name", "Ctrl"}, {"down", true}}, "Search");
      inject({{"type", "key"}, {"name", "A"}, {"down", true}}, "Search");
      inject({{"type", "key"}, {"name", "A"}, {"down", false}}, "Search");
      inject({{"type", "key"}, {"name", "Ctrl"}, {"down", false}}, "Search");
      inject({{"type", "text"}, {"value", "dpi"}}, "Search");
      inject({{"type", "wait"}, {"frames", 1}}, "Search");
      panel = impl->settingsPanel.diagnostics();
      if (panel["search"] != "dpi" || panel["toggle_name"] != "auto_dpi" ||
          panel["controls"] != Json::array({"auto_dpi", "print_dpi"}))
        throw std::runtime_error("Actual Preferences Search keyboard input failed");
      const auto before = observe();
      click(impl->settingsPanel.togglePoint(), "auto_dpi");
      const auto draft = observe();
      click(impl->settingsPanel.applyPoint(), "Apply");
      preferencesInput = {{"before", before},
                          {"changed_draft", draft},
                          {"applied", observe()},
                          {"input_events", std::move(events)}};
    } else if (exerciseCamera) {
      const auto &pane = impl->panes[activeIndex()];
      auto point =
          pane.doc->view().orthogonal
              ? pane.orthogonal.screenPoint(0, pane.doc->shape().x / 2,
                                            pane.doc->shape().y / 2)
              : ImVec2(pane.origin.x + pane.doc->shape().x * pane.zoom / 2,
                       pane.origin.y + pane.doc->shape().y * pane.zoom *
                                           pane.aspectY / 2);
      io.AddMousePosEvent(point.x, point.y);
      frame();
      io.AddKeyEvent(ImGuiMod_Ctrl, true);
      io.AddMouseWheelEvent(0, 1);
      frame();
      frame();
      io.AddKeyEvent(ImGuiMod_Ctrl, false);
      io.AddMouseButtonEvent(ImGuiMouseButton_Middle, true);
      frame();
      io.AddMousePosEvent(point.x + 17, point.y - 11);
      frame();
      io.AddMouseButtonEvent(ImGuiMouseButton_Middle, false);
      frame();
      frame();
    } else if (exercise && impl->dialog.open && impl->dialog.operation) {
      auto click = [&](ImVec2 point) {
        if (point.x < 0)
          throw std::invalid_argument(
              "No operation control available for interaction");
        io.AddMousePosEvent(point.x, point.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        frame();
      };
      if (impl->dialog.preview)
        click(impl->dialog.applyPoint);
      else {
        if (impl->dialog.choicePoint.x >= 0) {
          click(impl->dialog.choicePoint);
          if (impl->dialog.alternateChoicePoint.x >= 0)
            click(impl->dialog.alternateChoicePoint);
        }
        click(impl->dialog.previewPoint);
      }
      wait();
      frame();
    } else if (exercise && impl->settingsPanel.visible()) {
      auto click = [&](ImVec2 point) {
        if (point.x < 0)
          throw std::invalid_argument(
              "No preferences control available for interaction");
        io.AddMousePosEvent(point.x, point.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        frame();
      };
      click(impl->settingsPanel.togglePoint());
      click(impl->settingsPanel.applyPoint());
    } else if (exercise && impl->jobsPanel.visible() &&
               impl->jobsPanel.cancelPoint().x >= 0) {
      const auto point = impl->jobsPanel.cancelPoint();
      io.AddMousePosEvent(point.x, point.y);
      frame();
      io.AddMouseButtonEvent(0, true);
      frame();
      io.AddMouseButtonEvent(0, false);
      frame();
      wait();
      frame();
    } else if (exercise && !documents_.empty() &&
               document()->view().orthogonal) {
      auto &pane = impl->panes[activeIndex()];
      auto d = document();
      auto point =
          pane.orthogonal.screenPoint(0, std::min(1, d->frame().width - 1),
                                      std::min(1, d->frame().height - 1));
      io.AddMousePosEvent(point.x, point.y);
      frame();
      io.AddMouseButtonEvent(0, true);
      frame();
      io.AddMouseButtonEvent(0, false);
      frame();
      point = pane.orthogonal.screenPoint(1, 0, d->slice());
      io.AddMousePosEvent(point.x, point.y);
      frame();
      io.AddMouseWheelEvent(0, 1);
      frame();
      frame();
    } else if (exercise && !documents_.empty()) {
      impl->tool = "rectangle";
      auto &pane = impl->panes[activeIndex()];
      auto origin = pane.origin;
      float zoom = pane.zoom;
      io.AddMousePosEvent(origin.x + 3 * zoom,
                          origin.y + 4 * zoom * pane.aspectY);
      frame();
      io.AddMouseButtonEvent(0, true);
      frame();
      io.AddMousePosEvent(origin.x + 13 * zoom,
                          origin.y + 14 * zoom * pane.aspectY);
      frame();
      io.AddMouseButtonEvent(0, false);
      frame();
      frame();
    }
    result = impl->headlessReport();
    result["input_trace"] = std::move(inputTrace);
    result["input_frames"] = std::move(inputFramesTrace);
    result["cache_trace"] = std::move(cacheTrace);
    result["preferences_interaction"] = std::move(preferencesInput);
    finishBusy();
    result["busy_diagnostic"] = std::move(busyDiagnostic);
    session.close();
    return result;
  } catch (...) {
    const auto failure = std::current_exception();
    try { finishBusy(); } catch (...) { /* Preserve the triggering failure; worker has been released. */ }
    session.close();
    std::rethrow_exception(failure);
  }
}
int Window::run(const RunOptions &options) {
  checkThread();
  if (options.frames < 0)
    throw std::invalid_argument("Frame count must be nonnegative");
  if (options.rank_probe && options.appearance_probe)
    throw std::invalid_argument("Only one fixed OpenGL observation probe may run at a time");
  if (options.hidden && options.frames == 0)
    throw std::invalid_argument(
        "Hidden rendering requires a positive frame count");
  std::unique_lock<std::mutex> lock(guiMutex, std::try_to_lock);
  if (!lock.owns_lock())
    throw std::runtime_error("Only one ImGui editor can run at a time");
  if (!glfwInit())
    throw std::runtime_error(
        "GLFW initialization failed: a desktop display is required");
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  glfwWindowHint(GLFW_VISIBLE, options.hidden ? GLFW_FALSE : GLFW_TRUE);
  auto window = glfwCreateWindow(options.appearance_probe ? 2560 : 1250,
                                 options.appearance_probe ? 1600 : 820,
                                 "imagejxx | Dear ImGui", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    throw std::runtime_error("Could not create an OpenGL desktop window");
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  impl->resetInputs(true);
  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  impl->guiScale = 0;
  float dpiX = 1, dpiY = 1;
  glfwGetWindowContentScale(window, &dpiX, &dpiY);
  impl->appearance(options.appearance_probe ? options.appearance_probe->suppliedDpi()
                                           : std::max(dpiX, dpiY), true);
  impl->initializeLayout();
  impl->lastVisible = -1;
  impl->exit = false;
  impl->closingApp = false;
  bool platform = ImGui_ImplGlfw_InitForOpenGL(window, true),
       renderer = platform && ImGui_ImplOpenGL3_Init("#version 130");
  const bool previousDiagnostics = impl->headlessDiagnostics;
  auto cleanup = [&] {
    impl->appearanceProbe = nullptr;
    impl->appearanceFrame = 0;
    impl->appearanceFramebuffer = {};
    impl->appearanceWindow = {};
    impl->headlessDiagnostics = previousDiagnostics;
    if(impl->viewportJobs)impl->viewportJobs->cancelAll();
    for(auto &pane:impl->panes){pane.sampledSurface.reset();pane.sampleMemory.reset();pane.viewportPending=false;}
    impl->rankProbe = nullptr;
    impl->rankConsumed = {{"valid", false}};
    impl->rankSelectionHashes.clear();
    if (renderer) {
      for (auto &pane : impl->panes) {
        if (pane.texture)
          glDeleteTextures(1, &pane.texture);
        pane.texture = 0;pane.textureMemory.reset();
        pane.revision = ~uint64_t(0);
        pane.rankTexture = {{"valid", false}};
        std::vector<unsigned char>().swap(pane.rankScalarBytes);std::vector<unsigned char>().swap(pane.rankRgbBytes);pane.rankMemory.reset();
        pane.orthogonal.releaseTextures();
        pane.displayRenderer.release_textures();
        pane.importDisplayRenderer.release_textures();
      }
      if (impl->dialog.texture)
        glDeleteTextures(1, &impl->dialog.texture);
      impl->dialog.texture = 0;
      impl->nativeWorkflowPanel.release_textures();
      ImGui_ImplOpenGL3_Shutdown();
    }
    if (platform)
      ImGui_ImplGlfw_Shutdown();
    impl->gl = false;
    for (auto &pane : impl->panes)
      pane.selectionGesture.reset();
    impl->resetInputs();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
  };
  if (!renderer) {
    cleanup();
    throw std::runtime_error("ImGui OpenGL renderer initialization failed");
  }
  impl->gl = true;
  impl->rankProbe = options.rank_probe;
  impl->appearanceProbe = options.appearance_probe;
  if (options.appearance_probe) impl->headlessDiagnostics = true;
  impl->rankFrame = impl->rankUploadSerial = 0;
  glfwSetWindowUserPointer(window, impl.get());
  glfwSetDropCallback(
      window, [](GLFWwindow *window, int count, const char **paths) {
        auto state = static_cast<Impl *>(glfwGetWindowUserPointer(window));
        std::lock_guard<std::mutex> guard(state->dropMutex);
        for (int i = 0; i < count; ++i)
          state->dropped.push_back(paths[i]);
      });
  try {
    int frames = 0;
    const auto probeDeadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(options.appearance_probe ? 60 : 30);
    while (!impl->exit) {
      // A minimized/zero framebuffer still consumes the private cell deadline.
      if ((options.rank_probe || options.appearance_probe) &&
          std::chrono::steady_clock::now() >= probeDeadline)
        break;
      glfwPollEvents();
      if (glfwWindowShouldClose(window)) {
        glfwSetWindowShouldClose(window, GLFW_FALSE);
        impl->closingApp = true;
      }
      {
        std::vector<std::string> paths;
        {
          std::lock_guard<std::mutex> guard(impl->dropMutex);
          paths.swap(impl->dropped);
        }
        for (const auto &path : paths)
          impl->guard([&] { openFile(path); });
      }
      int width, height;
      glfwGetFramebufferSize(window, &width, &height);
      if (width == 0 || height == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        continue;
      }
      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      glfwGetWindowContentScale(window, &dpiX, &dpiY);
      const auto actualDpi = std::max(dpiX, dpiY);
      const auto suppliedDpi = options.appearance_probe
          ? options.appearance_probe->suppliedDpi() : actualDpi;
      impl->appearance(suppliedDpi, true);
      if (options.appearance_probe) {
        impl->appearanceFrame = static_cast<std::uint64_t>(frames) + 1;
        impl->appearanceActualDpi = actualDpi;
        impl->appearanceSuppliedDpi = suppliedDpi;
        impl->appearanceFramebuffer = {width, height};
        glfwGetWindowSize(window, &impl->appearanceWindow[0], &impl->appearanceWindow[1]);
        options.appearance_probe->beforeFrame(impl->appearanceFrame);
      }
      if (options.rank_probe) {
        impl->rankFrame = static_cast<std::uint64_t>(frames) + 1;
        options.rank_probe->beforeFrame(impl->rankFrame);
        impl->rankConsumed = {{"valid", false}, {"frame", impl->rankFrame}};
      }
      ImGui::NewFrame();
      impl->draw();
      ImGui::Render();
      glViewport(0, 0, width, height);
      auto background =
          color(impl->preferenceState.settings.text("canvas_background"));
      glClearColor(background[0] / 255.f, background[1] / 255.f,
                   background[2] / 255.f, background[3] / 255.f);
      glClear(GL_COLOR_BUFFER_BIT);
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
      if (options.rank_probe) {
        const auto renderSubmit = rankRuntimeClockStamp();
        options.rank_probe->afterRender(impl->rankFrame, renderSubmit);
        if (options.rank_probe->finished()) break;
      }
      if (options.appearance_probe) {
        options.appearance_probe->afterRender(impl->appearanceFrame, actualDpi);
        if (options.appearance_probe->finished()) break;
      }
      ++frames;
      if (options.appearance_probe && frames >= 750) break;
      if (options.frames > 0 && frames >= options.frames) {
        if (!options.screenshot.empty()) {
          std::vector<unsigned char> pixels(size_t(width) * height * 3);
          glPixelStorei(GL_PACK_ALIGNMENT, 1);
          glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE,
                       pixels.data());
          Image image(width, height, 3, PixelType::UInt8);
          for (int y = 0; y < height; ++y)
            for (int x = 0; x < width * 3; ++x)
              image.pixels[size_t(y) * width * 3 + x] =
                  pixels[size_t(height - 1 - y) * width * 3 + x];
          writeImages(options.screenshot, {image});
        }
        break;
      }
      glfwSwapBuffers(window);
    }
    if (!options.hidden && !options.rank_probe && !options.appearance_probe) {
      impl->preferences["layout"] = ImGui::SaveIniSettingsToMemory();
      impl->savePreferences();
    }
    cleanup();
    return 0;
  } catch (...) {
    cleanup();
    throw;
  }
}
} // namespace imagejxx::desktop
