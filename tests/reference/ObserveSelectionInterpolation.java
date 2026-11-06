package ij.plugin;
import ij.*;
import ij.gui.*;
import ij.process.*;
import java.io.*;

/** Original observation of the actual Interpolate command, not copied math. */
public class ObserveSelectionInterpolation {
    static boolean comma;
    static void points(FloatPolygon p) {
        System.out.print("[");
        for(int i=0;i<p.npoints;i++) {
            if(i>0)System.out.print(",");
            System.out.print("["+(double)p.xpoints[i]+","+(double)p.ypoints[i]+"]");
        }
        System.out.print("]");
    }
    static void sample(String id,String kind,double[][] input,double interval,boolean smooth,boolean adjust,boolean fitted) {
        float[] x=new float[input.length],y=new float[input.length];
        for(int i=0;i<x.length;i++){x[i]=(float)input[i][0];y[i]=(float)input[i][1];}
        Roi roi;
        if(kind.equals("line"))roi=new Line(input[0][0],input[0][1],input[1][0],input[1][1]);
        else if(kind.equals("rectangle"))roi=new Roi(input[0][0],input[0][1],input[1][0]-input[0][0],input[1][1]-input[0][1]);
        else roi=new PolygonRoi(x,y,x.length,kind.equals("polyline")?Roi.POLYLINE:kind.equals("freehand")?Roi.FREEROI:Roi.POLYGON);
        if(fitted)((PolygonRoi)roi).fitSpline(47);
        ImagePlus image=new ImagePlus("reference",new ByteProcessor(128,128));image.setRoi(roi);WindowManager.setTempCurrentImage(image);
        IJ.getErrorMessage();IJ.redirectErrorMessages(true);
        PrintStream output=System.out;System.setOut(new PrintStream(new ByteArrayOutputStream()));
        boolean failed=false;Roi result=null;
        try {
            Macro.setOptions("interval="+interval+(smooth?" smooth":"")+(adjust?" adjust":""));
            new Selection().run("interpolate");result=image.getRoi();
        } catch(RuntimeException e){failed=true;}
        finally {System.setOut(output);Macro.setOptions(null);}
        failed=failed || IJ.getErrorMessage()!=null || result==null;
        FloatPolygon p=failed?null:result.getFloatPolygon();
        if(!failed)for(int i=0;i<p.npoints;i++)failed|=!Float.isFinite(p.xpoints[i]) || !Float.isFinite(p.ypoints[i]);
        if(comma)System.out.print(",");comma=true;
        System.out.print("{\"id\":\""+id+"-"+kind+"-"+interval+"-"+smooth+"-"+adjust+"-"+fitted+"\",\"kind\":\""+kind+"\",\"interval\":"+interval+",\"smooth\":"+smooth+",\"adjust\":"+adjust+",\"fitted\":"+fitted+",\"input\":[");
        for(int i=0;i<input.length;i++){if(i>0)System.out.print(",");System.out.print("["+input[i][0]+","+input[i][1]+"]");}
        System.out.print("],\"error\":"+failed);
        if(!failed){System.out.print(",\"type\":"+result.getType()+",\"samples\":");points(p);}
        System.out.print("}");WindowManager.setTempCurrentImage(null);
    }
    public static void main(String[] args) {
        Thread.currentThread().setName("Run$_imagejxx-interpolation-reference");
        System.out.print("{\"reference\":\"ImageJ1.54r Selection.run(interpolate)\",\"cases\":[");
        double[][][] shapes={{{3,3},{13,3},{13,13},{3,13}},{{2.2,3.7},{12.8,3.1},{5.5,13.2}},{{-4,-3},{8,3},{8,3},{2,12},{-2,1}}};
        for(String kind:new String[]{"polygon","polyline","freehand"})
            for(int i=0;i<shapes.length;i++)for(double interval:new double[]{0.5,1,2.7,8})
                for(boolean smooth:new boolean[]{false,true})for(boolean adjust:new boolean[]{false,true})
                    sample("shape"+i,kind,shapes[i],interval,smooth,adjust,false);
        for(String kind:new String[]{"polygon","polyline"})for(double interval:new double[]{1,2.7})
            for(boolean smooth:new boolean[]{false,true})for(boolean adjust:new boolean[]{false,true})
                sample("spline",kind,shapes[0],interval,smooth,adjust,true);
        for(String kind:new String[]{"line","rectangle"})for(double interval:new double[]{1,2.7,8})
            for(boolean smooth:new boolean[]{false,true})for(boolean adjust:new boolean[]{false,true})
                sample("boxline",kind,new double[][]{{3.2,3.7},{13.8,13.1}},interval,smooth,adjust,false);
        for(double interval:new double[]{-2.7,0,0.005})for(boolean adjust:new boolean[]{false,true})
            sample("signed","polyline",shapes[0],interval,false,adjust,false);
        for(String kind:new String[]{"polygon","polyline"})for(boolean adjust:new boolean[]{false,true})
            sample("collinear",kind,new double[][]{{2,2},{8,2},{14,2}},2.7,false,adjust,false);
        System.out.println("]}");
    }
}
