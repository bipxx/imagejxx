import ij.IJ;
import ij.gui.Roi;
import ij.gui.ShapeRoi;
import ij.plugin.RoiEnlarger;
import ij.plugin.filter.ThresholdToSelection;
import ij.process.ByteProcessor;
import ij.process.ImageProcessor;
import java.awt.Rectangle;
import java.awt.geom.Area;
import java.util.Base64;

/** Observes ImageJ's ShapeRoi algebra over complete pixel-cell regions. */
public class SelectionRegionBooleanReference {
    static final String[] OPS={"union","intersection","subtraction","xor"};
    static ShapeRoi grid(Roi source,int dx,int dy) {
        Rectangle b=source.getBounds();
        ByteProcessor pixels=new ByteProcessor(Math.max(1,b.width),Math.max(1,b.height));
        for(int y=0;y<b.height;y++)for(int x=0;x<b.width;x++)
            if(source.contains(b.x+x,b.y+y))pixels.set(x,y,255);
        pixels.setThreshold(255,255,ImageProcessor.NO_LUT_UPDATE);
        Roi result=new ThresholdToSelection().convert(pixels);
        if(result==null)return new ShapeRoi(new Area());
        Rectangle local=result.getBounds();result.setLocation(local.x+b.x+dx,local.y+b.y+dy);
        return new ShapeRoi(result);
    }
    static String packed(Roi source,Rectangle sample) {
        byte[] data=new byte[(sample.width*sample.height+7)/8];
        for(int y=0;y<sample.height;y++)for(int x=0;x<sample.width;x++)
            if(source.contains(sample.x+x,sample.y+y)){int bit=y*sample.width+x;data[bit/8]|=1<<(7-bit%8);}
        return Base64.getEncoder().encodeToString(data);
    }
    static String input(Roi roi) {
        Rectangle b=roi.getBounds();Rectangle frame=new Rectangle(b.x,b.y,Math.max(1,b.width),Math.max(1,b.height));
        return "{\"kind\":\"region\",\"points\":[],\"width\":1,\"attributes\":{},\"origin\":["+b.x+","+b.y+
            "],\"shape\":["+frame.height+","+frame.width+"],\"mask\":\""+packed(roi,frame)+"\"}";
    }
    static ShapeRoi combine(ShapeRoi left,ShapeRoi right,int operation) {
        ShapeRoi result=new ShapeRoi(left);
        if(operation==0)return result.or(right);
        if(operation==1)return result.and(right);
        if(operation==2)return result.not(right);
        return result.xor(right);
    }
    static String stage(Roi roi,Rectangle sample) {
        Rectangle b=roi.getBounds();int occupied=0;
        for(int y=0;y<sample.height;y++)for(int x=0;x<sample.width;x++)if(roi.contains(sample.x+x,sample.y+y))occupied++;
        return "{\"bounds\":["+b.x+","+b.y+","+b.width+","+b.height+"],\"sample\":["+sample.x+","+sample.y+","+sample.width+","+sample.height+
            "],\"occupied\":"+occupied+",\"mask\":\""+packed(roi,sample)+"\"}";
    }
    public static void main(String[] args) {
        if(!IJ.getVersion().equals("1.54r"))throw new AssertionError(IJ.getVersion());
        System.out.print("{\"schema\":\"imagejxx.reference.selection-region-boolean\",\"version\":1,\"imagej_version\":\"1.54r\",\"cases\":[");
        int count=0;
        for(int kind=0;kind<9;kind++)for(int variant=0;variant<4;variant++)for(int op=0;op<4;op++) {
            ShapeRoi a=grid(RoiEnlarger.enlarge255(SelectionEnlargementReference.source(kind),2),-16-variant*3,-12-variant*2);
            ShapeRoi b=grid(SelectionEnlargementReference.source((kind+variant+1)%9),variant*7-12,variant*3-9);
            ShapeRoi c=grid(SelectionEnlargementReference.source((kind+4)%9),-20,8);
            if(kind==0 && variant==3)a=new ShapeRoi(new Area());
            if(kind==1 && variant==3)b=new ShapeRoi(a);
            Rectangle sample=a.getBounds().union(b.getBounds()).union(c.getBounds());sample.grow(2,2);
            ShapeRoi first=combine(a,b,op);int secondOp=(op+variant+1)%4;ShapeRoi second=combine(first,c,secondOp);
            if(count++>0)System.out.print(',');
            System.out.print("{\"kind\":"+kind+",\"variant\":"+variant+",\"inputs\":["+input(a)+","+input(b)+","+input(c)+
                "],\"operations\":[\""+OPS[op]+"\",\""+OPS[secondOp]+"\"],\"stages\":["+stage(first,sample)+","+stage(second,sample)+"]}");
        }
        System.out.println("]}");
    }
}
