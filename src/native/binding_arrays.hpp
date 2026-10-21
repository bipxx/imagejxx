#pragma once
#include <imagejxx/image.hpp>
#include <imagejxx/dataset.hpp>
#include <pybind11/numpy.h>

namespace imagejxx::bindings {
Image read_numpy_image(const pybind11::array &pixels);
Image read_numpy_image(const pybind11::array &pixels, FloatingSamplePolicy policy);
pybind11::array numpy_image(const Image &image);
pybind11::array numpy_image(const Image &image, FloatingSamplePolicy policy);
// Explicit opt-in transport for quiet-NaN Float32 pixel operations.
Image read_quiet_nan_numpy_image(const pybind11::array &pixels);
pybind11::array quiet_nan_numpy_image(const Image &image);
} // namespace imagejxx::bindings
