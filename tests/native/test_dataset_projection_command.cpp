#include <imagejxx/dataset_projection_command.hpp>
#include <iostream>
#include <stdexcept>

using namespace imagejxx;
namespace {
void check(bool condition) {
  if (!condition) throw std::runtime_error("Projection command contract failed");
}
template<class Error = std::invalid_argument, class F> void refuses(F function) {
  bool rejected = false;
  try { function(); } catch (const Error &) { rejected = true; }
  check(rejected);
}
MetadataObject object(const MetadataValue &value) {
  return std::get<MetadataObject>(std::get<std::shared_ptr<const MetadataTree>>(value)->value);
}
MetadataObject changed(MetadataObject record, const std::vector<std::string> &path,
                       MetadataValue replacement, std::size_t at = 0) {
  if (at + 1 == path.size()) record.at(path[at]) = std::move(replacement);
  else record.at(path[at]) = metadata_object(changed(object(record.at(path[at])), path, std::move(replacement), at+1));
  return record;
}
class Unreadable final : public PlaneSource {
public:
  DatasetShape dimensions{3, 2, 9, 5, 7, 3};
  DatasetShape shape() const override { return dimensions; }
  PixelType pixel_type() const override { return PixelType::UInt16; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    throw std::runtime_error("Command resolution read source pixels");
  }
};
}
int main() {
  SpatialCalibration calibration;
  calibration.spacing = {.5, 2, 3}; calibration.origin = {10, -4, 8};
  calibration.direction = {1, .25, .5, 0, 1, .25, 0, 0, -1};
  calibration.frame_interval = 2.5; calibration.time_origin = -3;
  DatasetReadObserver observer;
  auto provider = std::make_shared<Unreadable>();
  Dataset dataset(provider, calibration);
  Document document(dataset.observe_reads(observer));
  const auto source = document.snapshot();
  DatasetZProjectionRequest request;
  request.first = 2; request.last = 7; request.group_size = 3;
  request.active_t = 2; request.active_c = 1;
  request.max_working_bytes = 1024*1024;
  auto command = resolve_dataset_z_projection_command(source, request);
  const auto record = command.record();
  check(command.source().same_document(source) && command.request().active_t == 2);
  check(command.layout().shape.dimensions() == std::array<int, 6>{3, 2, 2, 5, 7, 3});
  check(command.layout().calibration.origin == std::array<double, 3>{13, -2.5, 2});
  check(!object(record.at("derived")).count("working_bytes"));
  validate_dataset_z_projection_command_record(record);
  auto replay = dataset_z_projection_command_from_record(source, record);
  check(metadata_equal(replay.record(), record));
  auto alternate = record;
  alternate = changed(alternate, {"request", "first"}, std::uint64_t(2));
  alternate = changed(alternate, {"derived", "source_first_z"}, std::uint64_t(2));
  validate_dataset_z_projection_command_record(alternate);
  check(metadata_equal(dataset_z_projection_command_from_record(source, alternate).record(), record));
  Document other(dataset);
  refuses([&] { dataset_z_projection_command_from_record(other.snapshot(), record); });
  auto explicit_request = request; explicit_request.all_time_frames = false;
  explicit_request.all_channels = false;
  auto rebound = rebind_dataset_z_projection_command(other.snapshot(), record, explicit_request);
  check(rebound.source().id() == other.id && rebound.layout().shape.t == 1 && rebound.layout().shape.c == 1);
  auto bad_record = [&](const MetadataObject &bad) {
    refuses([&] { validate_dataset_z_projection_command_record(bad); });
    refuses([&] { dataset_z_projection_command_from_record(source, bad); });
    refuses([&] { rebind_dataset_z_projection_command(other.snapshot(), bad, explicit_request); });
  };
  for (const auto &[name, value] : record) {
    (void)value; auto bad = record; bad.erase(name); bad_record(bad);
  }
  for (const auto section : {"source", "request", "derived"}) {
    const auto fields = object(record.at(section));
    for (const auto &[name, value] : fields) {
      (void)value; auto bad = fields; bad.erase(name);
      bad_record(changed(record, {section}, metadata_object(bad)));
    }
    auto extra = fields; extra.emplace("unknown", true);
    bad_record(changed(record, {section}, metadata_object(extra)));
  }
  auto extra = record; extra.emplace("unknown", true); bad_record(extra);
  for (auto field : {"schema", "operation", "profile", "destination"})
    bad_record(changed(record, {field}, std::string("unknown")));
  for (MetadataValue value : {MetadataValue(true), MetadataValue(1.0), MetadataValue(std::int64_t(-1)), MetadataValue(std::uint64_t(2))})
    bad_record(changed(record, {"version"}, value));
  for (auto field : {"first", "last", "group_size", "active_t", "active_c"})
    for (MetadataValue value : {MetadataValue(true), MetadataValue(1.5), MetadataValue(std::int64_t(-1)), MetadataValue(std::numeric_limits<std::uint64_t>::max())})
      bad_record(changed(record, {"request", field}, value));
  for (auto field : {"max_working_bytes", "max_output_pixel_bytes"})
    for (MetadataValue value : {MetadataValue(false), MetadataValue(1024.0), MetadataValue(std::int64_t(0)), MetadataValue(std::int64_t(-1))})
      bad_record(changed(record, {"request", field}, value));
  for (auto field : {"all_time_frames", "all_channels"})
    for (MetadataValue value : {MetadataValue(std::int64_t(1)), MetadataValue(1.0), MetadataValue(std::string("true")), MetadataValue(false)})
      bad_record(changed(record, {"request", field}, value));
  bad_record(changed(record, {"request", "method"}, std::string("Mean")));
  bad_record(changed(record, {"request", "group_size"}, std::int64_t(4)));
  bad_record(changed(record, {"derived", "pixel_type"}, std::string("uint16")));
  for (auto field : {"source_first_t", "source_first_c", "source_first_z", "contributors_per_group", "output_pixel_bytes", "reduction_scratch_bytes"})
    bad_record(changed(record, {"derived", field}, std::uint64_t(999)));
  bad_record(changed(record, {"derived", "calibration", "time_origin"}, 2.0));
  bad_record(changed(record, {"source", "calibration", "unit"}, std::string("different")));
  bad_record(changed(record, {"source", "document_id"}, true));
  bad_record(changed(record, {"source", "revisions", "pixels"}, 1.0));
  auto revised = changed(record, {"source", "revisions", "metadata"}, std::uint64_t(source.revisions().metadata + 1));
  validate_dataset_z_projection_command_record(revised);
  refuses<RevisionConflict>([&] { dataset_z_projection_command_from_record(source, revised); });
  auto current_calibration = calibration; current_calibration.time_origin = 12;
  auto data = source.data(); data.dataset = data.dataset.with_calibration(current_calibration);
  DocumentSnapshot adapter(document.identity, source.revisions(), std::make_shared<const DocumentData>(data), source.display());
  refuses([&] { dataset_z_projection_command_from_record(adapter, record); });
  auto display = source.display(); display.zoom = 2;
  dataset_z_projection_command_from_record(source.with_display(display), record);
  document.set_display(display);
  check(document.snapshot().revisions().display != source.revisions().display);
  dataset_z_projection_command_from_record(document.snapshot(), record);
  document.select(Selection("rectangle", {{0, 0}, {1, 1}}));
  dataset_z_projection_command_from_record(document.snapshot(), record);
  auto transaction = document.begin_edit(); transaction.set_title("changed"); document.commit(transaction);
  refuses<RevisionConflict>([&] { dataset_z_projection_command_from_record(document.snapshot(), record); });
  // A legal positive cap is a proposal, not a claim of prior ABI admission.
  auto insufficient = changed(record, {"request", "max_working_bytes"}, std::uint64_t(1));
  validate_dataset_z_projection_command_record(insufficient);
  refuses([&] { dataset_z_projection_command_from_record(source, insufficient); });
  rebind_dataset_z_projection_command(other.snapshot(), insufficient, explicit_request);
  int callbacks = 0; ExecutionControl control; control.checkpoint = [&] { ++callbacks; };
  refuses([&] { execute_dataset_z_projection(command, control, command.layout().working_bytes - 1); });
  refuses([&] { execute_dataset_z_projection(command, control, 0); });
  check(callbacks == 0 && metadata_equal(command.record(), record));
  provider->dimensions.t = 4;
  refuses([&] { execute_dataset_z_projection(command, control); });
  check(callbacks == 0);
  provider->dimensions.t = 3;
  check(observer.counts().plane_reads == 0 && observer.counts().region_reads == 0);
  for (auto policy : {FloatingSamplePolicy::PreserveQuietNaNFloat32, FloatingSamplePolicy::PreserveIEEEFloat32}) {
    Image plane(1, 1, 1, PixelType::Float32); plane.pixels[0] = 1;
    Document unsupported(Dataset::from_stack({plane}, {}, policy));
    refuses([&] { resolve_dataset_z_projection_command(unsupported.snapshot(), {}); });
  }
  Stack planes;
  for (int z = 0; z < 6; ++z) { Image plane(1, 1); plane.pixels[0] = z*z; planes.push_back(plane); }
  Document real(Dataset::from_stack(std::move(planes)));
  DatasetZProjectionRequest numerical; numerical.last = 5; numerical.group_size = 3;
  auto bound = resolve_dataset_z_projection_command(real.snapshot(), numerical);
  auto output = execute_dataset_z_projection(bound, {}, bound.layout().working_bytes);
  check(output.shape().z == 2 && output.plane()->pixels[0] == 5.0/3 && output.plane({0, 0, 1})->pixels[0] == 50.0/3);
  check(real.undo_steps() == 0 && real.snapshot().data().dataset.shape().z == 6);
  auto before = bound.record(); numerical.method = "sum";
  check(metadata_equal(bound.record(), before) && bound.request().method == "mean");
  auto ordinary = bound.request(); ordinary.group_size = 0;
  auto ordinary_command = resolve_dataset_z_projection_command(real.snapshot(), ordinary);
  check(ordinary_command.request().group_size == 0 && ordinary_command.layout().contributors_per_group == 6);
  check(metadata_equal(bound.record(), before));
  auto maximum_revision = changed(before, {"source", "revisions", "pixels"}, std::numeric_limits<std::uint64_t>::max());
  validate_dataset_z_projection_command_record(maximum_revision);
  refuses<RevisionConflict>([&] { dataset_z_projection_command_from_record(real.snapshot(), maximum_revision); });
  auto pixel_edit = real.begin_edit(); Image replacement(1, 1); replacement.pixels[0] = 100;
  pixel_edit.replace_plane({}, replacement); real.commit(pixel_edit);
  refuses<RevisionConflict>([&] { dataset_z_projection_command_from_record(real.snapshot(), before); });
  // Numerical execution uses the captured snapshot; live publication still
  // requires its own source guard, not an implicit retarget to current pixels.
  check(execute_dataset_z_projection(bound).plane()->pixels[0] == 5.0/3);
  std::cout << "Scientific projection strict command, replay, rebind and caps passed\n";
}
