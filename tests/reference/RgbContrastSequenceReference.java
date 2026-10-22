package ij.plugin.frame;
import ij.IJ;
import ij.ImagePlus;
import ij.gui.Roi;
import ij.process.ColorProcessor;
import java.lang.reflect.Field;
import sun.misc.Unsafe;
/** Uses actual ColorProcessor.snapshot at the exact promotion boundary in
 * ContrastAdjuster.itemStateChanged, followed by its reset and Auto operations.
 * Choice/AWT delivery itself is not exercised. Inherits the pinned presentation
 * bypass used by the raw RGB histogram/Auto reference. */
public class RgbContrastSequenceReference extends RgbHistogramAutoReference {
    static void map(RgbContrastSequenceReference d,ImagePlus image,ColorProcessor cp,double lo,double hi){d.min=lo;d.max=hi;d.setMinAndMax(image,lo,hi);d.doMasking(image,cp);}
    static void promote(RgbContrastSequenceReference d,ImagePlus image,ColorProcessor cp,int channels){cp.snapshot();d.channels=channels;d.reset(image,cp);}
    public static void main(String[] args)throws Exception{
        if(!IJ.getVersion().equals("1.54r"))throw new Exception("ImageJ version");
        Field f=Unsafe.class.getDeclaredField("theUnsafe");f.setAccessible(true);Unsafe unsafe=(Unsafe)f.get(null);
        f=IJ.class.getDeclaredField("macroRunning");f.setAccessible(true);f.setBoolean(null,true);
        double[][] ranges={{10.9,40.1},{-30.25,400.5},{10.9,10.9}};
        for(int k=0;k<ranges.length;k++)for(int a=1;a<=7;a++)for(int b=1;b<=7;b++)if(a!=b)for(int roi=0;roi<3;roi++){
            int[] p=new int[256*8];for(int y=0;y<8;y++)for(int x=0;x<256;x++)p[y*256+x]=0xff000000|(x<<16)|(((73*x+19*y)&255)<<8)|(255-x);
            ColorProcessor cp=new ColorProcessor(256,8,p);cp.setRGBWeights(new double[]{1d/3,1d/3,1d/3});cp.snapshot();ImagePlus image=new ImagePlus("RGB component promotion",cp);
            if(roi==1)image.setRoi(new Roi(17,0,219,8));if(roi==2)image.setRoi(new MaskRoi(false));
            RgbContrastSequenceReference d=(RgbContrastSequenceReference)unsafe.allocateInstance(RgbContrastSequenceReference.class);d.plot=(CapturePlot)unsafe.allocateInstance(CapturePlot.class);
            d.balance=d.RGBImage=true;d.channels=a;d.defaultMin=d.min=0;d.defaultMax=d.max=255;d.sliderRange=256;
            map(d,image,cp,ranges[k][0],ranges[k][1]);String first=pixels(cp);promote(d,image,cp,b);if(!first.equals(pixels(cp)))throw new Exception("Promotion reset changed current pixels");
            map(d,image,cp,20.25,200.75);String initial=pixels(cp);map(d,image,cp,-10.2,160.9);String moved=pixels(cp);
            promote(d,image,cp,a);if(!moved.equals(pixels(cp)))throw new Exception("Second promotion reset changed current pixels");d.autoAdjust(image,cp);String automatic=pixels(cp);
            System.out.println(k+" "+a+" "+b+" "+roi+" "+first+" "+initial+" "+moved+" "+d.min+" "+d.max+" "+d.autoThreshold+" "+automatic);
        }
    }
}
