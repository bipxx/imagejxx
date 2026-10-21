#include <imagejxx/imagej_roi_projection.hpp>
#include "roi_export_panel.hpp"
#include "roi_mixed_export_panel.hpp"
#include "../native/atomic_file.hpp"
#include "../native/imagej_roi_import_internal.hpp"
#include "../native/imagej_roi_zip_internal.hpp"
#include "roi_import_preview.hpp"
#include <algorithm>
#include <cctype>
#include <imagejxx/imagej_roi_pairing.hpp>
#include <imagejxx/roi_style.hpp>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <mutex>

namespace imagejxx::desktop {
namespace {
std::string export_error(const std::exception &error) {
  if (const auto file =
          dynamic_cast<const std::filesystem::filesystem_error *>(&error)) {
    auto text = "ROI file operation failed (code " +
                std::to_string(file->code().value()) +
                "): " + file->path1().u8string();
    if (!file->path2().empty())
      text += " -> " + file->path2().u8string();
    return text;
  }
  return error.what();
}
struct Review {
  ResolvedImageJRoiExport command;
  std::shared_ptr<const RoiImportPreview> preview;
};
struct ZipReview {
  ResolvedImageJRoiZipExport command;
  std::shared_ptr<const RoiImportPreview> preview;
};
struct Result {
  std::optional<ImageJRoiZipExportRequest> zip_request;
  std::shared_ptr<const ZipReview> zip_review;
  std::optional<ImageJRoiExportRequest> request;
  std::optional<ImageJRoiSourceCatalog> catalog;
  std::vector<PlanarAnnotation> rows;
  std::shared_ptr<const Review> review;
  std::unique_ptr<detail::StagedAtomicFile> staged;
};
Result inspect_inputs(const DocumentSnapshot &source,
                      const MetadataObject &original, bool zip) {
  Result result;
  if (!original.empty()) {
    if (zip)
      result.zip_request = decode_imagej_roi_zip_export_request(original);
    else
      result.request = decode_imagej_roi_export_request(original);
  }
  result.catalog = inspect_imagej_roi_sources(source);
  result.rows = planar_annotations(source.data());
  return result;
}
bool same_source(const DocumentSnapshot &source,
                 const DocumentSnapshot &current) {
  return source.same_document(current) && source.shares_data(current) &&
         source.revisions().matches(current.revisions(),
                                    Change::Pixels | Change::Metadata);
}
std::filesystem::path destination(const std::string &text, bool zip) {
  namespace fs = std::filesystem;
  if (text.empty() || text.find('\0') != std::string::npos)
    throw std::invalid_argument("Choose an output ROI or ZIP file");
  auto path = fs::absolute(fs::u8path(text));
  auto extension = path.extension().u8string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  if (extension != (zip ? ".zip" : ".roi") ||
      !fs::is_directory(path.parent_path()))
    throw std::invalid_argument(
        "Choose the expected ROI/ZIP extension in an existing folder");
  if (fs::exists(path) && !fs::is_regular_file(path))
    throw std::invalid_argument("Output must be a regular file");
  return path;
}
ImageJRoi projection(const ResolvedImageJRoiPairing &pair, int choice,
                     bool reset_geometry, bool group_override, int group,
                     bool properties_override, const std::string &properties,
                     const std::optional<ImageJRoi> &captured, const ImageJRoiLimits &limits) {
  if (choice < 0 || choice > 2 || (choice == 2 && !captured))
    throw std::invalid_argument("Choose fresh or retained standard metadata");
  ImageJRoi out = choice == 2   ? *captured
                  : choice == 1 ? pair.mapped().source_record()
                                : ImageJRoi{};
  if (reset_geometry) {
    out.source_type.reset();
    out.source_subtype = 0;
    out.ignored_rotated_parameters.reset();
    out.composite_path.clear();
    out.rounded_rectangle.reset();
    out.arrow.reset();
    out.spline.reset();
    out.point_counters.clear();
    out.point_type = out.point_size = 0;
    out.options = 8192;
  }
  auto selection = pair.mapped().annotation().selection;
  for (auto it = selection.attributes.begin();
       it != selection.attributes.end();)
    if (it->first != roi_style_attribute && !(selection.is_rounded_box() && it->first == "corner_diameter"))
      it = selection.attributes.erase(it);
    else
      ++it;
  if (selection.is_rounded_box() && !reset_geometry && out.roi.selection.kind == "mask" &&
      (out.rounded_rectangle || !out.composite_path.empty())) {
    auto retained=out.roi.selection;
    retained.width=selection.width;retained.attributes=selection.attributes;
    retained.attributes.erase("corner_diameter");selection=std::move(retained);
  }
  const auto &row = pair.mapped().annotation();
  out.roi = {row.name, std::move(selection), row.t, row.c, row.coordinate, {}};
  out.legacy_position = 0;
  if (group_override) {
    if (group < 0 || group > 255)
      throw std::invalid_argument("Group must be between 0 and 255");
    out.group = static_cast<unsigned char>(group);
  }
  if (properties_override)
    out.properties = properties;
  const auto geometry=planar_annotation_geometry(pair.mapped().target_extent(),row.plane);
  return imagej_roi_standard_projection(std::move(out),geometry.width,geometry.height,limits);
}
} // namespace
struct RoiExportPanel::State {
  explicit State(std::shared_ptr<JobScheduler> value)
      : mixed(std::make_unique<RoiMixedExportPanel>(value)),
        scheduler(std::move(value)) {}
  struct Cell {
    std::mutex mutex;
    std::optional<Result> result;
  };
  std::unique_ptr<RoiMixedExportPanel> mixed;
  bool mixed_mode=false;
  std::uint64_t route=0;
  std::shared_ptr<JobScheduler> scheduler;
  Job job;
  std::shared_ptr<Cell> cell;
  std::optional<DocumentSnapshot> source;
  std::optional<ImageJRoiSourceCatalog> catalog;
  MetadataObject original;
  std::optional<ImageJRoiExportRequest> captured;
  std::optional<ImageJRoiZipExportRequest> captured_zip;
  std::vector<ImageJRoiZipExportItem> queue;
  std::shared_ptr<const ZipReview> zip_review;
  bool zip_mode = false, compress = true, compose = true;
  int template_member = -1;
  std::string item_name;
  void clear_choices() {
    member = row = metadata = template_member = -1;
    confirmed = reset_geometry = group_override = properties_override = false;
    group = 0;
    properties.clear();
    item_name.clear();
  }
  void admit_queue_item() const {
    const auto limits =
        captured_zip ? captured_zip->limits : ImageJRoiZipExportLimits{};
    if (queue.size() >= limits.zip.max_entries)
      throw std::invalid_argument("ZIP member count limit");
    (void)detail::imagej_roi_zip_member_key(item_name, limits.zip);
    std::size_t bytes = 0, nodes = 0;
    auto charge = [&](const std::string &name, const ImageJRoi &projection) {
      detail::charge_imagej_roi_projection(projection, limits.sources, bytes,
                                           nodes);
      const auto extra = 2 * sizeof(ImageJRoiZipExportItem) + 4 * name.size();
      if (extra > limits.sources.max_decoded_bytes - bytes)
        throw std::invalid_argument("ZIP queue retention limit");
      bytes += extra;
    };
    for (const auto &item : queue) {
      if (item.name == item_name)
        throw std::invalid_argument("Duplicate ZIP member name");
      charge(item.name, item.standard_projection);
    }
    charge(item_name, review->command.standard_projection());
  }
  bool destination_confirmed = false;
  std::vector<PlanarAnnotation> rows;
  std::shared_ptr<const Review> review;
  std::unique_ptr<detail::StagedAtomicFile> staged;
  std::uint64_t generation = 0, worker_generation = 0;
  bool open = false, focus = false, harvested = true, confirmed = false,
       group_override = false, properties_override = false;
  bool overwrite = false, image_review = false, preview_enabled = true;
  bool reset_geometry = false;
  int member = -1, row = -1, metadata = -1, group = 0, phase = 0;
  std::string path, error, message, reason, properties;
  Json controls = Json::object();
  ~State() {
    if (job.valid()) {
      job.cancel();
      job.wait();
    }
  }
  bool working() const { return job.valid() && !harvested; }
  void invalidate() {
    if (job.valid())
      job.cancel();
    review.reset();
    zip_review.reset();
    staged.reset();
    image_review = false;
    error.clear();
    message.clear();
    ++generation;
  }
  void observe(const std::string &key, ImGuiID id = 0, float width = 0) {
    const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    controls[key] = {
        {"id", id ? id : ImGui::GetItemID()},
        {"enabled", !(ImGui::GetCurrentContext()->LastItemData.ItemFlags &
                      ImGuiItemFlags_Disabled)},
        {"rect", {a.x, a.y, b.x, b.y}},
        {"point",
         {width ? a.x + width / 2 : (a.x + b.x) / 2, (a.y + b.y) / 2}}};
  }
  void poll() {
    if (!job.valid() || !job.ready())
      return;
    if (harvested) {
      if ((review || zip_review || staged) &&
          job.info().state == JobState::Canceled) {
        invalidate();
        error = "ROI export canceled";
      }
      return;
    }
    harvested = true;
    const auto finished = phase;
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
      if (finished == 1)
        job.complete([&] {
          catalog = std::move(result.catalog);
          rows = std::move(result.rows);
          captured = std::move(result.request);
          captured_zip = std::move(result.zip_request);
        });
      else if (finished == 2)
        review = std::move(result.review);
      else if (finished == 4)
        zip_review = std::move(result.zip_review);
      else
        staged = std::move(result.staged);
    } catch (const OperationCancelled &) {
      if (open && worker_generation == generation) {
        invalidate();
        error = "ROI export canceled";
      }
    } catch (const std::exception &e) {
      if (open && worker_generation == generation)
        error = export_error(e);
    }
    cell.reset();
  }
  template <class Work>
  void start(int next_phase, Work work, std::size_t workspace) {
    if (working())
      throw std::invalid_argument("Wait for the previous ROI export worker");
    auto result = std::make_shared<Cell>();
    JobOptions options;
    options.label = next_phase == 4   ? "Prepare complete ROI ZIP"
                    : next_phase == 1 ? "Inspect ROI sources"
                    : next_phase == 2 ? "Prepare paired ROI export"
                                      : "Stage ROI export file";
    options.document_id = source->id();
    options.supersedes = "imagejxx.roi-export";
    options.resource_bytes = workspace;
    auto next = scheduler->submit(
        [result, work = std::move(work)](const JobContext &context) mutable {
          context.checkpoint();
          auto value = work(context);
          context.checkpoint();
          {
            std::lock_guard<std::mutex> lock(result->mutex);
            result->result = std::move(value);
          }
        },
        options);
    job = std::move(next);
    cell = result;
    harvested = false;
    worker_generation = generation;
    phase = next_phase;
  }
};
RoiExportPanel::RoiExportPanel(std::shared_ptr<JobScheduler> scheduler)
    : state_(std::make_unique<State>(std::move(scheduler))) {}
