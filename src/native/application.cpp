#include "operation_internal.hpp"
#include "target_mask_resources.hpp"
#include "operation_job_probe_internal.hpp"
#include <algorithm>
#include <imagejxx/application.hpp>
#include <limits>

namespace imagejxx {
struct PreparedHistoryPolicies::State {
  std::vector<std::shared_ptr<Document>> owners;
  std::vector<Document::HistoryPolicyCandidate> candidates;
  std::unique_lock<std::mutex> application_lock;
  std::vector<std::unique_lock<std::mutex>> document_locks;
  bool published = false, released = false;

  void unlock() noexcept {
    if (released)
      return;
    for (auto lock = document_locks.rbegin(); lock != document_locks.rend(); ++lock)
      if (lock->owns_lock())
        lock->unlock();
    if (application_lock.owns_lock())
      application_lock.unlock();
    released = true;
  }
  ~State() { unlock(); }
};
PreparedHistoryPolicies::PreparedHistoryPolicies() noexcept = default;
PreparedHistoryPolicies::PreparedHistoryPolicies(std::unique_ptr<State> state) noexcept
    : state_(std::move(state)) {}
PreparedHistoryPolicies::~PreparedHistoryPolicies() { unlock(); }
PreparedHistoryPolicies::PreparedHistoryPolicies(PreparedHistoryPolicies &&) noexcept = default;
PreparedHistoryPolicies &PreparedHistoryPolicies::operator=(
    PreparedHistoryPolicies &&other) noexcept {
  if (this != &other) {
    unlock();
    state_ = std::move(other.state_);
  }
  return *this;
}
bool PreparedHistoryPolicies::active() const noexcept {
  return state_ && !state_->released && !state_->published;
}
bool PreparedHistoryPolicies::committed() const noexcept {
  return state_ && state_->published;
}
void PreparedHistoryPolicies::commit() noexcept {
  if (!active())
    return;
  for (std::size_t i = 0; i < state_->owners.size(); ++i)
    state_->owners[i]->publish_history_policy_locked(state_->candidates[i]);
  state_->published = true;
}
void PreparedHistoryPolicies::unlock() noexcept {
  if (state_)
    state_->unlock();
}

Application::~Application() {
  if (scheduler_)
    scheduler_->shutdown();
}
std::shared_ptr<JobScheduler> Application::job_scheduler() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!scheduler_)
    scheduler_ = std::make_shared<JobScheduler>();
  return scheduler_;
}
OperationJob Application::submit(const std::string &operation,
                                 const ResolvedTarget &target,
                                 const OperationParameters &parameters,
                                 ExecutionOptions options,
                                 JobOptions job_options) {
  auto normalized =
      validate_execution_parameters(operation, target, parameters, options);
  const auto shape = target.source().data().dataset.shape();
  auto multiply = [](std::size_t a, std::size_t b) {
    if (b && a > std::numeric_limits<std::size_t>::max() / b)
      throw std::invalid_argument(
          "Job reservation overflows addressable memory");
    return a * b;
  };
  const auto pixels = multiply(std::size_t(shape.x), shape.y);
  const auto plane = multiply(
      pixels, multiply(std::size_t(std::max(shape.s, 4)), sizeof(double)));
  // Conservative admission estimate for staged planes plus plane workspace.
  const auto staged = multiply(plane, target.position_count());
  const auto workspace =
      multiply(pixels, multiply(std::size_t(shape.s), 16 * sizeof(double)));
  const auto extra =
      operation_extra_workspace(operation,
                                {shape.x,
                                 shape.y,
                                 shape.s,
                                 target.source().data().dataset.pixel_type(),
                                 {}},
                                normalized);
  if (staged > std::numeric_limits<std::size_t>::max() - workspace)
    throw std::invalid_argument("Job reservation overflows addressable memory");
  if (extra > std::numeric_limits<std::size_t>::max() - staged - workspace)
    throw std::invalid_argument("Job reservation overflows addressable memory");
  const auto rounded = detail::target_rounded_mask_workspace(target);
  if (rounded > std::numeric_limits<std::size_t>::max() - staged - workspace - extra)
    throw std::invalid_argument("Job reservation overflows addressable memory");
  job_options.resource_bytes =
      std::max(job_options.resource_bytes, staged + workspace + extra + rounded);
  job_options.document_id = target.document_id();
  if (job_options.label.empty())
    job_options.label = operation;
  auto result = std::make_shared<OperationJob::ResultCell>(
      job_options.retain_result_resources);
  std::shared_ptr<JobScheduler> scheduler;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    target.validate_source(lookup_locked(target.document_id())->snapshot());
    if (!scheduler_)
      scheduler_ = std::make_shared<JobScheduler>();
    scheduler = scheduler_;
  }
  // Supersession may destroy reader/cache captures that reenter this
  // controller. Keep admission outside the application lock, then catch a
  // concurrent close.
  auto probe = detail::capture_operation_job_probe();
  auto job = scheduler->submit_with_cleanup(
      [result, operation, target, normalized = std::move(normalized),
       options, probe = std::move(probe)](const JobContext &context) {
        auto prepared = [&] {
          // The default path keeps the original control and callbacks intact.
          if (!probe)
            return prepare_operation(operation, target, normalized, options,
                                     context.execution_control());

          // The context remains the owner. Only a non-owning tag escapes
          // diagnostics, and the temporary shared copy dies before callbacks.
          const std::weak_ptr<const void> weakResourceGuard(context.result_resource_guard());
          probe({"worker_entered", operation, target.document_id(), {}, 0, 0, weakResourceGuard});
          context.checkpoint();
          auto control = context.execution_control();
          auto original_stage = std::move(control.stage_progress);
          control.stage_progress =
              [&context, &probe, &operation, weakResourceGuard, source_id = target.document_id(),
               original_stage = std::move(original_stage)](
                  std::string_view stage, std::size_t completed,
                  std::size_t total) {
                // Native progress releases its state lock before diagnostics
                // may wait, and cancellation is checked after that wait.
                if (original_stage)
                  original_stage(stage, completed, total);
                probe({"stage_progress", operation, source_id, stage,
                       completed, total, weakResourceGuard});
                context.checkpoint();
              };
          return prepare_operation(operation, target, normalized, options,
                                   control);
        }();
        prepared.retain_resources(context.result_resource_guard());
        result->store(std::move(prepared));
      },
      std::move(job_options), result->cleanup_callback());
  result->bind_job(job);
  try {
    std::lock_guard<std::mutex> lock(mutex_);
    lookup_locked(target.document_id());
  } catch (...) {
    job.cancel();
    throw;
  }
  return OperationJob(std::move(job), std::move(result), job_identity_);
}
PublishedOperation Application::publish(OperationJob &operation_job,
                                        std::optional<std::string> title) {
  auto identity = operation_job.application_.lock();
  if (!identity || identity.get() != job_identity_.get())
    throw DocumentUnavailable();
  std::optional<PublishedOperation> result;
  operation_job.job_.complete([&] {
    auto prepared = operation_job.result_->get();
    result = publish(prepared, std::move(title));
  });
  return std::move(*result);
}

