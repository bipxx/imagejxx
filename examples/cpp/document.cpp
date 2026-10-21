#include <imagejxx/document.hpp>
#include <iostream>

int main() {
  using namespace imagejxx;
  Document document(Dataset::from_stack({Image(5, 4), Image(5, 4)}), "Example");
  auto original = document.snapshot();
  auto edit = original.begin_edit();
  edit.replace_plane({0, 0, 1}, fill(Image(5, 4), 42));
  edit.set_property("author", std::string("SDK example"));
  edit.set_extension(
      "acquisition",
      metadata_object({{"series", metadata_array({std::int64_t(0),
                                                  std::string("sample")})}}));
  auto preview = edit.preview();
  document.commit(edit);
  if (document.undo_steps() != 1 ||
      original.data().dataset.plane({0, 0, 1})->at(0, 0) != 0 ||
      preview.data().dataset.plane({0, 0, 1})->at(0, 0) != 42)
    return 1;
  if (!metadata_equal(document.snapshot().data().metadata.extensions,
                      preview.data().metadata.extensions))
    return 1;
  auto display = document.snapshot().display();
  display.crosshair = {2, 1, 1};
  display.zoom = 2;
  display.properties = {{"lut", std::string("Fire")}};
  document.set_display(display);
  document.undo();
  if (document.snapshot().dirty() || document.snapshot().display().zoom != 2 ||
      !metadata_equal(document.snapshot().display().properties,
                      display.properties))
    return 1;
  std::cout << "Headless document preview, commit and undo passed\n";
}
