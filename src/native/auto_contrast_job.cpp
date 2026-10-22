#include <imagejxx/application.hpp>
#include <imagejxx/auto_contrast_job.hpp>

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>

namespace imagejxx {
namespace detail {

// A cycle retains identities and scalar descriptors, never a pixel history,
// frontend, scheduler, result cell or strong Document owner.
struct AutoContrastCycleKey {
  std::weak_ptr<Document> owner;
  std::uint64_t id, pixels, metadata;
  std::shared_ptr<const int> activation;
  PlanePosition position;

  static AutoContrastCycleKey capture(const std::shared_ptr<Document> &owner,
      const DocumentSnapshot &source, std::shared_ptr<const int> activation) {
    const auto revisions = source.revisions();
    return {owner, source.id(), revisions.pixels, revisions.metadata,
      std::move(activation),
      {source.display().t, source.display().c, source.display().crosshair[2]}};
  }
  bool matches(const std::shared_ptr<Document> &current,
      const DocumentSnapshot &source,
      const std::shared_ptr<const int> &active_identity) const noexcept {
    const auto revisions = source.revisions();
    return owner.lock() == current && id == source.id() && activation == active_identity &&
      pixels == revisions.pixels && metadata == revisions.metadata &&
      position.t == source.display().t && position.c == source.display().c &&
      position.z == source.display().crosshair[2];
  }
};

// All access is under the owning Application's membership mutex. Epoch tokens
// are retained identities: no integer wrap or serializable pointer encoding.
struct AutoContrastControllerState {
  std::optional<AutoContrastCycleKey> last;
  int divisor = 0;
  std::shared_ptr<const int> epoch = std::make_shared<const int>(0);

