package ij.plugin.frame;

import ij.*;
import ij.gui.Roi;
import ij.process.*;
import java.awt.Choice;
import java.lang.reflect.Field;
import java.nio.ByteBuffer;
import sun.misc.Unsafe;

/** Real ImagePlus.setPosition and ContrastAdjuster setup/mapping/masking/reset.
 * Only presentation methods are bypassed. In particular, reset is inherited
 * directly from ContrastAdjuster, not the older histogram harness override.
 * No upstream processing source or bytecode is modified. */
public class RgbContrastNavigationReference extends ContrastAdjuster {
    @Override public void setTitle(String title) {}
    @Override void updateScrollBars(java.awt.Scrollbar bar, boolean newRange) {}
    @Override void updatePlot() {}
    @Override void updateLabels(ImagePlus image) {}

    static void map(RgbContrastNavigationReference d, ImagePlus image, double lo, double hi) {
        ColorProcessor cp = (ColorProcessor)d.setup(image);
        d.min=lo; d.max=hi;
        d.setMinAndMax(image,lo,hi);
        d.doMasking(image,cp);
    }
    static String pixels(ImagePlus image) throws Exception {
        return RgbHistogramAutoReference.pixels((ColorProcessor)image.getProcessor());
    }
    static String plane(ImagePlus image, int[] position) throws Exception {
        return RgbHistogramAutoReference.pixels((ColorProcessor)image.getStack().getProcessor(
            image.getStackIndex(position[1]+1,position[2]+1,position[0]+1)));
    }
    static String stack(ImagePlus image) throws Exception {
        ByteBuffer bytes=ByteBuffer.allocate(2*2*2*256*8*3);
        for(int t=0;t<2;t++)for(int c=0;c<2;c++)for(int z=0;z<2;z++) {
            int[] p=(int[])image.getStack().getProcessor(image.getStackIndex(c+1,z+1,t+1)).getPixels();
            for(int v:p) {bytes.put((byte)(v>>16));bytes.put((byte)(v>>8));bytes.put((byte)v);}
        }
        return RgbHistogramAutoReference.hash(bytes.array());
    }
    static void position(ImagePlus image,int[] p) {image.setPosition(p[1]+1,p[2]+1,p[0]+1);}
    static void selection(ImagePlus image,int kind) {
        Roi roi=RgbStackApplyReference.roi(kind);
        if(roi==null)image.deleteRoi();else image.setRoi(roi);
    }
    static void require(boolean condition,String message) throws Exception {
        if(!condition)throw new Exception(message);
    }
    public static void main(String[] args) throws Exception {
        require(IJ.getVersion().equals("1.54r"),"ImageJ version");
        Field f=Unsafe.class.getDeclaredField("theUnsafe");f.setAccessible(true);
        Unsafe unsafe=(Unsafe)f.get(null);
        f=IJ.class.getDeclaredField("macroRunning");f.setAccessible(true);f.setBoolean(null,true);
        for(int axis=0;axis<3;axis++)for(int origin=0;origin<2;origin++)
        for(int channels=1;channels<=7;channels++)for(int roi=0;roi<9;roi++) {
            ImageStack source=new ImageStack(256,8);
            for(int t=0;t<2;t++)for(int z=0;z<2;z++)for(int c=0;c<2;c++) {
                int[] p=new int[256*8];
                for(int y=0;y<8;y++)for(int x=0;x<256;x++)
                    p[y*256+x]=0xff000000|(((x+11*c+23*z+41*t)&255)<<16)
                        |(((73*x+19*y+17*c+31*z+47*t)&255)<<8)|((255-x+53*c+59*z+61*t)&255);
                source.addSlice(new ColorProcessor(256,8,p));
            }
            ImagePlus image=new ImagePlus("RGB navigation",source);image.setDimensions(2,2,2);
            int[] a={origin,origin,origin},b=a.clone();b[axis]=1-origin;
            position(image,a);selection(image,roi);
            RgbContrastNavigationReference d=(RgbContrastNavigationReference)unsafe.allocateInstance(RgbContrastNavigationReference.class);
            d.plot=(RgbHistogramAutoReference.CapturePlot)unsafe.allocateInstance(RgbHistogramAutoReference.CapturePlot.class);
            d.choice=new Choice();for(String label:channelLabels)d.choice.add(label);
            d.balance=d.RGBImage=true;d.channels=channels;d.sliderRange=256;
            String originalA=pixels(image),originalB=plane(image,b);
            d.setup(image);
            Object firstSnapshot=((ColorProcessor)image.getProcessor()).getSnapshotPixels();
            map(d,image,10.9,40.1);String first=pixels(image);

            // Selection changes do not create a new ContrastAdjuster snapshot.
            int changedRoi=(roi+1)%9;selection(image,changedRoi);d.setup(image);
            require(firstSnapshot==((ColorProcessor)image.getProcessor()).getSnapshotPixels(),"Selection recaptured snapshot");
            map(d,image,20.25,200.75);String selected=pixels(image);
            d.reset(image,image.getProcessor());
            require(originalA.equals(pixels(image)),"Reset after selection change lost original baseline");
            map(d,image,20.25,200.75);require(selected.equals(pixels(image)),"Selection remap is not deterministic");

            // A navigation round trip commits A's visible map into the stack.
            position(image,b);d.setup(image);
            require(originalB.equals(pixels(image)),"Visiting B changed its pixels");
            require(selected.equals(plane(image,a)),"Leaving A discarded its visible map");
            require(d.min==0 && d.max==255,"New slice range was not reset");
            require(firstSnapshot!=((ColorProcessor)image.getProcessor()).getSnapshotPixels(),"New slice retained A snapshot");
            map(d,image,-30.25,400.5);String mappedB=pixels(image);
            position(image,a);d.setup(image);
            require(selected.equals(pixels(image)),"Returning to A discarded its map");
            require(mappedB.equals(plane(image,b)),"Leaving B discarded its map");
            require(d.min==0 && d.max==255,"Returning slice range was not reset");
            d.reset(image,image.getProcessor());String resetA=pixels(image);
            require(selected.equals(resetA),"Returning Reset reverted an earlier visit");
            map(d,image,-10.2,160.9);String remappedA=pixels(image),remappedStack=stack(image);
            d.reset(image,image.getProcessor());String resetStack=stack(image);
            require(selected.equals(pixels(image)),"Reset lost returning-visit baseline");
            require(mappedB.equals(plane(image,b)),"Reset changed another plane");
            System.out.println(axis+" "+origin+" "+channels+" "+roi+" "+changedRoi+" "+originalA+" "+originalB+" "+first+" "+selected+" "+mappedB+" "+resetA+" "+remappedA+" "+remappedStack+" "+resetStack);
        }
        System.exit(0);
    }
}
