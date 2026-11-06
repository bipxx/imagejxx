import ij.ImagePlus;
import ij.gui.PointRoi;
import ij.measure.*;
import ij.plugin.filter.Analyzer;
import ij.process.*;

/** Ordered original third/fourth moments through the maintained Analyzer routes. */
public class ObserveMomentMeasurements {
  static final int EXTRA=Measurements.SKEWNESS|Measurements.KURTOSIS|Measurements.AREA_FRACTION;
  static String num(double v){return ObserveAreaCentroid.num(v);}
  static void tail(int type,int pattern,double[] values,int range,boolean limit,String density,boolean point) {
    ImageProcessor ip=type==0?new ByteProcessor(values.length,1):type==1?new ShortProcessor(values.length,1):new FloatProcessor(values.length,1);
    for(int i=0;i<values.length;i++)ip.setf(i,0,(float)values[i]);
    ImagePlus image=new ImagePlus("moment-tail",ip);image.setGlobalCalibration(null);Calibration cal=image.getLocalCalibration();cal.setUnit("pixel");
    if(density.equals("signed16"))cal.setSigned16BitCalibration();
    else if(!density.equals("raw")) {
      float[] table=new float[type==0?256:65536];for(int i=0;i<table.length;i++)table[i]=i;
      table[1]=density.equals("nan")?Float.NaN:density.equals("inf")?Float.POSITIVE_INFINITY:Float.NEGATIVE_INFINITY;
      cal.setCTable(table,"tail");
    }
    double low=range==1?0:range==2?(type==2?-Float.MIN_VALUE:1):Double.MIN_VALUE;
    double high=range==1?0:range==2?(type==2?Float.MIN_VALUE:2):Double.MIN_VALUE;
    if(range!=0)ip.setThreshold(low,high,ImageProcessor.NO_LUT_UPDATE);
    if(point)image.setRoi(new PointRoi(0,0));
    int flags=EXTRA|Measurements.AREA|Measurements.MEAN|Measurements.STD_DEV|Measurements.CENTROID|Measurements.CENTER_OF_MASS|Measurements.INTEGRATED_DENSITY;
    if(limit)flags|=Measurements.LIMIT;
    Analyzer.setMeasurements(flags);Analyzer.setRedirectImage(null);ResultsTable table=new ResultsTable();new Analyzer(image,flags,table).measure();
    StringBuilder out=new StringBuilder("{\"id\":\"moment-tail-type"+type+"-pattern"+pattern+"-range"+range+"-limit"+limit+"-"+density+"-point"+point+"\",\"type\":"+type+",\"density\":\""+density+"\",\"limit\":"+limit+",\"point\":"+point+",\"pixels\":[");
    for(int i=0;i<values.length;i++){if(i>0)out.append(',');out.append(num((double)ip.getf(i,0)));}
    out.append("],\"range\":").append(range==0?"null":"["+num(low)+","+num(high)+"]").append(",\"rows\":[");
    for(int n=0;n<table.size();n++){if(n>0)out.append(',');out.append('{');boolean first=true;for(String key:table.getHeadings()){if(!first)out.append(',');first=false;out.append('"').append(key).append("\":").append(num(table.getValue(key,n)));}out.append('}');}
    System.out.println(out.append("]}"));
  }
  static void tails() {
    double[][] floats={{0,0,0,0},{0,Float.MIN_VALUE,-Float.MIN_VALUE,0},{1e-20f,1.00001e-20f,-1e-20f,0},{1,1,Math.nextUp(1f),1},{1e20f,1e20f,Math.nextUp(1e20f),1e20f},{Float.MAX_VALUE,Math.nextDown(Float.MAX_VALUE),-Float.MAX_VALUE,0},{-0.0,0,-0.0,0},{1e20f},{-1,-1,-1,-1}};
    for(int p=0;p<floats.length;p++)for(int range=0;range<4;range++)for(boolean limit:new boolean[]{false,true})tail(2,p,floats[p],range,limit,"raw",false);
    for(int type=0;type<2;type++) {
      double[][] integers={{0,0,0,0},{0,1,1,4},{1,2,2,4},{7,7,7,7},{0,1,type==0?254:32768,type==0?255:65535}};
      for(int p=0;p<integers.length;p++)for(int range=0;range<3;range++)for(boolean limit:new boolean[]{false,true})tail(type,p,integers[p],range,limit,"raw",false);
      for(String density:new String[]{"nan","inf","negative_inf"})for(int range=0;range<3;range++)for(boolean limit:new boolean[]{false,true})tail(type,1,integers[1],range,limit,density,false);
    }
    for(int range=0;range<3;range++)for(boolean limit:new boolean[]{false,true})tail(1,8,new double[]{0,32768,32769,65535},range,limit,"signed16",false);
    for(int p=0;p<2;p++)for(int range=0;range<2;range++)for(boolean limit:new boolean[]{false,true})tail(2,20+p,new double[]{p==0?1e20f:Float.MIN_VALUE},range,limit,"raw",true);
  }
  public static void main(String[] args) {
    if(args.length==1 && args[0].equals("tails")){tails();return;}
    if(args.length==1 && args[0].equals("redirect")){ObserveRedirectMeasurements.extraFlags=EXTRA;ObserveRedirectMeasurements.main(new String[0]);return;}
    ObserveThresholdMeasurements.extraFlags=EXTRA;ObserveThresholdMeasurements.main(new String[0]);
    for(int shape=0;shape<7;shape++)for(int type=0;type<3;type++)for(int range:new int[]{0,3,4})ObserveThresholdMeasurements.observe(shape,type,range,0,0,false);
    for(int shape:new int[]{0,1,3})for(int type=0;type<2;type++)for(int range=1;range<4;range++)for(int density=1;density<=3;density++)ObserveThresholdMeasurements.observe(shape,type,range,density,0,false);
    for(int shape:new int[]{0,1,3,10,11})for(int type=0;type<3;type++)for(boolean limit:new boolean[]{false,true})ObserveThresholdMeasurements.observe(shape,type,5,0,0,limit);
    ObserveThresholdMeasurements.zeroSource=true;
    for(int shape=0;shape<12;shape++)for(int type=0;type<3;type++)for(int range=0;range<2;range++)for(boolean limit:new boolean[]{false,true})ObserveThresholdMeasurements.observe(shape,type,range,0,0,limit);
  }
}
