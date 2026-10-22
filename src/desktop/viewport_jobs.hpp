#pragma once
#include "desktop.hpp"
#include "render_memory.hpp"
#include <imagejxx/jobs.hpp>
#include <list>
#include <mutex>

namespace imagejxx::desktop {
// CPU-only output. Builders capture immutable source/display snapshots; GL,
// ImGui and frontend publication remain on the requesting thread.
struct ViewportSurface {
  std::shared_ptr<const Image> source;
  std::optional<Image> rgb;
};

// Private staging primitive for background ordinary/XYZ rendering. Not yet
// connected to Window: it introduces no user-visible asynchronous preference.
class ViewportJobs {
public:
  using Builder = std::function<ViewportSurface(const ExecutionControl &)>;
  struct Frame {
    RenderMemory::Lease reservation;
    std::optional<ViewportSurface> surface;
    Frame(RenderMemory::Lease lease, ViewportSurface value);
    Frame(Frame &&) = default;
    Frame &operator=(Frame &&) = delete;
    ~Frame();
  };
  explicit ViewportJobs(std::shared_ptr<RenderMemory> memory);
  ~ViewportJobs();
  ViewportJobs(const ViewportJobs &) = delete;
  ViewportJobs &operator=(const ViewportJobs &) = delete;
  // Null means pending, including temporary memory/capacity pressure. A key
  // identifies all source/position/display/resource inputs for one view. New
  // keys supersede old work; unchanged failed keys keep their original error.
  // Bytes are the caller's declared peak, not measured provider/RSS usage.
  std::optional<Frame> request(std::uint64_t document, int view,
                              std::string key, std::size_t bytes, Builder builder);
  void cancelDocument(std::uint64_t document);
  void cancelAll();
  void poll();
  Json diagnostics() const;
private:
  struct Cell {
    std::mutex mutex;
    bool abandoned = false;
    std::optional<ViewportSurface> value;
    void abandon();
  };
  struct Slot {
    std::uint64_t document;
    int view;
    std::string key, error;
    bool retired = false;
    Job job;
    std::shared_ptr<Cell> cell;
    RenderMemory::Lease reservation;
  };
  static constexpr std::size_t capacity = 16;
  const std::thread::id creator_ = std::this_thread::get_id();
  std::shared_ptr<RenderMemory> memory_;
  JobScheduler scheduler_{JobLimits{2, 0, capacity}};
  // A canceled running slot keeps its reservation until worker finalization.
  // Keeping it in place makes cancellation allocation-free and bounds both
  // retained result cells and captured providers, including retired requests.
  std::list<Slot> slots_;
  std::uint64_t submitted_ = 0, discarded_ = 0, published_ = 0, failed_ = 0;
  void checkThread() const;
  void cancel(Slot &slot);
};
} // namespace imagejxx::desktop
