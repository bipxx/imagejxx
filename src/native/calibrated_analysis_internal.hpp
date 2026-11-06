#pragma once
#include <imagejxx/calibrated_analysis.hpp>
#include <imagejxx/line_measurement.hpp>
namespace imagejxx::detail {
// Borrow a once-decoded lookup from an admitted immutable preparation.
ImageJScalarStatistics calibrated_statistics_with_table(const Image&,
    const SpatialCalibration&, const ImageJIntensityCalibration&,
    const std::vector<std::uint32_t>&, const Image*, std::size_t,
    std::optional<std::array<int,4>>* bounds = nullptr, bool source_validated = false,
    std::optional<std::array<double,2>>* centroid = nullptr,
    std::optional<std::array<double,2>>* center_of_mass = nullptr);
ImageJLineMeasurement calibrated_line_measurement_with_table(const Image&,
    const SpatialCalibration&,const ImageJIntensityCalibration&,
    const std::vector<std::uint32_t>&,const std::array<double,4>&,bool,std::size_t,
    bool source_validated = false,double voxel_z = 0);
struct PolylineEstimate {std::size_t samples=0,work_steps=0,working_bytes=0;double length=0;std::array<int,4> bounds{};};
PolylineEstimate estimate_polyline_measurement(int,int,PixelType,const SpatialCalibration&,
    const std::vector<std::array<double,2>>&,std::size_t,std::size_t);
ImageJPolylineMeasurement calibrated_polyline_measurement_with_table(const Image&,
    const SpatialCalibration&,const ImageJIntensityCalibration&,const std::vector<std::uint32_t>&,
    const std::vector<std::array<double,2>>&,bool,std::size_t,std::size_t,bool);
std::array<int,4> angle_bounds(const std::array<std::array<double,2>,3>&);
void validate_analysis_source(const Image&);
std::size_t calibrated_profile_sample_count(const std::array<double,4>&,
    std::size_t requested = 0);
std::vector<double> calibrated_profile_with_table(const Image&,
    const ImageJIntensityCalibration&, const std::vector<std::uint32_t>&,
    double,double,double,double,bool,std::size_t,
    std::size_t samples = 0, bool source_validated = false);
}
