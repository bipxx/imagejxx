package ij.plugin.frame;

import ij.IJ;
import ij.ImagePlus;
import ij.process.ByteProcessor;
import ij.process.ShortProcessor;
import ij.process.ImageProcessor;
import java.lang.reflect.Field;
import java.security.MessageDigest;
import sun.misc.Unsafe;

/** Calls the actual ImageJ Apply method, bypassing AWT construction and only
 * overriding post-Apply reset (whose controls are not under test here).
 * The source pixels cover every unsigned value for each processor type.
 */
public class ContrastApplyReference extends ContrastAdjuster {
    @Override void reset(ImagePlus imp, ImageProcessor ip) { }
    public static void main(String[] args) throws Exception {
        if (!IJ.getVersion().equals("1.54r")) throw new Exception("Expected ImageJ 1.54r");
        Field field=Unsafe.class.getDeclaredField("theUnsafe");field.setAccessible(true);
        Unsafe unsafe=(Unsafe)field.get(null);
        ContrastApplyReference driver=(ContrastApplyReference)unsafe.allocateInstance(ContrastApplyReference.class);
        Field running=IJ.class.getDeclaredField("macroRunning");running.setAccessible(true);running.setBoolean(null,true);
        double[][] ranges={{10.9,40.9},{10,10},{-30.25,400.5},{0,255},{0,65535},
                           {-2147483648.,2147483647.},{-2147483648.,1.},{-1e100,1e100}};
        for(int depth:new int[]{8,16}) for(int bits:new int[]{0,8,10,12,14,15,16}) {
            if(depth==8 && bits!=0)continue;
            ImagePlus.setDefault16bitRange(bits);
            for(int k=0;k<ranges.length;k++) {
                final double low=ranges[k][0],high=ranges[k][1];
                final int size=depth==8?256:65536;
                ImageProcessor ip;
                if(depth==8){byte[] p=new byte[size];for(int i=0;i<size;i++)p[i]=(byte)i;ip=new ByteProcessor(size,1,p,null);}
                else {short[] p=new short[size];for(int i=0;i<size;i++)p[i]=(short)i;ip=new ShortProcessor(size,1,p,null);}
                ImagePlus image=new ImagePlus("Apply oracle",ip){
                    @Override public double getDisplayRangeMin(){return low;}
                    @Override public double getDisplayRangeMax(){return high;}
                };
                driver.apply(image,ip);
                byte[] bytes=new byte[size*(depth/8)];
                for(int i=0;i<size;i++){int v=ip.get(i);bytes[i*(depth/8)]=(byte)v;if(depth==16)bytes[i*2+1]=(byte)(v>>>8);}
                byte[] digest=MessageDigest.getInstance("SHA-256").digest(bytes);StringBuilder hex=new StringBuilder();
                for(byte v:digest)hex.append(String.format("%02x",v&255));
                System.out.println(depth+" "+bits+" "+k+" "+hex);
                if(!image.changes)throw new Exception("Apply did not complete");
            }
        }
    }
}
