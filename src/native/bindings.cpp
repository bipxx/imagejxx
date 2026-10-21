#include "imagejxx/image.hpp"
#include <imagejxx/binary.hpp>
#include <imagejxx/conversion.hpp>
#include <imagejxx/rgb_conversion.hpp>
#include <imagejxx/gaussian.hpp>
#include <pybind11/complex.h>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace py = pybind11;
using namespace imagejxx;
void bind_dataset(py::module_& module);
void bind_float32_sources(py::module_& module);
void bind_settings(py::module_& module);
void bind_playback(py::module_& module);
void bind_avi_movie(py::module_& module);
void bind_shortcuts(py::module_& module);
void bind_preferences(py::module_& module);
void bind_startup(py::module_& module);
void bind_conversion(py::module_& module);
void bind_rgb_dataset(py::module_& module);
void bind_color_components(py::module_& module);
void bind_indexed_color(py::module_& module);
void bind_indexed_dataset(py::module_& module);
void bind_color_stack(py::module_& module);
void bind_document(py::module_& module);
void bind_application(py::module_& module);
void bind_saved_roi_geometry(py::module_& module);
void bind_operations(py::module_& module);
void bind_targets(py::module_& module);
void bind_execution(py::module_& module);
void bind_auto_threshold(py::module_& module);
void bind_jobs(py::module_& module);
void bind_gaussian_command(py::module_& module);
void bind_rank_command(py::module_& module);
void bind_axis_extraction_command(py::module_& module);
void bind_axis_orientation_command(py::module_& module);
void bind_float32_display(py::module_& module);
void bind_channel_display(py::module_& module);
void bind_selection_creation(py::module_& module);
void bind_roi_manager(py::module_& module);
void bind_manual_threshold(py::module_& module);
void bind_z_subset(py::module_& module);
void bind_axis_spacing_command(py::module_& module);
void bind_slicer_resize_command(py::module_& module);
void bind_planar_annotations(py::module_& module);
void bind_planar_display_annotations(py::module_& module);
void bind_imagej_roi(py::module_& module);
void bind_imagej_display_roi(py::module_& module);
void bind_imagej_roi_import(py::module_& module);
void bind_imagej_annotation_import(py::module_& module);
void bind_imagej_annotation_export(py::module_& module);
void bind_native_workflow(py::module_& module);
void bind_imagej_roi_zip_export(py::module_& module);
void bind_planar_annotation_command(py::module_& module);
void bind_dataset_histogram(py::module_& module);
void bind_auto_contrast(py::module_& module);
void bind_session(py::module_& module);
namespace {
int dimension(const py::array& a, int axis) {
    auto n = a.shape(axis);
    if (n <= 0 || n > std::numeric_limits<int>::max()) throw std::invalid_argument("Array dimensions must be nonempty and fit in int32");
    return static_cast<int>(n);
}
PixelType pixel_type(const py::array& array) {
    auto kind = py::str(array.dtype().attr("kind")).cast<std::string>();
    auto bytes = array.itemsize();
    if (kind == "u" && bytes == 1) return PixelType::UInt8;
    if (kind == "u" && bytes == 2) return PixelType::UInt16;
    if (kind == "f" && bytes == 4) return PixelType::Float32;
    if (kind == "f" && bytes == 8) return PixelType::Float64;
    throw std::invalid_argument("Images require uint8, uint16, float32 or float64 pixels; convert signed integers explicitly");
}
PixelType named_type(const std::string& name) {
    if (name == "uint8") return PixelType::UInt8;
    if (name == "uint16") return PixelType::UInt16;
    if (name == "float32") return PixelType::Float32;
    if (name == "float64") return PixelType::Float64;
    throw std::invalid_argument("Unsupported output dtype");
}
Image read_image(const py::array& array) {
    if (array.ndim() != 2 && array.ndim() != 3) throw std::invalid_argument("Image must have shape (H,W) or (H,W,3/4); use stack functions for volumes");
    int c = array.ndim() == 3 ? dimension(array, 2) : 1;
    if (array.ndim() == 3 && c != 3 && c != 4) throw std::invalid_argument("Color images must have three or four channels");
    Image image(dimension(array, 1), dimension(array, 0), c, pixel_type(array));
    auto values = py::array_t<double, py::array::c_style | py::array::forcecast>::ensure(array);
    if (!values) throw std::invalid_argument("Could not convert image pixels");
    std::copy_n(values.data(), image.pixels.size(), image.pixels.data());
    for (double v : image.pixels) require_finite(v, "Image pixel");
    return image;
}
std::unique_ptr<Image> read_optional(const py::object& array) {
    if (array.is_none()) return nullptr;
    auto converted = py::array::ensure(array);
    if (!converted) throw std::invalid_argument("Expected a NumPy mask or intensity image");
    return std::make_unique<Image>(read_image(converted));
}
Stack read_stack(const py::array& array) {
    if (array.ndim() != 3 && array.ndim() != 4) throw std::invalid_argument("Stack shape must be (Z,H,W) or (Z,H,W,3/4)");
    int depth = dimension(array, 0), h = dimension(array, 1), w = dimension(array, 2), c = array.ndim() == 4 ? dimension(array, 3) : 1;
    if (array.ndim() == 4 && c != 3 && c != 4) throw std::invalid_argument("Stack color channels must be three or four");
    auto type = pixel_type(array);
    auto values = py::array_t<double, py::array::c_style | py::array::forcecast>::ensure(array);
    if (!values) throw std::invalid_argument("Could not convert stack pixels");
    Stack out; out.reserve(static_cast<std::size_t>(depth));
    for (int z = 0; z < depth; ++z) {
        Image slice(w, h, c, type);
        std::copy_n(values.data() + static_cast<std::size_t>(z) * slice.pixels.size(), slice.pixels.size(), slice.pixels.data());
        for (double v : slice.pixels) require_finite(v, "Stack pixel");
        out.push_back(std::move(slice));
    }
    return out;
}
template<class T> py::array_t<T> typed_array(const Image& image) {
    std::vector<py::ssize_t> shape{image.height, image.width};
    if (image.channels != 1) shape.push_back(image.channels);
    py::array_t<T> out(shape); T* destination = out.mutable_data();
    {
        py::gil_scoped_release release;
        for (std::size_t i = 0; i < image.pixels.size(); ++i) {
            double v = image.pixels[i];
            if constexpr (std::is_integral_v<T>) {
                if (std::isnan(v)) throw std::invalid_argument("Cannot represent NaN as an integer pixel");
                v = std::floor(std::clamp(v, 0.0, static_cast<double>(std::numeric_limits<T>::max())) + 0.5);
            }
            destination[i] = static_cast<T>(v);
        }
    }
    return out;
}
py::array to_array(const Image& image) {
    switch (image.type) {
        case PixelType::UInt8: return typed_array<std::uint8_t>(image);
        case PixelType::UInt16: return typed_array<std::uint16_t>(image);
        case PixelType::Float32: return typed_array<float>(image);
        default: return typed_array<double>(image);
    }
}
template<class Function> py::array image_call(const py::array& input, Function function) {
    auto image = read_image(input);
    auto out = [&]() { py::gil_scoped_release release; return function(image); }();
    return to_array(out);
}
py::array_t<std::int32_t> label_array(const Labels& labels) {
    py::array_t<std::int32_t> out(std::vector<py::ssize_t>{labels.height, labels.width});
    std::copy(labels.pixels.begin(), labels.pixels.end(), out.mutable_data()); return out;
}
Labels read_labels(const py::array& array) {
    if (array.ndim() != 2) throw std::invalid_argument("Labels must be a 2D integer array");
    auto kind = py::str(array.dtype().attr("kind")).cast<std::string>();
    if (kind != "i" && kind != "u") throw std::invalid_argument("Labels must have an integer dtype");
    Labels labels{dimension(array, 1), dimension(array, 0), 0, {}};
    auto values = py::array_t<double, py::array::c_style | py::array::forcecast>::ensure(array);
    labels.pixels.resize(static_cast<std::size_t>(values.size()));
    for (std::size_t i = 0; i < labels.pixels.size(); ++i) {
        double v = values.data()[i];
        if (v < 0 || v > std::numeric_limits<std::int32_t>::max()) throw std::invalid_argument("Labels must be nonnegative and fit in int32");
        labels.pixels[i] = static_cast<std::int32_t>(v); labels.count = std::max(labels.count, labels.pixels[i]);
    }
    return labels;
}
Spectrum read_spectrum(const py::array& array) {
    if (array.ndim() != 2 || py::str(array.dtype().attr("kind")).cast<std::string>() != "c") throw std::invalid_argument("Spectrum must be a 2D complex array");
    Spectrum out{dimension(array, 1), dimension(array, 0), {}};
    auto values = py::array_t<std::complex<double>, py::array::c_style | py::array::forcecast>::ensure(array);
    out.pixels.assign(values.data(), values.data() + values.size());
    for (const auto& v : out.pixels) { require_finite(v.real(), "Spectrum real part"); require_finite(v.imag(), "Spectrum imaginary part"); }
    return out;
}
py::array_t<std::complex<double>> spectrum_array(const Spectrum& spectrum) {
    py::array_t<std::complex<double>> out(std::vector<py::ssize_t>{spectrum.height, spectrum.width});
    std::copy(spectrum.pixels.begin(), spectrum.pixels.end(), out.mutable_data()); return out;
}
py::dict statistics_dict(const Statistics& s) {
    py::dict d; d["count"] = s.count; d["min"] = s.minimum; d["max"] = s.maximum; d["mean"] = s.mean;
    d["stddev"] = s.stddev; d["sum"] = s.sum; d["median"] = s.median; d["integrated_density"] = s.integrated_density;
    return d;
}
py::dict particle_dict(const Particle& p) {
    py::dict d; d["label"] = p.label; d["pixels"] = p.pixels; d["area"] = p.area;
    d["bbox"] = py::make_tuple(p.x, p.y, p.width, p.height); d["centroid"] = py::make_tuple(p.centroid_y, p.centroid_x);
    d["perimeter"] = p.perimeter; d["mean"] = p.mean; d["min"] = p.minimum; d["max"] = p.maximum;
    d["integrated_density"] = p.integrated_density; d["eccentricity"] = p.eccentricity;
    d["circularity"] = p.circularity; d["touches_border"] = p.touches_border; return d;
}
} // namespace

