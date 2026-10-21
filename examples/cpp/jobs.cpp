#include <imagejxx/application.hpp>
#include <iostream>
int main() {
  imagejxx::Application app;
  auto id = app.create(imagejxx::Dataset::from_stack({imagejxx::Image(8, 6)}),
                       "Source");
  auto job = app.submit("add", app.resolve_target(), {{"value", 5}});
  job.wait();
  const auto info = job.info();
  if (info.state != imagejxx::JobState::Ready || info.stage.empty() ||
      info.stage_completed != info.stage_total || info.stage_total != 48 ||
      info.completed != 1 || info.total != 1 || app.history(id).undo_steps != 0)
    return 1;
  auto result = app.publish(job);
  if (result.document_id != id || app.history(id).undo_steps != 1 ||
      app.snapshot().data().dataset.plane()->at(0, 0) != 5 ||
      job.info().state != imagejxx::JobState::Completed)
    return 1;
  app.undo();
  if (app.job_scheduler()->cancel(0) ||
      app.job_scheduler()->cancel(job.info().id))
    return 1;
  std::cout << "Installed native job service example passed\n";
}
