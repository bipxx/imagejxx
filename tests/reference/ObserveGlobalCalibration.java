import ij.ImagePlus;
import ij.measure.Calibration;
import ij.process.*;

/** Observe actual pinned ImagePlus global/local calibration adaptation; no GUI. */
public class ObserveGlobalCalibration {
  static String quote(String s) {
    StringBuilder result=new StringBuilder("\"");
    for(int i=0;i<s.length();i++) {
      char c=s.charAt(i);
      if(c=='\"' || c=='\\')result.append('\\').append(c);
      else if(c<32 || c>=128)result.append(String.format(java.util.Locale.ROOT,"\\u%04x",(int)c));
      else result.append(c);
    }
    return result.append('\"').toString();
  }
  static String calibration(Calibration c) {
    String coefficients="null";
    if(c.getCoefficients()!=null)coefficients=java.util.Arrays.toString(c.getCoefficients());
    return "{\"pixel_width\":"+c.pixelWidth+",\"pixel_height\":"+c.pixelHeight+",\"pixel_depth\":"+c.pixelDepth+
      ",\"x_origin\":"+c.xOrigin+",\"unit\":"+quote(c.getUnit())+",\"value_unit\":"+quote(c.getValueUnit())+
      ",\"function\":"+c.getFunction()+",\"coefficients\":"+coefficients+",\"signed16\":"+c.isSigned16Bit()+
      ",\"calibrated\":"+c.calibrated()+",\"at_0\":"+c.getCValue(0)+",\"at_1\":"+c.getCValue(1)+"}";
  }
  static void observe(String id,ImagePlus image) {
    System.out.println("{\"id\":"+quote(id)+",\"bit_depth\":"+image.getBitDepth()+",\"global_present\":"+(image.getGlobalCalibration()!=null)+
      ",\"local\":"+calibration(image.getLocalCalibration())+",\"effective\":"+calibration(image.getCalibration())+"}");
  }
  public static void main(String[] args) {
    System.setProperty("java.awt.headless","true");
    ImagePlus a=new ImagePlus("u8",new ByteProcessor(2,1));
    ImagePlus b=new ImagePlus("u16",new ShortProcessor(2,1));
    ImagePlus f=new ImagePlus("f32",new FloatProcessor(2,1));
    ImagePlus rgb=new ImagePlus("rgb",new ColorProcessor(2,1));
    ImagePlus signed=new ImagePlus("signed",new ShortProcessor(2,1));
    a.setGlobalCalibration(null);
    a.getLocalCalibration().pixelWidth=2;a.getLocalCalibration().setUnit("local8");
    b.getLocalCalibration().pixelWidth=3;b.getLocalCalibration().setUnit("local16");
    f.getLocalCalibration().pixelWidth=4;f.getLocalCalibration().setValueUnit("local_float");
    signed.getLocalCalibration().setSigned16BitCalibration();
    observe("off_u8",a);observe("off_u16",b);observe("off_f32",f);
    Calibration global=new Calibration(a);global.pixelWidth=.5;global.pixelHeight=.75;global.pixelDepth=2;global.xOrigin=7;
    global.setUnit("um");global.setFunction(Calibration.STRAIGHT_LINE,new double[]{10,2},"OD");a.setGlobalCalibration(global);
    observe("enabled_u8",a);observe("enabled_u16",b);observe("enabled_f32",f);observe("enabled_rgb",rgb);observe("enabled_signed16",signed);
    a.setIgnoreGlobalCalibration(true);observe("ignored_u8",a);a.setIgnoreGlobalCalibration(false);
    global.pixelWidth=99;global.setValueUnit("mutated_input");a.getCalibration().pixelWidth=88;observe("copy_isolation",a);
    ImagePlus later=new ImagePlus("later",new ByteProcessor(2,1));observe("new_u8",later);
    a.setGlobalCalibration(null);observe("cleared_u8",a);observe("cleared_u16",b);
  }
}
