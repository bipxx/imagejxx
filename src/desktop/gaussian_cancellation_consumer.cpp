#include "gaussian_cancellation_internal.hpp"
#include "../native/operation_job_probe_internal.hpp"
#include <imagejxx/gaussian.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace imagejxx::desktop {
namespace {
using namespace std::chrono_literals;
constexpr int width = 173, height = 101;
constexpr const char *fixtureId = "H1-owned-f32-173x101";

void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}

std::string threadId() {
  std::ostringstream out;
  out << std::this_thread::get_id();
  return out.str();
}

const char *stateName(JobState state) {
  switch (state) {
  case JobState::Queued: return "Queued";
  case JobState::Running: return "Running";
  case JobState::Ready: return "Ready";
  case JobState::Publishing: return "Publishing";
  case JobState::Completed: return "Completed";
  case JobState::Failed: return "Failed";
  case JobState::Canceled: return "Canceled";
  }
  throw std::logic_error("Unknown native JobState");
}

Json jobJson(const JobInfo &info) {
  return {{"id", info.id}, {"document_id", info.document_id},
          {"label", info.label}, {"state", stateName(info.state)},
          {"cancel_requested", info.cancel_requested}, {"error", info.error},
          {"resource_bytes", info.resource_bytes}, {"completed", info.completed},
          {"total", info.total}, {"checkpoints", info.checkpoints},
          {"stage", info.stage}, {"stage_completed", info.stage_completed},
          {"stage_total", info.stage_total}};
}

Json usageJson(const JobUsage &usage) {
  return {{"running", usage.running}, {"queued", usage.queued},
          {"running_bytes", usage.running_bytes},
          {"retained_bytes", usage.retained_bytes},
          {"reserved_bytes", usage.reserved_bytes}};
}

Json scalarJobs(const std::shared_ptr<JobScheduler> &scheduler) {
  Json jobs = Json::array();
  if (scheduler)
    for (const auto &info : scheduler->jobs()) jobs.push_back(jobJson(info));
  return jobs;
}

Image ownedInput() {
  Image image(width, height, 1, PixelType::Float32);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
      image.at(x, y) = (37 * x + 61 * y + 13 * x * y) / 8.0 - .125;
  return image;
}

std::vector<unsigned char> rawFloat32(const Image &image) {
  static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
                "H1 fixture serialization requires IEEE754 Float32");
  require(image.type == PixelType::Float32, "H1 raw serialization requires Float32");
  std::vector<unsigned char> bytes;
  bytes.reserve(image.pixels.size() * 4);
  for (const double value : image.pixels) {
    const float sample = static_cast<float>(value);
    std::uint32_t bits;
    std::memcpy(&bits, &sample, sizeof(bits));
    for (int shift = 0; shift < 32; shift += 8)
      bytes.push_back(static_cast<unsigned char>(bits >> shift));
  }
  return bytes;
}

// SHA-256 is used only to identify emitted owned observations. Independent
// assertions reconstruct and hash the fixture without trusting these digests.
std::string sha256(const std::vector<unsigned char> &input) {
  constexpr std::array<std::uint32_t, 64> constants{
      0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
      0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
      0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
      0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
      0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
      0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
      0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
      0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
  std::array<std::uint32_t, 8> hash{
      0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
      0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  auto bytes = input;
  bytes.push_back(0x80);
  while (bytes.size() % 64 != 56) bytes.push_back(0);
  const auto bitCount = std::uint64_t(input.size()) * 8;
  for (int shift = 56; shift >= 0; shift -= 8)
    bytes.push_back(static_cast<unsigned char>(bitCount >> shift));
  auto rotate = [](std::uint32_t value, int count) {
    return (value >> count) | (value << (32 - count));
  };
  for (std::size_t offset = 0; offset < bytes.size(); offset += 64) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t i = 0; i < 16; ++i)
      for (std::size_t j = 0; j < 4; ++j)
        words[i] = (words[i] << 8) | bytes[offset + i * 4 + j];
    for (std::size_t i = 16; i < words.size(); ++i) {
      const auto a = words[i - 15], b = words[i - 2];
      words[i] = words[i - 16] + (rotate(a,7) ^ rotate(a,18) ^ (a >> 3)) +
                 words[i - 7] + (rotate(b,17) ^ rotate(b,19) ^ (b >> 10));
    }
    auto work = hash;
    for (std::size_t i = 0; i < words.size(); ++i) {
      const auto first = work[7] + (rotate(work[4],6) ^ rotate(work[4],11) ^ rotate(work[4],25)) +
          ((work[4] & work[5]) ^ (~work[4] & work[6])) + constants[i] + words[i];
      const auto second = (rotate(work[0],2) ^ rotate(work[0],13) ^ rotate(work[0],22)) +
          ((work[0] & work[1]) ^ (work[0] & work[2]) ^ (work[1] & work[2]));
      for (std::size_t j = 7; j > 0; --j) work[j] = work[j - 1];
      work[4] += first;
      work[0] = first + second;
    }
    for (std::size_t i = 0; i < hash.size(); ++i) hash[i] += work[i];
  }
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (const auto word : hash) out << std::setw(8) << word;
  return out.str();
}

