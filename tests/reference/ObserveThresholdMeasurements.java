import ij.ImagePlus;
import ij.gui.*;
import ij.measure.*;
import ij.plugin.filter.Analyzer;
import ij.process.*;

/** Original LIMIT per-column populations; uses reviewed ImageJ classes only. */
public class ObserveThresholdMeasurements {
  static String num(double v) {return ObserveAreaCentroid.num(v);}
  static double raw(int type,double v) {return type==1?v*257:type==2?v*.25-2:v;}
  static String histogram(int[] values) {
    if(values==null)return "null";
    StringBuilder b=new StringBuilder("{\"size\":").append(values.length).append(",\"nonzero\":[");boolean first=true;
    for(int i=0;i<values.length;i++)if(values[i]!=0){if(!first)b.append(',');first=false;b.append('[').append(i).append(',').append(values[i]).append(']');}
    return b.append("]}").toString();
  }
  static void observe(int shape,int type,int threshold,int density,int variant,boolean limit) {
    ImageProcessor ip=type==0?new ByteProcessor(8,6):type==1?new ShortProcessor(8,6):new FloatProcessor(8,6);
    for(int y=0;y<6;y++)for(int x=0;x<8;x++)ip.setf(x,y,(float)raw(type,x+2*y));
    ImagePlus image=new ImagePlus("threshold",ip);image.setGlobalCalibration(null);Calibration cal=image.getLocalCalibration();cal.setUnit("um");
    if(density==1)cal.setFunction(Calibration.STRAIGHT_LINE,new double[]{-2,.5},"linear");
    if(density==3){float[] table=new float[type==0?256:65536];for(int i=0;i<table.length;i++)table[i]=i;table[(int)raw(type,7)]=Float.NaN;cal.setCTable(table,"mixed_nan");}
    if(density==2){float[] table=new float[type==0?256:65536];java.util.Arrays.fill(table,Float.NaN);cal.setCTable(table,"all_nan");}
    String kind="whole_image",points="[]";Roi roi=null;
    if(shape==1 || shape==2){kind=shape==1?"rectangle":"ellipse";roi=shape==1?new Roi(1,1,5,4):new OvalRoi(1,1,5,4);points="[[1,1],[6,5]]";}
    else if(shape>=3) {
      float[] x=shape==3?new float[]{1,7,3}:shape==4?new float[]{1,6,6,1}:shape==5?new float[]{20,26,22}:shape==6?new float[]{2,3,4}:shape==7?new float[]{1,6}:shape==8?new float[]{1,6,6}:shape==9?new float[]{1,4,6}:shape==10?new float[]{2}:new float[]{2,6};
      float[] y=shape==3?new float[]{1,1,5}:shape==4?new float[]{1,1,5,5}:shape==5?new float[]{20,20,24}:shape==6?new float[]{2,2,2}:shape==7?new float[]{1,4}:shape==8?new float[]{1,1,4}:shape==9?new float[]{1,4,1}:shape==10?new float[]{2}:new float[]{2,4};
      kind=shape<7?"polygon":shape==7?"line":shape==8?"polyline":shape==9?"angle":shape==10?"point":"multipoint";
      roi=shape==7?new Line(x[0],y[0],x[1],y[1]):shape>=10?new PointRoi(x,y,x.length):new PolygonRoi(x,y,x.length,shape==8?Roi.POLYLINE:shape==9?Roi.ANGLE:Roi.POLYGON);
      StringBuilder b=new StringBuilder("[");for(int i=0;i<x.length;i++){if(i>0)b.append(',');b.append('[').append((double)x[i]).append(',').append((double)y[i]).append(']');}points=b.append(']').toString();
    }
    if(roi!=null)image.setRoi(roi);if(shape==4)((PolygonRoi)roi).fitSpline();
    double lower=threshold==1?raw(type,6):threshold==2?raw(type,6.4):threshold==3?raw(type,100):raw(type,-10);
    double upper=threshold==1?raw(type,10):threshold==2?raw(type,10.4):threshold==3?raw(type,110):raw(type,100);
    if(threshold!=0)ip.setThreshold(lower,upper,ImageProcessor.NO_LUT_UPDATE);
    boolean active=ip.isThreshold();double actualLower=ip.getMinThreshold(),actualUpper=ip.getMaxThreshold();
    int flags=Measurements.AREA|Measurements.MEAN|Measurements.STD_DEV|Measurements.MODE|Measurements.MIN_MAX|Measurements.MEDIAN|Measurements.CENTROID|Measurements.CENTER_OF_MASS|Measurements.INTEGRATED_DENSITY|Measurements.PERIMETER|Measurements.RECT|Measurements.FERET;
    if(variant==1)flags|=Measurements.ELLIPSE;if(variant==2)flags|=Measurements.SHAPE_DESCRIPTORS;if(limit)flags|=Measurements.LIMIT;
    Analyzer.setMeasurements(flags);ImageStatistics stats=shape<7?image.getStatistics(flags):null;
    java.awt.Rectangle bounds=ip.getRoi();ResultsTable table=new ResultsTable();new Analyzer(image,flags,table).measure();
    StringBuilder out=new StringBuilder("{\"id\":\"threshold-shape"+shape+"-type"+type+"-range"+threshold+"-density"+density+"-variant"+variant+"-limit"+limit+"\",\"kind\":\""+kind+"\",\"points\":"+points+",\"type\":"+type+",\"threshold_case\":"+threshold+",\"density\":"+density+",\"variant\":"+variant+",\"limit\":"+limit+",\"fitted\":"+(shape==4)+",\"selection_retained\":"+(image.getRoi()!=null)+",\"requested_range\":["+num(lower)+","+num(upper)+"],\"threshold_active\":"+active+",\"effective_range\":["+num(actualLower)+","+num(actualUpper)+"],\"processor_bounds\":["+bounds.x+","+bounds.y+","+bounds.width+","+bounds.height+"],\"summary\":");
    if(stats==null)out.append("null");else {
      out.append('{');String[] names={"pixel_count","area","mean","raw_mean","stddev","minimum","maximum","median","mode_value","mode_index","x","y","xm","ym","major","minor","angle","lower_threshold","upper_threshold"};
      double[] values={stats.pixelCount,stats.area,stats.mean,stats.umean,stats.stdDev,stats.min,stats.max,stats.median,stats.dmode,stats.mode,stats.xCentroid,stats.yCentroid,stats.xCenterOfMass,stats.yCenterOfMass,stats.major,stats.minor,stats.angle,stats.lowerThreshold,stats.upperThreshold};
      for(int i=0;i<names.length;i++){if(i>0)out.append(',');out.append('"').append(names[i]).append("\":").append(num(values[i]));}
      out.append(",\"histogram\":").append(histogram(stats.histogram)).append(",\"histogram16\":").append(histogram(stats.histogram16)).append('}');
    }
    out.append(",\"rows\":[");for(int row=0;row<table.size();row++){
      if(row>0)out.append(',');out.append('{');boolean first=true;for(String heading:table.getHeadings()){if(!first)out.append(',');first=false;out.append('"').append(heading).append("\":").append(num(table.getValue(heading,row)));}out.append('}');
    }System.out.println(out.append("]}").toString());
  }
  static void mixed(){for(int shape:new int[]{0,1,3})for(int type=0;type<2;type++)for(int variant=0;variant<2;variant++)observe(shape,type,1,3,variant,true);}
  public static void main(String[] args) {
    if(args.length==1 && args[0].equals("mixed-only")){mixed();return;}
    for(int shape=0;shape<7;shape++)for(int type=0;type<3;type++)for(int range=0;range<5;range++)for(int variant=0;variant<3;variant++)observe(shape,type,range,0,variant,true);
    for(int shape:new int[]{0,1,3})for(int type=0;type<2;type++)for(int range=1;range<4;range++)for(int density=1;density<=2;density++)for(int variant=0;variant<3;variant++)observe(shape,type,range,density,variant,true);
    for(int shape=0;shape<7;shape++)for(int type=0;type<3;type++)for(int range=1;range<=2;range++)observe(shape,type,range,0,0,false);
    if(args.length==1 && args[0].equals("area-only"))return;
    for(int shape=7;shape<12;shape++)for(int type=0;type<3;type++)for(int range=0;range<4;range++)for(boolean limit:new boolean[]{false,true})observe(shape,type,range,0,0,limit);
    mixed();
  }
}
