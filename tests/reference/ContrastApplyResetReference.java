package ij.plugin.frame;
import ij.IJ;
import ij.ImagePlus;
import ij.gui.Roi;
import ij.process.*;
import java.awt.image.IndexColorModel;
import java.lang.reflect.Field;
import sun.misc.Unsafe;
/** Actual scalar Apply plus ImagePlus/processor reset; AWT controls excluded. */
public class ContrastApplyResetReference extends ContrastAdjuster {
    @Override void reset(ImagePlus imp, ImageProcessor ip) {
        if (imp.getBitDepth()==16) imp.resetDisplayRange();
        else imp.setDisplayRange(0,255);
    }
    public static void main(String[] args) throws Exception {
        if(!IJ.getVersion().equals("1.54r"))throw new Exception("ImageJ version");
        Field f=Unsafe.class.getDeclaredField("theUnsafe");f.setAccessible(true);
        ContrastApplyResetReference driver=(ContrastApplyResetReference)((Unsafe)f.get(null)).allocateInstance(ContrastApplyResetReference.class);
        f=IJ.class.getDeclaredField("macroRunning");f.setAccessible(true);f.setBoolean(null,true);
        byte[] r=new byte[256],g=new byte[256],b=new byte[256];
        for(int i=0;i<256;i++){r[i]=(byte)i;g[i]=(byte)(255-i);b[i]=(byte)((3*i)%256);}
        IndexColorModel palette=new IndexColorModel(8,256,r,g,b);
        for(int depth:new int[]{8,16})for(int bits:new int[]{0,12,16})for(int scenario=0;scenario<3;scenario++){
            if(depth==8 && bits!=0)continue;ImagePlus.setDefault16bitRange(bits);
            ImageProcessor ip=depth==8?new ByteProcessor(4,1):new ShortProcessor(4,1);
            for(int i=0;i<4;i++)ip.set(i,scenario==2?20:10*(i+1));ip.setColorModel(palette);
            ImagePlus imp=new ImagePlus("Apply reset",ip);imp.setDisplayRange(10,40);
            if(scenario==1)imp.setRoi(new Roi(1,0,2,1));
            ip.setThreshold(15,35,ImageProcessor.NO_LUT_UPDATE);
            if(!ip.isThreshold())throw new Exception("No input threshold");driver.apply(imp,ip);
            boolean same=true;IndexColorModel actual=(IndexColorModel)ip.getColorModel();
            for(int i=0;i<256;i++)if(actual.getRGB(i)!=palette.getRGB(i))same=false;
            System.out.println(depth+" "+bits+" "+scenario+" "+imp.getDisplayRangeMin()+" "+imp.getDisplayRangeMax()+" "+ip.isThreshold()+" "+same+" "+ip.get(0)+","+ip.get(1)+","+ip.get(2)+","+ip.get(3));
        }
    }
}
