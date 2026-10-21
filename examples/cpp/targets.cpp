#include <imagejxx/application.hpp>
#include <imagejxx/operations.hpp>
#include <iostream>

int main() {
  using namespace imagejxx;
  Application app;
  app.create(Dataset::from_stack(Stack(4, Image(6, 5, 1, PixelType::UInt16))));
  TargetRequest request;
  request.scope = TargetScope::Ranges;
  request.z = AxisRange{1, 4, 2};
  request.roi = RoiPolicy::WholePlane;
  auto target = app.resolve_target(request, 0, "add");
  auto edit = target.begin_edit();
  for (std::size_t index = 0; index < target.position_count(); ++index) {
    auto p = target.position(index);
    auto result = execute_operation(
        "add", *target.source().data().dataset.plane(p), {{"value", 7}});
    edit.replace_plane(p, std::move(result.image));
  }
  app.validate_target(target);
  app.commit(edit);
  auto result = app.snapshot();
  if (result.data().dataset.plane({0, 0, 1})->at(0, 0) != 7 ||
      result.data().dataset.plane({0, 0, 2})->at(0, 0) != 0 ||
      app.history().undo_steps != 1)
    return 1;
  app.undo();
  if (app.snapshot().data().dataset.plane({0, 0, 3})->at(0, 0) != 0)
    return 2;
  std::cout << "Installed SDK resolved-target edit and undo passed\n";
}
