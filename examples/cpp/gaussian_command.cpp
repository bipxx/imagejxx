// Installed-SDK self-checks using public APIs only. Owned analytic fixtures and
// the separately verified numerical primitive establish integration expectations;
// this consumer does not establish independent upstream or UI parity.
#include <imagejxx/application.hpp>
#include <imagejxx/gaussian_command.hpp>

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>

using namespace imagejxx;
namespace {
constexpr const char *operation = "gaussian_blur_imagej";

void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <class Exception = std::invalid_argument, class F>
void rejects(F work, const char *message) {
  bool rejected = false;
  try {
    work();
  } catch (const Exception &) {
    rejected = true;
  }
  check(rejected, message);
}

bool sameImage(const Image &left, const Image &right) {
  return left.width == right.width && left.height == right.height &&
         left.channels == right.channels && left.type == right.type &&
         left.pixels.size() == right.pixels.size() &&
         std::memcmp(left.pixels.data(), right.pixels.data(),
                     left.pixels.size() * sizeof(double)) == 0;
}

// Original asymmetric planes are generated independently of target resolution
// and execution. Each RGB sample and scientific T/C/Z position is distinct.
Image fixture(PixelType type, int samples = 1, PlanePosition position = {}) {
  Image input(23, 17, samples, type);
  for (int y = 0; y < input.height; ++y)
    for (int x = 0; x < input.width; ++x)
      for (int sample = 0; sample < samples; ++sample) {
        const int value = 37 * x + 61 * y + 13 * x * y + 97 * position.t +
                          503 * position.c + 997 * position.z + 173 * sample;
        input.at(x, y, sample) =
            type == PixelType::UInt8 ? value % 256
            : type == PixelType::Float32 ? (value % 65536) / 8.0 - .125
                                        : value % 65536;
      }
  return input;
}

SpatialCalibration calibration() {
  SpatialCalibration result;
  result.spacing = {.5, 2, 3};
  result.origin = {10, -5, 2};
  result.direction = {0, -1, 0, 1, 0, 0, 0, 0, 1};
  result.unit = "um";
  return result;
}

struct SourceState {
  DocumentSnapshot source;
  std::size_t undo, redo;
  explicit SourceState(const Document &owner)
      : source(owner.snapshot()), undo(owner.undo_steps()), redo(owner.redo_steps()) {}
  void unchanged(const Document &owner) const {
    const auto current = owner.snapshot();
    check(current.shares_data(source) &&
              current.revisions().matches(source.revisions(), Change::All) &&
              current.dirty() == source.dirty() && owner.undo_steps() == undo &&
              owner.redo_steps() == redo,
          "Preparation or rejection changed source, dirty state, display or history");
  }
};

void processorsAndCalibratedSigma() {
  for (const auto layout :
       {std::pair<PixelType, int>{PixelType::UInt8, 1},
        {PixelType::UInt16, 1}, {PixelType::Float32, 1},
        {PixelType::UInt8, 3}}) {
    const auto input = fixture(layout.first, layout.second);
    const auto original = input;
    const auto direct = execute_operation(
        operation, input, {{"sigma", NumericArray{{2}, {.5, 2}}}});
    check(sameImage(direct.image, gaussian_blur_imagej(input, 2, .5)) &&
              sameImage(input, original) && !direct.labelCount && !direct.labelImage,
          "U8/U16/F32/RGB direct per-axis dispatch changed source or result");
  }
  Application app;
  const auto input = fixture(PixelType::Float32);
  auto owner = std::make_shared<Document>(Dataset::from_stack({input}, calibration()));
  app.adopt(owner);
  const SourceState before(*owner);
  const auto defaults = resolve_gaussian_command(before.source);
  check(defaults.options().sigma == 2 && !defaults.options().scaled_units &&
            defaults.sigma_x() == 2 && defaults.sigma_y() == 2,
        "Fresh command did not preserve its sigma2 pixel default");
  const auto command = resolve_gaussian_command(before.source, {}, {2, true});
  check(command.scaled_units_available() && command.sigma_x() == 4 &&
            command.sigma_y() == 1 &&
            command.calibration().voxel_to_world({7, 5, 2}) ==
                std::array<double, 3>{0, -1.5, 8},
        "Calibrated command did not retain spacing, origin and orientation");
  const auto prepared = app.prepare(operation, command.target(), command.parameters());
  const auto direct = execute_operation(
      operation, input, {{"sigma", NumericArray{{2}, {1, 4}}}});
  check(sameImage(*prepared.preview().data().dataset.plane(), direct.image) &&
            sameImage(direct.image, gaussian_blur_imagej(input, 4, 1)),
        "Scaled command and explicit pixel parameters produced different pixels");
  before.unchanged(*owner);
}

void originalGeometryAndIrregularExterior() {
  const auto input = fixture(PixelType::Float32);
  Application app;
  auto owner = std::make_shared<Document>(Dataset::from_stack({input}, calibration()));
  app.adopt(owner);
  owner->select(Selection("polygon", {{2.2, 1.2}, {9.2, 1.2}, {2.2, 8.2}}));
  const SourceState before(*owner);
  const auto command = resolve_gaussian_command(before.source, {}, {12, false});
  const auto region = gaussian_command_region(command.target(), 0);
  check(region.extent && region.extent->x == 2 && region.extent->y == 1 &&
            region.extent->width == 8 && region.extent->height == 8 && region.mask &&
            region.mask->at(8, 1) == 255 && region.mask->at(9, 1) == 0 &&
            region.mask->at(2, 7) == 255 && region.mask->at(2, 8) == 0,
        "Geometric extent was replaced by the smaller irregular-mask bounding box");
  // The expectation uses explicit original bounds and analytic cell inclusion;
  // it does not derive its extent or write mask from the command's region.
  const auto rectangle = gaussian_blur_imagej(input, 12, 12,
                                             GaussianBlurExtent{2, 1, 8, 8});
  auto expected = input;
  bool changed = false;
  for (int y = 0; y < input.height; ++y)
    for (int x = 0; x < input.width; ++x) {
      const double dx = x + .5 - 2.2, dy = y + .5 - 1.2;
      if (dx >= 0 && dy >= 0 && dx + dy < 7) {
        expected.at(x, y) = rectangle.at(x, y);
        changed = changed || expected.at(x, y) != input.at(x, y);
      }
    }
  const auto prepared = app.prepare(operation, command.target(), command.parameters());
  check(changed && sameImage(*prepared.preview().data().dataset.plane(), expected) &&
            prepared.preview().data().selection == before.source.data().selection,
        "Irregular ROI did not preserve exact exterior pixels and selected geometry");
  before.unchanged(*owner);
}

void scientificPositionsAndCapturedLegacyAllZ() {
  Stack planes;
  for (int t = 0; t < 2; ++t)
    for (int c = 0; c < 2; ++c)
      for (int z = 0; z < 3; ++z)
        planes.push_back(fixture(PixelType::UInt16, 1, {t, c, z}));
  Application app;
  auto owner = std::make_shared<Document>(Dataset::from_planes(
      {2, 2, 3, 17, 23, 1}, std::move(planes), calibration()));
  app.adopt(owner);
  auto display = owner->snapshot().display();
  display.t = display.c = 1;
  display.crosshair[2] = 2;
  owner->set_display(display);
  const auto legacy = resolve_legacy_target(owner->snapshot(), true, {}, operation);
  TargetRequest request;
  request.scope = TargetScope::Positions;
  request.positions = {{1, 1, 2}, {0, 0, 1}, {1, 0, 0}};
  const auto explicitCommand = resolve_gaussian_command(owner->snapshot(), request);
  display.t = display.c = 0;
  display.crosshair[2] = 0;
  owner->set_display(display);
  const SourceState before(*owner);
  const auto selected = app.prepare(operation, explicitCommand.target(), explicitCommand.parameters());
  const auto allZ = app.prepare(operation, legacy, explicitCommand.parameters());
  check(legacy.position_count() == 3 && legacy.position(0).t == 1 &&
            legacy.position(0).c == 1 && legacy.position(0).z == 0 &&
            legacy.position(2).z == 2 && selected.outputs().size() == 3,
        "Legacy AllZ was retargeted by navigation or expanded into all T/C planes");
  for (int t = 0; t < 2; ++t)
    for (int c = 0; c < 2; ++c)
      for (int z = 0; z < 3; ++z) {
        const auto original = fixture(PixelType::UInt16, 1, {t, c, z});
        const bool explicitMatch = (t == 1 && c == 1 && z == 2) ||
                                   (t == 0 && c == 0 && z == 1) ||
                                   (t == 1 && c == 0 && z == 0);
        const auto explicitExpected = explicitMatch
            ? gaussian_blur_imagej(original, 2, 2) : original;
        const auto legacyExpected = t == 1 && c == 1
            ? gaussian_blur_imagej(original, 2, 2) : original;
        check(sameImage(*selected.preview().data().dataset.plane({t, c, z}), explicitExpected) &&
                  sameImage(*allZ.preview().data().dataset.plane({t, c, z}), legacyExpected),
              "Scientific or legacy Gaussian changed an unrequested position");
      }
  before.unchanged(*owner);
}

void sourceBoundRecordRoundtripAndRejection() {
  Document owner{Dataset::from_stack({fixture(PixelType::Float32)}, calibration())};
  const SourceState before(owner);
  const auto command = resolve_gaussian_command(before.source, {}, {2, true});
  const auto record = command.record();
  check(metadata_equal(gaussian_command_from_record(before.source, record).record(), record),
        "Gaussian record did not round-trip raw intent, calibration and target");
  auto bad = record;
  bad["effective_sigma_xy"] = metadata_array({1.0, 4.0});
  rejects([&] { gaussian_command_from_record(before.source, bad); },
          "Contradictory effective (X,Y) sigma record was accepted");
  bad = record;
  bad["unknown"] = true;
  rejects([&] { gaussian_command_from_record(before.source, bad); },
          "Unknown Gaussian record field was ignored");
  bad = record;
  bad["schema_version"] = true;
  rejects([&] { gaussian_command_from_record(before.source, bad); },
          "Boolean Gaussian schema version was accepted");
  Document detached(before.source);
  rejects([&] { gaussian_command_from_record(detached.snapshot(), record); },
          "Gaussian record silently rebound to another source owner");
  before.unchanged(owner);
}

void previewApplyUndoRedo() {
  const auto input = fixture(PixelType::UInt16);
  const auto expected = gaussian_blur_imagej(input, 2, 2);
  check(!sameImage(input, expected), "Apply fixture is an identity operation");
  Application app;
  auto owner = std::make_shared<Document>(Dataset::from_stack({input}, calibration()));
  app.adopt(owner);
  const SourceState before(*owner);
  const auto command = resolve_gaussian_command(before.source);
  auto prepared = app.prepare(operation, command.target(), command.parameters());
  auto retained = prepared;
  check(prepared.active() && sameImage(*prepared.preview().data().dataset.plane(), expected),
        "Gaussian preview was inactive or had unexpected pixels");
  before.unchanged(*owner);
  const auto published = app.publish(prepared);
  check(published.document == owner && !published.created &&
            owner->undo_steps() == 1 && owner->snapshot().dirty() &&
            !prepared.active() && !retained.active() &&
            sameImage(*owner->snapshot().data().dataset.plane(), expected),
        "Changed Gaussian Apply did not publish exactly one native history step");
  app.undo();
  check(owner->undo_steps() == 0 && owner->redo_steps() == 1 &&
            sameImage(*owner->snapshot().data().dataset.plane(), input),
        "Gaussian undo did not restore the original source pixels");
  app.redo();
  check(owner->undo_steps() == 1 && owner->redo_steps() == 0 &&
            sameImage(*owner->snapshot().data().dataset.plane(), expected) &&
            sameImage(*retained.preview().data().dataset.plane(), expected) &&
            sameImage(*before.source.data().dataset.plane(), input),
        "Gaussian redo or retained snapshots lost exact historical pixels");
}

void zeroSigmaNativeNoop() {
  Application app;
  auto owner = std::make_shared<Document>(Dataset::from_stack({fixture(PixelType::UInt16)}));
  app.adopt(owner);
  const SourceState before(*owner);
  const auto command = resolve_gaussian_command(before.source, {}, {0, false});
  auto prepared = app.prepare(operation, command.target(), command.parameters());
  app.publish(prepared);
  check(!prepared.active(), "Zero-sigma publication did not consume its preparation");
  before.unchanged(*owner);
}

void stalePublicationAtomicity() {
  const auto input = fixture(PixelType::UInt16);
  Application app;
  auto owner = std::make_shared<Document>(Dataset::from_stack({input}));
  app.adopt(owner);
  const auto command = resolve_gaussian_command(owner->snapshot());
  auto prepared = app.prepare(operation, command.target(), command.parameters());
  auto newer = input;
  newer.at(1, 1) += 777;
  auto edit = owner->begin_edit();
  edit.replace_plane({}, newer);
  app.commit(edit);
  const SourceState before(*owner);
  rejects<RevisionConflict>([&] { app.publish(prepared); },
                           "Stale Gaussian preparation overwrote a newer source edit");
  check(prepared.active() && sameImage(*owner->snapshot().data().dataset.plane(), newer),
        "Stale rejection consumed its preview or changed the newer pixels");
  before.unchanged(*owner);
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 1) {
    std::cerr << argv[0] << " takes no arguments\n";
    return 2;
  }
  const std::pair<const char *, void (*)()> observations[] = {
      {"processors_and_calibrated_sigma", processorsAndCalibratedSigma},
      {"original_geometry_and_irregular_exterior", originalGeometryAndIrregularExterior},
      {"scientific_positions_and_captured_legacy_all_z", scientificPositionsAndCapturedLegacyAllZ},
      {"source_bound_record_roundtrip_and_rejection", sourceBoundRecordRoundtripAndRejection},
      {"preview_apply_undo_redo", previewApplyUndoRedo},
      {"zero_sigma_native_noop", zeroSigmaNativeNoop},
      {"stale_publication_atomicity", stalePublicationAtomicity}};
  for (const auto &observation : observations) {
    try {
      observation.second();
      std::cout << "PASS " << observation.first << '\n';
    } catch (const std::exception &error) {
      std::cerr << "FAIL " << observation.first << ": " << error.what() << '\n';
      return 1;
    }
  }
  std::cout << "Installed SDK Gaussian command checks passed (7 observations)\n";
  return 0;
}