Json rawMember(const std::filesystem::path &directory,
               const std::string &member, const Image &image) {
  const auto bytes = rawFloat32(image);
  const auto path = directory / member;
  require(!std::filesystem::exists(path), "H1 raw observation already exists");
  writeBytes(path.u8string(), bytes);
  return {{"member_id", member}, {"path", path.u8string()},
          {"encoding", "IEEE754 Float32LE"}, {"shape", {image.height, image.width}},
          {"dtype", "float32"}, {"sample_count", image.pixels.size()},
          {"byte_count", bytes.size()}, {"sha256", sha256(bytes)}};
}

std::filesystem::path freshDirectory(const std::string &directory) {
  require(!directory.empty(), "H1 output directory is required");
  const auto path = std::filesystem::absolute(std::filesystem::u8path(directory)).lexically_normal();
  require(path != path.root_path(), "H1 cannot own a filesystem root");
  std::filesystem::create_directories(path.parent_path());
  require(std::filesystem::create_directory(path), "H1 output directory must be fresh");
  return path;
}

int caseNumber(const std::string &id) {
  for (int i = 1; i <= 12; ++i) {
    std::ostringstream name;
    name << "H1-" << std::setw(2) << std::setfill('0') << i;
    if (id == name.str()) return i;
  }
  throw std::invalid_argument("Unknown fixed H1 case ID");
}

struct CancellationActorGuard {
  std::shared_ptr<GaussianCancellationSession> session;
  std::thread &actor;
  ~CancellationActorGuard() {
    if (actor.joinable()) {
      session->abort();
      actor.join();
    }
  }
};
} // namespace

struct GaussianCancellationSession::State {
  mutable std::mutex mutex;
  std::condition_variable changed;
  std::shared_ptr<JobScheduler> scheduler;
  std::string creator_thread, canceller_thread, case_id, context;
  Json events = Json::array(), timeouts = Json::array();
  bool configured = false, point = false, entered = false, returned = false,
       aborted = false, worker_entered = false, worker_released = false,
       blocker_entered = false, blocker_released = false, preparation_released = false;
  std::uint64_t cancel_calls = 0;

  void appendLocked(Json event) {
    event["sequence"] = events.size();
    event["thread_id"] = threadId();
    events.push_back(std::move(event));
  }
  void append(Json event) {
    std::lock_guard<std::mutex> lock(mutex);
    appendLocked(std::move(event));
  }
  void timeoutLocked(const char *phase) {
    timeouts.push_back(phase);
    appendLocked({{"phase", "timeout"}, {"boundary", phase}});
  }
  void cancelPoint(const char *boundary) {
    std::lock_guard<std::mutex> lock(mutex);
    require(!point, "H1 session already signaled its sole cancellation point");
    appendLocked({{"phase", "cancel_point"}, {"boundary", boundary}, {"role", "host"}});
    point = true;
    changed.notify_all();
  }
  void awaitReturned() {
    std::unique_lock<std::mutex> lock(mutex);
    if (!changed.wait_for(lock, 5s, [&] { return returned || aborted; })) {
      timeoutLocked("cancel_returned");
      throw std::runtime_error("H1 off-thread cancellation did not return within 5000 ms");
    }
    require(returned && !aborted, "H1 external cancellation actor aborted");
  }
};

GaussianCancellationSession::GaussianCancellationSession()
    : state_(std::make_shared<State>()) {}
GaussianCancellationSession::~GaussianCancellationSession() { abort(); }

bool GaussianCancellationSession::waitForCancelPoint(int timeout_ms) {
  if (timeout_ms < 1 || timeout_ms > 60000)
    throw std::invalid_argument("H1 cancellation wait must be 1..60000 ms");
  std::unique_lock<std::mutex> lock(state_->mutex);
  if (!state_->changed.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                              [&] { return state_->point || state_->aborted; })) {
    state_->timeoutLocked("wait_for_cancel_point");
    return false;
  }
  return state_->point && !state_->aborted;
}
void GaussianCancellationSession::cancelEntered() {
  std::shared_ptr<JobScheduler> scheduler;
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    require(state_->configured && state_->point && !state_->entered && !state_->aborted,
            "H1 cancelEntered requires a fresh active cancellation point");
    require(threadId() != state_->creator_thread, "H1 canceller must run off the creator thread");
    state_->canceller_thread = threadId();
    state_->entered = true;
    ++state_->cancel_calls;
    scheduler = state_->scheduler;
  }
  state_->append({{"phase", "off_thread_window_cancel_entered"}, {"role", "canceller"},
                  {"jobs", scalarJobs(scheduler)}, {"usage", usageJson(scheduler->usage())}});
}
void GaussianCancellationSession::cancelReturned() {
  std::shared_ptr<JobScheduler> scheduler;
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    require(state_->entered && !state_->returned && !state_->aborted &&
                threadId() == state_->canceller_thread,
            "H1 cancelReturned must follow the same external cancellation actor");
    scheduler = state_->scheduler;
  }
  const auto jobs = scalarJobs(scheduler);
  const auto usage = usageJson(scheduler->usage());
  std::lock_guard<std::mutex> lock(state_->mutex);
  state_->appendLocked({{"phase", "off_thread_window_cancel_returned"}, {"role", "canceller"},
                         {"jobs", jobs}, {"usage", usage}});
  state_->returned = true;
  state_->changed.notify_all();
}
void GaussianCancellationSession::abort() {
  std::lock_guard<std::mutex> lock(state_->mutex);
  if (!state_->aborted) {
    state_->appendLocked({{"phase", "session_abort"}});
    state_->aborted = true;
  }
  state_->worker_released = state_->blocker_released = state_->preparation_released = true;
  state_->changed.notify_all();
}
Json GaussianCancellationSession::diagnostics() const {
  std::lock_guard<std::mutex> lock(state_->mutex);
  return {{"case_id", state_->case_id}, {"context", state_->context},
          {"creator_thread", state_->creator_thread}, {"canceller_thread", state_->canceller_thread},
          {"cancel_point_reached", state_->point}, {"cancel_entered", state_->entered},
          {"cancel_returned", state_->returned}, {"cancel_calls", state_->cancel_calls},
          {"aborted", state_->aborted}, {"timeouts", state_->timeouts},
          {"ordered_probe_events", state_->events}};
}

