import ij.ImagePlus;
import ij.ImageStack;
import ij.gui.*;
import ij.measure.*;
import ij.plugin.filter.Analyzer;
import ij.process.*;

/** Original Analyzer two-image ownership; no application implementation imported. */
public class ObserveRedirectMeasurements {
  static int extraFlags=0;
  static String number(double v) {
    return Double.isFinite(v)?Double.toString(v):"\""+(Double.isNaN(v)?"nan":v>0?"inf":"-inf")+"\"";
  }
  static double raw(int type,double v) {return type==1?v*257:type==2?v*.25-2:v;}
  static ImagePlus image(String title,int type,int depth,boolean destination) {
    ImageStack stack=new ImageStack(8,6);
    for(int z=0;z<depth;z++) {
      ImageProcessor ip=type==0?new ByteProcessor(8,6):type==1?new ShortProcessor(8,6):new FloatProcessor(8,6);
      for(int y=0;y<6;y++)for(int x=0;x<8;x++)
        ip.setf(x,y,(float)raw(type,(destination?30+3*x+y:x+2*y)+z*10));
      stack.addSlice(ip);
    }
    ImagePlus image=new ImagePlus(title,stack);image.setGlobalCalibration(null);return image;
  }
  static Roi selection(int shape) {
    if(shape==0)return null;
    if(shape==1)return new Roi(1,1,5,4);
    if(shape==2)return new OvalRoi(1,1,5,4);
    float[] x=shape==3?new float[]{1,7,3}:shape==4?new float[]{1,6,6,1}:shape==5?new float[]{20,26,22}:shape==6?new float[]{2,3,4}:shape==7?new float[]{1,6}:shape==8?new float[]{1,6,6}:shape==9?new float[]{1,4,6}:shape==10?new float[]{2}:new float[]{2,6};
    float[] y=shape==3?new float[]{1,1,5}:shape==4?new float[]{1,1,5,5}:shape==5?new float[]{20,20,24}:shape==6?new float[]{2,2,2}:shape==7?new float[]{1,4}:shape==8?new float[]{1,1,4}:shape==9?new float[]{1,4,1}:shape==10?new float[]{2}:new float[]{2,4};
    return shape==7?new Line(x[0],y[0],x[1],y[1]):shape>=10?new PointRoi(x,y,x.length):new PolygonRoi(x,y,x.length,shape==8?Roi.POLYLINE:shape==9?Roi.ANGLE:Roi.POLYGON);
  }
  static void calibration(ImagePlus image,boolean spatial,boolean density,boolean destination) {
    Calibration c=image.getLocalCalibration();
    if(spatial){c.pixelWidth=destination?.5:2;c.pixelHeight=destination?1.25:3;c.xOrigin=destination?2:.5;c.yOrigin=destination?-.5:1;c.setUnit(destination?"target_um":"source_um");}
    if(density && image.getBitDepth()!=32)c.setFunction(Calibration.STRAIGHT_LINE,destination?new double[]{5,.25}:new double[]{-7,2},destination?"target_density":"source_density");
  }
  static void observe(String group,int shape,int type,int threshold,int variant,boolean limit,int spatial,int density,int sourceDepth,int targetDepth,int mode) {
    ImagePlus source=image("source",0,sourceDepth,false),target=image("target",type,targetDepth,true);
    source.setSlice(Math.min(2,sourceDepth));target.setSlice(targetDepth);
    if(mode==3){source.setDimensions(2,2,1);target.setDimensions(1,2,2);source.setSlice(3);target.setSlice(1);}
    calibration(source,(spatial&1)!=0,(density&1)!=0,false);
    calibration(target,(spatial&2)!=0,(density&2)!=0,true);
    Roi roi=selection(shape);if(roi!=null)source.setRoi(roi);if(shape==4)((PolygonRoi)roi).fitSpline();
    if(mode==2){source.setTitle("mask");for(int z=1;z<=sourceDepth;z++)for(int y=0;y<6;y++)for(int x=0;x<8;x++)source.getStack().getProcessor(z).set(x,y,x>=3?255:0);}
    if((threshold&1)!=0)source.getProcessor().setThreshold(6.4,10.4,ImageProcessor.NO_LUT_UPDATE);
    if((threshold&2)!=0)target.getProcessor().setThreshold(raw(type,40.4),raw(type,46.4),ImageProcessor.NO_LUT_UPDATE);
    int flags=Measurements.AREA|Measurements.MEAN|Measurements.STD_DEV|Measurements.MODE|Measurements.MIN_MAX|Measurements.MEDIAN|Measurements.CENTROID|Measurements.CENTER_OF_MASS|Measurements.INTEGRATED_DENSITY|Measurements.PERIMETER|Measurements.RECT|Measurements.FERET|Measurements.STACK_POSITION;
    if(variant==1)flags|=Measurements.ELLIPSE;if(variant==2)flags|=Measurements.SHAPE_DESCRIPTORS;if(limit)flags|=Measurements.LIMIT;
    flags|=extraFlags;Analyzer.setMeasurements(flags);Analyzer.setRedirectImage(mode==1?source:target);
    int before=target.getCurrentSlice();ResultsTable table=new ResultsTable();new Analyzer(source,flags,table).measure();
    StringBuilder b=new StringBuilder("{\"id\":\"redirect-"+group+"-shape"+shape+"-type"+type+"-threshold"+threshold+"-variant"+variant+"-limit"+limit+"-spatial"+spatial+"-density"+density+"-depth"+sourceDepth+"x"+targetDepth+"-mode"+mode+"\",\"shape\":"+shape+",\"type\":"+type+",\"threshold_case\":"+threshold+",\"variant\":"+variant+",\"limit\":"+limit+",\"spatial\":"+spatial+",\"density\":"+density+",\"source_depth\":"+sourceDepth+",\"target_depth\":"+targetDepth+",\"mode\":"+mode+",\"source_slice\":"+source.getCurrentSlice()+",\"target_slice_before\":"+before+",\"target_slice_after\":"+target.getCurrentSlice()+",\"roi_owner_after\":\""+(roi==null?"none":roi.getImage()==source?"source":roi.getImage()==target?"target":"other")+"\",\"rows\":[");
    for(int row=0;row<table.size();row++) {
      if(row>0)b.append(',');b.append('{');boolean first=true;
      for(String heading:table.getHeadings()){if(!first)b.append(',');first=false;b.append('"').append(heading).append("\":").append(number(table.getValue(heading,row)));}
      b.append('}');
    }
    System.out.println(b.append("]}").toString());Analyzer.setRedirectImage(null);
  }
  public static void main(String[] args) {
    for(int shape=0;shape<12;shape++)for(int type=0;type<3;type++)for(int threshold=0;threshold<4;threshold++)for(int variant=0;variant<(shape<7?3:1);variant++)observe("populations",shape,type,threshold,variant,true,3,3,1,1,0);
    for(int shape=0;shape<12;shape++)for(int type=0;type<3;type++)observe("flagoff",shape,type,3,shape<7?1:0,false,3,3,1,1,0);
    for(int shape:new int[]{0,1,3,7,8,10,11})for(int type=0;type<3;type++)for(int spatial=0;spatial<3;spatial++)observe("calibration",shape,type,3,0,true,spatial,spatial,1,1,0);
    for(int shape:new int[]{0,1,7,10,11})for(int type=0;type<3;type++)for(int[] depth:new int[][]{{3,3},{3,2},{3,1},{1,3},{4,4}})observe("stacks",shape,type,0,0,true,3,3,depth[0],depth[1],depth[0]==4?3:0);
    for(int shape:new int[]{0,1,3,7,10})for(int type=0;type<3;type++)observe("self",shape,type,3,0,true,3,3,1,1,1);
    for(int shape:new int[]{0,1,2,3})for(int type=0;type<3;type++)for(int threshold=0;threshold<4;threshold++)observe("mask-title",shape,type,threshold,0,true,3,3,1,1,2);
  }
}
