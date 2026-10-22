#include "viewport_jobs.hpp"
#include "../native/kernel_control.hpp"
#include <algorithm>

namespace imagejxx::desktop {
ViewportJobs::Frame::Frame(RenderMemory::Lease lease, ViewportSurface value)
    : reservation(std::move(lease)), surface(std::move(value)) {}
ViewportJobs::Frame::~Frame() {
  surface.reset(); // Release CPU outputs before releasing their reservation.
  reservation.reset();
}
void ViewportJobs::Cell::abandon() {
  std::optional<ViewportSurface> old;
  {
    std::lock_guard<std::mutex> lock(mutex);
    abandoned = true;
    old.swap(value);
  }
}
ViewportJobs::ViewportJobs(std::shared_ptr<RenderMemory> memory)
    : memory_(std::move(memory)) {
  if (!memory_) throw std::invalid_argument("Viewport render memory required");
}
ViewportJobs::~ViewportJobs() {
  for (auto &slot : slots_) if (slot.cell) slot.cell->abandon();
  // Shutdown cancels and joins: providers that ignore cancellation can delay
  // destruction. Never release reservations while their worker still runs.
  scheduler_.shutdown();
  slots_.clear();
}
void ViewportJobs::checkThread() const {
  if (std::this_thread::get_id() != creator_)
    throw std::logic_error("Viewport jobs require their creator thread");
}
void ViewportJobs::cancel(Slot &slot) {
  if (slot.retired) return;
  slot.retired = true;
  if (slot.cell) slot.cell->abandon();
  if (slot.job.valid()) { slot.job.cancel(); ++discarded_; }
}
void ViewportJobs::poll() {
  checkThread();
  for (auto it = slots_.begin(); it != slots_.end();) {
    if (it->retired && (!it->job.valid() || it->job.ready()))
      it = slots_.erase(it);
    else ++it;
  }
}
std::optional<ViewportJobs::Frame> ViewportJobs::request(
    std::uint64_t document, int view, std::string key, std::size_t bytes,
    Builder builder) {
  checkThread();
  if (!builder || !bytes) throw std::invalid_argument("Viewport builder and positive reservation required");
  if (bytes > memory_->limit()) throw std::invalid_argument("Viewport surface exceeds render memory limit");
  poll();
  auto it = std::find_if(slots_.begin(), slots_.end(), [&](const Slot &s) {
    return !s.retired && s.document == document && s.view == view;
  });
  if (it != slots_.end() && it->key != key) {
    cancel(*it); poll(); it = slots_.end();
  }
  if (it != slots_.end()) {
    auto &slot = *it;
    if (!slot.error.empty()) throw std::runtime_error(slot.error);
    if (!slot.job.ready()) return std::nullopt;
    const auto info = slot.job.info();
    if (info.state == JobState::Failed || info.state == JobState::Canceled) {
      slot.error = info.error.empty() ? "Viewport read canceled" : info.error;
      ++failed_;
      slot.cell->abandon(); slot.cell.reset(); slot.job = Job{};
      slot.reservation.reset();
      throw std::runtime_error(slot.error);
    }
    std::optional<ViewportSurface> value;
    slot.job.complete([&] {
      std::lock_guard<std::mutex> lock(slot.cell->mutex);
      value.swap(slot.cell->value);
    });
    if (!value) {
      slots_.erase(it);
      throw std::logic_error("Viewport worker returned no surface");
    }
    Frame frame(std::move(slot.reservation), std::move(*value));
    slots_.erase(it); ++published_;
    return frame;
  }
  // Temporary pressure never blocks or captures another source. Canceled
  // predecessors still consume their slots/bytes until actually finalized.
  if (slots_.size() == capacity || bytes > memory_->limit() - memory_->used())
    return std::nullopt;
  auto reservation = memory_->acquire(bytes);
  auto cell = std::make_shared<Cell>();
  slots_.push_back({document, view, std::move(key), {}, false, {}, cell, std::move(reservation)});
  auto &slot = slots_.back();
  try {
    JobOptions options; options.label = "Viewport render"; options.document_id = document;
    slot.job = scheduler_.submit([cell, builder = std::move(builder)](const JobContext &context) {
      const auto control = context.execution_control();
      ExecutionScope scope(control);
      context.checkpoint();
      auto value = builder(control);
      context.checkpoint();
      std::lock_guard<std::mutex> lock(cell->mutex);
      if (!cell->abandoned) cell->value = std::move(value);
    }, std::move(options));
  } catch (...) { slots_.pop_back(); throw; }
  ++submitted_;
  return std::nullopt;
}
void ViewportJobs::cancelDocument(std::uint64_t document) {
  checkThread();
  for (auto &slot : slots_) if (slot.document == document) cancel(slot);
  poll();
}
void ViewportJobs::cancelAll() {
  checkThread(); for (auto &slot : slots_) cancel(slot); poll();
}
Json ViewportJobs::diagnostics() const {
  checkThread(); const auto usage = scheduler_.usage();
  const auto retiring = std::count_if(slots_.begin(), slots_.end(), [](const Slot &s) { return s.retired; });
  const auto ready = std::count_if(slots_.begin(), slots_.end(), [](const Slot &s) { return !s.retired && s.job.valid() && s.job.ready(); });
  return {{"submitted", submitted_}, {"discarded", discarded_},
          {"published", published_}, {"failed", failed_},
          {"queued", usage.queued}, {"running", usage.running},
          {"retiring", retiring}, {"ready", ready}, {"slots", slots_.size()}, {"capacity", capacity}};
}
} // namespace imagejxx::desktop
