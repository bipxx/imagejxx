#include <imagejxx/operations.hpp>
#include <iostream>
#include <set>

using namespace imagejxx;
namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
  bool rejected = false;
  try {
    f();
  } catch (const std::exception &) {
    rejected = true;
  }
  check(rejected, "Invalid operation was accepted");
}
} // namespace
int main() {
  try {
    std::set<std::string> ids;
    const std::set<std::string> cancellable{
        "rank_filter_imagej", "remove_outliers_imagej", "top_hat_imagej",
        "remove_nans_imagej"};
    for (const auto &entry : operation_registry()) {
      check(ids.insert(entry.id).second, "Unique operation IDs");
      check(entry.planeOnly && entry.cancellationInsideKernel ==
                                  (cancellable.count(entry.id) != 0),
            "Accurate current execution capabilities");
    }
    check(ids.size() == 91 && ids.count("gaussian_blur") == 1 &&
              ids.count("gaussian_blur_imagej") == 1 &&
              ids.count("rank_filter_imagej") == 1 &&
              ids.count("remove_outliers_imagej") == 1 &&
              ids.count("top_hat_imagej") == 1 &&
              ids.count("remove_nans_imagej") == 1,
          "Every existing processing operation has a native contract");
    Image source(7, 5, 1, PixelType::UInt16);
    for (std::size_t k = 0; k < source.pixels.size(); ++k)
      source.pixels[k] = double(k + 1);
    auto copy = source.pixels;
    auto changed = execute_operation("add", source, {{"value", 2.5}});
    check(changed.image.at(0, 0) == 4 && source.pixels == copy,
          "Canonical output, immutable source");
    auto nativeDefaults = operation_defaults("add", &source);
    auto uiDefaults =
        operation_defaults("add", &source, DefaultProfile::DesktopUI);
    check(std::get<std::int64_t>(nativeDefaults.at("value")) == 0 &&
              std::get<std::int64_t>(uiDefaults.at("value")) == 10,
          "Legacy execution and UI defaults preserved separately");
    auto resized =
        operation_defaults("resize", &source, DefaultProfile::DesktopUI);
    check(std::get<std::int64_t>(resized.at("width")) == 7 &&
              std::get<std::int64_t>(resized.at("height")) == 5,
          "Defaults use source dimensions");
    OperationDefaultContext context{40000, 50000, 3, PixelType::UInt16, {}};
    auto channels =
        operation_defaults("in_range", context, DefaultProfile::DesktopUI);
    check(std::get<NumericArray>(channels.at("upper")).values ==
              std::vector<double>({65535, 65535, 65535}),
          "Metadata-only defaults need no pixel allocation");
    auto lutDefaults =
        operation_defaults("apply_lut", context, DefaultProfile::DesktopUI);
    const auto &lut = std::get<NumericArray>(lutDefaults.at("lut"));
    check(lut.shape == std::vector<std::size_t>{65536} &&
              lut.values.front() == 0 && lut.values.back() == 65535,
          "16-bit LUT defaults cover every input value");
    validate_operation_record("resize");
    validate_operation_record("add_noise",
                              {{"seed", std::uint64_t(UINT64_MAX)}});
    rejects([&] { validate_operation_record("add", {{"typo", 2.}}); });
    rejects([&] { validate_operation_record("convolve"); });
    rejects([&] {
      validate_operation_record("flip", {{"horizontal", std::string("false")}});
    });
    rejects([&] {
      validate_operation_record("gaussian_blur",
                                {{"sigma", NumericArray{{3}, {1, 2, 3}}}});
    });
    auto gauss = execute_operation("gaussian_blur", source,
                                   {{"sigma", NumericArray{{2}, {.5, 1.5}}}});
    auto reference = canonical_image(gaussian_blur(source, 1.5, .5));
    check(gauss.image.pixels == reference.pixels,
          "Y/X Gaussian parameters keep their legacy ordering");
    auto calculator =
        execute_operation("calculate", source,
                          {{"other", std::make_shared<const Image>(source)},
                           {"operation", std::string("add")},
                           {"dtype", std::string("float64")}});
    check(calculator.image.type == PixelType::Float64 &&
              calculator.image.at(6, 4) == 70,
          "Typed image parameters and output type");
    auto noise = execute_operation("add_noise", source,
                                   {{"kind", std::string("uniform")},
                                    {"amount", 10.},
                                    {"seed", std::uint64_t(UINT64_MAX)}});
    auto again = execute_operation("add_noise", source,
                                   {{"kind", std::string("uniform")},
                                    {"amount", 10.},
                                    {"seed", std::uint64_t(UINT64_MAX)}});
    check(noise.image.pixels == again.image.pixels,
          "Full-width uint64 seed remains exact");
    Image binary(5, 4, 1, PixelType::UInt8);
    binary.at(1, 1) = 255;
    binary.at(3, 2) = 255;
    auto labels = execute_operation(
        "label", binary,
        {{"return_num", true}, {"connectivity", std::int64_t(4)}});
    check(labels.labelCount == 2 && labels.image.type == PixelType::Float64,
          "Requested label count is an explicit output");
    rejects([&] { execute_operation("missing", source); });
    rejects([&] { execute_operation("add", source, {{"typo", 1.}}); });
    rejects([&] {
      execute_operation("flip", source, {{"horizontal", std::string("yes")}});
    });
    rejects([&] { execute_operation("smooth", source, {{"radius", 1.5}}); });
    rejects([&] {
      execute_operation("gaussian_blur", source,
                        {{"sigma", NumericArray{{3}, {1, 2, 3}}}});
    });
    rejects([&] { execute_operation("binning", source, {{"factor", 1.5}}); });
    rejects([&] {
      execute_operation("convolve", source,
                        {{"kernel", NumericArray{{3, 3}, {1, 2}}}});
    });
    rejects([&] { execute_operation("add_noise", source, {{"seed", -1.}}); });
    rejects([&] { execute_operation("convolve", source); });
    rejects([&] {
      execute_operation("fill_color", source,
                        {{"color", std::string("invalid")}});
    });
    check(source.pixels == copy,
          "Failed operations cannot alter source pixels");
    std::cout << "Headless operation registry and typed execution passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