  int effective_divisor(const std::shared_ptr<Document> &owner,
      const DocumentSnapshot &source,
      const std::shared_ptr<const int> &activation) const noexcept {
    return last && last->matches(owner, source, activation) ? divisor : 0;
  }
};

struct AutoContrastSubmission {
  DocumentSnapshot source;
  std::shared_ptr<Document> owner;
  std::shared_ptr<AutoContrastControllerState> controller;
  std::shared_ptr<const int> activation, epoch, replacement_epoch;
  AutoContrastCycleKey key;
  AutoContrastRequest request;
  AutoContrastLimits limits;
  std::optional<DocumentSnapshot> calculation_source;
};

} // namespace detail
namespace {

static_assert(std::is_nothrow_move_constructible_v<ResolvedAutoContrastCommand>);
static_assert(std::is_nothrow_move_assignable_v<ResolvedAutoContrastCommand>);
static_assert(std::is_nothrow_copy_constructible_v<ResolvedAutoContrastCommand>);
// Map-backed snapshot moves can allocate on MSVC. Captures are staged before
// publication and outside the Application lock's destruction scope; only the
// immutable command and scalar/identity key need non-throwing final moves.
static_assert(std::is_nothrow_copy_constructible_v<detail::AutoContrastCycleKey>);

std::size_t add(std::size_t a, std::size_t b) {
  if (b > std::numeric_limits<std::size_t>::max() - a)
    throw std::invalid_argument("Auto contrast job reservation overflows");
  return a + b;
}
std::size_t multiply(std::size_t a, std::size_t b) {
  if (a && b > std::numeric_limits<std::size_t>::max() / a)
    throw std::invalid_argument("Auto contrast job reservation overflows");
  return a * b;
}
void validate_request_header(const AutoContrastRequest &request,
                             const AutoContrastLimits &limits) {
  switch (request.previous_divisor) {
  case 0: case 5000: case 2500: case 1250: case 625: case 312:
  case 156: case 78: case 39: case 19: case 9: break;
  default: throw std::invalid_argument("Invalid Auto contrast divisor");
  }
  switch (request.scope) {
  case AutoContrastScope::ActiveChannel: case AutoContrastScope::CompositeAll: break;
  default: throw std::invalid_argument("Invalid Auto contrast scope");
  }
  switch (request.roi) {
  case AutoContrastRoiPolicy::ActiveSelection: case AutoContrastRoiPolicy::WholePlane: break;
  default: throw std::invalid_argument("Invalid Auto contrast ROI policy");
  }
  switch (request.unsigned16_bits) {
  case 0: case 8: case 10: case 12: case 14: case 15: case 16: break;
  default: throw std::invalid_argument("Invalid Auto contrast unsigned16 bits");
  }
  if (!limits.max_pixels || !limits.max_working_bytes ||
      !limits.max_record_bytes || !limits.max_selection_vertices)
    throw std::invalid_argument("Auto contrast limits must be positive");
}
void normalize_options(JobOptions &options, const AutoContrastEstimate &estimate,
                       std::uint64_t id, std::size_t cell_bytes) {
  // The command estimator covers its own work. Add the simultaneous captured
  // submission, admission/publication snapshot copies, typed result/cell and
  // scheduler/task/function holders. Allocator bookkeeping and opaque callable
  // target storage retain their documented exclusions.
  auto bytes = add(estimate.working_bytes, multiply(3, estimate.display_bytes));
  bytes = add(bytes, sizeof(detail::AutoContrastSubmission));
  bytes = add(bytes, sizeof(detail::AutoContrastControllerState));
  bytes = add(bytes, cell_bytes);
  bytes = add(bytes, sizeof(AutoContrastJob) + sizeof(JobOptions) +
      sizeof(Job) + sizeof(JobContext) + sizeof(AutoContrastEstimate) +
      2 * sizeof(std::function<void(const JobContext &)>) +
      2 * sizeof(std::function<void()>) + 4 * sizeof(std::shared_ptr<const int>) +
      3 * sizeof(int));
  if (options.label.empty()) options.label = "auto_contrast";
  if (options.supersedes.empty()) options.supersedes = "auto_contrast";
  bytes = add(bytes, multiply(4, add(options.label.size(), 1)));
  bytes = add(bytes, multiply(2, add(options.supersedes.size(), 1)));
  options.resource_bytes = std::max(options.resource_bytes, bytes);
  options.document_id = id;
  options.retain_result_resources = true;
}
void require_capture(const detail::AutoContrastSubmission &captured,
    const std::shared_ptr<Document> &owner, const DocumentSnapshot &current,
    std::uint64_t active, const std::shared_ptr<const int> &activation,
    const std::shared_ptr<detail::AutoContrastControllerState> &controller) {
  if (owner != captured.owner || !current.same_document(captured.source))
    throw DocumentUnavailable();
  if (active != captured.source.id() || activation != captured.activation ||
      controller != captured.controller || controller->epoch != captured.epoch ||
      controller->effective_divisor(owner, current, activation) != captured.request.previous_divisor ||
      !current.revisions().matches(captured.source.revisions(), Change::All))
    throw RevisionConflict();
}

} // namespace

AutoContrastJob::ResultCell::~ResultCell() {
  if (auto state = job.lock()) Job(std::move(state)).cancel();
}
void AutoContrastJob::ResultCell::store(ResolvedAutoContrastCommand command) {
  std::lock_guard<std::mutex> lock(mutex);
  if (value || !submission)
    throw std::invalid_argument("Auto contrast result is already stored or cleaned");
  value.emplace(std::move(command));
}
ResolvedAutoContrastCommand AutoContrastJob::ResultCell::get() const {
  std::lock_guard<std::mutex> lock(mutex);
  if (!value) throw std::invalid_argument("Auto contrast job has no retained result");
  return *value;
}
void AutoContrastJob::ResultCell::clear() noexcept {
  std::optional<ResolvedAutoContrastCommand> retired;
  std::shared_ptr<const detail::AutoContrastSubmission> retired_submission;
  {
    std::lock_guard<std::mutex> lock(mutex);
    retired.swap(value);
    retired_submission.swap(submission);
  }
  // Resource guards, source/provider captures and Document owners retire after
  // releasing the cell mutex, and scheduler terminal cleanup holds no app lock.
}
void AutoContrastJob::ResultCell::bind_job(const Job &handle) {
  std::lock_guard<std::mutex> lock(mutex);
  job = handle.state_;
}
std::function<void()> AutoContrastJob::ResultCell::cleanup_callback() {
  return [weak = weak_from_this()] {
    if (auto result = weak.lock()) result->clear();
  };
}
ResolvedAutoContrastCommand AutoContrastJob::resolved() const {
  std::optional<ResolvedAutoContrastCommand> result;
  job_.inspect_retained_result([&] {
    if (!result_) throw std::invalid_argument("Empty Auto contrast result cell");
    result.emplace(result_->get());
  });
  return std::move(*result);
}

AutoContrastJob Application::submit_auto_contrast(const AutoContrastOptions &options,
    const AutoContrastLimits &limits,JobOptions job_options){
  return submit_auto_contrast_impl(options,limits,std::move(job_options),nullptr);
}
AutoContrastJob Application::submit_seeded_auto_contrast(const ChannelDisplayState &initial,
    const AutoContrastOptions &options,const AutoContrastLimits &limits,JobOptions job_options){
  return submit_auto_contrast_impl(options,limits,std::move(job_options),&initial);
}
AutoContrastJob Application::submit_auto_contrast_impl(const AutoContrastOptions &options,
    const AutoContrastLimits &limits,JobOptions job_options,const ChannelDisplayState *initial) {
  const AutoContrastOptions frozen_options = options;
  const AutoContrastLimits frozen_limits = limits;
  AutoContrastRequest request{0, frozen_options.scope, frozen_options.roi, frozen_options.unsigned16_bits};
  validate_request_header(request, frozen_limits);
  // Stage identity allocations before publishing even the initial private
  // controller holder. Failed work never replaces its last successful key.
  auto staged_controller = std::make_shared<detail::AutoContrastControllerState>();
  auto replacement_epoch = std::make_shared<const int>(0);
  std::shared_ptr<Document> captured_owner;
  std::optional<DocumentSnapshot> captured_source;
  std::shared_ptr<detail::AutoContrastControllerState> captured_controller;
  std::shared_ptr<const int> captured_activation, captured_epoch;
  std::optional<detail::AutoContrastCycleKey> captured_key;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    captured_owner = lookup_locked(0);
    captured_source.emplace(captured_owner->snapshot());
    captured_controller = auto_contrast_ ? auto_contrast_ : staged_controller;
    captured_activation = activation_identity_;
    captured_epoch = captured_controller->epoch;
    request.previous_divisor = captured_controller->effective_divisor(
        captured_owner, *captured_source, captured_activation);
    captured_key.emplace(detail::AutoContrastCycleKey::capture(
        captured_owner, *captured_source, captured_activation));
    if (!auto_contrast_) auto_contrast_ = captured_controller;
  }
  std::optional<DocumentSnapshot> calculation;
  if(initial){
    if(captured_source->display().properties.count("channel_display"))
      throw std::invalid_argument("Seeded Auto requires an absent channel display property");
    // Charge typed input and a conservative wire allowance before cloning the
    // seed or allocating its encoded display tree. Caller-owned input, like
    // other frontend argument copies, is outside scheduler reservations.
    auto seed_bytes=add(4096,multiply(initial->channels.size(),sizeof(ChannelDisplayChannel)));
    for(const auto &channel:initial->channels){
      seed_bytes=add(seed_bytes,add(2048,channel.lut.name.size()));
      if(channel.lut.table)seed_bytes=add(seed_bytes,1048576);
    }
    if(seed_bytes>frozen_limits.max_record_bytes || seed_bytes>frozen_limits.max_working_bytes/8)
      throw std::invalid_argument("Auto initial display exceeds metadata budget");
    const auto seed=resolve_channel_display_command(*captured_source,
        {ChannelDisplayAction::Set,*initial,{}},
        {frozen_limits.max_pixels,frozen_limits.max_working_bytes});
    calculation=captured_source->with_display(command_display_state(seed,*captured_source));
  }
  auto captured = std::make_shared<const detail::AutoContrastSubmission>(
      detail::AutoContrastSubmission{std::move(*captured_source), std::move(captured_owner),
        std::move(captured_controller), std::move(captured_activation), std::move(captured_epoch),
        std::move(replacement_epoch), std::move(*captured_key), request, frozen_limits,std::move(calculation)});
  const auto &input=captured->calculation_source?*captured->calculation_source:captured->source;
  const auto estimate = estimate_auto_contrast_command(input, captured->request, captured->limits);
  normalize_options(job_options, estimate, captured->source.id(), sizeof(AutoContrastJob::ResultCell));
  auto result = std::make_shared<AutoContrastJob::ResultCell>(captured);
  auto scheduler = job_scheduler();
  const std::weak_ptr<AutoContrastJob::ResultCell> weak_result = result;
  // No strong task->cell edge: dropping the last user handle cancels queued or
  // running work immediately through ResultCell's weak Job state reference.
  auto job = scheduler->submit_with_cleanup(
      [weak_result, captured](const JobContext &context) {
        context.checkpoint();
        auto command = resolve_auto_contrast_command(captured->calculation_source?*captured->calculation_source:captured->source, captured->request,
            captured->limits, context.execution_control());
        command.resource_guard_ = context.result_resource_guard();
        context.checkpoint();
        auto cell = weak_result.lock();
        if (!cell) throw OperationCancelled();
        cell->store(std::move(command));
      }, std::move(job_options), result->cleanup_callback());
  result->bind_job(job);
  std::shared_ptr<Document> inspected_owner;
  std::optional<DocumentSnapshot> inspected_source;
  try {
    std::lock_guard<std::mutex> lock(mutex_);
    inspected_owner = lookup_locked(captured->source.id());
    inspected_source.emplace(inspected_owner->snapshot());
    require_capture(*captured, inspected_owner, *inspected_source, active_, activation_identity_, auto_contrast_);
  } catch (...) {
    job.cancel();
    throw;
  }
  return AutoContrastJob(std::move(job), std::move(result), job_identity_, std::move(captured));
}

