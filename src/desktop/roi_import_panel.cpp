#include "roi_import_panel.hpp"
#include <imagejxx/imagej_roi_projection.hpp>
#include <imagejxx/native_workflow.hpp>
#include "../native/native_workflow_internal.hpp"
#include "roi_import_limits.hpp"
#include "../native/imagej_roi_internal.hpp"
#include "../native/imagej_annotation_import_internal.hpp"
#include "../native/kernel_control.hpp"
#include "display_annotation_renderer.hpp"
#include "planar_annotation_visual.hpp"
#include "roi_display_replacement_editor.hpp"
#include "roi_import_preview.hpp"
#include "roi_import_replacement_editor.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <fstream>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace imagejxx::desktop {
namespace {
struct Entry {
  ImageJRoiImportItem item;
  std::shared_ptr<const std::vector<unsigned char>> data;
  bool display = false, is_text = false, needs_default = false,
       default_confirmed = false;
  std::string default_color = "ff000000";
  std::optional<ImageJDisplayRoiNativeReplacement> display_original_replacement,
      display_replacement;
  std::optional<ImageJRoiNativeReplacement> replacement;
  std::optional<ImageJRoiPlaneMapping> edited_mapping;
  std::optional<std::uint32_t> edited_default_color;
  bool has_edit()const{return bool(item.native_replacement)||bool(replacement)||bool(display_original_replacement)||bool(display_replacement);}
  bool envelope = false, explicit_slice = false;
  int plane = -1, slice = 1, z_policy = 0, legacy_policy = 0;
};
struct Result {
  std::optional<ImageJRoiPlaneMapping> editor_mapping;
  std::optional<std::uint32_t> editor_default_color;
  std::optional<RoiDisplayReplacementEditor> display_editor;
  std::vector<PlanarDisplayAnnotation> display_editor_rows;
  std::optional<RoiImportReplacementEditor> editor;
  std::vector<PlanarAnnotation> editor_rows;
  std::vector<Entry> entries;
  std::shared_ptr<const ResolvedImageJAnnotationImport> mixed_prepared;
  std::shared_ptr<const ResolvedImageJRoiImport> prepared;
  std::shared_ptr<const RoiImportPreview> preview;
  std::optional<ImageJRoiImportRequest> request;
  std::optional<ImageJAnnotationImportRequest> mixed_request;
};
bool display_wire(const std::vector<unsigned char> &bytes) {
  if (bytes.size() < 64 || bytes[6] != 1)
    return false;
  const auto subtype = unsigned(bytes[48]) * 256 + bytes[49];
  return subtype == 1 || subtype == 4;
}
Entry captured_entry(ImageJAnnotationImportItem value,
                     const ImageJRoiLimits &limits) {
  Entry e;
  std::optional<ImageJRoiPlaneMapping> plane;
  if (auto geometric = std::get_if<ImageJRoiImportItem>(&value)) {
    e.item = std::move(*geometric);
    plane = e.item.mapping;
  } else {
    auto d = std::move(std::get<ImageJDisplayRoiImportItem>(value));
    e.display = true;
    e.is_text = d.data[49] == 1;
    if (d.mapping) {
      plane = d.mapping->plane;
      e.edited_default_color=d.mapping->text_default_color;
      if (d.mapping->text_default_color) {
        std::ostringstream text;
        text << std::hex << std::setw(8) << std::setfill('0')
             << *d.mapping->text_default_color;
        e.default_color = text.str();
        e.default_confirmed = true;
      }
    }
    e.display_original_replacement = std::move(d.native_replacement);
    e.item = {std::move(d.name), std::move(d.data), plane};
    if (plane && e.is_text)
      e.needs_default =
          !decode_imagej_text_roi(e.item.data, limits).stroke_color;
  }
  e.data=std::make_shared<const std::vector<unsigned char>>(std::move(e.item.data));
  e.envelope = !plane;
  if(plane&&e.has_edit())e.edited_mapping=plane;
  if (plane) {
    e.plane = int(plane->plane);
    e.explicit_slice = bool(plane->coordinate);
    e.slice = plane->coordinate.value_or(0) + 1;
    e.z_policy = int(plane->z_position);
    e.legacy_policy = int(plane->legacy_position);
  }
  return e;
}
Result captured_inputs(const MetadataObject &record) {
  Result r;
  const auto schema = std::get_if<std::string>(&record.at("schema"));
  if (schema && *schema == "imagejxx.imagej-annotation-import-command") {
    auto request = decode_imagej_annotation_import_request(record);
    r.entries.reserve(request.items.size());
    for (auto &item : request.items)
      r.entries.push_back(
          captured_entry(std::move(item), request.limits.batch.roi));
    request.items.clear();
    r.mixed_request = std::move(request);
  } else {
    auto request = decode_imagej_roi_import_request(record);
    r.entries.reserve(request.items.size());
    for (auto &item : request.items)
      r.entries.push_back(captured_entry(std::move(item), request.limits.roi));
    request.items.clear();
    r.request = std::move(request);
  }
  return r;
}

Result typed_inputs(const NativeWorkflowImport &intent, DatasetShape shape) {
  detail::validate_imagej_annotation_import_limits(intent.limits);
  if (intent.original) detail::validate_native_workflow_import_original(*intent.original);
  if (intent.items.empty() || intent.items.size() > intent.limits.batch.max_entries)
    throw std::invalid_argument("Workflow import member limit");
  for (const auto &item : intent.items) (void)detail::admit_imagej_annotation_import_item(item,intent.limits);
  Result result;
  for (const auto &item : intent.items) result.entries.push_back(captured_entry(item,intent.limits.batch.roi));
  result.mixed_request=ImageJAnnotationImportRequest{{},intent.mode,intent.limits,shape};
  return result;
}
bool envelope(const std::vector<unsigned char> &bytes,
              const ImageJRoiLimits &limits) {
  if (bytes.size() < 64)
    throw std::invalid_argument("Truncated ROI file");
  std::uint32_t h2 = 0;
  for (int i = 60; i < 64; ++i)
    h2 = (h2 << 8) | bytes[i];
  if (!h2)
    return false;
  const auto text = detail::preview_imagej_roi_properties(bytes, limits);
  for (std::size_t start = 0; start < text.size();) {
    auto end = text.find('\n', start);
    if (end == std::string::npos)
      end = text.size();
    auto key = text.find_first_not_of(" \t\r", start);
    if (key < end &&
        (text.compare(key, sizeof("imagejxx.roi.") - 1, "imagejxx.roi.") == 0 ||
         text.compare(key, sizeof("imagejxx.display-roi.") - 1,
                      "imagejxx.display-roi.") == 0))
      return true;
    start = end < text.size() ? end + 1 : end;
  }
  return false;
}
std::size_t load_workspace(const std::string &path,const ImageJRoiImportLimits &limits,const ImageJRoiZipLimits &transport) {
  auto extension=std::filesystem::u8path(path).extension().u8string();
  std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
  return std::max<std::size_t>(64*1048576,std::max(limits.max_decoded_bytes,extension==".zip"?transport.max_working_bytes:0));
}
std::vector<Entry> load(const std::string &path,const ImageJRoiImportLimits &limits,ImageJRoiZipLimits transport) {
  namespace fs = std::filesystem;
  const auto file = fs::u8path(path);
  if (!fs::is_regular_file(file))
    throw std::invalid_argument("Choose a regular .roi or .zip file");
  auto extension = file.extension().u8string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  const bool zip = extension == ".zip";
  if (!zip && extension != ".roi")
    throw std::invalid_argument("Choose a .roi or .zip file");
  const auto size = fs::file_size(file),
             cap = zip ? std::uintmax_t(transport.max_archive_bytes)
                       : std::uintmax_t(std::min(limits.max_total_bytes,limits.roi.max_bytes));
  if (size > cap)
    throw std::invalid_argument("ROI import file exceeds the input byte limit");
  std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
  std::ifstream input(file, std::ios::binary);
  if (!input)
    throw std::invalid_argument("Cannot open ROI import file");
  execution_stage_progress("Read ROI file", 0, bytes.size());
  for (std::size_t offset = 0; offset < bytes.size();) {
    kernel_checkpoint();
    const auto count = std::min<std::size_t>(65536, bytes.size() - offset);
    if (!input.read(reinterpret_cast<char *>(bytes.data() + offset),
                    std::streamsize(count)))
      throw std::invalid_argument(
          "ROI import file changed or could not be read");
    offset += count;
    execution_stage_progress("Read ROI file", offset, bytes.size());
  }
  kernel_checkpoint();
  if (input.peek() != std::char_traits<char>::eof() || input.bad())
    throw std::invalid_argument(
        "ROI import file changed or failed while loading");
  std::vector<ImageJRoiZipEntry> members;
  if (zip) {
    auto z=transport;
    z.max_entries = std::min(z.max_entries,limits.max_entries);
    z.max_entry_bytes = std::min(z.max_entry_bytes,limits.roi.max_bytes);
    z.max_total_bytes = std::min(z.max_total_bytes,limits.max_total_bytes);
    members = decode_imagej_roi_zip(bytes, z);
  } else
    members.push_back({file.filename().u8string(), std::move(bytes)});
  if (members.empty())
    throw std::invalid_argument("ROI import contains no members");
  std::vector<Entry> result;
  result.reserve(members.size());
  for (auto &member : members) {
    kernel_checkpoint();
    Entry e;
    e.envelope = envelope(member.data, limits.roi);
    e.display = display_wire(member.data);
    e.is_text = e.display && member.data[49] == 1;
    if (e.display && e.is_text && !e.envelope)
      e.needs_default =
          !decode_imagej_text_roi(member.data, limits.roi).stroke_color;
    e.item = {std::move(member.name), std::move(member.data), {}};
    e.data=std::make_shared<const std::vector<unsigned char>>(std::move(e.item.data));
    result.push_back(std::move(e));
  }
  return result;
}
} // namespace
struct RoiImportPanel::State {
  explicit State(std::shared_ptr<JobScheduler> value)
      : scheduler(std::move(value)) {
    if (!scheduler)
      throw std::invalid_argument("ROI import requires a job scheduler");
  }
  struct Cell {
    std::mutex mutex;
    std::optional<Result> result;
  };
  std::shared_ptr<JobScheduler> scheduler;
  Job job;
  std::shared_ptr<Cell> cell;
  bool harvested = true;
  std::optional<DocumentSnapshot> source;
  std::vector<Entry> entries;
  std::optional<ImageJRoiPlaneMapping> editor_mapping;
  std::optional<std::uint32_t> editor_default_color;
  std::size_t editor_cap=0;
  std::optional<RoiDisplayReplacementEditor> display_editor;
  std::vector<PlanarDisplayAnnotation> display_editor_rows;
  std::optional<RoiImportReplacementEditor> editor;
  std::vector<PlanarAnnotation> editor_rows;
  std::shared_ptr<const ResolvedImageJAnnotationImport> mixed_prepared;
  std::shared_ptr<const ResolvedImageJRoiImport> prepared;
  std::shared_ptr<const RoiImportPreview> preview;
  std::string path, error, reason;
  bool open = false, focus = false;
  bool image_review = false, preview_enabled = true, draft = false;
  MetadataObject original;
  std::shared_ptr<const NativeWorkflowImport> typed;
  bool reviewing() const { return typed || !original.empty(); }
  std::optional<DatasetShape> recorded_extent;
  ImageJRoiImportLimits limits;
  std::size_t display_retained = 128 * 1048576;
  ImageJRoiZipLimits transport;
  std::optional<RoiImportLimitEditor> limit_editor;
  bool original_mixed = false;
  bool destination_confirmed = false;
  int selected = 0, mode = 0, phase = 0;
  std::uint64_t generation = 0, worker_generation = 0;
  Json controls = Json::object();
  Json bounds = Json::array();
  ~State() {
    if (job.valid()) {
      job.cancel();
      job.wait();
    }
  }
  bool working() const { return job.valid() && !harvested; }
  void observe(const std::string &key, bool enabled = true, ImGuiID id = 0) {
    const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    controls[key] = {
        {"id", id ? id : ImGui::GetItemID()},
        {"enabled",
         enabled && !(ImGui::GetCurrentContext()->LastItemData.ItemFlags &
                      ImGuiItemFlags_Disabled)},
        {"rect", {a.x, a.y, b.x, b.y}},
        {"point", {(a.x + b.x) / 2, (a.y + b.y) / 2}}};
    if (key == "slice" || key == "default_color")
      controls[key]["point"][0] = a.x + 50;
  }
  bool combo(const char *label, int &value, const char *const *names, int count,
             const char *key, bool enabled) {
    bool changed = false;
    if (ImGui::BeginCombo(label, names[value])) {
      for (int i = 0; i < count; ++i) {
        ImGui::PushID(i);
        if (ImGui::Selectable(names[i], value == i)) {
          value = i;
          changed = true;
        }
        observe(std::string(key) + "." + std::to_string(i), enabled);
        ImGui::PopID();
      }
      ImGui::EndCombo();
    }
    observe(key, enabled);
    return changed;
  }
  void invalidate() {
    if (job.valid())
      job.cancel();
    prepared.reset();
    mixed_prepared.reset();
    preview.reset();
    image_review = false;
    error.clear();
    ++generation;
  }
  void poll() {
    if (!job.valid() || !job.ready())
      return;
    if (harvested) {
      if ((prepared || mixed_prepared) &&
          job.info().state == JobState::Canceled) {
        invalidate();
        reason = "job_canceled";
        error = "ROI import preparation was canceled";
      }
      return;
    }
    harvested = true;
    const auto completed_phase = phase;
    phase = 0;
    try {
      job.rethrow();
      if (!open || worker_generation != generation) {
        cell.reset();
        return;
      }
      Result result;
      {
        std::lock_guard<std::mutex> lock(cell->mutex);
        result = std::move(cell->result.value());
      }
      cell.reset();
      if (completed_phase == 1 || completed_phase == 3) {
        job.complete([&] {
          entries = std::move(result.entries);
          selected = 0;
          if (result.request) {
            mode = result.request->mode == ImageJRoiImportMode::Append ? 0 : 1;
            limits = result.request->limits;
            recorded_extent = result.request->source_extent;
          } else if (result.mixed_request) {
            mode = result.mixed_request->mode == ImageJRoiImportMode::Append
                       ? 0
                       : 1;
            limits = result.mixed_request->limits.batch;
            display_retained =
                result.mixed_request->limits.max_display_retained_bytes;
            recorded_extent = result.mixed_request->source_extent;
            original_mixed = original.empty() || detail::native_workflow_original_schema(original)=="imagejxx.imagej-annotation-import-command";
          }
        });
      } else if (completed_phase == 4) {
        job.complete([&] {
          editor_mapping=std::move(result.editor_mapping);editor_default_color=result.editor_default_color;
          display_editor = std::move(result.display_editor);
          display_editor_rows = std::move(result.display_editor_rows);
          editor = std::move(result.editor);
          editor_rows = std::move(result.editor_rows);
        });
      } else {
        prepared = std::move(result.prepared);
        mixed_prepared = std::move(result.mixed_prepared);
        preview = std::move(result.preview);
      }
    } catch (const OperationCancelled &) {
      if (open && worker_generation == generation) {
        invalidate();
        reason = "job_canceled";
        error = "ROI import preparation was canceled";
      }
    } catch (const std::exception &e) {
      if (open && worker_generation == generation)
        error = e.what();
    }
    cell.reset();
  }
  template <class Work>
  void start(int next_phase, Work work, std::size_t workspace = 64 * 1048576) {
    if (working())
      throw std::invalid_argument("Wait for the previous ROI import worker");
    const auto result = std::make_shared<Cell>();
    JobOptions options;
    options.label = next_phase == 1   ? "Load ImageJ ROI members"
                    : next_phase == 4 ? "Read native ROI mapping"
                    : next_phase == 3 ? "Admit captured ROI import"
                                      : "Prepare ImageJ ROI preview";
    options.document_id = source->id();
    options.supersedes = "imagejxx.roi-import";
    // A declared running-workspace estimate; document/draft/cache allocations
    // remain outside the scheduler's workspace accounting, as in Jobs and log.
    options.resource_bytes = workspace;
    auto next = scheduler->submit(
        [result, work = std::move(work),
         next_phase](const JobContext &context) mutable {
          context.stage_progress(
              next_phase == 1   ? "Read and classify ROI members"
              : next_phase == 4 ? "Read native ROI mapping"
              : next_phase == 3 ? "Validate captured import equation"
                                : "Validate complete import",
              0, 1);
          auto value = work();
          context.checkpoint();
          {
            std::lock_guard<std::mutex> lock(result->mutex);
            result->result = std::move(value);
          }
          context.progress(1, 1);
        },
        options);
    job = std::move(next);
    cell = result;
    harvested = false;
    worker_generation = generation;
    phase = next_phase;
  }
  bool mixed() const {
    if (typed) return true;
    if (original_mixed || std::any_of(entries.begin(), entries.end(),
                                      [](const auto &e) { return e.display || (!e.envelope&&e.has_edit()); }))
      return true;
    if (!source)
      return false;
    const auto found = source->data().metadata.extensions.find(
        imagej_roi_import_sources_extension);
    if (found == source->data().metadata.extensions.end())
      return false;
    const auto p =
        std::get_if<std::shared_ptr<const MetadataTree>>(&found->second);
    if (!p || !*p)
      return false;
    const auto object = std::get_if<MetadataObject>(&(*p)->value);
    if (!object || !object->count("version"))
      return false;
    const auto &v = object->at("version");
    if (auto n = std::get_if<std::int64_t>(&v))
      return *n == 3 || *n == 4;
    if (auto n = std::get_if<std::uint64_t>(&v))
      return *n == 3 || *n == 4;
    return false;
  }
  static std::vector<ImageJAnnotationImportItem> typed_request(const std::vector<Entry> &entries,const DocumentSnapshot &source,bool original,bool destination_confirmed) {
    auto legacy = request(entries,source,original,destination_confirmed);
    std::vector<ImageJAnnotationImportItem> values;
    values.reserve(legacy.size());
    for (std::size_t i = 0; i < legacy.size(); ++i) {
      auto &v = legacy[i];
      const auto &e = entries[i];
      if (!e.display) {
        values.emplace_back(std::move(v));
        continue;
      }
      std::optional<ImageJDisplayRoiMapping> mapping;
      if (v.mapping) {
        mapping = ImageJDisplayRoiMapping{*v.mapping, {}};
        if(e.edited_mapping)mapping->text_default_color=e.edited_default_color;
        else if (e.needs_default) {
          if (!e.default_confirmed)
            throw std::invalid_argument(
                "Choose an explicit default color for unspecified text color");
          std::uint32_t color = 0;
          const auto &text = e.default_color;
          const auto parsed = std::from_chars(
              text.data(), text.data() + text.size(), color, 16);
          if (text.size() != 8 || parsed.ec != std::errc{} ||
              parsed.ptr != text.data() + text.size())
            throw std::invalid_argument(
                "Text ARGB requires exactly8 hexadecimal digits");
          mapping->text_default_color = color;
        }
      }
      values.emplace_back(ImageJDisplayRoiImportItem{
          std::move(v.name), std::move(v.data), mapping,
          e.display_replacement ? e.display_replacement
                                : e.display_original_replacement});
    }
    return values;
  }
  Json review() const {
    Json result = Json::array();
    auto append = [&](const auto &record) {
      const auto &row = record.annotation();
      std::string kind;
      if constexpr (std::is_same_v<std::decay_t<decltype(row)>,
                                   PlanarAnnotation>)
        kind = row.selection.kind;
      else
        kind = row.payload.index() == 0 ? "text" : "image";
      result.push_back({{"name", row.name},
                        {"kind", kind},
                        {"plane", int(row.plane)},
                        {"coordinate", row.coordinate},
                        {"t", row.t ? Json(*row.t) : Json(nullptr)},
                        {"c", row.c ? Json(*row.c) : Json(nullptr)}});
    };
    if (prepared)
      for (const auto &record : prepared->records())
        append(record);
    if (mixed_prepared)
      for (const auto &record : mixed_prepared->records())
        std::visit(append, record);
    return result;
  }
  static std::vector<ImageJRoiImportItem> request(const std::vector<Entry> &entries,const DocumentSnapshot &source,bool original,bool destination_confirmed) {
    if (original && !destination_confirmed)
      throw std::invalid_argument(
          "Confirm the reviewed destination before rebinding this import");
    std::vector<ImageJRoiImportItem> items;
    items.reserve(entries.size());
    for (const auto &entry : entries) {
      auto item = entry.item;
      item.data=*entry.data;
      if (!entry.envelope) {
        if (entry.plane < 0 || entry.plane > 2)
          throw std::invalid_argument("Choose a plane for every standard ROI");
        ImageJRoiPlaneMapping mapping;
        mapping.extent = source.data().dataset.shape();
        mapping.plane = Plane(entry.plane);
        if (entry.explicit_slice) {
          if (entry.slice < 1)
            throw std::invalid_argument(
                "Slice must be a positive 1-based index");
          mapping.coordinate = entry.slice - 1;
        }
        mapping.z_position =
            static_cast<ImageJZPositionMapping>(entry.z_policy);
        mapping.legacy_position =
            static_cast<ImageJLegacyPositionMapping>(entry.legacy_policy);
        item.mapping = entry.edited_mapping?entry.edited_mapping:std::optional<ImageJRoiPlaneMapping>{mapping};
      } else
        item.mapping.reset();
      if (entry.replacement)
        item.native_replacement = entry.replacement;
      items.push_back(std::move(item));
    }
    return items;
  }
};
RoiImportPanel::RoiImportPanel(std::shared_ptr<JobScheduler> scheduler)
    : state_(std::make_unique<State>(std::move(scheduler))) {}
