import ij.IJ;
import ij.ImagePlus;
import ij.Prefs;
import ij.gui.ShapeRoi;
import ij.plugin.filter.RankFilters;
import ij.process.ByteProcessor;
import ij.process.FloatProcessor;
import ij.process.ImageProcessor;
import ij.process.ShortProcessor;
import java.awt.Rectangle;
import java.util.Map;

/** Bounded, reference-only typed rank observations; no expected filter output. */
public final class ObservePathRankTargets {
    private static final char[] HEX = "0123456789abcdef".toCharArray();

    private ObservePathRankTargets() {}

    public static void main(String[] args) {
        try {
            ObservePathFilterTargets.require(args.length == 13,
                    "name width height path operation type radius which threshold light dontSubtract inverted pattern");
            long chars = 0;
            for (String arg : args) chars += arg.length();
            ObservePathFilterTargets.require(chars <= 65536, "Rank argument bound");
            ObservePathFilterTargets.Config geometry = ObservePathFilterTargets.parse(
                    new String[] {args[0], args[1], args[2], args[3], "1", "small-mask"});
            double radius = Double.parseDouble(args[6]);
            int which = Integer.parseInt(args[7]);
            float threshold = Float.parseFloat(args[8]);
            boolean light = flag(args[9]), dontSubtract = flag(args[10]), inverted = flag(args[11]);
            int filter = operation(args[4]);
            int maximum = filter == RankFilters.MEDIAN || filter == RankFilters.OUTLIERS
                    || filter == RankFilters.REMOVE_NAN ? 100 : 1000;
            ObservePathFilterTargets.require(Double.isFinite(radius) && radius >= 0 && radius <= maximum,
                    "Rank radius bound");
            ObservePathFilterTargets.require(which == 0 || which == 1, "Outlier direction");
            ObservePathFilterTargets.require(Float.isFinite(threshold) && threshold >= 0, "Threshold bound");
            ObservePathFilterTargets.require(!"despeckle".equals(args[4]) || radius == 1,
                    "Despeckle is median radius 1");
            ObservePathFilterTargets.require("finite".equals(args[12])
                    || (("quiet_nan".equals(args[12]) || "ieee".equals(args[12]))
                        && filter == RankFilters.REMOVE_NAN && "f32".equals(args[5])),
                    "Nonfinite inputs require Float32 Remove NaNs");
            ObservePathFilterTargets.require(filter != RankFilters.REMOVE_NAN || "f32".equals(args[5]),
                    "Remove NaNs requires Float32");
            System.setProperty("java.awt.headless", "true");
            ObservePathFilterTargets.require("1.54r".equals(IJ.getVersion()), "Reference version");
            Prefs.setThreads(1);
            ImageProcessor processor = input(geometry.width, geometry.height, args[5], args[12]);
            if (inverted) processor.invertLut();
            ShapeRoi roi = new ShapeRoi(geometry.commands);
            Map<String, Object> before = ObservePathFilterTargets.roiMetadata(roi);
            ImagePlus image = new ImagePlus(geometry.name, processor);
            image.setRoi(roi, false);
            processor = image.getProcessor();
            Map<String, Object> result = ObservePathFilterTargets.object(
                    "schema", "imagejxx.reference.path-rank-target", "version", 1,
                    "name", geometry.name, "imagej_version", IJ.getVersion(), "threads", Prefs.getThreads(),
                    "java_runtime_version", System.getProperty("java.runtime.version"),
                    "commands_f32_words", ObservePathFilterTargets.floatWords(geometry.commands),
                    "roi_before_attachment", before,
                    "roi_after_attachment", ObservePathFilterTargets.roiMetadata(roi),
                    "image_retains_same_roi", image.getRoi() == roi,
                    "mask_absent_before_explicit_request", processor.getMask() == null,
                    "processor_roi_before_mask", ObservePathFilterTargets.rectangle(processor.getRoi()),
                    "input", samples(processor, args[5]),
                    "options", ObservePathFilterTargets.object("operation", args[4], "pixel_type", args[5],
                        "radius", radius, "filter_type", filter, "which_outliers", which,
                        "threshold", (double) threshold, "light_background", light,
                        "dont_subtract", dontSubtract, "inverted_lut", processor.isInvertedLut(),
                        "input_pattern", args[12]));
            ObservePathFilterTargets.require(processor.isInvertedLut() == inverted, "LUT inversion differs");
            Rectangle bounds = roi.getBounds();
            ObservePathFilterTargets.checkedArea(bounds.width, bounds.height, 1024L*1024, true,
                    "Rank full stored mask");
            ImageProcessor roiMask = image.getMask();
            ImageProcessor processorMask = processor.getMask();
            Rectangle clipped = processor.getRoi();
            result.put("roi_mask", ObservePathFilterTargets.maskCapture(roiMask, bounds.x, bounds.y));
            result.put("processor_mask", ObservePathFilterTargets.maskCapture(processorMask, clipped.x, clipped.y));
            result.put("processor_roi_after_mask", ObservePathFilterTargets.rectangle(clipped));
            processor.snapshot();
            new RankFilters().rank(processor, radius, filter, which, threshold, light, dontSubtract);
            ObservePathFilterTargets.require(!Thread.currentThread().isInterrupted(), "Rank interrupted");
            result.put("before_mask_reset", samples(processor, args[5]));
            if (processorMask != null) processor.reset(processorMask);
            result.put("after_mask_reset", samples(processor, args[5]));
            result.put("mask_reset_performed", processorMask != null);
            result.put("processor_roi_after_reset", ObservePathFilterTargets.rectangle(processor.getRoi()));
            System.out.println(ObservePathFilterTargets.Json.encode(result));
            System.out.flush();
            if (System.out.checkError()) throw new IllegalStateException("Incomplete rank output");
            System.exit(0);
        } catch (IllegalArgumentException error) {
            ObservePathFilterTargets.fail(2, error);
        } catch (Exception error) {
            ObservePathFilterTargets.fail(1, error);
        }
    }

