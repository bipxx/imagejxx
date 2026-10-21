#include "../../src/native/kernel_control.hpp"
#include <atomic>
#include <chrono>
#include <imagejxx/application.hpp>
#include <imagejxx/conversion.hpp>
#include <imagejxx/rgb_conversion.hpp>
#include <imagejxx/channel_display.hpp>
#include <imagejxx/settings.hpp>
#include <iostream>
#include <limits>
#ifdef IMAGEJXX_RGB_REFERENCE_FIXTURE
#include <fstream>
#include <nlohmann/json.hpp>
#endif

using namespace imagejxx;
namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class Exception = std::invalid_argument, class F>
void rejects(F work) {
  bool rejected = false;
  try {
    work();
  } catch (const Exception &) {
    rejected = true;
  }
  check(rejected, "Invalid or canceled conversion work was accepted");
}

class CountingSource final : public PlaneSource {
public:
  DatasetShape shape() const override { return {2, 2, 3, 8, 12, 1}; }
  PixelType pixel_type() const override { return PixelType::UInt16; }
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    ++reads;
    auto image = std::make_shared<Image>(12, 8, 1, PixelType::UInt16);
    for (std::size_t i = 0; i < image->pixels.size(); ++i)
      image->pixels[i] = 1000 * p.t + 100 * p.c + 10 * p.z + i;
    return image;
  }
  mutable std::atomic<int> reads{0};
};
void testRangesAndWeights() {
  Image u16(4, 1, 1, PixelType::UInt16);
  u16.pixels = {10, 20, 30, 40};
  check(convert_image(u16, PixelType::UInt8).pixels ==
            std::vector<double>({0, 83, 165, 248}),
        "Short-to-byte scaling must use the inclusive source range");
  ConversionOptions options;
  options.scale = false;
  check(convert_image(u16, PixelType::UInt8, options).pixels == u16.pixels,
        "Unscaled conversion changed raw in-range values");
  options.scale = true;
  options.range_policy = "explicit";
  options.input_range = std::array<double, 2>{0, 255};
  check(convert_image(u16, PixelType::UInt8, options).pixels == u16.pixels,
        "Explicit source range was ignored");
  Image f32(4, 1, 1, PixelType::Float32);
  f32.pixels = {10, 20, 30, 40};
  check(convert_image(f32, PixelType::UInt8).pixels ==
            std::vector<double>({0, 85, 170, 255}),
        "Float-to-byte scaling must use the continuous source range");
  Image color(3, 1, 4, PixelType::UInt8);
  color.pixels = {255, 0, 0, 11, 0, 255, 0, 22, 0, 0, 255, 33};
  check(convert_image(color, PixelType::UInt16).pixels ==
            std::vector<double>({85, 85, 85}),
        "RGB-to-short must use equal-weight byte grayscale and ignore alpha");
  options = {};
  options.weighted_rgb = true;
  check(convert_image(color, PixelType::UInt8, options).pixels ==
            std::vector<double>({76, 150, 29}),
        "Perceptual weights were ignored");
  options.rgb_weights = {1, 0, 0};
  check(convert_image(color, PixelType::Float32, options).pixels ==
            std::vector<double>({255, 0, 0}),
        "Custom RGB weights were ignored");
  check(invert_uint16(u16).pixels == std::vector<double>({40, 30, 20, 10}),
        "Default 16-bit inversion must use observed extrema");
  options.full_range_inversions = true;
  options.unsigned16_bits = 12;
  check(invert_uint16(u16, options).pixels ==
            std::vector<double>({4085, 4075, 4065, 4055}),
        "Full inversion must use the configured unsigned bit range");
  check(u16.pixels == std::vector<double>({10, 20, 30, 40}),
        "Conversion changed source pixels");
}
void testValidationAndSettings() {
  ConversionOptions options;
  options.unsigned16_bits = 9;
  rejects([&] { options.validate(); });
  options = {};
  options.range_policy = "explicit";
  rejects([&] { options.validate(); });
  options.input_range = std::array<double, 2>{10, 0};
  rejects([&] { options.validate(); });
  options = {};
  options.rgb_weights[0] = std::numeric_limits<double>::quiet_NaN();
  rejects([&] { options.validate(); });
  Image bad(2, 2, 1, PixelType::UInt8);
  bad.at(0, 0) = .5;
  rejects([&] { convert_image(bad, PixelType::UInt8); });
  bad.pixels.pop_back();
  rejects([&] { convert_image(bad, PixelType::UInt8); });
  rejects([&] { invert_uint16(Image(2, 2)); });
  rejects([&] {
    convert_image(Image(2, 2, 3, PixelType::UInt16), PixelType::UInt8);
  });
  Settings settings;
  settings.update({{"conversion_scale", false},
                   {"conversion_weighted_rgb", true},
                   {"conversion_red_weight", 1.},
                   {"conversion_green_weight", 0.},
                   {"conversion_blue_weight", 0.},
                   {"conversion_full_range_inversions", true},
                   {"conversion_unsigned16_range", std::string("12")}});
  const auto configured = settings.conversion_options();
  check(!configured.scale && configured.weighted_rgb &&
            configured.rgb_weights[0] == 1 &&
            configured.full_range_inversions &&
            configured.unsigned16_bits == 12,
        "Conversion defaults did not reach their native consumer");
  rejects(
      [&] { settings.set("conversion_unsigned16_range", std::string("9")); });
  Application app;
  auto owner = std::make_shared<Document>(Dataset::from_stack(
      {Image(4, 3, 1, PixelType::UInt16), Image(4, 3, 1, PixelType::UInt16)}));
  app.adopt(owner);
  rejects([&] { prepare_operation("convert_image", app.resolve_target()); });
  const auto target =
      resolve_legacy_target(owner->snapshot(), false, 0, "convert_image");
  check(target.position_count() == 2 &&
            target.roi_policy() == RoiPolicy::WholePlane,
        "Type conversion did not resolve the whole dataset");
  auto prepared = prepare_operation("convert_image", target);
  check(prepared.preview().data().dataset.pixel_type() == PixelType::UInt8,
        "Whole-dataset type conversion did not produce the requested type");
}
void testCancellation() {
  const Image image = fill(Image(128, 128, 1, PixelType::UInt16), 1234);
  for (const auto *name :
       {"Conversion input validation", "Conversion data range",
        "Conversion grayscale pixels", "Inversion uint16 pixels",
        "Conversion RGB pixels"}) {
    ExecutionControl control;
    control.cancellation = std::make_shared<CancellationToken>();
    std::size_t done = 0;
    control.stage_progress = [&](std::string_view stage, std::size_t completed,
                                 std::size_t total) {
      check(completed <= total, "Conversion phase progress exceeded its work");
      if (stage == name && completed == 1024) {
        done = completed;
        control.cancellation->cancel();
      }
    };
    bool canceled = false;
    try {
      ExecutionScope scope(control);
      if (std::string(name) == "Inversion uint16 pixels")
        (void)invert_uint16(image);
      else if (std::string(name) == "Conversion RGB pixels")
        (void)convert_image(Image(128, 128, 3, PixelType::UInt8),
                            PixelType::UInt8);
      else
        (void)convert_image(image, PixelType::UInt8);
    } catch (const OperationCancelled &) {
      canceled = true;
    }
    check(canceled && done == 1024,
          "Conversion did not cancel within its named phase");
  }
  check(image.at(0, 0) == 1234, "Canceled conversion changed the source");
}
void testTargetRejectionBeforeReads() {
  auto source = std::make_shared<CountingSource>();
  auto owner = std::make_shared<Document>(Dataset(source));
  owner->select(Selection("rectangle", {{1, 1}, {4, 5}}));
  auto records = owner->begin_edit();
  records.set_rois({Roi{
      "positioned", Selection("rectangle", {{2, 2}, {6, 6}}), 1, 1, 2, {}}});
  owner->commit(records);
  owner->mark_saved();
  Application app;
  app.adopt(owner);
  const auto before = owner->snapshot();
  const auto history = owner->history();
  TargetRequest incomplete;
  incomplete.roi = RoiPolicy::WholePlane;
  TargetRequest full;
  full.scope = TargetScope::Ranges;
  full.t = AxisRange{0, 2, 1};
  full.c = AxisRange{0, 2, 1};
  full.z = AxisRange{0, 3, 1};
  full.roi = RoiPolicy::ActiveSelection;
  auto positioned = full;
  positioned.roi = RoiPolicy::PositionedRois;
  for (const auto &request : {incomplete, full, positioned}) {
    auto target = app.resolve_target(request);
    rejects([&] { app.prepare("convert_image", target); });
    rejects([&] { app.submit("convert_image", target); });
    const auto after = owner->snapshot();
    check(source->reads == 0 && app.job_scheduler()->jobs().empty(),
          "Unsupported conversion target read lazy pixels or scheduled work");
    check(after.shares_data(before) &&
              after.revisions().matches(before.revisions(), Change::All) &&
              after.dirty() == before.dirty() &&
              owner->history().undo_steps == history.undo_steps &&
              owner->history().redo_steps == history.redo_steps &&
              app.document_ids() == std::vector<std::uint64_t>{owner->id},
          "Unsupported conversion target changed records/history/documents");
  }
}
void testPreparedAndQueuedCancellation() {
  const DatasetShape shape{2, 1, 2, 128, 128, 1};
  Stack originals;
  for (int i = 0; i < 4; ++i) {
    Image image(shape.x, shape.y, 1, PixelType::UInt16);
    for (std::size_t j = 0; j < image.pixels.size(); ++j)
      image.pixels[j] = 1000 * i + j % 251;
    originals.push_back(std::move(image));
  }
  auto owner =
      std::make_shared<Document>(Dataset::from_planes(shape, originals));
  owner->select(Selection("rectangle", {{1, 1}, {4, 5}}));
  Application app;
  app.adopt(owner);
  const auto before = owner->snapshot();
  const auto history = owner->history();
  const auto target = resolve_legacy_target(before, false, 0, "convert_image");
  const OperationParameters parameters{
      {"dtype", std::string("uint8")},
      {"range_policy", std::string("explicit")},
      {"range_min", 0.},
      {"range_max", 4095.}};
  auto unchanged = [&] {
    const auto after = owner->snapshot();
    check(after.shares_data(before) &&
              after.revisions().matches(before.revisions(), Change::All) &&
              after.dirty() == before.dirty() &&
              owner->history().undo_steps == history.undo_steps &&
              owner->history().redo_steps == history.redo_steps &&
              app.document_ids() == std::vector<std::uint64_t>{owner->id},
          "Canceled conversion changed source/history/records/documents");
    for (std::size_t i = 0; i < target.position_count(); ++i)
      check(after.data().dataset.plane(target.position(i))->pixels ==
                originals[i].pixels,
            "Canceled conversion published a partially converted plane");
  };
  for (bool queued : {false, true}) {
    std::size_t completedPlanes = 0, stoppedSamples = 0;
    std::optional<PreparedOperation> prepared;
    std::optional<PublishedOperation> published;
    auto cancelInsideSecondPlane = [&](ExecutionControl control) {
      if (!control.cancellation)
        control.cancellation = std::make_shared<CancellationToken>();
      const auto token = control.cancellation;
      const auto reportPlanes = control.progress;
      const auto reportStage = control.stage_progress;
      control.progress = [&, reportPlanes](std::size_t done,
                                           std::size_t total) {
        completedPlanes = done;
        if (reportPlanes)
          reportPlanes(done, total);
      };
      control.stage_progress = [&, reportStage, token](std::string_view name,
                                                       std::size_t done,
                                                       std::size_t total) {
        if (reportStage)
          reportStage(name, done, total);
        if (name == "Conversion grayscale pixels" && completedPlanes == 1 &&
            done == 1024) {
          stoppedSamples = done;
          token->cancel();
        }
      };
      return control;
    };
    if (queued) {
      auto job = app.job_scheduler()->submit([&](const JobContext &context) {
        prepared =
            app.prepare("convert_image", target, parameters, {},
                        cancelInsideSecondPlane(context.execution_control()));
      });
      check(job.wait_for(std::chrono::seconds(2)) &&
                job.info().state == JobState::Canceled,
            "Queued conversion did not cancel during its pixel phase");
      bool publicationCalled = false;
      rejects<OperationCancelled>([&] {
        job.complete([&] {
          publicationCalled = true;
          check(bool(prepared),
                "Canceled job called publication without a result");
          published = app.publish(*prepared);
        });
      });
      check(!publicationCalled, "Canceled job ran its publication callback");
    } else {
      rejects<OperationCancelled>([&] {
        prepared = app.prepare("convert_image", target, parameters, {},
                               cancelInsideSecondPlane({}));
        published = app.publish(*prepared);
      });
    }
    check(completedPlanes == 1 && stoppedSamples == 1024 && !prepared &&
              !published,
          "Conversion cancellation leaked partially prepared edits/results");
    unchanged();
  }
  // Also cover the public operation-job adapter: a complete held preview must
  // remain isolated when cancellation wins the Apply/publication race.
  auto job = app.submit("convert_image", target, parameters);
  check(job.wait_for(std::chrono::seconds(2)) &&
            job.info().state == JobState::Ready,
        "Conversion job did not prepare its multi-plane preview");
  auto retained = job.prepared();
  check(retained.outputs().size() == target.position_count() &&
            retained.preview().data().dataset.pixel_type() == PixelType::UInt8,
        "Conversion preview omitted target results or the requested dtype");
  unchanged();
  check(job.cancel(), "Ready conversion did not accept cancellation");
  auto canceledPreview = job.prepared();
  check(canceledPreview.preview().shares_data(retained.preview()),
        "Canceled conversion lost its retained readable preview");
  rejects<OperationCancelled>([&] { app.publish(job); });
  rejects<OperationCancelled>([&] { app.publish(retained); });
  check(retained.outputs().size() == target.position_count(),
        "Canceled held conversion preview lost its retained output records");
  unchanged();
}
void testScientificDisplayTransaction() {
  for (bool direct : {false, true}) {
    const DatasetShape shape{2, 2, 2, 2, 3, 1};
    Stack planes;
    for (int i = 0; i < 8; ++i) {
      Image image(shape.x, shape.y, 1, PixelType::UInt16);
      image.pixels = {10., 20., 30., 40., 50., 60.};
      planes.push_back(image);
    }
    auto owner = std::make_shared<Document>(Dataset::from_planes(shape, planes));
    auto display = owner->snapshot().display();
    display.t = display.c = 1;
    display.crosshair = {1, 1, 1};
    display.properties = {
        {"display_range", metadata_array({10., 60.})},
        {"manual_threshold", metadata_object({{"version", 1}, {"lower", 20.},
             {"upper", 50.}, {"t", 1}, {"c", 1}, {"z", 1}})},
        {"future_display", metadata_object({{"preserved", true}})}};
    display.properties["channel_display"] = metadata_object(encode_channel_display(
        default_channel_display(owner->snapshot().data().dataset, 1, 1), shape,
        PixelType::UInt16, FloatingSamplePolicy::FiniteOnly));
    owner->set_display(display);
    owner->mark_saved();
    const auto baseline = owner->snapshot();
    Application app;
    app.adopt(owner);
    const auto target = resolve_legacy_target(baseline, false, 0, "convert_image");
    auto prepared = prepare_operation("convert_image", target,
        {{"dtype", std::string("uint8")}, {"scale", false}});
    const auto expected = prepared.preview().display().properties;
    check(expected.size() == 1 && expected.count("future_display"),
          "Scientific conversion retained numeric display interpretation");
    check(owner->snapshot().shares_data(baseline) && owner->history().undo_steps == 0,
          "Scientific conversion mutated the live owner during preparation");
    if (direct) {
      auto tx = prepared.begin_edit();
      owner->commit(tx);
    } else
      app.publish(prepared);
    auto after = owner->snapshot();
    check(after.data().dataset.pixel_type() == PixelType::UInt8 &&
              metadata_equal(after.display().properties, expected) &&
              after.display().t == 1 && after.display().c == 1 &&
              after.display().crosshair == display.crosshair &&
              owner->history().undo_steps == 1,
          "Scientific conversion split display/pixel publication");
    owner->undo();
    check(owner->snapshot().data().dataset.pixel_type() == PixelType::UInt16 &&
              metadata_equal(owner->snapshot().display().properties, display.properties),
          "Conversion Undo failed to restore the source numeric presentation");
    owner->redo();
    check(owner->snapshot().data().dataset.pixel_type() == PixelType::UInt8 &&
              metadata_equal(owner->snapshot().display().properties, expected),
          "Conversion Redo failed to restore converted presentation");
    owner->undo();
    auto stale = prepare_operation("convert_image",
        resolve_legacy_target(owner->snapshot(), false, 0, "convert_image"),
        {{"dtype", std::string("float32")}});
    auto navigated = owner->snapshot().display();
    navigated.t = 0;
    owner->set_display(navigated);
    const auto fresh = owner->snapshot();
    rejects<RevisionConflict>([&] {
      if (direct) { auto tx = stale.begin_edit(); owner->commit(tx); }
      else app.publish(stale);
    });
    check(owner->snapshot().shares_data(fresh) &&
              owner->snapshot().revisions().matches(fresh.revisions(), Change::All),
          "Stale conversion changed the navigated owner");
  }
}
void testRgbOutput() {
  Image input(4, 1, 1, PixelType::UInt8); input.pixels = {0, 100, 200, 255};
  const auto original = input.pixels;
  auto gray = convert_rgb(input);
  check(gray.channels == 3 && gray.type == PixelType::UInt8 &&
            gray.pixels == std::vector<double>{0,0,0,100,100,100,200,200,200,255,255,255},
        "RGB default byte palette changed raw indices");
  auto ranged = convert_rgb(input, std::array<double,2>{0,200});
  check(ranged.pixels == std::vector<double>{0,0,0,128,128,128,255,255,255,255,255,255},
        "RGB byte display mapping differs from the processor contract");
  RgbLookupTable palette{}; palette[0] = {11,22,33};
  ExecutionControl freeze;
  freeze.stage_progress = [&](std::string_view stage, std::size_t, std::size_t) {
    if (stage == "Conversion RGB output") palette[0] = {99,88,77};
  };
  { ExecutionScope scope(freeze);
    auto result = convert_rgb(input, {}, palette);
    check(result.pixels[0] == 11 && result.pixels[1] == 22 && result.pixels[2] == 33,
          "RGB conversion did not freeze its caller-owned palette");
  }
  check(input.pixels == original, "RGB conversion changed its source");
  Image rgba(1, 1, 4, PixelType::UInt8); rgba.pixels = {7,31,127,0};
  check(convert_rgb(rgba).pixels == std::vector<double>{7,31,127}, "RGB alpha removal changed color");
  rejects([&] { convert_rgb(rgba, {}, palette); });
  rejects([&] { convert_rgb(input, std::array<double,2>{1,0}); });
  Image large(2048, 2, 1, PixelType::UInt8);
  ExecutionControl control; control.cancellation = std::make_shared<CancellationToken>();
  control.stage_progress = [&](std::string_view stage, std::size_t done, std::size_t) {
    if (stage == "Conversion RGB output" && done == 1024) control.cancellation->cancel();
  };
  { ExecutionScope scope(control); rejects<OperationCancelled>([&] { convert_rgb(large); }); }
#ifdef IMAGEJXX_RGB_REFERENCE_FIXTURE
  std::ifstream stream(IMAGEJXX_RGB_REFERENCE_FIXTURE);
  check(bool(stream), "RGB reference fixture is missing");
  const auto reference = nlohmann::json::parse(stream);
  check(reference.at("cases").size() == 111, "RGB reference case count changed");
  for (const auto &row : reference.at("cases")) {
    const auto name = row.at("source_dtype").get<std::string>();
    const auto type = name == "uint8" ? PixelType::UInt8 : name == "uint16" ? PixelType::UInt16 : PixelType::Float32;
    Image source(8,2,row.at("source_samples").get<int>(),type);
    source.pixels = row.at("source").get<std::vector<double>>();
    std::optional<std::array<double,2>> range;
    if (!row.at("display_range").is_null()) range = row.at("display_range").get<std::array<double,2>>();
    std::optional<RgbLookupTable> lut;
    if (!row.at("lut").is_null()) {
      lut.emplace();
      const auto kind = row.at("lut").get<std::string>();
      for (int i=0;i<256;++i)
        (*lut)[i] = kind == "gray" ? std::array<std::uint8_t,3>{std::uint8_t(i),std::uint8_t(i),std::uint8_t(i)}
          : kind == "inverse" ? std::array<std::uint8_t,3>{std::uint8_t(255-i),std::uint8_t(255-i),std::uint8_t(255-i)}
          : std::array<std::uint8_t,3>{std::uint8_t((17*i+3)%256),std::uint8_t(255-i),std::uint8_t((i*i+7)%256)};
    }
    const auto actual = convert_rgb(source,range,lut);
    check(actual.type == PixelType::UInt8 && actual.channels == 3 &&
              actual.pixels == row.at("pixels").get<std::vector<double>>(),
          "RGB output differs from pinned ImageJ processor pixels");
  }
#endif
}
} // namespace
int main() {
  try {
    testRangesAndWeights();
    testValidationAndSettings();
    testCancellation();
    testTargetRejectionBeforeReads();
    testPreparedAndQueuedCancellation();
    testScientificDisplayTransaction();
    testRgbOutput();
    std::cout << "Conversion rules, defaults, targets and phase cancellation "
                 "passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
