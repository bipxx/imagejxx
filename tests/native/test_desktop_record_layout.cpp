#include "desktop.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace imagejxx;
namespace desktop = imagejxx::desktop;
using Json = desktop::Json;
std::size_t checks = 0;
std::string current;
void check(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(current + ": " + message);
}
MetadataObject opaque_layout() {
  return {{"future", metadata_object({{"present", true},
              {"tag", std::string("retain native layout")},
              {"integer", std::numeric_limits<std::uint64_t>::max()},
              {"values", metadata_array({std::int64_t(7), false, std::string("opaque")})}})},
          {"negative_zero", -0.0},
          {"empty_future", metadata_array({})}};
}
void layout(const DocumentSnapshot &snapshot, const MetadataObject &opaque,
            int mode, bool has_stored_row) {
  const auto &actual = snapshot.data().metadata.record_layout;
  if (mode == 2) {
    check(actual.empty(), "Canonical empty layout gained presence flags");
    return;
  }
  check(actual.size() == opaque.size() + 4, "Layout lost or invented an opaque field/group");
  for (const auto &[key, value] : opaque) {
    const auto found = actual.find(key);
    check(found != actual.end() && metadata_equal(found->second,value),
          "Native opaque layout field changed during legacy-state edit");
  }
  check(std::signbit(std::get<double>(actual.at("negative_zero"))),
        "Opaque layout signed zero was reformatted");
  const std::vector<std::string> roots = mode == 1
      ? std::vector<std::string>{"rois","title"} : std::vector<std::string>{"rois"};
  const std::vector<std::string> calibration = mode == 1
      ? std::vector<std::string>{"unit"} : std::vector<std::string>{};
  const std::vector<std::string> slices = has_stored_row
      ? std::vector<std::string>{"1"} : std::vector<std::string>{};
  check(std::get<std::vector<std::string>>(actual.at("root_fields")) == roots,
        "Changed ROI root presence was not rederived");
  check(std::get<std::vector<std::string>>(actual.at("null_fields")).empty(),
        "Non-null ROI edit invented null field presence");
  check(std::get<std::vector<std::string>>(actual.at("calibration_fields")) == calibration,
        "Unchanged calibration field presence changed");
  check(std::get<std::vector<std::string>>(actual.at("roi_slice_fields")) == slices,
        "Added/removed ROI slice field presence was not rederived");
}
void unrelated(const DocumentSnapshot &actual, const DocumentSnapshot &before) {
  check(actual.data().dataset.shares_pixels(before.data().dataset),
        "ROI metadata edit replaced source pixels");
  check(actual.data().selection == before.data().selection &&
        actual.data().previous_selection == before.data().previous_selection,
        "ROI metadata edit changed active/Previous geometry");
  check(actual.data().metadata.title == before.data().metadata.title &&
        metadata_equal(actual.data().metadata.properties,before.data().metadata.properties) &&
        metadata_equal(actual.data().metadata.extensions,before.data().metadata.extensions),
        "ROI metadata edit changed unrelated document metadata");
  check(actual.data().rois.at(0) == before.data().rois.at(0),
        "ROI metadata edit changed the existing row");
}
void run(int mode) {
  current = mode == 0 ? "opaque_only" : mode == 1 ? "known_and_opaque" : "canonical_empty";
  Image plane(8,6,1,PixelType::UInt16);
  for (int y = 0; y < 6; ++y) for (int x = 0; x < 8; ++x)
    plane.at(x,y) = 10*y+x;
  DocumentData data{Dataset::from_stack({plane})};
  data.metadata.title = "Desktop record layout";
  data.metadata.properties["properties"] = std::string("keep");
  data.metadata.extensions["extension"] = metadata_array({true,std::int64_t(9)});
  const auto opaque = mode == 2 ? MetadataObject{} : opaque_layout();
  data.metadata.record_layout = opaque;
  if (mode == 1) {
    data.metadata.record_layout["root_fields"] = std::vector<std::string>{"title"};
    data.metadata.record_layout["null_fields"] = std::vector<std::string>{};
    data.metadata.record_layout["calibration_fields"] = std::vector<std::string>{"unit"};
    data.metadata.record_layout["roi_slice_fields"] = std::vector<std::string>{};
  }
  data.selection = Selection::from_path({0,1,1,1,5,1,2,6,2,5,4,3,4,5,2,5,1,4,4});
  data.selection->attributes["opaque_path"] = std::string("keep");
  data.previous_selection = Selection("line",{{0,0},{1,1}});
  data.rois = {{"unrelated",Selection("rectangle",{{6,1},{7,2}}),{},{},{},
                {{"row",std::string("keep")}}}};
  auto owner = std::make_shared<imagejxx::Document>(std::move(data));
  desktop::Document facade(owner);
  const auto before = facade.nativeSnapshot();
  check(!facade.scientific(), "Fixture did not enter ordinary State-based editing");

  // The ordinary ROI Manager's Store action builds this same complete legacy
  // State mutation before Document::commit. This is a desktop adapter test;
  // the independent primary UI reader supplies actual-input coverage.
  auto stored = facade.state();
  stored.metadata["rois"].push_back({{"name","stored path"},
      {"selection",stored.metadata.at("selection")},{"slice_index",0}});
  facade.commit(std::move(stored));
  const auto added = facade.nativeSnapshot();
  layout(added,opaque,mode,true);
  unrelated(added,before);
  check(added.data().rois.size() == 2 &&
        added.data().rois[1].selection == *before.data().selection &&
        !added.data().rois[1].t && !added.data().rois[1].c && added.data().rois[1].z == 0,
        "Store changed path or ordinary ROI position semantics");
  check(facade.undoSteps() == 1 && facade.redoSteps() == 0,
        "Store did not create exactly one history unit");
  facade.undo();
  check(metadata_equal(facade.nativeSnapshot().data().metadata.record_layout,
                       before.data().metadata.record_layout) &&
        facade.nativeSnapshot().data().rois == before.data().rois,
        "Store Undo did not restore the exact original layout/rows");
  facade.redo();
  check(metadata_equal(facade.nativeSnapshot().data().metadata.record_layout,
                       added.data().metadata.record_layout),
        "Store Redo did not restore derived and opaque layout fields");

  auto renamed = facade.state();
  renamed.metadata["rois"][1]["name"] = "renamed path";
  facade.commit(std::move(renamed));
  layout(facade.nativeSnapshot(),opaque,mode,true);
  unrelated(facade.nativeSnapshot(),before);
  check(facade.nativeSnapshot().data().rois[1].name == "renamed path",
        "Rename did not exercise a second state reconstruction");

  auto removed = facade.state();
  removed.metadata["rois"].erase(removed.metadata["rois"].begin()+1);
  facade.commit(std::move(removed));
  layout(facade.nativeSnapshot(),opaque,mode,false);
  unrelated(facade.nativeSnapshot(),before);
  check(facade.nativeSnapshot().data().rois == before.data().rois,
        "Delete did not restore the literal existing rows");
  facade.undo();
  layout(facade.nativeSnapshot(),opaque,mode,true);
  facade.redo();
  layout(facade.nativeSnapshot(),opaque,mode,false);
  std::cout << current << " completed\n";
}
} // namespace

int main() {
  try {
    for (int mode = 0; mode < 3; ++mode) run(mode);
    std::cout << "Desktop record layout: 3 cases / " << checks << " checks\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
