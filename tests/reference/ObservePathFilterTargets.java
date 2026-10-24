import ij.IJ;
import ij.ImagePlus;
import ij.Prefs;
import ij.gui.ShapeRoi;
import ij.plugin.filter.GaussianBlur;
import ij.process.FloatProcessor;
import ij.process.ImageProcessor;
import java.awt.Rectangle;
import java.awt.geom.PathIterator;
import java.awt.geom.Rectangle2D;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * One bounded observation from pinned ImageJ 1.54r; no expected geometry or pixels.
 *
 * CLI: name width height encodedFloatCommands sigma mode
 * encodedFloatCommands is a comma-separated ShapeRoi(float[]) array, with
 * PathIterator types 0=M, 1=L, 2=Q, 3=C, 4=Z and their coordinate arguments.
 * sigma is 1 or 12; mode is metadata-only or small-mask. Run in a fresh JVM.
 *
 * Output is one ASCII JSON object, schema imagejxx.reference.path-filter-target,
 * version 1. F32 words are eight lowercase hexadecimal digits prefixed by 0x,
 * obtained with Float.floatToRawIntBits. Pixel arrays are row-major. Mask byte
 * hex has two digits per pixel, with no prefix. Every raster identifies its
 * image-space origin and dimensions; a null mask means ImageJ supplied no mask.
 *
 * metadata-only never calls a mask-construction API. In particular, attachment
 * uses ImagePlus.setRoi(Roi, false), not ImageProcessor.setRoi(Roi). small-mask
 * checks the full stored ROI area in long before calling ImagePlus.getMask().
 * It records that full mask separately from ImageProcessor.getMask(), which
 * may have been cropped. Gaussian is called directly, then reset(processorMask)
 * performs the documented non-rectangular restoration. This is not a claim
 * that a GUI/plugin runner was exercised.
 */
public final class ObservePathFilterTargets {
    private static final int MAX_AXIS = 4096;
    private static final int MAX_IMAGE_PIXELS = 65536;
    private static final int MAX_ARGUMENT_CHARS = 65536;
    private static final int MAX_COMMAND_CHARS = 32768;
    private static final int MAX_COMMAND_VALUES = 2048;
    private static final int MAX_SEGMENTS = 512;
    private static final long MAX_MASK_BYTES = 1024L * 1024L;
    private static final int MAX_JSON_CHARS = 8 * 1024 * 1024;
    private static final double ACCURACY = 0.0002;
    private static final char[] HEX = "0123456789abcdef".toCharArray();

    private ObservePathFilterTargets() {}

    public static void main(String[] args) {
        try {
            Config config = parse(args);
            System.setProperty("java.awt.headless", "true");
            if (!"1.54r".equals(IJ.getVersion()))
                throw new IllegalStateException("Expected ImageJ 1.54r, found " + IJ.getVersion());
            Prefs.setThreads(1);
            String output = Json.encode(observe(config));
            System.out.print(output);
            System.out.print('\n');
            System.out.flush();
            if (System.out.checkError())
                throw new IllegalStateException("Could not write complete observation");
            System.exit(0); // Standalone observer: do not retain ImageJ worker/UI threads.
        } catch (IllegalArgumentException error) {
            fail(2, error);
        } catch (Exception error) {
            fail(1, error);
        }
    }

    static void fail(int status, Exception error) {
        String message = String.valueOf(error.getMessage());
        if (message.length() > 512) message = message.substring(0, 512);
        message = message.replace('\n', ' ').replace('\r', ' ');
        System.err.println("ObservePathFilterTargets: " + error.getClass().getSimpleName()
                + ": " + message);
        System.exit(status);
    }