PYBIND11_MODULE(_native, m) {
    bind_dataset(m);
    bind_float32_sources(m);
    bind_conversion(m);
    bind_shortcuts(m);
    bind_settings(m);
    bind_playback(m);
    bind_avi_movie(m);
    bind_preferences(m);
    bind_startup(m);
    bind_document(m);
    bind_targets(m);
    bind_operations(m);
    bind_execution(m);
    bind_auto_threshold(m);
    bind_avi_movie(m);
    bind_jobs(m);
    bind_gaussian_command(m);
    bind_rank_command(m);
    bind_axis_extraction_command(m);
    bind_axis_orientation_command(m);
    bind_float32_display(m);
    bind_channel_display(m);
    bind_selection_creation(m);
    bind_roi_manager(m);
    bind_manual_threshold(m);
    bind_z_subset(m);
    bind_axis_spacing_command(m);
  bind_slicer_resize_command(m);
  bind_rgb_dataset(m);
  bind_color_components(m);
  bind_indexed_color(m);
  bind_indexed_dataset(m);
  bind_color_stack(m);
  bind_planar_annotations(m);
  bind_planar_display_annotations(m);
  bind_imagej_roi(m);
  bind_imagej_display_roi(m);
  bind_imagej_roi_import(m);
  bind_imagej_annotation_import(m);
  bind_imagej_annotation_export(m);
  bind_native_workflow(m);
  bind_imagej_roi_zip_export(m);
  bind_planar_annotation_command(m);
    bind_dataset_histogram(m);
    bind_auto_contrast(m);
    bind_saved_roi_geometry(m);
    bind_application(m);
    bind_session(m);
    m.doc() = "C++17 imagejxx processing kernels";
    m.attr("__version__") = "0.2.0";
    m.def("convert_image", [](const py::array& a, const std::string& dtype, ConversionOptions options) {
        auto type = named_type(dtype);
        return image_call(a, [&](const Image& image) { return convert_image(image, type, options); });
    }, py::arg("image"), py::arg("dtype"), py::arg("options") = ConversionOptions{});
    m.def("convert_rgb", [](const py::array& a, py::object range, py::object palette) {
        std::optional<std::array<double, 2>> limits;
        if (!range.is_none()) {
            if (py::isinstance<py::str>(range) || !py::isinstance<py::sequence>(range) || py::len(range) != 2)
                throw std::invalid_argument("RGB display range requires two numbers");
            const auto values = py::reinterpret_borrow<py::sequence>(range);
            std::array<double, 2> pair;
            if (py::isinstance<py::array>(range)) {
                const auto array = py::cast<py::array>(range);
                const auto kind = py::str(array.dtype().attr("kind")).cast<std::string>();
                if (array.ndim() != 1 || (kind != "u" && kind != "i" && kind != "f"))
                    throw std::invalid_argument("RGB display range requires a numeric vector");
                const auto numeric = py::array_t<double, py::array::c_style | py::array::forcecast>::ensure(array);
                pair = {numeric.data()[0], numeric.data()[1]};
            } else for (int i = 0; i < 2; ++i) {
                const auto value = values[i];
                if (py::isinstance<py::bool_>(value) || (!py::isinstance<py::int_>(value) && !py::isinstance<py::float_>(value)))
                    throw std::invalid_argument("RGB display range requires numeric endpoints");
                pair[i] = py::cast<double>(value);
            }
            if (!std::isfinite(pair[0]) || !std::isfinite(pair[1]) || pair[0] > pair[1])
                throw std::invalid_argument("RGB display range must be finite and ordered");
            limits = pair;
        }
        std::optional<RgbLookupTable> table;
        if (!palette.is_none()) {
            const auto array = py::array::ensure(palette);
            if (!array || !array.dtype().is(py::dtype::of<std::uint8_t>()) || array.ndim() != 2 || array.shape(0) != 256 || array.shape(1) != 3)
                throw std::invalid_argument("RGB LUT requires a uint8 array with shape (256,3)");
            const auto typed = py::array_t<std::uint8_t>::ensure(array);
            const auto values = typed.unchecked<2>();
            table.emplace();
            for (int i = 0; i < 256; ++i) for (int c = 0; c < 3; ++c) (*table)[i][c] = values(i,c);
        }
        return image_call(a, [&](const Image& image) { return convert_rgb(image, limits, table); });
    }, py::arg("image"), py::arg("display_range") = py::none(), py::arg("lut") = py::none());
    m.def("invert_uint16", [](const py::array& a, ConversionOptions options) {
        return image_call(a, [&](const Image& image) { return invert_uint16(image, options); });
    }, py::arg("image"), py::arg("options") = ConversionOptions{});
    m.def("binary_morphology", [](const py::array& a, const std::string& operation, BinaryMorphologyOptions options) {
        return image_call(a, [&](const Image& image) { return binary_morphology(image, operation, options); });
    }, py::arg("image"), py::arg("operation"), py::arg("options") = BinaryMorphologyOptions{});
    m.def("point", [](const py::array& a, const std::string& op, double v) { return image_call(a, [&](const Image& i) { return point(i, op, v); }); });
    m.def("convert", [](const py::array& a, const std::string& dtype, bool scale) { auto type = named_type(dtype); return image_call(a, [&](const Image& i) { return convert(i, type, scale); }); });
    m.def("linear", [](const py::array& a, double scale, double offset) { return image_call(a, [&](const Image& i) { return linear(i, scale, offset); }); });
    m.def("calculate", [](const py::array& a, const py::array& b, const std::string& op, const std::string& dtype) {
        auto first = read_image(a), second = read_image(b);
        auto out = [&]() { py::gil_scoped_release release; return calculate(first, second, op); }();
        if (!dtype.empty()) out.type = named_type(dtype);
        return to_array(out);
    });
    m.def("normalize", [](const py::array& a, double low, double high, double saturation) { return image_call(a, [&](const Image& i) { return normalize(i, low, high, saturation); }); });
    m.def("apply_lut", [](const py::array& a, const std::vector<double>& lut) { return image_call(a, [&](const Image& i) { return apply_lut(i, lut); }); });
    m.def("threshold", [](const py::array& a, double low, double high) { return image_call(a, [&](const Image& i) { return threshold(i, low, high); }); });
    m.def("in_range", [](const py::array& a, const std::vector<double>& low, const std::vector<double>& high) { return image_call(a, [&](const Image& i) { return in_range(i, low, high); }); });
    m.def("fill", [](const py::array& a, double value, const py::object& mask) {
        auto selection = read_optional(mask); return image_call(a, [&](const Image& i) { return fill(i, value, selection.get()); });
    });
    m.def("add_noise", [](const py::array& a, const std::string& kind, double amount, std::uint64_t seed) { return image_call(a, [&](const Image& i) { return add_noise(i, kind, amount, seed); }); });
    m.def("convolve", [](const py::array& a, const py::array& kernel, const std::string& border, double constant, bool normalize_kernel) {
        if (kernel.ndim() != 2) throw std::invalid_argument("Kernel must be a 2D array");
        int kw = dimension(kernel, 1), kh = dimension(kernel, 0);
        auto values = py::array_t<double, py::array::c_style | py::array::forcecast>::ensure(kernel);
        if (!values) throw std::invalid_argument("Kernel must contain real numeric values");
        std::vector<double> weights(values.data(), values.data() + values.size()); auto b = parse_border(border);
        return image_call(a, [&](const Image& i) { return convolve(i, weights, kw, kh, b, constant, normalize_kernel); });
    });
    m.def("gaussian_blur", [](const py::array& a, double sx, double sy, const std::string& border) { auto b = parse_border(border); return image_call(a, [&](const Image& i) { return gaussian_blur(i, sx, sy, b); }); });
    m.def("gaussian_blur_imagej", [](const py::array& a, double sx, double sy, const py::object& extent) {
        std::optional<GaussianBlurExtent> rectangle;
        if (!extent.is_none()) {
            if (!py::isinstance<py::sequence>(extent) || py::len(extent) != 4)
                throw std::invalid_argument("Gaussian extent must contain x, y, width and height");
            auto sequence = py::reinterpret_borrow<py::sequence>(extent);
            const auto numpy_bool = py::module_::import("numpy").attr("bool_");
            std::array<int, 4> coordinates{};
            for (std::size_t index = 0; index < coordinates.size(); ++index) {
                auto item = sequence[static_cast<py::ssize_t>(index)];
                if (PyBool_Check(item.ptr()) || py::isinstance(item, numpy_bool) || !PyIndex_Check(item.ptr()))
                    throw std::invalid_argument("Gaussian extent coordinates must be integers");
                auto integer = py::reinterpret_steal<py::object>(PyNumber_Index(item.ptr()));
                if (!integer) throw py::error_already_set();
                auto value = py::cast<long long>(integer);
                if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
                    throw std::invalid_argument("Gaussian extent coordinates must fit in int32");
                coordinates[index] = static_cast<int>(value);
            }
            rectangle = GaussianBlurExtent{coordinates[0], coordinates[1], coordinates[2], coordinates[3]};
        }
        return image_call(a, [&](const Image& image) { return gaussian_blur_imagej(image, sx, sy, rectangle); });
    }, py::arg("image"), py::arg("sigma_x") = 2.0, py::arg("sigma_y") = 2.0,
       py::arg("extent") = py::none());
    m.def("shadow_imagej", [](const py::array& a, const std::string& direction, const py::object& extent) {
        std::optional<std::array<int, 4>> rectangle;
        if (!extent.is_none()) {
            if (!py::isinstance<py::sequence>(extent) || py::len(extent) != 4)
                throw std::invalid_argument("Shadow extent must contain x, y, width and height");
            auto sequence = py::reinterpret_borrow<py::sequence>(extent);
            const auto numpy_bool = py::module_::import("numpy").attr("bool_");
            std::array<int, 4> coordinates{};
            for (std::size_t index = 0; index < coordinates.size(); ++index) {
                auto item = sequence[static_cast<py::ssize_t>(index)];
                if (PyBool_Check(item.ptr()) || py::isinstance(item, numpy_bool) || !PyIndex_Check(item.ptr()))
                    throw std::invalid_argument("Shadow extent coordinates must be integers");
                auto integer = py::reinterpret_steal<py::object>(PyNumber_Index(item.ptr()));
                if (!integer) throw py::error_already_set();
                auto value = py::cast<long long>(integer);
                if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
                    throw std::invalid_argument("Shadow extent coordinates must fit in int32");
                coordinates[index] = static_cast<int>(value);
            }
            rectangle = coordinates;
        }
        return image_call(a, [&](const Image& image) { return shadow_imagej(image, direction, rectangle); });
    }, py::arg("image"), py::arg("direction") = "north",
       py::arg("extent") = py::none());
    m.def("box_blur", [](const py::array& a, int r, const std::string& border) { auto b = parse_border(border); return image_call(a, [&](const Image& i) { return box_blur(i, r, b); }); });
    m.def("rank_filter", [](const py::array& a, int r, const std::string& op, bool disk, const std::string& border, double constant) { auto b = parse_border(border); return image_call(a, [&](const Image& i) { return rank_filter(i, r, op, disk, b, constant); }); });
    m.def("sobel", [](const py::array& a, const std::string& border) { auto b = parse_border(border); return image_call(a, [&](const Image& i) { return sobel(i, b); }); });
    m.def("unsharp_mask", [](const py::array& a, double sigma, double amount) { return image_call(a, [&](const Image& i) { return unsharp_mask(i, sigma, amount); }); });
    m.def("bilateral_filter", [](const py::array& a, int r, double ss, double sr) { return image_call(a, [&](const Image& i) { return bilateral_filter(i, r, ss, sr); }); });
    m.def("kuwahara_filter", [](const py::array& a, int r) { return image_call(a, [&](const Image& i) { return kuwahara_filter(i, r); }); });
    m.def("morphology", [](const py::array& a, const std::string& op, int r, bool disk, const std::string& border, double constant) { auto b = parse_border(border); return image_call(a, [&](const Image& i) { return morphology(i, op, r, disk, b, constant); }); });
    m.def("clahe", [](const py::array& a, int tw, int th, double clip) { return image_call(a, [&](const Image& i) { return clahe(i, tw, th, clip); }); });
    m.def("local_threshold", [](const py::array& a, const std::string& method, int r, double k, double offset, double range, double contrast) { return image_call(a, [&](const Image& i) { return local_threshold(i, method, r, k, offset, range, contrast); }); });
    m.def("subtract_background", [](const py::array& a, int r, bool light) { return image_call(a, [&](const Image& i) { return subtract_background(i, r, light); }); });
    m.def("rolling_ball_background", [](const py::array& a, double r, bool light) { return image_call(a, [&](const Image& i) { return rolling_ball_background(i, r, light); }); });
    m.def("subtract_rolling_ball", [](const py::array& a, double r, bool light) { return image_call(a, [&](const Image& i) { return subtract_rolling_ball(i, r, light); }); });
    m.def("remove_outliers", [](const py::array& a, int r, double threshold, bool bright) { return image_call(a, [&](const Image& i) { return remove_outliers(i, r, threshold, bright); }); });
    m.def("anisotropic_diffusion", [](const py::array& a, int n, double k, double step) { return image_call(a, [&](const Image& i) { return anisotropic_diffusion(i, n, k, step); }); });
    m.def("crop", [](const py::array& a, int x, int y, int w, int h) { return image_call(a, [&](const Image& i) { return crop(i, x, y, w, h); }); });
    m.def("paste", [](const py::array& a, const py::array& b, int x, int y, const py::object& mask) {
        auto source = read_image(b); auto selection = read_optional(mask);
        return image_call(a, [&](const Image& i) { return paste(i, source, x, y, selection.get()); });
    });
    m.def("flip", [](const py::array& a, bool horizontal, bool vertical) { return image_call(a, [&](const Image& i) { return flip(i, horizontal, vertical); }); });
    m.def("rotate90", [](const py::array& a, int turns) { return image_call(a, [&](const Image& i) { return rotate90(i, turns); }); });
    m.def("resize", [](const py::array& a, int w, int h, const std::string& mode) { auto interpolation = parse_interpolation(mode); return image_call(a, [&](const Image& i) { return resize(i, w, h, interpolation); }); });
    m.def("affine_transform", [](const py::array& a, const std::array<double, 6>& inverse, int w, int h, const std::string& mode, const std::string& border, double constant) {
        auto interpolation = parse_interpolation(mode); auto b = parse_border(border);
        return image_call(a, [&](const Image& i) { return affine_transform(i, inverse, w, h, interpolation, b, constant); });
    });
    m.def("rotate", [](const py::array& a, double degrees, bool expand, const std::string& mode, double constant) { auto interpolation = parse_interpolation(mode); return image_call(a, [&](const Image& i) { return rotate(i, degrees, expand, interpolation, constant); }); });
    m.def("bin", [](const py::array& a, int fx, int fy, const std::string& method) { return image_call(a, [&](const Image& i) { return bin(i, fx, fy, method); }); });
    m.def("resize_canvas", [](const py::array& a, int w, int h, int x, int y, double value) { return image_call(a, [&](const Image& i) { return resize_canvas(i, w, h, x, y, value); }); });
    m.def("draw_line", [](const py::array& a, double x1, double y1, double x2, double y2, double value, double thickness) { return image_call(a, [&](const Image& i) { return draw_line(i, x1, y1, x2, y2, value, thickness); }); });
    m.def("rectangle_mask", [](int w, int h, int x, int y, int rw, int rh) { auto out = [&]() { py::gil_scoped_release release; return rectangle_mask(w, h, x, y, rw, rh); }(); return to_array(out); });
    m.def("wand_mask", [](const py::array& a, int x, int y, double tolerance, int connectivity) { return image_call(a, [&](const Image& i) { return wand_mask(i, x, y, tolerance, connectivity); }); }, py::arg("image"), py::arg("x"), py::arg("y"), py::arg("tolerance") = 0, py::arg("connectivity") = 4);
    m.def("ellipse_mask", [](int w, int h, double cx, double cy, double rx, double ry) { auto out = [&]() { py::gil_scoped_release release; return ellipse_mask(w, h, cx, cy, rx, ry); }(); return to_array(out); });
    m.def("polygon_mask", [](int w, int h, const std::vector<std::array<double, 2>>& points) { auto out = [&]() { py::gil_scoped_release release; return polygon_mask(w, h, points); }(); return to_array(out); });
    m.def("line_profile", [](const py::array& a, double x1, double y1, double x2, double y2, int samples) {
        auto image = read_image(a);
        auto values = [&]() { py::gil_scoped_release release; return line_profile(image, x1, y1, x2, y2, samples); }();
        py::array_t<double> out(static_cast<py::ssize_t>(values.size())); std::copy(values.begin(), values.end(), out.mutable_data()); return out;
    });
    m.def("grayscale", [](const py::array& a) { return image_call(a, [](const Image& i) { return grayscale(i); }); });
    m.def("rgb_to_hsv", [](const py::array& a) { return image_call(a, [](const Image& i) { return rgb_to_hsv(i); }); });
    m.def("hsv_to_rgb", [](const py::array& a, const std::string& dtype) { auto type = named_type(dtype); return image_call(a, [&](const Image& i) { return hsv_to_rgb(i, type); }); });
    m.def("split_channels", [](const py::array& a) {
        auto image = read_image(a);
        auto channels = [&]() { py::gil_scoped_release release; return split_channels(image); }();
        py::list out; for (const auto& channel : channels) out.append(to_array(channel)); return out;
    });
    m.def("merge_channels", [](const std::vector<py::array>& arrays) {
        if (arrays.size() != 1 && arrays.size() != 3 && arrays.size() != 4) throw std::invalid_argument("Merge requires one, three or four channels");
        Stack channels; for (const auto& a : arrays) channels.push_back(read_image(a));
        auto out = [&]() { py::gil_scoped_release release; return merge_channels(channels); }(); return to_array(out);
    });
    m.def("color_deconvolution", [](const py::array& a, const std::array<double, 9>& stains) { return image_call(a, [&](const Image& i) { return color_deconvolution(i, stains); }); });
    m.def("histogram", [](const py::array& a, int bins, double low, double high) {
        auto image = read_image(a);
        auto h = [&]() { py::gil_scoped_release release; return histogram(image, bins, low, high); }();
        py::array_t<std::uint64_t> counts(static_cast<py::ssize_t>(h.counts.size()));
        py::array_t<double> edges(static_cast<py::ssize_t>(h.edges.size()));
        std::copy(h.counts.begin(), h.counts.end(), counts.mutable_data()); std::copy(h.edges.begin(), h.edges.end(), edges.mutable_data());
        return py::make_tuple(counts, edges);
    });
    m.def("statistics", [](const py::array& a, const py::object& mask) {
        auto image = read_image(a); auto selection = read_optional(mask);
        auto stats = [&]() { py::gil_scoped_release release; return statistics(image, selection.get()); }(); return statistics_dict(stats);
    });
    m.def("auto_threshold", [](const py::array& a, const std::string& method, int bins) {
        auto image = read_image(a); py::gil_scoped_release release; return auto_threshold(image, method, bins);
    });
    m.def("auto_mask", [](const py::array& a, const std::string& method, bool bright, int bins) { return image_call(a, [&](const Image& i) { return auto_mask(i, method, bright, bins); }); });
    m.def("equalize_histogram", [](const py::array& a) { return image_call(a, [](const Image& i) { return equalize_histogram(i); }); });
    m.def("project", [](const py::array& a, const std::string& method) {
        auto stack = read_stack(a); auto out = [&]() { py::gil_scoped_release release; return project(stack, method); }(); return to_array(out);
    });
    m.def("gaussian_blur3d", [](const py::array& a, double sx, double sy, double sz, const std::string& border) {
        auto stack = read_stack(a); auto b = parse_border(border);
        auto blurred = [&]() { py::gil_scoped_release release; return gaussian_blur3d(stack, sx, sy, sz, b); }();
        py::list out; for (const auto& slice : blurred) out.append(to_array(slice)); return out;
    });
    m.def("connected_components", [](const py::array& a, int connectivity) {
        auto image = read_image(a); auto labels = [&]() { py::gil_scoped_release release; return connected_components(image, connectivity); }();
        return py::make_tuple(label_array(labels), labels.count);
    });
    m.def("region_properties", [](const py::array& a, const py::object& intensity, double sx, double sy) {
        auto labels = read_labels(a); auto image = read_optional(intensity);
        auto properties = [&]() { py::gil_scoped_release release; return region_properties(labels, image.get(), sx, sy); }();
        py::list out; for (const auto& p : properties) out.append(particle_dict(p)); return out;
    });
    m.def("fill_holes", [](const py::array& a, int connectivity) { return image_call(a, [&](const Image& i) { return fill_holes(i, connectivity); }); });
    m.def("skeletonize", [](const py::array& a) { return image_call(a, [](const Image& i) { return skeletonize(i); }); });
    m.def("binary_outline", [](const py::array& a) { return image_call(a, [](const Image& i) { return binary_outline(i); }); });
    m.def("remove_small_objects", [](const py::array& a, std::uint64_t size, int connectivity) { return image_call(a, [&](const Image& i) { return remove_small_objects(i, size, connectivity); }); });
    m.def("clear_border", [](const py::array& a, int connectivity) { return image_call(a, [&](const Image& i) { return clear_border(i, connectivity); }); });
    m.def("distance_transform", [](const py::array& a, double sx, double sy) { return image_call(a, [&](const Image& i) { return distance_transform(i, sx, sy); }); });
    m.def("local_maxima", [](const py::array& a, int radius) { return image_call(a, [&](const Image& i) { return local_maxima(i, radius); }); });
    m.def("watershed", [](const py::array& a, const py::array& b, const py::object& mask, int connectivity) {
        auto image = read_image(a); auto markers = read_labels(b); auto selection = read_optional(mask);
        auto out = [&]() { py::gil_scoped_release release; return watershed(image, markers, selection.get(), connectivity); }(); return label_array(out);
    });
    m.def("binary_watershed", [](const py::array& a, int radius, int connectivity) {
        auto image = read_image(a); auto out = [&]() { py::gil_scoped_release release; return binary_watershed(image, radius, connectivity); }(); return label_array(out);
    });
    m.def("fft2", [](const py::array& a, bool pad) { auto image = read_image(a); auto out = [&]() { py::gil_scoped_release release; return fft2(image, pad); }(); return spectrum_array(out); });
    m.def("ifft2", [](const py::array& a) { auto spectrum = read_spectrum(a); auto out = [&]() { py::gil_scoped_release release; return ifft2(spectrum); }(); return to_array(out); });
    m.def("power_spectrum", [](const py::array& a, bool log_scale, bool shift) { auto spectrum = read_spectrum(a); auto out = [&]() { py::gil_scoped_release release; return power_spectrum(spectrum, log_scale, shift); }(); return to_array(out); });
    m.def("fft_bandpass", [](const py::array& a, double low, double high) { return image_call(a, [&](const Image& i) { return fft_bandpass(i, low, high); }); });
    m.def("read_pnm", [](const std::string& path) { auto out = [&]() { py::gil_scoped_release release; return read_pnm(path); }(); return to_array(out); });
    m.def("write_pnm", [](const py::array& a, const std::string& path) { auto image = read_image(a); py::gil_scoped_release release; write_pnm(image, path); });
}
