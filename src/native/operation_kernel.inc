Image execute_kernel(const std::string &id, const Image &image,
                     const OperationParameters &p,
                     std::optional<int> &labelCount) {
  auto n = [&](const char *key, double d = 0) {
    return parameterNumber(p, key, d);
  };
  auto i = [&](const char *key, int d = 0) {
    return parameterInteger(p, key, d);
  };
  auto b = [&](const char *key, bool d = false) {
    return parameterBoolean(p, key, d);
  };
  auto s = [&](const char *key, const char *d = "") {
    return parameterText(p, key, d);
  };
  const auto border = parse_border(s("border", "reflect"));
  const auto interpolation = parse_interpolation(s("interpolation", "linear"));
  const auto name = id;
  if (id == "convert")
    return convert(image,
                   operationPixelType(parameterText(p, "dtype", "uint8")),
                   b("scale"));
  if (id == "grayscale")
    return grayscale(image);
  if (id == "convert_image")
    return convert_image(image, operationPixelType(s("dtype", "uint8")),
                         conversionOptions(p));
  if (id == "invert_uint16")
    return invert_uint16(image, conversionOptions(p));
  if (id == "math")
    return point(image, s("operation", "add"), n("value"));
  if (id == "absolute")
    return point(image, "abs");
  if (id == "invert" || id == "add" || id == "subtract" || id == "multiply" ||
      id == "divide" || id == "square" || id == "sqrt" || id == "log" ||
      id == "log1p" || id == "exp" || id == "gamma")
    return point(image, name, n("value"));
  if (id == "clip") {
    if (n("lower") > n("upper"))
      throw std::invalid_argument("Lower exceeds upper");
    return point(point(image, "maximum", n("lower")), "minimum", n("upper"));
  }
  if (id == "adjust_brightness_contrast")
    return linear(image, n("contrast", 1),
                  n("center") * (1 - n("contrast", 1)) + n("brightness"));
  if (id == "normalize")
    return normalize(image, n("lower"), n("upper", type_max(image.type)),
                     n("saturation"));
  if (id == "equalize_histogram")
    return equalize_histogram(image);
  if (id == "calculate") {
    auto source = parameterImage(p, "other");
    auto output = calculate(image, source, s("operation", "add"));
    if (std::holds_alternative<std::string>(get_parameter(p, "dtype")) &&
        parameterText(p, "dtype", "preserve") != "preserve") {
      output.type = operationPixelType(parameterText(p, "dtype", "preserve"));
    }
    return output;
  }
  if (id == "apply_lut")
    return apply_lut(image, vector(get_parameter(p, "lut")));
  if (id == "threshold")
    return threshold(image, n("lower"),
                     n("upper", std::numeric_limits<double>::infinity()));
  if (id == "auto_mask")
    return auto_mask(image, s("method", "otsu"), b("bright", true), i("bins"));
  if (id == "in_range")
    return in_range(image, vector(get_parameter(p, "lower")),
                    vector(get_parameter(p, "upper")));
  if (id == "local_threshold") {
    const auto method = s("method", "sauvola");
    return local_threshold(
        image, method, i("radius", 15),
        n("k", method == "niblack"      ? -.2
               : method == "phansalkar" ? .25
                                        : .2),
        n("offset"), n("dynamic_range", type_max(image.type) / 2),
        n("contrast_threshold", type_max(image.type) * 15 / 255));
  }
  if (id == "fill")
    return fill(image, n("value"));
  if (id == "add_noise") {
    return add_noise(image, s("kind", "gaussian"), n("amount"),
                     parameterUnsigned(p, "seed"));
  }
  if (id == "gaussian_blur_imagej") {
    const auto sigma = pair(get_parameter(p, "sigma"));
    return gaussian_blur_imagej(image, sigma[1], sigma[0]);
  }
  if (id == "gaussian_blur") {
    auto v = pair(get_parameter(p, "sigma"));
    return gaussian_blur(image, v[1], v[0], border);
  }
  if (id == "smooth")
    return box_blur(image, i("radius", 1), border);
  if (id == "shadow_imagej")
    return shadow_imagej(image, s("direction", "north"));
  if (id == "sharpen")
    return convolve(image, {-1, -1, -1, -1, 12, -1, -1, -1, -1}, 3, 3, border,
                    0, true);
  if (id == "convolve") {
    auto rows = get_parameter(p, "kernel");
    return convolve(image, vector(get_parameter(p, "kernel")),
                    array_dimension(rows, 1), array_dimension(rows, 0), border,
                    n("cval"), b("normalize"));
  }
  if (id == "remove_nans_imagej")
    return remove_nans_imagej(image, n("radius", 2.0));
  if (id == "top_hat_imagej")
    return top_hat_imagej(image, n("radius", 2.0), b("light_background", false),
        b("dont_subtract", false), b("inverted_lut", false));
  if (id == "remove_outliers_imagej")
    return remove_outliers_imagej(image, n("radius", 2.0), n("threshold", 50.0),
                                  s("which", "bright"), b("inverted_lut"));
  if (id == "rank_filter_imagej")
    return rank_filter_imagej(image, parse_rank_filter_method(s("method", "median")),
                              n("radius", 2.0));
  if (id == "rank_filter" || id == "median_filter" || id == "minimum_filter" ||
      id == "maximum_filter" || id == "variance_filter") {
    auto operation = id == "rank_filter" ? s("method", "median")
                                         : name.substr(0, name.find('_'));
    return rank_filter(image, i("radius", 1), operation,
                       s("footprint", "disk") == "disk", border, n("cval"));
  }
  if (id == "find_edges")
    return sobel(image, border);
  if (id == "laplacian")
    return convolve(convert(image, PixelType::Float64),
                    {0, 1, 0, 1, -4, 1, 0, 1, 0}, 3, 3, border);
  if (id == "difference_of_gaussians") {
    if (n("sigma_small", 1) > n("sigma_large", 2))
      throw std::invalid_argument("Small sigma exceeds large sigma");
    auto work = convert(image, PixelType::Float64);
    return calculate(
        gaussian_blur(work, n("sigma_small", 1), n("sigma_small", 1)),
        gaussian_blur(work, n("sigma_large", 2), n("sigma_large", 2)),
        "subtract");
  }
  if (id == "unsharp_mask")
    return unsharp_mask(image, n("sigma", 1), n("amount", 1));
  if (id == "bilateral_filter")
    return bilateral_filter(image, i("radius", 3), n("sigma_spatial", 2),
                            n("sigma_range", 25));
  if (id == "kuwahara_filter")
    return kuwahara_filter(image, i("radius", 2));
  if (id == "anisotropic_diffusion")
    return anisotropic_diffusion(image, i("iterations", 10), n("kappa", 20),
                                 n("step", .2));
  if (id == "clahe") {
    auto v = pair(get_parameter(p, "tile_size"), 64);
    return clahe(image, int(v[1]), int(v[0]), n("clip_limit", 2));
  }
  if (id == "morphology" || id == "erode" || id == "dilate" ||
      id == "opening" || id == "closing" || id == "top_hat" ||
      id == "black_hat") {
    auto op = id == "morphology" ? s("operation", "erode")
                                 : (id == "opening"   ? "open"
                                    : id == "closing" ? "close"
                                                      : name);
    return morphology(image, op, i("radius", 1),
                      s("footprint", "disk") == "disk", border, n("cval"));
  }
  if (id == "binary_erode" || id == "binary_dilate" || id == "binary_open" ||
      id == "binary_close")
    return binary_morphology(image, id.substr(7),
                             {i("iterations", 1), i("neighbors", 1),
                              b("black_background", true),
                              b("pad_edges", false)});
  if (id == "subtract_background")
    return subtract_background(image, i("radius", 10), b("light_background"));
  if (id == "rolling_ball_background")
    return rolling_ball_background(image, n("radius", 10),
                                   b("light_background"));
  if (id == "subtract_rolling_ball")
    return subtract_rolling_ball(image, n("radius", 10), b("light_background"));
  if (id == "remove_outliers")
    return remove_outliers(image, i("radius", 2), n("threshold", 50),
                           b("bright", true));
  if (id == "crop")
    return crop(image, i("x"), i("y"), i("width", image.width),
                i("height", image.height));
  if (id == "paste")
    return paste(image, parameterImage(p, "source"), i("x"), i("y"));
  if (id == "flip")
    return flip(image, b("horizontal"), b("vertical"));
  if (id == "rotate90")
    return rotate90(image, i("turns", 1));
  if (id == "resize")
    return resize(image, i("width", image.width), i("height", image.height),
                  interpolation);
  if (id == "rotate")
    return rotate(image, n("degrees"), b("expand"), interpolation, n("cval"));
  if (id == "affine_transform" || id == "translate") {
    std::array<double, 6> matrix{1, 0, -n("dx"), 0, 1, -n("dy")};
    if (id == "affine_transform") {
      auto v = vector(get_parameter(p, "inverse_matrix"));
      if (v.size() != 6)
        throw std::invalid_argument("Inverse matrix needs six values");
      std::copy(v.begin(), v.end(), matrix.begin());
    }
    auto size = pair(get_parameter(p, "output_shape"));
    int w = image.width, h = image.height;
    if (std::holds_alternative<NumericArray>(
            get_parameter(p, "output_shape"))) {
      w = int(size[1]);
      h = int(size[0]);
    }
    return affine_transform(image, matrix, w, h, interpolation,
                            id == "translate"
                                ? Border::Constant
                                : parse_border(s("border", "constant")),
                            n("cval"));
  }
  if (id == "binning") {
    auto v = pair(get_parameter(p, "factor"), 2);
    return bin(image, int(v[1]), int(v[0]), s("method", "mean"));
  }
  if (id == "resize_canvas") {
    auto v = pair(get_parameter(p, "offset"), 0);
    return resize_canvas(image, i("width", image.width),
                         i("height", image.height), int(v[0]), int(v[1]),
                         n("value"));
  }
  if (id == "draw_line") {
    auto a = pair(get_parameter(p, "start"), 0),
         z = pair(get_parameter(p, "end"), 0);
    return draw_line(image, a[0], a[1], z[0], z[1], n("value", 255),
                     n("thickness", 1));
  }
  if (id == "fill_color" || id == "paint_stroke") {
    auto paint = parse_color(parameterText(p, "color", "#ffffff"));
    auto planes = split_channels(image);
    std::vector<double> colors{double(paint[0]), double(paint[1]),
                               double(paint[2]), double(paint[3])};
    if (image.channels == 1)
      colors = {
          std::floor(.299 * paint[0] + .587 * paint[1] + .114 * paint[2] + .5)};
    const NumericArray noPoints{{0, 2}, {}};
    const auto &points =
        id == "paint_stroke"
            ? std::get<NumericArray>(get_parameter(p, "points"))
            : noPoints;
    for (int c = 0; c < image.channels; ++c) {
      double value = colors[c] * type_max(image.type) / 255;
      if (id == "fill_color")
        planes[c] = fill(planes[c], value);
      else
        for (int k = 0; k < array_dimension(get_parameter(p, "points"), 0);
             ++k) {
          std::array<double, 2> a{points.values[2 * std::max(0, k - 1)],
                                  points.values[2 * std::max(0, k - 1) + 1]},
              z{points.values[2 * k], points.values[2 * k + 1]};
          planes[c] = canonical_image(draw_line(planes[c], a[0], a[1], z[0],
                                                z[1], value, n("width", 8)));
        }
    }
    return merge_channels(planes);
  }
  if (id == "rgb_to_hsv")
    return rgb_to_hsv(image);
  if (id == "hsv_to_rgb")
    return hsv_to_rgb(image,
                      operationPixelType(parameterText(p, "dtype", "uint8")));
  if (id == "color_deconvolution") {
    auto v = vector(get_parameter(p, "stains"));
    if (v.size() != 9)
      throw std::invalid_argument("Stains need a 3x3 matrix");
    std::array<double, 9> m;
    std::copy(v.begin(), v.end(), m.begin());
    return color_deconvolution(image, m);
  }
  if (id == "fill_holes")
    return fill_holes(image, i("connectivity", 4));
  if (id == "binary_outline")
    return binary_outline(image);
  if (id == "skeletonize")
    return skeletonize(image);
  if (id == "remove_small_objects") {
    if (i("min_size", 64) < 0)
      throw std::invalid_argument("Minimum size must be nonnegative");
    return remove_small_objects(image, i("min_size", 64), i("connectivity", 8));
  }
  if (id == "clear_border")
    return clear_border(image, i("connectivity", 8));
  if (id == "distance_transform") {
    auto v = pair(get_parameter(p, "spacing"));
    return distance_transform(image, v[1], v[0]);
  }
  if (id == "local_maxima")
    return local_maxima(image, i("radius", 1));
  if (id == "binary_watershed")
    return operationLabels(
        binary_watershed(image, i("maxima_radius", 1), i("connectivity", 8)));
  if (id == "label") {
    auto labels = connected_components(image, i("connectivity", 8));
    if (b("return_num"))
      labelCount = labels.count;
    return operationLabels(labels);
  }
  if (id == "fft_bandpass")
    return fft_bandpass(image, n("low_cutoff"), n("high_cutoff"));
  throw std::invalid_argument("Unknown native command: " + name);
}
