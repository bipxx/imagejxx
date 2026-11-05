import ij.gui.*;
import java.awt.Rectangle;

/** Original driver observing constructors/getBounds used by Selection.tobox. */
public class ObserveBoundingRectangle {
    static boolean comma;
    static void sample(String id, String kind, double[][] points) {
        float[] x=new float[points.length],y=new float[points.length];
        for(int i=0;i<points.length;i++){x[i]=(float)points[i][0];y[i]=(float)points[i][1];}
        Roi roi;
        if(kind.equals("rectangle") || kind.equals("ellipse")) {
            double a=Math.min(points[0][0],points[1][0]),b=Math.min(points[0][1],points[1][1]);
            double w=Math.abs(points[1][0]-points[0][0]),h=Math.abs(points[1][1]-points[0][1]);
            roi=kind.equals("ellipse")?new OvalRoi(a,b,w,h):new Roi(a,b,w,h);
        } else if(kind.equals("line")) roi=new Line(points[0][0],points[0][1],points[1][0],points[1][1]);
        else if(kind.equals("point") || kind.equals("multipoint")) roi=new PointRoi(x,y,x.length);
        else roi=new PolygonRoi(x,y,x.length,kind.equals("polyline")?Roi.POLYLINE:kind.equals("angle")?Roi.ANGLE:kind.equals("freehand")?Roi.FREEROI:Roi.POLYGON);
        Rectangle bounds=roi.getBounds();
        Rectangle result=new Roi(bounds).getBounds();
        if(comma)System.out.print(",");comma=true;
        System.out.print("{\"id\":\""+id+"-"+kind+"\",\"kind\":\""+kind+"\",\"points\":[");
        for(int i=0;i<points.length;i++) {if(i>0)System.out.print(",");System.out.print("["+points[i][0]+","+points[i][1]+"]");}
        System.out.print("],\"bounds\":["+result.x+","+result.y+","+(result.x+result.width)+","+(result.y+result.height)+"]}");
    }
    public static void main(String[] args) {
        System.out.print("{\"reference\":\"ImageJ 1.54r ROI getBounds and new Roi(Rectangle)\",\"cases\":[");
        for(String kind:new String[]{"multipoint","polygon","freehand","polyline","angle"}) {
            sample("integer",kind,new double[][]{{2,3},{12,3},{5,13}});
            sample("subpixel",kind,new double[][]{{2.2,3.7},{12.8,3.1},{5.5,13.2}});
            sample("negative",kind,new double[][]{{-7.2,-5.3},{-2.1,-5.1},{-4.4,-1.7}});
            sample("crossing",kind,new double[][]{{-2.6,-1.5},{2.3,-1.5},{0.1,5.2}});
            sample("collapsed",kind,new double[][]{{2.5,3.5},{2.5,3.5},{2.5,3.5}});
            sample("float-rounding",kind,new double[][]{{16777216,1},{16777217,1},{16777220,4}});
        }
        for(String kind:new String[]{"rectangle","ellipse","line"}) {
            sample("integer",kind,new double[][]{{2,3},{12,13}});
            sample("subpixel",kind,new double[][]{{2.2,3.7},{12.8,13.2}});
            sample("negative",kind,new double[][]{{-7.2,-5.3},{-2.1,-1.7}});
            sample("reversed",kind,new double[][]{{12.8,13.2},{2.2,3.7}});
        }
        sample("single","point",new double[][]{{2.5,-1.5}});
        sample("horizontal","line",new double[][]{{2,3},{12,3}});
        sample("vertical","line",new double[][]{{2,3},{2,13}});
        System.out.println("]}");
    }
}
