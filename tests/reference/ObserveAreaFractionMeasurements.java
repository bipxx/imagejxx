import ij.measure.Measurements;

/** Add AREA_FRACTION to maintained observers without changing their default runs. */
public class ObserveAreaFractionMeasurements {
  static void extremes() {
    double max=Float.MAX_VALUE;
    double edge=max+Math.ulp(Float.MAX_VALUE)*.5;
    double[] ranges={Math.nextUp(max),Math.nextDown(edge),edge,-Math.nextUp(max),-Math.nextDown(edge),-edge};
    for(int i=0;i<ranges.length;i++) {
      float extreme=i<3?Float.MAX_VALUE:-Float.MAX_VALUE;
      ij.process.FloatProcessor ip=new ij.process.FloatProcessor(2,1,new float[]{0,extreme});
      ij.ImagePlus image=new ij.ImagePlus("extreme",ip);image.setGlobalCalibration(null);
      ip.setThreshold(ranges[i],ranges[i],ij.process.ImageProcessor.NO_LUT_UPDATE);
      ij.process.ImageStatistics stats=image.getStatistics(Measurements.AREA_FRACTION);
      System.out.println("{\"id\":\"float-overflow-"+i+"\",\"pixels\":[0,"+((double)extreme)+"],\"range\":["+ranges[i]+","+ranges[i]+"],\"expected\":"+stats.areaFraction+"}");
    }
  }
  public static void main(String[] args) {
    if(args.length==1 && args[0].equals("extremes")){extremes();return;}
    if(args.length==1 && args[0].equals("redirect")) {
      ObserveRedirectMeasurements.extraFlags=Measurements.AREA_FRACTION;
      ObserveRedirectMeasurements.main(new String[0]);return;
    }
    ObserveThresholdMeasurements.extraFlags=Measurements.AREA_FRACTION;
    if(args.length==1 && args[0].equals("rounding")) {
      for(int shape:new int[]{0,1,3,10,11})for(int type=0;type<3;type++)for(boolean limit:new boolean[]{false,true})
        ObserveThresholdMeasurements.observe(shape,type,5,0,0,limit);
      return;
    }
    ObserveThresholdMeasurements.main(new String[0]);
    for(int shape=0;shape<7;shape++)for(int type=0;type<3;type++)for(int range:new int[]{0,3,4})
      ObserveThresholdMeasurements.observe(shape,type,range,0,0,false);
    for(int shape:new int[]{0,1,3})for(int type=0;type<2;type++)for(int range=1;range<4;range++)for(int density=1;density<=3;density++)
      ObserveThresholdMeasurements.observe(shape,type,range,density,0,false);
    ObserveThresholdMeasurements.zeroSource=true;
    for(int shape=0;shape<12;shape++)for(int type=0;type<3;type++)for(int range=0;range<2;range++)for(boolean limit:new boolean[]{false,true})
      ObserveThresholdMeasurements.observe(shape,type,range,0,0,limit);
  }
}
