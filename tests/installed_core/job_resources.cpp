#include <imagejxx/jobs.hpp>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace imagejxx;
int main() {
  try {
    JobScheduler scheduler({1, 100, 8});
    std::shared_ptr<const void> guard;
    auto job = scheduler.submit([&](const JobContext& context) {
      context.reserve_resources(73);
      context.reserve_resources(19);
      bool refused = false;
      try { context.reserve_resources(101); }
      catch (const std::length_error&) { refused = true; }
      if (!refused) throw std::runtime_error("SDK over-budget growth accepted");
      guard = context.result_resource_guard();
    }, {"installed growth", {}, 0, 7, true});
    job.wait(); job.rethrow();
    if (job.info().resource_bytes != 73 || job.info().peak_reserved_bytes != 73 ||
        scheduler.usage().reserved_bytes != 73 || scheduler.usage().retained_bytes != 73)
      throw std::runtime_error("SDK grown reservation not retained");
    job.complete([] {});
    guard.reset();
    if (scheduler.usage().reserved_bytes != 0)
      throw std::runtime_error("SDK grown reservation leaked");
    std::cout << "PASS installed growing job reservation: admission, refusal, retained release\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n'; return 1;
  }
}
