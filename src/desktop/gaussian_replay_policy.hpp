#pragma once

#include "desktop.hpp"

namespace imagejxx::desktop {

// UI replay consent only: validate every typed original before comparing its
// complete calibration with the destination. Legacy steps remain the runner's
// responsibility. No pixels are read and no original source existence is proved.
// Consent permits the existing explicit native rebind to recalculate sigmas; it
// does not rewrite a record or replace the caller's required TargetRequest.
void validateGaussianReplayCalibration(const Json &steps,
                                       const DocumentSnapshot &destination,
                                       bool useDestinationCalibration);

} // namespace imagejxx::desktop