    static Config parse(String[] args) {
        require(args.length == 6,
                "Usage: name width height encodedFloatCommands sigma metadata-only|small-mask");
        long argumentChars = 0;
        for (String arg : args) argumentChars += arg.length();
        require(argumentChars <= MAX_ARGUMENT_CHARS, "Arguments exceed character limit");
        require(args[0].matches("[A-Za-z0-9][A-Za-z0-9._-]{0,79}"), "Invalid case name");
        int width = Integer.parseInt(args[1]);
        int height = Integer.parseInt(args[2]);
        require(width > 0 && height > 0 && width <= MAX_AXIS && height <= MAX_AXIS,
                "Image axes must be in 1.." + MAX_AXIS);
        require((long) width * height <= MAX_IMAGE_PIXELS, "Image pixel limit exceeded");
        require(!args[3].isEmpty() && args[3].length() <= MAX_COMMAND_CHARS,
                "Invalid command text length");
        String[] tokens = args[3].split(",", -1);
        require(tokens.length <= MAX_COMMAND_VALUES, "Command value limit exceeded");
        float[] commands = new float[tokens.length];
        for (int i = 0; i < tokens.length; ++i) {
            require(!tokens[i].trim().isEmpty(), "Empty command value at " + i);
            commands[i] = Float.parseFloat(tokens[i].trim());
            require(Float.isFinite(commands[i]), "Non-finite command value at " + i);
        }
        int cursor = 0;
        int segments = 0;
        boolean hasMove = false;
        while (cursor < commands.length) {
            float encodedType = commands[cursor++];
            int type = (int) encodedType;
            require(encodedType == type, "Non-integral path segment type");
            int coordinates = coordinateCount(type);
            require(cursor + coordinates <= commands.length, "Truncated path segment");
            if (type == PathIterator.SEG_MOVETO) hasMove = true;
            require(hasMove, "Path must begin with a move segment");
            cursor += coordinates;
            require(++segments <= MAX_SEGMENTS, "Path segment limit exceeded");
        }
        double sigma = Double.parseDouble(args[4]);
        require(sigma == 1.0 || sigma == 12.0, "Sigma must be 1 or 12");
        require("metadata-only".equals(args[5]) || "small-mask".equals(args[5]),
                "Mode must be metadata-only or small-mask");
        return new Config(args[0], width, height, commands, sigma, args[5]);
    }

    private static Map<String, Object> observe(Config config) {
        ShapeRoi roi = new ShapeRoi(config.commands);
        Map<String, Object> beforeAttachment = roiMetadata(roi);
        FloatProcessor fresh = new FloatProcessor(config.width, config.height,
                deterministicInput(config.width, config.height));
        ImagePlus image = new ImagePlus(config.name, fresh);
        image.setRoi(roi, false); // Rectangle-only clipping; no ShapeRoi mask construction.
        ImageProcessor processor = image.getProcessor();
        Rectangle processorRoi = processor.getRoi();
        Map<String, Object> result = object(
                "schema", "imagejxx.reference.path-filter-target", "version", 1,
                "name", config.name, "mode", config.mode, "sigma", config.sigma,
                "imagej_version", IJ.getVersion(),
                "java_version", System.getProperty("java.version"),
                "java_runtime_version", System.getProperty("java.runtime.version"),
                "java_vendor", System.getProperty("java.vendor"),
                "threads", Prefs.getThreads(),
                "limits", object("image_pixels", MAX_IMAGE_PIXELS,
                        "command_values", MAX_COMMAND_VALUES, "segments", MAX_SEGMENTS,
                        "full_mask_bytes", MAX_MASK_BYTES, "json_ascii_bytes", MAX_JSON_CHARS),
                "commands_f32_words", floatWords(config.commands),
                "input", object("generator", "dyadic-grid-v1",
                        "image_origin", new int[] {0, 0},
                        "dimensions", new int[] {processor.getWidth(), processor.getHeight()},
                        "f32_words", processorWords(processor)),
                "roi_before_attachment", beforeAttachment,
                "roi_after_attachment", roiMetadata(roi),
                "image_retains_same_roi", image.getRoi() == roi,
                "roi_references_image", roi.getImage() == image,
                "processor_roi_before_mask", rectangle(processorRoi),
                "mask_absent_before_explicit_request", processor.getMask() == null);

        if ("metadata-only".equals(config.mode)) return result;

        Rectangle fullBounds = roi.getBounds();
        checkedArea(fullBounds.width, fullBounds.height, MAX_MASK_BYTES, true,
                "Full stored ROI mask");
        // The only mask-construction call in this observer, after the checked-long guard.
        ImageProcessor roiMask = image.getMask();
        ImageProcessor processorMask = processor.getMask();
        Rectangle clipped = processor.getRoi();
        result.put("roi_mask", maskCapture(roiMask, fullBounds.x, fullBounds.y));
        result.put("processor_mask", maskCapture(processorMask, clipped.x, clipped.y));
        result.put("processor_roi_after_mask", rectangle(clipped));

        // Explicit snapshot is necessary even when the rectangular ROI spans full height.
        processor.snapshot();
        new GaussianBlur().blurGaussian(processor, config.sigma, config.sigma, ACCURACY);
        require(!Thread.currentThread().isInterrupted(), "Gaussian execution was interrupted");
        Map<String, Object> gaussian = object(
                "entry_point", "GaussianBlur.blurGaussian(ImageProcessor,double,double,double)",
                "sigma_x", config.sigma, "sigma_y", config.sigma, "accuracy", ACCURACY,
                "image_origin", new int[] {0, 0},
                "dimensions", new int[] {processor.getWidth(), processor.getHeight()},
                "processor_roi_before_reset", rectangle(processor.getRoi()),
                "before_mask_reset_f32_words", processorWords(processor),
                "processor_mask_is_null", processorMask == null,
                "mask_reset_performed", processorMask != null);
        // PlugInFilterRunner restores outside a mask only when one exists.
        if (processorMask != null) processor.reset(processorMask);
        gaussian.put("after_mask_reset_f32_words", processorWords(processor));
        gaussian.put("processor_roi_after_reset", rectangle(processor.getRoi()));
        result.put("gaussian", gaussian);
        return result;
    }

