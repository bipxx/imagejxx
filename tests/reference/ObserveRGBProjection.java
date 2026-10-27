// Project-owned supplemental observer; pinned ImageJ APIs only, no target code.
import ij.ImagePlus;
import ij.ImageStack;
import ij.measure.Calibration;
import ij.plugin.ZProjector;
import ij.process.ColorProcessor;

public class ObserveRGBProjection extends ObserveScientificZProjection {
    static ImagePlus varied(String id, String profile, boolean clip) {
        boolean large = profile.equals("rounding_large");
        int w = large ? 257 : 17, h = large ? 1 : 2, depth = large ? 32 : 7;
        ImageStack stack = new ImageStack(w,h);
        for (int z=0; z<depth; ++z) {
            int[] pixels = new int[w*h];
            for (int y=0; y<h; ++y) for (int x=0; x<w; ++x) {
                int rgb = 0xff000000;
                for (int c=0; c<3; ++c) {
                    int v;
                    if (profile.equals("zero")) v=0;
                    else if (profile.equals("constant")) v=new int[]{17,81,203}[c];
                    else if (profile.equals("stationary")) v=(x*x*(c+3)+y*73+c*41)&255;
                    else if (profile.equals("rounding"))
                        v=(x*x*23+y*157+z*z*41+c*37+(x+z)*(c+1)*11)&255;
                    else {
                        int bits=0x6d2b79f5 ^ (x*0x9e3779b9) ^ (y*0x85ebca6b)
                            ^ (z*0xc2b2ae35) ^ (c*0x27d4eb2f);
                        bits=bits*1664525+1013904223;
                        bits^=bits>>>13;
                        v=(bits>>>8)&255;
                    }
                    rgb|=v<<(16-8*c);
                }
                pixels[y*w+x]=rgb;
            }
            stack.addSlice("z"+z,new ColorProcessor(w,h,pixels));
        }
        ImagePlus image=new ImagePlus(id,stack);
        Calibration cal=image.getCalibration();
        cal.pixelWidth=.5;cal.pixelHeight=1.25;cal.pixelDepth=3;
        cal.xOrigin=2;cal.yOrigin=-4;cal.zOrigin=1.5;
        cal.frameInterval=2.5;cal.setTimeUnit("min");cal.setUnit("um");
        image.setProp("ClipWhenSumming",clip?"true":"false");
        return image;
    }
    static void observe(String profile, String range, String method, boolean clip) throws Exception {
        String id="rgb-extra."+profile+"."+range+"."+method+".clip"+clip;
        ImagePlus image=varied(id,profile,clip);
        int first=range.equals("even")?2:1;
        int last=range.equals("even")?5:image.getStackSize();
        Object before=state(image);
        ImagePlus result=ZProjector.run(image,method,first,last);
        String row=json(map("case_id",id,"kind","rgb","profile",profile,
            "method",method,"entrypoint","ZProjector.run",
            "bounds_one_based",new int[]{first,last},"all_time_frames",false,
            "group_size",0,"ui_executed",false,"before",before,
            "expected_outcome","ok","output_api",state(result),"status","ok",
            "after",state(image)));
        emitted+=row.length()+1;
        if (emitted>4*1024*1024) throw new IllegalStateException("Capture byte limit exceeded");
        System.out.println(row);
        ++cases;
    }
    public static void main(String[] args) throws Exception {
        for (String p : new String[]{"zero","constant","stationary","varied","rounding"})
            for (String range : new String[]{"full","even"})
                for (String method : METHODS)
                    for (boolean clip : new boolean[]{false,true}) observe(p,range,method,clip);
        for (String method : new String[]{"sum","sd"})
            for (boolean clip : new boolean[]{false,true}) observe("rounding_large","full",method,clip);
        if (cases!=124) throw new IllegalStateException("Wrong case count");
        System.err.println("124 supplemental RGB cases observed");
    }
}
