#include "window.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
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
    check(std::chrono::steady_clock::now() < until, "Startup IO timeout");
    w.wait();
  }
}
void choose(Window &w, bool flat = false) {
  settle(w);
  w.action("scientific_io_draft", flat ? Json{{"accept_flat_pixels_only", true}}
                                       : Json{{"confirm_axes", true}});
  w.action(flat ? "scientific_io_flat_open" : "scientific_io_apply");
  w.wait();
}
} // namespace
int main(int argc, char **argv) {
  try {
    check(argc == 3, "Need startup scratch and TIFF fixture");
    const auto root =
        fs::absolute(fs::u8path(argv[1])) /
        std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(root);
    const auto first = root / "first.tif", last = root / "last.tif";
    fs::copy_file(fs::u8path(argv[2]), first);
    fs::copy_file(first, last);
    auto request = [&] {
      StartupRequest r;
      r.explicit_paths = {first.u8string(), last.u8string()};
      return r;
    };
    {
      Window w((root / "success.json").u8string());
      w.startStartup(request());
      check(w.startupState().at("status") == "reviewing" &&
                w.documents().empty(),
            "Startup bypassed review");
      choose(w);
      settle(w);
      check(w.documents().empty(),
            "First TIFF published before batch acceptance");
      choose(w, true);
      check(w.documents().size() == 2 && w.activeIndex() == 1 &&
                w.startupState().at("status") == "loaded",
            "Startup final attachment differs");
      check(w.documents()[0]
                    ->nativeSnapshot()
                    .data()
                    .dataset.shape()
                    .dimensions() == std::array<int, 6>{2, 3, 2, 3, 5, 1},
            "Startup scientific axes lost");
      check(w.documents()[1]
                    ->nativeSnapshot()
                    .data()
                    .dataset.shape()
                    .dimensions() == std::array<int, 6>{1, 1, 12, 3, 5, 1},
            "Startup flat mapping differs");
      for (const auto &d : w.documents())
        check(!d->nativeSnapshot().dirty() &&
                  d->nativeOwner()->undo_steps() == 0,
              "Startup owner is dirty or has history");
      check(w.results.empty() && w.workflow.empty(),
            "Startup executed/recorded a workflow");
    }
    {
      Window w((root / "cancel.json").u8string());
      w.startStartup(request());
      choose(w);
      w.action("cancel_scientific_io");
      w.wait();
      check(w.documents().empty() &&
                w.startupState().at("status") == "canceled",
            "Canceled batch published a prefix");
    }
    {
      Window w((root / "changed.json").u8string());
      w.startStartup(request());
      choose(w, true);
      settle(w);
      {
        std::ofstream f(first, std::ios::binary | std::ios::app);
        f << "changed after flat acceptance";
      }
      choose(w);
      check(w.documents().empty() && w.startupState().at("status") == "failed",
            "Changed accepted input published batch");
    }
    std::cout
        << "PASS native startup TIFF: detached choices; single attachment; "
           "scientific/flat mapping; cancel; accepted-source guard\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