class GaussianCancellationDriver {
public:
  GaussianCancellationDriver(Window &window, std::string caseId,
                             std::string context,
                             std::shared_ptr<GaussianCancellationSession> session,
                             std::filesystem::path directory)
      : window_(window), number_(caseNumber(caseId)),
        preview_(number_ == 1 || number_ == 3 || number_ == 5 || number_ == 7),
        session_(std::move(session)), state_(session_->state_),
        scheduler_(GaussianCancellationAccess::scheduler(window_)),
        directory_(std::move(directory)) {
    report_ = {{"schema_version", 1}, {"contract_id", "p02a.gaussian.h1-races.001"},
               {"status", "unverified"}, {"acceptance_claim", false},
               {"case_id", caseId}, {"context", context}, {"fixture_id", fixtureId},
               {"api_inputs", Json::array()}, {"input_events", Json::array()},
               {"job_observations", Json::array()}, {"resource_checkpoints", Json::array()},
               {"host_checkpoints", Json::array()}, {"member_ids", Json::array()},
               {"timeouts_or_failure", Json::array()}};
    std::lock_guard<std::mutex> lock(state_->mutex);
    require(!state_->configured && !state_->point && !state_->aborted,
            "H1 requires a fresh cancellation session");
    state_->configured = true;
    state_->scheduler = scheduler_;
    state_->creator_thread = threadId();
    state_->case_id = std::move(caseId);
    state_->context = std::move(context);
  }

  ~GaussianCancellationDriver() {
    if (!cleaned_) cleanupFailure();
    try { GaussianCancellationAccess::setHostProbe(window_, {}); } catch (...) {}
  }

