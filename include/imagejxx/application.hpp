#pragma once

#include <imagejxx/document.hpp>
#include <imagejxx/execution.hpp>
#include <imagejxx/jobs.hpp>
#include <imagejxx/auto_contrast_job.hpp>
#include <imagejxx/targets.hpp>
#include <imagejxx/saved_roi_geometry.hpp>
#include <unordered_map>
#include <unordered_set>

namespace imagejxx {

namespace detail { struct AutoContrastControllerState; }

class DocumentUnavailable : public std::runtime_error {
public:
  DocumentUnavailable()
      : std::runtime_error("Document is closed or unavailable") {}
};

// Minted only by an Application after any explicit row navigation. Copies
// retain this exact owner/activation and immutable source, not a numeric handle.
class SavedRoiGeometryCapture {
public:
  SavedRoiGeometryCapture(const SavedRoiGeometryCapture &) = default;
  SavedRoiGeometryCapture(SavedRoiGeometryCapture &&) noexcept = default;
  SavedRoiGeometryCapture &operator=(const SavedRoiGeometryCapture &) = default;
  SavedRoiGeometryCapture &operator=(SavedRoiGeometryCapture &&) noexcept = default;
  std::uint64_t document_id() const { return source_.id(); }
  const DocumentSnapshot &source() const { return source_; }

private:
  friend class Application;
  SavedRoiGeometryCapture(std::shared_ptr<Document> owner,
                          DocumentSnapshot source,
                          std::shared_ptr<const int> activation,
                          const std::shared_ptr<const int> &application)
      : owner_(std::move(owner)), source_(std::move(source)),
        activation_(std::move(activation)), application_(application) {}
  std::shared_ptr<Document> owner_;
  DocumentSnapshot source_;
  std::shared_ptr<const int> activation_;
  std::weak_ptr<const int> application_;
};

struct SavedRoiGeometryPublication {
  std::uint64_t document_id;
  bool changed;
};

// Metadata-only staged storage; no native mutation until guarded publication.
// Every terminal publication attempt consumes it. Owned storage and the live
// logical reservation are released on success, refusal, cancellation or error.
class PreparedSavedRoiGeometryEdit {
public:
  ~PreparedSavedRoiGeometryEdit();
  PreparedSavedRoiGeometryEdit(PreparedSavedRoiGeometryEdit &&) noexcept;
  PreparedSavedRoiGeometryEdit &operator=(PreparedSavedRoiGeometryEdit &&) noexcept;
  PreparedSavedRoiGeometryEdit(const PreparedSavedRoiGeometryEdit &) = delete;
  PreparedSavedRoiGeometryEdit &operator=(const PreparedSavedRoiGeometryEdit &) = delete;
  bool active() const noexcept;
  const MetadataObject &record() const;
  std::size_t working_bytes() const noexcept;
  std::size_t retained_working_bytes() const noexcept;

private:
  friend class Application;
  struct State;
  explicit PreparedSavedRoiGeometryEdit(std::unique_ptr<State> state) noexcept;
  std::unique_ptr<State> state_;
};

struct DocumentInitialDisplay {
  DocumentSnapshot expected;
  DisplayState display;
};
struct DocumentAdoption {
  std::shared_ptr<Document> document;
  std::optional<DocumentInitialDisplay> initial_display;
};

// A prepared batch retains the Application membership lock and every selected
// Document lock, in ascending ID order. The Application must outlive this guard;
// preparation, moves, commit, unlock and destruction stay on the same thread.
// Do not reenter the Application or its selected Documents while locks are held.
// commit publishes only prepared swaps/scalars and retains the locks. unlock
// aborts an uncommitted candidate without changing live state. Both operations
// are repeatable no-ops after their applicable state has ended. Retired histories
// remain owned until destruction, which unlocks before releasing their sources.
// Declare the holder outside any scheduler acceptance callback so retirement
// also occurs after that caller's scheduling lock has been released.
class PreparedHistoryPolicies {
public:
  PreparedHistoryPolicies() noexcept;
  ~PreparedHistoryPolicies();
  PreparedHistoryPolicies(PreparedHistoryPolicies &&) noexcept;
  PreparedHistoryPolicies &operator=(PreparedHistoryPolicies &&) noexcept;
  PreparedHistoryPolicies(const PreparedHistoryPolicies &) = delete;
  PreparedHistoryPolicies &operator=(const PreparedHistoryPolicies &) = delete;
  // active means prepared, not committed and not unlocked (including an empty
  // valid batch). committed remains observable after unlock.
  bool active() const noexcept;
  bool committed() const noexcept;
  void commit() noexcept;
  void unlock() noexcept;

private:
  friend class Application;
  struct State;
  explicit PreparedHistoryPolicies(std::unique_ptr<State> state) noexcept;
  std::unique_ptr<State> state_;
};
// For nonempty adoption, membership is published and retirement is committed/
// unlocked. Empty adoption has empty IDs and a default retirement guard. Keep
// this result alive until the caller has finished its no-throw attachment work;
// pruned custom sources may reenter only when the retirement holder is released.
struct HistoryPolicyAdoption {
  std::vector<std::uint64_t> document_ids;
  PreparedHistoryPolicies retirement;
};

// Select one incoming owner by its ordinal while preserving input/tab order.
// The activation identity is staged with membership, before publication.
struct IncomingDocumentActivation {
  std::size_t index;
};

// Headless application membership and activation. A zero ID resolves the active
// document. Registry mutations and document publications are serialized; work
// reads retained snapshots outside that lock. Closed IDs cannot be readmitted
// to the same application, so an old handle never addresses a later document.
// Adopted Document objects remain usable independently by their external owner.
class Application {
public:
  ~Application();
  std::shared_ptr<JobScheduler> job_scheduler();
  OperationJob submit(const std::string &operation,
                      const ResolvedTarget &target,
                      const OperationParameters &parameters = {},
                      ExecutionOptions options = {},
                      JobOptions job_options = {});
  AutoContrastJob submit_auto_contrast(
      const AutoContrastOptions &options = {},
      const AutoContrastLimits &limits = {}, JobOptions job_options = {});
  // Explicit initial display for an ordinary source with no channel_display
  // property. The seed is staged privately: admission/failure/cancellation do
  // not publish it. A successful Auto atomically publishes its complete Set
  // effect and the normal controller cycle. Existing submit remains strict.
  AutoContrastJob submit_seeded_auto_contrast(const ChannelDisplayState &initial,
      const AutoContrastOptions &options = {},
      const AutoContrastLimits &limits = {}, JobOptions job_options = {});
  AutoContrastJob submit_rebound_auto_contrast(
      const MetadataObject &original, const AutoContrastRequest &complete_request,
      const AutoContrastLimits &limits = {}, JobOptions job_options = {});
  ResolvedAutoContrastCommand publish_auto_contrast(AutoContrastJob &job);
  ResolvedAutoContrastCommand replay_auto_contrast(
      const MetadataObject &record, const AutoContrastLimits &limits = {},
      std::uint64_t id = 0);
  // Effective value for the current owner/activation/TCZ/Pixels+Metadata key.
  // Querying does not publish a cycle change; explicit reset invalidates work
  // without changing any Document revision or canceling unrelated jobs.
  int auto_contrast_divisor() const;
  void reset_auto_contrast_cycle();
  // ResetRange publication: exact owner/all4 guard, then an allocation-free
  // cycle reset if this owner is active. An inactive owner's display edit
  // preserves the unrelated active owner's cycle and pending Auto work.
  void set_display_and_reset_auto_contrast(const DocumentSnapshot &expected,
                                            DisplayState display);
  SavedRoiGeometryCapture capture_saved_roi_geometry() const;
  // In addition to the pure snapshot estimate, admission charges the owner's
  // actual undo-holder staging. It neither acquires pixels nor changes policy.
  PreparedSavedRoiGeometryEdit prepare_saved_roi_geometry(
      const SavedRoiGeometryCapture &capture,
      const ResolvedSavedRoiGeometryCommand &command,
      SavedRoiGeometryInteraction interaction = SavedRoiGeometryInteraction::Numeric,
      const SavedRoiGeometryLimits &limits = {},
      const ExecutionControl &control = {}) const;
  SavedRoiGeometryPublication publish_saved_roi_geometry(
      PreparedSavedRoiGeometryEdit &prepared);
  std::uint64_t create(Dataset dataset, std::string title = "Untitled");
  std::uint64_t adopt(std::shared_ptr<Document> document,
                      bool make_active = true);
  // Publish the complete batch or leave membership, order and activation
  // unchanged. Null, duplicate, registered and previously closed IDs reject
  // the whole batch. Input order is retained; an empty batch is a no-op.
  // make_active selects the last new owner; otherwise keep the active owner,
  // selecting the first new owner only when the application had none.
  std::vector<std::uint64_t>
  adopt_all(std::vector<std::shared_ptr<Document>> documents,
             bool make_active = true);
  std::vector<std::uint64_t>
  adopt_all(std::vector<std::shared_ptr<Document>> documents,
            IncomingDocumentActivation activation);
  // Atomically publish membership, the supplied history policy and optional
  // initial displays for all incoming owners. An initial display requires its
  // exact captured owner and all four revisions; it publishes one Display
  // revision without changing pixels, history identities or dirty state.
  // Keep the returned retirement holder through caller attachment; it already
  // holds no publication locks. Existing adopt/adopt_all are unchanged.
  HistoryPolicyAdoption adopt_all_with_history_policy(
      std::vector<DocumentAdoption> documents, HistoryPolicy policy,
      bool make_active = true);
  HistoryPolicyAdoption adopt_all_with_history_policy(
      std::vector<DocumentAdoption> documents, HistoryPolicy policy,
      IncomingDocumentActivation activation);
  std::uint64_t duplicate(std::uint64_t id = 0);
  std::vector<std::uint64_t> document_ids() const;
  std::uint64_t active_document_id() const;
  void activate(std::uint64_t id);
  bool contains(std::uint64_t id) const;
  void close(std::uint64_t id = 0, bool discard = false);
  DocumentSnapshot snapshot(std::uint64_t id = 0) const;
  DocumentTransaction begin_edit(std::uint64_t id = 0,
                                 Change dependencies = Change::Data) const;
  ResolvedTarget resolve_target(const TargetRequest &request = {},
                                std::uint64_t id = 0,
                                const std::string &operation = {}) const;
  void validate_target(const ResolvedTarget &target) const;
  PreparedOperation prepare(const std::string &operation,
                            const ResolvedTarget &target,
                            const OperationParameters &parameters = {},
                            ExecutionOptions options = {},
                            const ExecutionControl &control = {}) const;
  PreparedOperation prepare_axis_plane_export(
      const AxisPlaneExportRequest &request = {}, std::uint64_t id = 0,
      const ExecutionControl &control = {}) const;
  PreparedOperation prepare_axis_range_reslice(
      const AxisRangeResliceRequest &request = {}, std::uint64_t id = 0,
      const ExecutionControl &control = {}) const;
  PreparedOperation prepare_axis_range_orientation(
      const AxisRangeOrientationRequest &request = {}, std::uint64_t id = 0,
      const ExecutionControl &control = {}) const;
  PreparedOperation prepare_axis_range_spacing(
      const AxisRangeSpacingRequest &request = {}, std::uint64_t id = 0,
      const ExecutionControl &control = {}) const;
  PreparedOperation prepare_axis_range_slicer_resize(
      const AxisRangeSlicerResizeRequest &request = {}, std::uint64_t id = 0,
      const ExecutionControl &control = {}) const;
  PublishedOperation publish(PreparedOperation &prepared,
                             std::optional<std::string> title = std::nullopt);
  PublishedOperation publish(OperationJob &job,
                             std::optional<std::string> title = std::nullopt);
  void commit(DocumentTransaction &transaction,
              std::optional<DisplayState> display = std::nullopt,
              bool preserve_concurrent_display = false);
  void set_display(DisplayState display, std::uint64_t id = 0);
  // Resolve the captured owner under the membership lock, then check all
  // revisions and publish atomically under its document lock. Never retarget
  // the currently active document; a closed captured owner refuses.
  void set_display_if_current(const DocumentSnapshot &expected,
                              DisplayState display);
  void select(std::optional<Selection> selection, std::uint64_t id = 0);
  void undo(std::uint64_t id = 0);
  void redo(std::uint64_t id = 0);
  void mark_saved(std::uint64_t id = 0);
  void mark_saved(const DocumentSnapshot &saved);
  void set_history_policy(std::size_t limit, std::size_t pixel_budget = 0,
                          std::uint64_t id = 0);
  // IDs are explicit: zero, duplicate, closed and unknown IDs reject. Empty
  // batches are no-ops but still require a positive step limit.
  PreparedHistoryPolicies prepare_history_policies(
      const std::vector<std::uint64_t> &explicit_ids, HistoryPolicy policy);
  HistoryInfo history(std::uint64_t id = 0) const;

private:
  AutoContrastJob submit_auto_contrast_impl(const AutoContrastOptions &,
      const AutoContrastLimits &,JobOptions,const ChannelDisplayState *);
  friend class PreparedOperation;
  std::shared_ptr<Document> lookup_locked(std::uint64_t id) const;
  std::uint64_t adopt_locked(std::shared_ptr<Document> document,
                             bool make_active);
  std::vector<std::uint64_t> adopt_all_impl(
      std::vector<std::shared_ptr<Document>> documents, bool make_active,
      std::optional<std::size_t> incoming_active);
  HistoryPolicyAdoption adopt_all_with_history_policy_impl(
      std::vector<DocumentAdoption> documents, HistoryPolicy policy,
      bool make_active, std::optional<std::size_t> incoming_active);
  mutable std::mutex mutex_;
  std::unordered_map<std::uint64_t, std::shared_ptr<Document>> documents_;
  std::vector<std::uint64_t> order_;
  std::unordered_set<std::uint64_t> closed_;
  std::uint64_t active_ = 0;
  std::shared_ptr<const int> activation_identity_ = std::make_shared<const int>(0);
  std::shared_ptr<detail::AutoContrastControllerState> auto_contrast_;
  std::shared_ptr<JobScheduler> scheduler_;
  std::shared_ptr<const int> job_identity_ = std::make_shared<const int>(0);
};

} // namespace imagejxx
