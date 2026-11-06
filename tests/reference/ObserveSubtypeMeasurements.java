import ij.ImagePlus;
import ij.gui.PolygonRoi;
import ij.gui.Roi;
import ij.measure.Calibration;
import ij.measure.Measurements;
import ij.measure.ResultsTable;
import ij.plugin.filter.Analyzer;
import ij.process.ByteProcessor;

/** Original pinned ImageJ calls; no imagejxx input or output is loaded. */
public class ObserveSubtypeMeasurements {
  static String number(double x) {
    return Double.isNaN(x) ? "\"nan\"" : Double.isInfinite(x) ? "\"inf\"" : Double.toString(x);
  }
  static void observe(int shape, double[][] points, int type, boolean integers, double sx, double sy) {
    StringBuilder row = new StringBuilder("{\"id\":\"shape"+shape+"-type"+type+"-"+(integers?"integer":"float")+"-"+sx+"-"+sy+"\",\"type\":"+type+",\"integer\":"+integers+",\"spacing\":["+sx+","+sy+"],\"points\":[");
    int n=points.length;
    int[] xi=new int[n],yi=new int[n];float[] xf=new float[n],yf=new float[n];
    for(int i=0;i<n;i++) {
      xi[i]=(int)points[i][0];yi[i]=(int)points[i][1];
      xf[i]=(float)points[i][0];yf[i]=(float)points[i][1];
      if(i>0)row.append(',');
      row.append('[').append(integers?xi[i]:xf[i]).append(',').append(integers?yi[i]:yf[i]).append(']');
    }
    row.append(']');
    try {
      byte[] pixels=new byte[40*40];for(int y=0;y<40;y++)for(int x=0;x<40;x++)pixels[y*40+x]=(byte)(x+2*y);
      ImagePlus image=new ImagePlus("subtype",new ByteProcessor(40,40,pixels));
      image.setGlobalCalibration(null);Calibration cal=image.getLocalCalibration();cal.pixelWidth=sx;cal.pixelHeight=sy;cal.setUnit("um");
      PolygonRoi roi=integers?new PolygonRoi(xi,yi,n,type):new PolygonRoi(xf,yf,n,type);
      image.setRoi(roi);roi.setStrokeWidth(1);
      row.append(",\"selection_retained\":").append(image.getRoi()!=null);
      row.append(",\"length\":").append(number(roi.getLength()));
      ResultsTable table=new ResultsTable();
      new Analyzer(image,Measurements.AREA|Measurements.MEAN|Measurements.PERIMETER|Measurements.INTEGRATED_DENSITY,table).measure();
      row.append(",\"rows\":[");
      for(int i=0;i<table.size();i++) {
        if(i>0)row.append(',');row.append('{');boolean first=true;
        for(String heading:table.getHeadings()) {
          if(!first)row.append(',');first=false;
          row.append('"').append(heading).append("\":").append(number(table.getValue(heading,i)));
        }
        row.append('}');
      }
      row.append(']');
    } catch(Exception error) {
      row.append(",\"error\":\"").append(error.getClass().getName()).append('"');
    }
    System.out.println(row.append('}'));
  }
  public static void main(String[] args) {
    double[][][] shapes={
      {{3,3},{13,3},{13,13},{3,13}},
      {{3.2,4.7},{13.8,3.1},{19.4,14.2},{11.1,20.6},{4.3,13.8}},
      {{3,3},{4,3},{4,4},{3,4}},
      {{3,3},{7,3},{11,3},{15,3}},
      {{3,3},{13,13}},
      {{3,3}},
      {{3,3},{3,3},{3,3}},
      {{3,3},{10,3},{10,7},{7,7},{7,11},{3,11}},
      {{3,3},{13,13},{3,13},{13,3}}
    };
    for(int s=0;s<shapes.length;s++)for(int type:new int[]{Roi.POLYGON,Roi.FREEROI,Roi.TRACED_ROI,Roi.POLYLINE,Roi.FREELINE})
      for(boolean integers:new boolean[]{false,true})for(double[] spacing:new double[][]{{1,1},{2,.5}})
        observe(s,shapes[s],type,integers,spacing[0],spacing[1]);
  }
}