  Json run() {
    try {
      prepare();
      if (number_ <= 2) startBlocker();
      if (number_ == 11) installValidator();
      GaussianCancellationAccess::setHostProbe(window_,
          [this](const GaussianHostProbeEvent &event) { hostProbe(event); });
      request();
      const auto firstId = actualJobs_.front().info().id;
      report_["first_native_job_id"] = firstId;

      if (number_ <= 4) {
        if (number_ >= 3) awaitWorkerGate();
        checkpoint("before_cancel");
        const auto info = actualJobs_.back().info();
        observeJob("before_cancel", info);
        require(info.state == (number_ <= 2 ? JobState::Queued : JobState::Running),
                "H1 real native job did not reach its declared queued/running boundary");
        if (number_ >= 3)
          require(info.stage == "ImageJ Gaussian source copy" &&
                      info.stage_completed == 1024 && info.stage_total == 17473,
                  "H1 running barrier did not observe actual positive source-copy progress");
        const auto host = GaussianCancellationAccess::diagnostics(window_);
        require(host.at("mirrored_ids").at(preview_ ? 0 : 1) == firstId,
                "H1 actual job ID was not mirrored before cancellation");
        state_->cancelPoint(number_ <= 2 ? "queued" : "positive_native_source_copy_progress");
        state_->awaitReturned();
        checkpoint("after_cancel_while_gate_held");
        observeJob("after_cancel_while_gate_held", actualJobs_.back().info());
        if (number_ <= 2) {
          require(actualJobs_.back().info().state == JobState::Canceled,
                  "H1 queued native job was not canceled before blocker release");
          const bool accepted = blocker_->cancel();
          state_->append({{"phase", "blocker_cancel_requested"}, {"role", "host"},
                           {"accepted", accepted}, {"job", jobJson(blocker_->info())}});
        } else {
          require(actualJobs_.back().info().cancel_requested,
                  "H1 running native cancellation flag was not set while its gate was held");
        }
        releaseWorkerGates();
      } else if (number_ == 9) {
        waitActual();
        checkpoint("before_cancel");
        observeJob("finalized_ready_before_cancel", actualJobs_.back().info());
        require(actualJobs_.back().info().state == JobState::Ready &&
                    scheduler_->usage().retained_bytes > 0,
                "H1 Ready Apply must own its actual retained reservation before cancellation");
        state_->cancelPoint("mirrored_ready_before_host_poll");
        state_->awaitReturned();
        checkpoint("after_cancel_while_gate_held");
        observeJob("after_cancel_before_host_poll", actualJobs_.back().info());
      } else if (number_ == 10 || number_ == 11) {
        waitActual();
        checkpoint("ready_before_host_poll");
        observeJob("finalized_ready_before_host_poll", actualJobs_.back().info());
        require(actualJobs_.back().info().state == JobState::Ready,
                "H1 publication boundary requires a real finalized Ready Apply");
        window_.poll();
      }

      waitActual();
      if (blocker_)
        require(blocker_->wait_for(5s), "H1 real blocker did not finalize within 5000 ms");
      checkpoint("after_native_worker_finalization");
      report_["first_terminal_job"] = jobJson(actualJobs_.front().info());
      report_["terminal_job"] = jobJson(actualJobs_.back().info());
      observeJob("native_finalized", actualJobs_.back().info());
      window_.poll();
      checkpoint("after_host_drain");

      if (number_ == 12) {
        report_["before_second_admission"] = sourceState("source-before-second.f32le");
        requireUnchanged(report_.at("before_second_admission"));
        const auto drained = GaussianCancellationAccess::diagnostics(window_);
        require(drained.at("drained_epoch") == drained.at("current_epoch"),
                "H1 first cancel epoch was not drained before a fresh admission");
        dropJobAliases();
        checkpoint("first_cleanup");
        requireZeroUsage();
        ++requestIndex_;
        request();
        const auto secondId = actualJobs_.back().info().id;
        report_["second_native_job_id"] = secondId;
        require(secondId > 0 && secondId != firstId,
                "H1 new admission reused or fabricated its actual native ID");
        waitActual();
        observeJob("second_finalized_ready", actualJobs_.back().info());
        window_.poll();
        report_["terminal_job"] = jobJson(actualJobs_.back().info());
        observeJob("second_published", actualJobs_.back().info());
      }

      report_["after"] = sourceState("source-after.f32le");
      report_["publication_callback_count"] =
          GaussianCancellationAccess::diagnostics(window_).at("gaussian_jobs").at("apply_callback_count");
      report_["native_publication_succeeded"] =
          report_.at("terminal_job").at("state") == "Completed";
      window_.poll();
      report_["second_poll"] = sourceState("source-second-poll.f32le");
      report_["second_poll_callback_count"] =
          GaussianCancellationAccess::diagnostics(window_).at("gaussian_jobs").at("apply_callback_count");

      if (number_ == 11 || number_ == 12) {
        require(rawFloat32(*owner_->snapshot().data().dataset.plane({0,0,0})) == rawFloat32(expected_),
                "H1 publishing winner did not produce the exact owned primitive result");
        report_["expected_changed_raw"] = rawMember(directory_, "expected-changed.f32le", expected_);
        api("undo");
        window_.action("undo");
        report_["undo"] = sourceState("source-undo.f32le");
        api("redo");
        window_.action("redo");
        report_["redo"] = sourceState("source-redo.f32le");
      }
      validatorArmed_->store(false);
      validator_.reset();
      dropJobAliases();
      window_.poll();
      checkpoint("after_host_drain_and_output_alias_drop");
      report_["final_cleanup"] = {
          {"host", GaussianCancellationAccess::diagnostics(window_)},
          {"usage", usageJson(scheduler_->usage())}, {"jobs", scalarJobs(scheduler_)},
          {"output_aliases_dropped", actualJobs_.empty() && !blocker_}};
      report_["original_after_raw"] = rawMember(
          directory_, "original-after.f32le", *before_->data().dataset.plane({0,0,0}));
      report_["window_error"] = window_.error();
      cleaned_ = true;
      serializeDiagnostics();
      save("case-unverified.json");
      verify();
      report_["status"] = "passed";
      save("case.json");
      return report_;
    } catch (const std::exception &error) {
      report_["timeouts_or_failure"].push_back(error.what());
      report_["status"] = "failed";
      cleanupFailure();
      try {
        report_["failure_cleanup"] = {
            {"host", GaussianCancellationAccess::diagnostics(window_)},
            {"usage", usageJson(scheduler_->usage())}, {"jobs", scalarJobs(scheduler_)}};
        serializeDiagnostics();
        save("case-failed.json");
      } catch (...) {}
      throw;
    }
  }

private:
  Window &window_;
  const int number_;
  const bool preview_;
  std::shared_ptr<GaussianCancellationSession> session_;
  std::shared_ptr<GaussianCancellationSession::State> state_;
  std::shared_ptr<JobScheduler> scheduler_;
  std::filesystem::path directory_;
  std::shared_ptr<imagejxx::Document> owner_;
  std::optional<DocumentSnapshot> before_;
  Image input_{1, 1, 1, PixelType::Float32}, expected_{1, 1, 1, PixelType::Float32};
  Json report_;
  std::optional<ResolvedGaussianBlurCommand> command_;
  std::vector<OperationJob> actualJobs_;
  std::optional<OperationJob> blocker_;
  std::shared_ptr<std::atomic<bool>> validatorArmed_ = std::make_shared<std::atomic<bool>>(false);
  std::shared_ptr<const DocumentValidator> validator_;
  int requestIndex_ = 0;
  bool hostGateUsed_ = false, cleaned_ = false;
  std::size_t checkpointIndex_ = 0;

