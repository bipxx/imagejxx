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
    if (!density.equals("raw")) cal.setFunction(density.equals("linear")?Calibration.STRAIGHT_LINE:Calibration.POLY2,
      density.equals("linear")?new double[]{10,2}:new double[]{0,0,1},"value");
    Roi roi;
    if(kind.equals("line")) roi=new Line(xy[0],xy[1],xy[2],xy[3]);
    else {
      float[] x=new float[xy.length/2],y=new float[x.length];
      for(int i=0;i<x.length;i++){x[i]=(float)xy[2*i];y[i]=(float)xy[2*i+1];}
      roi=new PolygonRoi(x,y,x.length,kind.equals("angle")?Roi.ANGLE:Roi.POLYLINE);
    }
    image.setRoi(roi);roi.setStrokeWidth(1);
    ResultsTable table=new ResultsTable();
    Analyzer analyzer=new Analyzer(image,Measurements.AREA|Measurements.MEAN|Measurements.STD_DEV|
      Measurements.MIN_MAX|Measurements.MEDIAN|Measurements.CENTROID|Measurements.INTEGRATED_DENSITY,table);
    analyzer.measure();
    StringBuilder row=new StringBuilder("{\"kind\":\"").append(kind).append("\",\"spacing\":[")
      .append(sx).append(',').append(sy).append("],\"density\":\"").append(density).append("\",\"points\":[");
    for(int i=0;i<xy.length;i++){if(i>0)row.append(',');row.append(xy[i]);}
    row.append(']');
    if(!inputType.equals("uint8"))row.append(",\"type\":\"").append(inputType).append('"');
    row.append(",\"roi_length\":").append(number(roi.getLength())).append(",\"columns\":{");
    boolean first=true;
    for(String heading:table.getHeadings()) {
      if(!first)row.append(',');first=false;
      row.append('"').append(heading).append("\":").append(number(table.getValue(heading,0)));
    }
    System.out.println(row.append("}}"));
  }
  public static void main(String[] args) {
    if(args.length>0)inputType=args[0];
    if(!inputType.equals("uint8") && !inputType.equals("uint16") && !inputType.equals("float32"))throw new IllegalArgumentException("Unknown input type");
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
