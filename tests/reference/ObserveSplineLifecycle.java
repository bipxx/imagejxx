import ij.*;
import ij.gui.*;
import ij.measure.*;
import ij.plugin.Selection;
import ij.plugin.filter.Analyzer;
import ij.process.*;
import java.io.*;

/** New original lifecycle compositions; shared formatting has no native inputs. */
public class ObserveSplineLifecycle {
  static void command(String options) {
    Macro.setOptions(options);IJ.getErrorMessage();IJ.redirectErrorMessages(true);
    new Selection().run("spline");
    if(IJ.getErrorMessage()!=null)throw new IllegalArgumentException("command refused");
  }
  static void observe(int shape,double[][] input,int type,boolean integer,String mode,double sx,double sy) {
    int n=input.length;int[] xi=new int[n],yi=new int[n];float[] xf=new float[n],yf=new float[n];
    for(int i=0;i<n;i++){xi[i]=(int)input[i][0];yi[i]=(int)input[i][1];xf[i]=(float)input[i][0];yf[i]=(float)input[i][1];}
    StringBuilder out=new StringBuilder("{\"id\":\"shape"+shape+"-type"+type+"-"+(integer?"int":"float")+"-"+mode+"-"+sx+"-"+sy+"\",\"type\":"+type+",\"integer\":"+integer+",\"mode\":\""+mode+"\",\"spacing\":["+sx+","+sy+"]");
    ImagePlus image=new ImagePlus("lifecycle",new ByteProcessor(512,512));
    image.setGlobalCalibration(null);Calibration cal=image.getLocalCalibration();cal.pixelWidth=sx;cal.pixelHeight=sy;cal.setUnit("um");
    PrintStream stdout=System.out;String stage="construct";
    try {
      PolygonRoi roi=integer?new PolygonRoi(xi,yi,n,type):new PolygonRoi(xf,yf,n,type);
      image.setRoi(roi);WindowManager.setTempCurrentImage(image);
      out.append(",\"input\":").append(ObserveFittedMeasurements.points(roi.getNonSplineFloatPolygon()));
      System.setOut(new PrintStream(new ByteArrayOutputStream()));
      stage="initial";
      if(mode.equals("straighten"))command("straighten");
      else {
        command("");stage="second";
        if(mode.equals("refit"))command("");
        else if(mode.equals("remove"))command("remove");
        else if(mode.equals("fit-straighten"))command("straighten");
        else if(mode.equals("refit7"))((PolygonRoi)image.getRoi()).fitSpline(7);
        else throw new IllegalArgumentException("unknown lifecycle");
      }
      roi=(PolygonRoi)image.getRoi();stage="result";
      FloatPolygon samples=roi.getFloatPolygon(),controls=roi.getNonSplineFloatPolygon();
      for(int i=0;i<samples.npoints;i++)if(!Float.isFinite(samples.xpoints[i]) || !Float.isFinite(samples.ypoints[i]))throw new IllegalStateException("nonfinite result");
      out.append(",\"result_type\":").append(roi.getType()).append(",\"fitted\":").append(roi.isSplineFit());
      out.append(",\"controls\":").append(ObserveFittedMeasurements.points(controls));
      out.append(",\"samples\":").append(ObserveFittedMeasurements.points(samples));
      out.append(",\"length\":").append(ObserveFittedMeasurements.number(roi.getLength()));
      ResultsTable table=new ResultsTable();new Analyzer(image,Measurements.AREA|Measurements.PERIMETER,table).measure();
      out.append(",\"rows\":[");
      for(int i=0;i<table.size();i++) {
        if(i>0)out.append(',');out.append('{');boolean first=true;
        for(String heading:table.getHeadings()) {
          if(!first)out.append(',');first=false;
          out.append('"').append(heading).append("\":").append(ObserveFittedMeasurements.number(table.getValue(heading,i)));
        }
        out.append('}');
      }
      out.append(']');
    } catch(Exception error) {
      out.append(",\"error\":\"").append(error.getClass().getName()).append("\",\"stage\":\"").append(stage).append('"');
    } finally {System.setOut(stdout);Macro.setOptions(null);WindowManager.setTempCurrentImage(null);}
    System.out.println(out.append('}'));
  }
  public static void main(String[] args) {
    Thread.currentThread().setName("Run$_imagejxx-spline-lifecycle");
    double[][][] shapes={{{3,3},{13,3},{13,13}},{{3,3},{13,3},{13,13},{3,13}},
      {{3.2,4.7},{13.8,3.1},{19.4,14.2},{11.1,20.6},{4.3,13.8}},{{3,3},{203,3},{403,203}}};
    for(int shape=0;shape<shapes.length;shape++)for(int type:new int[]{Roi.POLYGON,Roi.FREEROI,Roi.TRACED_ROI,Roi.POLYLINE,Roi.FREELINE}) {
      if(shape==3 && type!=Roi.POLYGON && type!=Roi.POLYLINE)continue;
      for(boolean integer:new boolean[]{false,true})for(String mode:new String[]{"refit","remove","straighten","fit-straighten","refit7"}) {
        if(mode.equals("refit7") && type!=Roi.POLYGON && type!=Roi.POLYLINE)continue;
        for(double[] spacing:new double[][]{{1,1},{2,.5}})observe(shape,shapes[shape],type,integer,mode,spacing[0],spacing[1]);
      }
    }
  }
}