  void api(const char *name, const Json &parameters = Json::object()) {
    report_["api_inputs"].push_back({{"method", name}, {"parameters", parameters}});
  }
  void prepare() {
    auto limits = scheduler_->limits();
    limits.workers = 1;
    limits.capacity = std::max<std::size_t>(2, limits.capacity);
    scheduler_->set_limits(limits);
    api("set_scheduler_limits", {{"workers", limits.workers}, {"capacity", limits.capacity},
                                  {"memory_budget", limits.memory_budget}});
    input_ = ownedInput();
    expected_ = gaussian_blur_imagej(input_, 2, 2);
    require(rawFloat32(input_) != rawFloat32(expected_), "H1 sigma 2 must change the owned fixture");
    owner_ = std::make_shared<imagejxx::Document>(Dataset::from_stack({input_}), "Owned H1 Gaussian source");
    window_.addNativeDocument(owner_);
    window_.recording = true;
    api("addNativeDocument", {{"document_id", owner_->id}, {"shape", {1,1,1,height,width,1}},
                              {"dtype", "float32"}, {"recording", true}});
    before_ = owner_->snapshot();
    TargetRequest request;
    request.scope = TargetScope::CurrentPlane;
    request.roi = RoiPolicy::WholePlane;
    command_ = resolve_gaussian_command(*before_, request, {2, false});
    report_["command_record"] = metadataJson(command_->record());
    report_["target"] = metadataJson(command_->target().record());
    report_["before"] = sourceState("source-before.f32le");
    require(owner_->undo_steps() == 0 && owner_->redo_steps() == 0 &&
                window_.workflow.empty() && window_.results.empty(),
            "H1 requires original source/history/workflow/results baselines");
    checkpoint("initial");
  }

  Json sourceState(const std::string &member) {
    const auto snapshot = owner_->snapshot();
    const auto pixels = snapshot.data().dataset.plane({0,0,0});
    const auto info = window_.documentInfo();
    return {{"source_raw", rawMember(directory_, member, *pixels)},
            {"document_id", snapshot.id()},
            {"data_identity", snapshot.data_identity() ? Json(*snapshot.data_identity()) : Json()},
            {"dirty", snapshot.dirty()}, {"revisions", info.at("revisions")},
            {"undo_steps", owner_->undo_steps()}, {"redo_steps", owner_->redo_steps()},
            {"document_info", info}, {"workflow", window_.workflow}, {"results", window_.results}};
  }

  void checkpoint(const char *name) {
    const auto host = GaussianCancellationAccess::diagnostics(window_);
    const auto usage = usageJson(scheduler_->usage());
    report_["host_checkpoints"].push_back({{"name", name}, {"request_index", requestIndex_}, {"host", host}});
    report_["resource_checkpoints"].push_back({{"name", name}, {"request_index", requestIndex_}, {"usage", usage}});
    state_->append({{"phase", "resource_observation"}, {"role", "host"}, {"name", name},
                    {"request_index", requestIndex_}, {"host", host}, {"usage", usage},
                    {"jobs", scalarJobs(scheduler_)}});
    serializeDiagnostics();
    save("diagnostic-" + std::to_string(checkpointIndex_++) + ".json");
  }

  void observeJob(const char *name, const JobInfo &info) {
    Json event{{"phase", "job_observation"}, {"role", "host"},
               {"name", name}, {"job", jobJson(info)}, {"request_index", requestIndex_}};
    report_["job_observations"].push_back(event);
    state_->append(std::move(event));
  }

  detail::OperationJobProbe nativeProbe(bool blocker) {
    const auto state = state_;
    const bool running = number_ == 3 || number_ == 4;
    return [state, blocker, running](const detail::OperationJobProbeEvent &event) {
      if (event.operation != "gaussian_blur_imagej") return;
      state->append({{"phase", std::string(event.phase)}, {"role", "native_worker"},
                     {"lane", blocker ? "blocker" : "window"},
                     {"operation", std::string(event.operation)}, {"source_id", event.source_id},
                     {"stage", std::string(event.stage)}, {"completed", event.completed}, {"total", event.total}});
      if (!blocker && running && event.phase == "worker_entered") {
        std::unique_lock<std::mutex> lock(state->mutex);
        if (!state->changed.wait_for(lock, 5s, [&] { return state->preparation_released || state->aborted; })) {
          state->timeoutLocked("running_request_mirror_before_preparation");
          throw std::runtime_error("H1 running request did not release preparation after mirroring");
        }
        state->appendLocked({{"phase", "preparation_gate_released"}, {"role", "native_worker"}});
      }
      const bool stop = blocker ? event.phase == "worker_entered"
          : running && event.phase == "stage_progress" &&
                event.stage == "ImageJ Gaussian source copy" && event.completed == 1024 && event.total == 17473;
      if (!stop) return;
      std::unique_lock<std::mutex> lock(state->mutex);
      bool &entered = blocker ? state->blocker_entered : state->worker_entered;
      bool &released = blocker ? state->blocker_released : state->worker_released;
      entered = true;
      state->changed.notify_all();
      if (!state->changed.wait_for(lock, 5s, [&] { return released || state->aborted; })) {
        state->timeoutLocked(blocker ? "blocker_worker_gate" : "positive_source_copy_gate");
        throw std::runtime_error("H1 real native worker gate exceeded 5000 ms");
      }
      state->appendLocked({{"phase", "worker_gate_released"}, {"role", "native_worker"},
                            {"lane", blocker ? "blocker" : "window"}});
    };
  }

