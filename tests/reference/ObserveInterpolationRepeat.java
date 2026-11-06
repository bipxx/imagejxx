package ij.plugin;
import ij.*;
import ij.gui.*;
import ij.process.*;
import java.awt.geom.*;
import java.io.*;

/** Original two-command observations, including attached canvas context. */
public class ObserveInterpolationRepeat {
    static boolean comma;
    static class CanvasImage extends ImagePlus {
        ImageCanvas canvas;
        CanvasImage(double zoom) {
            super("reference",new ByteProcessor(40,40));
            if(zoom>0){canvas=new ImageCanvas(this);canvas.setMagnification(zoom);}
        }
        public ImageCanvas getCanvas(){return canvas;}
    }
    static void points(FloatPolygon p) {
        System.out.print("[");for(int i=0;i<p.npoints;i++){
            if(i>0)System.out.print(",");
            System.out.print("["+(double)p.xpoints[i]+","+(double)p.ypoints[i]+"]");
        }System.out.print("]");
    }
    static void roi(Roi value) {
        System.out.print("{\"type\":"+value.getType()+",\"samples\":");points(value.getFloatPolygon());
        System.out.print(",\"mask\":[");
        ImageProcessor mask=value.getMask();java.awt.Rectangle b=value.getBounds();
        for(int y=0;y<40;y++){
            if(y>0)System.out.print(",");System.out.print("\"");
            for(int x=0;x<40;x++)System.out.print(b.contains(x,y)&&(mask==null||mask.getPixel(x-b.x,y-b.y)!=0)?"1":"0");
            System.out.print("\"");
        }System.out.print("]}");
    }
    static Roi source(String kind) {
        float[] x={3.2f,13.8f,19.4f,11.1f,4.3f},y={4.7f,3.1f,14.2f,20.6f,13.8f};
        if(kind.equals("collinear")){x=new float[]{2,8,14};y=new float[]{2,2,2};}
        if(kind.equals("tiny")){x=new float[]{2,2.1f,2.2f};y=new float[]{2,2.2f,2};}
        if(kind.equals("dense")){
            x=new float[100];y=new float[100];
            for(int i=0;i<100;i++){x[i]=(float)(20+15*Math.cos(i*Math.PI/50));y[i]=(float)(20+15*Math.sin(i*Math.PI/50));}
        }
        if(kind.equals("point"))return new PointRoi(new float[]{4.5f},new float[]{6.25f},1);
        if(kind.equals("multipoint"))return new PointRoi(x,y,x.length);
        if(kind.equals("line"))return new Line(x[0],y[0],x[1],y[1]);
        int type=kind.equals("polyline")?Roi.POLYLINE:kind.equals("freeline")?Roi.FREELINE:
            kind.equals("traced")?Roi.TRACED_ROI:kind.equals("freehand")?Roi.FREEROI:Roi.POLYGON;
        Roi result=new PolygonRoi(x,y,x.length,type);
        if(kind.equals("shape"))return new ShapeRoi(result);
        if(kind.equals("compound"))return new ShapeRoi(result).xor(new ShapeRoi(new Roi(27,27,6,6)));
        return result;
    }
    static void sample(String kind,double zoom,double interval,boolean smooth,boolean adjust) {
        CanvasImage image=new CanvasImage(zoom);Roi original=source(kind);image.setRoi(original);
        WindowManager.setTempCurrentImage(image);
        if(comma)System.out.print(",");comma=true;
        System.out.print("{\"id\":\""+kind+"-"+zoom+"-"+interval+"-"+smooth+"-"+adjust+"\",\"kind\":\""+kind+"\",\"magnification\":"+zoom+",\"interval\":"+interval+",\"smooth\":"+smooth+",\"adjust\":"+adjust+",\"source\":");roi(original);
        System.out.print(",\"steps\":[");
        for(int step=0;step<2;step++){
            if(step>0)System.out.print(",");
            IJ.getErrorMessage();IJ.redirectErrorMessages(true);boolean failed=false;
            PrintStream out=System.out;System.setOut(new PrintStream(new ByteArrayOutputStream()));
            try{
                Macro.setOptions("interval="+interval+(smooth?" smooth":"")+(adjust?" adjust":""));
                new Selection().run("interpolate");
            }catch(RuntimeException error){failed=true;}
            finally{Macro.setOptions(null);System.setOut(out);}
            failed|=IJ.getErrorMessage()!=null||image.getRoi()==null;
            System.out.print("{\"error\":"+failed+",\"same_source\":"+(image.getRoi()==original));
            if(!failed){System.out.print(",\"result\":");roi(image.getRoi());}
            System.out.print("}");
        }
        System.out.print("]}");WindowManager.setTempCurrentImage(null);
    }
    public static void main(String[] args) {
        Thread.currentThread().setName("Run$_imagejxx-repeat-reference");
        System.out.print("{\"reference\":\"ImageJ1.54r Selection.run(interpolate) twice\",\"cases\":[");
        for(String kind:new String[]{"polygon","freehand","polyline","freeline","traced","line","shape","compound"})
            for(double zoom:new double[]{0,11.99,12})for(double interval:new double[]{1,8})
                for(boolean smooth:new boolean[]{false,true})sample(kind,zoom,interval,smooth,false);
        for(String kind:new String[]{"polygon","freeline","traced"})for(double interval:new double[]{2.7,-2.7})
            for(boolean adjust:new boolean[]{false,true})sample(kind,16,interval,true,adjust);
        for(String kind:new String[]{"point","multipoint","tiny","collinear"})
            for(double interval:new double[]{1,8})sample(kind,12,interval,true,true);
        for(double zoom:new double[]{11.99,12})for(double interval:new double[]{.5,.7})sample("dense",zoom,interval,false,false);
        System.out.println("]}");
    }
}
