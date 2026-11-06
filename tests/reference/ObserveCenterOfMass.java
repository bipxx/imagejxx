import ij.ImagePlus;
import ij.gui.*;
import ij.measure.*;
import ij.plugin.filter.Analyzer;
import ij.process.*;

/** Bounded original XM/YM arithmetic; formatter reused from prior observer. */
public class ObserveCenterOfMass {
  static void observe(int shape,int type,String mode,int pattern,int calibration) {
    ImageProcessor ip=type==0?new ByteProcessor(8,6):type==1?new ShortProcessor(8,6):new FloatProcessor(8,6);
    for(int y=0;y<6;y++)for(int x=0;x<8;x++) {
      float v=pattern==0?x+2*y+1:pattern==1?0:pattern==2?(x==5&&y==3?4:0):type==2?2*(x%2)-1:x%2;
      ip.setf(x,y,v);
    }
    ImagePlus image=new ImagePlus("mass",ip);image.setGlobalCalibration(null);Calibration cal=image.getLocalCalibration();cal.setUnit("um");
    if(calibration>0){cal.pixelWidth=2;cal.pixelHeight=.5;cal.xOrigin=3.25;cal.yOrigin=-1.5;}
    if(calibration==2)cal.setInvertY(true);
    if(mode.equals("linear"))cal.setFunction(Calibration.STRAIGHT_LINE,new double[]{10,2},"OD");
    if(mode.equals("cancel"))cal.setFunction(Calibration.STRAIGHT_LINE,new double[]{-1,2},"OD");
    if(mode.equals("signed16"))cal.setSigned16BitCalibration();
    if(mode.equals("nan")||mode.equals("infinite")) {
      float[] table=new float[type==0?256:65536];for(int i=0;i<table.length;i++)table[i]=i;
      table[0]=mode.equals("nan")?Float.NaN:Float.POSITIVE_INFINITY;cal.setCTable(table,"table");
    }
    String kind="whole_image";String points="[]";
    if(shape==1){kind="rectangle";points="[[1,1],[7,5]]";image.setRoi(new Roi(1,1,6,4));}
    if(shape==2){kind="ellipse";points="[[1,1],[7,5]]";image.setRoi(new OvalRoi(1,1,6,4));}
    if(shape==3){kind="polygon";points="[[1,1],[7,1],[3,5]]";image.setRoi(new PolygonRoi(new float[]{1,7,3},new float[]{1,1,5},3,Roi.POLYGON));}
    if(shape==4){kind="rectangle";points="[[20,20],[22,22]]";image.setRoi(new Roi(20,20,2,2));}
    ResultsTable table=new ResultsTable();new Analyzer(image,Measurements.AREA|Measurements.CENTROID|Measurements.CENTER_OF_MASS,table).measure();
    StringBuilder out=new StringBuilder("{\"id\":\"shape"+shape+"-type"+type+"-"+mode+"-pattern"+pattern+"-cal"+calibration+"\",\"kind\":\""+kind+"\",\"points\":"+points+",\"type\":"+type+",\"mode\":\""+mode+"\",\"pattern\":"+pattern+",\"spacing\":["+cal.pixelWidth+","+cal.pixelHeight+"],\"origins\":["+cal.xOrigin+","+cal.yOrigin+"],\"invert_y\":"+(calibration==2)+",\"rows\":[");
    for(int i=0;i<table.size();i++) {
      if(i>0)out.append(',');out.append('{');boolean first=true;
      for(String heading:table.getHeadings()){if(!first)out.append(',');first=false;out.append('"').append(heading).append("\":").append(ObserveAreaCentroid.num(table.getValue(heading,i)));}
      out.append('}');
    }
    System.out.println(out.append("]}"));
  }
  public static void main(String[] args) {
    for(int type=0;type<3;type++)for(String mode:type==2?new String[]{"raw"}:type==0?new String[]{"raw","linear","cancel","nan","infinite"}:new String[]{"raw","linear","cancel","nan","infinite","signed16"})
      for(int shape=0;shape<5;shape++)for(int pattern=0;pattern<4;pattern++)for(int calibration=0;calibration<3;calibration++)observe(shape,type,mode,pattern,calibration);
  }
}
