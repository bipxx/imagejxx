import ij.ImagePlus;
import ij.gui.Line;
import ij.gui.PolygonRoi;
import ij.gui.Roi;
import ij.measure.Calibration;
import ij.measure.Measurements;
import ij.measure.ResultsTable;
import ij.plugin.filter.Analyzer;
import ij.process.ByteProcessor;
import ij.process.ShortProcessor;
import ij.process.FloatProcessor;

/** Original ImageJ calls only; never imports or uses imagejxx output. */
public class ObserveSelectionMeasurements {
  static String inputType="uint8";
  static boolean geometry=false,interpolate=true,reflex=false;
  static boolean pointOrigin=false;
  static String number(double value) {
    if (Double.isNaN(value)) return "\"nan\"";
    if (Double.isInfinite(value)) return value > 0 ? "\"inf\"" : "\"-inf\"";
    return Double.toString(value);
  }
  static void observe(String kind, double[] xy, double sx, double sy, String density) {
    byte[] pixels = new byte[35];
    for (int y=0;y<5;y++) for(int x=0;x<7;x++) pixels[y*7+x]=(byte)(x+10*y);
    ImagePlus image;
    if(inputType.equals("uint16")) {
      short[] values=new short[35];for(int i=0;i<35;i++)values[i]=(short)((pixels[i]&255)*257);
      image=new ImagePlus("selection measurement",new ShortProcessor(7,5,values,null));
    } else if(inputType.equals("float32")) {
      float[] values=new float[35];for(int i=0;i<35;i++)values[i]=(pixels[i]&255)*.25f-8;
      image=new ImagePlus("selection measurement",new FloatProcessor(7,5,values));
    } else image=new ImagePlus("selection measurement",new ByteProcessor(7,5,pixels));
    image.setGlobalCalibration(null);
    Calibration cal=image.getLocalCalibration();cal.pixelWidth=sx;cal.pixelHeight=sy;cal.setUnit("um");
    boolean point=kind.equals("point") || kind.equals("multipoint");
    if(pointOrigin){cal.xOrigin=-11/sx;cal.yOrigin=7/sy;}
    if (!density.equals("raw")) cal.setFunction(density.equals("linear")?Calibration.STRAIGHT_LINE:Calibration.POLY2,
      density.equals("linear")?new double[]{10,2}:new double[]{0,0,1},"value");
    Roi roi;
    if(kind.equals("line")) roi=new Line(xy[0],xy[1],xy[2],xy[3]);
    else {
      float[] x=new float[xy.length/2],y=new float[x.length];
      for(int i=0;i<x.length;i++){x[i]=(float)xy[2*i];y[i]=(float)xy[2*i+1];}
      roi=point?new ij.gui.PointRoi(x,y,x.length):new PolygonRoi(x,y,x.length,kind.equals("angle")?Roi.ANGLE:Roi.POLYLINE);
    }
    image.setRoi(roi);roi.setStrokeWidth(1);
    ij.gui.PlotWindow.interpolate=interpolate;ij.Prefs.reflexAngle=reflex;
    if(geometry && kind.equals("angle")) {
      try {java.lang.reflect.Method refresh=PolygonRoi.class.getDeclaredMethod("getAngleAsString");
        refresh.setAccessible(true);refresh.invoke(roi);
      } catch(Exception error) {throw new RuntimeException(error);}
    }
    ResultsTable table=new ResultsTable();
    Analyzer analyzer=new Analyzer(image,Measurements.AREA|Measurements.MEAN|Measurements.STD_DEV|
      Measurements.MIN_MAX|Measurements.MEDIAN|Measurements.CENTROID|Measurements.INTEGRATED_DENSITY|
      (geometry || point?Measurements.RECT:0),table);
    String failure=null;
    try {analyzer.measure();}catch(RuntimeException error){if(!geometry)throw error;failure=error.getClass().getName();}
    StringBuilder row=new StringBuilder("{\"kind\":\"").append(kind).append("\",\"spacing\":[")
      .append(sx).append(',').append(sy).append("],\"density\":\"").append(density).append("\",\"points\":[");
    for(int i=0;i<xy.length;i++){if(i>0)row.append(',');row.append(xy[i]);}
    row.append(']');
    if(!inputType.equals("uint8"))row.append(",\"type\":\"").append(inputType).append('"');
    if(point) {
      row.append(",\"origin\":[").append(pointOrigin?11:0).append(',').append(pointOrigin?-7:0).append(']');
      row.append(",\"rows\":[");
      for(int index=0;index<table.size();index++) {
        if(index>0)row.append(',');row.append('{');boolean first=true;
        for(String heading:table.getHeadings()) {
          if(!first)row.append(',');first=false;
          row.append('"').append(heading).append("\":").append(number(table.getValue(heading,index)));
        }
        row.append('}');
      }
      System.out.println(row.append("]}"));return;
    }
    if(geometry) {
      java.awt.Rectangle bounds=roi.getBounds();
      row.append(",\"bounds\":[").append(bounds.x).append(',').append(bounds.y).append(',').append(bounds.width).append(',').append(bounds.height).append(']');
      row.append(",\"interpolate\":").append(interpolate).append(",\"reflex\":").append(reflex);
      if(kind.equals("angle"))row.append(",\"angle_policy\":\"calibrated_refresh\"");
      if(failure!=null)row.append(",\"error\":\"").append(failure).append('"');
      if(!kind.equals("angle") && failure==null) {
        double[] profile=new ij.gui.ProfilePlot(image).getProfile();row.append(",\"profile\":[");
        for(int i=0;i<profile.length;i++){if(i>0)row.append(',');row.append(number(profile[i]));}row.append(']');
      }
    }
    row.append(",\"roi_length\":").append(number(roi.getLength())).append(",\"columns\":{");
    boolean first=true;
    for(String heading:failure==null?table.getHeadings():new String[0]) {
      if(!first)row.append(',');first=false;
      row.append('"').append(heading).append("\":").append(number(table.getValue(heading,0)));
    }
    System.out.println(row.append("}}"));
  }
  static void geometryCases() {
    Analyzer.setRedirectImage(null);
    double[][] paths={{0,0,2,0,2,2},{.25,.5,3.25,2.5,5.5,1.25},{2,2,2,2},
      {0,0,0,0},{0,0,2,2,2,2,4,1},{5.5,3.25,1.25,.5,.25,1.5},{2,1,4,1}};
    for(double[] spacing:new double[][]{{1,1},{.5,2}}) {
      for(String density:inputType.equals("float32")?new String[]{"raw"}:new String[]{"raw","linear","square"})
        for(boolean sampling:new boolean[]{false,true}) {
          interpolate=sampling;reflex=false;
          for(double[] path:paths)observe("polyline",path,spacing[0],spacing[1],density);
        }
      if(inputType.equals("uint8")) {
        for(boolean option:new boolean[]{false,true}) {reflex=option;
          for(double[] points:new double[][]{{0,0,2,1,4,0},{0,0,0,0,2,1},{0,0,1,0,2,0},{2,2,2,2,2,2}})
            observe("angle",points,spacing[0],spacing[1],"raw");
        }
        reflex=false;
        for(double[] points:new double[][]{{0,0,4,2},{.25,.5,5.5,3.25},{2,2,2,2},{4,2,0,0}})
          observe("line",points,spacing[0],spacing[1],"raw");
      }
    }
  }
  static void annotationCases() {
    int[][] counters={{0,0,0,0},{0,2,2,99},{2,2,2,2},{2},{0}};
    int[][] positions={{0,0,0,0},{0,1,4,8},{8,7,2,0},{8},{0}};
    for(double[] spacing:new double[][]{{1,1},{.5,2}})
      for(String density:inputType.equals("float32")?new String[]{"raw"}:new String[]{"raw","linear","square"})
        for(int test=0;test<counters.length;test++) {
          ij.ImageStack stack=new ij.ImageStack(7,5);
          for(int index=0;index<8;index++) {
            int base=80*(index/4)+30*(index%2)+10*((index/2)%2);
            if(inputType.equals("uint16")) {
              short[] values=new short[35];for(int y=0;y<5;y++)for(int x=0;x<7;x++)values[y*7+x]=(short)((base+x+10*y)*257);
              stack.addSlice(new ShortProcessor(7,5,values,null));
            } else if(inputType.equals("float32")) {
              float[] values=new float[35];for(int y=0;y<5;y++)for(int x=0;x<7;x++)values[y*7+x]=(base+x+10*y)*.25f-8;
              stack.addSlice(new FloatProcessor(7,5,values));
            } else {
              byte[] values=new byte[35];for(int y=0;y<5;y++)for(int x=0;x<7;x++)values[y*7+x]=(byte)(base+x+10*y);
              stack.addSlice(new ByteProcessor(7,5,values));
            }
          }
          ImagePlus image=new ImagePlus("point annotations",stack);image.setDimensions(2,2,2);image.setOpenAsHyperStack(true);
          image.setGlobalCalibration(null);Calibration cal=image.getLocalCalibration();
          cal.pixelWidth=spacing[0];cal.pixelHeight=spacing[1];cal.pixelDepth=3;cal.xOrigin=-11/spacing[0];cal.yOrigin=7/spacing[1];cal.setUnit("um");
          if(!density.equals("raw"))cal.setFunction(density.equals("linear")?Calibration.STRAIGHT_LINE:Calibration.POLY2,
            density.equals("linear")?new double[]{10,2}:new double[]{0,0,1},"value");
          image.setPosition(2,1,2);
          int n=counters[test].length;float[] x=new float[n],y=new float[n];int[] words=new int[n];
          for(int i=0;i<n;i++){x[i]=new float[]{0,2.25f,1,2.25f}[i];y[i]=new float[]{0,1.75f,1,1.75f}[i];words[i]=(positions[test][i]<<8)|counters[test][i];}
          ij.gui.PointRoi roi=new ij.gui.PointRoi(x,y,n);image.setRoi(roi);roi.setCounters(words);
          ResultsTable table=new ResultsTable();new Analyzer(image,Measurements.AREA|Measurements.MEAN|Measurements.STD_DEV|
            Measurements.MIN_MAX|Measurements.MEDIAN|Measurements.CENTROID|Measurements.INTEGRATED_DENSITY|Measurements.STACK_POSITION,table).measure();
          StringBuilder row=new StringBuilder("{\"type\":\"").append(inputType).append("\",\"density\":\"").append(density)
            .append("\",\"spacing\":[").append(spacing[0]).append(',').append(spacing[1]).append("],\"points\":[");
          for(int i=0;i<n;i++){if(i>0)row.append(',');row.append('[').append(x[i]).append(',').append(y[i]).append(']');}
          row.append("],\"words\":[");for(int i=0;i<n;i++){if(i>0)row.append(',');row.append(words[i]);}
          row.append("],\"rows\":[");
          for(int i=0;i<table.size();i++) {
            if(i>0)row.append(',');row.append('{');boolean first=true;
            for(String heading:table.getHeadings()){if(!first)row.append(',');first=false;row.append('"').append(heading).append("\":").append(number(table.getValue(heading,i)));}
            row.append('}');
          }
          System.out.println(row.append("]}"));
        }
  }
  public static void main(String[] args) {
    if(args.length>0)inputType=args[0];
    if(!inputType.equals("uint8") && !inputType.equals("uint16") && !inputType.equals("float32"))throw new IllegalArgumentException("Unknown input type");
    if(args.length>1 && args[1].equals("annotations")){annotationCases();return;}
    if(args.length>1 && args[1].equals("points")) {
      Analyzer.setRedirectImage(null);
      for(boolean offset:new boolean[]{false,true})for(double[] spacing:new double[][]{{1,1},{.5,2}})
        for(String density:inputType.equals("float32")?new String[]{"raw"}:new String[]{"raw","linear","square"}) {
          pointOrigin=offset;
          for(double[] xy:new double[][]{{0,0},{6,4},{.49,.49},{.5,.5},{2.25,1.75},{6.49,4.49},
              {6.5,4.5},{-.25,-.25},{-.6,1},{8,9}})observe("point",xy,spacing[0],spacing[1],density);
          observe("multipoint",new double[]{2.25,1.75,0,0,2.25,1.75,6,4},spacing[0],spacing[1],density);
        }
      return;
    }
    geometry=args.length>1 && args[1].equals("geometry");
    if(geometry){geometryCases();return;}
    Analyzer.setRedirectImage(null);
    ij.Prefs.reflexAngle=false;
    for(double[] spacing:new double[][]{{1,1},{.5,2}}) {
      for(String density:inputType.equals("float32")?new String[]{"raw"}:new String[]{"raw","linear","square"}) {
        observe("line",new double[]{0,0,4,2},spacing[0],spacing[1],density);
        observe("line",new double[]{.25,.5,5.5,3.25},spacing[0],spacing[1],density);
        if(inputType.equals("uint8"))observe("polyline",new double[]{0,0,2,0,2,2},spacing[0],spacing[1],density);
        if(inputType.equals("uint8"))observe("angle",new double[]{0,0,2,1,4,0},spacing[0],spacing[1],density);
      }
      for(double[] xy:new double[][]{{2,2,2,2},{4,2,0,0},{2,0,2,4}})
        observe("line",xy,spacing[0],spacing[1],"raw");
      for(double[] xy:new double[][]{{0,0,0,0,2,1},{0,0,1,0,2,0},{0,0,1,0,0,0}})
        if(inputType.equals("uint8"))observe("angle",xy,spacing[0],spacing[1],"raw");
    }
  }
}