RoiExportPanel::~RoiExportPanel() = default;
void RoiExportPanel::poll() { state_->poll(); state_->mixed->poll(); }
void RoiExportPanel::wait() const {
  if(state_->mixed_mode){state_->mixed->wait();return;}
  if (state_->job.valid())
    state_->job.wait();
}
bool RoiExportPanel::wait_for(std::chrono::milliseconds time) const {
  if(state_->mixed_mode)return state_->mixed->wait_for(time);
  return !state_->job.valid() ||
         state_->job.wait_for(std::max(time, std::chrono::milliseconds(0)));
}
bool RoiExportPanel::zip_mode() const { return state_->mixed_mode?state_->mixed->zip_mode():state_->zip_mode; }
bool RoiExportPanel::visible() const { return state_->mixed_mode?state_->mixed->visible():state_->open; }
void RoiExportPanel::cancel(const char *reason) {
  auto &s = *state_;
  ++s.route;s.mixed->cancel(reason);
  s.invalidate();
  s.open = false;
  s.source.reset();
  s.catalog.reset();
  s.original.clear();
  s.captured.reset();
  s.captured_zip.reset();
  s.queue.clear();
  s.zip_review.reset();
  s.destination_confirmed = false;
  s.rows.clear();
  s.reason = reason;
  s.controls = Json::object();
}
void RoiExportPanel::show(const DocumentSnapshot &source,
                          MetadataObject original, bool zip) {
  auto &s = *state_;
  s.poll();
  s.mixed->poll();
  if(s.mixed->working())throw std::invalid_argument("Wait for the previous mixed export worker");
  if (s.working() && s.job.info().state == JobState::Queued) {
    s.job.cancel();
    s.poll();
  }
  if (s.working())
    throw std::invalid_argument("Wait for the previous ROI export worker");
  cancel("reopened");
  s.mixed_mode=needs_mixed_roi_export(source,original);
  if(s.mixed_mode){s.mixed->show(source,std::move(original),zip);return;}
  s.zip_mode = zip;
  s.compress = true;
  s.clear_choices();
  s.compose = true;
  s.source = source;
  s.original = std::move(original);
  s.open = s.focus = true;
  s.member = s.row = s.metadata = -1;
  s.confirmed = s.group_override = s.properties_override = s.overwrite = false;
  s.reset_geometry = false;
  s.preview_enabled = true;
  s.path.clear();
  s.properties.clear();
  s.group = 0;
  s.reason.clear();
  s.start(
      1,
      [source, original = s.original, zip](const JobContext &) {
        return inspect_inputs(source, original, zip);
      },
      64 * 1048576);
}
std::shared_ptr<const RoiImportPreview>
RoiExportPanel::preview(const DocumentSnapshot &current,
                        std::size_t cap) const {
  const auto &s = *state_;
  if(s.mixed_mode)return s.mixed->preview(current,cap);
  const auto preview = s.zip_review ? s.zip_review->preview
                       : s.review   ? s.review->preview
                                    : nullptr;
  return s.open && preview && s.preview_enabled &&
                 preview->matches(current, cap)
             ? preview
             : nullptr;
}
void RoiExportPanel::preview_error(const std::string &error) {
  if(state_->mixed_mode){state_->mixed->preview_error(error);return;}
  state_->invalidate();
  state_->error = "Export preview failed: " + error;
}
RoiImportBrowseRequest RoiExportPanel::browse_request() const {
  const auto &s = *state_;
  if(s.mixed_mode){auto r=s.mixed->browse_request();r.route=s.route;return r;}
  if (!s.open || !s.source || s.working())
    throw std::invalid_argument("Browse requires an idle ROI export");
  return {*s.source, s.generation, s.path, s.route};
}
bool RoiExportPanel::browse_current(
    const RoiImportBrowseRequest &request,
    const std::optional<DocumentSnapshot> &current) const {
  const auto &s = *state_;
  if(request.route!=s.route)return false;
  if(s.mixed_mode)return s.mixed->browse_current(request,current);
  return s.open && s.source && current && !s.working() &&
         s.generation == request.generation &&
         same_source(request.source, *current) &&
         same_source(*s.source, *current);
}
void RoiExportPanel::path(const RoiImportBrowseRequest &request,
                          const std::optional<DocumentSnapshot> &current,
                          std::string path) {
  if (!browse_current(request, current))
    throw std::invalid_argument("ROI export file choice expired");
  if(state_->mixed_mode){state_->mixed->path(request,current,std::move(path));return;}
  state_->path = std::move(path);
  state_->overwrite = false;
  ++state_->generation;
}
void RoiExportPanel::draw(
    const std::optional<DocumentSnapshot> &current, bool busy, std::size_t cap,
    const std::function<void()> &browse,
    const std::function<void(const PlanarAnnotation &)> &go_to,
    const std::function<void(const ResolvedImageJRoiExport &,
                             const std::function<void()> &)> &publish,
    const std::function<void(const ResolvedImageJRoiZipExport &,
                             const std::function<void()> &)> &publish_zip,
    const std::function<void(const PlanarDisplayAnnotation &)> &go_to_display,
    const std::function<void(const ResolvedImageJAnnotationExport &,const std::function<void()> &)> &publish_mixed) {
  auto &s = *state_;
  if(s.mixed_mode){s.mixed->draw(current,busy,cap,browse,go_to,go_to_display,publish_mixed);return;}
  s.poll();
  s.controls = Json::object();
  if (!s.open)
    return;
  if (!current || !s.source || !same_source(*s.source, *current)) {
    cancel("source_changed");
    return;
  }
  if (busy) {
    cancel("busy");
    return;
  }
  if ((s.review && s.review->preview->cap() != cap) ||
      (s.zip_review && s.zip_review->preview->cap() != cap)) {
    s.invalidate();
    s.error = "Processing memory setting changed; validate the export again";
  }
  if (s.staged) {
    try {
      auto success = "Exported " + s.path;
      s.job.complete([&] {
        if (!same_source(*s.source, *current))
          throw RevisionConflict();
        if (s.zip_mode)
          publish_zip(s.zip_review->command,
                      [&] { s.staged->publish(s.overwrite); });
        else
          publish(s.review->command, [&] { s.staged->publish(s.overwrite); });
      });
      s.staged.reset();
      s.message.swap(success);
    } catch (const std::exception &e) {
      s.staged.reset();
      s.error = export_error(e);
    }
  }
  if (s.focus)
    ImGui::SetNextWindowFocus();
  s.focus = false;
  ImGui::SetNextWindowSize(s.image_review ? ImVec2{440, 380} : ImVec2{700, 740},
                           ImGuiCond_FirstUseEver);
  ImGui::Begin(s.image_review ? "ROI export preview"
               : s.zip_mode   ? "Export ImageJ ROI ZIP"
                              : "Export paired ImageJ ROI",
               &s.open);
  ImGui::Text("Image: %s", s.source->data().metadata.title.c_str());
  ImGui::BeginDisabled(s.working());
  if (!s.catalog && !s.working()) {
    if (ImGui::Button("Retry inspected inputs"))
      try {
        s.invalidate();
        const auto source = *s.source;
        const auto original = s.original;
        const auto zip = s.zip_mode;
        s.start(
            1,
            [source, original, zip](const JobContext &) {
              return inspect_inputs(source, original, zip);
            },
            64 * 1048576);
      } catch (const std::exception &e) {
        s.error = export_error(e);
      }
    s.observe("retry_inputs");
  }
  if (s.zip_mode && !s.image_review && s.catalog) {
    if (ImGui::Checkbox("Edit a member", &s.compose))
      s.invalidate();
    s.observe("compose");
  }
  if (!s.image_review && s.catalog && (!s.zip_mode || s.compose)) {
    if (!s.original.empty()) {
      ImGui::TextWrapped("The captured export stays unchanged. Choose this "
                         "image's pairing and a new output file.");
      if (ImGui::Checkbox("Use reviewed pairing on this image",
                          &s.destination_confirmed))
        s.invalidate();
      s.observe("confirm_destination");
    }
    auto choose = [&](const char *label, const char *key, int &chosen,
                      const auto &values, auto name) {
      const std::string selected = chosen >= 0
                                       ? name(values.at(std::size_t(chosen)))
                                       : "Choose explicitly";
      if (ImGui::BeginCombo(label, selected.c_str())) {
        ImGuiListClipper clip;
        clip.Begin(int(values.size()));
        while (clip.Step())
          for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
            ImGui::PushID(i);
            const auto text = name(values[i]);
            if (ImGui::Selectable(text.c_str(), chosen == i)) {
              chosen = i;
              s.invalidate();
            }
            s.observe(std::string(key) + "." + std::to_string(i));
            ImGui::PopID();
          }
        ImGui::EndCombo();
      }
      s.observe(key);
    };
    ImGui::TextWrapped("Choose source metadata and a current annotation "
                       "independently. Names do not select a pair.");
    choose("Source member", "source", s.member, s.catalog->entries(),
           [](const ImageJRoiSourceSummary &v) {
             return std::to_string(v.index + 1) + ". " + v.name;
           });
    choose("Current annotation", "annotation", s.row, s.rows,
           [](const PlanarAnnotation &v) { return v.name; });
    if (s.captured_zip) {
      choose("Captured metadata template", "template", s.template_member,
             s.captured_zip->items,
             [](const ImageJRoiZipExportItem &item) { return item.name; });
    }
    const char *modes[]{"Choose explicitly", "Fresh standard metadata",
                        "Retain admitted source metadata",
                        "Retain captured projection metadata"};
    int mode = s.metadata + 1;
    if (ImGui::BeginCombo("Standard metadata", modes[mode])) {
      for (int i = 0;
           i <
           ((s.captured || (s.captured_zip && s.template_member >= 0)) ? 4 : 3);
           ++i) {
        if (ImGui::Selectable(modes[i], mode == i)) {
          s.metadata = i - 1;
          s.invalidate();
        }
        s.observe("metadata." + std::to_string(i));
      }
      ImGui::EndCombo();
    }
    s.observe("metadata");
    ImGui::BeginDisabled(s.metadata < 1);
    if (ImGui::Checkbox("Reset source geometry, counters and option flags",
                        &s.reset_geometry))
      s.invalidate();
    s.observe("reset_geometry");
    ImGui::EndDisabled();
    if (ImGui::Checkbox(
            "Use native plane coordinate as file Z; clear legacy position",
            &s.confirmed))
      s.invalidate();
    s.observe("confirm");
    if (ImGui::Checkbox("Override group", &s.group_override))
      s.invalidate();
    s.observe("group_override");
    ImGui::SameLine();
    ImGui::BeginDisabled(!s.group_override);
    ImGui::SetNextItemWidth(100);
    const auto gid = ImGui::GetID("Group (0-255)");
    if (ImGui::InputInt("Group (0-255)", &s.group, 0, 0))
      s.invalidate();
    s.observe("group", gid, 100);
    ImGui::EndDisabled();
    if (ImGui::Checkbox("Override standard properties", &s.properties_override))
      s.invalidate();
    s.observe("properties_override");
    ImGui::BeginDisabled(!s.properties_override);
    if (ImGui::InputTextMultiline("Properties", &s.properties, {500, 65}))
      s.invalidate();
    s.observe("properties", 0, 500);
    ImGui::EndDisabled();
    if (ImGui::Button("Validate projection and review"))
      try {
        s.invalidate();
        if (s.member < 0 || s.row < 0)
          throw std::invalid_argument(
              "Choose both a source member and a current annotation");
        if (!s.confirmed)
          throw std::invalid_argument(
              "Confirm the file position interpretation");
        if (!s.original.empty() && !s.destination_confirmed)
          throw std::invalid_argument(
              "Confirm the reviewed pairing on this image");
        const auto catalog = *s.catalog;
        const auto member = s.member, row = s.row, metadata_mode = s.metadata,
                   group = s.group;
        const auto group_override = s.group_override,
                   properties_override = s.properties_override;
        const auto properties = s.properties;
        const auto reset_geometry = s.reset_geometry;
        const auto original = s.zip_mode ? MetadataObject{} : s.original;
        const auto captured =
            s.captured
                ? std::optional<ImageJRoi>(s.captured->standard_projection)
            : (s.captured_zip && s.template_member >= 0)
                ? std::optional<ImageJRoi>(
                      s.captured_zip->items.at(std::size_t(s.template_member))
                          .standard_projection)
                : std::optional<ImageJRoi>{};
        const auto limits =
            s.captured ? s.captured->limits : ImageJRoiExportLimits{};
        s.start(
            2,
            [catalog, member, row, metadata_mode, group, group_override,
             properties_override, properties, reset_geometry, cap, original,
             captured, limits](const JobContext &) {
              auto pair = resolve_imagej_roi_pairing(
                  catalog, std::size_t(member), std::size_t(row));
              auto standard = projection(
                  pair, metadata_mode, reset_geometry, group_override, group,
                  properties_override, properties, captured, limits.roi);
              auto command =
                  original.empty()
                      ? resolve_imagej_roi_export(catalog.source(), pair,
                                                  standard, limits)
                      : rebind_imagej_roi_export(catalog.source(), original,
                                                 pair, standard, limits);
              auto preview =
                  std::make_shared<const RoiImportPreview>(pair, cap);
              Result result;
              result.review = std::make_shared<const Review>(
                  Review{std::move(command), std::move(preview)});
              return result;
            },
            std::max<std::size_t>(64 * 1048576, cap));
      } catch (const std::exception &e) {
        s.error = export_error(e);
      }
    s.observe("validate");
  }
  if (s.review && (!s.zip_mode || s.compose)) {
    const auto &row = s.review->command.pairing().mapped().annotation();
    ImGui::Text("Ready: %s | %s | %zu bytes", row.name.c_str(),
                row.selection.kind.c_str(), s.review->command.data().size());
    ImGui::Text("Standard group: %u; native plane: %s, slice %d",
                unsigned(s.review->command.standard_projection().group),
                std::array<const char *, 3>{"XY", "XZ", "YZ"}.at(
                    std::size_t(row.plane)),
                row.coordinate + 1);
    ImGui::Checkbox("Show paired annotation", &s.preview_enabled);
    s.observe("preview_enabled");
    if (ImGui::Button("Go to annotation plane"))
      go_to(row);
    s.observe("go_to");
    if (ImGui::Button(s.image_review ? "Edit export choices"
                                     : "Review on image")) {
      s.image_review = !s.image_review;
      s.focus = true;
    }
    s.observe("image_review");
  }
  if (s.zip_mode) {
    if (!s.image_review && s.compose) {
      if (ImGui::InputText("ZIP member name (.roi)", &s.item_name))
        s.error.clear();
      s.observe("member_name", 0, 300);
      ImGui::BeginDisabled(!s.review);
      if (ImGui::Button("Add reviewed member"))
        try {
          s.admit_queue_item();
          s.queue.push_back({s.item_name, std::size_t(s.member),
                             std::size_t(s.row),
                             s.review->command.standard_projection()});
          s.invalidate();
          s.clear_choices();
          s.compose = false;
        } catch (const std::exception &e) {
          s.error = export_error(e);
        }
      s.observe("add_member");
      ImGui::EndDisabled();
    }
    if (!s.image_review && !s.compose) {
      ImGui::Text("Queued members: %zu", s.queue.size());
      ImGui::BeginChild("ZIP members", {0, 100}, true);
      ImGuiListClipper clip;
      clip.Begin(int(s.queue.size()));
      int remove = -1;
      while (clip.Step())
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
          ImGui::PushID(i);
          if (ImGui::SmallButton("Remove"))
            remove = i;
          s.observe("remove." + std::to_string(i));
          ImGui::SameLine();
          const auto &item = s.queue[std::size_t(i)];
          ImGui::Text("%d. %s | source %zu, annotation %zu", i + 1,
                      item.name.c_str(), item.source_index + 1,
                      item.annotation_index + 1);
          ImGui::PopID();
        }
      ImGui::EndChild();
      if (remove >= 0) {
        s.queue.erase(s.queue.begin() + remove);
        s.invalidate();
      }
      if (ImGui::Checkbox("Compress ZIP members", &s.compress))
        s.invalidate();
      s.observe("compress");
      if (ImGui::Button("Validate complete ZIP and review"))
        try {
          s.invalidate();
          if (!s.original.empty() && !s.destination_confirmed)
            throw std::invalid_argument(
                "Confirm the reviewed pairing on this image");
          const auto source = *s.source;
          const auto items = s.queue;
          const auto limits = s.captured_zip ? s.captured_zip->limits
                                             : ImageJRoiZipExportLimits{};
          const auto compress = s.compress;
          // Captured originals were fully validated by inspect_inputs. This
          // new explicit list resolves once against the same frozen owner.
          s.start(
              4,
              [source, items, limits, compress, cap](const JobContext &) {
                auto command = resolve_imagej_roi_zip_export(source, items,
                                                             limits, compress);
                auto preview =
                    std::make_shared<const RoiImportPreview>(command, cap);
                Result result;
                result.zip_review = std::make_shared<const ZipReview>(
                    ZipReview{std::move(command), std::move(preview)});
                return result;
              },
              std::max<std::size_t>(192 * 1048576, cap));
        } catch (const std::exception &e) {
          s.error = export_error(e);
        }
      s.observe("validate_zip");
    }
    if (s.zip_review) {
      ImGui::Text("Ready ZIP: %zu members, %zu bytes",
                  s.zip_review->command.members().size(),
                  s.zip_review->command.data().size());
      if (ImGui::BeginCombo("Go to member", "Choose member plane")) {
        ImGuiListClipper clip;
        clip.Begin(int(s.zip_review->command.members().size()));
        while (clip.Step())
          for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
            ImGui::PushID(i);
            const auto &member =
                s.zip_review->command.members()[std::size_t(i)];
            if (ImGui::Selectable("##member"))
              go_to(member.mapped.annotation());
            s.observe("zip_go_to." + std::to_string(i));
            ImGui::GetWindowDrawList()->AddText(
                ImGui::GetItemRectMin(), ImGui::GetColorU32(ImGuiCol_Text),
                member.name.c_str());
            ImGui::PopID();
          }
        ImGui::EndCombo();
      }
      s.observe("zip_go_to");
      ImGui::Checkbox("Show ZIP annotations", &s.preview_enabled);
      s.observe("zip_preview");
      if (ImGui::Button(s.image_review ? "Edit ZIP choices"
                                       : "Review ZIP on image")) {
        s.image_review = !s.image_review;
        s.focus = true;
      }
      s.observe("zip_image_review");
    }
  }
  if (!s.zip_mode || !s.compose || s.image_review) {
    ImGui::SetNextItemWidth(s.image_review ? 260.f : 450.f);
    if (ImGui::InputText(s.zip_mode ? "Output .zip" : "Output .roi", &s.path)) {
      s.overwrite = false;
      ++s.generation;
      s.message.clear();
    }
    s.observe("path", 0, s.image_review ? 260.f : 450.f);
    if (!s.image_review)
      ImGui::SameLine();
    if (ImGui::Button("Browse..."))
      browse();
    s.observe("browse");
    ImGui::Checkbox("Replace an existing file", &s.overwrite);
    s.observe("overwrite");
    ImGui::BeginDisabled(s.zip_mode ? !s.zip_review : !s.review);
    if (ImGui::Button("Export reviewed file"))
      try {
        const auto target = destination(s.path, s.zip_mode);
        const auto review = s.review;
        const auto zip_review = s.zip_review;
        const bool overwrite = s.overwrite;
        s.error.clear();
        s.message.clear();
        s.start(
            3,
            [review, zip_review, target, overwrite](const JobContext &context) {
              if (!overwrite && std::filesystem::exists(target))
                throw std::invalid_argument(
                    "File exists; choose another name or confirm replacement");
              Result result;
              const auto &bytes = zip_review ? zip_review->command.data()
                                             : review->command.data();
              result.staged =
                  detail::stage_atomic_file(target, bytes.data(), bytes.size(),
                                            context.execution_control());
              return result;
            },
            64 * 1048576);
      } catch (const std::exception &e) {
        s.error = export_error(e);
      }
    s.observe("export");
    ImGui::EndDisabled();
  }
  ImGui::EndDisabled();
  if (s.working())
    ImGui::TextUnformatted(s.phase == 4   ? "Preparing complete ZIP..."
                           : s.phase == 1 ? "Inspecting sources..."
                           : s.phase == 2 ? "Preparing projection..."
                                          : "Writing export...");
  const bool close = ImGui::Button("Close");
  s.observe("close");
  if (!s.error.empty())
    ImGui::TextWrapped("%s", s.error.c_str());
  if (!s.message.empty())
    ImGui::TextWrapped("%s", s.message.c_str());
  const bool escape =
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
      ImGui::IsKeyPressed(ImGuiKey_Escape);
  ImGui::End();
  if (!s.open || close || escape)
    cancel("canceled");
}
Json RoiExportPanel::diagnostics() const {
  const auto &s = *state_;
  if(s.mixed_mode)return s.mixed->diagnostics();
  Json sources = Json::array();
  if (s.catalog)
    for (const auto &v : s.catalog->entries())
      sources.push_back(
          {{"index", v.index}, {"name", v.name}, {"bytes", v.data_bytes}});
  return {{"visible", s.open},
          {"worker_active", s.working()},
          {"phase", s.phase},
          {"job_id", s.job.valid() ? s.job.info().id : 0},
          {"ready", s.zip_mode ? bool(s.zip_review) : bool(s.review)},
          {"member_ready", bool(s.review)},
          {"zip_mode", s.zip_mode},
          {"queue_count", s.queue.size()},
          {"compress", s.compress},
          {"member_name", s.item_name},
          {"compose", s.compose},
          {"workflow_review", !s.original.empty()},
          {"destination_confirmed", s.destination_confirmed},
          {"source_index", s.member},
          {"annotation_index", s.row},
          {"metadata_choice", s.metadata},
          {"confirmed", s.confirmed},
          {"sources", sources},
          {"annotation_count", s.rows.size()},
          {"path", s.path},
          {"overwrite", s.overwrite},
          {"image_review", s.image_review},
          {"output_bytes", s.zip_review ? s.zip_review->command.data().size()
                           : s.review   ? s.review->command.data().size()
                                        : 0},
          {"projection_group",
           s.review ? Json(s.review->command.standard_projection().group)
                    : Json(nullptr)},
          {"error", s.error},
          {"message", s.message},
          {"cancel_reason", s.reason},
          {"controls", s.controls}};
}
} // namespace imagejxx::desktop