AutoContrastJob Application::submit_rebound_auto_contrast(const MetadataObject &original,
    const AutoContrastRequest &complete_request, const AutoContrastLimits &limits,
    JobOptions job_options) {
  // Validate every original claim before reading replacement request fields or
  // consulting the destination Application owner. No original effect is used
  // as an implicit replacement default or retained worker input.
  validate_auto_contrast_command_record(original, limits);
  const AutoContrastRequest request = complete_request;
  const AutoContrastLimits frozen_limits = limits;
  validate_request_header(request, frozen_limits);
  auto staged_controller = std::make_shared<detail::AutoContrastControllerState>();
  auto replacement_epoch = std::make_shared<const int>(0);
  std::shared_ptr<Document> captured_owner;
  std::optional<DocumentSnapshot> captured_source;
  std::shared_ptr<detail::AutoContrastControllerState> captured_controller;
  std::shared_ptr<const int> captured_activation, captured_epoch;
  std::optional<detail::AutoContrastCycleKey> captured_key;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    captured_owner = lookup_locked(0);
    captured_source.emplace(captured_owner->snapshot());
    captured_controller = auto_contrast_ ? auto_contrast_ : staged_controller;
    captured_activation = activation_identity_;
    captured_epoch = captured_controller->epoch;
    if (request.previous_divisor != captured_controller->effective_divisor(
          captured_owner, *captured_source, captured_activation))
      throw RevisionConflict();
    captured_key.emplace(detail::AutoContrastCycleKey::capture(
        captured_owner, *captured_source, captured_activation));
    if (!auto_contrast_) auto_contrast_ = captured_controller;
  }
  auto captured = std::make_shared<const detail::AutoContrastSubmission>(
      detail::AutoContrastSubmission{std::move(*captured_source), std::move(captured_owner),
        std::move(captured_controller), std::move(captured_activation), std::move(captured_epoch),
        std::move(replacement_epoch), std::move(*captured_key), request, frozen_limits});
  const auto estimate = estimate_auto_contrast_command(captured->source, captured->request, captured->limits);
  normalize_options(job_options, estimate, captured->source.id(), sizeof(AutoContrastJob::ResultCell));
  auto result = std::make_shared<AutoContrastJob::ResultCell>(captured);
  auto scheduler = job_scheduler();
  const std::weak_ptr<AutoContrastJob::ResultCell> weak_result = result;
  auto job = scheduler->submit_with_cleanup(
      [weak_result, captured](const JobContext &context) {
        context.checkpoint();
        auto command = resolve_auto_contrast_command(captured->source, captured->request,
            captured->limits, context.execution_control());
        command.resource_guard_ = context.result_resource_guard();
        context.checkpoint();
        auto cell = weak_result.lock();
        if (!cell) throw OperationCancelled();
        cell->store(std::move(command));
      }, std::move(job_options), result->cleanup_callback());
  result->bind_job(job);
  std::shared_ptr<Document> inspected_owner;
  std::optional<DocumentSnapshot> inspected_source;
  try {
    std::lock_guard<std::mutex> lock(mutex_);
    inspected_owner = lookup_locked(captured->source.id());
    inspected_source.emplace(inspected_owner->snapshot());
    require_capture(*captured, inspected_owner, *inspected_source, active_, activation_identity_, auto_contrast_);
  } catch (...) {
    job.cancel();
    throw;
  }
  return AutoContrastJob(std::move(job), std::move(result), job_identity_, std::move(captured));
}

