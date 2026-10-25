#include "../../src/native/session_metadata.hpp"
#include <imagejxx/application.hpp>
#include <imagejxx/manual_threshold_command.hpp>
#include <imagejxx/native_workflow_program.hpp>
#include <imagejxx/session.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace imagejxx;
namespace codec = detail::session_metadata;
using Json = codec::Json;
std::size_t checks = 0;
void check(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
template<class Error = std::invalid_argument, class Work>
void refuses(Work work, const char *message) {
  try { work(); } catch (const Error &) { ++checks; return; }
  throw std::runtime_error(message);
}
Selection path(bool precise = false) {
  auto result = Selection::from_path({0., precise ? std::nextafter(2.2, 3.) : 2.2,
      precise ? -0. : 1.2, 1., 9.2, 1.2, 1., 2.2, 8.2, 4.});
  result.width = 2.5;
  result.attributes = {{"feature", std::string("threshold-path")},
                       {"opaque", metadata_object({{"retained", true}})}};
  return result;
}
std::shared_ptr<Document> fixture(const DatasetReadObserver &reads, Selection selected = path()) {
  Stack planes;
  for (int z = 0; z < 2; ++z) {
    Image image(23, 17, 1, PixelType::UInt8);
    for (int y = 0; y < 17; ++y) for (int x = 0; x < 23; ++x)
      image.at(x, y) = x < 11 && y < 11 ? 100 + z : 240;
    planes.push_back(std::move(image));
  }
  DocumentData data{Dataset::from_planes({1, 1, 2, 17, 23, 1}, std::move(planes)).observe_reads(reads)};
  data.selection = std::move(selected);
  auto result = std::make_shared<Document>(std::move(data));
  result->mark_saved();
  return result;
}
TargetRequest all_z() {
  TargetRequest request;
  request.scope = TargetScope::AllZ;
  return request;
}
void no_reads(const DatasetReadObserver &reads) {
  const auto counts = reads.counts();
  check(counts.plane_reads == 0 && counts.region_reads == 0 && !counts.overflowed,
        "Threshold record validation acquired source pixels");
}
void bits_equal(const std::vector<double> &a, const std::vector<double> &b) {
  check(a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(double)) == 0,
        "Native threshold path lost binary64 coordinates or signed zero");
}
void record_targets_and_precision() {
  DatasetReadObserver reads;
  auto owner = fixture(reads, path(true));
  const auto before = owner->snapshot();
  TargetRequest positions;
  positions.scope = TargetScope::Positions;
  positions.positions = {{0, 0, 1}, {0, 0, 0}};
  TargetRequest ranges;
  ranges.scope = TargetScope::Ranges;
  ranges.z = AxisRange{0, 2, 1};
  for (const auto &request : {TargetRequest{}, all_z(), positions, ranges}) {
    const auto command = resolve_manual_threshold_command(before, {101, 255}, request);
    const auto record = command.record();
    const auto value = codec::to_json(record);
    check(value.size() == 7 && value.at("schema_version") == 3 &&
              value.at("profile") == "manual_raw_inclusive_v3" &&
              value.at("operation") == "threshold" && value.at("lower") == 101. &&
              value.at("upper") == 255., "Native threshold V3 record header changed");
    const auto &selected = value.at("selection");
    check(selected.size() == 5 && selected.at("kind") == "path" &&
              selected.at("points") == Json::array() && selected.at("width") == 2.5 &&
              selected.at("attributes") == Json({{"feature", "threshold-path"},
                  {"opaque", {{"retained", true}}}}),
          "Native threshold selection lost strict fields or nested attributes");
    bits_equal(selected.at("path").get<std::vector<double>>(), before.data().selection->path_commands());
    validate_manual_threshold_command_record(record);
    const auto restored = manual_threshold_command_from_record(before, record);
    check(metadata_equal(restored.record(), record), "Strict threshold replay changed the record");
    const auto transported = codec::object_from_json(Json::parse(value.dump()));
    const auto restored_wire = manual_threshold_command_from_record(before, transported);
    bits_equal(codec::to_json(restored_wire.record()).at("selection").at("path").get<std::vector<double>>(),
               before.data().selection->path_commands());
    check(restored.target().position_count() == command.target().position_count(),
          "Threshold record changed target cardinality");
    for (std::size_t i = 0; i < command.target().position_count(); ++i)
      check(restored.target().position(i).z == command.target().position(i).z,
            "Threshold replay reordered captured positions");
  }
  no_reads(reads);
  check(owner->snapshot().shares_data(before) && owner->undo_steps() == 0,
        "Threshold record capture mutated its owner");
}
void legacy_versions_remain_closed() {
  DatasetReadObserver reads;
  auto rectangle = Selection("rectangle", {{1, 1}, {5, 4}});
  rectangle.attributes = {{"feature", std::string("legacy")}};
  auto owner = fixture(reads, rectangle);
  const Json legacy_selection{{"kind", "rectangle"}, {"points", {{1., 1.}, {5., 4.}}},
                              {"width", 1.}, {"feature", "legacy"}};
  const auto native_path = codec::to_json(resolve_manual_threshold_command(
      fixture(reads)->snapshot(), {101, 255}).record()).at("selection");
  for (const auto &request : {TargetRequest{}, all_z()}) {
    const auto record = resolve_manual_threshold_command(owner->snapshot(), {101, 255}, request).record();
    auto raw = codec::to_json(record);
    const int version = request.scope == TargetScope::CurrentPlane ? 1 : 2;
    check(raw.at("schema_version") == version && raw.at("profile") ==
              (version == 1 ? "manual_raw_inclusive_v1" : "manual_raw_inclusive_v2") &&
              raw.at("selection") == legacy_selection,
          "Legacy threshold grammar or version changed");
    validate_manual_threshold_command_record(record);
    check(metadata_equal(manual_threshold_command_from_record(owner->snapshot(), record).record(), record),
          "Legacy threshold stopped roundtripping unchanged");
    raw["selection"] = native_path;
    const auto forged = codec::object_from_json(raw);
    refuses([&] { validate_manual_threshold_command_record(forged); },
            "Legacy threshold version admitted a native path");
    refuses([&] { rebind_manual_threshold_command(owner->snapshot(), forged, {}); },
            "Rebind bypassed the legacy path-codec refusal");
  }
  auto path_owner = fixture(reads);
  TargetRequest whole;
  whole.roi = RoiPolicy::WholePlane;
  const auto unused = codec::to_json(resolve_manual_threshold_command(path_owner->snapshot(), {1, 2}, whole).record());
  check(unused.at("schema_version") == 1 && unused.at("selection").is_null(),
        "An unused source path promoted a WholePlane record");
  no_reads(reads);
}
void malformed_and_strict_source() {
  DatasetReadObserver reads;
  auto owner = fixture(reads);
  const auto before = owner->snapshot();
  const auto record = resolve_manual_threshold_command(before, {101, 255}).record();
  const auto valid = codec::to_json(record);
  std::vector<Json> invalid;
  auto add = [&](auto change) { auto value = valid; change(value); invalid.push_back(std::move(value)); };
  add([](Json &v) { v["schema_version"] = true; });
  add([](Json &v) { v["schema_version"] = 4; });
  add([](Json &v) { v["profile"] = "manual_raw_inclusive_v2"; });
  add([](Json &v) { v["extra"] = 1; });
  add([](Json &v) { v["selection"] = nullptr; });
  add([](Json &v) { v.erase("selection"); });
  add([](Json &v) { v["selection"]["kind"] = "rectangle"; });
  add([](Json &v) { v["selection"]["points"] = {{1, 1}}; });
  add([](Json &v) { v["selection"].erase("attributes"); });
  add([](Json &v) { v["selection"]["feature"] = "flattened legacy attribute"; });
  add([](Json &v) { v["selection"]["width"] = -1; });
  add([](Json &v) { v["selection"]["path"][1] = true; });
  add([](Json &v) { v["selection"]["path"] = {0, 1}; });
  add([](Json &v) { v["target"]["roi_policy"] = "whole_plane"; });
  add([](Json &v) { v["target"]["roi_policy"] = "positioned_rois"; });
  add([](Json &v) { v["lower"] = true; });
  add([](Json &v) { v["upper"] = 0; });
  for (const auto &raw : invalid) {
    const auto bad = codec::object_from_json(raw);
    refuses([&] { validate_manual_threshold_command_record(bad); }, "Malformed V3 threshold was admitted");
    refuses([&] { rebind_manual_threshold_command(before, bad, {}); }, "Rebind skipped V3 validation");
  }
  auto foreign = fixture(reads);
  refuses([&] { manual_threshold_command_from_record(foreign->snapshot(), record); },
          "Strict threshold replay ignored document identity");
  auto changed_selection = valid;
  changed_selection["selection"]["path"][1] = std::nextafter(2.2, 3.);
  refuses([&] { manual_threshold_command_from_record(before, codec::object_from_json(changed_selection)); },
          "Strict threshold replay ignored exact recorded selection");
  auto view = before.display(); view.crosshair[2] = 1; owner->set_display(view);
  check(manual_threshold_command_from_record(owner->snapshot(), record).target().position(0).z == 0,
        "Display-only navigation changed the frozen threshold target");
  owner->select(Selection("rectangle", {{0, 0}, {3, 3}}));
  refuses<RevisionConflict>([&] { manual_threshold_command_from_record(owner->snapshot(), record); },
                            "Threshold replay ignored stale data revisions");
  const auto rebound = rebind_manual_threshold_command(owner->snapshot(), record, {});
  check(rebound.target().position(0).z == 1 && codec::to_json(rebound.record()).at("schema_version") == 1 &&
            rebound.target().source().data().selection == owner->snapshot().data().selection,
        "Explicit rebinding restored the saved path instead of current destination choices");
  check(codec::to_json(record) == valid, "Rebind changed the immutable original record");
  no_reads(reads);
}
void selected_pixels_and_history() {
  DatasetReadObserver reads;
  auto owner = fixture(reads);
  const auto before = owner->snapshot();
  const auto command = resolve_manual_threshold_command(before, {101, 255}, all_z());
  const auto restored = manual_threshold_command_from_record(before, command.record());
  Application app; app.adopt(owner);
  auto prepared = app.prepare("threshold", restored.target(), restored.parameters());
  check(owner->snapshot().shares_data(before), "Threshold preparation published early");
  app.publish(prepared);
  constexpr std::array<int, 8> rows{7, 6, 5, 4, 3, 2, 1, 0};
  auto verify = [&](bool applied) {
    std::size_t members = 0;
    for (int z = 0; z < 2; ++z) {
      const auto image = owner->snapshot().data().dataset.plane({0, 0, z});
      for (int y = 0; y < 17; ++y) for (int x = 0; x < 23; ++x) {
        const bool selected = y >= 1 && y <= 8 && x >= 2 && x < 2 + rows[std::size_t(y - 1)];
        double expected = x < 11 && y < 11 ? 100 + z : 240;
        if (applied && selected) expected = z == 0 ? 0 : 255;
        check(image->at(x, y) == expected, "Threshold differs from literal inclusive bounds/pinned mask");
        members += selected;
      }
    }
    check(members == 56, "Pinned two-plane mask cardinality changed");
  };
  verify(true);
  check(owner->undo_steps() == 1 && owner->snapshot().data().selection == before.data().selection,
        "Path threshold lost selection or single-entry history");
  owner->undo(); verify(false);
  owner->redo(); verify(true);
}
void session_and_workflow_boundary() {
  DatasetReadObserver reads;
  auto owner = fixture(reads);
  const auto before = owner->snapshot();
  const auto record = resolve_manual_threshold_command(before, {101, 255}, all_z()).record();
  const auto encoded = encode_session({{before}, {}, {metadata_object(record)}, {}});
  const auto session = decode_session(encoded);
  check(session.source_version == 5 && session.documents.size() == 1 && session.workflow.size() == 1 &&
            metadata_equal(session.workflow.front(), metadata_object(record)),
        "V5 session changed the V3 threshold workflow record");
  const auto reopened = session.documents.front()->snapshot();
  refuses([&] { manual_threshold_command_from_record(reopened, record); },
          "Session reopening silently rebound the original command identity");
  const auto rebound = rebind_manual_threshold_command(reopened, record, all_z());
  check(rebound.target().position_count() == 2 &&
            codec::to_json(rebound.record()).at("schema_version") == 3,
        "Explicit reopened path rebind lost the V3 carrier");

  // V9 explicitly admits typed V1/V2/V3 manual captures. Earlier portable
  // program grammars retain their closed refusal; direct codecs are unchanged.
  auto legacy = fixture(reads, Selection("rectangle", {{1, 1}, {5, 4}}));
  const std::array<MetadataObject, 3> records{
      resolve_manual_threshold_command(legacy->snapshot(), {101, 255}).record(),
      resolve_manual_threshold_command(legacy->snapshot(), {101, 255}, all_z()).record(), record};
  for (const auto &original : records) {
    validate_manual_threshold_command_record(original);
    NativeWorkflowOperation step;
    step.operation = "threshold"; step.original = original;
    const auto program=make_native_workflow_program({step});
    check(codec::to_json(program.record()).at("version")==9 &&
              metadata_equal(*std::get<NativeWorkflowOperation>(program.steps()[0]).original,original),
          "V9 changed the original manual capture");
    const auto prepared=prepare_native_workflow(before,program.steps());
    check(prepared.completed_steps()==1 && prepared.preview().data().selection==before.data().selection,
          "V9 failed to rebind the destination manual target");
    auto old=codec::to_json(program.record());old["version"]=1;
    for(const auto key:{"filter_options","enabled","selection_creation","annotation_intent","selection_creation_extent"})
      old["steps"][0].erase(key);
    refuses([&]{native_workflow_program_from_json(old.dump());},
            "Legacy V1 program silently admitted a typed manual capture");
  }
  check(owner->snapshot().shares_data(before) && owner->undo_steps() == 0,
        "Typed workflow preparation or session capture changed the source");
}
} // namespace

int main() {
  try {
    const std::pair<const char *, void (*)()> groups[] = {
        {"MT-PATH-01 targets-and-binary64", record_targets_and_precision},
        {"MT-PATH-02 legacy-versions", legacy_versions_remain_closed},
        {"MT-PATH-03 malformed-and-strict-source", malformed_and_strict_source},
        {"MT-PATH-04 selected-pixels-and-history", selected_pixels_and_history},
        {"MT-PATH-05 session-and-workflow-boundary", session_and_workflow_boundary}};
    for (const auto &group : groups) {
      std::cout << group.first << std::endl;
      group.second();
    }
    std::cout << "Manual threshold path V3: 5 groups, " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
