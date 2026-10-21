#include "jobs_panel.hpp"
#include <algorithm>
#include <imgui_internal.h>

namespace imagejxx::desktop {
namespace {
const char *stateName(JobState state) {
  switch (state) {
  case JobState::Queued:
    return "Queued";
  case JobState::Running:
    return "Running";
  case JobState::Ready:
    return "Ready";
  case JobState::Publishing:
    return "Publishing";
  case JobState::Completed:
    return "Completed";
  case JobState::Failed:
    return "Failed";
  case JobState::Canceled:
    return "Canceled";
  }
  return "Unknown";
}
bool cancelable(const JobInfo &info) {
  return !info.cancel_requested &&
         (info.state == JobState::Queued || info.state == JobState::Running ||
          info.state == JobState::Ready);
}
} // namespace

void JobsPanel::draw(const JobScheduler &scheduler,
                     const std::function<void(std::uint64_t)> &cancel) {
  cancel_ = {-1, -1};
  visibleIds_.clear();
  jobs_ = Json::array();
  if (!open_)
    return;
  ImGui::SetNextWindowSize({800, 430}, ImGuiCond_FirstUseEver);
  if (focus_)
    ImGui::SetNextWindowFocus();
  focus_ = false;
  if (!ImGui::Begin("Jobs and log", &open_)) {
    ImGui::End();
    return;
  }
  const auto limits = scheduler.limits();
  const auto usage = scheduler.usage();
  usage_ = {{"running", usage.running},
            {"queued", usage.queued},
            {"reserved_bytes", usage.reserved_bytes},
            {"running_bytes", usage.running_bytes},
            {"retained_bytes", usage.retained_bytes}};
  const auto records = scheduler.jobs();
  constexpr double mib = 1024. * 1024.;
  ImGui::Text("Workers: %zu / %zu   Queued: %zu", usage.running, limits.workers,
              usage.queued);
  if (limits.memory_budget)
    ImGui::Text("Reserved workspace: %.1f / %.1f MiB",
                usage.reserved_bytes / mib, limits.memory_budget / mib);
  else
    ImGui::Text("Reserved workspace: %.1f MiB (reservation limit disabled)",
                usage.reserved_bytes / mib);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Running-job workspace reservations are estimates. "
                      "Documents, retained results, history and caches "
                      "are not fully included in this figure.");
  filter_.Draw("Filter task, phase, document or error", 260);
  ImGui::SameLine();
  ImGui::Checkbox("Errors only", &errorsOnly_);
  const float height = std::max(80.f, ImGui::GetContentRegionAvail().y - 95.f);
  if (ImGui::BeginTable("jobs", 6,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                        {0, height})) {
    ImGui::TableSetupColumn("Job", ImGuiTableColumnFlags_WidthStretch, 2.f);
    ImGui::TableSetupColumn("Document");
    ImGui::TableSetupColumn("State");
    ImGui::TableSetupColumn("Progress");
    ImGui::TableSetupColumn("Workspace");
    ImGui::TableSetupColumn("Action");
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();
    for (auto it = records.rbegin(); it != records.rend(); ++it) {
      const auto &info = *it;
      const auto identity = std::to_string(info.id);
      const auto label = identity + "  " + info.label;
      const auto searchable = label + " " + std::to_string(info.document_id) +
                              " " + stateName(info.state) + " " + info.stage +
                              " " + info.error;
      if ((errorsOnly_ && info.error.empty()) ||
          !filter_.PassFilter(searchable.c_str()))
        continue;
      visibleIds_.push_back(info.id);
      ImGui::PushID(identity.c_str());
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      if (ImGui::Selectable(label.c_str(), selected_ == info.id))
        selected_ = info.id;
      if (!info.error.empty() && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", info.error.c_str());
      ImGui::TableSetColumnIndex(1);
      if (info.document_id)
        ImGui::TextUnformatted(std::to_string(info.document_id).c_str());
      else
        ImGui::TextUnformatted("None");
      ImGui::TableSetColumnIndex(2);
      ImGui::TextUnformatted(info.cancel_requested &&
                                     info.state == JobState::Running
                                 ? "Canceling"
                                 : stateName(info.state));
      ImGui::TableSetColumnIndex(3);
      const bool showStage =
          info.state == JobState::Running && !info.stage.empty();
      const auto completed = showStage ? info.stage_completed : info.completed;
      const auto total = showStage ? info.stage_total : info.total;
      const auto progress =
          std::to_string(completed) + " / " + std::to_string(total);
      ImGui::ProgressBar(total ? float(double(completed) / total) : 0.f,
                         {-1, 0}, progress.c_str());
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Completed planes: %zu / %zu", info.completed,
                          info.total);
      if (!info.stage.empty())
        ImGui::TextUnformatted(info.stage.c_str());
      ImGui::TableSetColumnIndex(4);
      ImGui::Text("%.1f MiB", info.resource_bytes / mib);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Requested: %.1f MiB\nPeak acquired reservation: %.1f MiB\n%s",
            info.resource_bytes / mib, info.peak_reserved_bytes / mib,
            info.reservation_acquired ? "Workspace was acquired"
                                      : "Workspace has not been acquired");
      ImGui::TableSetColumnIndex(5);
      // An absent Cancel has no item identity or hitbox in this frame.
      Json cancelControl{{"id", 0},           {"rect", {-1, -1, -1, -1}},
                         {"point", {-1, -1}}, {"enabled", false},
                         {"active", false},   {"focused", false}};
      if (cancelable(info)) {
        if (ImGui::Button("Cancel"))
          cancel(info.id);
        const auto lower = ImGui::GetItemRectMin();
        const auto upper = ImGui::GetItemRectMax();
        cancelControl = {
            {"id", ImGui::GetItemID()},
            {"rect", {lower.x, lower.y, upper.x, upper.y}},
            {"point", {(lower.x + upper.x) / 2, (lower.y + upper.y) / 2}},
            {"enabled", !(ImGui::GetCurrentContext()->CurrentItemFlags &
                          ImGuiItemFlags_Disabled)},
            {"active", ImGui::IsItemActive()},
            {"focused", ImGui::IsItemFocused()}};
        if (cancel_.x < 0) {
          cancel_ = {(lower.x + upper.x) / 2, (lower.y + upper.y) / 2};
        }
      }
      jobs_.push_back({{"id", info.id},
                       {"state", stateName(info.state)},
                       {"label", info.label},
                       {"document_id", info.document_id},
                       {"stage", info.stage},
                       {"stage_completed", info.stage_completed},
                       {"stage_total", info.stage_total},
                       {"peak_reserved_bytes", info.peak_reserved_bytes},
                       {"reservation_acquired", info.reservation_acquired},
                       {"cancel", std::move(cancelControl)}});
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  auto selected =
      std::find_if(records.begin(), records.end(),
                   [&](const JobInfo &info) { return info.id == selected_; });
  if (selected != records.end()) {
    ImGui::TextUnformatted(selected->label.c_str());
    if (!selected->error.empty())
      ImGui::TextWrapped("%s", selected->error.c_str());
    else
      ImGui::Text("%s. Completed planes: %zu / %zu", stateName(selected->state),
                  selected->completed, selected->total);
    if (!selected->stage.empty())
      ImGui::Text("%s: %zu / %zu", selected->stage.c_str(),
                  selected->stage_completed, selected->stage_total);
  } else if (visibleIds_.empty())
    ImGui::TextUnformatted("No matching jobs.");
  else
    ImGui::TextUnformatted("Select a job to inspect its status or error.");
  ImGui::End();
}
Json JobsPanel::diagnostics() const {
  return {{"visible", open_},
          {"job_ids", visibleIds_},
          {"jobs", jobs_},
          {"usage", usage_}};
}
} // namespace imagejxx::desktop