    private static boolean flag(String value) {
        ObservePathFilterTargets.require("true".equals(value) || "false".equals(value), "Expected boolean");
        return "true".equals(value);
    }

    private static int operation(String name) {
        if ("minimum".equals(name)) return RankFilters.MIN;
        if ("maximum".equals(name)) return RankFilters.MAX;
        if ("mean".equals(name)) return RankFilters.MEAN;
        if ("variance".equals(name)) return RankFilters.VARIANCE;
        if ("median".equals(name) || "despeckle".equals(name)) return RankFilters.MEDIAN;
        if ("outliers".equals(name)) return RankFilters.OUTLIERS;
        if ("top_hat".equals(name)) return RankFilters.TOP_HAT;
        if ("remove_nans".equals(name)) return RankFilters.REMOVE_NAN;
        throw new IllegalArgumentException("Unknown rank operation");
    }

    private static ImageProcessor input(int width, int height, String type, String pattern) {
        int count = width*height;
        if ("f32".equals(type)) {
            float[] values = ObservePathFilterTargets.deterministicInput(width, height);
            if ("quiet_nan".equals(pattern)) {
                values[0] = Float.intBitsToFloat(0x7fc12345);
                values[count-1] = Float.intBitsToFloat(0xffc02345);
                values[2*width+3] = Float.intBitsToFloat(0x7fc01111);
                values[3*width+5] = Float.intBitsToFloat(0xffc02222);
            } else if ("ieee".equals(pattern)) {
                values[0] = Float.POSITIVE_INFINITY;
                values[count-1] = Float.NEGATIVE_INFINITY;
                values[2*width+3] = Float.intBitsToFloat(0x7f800001);
                values[3*width+5] = Float.intBitsToFloat(0xffc02222);
            }
            return new FloatProcessor(width, height, values);
        }
        ObservePathFilterTargets.require("u8".equals(type) || "u16".equals(type), "Unknown sample type");
        ImageProcessor result = "u8".equals(type) ? new ByteProcessor(width, height)
                : new ShortProcessor(width, height);
        for (int y=0; y<height; ++y)
            for (int x=0; x<width; ++x) {
                int seed = (x*37+y*53+(x*y)%17)%257;
                result.set(x, y, "u8".equals(type) ? seed%256 : seed*251);
            }
        return result;
    }

    private static Object samples(ImageProcessor processor, String type) {
        int count = Math.multiplyExact(processor.getWidth(), processor.getHeight());
        ObservePathFilterTargets.require(count <= 65536, "Rank sample count");
        String[] words;
        if ("f32".equals(type)) words = ObservePathFilterTargets.floatWords((float[])processor.getPixels());
        else {
            int digits = "u8".equals(type) ? 2 : 4;
            words = new String[count];
            for (int i=0; i<count; ++i) {
                int word = processor.get(i);
                char[] text = new char[digits+2]; text[0]='0'; text[1]='x';
                for (int nibble=0; nibble<digits; ++nibble)
                    text[2+nibble]=HEX[(word >>> (4*(digits-1-nibble)))&15];
                words[i]=new String(text);
            }
        }
        return ObservePathFilterTargets.object("image_origin", new int[] {0,0},
                "dimensions", new int[] {processor.getWidth(),processor.getHeight()},
                "pixel_type", type, "sample_words", words);
    }
}
