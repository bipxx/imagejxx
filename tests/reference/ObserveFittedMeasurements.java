import ij.*;
import ij.gui.*;
import ij.measure.*;
import ij.plugin.Selection;
import ij.plugin.filter.Analyzer;
import ij.process.*;
import java.io.*;

/** Observe original constructor -> Fit Spline -> Analyzer composition only. */
public class ObserveFittedMeasurements {
  static String number(double value) {
    return Double.isNaN(value)?"\"nan\"":Double.isInfinite(value)?"\"inf\"":Double.toString(value);
  }
  static String points(FloatPolygon polygon) {
    StringBuilder out=new StringBuilder("[");
    for(int i=0;i<polygon.npoints;i++) {
      if(i>0)out.append(',');
      out.append('[').append((double)polygon.xpoints[i]).append(',').append((double)polygon.ypoints[i]).append(']');
    }
    return out.append(']').toString();
  }
  static void observe(int shape,double[][] input,int type,boolean integer,boolean command,double sx,double sy) {
    int n=input.length;int[] xi=new int[n],yi=new int[n];float[] xf=new float[n],yf=new float[n];
    for(int i=0;i<n;i++){xi[i]=(int)input[i][0];yi[i]=(int)input[i][1];xf[i]=(float)input[i][0];yf[i]=(float)input[i][1];}
    StringBuilder out=new StringBuilder("{\"id\":\"shape"+shape+"-type"+type+"-"+(integer?"int":"float")+"-"+(command?"command":"explicit7")+"-"+sx+"-"+sy+"\",\"type\":"+type+",\"integer\":"+integer+",\"command\":"+command+",\"spacing\":["+sx+","+sy+"]");
    ImagePlus image=new ImagePlus("fitted",new ByteProcessor(40,40));
    image.setGlobalCalibration(null);Calibration cal=image.getLocalCalibration();cal.pixelWidth=sx;cal.pixelHeight=sy;cal.setUnit("um");
    PrintStream stdout=System.out;
    try {
      PolygonRoi roi=integer?new PolygonRoi(xi,yi,n,type):new PolygonRoi(xf,yf,n,type);
      image.setRoi(roi);WindowManager.setTempCurrentImage(image);
      out.append(",\"input\":").append(points(roi.getNonSplineFloatPolygon()));
      out.append(",\"before_length\":").append(number(roi.getLength()));
      IJ.getErrorMessage();IJ.redirectErrorMessages(true);Macro.setOptions("");
      System.setOut(new PrintStream(new ByteArrayOutputStream()));
      if(command)new Selection().run("spline");else roi.fitSpline(7);
      System.setOut(stdout);
      String error=IJ.getErrorMessage();
      if(error!=null)throw new IllegalArgumentException("command refused");
      roi=(PolygonRoi)image.getRoi();
      out.append(",\"result_type\":").append(roi.getType()).append(",\"fitted\":").append(roi.isSplineFit());
      out.append(",\"controls\":").append(points(roi.getNonSplineFloatPolygon()));
      out.append(",\"samples\":").append(points(roi.getFloatPolygon()));
      out.append(",\"length\":").append(number(roi.getLength()));
      ResultsTable table=new ResultsTable();
      new Analyzer(image,Measurements.AREA|Measurements.MEAN|Measurements.PERIMETER,table).measure();
      out.append(",\"rows\":[");
      for(int i=0;i<table.size();i++) {
        if(i>0)out.append(',');out.append('{');boolean first=true;
        for(String heading:table.getHeadings()) {
          if(!first)out.append(',');first=false;
          out.append('"').append(heading).append("\":").append(number(table.getValue(heading,i)));
        }
        out.append('}');
      }
      out.append(']');
    } catch(Exception error) {
      out.append(",\"error\":\"").append(error.getClass().getName()).append('"');
    } finally {System.setOut(stdout);Macro.setOptions(null);WindowManager.setTempCurrentImage(null);}
    System.out.println(out.append('}'));
  }
  public static void main(String[] args) {
    Thread.currentThread().setName("Run$_imagejxx-fitted-measurement");
    double[][][] shapes={{{3,3},{13,13}},{{3,3},{13,3},{13,13}},
      {{3,3},{13,3},{13,13},{3,13}},{{3.2,4.7},{13.8,3.1},{19.4,14.2},{11.1,20.6},{4.3,13.8}},
      {{3,3},{7,3},{11,3},{15,3}}};
    for(int shape=0;shape<shapes.length;shape++)for(int type:new int[]{Roi.POLYGON,Roi.FREEROI,Roi.TRACED_ROI,Roi.POLYLINE,Roi.FREELINE})
      for(boolean integer:new boolean[]{false,true})for(boolean command:new boolean[]{false,true})
        for(double[] spacing:new double[][]{{1,1},{2,.5}})observe(shape,shapes[shape],type,integer,command,spacing[0],spacing[1]);
  }
}
