package ij.plugin;
import ij.*;
import ij.gui.*;
import ij.process.ByteProcessor;
import java.awt.Rectangle;
import java.io.*;
import java.util.Random;

/** Original observer invoking the pinned editor method, not a copied fit. */
public class ObserveFitRectangle {
    static boolean comma;
    static void sample(String id,String kind,double[][] points) {
        float[] x=new float[points.length],y=new float[points.length];
        for(int i=0;i<points.length;i++){x[i]=(float)points[i][0];y[i]=(float)points[i][1];}
        Roi roi;
        if(kind.equals("rectangle") || kind.equals("ellipse")) {
            double a=Math.min(points[0][0],points[1][0]),b=Math.min(points[0][1],points[1][1]);
            double w=Math.abs(points[0][0]-points[1][0]),h=Math.abs(points[0][1]-points[1][1]);
            roi=kind.equals("ellipse")?new OvalRoi(a,b,w,h):new Roi(a,b,w,h);
        } else if(kind.equals("multipoint"))roi=new PointRoi(x,y,x.length);
        else roi=new PolygonRoi(x,y,x.length,kind.equals("polyline")?Roi.POLYLINE:kind.equals("angle")?Roi.ANGLE:kind.equals("freehand")?Roi.FREEROI:Roi.POLYGON);
        ImagePlus image=new ImagePlus("reference",new ByteProcessor(64,64));
        image.setRoi(roi);WindowManager.setTempCurrentImage(image);
        IJ.getErrorMessage();IJ.redirectErrorMessages(true);
        PrintStream output=System.out;ByteArrayOutputStream ignored=new ByteArrayOutputStream();
        System.setOut(new PrintStream(ignored));
        boolean failed=false;
        ij.process.FloatPolygon hull=roi.getFloatConvexHull();
        try{new Selection().run("rect");}catch(RuntimeException e){failed=true;}
        finally{System.setOut(output);}
        failed=failed || IJ.getErrorMessage()!=null || image.getRoi()==null || !(image.getRoi() instanceof RotatedRectRoi);
        if(comma)System.out.print(",");comma=true;
        System.out.print("{\"id\":\""+id+"-"+kind+"\",\"kind\":\""+kind+"\",\"points\":[");
        for(int i=0;i<points.length;i++){if(i>0)System.out.print(",");System.out.print("["+points[i][0]+","+points[i][1]+"]");}
        System.out.print("],\"error\":"+failed);
        if(!failed){double[] params=((RotatedRectRoi)image.getRoi()).getParams();
            System.out.print(",\"params\":[");
            for(int i=0;i<params.length;i++){if(i>0)System.out.print(",");System.out.print(params[i]);}System.out.print("]");}
        if(hull!=null){System.out.print(",\"hull\":[");for(int i=0;i<hull.npoints;i++){if(i>0)System.out.print(",");System.out.print("["+hull.xpoints[i]+","+hull.ypoints[i]+"]");}System.out.print("]");}
        System.out.print("}");WindowManager.setTempCurrentImage(null);
    }
    public static void main(String[] args) {
        System.out.print("{\"reference\":\"ImageJ 1.54r Selection.fitRectangle on64x64 ByteProcessor\",\"cases\":[");
        for(String kind:new String[]{"rectangle","ellipse"}) {
            sample("integer",kind,new double[][]{{3,3},{13,13}});
            sample("subpixel",kind,new double[][]{{2.2,3.7},{12.8,13.2}});
            sample("clipped",kind,new double[][]{{-4,-3},{8,7}});
            sample("tiny",kind,new double[][]{{2,2},{3,3}});
        }
        for(String kind:new String[]{"polygon","freehand"}) {
            sample("triangle",kind,new double[][]{{2,2},{14,2},{8,15}});
            sample("subpixel",kind,new double[][]{{2.2,3.7},{12.8,3.1},{5.5,13.2}});
            sample("clipped",kind,new double[][]{{-4,-3},{8,3},{2,12}});
        }
        for(String kind:new String[]{"multipoint","polyline","angle"}) {
            sample("cardinal",kind,new double[][]{{3,8},{8,3},{13,8},{8,13}});
            sample("subpixel",kind,new double[][]{{3.4,8.1},{8.1,3.6},{13.4,8.1},{8.1,13.6}});
            sample("three",kind,new double[][]{{3,8},{8,3},{13,8}});
            sample("collinear",kind,new double[][]{{2,2},{8,8},{13,13}});
            sample("duplicates",kind,new double[][]{{3,8},{8,3},{13,8},{3,8}});
            sample("too-few",kind,new double[][]{{3,8},{8,3}});
        }
        sample("single","multipoint",new double[][]{{3.4,8.1}});
        sample("duplicates-only","multipoint",new double[][]{{3.4,8.1},{3.4,8.1}});
        sample("fractional-mask","multipoint",new double[][]{{2.4,3.4},{2.6,3.6},{7.1,8.2}});
        Random random=new Random(8197);
        for(int k=0;k<20;k++) {
            double[][] points=new double[5+k%7][2];
            for(int i=0;i<points.length;i++){double a=2*Math.PI*i/points.length;double r=5+random.nextDouble()*2;points[i][0]=24+r*Math.cos(a);points[i][1]=21+r*Math.sin(a);}
            sample("noisy-"+k,"multipoint",points);
        }
        System.out.println("]}");
    }
}
