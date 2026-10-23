import ij.IJ;
import ij.gui.Roi;
import ij.gui.ShapeRoi;
import ij.plugin.RoiRotator;
import java.awt.Rectangle;
import java.awt.geom.Area;
import java.util.Base64;

/** Actual ImageJ rotation of unions of complete image-grid pixel cells. */
public class SelectionRegionRotationReference {
    static final Rectangle SAMPLE = new Rectangle(-24,-24,64,64);
    static String packed(Roi source) {
        byte[] bytes=new byte[512];
        for(int y=0;y<64;y++)for(int x=0;x<64;x++)if(source.contains(x-24,y-24)) {
            int i=y*64+x;bytes[i/8]|=0x80>>(i%8);
        }
        return Base64.getEncoder().encodeToString(bytes);
    }
    public static void main(String[] args) {
        if(!IJ.getVersion().equals("1.54r"))throw new AssertionError(IJ.getVersion());
        System.out.print("{\"schema\":\"imagejxx.reference.region-rotation\",\"imagej_version\":\"1.54r\",\"sample\":[-24,-24,64,64],\"cases\":[");
        int count=0;
        for(int kind=0;kind<3;kind++)for(double angle:new double[]{0,90,-90,180,270,360,23,-37,45,137})for(int pivot=0;pivot<2;pivot++) {
            Area area=new Area();
            for(int y=0;y<3;y++)for(int x=0;x<9;x++) {
                boolean member=kind==0 ? !(y==1 && x>=3 && x<=5) : kind==1 ? ((x+y)%3==0) : x==0 && y==0 || x==8 && y==2;
                if(member)area.add(new Area(new Rectangle(x-2,y+2,1,1)));
            }
            ShapeRoi source=new ShapeRoi(area);
            double cx=pivot==0?2.5:4,cy=pivot==0?3.5:-2;
            Roi result=RoiRotator.rotate(source,angle,cx,cy);
            if(count++>0)System.out.print(",");
            System.out.print("{\"kind\":"+kind+",\"angle\":"+angle+",\"pivot\":["+cx+","+cy+"],\"mask\":\""+packed(result)+"\"}");
        }
        System.out.println("]}");
    }
}