    static Map<String, Object> roiMetadata(ShapeRoi roi) {
        Rectangle2D.Double floating = roi.getFloatBounds();
        PathIterator iterator = roi.getShape().getPathIterator(null);
        int windingRule = iterator.getWindingRule();
        List<Object> segments = new ArrayList<Object>();
        float[] coordinates = new float[6];
        while (!iterator.isDone()) {
            require(segments.size() < MAX_SEGMENTS, "Observed path segment limit exceeded");
            int type = iterator.currentSegment(coordinates);
            int count = coordinateCount(type);
            float[] exactCoordinates = new float[count];
            System.arraycopy(coordinates, 0, exactCoordinates, 0, count);
            segments.add(object("type", type, "coordinates_f32_words", floatWords(exactCoordinates)));
            iterator.next();
        }
        return object("bounds", rectangle(roi.getBounds()),
                "float_bounds", new double[] {floating.x, floating.y, floating.width, floating.height},
                "local_path", object("image_origin", new double[] {roi.getXBase(), roi.getYBase()},
                        "winding_rule", windingRule, "segments", segments));
    }

    static Object maskCapture(ImageProcessor mask, int imageX, int imageY) {
        if (mask == null) return null;
        long area = checkedArea(mask.getWidth(), mask.getHeight(), MAX_MASK_BYTES, true,
                "Observed mask");
        require(mask.getPixels() instanceof byte[], "Observed mask is not byte-valued");
        byte[] pixels = (byte[]) mask.getPixels();
        require(pixels.length == area, "Observed mask length does not match its dimensions");
        char[] hex = new char[Math.toIntExact(area * 2)];
        for (int i = 0; i < pixels.length; ++i) {
            hex[2 * i] = HEX[(pixels[i] & 255) >>> 4];
            hex[2 * i + 1] = HEX[pixels[i] & 15];
        }
        Rectangle localRoi = mask.getRoi();
        return object("image_origin", new int[] {imageX, imageY},
                "dimensions", new int[] {mask.getWidth(), mask.getHeight()},
                "local_processor_roi", object("origin", new int[] {localRoi.x, localRoi.y},
                        "dimensions", new int[] {localRoi.width, localRoi.height}),
                "u8_hex", new String(hex));
    }

    static long checkedArea(int width, int height, long maximum, boolean allowEmpty,
            String label) {
        require(width >= 0 && height >= 0, label + " has negative dimensions");
        long area = Math.multiplyExact((long) width, (long) height);
        require((allowEmpty || area > 0) && area <= maximum,
                label + " exceeds the admitted area or is empty");
        return area;
    }

    private static String[] processorWords(ImageProcessor processor) {
        require(processor instanceof FloatProcessor && processor.getPixels() instanceof float[],
                "Observed processor is not FloatProcessor");
        float[] pixels = (float[]) processor.getPixels();
        require(pixels.length == checkedArea(processor.getWidth(), processor.getHeight(),
                MAX_IMAGE_PIXELS, false, "Observed processor"), "Processor pixel length mismatch");
        return floatWords(pixels);
    }

    static float[] deterministicInput(int width, int height) {
        // Bounded integer arithmetic followed by an exactly representable dyadic scale.
        // All words are emitted; this generates inputs, never expected filter outputs.
        float[] pixels = new float[width * height];
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                pixels[y * width + x] = ((x * 37 + y * 53 + (x * y) % 17) % 257 - 128) / 16.0f;
        pixels[0] = Float.intBitsToFloat(0x80000000);
        if (pixels.length > 1) pixels[1] = 0.0f;
        return pixels;
    }

