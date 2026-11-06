package ij.plugin;
import ij.*;
import ij.gui.*;
import ij.process.*;
import java.awt.geom.*;
import java.io.*;

/** Original observations of actual compound dispatch and component methods. */
public class ObserveInterpolationComponents {
    static boolean comma;
    static double[] rect(double x,double y,double r,double b) {
        return new double[]{0,x,y,1,r,y,1,r,b,1,x,b,4};
    }
    static double[] join(double[]... paths) {
        int n=0;for(double[] p:paths)n+=p.length;double[] out=new double[n];int at=0;
        for(double[] p:paths)for(double v:p)out[at++]=v;return out;
    }
    static GeneralPath path(double[] values) {
        GeneralPath p=new GeneralPath(GeneralPath.WIND_EVEN_ODD);
        for(int i=0;i<values.length;) {
            int op=(int)values[i++];
            if(op==0){p.moveTo(values[i],values[i+1]);i+=2;}
            else if(op==1){p.lineTo(values[i],values[i+1]);i+=2;}
            else if(op==2){p.quadTo(values[i],values[i+1],values[i+2],values[i+3]);i+=4;}
            else if(op==3){p.curveTo(values[i],values[i+1],values[i+2],values[i+3],values[i+4],values[i+5]);i+=6;}
            else p.closePath();
        }
        return p;
    }
    static void numbers(double[] a) {System.out.print("[");for(int i=0;i<a.length;i++){if(i>0)System.out.print(",");System.out.print(a[i]);}System.out.print("]");}
    static void points(FloatPolygon p) {
        System.out.print("[");for(int i=0;i<p.npoints;i++){if(i>0)System.out.print(",");System.out.print("["+(double)p.xpoints[i]+","+(double)p.ypoints[i]+"]");}System.out.print("]");
    }
    static void mask(Roi roi) {
        ImageProcessor m=roi==null?null:roi.getMask();java.awt.Rectangle b=roi==null?new java.awt.Rectangle():roi.getBounds();
        System.out.print("[");for(int y=0;y<40;y++){if(y>0)System.out.print(",");System.out.print("\"");for(int x=0;x<40;x++)System.out.print(b.contains(x,y) && (m==null || m.getPixel(x-b.x,y-b.y)!=0)?"1":"0");System.out.print("\"");}System.out.print("]");
    }
    static void sample(String scenario,String kind,double[] commands,double interval,boolean smooth,boolean adjust) {
        Roi source;
        if(kind.equals("ellipse"))source=new OvalRoi(3.2,4.7,23.6,15.2);
        else if(kind.equals("rounded_rectangle")){source=new Roi(3,4,24,16);source.setCornerDiameter(6);}
        else source=new ShapeRoi(path(commands));
        Roi[] pieces=source instanceof ShapeRoi?((ShapeRoi)source).getRois():new Roi[]{source};
        FloatPolygon[] inputs=new FloatPolygon[pieces.length],samples=new FloatPolygon[pieces.length];ShapeRoi expected=null;
        boolean profileFailed=false;IJ.getErrorMessage();IJ.redirectErrorMessages(true);
        PrintStream output=System.out;System.setOut(new PrintStream(new ByteArrayOutputStream()));
        try {
            for(int i=0;i<pieces.length;i++) {
                inputs[i]=pieces[i].getFloatPolygon();samples[i]=pieces[i].getInterpolatedPolygon(adjust?-interval:interval,smooth);
                ShapeRoi component=new ShapeRoi(new PolygonRoi(samples[i],Roi.POLYGON));
                expected=expected==null?component:expected.xor(component);
            }
        }catch(RuntimeException e){profileFailed=true;}
        finally{System.setOut(output);}
        profileFailed|=IJ.getErrorMessage()!=null;
        ImagePlus image=new ImagePlus("reference",new ByteProcessor(40,40));image.setRoi(source);WindowManager.setTempCurrentImage(image);
        IJ.getErrorMessage();IJ.redirectErrorMessages(true);Roi result=null;boolean failed=false;
        System.setOut(new PrintStream(new ByteArrayOutputStream()));
        try{Macro.setOptions("interval="+interval+(smooth?" smooth":"")+(adjust?" adjust":""));new Selection().run("interpolate");result=image.getRoi();}
        catch(RuntimeException e){failed=true;}finally{System.setOut(output);Macro.setOptions(null);}
        failed|=IJ.getErrorMessage()!=null || result==null;
        if(comma)System.out.print(",");comma=true;
        System.out.print("{\"id\":\""+scenario+"-"+interval+"-"+smooth+"-"+adjust+"\",\"scenario\":\""+scenario+"\",\"kind\":\""+kind+"\",\"interval\":"+interval+",\"smooth\":"+smooth+",\"adjust\":"+adjust+",\"commands\":");numbers(commands);
        System.out.print(",\"components\":[");
        for(int i=0;i<pieces.length;i++){if(i>0)System.out.print(",");System.out.print("{\"type\":"+pieces[i].getType()+",\"input\":");points(inputs[i]);System.out.print(",\"samples\":");points(samples[i]);System.out.print("}");}
        System.out.print("],\"profile_error\":"+profileFailed+",\"command_error\":"+failed+",\"profile_mask\":");mask(expected);
        System.out.print(",\"command_mask\":");mask(result);
        if(scenario.equals("single-curve")){System.out.print(",\"command_samples\":");points(result.getFloatPolygon());}
        System.out.print("}");WindowManager.setTempCurrentImage(null);
    }
    public static void main(String[] args) {
        Thread.currentThread().setName("Run$_imagejxx-compound-interpolation");
        System.out.print("{\"reference\":\"ImageJ1.54r actual Interpolate plus public component sampling/even-odd profile\",\"cases\":[");
        if(args.length>0 && args[0].equals("single")) {
            double[] curve={0,2,15,2,16,-2,30,15,3,28,30,4,30,2,15,4};
            for(double interval:new double[]{1,2.7,5})for(boolean smooth:new boolean[]{false,true})for(boolean adjust:new boolean[]{false,true})
                sample("single-curve","path",curve,interval,smooth,adjust);
            System.out.println("]}");return;
        }
        String[] names={"hole","disjoint","nested","offcanvas","curve","ellipse","rounded"};
        double[][] paths={join(rect(2,2,30,30),rect(10,10,22,22)),join(rect(2,2,14,14),rect(20,18,32,30)),join(rect(2,2,34,34),rect(8,8,28,28),rect(14,14,22,22)),join(rect(-6,-4,24,26),rect(5,5,15,15)),new double[]{0,2,15,2,16,-2,30,15,3,28,30,4,30,2,15,4},new double[0],new double[0]};
        for(int i=0;i<names.length;i++)for(double interval:new double[]{1,2.7,5})for(boolean smooth:new boolean[]{false,true})for(boolean adjust:new boolean[]{false,true})
            sample(names[i],i==5?"ellipse":i==6?"rounded_rectangle":"path",paths[i],interval,smooth,adjust);
        System.out.println("]}");
    }
}
