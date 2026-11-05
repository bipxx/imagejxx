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
    int retired=0;bool before_release=false;
    auto canceled=scheduler.submit([&](const JobContext& context) {
      context.reserve_resources(71);guard=context.result_resource_guard();
      context.execution_control().cancellation->cancel();
    }, {"installed retirement",{},0,7,true}, [&] {
      before_release=scheduler.usage().running_bytes==71;
      guard.reset();++retired;
    });
    canceled.wait();
    if(canceled.info().state!=JobState::Canceled || retired!=1 || !before_release || scheduler.usage().reserved_bytes!=0)
      throw std::runtime_error("SDK canceled payload was not retired before reservation release");
    bool invalid=false;
    try{(void)scheduler.submit([](const JobContext&){},{},[]{});}
    catch(const std::invalid_argument&){invalid=true;}
    if(!invalid)throw std::runtime_error("SDK retirement accepted without retained resources");
    std::cout << "PASS installed growing job reservation: admission, refusal, retained release\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n'; return 1;
  }
}
