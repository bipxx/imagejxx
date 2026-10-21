#include <algorithm>
#include <cctype>
#include <cmath>
#include <imagejxx/settings.hpp>
#include <imagejxx/operations.hpp>
#include <imagejxx/shortcuts.hpp>
#include <nlohmann/json.hpp>
#include <limits>
#include <stdexcept>
#include <utility>

namespace imagejxx {
namespace {
SettingValue validate(const SettingDefinition &d, SettingValue value) {
  auto invalid = [&](const std::string &reason) -> void {
    throw std::invalid_argument(d.name + ": " + reason);
  };
  if (d.kind == SettingKind::Boolean) {
    if (!std::holds_alternative<bool>(value))
      invalid("expected a boolean");
  } else if (d.kind == SettingKind::Integer) {
    if (!std::holds_alternative<std::int64_t>(value))
      invalid("expected an integer");
    auto number = std::get<std::int64_t>(value);
    if (number < d.minimum || number > d.maximum)
      invalid("value is outside the supported range");
    if (d.name == "wand_connectivity" && number != 4 && number != 8)
      invalid("expected 4 or 8");
    if (d.name == "profile_samples" && number == 1)
      invalid("use zero for automatic sampling or at least two samples");
  } else if (d.kind == SettingKind::Number) {
    if (auto integer = std::get_if<std::int64_t>(&value))
      value = static_cast<double>(*integer);
    if (!std::holds_alternative<double>(value))
      invalid("expected a number");
    auto number = std::get<double>(value);
    if (!std::isfinite(number) || number < d.minimum || number > d.maximum)
      invalid("value must be finite and within the supported range");
  } else if (d.kind == SettingKind::Text) {
    if (!std::holds_alternative<std::string>(value))
      invalid("expected text");
    const auto &text = std::get<std::string>(value);
    if (text.size() > 4096 || text.find('\0') != std::string::npos)
      invalid("text must contain no NUL and at most 4096 UTF-8 bytes");
    try {
      (void)nlohmann::json(text).dump();
    } catch (const nlohmann::json::exception &) {
      invalid("text must be strict UTF-8");
    }
  } else if (d.kind == SettingKind::Choice || d.kind == SettingKind::Color) {
    if (!std::holds_alternative<std::string>(value))
      invalid("expected text");
    auto &text = std::get<std::string>(value);
    if (d.kind == SettingKind::Choice) {
      if (std::find(d.choices.begin(), d.choices.end(), text) ==
          d.choices.end())
        invalid("unsupported choice");
    } else {
      if ((text.size() != 7 && text.size() != 9) || text[0] != '#' ||
          !std::all_of(text.begin() + 1, text.end(),
                       [](unsigned char c) { return std::isxdigit(c); }))
        invalid("expected #RRGGBB or #RRGGBBAA");
      std::transform(
          text.begin(), text.end(), text.begin(),
          [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    }
  } else if (d.name == "shortcut_bindings") {
    if (!std::holds_alternative<std::vector<std::string>>(value))
      invalid("expected a list of shortcut override lines");
    // This is the sole Settings validator for this key. The native compiler
    // owns grammar, complete-map portable conflicts, caps and canonical order.
    value = compile_shortcuts(std::get<std::vector<std::string>>(value))
                .override_lines();
  } else {
    if (!std::holds_alternative<std::vector<std::string>>(value))
      invalid("expected a list of column names");
    auto &columns = std::get<std::vector<std::string>>(value);
    if (columns.size() > 1024)
      invalid("too many columns");
    std::vector<std::string> seen;
    for (const auto &column : columns) {
      if (column.empty() || column.size() > 256 ||
          column.find_first_not_of(" \t\r\n") == std::string::npos)
        invalid("column names must be nonempty and at most 256 characters");
      if (std::find(seen.begin(), seen.end(), column) != seen.end())
        invalid("duplicate column: " + column);
      seen.push_back(column);
    }
  }
  return value;
}
void validateStartupPair(const SettingsPatch &values) {
  if (std::get<std::string>(values.at("startup_mode")) == "workspace" &&
      std::get<std::string>(values.at("startup_workspace")).empty())
    throw std::invalid_argument(
        "startup_mode: workspace mode requires a nonempty startup_workspace");
}
} // namespace
const std::vector<SettingDefinition> &Settings::schema() {
  static const std::vector<SettingDefinition> definitions{
      {"theme",
       "Appearance",
       "Theme",
       "Colors of desktop controls.",
       SettingKind::Choice,
       std::string("dark"),
       0,
       0,
       {"dark", "light", "classic"}},
      {"ui_scale", "Appearance", "UI and font scale",
       "Scale controls and text relative to the display scale.",
       SettingKind::Number, 1., .5, 3},
      {"auto_dpi", "Appearance", "Follow display DPI",
       "Apply the operating system display scale to controls and fonts.",
       SettingKind::Boolean, true},
      {"canvas_background", "Appearance", "Canvas background",
       "Color behind images in the image canvas.", SettingKind::Color,
       std::string("#181b21")},
      {"default_zoom", "Appearance", "Initial image zoom",
       "Zero fits new images; positive values are screen pixels per image X "
       "pixel.",
       SettingKind::Number, 0., 0, 64},
      {"interpolation",
       "Appearance",
       "Image interpolation",
       "How displayed images are sampled when zoomed; does not change pixels.",
       SettingKind::Choice,
       std::string("nearest"),
       0,
       0,
       {"nearest", "linear"}},
      {"foreground", "Drawing", "Paint color",
       "Default color for fill, brush and annotations.", SettingKind::Color,
       std::string("#ffffff")},
      {"background", "Drawing", "Erase color",
       "Default color for clearing pixels and the eraser.", SettingKind::Color,
       std::string("#000000")},
      {"brush_width", "Drawing", "Brush width",
       "Brush and eraser diameter in image pixels.", SettingKind::Number, 8., 1,
       256},
      {"rounded_corner_diameter", "Drawing", "Rounded rectangle corner diameter",
       "Corner diameter in image pixels, clamped to the box dimensions.",
       SettingKind::Integer, std::int64_t(8), 0, 2147483647.},
      {"line_width", "Drawing", "Line selection width",
       "Raster width of line, polyline and angle selections.",
       SettingKind::Number, 1., 1, 256},
      {"point_size", "Drawing", "Point size",
       "Raster diameter of point and multipoint selections.",
       SettingKind::Number, 8., 1, 256},
      {"text_size", "Drawing", "Text size",
       "Default annotation font size in image pixels.", SettingKind::Integer,
       std::int64_t(16), 1, 256},
      {"show_rois", "Selection", "Show ROIs",
       "Display positioned saved ROI outlines.", SettingKind::Boolean, true},
      {"wand_tolerance", "Selection", "Wand tolerance",
       "Maximum intensity difference from the seed pixel.", SettingKind::Number,
       0., 0, std::numeric_limits<double>::max()},
      {"wand_connectivity", "Selection", "Wand connectivity",
       "Use four or eight neighboring pixels for wand flood fill.",
       SettingKind::Integer, std::int64_t(4), 4, 8},
      {"animation_fps", "Playback", "Frames per second",
       "Z, channel or time playback rate.", SettingKind::Number, 8., .1, 120},
      {"conversion_scale", "Conversion", "Scale when converting",
       "Scale 16-bit/float to 8-bit and float to 16-bit with the chosen source "
       "range.",
       SettingKind::Boolean, true},
      {"conversion_weighted_rgb", "Conversion", "Weighted RGB conversion",
       "Use the configured RGB weights; otherwise use equal one-third weights.",
       SettingKind::Boolean, false},
      {"conversion_red_weight", "Conversion", "Red weight",
       "Red coefficient for weighted grayscale conversion.",
       SettingKind::Number, .299, -std::numeric_limits<double>::max(),
       std::numeric_limits<double>::max()},
      {"conversion_green_weight", "Conversion", "Green weight",
       "Green coefficient for weighted grayscale conversion.",
       SettingKind::Number, .587, -std::numeric_limits<double>::max(),
       std::numeric_limits<double>::max()},
      {"conversion_blue_weight", "Conversion", "Blue weight",
       "Blue coefficient for weighted grayscale conversion.",
       SettingKind::Number, .114, -std::numeric_limits<double>::max(),
       std::numeric_limits<double>::max()},
      {"conversion_range_policy",
       "Conversion",
       "Conversion source range",
       "Display captures the current range or unsigned 16-bit default, falling "
       "back to per-plane data. Data uses per-plane extrema. Dtype uses the "
       "native type range.",
       SettingKind::Choice,
       std::string("display"),
       0,
       0,
       {"display", "data", "dtype"}},
      {"conversion_full_range_inversions", "Conversion",
       "Full-range 16-bit inversion",
       "Invert unsigned 16-bit pixels using the configured bit range instead "
       "of observed image extrema.",
       SettingKind::Boolean, false},
      {"conversion_unsigned16_range",
       "Conversion",
       "Unsigned 16-bit range (bits)",
       "Auto uses data extrema for display conversion and 16 bits for "
       "full-range inversion. Fixed choices set the maximum to 2^bits-1.",
       SettingKind::Choice,
       std::string("auto"),
       0,
       0,
       {"auto", "8", "10", "12", "14", "15", "16"}},
      {"threshold_method", "Threshold", "Auto method",
       "Last method used by Auto in the manual Threshold panel.",
       SettingKind::Choice, std::string("default"), 0, 0, [] {
         std::vector<std::string> methods;
         for (const auto &field : operation_spec("auto_mask").parameters)
           if (field.name == "method")
             for (const auto &choice : field.choices)
               methods.push_back(std::get<std::string>(choice));
         return methods;
       }()},
      {"threshold_histogram_roi", "Threshold", "Use target ROI for Auto histogram",
       "Estimate Auto from target ROI members; overlays still show raw range membership.",
       SettingKind::Boolean, false},
      {"threshold_apply_roi", "Threshold", "Scientific Apply ROI",
       "Default ROI policy when configuring scientific threshold targets.",
       SettingKind::Choice, std::string("active_selection"), 0, 0,
       {"whole_plane","active_selection","positioned_rois"}},
      {"threshold_stack_histogram", "Threshold", "Use all Z slices",
       "Auto estimates over all Z at captured T/C, and Apply targets those slices.",
       SettingKind::Boolean, false},
      {"threshold_bright", "Threshold", "Bright objects",
       "Auto selects values above the threshold; otherwise selects at or below it.",
       SettingKind::Boolean, true},
      {"threshold_ignore_black", "Threshold", "Ignore black",
       "Exclude the first histogram bin when estimating an automatic threshold.",
       SettingKind::Boolean, false},
      {"threshold_ignore_white", "Threshold", "Ignore white",
       "Exclude the last histogram bin when estimating an automatic threshold.",
       SettingKind::Boolean, false},
      {"binary_iterations", "Binary processing", "Morphology iterations",
       "Repeat each erosion/dilation stage of the binary commands this many "
       "times.",
       SettingKind::Integer, std::int64_t(1), 1, 100},
      {"binary_neighbors", "Binary processing", "Neighbor count",
       "Required adjacent background pixels for erosion or foreground pixels "
       "for dilation (eight-neighbor neighborhood).",
       SettingKind::Integer, std::int64_t(1), 1, 8},
      {"binary_black_background", "Binary processing", "Black background",
       "Use foreground pixel value 255 and background 0; otherwise foreground "
       "is 0 and background 255.",
       SettingKind::Boolean, true},
      {"binary_pad_edges", "Binary processing", "Pad edges when eroding",
       "Treat pixels beyond the image boundary as foreground during binary "
       "erosion, including opening/closing stages.",
       SettingKind::Boolean, false},
      {"history_limit", "Resources", "Undo steps",
       "Maximum undo steps per document.", SettingKind::Integer,
       std::int64_t(8), 1, 128},
      {"history_memory_mb", "Resources", "Undo pixel budget (MiB)",
       "Per-document budget for pixel buffers held only by undo/redo. Zero "
       "retains the step limit without a byte limit.",
       SettingKind::Integer, std::int64_t(0), 0, 1048576},
      {"processing_workers", "Resources", "Processing workers",
       "Maximum simultaneous native processing jobs.", SettingKind::Integer,
       std::int64_t(2), 1, 64},
      {"processing_memory_mb", "Resources", "Processing reservations (MiB)",
       "Budget for declared running-job and opted-in retained-result "
       "reservations. Zero removes the reservation limit; document/history, "
       "reader/render and other allocations are separate.",
       SettingKind::Integer, std::int64_t(512), 0, 1048576},
      {"measurement_precision", "Measurements", "Decimal places",
       "Minus one preserves full precision; other values format the results "
       "view and CSV. Native results retain full precision.",
       SettingKind::Integer, std::int64_t(-1), -1, 15},
      {"measurements",
       "Measurements",
       "Results columns",
       "Choose visible/exported columns; an empty list shows every available "
       "column.",
       SettingKind::StringList,
       std::vector<std::string>{},
       0,
       0,
       {"Image",
        "Slice",
        "count",
        "minimum",
        "maximum",
        "mean",
        "stddev",
        "median",
        "sum",
        "integrated_density",
        "Area",
        "Unit",
        "ROI",
        "Bounds",
        "Length",
        "Angle",
        "label",
        "x",
        "y",
        "width",
        "height",
        "pixels",
        "area",
        "centroid_x",
        "centroid_y",
        "perimeter",
        "eccentricity",
        "circularity",
        "touches_border"}},
      {"profile_samples", "Measurements", "Profile samples per segment",
       "Zero samples every pixel interval; positive values specify samples per "
       "line segment.",
       SettingKind::Integer, std::int64_t(0), 0, 1000000},
      {"print_dpi", "Files", "PDF resolution (DPI)",
       "Default physical resolution for PDF export.", SettingKind::Number, 150.,
       1, 9600},
      {"print_landscape", "Files", "Landscape PDF",
       "Default page orientation for PDF export.", SettingKind::Boolean,
       false},
      {"startup_mode", "Startup", "At startup",
       "Start empty or restore an explicitly saved workspace on next launch.",
       SettingKind::Choice, std::string("empty"), 0, 0,
       {"empty", "workspace"}},
      {"startup_workspace", "Startup", "Saved workspace",
       "Path to an explicitly saved workspace. No automatic exit snapshot.",
       SettingKind::Text, std::string("")},
      {"shortcut_bindings", "Shortcuts", "Keyboard shortcuts",
       "Native command overrides. An empty list inherits registry defaults; "
       "an empty command right-hand side clears its binding.",
       SettingKind::StringList, std::vector<std::string>{}}};
  return definitions;
}
const SettingDefinition &Settings::definition(const std::string &name) {
  for (const auto &d : schema())
    if (d.name == name)
      return d;
  throw std::invalid_argument("Unknown setting: " + name);
}
Settings::Settings() { reset(); }
const SettingValue &Settings::at(const std::string &name) const {
  definition(name);
  return values_.at(name);
}
bool Settings::boolean(const std::string &name) const {
  return std::get<bool>(at(name));
}
std::int64_t Settings::integer(const std::string &name) const {
  return std::get<std::int64_t>(at(name));
}
double Settings::number(const std::string &name) const {
  return std::get<double>(at(name));
}
const std::string &Settings::text(const std::string &name) const {
  return std::get<std::string>(at(name));
}
const std::vector<std::string> &
Settings::strings(const std::string &name) const {
  return std::get<std::vector<std::string>>(at(name));
}
BinaryMorphologyOptions Settings::binary_options() const {
  BinaryMorphologyOptions options;
  options.iterations = static_cast<int>(integer("binary_iterations"));
  options.neighbors = static_cast<int>(integer("binary_neighbors"));
  options.black_background = boolean("binary_black_background");
  options.pad_edges = boolean("binary_pad_edges");
  options.validate();
  return options;
}
ConversionOptions Settings::conversion_options() const {
  ConversionOptions options;
  options.scale = boolean("conversion_scale");
  options.weighted_rgb = boolean("conversion_weighted_rgb");
  options.rgb_weights = {number("conversion_red_weight"),
                         number("conversion_green_weight"),
                         number("conversion_blue_weight")};
  options.range_policy = text("conversion_range_policy");
  options.full_range_inversions = boolean("conversion_full_range_inversions");
  const auto bits = text("conversion_unsigned16_range");
  options.unsigned16_bits = bits == "auto" ? 0 : std::stoi(bits);
  options.validate();
  return options;
}
void Settings::set(const std::string &name, SettingValue value) {
  update({{name, std::move(value)}});
}
void Settings::update(const SettingsPatch &patch) {
  auto proposed = values_;
  for (const auto &[name, value] : patch)
    proposed[name] = validate(definition(name), value);
  validateStartupPair(proposed);
  values_ = std::move(proposed);
}
void Settings::reset(const std::string &group) {
  bool found = false;
  auto proposed = values_;
  for (const auto &d : schema())
    if (group.empty() || group == d.group) {
      proposed[d.name] = d.name == "shortcut_bindings"
                             ? validate(d, d.default_value)
                             : d.default_value;
      found = true;
    }
  if (!found)
    throw std::invalid_argument("Unknown settings group: " + group);
  validateStartupPair(proposed);
  values_.swap(proposed);
}
Settings Settings::migrate(const SettingsPatch &knownValues,
                           std::vector<std::string> &warnings, bool legacy) {
  Settings result;
  for (const auto &[name, value] : knownValues) {
    // The two startup values form one configuration. In lexical map order,
    // validating startup_mode alone would discard a valid workspace pair.
    if (name == "startup_mode" || name == "startup_workspace")
      continue;
    try {
      auto migrated = value;
      if (legacy && (name == "foreground" || name == "background") &&
          std::holds_alternative<std::string>(migrated)) {
        // The old desktop accepted these names and short hexadecimal colors.
        // Keep that behavior in the file adapter; new settings use full hex.
        static const std::map<std::string, std::string> names{
            {"white", "#ffffff"}, {"black", "#000000"},   {"red", "#ff0000"},
            {"green", "#008000"}, {"blue", "#0000ff"},    {"yellow", "#ffff00"},
            {"cyan", "#00ffff"},  {"magenta", "#ff00ff"}, {"gray", "#808080"}};
        auto &text = std::get<std::string>(migrated);
        auto alias = names.find(text);
        if (alias != names.end())
          text = alias->second;
        else if (text.size() == 4 && text[0] == '#')
          text = std::string("#") + text[1] + text[1] + text[2] + text[2] +
                 text[3] + text[3];
      }
      result.set(name, std::move(migrated));
    } catch (const std::exception &error) {
      warnings.push_back(error.what());
    }
  }
  SettingsPatch startup;
  for (const auto *name : {"startup_mode", "startup_workspace"}) {
    const auto found = knownValues.find(name);
    if (found != knownValues.end())
      startup.emplace(found->first, found->second);
  }
  try {
    result.update(startup);
  } catch (const std::exception &error) {
    // Neither supplied startup value survives a bad supplied pair. Preserve
    // unrelated settings and their original structured validation behavior.
    warnings.push_back(std::string(error.what()) +
                       "; using safe Startup defaults");
  }
  if (legacy && knownValues.count("brush_width") &&
      !knownValues.count("point_size"))
    result.set("point_size", result.number("brush_width"));
  return result;
}
} // namespace imagejxx
