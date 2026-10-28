import ij.ImagePlus;
import ij.measure.Calibration;
import ij.process.*;

/** Actual pinned global density adaptation variants; ASCII-safe JSON. */
public class ObserveGlobalCalibrationVariants {
  static String quote(String s) {
    StringBuilder out=new StringBuilder("\"");
    for(int i=0;i<s.length();i++){char c=s.charAt(i);if(c=='\"'||c=='\\')out.append('\\').append(c);else if(c<32||c>=128)out.append(String.format(java.util.Locale.ROOT,"\\u%04x",(int)c));else out.append(c);}
    return out.append('\"').toString();
  }
  static String value(Calibration c) {
    String words="null";float[] table=c.getCTable();
    if(table!=null){words="[";for(int i=0;i<4;i++){if(i>0)words+=",";words+=Integer.toUnsignedLong(Float.floatToRawIntBits(table[i]));}words+="]";}
    return "{\"pixel_width\":"+c.pixelWidth+",\"pixel_height\":"+c.pixelHeight+",\"pixel_depth\":"+c.pixelDepth+
      ",\"x_origin\":"+c.xOrigin+",\"unit\":"+quote(c.getUnit())+",\"value_unit\":"+quote(c.getValueUnit())+
      ",\"density\":"+c.calibrated()+",\"signed16\":"+c.isSigned16Bit()+",\"first_words\":"+words+"}";
  }
  public static void main(String[] args) {
    System.setProperty("java.awt.headless","true");
    String[] kinds={"none8","none16","line16","table8","table16"};
    for(String kind:kinds) {
      boolean sixteen=kind.endsWith("16");int bits=sixteen?16:8;
      ImagePlus source=new ImagePlus("source",sixteen?new ShortProcessor(2,1):new ByteProcessor(2,1));source.setGlobalCalibration(null);
      Calibration global=new Calibration(source);global.pixelWidth=.5;global.pixelHeight=.75;global.pixelDepth=2;global.xOrigin=7;global.setUnit("um");
      if(kind.startsWith("none"))global.setValueUnit("unit-"+kind);
      else if(kind.startsWith("line"))global.setFunction(Calibration.STRAIGHT_LINE,new double[]{10,2},"unit-"+kind);
      else {
        int[] pattern={0x3e800000,0x7fc12345,0x7f800000,0x80000000};float[] table=new float[sixteen?65536:256];
        for(int i=0;i<table.length;i++)table[i]=Float.intBitsToFloat(pattern[i%4]);global.setCTable(table,"unit-"+kind);
      }
      source.setGlobalCalibration(global);
      ImagePlus[] targets={new ImagePlus("u8",new ByteProcessor(2,1)),new ImagePlus("u16",new ShortProcessor(2,1)),new ImagePlus("f32",new FloatProcessor(2,1)),new ImagePlus("rgb",new ColorProcessor(2,1)),new ImagePlus("signed16",new ShortProcessor(2,1))};
      targets[4].getLocalCalibration().setSigned16BitCalibration();
      for(ImagePlus target:targets)System.out.println("{\"id\":"+quote(kind+"_"+target.getTitle())+",\"kind\":"+quote(kind)+",\"source_bits\":"+bits+
        ",\"destination\":"+quote(target.getTitle())+",\"destination_bits\":"+target.getBitDepth()+",\"effective\":"+value(target.getCalibration())+"}");
      source.setGlobalCalibration(null);
    }
  }
}
