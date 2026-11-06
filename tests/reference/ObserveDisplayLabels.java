import ij.ImagePlus;
import ij.ImageStack;
import ij.gui.*;
import ij.measure.*;
import ij.plugin.filter.Analyzer;
import ij.process.ByteProcessor;
import java.nio.charset.StandardCharsets;

/** Original Analyzer label ownership and string projection, independent of native code. */
public class ObserveDisplayLabels {
  static boolean first=true;
  static String q(String s) {
    if(s==null)return "null";StringBuilder out=new StringBuilder("\"");
    for(int i=0;i<s.length();i++) {
      char c=s.charAt(i);
      if(c=='"'||c=='\\')out.append('\\').append(c);
      else if(c<32||c>126)out.append(String.format("\\u%04x",(int)c));
      else out.append(c);
    }
    return out.append('"').toString();
  }
  static String utf8(String s){return s==null?null:new String(s.getBytes(StandardCharsets.UTF_8),StandardCharsets.UTF_8);}
  static ImagePlus image(String title,int channels,int slices,int frames) {
    ImageStack stack=new ImageStack(4,3);
    for(int t=0;t<frames;t++)for(int z=0;z<slices;z++)for(int c=0;c<channels;c++) {
      ByteProcessor ip=new ByteProcessor(4,3);ip.setValue(10*t+3*z+c+1);ip.fill();stack.addSlice("other-"+t+"-"+c+"-"+z+".tif",ip);
    }
    ImagePlus image=new ImagePlus(title,stack);image.setDimensions(channels,slices,frames);image.setGlobalCalibration(null);return image;
  }
  static void observe(int layout,int mode,int nameIndex,int sliceIndex,int kind,int titleVariant,String name,String label) {
    int t=layout>=2?2:1,c=layout>=2?2:1,z=layout==0?1:layout==1?3:2;
    String title=titleVariant==1?"long"+"x".repeat(300)+"\uD83E\uDDEA\n,\"end":layout==0?"source.tif":layout==1?"":"source\"title,\u03bb\nline";
    ImagePlus source=image(title,c,z,t),other=image("target\"title,\u03bc\nline",mode==3?1:c,mode==3?1:z,mode==3?1:t);
    int pt=layout==3?1:t,pc=layout==3?2:c,pz=layout==3?1:z;
    source.setPosition(pc,pz,pt);other.setPosition(other.getNChannels(),other.getNSlices(),other.getNFrames());
    source.getStack().setSliceLabel(label,source.getCurrentSlice());
    Roi roi=kind==5?null:kind==1?new Line(0,0,2,1):kind==2?new PolygonRoi(new float[]{0,1,2},new float[]{0,1,0},3,Roi.ANGLE):kind==3?new PointRoi(1,1):kind==4?new PointRoi():new Roi(0,0,3,2);
    if(kind==4){PointRoi points=(PointRoi)roi;points.addPoint(1,1,1);points.addPoint(2,1,source.getStackSize());}
    if(roi!=null){roi.setName(name);source.setRoi(roi);}
    int flags=Measurements.AREA|Measurements.MEAN|Measurements.LABELS|Measurements.STACK_POSITION;
    Analyzer.setMeasurements(flags);Analyzer.setRedirectImage(mode==0?null:mode==1?source:other);
    ResultsTable table=new ResultsTable();new Analyzer(source,flags,table).measure();
    if(table.size()==0)throw new IllegalStateException("Label measurement produced no row");
    if(!first)System.out.print(",");first=false;
    StringBuilder out=new StringBuilder("{\"id\":\"label-layout"+layout+"-mode"+mode+"-name"+nameIndex+"-slice"+sliceIndex+"-kind"+kind+"-title"+titleVariant+"\",\"layout\":"+layout+",\"mode\":"+mode+",\"kind\":"+kind+",\"shape\":["+t+","+c+","+z+"],\"position\":["+(pt-1)+","+(pc-1)+","+(pz-1)+"],\"source_title\":"+q(source.getTitle())+",\"target_title\":"+q(mode==1?source.getTitle():other.getTitle())+",\"roi_name\":"+q(name)+",\"slice_label\":"+q(label)+",\"source_slice_after\":"+source.getCurrentSlice()+",\"rows\":[");
    for(int row=0;row<table.size();row++) {
      if(row>0)out.append(',');String value=table.getLabel(row);
      out.append("{\"label_utf16\":").append(q(value)).append(",\"label_utf8\":").append(q(utf8(value))).append('}');
    }
    System.out.print(out.append("]}").toString());Analyzer.setRedirectImage(null);
  }
  public static void main(String[] args) {
    String[] names={null,"","roi","has.dot","x".repeat(30),"x".repeat(31),"\u03bb".repeat(31),"a".repeat(26)+"\uD83E\uDDEAzzz","\uD83E\uDDEA".repeat(16),"quote\"comma,\nrow","name","\u6c49".repeat(31)};
    String[] labels={null,"","hello.tif","hello.123","line\nsecond","\nsecond","a.long","123.txt","q\"x,y\nstuff","\u03bb".repeat(61),"x".repeat(59)+"\uD83E\uDDEA","\uD83E\uDDEA".repeat(31),"x\ry","a".repeat(60)};
    System.out.print("{\"schema\":\"imagejxx.original-display-label.001\",\"reference\":\"ImageJ1.54r\",\"cases\":[");
    for(int layout=0;layout<3;layout++)for(int mode=0;mode<4;mode++) {
      for(int n=0;n<names.length;n++)for(int s=0;s<labels.length;s++)observe(layout,mode,n,s,0,0,names[n],labels[s]);
      for(int kind=1;kind<=4;kind++)for(int n:new int[]{0,1,3,7})observe(layout,mode,n,2,kind,0,names[n],labels[2]);
      observe(layout,mode,2,2,0,1,names[2],labels[2]);
    }
    java.util.ArrayList<int[]> digits=new java.util.ArrayList<>();
    for(int cp=0;cp<=65535;cp++)if(Character.isDigit((char)cp)) {
      int start=cp;while(cp<65535 && Character.isDigit((char)(cp+1)))cp++;
      digits.add(new int[]{start,cp});
      observe(1,0,0,10000+start,0,0,null,"digits.xx"+(char)start);
    }
    String[] tails={"unicode.\u00e4bc","unicode.\u00b2bc","unicode.xx\u00b2","unicode.x\uD835\uDFCE","unicode.\uD835\uDFCEa"};
    for(int n=0;n<tails.length;n++)observe(1,0,0,90000+n,0,0,null,tails[n]);
    for(int layout=0;layout<3;layout++)for(int mode=0;mode<4;mode++)observe(layout,mode,0,2,5,0,null,"hello.tif");
    for(int mode=0;mode<4;mode++)for(int kind=0;kind<=5;kind++)for(int n:new int[]{0,2,7})for(int s:new int[]{0,2,10})observe(3,mode,n,s,kind,0,names[n],labels[s]);
    System.out.print("],\"bmp_decimal_digit_ranges\":[");
    for(int n=0;n<digits.size();n++){if(n>0)System.out.print(",");int[] range=digits.get(n);System.out.print("["+range[0]+","+range[1]+"]");}
    System.out.println("]}");
  }
}
