#pragma once

#include <imagejxx/gaussian.hpp>
#include <imagejxx/operations.hpp>
#include <imagejxx/targets.hpp>

namespace imagejxx {

struct GaussianBlurCommandOptions {
  double sigma = 2.0;
  bool scaled_units = false;
};

// Raw intent only; effective sigma bounds also depend on destination
// calibration.
void validate_gaussian_command_options(const GaussianBlurCommandOptions &);

// Native pixel-unit aliases px/pixel/pixels have no scaled-units control.
bool gaussian_scaled_units_available(const SpatialCalibration &calibration);

class ResolvedGaussianBlurCommand {
public:
  const GaussianBlurCommandOptions &options() const { return options_; }
  const SpatialCalibration &calibration() const { return calibration_; }
  const ResolvedTarget &target() const { return target_; }
  double sigma_x() const { return sigma_x_; }
  double sigma_y() const { return sigma_y_; }
  bool scaled_units_available() const;
  // Canonical operation sigma is a two-element NumericArray in (Y,X) order.
  OperationParameters parameters() const;
  // Version1 record retains raw intent, full calibration, effective (X,Y)
  // sigma and the source-bound target. It does not store UI session defaults.
  MetadataObject record() const;

private:
  friend ResolvedGaussianBlurCommand
  resolve_gaussian_command(const ResolvedTarget &, GaussianBlurCommandOptions);
  friend ResolvedGaussianBlurCommand
  resolve_gaussian_command(const DocumentSnapshot &, const TargetRequest &,
                           GaussianBlurCommandOptions);
  friend ResolvedGaussianBlurCommand
  gaussian_command_from_record(const DocumentSnapshot &,
                               const MetadataObject &);
  ResolvedGaussianBlurCommand(GaussianBlurCommandOptions options,
                              SpatialCalibration calibration,
                              ResolvedTarget target, double sigma_x,
                              double sigma_y);
  GaussianBlurCommandOptions options_;
  SpatialCalibration calibration_;
  ResolvedTarget target_;
  double sigma_x_, sigma_y_;
};

// Resolve physical sigma against an Application target's captured effective
// spatial calibration, retaining its publication generation guard.
ResolvedGaussianBlurCommand resolve_gaussian_command(
    const ResolvedTarget& target, GaussianBlurCommandOptions options = {});
ResolvedGaussianBlurCommand
resolve_gaussian_command(const DocumentSnapshot &source,
                         const TargetRequest &request = {},
                         GaussianBlurCommandOptions options = {});
ResolvedGaussianBlurCommand
gaussian_command_from_record(const DocumentSnapshot &source,
                             const MetadataObject &record);
// Validate schema1 intent/calibration/effective sigma and original target
// structure without a source or pixel reads. This does not authenticate old
// source identities/revisions or prove that any original source still exists.
void validate_gaussian_command_record(const MetadataObject &record);
// Validate the complete original record, then explicitly resolve its raw sigma
// and scaled-units intent on a new destination using the required caller
// request. The original target is checked against its recorded shape, without
// reading pixels or claiming its old source/revisions still exist. No identity
// fields are rewritten; effective sigma uses the destination's current
// calibration.
ResolvedGaussianBlurCommand
rebind_gaussian_command(const DocumentSnapshot &destination,
                        const MetadataObject &record,
                        const TargetRequest &request);

struct GaussianBlurCommandRegion {
  std::optional<GaussianBlurExtent> extent;
  std::optional<Image> mask;
};

// Geometry bounds determine resampling phase; mask occupancy determines writes.
// No source pixel planes are read. Non-area selections mean the whole plane;
// unmatched positioned ROIs yield an explicit empty extent and empty mask.
GaussianBlurCommandRegion gaussian_command_region(const ResolvedTarget &target,
                                                  std::size_t index);

} // namespace imagejxx
