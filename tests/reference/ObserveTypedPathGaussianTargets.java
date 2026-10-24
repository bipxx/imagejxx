import ij.IJ;
import ij.ImagePlus;
import ij.Prefs;
import ij.gui.ShapeRoi;
import ij.plugin.filter.GaussianBlur;
import ij.process.ByteProcessor;
import ij.process.ColorProcessor;
import ij.process.FloatProcessor;
import ij.process.ImageProcessor;
import ij.process.ShortProcessor;
import java.awt.Rectangle;
import java.util.Map;

/** Typed/anisotropic Gaussian observations, using the pinned ImageJ entry point. */
public final class ObserveTypedPathGaussianTargets {
    private static final char[] HEX = "0123456789abcdef".toCharArray();
    private ObserveTypedPathGaussianTargets() {}

    public static void main(String[] args) {
        try {
            ObservePathFilterTargets.require(args.length == 7,
                    "name width height path pixelType sigmaX sigmaY");
            long chars = 0;
            for (String arg : args) chars += arg.length();
            ObservePathFilterTargets.require(chars <= 65536, "Typed Gaussian argument bound");
            ObservePathFilterTargets.Config geometry = ObservePathFilterTargets.parse(
                    new String[] {args[0],args[1],args[2],args[3],"1","small-mask"});
            String type = args[4];
            ObservePathFilterTargets.require("u8".equals(type) || "u16".equals(type)
                    || "f32".equals(type) || "rgb".equals(type), "Unsupported Gaussian sample type");
            double sigmaX = Double.parseDouble(args[5]), sigmaY = Double.parseDouble(args[6]);
            ObservePathFilterTargets.require(Double.isFinite(sigmaX) && Double.isFinite(sigmaY)
                    && sigmaX >= 0 && sigmaY >= 0 && sigmaX <= 24 && sigmaY <= 24,
                    "Typed Gaussian sigma bound");
            System.setProperty("java.awt.headless", "true");
            ObservePathFilterTargets.require("1.54r".equals(IJ.getVersion()), "Reference version");
            Prefs.setThreads(1);
            ImageProcessor processor = input(geometry.width, geometry.height, type);
            ShapeRoi roi = new ShapeRoi(geometry.commands);
            Map<String,Object> before = ObservePathFilterTargets.roiMetadata(roi);
            ImagePlus image = new ImagePlus(geometry.name, processor);
            image.setRoi(roi, false);
            processor = image.getProcessor();
            double accuracy = "u8".equals(type) || "rgb".equals(type) ? 0.002 : 0.0002;
            Map<String,Object> result = ObservePathFilterTargets.object(
                    "schema", "imagejxx.reference.typed-path-gaussian-target", "version", 1,
                    "name", geometry.name, "imagej_version", IJ.getVersion(), "threads", Prefs.getThreads(),
                    "java_runtime_version", System.getProperty("java.runtime.version"),
                    "commands_f32_words", ObservePathFilterTargets.floatWords(geometry.commands),
                    "input", samples(processor,type),
                    "options", ObservePathFilterTargets.object("pixel_type", type, "sigma_x", sigmaX,
                            "sigma_y", sigmaY, "accuracy", accuracy),
                    "roi_before_attachment", before,
                    "roi_after_attachment", ObservePathFilterTargets.roiMetadata(roi),
                    "image_retains_same_roi", image.getRoi() == roi,
                    "mask_absent_before_explicit_request", processor.getMask() == null,
                    "processor_roi_before_mask", ObservePathFilterTargets.rectangle(processor.getRoi()));
            Rectangle bounds = roi.getBounds();
            ObservePathFilterTargets.checkedArea(bounds.width,bounds.height,1024L*1024,true,
                    "Typed Gaussian full mask");
            ImageProcessor full = image.getMask(), mask = processor.getMask();
            Rectangle clipped = processor.getRoi();
            result.put("roi_mask", ObservePathFilterTargets.maskCapture(full,bounds.x,bounds.y));
            result.put("processor_mask", ObservePathFilterTargets.maskCapture(mask,clipped.x,clipped.y));
            result.put("processor_roi_after_mask", ObservePathFilterTargets.rectangle(clipped));
            processor.snapshot();
            new GaussianBlur().blurGaussian(processor,sigmaX,sigmaY,accuracy);
            ObservePathFilterTargets.require(!Thread.currentThread().isInterrupted(), "Gaussian interrupted");
            result.put("before_mask_reset", samples(processor,type));
            if (mask != null) processor.reset(mask);
            result.put("after_mask_reset", samples(processor,type));
            result.put("mask_reset_performed", mask != null);
            result.put("processor_roi_after_reset", ObservePathFilterTargets.rectangle(processor.getRoi()));
            System.out.println(ObservePathFilterTargets.Json.encode(result));
            System.out.flush();
            if (System.out.checkError()) throw new IllegalStateException("Incomplete typed Gaussian output");
            System.exit(0);
        } catch (IllegalArgumentException error) {
            ObservePathFilterTargets.fail(2,error);
        } catch (Exception error) {
            ObservePathFilterTargets.fail(1,error);
        }
    }

    private static ImageProcessor input(int width,int height,String type) {
        if ("f32".equals(type)) return new FloatProcessor(width,height,
                ObservePathFilterTargets.deterministicInput(width,height));
        ImageProcessor result = "rgb".equals(type) ? new ColorProcessor(width,height)
                : "u8".equals(type) ? new ByteProcessor(width,height) : new ShortProcessor(width,height);
        for (int y=0;y<height;++y) for (int x=0;x<width;++x) {
            int seed=(x*37+y*53+(x*y)%17)%257;
            int value="u8".equals(type) ? seed%256 : seed*251;
            if ("rgb".equals(type)) value=((seed%256)<<16)|(((seed*3+17)%256)<<8)|((seed*5+29)%256);
            result.set(x,y,value);
        }
        return result;
    }

    private static Object samples(ImageProcessor processor,String type) {
        int count=Math.multiplyExact(processor.getWidth(),processor.getHeight());
        ObservePathFilterTargets.require(count<=65536,"Typed Gaussian sample bound");
        String[] words;
        if ("f32".equals(type)) words=ObservePathFilterTargets.floatWords((float[])processor.getPixels());
        else {
            int digits="u8".equals(type)?2:"u16".equals(type)?4:6;
            words=new String[count];
            for(int i=0;i<count;++i) {
                int word=processor.get(i);char[] text=new char[digits+2];text[0]='0';text[1]='x';
                for(int nibble=0;nibble<digits;++nibble) text[nibble+2]=HEX[(word>>>(4*(digits-1-nibble)))&15];
                words[i]=new String(text);
            }
        }
        return ObservePathFilterTargets.object("image_origin",new int[]{0,0},
                "dimensions",new int[]{processor.getWidth(),processor.getHeight()},
                "pixel_type",type,"sample_words",words);
    }
}
