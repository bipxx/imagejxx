#pragma once

#include <imagejxx/application.hpp>
#include <imagejxx/gaussian_command.hpp>
#include <imagejxx/rank_command.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace imagejxx {class ResolvedCalibratedCommand;}
namespace imagejxx::desktop {

// A frozen presentation delta, never a source/result/resource owner. Consumers
// may copy it into a viewport worker request after the host-thread query.
struct PlanePreviewDisplayEffects {
  PlanePosition position;
  std::uint64_t generation;
  std::optional<std::array<double, 2>> range;
  bool suppress_threshold_overlay = false;
};

// Host-thread controller. Application must outlive this object; workers only
// receive immutable native command/parameter copies. It owns no workflow state.
class GaussianJobs {
public:
  using Publish = std::function<void(const ResolvedGaussianBlurCommand &,
                                     const OperationJob &)>;
  using RankPublish = std::function<void(const ResolvedRankFilterCommand &,
                                         const OperationJob &)>;

  explicit GaussianJobs(Application &application);
  ~GaussianJobs();
  GaussianJobs(const GaussianJobs &) = delete;
  GaussianJobs &operator=(const GaussianJobs &) = delete;

  // First generation may be zero; subsequent requests for the same operation
  // must strictly increase. The panels have independent generation counters.
  // Rejected duplicate/older requests leave the latest shared preview intact.
  // Admission failures return false and retain their diagnostic error.
  bool request_preview(const ResolvedGaussianBlurCommand &command,
                       std::uint64_t generation);
  bool request_preview(const ResolvedRankFilterCommand &command,
                       std::uint64_t generation);
  void cancel_preview();
  bool request_apply(const ResolvedGaussianBlurCommand &command);
  bool request_apply(const ResolvedCalibratedCommand &command);
  const MetadataObject& apply_record() const;
  bool request_apply(const ResolvedRankFilterCommand &command);
  void cancel_all();
  void document_closed(std::uint64_t document_id);

  // Nonblocking. current is the active source, not a prepared preview. Missing,
  // foreign, closed or stale sources invalidate their owned work. Navigation
  // revisions alone do not retarget the captured command. Publish must finish
  // native publication (normally with a copy passed to Application::publish).
  void poll(const std::optional<DocumentSnapshot> &current,
            const Publish &publish, const RankPublish &rank_publish = {});
  bool apply_busy() const;
  // Host-thread-only cancellation mirror: preview first, Apply second, zero
  // for an absent lane. Numeric IDs do not retain a prepared-result payload.
  std::array<std::uint64_t, 2> active_job_ids() const;
  std::optional<OperationJob> preview_job() const;
  std::optional<OperationJob> apply_job() const;
  // Host-thread borrowed command target only; no result/OperationJob owner.
  // Valid until this controller is polled, canceled or otherwise mutated.
  // Missing, non-READY, canceled or uninstalled lanes return nullptr.
  const ResolvedTarget *preview_target(std::uint64_t document_id) const;
  std::optional<DocumentSnapshot>
  preview_snapshot(std::uint64_t document_id) const;
  std::optional<std::uint64_t>
  preview_generation(std::uint64_t document_id) const;
  // Host-thread only. The candidate is the final scalar plane or individual
  // composite contributor. Only an installed Top Hat subtraction proposal may
  // supply a delta. Live range/threshold edits override their respective fields;
  // navigation and unrelated display changes leave matching fields eligible.
  // No provider reads or result-resource ownership escape this value query.
  std::optional<PlanePreviewDisplayEffects>
  preview_display_effects(const DocumentSnapshot &current,
                          PlanePosition candidate) const;
  // Detailed inspection is host-thread-only and returns scalar metadata. The
  // default avoids eager plane-table inspection and preserves its old shape.
  MetadataObject diagnostics(bool detailed = false) const;
  // Called by the creator after actual native publication. A foreign/stale
  // job ID cannot change the current Apply lane's publication lineage.
  void record_publication(std::uint64_t job_id,
                          std::uint64_t published_document_id) noexcept;

private:
  using Command = std::variant<ResolvedGaussianBlurCommand,
                               ResolvedRankFilterCommand>;
  struct Lane {
    std::optional<Command> command;
    std::optional<OperationJob> job;
    std::optional<JobInfo> last_job;
    MetadataObject target;
    MetadataObject command_record;
    std::optional<std::uint64_t> request_generation, published_document_id;
    std::uint64_t source_id = 0;
    std::size_t record_working_bytes = 0;
    // Retain the diagnostic name even after command/result ownership is freed.
    const char *name = "Gaussian";
    std::string exit = "idle", error;
  };

  static const ResolvedTarget &target(const Command &command);
  static std::size_t operation_index(const Command &command);
  static const char *operation_id(const Command &command);
  static const char *command_name(const Command &command);
  void start(Lane &lane, const Command &command);
  void admit_record(Lane&,const ResolvedCalibratedCommand* = nullptr);
  bool request_preview_command(const Command &command,
                               std::uint64_t generation);
  bool request_apply_command(const Command &command,const ResolvedCalibratedCommand* = nullptr);
  void release(Lane &lane, const char *exit, std::string error = {});
  void reject_generation(std::uint64_t generation, std::string reason);
  std::string invalid_source(const Lane &lane,
                             const std::optional<DocumentSnapshot> &current) const;
  void discard_preview(const char *exit, std::string error = {});

  Application &application_;
  Lane preview_, apply_;
  MetadataObject last_captured_command_record_;
  std::optional<DocumentSnapshot> installed_preview_;
  bool installed_preview_processed_ = false;
  std::optional<std::uint64_t> requested_generation_, installed_generation_;
  std::array<std::optional<std::uint64_t>, 2> operation_generations_{};
  std::optional<std::uint64_t> active_installed_generation_;
  std::vector<std::uint64_t> rejected_generations_;
  std::uint64_t rejected_count_ = 0, apply_callbacks_ = 0;
  bool applying_callback_ = false;
  std::string request_error_;
};

} // namespace imagejxx::desktop
