#include "roi_mixed_export_panel.hpp"
#include "../native/atomic_file.hpp"
#include "../native/imagej_annotation_export_internal.hpp"
#include "../native/imagej_roi_zip_internal.hpp"
#include "roi_export_common.hpp"
#include "roi_import_preview.hpp"
#include <algorithm>
#include <imagejxx/imagej_roi_zip_export.hpp>
#include <imagejxx/native_workflow.hpp>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <mutex>
#include <set>
namespace imagejxx::desktop {
namespace {
using namespace roi_export_detail;
struct Inputs {
  ImageJAnnotationSourceCatalog catalog;
  std::vector<PlanarAnnotation> rows;
  std::vector<PlanarDisplayAnnotation> displays;
  std::optional<ImageJAnnotationExportRequest> captured;
  ImageJAnnotationExportLimits limits;
};
struct Review {
  ResolvedImageJAnnotationExport command;
  std::shared_ptr<const RoiImportPreview> native, standard;
};
using Item = std::shared_ptr<const ImageJAnnotationExportItem>;
struct Result {
  std::shared_ptr<const Inputs> inputs;
  std::shared_ptr<const Review> review;
  Item item;
  std::unique_ptr<detail::StagedAtomicFile> staged;
};
ImageJAnnotationExportLimits expanded_limits(ImageJAnnotationExportLimits l) {
  l.sources.batch.max_total_bytes = 64 * 1048576;
  l.sources.batch.max_provenance_bytes = 64 * 1048576;
  l.sources.batch.max_decoded_bytes = 128 * 1048576;
  l.sources.max_display_retained_bytes = 256 * 1048576;
  l.output.roi.max_bytes = l.output.roi.max_text_bytes = 64 * 1048576;
  l.output.max_record_bytes = 128 * 1048576;
  l.max_display_retained_bytes = 256 * 1048576;
  l.zip.max_entry_bytes = 64 * 1048576;
  return l;
}
std::optional<ImageJAnnotationExportRequest>
captured_request(const MetadataObject &r) {
  if (r.empty())
    return {};
  const auto &schema = std::get<std::string>(r.at("schema"));
  if (schema == "imagejxx.imagej-annotation-export")
    return decode_imagej_annotation_export_request(r);
  ImageJAnnotationExportRequest result;
  result.is_zip = false;
  result.compress = false;
  if (schema == "imagejxx.imagej-roi-zip-export") {
    auto old = decode_imagej_roi_zip_export_request(r);
    result.is_zip = true;
    result.compress = old.compress;
    result.limits.sources.batch = old.limits.sources;
    result.limits.output = old.limits.output;
    result.limits.zip = old.limits.zip;
    for (auto &i : old.items)
      result.items.push_back({i.name,
                              i.source_index,
                              ImageJAnnotationKind::Selection,
                              i.annotation_index,
                              std::move(i.standard_projection),
                              {}});
  } else if (schema == "imagejxx.imagej-roi-export") {
    auto old = decode_imagej_roi_export_request(r);
    result.limits.output = old.limits;
    const auto &pair = old.pairing_record;
    auto index = [](const MetadataValue &v) {
      return std::visit(
          [](const auto &n) -> std::size_t {
            if constexpr (std::is_same_v<std::decay_t<decltype(n)>,
                                         std::uint64_t> ||
                          std::is_same_v<std::decay_t<decltype(n)>,
                                         std::int64_t>)
              return std::size_t(n);
            else
              throw std::invalid_argument("Invalid captured index");
          },
          v);
    };
    result.items.push_back({"captured.roi",
                            index(pair.at("source_index")),
                            ImageJAnnotationKind::Selection,
                            index(pair.at("annotation_index")),
                            std::move(old.standard_projection),
                            {}});
  } else
    throw std::invalid_argument("Unsupported captured export schema");
  return result;
}
Result inspect(const DocumentSnapshot &source, const MetadataObject &original,
               bool expanded, const NativeWorkflowExport *intent = nullptr) {
  // Full original admission precedes current catalog/row/limit choices.
  auto captured = captured_request(original);
  if (intent)
    captured = ImageJAnnotationExportRequest{intent->items, intent->limits,
                                             intent->zip,
                                             intent->zip && intent->compress};
  auto limits = captured ? captured->limits : ImageJAnnotationExportLimits{};
  if (expanded)
    limits = expanded_limits(limits);
  auto catalog = inspect_imagej_annotation_catalog(source, limits.sources);
  Result result;
  result.inputs = std::make_shared<const Inputs>(Inputs{
      std::move(catalog), planar_annotations(source.data()),
      planar_display_annotations(source.data()), std::move(captured), limits});
  return result;
}
std::shared_ptr<const Review> review(ResolvedImageJAnnotationExport command,
                                     std::size_t cap) {
  auto native = std::make_shared<const RoiImportPreview>(command, cap, false);
  auto standard = std::make_shared<const RoiImportPreview>(command, cap, true);
  return std::make_shared<const Review>(
      Review{std::move(command), std::move(native), std::move(standard)});
}
ImageJAnnotationProjection source_projection(const MappedImageJAnnotation &m) {
  if (auto g = std::get_if<MappedImageJRoi>(&m))
    return g->source_record();
  return std::visit(
      [](const auto &v) -> ImageJAnnotationProjection { return v; },
      std::get<MappedImageJDisplayRoi>(m).source_record());
}
std::vector<ImageJAnnotationExportItem>
own_queue(const std::vector<Item> &refs,
          const ImageJAnnotationExportLimits &limits) {
  if (refs.empty() || refs.size() > limits.zip.max_entries)
    throw std::invalid_argument("ZIP member count limit");
  std::size_t bytes = 0, nodes = 0;
  std::set<std::string> names;
  for (const auto &i : refs) {
    if (!names.insert(detail::imagej_roi_zip_member_key(i->name, limits.zip))
             .second)
      throw std::invalid_argument("Duplicate ZIP member name");
    detail::preflight_imagej_annotation_projection(i->standard_projection,
                                                   limits, bytes, nodes);
    const auto extra =
        2 * sizeof(ImageJAnnotationExportItem) + 4 * i->name.size();
    if (bytes > limits.zip.max_working_bytes ||
        extra > limits.zip.max_working_bytes - bytes)
      throw std::invalid_argument("ZIP queue retention limit");
    bytes += extra;
  }
  std::vector<ImageJAnnotationExportItem> out;
  out.reserve(refs.size());
  for (const auto &i : refs)
    out.push_back(*i);
  return out;
}
} // namespace
bool needs_mixed_roi_export(const DocumentSnapshot &source,
                            const MetadataObject &original) {
  if (!original.empty() && std::get<std::string>(original.at("schema")) ==
                               "imagejxx.imagej-annotation-export")
    return true;
  const auto &ext = source.data().metadata.extensions;
  if (ext.count(planar_display_annotations_extension))
    return true;
  const auto found = ext.find(imagej_roi_import_sources_extension);
  if (found == ext.end())
    return true;
  const auto &archive = std::get<MetadataObject>(
      std::get<std::shared_ptr<const MetadataTree>>(found->second)->value);
  const auto &version = archive.at("version");
  const bool v3 = std::visit(
      [](const auto &v) {
        if constexpr (std::is_integral_v<std::decay_t<decltype(v)>>)
          return v == 3 || v == 4;
        else
          return false;
      },
      version);
  return v3 ||
         std::get<MetadataArray>(std::get<std::shared_ptr<const MetadataTree>>(
                                     archive.at("entries"))
                                     ->value)
             .empty();
}
struct RoiMixedExportPanel::State {
  explicit State(std::shared_ptr<JobScheduler> value)
      : scheduler(std::move(value)) {}
  struct Cell {
    std::mutex mutex;
    std::optional<Result> result;
  };
  std::shared_ptr<JobScheduler> scheduler;
  Job job;
  std::shared_ptr<Cell> cell;
  std::optional<DocumentSnapshot> source;
  MetadataObject original;
  std::shared_ptr<const Inputs> inputs;
  std::shared_ptr<const Review> member_review, zip_review;
  std::unique_ptr<detail::StagedAtomicFile> staged;
  std::vector<Item> queue;
  std::uint64_t generation = 0, worker_generation = 0;
  bool open = false, focus = false, harvested = true, zip = false,
       compose = true, compress = true, expanded = false;
  bool confirmed = false, destination_confirmed = false, reset = false,
       regenerate = false, round_font = false, drop_alpha = false,
       unscale = false, round_origin = false, zero_color = false;
  bool group_override = false, properties_override = false, overwrite = false,
       preview_enabled = true, standard = false, image_review = false,
       draft = false;
  int source_index = -1, kind = -1, row = -1, metadata = -1,
      template_member = -1, group = 0, phase = 0;
  std::string name, path, properties, error, message, reason;
  std::shared_ptr<const NativeWorkflowExport> typed;
  bool reviewing() const { return typed || !original.empty(); }
  Json controls = Json::object(), window = Json::object();
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
    member_review.reset();
    zip_review.reset();
    staged.reset();
    image_review = false;
    error.clear();
    message.clear();
    ++generation;
  }
  void choices() {
    source_index = kind = row = metadata = template_member = -1;
    confirmed = reset = regenerate = round_font = drop_alpha = unscale =
        round_origin = zero_color = false;
    group_override = properties_override = false;
    group = 0;
    properties.clear();
    name.clear();
  }
  void changed_pair() {
    invalidate();
    metadata = template_member = -1;
    confirmed = reset = regenerate = round_font = drop_alpha = unscale =
        round_origin = zero_color = false;
  }
  void observe(const std::string &key, float width = 0) {
    const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    controls[key] = {
        {"id", ImGui::GetItemID()},
        {"visible", ImGui::IsItemVisible()},
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
      if ((member_review || zip_review || staged) &&
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
      auto transfer = [&] {
        if (finished == 1) {
          inputs = std::move(result.inputs);
          if (typed) {
            queue.clear();
            if (zip) {
              for (const auto &item : typed->items)
                queue.emplace_back(typed, &item);
              compose = false;
            }
            const auto &item = typed->items.at(0);
            source_index =
                item.fresh_source ? -2
                : item.source_index < inputs->catalog.entries().size()
                    ? int(item.source_index)
                    : -1;
            kind = int(item.annotation_kind);
            const auto count =
                kind == 0 ? inputs->rows.size() : inputs->displays.size();
            row =
                item.annotation_index < count ? int(item.annotation_index) : -1;
            template_member = 0;
            metadata = 2;
            name = item.name;
            round_font = item.options.round_font_size;
            drop_alpha = item.options.drop_image_alpha;
            unscale = item.options.reset_image_scale;
            round_origin = item.options.round_image_origin;
            zero_color = item.options.text_default_color ==
                         std::optional<std::uint32_t>{0};
          }
        } else if (finished == 2)
          member_review = std::move(result.review);
        else if (finished == 4)
          zip_review = std::move(result.review);
        else if (finished == 5) {
          queue.push_back(std::move(result.item));
          member_review.reset();
          zip_review.reset();
          choices();
          compose = false;
          ++generation;
        } else
          staged = std::move(result.staged);
      };
      if (finished == 1 || finished == 5)
        job.complete(transfer);
      else
        transfer();
    } catch (const std::exception &e) {
      if (open && worker_generation == generation)
        error = export_error(e);
    }
    cell.reset();
  }
  template <class Work> void start(int next, Work work, std::size_t bytes) {
    if (working())
      throw std::invalid_argument("Wait for the previous ROI export worker");
    auto output = std::make_shared<Cell>();
    JobOptions options;
    options.label = next == 1   ? "Inspect mixed ROI export"
                    : next == 3 ? "Stage ROI export file"
                    : next == 5 ? "Admit ZIP queue member"
                                : "Prepare mixed ROI export";
    options.document_id = source->id();
    options.supersedes = "imagejxx.roi-export";
    options.resource_bytes = bytes;
    job = scheduler->submit(
        [output, work = std::move(work)](const JobContext &context) mutable {
          context.checkpoint();
          auto value = work(context);
          context.checkpoint();
          std::lock_guard<std::mutex> lock(output->mutex);
          output->result = std::move(value);
        },
        options);
    cell = output;
    harvested = false;
    worker_generation = generation;
    phase = next;
  }
};
RoiMixedExportPanel::RoiMixedExportPanel(
    std::shared_ptr<JobScheduler> scheduler)
    : state_(std::make_unique<State>(std::move(scheduler))) {}
RoiMixedExportPanel::~RoiMixedExportPanel() = default;
void RoiMixedExportPanel::poll() { state_->poll(); }
bool RoiMixedExportPanel::working() const { return state_->working(); }
std::optional<JobInfo> RoiMixedExportPanel::job_info() const {
  return state_->working() ? std::optional<JobInfo>(state_->job.info())
                           : std::nullopt;
}
bool RoiMixedExportPanel::visible() const { return state_->open; }
bool RoiMixedExportPanel::zip_mode() const { return state_->zip; }
void RoiMixedExportPanel::wait() const {
  if (state_->job.valid())
    state_->job.wait();
}
bool RoiMixedExportPanel::wait_for(std::chrono::milliseconds t) const {
  return !state_->job.valid() ||
         state_->job.wait_for(std::max(t, std::chrono::milliseconds(0)));
}
void RoiMixedExportPanel::cancel(const char *reason) {
  auto &s = *state_;
  s.invalidate();
  s.open = false;
  s.source.reset();
  s.inputs.reset();
  s.original.clear();
  s.typed.reset();
  s.queue.clear();
  s.destination_confirmed = false;
  s.controls = Json::object();
  s.reason = reason;
}
void RoiMixedExportPanel::show(const DocumentSnapshot &source,
                               MetadataObject original, bool zip) {
  show_impl(source, std::move(original), zip, {});
}
void RoiMixedExportPanel::show_impl(
    const DocumentSnapshot &source, MetadataObject original, bool zip,
    std::shared_ptr<const NativeWorkflowExport> intent) {
  auto &s = *state_;
  s.poll();
  if (s.working() && s.job.info().state == JobState::Queued) {
    s.job.cancel();
    s.poll();
  }
  if (s.working())
    throw std::invalid_argument("Wait for the previous ROI export worker");
  cancel("reopened");
  s.source = source;
  s.original = std::move(original);
  s.typed = std::move(intent);
  s.zip = zip;
  s.compose = true;
  s.compress = s.typed ? s.typed->compress : true;
  s.draft = false;
  s.expanded = false;
  s.choices();
  s.overwrite = s.standard = s.image_review = false;
  s.preview_enabled = true;
  s.path.clear();
  s.reason.clear();
  s.open = s.focus = true;
  s.start(
      1,
      [source, original = s.original, intent = s.typed](const JobContext &) {
        return inspect(source, original, false, intent.get());
      },
      64 * 1048576);
}
void RoiMixedExportPanel::show_draft(const DocumentSnapshot &source,
                                     MetadataObject original, bool zip) {
  show(source, std::move(original), zip);
  state_->draft = true;
}
void RoiMixedExportPanel::show_draft(
    const DocumentSnapshot &source,
    std::shared_ptr<const NativeWorkflowExport> intent) {
  if (!intent)
    throw std::invalid_argument("Missing workflow export intent");
  auto original = intent->original.value_or(MetadataObject{});
  const auto zip = intent->zip;
  show_impl(source, std::move(original), zip, std::move(intent));
  state_->draft = true;
}
RoiImportBrowseRequest RoiMixedExportPanel::browse_request() const {
  const auto &s = *state_;
  if (!s.open || !s.source || s.working())
    throw std::invalid_argument("Browse requires an idle ROI export");
  return {*s.source, s.generation, s.path};
}
bool RoiMixedExportPanel::browse_current(
    const RoiImportBrowseRequest &r,
    const std::optional<DocumentSnapshot> &current) const {
  const auto &s = *state_;
  return s.open && s.source && current && !s.working() &&
         s.generation == r.generation && same_source(r.source, *current) &&
         same_source(*s.source, *current);
}
void RoiMixedExportPanel::path(const RoiImportBrowseRequest &r,
                               const std::optional<DocumentSnapshot> &current,
                               std::string path) {
  if (!browse_current(r, current))
    throw std::invalid_argument("ROI export file choice expired");
  state_->path = std::move(path);
  state_->overwrite = false;
  ++state_->generation;
}
std::shared_ptr<const RoiImportPreview>
RoiMixedExportPanel::preview(const DocumentSnapshot &current,
                             std::size_t cap) const {
  const auto &s = *state_;
  auto r = s.zip_review ? s.zip_review : s.member_review;
  auto p = r ? (s.standard ? r->standard : r->native) : nullptr;
  return s.open && s.preview_enabled && p && p->matches(current, cap) ? p
                                                                      : nullptr;
}
void RoiMixedExportPanel::preview_error(const std::string &error) {
  state_->invalidate();
  state_->error = "Export preview failed: " + error;
}
void RoiMixedExportPanel::draw(
    const std::optional<DocumentSnapshot> &current, bool busy, std::size_t cap,
    const std::function<void()> &browse,
    const std::function<void(const PlanarAnnotation &)> &go_to,
    const std::function<void(const PlanarDisplayAnnotation &)> &go_to_display,
    const std::function<void(const ResolvedImageJAnnotationExport &,
                             const std::function<void()> &)> &publish,
    const std::function<void(const ResolvedImageJAnnotationExport &)>
        &accept_choices) {
  auto &s = *state_;
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
  if ((s.member_review && s.member_review->native->cap() != cap) ||
      (s.zip_review && s.zip_review->native->cap() != cap)) {
    s.invalidate();
    s.error = "Processing memory setting changed; validate again";
  }
  if (s.staged) {
    try {
      auto success = "Exported " + s.path;
      s.job.complete([&] {
        if (!same_source(*s.source, *current))
          throw RevisionConflict();
        auto r = s.zip ? s.zip_review : s.member_review;
        publish(r->command, [&] { s.staged->publish(s.overwrite); });
      });
      s.staged.reset();
      s.message = std::move(success);
    } catch (const std::exception &e) {
      s.staged.reset();
      s.error = export_error(e);
    }
  }
  if (s.focus)
    ImGui::SetNextWindowFocus();
  s.focus = false;
  const auto area = ImGui::GetMainViewport()->WorkSize;
  ImGui::SetNextWindowSize(s.image_review
                               ? ImVec2{430, 360}
                               : ImVec2{650, std::min(680.f, area.y - 40)},
                           ImGuiCond_Always);
  ImGui::SetNextWindowPos(
      {std::max(10.f, area.x - (s.image_review ? 450.f : 670.f)), 25},
      ImGuiCond_FirstUseEver);
  ImGui::Begin(s.draft          ? "Edit workflow export choices"
               : s.image_review ? "Mixed ROI export preview"
               : s.zip          ? "Export ImageJ mixed ROI ZIP"
                                : "Export ImageJ annotation",
               &s.open);
  const auto wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
  s.window = {{"rect", {wp.x, wp.y, wp.x + ws.x, wp.y + ws.y}}};
  ImGui::Text("Image: %s", s.source->data().metadata.title.c_str());
  ImGui::BeginDisabled(s.working());
  ImGui::BeginChild("Export choices",
                    {0, std::max(80.f, ImGui::GetContentRegionAvail().y - 190)},
                    false);
  const auto choice_pos = ImGui::GetWindowPos(),
             choice_size = ImGui::GetWindowSize();
  s.window["choices"] = {choice_pos.x, choice_pos.y,
                         choice_pos.x + choice_size.x,
                         choice_pos.y + choice_size.y};
  auto checkbox = [&](const char *label, const char *key, bool &value) {
    if (ImGui::Checkbox(label, &value))
      s.invalidate();
    s.observe(key);
  };
  if (!s.image_review) {
    if (ImGui::Checkbox(
            "Allow larger export inputs (up to 128 MiB decoded sources)",
            &s.expanded)) {
      s.invalidate();
      s.inputs.reset();
      s.queue.clear();
      s.choices();
      const auto source = *s.source;
      const auto original = s.original;
      const auto expanded = s.expanded;
      const auto intent = s.typed;
      try {
        s.start(
            1,
            [source, original, expanded, intent](const JobContext &) {
              return inspect(source, original, expanded, intent.get());
            },
            192 * 1048576);
      } catch (const std::exception &e) {
        s.error = export_error(e);
      }
    }
    s.observe("expanded");
    if (!s.inputs && !s.working()) {
      if (ImGui::Button("Retry inspected inputs")) {
        try {
          s.invalidate();
          const auto source = *s.source;
          const auto original = s.original;
          const auto expanded = s.expanded;
          const auto intent = s.typed;
          s.start(
              1,
              [source, original, expanded, intent](const JobContext &) {
                return inspect(source, original, expanded, intent.get());
              },
              192 * 1048576);
        } catch (const std::exception &e) {
          s.error = export_error(e);
        }
      }
      s.observe("retry_inputs");
    }
    if (s.inputs) {
      if (s.reviewing()) {
        ImGui::TextWrapped(s.draft
                               ? "Choose the current rows and projection "
                                 "metadata for this workflow step."
                               : "The captured export stays unchanged. Choose "
                                 "this image's rows and a new output file.");
        checkbox("Use reviewed choices on this image", "confirm_destination",
                 s.destination_confirmed);
      }
      if (s.zip) {
        if (ImGui::Checkbox("Edit a member", &s.compose))
          s.invalidate();
        s.observe("compose");
      }
      if (!s.zip || s.compose) {
        auto choose = [&](const char *label, const char *key, int &chosen,
                          int count, auto name, auto changed) {
          const auto selected =
              chosen >= 0 ? name(chosen) : std::string("Choose explicitly");
          if (ImGui::BeginCombo(label, selected.c_str())) {
            ImGuiListClipper clip;
            clip.Begin(count);
            while (clip.Step())
              for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                ImGui::PushID(i);
                if (ImGui::Selectable("##choice", chosen == i)) {
                  chosen = i;
                  changed();
                }
                s.observe(std::string(key) + "." + std::to_string(i));
                const auto text = name(i);
                ImGui::GetWindowDrawList()->AddText(
                    ImGui::GetItemRectMin(), ImGui::GetColorU32(ImGuiCol_Text),
                    text.c_str());
                ImGui::PopID();
              }
            ImGui::EndCombo();
          }
          s.observe(key);
        };
        int source_choice = s.source_index == -2  ? 0
                            : s.source_index >= 0 ? s.source_index + 1
                                                  : -1;
        choose(
            "Source basis", "source", source_choice,
            int(s.inputs->catalog.entries().size() + 1),
            [&](int i) {
              return i == 0 ? std::string(
                                  "Generate from chosen standard projection")
                            : std::to_string(i) + ". " +
                                  s.inputs->catalog.entries()[i - 1].name;
            },
            [&] {
              s.source_index = source_choice == 0 ? -2 : source_choice - 1;
              s.changed_pair();
            });
        choose(
            "Annotation category", "kind", s.kind, 2,
            [](int i) {
              return i == 0 ? std::string("Selection geometry")
                            : std::string("Text / image display");
            },
            [&] {
              s.row = -1;
              s.changed_pair();
            });
        if (s.kind >= 0)
          choose(
              "Current annotation", "annotation", s.row,
              int(s.kind == 0 ? s.inputs->rows.size()
                              : s.inputs->displays.size()),
              [&](int i) {
                return s.kind == 0 ? s.inputs->rows[i].name
                                   : s.inputs->displays[i].name;
              },
              [&] { s.changed_pair(); });
        if (s.inputs->captured)
          choose(
              "Captured metadata template", "template", s.template_member,
              int(s.inputs->captured->items.size()),
              [&](int i) { return s.inputs->captured->items[i].name; },
              [&] {
                s.invalidate();
                s.metadata = -1;
                s.confirmed = s.reset = s.regenerate = s.round_font =
                    s.drop_alpha = s.unscale = s.round_origin = s.zero_color =
                        false;
              });
        const char *modes[]{"Fresh standard metadata",
                            "Retain admitted source metadata",
                            "Retain captured projection metadata"};
        if (ImGui::BeginCombo("Standard metadata", s.metadata >= 0
                                                       ? modes[s.metadata]
                                                       : "Choose explicitly")) {
          for (int i = 0; i < 3; ++i) {
            ImGui::BeginDisabled(
                (i == 1 && s.source_index < 0) ||
                (i == 2 && (!s.inputs->captured || s.template_member < 0)));
            if (ImGui::Selectable(modes[i], s.metadata == i)) {
              s.metadata = i;
              s.invalidate();
              s.confirmed = s.reset = s.regenerate = s.round_font =
                  s.drop_alpha = s.unscale = s.round_origin = s.zero_color =
                      false;
            }
            s.observe("metadata." + std::to_string(i));
            ImGui::EndDisabled();
          }
          ImGui::EndCombo();
        }
        s.observe("metadata");
        checkbox("Reset retained representation and option flags",
                 "reset_geometry", s.reset);
        checkbox("Regenerate embedded image TIFF from current pixels",
                 "regenerate", s.regenerate);
        checkbox("Use native plane coordinate as file Z; clear legacy position",
                 "confirm", s.confirmed);
        checkbox("Round standard text font size; keep native precision",
                 "round_font", s.round_font);
        checkbox("Omit standard image alpha; keep native RGBA", "drop_alpha",
                 s.drop_alpha);
        checkbox("Use unscaled standard image bounds; keep native bounds",
                 "unscale", s.unscale);
        checkbox("Round standard image origin; keep native origin",
                 "round_origin", s.round_origin);
        checkbox(
            "Interpret unspecified standard text color as native zero ARGB",
            "zero_color", s.zero_color);
        checkbox("Override group", "group_override", s.group_override);
        ImGui::SameLine();
        ImGui::BeginDisabled(!s.group_override);
        ImGui::SetNextItemWidth(90);
        if (ImGui::InputInt("Group (0-255)", &s.group, 0, 0))
          s.invalidate();
        s.observe("group", 90);
        ImGui::EndDisabled();
        checkbox("Override standard properties", "properties_override",
                 s.properties_override);
        ImGui::BeginDisabled(!s.properties_override);
        if (ImGui::InputTextMultiline("Properties", &s.properties, {400, 55}))
          s.invalidate();
        s.observe("properties", 400);
        ImGui::EndDisabled();
        if (ImGui::Button("Validate projection and review"))
          try {
            s.invalidate();
            if (s.source_index == -1 || s.kind < 0 || s.row < 0 ||
                s.metadata < 0)
              throw std::invalid_argument(
                  "Choose source basis, category, current row and metadata "
                  "explicitly");
            if (!s.confirmed)
              throw std::invalid_argument(
                  "Confirm the file position interpretation");
            if (s.reviewing() && !s.destination_confirmed)
              throw std::invalid_argument(
                  "Confirm the reviewed choices on this image");
            const auto inputs = s.inputs;
            const auto source_index = s.source_index, kind = s.kind,
                       row = s.row, metadata = s.metadata,
                       templ = s.template_member, group = s.group;
            const auto reset = s.reset, regenerate = s.regenerate,
                       group_override = s.group_override,
                       properties_override = s.properties_override;
            const auto properties = s.properties;
            ImageJDisplayProjectionOptions options;
            options.round_font_size = s.round_font;
            options.drop_image_alpha = s.drop_alpha;
            options.reset_image_scale = s.unscale;
            options.round_image_origin = s.round_origin;
            if (s.zero_color)
              options.text_default_color = 0;
            s.start(
                2,
                [inputs, source_index, kind, row, metadata, templ, group, reset,
                 regenerate, group_override, properties_override, properties,
                 options, cap](const JobContext &) {
                  std::optional<ImageJAnnotationProjection> base;
                  if (metadata == 1)
                    base = source_projection(
                        resolve_imagej_annotation_pairing(
                            inputs->catalog, std::size_t(source_index),
                            kind == 0 ? ImageJAnnotationKind::Selection
                                      : ImageJAnnotationKind::Display,
                            std::size_t(row))
                            .mapped());
                  else if (metadata == 2)
                    base = inputs->captured->items.at(std::size_t(templ))
                               .standard_projection;
                  ImageJAnnotationProjection projection;
                  if (kind == 0) {
                    ImageJRoi original;
                    if (base) {
                      if (auto g = std::get_if<ImageJRoi>(&*base))
                        original = *g;
                      else {
                        if (!reset)
                          throw std::invalid_argument(
                              "Explicitly reset a different representation or "
                              "choose fresh metadata");
                        std::visit(
                            [&](const auto &v) {
                              original.group = v.group;
                              original.properties = v.properties;
                              original.overlay_label_color =
                                  v.overlay_label_color;
                              original.overlay_font_size = v.overlay_font_size;
                            },
                            *base);
                      }
                    }
                    projection = fill_geometric_projection(
                        std::move(original), inputs->rows.at(row), reset);
                  } else
                    projection = display_projection(inputs->displays.at(row),
                                                    std::move(base), reset,
                                                    regenerate, options);
                  std::visit(
                      [&](auto &p) {
                        if (group_override) {
                          if (group < 0 || group > 255)
                            throw std::invalid_argument(
                                "Group must be between 0 and 255");
                          p.group = static_cast<unsigned char>(group);
                        }
                        if (properties_override)
                          p.properties = properties;
                      },
                      projection);
                  ImageJAnnotationExportItem item{
                      "annotation.roi",
                      source_index == -2
                          ? std::numeric_limits<std::size_t>::max()
                          : std::size_t(source_index),
                      kind == 0 ? ImageJAnnotationKind::Selection
                                : ImageJAnnotationKind::Display,
                      std::size_t(row),
                      std::move(projection),
                      options,
                      source_index == -2};
                  Result result;
                  result.review = review(
                      resolve_imagej_annotation_export(inputs->catalog.source(),
                                                       item, inputs->limits),
                      cap);
                  return result;
                },
                std::max<std::size_t>(192 * 1048576, cap));
          } catch (const std::exception &e) {
            s.error = export_error(e);
          }
        s.observe("validate");
        if (s.zip) {
          ImGui::SetNextItemWidth(300);
          if (ImGui::InputText("ZIP member name (.roi)", &s.name))
            s.error.clear();
          s.observe("member_name", 300);
          ImGui::BeginDisabled(!s.member_review);
          if (ImGui::Button("Add reviewed member"))
            try {
              const auto reviewed = s.member_review;
              const auto refs = s.queue;
              const auto name = s.name;
              const auto limits = s.inputs->limits;
              s.start(
                  5,
                  [reviewed, refs, name, limits](const JobContext &) {
                    if (refs.size() >= limits.zip.max_entries)
                      throw std::invalid_argument("ZIP member count limit");
                    const auto key =
                        detail::imagej_roi_zip_member_key(name, limits.zip);
                    std::size_t bytes = 0, nodes = 0;
                    for (const auto &existing : refs) {
                      if (detail::imagej_roi_zip_member_key(existing->name,
                                                            limits.zip) == key)
                        throw std::invalid_argument(
                            "Duplicate ZIP member name");
                      detail::preflight_imagej_annotation_projection(
                          existing->standard_projection, limits, bytes, nodes);
                    }
                    detail::preflight_imagej_annotation_projection(
                        reviewed->command.members()
                            .front()
                            .request.standard_projection,
                        limits, bytes, nodes);
                    auto item = std::make_shared<ImageJAnnotationExportItem>(
                        reviewed->command.members().front().request);
                    item->name = name;
                    auto all = refs;
                    all.push_back(item);
                    (void)own_queue(all, limits);
                    Result result;
                    result.item = std::move(item);
                    return result;
                  },
                  192 * 1048576);
            } catch (const std::exception &e) {
              s.error = export_error(e);
            }
          s.observe("add_member");
          ImGui::EndDisabled();
        }
      }
      if (s.zip && !s.compose) {
        ImGui::Text("Queued members: %zu", s.queue.size());
        int remove = -1;
        ImGui::BeginChild("ZIP members", {0, 100}, true);
        ImGuiListClipper clip;
        clip.Begin(int(s.queue.size()));
        while (clip.Step())
          for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
            ImGui::PushID(i);
            if (ImGui::SmallButton("Remove"))
              remove = i;
            s.observe("remove." + std::to_string(i));
            ImGui::SameLine();
            ImGui::TextUnformatted(s.queue[i]->name.c_str());
            ImGui::PopID();
          }
        ImGui::EndChild();
        if (remove >= 0) {
          s.queue.erase(s.queue.begin() + remove);
          s.invalidate();
        }
        checkbox("Compress ZIP members", "compress", s.compress);
        if (ImGui::Button("Validate complete ZIP and review"))
          try {
            s.invalidate();
            if (s.reviewing() && !s.destination_confirmed)
              throw std::invalid_argument(
                  "Confirm the reviewed choices on this image");
            const auto inputs = s.inputs;
            const auto refs = s.queue;
            const auto compress = s.compress;
            s.start(
                4,
                [inputs, refs, compress, cap](const JobContext &) {
                  auto items = own_queue(refs, inputs->limits);
                  Result result;
                  result.review = review(resolve_imagej_annotation_zip_export(
                                             inputs->catalog.source(), items,
                                             inputs->limits, compress),
                                         cap);
                  return result;
                },
                std::max<std::size_t>(192 * 1048576, cap));
          } catch (const std::exception &e) {
            s.error = export_error(e);
          }
        s.observe("validate_zip");
      }
    }
  }
  auto active = s.zip_review ? s.zip_review : s.member_review;
  if (active) {
    ImGui::Text("Reviewed: %zu members, %zu bytes",
                active->command.members().size(),
                active->command.data().size());
    if (!s.draft) {
      ImGui::Checkbox("Show export annotations", &s.preview_enabled);
      s.observe("preview_enabled");
      ImGui::Checkbox("Show standard projection instead of exact native row",
                      &s.standard);
      s.observe("standard_preview");
      if (ImGui::BeginCombo("Go to member", "Choose member plane")) {
        for (std::size_t i = 0; i < active->command.members().size(); ++i) {
          ImGui::PushID(int(i));
          const auto &member = active->command.members()[i];
          if (ImGui::Selectable("##member")) {
            std::visit(
                [&](const auto &mapped) {
                  using T = std::decay_t<decltype(mapped)>;
                  if constexpr (std::is_same_v<T, MappedImageJRoi>)
                    go_to(mapped.annotation());
                  else
                    go_to_display(mapped.annotation());
                },
                member.mapped);
          }
          s.observe("go_to." + std::to_string(i));
          ImGui::GetWindowDrawList()->AddText(ImGui::GetItemRectMin(),
                                              ImGui::GetColorU32(ImGuiCol_Text),
                                              member.request.name.c_str());
          ImGui::PopID();
        }
        ImGui::EndCombo();
      }
      s.observe("go_to");
    }
  }
  ImGui::EndChild();
  if (active && !s.draft) {
    if (ImGui::Button(s.image_review ? "Edit export choices"
                                     : "Review on image")) {
      s.image_review = !s.image_review;
      s.focus = true;
    }
    s.observe("image_review");
  }
  if (s.draft) {
    ImGui::TextWrapped("Workflow choices only. Confirm the parent step and "
                       "output destination after returning.");
    ImGui::BeginDisabled(!active || !accept_choices);
    if (ImGui::Button("Keep workflow export choices"))
      try {
        s.job.complete([&] { accept_choices(active->command); });
        cancel("accepted");
      } catch (const std::exception &e) {
        s.error = export_error(e);
      }
    s.observe("keep_choices");
    ImGui::EndDisabled();
  } else {
    ImGui::SetNextItemWidth(s.image_review ? 250 : 440);
    if (ImGui::InputText(s.zip ? "Output .zip" : "Output .roi", &s.path)) {
      s.overwrite = false;
      ++s.generation;
      s.message.clear();
    }
    s.observe("path", s.image_review ? 250 : 440);
    if (ImGui::Button("Browse..."))
      browse();
    s.observe("browse");
    ImGui::SameLine();
    ImGui::Checkbox("Replace an existing file", &s.overwrite);
    s.observe("overwrite");
    ImGui::BeginDisabled(s.zip ? !s.zip_review : !s.member_review);
    if (ImGui::Button("Export reviewed file"))
      try {
        const auto target = destination(s.path, s.zip);
        const auto reviewed = s.zip ? s.zip_review : s.member_review;
        const auto overwrite = s.overwrite;
        s.error.clear();
        s.message.clear();
        s.start(
            3,
            [target, reviewed, overwrite](const JobContext &context) {
              if (!overwrite && std::filesystem::exists(target))
                throw std::invalid_argument(
                    "File exists; choose another name or confirm replacement");
              Result r;
              const auto &bytes = reviewed->command.data();
              r.staged =
                  detail::stage_atomic_file(target, bytes.data(), bytes.size(),
                                            context.execution_control());
              return r;
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
    ImGui::TextUnformatted("Preparing export...");
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
Json RoiMixedExportPanel::diagnostics() const {
  const auto &s = *state_;
  Json sources = Json::array();
  if (s.inputs)
    for (const auto &v : s.inputs->catalog.entries())
      sources.push_back(
          {{"index", v.index}, {"name", v.name}, {"bytes", v.data_bytes}});
  auto active = s.zip_review ? s.zip_review : s.member_review;
  return {{"draft_mode", s.draft},
          {"mixed", true},
          {"job_id", s.job.valid() ? s.job.info().id : 0},
          {"visible", s.open},
          {"worker_active", s.working()},
          {"phase", s.phase},
          {"ready", s.zip ? bool(s.zip_review) : bool(s.member_review)},
          {"member_ready", bool(s.member_review)},
          {"zip_mode", s.zip},
          {"queue_count", s.queue.size()},
          {"compose", s.compose},
          {"compress", s.compress},
          {"source_index", s.source_index},
          {"annotation_kind", s.kind},
          {"annotation_index", s.row},
          {"metadata_choice", s.metadata},
          {"confirmed", s.confirmed},
          {"workflow_review", s.reviewing()},
          {"destination_confirmed", s.destination_confirmed},
          {"sources", sources},
          {"annotation_count", s.inputs ? s.inputs->rows.size() : 0},
          {"display_count", s.inputs ? s.inputs->displays.size() : 0},
          {"path", s.path},
          {"overwrite", s.overwrite},
          {"image_review", s.image_review},
          {"standard_preview", s.standard},
          {"output_bytes", active ? active->command.data().size() : 0},
          {"projection_group",
           s.member_review
               ? std::visit([](const auto &p) { return Json(p.group); },
                            s.member_review->command.members()
                                .front()
                                .request.standard_projection)
               : Json(nullptr)},
          {"error", s.error},
          {"message", s.message},
          {"cancel_reason", s.reason},
          {"window", s.window},
          {"controls", s.controls}};
}
} // namespace imagejxx::desktop
