import ij.ImagePlus;
import ij.gui.*;
import ij.measure.*;
import ij.plugin.filter.Analyzer;
import ij.process.*;

/** Missing ELLIPSE/implicit SHAPE centroid cases; existing225axes stay immutable. */
public class ObserveEllipseMeasurements {
  static void observe(int shape,int type,boolean shifted,boolean allNan,boolean implicit) {
    ImageProcessor ip=type==0?new ByteProcessor(40,40):type==1?new ShortProcessor(40,40):new FloatProcessor(40,40);
    for(int y=0;y<40;y++)for(int x=0;x<40;x++)ip.setf(x,y,x+2*y);
    ImagePlus image=new ImagePlus("ellipse",ip);image.setGlobalCalibration(null);Calibration cal=image.getLocalCalibration();cal.setUnit("um");
    if(shifted){cal.pixelWidth=2;cal.pixelHeight=.5;cal.xOrigin=7.25;cal.yOrigin=-3.5;cal.setInvertY(true);}
    if(allNan){float[] table=new float[type==0?256:65536];java.util.Arrays.fill(table,Float.NaN);cal.setCTable(table,"all_nan");}
    String kind="whole_image",points="[]";
    if(shape==1||shape==2||shape==6||shape==8) {
      boolean oval=shape==2||shape==6;kind=oval?"ellipse":"rectangle";
      double[] b=shape==1||shape==2?new double[]{60,60,4,4}:shape==6?new double[]{3,3,1,1}:new double[]{3,3,1,14};
      image.setRoi(oval?new OvalRoi(b[0],b[1],b[2],b[3]):new Roi(b[0],b[1],b[2],b[3]));
      points="[["+b[0]+","+b[1]+"],["+(b[0]+b[2])+","+(b[1]+b[3])+"]]";
    }else if(shape>0) {
      kind="polygon";float[] x=shape==3?new float[]{60,70,65}:shape==4?new float[]{3,4,5}:shape==7?new float[]{3.1f,3.2f,3.15f}:new float[]{3,13,9};
      float[] y=shape==3?new float[]{60,60,68}:shape==4?new float[]{3,3,3}:shape==7?new float[]{3.1f,3.1f,3.2f}:new float[]{3,3,11};
      image.setRoi(new PolygonRoi(x,y,3,Roi.POLYGON));StringBuilder b=new StringBuilder("[");for(int i=0;i<3;i++){if(i>0)b.append(',');b.append('[').append((double)x[i]).append(',').append((double)y[i]).append(']');}points=b.append(']').toString();
    }
    int flags=Measurements.AREA|Measurements.CENTROID|(implicit?Measurements.SHAPE_DESCRIPTORS:Measurements.ELLIPSE);
    ResultsTable table=new ResultsTable();new Analyzer(image,flags,table).measure();ImageStatistics stats=ImageStatistics.getStatistics(ip,flags,cal);
    if(table.size()!=1||Double.compare(table.getValue("X",0),stats.xCentroid)!=0||Double.compare(table.getValue("Y",0),stats.yCentroid)!=0)throw new RuntimeException("Direct/Analyzer ellipse center mismatch");
    java.awt.Rectangle bounds=ip.getRoi();
    StringBuilder out=new StringBuilder("{\"id\":\"extra"+shape+"-type"+type+"-shift"+shifted+"-nan"+allNan+"-implicit"+implicit+"\",\"kind\":\""+kind+"\",\"points\":"+points+",\"type\":"+type+",\"spacing\":["+cal.pixelWidth+","+cal.pixelHeight+"],\"origins\":["+cal.xOrigin+","+cal.yOrigin+"],\"invert_y\":"+shifted+",\"density\":\""+(allNan?"all_nan":"raw")+"\",\"implicit\":"+implicit+",\"fitted\":false,\"traced\":false,\"processor_bounds\":["+bounds.x+","+bounds.y+","+bounds.width+","+bounds.height+"],\"summary\":{");
    String[] names={"Major","Minor","Angle","X","Y"};double[] values={stats.major,stats.minor,stats.angle,stats.xCentroid,stats.yCentroid};for(int i=0;i<names.length;i++){if(i>0)out.append(',');out.append('"').append(names[i]).append("\":").append(ObserveAreaCentroid.num(values[i]));}out.append("},\"rows\":[{");
    boolean first=true;for(String heading:table.getHeadings()){if(!first)out.append(',');first=false;out.append('"').append(heading).append("\":").append(ObserveAreaCentroid.num(table.getValue(heading,0)));}System.out.println(out.append("}]}").toString());
  }
  public static void main(String[] args){for(int shape=0;shape<9;shape++)for(int type:new int[]{0,2})for(boolean shifted:new boolean[]{false,true})for(boolean implicit:new boolean[]{false,true})observe(shape,type,shifted,false,implicit);for(int shape=0;shape<9;shape++)for(int type=0;type<2;type++)for(boolean implicit:new boolean[]{false,true})observe(shape,type,false,true,implicit);}
}
