#include <imagejxx/application.hpp>
#include <imagejxx/axis_remap.hpp>
#include <imagejxx/planar_annotation_command.hpp>
#include <imagejxx/planar_annotations.hpp>
#include <imagejxx/session.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace imagejxx;
namespace {
std::size_t checks = 0;
void check(bool v, const char *message) {
  ++checks;
  if (!v)
    throw std::runtime_error(message);
}
template <class F> void refuses(F f, const char *message) {
  bool rejected = false;
  try {
    f();
  } catch (const std::exception &) {
    rejected = true;
  }
  check(rejected, message);
}
Selection pattern() {
  std::vector<unsigned char> bits(4, 0);
  for (int y = 0; y < 9; ++y) {
    const auto i = y * 3 + y % 3;
    bits[i / 8] |= static_cast<unsigned char>(0x80u >> (i % 8));
  }
  auto value = Selection::from_packed_region(3, 9, std::move(bits), {-2, -2});
  value.width = 2.5;
  value.attributes = {{"opaque", metadata_array({true, std::uint64_t(-1)})}};
  return value;
}
} // namespace
int main() {
  try {
    Stack planes;
    for (int z = 0; z < 5; ++z) {
      Image p(7, 5, 1, PixelType::UInt16);
      p.at(0, 0) = z;
      planes.push_back(std::move(p));
    }
    DatasetReadObserver reads;
    auto owner = std::make_shared<Document>(
        Dataset::from_stack(std::move(planes)).observe_reads(reads));
    const auto region = pattern();
    PlanarAnnotation row{
        "regional", region, Plane::XZ, 0, 0, {}, {{"row", std::string("kept")}}};
    PlanarAnnotationRequest store{PlanarAnnotationOperation::Store, {}, row};
    auto command = resolve_planar_annotation_command(owner->snapshot(), store);
    check(std::get<std::int64_t>(command.record().at("version")) == 2,
          "Regional command requires V2");
    validate_planar_annotation_command_record(command.record());
    auto stored = prepare_planar_annotation(
        planar_annotation_command_from_record(owner->snapshot(), command.record()));
    owner->commit(stored);
    const auto source = owner->snapshot();
    auto expected = Selection::from_packed_region(3, 4, {0x50, 0xa0}, {-2, -1});
    expected.width = region.width;
    expected.attributes = region.attributes;
    ZSubsetRequest request{1, 4, 2, OutputDestination::InPlace};
    const auto prepared = prepare_z_axis_remap(source, request);
    const auto rows = planar_annotations(prepared.transaction.preview().data());
    check(rows.size() == 1 && rows[0].selection == expected,
          "Off-volume global lattice cells lost");
    check(metadata_equal(rows[0].attributes, row.attributes), "Row attributes changed");
    check(reads.counts().plane_reads == 0 && reads.counts().region_reads == 0,
          "Regional remap read source words");
    check(owner->snapshot().shares_data(source), "Preparation changed source owner");
    auto transaction = prepared.transaction;
    owner->commit(transaction);
    check(planar_annotations(owner->snapshot().data())[0].selection == expected,
          "Publication lost full mask");
    owner->undo();
    check(owner->snapshot().shares_data(source), "Undo lost exact source");
    owner->redo();
    check(planar_annotations(owner->snapshot().data())[0].selection == expected,
          "Redo lost output");
    auto capture = SessionCapture{{owner->snapshot()}};
    auto decoded = decode_session(encode_session(capture));
    check(planar_annotations(decoded.documents[0]->snapshot().data())[0].selection ==
              expected,
          "Session lost intrinsic frame");
    auto mutable_source = source;
    ExecutionControl control;
    std::size_t calls = 0;
    control.checkpoint = [&] {
      ++calls;
      mutable_source = owner->snapshot();
      request.first = -1;
      control.checkpoint = {};
    };
    const auto frozen = prepare_z_axis_remap(mutable_source, request, control);
    check(calls > 0 &&
              planar_annotations(frozen.transaction.preview().data())[0].selection ==
                  expected,
          "Callback redirected source/request");
    auto bad = Selection::from_packed_region(1, 1, {0x80},
                                             {0, std::numeric_limits<int>::min()});
    auto bad_row = row;
    bad_row.selection = bad;
    auto edit = owner->begin_edit();
    edit.set_extension(
        planar_annotations_extension,
        encode_planar_annotations(owner->snapshot().data().dataset.shape(), {bad_row}));
    owner->commit(edit);
    request = {1, 1, 1, OutputDestination::InPlace};
    calls = 0;
    control.checkpoint = [&] { ++calls; };
    refuses([&] { prepare_z_axis_remap(owner->snapshot(), request, control); },
            "Unrepresentable output origin accepted");
    check(calls == 0, "Invalid regional frame dispatched callbacks");
    control.checkpoint = [] { throw OperationCancelled(); };
    refuses([&] { prepare_z_axis_remap(source, ZSubsetRequest{1, 4, 2}, control); },
            "Cancellation accepted");
    std::cout << "PASS planar V2/full Z lattice/style/no-IO/publication/capture/undo: "
              << checks << " checks\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
