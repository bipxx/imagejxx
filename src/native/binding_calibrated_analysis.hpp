#pragma once
#include <algorithm>
#include <imagejxx/calibrated_analysis.hpp>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
namespace imagejxx::bindings {
template<class T> pybind11::array_t<T> analysis_array(const std::vector<T>& values) {
  pybind11::array_t<T> out(values.size());
  std::copy(values.begin(),values.end(),out.mutable_data());return out;
}
inline pybind11::dict calibrated_statistics_python(const ImageJScalarStatistics& s) {
  pybind11::dict out;
  out["pixel_count"]=s.pixel_count;out["area"]=s.area;out["mean"]=s.mean;out["raw_mean"]=s.raw_mean;
  out["stddev"]=s.stddev;out["minimum"]=s.minimum;out["maximum"]=s.maximum;out["median"]=s.median;
  out["mode_index"]=s.mode_index;out["mode_value"]=s.mode_value;out["integrated_density"]=s.integrated_density;
  out["histogram"]=analysis_array(s.histogram);out["histogram16"]=analysis_array(s.histogram16);return out;
}
}
