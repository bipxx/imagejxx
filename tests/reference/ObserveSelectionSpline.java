package ij.plugin;
import ij.*;
import ij.gui.*;
import ij.process.*;
import java.io.*;

/** Original observer: invoke the editor dispatch or its explicit-count method. */
public class ObserveSelectionSpline {
    static boolean comma;
    static void points(FloatPolygon p) {
        System.out.print("[");
        for(int i=0;i<p.npoints;i++) {
            if(i>0)System.out.print(",");
            System.out.print("["+(double)p.xpoints[i]+","+(double)p.ypoints[i]+"]");
        }
        System.out.print("]");
    }
    static void sample(String id,String kind,String mode,double[][] input,int count) {
        float[] x=new float[input.length],y=new float[input.length];
        for(int i=0;i<x.length;i++){x[i]=(float)input[i][0];y[i]=(float)input[i][1];}
        int type=kind.equals("polygon")?Roi.POLYGON:kind.equals("polyline")?Roi.POLYLINE:Roi.FREEROI;
        PolygonRoi roi=new PolygonRoi(x,y,x.length,type);
        ImagePlus image=new ImagePlus("reference",new ByteProcessor(128,128));
        image.setRoi(roi);WindowManager.setTempCurrentImage(image);
        IJ.getErrorMessage();IJ.redirectErrorMessages(true);
        PrintStream output=System.out;System.setOut(new PrintStream(new ByteArrayOutputStream()));
        boolean failed=false;PolygonRoi result=null;
        try {
            Macro.setOptions(mode.equals("straighten")?"straighten":"");
            if(count>0)roi.fitSpline(count);else new Selection().run("spline");
            if(mode.equals("remove")){Macro.setOptions("remove");new Selection().run("spline");}
            if(mode.equals("refit")){Macro.setOptions("");new Selection().run("spline");}
            result=(PolygonRoi)image.getRoi();
        } catch(RuntimeException e){failed=true;}
        finally {System.setOut(output);Macro.setOptions(null);}
        failed=failed || IJ.getErrorMessage()!=null || result==null;
        if(!failed)for(float v:result.getFloatPolygon().xpoints)failed|=!Float.isFinite(v);
        if(!failed)for(float v:result.getFloatPolygon().ypoints)failed|=!Float.isFinite(v);
        if(comma)System.out.print(",");comma=true;
        System.out.print("{\"id\":\""+id+"-"+kind+"-"+mode+"-"+count+"\",\"kind\":\""+kind+"\",\"mode\":\""+mode+"\",\"evaluation_points\":"+count+",\"input\":[");
        for(int i=0;i<input.length;i++){if(i>0)System.out.print(",");System.out.print("["+input[i][0]+","+input[i][1]+"]");}
        System.out.print("],\"error\":"+failed);
        if(!failed) {
            System.out.print(",\"type\":"+result.getType()+",\"fitted\":"+result.isSplineFit()+",\"controls\":");points(result.getNonSplineFloatPolygon());
            System.out.print(",\"samples\":");points(result.getFloatPolygon());
        }
        System.out.print("}");WindowManager.setTempCurrentImage(null);
    }
    public static void main(String[] args) {
        Thread.currentThread().setName("Run$_imagejxx-spline-reference");
        System.out.print("{\"reference\":\"ImageJ1.54r Selection.run(spline) and PolygonRoi.fitSpline(count)\",\"cases\":[");
        double[][][] inputs={{{3,3},{13,3},{13,13},{3,13}},{{2.2,3.7},{12.8,3.1},{5.5,13.2}},{{2,2},{6,10},{6,10},{14,2}},{{-4,-3},{8,3},{2,12}},{{2,2},{8,2},{14,2}}};
        for(String kind:new String[]{"polygon","polyline","freehand"})
            for(String mode:new String[]{"fit","straighten","remove"})
                for(int i=0;i<inputs.length;i++)sample("shape"+i,kind,mode,inputs[i],0);
        for(String kind:new String[]{"polygon","polyline"})
            for(int count:new int[]{7,47,137})sample("explicit",kind,"fit",inputs[0],count);
        for(int i:new int[]{1,3,4})sample("repeat"+i,"freehand","refit",inputs[i],0);
        double[][] longLine={{0,0},{200,0},{400,200}};
        for(String mode:new String[]{"fit","refit"})sample("long","polyline",mode,longLine,0);
        System.out.println("]}");
    }
}
