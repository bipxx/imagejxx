#include <imagejxx/application.hpp>
#include <iostream>

int main() {
  using namespace imagejxx;
  Application application;
  auto document = std::make_shared<Document>(
      Dataset::from_stack({Image(5, 4), Image(5, 4)}), "Example");
  auto id = application.adopt(document);
  application.set_history_policy(8, 160, id);
  auto original = application.snapshot(id);
  auto edit = application.begin_edit(id);
  edit.replace_plane({0, 0, 1}, fill(Image(5, 4), 42));
  edit.set_extension("acquisition",
                     metadata_object({{"series", std::int64_t(2)}}));
  auto display = original.display();
  display.crosshair = {2, 1, 1};
  application.commit(edit, display);
  auto saved = application.snapshot(id);
  application.mark_saved(saved);
  if (!saved.same_document(document->snapshot()) ||
      document->snapshot().data().dataset.plane({0, 0, 1})->at(0, 0) != 42 ||
      application.history(id).pixels.obsolete_bytes != 160)
    return 1;
  application.close(id);
  try {
    application.snapshot(id);
    return 1;
  } catch (const DocumentUnavailable &) {
  }
  if (original.data().dataset.plane({0, 0, 1})->at(0, 0) != 0 ||
      saved.data().dataset.plane({0, 0, 1})->at(0, 0) != 42)
    return 1;
  std::cout << "Headless application ownership, history and close passed\n";
}