std::shared_ptr<Document> Application::lookup_locked(std::uint64_t id) const {
  auto found = documents_.find(id ? id : active_);
  if (found == documents_.end())
    throw DocumentUnavailable();
  return found->second;
}
std::uint64_t Application::adopt_locked(std::shared_ptr<Document> document,
                                        bool make_active) {
  if (!document || !document->id)
    throw std::invalid_argument("A document with a nonzero ID is required");
  auto id = document->id;
  if (closed_.count(id))
    throw DocumentUnavailable();
  if (documents_.count(id))
    throw std::invalid_argument("Document ID is already registered");
  // Stage identity allocation before membership changes. Returning to the same
  // ID after another activation must never revive captured Auto work.
  auto activation = make_active || !active_
      ? std::make_shared<const int>(0) : activation_identity_;
  // Allocate the order entry first; roll it back if map insertion fails.
  order_.push_back(id);
  try {
    documents_.emplace(id, std::move(document));
  } catch (...) {
    order_.pop_back();
    throw;
  }
  if (make_active || !active_) {
    active_ = id;
    activation_identity_ = std::move(activation);
  }
  return id;
}
std::uint64_t Application::create(Dataset dataset, std::string title) {
  return adopt(
      std::make_shared<Document>(std::move(dataset), std::move(title)));
}
std::uint64_t Application::adopt(std::shared_ptr<Document> document,
                                 bool make_active) {
  std::lock_guard<std::mutex> lock(mutex_);
  return adopt_locked(std::move(document), make_active);
}
std::vector<std::uint64_t>
Application::adopt_all(std::vector<std::shared_ptr<Document>> documents,
                        bool make_active) {
  return adopt_all_impl(std::move(documents), make_active, std::nullopt);
}
std::vector<std::uint64_t> Application::adopt_all(
    std::vector<std::shared_ptr<Document>> documents,
    IncomingDocumentActivation activation) {
  return adopt_all_impl(std::move(documents), false, activation.index);
}
std::vector<std::uint64_t> Application::adopt_all_impl(
    std::vector<std::shared_ptr<Document>> documents, bool make_active,
    std::optional<std::size_t> incoming_active) {
  if (incoming_active && *incoming_active >= documents.size())
    throw std::out_of_range("Incoming active document ordinal is out of range");
  std::vector<std::uint64_t> ids;
  ids.reserve(documents.size());
  std::unordered_set<std::uint64_t> incoming;
  incoming.reserve(documents.size());
  for (const auto &document : documents) {
    if (!document || !document->id)
      throw std::invalid_argument("A document with a nonzero ID is required");
    if (!incoming.insert(document->id).second)
      throw std::invalid_argument("Document ID occurs more than once in the batch");
    ids.push_back(document->id);
  }
  if (ids.empty())
    return ids;

  // Staged containers outlive the lock. Neither successful replacement nor
  // exception cleanup can release an owner's last reference under mutex_.
  decltype(documents_) staged_documents;
  decltype(order_) staged_order;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto id : ids) {
      if (closed_.count(id))
        throw DocumentUnavailable();
      if (documents_.count(id))
        throw std::invalid_argument("Document ID is already registered");
    }
    staged_documents = documents_;
    staged_order = order_;
    if (ids.size() > staged_documents.max_size() - staged_documents.size() ||
        ids.size() > staged_order.max_size() - staged_order.size())
      throw std::length_error("Document batch exceeds application capacity");
    staged_documents.reserve(staged_documents.size() + ids.size());
    staged_order.reserve(staged_order.size() + ids.size());
    for (std::size_t i = 0; i < documents.size(); ++i) {
      staged_documents.emplace(ids[i], documents[i]);
      staged_order.push_back(ids[i]);
    }
    const auto staged_active = incoming_active ? ids[*incoming_active]
                            : make_active ? ids.back()
                                          : active_ ? active_ : ids.front();
    auto staged_activation = staged_active != active_
        ? std::make_shared<const int>(0) : activation_identity_;
    // All allocations and validation precede this single publication region.
    // Default allocators/hashers make both swaps noexcept; observers share
    // mutex_ and cannot see a partially installed batch.
    static_assert(noexcept(documents_.swap(staged_documents)));
    static_assert(noexcept(order_.swap(staged_order)));
    documents_.swap(staged_documents);
    order_.swap(staged_order);
    active_ = staged_active;
    activation_identity_ = std::move(staged_activation);
  }
  return ids;
}
HistoryPolicyAdoption Application::adopt_all_with_history_policy(
    std::vector<DocumentAdoption> documents, HistoryPolicy policy,
    bool make_active) {
  return adopt_all_with_history_policy_impl(std::move(documents), policy,
                                            make_active, std::nullopt);
}
HistoryPolicyAdoption Application::adopt_all_with_history_policy(
    std::vector<DocumentAdoption> documents, HistoryPolicy policy,
    IncomingDocumentActivation activation) {
  return adopt_all_with_history_policy_impl(std::move(documents), policy,
                                            false, activation.index);
}
HistoryPolicyAdoption Application::adopt_all_with_history_policy_impl(
    std::vector<DocumentAdoption> documents, HistoryPolicy policy,
    bool make_active, std::optional<std::size_t> incoming_active) {
  if (incoming_active && *incoming_active >= documents.size())
    throw std::out_of_range("Incoming active document ordinal is out of range");
  if (!policy.step_limit)
    throw std::invalid_argument("History limit must be positive");
  std::vector<std::uint64_t> ids;
  std::vector<std::size_t> sorted;
  ids.reserve(documents.size());
  sorted.reserve(documents.size());
  for (std::size_t i = 0; i < documents.size(); ++i) {
    const auto &document = documents[i].document;
    if (!document || !document->id)
      throw std::invalid_argument("A document with a nonzero ID is required");
    ids.push_back(document->id);
    sorted.push_back(i);
  }
  std::sort(sorted.begin(), sorted.end(), [&](std::size_t a, std::size_t b) {
    return ids[a] < ids[b];
  });
  for (std::size_t i = 1; i < sorted.size(); ++i)
    if (ids[sorted[i - 1]] == ids[sorted[i]])
      throw std::invalid_argument("Document ID occurs more than once in the batch");
  if (ids.empty())
    return {};

  // The opaque state unlocks before retiring staged or replaced histories,
  // including on exception. Membership containers also outlive these locks.
  decltype(documents_) staged_documents;
  decltype(order_) staged_order;
  PreparedHistoryPolicies prepared(std::make_unique<PreparedHistoryPolicies::State>());
  auto &state = *prepared.state_;
  state.owners.reserve(sorted.size());
  state.candidates.reserve(sorted.size());
  state.document_locks.reserve(sorted.size());
  for (const auto index : sorted)
    state.owners.push_back(documents[index].document);
  state.application_lock = std::unique_lock<std::mutex>(mutex_);
  for (const auto id : ids) {
    if (closed_.count(id))
      throw DocumentUnavailable();
    if (documents_.count(id))
      throw std::invalid_argument("Document ID is already registered");
  }
  staged_documents = documents_;
  staged_order = order_;
  if (ids.size() > staged_documents.max_size() - staged_documents.size() ||
      ids.size() > staged_order.max_size() - staged_order.size())
    throw std::length_error("Document batch exceeds application capacity");
  staged_documents.reserve(staged_documents.size() + ids.size());
  staged_order.reserve(staged_order.size() + ids.size());
  for (std::size_t i = 0; i < documents.size(); ++i) {
    staged_documents.emplace(ids[i], documents[i].document);
    staged_order.push_back(ids[i]);
  }
  const auto staged_active = incoming_active ? ids[*incoming_active]
                          : make_active ? ids.back()
                                        : active_ ? active_ : ids.front();
  auto staged_activation = staged_active != active_
      ? std::make_shared<const int>(0) : activation_identity_;
  for (const auto &owner : state.owners)
    state.document_locks.emplace_back(owner->mutex_);
  for (const auto index : sorted) {
    const auto &entry = documents[index];
    const auto *initial = entry.initial_display ? &*entry.initial_display : nullptr;
    state.candidates.push_back(entry.document->prepare_history_policy_locked(
        policy, initial ? &initial->expected : nullptr,
        initial ? &initial->display : nullptr));
  }
  // Only no-throw swaps/scalars remain. Both registry and owner observers are
  // excluded until every history, display and membership change is installed.
  static_assert(noexcept(documents_.swap(staged_documents)));
  static_assert(noexcept(order_.swap(staged_order)));
  prepared.commit();
  documents_.swap(staged_documents);
  order_.swap(staged_order);
  active_ = staged_active;
  activation_identity_.swap(staged_activation);
  prepared.unlock();
  return {std::move(ids), std::move(prepared)};
}
std::uint64_t Application::duplicate(std::uint64_t id) {
  std::lock_guard<std::mutex> lock(mutex_);
  return adopt_locked(std::make_shared<Document>(lookup_locked(id)->snapshot()),
                      true);
}
std::vector<std::uint64_t> Application::document_ids() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return order_;
}
std::uint64_t Application::active_document_id() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return active_;
}
void Application::activate(std::uint64_t id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto next = lookup_locked(id)->id;
  if (next != active_) {
    auto activation = std::make_shared<const int>(0);
    active_ = next;
    activation_identity_ = std::move(activation);
  }
}
bool Application::contains(std::uint64_t id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return documents_.count(id ? id : active_) != 0;
}
void Application::close(std::uint64_t id, bool discard) {
  std::shared_ptr<Document> document;
  std::shared_ptr<JobScheduler> scheduler;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    document = lookup_locked(id);
    id = document->id;
    if (!discard && document->snapshot().dirty())
      throw std::invalid_argument(
          "Document has unsaved changes; use discard to close");
    auto found = std::find(order_.begin(), order_.end(), id);
    auto index = static_cast<std::size_t>(found - order_.begin());
    const auto next_active = active_ != id ? active_ : order_.size() == 1
        ? std::uint64_t(0) : order_[index + 1 < order_.size() ? index + 1 : index - 1];
    auto next_activation = next_active != active_
        ? std::make_shared<const int>(0) : activation_identity_;
    closed_.insert(id);
    documents_.erase(id);
    order_.erase(found);
    active_ = next_active;
    activation_identity_ = std::move(next_activation);
    scheduler = scheduler_;
  }
  if (scheduler)
    scheduler->cancel_document(id);
}
DocumentSnapshot Application::snapshot(std::uint64_t id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return lookup_locked(id)->snapshot();
}
DocumentTransaction Application::begin_edit(std::uint64_t id,
                                            Change dependencies) const {
  return snapshot(id).begin_edit(dependencies);
}
ResolvedTarget Application::resolve_target(const TargetRequest &request,
                                           std::uint64_t id,
                                           const std::string &operation) const {
  return imagejxx::resolve_target(snapshot(id), request, operation);
}
void Application::validate_target(const ResolvedTarget &target) const {
  target.validate_source(snapshot(target.document_id()));
}
PreparedOperation Application::prepare(const std::string &operation,
                                       const ResolvedTarget &target,
                                       const OperationParameters &parameters,
                                       ExecutionOptions options,
                                       const ExecutionControl &control) const {
  validate_target(target);
  return prepare_operation(operation, target, parameters, options, control);
}
PublishedOperation Application::publish(PreparedOperation &prepared,
                                        std::optional<std::string> title) {
  return prepared.publish(*this, std::move(title));
}
PreparedOperation Application::prepare_axis_plane_export(
    const AxisPlaneExportRequest &request, std::uint64_t id,
    const ExecutionControl &control) const {
  return imagejxx::prepare_axis_plane_export(snapshot(id), request, control);
}
PreparedOperation Application::prepare_axis_range_reslice(
    const AxisRangeResliceRequest &request, std::uint64_t id,
    const ExecutionControl &control) const {
  return imagejxx::prepare_axis_range_reslice(snapshot(id), request, control);
}
PreparedOperation Application::prepare_axis_range_orientation(
    const AxisRangeOrientationRequest &request, std::uint64_t id,
    const ExecutionControl &control) const {
  const auto captured_request = request;
  const auto captured_source = snapshot(id);
  return imagejxx::prepare_axis_range_orientation(captured_source,
                                                 captured_request, control);
}
PreparedOperation Application::prepare_axis_range_spacing(
    const AxisRangeSpacingRequest &request, std::uint64_t id,
    const ExecutionControl &control) const {
  const auto captured_request = request;
  const auto captured_source = snapshot(id);
  return imagejxx::prepare_axis_range_spacing(captured_source,
                                             captured_request, control);
}
PreparedOperation Application::prepare_axis_range_slicer_resize(
    const AxisRangeSlicerResizeRequest &request, std::uint64_t id,
    const ExecutionControl &control) const {
  const auto captured_request = request;
  const auto captured_source = snapshot(id);
  return imagejxx::prepare_axis_range_slicer_resize(captured_source,
                                                   captured_request, control);
}
void Application::commit(DocumentTransaction &transaction,
                         std::optional<DisplayState> display,
                         bool preserve_concurrent_display) {
  std::lock_guard<std::mutex> lock(mutex_);
  lookup_locked(transaction.document_id())
      ->commit(transaction, std::move(display), preserve_concurrent_display);
}
void Application::set_display(DisplayState display, std::uint64_t id) {
  std::lock_guard<std::mutex> lock(mutex_);
  lookup_locked(id)->set_display(std::move(display));
}
void Application::set_display_if_current(const DocumentSnapshot &expected,
                                         DisplayState display) {
  std::lock_guard<std::mutex> lock(mutex_);
  lookup_locked(expected.id())
      ->set_display_if_current(expected, std::move(display));
}
void Application::select(std::optional<Selection> selection, std::uint64_t id) {
  std::lock_guard<std::mutex> lock(mutex_);
  lookup_locked(id)->select(std::move(selection));
}
void Application::undo(std::uint64_t id) {
  std::lock_guard<std::mutex> lock(mutex_);
  lookup_locked(id)->undo();
}
void Application::redo(std::uint64_t id) {
  std::lock_guard<std::mutex> lock(mutex_);
  lookup_locked(id)->redo();
}
void Application::mark_saved(std::uint64_t id) {
  std::lock_guard<std::mutex> lock(mutex_);
  lookup_locked(id)->mark_saved();
}
void Application::mark_saved(const DocumentSnapshot &saved) {
  std::lock_guard<std::mutex> lock(mutex_);
  lookup_locked(saved.id())->mark_saved(saved);
}
void Application::set_history_policy(std::size_t limit,
                                     std::size_t pixel_budget,
                                     std::uint64_t id) {
  std::lock_guard<std::mutex> lock(mutex_);
  lookup_locked(id)->set_history_policy(limit, pixel_budget);
}
PreparedHistoryPolicies Application::prepare_history_policies(
    const std::vector<std::uint64_t> &explicit_ids, HistoryPolicy policy) {
  if (!policy.step_limit)
    throw std::invalid_argument("History limit must be positive");
  auto ids = explicit_ids;
  std::sort(ids.begin(), ids.end());
  if (!ids.empty() && !ids.front())
    throw std::invalid_argument("History policy requires explicit nonzero IDs");
  if (std::adjacent_find(ids.begin(), ids.end()) != ids.end())
    throw std::invalid_argument("History policy ID occurs more than once");
  PreparedHistoryPolicies prepared(std::make_unique<PreparedHistoryPolicies::State>());
  auto &state = *prepared.state_;
  state.owners.reserve(ids.size());
  state.candidates.reserve(ids.size());
  state.document_locks.reserve(ids.size());
  if (ids.empty())
    return prepared;
  state.application_lock = std::unique_lock<std::mutex>(mutex_);
  for (const auto id : ids)
    state.owners.push_back(lookup_locked(id));
  for (const auto &owner : state.owners)
    state.document_locks.emplace_back(owner->mutex_);
  for (const auto &owner : state.owners)
    state.candidates.push_back(owner->prepare_history_policy_locked(policy));
  return prepared;
}
HistoryInfo Application::history(std::uint64_t id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return lookup_locked(id)->history();
}

} // namespace imagejxx
