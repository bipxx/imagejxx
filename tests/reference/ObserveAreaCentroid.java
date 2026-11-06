import ij.ImagePlus;
import ij.gui.*;
import ij.measure.*;
import ij.plugin.filter.Analyzer;
import ij.process.*;
import java.awt.Rectangle;

/** Bounded original Analyzer centroid and the exact consumed pixel membership. */
public class ObserveAreaCentroid {
  static String num(double value) {return Double.isFinite(value)?Double.toString(value):"\""+(Double.isNaN(value)?"nan":value>0?"inf":"-inf")+"\"";}
  static void observe(int shape,int type,int calibration) {
    ImageProcessor ip=type==0?new ByteProcessor(40,40):type==1?new ShortProcessor(40,40):new FloatProcessor(40,40);
    for(int y=0;y<40;y++)for(int x=0;x<40;x++)ip.setf(x,y,type==2?x-y*.5f:x+y);
    ImagePlus image=new ImagePlus("centroid",ip);image.setGlobalCalibration(null);
    Calibration cal=image.getLocalCalibration();cal.setUnit("um");
    if(calibration>0){cal.pixelWidth=2;cal.pixelHeight=.5;cal.xOrigin=3.25;cal.yOrigin=-1.5;}
    if(calibration==2)cal.setInvertY(true);
    Roi roi=null;String kind="whole_image";double[][] points=null;
    if(shape>0 && shape<=6) {
      double[][] boxes={{3,3,10,8},{3.2,4.7,10.2,8.3},{-2.7,-1.2,5.1,4.4},
        {3,3,10,8},{3.2,4.7,10.2,8.3},{12.3,12.9,.2,.8}};
      double[] b=boxes[shape-1];kind=shape>=4?"ellipse":"rectangle";
      roi=shape>=4?new OvalRoi(b[0],b[1],b[2],b[3]):new Roi(b[0],b[1],b[2],b[3]);
      points=new double[][]{{b[0],b[1]},{b[0]+b[2],b[1]+b[3]}};
    } else if(shape==7 || shape==8) {
      kind="polygon";float[] x=shape==7?new float[]{3,13,9}:new float[]{3.2f,13.8f,9.4f};
      float[] y=shape==7?new float[]{3,3,11}:new float[]{4.7f,3.1f,14.2f};
      roi=new PolygonRoi(x,y,3,Roi.POLYGON);points=new double[3][2];
      for(int i=0;i<3;i++)points[i]=new double[]{x[i],y[i]};
    } else if(shape==9 || shape==10) {
      kind=shape==9?"rectangle":"ellipse";roi=shape==9?new Roi(60,60,4,4):new OvalRoi(60,60,4,4);points=new double[][]{{60,60},{64,64}};
    }
    if(roi!=null)image.setRoi(roi);
    ResultsTable table=new ResultsTable();new Analyzer(image,Measurements.AREA|Measurements.CENTROID,table).measure();
    Rectangle bounds=ip.getRoi();ImageProcessor mask=ip.getMask();
    StringBuilder members=new StringBuilder("[");boolean first=true;
    for(int y=bounds.y;y<bounds.y+bounds.height;y++)for(int x=bounds.x;x<bounds.x+bounds.width;x++)
      if(x>=0&&y>=0&&x<40&&y<40&&(mask==null||mask.get(x-bounds.x,y-bounds.y)!=0)) {
        if(!first)members.append(',');first=false;members.append(y*40+x);
      }
    members.append(']');StringBuilder geometry=new StringBuilder("[");
    if(points!=null)for(int i=0;i<points.length;i++){if(i>0)geometry.append(',');geometry.append('[').append(points[i][0]).append(',').append(points[i][1]).append(']');}
    geometry.append(']');
    StringBuilder out=new StringBuilder("{\"id\":\"shape"+shape+"-type"+type+"-cal"+calibration+"\",\"kind\":\""+kind+"\",\"type\":"+type+",\"points\":"+geometry+",\"spacing\":["+cal.pixelWidth+","+cal.pixelHeight+"],\"origins\":["+cal.xOrigin+","+cal.yOrigin+"],\"invert_y\":"+(calibration==2)+",\"selection_retained\":"+(image.getRoi()!=null)+",\"members\":"+members+",\"rows\":[");
    for(int i=0;i<table.size();i++) {
      if(i>0)out.append(',');out.append('{');first=true;
      for(String heading:table.getHeadings()){if(!first)out.append(',');first=false;out.append('"').append(heading).append("\":").append(num(table.getValue(heading,i)));}
      out.append('}');
    }
    System.out.println(out.append("]}"));
  }
  public static void main(String[] args){for(int shape=0;shape<11;shape++)for(int type=0;type<3;type++)for(int calibration=0;calibration<3;calibration++)observe(shape,type,calibration);}
}