ResolvedAutoContrastCommand Application::publish_auto_contrast(AutoContrastJob &handle) {
  // Foreign handles are rejected before resolving their results or inspecting
  // this application's current owner/cycle.
  auto identity = handle.application_.lock();
  if (!identity || identity != job_identity_) throw DocumentUnavailable();
  auto command = handle.resolved();
  auto captured = handle.submission_.lock();
  if (!captured) throw std::invalid_argument("Auto contrast submission was cleaned");
  auto proposed = command_display_state(command, captured->source);
  // command, proposed display, capture/key and replacement epoch are all owned
  // before Job.complete arbitrates READY publication versus cancellation.
  handle.job_.complete([&] {
    std::lock_guard<std::mutex> lock(mutex_);
    auto owner = lookup_locked(captured->source.id());
    // Document's guarded setter supplies the authoritative current all-four
    // check. Avoid allocating/copying a new snapshot in the publication lane.
    require_capture(*captured, owner, captured->source, active_, activation_identity_, auto_contrast_);
    owner->set_display_if_current(captured->source, std::move(proposed));
    // No allocations, frontend calls or user callbacks after display mutation.
    captured->controller->last.emplace(captured->key);
    captured->controller->divisor = command.cycle().next;
    captured->controller->epoch = captured->replacement_epoch;
  });
  return command;
}

