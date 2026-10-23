import ij.IJ;
import ij.gui.Roi;
import ij.plugin.RoiEnlarger;
import java.awt.Rectangle;
import java.util.Base64;

/** Full off-image membership from actual ImageJ methods, including repetitions. */
public class SelectionRegionReference {
    static final double[][] SEQUENCES={{2,2},{13,-13},{-2,4},{.5,.5},
        {0,1},{-100,2},{255.5,-255.5},{256,-256}};
    public static void main(String[] args) {
        if(!IJ.getVersion().equals("1.54r"))throw new AssertionError(IJ.getVersion());
        System.out.print("{\"schema\":\"imagejxx.reference.selection-region\",\"version\":1,\"imagej_version\":\"1.54r\",\"cases\":[");
        int count=0;
        for(int kind=0;kind<9;kind++)for(int sequence=0;sequence<SEQUENCES.length;sequence++) {
            Roi roi=SelectionEnlargementReference.source(kind);
            for(int step=0;step<SEQUENCES[sequence].length;step++) {
                double distance=SEQUENCES[sequence][step];
                roi=Math.abs(distance)<256?RoiEnlarger.enlarge255(roi,distance):RoiEnlarger.enlarge(roi,distance);
                Rectangle bounds=roi.getBounds();
                Rectangle sample=bounds.union(new Rectangle(0,0,48,40));sample.grow(2,2);
                byte[] packed=new byte[(sample.width*sample.height+7)/8];
                int occupied=0;
                for(int y=0;y<sample.height;y++)for(int x=0;x<sample.width;x++) {
                    int i=y*sample.width+x;
                    if(roi.contains(x+sample.x,y+sample.y)) {packed[i/8]|=1<<(7-i%8);occupied++;}
                }
                java.awt.geom.Rectangle2D.Double f=roi.getFloatBounds();
                if(count++>0)System.out.print(',');
                System.out.print("{\"kind\":"+kind+",\"sequence\":"+sequence+",\"step\":"+step+",\"distance\":"+distance+
                    ",\"bounds\":["+bounds.x+","+bounds.y+","+bounds.width+","+bounds.height+"]"+
                    ",\"float_bounds\":["+f.x+","+f.y+","+f.width+","+f.height+"]"+
                    ",\"sample\":["+sample.x+","+sample.y+","+sample.width+","+sample.height+"]"+
                    ",\"occupied\":"+occupied+",\"mask\":\""+Base64.getEncoder().encodeToString(packed)+"\"}");
            }
        }
        System.out.println("]}");
    }
}
