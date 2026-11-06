import ij.ImagePlus;
import ij.Macro;
import ij.WindowManager;
import ij.gui.PolygonRoi;
import ij.gui.Roi;
import ij.measure.Measurements;
import ij.measure.ResultsTable;
import ij.process.ByteProcessor;
import ij.process.FloatPolygon;

/** Actual original Interpolate command followed by original Analyzer. */
public class ObserveInterpolatedMeasurement {
  public static void main(String[] args) {
    Thread.currentThread().setName("Run$_imagejxx-interpolated-measurement");
    byte[] pixels=new byte[256];for(int i=0;i<256;i++)pixels[i]=(byte)i;
    ImagePlus image=new ImagePlus("composition",new ByteProcessor(16,16,pixels));
    image.setGlobalCalibration(null);
    image.setRoi(new PolygonRoi(new float[]{3,13,13},new float[]{3,3,13},3,Roi.POLYLINE));
    WindowManager.setTempCurrentImage(image);
    try {
      Macro.setOptions("interval=2.7 adjust");
      new ij.plugin.Selection().run("interpolate");
      Macro.setOptions(null);
      Roi result=image.getRoi();FloatPolygon points=result.getFloatPolygon();
      ResultsTable table=new ResultsTable();
      new ij.plugin.filter.Analyzer(image,Measurements.AREA|Measurements.MEAN|Measurements.PERIMETER,table).measure();
      StringBuilder out=new StringBuilder("{\"type\":"+result.getType()+",\"length\":"+result.getLength()+",\"samples\":[");
      for(int i=0;i<points.npoints;i++) {
        if(i>0)out.append(',');
        out.append('[').append((double)points.xpoints[i]).append(',').append((double)points.ypoints[i]).append(']');
      }
      out.append("],\"columns\":{");boolean first=true;
      for(String key:table.getHeadings()) {
        if(!first)out.append(',');first=false;
        out.append('"').append(key).append("\":").append(table.getValue(key,0));
      }
      System.out.println(out.append("}}"));
    } finally {Macro.setOptions(null);WindowManager.setTempCurrentImage(null);}
  }
}
