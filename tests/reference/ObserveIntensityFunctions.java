import ij.ImagePlus;
import ij.measure.Calibration;
import ij.process.ByteProcessor;
import ij.process.ShortProcessor;
import java.security.MessageDigest;
import java.util.Locale;
/** Read-only ImageJ table oracle: all-code hashes, bounded endpoint samples. */
public class ObserveIntensityFunctions {
  static String hex(byte[] bytes) {StringBuilder b=new StringBuilder();for(byte v:bytes)b.append(String.format(Locale.ROOT,"%02x",v&255));return b.toString();}
  public static void main(String[] args) throws Exception {
    String[] names={"straight_line","poly2","poly3","poly4","exponential","power","log","rodbard","gamma_variate","log2","rodbard2","exp_with_offset","gaussian","exp_recovery","signed_poly2","uncalibrated_od"};
    double[][] parameters={{-7,2},{3,-2,0.125},{-1,2,-0.25,0.001},{2,-1,0.25,-0.001,0.000001},{3,0.001},{2,0.5},{2,0.01},{8,1.5,200,1},{5,2,1.5,40},{2,3,10},{1,2,100,256},{3,0.01,2},{1,10,50,20},{3,0.01,2},{-32768,1,0.001},{}};
    for(int n=0;n<names.length;n++)for(int bits:new int[]{8,16})for(boolean clip:new boolean[]{false,true})for(boolean inverted:new boolean[]{false,true}) {
      int id=n==14?1:n==15?21:n;
      if(id==21&&bits==16)continue;
      ImagePlus imp=new ImagePlus("oracle",bits==8?new ByteProcessor(1,1):new ShortProcessor(1,1));
      if(inverted)imp.getProcessor().invertLut();
      Calibration cal=new Calibration(imp);cal.setFunction(id,parameters[n],"unit",clip);
      float[] table=cal.getCTable();MessageDigest raw=MessageDigest.getInstance("SHA-256"),canonical=MessageDigest.getInstance("SHA-256");
      for(float value:table) {int a=Float.floatToRawIntBits(value),b=Float.floatToIntBits(value);
        for(int shift=24;shift>=0;shift-=8){raw.update((byte)(a>>>shift));canonical.update((byte)(b>>>shift));}}
      StringBuilder samples=new StringBuilder();int[] indices={0,1,2,5,10,50,127,128,254,255,256,32767,32768,65535};
      for(int i:indices)if(i<table.length){if(samples.length()>0)samples.append(',');samples.append("[").append(i).append(',').append(Integer.toUnsignedString(Float.floatToRawIntBits(table[i]))).append(']');}
      StringBuilder coeff=new StringBuilder();for(double v:parameters[n]){if(coeff.length()>0)coeff.append(',');coeff.append(Double.toString(v));}
      System.out.println("{\"function\":\""+(n==14?"poly2":names[n])+"\",\"coefficients\":["+coeff+"],\"bit_depth\":"+bits+",\"zero_clip\":"+clip+",\"inverted_lut\":"+inverted+",\"signed16\":"+cal.isSigned16Bit()+",\"raw_sha256\":\""+hex(raw.digest())+"\",\"canonical_sha256\":\""+hex(canonical.digest())+"\",\"samples\":["+samples+"]}");
    }
  }
}
