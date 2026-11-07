import ij.ImagePlus;
import ij.ImageStack;
import ij.CompositeImage;
import ij.gui.*;
import ij.measure.*;
import ij.plugin.filter.Analyzer;
import ij.plugin.frame.RoiManager;
import ij.process.ByteProcessor;
import java.awt.Color;
import java.awt.geom.PathIterator;
import java.awt.geom.Rectangle2D;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

/** Original run() side effects; only the GUI Results sink is suppressed. */
public class ObserveMeasurementOverlays {
  static class SilentAnalyzer extends Analyzer {
    SilentAnalyzer(ImagePlus image,int flags,ResultsTable table){super(image,flags,table);disableReset(true);}
    @Override public void displayResults() {}
  }
  static final String[] KINDS={"none","rectangle","ellipse","polygon","line","polyline","angle","point","multipoint","compound"};
  static String q(String text) {
    if(text==null)return "null";StringBuilder s=new StringBuilder("\"");
    for(int i=0;i<text.length();i++) {char c=text.charAt(i);switch(c) {
      case '"':s.append("\\\"");break;case '\\':s.append("\\\\");break;
      case '\n':s.append("\\n");break;case '\r':s.append("\\r");break;case '\t':s.append("\\t");break;
      default:if(c<32)s.append(String.format("\\u%04x",(int)c));else s.append(c);
    }}return s.append('"').toString();
  }
  static String color(Color value){return value==null?"null":q(String.format("%08x",value.getRGB()));}
  static String roi(Roi r) {
    if(r==null)return "null";Rectangle2D.Double b=r.getFloatBounds();
    StringBuilder s=new StringBuilder("{\"name\":"+q(r.getName())+",\"type\":"+r.getType()+",\"bounds\":["+b.x+","+b.y+","+b.width+","+b.height+"]");
    s.append(",\"position\":").append(r.getPosition()).append(",\"c\":").append(r.getCPosition()).append(",\"z\":").append(r.getZPosition()).append(",\"t\":").append(r.getTPosition()).append(",\"hyper\":").append(r.hasHyperStackPosition());
    s.append(",\"stroke\":").append(color(r.getStrokeColor())).append(",\"fill\":").append(color(r.getFillColor())).append(",\"width\":").append(r.getStrokeWidth()).append(",\"properties\":").append(q(r.getProperties()));
    if(r instanceof PointRoi){PointRoi point=(PointRoi)r;s.append(",\"point_type\":").append(point.getPointType()).append(",\"point_size\":").append(point.getSize());s.append(",\"point_positions\":[");for(int i=0;i<point.getNCoordinates();i++){if(i>0)s.append(',');s.append(point.getPointPosition(i));}s.append(']');s.append(",\"point_counters\":[");for(int i=0;i<point.getNCoordinates();i++){if(i>0)s.append(',');s.append(point.getCounter(i));}s.append(']');}
    if(r instanceof ShapeRoi) {
      s.append(",\"path\":[");PathIterator path=((ShapeRoi)r).getShape().getPathIterator(null);boolean first=true;double[] v=new double[6];
      while(!path.isDone()){int kind=path.currentSegment(v);if(!first)s.append(',');first=false;s.append('[').append(kind);int n=kind==PathIterator.SEG_CLOSE?0:kind==PathIterator.SEG_CUBICTO?6:kind==PathIterator.SEG_QUADTO?4:2;for(int i=0;i<n;i++)s.append(',').append(v[i]);s.append(']');path.next();}s.append(']');
    } else {
      ij.process.FloatPolygon p=r.getFloatPolygon();s.append(",\"points\":[");if(p!=null)for(int i=0;i<p.npoints;i++){if(i>0)s.append(',');s.append('[').append(p.xpoints[i]).append(',').append(p.ypoints[i]).append(']');}s.append(']');
    }
    return s.append('}').toString();
  }
  static String overlay(Overlay o) {
    if(o==null)return "null";StringBuilder s=new StringBuilder("{\"labels\":"+o.getDrawLabels()+",\"names\":"+o.getDrawNames()+",\"backgrounds\":"+o.getDrawBackgrounds()+",\"label_color\":"+color(o.getLabelColor())+",\"rows\":[");
    for(int i=0;i<o.size();i++){if(i>0)s.append(',');s.append(roi(o.get(i)));}return s.append("]}").toString();
  }
  static ImagePlus image(String title,int layout,int offset) {
    int channels=layout==2||layout==3||layout==5||layout==7||layout==8||layout==9?2:1;
    int slices=layout==1||layout==10?3:layout==2||layout==5||layout==6||layout==9?2:1;
    int frames=layout==2||layout==4||layout==6||layout==7||layout==9?2:1;
    ImageStack stack=new ImageStack(8,6);int count=channels*slices*frames;
    for(int n=0;n<count;n++){ByteProcessor ip=new ByteProcessor(8,6);for(int y=0;y<6;y++)for(int x=0;x<8;x++)ip.set(x,y,x+2*y+1+offset+n);stack.addSlice(ip);}
    ImagePlus image=new ImagePlus(title,stack);image.setGlobalCalibration(null);
    image.setDimensions(channels,slices,frames);
    if(layout==8||layout==9)image=new CompositeImage(image,CompositeImage.COMPOSITE);
    if(layout==10)image.setOpenAsHyperStack(true);
    image.setPosition(channels,slices==3?2:slices,frames);return image;
  }
  static long pixels(ImagePlus image){long value=0;for(int i=1;i<=image.getStackSize();i++)value=value*31+Arrays.hashCode((byte[])image.getStack().getPixels(i));return value;}
  static Roi selection(int kind,int naming) {
    Roi r=null;
    if(kind==1)r=new Roi(1,1,6,4);if(kind==2)r=new OvalRoi(1,1,6,4);
    if(kind==3)r=new PolygonRoi(new float[]{1,7,3},new float[]{1,1,5},3,Roi.POLYGON);
    if(kind==4)r=new Line(1.25,1.5,5.5,3.25);
    if(kind==5||kind==6)r=new PolygonRoi(new float[]{1,5,6},new float[]{1,3,2},3,kind==5?Roi.POLYLINE:Roi.ANGLE);
    if(kind==7)r=new PointRoi(2.5,1.25);if(kind==8)r=new PointRoi(new float[]{1.25f,5.5f},new float[]{1.5f,3.25f},2);
    if(kind==9)r=new ShapeRoi(new Roi(1,1,6,4)).not(new ShapeRoi(new Roi(3,2,2,1)));
    if(r!=null){r.setProperty("opaque","keep,\"value");
      if(naming>0){r.setName(naming==1?"":"roi,\"alpha\u03b1\nnext");r.setStrokeWidth(2.5);r.setStrokeColor(new Color(17,34,51,128));r.setFillColor(new Color(50,60,70,85));if(naming==1)r.setPosition(3);else r.setPosition(1,1,1);}
      if(r instanceof PointRoi){((PointRoi)r).setSize(naming==1?4:2);((PointRoi)r).setPointType(naming==2?PointRoi.CROSS:PointRoi.HYBRID);}
    }return r;
  }
  static Overlay initial(int naming){if(naming==0)return null;Overlay o=new Overlay();Roi r=new Roi(0,0,1,1);r.setName("old");r.setStrokeColor(Color.magenta);o.add(r);o.drawLabels(naming==2);o.drawNames(false);o.drawBackgrounds(false);o.setLabelColor(Color.red);return o;}
  static String observe(int kind,int layout,int naming,boolean enabled,boolean redirect,int repeats,boolean manager) {
    ImagePlus source=image("source",layout,0),target=image("target",layout,20);source.setOverlay(initial(naming));Roi selection=selection(kind,naming);if(selection!=null)source.setRoi(selection);
    Roi active=source.getRoi();String before=roi(active),overlayBefore=overlay(source.getOverlay());long pixels=pixels(source),targetPixels=pixels(target);String old=source.getOverlay()==null?null:roi(source.getOverlay().get(0));
    ResultsTable table=new ResultsTable();for(int i=0;i<3;i++){table.incrementCounter();table.addValue("seed",i);}
    int flags=Measurements.AREA|Measurements.MEAN|(enabled?Measurements.ADD_TO_OVERLAY:0);Analyzer.setMeasurements(flags);Analyzer.drawLabels(naming!=1);Analyzer.setRedirectImage(redirect?target:null);
    // RoiManager.measure() masks this flag before invoking Analyzer.run().
    int consumed=manager?flags&~Measurements.ADD_TO_OVERLAY:flags;
    String refusal=null;
    try {for(int i=0;i<repeats;i++)new SilentAnalyzer(source,consumed,table).run(source.getProcessor());}
    catch(IllegalArgumentException error){if(!error.getMessage().startsWith("n out of range:"))throw error;refusal=error.getClass().getName()+": "+error.getMessage();}
    String after=overlay(source.getOverlay());boolean sameActive=before.equals(roi(source.getRoi()));boolean cloned=source.getOverlay()==null||source.getOverlay().size()==(old==null?0:1)||source.getOverlay().get(source.getOverlay().size()-1)!=active;
    boolean oldPreserved=old==null||old.equals(roi(source.getOverlay().get(0)));String cloneBefore=after;
    if(active!=null){active.setName("changed after clone");active.setStrokeColor(Color.green);active.setLocation(0,0);}boolean detached=cloneBefore.equals(overlay(source.getOverlay()));
    return "{\"id\":"+q((manager?"manager-masked":"current")+"-"+KINDS[kind]+"-layout"+layout+"-name"+naming+"-flag"+enabled+"-redirect"+redirect+"-repeat"+repeats)+",\"kind\":"+q(KINDS[kind])+",\"layout\":"+layout+",\"naming\":"+naming+",\"enabled\":"+enabled+",\"redirect\":"+redirect+",\"manager_masked\":"+manager+",\"repeats\":"+repeats+",\"rows_before\":3,\"rows_after\":"+table.size()+",\"source_before\":"+before+",\"overlay_before\":"+overlayBefore+",\"overlay_after\":"+after+",\"source_roi_unchanged\":"+sameActive+",\"old_overlay_preserved\":"+oldPreserved+",\"distinct_clone\":"+cloned+",\"detached_clone\":"+detached+",\"pixels_unchanged\":"+(pixels==pixels(source)&&targetPixels==pixels(target))+",\"target_overlay\":"+overlay(target.getOverlay())+(refusal==null?"":",\"original_refusal\":"+q(refusal))+"}";
  }
  static String multi(int layout,boolean enabled) {
    ImagePlus source=image("manager",layout,0);source.setOverlay(initial(2));String before=overlay(source.getOverlay());
    Analyzer.setRedirectImage(null);Analyzer.setResultsTable(new ResultsTable());Analyzer.setMeasurements(Measurements.AREA|Measurements.MEAN|(enabled?Measurements.ADD_TO_OVERLAY:0));
    RoiManager.multiMeasure(source,new Roi[]{selection(1,0),selection(8,2),selection(4,1)},false);
    return "{\"id\":"+q("actual-static-multi-layout"+layout+"-flag"+enabled)+",\"overlay_unchanged\":"+before.equals(overlay(source.getOverlay()))+"}";
  }
  public static void main(String[] args)throws Exception {
    StringBuilder out=new StringBuilder("{\"schema\":\"imagejxx.original-measurement-overlay.001\",\"reference\":\"ImageJ1.54r\",\"cases\":[");boolean first=true;
    for(int kind=0;kind<KINDS.length;kind++)for(int layout=0;layout<11;layout++)for(int naming=0;naming<3;naming++)for(boolean enabled:new boolean[]{false,true})for(boolean redirect:new boolean[]{false,true}){if(!first)out.append(',');first=false;out.append(observe(kind,layout,naming,enabled,redirect,1,false));}
    for(int layout=0;layout<11;layout++)for(int naming:new int[]{0,2}){out.append(',').append(observe(8,layout,naming,true,false,2,false));for(int kind:new int[]{1,8,4})out.append(',').append(observe(kind,layout,naming,true,false,1,true));}
    out.append("],\"static_multi\":[");first=true;for(int layout=0;layout<11;layout++)for(boolean enabled:new boolean[]{false,true}){if(!first)out.append(',');first=false;out.append(multi(layout,enabled));}
    out.append("]}");if(out.length()>3*1024*1024)throw new IllegalStateException("Observer output limit");Files.writeString(Path.of(args[0]),out,StandardCharsets.UTF_8,StandardOpenOption.CREATE_NEW);
  }
}
