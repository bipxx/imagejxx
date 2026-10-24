import ij.IJ;
import ij.Prefs;
import ij.gui.ShapeRoi;
import ij.plugin.filter.GaussianBlur;
import ij.plugin.filter.RankFilters;
import ij.process.ByteProcessor;
import ij.process.FloatProcessor;
import ij.process.ImageProcessor;
import java.awt.Rectangle;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/** Reference kernel harness for explicitly defined saved-mask OR semantics. */
public final class ObserveScientificPathFilterTargets {
    private static final int WIDTH=23, HEIGHT=17;
    private static final float[] P={0,2.2f,1.2f,1,9.2f,1.2f,1,2.2f,8.2f,4};
    private static final float[] Q={0,5.2f,2.2f,1,12.2f,2.2f,1,5.2f,9.2f,4};
    private ObserveScientificPathFilterTargets() {}

    public static void main(String[] args) {
        try {
            ObservePathFilterTargets.require(args.length==4,"name gaussian|mean offset active|union");
            ObservePathFilterTargets.require(args[0].matches("[A-Za-z0-9][A-Za-z0-9._-]{0,79}"),"Case name");
            boolean gaussian="gaussian".equals(args[1]);
            ObservePathFilterTargets.require(gaussian || "mean".equals(args[1]),"Operation");
            int offset=Integer.parseInt(args[2]);
            ObservePathFilterTargets.require(offset>=0 && offset<=1024,"Offset bound");
            boolean union="union".equals(args[3]);
            ObservePathFilterTargets.require(union || "active".equals(args[3]),"Region mode");
            System.setProperty("java.awt.headless","true");
            ObservePathFilterTargets.require("1.54r".equals(IJ.getVersion()),"Reference version");
            Prefs.setThreads(1);
            float[] pixels=new float[WIDTH*HEIGHT];
            for(int y=0;y<HEIGHT;++y) for(int x=0;x<WIDTH;++x)
                pixels[y*WIDTH+x]=((x*37+y*53+(x*y)%17)%257-128)/16.0f+offset;
            if(offset==0) {pixels[0]=Float.intBitsToFloat(0x80000000);pixels[1]=0;}
            FloatProcessor processor=new FloatProcessor(WIDTH,HEIGHT,pixels);
            byte[] canvas=new byte[WIDTH*HEIGHT];
            List<Object> operands=new ArrayList<Object>();
            Rectangle extent=null;
            for(float[] commands:union?new float[][]{P,Q}:new float[][]{P}) {
                ShapeRoi roi=new ShapeRoi(commands);
                Rectangle bounds=roi.getBounds();
                ObservePathFilterTargets.checkedArea(bounds.width,bounds.height,1024*1024,true,"Operand mask");
                ImageProcessor mask=roi.getMask();
                operands.add(ObservePathFilterTargets.object("commands_f32_words",
                    ObservePathFilterTargets.floatWords(commands),"geometry",ObservePathFilterTargets.roiMetadata(roi),
                    "mask",ObservePathFilterTargets.maskCapture(mask,bounds.x,bounds.y)));
                extent=extent==null?bounds:extent.union(bounds);
                byte[] values=(byte[])mask.getPixels();
                for(int y=Math.max(0,bounds.y);y<Math.min(HEIGHT,bounds.y+bounds.height);++y)
                    for(int x=Math.max(0,bounds.x);x<Math.min(WIDTH,bounds.x+bounds.width);++x)
                        if(values[(y-bounds.y)*bounds.width+x-bounds.x]!=0) canvas[y*WIDTH+x]=(byte)255;
            }
            extent=extent.intersection(new Rectangle(0,0,WIDTH,HEIGHT));
            byte[] local=new byte[extent.width*extent.height];
            for(int y=0;y<extent.height;++y) for(int x=0;x<extent.width;++x)
                local[y*extent.width+x]=canvas[(extent.y+y)*WIDTH+extent.x+x];
            ImageProcessor mask=new ByteProcessor(extent.width,extent.height,local);
            processor.setRoi(extent);processor.setMask(mask);
            Map<String,Object> result=ObservePathFilterTargets.object(
                "schema","imagejxx.reference.scientific-path-filter-target","version",1,
                "name",args[0],"imagej_version",IJ.getVersion(),"threads",Prefs.getThreads(),
                "java_runtime_version",System.getProperty("java.runtime.version"),
                "harness","OR of separately rasterized bounded ShapeRoi masks; stored-rectangle union; snapshot reset",
                "options",ObservePathFilterTargets.object("operation",args[1],"input_offset",offset,
                    "region",args[3],"sigma",gaussian?1.0:null,"radius",gaussian?null:2.5),
                "operands",operands,"processor_roi",ObservePathFilterTargets.rectangle(extent),
                "processor_mask",ObservePathFilterTargets.maskCapture(mask,extent.x,extent.y),
                "input_f32_words",ObservePathFilterTargets.floatWords(pixels));
            processor.snapshot();
            if(gaussian)new GaussianBlur().blurGaussian(processor,1,1,0.0002);
            else new RankFilters().rank(processor,2.5,RankFilters.MEAN);
            ObservePathFilterTargets.require(!Thread.currentThread().isInterrupted(),"Interrupted");
            result.put("before_mask_reset_f32_words",ObservePathFilterTargets.floatWords((float[])processor.getPixels()));
            processor.reset(mask);
            result.put("after_mask_reset_f32_words",ObservePathFilterTargets.floatWords((float[])processor.getPixels()));
            System.out.println(ObservePathFilterTargets.Json.encode(result));System.out.flush();
            if(System.out.checkError())throw new IllegalStateException("Incomplete scientific output");
            System.exit(0);
        } catch(IllegalArgumentException error) {ObservePathFilterTargets.fail(2,error);}
        catch(Exception error) {ObservePathFilterTargets.fail(1,error);}
    }
}
