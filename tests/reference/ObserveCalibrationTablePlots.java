import ij.ImagePlus;
import ij.measure.Calibration;
import ij.process.ByteProcessor;
import ij.process.ShortProcessor;
import ij.util.Tools;
/** Actual ImageJ calibration tables sampled using inspected Calibrator.showPlot policy; no GUI shown. */
public class ObserveCalibrationTablePlots {
  static String numbers(double[] a){StringBuilder b=new StringBuilder("[");for(int i=0;i<a.length;i++){if(i>0)b.append(',');b.append(Double.isFinite(a[i])?Double.toString(a[i]):"null");}return b.append(']').toString();}
  static void emit(String kind,String name,int bits,double[] p,boolean clip,boolean inverted,int[] pattern,Calibration cal,boolean canonical) {
    float[] table=cal.getCTable(),values=new float[256];double[] x=new double[256];StringBuilder words=new StringBuilder("["),indices=new StringBuilder("[");
    for(int i=0;i<256;i++){x[i]=(float)((i/255.0)*(table.length-1));int index=(int)x[i];values[i]=table[index];
      if(i>0){words.append(',');indices.append(',');}indices.append(index);words.append(Integer.toUnsignedString(canonical?Float.floatToIntBits(values[i]):Float.floatToRawIntBits(values[i])));}
    StringBuilder rawPattern=new StringBuilder("[");for(int i=0;i<pattern.length;i++){if(i>0)rawPattern.append(',');rawPattern.append(Integer.toUnsignedString(pattern[i]));}
    System.out.println("{\"kind\":\""+kind+"\",\"function\":\""+name+"\",\"bit_depth\":"+bits+",\"coefficients\":"+numbers(p)+",\"zero_clip\":"+clip+",\"inverted_lut\":"+inverted+",\"pattern_words\":"+rawPattern.append(']')+",\"curve_x\":"+numbers(x)+",\"indices\":"+indices.append(']')+",\"curve_words\":"+words.append(']')+",\"limits\":"+numbers(Tools.getMinMax(values))+"}");
  }
  static ImagePlus image(int bits){return new ImagePlus("oracle",bits==8?new ByteProcessor(1,1):new ShortProcessor(1,1));}
  public static void main(String[] args) {
    String[] names={"straight_line","poly2","poly3","poly4","exponential","power","log","rodbard","gamma_variate","log2","rodbard2","exp_with_offset","gaussian","exp_recovery","poly2","uncalibrated_od"};
    double[][] p={{-7,2},{3,-2,.125},{-1,2,-.25,.001},{2,-1,.25,-.001,.000001},{3,.001},{2,.5},{2,.01},{8,1.5,200,1},{5,2,1.5,40},{2,3,10},{1,2,100,256},{3,.01,2},{1,10,50,20},{3,.01,2},{-32768,1,.001},{}};
    for(int n=0;n<16;n++)for(int bits:new int[]{8,16})for(boolean clip:new boolean[]{false,true})for(boolean inverted:new boolean[]{false,true}) {
      if(n==15&&bits==16)continue;ImagePlus imp=image(bits);if(inverted)imp.getProcessor().invertLut();
      Calibration cal=new Calibration(imp);cal.setFunction(n==14?1:n==15?21:n,p[n],"unit",clip);
      emit("function",names[n],bits,p[n],clip,inverted,new int[]{},cal,true);
    }
    Calibration signed=new Calibration(image(16));signed.setFunction(0,new double[]{-32768,1},"unit",false);emit("signed16","signed16",16,new double[]{-32768,1},false,false,new int[]{},signed,false);
    int[] pattern={0x80000000,0x7f800000,0xff800000,0x7fc12345,0x3f800000,0xc0000000,0xffc43210,0};
    for(int bits:new int[]{8,16}){Calibration cal=new Calibration(image(bits));float[] table=new float[1<<bits];for(int i=0;i<table.length;i++)table[i]=Float.intBitsToFloat(pattern[i%pattern.length]);cal.setCTable(table,"unit");emit("lookup","lookup",bits,new double[]{},false,false,pattern,cal,false);}
    Calibration empty=new Calibration(image(8));float[] table=new float[256];for(int i=0;i<256;i++)table[i]=Float.intBitsToFloat(0x7fc12345);empty.setCTable(table,"unit");emit("lookup","lookup",8,new double[]{},false,false,new int[]{0x7fc12345},empty,false);
  }
}