  void startBlocker() {
    detail::ScopedOperationJobProbe probe(nativeProbe(true));
    blocker_ = GaussianCancellationAccess::application(window_).submit(
        "gaussian_blur_imagej", command_->target(), command_->parameters());
    api("submit_real_gaussian_blocker", {{"job_id", blocker_->info().id}});
    std::unique_lock<std::mutex> lock(state_->mutex);
    if (!state_->changed.wait_for(lock, 5s, [&] { return state_->blocker_entered || state_->aborted; })) {
      state_->timeoutLocked("blocker_worker_entered");
      throw std::runtime_error("H1 real Gaussian blocker did not enter within 5000 ms");
    }
    require(!state_->aborted, "H1 actor aborted before real blocker startup");
    lock.unlock();
    report_["blocker_job_id"] = blocker_->info().id;
    observeJob("blocker_real_worker_entered", blocker_->info());
    require(blocker_->info().state == JobState::Running,
            "H1 blocker was not genuinely Running on its scheduler");
  }

  void awaitWorkerGate() {
    std::unique_lock<std::mutex> lock(state_->mutex);
    if (!state_->changed.wait_for(lock, 5s, [&] { return state_->worker_entered || state_->aborted; })) {
      state_->timeoutLocked("positive_source_copy_gate_entered");
      throw std::runtime_error("H1 real Window worker did not reach positive progress within 5000 ms");
    }
    require(!state_->aborted, "H1 cancellation actor aborted before worker boundary");
  }

  void hostProbe(const GaussianHostProbeEvent &event) {
    state_->append({{"phase", event.phase}, {"role", "host"}, {"request_index", requestIndex_},
                    {"admission_epoch", event.admission_epoch}, {"current_epoch", event.current_epoch},
                    {"drained_epoch", event.drained_epoch}, {"mirrored_ids", event.mirrored_ids},
                    {"actual_lane_ids", event.actual_lane_ids}});
    if (event.phase == "admission_after_submit") {
      auto job = GaussianCancellationAccess::laneJob(window_, preview_);
      require(job.has_value(), "H1 host probe did not expose the actual submitted lane job");
      actualJobs_.push_back(*job);
      observeJob("actual_window_job_submitted", job->info());
    }
    const bool gate = !hostGateUsed_ &&
        (((number_ == 5 || number_ == 6 || number_ == 12) && event.phase == "admission_before_submit") ||
         ((number_ == 7 || number_ == 8) && event.phase == "admission_after_submit") ||
         (number_ == 10 && event.phase == "before_native_publish"));
    if (!gate) return;
    hostGateUsed_ = true;
    checkpoint("before_cancel");
    state_->cancelPoint(event.phase.c_str());
    state_->awaitReturned();
    checkpoint("after_cancel_while_gate_held");
    state_->append({{"phase", "host_gate_released"}, {"boundary", event.phase},
                    {"role", "host"}, {"host", GaussianCancellationAccess::diagnostics(window_)}});
  }

  void request() {
    api("request_gaussian", {{"lane", preview_ ? "preview" : "apply"},
                             {"generation", preview_ ? Json(1) : Json()},
                             {"request_index", requestIndex_}});
    detail::ScopedOperationJobProbe probe(nativeProbe(false));
    GaussianCancellationAccess::request(window_, *command_,
        preview_ ? std::optional<std::uint64_t>(1) : std::nullopt);
    require(!actualJobs_.empty(), "H1 actual native request did not produce a wait handle");
    checkpoint("after_request_and_mirror");
    if (number_ == 3 || number_ == 4) {
      std::lock_guard<std::mutex> lock(state_->mutex);
      state_->preparation_released = true;
      state_->changed.notify_all();
    }
  }

  void installValidator() {
    const auto original = before_->data().dataset;
    const auto armed = validatorArmed_;
    const auto state = state_;
    validator_ = owner_->constrain([original, armed, state](const DocumentData &proposed, const DisplayState &) {
      if (!armed->load() || proposed.dataset.shares_pixels(original) || !armed->exchange(false)) return;
      state->append({{"phase", "validator_gate_entered"}, {"role", "host"},
                     {"changed_dataset", true}});
      state->cancelPoint("existing_document_validator_during_native_publication");
      state->awaitReturned();
      state->append({{"phase", "validator_gate_released"}, {"role", "host"}});
    });
    state_->append({{"phase", "document_validator_registered_disarmed"}, {"role", "host"}});
    validatorArmed_->store(true);
    state_->append({{"phase", "document_validator_armed_for_changed_dataset"}, {"role", "host"}});
  }

