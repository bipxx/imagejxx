import ij.IJ;
import ij.gui.*;
import java.awt.Rectangle;

/** Mixed parametric/packed inputs; expectations still come from ImageJ. */
public class SelectionRegionBooleanVectorReference {
    static Roi vector(int kind,int dx,int dy) {
        if(kind<3){Roi r=kind==1?new OvalRoi(4.75+dx,5.25+dy,19.5,16.5):new Roi(4.75+dx,5.25+dy,19.5,16.5);if(kind==2)r.setCornerDiameter(8);return r;}
        float[] xs=kind==3?new float[]{3,27,35,20,7}:new float[]{-4,19,31,12,-3};
        float[] ys=kind==3?new float[]{4,3,17,32,23}:new float[]{-2,1,20,35,17};
        for(int i=0;i<xs.length;i++){xs[i]+=dx;ys[i]+=dy;}
        return new PolygonRoi(xs,ys,xs.length,kind==3?Roi.POLYGON:Roi.FREEROI);
    }
    static String input(int kind,int dx,int dy) {
        String[] names={"rectangle","ellipse","rounded_rectangle","polygon","freehand"};
        double[][] points=kind<3?new double[][]{{4.75+dx,5.25+dy},{24.25+dx,21.75+dy}}:
            kind==3?new double[][]{{3+dx,4+dy},{27+dx,3+dy},{35+dx,17+dy},{20+dx,32+dy},{7+dx,23+dy}}:
            new double[][]{{-4+dx,-2+dy},{19+dx,1+dy},{31+dx,20+dy},{12+dx,35+dy},{-3+dx,17+dy}};
        StringBuilder out=new StringBuilder("{\"kind\":\"").append(names[kind]).append("\",\"width\":1,\"attributes\":");
        out.append(kind==2?"{\"corner_diameter\":8}":"{}").append(",\"points\":[");
        for(int i=0;i<points.length;i++){if(i>0)out.append(',');out.append('[').append(points[i][0]).append(',').append(points[i][1]).append(']');}
        return out.append("]}").toString();
    }
    public static void main(String[] args) {
        if(!IJ.getVersion().equals("1.54r"))throw new AssertionError(IJ.getVersion());
        System.out.print("{\"schema\":\"imagejxx.reference.selection-region-boolean-vectors\",\"version\":1,\"imagej_version\":\"1.54r\",\"cases\":[");int count=0;
        for(int kind=0;kind<5;kind++)for(int variant=0;variant<2;variant++)for(int op=0;op<4;op++) {
            int dx=variant==0?-19:0,dy=variant==0?-13:0,third=(kind+2)%5;
            ShapeRoi a=SelectionRegionBooleanReference.grid(vector(kind,dx,dy),0,0);
            ShapeRoi b=SelectionRegionBooleanReference.grid(SelectionEnlargementReference.source((kind+5)%9),-9,3);
            ShapeRoi c=SelectionRegionBooleanReference.grid(vector(third,-12,-6),0,0);
            Rectangle sample=a.getBounds().union(b.getBounds()).union(c.getBounds());sample.grow(2,2);
            ShapeRoi first=SelectionRegionBooleanReference.combine(a,b,op);int next=(op+kind+1)%4;
            ShapeRoi second=SelectionRegionBooleanReference.combine(first,c,next);
            if(count++>0)System.out.print(',');
            System.out.print("{\"kind\":"+kind+",\"variant\":"+variant+",\"inputs\":["+input(kind,dx,dy)+","+SelectionRegionBooleanReference.input(b)+","+input(third,-12,-6)+
                "],\"operations\":[\""+SelectionRegionBooleanReference.OPS[op]+"\",\""+SelectionRegionBooleanReference.OPS[next]+"\"],\"stages\":["+
                SelectionRegionBooleanReference.stage(first,sample)+","+SelectionRegionBooleanReference.stage(second,sample)+"]}");
        }
        System.out.println("]}");
    }
}
