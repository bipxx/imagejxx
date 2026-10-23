import ij.IJ;
import ij.gui.*;
import ij.plugin.RoiEnlarger;
import ij.plugin.filter.ThresholdToSelection;
import ij.process.*;
import java.awt.Rectangle;

/** Observes actual ImageJ methods; it implements no enlargement algorithm. */
public class SelectionEnlargementReference {
    static final int W=48,H=40;
    static final double[] distances={0,.49,.5,-.49,-.5,-.51,1,-1,2,-2,13,-13,255.49,255.5,256,-256};
    static String quote(String s){return "\""+s+"\"";}
    static String rows(Roi roi){
        StringBuilder b=new StringBuilder("[");
        for(int y=0;y<H;y++){
            if(y>0)b.append(',');b.append('"');
            for(int x=0;x<W;x++)b.append(roi!=null && roi.contains(x,y)?'1':'0');
            b.append('"');
        }
        return b.append(']').toString();
    }
    static Roi mask(int kind){
        ByteProcessor p=new ByteProcessor(W,H);
        for(int y=0;y<H;y++)for(int x=0;x<W;x++){
            boolean v=kind==0 ? (x>=5 && x<30 && y>=4 && y<29 && !(x>=10 && x<25 && y>=9 && y<24)) :
                kind==1 ? ((x==8 && y==7)||(x==30 && y==29)) :
                kind==2 ? ((x>=2 && x<8 && y>=2 && y<10)||(x>=22 && x<29 && y>=18 && y<25)) :
                ((x*17+y*31+x*y*7)%53<11 && x>2 && x<45 && y>2 && y<37);
            if(v)p.set(x,y,255);
        }
        p.setThreshold(255,255,ImageProcessor.NO_LUT_UPDATE);
        return new ThresholdToSelection().convert(p);
    }
    static Roi source(int kind){
        if(kind==0)return new Roi(4.75,5.25,19.5,16.5);
        if(kind==1)return new OvalRoi(4.75,5.25,19.5,16.5);
        if(kind==2){Roi r=new Roi(4,5,20,17);r.setCornerDiameter(8);return r;}
        if(kind==3)return new PolygonRoi(new float[]{3,27,35,20,7},new float[]{4,3,17,32,23},5,Roi.POLYGON);
        if(kind==4)return new PolygonRoi(new float[]{-4,19,31,12,-3},new float[]{-2,1,20,35,17},5,Roi.FREEROI);
        return mask(kind-5);
    }
    public static void main(String[] args){
        if(!IJ.getVersion().equals("1.54r"))throw new AssertionError("Unexpected ImageJ version "+IJ.getVersion());
        System.out.print("{\"schema\":\"imagejxx.reference.selection-enlargement\",\"version\":1,\"imagej_version\":\"1.54r\",\"width\":"+W+",\"height\":"+H+",\"cases\":[");
        int count=0;
        for(int kind=0;kind<9;kind++)for(double d:distances){
            Roi before=source(kind);String original=rows(before);
            Roi after=Math.abs(d)<256?RoiEnlarger.enlarge255(before,d):RoiEnlarger.enlarge(before,d);
            if(count++>0)System.out.print(',');
            Rectangle b=after.getBounds();
            java.awt.geom.Rectangle2D.Double f=after.getFloatBounds();
            System.out.print("{\"kind\":"+kind+",\"distance\":"+d+",\"before\":"+original+",\"after\":"+rows(after)+",\"bounds\":["+b.x+","+b.y+","+b.width+","+b.height+"],\"float_bounds\":["+f.x+","+f.y+","+f.width+","+f.height+"],\"roi_type\":"+after.getType()+"}");
        }
        System.out.println("]}");
    }
}
