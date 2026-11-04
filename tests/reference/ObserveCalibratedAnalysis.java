import ij.ImagePlus;
import ij.gui.Line;
import ij.gui.ProfilePlot;
import ij.gui.Roi;
import ij.gui.PlotWindow;
import ij.measure.Calibration;
import ij.measure.Measurements;
import ij.process.*;
import java.util.Locale;

/** Actual ImageJ statistics/profile calls; never opens a window or uses native output. */
public class ObserveCalibratedAnalysis {
  static String q(String s) { return "\""+s.replace("\\","\\\\").replace("\"","\\\"")+"\""; }
  static String number(double v) {
    if(Double.isNaN(v))return q("nan");
    if(Double.isInfinite(v))return q(v>0?"inf":"-inf");
    return Double.toString(v);
  }
  static String values(double[] values) {
    StringBuilder s=new StringBuilder("[");
    for(int i=0;i<values.length;i++){if(i>0)s.append(',');s.append(number(values[i]));}
    return s.append(']').toString();
  }
  static String sparse(int[] hist) {
    if(hist==null)return "null";
    StringBuilder s=new StringBuilder("[");boolean first=true;
    for(int i=0;i<hist.length;i++)if(hist[i]!=0){if(!first)s.append(',');first=false;s.append('[').append(i).append(',').append(hist[i]).append(']');}
    return s.append(']').toString();
  }
  static ImagePlus image(String type,boolean profile) {
    int w=profile?5:4,h=2;
    int[] input=profile?new int[]{0,1,2,3,4,4,3,2,1,0}:new int[]{0,1,1,3,2,3,4,4};
    ImageProcessor ip;
    if(type.equals("uint8")) {
      byte[] a=new byte[input.length];for(int i=0;i<a.length;i++)a[i]=(byte)input[i];ip=new ByteProcessor(w,h,a);
    } else if(type.equals("uint16")) {
      short[] a=new short[input.length];for(int i=0;i<a.length;i++)a[i]=(short)input[i];ip=new ShortProcessor(w,h,a,null);
    } else {
      float[] a=new float[input.length];for(int i=0;i<a.length;i++)a[i]=input[i];ip=new FloatProcessor(w,h,a);
    }
    ImagePlus result=new ImagePlus(type,ip);result.setGlobalCalibration(null);
    Calibration c=result.getLocalCalibration();c.pixelWidth=3;c.pixelHeight=4;c.setUnit("local");return result;
  }
  static Calibration calibration(String mode,int bits) {
    Calibration c=new Calibration();c.pixelWidth=.5;c.pixelHeight=1.25;c.setUnit("um");
    if(mode.equals("linear"))c.setFunction(Calibration.STRAIGHT_LINE,new double[]{10,2},"OD");
    else if(mode.equals("decreasing"))c.setFunction(Calibration.STRAIGHT_LINE,new double[]{10,-2},"OD");
    else if(!mode.equals("raw")) {
      float[] table=new float[bits==16?65536:256];
      for(int i=0;i<table.length;i++)table[i]=(float)(i*i);
      table[0]=4;table[1]=1;table[2]=9;table[3]=0;table[4]=2;
      if(mode.equals("nan"))table[1]=Float.NaN;
      if(mode.equals("nan_gap"))table[2]=Float.NaN;
      if(mode.equals("infinite"))table[1]=Float.POSITIVE_INFINITY;
      if(mode.equals("all_nan"))java.util.Arrays.fill(table,Float.NaN);
      c.setCTable(table,"table");
    }
    return c;
  }
  static void statistics(String type,String mode,boolean roi,boolean global,boolean ignore) {
    ImagePlus image=image(type,false);
    Calibration c=calibration(mode,type.equals("uint16")?16:8);
    if(global)image.setGlobalCalibration(c);else image.setCalibration(c);
    image.setIgnoreGlobalCalibration(ignore);
    if(roi)image.setRoi(new Roi(1,0,2,2));
    int options=Measurements.AREA|Measurements.MEAN|Measurements.STD_DEV|Measurements.MIN_MAX|Measurements.MEDIAN|Measurements.MODE;
    ImageStatistics s=image.getStatistics(options);
    String id="stats_"+type+"_"+mode+(roi?"_roi":"")+(global?"_global":"_local")+(ignore?"_ignored":"");
    System.out.println("{\"id\":"+q(id)+",\"kind\":\"statistics\",\"type\":"+q(type)+",\"mode\":"+q(mode)+
      ",\"roi\":"+roi+",\"global\":"+global+",\"ignore\":"+ignore+
      ",\"pixel_count\":"+s.pixelCount+",\"area\":"+number(s.area)+",\"mean\":"+number(s.mean)+
      ",\"raw_mean\":"+number(s.umean)+",\"stddev\":"+number(s.stdDev)+",\"minimum\":"+number(s.min)+
      ",\"maximum\":"+number(s.max)+",\"median\":"+number(s.median)+",\"mode_index\":"+s.mode+
      ",\"mode_value\":"+number(s.dmode)+",\"integrated_density\":"+number(s.area*s.mean)+
      ",\"histogram\":"+sparse(s.histogram)+",\"histogram16\":"+sparse(s.histogram16)+"}");
    image.setGlobalCalibration(null);
  }
  static void profile(String type,String mode,boolean interpolate,boolean diagonal) {
    ImagePlus image=image(type,true);image.setGlobalCalibration(calibration(mode,type.equals("uint16")?16:8));
    image.setRoi(new Line(0,0,4,diagonal?1:0));PlotWindow.interpolate=interpolate;
    double[] result=new ProfilePlot(image).getProfile();
    System.out.println("{\"id\":"+q("profile_"+type+"_"+mode+(interpolate?"_interpolated":"_nearest")+(diagonal?"_diagonal":"_horizontal"))+
      ",\"kind\":\"profile\",\"type\":"+q(type)+",\"mode\":"+q(mode)+",\"interpolate\":"+interpolate+
      ",\"diagonal\":"+diagonal+",\"values\":"+values(result)+"}");image.setGlobalCalibration(null);
  }
  public static void main(String[] args) {
    Locale.setDefault(Locale.ROOT);System.setProperty("java.awt.headless","true");
    for(String type:new String[]{"uint8","uint16"}) {
      for(String mode:new String[]{"raw","linear","decreasing","lookup","nan","nan_gap","infinite","all_nan"})
        statistics(type,mode,false,true,false);
      statistics(type,"linear",true,true,false);
      statistics(type,"linear",false,true,true);
      statistics(type,"linear",false,false,false);
    }
    statistics("float32","linear",false,true,false);
    for(String type:new String[]{"uint8","uint16","float32"})
      for(String mode:new String[]{"raw","linear","lookup"})
        for(boolean interpolate:new boolean[]{false,true})
          for(boolean diagonal:new boolean[]{false,true})profile(type,mode,interpolate,diagonal);
  }
}
