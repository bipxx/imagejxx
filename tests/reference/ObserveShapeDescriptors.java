import ij.*;
import ij.gui.*;
import ij.measure.*;
import ij.plugin.filter.Analyzer;
import ij.plugin.Selection;
import ij.process.*;
import java.awt.Polygon;

/** One original Shape descriptors option; immutable prior formatter reused. */
public class ObserveShapeDescriptors {
  static void observe(int shape,int type,int calibration) {
    ImageProcessor ip=type==0?new ByteProcessor(40,40):type==1?new ShortProcessor(40,40):new FloatProcessor(40,40);
    for(int y=0;y<40;y++)for(int x=0;x<40;x++)ip.setf(x,y,x+2*y);
    ImagePlus image=new ImagePlus("shape",ip);image.setGlobalCalibration(null);Calibration cal=image.getLocalCalibration();cal.setUnit("um");
    double[][] spacings={{1,1},{2,2},{2,.5},{1,1.009},{1,1.01}};cal.pixelWidth=spacings[calibration][0];cal.pixelHeight=spacings[calibration][1];
    Roi roi=null;String kind="whole_image",points="[]";boolean fitted=shape==13,traced=shape==14;
    if(shape==1||shape==2||shape==5||shape==6||shape==7||shape==8||shape==9||shape==10||shape==11) {
      boolean oval=shape==2||shape==6||shape==9;kind=oval?"ellipse":"rectangle";
      double[] box=shape==5||shape==6?new double[]{3.2,4.7,10.2,8.3}:shape==7?new double[]{-2.7,-1.2,5.1,4.4}:shape==8||shape==9?new double[]{60,60,4,4}:shape==10?new double[]{3,3,1,1}:shape==11?new double[]{3,3,1,14}:new double[]{3,3,10,8};
      roi=oval?new OvalRoi(box[0],box[1],box[2],box[3]):new Roi(box[0],box[1],box[2],box[3]);
      points="[["+box[0]+","+box[1]+"],["+(box[0]+box[2])+","+(box[1]+box[3])+"]]";
    } else if(shape>0) {
      float[] x=shape==4?new float[]{3,13,13,8,8,3}:shape==13?new float[]{3,13,13,3}:traced?new float[]{3.2f,13.8f,9.4f}:new float[]{3,13,9};
      float[] y=shape==4?new float[]{3,3,7,7,11,11}:shape==13?new float[]{3,3,13,13}:traced?new float[]{4.7f,3.1f,14.2f}:new float[]{3,3,11};
      if(shape==12){x=new float[]{3,4,5};y=new float[]{3,3,3};}
      kind=traced?"freehand":"polygon";roi=new PolygonRoi(x,y,x.length,traced?Roi.TRACED_ROI:Roi.POLYGON);
      StringBuilder b=new StringBuilder("[");for(int i=0;i<x.length;i++){if(i>0)b.append(',');b.append('[').append((double)x[i]).append(',').append((double)y[i]).append(']');}points=b.append(']').toString();
    }
    if(roi!=null)image.setRoi(roi);
    if(fitted){WindowManager.setTempCurrentImage(image);Macro.setOptions("");new Selection().run("spline");Macro.setOptions(null);WindowManager.setTempCurrentImage(null);roi=image.getRoi();}
    ResultsTable table=new ResultsTable();new Analyzer(image,Measurements.AREA|Measurements.PERIMETER|Measurements.ELLIPSE|Measurements.SHAPE_DESCRIPTORS,table).measure();
    String hull="null";
    if(roi!=null){Polygon h=roi.getConvexHull();if(h!=null){StringBuilder b=new StringBuilder("[");for(int i=0;i<h.npoints;i++){if(i>0)b.append(',');b.append('[').append(h.xpoints[i]).append(',').append(h.ypoints[i]).append(']');}hull=b.append(']').toString();}}
    StringBuilder out=new StringBuilder("{\"id\":\"shape"+shape+"-type"+type+"-cal"+calibration+"\",\"kind\":\""+kind+"\",\"points\":"+points+",\"type\":"+type+",\"spacing\":["+cal.pixelWidth+","+cal.pixelHeight+"],\"fitted\":"+fitted+",\"traced\":"+traced+",\"selection_retained\":"+(image.getRoi()!=null)+",\"hull\":"+hull+",\"rows\":[");
    for(int i=0;i<table.size();i++){if(i>0)out.append(',');out.append('{');boolean first=true;for(String heading:table.getHeadings()){if(!first)out.append(',');first=false;out.append('"').append(heading).append("\":").append(ObserveAreaCentroid.num(table.getValue(heading,i)));}out.append('}');}
    System.out.println(out.append("]}"));
  }
  public static void main(String[] args){Thread.currentThread().setName("Run$_imagejxx-shape");for(int shape=0;shape<15;shape++)for(int type=0;type<3;type++)for(int calibration=0;calibration<5;calibration++)observe(shape,type,calibration);}
}
