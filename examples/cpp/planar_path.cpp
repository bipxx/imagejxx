#include <imagejxx/application.hpp>
#include <imagejxx/composite_path.hpp>
#include <imagejxx/planar_annotation_command.hpp>
#include <imagejxx/axis_remap.hpp>
#include <imagejxx/session.hpp>
#include <imagejxx/z_subset_command.hpp>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
namespace {
int checks = 0;
void check(bool value) {
  ++checks;
  if (!value)
    throw std::runtime_error("Planar path check " + std::to_string(checks));
}
template <class F> void refuses(F f) {
  bool caught = false;
  try {
    f();
  } catch (const std::exception &) {
    caught = true;
  }
  check(caught);
}
const MetadataObject &object(const MetadataValue &v) {
  return std::get<MetadataObject>(
      std::get<std::shared_ptr<const MetadataTree>>(v)->value);
}
} // namespace
int main() {
  try {
    auto curve = Selection::from_path(std::vector<double>{
        0, -4, -3, 2, 3, 8, 8, -3, 4, 0, 10, 2, 3, 11, 7, 13, -2, 14, 2, 4});
    curve.width = 2.5;
    curve.attributes = {{"opaque", std::string("kept")}};
    DatasetShape shape{2, 2, 5, 5, 7, 1};
    Image image(7, 5, 1, PixelType::UInt16);
    Stack images(20, image);
    SpatialCalibration calibration;
    calibration.spacing = {2, 3, 5};
    calibration.origin = {10, 20, 30};
    DatasetReadObserver reads;
    auto owner = std::make_shared<Document>(
        Dataset::from_planes(shape, std::move(images), calibration)
            .observe_reads(reads));
    PlanarAnnotation annotation{
        "curve", curve, Plane::XZ, 0, 1, 1, {{"row", std::string("kept")}}};
    PlanarAnnotationRequest request{PlanarAnnotationOperation::Store, {}, annotation};
    auto command = resolve_planar_annotation_command(owner->snapshot(), request);
    check(std::get<std::int64_t>(command.record().at("version")) == 3);
    validate_planar_annotation_command_record(command.record());
    auto replay =
        planar_annotation_command_from_record(owner->snapshot(), command.record());
    auto edit = prepare_planar_annotation(replay);
    owner->commit(edit);
    auto source = owner->snapshot();
    check(planar_annotations(source.data())[0].selection == curve);
    auto bad = command.record();
    bad["version"] = 2;
    refuses([&] { validate_planar_annotation_command_record(bad); });
    auto encoded = encode_planar_annotations(shape, {annotation});
    check(std::get<std::int64_t>(object(encoded).at("version")) == 3);
    check(decode_planar_annotations(shape, encoded)[0] == annotation);
    auto prepared = prepare_z_axis_remap(
        source, ZSubsetRequest{1, 4, 2, OutputDestination::InPlace});
    auto preview = prepared.transaction.preview();
    auto mapped = planar_annotations(preview.data())[0];
    check(mapped.selection.path_commands()[1] == -4 &&
          mapped.selection.path_commands()[2] == -1.75);
    check(mapped.selection.width == 2.5 &&
          mapped.selection.attributes == curve.attributes);
    check(planar_annotation_world(source.data().dataset.calibration(), annotation,
                                  {-4, -3}) ==
          planar_annotation_world(preview.data().dataset.calibration(), mapped,
                                  {-4, -1.75}));
    check(reads.counts().plane_reads == 0 && reads.counts().region_reads == 0);
    owner->commit(prepared.transaction);
    check(planar_annotations(owner->snapshot().data())[0] == mapped);
    owner->undo();
    check(owner->snapshot().shares_data(source));
    owner->redo();
    auto bytes = encode_session(SessionCapture{{owner->snapshot()}});
    check(bytes[8] == '3');
    auto loaded = decode_session(bytes);
    check(planar_annotations(loaded.documents[0]->snapshot().data())[0] == mapped);
    auto mutable_command =
        resolve_z_subset_command(source, {1, 4, 2, OutputDestination::InPlace});
    bool changed = false;
    ExecutionControl mutate;
    mutate.checkpoint = [&] {
      if (!changed) {
        changed = true;
        mutable_command =
            resolve_z_subset_command(source, {0, 4, 1, OutputDestination::InPlace});
      }
    };
    auto staged = prepare_z_subset(mutable_command, mutate);
    check(changed && staged.preview().data().dataset.shape().z == 2 &&
          mutable_command.request().step == 1);
    auto cancel = std::make_shared<CancellationToken>();
    ExecutionControl cancelled;
    cancelled.cancellation = cancel;
    cancelled.checkpoint = [&] { cancel->cancel(); };
    bool stopped = false;
    try {
      prepare_z_subset(resolve_z_subset_command(source, {1, 4, 2}), cancelled);
    } catch (const OperationCancelled &) {
      stopped = true;
    }
    check(stopped);
    auto limited = curve;
    limited =
        Selection::from_path(std::vector<double>{0, 0, -1e9, 2, 2, -1e9, 4, -1e9, 4});
    annotation.selection = limited;
    auto reset = prepare_update_planar_annotation(owner->snapshot(), 0, annotation);
    owner->commit(reset);
    std::size_t callbacks = 0;
    ExecutionControl control;
    control.checkpoint = [&] { ++callbacks; };
    refuses([&] {
      prepare_z_axis_remap(owner->snapshot(), ZSubsetRequest{1, 1, 1}, control);
    });
    check(callbacks == 0);
    std::cout << "PASS " << checks << " planar path checks\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
