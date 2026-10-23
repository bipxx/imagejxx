package ij.plugin.frame;

import ij.*;
import ij.process.*;
import java.awt.Choice;
import java.awt.event.ItemEvent;
import java.lang.reflect.*;
import sun.misc.Unsafe;

/** Actual component callback, inherited Reset/Auto, navigation and private stack
 * Apply. Only presentation is bypassed; no upstream processing is patched and
 * the AWT event queue itself is not exercised. */
public class RgbContrastCombinedReference extends RgbContrastNavigationReference {
    static void promote(RgbContrastCombinedReference d,ImagePlus image,int channels) throws Exception {
        int index=-1;
        for(int i=0;i<channelConstants.length;i++)if(channelConstants[i]==channels)index=i;
        require(index>=0 && channels!=d.channels,"Expected a changed component");
        String before=pixels(image);
        d.choice.select(index);
        WindowManager.setTempCurrentImage(image);
        d.itemStateChanged(new ItemEvent(d.choice,ItemEvent.ITEM_STATE_CHANGED,channelLabels[index],ItemEvent.SELECTED));
        require(d.channels==channels && d.doReset,"Component callback did not request Reset");
        d.doReset=false;
        d.reset(image,image.getProcessor());
        require(before.equals(pixels(image)),"Component promotion changed visible pixels");
    }
    public static void main(String[] args) throws Exception {
        Thread.setDefaultUncaughtExceptionHandler((thread,error)->{error.printStackTrace();System.exit(1);});
        require(IJ.getVersion().equals("1.54r"),"ImageJ version");
        Thread.currentThread().setName("Run$_RGBCombinedReference");Macro.setOptions("confirm");
        Field field=Unsafe.class.getDeclaredField("theUnsafe");field.setAccessible(true);
        Unsafe unsafe=(Unsafe)field.get(null);
        field=IJ.class.getDeclaredField("macroRunning");field.setAccessible(true);field.setBoolean(null,true);
        Method apply=ContrastAdjuster.class.getDeclaredMethod("applyRGBStack",ImagePlus.class);apply.setAccessible(true);
        ColorProcessor.setWeightingFactors(1d/3,1d/3,1d/3);
        for(int axis=0;axis<3;axis++)for(int origin=0;origin<2;origin++)
        for(int first=1;first<=7;first++)for(int second=1;second<=7;second++)if(first!=second)
        for(int roi=0;roi<9;roi++)for(int automatic=0;automatic<2;automatic++) {
            ImageStack source=new ImageStack(256,8);
            for(int t=0;t<2;t++)for(int z=0;z<2;z++)for(int c=0;c<2;c++) {
                int[] p=new int[256*8];
                for(int y=0;y<8;y++)for(int x=0;x<256;x++)p[y*256+x]=0xff000000
                    |(((x+11*c+23*z+41*t)&255)<<16)|(((73*x+19*y+17*c+31*z+47*t)&255)<<8)
                    |((255-x+53*c+59*z+61*t)&255);
                source.addSlice(new ColorProcessor(256,8,p));
            }
            ImagePlus image=new ImagePlus("RGB combined transitions",source);image.setDimensions(2,2,2);
            int[] a={origin,origin,origin},b=a.clone();b[axis]=1-origin;position(image,a);selection(image,roi);
            RgbContrastCombinedReference d=(RgbContrastCombinedReference)unsafe.allocateInstance(RgbContrastCombinedReference.class);
            d.plot=(RgbHistogramAutoReference.CapturePlot)unsafe.allocateInstance(RgbHistogramAutoReference.CapturePlot.class);
            d.choice=new Choice();for(String label:channelLabels)d.choice.add(label);
            d.balance=d.RGBImage=true;d.channels=first;d.sliderRange=256;
            for(int i=0;i<channelConstants.length;i++)if(channelConstants[i]==first)d.choice.select(i);
            WindowManager.setTempCurrentImage(image);d.setup(image);
            map(d,image,10.9,40.1);String firstA=pixels(image);
            int current=second%7+1;
            promote(d,image,second);
            int changed=(roi+1)%9,other=(roi+2)%9;
            selection(image,changed);d.setup(image);map(d,image,20.25,200.75);String selectedA=pixels(image);
            promote(d,image,current);
            position(image,b);d.setup(image);selection(image,other);d.setup(image);
            require(d.min==0 && d.max==255,"New visit range");
            map(d,image,-30.25,400.5);String mappedB=pixels(image);
            position(image,a);d.setup(image);
            require(selectedA.equals(pixels(image)),"Returning lost promotions");
            map(d,image,5.25,90.75);String draftA=pixels(image);
            d.reset(image,image.getProcessor());String resetA=pixels(image);
            require(selectedA.equals(resetA),"Returning Reset lost promoted baseline");
            require(mappedB.equals(plane(image,b)),"Returning Reset changed B");
            if(automatic!=0)d.autoAdjust(image,image.getProcessor());else map(d,image,-10.2,160.9);
            String finalA=pixels(image),preview=stack(image);
            double minimum=d.min,maximum=d.max,processorMinimum=image.getDisplayRangeMin(),processorMaximum=image.getDisplayRangeMax();
            int cycle=d.autoThreshold;
            apply.invoke(d,image);
            require(finalA.equals(pixels(image)),"Final stack Apply changed active preview");
            String applied=stack(image);WindowManager.setTempCurrentImage(null);
            System.out.println(axis+" "+origin+" "+first+" "+second+" "+current+" "+roi+" "+changed+" "+other+" "+automatic
                +" "+minimum+" "+maximum+" "+processorMinimum+" "+processorMaximum+" "+cycle
                +" "+firstA+" "+selectedA+" "+mappedB+" "+draftA+" "+resetA+" "+finalA+" "+preview+" "+applied);
        }
        System.exit(0);
    }
}
