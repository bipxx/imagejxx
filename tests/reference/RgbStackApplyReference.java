package ij.plugin.frame;
import ij.*;
import ij.gui.*;
import ij.process.*;
import java.awt.Choice;
import java.lang.reflect.*;
import java.nio.*;
import sun.misc.Unsafe;
/** Calls the real private ContrastAdjuster.applyRGBStack, including its real
 * macro-confirmed dialog and setup. Only plot/label/scrollbar presentation is
 * bypassed. No ImageJ processing sources or bytecode are patched. */
public class RgbStackApplyReference extends RgbContrastSequenceReference {
    @Override void updatePlot(){}
    @Override void updateLabels(ImagePlus image){}
    static Roi roi(int k){
        switch(k){
        case 1:return new Roi(17,0,219,8);
        case 2:return new MaskRoi(false);
        case 3:return new MaskRoi(true);
        case 4:return new Line(20.25,1.25,90.75,5.75);
        case 5:return new PointRoi(new float[]{20.25f,90.75f},new float[]{1.25f,5.75f},2);
        case 6:return new PolygonRoi(new float[]{20.25f,90.75f,35.5f},new float[]{1.25f,3.5f,5.75f},3,Roi.POLYLINE);
        case 7:return new PolygonRoi(new float[]{20.25f,90.75f,35.5f},new float[]{1.25f,3.5f,5.75f},3,Roi.ANGLE);
        case 8:return new OvalRoi(17,0,219,8);
        default:return null;
        }
    }
    static String stackPixels(ImagePlus image)throws Exception{
        ByteBuffer bytes=ByteBuffer.allocate(3*3*3*256*8*3);
        // Canonical native T,C,Z order, independent of ImageJ's C,Z,T storage.
        for(int t=0;t<3;t++)for(int c=0;c<3;c++)for(int z=0;z<3;z++){
            int[] p=(int[])image.getStack().getProcessor(image.getStackIndex(c+1,z+1,t+1)).getPixels();
            for(int v:p){bytes.put((byte)(v>>16));bytes.put((byte)(v>>8));bytes.put((byte)v);}
        }
        return hash(bytes.array());
    }
    public static void main(String[] args)throws Exception{
        if(!IJ.getVersion().equals("1.54r"))throw new Exception("ImageJ version");
        Thread.currentThread().setName("Run$_RGBStackReference");Macro.setOptions("confirm");
        Field f=Unsafe.class.getDeclaredField("theUnsafe");f.setAccessible(true);Unsafe unsafe=(Unsafe)f.get(null);
        f=IJ.class.getDeclaredField("macroRunning");f.setAccessible(true);f.setBoolean(null,true);
        Method apply=ContrastAdjuster.class.getDeclaredMethod("applyRGBStack",ImagePlus.class);apply.setAccessible(true);
        double[][] ranges={{10.9,40.1},{-30.25,400.5},{10.9,10.9},{0,255},{-2147483648.,2147483647.},{-1e100,1e100},{0,1e-300}};
        int[][] anchors={{0,0,0},{1,1,1},{2,2,2}};
        for(int k=0;k<ranges.length;k++)for(int channels=1;channels<=7;channels++)for(int roi=0;roi<9;roi++)for(int a=0;a<anchors.length;a++)for(int prefix=0;prefix<2;prefix++)for(int whole=0;whole<2;whole++){
            ImageStack stack=new ImageStack(256,8);
            for(int t=0;t<3;t++)for(int z=0;z<3;z++)for(int c=0;c<3;c++){
                int[] p=new int[256*8];for(int y=0;y<8;y++)for(int x=0;x<256;x++){
                    int r=(x+11*c+23*z+41*t)&255,g=(73*x+19*y+17*c+31*z+47*t)&255,b=(255-x+53*c+59*z+61*t)&255;p[y*256+x]=0xff000000|(r<<16)|(g<<8)|b;
                }stack.addSlice(new ColorProcessor(256,8,p));
            }
            ImagePlus image=new ImagePlus("RGB stack",stack);image.setDimensions(3,3,3);int[] anchor=anchors[a];image.setPosition(anchor[1]+1,anchor[2]+1,anchor[0]+1);
            Roi selection=roi(roi);if(selection!=null)image.setRoi(selection);ColorProcessor cp=(ColorProcessor)image.getProcessor();cp.snapshot();cp.caSnapshot(true);
            RgbStackApplyReference d=(RgbStackApplyReference)unsafe.allocateInstance(RgbStackApplyReference.class);d.plot=(CapturePlot)unsafe.allocateInstance(CapturePlot.class);d.choice=new Choice();for(String label:channelLabels)d.choice.add(label);
            d.balance=d.RGBImage=true;d.defaultMin=d.min=0;d.defaultMax=d.max=255;d.sliderRange=256;
            if(prefix!=0){d.channels=7;d.setup(image);RgbContrastSequenceReference.map(d,image,cp,20.25,200.75);image.getProcessor().snapshot();d.channels=channels;d.reset(image,cp);}
            d.channels=channels;d.setup(image);if(whole!=0)image.deleteRoi();RgbContrastSequenceReference.map(d,image,cp,ranges[k][0],ranges[k][1]);if(whole!=0 && selection!=null)image.setRoi(selection);String active=pixels(cp);double low=cp.getMin(),high=cp.getMax();
            WindowManager.setTempCurrentImage(image);apply.invoke(d,image);WindowManager.setTempCurrentImage(null);
            String after=pixels((ColorProcessor)image.getProcessor());if(!active.equals(after))throw new Exception("Stack commit changed active preview");
            System.out.println(k+" "+channels+" "+roi+" "+a+" "+prefix+" "+whole+" "+low+" "+high+" "+active+" "+stackPixels(image));
        }
        System.exit(0);
    }
}