RoiImportPanel::~RoiImportPanel() = default;
void RoiImportPanel::poll() { state_->poll(); }
void RoiImportPanel::wait() const {
  if (state_->job.valid())
    state_->job.wait();
}
bool RoiImportPanel::wait_for(std::chrono::milliseconds timeout) const {
  return !state_->job.valid() ||
         state_->job.wait_for(std::max(timeout, std::chrono::milliseconds(0)));
}
bool RoiImportPanel::visible() const { return state_->open; }
std::shared_ptr<const RoiImportPreview>
RoiImportPanel::preview(const DocumentSnapshot &current,
                        std::size_t cap) const {
  const auto &s = *state_;
  return s.open && (s.prepared || s.mixed_prepared) && s.preview &&
                 s.preview_enabled && !s.working() &&
                 s.job.info().state == JobState::Ready &&
                 s.preview->matches(current, cap)
             ? s.preview
             : nullptr;
}
void RoiImportPanel::preview_error(const std::string &message) {
  auto &s = *state_;
  if (!s.open)
    return;
  s.invalidate();
  s.error = "Import preview failed: " + message;
}
void RoiImportPanel::cancel(const char *reason) {
  auto &s = *state_;
  if (s.job.valid())
    s.job.cancel();
  s.open = false;
  s.source.reset();
  s.entries.clear();
  s.editor.reset();
  s.display_editor.reset();
  s.display_editor_rows.clear();
  s.editor_rows.clear();s.editor_mapping.reset();s.editor_default_color.reset();s.editor_cap=0;
  s.prepared.reset();
  s.mixed_prepared.reset();
  s.preview.reset();
  s.original.clear();
  s.typed.reset();
  s.recorded_extent.reset();
  s.destination_confirmed = false;
  s.limits = ImageJRoiImportLimits{};
  s.display_retained = 128 * 1048576;
  s.transport=ImageJRoiZipLimits{};s.limit_editor.reset();
  s.original_mixed = false;
  s.image_review = false;
  s.reason = reason;
  s.controls = Json::object();
  ++s.generation;
}
void RoiImportPanel::show(const DocumentSnapshot &source) {
  auto &s = *state_;
  s.poll();
  if (s.working() && s.job.info().state == JobState::Queued) {
    s.job.cancel();
    s.poll();
  }
  if (s.working())
    throw std::invalid_argument(
        "Wait for the canceled ROI import worker to finish");
  cancel("reopened");
  s.source = source;
  s.open = s.focus = true;
  s.path.clear();
  s.error.clear();
  s.reason.clear();
  s.mode = 0;
  s.draft = false;
  s.preview_enabled = true;
}
void RoiImportPanel::show_record(const DocumentSnapshot &source,
                                 MetadataObject original) {
  show(source);
  auto &s = *state_;
  s.original = std::move(original);
  const auto record = s.original;
  s.start(3, [record] { return captured_inputs(record); });
}
void RoiImportPanel::show_draft(const DocumentSnapshot &source, MetadataObject record) {
  show_record(source, std::move(record));
  state_->draft = true;
}
void RoiImportPanel::show_draft(const DocumentSnapshot &source,
                                 std::shared_ptr<const NativeWorkflowImport> intent) {
  if (!intent) throw std::invalid_argument("Missing workflow import intent");
  show(source); auto &s=*state_; s.draft=true; s.typed=std::move(intent);
  if (s.typed->original) s.original=*s.typed->original;
  const auto held=s.typed; const auto shape=source.data().dataset.shape();
  s.start(3,[held,shape] { return typed_inputs(*held,shape); },
          std::max<std::size_t>(64*1048576,std::max(held->limits.batch.max_decoded_bytes,held->limits.max_display_retained_bytes)));
}
bool RoiImportPanel::working() const { return state_->working(); }
std::optional<JobInfo> RoiImportPanel::job_info() const {
  return state_->working() ? std::optional<JobInfo>(state_->job.info()) : std::nullopt;
}
RoiImportBrowseRequest RoiImportPanel::browse_request() const {
  const auto &s = *state_;
  if (!s.open || !s.source || s.working() || s.reviewing())
    throw std::invalid_argument("Browse requires an idle file import draft");
  return {*s.source, s.generation, s.path};
}
bool RoiImportPanel::browse_current(
    const RoiImportBrowseRequest &request,
    const std::optional<DocumentSnapshot> &current) const {
  const auto &s = *state_;
  return s.open && s.source && current && !s.working() && !s.reviewing() &&
         s.generation == request.generation &&
         request.source.same_document(*s.source) &&
         request.source.same_document(*current) &&
         request.source.revisions().matches(current->revisions(),
                                            Change::Pixels | Change::Metadata);
}
void RoiImportPanel::path(const RoiImportBrowseRequest &request,
                          const std::optional<DocumentSnapshot> &current,
                          std::string path) {
  auto &s = *state_;
  if (!browse_current(request, current))
    throw std::invalid_argument(
        "ROI import file choice expired; reopen Browse");
  s.path = std::move(path);
  s.entries.clear();
  s.invalidate();
}
void RoiImportPanel::draw(
    const std::optional<DocumentSnapshot> &current, bool busy, std::size_t cap,
    const std::function<void()> &browse,
    const std::function<void(Plane, int, std::optional<int>,
                             std::optional<int>)> &navigate,
    const std::function<void(const ResolvedImageJRoiImport &)> &publish,
    const std::function<void(const ResolvedImageJAnnotationImport &)>
        &publish_mixed) {
  auto &s = *state_;
  s.poll();
  s.controls = Json::object();
  if (!s.open)
    return;
  if (!current || !s.source || !s.source->same_document(*current) ||
      !s.source->revisions().matches(current->revisions(),
                                     Change::Pixels | Change::Metadata)) {
    cancel("source_changed");
    return;
  }
  if (busy) {
    cancel("busy");
    return;
  }
  if((s.editor||s.display_editor||s.phase==4)&&s.editor_cap!=cap){s.invalidate();s.editor.reset();s.display_editor.reset();s.editor_rows.clear();s.display_editor_rows.clear();s.error="Processing memory setting changed; reopen the annotation editor";}
  if (s.preview && s.preview->cap() != cap) {
    s.invalidate();
    s.error = "Processing memory setting changed; validate the import again";
  }
  if (s.focus)
    ImGui::SetNextWindowFocus();
  ImGui::SetNextWindowSize(s.image_review ? ImVec2{420, 350} : ImVec2{680, 690},
                           ImGuiCond_FirstUseEver);
  if (s.image_review) {
    const auto *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        {viewport->WorkPos.x + std::max(10.f, viewport->WorkSize.x - 430),
         viewport->WorkPos.y + std::max(10.f, viewport->WorkSize.y - 390)},
        ImGuiCond_FirstUseEver);
  }
  ImGui::Begin(s.draft ? "Edit workflow import choices"
                          : s.image_review ? "ROI import preview" : "Import ImageJ ROIs",
               &s.open);
  s.focus = false;
  {
    const auto a = ImGui::GetCurrentWindow()->InnerRect.Min,
               b = ImGui::GetCurrentWindow()->InnerRect.Max;
    s.bounds = {a.x, a.y, b.x, b.y};
  }
  ImGui::Text("Destination: %s", s.source->data().metadata.title.c_str());
  if (!s.image_review) {
    const bool running =
        s.working() || bool(s.editor) || bool(s.display_editor) || bool(s.limit_editor);
    ImGui::BeginDisabled(running);
    if (!s.reviewing()) {
      ImGui::TextWrapped("Load a .roi or .zip, choose each standard ROI's "
                         "plane, then validate "
                         "the complete import. Nothing changes until Apply.");
      ImGui::SetNextItemWidth(420);
      if (ImGui::InputText("File", &s.path)) {
        s.entries.clear();
        s.invalidate();
      }
      s.observe("path", !running);
      ImGui::SameLine();
      if (ImGui::Button("Browse..."))
        browse();
      s.observe("browse", !running);
      if (ImGui::Button("Load members"))
        try {
          s.entries.clear();
          s.invalidate();
          const auto path = s.path;
          const auto limits=s.limits;const auto transport=s.transport;
          s.start(1, [path,limits,transport] {
            Result r;
            r.entries = load(path,limits,transport);
            return r;
          },load_workspace(path,limits,transport));
        } catch (const std::exception &e) {
          s.error = e.what();
        }
      s.observe("load", !running);
      ImGui::SameLine();
    } else {
      ImGui::TextWrapped(
          "Captured inputs stay unchanged. Review the current destination and "
          "mapping choices to create a new import.");
      if (s.entries.empty()) {
        if (ImGui::Button("Retry captured inputs"))
          try {
            s.invalidate();
            const auto record = s.original;
            const auto intent=s.typed; const auto shape=s.source->data().dataset.shape();
            s.start(3, [record,intent,shape] { return intent ? typed_inputs(*intent,shape) : captured_inputs(record); });
          } catch (const std::exception &e) {
            s.error = e.what();
          }
        s.observe("retry_inputs", !running);
      }
      const auto d = s.source->data().dataset.shape();
      ImGui::Text("Destination T/C/Z/Y/X/S: %d/%d/%d/%d/%d/%d", d.t, d.c, d.z,
                  d.y, d.x, d.s);
      if (s.recorded_extent) {
        const auto old = *s.recorded_extent;
        ImGui::Text(s.typed ? "Choice template T/C/Z/Y/X/S: %d/%d/%d/%d/%d/%d" : "Recorded T/C/Z/Y/X/S: %d/%d/%d/%d/%d/%d", old.t, old.c,
                    old.z, old.y, old.x, old.s);
      }
      if (ImGui::Checkbox("Use reviewed mappings on this destination",
                          &s.destination_confirmed))
        s.invalidate();
      s.observe("confirm_destination", !running);
      ImGui::SameLine();
    }
    ImGui::BeginDisabled(s.reviewing() && s.entries.empty());
    if(ImGui::Button("Import limits...")){s.limit_editor.emplace(RoiImportLimitValues{{s.limits,s.display_retained},s.transport});++s.generation;}
    s.observe("limits",!running && (s.original.empty() || !s.entries.empty()));ImGui::EndDisabled();
    if (s.mixed())
      ImGui::TextWrapped("Mixed import: selection and text/image annotations. "
                         "Replace replaces both sets.");
    const char *modes[]{"Append annotations", "Replace annotations"};
    if (s.combo("Import mode", s.mode, modes, 2, "mode", !running))
      s.invalidate();
    if (!s.entries.empty()) {
      ImGui::Text("%zu members; every member will be imported",
                  s.entries.size());
      if (ImGui::BeginListBox("Members", {600, 115})) {
        ImGuiListClipper clip;
        clip.Begin(int(s.entries.size()));
        while (clip.Step())
          for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
            ImGui::PushID(i);
            if (ImGui::Selectable(s.entries[i].item.name.c_str(),
                                  s.selected == i))
              s.selected = i;
            s.observe("member." + std::to_string(i), !running);
            ImGui::PopID();
          }
        ImGui::EndListBox();
      }
      auto &e = s.entries.at(std::size_t(s.selected));
      if (e.display)
        ImGui::Text("Type: %s annotation", e.is_text ? "text" : "image");
      int format = e.envelope ? 1 : 0;
      const char *formats[]{"Standard ImageJ ROI",
                            "Native envelope (stored mapping)"};
      ImGui::BeginDisabled(
          bool(e.item.native_replacement) || bool(e.replacement) ||
          bool(e.display_original_replacement) || bool(e.display_replacement));
      if (s.combo("Interpretation", format, formats, 2, "format", !running)) {
        e.envelope = format == 1;
        s.invalidate();
      }
      ImGui::EndDisabled();
      if (!e.envelope) {
        ImGui::BeginDisabled(e.has_edit());
        if (e.needs_default) {
          if (ImGui::Checkbox("Use an explicit default text color",
                              &e.default_confirmed))
            s.invalidate();
          s.observe("default_color_confirm", !running);
          ImGui::SetNextItemWidth(130);
          if (ImGui::InputText("Default text ARGB (8 hex digits)",
                               &e.default_color))
            s.invalidate();
          s.observe("default_color", !running);
        }
        const char *names[]{"Choose a plane", "XY", "XZ", "YZ"};
        int plane = e.plane + 1;
        if (s.combo("Plane", plane, names, 4, "plane", !running)) {
          e.plane = plane - 1;
          s.invalidate();
        }
        if (ImGui::Checkbox("Choose an explicit slice", &e.explicit_slice))
          s.invalidate();
        s.observe("explicit_slice", !running);
        ImGui::BeginDisabled(!e.explicit_slice);
        ImGui::SetNextItemWidth(100);
        const auto id = ImGui::GetID("Slice (1-based)");
        if (ImGui::InputInt("Slice (1-based)", &e.slice, 0, 0))
          s.invalidate();
        s.observe("slice", !running && e.explicit_slice, id);
        ImGui::EndDisabled();
        const char *z_names[]{"Scientific Z (XY only)",
                              "Chosen plane's fixed axis"};
        if (s.combo("File Z position", e.z_policy, z_names, 2, "z_policy",
                    !running))
          s.invalidate();
        const char *legacy_names[]{"Reject ambiguous position",
                                   "Chosen plane's fixed axis",
                                   "C-fastest hyperstack (XY only)"};
        if (s.combo("Legacy stack position", e.legacy_policy, legacy_names, 3,
                    "legacy_policy", !running))
          s.invalidate();
        if (ImGui::Button("Use these choices for all standard ROIs")) {
          for (auto &entry : s.entries)
            if (!entry.envelope && !entry.has_edit()) {
              entry.plane = e.plane;
              entry.slice = e.slice;
              entry.explicit_slice = e.explicit_slice;
              entry.z_policy = e.z_policy;
              entry.legacy_policy = e.legacy_policy;
            }
          s.invalidate();
        }
        s.observe("map_all", !running);
        ImGui::EndDisabled();ImGui::SameLine();
      } else {
        ImGui::TextWrapped(
            "The envelope keeps its stored plane, extent and native metadata. "
            "A "
            "different destination extent needs explicit rebinding.");
      }
      // Offer promotion only for a bare rounded-primitive header. The worker
      // still admits the entire record and verifies exact mask membership.
      const auto &wire = *e.data;
      const bool rounded_source = !e.display && !e.envelope && !e.has_edit() &&
          wire.size() >= 64 && wire[6] == 1 && wire[48] == 0 && wire[49] == 0 &&
          (unsigned(wire[54]) * 256 + wire[55]) > 0 &&
          (unsigned(wire[54]) * 256 + wire[55]) <= 60535;
      bool promote = false;
      if (rounded_source) {
        promote = ImGui::Button("Edit as rounded rectangle...");
        s.observe("promote_rounded", !running);
        ImGui::SetItemTooltip("Create an editable rounded box while retaining the source ROI and mapping. Review and confirm the replacement before importing.");
      }
      const bool edit_replacement = ImGui::Button(e.envelope?"Edit native replacement...":"Edit mapped annotation...");
      s.observe("edit_replacement",!running);
      if(edit_replacement || promote)try{
        s.invalidate();s.editor_cap=cap;
        const auto entry=e;const auto source=*s.source;const auto index=std::size_t(s.selected);
        const ImageJAnnotationImportLimits limits{s.limits,s.display_retained};
        s.start(4,[entry,source,index,limits,promote]{
          detail::validate_imagej_annotation_import_limits(limits);
          auto items=State::typed_request({entry},source,false,true);
          auto admitted=detail::admit_imagej_annotation_import_item(items.at(0),limits);
          Result result;
          if(const auto g=std::get_if<MappedImageJRoi>(&admitted)){
            const auto mapped = promote ? editable_imagej_roi_mapping(*g, limits.batch.roi) : *g;
            result.editor.emplace(index,mapped.target_extent(),mapped.annotation());
            result.editor_rows=planar_annotations(source.data());
            if(!entry.envelope)result.editor_mapping=g->source_mapping();
          }else{
            const auto &d=std::get<MappedImageJDisplayRoi>(admitted);
            result.display_editor.emplace(index,d.target_extent(),d.annotation(),source);
            result.display_editor_rows=planar_display_annotations(source.data());
            if(!entry.envelope){result.editor_mapping=d.source_mapping().plane;result.editor_default_color=d.source_mapping().text_default_color;}
          }
          return result;
        },std::max<std::size_t>(64*1048576,e.display?s.display_retained:s.limits.max_decoded_bytes));
      }catch(const std::exception &error){s.error=error.what();}
      if(e.has_edit()){
        if(!e.envelope)ImGui::TextWrapped("Source mapping retained. The edited native row has independent destination positions; reset the edit to change the source mapping.");
        else ImGui::TextUnformatted("Explicit native replacement staged for this destination.");
        if(ImGui::Button(e.envelope?"Use recorded mapping":"Reset bare native edit")){
          e.replacement.reset();e.display_replacement.reset();
          if(!e.envelope){e.item.native_replacement.reset();e.display_original_replacement.reset();e.edited_mapping.reset();e.edited_default_color.reset();}
          s.invalidate();
        }
        s.observe("reset_replacement",!running);
      }
      if (ImGui::Button("Validate and review"))
        try {
          s.invalidate();
          if (s.mixed()) {
            const auto entries=s.entries;const auto confirmed=s.destination_confirmed;const auto reviewing=s.reviewing();
            const auto source = *s.source;
            const auto mode = s.mode == 0 ? ImageJRoiImportMode::Append
                                          : ImageJRoiImportMode::Replace;
            const auto original = s.original;
            const auto original_mixed = s.original_mixed;
            const ImageJAnnotationImportLimits limits{s.limits,
                                                      s.display_retained};
            s.start(
                2,
                [source, entries, confirmed, mode, cap, original,
                 original_mixed, limits, reviewing] {
                  if (!original.empty() && !original_mixed)
                    validate_imagej_roi_import_record(original);
                  auto items=State::typed_request(entries,source,reviewing,confirmed);
                  auto prepared =
                      original.empty() || !original_mixed
                          ? resolve_imagej_annotation_import(source, items,
                                                             mode, limits)
                          : rebind_imagej_annotation_import(
                                source, original, items, mode, limits);
                  admit_planar_annotation_visuals(source.data().dataset.shape(),
                                                  prepared.annotations(), cap);
                  admit_display_annotation_visuals(
                      prepared.display_annotations(), cap);
                  Result r;
                  r.mixed_prepared =
                      std::make_shared<const ResolvedImageJAnnotationImport>(
                          std::move(prepared));
                  r.preview = std::make_shared<const RoiImportPreview>(
                      *r.mixed_prepared, cap);
                  return r;
                },
                cap);
          } else {
            const auto entries=s.entries;const auto confirmed=s.destination_confirmed;
            const auto source = *s.source;
            const auto mode = s.mode == 0 ? ImageJRoiImportMode::Append
                                          : ImageJRoiImportMode::Replace;
            const auto original = s.original;
            const auto limits = s.limits;
            s.start(
                2,
                [source, entries, confirmed, mode, cap, original,
                 limits] {
                  Result r;
                  auto items=State::request(entries,source,!original.empty(),confirmed);
                  auto prepared =
                      original.empty()
                          ? resolve_imagej_roi_import(source, items, mode,
                                                      limits)
                          : rebind_imagej_roi_import(source, original, items,
                                                     mode, limits);
                  admit_planar_annotation_visuals(source.data().dataset.shape(),
                                                  prepared.annotations(), cap);
                  r.prepared = std::make_shared<const ResolvedImageJRoiImport>(
                      std::move(prepared));
                  r.preview = std::make_shared<const RoiImportPreview>(
                      *r.prepared, cap);
                  return r;
                },
                cap);
          }
        } catch (const std::exception &error) {
          s.error = error.what();
        }
      s.observe("validate", !running);
    }
    ImGui::EndDisabled();
  }
  if (s.working())
    ImGui::TextUnformatted(s.phase == 1   ? "Loading members..."
                           : s.phase == 3 ? "Validating captured import..."
                           : s.phase == 4 ? "Reading native replacement..."
                                          : "Validating the complete batch...");
  if (s.prepared || s.mixed_prepared) {
    ImGui::Separator();
    const auto review = s.review();
    ImGui::Text("Ready: %zu imported (%s)", review.size(),
                s.mode == 0 ? "append" : "replace");
    if (s.mixed_prepared)
      ImGui::Text("After: %zu selection, %zu display annotations",
                  s.mixed_prepared->annotations().size(),
                  s.mixed_prepared->display_annotations().size());
    const auto &row = review.at(std::size_t(s.selected));
    ImGui::Text("Selected: %s | %s | %s slice %d",
                row.at("name").get_ref<const std::string &>().c_str(),
                row.at("kind").get_ref<const std::string &>().c_str(),
                std::array<const char *, 3>{"XY", "XZ", "YZ"}.at(
                    row.at("plane").get<std::size_t>()),
                row.at("coordinate").get<int>() + 1);
    const auto time = row.at("t").is_null()
                          ? "all"
                          : std::to_string(row.at("t").get<int>() + 1);
    const auto channel = row.at("c").is_null()
                             ? "all"
                             : std::to_string(row.at("c").get<int>() + 1);
    ImGui::Text("Time: %s, scientific channel: %s", time.c_str(),
                channel.c_str());
    if (!s.draft) {
    if (ImGui::Button("Go to selected member"))
      navigate(
          Plane(row.at("plane").get<int>()), row.at("coordinate").get<int>(),
          row.at("t").is_null() ? std::optional<int>{} : row.at("t").get<int>(),
          row.at("c").is_null() ? std::optional<int>{}
                                : row.at("c").get<int>());
    s.observe("go_to");
    ImGui::Checkbox("Show prepared annotations", &s.preview_enabled);
    s.observe("preview_enabled");
    if (ImGui::Button(s.image_review ? "Edit mapping choices"
                                     : "Review on image")) {
      s.image_review = !s.image_review;
      s.focus = true;
    }
    s.observe("image_review");
    }
  }
  ImGui::BeginDisabled((!s.prepared && !s.mixed_prepared) || s.working() ||
                       bool(s.editor) || bool(s.display_editor) || bool(s.limit_editor));
  const bool apply =
      ImGui::Button(s.draft ? "Keep workflow import choices"
                    : s.mode == 0 ? "Apply complete import"
                                : "Replace annotations with this import");
  s.observe("apply",
            (bool(s.prepared) || bool(s.mixed_prepared)) && !s.working());
  ImGui::EndDisabled();
  if (!s.image_review)
    ImGui::SameLine();
  bool close = ImGui::Button("Cancel");
  s.observe("cancel");
  close |= ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
           ImGui::IsKeyPressed(ImGuiKey_Escape);
  if (!s.error.empty())
    ImGui::TextWrapped("%s", s.error.c_str());
  ImGui::End();
  if (!s.open || close) {
    cancel("canceled");
    return;
  }
  if (s.display_editor) {
    const auto status = s.display_editor->draw(s.source->data().dataset.shape(),
                                               s.display_editor_rows,cap);
    const auto diagnostics = s.display_editor->diagnostics();
    for (const auto &control : diagnostics.at("controls").items())
      s.controls["replacement." + control.key()] = control.value();
    if (status == 1) {
      s.entries.at(s.display_editor->index()).display_replacement =
          s.display_editor->replacement(s.source->data().dataset.shape());
      auto &entry=s.entries.at(s.display_editor->index());if(!entry.envelope){entry.edited_mapping=s.editor_mapping;entry.edited_default_color=s.editor_default_color;}
      s.invalidate();
    }
    if (status) {
      s.display_editor.reset();
      s.display_editor_rows.clear();
    }
  }
  if (s.editor) {
    const auto status =
        s.editor->draw(s.source->data().dataset.shape(), s.editor_rows);
    const auto editor_diagnostics = s.editor->diagnostics();
    for (const auto &control : editor_diagnostics.at("controls").items())
      s.controls["replacement." + control.key()] = control.value();
    if (status == 1) {
      s.entries.at(s.editor->index()).replacement =
          s.editor->replacement(s.source->data().dataset.shape());
      auto &entry=s.entries.at(s.editor->index());if(!entry.envelope)entry.edited_mapping=s.editor_mapping;
      s.invalidate();
    }
    if (status) {
      s.editor.reset();
      s.display_editor.reset();
      s.display_editor_rows.clear();
      s.editor_rows.clear();
    }
  }
  if(s.limit_editor){
    RoiImportLimitValues values;const auto status=s.limit_editor->draw(values);
    if(status==1){s.invalidate();s.limits=values.annotations.batch;s.display_retained=values.annotations.max_display_retained_bytes;s.transport=values.zip;
      if(s.original.empty())s.entries.clear();
      s.editor.reset();s.display_editor.reset();s.editor_rows.clear();s.display_editor_rows.clear();
    }
    if(status)s.limit_editor.reset();
  }
  if (apply)
    try {
      s.job.complete([&] {
        if (s.mixed_prepared)
          publish_mixed(*s.mixed_prepared);
        else
          publish(*s.prepared);
      });
      cancel("accepted");
    } catch (const std::exception &e) {
      s.error = e.what();
      s.prepared.reset();
      s.mixed_prepared.reset();
      s.preview.reset();
      s.image_review = false;
    }
}
Json RoiImportPanel::diagnostics() const {
  const auto &s = *state_;
  Json entries = Json::array(), review = Json::array();
  for (const auto &e : s.entries)
    entries.push_back({{"name", e.item.name},
                       {"envelope", e.envelope},{"native_edit",e.has_edit()},{"mapping_frozen",bool(e.edited_mapping)},{"source_extent",e.edited_mapping?Json(e.edited_mapping->extent.dimensions()):Json(nullptr)},
                       {"plane", e.plane},
                       {"explicit_slice", e.explicit_slice},
                       {"slice", e.slice},
                       {"z_policy", e.z_policy},
                       {"legacy_policy", e.legacy_policy},
                       {"display", e.display},
                       {"text", e.is_text},
                       {"needs_default", e.needs_default},
                       {"default_confirmed", e.default_confirmed}});
  review = s.review();
  return {
      {"draft_mode", s.draft},
      {"visible", s.open},
      {"worker_active", s.working()},
      {"job_id", s.job.valid() ? s.job.info().id : 0},
      {"job_cancel_requested", s.job.valid() && s.job.info().cancel_requested},
      {"phase", s.phase},
      {"ready", bool(s.prepared) || bool(s.mixed_prepared)},
      {"mixed", s.mixed()},
      {"image_review", s.image_review},
      {"workflow_rebind", s.reviewing()},
      {"destination_confirmed", s.destination_confirmed},
      {"preview_enabled", s.preview_enabled},
      {"source_document_id", s.source ? s.source->id() : 0},
      {"path", s.path},
      {"entries", entries},
      {"limits_editor",s.limit_editor?s.limit_editor->diagnostics():Json(nullptr)},
      {"limits",{{"input",s.limits.max_total_bytes},{"decoded",s.limits.max_decoded_bytes},{"archive",s.limits.max_provenance_bytes},{"wire",s.limits.roi.max_bytes},{"text",s.limits.roi.max_text_bytes},{"pixels",s.limits.roi.max_image_pixels},{"display",s.display_retained},{"zip_archive",s.transport.max_archive_bytes},{"zip_member",s.transport.max_entry_bytes},{"zip_working",s.transport.max_working_bytes}}},
      {"replacement_editor",
       s.display_editor ? s.display_editor->diagnostics()
                        : (s.editor ? s.editor->diagnostics() : Json(nullptr))},
      {"selected", s.selected},
      {"mode", s.mode},
      {"review", review},
      {"controls", s.controls},
      {"window", s.bounds},
      {"error", s.error},
      {"cancel_reason", s.reason}};
}
} // namespace imagejxx::desktop
