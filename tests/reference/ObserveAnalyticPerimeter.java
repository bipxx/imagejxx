import ij.ImagePlus;
import ij.gui.Roi;
import ij.gui.OvalRoi;
import ij.measure.*;
import ij.plugin.filter.Analyzer;
import ij.process.ByteProcessor;
import java.awt.Rectangle;
import java.awt.geom.Rectangle2D;

/** Original analytic ROI constructors/getLength/Analyzer, with bounded mask inputs. */
public class ObserveAnalyticPerimeter {
  static void observe(int shape,double[] box,String kind,boolean integer,double sx,double sy) {
    String id="shape"+shape+"-"+kind+"-"+(integer?"int":"double")+"-"+sx+"-"+sy;
    ImagePlus image=new ImagePlus("analytic",new ByteProcessor(40,40));
    image.setGlobalCalibration(null);Calibration cal=image.getLocalCalibration();
    cal.pixelWidth=sx;cal.pixelHeight=sy;cal.setUnit("um");
    boolean whole=kind.equals("whole_image"),oval=kind.equals("ellipse");
    Roi roi;
    if(whole) {roi=new Roi(0,0,40,40);roi.setImage(image);}
    else if(integer)roi=oval?new OvalRoi((int)box[0],(int)box[1],(int)box[2],(int)box[3]):new Roi((int)box[0],(int)box[1],(int)box[2],(int)box[3]);
    else roi=oval?new OvalRoi(box[0],box[1],box[2],box[3]):new Roi(box[0],box[1],box[2],box[3]);
    if(!whole)image.setRoi(roi);
    Rectangle2D.Double bounds=roi.getFloatBounds();Rectangle ints=roi.getBounds();
    StringBuilder out=new StringBuilder("{\"id\":\""+id+"\",\"kind\":\""+kind+"\",\"integer\":"+integer+",\"spacing\":["+sx+","+sy+"],\"geometry_bounds\":["+bounds.x+","+bounds.y+","+bounds.width+","+bounds.height+"],\"integer_bounds\":["+ints.x+","+ints.y+","+ints.width+","+ints.height+"],\"positive_extent\":"+(bounds.width>0 && bounds.height>0)+",\"selection_retained\":"+(image.getRoi()!=null));
    out.append(",\"length\":").append(ObserveFittedMeasurements.number(roi.getLength()));
    // Oval.getMask would allocate its complete integer bounding extent. Keep
    // the huge-extent scalar observation without launching that unrelated work.
    if(oval && (long)ints.width*ints.height>65536)out.append(",\"analyzer_unavailable\":\"mask extent exceeds bounded reference allocation\"");
    else {
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
    }
    System.out.println(out.append('}'));
  }
  public static void main(String[] args) {
    double[][] boxes={{3,3,10,8},{3.2,4.7,10.2,8.3},{-2.7,-1.2,5.1,4.4},
      {12.3,12.9,.2,.8},{3,3,1,1},{3,3,1,14},{3,3,14,1},
      {3.2,4.7,10.000000000000002,8.000000000000002},{0,0,40,40},
      {60.1,70.2,10.4,12.3},{100000000.1,100000000.2,.2,.8},
      {0,0,2147483747.0,1},{3,3,0,8},{3,3,10,0}};
    double[][] spacings={{1,1},{2,.5},{.25,3}};
    for(int shape=0;shape<boxes.length;shape++)for(String kind:new String[]{"rectangle","ellipse"})
      for(boolean integer:new boolean[]{false,true})for(double[] spacing:spacings)
        observe(shape,boxes[shape],kind,integer,spacing[0],spacing[1]);
    for(double[] spacing:spacings)observe(14,new double[]{0,0,40,40},"whole_image",false,spacing[0],spacing[1]);
  }
}
