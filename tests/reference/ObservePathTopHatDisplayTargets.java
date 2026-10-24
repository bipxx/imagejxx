import ij.IJ;
import ij.ImagePlus;
import ij.Macro;
import ij.Prefs;
import ij.WindowManager;
import ij.gui.ShapeRoi;
import ij.plugin.filter.ExtendedPlugInFilter;
import ij.plugin.filter.PlugInFilterRunner;
import ij.plugin.filter.RankFilters;
import ij.process.FloatProcessor;
import ij.process.ImageProcessor;
import java.awt.geom.GeneralPath;
import java.lang.reflect.Field;
import java.util.Map;

/** Reference-only phase observation; no imagejxx imports or expected outputs. */
public final class ObservePathTopHatDisplayTargets {
    private static final int WIDTH = 23, HEIGHT = 17;

    private static boolean bool(String value) {
        ObservePathFilterTargets.require(value.equals("true") || value.equals("false"), "Invalid boolean");
        return Boolean.parseBoolean(value);
    }

    private static void field(RankFilters filter, String name, Object value) throws Exception {
        Field member = RankFilters.class.getDeclaredField(name);
        member.setAccessible(true);
        member.set(filter, value);
    }

    private static Object field(RankFilters filter, String name) throws Exception {
        Field member = RankFilters.class.getDeclaredField(name);
        member.setAccessible(true);
        return member.get(filter);
    }

    private static Object range(ImageProcessor processor) {
        return new double[]{processor.getMin(), processor.getMax()};
    }

    private static Object threshold(ImageProcessor processor) {
        return processor.getMinThreshold() == ImageProcessor.NO_THRESHOLD ? null
                : new double[]{processor.getMinThreshold(), processor.getMaxThreshold()};
    }

    public static void main(String[] args) {
        ImagePlus image = null;
        try {
            ObservePathFilterTargets.require(args.length == 5, "Expected name, phase and three booleans");
            String name = args[0], phase = args[1];
            ObservePathFilterTargets.require(name.length() <= 96 && name.matches("[a-z0-9-]+"), "Invalid name");
            ObservePathFilterTargets.require(phase.equals("preview") || phase.equals("apply"), "Invalid phase");
            boolean light = bool(args[2]), dont = bool(args[3]), inverted = bool(args[4]);
            ObservePathFilterTargets.require(IJ.getVersion().equals("1.54r"), "Unpinned ImageJ");
            Prefs.setThreads(1);
            Prefs.useInvertingLut = false;
            Thread.currentThread().setName("Run$_p13i-top-hat-display-reference");
            FloatProcessor processor = new FloatProcessor(WIDTH, HEIGHT,
                    ObservePathFilterTargets.deterministicInput(WIDTH, HEIGHT));
            if (inverted) processor.invertLut();
            image = new ImagePlus("p13i-top-hat-display", processor);
            GeneralPath path = new GeneralPath();
            path.moveTo((float)2.2, (float)1.2);
            path.lineTo((float)9.2, (float)1.2);
            path.lineTo((float)2.2, (float)8.2);
            path.closePath();
            ShapeRoi roi = new ShapeRoi(path);
            ObservePathFilterTargets.checkedArea(roi.getBounds().width, roi.getBounds().height,
                    1024*1024, true, "Phase mask area");
            image.setRoi(roi, false);
            processor = (FloatProcessor)image.getProcessor();
            ImageProcessor mask = image.getMask();
            processor.setMinAndMax(-8, 8);
            Map<String,Object> result = ObservePathFilterTargets.object(
                    "schema", "imagejxx.reference.path-top-hat-display-target", "version", 1,
                    "name", name, "imagej_version", IJ.getVersion(), "threads", Prefs.getThreads(),
                    "java_runtime_version", System.getProperty("java.runtime.version"),
                    "phase", phase, "options", ObservePathFilterTargets.object(
                        "radius", 2.0, "light_background", light, "dont_subtract", dont,
                        "inverted_lut", inverted),
                    "roi", ObservePathFilterTargets.roiMetadata(roi),
                    "processor_roi", ObservePathFilterTargets.rectangle(processor.getRoi()),
                    "processor_mask", ObservePathFilterTargets.maskCapture(mask, roi.getBounds().x, roi.getBounds().y),
                    "input_f32_words", ObservePathFilterTargets.floatWords((float[])processor.getPixels()),
                    "initial_range", range(processor), "initial_threshold", threshold(processor));
            RankFilters filter = new RankFilters();
            if (phase.equals("preview")) {
                int flags = filter.setup("tophat", image);
                field(filter, "radius", 2.0);
                field(filter, "lightBackground", light);
                field(filter, "dontSubtract", dont);
                field(filter, "previewing", true);
                field(filter, "flags", dont ? flags : flags|ExtendedPlugInFilter.FINAL_PROCESSING);
                processor.snapshot();
                filter.run(processor);
                result.put("before_mask_reset_f32_words", ObservePathFilterTargets.floatWords((float[])processor.getPixels()));
                result.put("range_before_mask_reset", range(processor));
                ImageProcessor currentMask = processor.getMask();
                result.put("mask_reset_performed", currentMask != null);
                // Match PlugInFilterRunner.processOneImage: RankFilters display
                // adjustment can reset the ROI and drop its processor mask.
                if (currentMask != null) processor.reset(currentMask);
            } else {
                // Same actual runner path as the retained two-case phase driver.
                WindowManager.setTempCurrentImage(image);
                String macro = "radius=2" + (light ? " light" : "") + (dont ? " don't" : "") + " slice";
                result.put("macro_options", macro);
                Macro.setOptions(macro);
                new PlugInFilterRunner(filter, "Top Hat", "tophat");
                result.put("before_mask_reset_f32_words", null);
                result.put("range_before_mask_reset", null);
                result.put("mask_reset_performed", true);
            }
            ObservePathFilterTargets.require(((Double)field(filter,"radius")) == 2.0
                    && ((Boolean)field(filter,"lightBackground")) == light
                    && ((Boolean)field(filter,"dontSubtract")) == dont,
                    "Actual ImageJ filter options differ from request");
            result.put("effective_options", ObservePathFilterTargets.object(
                    "radius", field(filter,"radius"), "light_background", field(filter,"lightBackground"),
                    "dont_subtract", field(filter,"dontSubtract"), "inverted_lut", processor.isInvertedLut()));
            result.put("after_mask_reset_f32_words", ObservePathFilterTargets.floatWords((float[])processor.getPixels()));
            result.put("processor_roi_after_processing", ObservePathFilterTargets.rectangle(processor.getRoi()));
            result.put("processor_mask_after_processing", ObservePathFilterTargets.maskCapture(processor.getMask(), processor.getRoi().x, processor.getRoi().y));
            result.put("final_range", range(processor));
            result.put("final_threshold", threshold(processor));
            System.out.println(ObservePathFilterTargets.Json.encode(result));
            System.out.flush();
            ObservePathFilterTargets.require(!System.out.checkError(), "Incomplete phase observation");
            Macro.setOptions(null);
            WindowManager.setTempCurrentImage(null);
            image.close();
            System.exit(0);
        } catch (Exception error) {
            Macro.setOptions(null);
            WindowManager.setTempCurrentImage(null);
            ObservePathFilterTargets.fail(1, error);
        }
    }
}