ResolvedAutoContrastCommand Application::replay_auto_contrast(const MetadataObject &record,
    const AutoContrastLimits &limits, std::uint64_t id) {
  validate_auto_contrast_command_record(record, limits);
  struct ReplayCapture {
    std::shared_ptr<Document> owner;
    DocumentSnapshot source;
    std::shared_ptr<const int> activation;
  };
  std::optional<ReplayCapture> captured;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto owner = lookup_locked(id);
    if (owner->id != active_) throw RevisionConflict();
    captured.emplace(ReplayCapture{owner, owner->snapshot(), activation_identity_});
  }
  auto command = auto_contrast_command_from_record(captured->source, record, limits);
  auto proposed = command_display_state(command, captured->source);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto owner = lookup_locked(captured->source.id());
    if (owner != captured->owner)
      throw DocumentUnavailable();
    if (active_ != captured->source.id() || activation_identity_ != captured->activation)
      throw RevisionConflict();
    owner->set_display_if_current(captured->source, std::move(proposed));
  }
  // Historical cycle/Selection/Display provenance stays in the returned record;
  // live cycle state/epoch and current navigation are never restored/advanced.
  return command;
}

int Application::auto_contrast_divisor() const {
  std::shared_ptr<Document> owner;
  std::optional<DocumentSnapshot> source;
  int result = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto_contrast_ && active_) {
      owner = lookup_locked(0);
      source.emplace(owner->snapshot());
      result = auto_contrast_->effective_divisor(owner, *source, activation_identity_);
    }
  }
  return result;
}

void Application::reset_auto_contrast_cycle() {
  auto staged = std::make_shared<detail::AutoContrastControllerState>();
  auto epoch = std::make_shared<const int>(0);
  std::lock_guard<std::mutex> lock(mutex_);
  if (!auto_contrast_) auto_contrast_ = std::move(staged);
  auto_contrast_->last.reset();
  auto_contrast_->divisor = 0;
  auto_contrast_->epoch = std::move(epoch);
}

void Application::set_display_and_reset_auto_contrast(
    const DocumentSnapshot &expected, DisplayState display) {
  auto staged = std::make_shared<detail::AutoContrastControllerState>();
  auto epoch = std::make_shared<const int>(0);
  std::lock_guard<std::mutex> lock(mutex_);
  auto owner = lookup_locked(expected.id());
  owner->set_display_if_current(expected, std::move(display));
  // All potentially throwing work is complete. Do not invalidate an unrelated
  // active owner's Auto capture when resetting an inactive document's range.
  if (active_ == expected.id()) {
    if (!auto_contrast_) auto_contrast_ = std::move(staged);
    auto_contrast_->last.reset();
    auto_contrast_->divisor = 0;
    auto_contrast_->epoch = std::move(epoch);
  }
}

} // namespace imagejxx
