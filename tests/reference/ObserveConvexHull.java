import ij.process.FloatPolygon;
import java.util.Random;

/** Original observation driver; the hull itself is the pinned ImageJ method. */
public class ObserveConvexHull {
    static boolean comma=false;
    static void sample(String name,double[][] input) {
        if(comma)System.out.print(",");comma=true;
        System.out.print("{\"id\":\""+name+"\",\"input\":[");
        float[] x=new float[input.length],y=new float[input.length];
        for(int i=0;i<input.length;i++) {
            if(i>0)System.out.print(",");
            System.out.print("["+input[i][0]+","+input[i][1]+"]");
            x[i]=(float)input[i][0];y[i]=(float)input[i][1];
        }
        System.out.print("]");
        try {
            FloatPolygon p=new FloatPolygon(x,y,x.length).getConvexHull();
            System.out.print(",\"hull\":[");
            for(int i=0;i<p.npoints;i++) {
                if(i>0)System.out.print(",");
                System.out.print("["+(double)p.xpoints[i]+","+(double)p.ypoints[i]+"]");
            }
            System.out.print("]");
        } catch(RuntimeException e) {
            System.out.print(",\"error\":\""+e.getClass().getSimpleName()+"\"");
        }
        System.out.print("}");
    }
    public static void main(String[] args) {
        System.out.print("{\"reference\":\"ImageJ 1.54r FloatPolygon.getConvexHull\",\"cases\":[");
        sample("square",new double[][]{{1,1},{9,1},{9,9},{1,9}});
        sample("concave",new double[][]{{1,1},{9,1},{4,4},{9,9},{1,9}});
        sample("duplicates",new double[][]{{9,9},{1,1},{1,9},{9,1},{1,1},{9,9}});
        sample("collinear-edges",new double[][]{{1,1},{4,1},{9,1},{9,4},{9,9},{4,9},{1,9},{1,4}});
        sample("subpixel",new double[][]{{.1,.2},{8.9,.3},{8.7,9.2},{.3,8.9},{4,4}});
        sample("negative",new double[][]{{-3,-2},{4,-2},{7,5},{-3,5},{0,0}});
        sample("float-rounding",new double[][]{{16777216,0},{16777217,0},{16777220,0},{16777220,4},{16777216,4}});
        sample("collinear",new double[][]{{1,1},{2,2},{3,3},{2,2}});
        sample("identical",new double[][]{{2,2},{2,2},{2,2}});
        sample("two",new double[][]{{1,1},{4,5}});
        sample("one",new double[][]{{1,1}});
        sample("empty",new double[][]{});
        Random random=new Random(7319);
        for(int k=0;k<32;k++) {
            double[][] input=new double[4+k][2];
            for(int i=0;i<input.length;i++)for(int axis=0;axis<2;axis++)
                input[i][axis]=random.nextInt(48)+random.nextInt(4)*.25;
            sample("seeded-"+k,input);
        }
        System.out.println("]}");
    }
}
