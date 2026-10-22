package ij.plugin.frame;
import ij.IJ;
import ij.ImagePlus;
import ij.process.ColorProcessor;
import ij.process.ByteProcessor;
import ij.process.ImageProcessor;
import java.lang.reflect.Field;
import java.security.MessageDigest;
import sun.misc.Unsafe;
/** Actual ColorProcessor preview and ContrastAdjuster.applyRGB commit. Only AWT
 * reset controls are replaced by their real processor reset/range operations. */
public class RgbContrastApplyReference extends ContrastAdjuster {
    @Override void reset(ImagePlus imp, ImageProcessor ip) { ip.reset(); ip.setMinAndMax(0,255); }
    private static String digest(ColorProcessor cp) throws Exception {
        int[] pixels=(int[])cp.getPixels();byte[] bytes=new byte[pixels.length*3];
        for(int i=0;i<pixels.length;i++)for(int c=0;c<3;c++)bytes[3*i+c]=(byte)(pixels[i]>>(16-8*c));
        StringBuilder out=new StringBuilder();for(byte b:MessageDigest.getInstance("SHA-256").digest(bytes))out.append(String.format("%02x",b&255));return out.toString();
    }
    public static void main(String[] args) throws Exception {
        if(!IJ.getVersion().equals("1.54r"))throw new Exception("ImageJ version");
        Field f=Unsafe.class.getDeclaredField("theUnsafe");f.setAccessible(true);
        RgbContrastApplyReference driver=(RgbContrastApplyReference)((Unsafe)f.get(null)).allocateInstance(RgbContrastApplyReference.class);
        f=IJ.class.getDeclaredField("macroRunning");f.setAccessible(true);f.setBoolean(null,true);
        double[][] ranges={{10.9,40.1},{10.9,10.9},{-30.25,400.5},{0,255},
            {-2147483648.,2147483647.},{-2147483648.,1.},{-1e100,1e100},
            {0,1e-300},{0,0},{0,-0.0},{-0.0,0},{1e308,1e308},{-1e308,1e308}};
        for(int k=0;k<ranges.length;k++)for(int channels=1;channels<=7;channels++)for(int roi=0;roi<3;roi++){
            int width=256,height=2;int[] input=new int[width*height];
            for(int y=0;y<height;y++)for(int x=0;x<width;x++)input[y*width+x]=0xff000000|(x<<16)|((255-x)<<8)|((73*x+19*y)&255);
            ColorProcessor cp=new ColorProcessor(width,height,input);cp.snapshot();
            ByteProcessor mask=null;
            if(roi!=0){cp.setRoi(17,0,219,height);if(roi==2){mask=new ByteProcessor(219,height);for(int y=0;y<height;y++)for(int x=0;x<219;x++)mask.set(x,y,(x+17+y)%3==0?0:255);}}
            cp.setMinAndMax(ranges[k][0],ranges[k][1],channels);cp.reset(mask);
            String preview=digest(cp);ImagePlus image=new ImagePlus("RGB Apply",cp);driver.applyRGB(image,cp);
            String applied=digest(cp);if(!preview.equals(applied))throw new Exception("Commit changed preview pixels");
            if(cp.getMin()!=0 || cp.getMax()!=255)throw new Exception("Commit reset range");
            cp.setRoi(0,0,width,height);cp.setMinAndMax(ranges[k][0],ranges[k][1],channels);
            System.out.println(k+" "+channels+" "+roi+" "+applied+" "+digest(cp));
        }
    }
}
