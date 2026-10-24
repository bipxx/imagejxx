#include <imagejxx/saved_roi_geometry.hpp>

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace imagejxx;
namespace {
std::size_t checks = 0;
std::string current;
void stage(const char *id, const char *phase) {
  current = std::string(id) + ": " + phase;
  std::cout << current << '\n' << std::flush;
}
void check(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
template<class F> void refuses(F &&call) {
  bool refused = false;
  try { call(); } catch (const std::invalid_argument &) { refused = true; }
  check(refused, "Saved path resource bound did not refuse");
}
class UnreadSource final : public PlaneSource {
public:
  DatasetShape shape() const override { return {2,2,8,12,16,1}; }
  PixelType pixel_type() const override { return PixelType::UInt16; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++reads; throw std::runtime_error("Saved path admission read a plane");
  }
  Image read_region(PlanePosition, int, int, int, int) const override {
    ++reads; throw std::runtime_error("Saved path admission read a region");
  }
  mutable std::size_t reads = 0;
};
Selection styled(Selection selection) {
  selection.width = 1.75;
  selection.attributes = {
      {"opaque", std::string("saved path resource fixture")},
      {"nested", metadata_object({{"values", metadata_array({true, std::int64_t(7)})}})}};
  return selection;
}
Selection long_path(bool changed) {
  // Literal M/L/Z/M/Q/C prefix, followed by the same 8192 complete line
  // commands as the large UI workload. The replacement changes one known
  // endpoint; no production transform generates the expected geometry.
  std::vector<double> commands{0,2,2,1,6,2,1,6,6,1,2,6,4,
                              0,3,3,2,4,2,5,3,3,6,3,6,5,5,5};
  const std::array<std::array<double,2>,4> corners{{{2,2},{6,2},{6,6},{2,6}}};
  for (std::size_t i = 0; i < 8192; ++i) {
    commands.push_back(1);
    commands.push_back(corners[i % 4][0]);
    commands.push_back(corners[i % 4][1]);
  }
  if (changed) commands[4] = 6.25;
  check(commands.size() == 24604, "Literal long path scalar cardinality changed");
  return styled(Selection::from_path(commands));
}
Selection rectangle() {
  return styled(Selection("rectangle", {{2,2},{6,6}}));
}
void run(const char *id, const Selection &original, const Selection &replacement) {
  auto provider = std::make_shared<UnreadSource>();
  SpatialCalibration calibration;
  calibration.spacing = {.5,1.25,2};
  calibration.origin = {3,-4,5};
  calibration.direction = {0,-1,0,1,0,0,0,0,1};
  DocumentData data{Dataset(provider,calibration)};
  data.metadata.title = "Large saved path admission";
  data.metadata.properties["properties"] = metadata_object({{"unsigned", std::uint64_t(9)}});
  data.metadata.extensions["extensions"] = metadata_array({false, std::string("retained")});
  data.metadata.record_layout["layout"] = metadata_object({{"kept", true}});
  data.overlays = {{{"overlay", metadata_array({std::int64_t(11), std::string("kept")})}}};
  data.selection = Selection::from_path({0,1,1,1,3,1,1,3,3,4});
  data.previous_selection = Selection("line", {{0,0},{1,1}});
  data.rois = {{"target", original, {}, {}, {}, {{"row", std::string("first")}}},
               {"untouched", rectangle(), 0, 0, 0, {{"row", std::int64_t(2)}}}};
  DisplayState display;
  display.orthogonal = true;
  display.crosshair = {0,0,0};
  display.zoom = 8;
  display.properties["display"] = metadata_object({{"retained", true}});
  Document owner(std::move(data),display);
  const auto before = owner.snapshot();
  const SavedRoiGeometryRequest request(0,replacement);
  SavedRoiGeometryLimits high;
  high.max_working_bytes = 64u * 1024u * 1024u;
  stage(id,"estimate");
  const auto estimate = estimate_saved_roi_geometry(before,request,high);
  check(estimate.working_bytes <= high.max_working_bytes &&
        estimate.working_bytes > 1024u * 1024u,
        "Long path does not distinguish the unchanged 64 MiB and 1 MiB budgets");
  check(estimate.record_wire_bytes > 1 && estimate.record_metadata_bytes > 0,
        "Long path estimate omitted its retained record");

  SavedRoiGeometryLimits exact = high;
  exact.max_working_bytes = estimate.working_bytes;
  exact.max_record_bytes = estimate.record_wire_bytes;
  std::array<std::size_t,3> calls{};
  ExecutionControl control;
  control.checkpoint = [&] { ++calls[0]; };
  control.progress = [&](auto,auto) { ++calls[1]; };
  control.stage_progress = [&](auto,auto,auto) { ++calls[2]; };
  std::cout << "capture caps: working=" << exact.max_working_bytes
            << " wire=" << exact.max_record_bytes
            << " metadata=" << estimate.record_metadata_bytes << '\n' << std::flush;

  // A successful borrowed estimate must cover the materialized record on the
  // exact same immutable source and request. This fails before the separator
  // accounting fix even when max_working_bytes is increased.
  stage(id,"resolve at exact capture caps");
  const auto command = resolve_saved_roi_geometry(before,request,exact,control);
  check(command.original_roi() == before.data().rois[0] && command.replacement() == replacement,
        "Resolving the admitted saved path changed complete geometry/style");
  check(command.estimate().working_bytes == estimate.working_bytes &&
        command.estimate().record_wire_bytes == estimate.record_wire_bytes &&
        command.estimate().record_metadata_bytes == estimate.record_metadata_bytes,
        "Resolve changed the already advertised resource bounds");
  check(calls[0] > 0 && calls[2] > 0, "Admitted resolve did not reach capture callbacks");
  // Importing an untrusted record has a separate decoder/container liveness
  // admission: 12*C + 3*W + fixed + 4*row descriptors. The source/request
  // estimate advertises capture storage, not that additional import workload.
  // Its record/ROI upper bounds safely cover the actual record and descriptors.
  // The successful <=64 MiB capture above also bounds this arithmetic well
  // below size_t overflow on supported 32-bit and 64-bit platforms.
  const auto record_working = 12 * estimate.record_metadata_bytes +
      3 * estimate.record_wire_bytes + estimate.fixed_bytes + 4 * estimate.roi_bytes;
  auto record_limits = exact;
  if (record_working > record_limits.max_working_bytes)
    record_limits.max_working_bytes = record_working;
  check(record_limits.max_working_bytes <= SavedRoiGeometryLimits{}.max_working_bytes,
        "Separate record-import workload exceeds the unchanged default ceiling");
  std::cout << "record import cap: working=" << record_limits.max_working_bytes
            << " wire=" << record_limits.max_record_bytes << '\n' << std::flush;
  stage(id,"validate record");
  validate_saved_roi_geometry_record(command.record(),record_limits);
  stage(id,"decode record");
  const auto decoded = decode_saved_roi_geometry_request(command.record(),record_limits);
  check(decoded.row_index == 0 && decoded.replacement == replacement,
        "Strict saved-path record decode lost the request");
  stage(id,"replay with separate record budget");
  const auto replay = saved_roi_geometry_from_record(before,command.record(),record_limits);
  check(replay.is_replay() && replay.replacement() == replacement &&
        metadata_equal(replay.record(),command.record()),
        "Replay changed saved-path intent/provenance");
  stage(id,"rebind with separate record budget");
  const auto rebound = rebind_saved_roi_geometry(before,command.record(),request,record_limits);
  check(!rebound.is_replay() && rebound.replacement() == replacement,
        "Explicit saved-path rebind failed");

  for (int boundary = 0; boundary < 3; ++boundary) {
    auto low = exact;
    if (boundary == 0) --low.max_working_bytes;
    if (boundary == 1) --low.max_record_bytes;
    if (boundary == 2) low.max_working_bytes = 1024u * 1024u;
    stage(id,boundary == 0 ? "one-byte-under capture working refusal" :
             boundary == 1 ? "one-byte-under capture wire refusal" : "1 MiB capture refusal");
    calls = {};
    refuses([&] { (void)estimate_saved_roi_geometry(before,request,low); });
    refuses([&] { (void)resolve_saved_roi_geometry(before,request,low,control); });
    check(calls == std::array<std::size_t,3>{},
          "Saved-path cap refusal dispatched callbacks before admission");
  }
  stage(id,"1 MiB record-import refusal");
  auto low_record = record_limits;
  low_record.max_working_bytes = 1024u * 1024u;
  calls = {};
  refuses([&] { (void)saved_roi_geometry_from_record(before,command.record(),low_record,control); });
  check(calls == std::array<std::size_t,3>{},
        "Saved-path record-import refusal dispatched callbacks before admission");
  // A refused attempt cannot poison recovery at the original advertised cap.
  stage(id,"exact capture cap recovery");
  check(resolve_saved_roi_geometry(before,request,exact).replacement() == replacement,
        "Saved-path refusal prevented exact-cap recovery");
  const auto after = owner.snapshot();
  check(after.data_identity() == before.data_identity() &&
        after.revisions().matches(before.revisions(),Change::All) &&
        after.dirty() == before.dirty() && owner.undo_steps() == 0 && owner.redo_steps() == 0,
        "Read-only saved-path admission changed owner/history");
  check(provider->reads == 0, "Saved-path admission acquired source pixels");
  std::cout << id << ": working=" << estimate.working_bytes
            << " wire=" << estimate.record_wire_bytes
            << " metadata=" << estimate.record_metadata_bytes << '\n';
}
} // namespace

int main() {
  try {
    const auto original = long_path(false), changed = long_path(true), box = rectangle();
    run("saved_path_identity",original,original);
    run("saved_path_changed",original,changed);
    run("saved_path_to_ordinary",original,box);
    run("saved_ordinary_to_path",box,changed);
    std::cout << "Saved path resources: 4 cases / " << checks << " checks\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << current << ": " << error.what() << '\n';
    return 1;
  }
}
