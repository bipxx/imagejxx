#include "window.hpp"
#include <chrono>
#include <filesystem>
#include <imagejxx/startup.hpp>
#include <iostream>
using namespace imagejxx;
using namespace imagejxx::desktop;
namespace fs = std::filesystem;
namespace {
void check(bool v, const char *message) {
  if (!v)
    throw std::runtime_error(message);
}
void settle(Window &w) {
  const auto until =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (w.jobInfo()) {
    check(std::chrono::steady_clock::now() < until, "Append IO timeout");
    w.wait();
  }
}
Json append(const Window &w) { return w.startupState().at("append"); }
} // namespace
int main(int argc, char **argv) {
  try {
    check(argc == 3, "Need append scratch and TIFF fixture");
    const auto root =
        fs::absolute(fs::u8path(argv[1])) /
        std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(root);
    Image pixels(5, 3, 1, PixelType::UInt8);
    pixels.pixels.assign(15, 17);
    imagejxx::desktop::Document input({pixels}, "original title");
    const auto document = documentJson(input);
    auto items =
        Json::array({{{"document", document}},
                     {{"path", fs::absolute(fs::u8path(argv[2])).u8string()}},
                     {{"document", document}}});
    {
      Window w((root / "success.json").u8string());
      w.action("queue_launch", {{"items", items}});
      check(w.documents().size() == 1 && append(w).at("status") == "reviewing",
            "Append did not pause after its prefix");
      items[2]["document"]["title"] = "later caller mutation";
      settle(w);
      w.action("scientific_io_draft", {{"confirm_axes", true}});
      w.action("scientific_io_apply");
      w.wait();
      check(w.documents().size() == 3 && append(w).at("status") == "completed",
            "Append did not complete");
      check(w.documents()[1]
                    ->nativeSnapshot()
                    .data()
                    .dataset.shape()
                    .dimensions() == std::array<int, 6>{2, 3, 2, 3, 5, 1},
            "Append flattened scientific axes");
      check(w.documents()[2]->nativeSnapshot().data().metadata.title ==
                "original title",
            "Queued document retained caller JSON reference");
      check(w.documents()[0]
                        ->nativeSnapshot()
                        .data()
                        .dataset.plane({0, 0, 0})
                        ->at(0, 0) == 17 &&
                w.documents()[2]
                        ->nativeSnapshot()
                        .data()
                        .dataset.plane({0, 0, 0})
                        ->at(0, 0) == 17,
            "Append scalar pixels differ");
    }
    {
      Window w((root / "cancel.json").u8string());
      w.action("queue_launch", {{"items", items}});
      settle(w);
      w.action("cancel_launch");
      w.wait();
      check(w.documents().size() == 1 && append(w).at("status") == "canceled",
            "Append cancel removed its prefix or published its suffix");
    }
    {
      Window w((root / "invalid.json").u8string());
      bool refused = false;
      try {
        w.action("queue_launch",
                 {{"items",
                   Json::array({{{"document", document}}, {{"path", ""}}})}});
      } catch (const std::exception &) {
        refused = true;
      }
      check(refused && w.documents().empty(),
            "Invalid future item published prefix");
    }
    std::cout << "PASS native mixed append: reviewed order, owned candidates, "
                 "prefix-preserving cancel, admission isolation\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
