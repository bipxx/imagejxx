// Project-owned observer. Executes pinned ImageJ APIs only, never imagejxx.
import ij.*;
import ij.gui.*;
import ij.measure.Calibration;
import ij.plugin.*;
import ij.process.*;
import java.awt.Color;
import java.awt.geom.Rectangle2D;
import java.lang.reflect.Array;
import java.security.MessageDigest;
import java.util.*;

public class ObserveScientificZProjection {
    static final String[] METHODS = {"avg", "max", "min", "sum", "sd", "median"};
    static final char[] HEX = "0123456789abcdef".toCharArray();
    static long emitted;
    static int cases;
    static Map<String,Object> map(Object... pairs) {
        Map<String,Object> result = new LinkedHashMap<String,Object>();
        for (int i=0; i<pairs.length; i+=2) result.put((String)pairs[i], pairs[i+1]);
        return result;
    }
    static String quote(String s) {
        StringBuilder out = new StringBuilder("\"");
        for (int i=0; i<s.length(); ++i) {
            char c = s.charAt(i);
            if (c=='"' || c=='\\') out.append('\\').append(c);
            else if (c<32 || c>126) out.append(String.format(Locale.ROOT,"\\u%04x",(int)c));
            else out.append(c);
        }
        return out.append('"').toString();
    }
    static String json(Object value) {
        if (value==null) return "null";
        if (value instanceof String) return quote((String)value);
        if (value instanceof Boolean || value instanceof Number) return value.toString();
        StringBuilder out = new StringBuilder();
        if (value instanceof Map) {
            out.append('{'); boolean comma=false;
            for (Map.Entry<?,?> e : ((Map<?,?>)value).entrySet()) {
                if (comma) out.append(','); comma=true;
                out.append(quote((String)e.getKey())).append(':').append(json(e.getValue()));
            }
            return out.append('}').toString();
        }
        if (value instanceof Iterable) {
            out.append('['); boolean comma=false;
            for (Object v : (Iterable<?>)value) { if (comma) out.append(','); comma=true; out.append(json(v)); }
            return out.append(']').toString();
        }
        if (value.getClass().isArray()) {
            out.append('[');
            for (int i=0; i<Array.getLength(value); ++i) { if (i>0) out.append(','); out.append(json(Array.get(value,i))); }
            return out.append(']').toString();
        }
        throw new IllegalArgumentException("Unsupported observation value: "+value.getClass());
    }
    static String f64(double value) { return String.format(Locale.ROOT,"%016x",Double.doubleToRawLongBits(value)); }
    static void word(StringBuilder out, int value, int bytes) {
        for (int b=0; b<bytes; ++b) { int v=(value >>> (8*b))&255; out.append(HEX[v>>>4]).append(HEX[v&15]); }
    }
    static String raw(Object pixels) {
        StringBuilder out = new StringBuilder();
        if (pixels instanceof byte[]) for (byte v : (byte[])pixels) word(out,v&255,1);
        else if (pixels instanceof short[]) for (short v : (short[])pixels) word(out,v&65535,2);
        else if (pixels instanceof float[]) for (float v : (float[])pixels) word(out,Float.floatToRawIntBits(v),4);
        else if (pixels instanceof int[]) for (int v : (int[])pixels) word(out,v,4);
        else throw new IllegalArgumentException("Unknown ImageJ plane storage");
        return out.toString();
    }
    static String lutHash(LUT lut) throws Exception {
        byte[] bytes = new byte[768];
        for (int i=0; i<256; ++i) { bytes[3*i]=(byte)lut.getRed(i); bytes[3*i+1]=(byte)lut.getGreen(i); bytes[3*i+2]=(byte)lut.getBlue(i); }
        StringBuilder out=new StringBuilder();
        for (byte v : MessageDigest.getInstance("SHA-256").digest(bytes)) word(out,v&255,1);
        return out.toString();
    }
    static Object roi(Roi r) {
        if (r==null) return null;
        Rectangle2D b=r.getFloatBounds(); Color color=r.getStrokeColor();
        return map("name",r.getName(),"type",r.getType(),"bounds",new double[]{b.getX(),b.getY(),b.getWidth(),b.getHeight()},
            "position",r.getPosition(),"czt",new int[]{r.getCPosition(),r.getZPosition(),r.getTPosition()},
            "hyper_position",r.hasHyperStackPosition(),"stroke_width",r.getStrokeWidth(),
            "stroke_argb",color==null?null:Long.valueOf(color.getRGB()&0xffffffffL));
    }
    static Object state(ImagePlus image) throws Exception {
        if (image==null) return null;
        Calibration c=image.getCalibration();
        List<Object> overlays=new ArrayList<Object>();
        if (image.getOverlay()!=null) for (Roi r : image.getOverlay().toArray()) overlays.add(roi(r));
        List<Object> planes=new ArrayList<Object>();
        for (int i=1; i<=image.getStackSize(); ++i)
            planes.add(map("label",image.getStack().getSliceLabel(i),"raw_le_hex",raw(image.getStack().getPixels(i))));
        List<Object> luts=new ArrayList<Object>();
        if (image.getBitDepth()!=24) for (LUT lut : image.getLuts())
            luts.add(map("rgb256_sha256",lutHash(lut),"min_f64",f64(lut.min),"max_f64",f64(lut.max)));
        List<String> coefficients=new ArrayList<String>();
        if (c.getCoefficients()!=null) for (double v : c.getCoefficients()) coefficients.add(f64(v));
        return map("title",image.getTitle(),"dimensions_xyczt",image.getDimensions(),"bit_depth",image.getBitDepth(),
            "plane_storage",image.getBitDepth()==24?"java_int32_rgb":"scalar", "stack_order","t,z,c",
            "position_czt",new int[]{image.getC(),image.getZ(),image.getT()},"hyperstack",image.isHyperStack(),
            "open_as_hyperstack",image.getOpenAsHyperStack(),"composite_mode",image instanceof CompositeImage?((CompositeImage)image).getMode():0,
            "active_channels",image instanceof CompositeImage?((CompositeImage)image).getActiveChannels():null,
            "display_min_f64",f64(image.getDisplayRangeMin()),"display_max_f64",f64(image.getDisplayRangeMax()),
            "calibration",map("spacing_f64",new String[]{f64(c.pixelWidth),f64(c.pixelHeight),f64(c.pixelDepth)},
                "origin_indices_f64",new String[]{f64(c.xOrigin),f64(c.yOrigin),f64(c.zOrigin)},"unit",c.getUnit(),
                "frame_interval_f64",f64(c.frameInterval),"time_unit",c.getTimeUnit(),
                "density_function",c.getFunction(),"density_coefficients_f64",coefficients,"value_unit",c.getValueUnit()),
            "note",image.getProperty("imagejxx.reference.note"),"clip_when_summing",image.getProp("ClipWhenSumming"),
            "selection",roi(image.getRoi()),"overlay",overlays,"luts",luts,"planes",planes);
    }
    static Roi marked(String name, int c, int z, int t, boolean hyper) {
        Roi r=new Roi(.25,0,1.5,1); r.setName(name); r.setStrokeWidth(2); r.setStrokeColor(new Color(0xff123456,true));
        if (hyper) r.setPosition(c,z,t); else r.setPosition(z);
        return r;
    }
    static ImagePlus source(String id, String kind, String profile, int channels, int zs, int times, boolean hyper) {
        int width=channels>1?2:4, height=channels>1?2:1;
        ImageStack stack=new ImageStack(width,height);
        int[] signed={0,16384,32768,49152,65535};
        int[][] rounding={{0x4b800000,0x3f800000,0xcb800000},{0x7f7fffff,0x7f7fffff,0x7f7fffff},
            {0x3f800001,0x3f800000,0x3f800001},{0x00000001,0x00000002,0x00000003}};
        int[][] nonfinite={{0x7fc12345,0x3f800000,0x40000000,0x40400000,0x40800000},
            {0x7f800000,0x7f800000,0x7f800000,0x7f800000,0x7f800000},
            {0xff800000,0xff800000,0xff800000,0xff800000,0xff800000},
            {0x7f800000,0xff800000,0x3f800000,0x7fc00001,0x00000000}};
        int[][] zeros={{0x80000000,0,0x80000000,0,0x80000000},{0x80000000,0x80000000,0x80000000,0x80000000,0x80000000},
            {0x7fa12345,0x3f800000,0x40000000,0x40400000,0x40800000},{0xffa12345,0xffc12345,0x7fc12345,0x7fa00001,0x7fc00000}};
        for (int t=0; t<times; ++t) for (int z=0; z<zs; ++z) for (int ch=0; ch<channels; ++ch) {
            int zi=zs==1 && times>1?t:z;
            ImageProcessor ip=kind.equals("u8")?new ByteProcessor(width,height):kind.equals("u16")?new ShortProcessor(width,height):
                kind.equals("f32")?new FloatProcessor(width,height):new ColorProcessor(width,height);
            for (int p=0; p<width*height; ++p) {
                if (kind.equals("rgb")) ip.set(p,0xff000000|((20*z+7*p+30*t+40*ch)&255)<<16|((200-15*z+3*p+10*t)&255)<<8|((60+11*z+5*p+20*ch)&255));
                else if (kind.equals("f32")) {
                    float v=(float)(zi*zi*.25-zi+p*.125);
                    if (channels>1) v=(1000*t+100*ch+10*z+p)/8f;
                    if (profile.equals("rounding")) v=Float.intBitsToFloat(rounding[p][z]);
                    if (profile.equals("nonfinite")) v=Float.intBitsToFloat(nonfinite[p][z]);
                    if (profile.equals("all_nan")) v=Float.intBitsToFloat((p%2==0?0x7fc12345:0xffc12345));
                    if (profile.equals("zeros_snan")) v=Float.intBitsToFloat(zeros[p][z]);
                    ip.setf(p,v);
                } else {
                    int v=kind.equals("u8")?3*zi*zi+2*p+1:1000*zi*zi+zi+p*p;
                    if (channels>1) v=kind.equals("u8")?40*t+25*ch+5*z+p:10000*t+1000*ch+10*z+p;
                    if (profile.equals("signed16")) v=(signed[z]+p)&65535;
                    ip.set(p,v);
                }
            }
            stack.addSlice("t"+t+"/c"+ch+"/z"+z,ip);
        }
        ImagePlus image=new ImagePlus(id,stack); image.setDimensions(channels,zs,times); image.setOpenAsHyperStack(hyper);
        if (channels>1 && !kind.equals("rgb")) {
            image=new CompositeImage(image,CompositeImage.COMPOSITE);
            for (int c=1; c<=channels; ++c) { LUT lut=LUT.createLutFromColor(c==1?Color.RED:Color.GREEN); lut.min=11*c; lut.max=111*c; ((CompositeImage)image).setChannelLut(lut,c); }
            image.setActiveChannels("10");
        }
        Calibration cal=new Calibration(); cal.pixelWidth=.5; cal.pixelHeight=1.25; cal.pixelDepth=3;
        cal.xOrigin=2; cal.yOrigin=-4; cal.zOrigin=1.5; cal.frameInterval=2.5; cal.setUnit("um"); cal.setTimeUnit("min");
        if (profile.equals("signed16")) cal.setSigned16BitCalibration();
        image.setCalibration(cal); image.setPosition(channels,Math.min(zs,2),Math.min(times,2));
        image.setProperty("imagejxx.reference.note","note/"+id);
        if (profile.equals("clip")) image.setProp("ClipWhenSumming","true");
        Overlay overlay=new Overlay();
        if (hyper) {
            overlay.add(marked("global",0,0,0,true)); overlay.add(marked("in-t1",1,2,1,true));
            overlay.add(marked("in-t2",2,3,2,true)); overlay.add(marked("in-t3",1,4,3,true));
            overlay.add(marked("out-z",2,1,2,true)); overlay.add(marked("wild-c-out-z",0,1,3,true));
            overlay.add(marked("wild-z",2,0,2,true));
        } else { overlay.add(marked("global",0,0,0,false)); overlay.add(marked("in",0,2,0,false)); overlay.add(marked("out",0,1,0,false)); }
        image.setOverlay(overlay); Roi active=new Roi(0,0,1,1); active.setName("active-selection"); image.setRoi(active);
        return image;
    }
    static void capture(String id, String kind, String profile, String method, int channels, int zs, int times,
                        boolean hyper, int first, int last, boolean all, int group) throws Exception {
        ImagePlus input=source(id,kind,profile,channels,zs,times,hyper);
        Map<String,Object> record=map("case_id",id,"kind",kind,"profile",profile,"method",method,
            "entrypoint",group==0?"ZProjector.run":"GroupedZProjector.groupZProject", "bounds_one_based",new int[]{first,last},
            "all_time_frames",all,"group_size",group,"ui_executed",false,"before",state(input));
        record.put("expected_outcome",hyper && channels>1 && kind.equals("rgb")?"exception":"ok");
        try {
            ImagePlus output;
            if (group==0) output=ZProjector.run(input,method+(all?" all":""),first,last);
            else output=new GroupedZProjector().groupZProject(input,Arrays.asList(METHODS).indexOf(method),group);
            record.put("output_api",state(output));
            if (group!=0) {
                output.setCalibration(input.getCalibration()); output.getCalibration().pixelDepth*=group;
                record.put("output_after_plugin_calibration_steps",state(output));
                record.put("calibration_steps_reproduced_from_source",true);
            }
            record.put("status","ok");
        } catch (Throwable failure) {
            record.put("status","exception"); record.put("exception",failure.getClass().getName()); record.put("message",failure.getMessage());
            List<String> frames=new ArrayList<String>();
            for (StackTraceElement frame : failure.getStackTrace()) { if (frames.size()==8) break; frames.add(frame.toString()); }
            record.put("exception_frames",frames);
        }
        record.put("after",state(input));
        String row=json(record); emitted+=row.length()+1;
        if (emitted>4*1024*1024) throw new IllegalStateException("Reference capture exceeds4MiB");
        System.out.print(row); System.out.print('\n'); ++cases;
    }
    public static void main(String[] args) throws Exception {
        if (!IJ.getVersion().equals("1.54r")) throw new IllegalStateException("Wrong ImageJ reference version");
        for (String kind : new String[]{"u8","u16","f32","rgb"}) for (String profile : new String[]{"full","range_even","singleton"})
            for (String method : METHODS) capture("scalar."+kind+"."+profile+"."+method,kind,profile,method,1,
                profile.equals("range_even")?6:profile.equals("singleton")?3:5,1,false,profile.equals("full")?1:2,profile.equals("singleton")?2:5,false,0);
        for (String method : METHODS) capture("scalar.u16.signed16."+method,"u16","signed16",method,1,5,1,false,1,5,false,0);
        for (String profile : new String[]{"rounding","nonfinite","all_nan","zeros_snan"}) for (String method : METHODS)
            capture("scalar.f32."+profile+"."+method,"f32",profile,method,1,profile.equals("rounding")?3:5,1,false,1,profile.equals("rounding")?3:5,false,0);
        for (String method : METHODS) capture("scalar.rgb.clip."+method,"rgb","clip",method,1,5,1,false,1,5,false,0);
        for (String kind : new String[]{"u8","u16","f32","rgb"}) for (int zs : new int[]{4,1})
            for (boolean all : new boolean[]{false,true}) for (String method : METHODS)
                capture("hyper."+kind+".z"+zs+".all"+all+"."+method,kind,"hyper",method,2,zs,3,true,zs==1?1:2,zs==1?3:4,all,0);
        for (int zs : new int[]{4,1}) for (boolean all : new boolean[]{false,true}) for (String method : METHODS)
            capture("hyper_rgb_c1.z"+zs+".all"+all+"."+method,"rgb","hyper",method,1,zs,3,true,zs==1?1:2,zs==1?3:4,all,0);
        for (String kind : new String[]{"u8","u16","f32","rgb"}) for (boolean time : new boolean[]{false,true})
            for (int group : new int[]{2,3}) for (String method : METHODS)
                capture("group."+kind+"."+(time?"time":"z")+".g"+group+"."+method,kind,"group",method,1,time?1:6,time?6:1,false,1,6,true,group);
        if (cases!=324) throw new IllegalStateException("Unexpected reference case count: "+cases);
    }
}