  void releaseWorkerGates() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->worker_released = state_->blocker_released = state_->preparation_released = true;
    state_->appendLocked({{"phase", "all_gates_released"}, {"role", "host"}});
    state_->changed.notify_all();
  }
  void waitActual() {
    for (const auto &job : actualJobs_)
      require(job.wait_for(5s), "H1 actual Window job did not finalize within 5000 ms");
  }
  void dropJobAliases() {
    actualJobs_.clear();
    blocker_.reset();
    state_->append({{"phase", "output_aliases_dropped"}, {"role", "host"}});
  }
  void requireZeroUsage() const {
    const auto usage = scheduler_->usage();
    require(usage.running == 0 && usage.queued == 0 && usage.running_bytes == 0 &&
                usage.retained_bytes == 0 && usage.reserved_bytes == 0,
            "H1 actual scheduler resources did not return to zero after final owner drop");
  }
  void requireUnchanged(const Json &observed) const {
    const auto &before = report_.at("before");
    require(observed.at("source_raw").at("sha256") == before.at("source_raw").at("sha256") &&
                observed.at("document_id") == before.at("document_id") &&
                observed.at("data_identity") == before.at("data_identity") &&
                observed.at("revisions") == before.at("revisions") &&
                observed.at("dirty") == before.at("dirty") &&
                observed.at("undo_steps") == before.at("undo_steps") &&
                observed.at("redo_steps") == before.at("redo_steps") &&
                observed.at("workflow") == before.at("workflow") && observed.at("results") == before.at("results"),
            "H1 canceled request changed original bytes/identity/revisions/dirty/history/records");
    require(rawFloat32(*owner_->snapshot().data().dataset.plane({0,0,0})) == rawFloat32(input_),
            "H1 canceled request did not preserve exact original raw pixels");
  }

  void serializeDiagnostics() {
    const auto diagnostics = session_->diagnostics();
    report_["session"] = diagnostics;
    report_["ordered_probe_events"] = diagnostics.at("ordered_probe_events");
    report_["host_and_canceller_thread_roles"] = {
        {"host", diagnostics.at("creator_thread")}, {"canceller", diagnostics.at("canceller_thread")},
        {"context", report_.at("context")}};
    report_["actual_native_jobs"] = scalarJobs(scheduler_);
    report_["external_cancel_api"] = {
        {"method", "Window.cancel"}, {"return_type", "void"},
        {"call_count", diagnostics.at("cancel_calls")},
        {"thread_id", diagnostics.at("canceller_thread")}};
  }
  void save(const std::string &member) {
    const auto path = directory_ / member;
    require(!std::filesystem::exists(path), "H1 diagnostic report member already exists");
    report_["member_ids"].push_back(member);
    writeJson(path.u8string(), report_);
  }

  void verify() {
    const auto &session = report_.at("session");
    require(session.at("cancel_calls") == 1 && session.at("cancel_entered") == true &&
                session.at("cancel_returned") == true && session.at("aborted") == false &&
                session.at("timeouts").empty(), "H1 did not observe one completed genuine external cancel");
    const auto &host = report_.at("final_cleanup").at("host");
    require(host.at("current_epoch") == host.at("drained_epoch") &&
                host.at("current_epoch").get<std::uint64_t>() ==
                    report_.at("host_checkpoints").front().at("host").at("current_epoch").get<std::uint64_t>() + 1 &&
                host.at("mirrored_ids") == Json::array({0,0}) &&
                host.at("actual_lane_ids") == Json::array({0,0}) &&
                host.at("gaussian_jobs").at("active_installed_generation").is_null() &&
                host.at("gaussian_jobs").at("preview").at("owns_job") == false &&
                host.at("gaussian_jobs").at("apply").at("owns_job") == false,
            "H1 host did not drain real cancellation epoch/lane/mirror ownership");
    requireZeroUsage();
    require(rawFloat32(*before_->data().dataset.plane({0,0,0})) == rawFloat32(input_),
            "H1 operation mutated the original immutable source buffers");
    require(report_.at("original_after_raw").at("sha256") ==
                report_.at("before").at("source_raw").at("sha256"),
            "H1 retained original raw observation differs after final work");
    const bool winner = number_ == 11 || number_ == 12;
    require(report_.at("terminal_job").at("state") == (winner ? "Completed" : "Canceled"),
            "H1 native terminal state differs from its declared race outcome");
    require(report_.at("publication_callback_count") == (number_ >= 10 ? 1 : 0) &&
                report_.at("native_publication_succeeded") == winner,
            "H1 real native publication/callback count differs from its race outcome");
    require(report_.at("second_poll").at("source_raw").at("sha256") == report_.at("after").at("source_raw").at("sha256") &&
                report_.at("second_poll_callback_count") == report_.at("publication_callback_count"),
            "H1 second host poll duplicated publication or changed its committed state");
    for (const auto field : {"document_id", "data_identity", "dirty", "revisions", "undo_steps", "redo_steps", "workflow", "results"})
      require(report_.at("second_poll").at(field) == report_.at("after").at(field),
              "H1 second host poll changed committed identity/history/records");
    if (number_ <= 2)
      for (const auto &event : report_.at("ordered_probe_events"))
        require(!(get(event, "phase") == "worker_entered" && get(event, "lane") == "window"),
                "H1 canceled queued Window work entered native preparation");
    if (number_ == 11) {
      for (const auto phase : {"off_thread_window_cancel_entered", "off_thread_window_cancel_returned"}) {
        bool observedPublishing = false;
        for (const auto &event : report_.at("ordered_probe_events"))
          if (get(event, "phase") == phase)
            for (const auto &job : event.at("jobs"))
              if (job.at("id") == report_.at("first_native_job_id"))
                observedPublishing = job.at("state") == "Publishing" && job.at("cancel_requested") == false;
        require(observedPublishing, "H1 publishing winner did not observe real Publishing before/after actual cancel");
      }
    }
    if (number_ == 12)
      require(report_.at("first_terminal_job").at("state") == "Canceled" &&
                  report_.at("first_native_job_id") != report_.at("second_native_job_id"),
              "H1 new admission did not follow a distinct finalized canceled native request");
    if (!winner) {
      requireUnchanged(report_.at("after"));
    } else {
      require(report_.at("after").at("source_raw").at("sha256") == report_.at("expected_changed_raw").at("sha256") &&
                  report_.at("after").at("undo_steps") == 1 && report_.at("after").at("redo_steps") == 0 &&
                  report_.at("after").at("workflow") == Json::array({report_.at("command_record")}) &&
                  report_.at("after").at("results").empty(),
              "H1 publishing winner did not commit exact changed output/history/workflow once");
      require(report_.at("undo").at("source_raw").at("sha256") == report_.at("before").at("source_raw").at("sha256") &&
                  report_.at("undo").at("undo_steps") == 0 && report_.at("undo").at("redo_steps") == 1 &&
                  report_.at("redo").at("source_raw").at("sha256") == report_.at("expected_changed_raw").at("sha256") &&
                  report_.at("redo").at("undo_steps") == 1 && report_.at("redo").at("redo_steps") == 0,
              "H1 winner Undo/Redo did not restore exact original/changed output once");
    }
  }

  void cleanupFailure() noexcept {
    if (cleaned_) return;
    validatorArmed_->store(false);
    try {
      for (const auto &info : scheduler_->jobs()) scheduler_->cancel(info.id);
    } catch (...) {}
    try { session_->abort(); } catch (...) {}
    try { waitActual(); } catch (...) {}
    try { if (blocker_) blocker_->wait_for(5s); } catch (...) {}
    validator_.reset();
    actualJobs_.clear();
    blocker_.reset();
    try { window_.poll(); } catch (...) {}
    cleaned_ = true;
  }
};

