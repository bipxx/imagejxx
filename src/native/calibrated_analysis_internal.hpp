#pragma once
#include <imagejxx/calibrated_analysis.hpp>
namespace imagejxx::detail {
// Borrow a once-decoded lookup from an admitted immutable preparation.
ImageJScalarStatistics calibrated_statistics_with_table(const Image&,
    const SpatialCalibration&, const ImageJIntensityCalibration&,
    const std::vector<std::uint32_t>&, const Image*, std::size_t);
std::vector<double> calibrated_profile_with_table(const Image&,
    const ImageJIntensityCalibration&, const std::vector<std::uint32_t>&,
    double,double,double,double,bool,std::size_t);
}
