import ij.ImagePlus;
import ij.ImageStack;
import ij.gui.*;
import ij.measure.*;
import ij.plugin.filter.Analyzer;
import ij.process.ByteProcessor;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;

/** Small original Analyzer composition corpus; never a native-generated oracle. */
public class ObserveMixedMeasurements {
  static class SilentAnalyzer extends Analyzer {
    SilentAnalyzer(ImagePlus image,int flags,ResultsTable table) {super(image,flags,table);disableReset(true);}
    @Override public void displayResults() {}
  }
  static String q(String value) {
    if(value==null)return "null";
    return "\""+value.replace("\\","\\\\").replace("\"","\\\"").replace("\r","\\r").replace("\n","\\n").replace("\t","\\t")+"\"";
  }
  static String number(double value) {
    return Double.isFinite(value)?Double.toString(value):q(Double.isNaN(value)?"nan":value>0?"inf":"-inf");
  }
  static ImagePlus image(boolean redirect) {
    ImageStack stack=new ImageStack(8,6);
    for(int n=0;n<8;n++) {
      ByteProcessor ip=new ByteProcessor(8,6);
      for(int y=0;y<6;y++)for(int x=0;x<8;x++)ip.set(x,y,x+2*y+1+n+(redirect?20:0));
      stack.addSlice("plane"+n+".tif\nignored",ip);
    }
    ImagePlus image=new ImagePlus(redirect?"sample,title":"source,title",stack);
    image.setGlobalCalibration(null);image.setDimensions(2,2,2);image.setPosition(2,2,2);
    Calibration cal=image.getLocalCalibration();cal.setUnit("um");
    cal.pixelWidth=redirect?.75:2;cal.pixelHeight=redirect?2:.5;cal.yOrigin=redirect?4.25:0;
    image.getProcessor().setThreshold(redirect?31:11,redirect?37:17,ByteProcessor.NO_LUT_UPDATE);
    return image;
  }
  static Roi roi(int kind) {
    switch(kind) {
      case 0:return new Roi(1,1,6,4);
      case 1:return new Line(1.25,1.5,5.5,3.25);
      case 2:return new PolygonRoi(new float[]{1,5,6},new float[]{1,3,2},3,Roi.POLYLINE);
      case 3:return new PolygonRoi(new float[]{1,5,6},new float[]{1,3,2},3,Roi.ANGLE);
      case 4:return new PointRoi(2.5,1.25);
      case 5:return new PointRoi(new float[]{1.25f,5.5f},new float[]{1.5f,3.25f},2);
      default:return null;
    }
  }
  static String row(ResultsTable table,int index) {
    StringBuilder out=new StringBuilder("{\"index\":"+index+",\"label\":"+q(table.getLabel(index))+",\"raw\":{");boolean first=true;
    for(String name:table.getHeadings()) {
      if(name.equals("Label"))continue;if(!first)out.append(',');first=false;
      out.append(q(name)).append(':').append(number(table.getValue(name,index)));
    }
    out.append("},\"text\":{");first=true;
    for(String name:table.getHeadings()) {
      if(name.equals("Label"))continue;if(!first)out.append(',');first=false;
      out.append(q(name)).append(':').append(q(table.getStringValue(name,index)));
    }
    return out.append("}}").toString();
  }
  static String observe(String id,boolean enabled,boolean redirect,boolean whole,boolean manager,int seed) {
    // Explicitly reset every original global consumed by these cases.
    Analyzer.setRedirectImage(null);Analyzer.setMeasurements(Measurements.AREA|Measurements.MEAN);
    Analyzer.setPrecision(3);Analyzer.drawLabels(true);Analyzer.setResultsTable(new ResultsTable());
    ij.gui.PlotWindow.interpolate=true;ij.Prefs.reflexAngle=false;
    ImagePlus source=image(false),sample=image(true);ResultsTable table=new ResultsTable();
    source.setRoi(roi(0));
    for(int i=0;i<seed;i++)new SilentAnalyzer(source,Measurements.AREA|Measurements.MEAN,table).run(source.getProcessor());
    int flags=Measurements.AREA|Measurements.MEAN;
    if(enabled) {
      flags|=Measurements.LABELS|Measurements.INVERT_Y|Measurements.NaN_EMPTY_CELLS|Measurements.SCIENTIFIC_NOTATION|Measurements.ADD_TO_OVERLAY|Measurements.LIMIT|Measurements.AREA_FRACTION|Measurements.SKEWNESS|Measurements.KURTOSIS|Measurements.CENTROID;
      // Native RECT/COM acceptance is currently ordinary-area only. Their
      // non-area variants remain an explicit residual, not claimed by M4.
      if(!manager)flags|=Measurements.CENTER_OF_MASS|Measurements.RECT;
    }
    Analyzer.setMeasurements(flags);Analyzer.setRedirectImage(redirect?sample:null);
    int count=manager?6:1;
    for(int kind=0;kind<count;kind++) {
      Roi selection=whole?null:roi(kind);
      if(manager)selection.setName("stored-"+kind);
      if(selection==null)source.deleteRoi();else source.setRoi(selection);
      if(kind==3)try {
        java.lang.reflect.Method refresh=PolygonRoi.class.getDeclaredMethod("getAngleAsString");
        refresh.setAccessible(true);refresh.invoke(selection);
      }catch(Exception error){throw new RuntimeException(error);}
      new SilentAnalyzer(source,manager?flags&~Measurements.ADD_TO_OVERLAY:flags,table).run(source.getProcessor());
    }
    if(seed>0) {
      source.setRoi(roi(0));
      new SilentAnalyzer(source,flags,table).run(source.getProcessor());
    }
    StringBuilder out=new StringBuilder("{\"id\":"+q(id)+",\"enabled\":"+enabled+",\"redirect\":"+redirect+",\"whole\":"+whole+",\"manager\":"+manager+",\"seed\":"+seed+",\"flags\":"+flags+",\"angle_policy\":\"calibrated_refresh\",\"rows\":[");
    for(int index=seed;index<table.size();index++){if(index>seed)out.append(',');out.append(row(table,index));}
    out.append("],\"seed_cells\":[");
    if(seed>0)out.append(row(table,0)).append(',').append(row(table,seed-1));
    out.append("],\"row_count\":").append(table.size()).append(",\"overlays\":[");
    Overlay overlays=source.getOverlay();if(overlays!=null)for(int i=0;i<overlays.size();i++) {
      if(i>0)out.append(',');Roi r=overlays.get(i);
      out.append("{\"name\":").append(q(r.getName())).append(",\"c\":").append(r.getCPosition()).append(",\"z\":").append(r.getZPosition()).append(",\"t\":").append(r.getTPosition()).append('}');
    }
    out.append("],\"draw_labels\":").append(overlays!=null&&overlays.getDrawLabels()).append('}');
    Analyzer.setRedirectImage(null);Analyzer.setMeasurements(Measurements.AREA|Measurements.MEAN);Analyzer.setPrecision(3);Analyzer.drawLabels(true);Analyzer.setResultsTable(new ResultsTable());
    return out.toString();
  }
  public static void main(String[] args)throws Exception {
    StringBuilder out=new StringBuilder("{\"schema\":\"imagejxx.original-mixed-measurements.001\",\"reference\":\"ImageJ1.54r\",\"cases\":[");
    out.append(observe("default-current",false,false,false,false,0));
    out.append(',').append(observe("combined-current",true,false,false,false,0));
    out.append(',').append(observe("combined-redirect",true,true,false,false,0));
    out.append(',').append(observe("combined-whole",true,false,true,false,0));
    out.append(',').append(observe("combined-manager",true,false,false,true,0));
    out.append(',').append(observe("combined-manager-redirect",true,true,false,true,0));
    out.append(',').append(observe("late-column-capacity",true,false,false,true,99));
    out.append("]}\n");if(out.length()>256*1024)throw new IllegalStateException("Composition corpus bound");
    Files.writeString(Path.of(args[0]),out,StandardCharsets.UTF_8,StandardOpenOption.CREATE_NEW);
  }
}
