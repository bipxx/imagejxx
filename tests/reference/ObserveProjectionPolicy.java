// Project-owned observer of legacy channel dispatch and supplied density tables.
import ij.ImagePlus;
import ij.ImageStack;
import ij.measure.Calibration;
import ij.plugin.GroupedZProjector;
import ij.plugin.ZProjector;
import ij.process.ByteProcessor;
import ij.process.ImageProcessor;
import ij.process.ShortProcessor;
import java.util.*;

public class ObserveProjectionPolicy extends ObserveScientificZProjection {
    static ImagePlus density(String id,String kind,String profile) {
        int[] codes={0,1,127,255,32768,65535};
        int mask=kind.equals("u8")?255:65535;
        ImageStack stack=new ImageStack(4,1);
        for(int z=0;z<6;++z) {
            ImageProcessor ip=kind.equals("u8")?new ByteProcessor(4,1):new ShortProcessor(4,1);
            for(int p=0;p<4;++p) ip.set(p,(codes[(z+p)%6]+17*p)&mask);
            stack.addSlice("z"+z,ip);
        }
        ImagePlus image=new ImagePlus(id,stack);
        Calibration cal=image.getCalibration();
        cal.pixelWidth=.5;cal.pixelHeight=1.25;cal.pixelDepth=3;
        cal.xOrigin=2;cal.yOrigin=-4;cal.zOrigin=1.5;
        if(profile.equals("linear")) cal.setFunction(Calibration.STRAIGHT_LINE,new double[]{-30,2},"units");
        else if(profile.equals("poly2")) cal.setFunction(Calibration.POLY2,new double[]{-20,.25,.00001},"units");
        else if(profile.equals("zero_clip")) cal.setFunction(Calibration.STRAIGHT_LINE,new double[]{-100,1},"units",true);
        else if(profile.equals("od")) cal.setFunction(Calibration.UNCALIBRATED_OD,null,"OD");
        else {
            float[] table=new float[mask+1];
            int[] ieee={0xbfc00000,0x3fc00000,0x80000000,0,0x7fa12345,0x7fc12345,0x7f800000,0xff800000};
            for(int i=0;i<table.length;++i) {
                if(profile.equals("custom_signed")) table[i]=i-(kind.equals("u8")?128:32768);
                else if(profile.equals("custom_range")) table[i]=((i%9)-4)*123.75f;
                else table[i]=Float.intBitsToFloat(ieee[i%ieee.length]);
            }
            cal.setCTable(table,"units");
        }
        return image;
    }
    static Object entries(ImagePlus image) {
        float[] table=image.getCalibration().getCTable();
        List<Object> result=new ArrayList<Object>();
        if(table==null) return result;
        Set<Integer> codes=new TreeSet<Integer>();
        for(int z=1;z<=image.getStackSize();++z) {
            Object pixels=image.getStack().getPixels(z);
            if(pixels instanceof byte[]) for(byte v:(byte[])pixels) codes.add(v&255);
            else if(pixels instanceof short[]) for(short v:(short[])pixels) codes.add(v&65535);
        }
        for(int code:codes) result.add(map("code",code,"bits",String.format(Locale.ROOT,"%08x",Float.floatToRawIntBits(table[code]))));
        return result;
    }
    static void observe(String family,String kind,String profile,String method,String range,int group) throws Exception {
        String id="policy."+family+"."+kind+"."+profile+"."+range+"."+method;
        ImagePlus input=family.equals("channels")?source(id,kind,"full",6,1,1,false):density(id,kind,profile);
        int first=range.equals("even")?2:1,last=range.equals("even")?5:6;
        Object table=entries(input),before=state(input);
        ImagePlus output=group==0?ZProjector.run(input,method,first,last):
            new GroupedZProjector().groupZProject(input,Arrays.asList(METHODS).indexOf(method),group);
        Map<String,Object> row=map("case_id",id,"family",family,"kind",kind,"profile",profile,
            "method",method,"bounds_one_based",new int[]{first,last},"group_size",group,
            "entrypoint",group==0?"ZProjector.run":"GroupedZProjector.groupZProject",
            "ui_executed",false,"before",before,"table_entries_used",table,
            "signed16",input.getCalibration().isSigned16Bit(),"output_api",state(output));
        if(group!=0) {
            output.setCalibration(input.getCalibration());output.getCalibration().pixelDepth*=group;
            row.put("output_after_plugin_calibration_steps",state(output));
            row.put("calibration_steps_reproduced_from_source",true);
        }
        row.put("after",state(input));row.put("after_table_entries_used",entries(input));row.put("status","ok");
        String line=json(row);emitted+=line.length()+1;
        if(emitted>4*1024*1024) throw new IllegalStateException("Capture limit exceeded");
        System.out.print(line);System.out.print('\n');++cases;
    }
    public static void main(String[] args) throws Exception {
        if(!ij.IJ.getVersion().equals("1.54r")) throw new IllegalStateException("Reference version differs");
        for(String kind:new String[]{"u8","u16","f32","rgb"})
            for(String range:new String[]{"full","even","g2","g3"})
                for(String method:METHODS) observe("channels",kind,"none",method,range,range.equals("g2")?2:range.equals("g3")?3:0);
        for(String kind:new String[]{"u8","u16"})
            for(String profile:new String[]{"linear","poly2","zero_clip","custom_signed","custom_range","custom_ieee"})
                for(String range:new String[]{"full","even"})
                    for(String method:METHODS) observe("density",kind,profile,method,range,0);
        for(String range:new String[]{"full","even"}) for(String method:METHODS)
            observe("density","u8","od",method,range,0);
        if(cases!=252) throw new IllegalStateException("Wrong policy case count");
    }
}
