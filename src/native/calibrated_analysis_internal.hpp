#pragma once
#include <imagejxx/calibrated_analysis.hpp>
namespace imagejxx::detail {
// Borrow a once-decoded lookup from an admitted immutable preparation.
ImageJScalarStatistics calibrated_statistics_with_table(const Image&,
    const SpatialCalibration&, const ImageJIntensityCalibration&,
    const std::vector<std::uint32_t>&, const Image*, std::size_t,
    std::optional<std::array<int,4>>* bounds = nullptr);
void validate_analysis_source(const Image&);
std::size_t calibrated_profile_sample_count(const std::array<double,4>&,
    std::size_t requested = 0);
std::vector<double> calibrated_profile_with_table(const Image&,
    const ImageJIntensityCalibration&, const std::vector<std::uint32_t>&,
    double,double,double,double,bool,std::size_t,
    std::size_t samples = 0, bool source_validated = false);
}