Json Window::gaussianCancellationCase(
    const std::string &caseId, const std::string &context,
    const std::shared_ptr<GaussianCancellationSession> &session,
    const std::string &directory) {
  checkThread();
  caseNumber(caseId);
  if (context != "imgui_cpp" && context != "imgui_python")
    throw std::invalid_argument("H1 launch context must be imgui_cpp or imgui_python");
  require(session && documents_.empty() && workflow.empty() && results.empty(),
          "H1 requires a fresh caller Window and external actor session");
  GaussianCancellationDriver driver(*this, caseId, context, session, freshDirectory(directory));
  return driver.run();
}

int Window::gaussianCancellationConsumerTest(const std::string &directory) {
  checkThread();
  require(documents_.empty(), "H1 standalone consumer requires a fresh Window");
  const auto root = freshDirectory(directory);
  Json report{{"schema_version", 1}, {"contract_id", "p02a.gaussian.h1-races.001"},
              {"context", "imgui_cpp"}, {"status", "unverified"},
              {"acceptance_claim", false}, {"cases", Json::array()},
              {"pending", Json::array({"complete 61-variant actual-input Gaussian UI matrix",
                                         "R16 actual Preferences below-retained rejection",
                                         "scientific C/T/composite/virtual desktop support"})}};
  for (int number = 1; number <= 12; ++number) {
    std::ostringstream name;
    name << "H1-" << std::setw(2) << std::setfill('0') << number;
    const auto caseId = name.str();
    const auto caseDirectory = root / caseId;
    Window window((caseDirectory / "preferences.json").u8string());
    auto session = std::make_shared<GaussianCancellationSession>();
    std::exception_ptr actorFailure;
    std::thread actor;
    CancellationActorGuard actorGuard{session, actor};
    actor = std::thread([&] {
      try {
        require(session->waitForCancelPoint(), "H1 C++ canceller did not receive its real boundary");
        session->cancelEntered();
        window.cancel();
        session->cancelReturned();
      } catch (...) {
        actorFailure = std::current_exception();
        session->abort();
      }
    });
    try {
      auto observed = window.gaussianCancellationCase(caseId, "imgui_cpp", session,
                                                       caseDirectory.u8string());
      // A process supervisor provides the outer deadline if native cancel
      // deadlocks. Never detach an actor that borrows this Window's lifetime.
      actor.join();
      if (actorFailure) std::rethrow_exception(actorFailure);
      report["cases"].push_back(std::move(observed));
      std::cout << "PASS " << caseId << " imgui_cpp\n";
    } catch (const std::exception &error) {
      session->abort();
      if (actor.joinable()) actor.join();
      report["status"] = "failed";
      report["error"] = error.what();
      report["failed_case_id"] = caseId;
      report["failed_session"] = session->diagnostics();
      writeJson((root / "gaussian-cancellation-failed.json").u8string(), report);
      throw;
    }
  }
  report["status"] = "passed";
  writeJson((root / "imgui-cpp.json").u8string(), report);
  return 0;
}

} // namespace imagejxx::desktop