    static String[] floatWords(float[] values) {
        String[] words = new String[values.length];
        for (int i = 0; i < values.length; ++i) {
            int word = Float.floatToRawIntBits(values[i]);
            char[] text = new char[10];
            text[0] = '0';
            text[1] = 'x';
            for (int nibble = 0; nibble < 8; ++nibble)
                text[2 + nibble] = HEX[(word >>> (28 - 4 * nibble)) & 15];
            words[i] = new String(text);
        }
        return words;
    }

    private static int coordinateCount(int type) {
        switch (type) {
            case PathIterator.SEG_MOVETO:
            case PathIterator.SEG_LINETO: return 2;
            case PathIterator.SEG_QUADTO: return 4;
            case PathIterator.SEG_CUBICTO: return 6;
            case PathIterator.SEG_CLOSE: return 0;
            default: throw new IllegalArgumentException("Unknown path segment type: " + type);
        }
    }

    static Map<String, Object> rectangle(Rectangle rectangle) {
        return object("image_origin", new int[] {rectangle.x, rectangle.y},
                "dimensions", new int[] {rectangle.width, rectangle.height});
    }

    static Map<String, Object> object(Object... fields) {
        Map<String, Object> object = new LinkedHashMap<String, Object>();
        for (int i = 0; i < fields.length; i += 2) object.put((String) fields[i], fields[i + 1]);
        return object;
    }

    static void require(boolean condition, String message) {
        if (!condition) throw new IllegalArgumentException(message);
    }

    static final class Config {
        final String name;
        final int width;
        final int height;
        final float[] commands;
        final double sigma;
        final String mode;

        Config(String name, int width, int height, float[] commands, double sigma, String mode) {
            this.name = name;
            this.width = width;
            this.height = height;
            this.commands = commands;
            this.sigma = sigma;
            this.mode = mode;
        }
    }

    /** Small JDK-only writer. It buffers the whole bounded object before stdout. */
    static final class Json {
        private final StringBuilder out = new StringBuilder();

        static String encode(Object value) {
            Json writer = new Json();
            writer.value(value, 0);
            return writer.out.toString();
        }

        private void append(String text) {
            require(out.length() <= MAX_JSON_CHARS - 1 - text.length(), "JSON output limit exceeded");
            out.append(text);
        }

        private void append(char character) {
            require(out.length() < MAX_JSON_CHARS - 1, "JSON output limit exceeded");
            out.append(character);
        }

        private void string(String text) {
            append('"');
            for (int i = 0; i < text.length(); ++i) {
                char character = text.charAt(i);
                if (character == '"' || character == '\\') {
                    append('\\');
                    append(character);
                } else if (character < 32 || character > 126) {
                    append("\\u");
                    for (int shift = 12; shift >= 0; shift -= 4) append(HEX[(character >>> shift) & 15]);
                } else append(character);
            }
            append('"');
        }

        private void value(Object value, int depth) {
            require(depth <= 24, "JSON nesting limit exceeded");
            if (value == null) append("null");
            else if (value instanceof String) string((String) value);
            else if (value instanceof Boolean || value instanceof Integer || value instanceof Long)
                append(value.toString());
            else if (value instanceof Double) {
                require(Double.isFinite((Double) value), "Non-finite JSON number");
                append(value.toString());
            } else if (value instanceof Map) {
                append('{');
                boolean first = true;
                for (Map.Entry<?, ?> entry : ((Map<?, ?>) value).entrySet()) {
                    if (!first) append(',');
                    first = false;
                    string((String) entry.getKey());
                    append(':');
                    value(entry.getValue(), depth + 1);
                }
                append('}');
            } else if (value instanceof Iterable) {
                append('[');
                boolean first = true;
                for (Object element : (Iterable<?>) value) {
                    if (!first) append(',');
                    first = false;
                    value(element, depth + 1);
                }
                append(']');
            } else if (value instanceof Object[]) {
                append('[');
                Object[] values = (Object[]) value;
                for (int i = 0; i < values.length; ++i) {
                    if (i != 0) append(',');
                    value(values[i], depth + 1);
                }
                append(']');
            } else if (value instanceof int[]) {
                append('[');
                int[] values = (int[]) value;
                for (int i = 0; i < values.length; ++i) {
                    if (i != 0) append(',');
                    append(Integer.toString(values[i]));
                }
                append(']');
            } else if (value instanceof double[]) {
                append('[');
                double[] values = (double[]) value;
                for (int i = 0; i < values.length; ++i) {
                    if (i != 0) append(',');
                    require(Double.isFinite(values[i]), "Non-finite JSON array number");
                    append(Double.toString(values[i]));
                }
                append(']');
            } else throw new IllegalArgumentException("Unsupported JSON value type");
        }
    }
}
