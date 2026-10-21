#include <imagejxx/application.hpp>
#include <iostream>

int main() {
  using namespace imagejxx;
  Application app;
  auto id = app.create(
      Dataset::from_stack(Stack(4, Image(6, 5, 1, PixelType::UInt16))));
  TargetRequest request;
  request.scope = TargetScope::Ranges;
  request.z = AxisRange{1, 4, 2};
  request.roi = RoiPolicy::WholePlane;
  auto target = app.resolve_target(request, id, "add");
  auto prepared = app.prepare("add", target, {{"value", 7}});
  if (app.snapshot(id).data().dataset.plane({0, 0, 1})->at(0, 0) != 0 ||
      prepared.preview().data().dataset.plane({0, 0, 1})->at(0, 0) != 7)
    return 1;
  auto result = app.publish(prepared);
  if (result.document_id != id || result.created || prepared.active() ||
      app.history(id).undo_steps != 1)
    return 2;
  app.undo(id);
  request.destination = OutputDestination::NewDocument;
  auto derived =
      app.prepare("add", app.resolve_target(request, id), {{"value", 9}});
  auto output = app.publish(derived, "Processed");
  if (!output.created || !output.document || output.document_id == id ||
      output.document->snapshot().data().dataset.plane({0, 0, 3})->at(0, 0) !=
          9)
    return 3;
  std::cout
      << "Installed SDK isolated dataset execution and publication passed\n";
}
