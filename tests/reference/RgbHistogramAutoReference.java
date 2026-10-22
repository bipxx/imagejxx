package ij.plugin.frame;
import ij.IJ;
import ij.ImagePlus;
import ij.gui.Roi;
import ij.gui.Line;
import ij.process.*;
import java.lang.reflect.Field;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.security.MessageDigest;
import sun.misc.Unsafe;
/** Actual ContrastAdjuster plotHistogram/autoAdjust. Only plot presentation and
 * reset widgets are replaced; processor statistics, ROI handling and RGB LUT
 * mapping remain actual ImageJ methods. No upstream implementation is patched. */
public class RgbHistogramAutoReference extends ContrastAdjuster {
    static class CapturePlot extends ContrastPlot {
        ImageStatistics captured;
        @Override void setHistogram(ImageStatistics s,boolean log){captured=s;}
    }
    static class MaskRoi extends Roi {
        final ByteProcessor mask;
        MaskRoi(boolean empty){super(17,0,219,8);mask=new ByteProcessor(219,8);if(!empty)for(int y=0;y<8;y++)for(int x=0;x<219;x++)mask.set(x,y,(x+17+y)%3==0?0:255);}
        @Override public ImageProcessor getMask(){return mask;}
    }
    @Override public void setTitle(String title){}
    @Override void updateScrollBars(java.awt.Scrollbar bar,boolean newRange){}
    @Override void reset(ImagePlus imp,ImageProcessor ip){ip.reset();min=defaultMin=0;max=defaultMax=255;setMinAndMax(imp,min,max);autoThreshold=0;}
    static String hash(byte[] bytes)throws Exception{StringBuilder s=new StringBuilder();for(byte b:MessageDigest.getInstance("SHA-256").digest(bytes))s.append(String.format("%02x",b&255));return s.toString();}
    static String histogram(int[] h)throws Exception{ByteBuffer b=ByteBuffer.allocate(256*8).order(ByteOrder.LITTLE_ENDIAN);for(int v:h)b.putLong(v);return hash(b.array());}
    static String pixels(ColorProcessor cp)throws Exception{int[] p=(int[])cp.getPixels();byte[] bytes=new byte[p.length*3];for(int i=0;i<p.length;i++)for(int c=0;c<3;c++)bytes[i*3+c]=(byte)(p[i]>>(16-8*c));return hash(bytes);}
    public static void main(String[] args)throws Exception{
        if(!IJ.getVersion().equals("1.54r"))throw new Exception("ImageJ version");
        Field f=Unsafe.class.getDeclaredField("theUnsafe");f.setAccessible(true);Unsafe unsafe=(Unsafe)f.get(null);
        f=IJ.class.getDeclaredField("macroRunning");f.setAccessible(true);f.setBoolean(null,true);
        double[][] weights={{1d/3,1d/3,1d/3},{.299,.587,.114},{.125,.25,.5},{1,0,0}};
        int[] previous={0,5000,2500,1250,625,312,156,78,39,19,9};
        for(int scenario=0;scenario<4;scenario++)for(int w=0;w<weights.length;w++)for(int roi=0;roi<5;roi++)for(int channels=1;channels<=7;channels++)for(int prev:previous){
            int[] p=new int[256*8];
            for(int y=0;y<8;y++)for(int x=0;x<256;x++){
                int r=x,g=(73*x+19*y)&255,b=255-x;
                if(scenario==1){r=20;g=40;b=80;}
                if(scenario>=2){r=g=b=0;if(y==0&&x<(scenario==2?32:16)){r=scenario==2?100+x:160;g=scenario==2?110+x:160;b=scenario==2?120+x:160;}}
                p[y*256+x]=0xff000000|(r<<16)|(g<<8)|b;
            }
            ColorProcessor cp=new ColorProcessor(256,8,p);cp.setRGBWeights(weights[w]);cp.snapshot();ImagePlus image=new ImagePlus("RGB histogram Auto",cp);
            if(roi==1)image.setRoi(new Roi(17,0,219,8));if(roi==2)image.setRoi(new MaskRoi(false));if(roi==3)image.setRoi(new MaskRoi(true));if(roi==4)image.setRoi(new Line(0,0,255,7));
            RgbHistogramAutoReference driver=(RgbHistogramAutoReference)unsafe.allocateInstance(RgbHistogramAutoReference.class);
            driver.plot=(CapturePlot)unsafe.allocateInstance(CapturePlot.class);driver.balance=true;driver.RGBImage=true;driver.channels=channels;driver.autoThreshold=prev;driver.defaultMin=driver.min=0;driver.defaultMax=driver.max=255;driver.sliderRange=256;
            driver.plotHistogram(image);String plotBefore=histogram(((CapturePlot)driver.plot).captured.histogram);
            String autoHistogram=histogram(image.getRawStatistics().histogram);
            driver.autoAdjust(image,cp);String applied=pixels(cp);driver.plotHistogram(image);
            System.out.println(scenario+" "+w+" "+roi+" "+channels+" "+prev+" "+driver.autoThreshold+" "+driver.min+" "+driver.max+" "+plotBefore+" "+autoHistogram+" "+applied+" "+histogram(((CapturePlot)driver.plot).captured.histogram));
        }
    }
}
