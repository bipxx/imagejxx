import ij.ImagePlus;
import ij.gui.*;
import ij.measure.*;
import ij.plugin.filter.Analyzer;
import ij.process.ByteProcessor;

/** Original Analyzer coordinate outputs, independently observed before native edits. */
public class ObserveInvertY {
  static boolean first=true;
  static String number(double v) {
    return Double.isFinite(v)?Double.toString(v):"\""+(Double.isNaN(v)?"nan":v>0?"inf":"-inf")+"\"";
  }
  static ImagePlus image(String name,int height,int calibration,boolean localInvert,int offset) {
    ByteProcessor ip=new ByteProcessor(8,height);
    for(int y=0;y<height;y++)for(int x=0;x<8;x++)ip.set(x,y,x+2*y+1+offset);
    ImagePlus image=new ImagePlus(name,ip);image.setGlobalCalibration(null);
    Calibration c=image.getLocalCalibration();c.setUnit("um");
    if(calibration==1){c.pixelWidth=2;c.pixelHeight=.5;c.xOrigin=3.25;c.yOrigin=-1.5;}
    if(calibration==2){c.pixelWidth=.75;c.pixelHeight=2;c.xOrigin=0;c.yOrigin=4.25;}
    if(calibration==3){c.pixelHeight=2;c.yOrigin=1.25;}
    c.setInvertY(localInvert);return image;
  }
  static void observe(int kind,int calibration,boolean localInvert,boolean invert,int mode,boolean fitted) {
    ImagePlus source=image("source",6,calibration,localInvert,0);
    ImagePlus target=image("target",mode==3?9:6,(calibration+1)%3,!localInvert,20);
    Roi roi=null;String name="whole_image",points="[]";
    if(kind==1||kind==2){name=kind==1?"rectangle":"ellipse";roi=kind==1?new Roi(1,1,6,4):new OvalRoi(1,1,6,4);points="[[1,1],[7,5]]";}
    if(kind==3){name="polygon";roi=new PolygonRoi(new float[]{1,7,3},new float[]{1,1,5},3,Roi.POLYGON);points="[[1,1],[7,1],[3,5]]";}
    if(kind==4){name="point";roi=new PointRoi(2.5,1.25);points="[[2.5,1.25]]";}
    if(kind==5){name="multipoint";roi=new PointRoi(new float[]{1.25f,5.5f},new float[]{1.5f,3.25f},2);points="[[1.25,1.5],[5.5,3.25]]";}
    if(kind==6){name="line";roi=new Line(1.25,1.5,5.5,3.25);points="[[1.25,1.5],[5.5,3.25]]";}
    if(kind==7){name="polyline";roi=new PolygonRoi(new float[]{1,5,6},new float[]{1,3,2},3,Roi.POLYLINE);points="[[1,1],[5,3],[6,2]]";}
    if(kind==8){name="angle";roi=new PolygonRoi(new float[]{1,5,6},new float[]{1,3,2},3,Roi.ANGLE);points="[[1,1],[5,3],[6,2]]";}
    if(roi!=null)source.setRoi(roi);
    int flags=Measurements.AREA|Measurements.MEAN|Measurements.CENTROID|Measurements.CENTER_OF_MASS|Measurements.RECT|Measurements.FERET|Measurements.PERIMETER;
    if(invert)flags|=Measurements.INVERT_Y;if(fitted)flags|=Measurements.ELLIPSE;
    Analyzer.setMeasurements(flags);Analyzer.setRedirectImage(mode==0?null:mode==1?source:target);
    ResultsTable table=new ResultsTable();
    java.io.ByteArrayOutputStream diagnostic=new java.io.ByteArrayOutputStream();
    java.io.PrintStream stdout=System.out;
    try {System.setOut(new java.io.PrintStream(diagnostic));new Analyzer(source,flags,table).measure();}
    finally {System.setOut(stdout);}
    if(table.size()==0 && mode!=3)throw new IllegalStateException("Unexpected original refusal");
    if(!first)System.out.print(',');first=false;
    StringBuilder out=new StringBuilder("{\"id\":\"invert-kind"+kind+"-cal"+calibration+"-local"+localInvert+"-flag"+invert+"-mode"+mode+"-fit"+fitted+"\",\"kind\":\""+name+"\",\"points\":"+points+",\"calibration\":"+calibration+",\"local_invert\":"+localInvert+",\"invert\":"+invert+",\"mode\":"+mode+",\"fitted\":"+fitted+",\"source_height\":6,\"target_height\":"+(mode==3?9:6)+",\"rows\":[");
    for(int row=0;row<table.size();row++) {
      if(row>0)out.append(',');out.append('{');boolean separator=false;
      for(String column:table.getHeadings()) {
        if(separator)out.append(',');separator=true;
        out.append('"').append(column).append("\":").append(number(table.getValue(column,row)));
      }
      out.append('}');
    }
    System.out.print(out.append("],\"refused\":").append(table.size()==0).append(",\"diagnostic\":\"").append(diagnostic.toString().replace("\\","\\\\").replace("\"","\\\"").replace("\r","\\r").replace("\n","\\n")).append("\"}"));Analyzer.setRedirectImage(null);
  }
  public static void main(String[] args) {
    System.out.print("{\"schema\":\"imagejxx.original-invert-y.001\",\"reference\":\"ImageJ1.54r\",\"cases\":[");
    for(int kind=0;kind<9;kind++)for(int c=0;c<3;c++)for(boolean local:new boolean[]{false,true})for(boolean invert:new boolean[]{false,true})for(int mode=0;mode<4;mode++)for(boolean fit:new boolean[]{false,true})observe(kind,c,local,invert,mode,fit);
    observe(4,3,false,true,0,false); // Exact nonzero-origin coordinate: preserve the original zero sign.
    System.out.println("]}");
  }
}
