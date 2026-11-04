import ij.ImagePlus;
import ij.measure.Calibration;
import ij.measure.Measurements;
import ij.process.*;

/** Additive edge observations; leaves reference001 producer/expectations unchanged. */
public class ObserveCalibratedAnalysisEdges {
  static void emit(String id,String type,String mode,String membership) {
    ImagePlus image=ObserveCalibratedAnalysis.image(type,false);ImageProcessor ip=image.getProcessor();
    Calibration c=ObserveCalibratedAnalysis.calibration(mode,type.equals("uint16")?16:8);
    if(mode.equals("signed16"))c.setSigned16BitCalibration();
    if(membership.equals("gap"))ip.set(0,1,3);
    if(membership.equals("signed")) {
      int[] a={0,32767,32768,65535,0,32767,32768,65535};for(int i=0;i<a.length;i++)ip.set(i,a[i]);
    }
    if(membership.equals("constant"))for(int i=0;i<8;i++)ip.setf(i,5);
    if(membership.equals("empty") || membership.equals("single")) {
      byte[] mask=new byte[8];if(membership.equals("single"))mask[1]=(byte)255;ip.setMask(new ByteProcessor(4,2,mask));
    }
    c.setImage(image);ImageStatistics s=ImageStatistics.getStatistics(ip,
      Measurements.AREA|Measurements.MEAN|Measurements.STD_DEV|Measurements.MIN_MAX|Measurements.MEDIAN|Measurements.MODE,c);
    System.out.println("{\"id\":"+ObserveCalibratedAnalysis.q(id)+",\"type\":"+ObserveCalibratedAnalysis.q(type)+
      ",\"mode\":"+ObserveCalibratedAnalysis.q(mode)+",\"membership\":"+ObserveCalibratedAnalysis.q(membership)+
      ",\"pixel_count\":"+s.pixelCount+",\"area\":"+ObserveCalibratedAnalysis.number(s.area)+
      ",\"mean\":"+ObserveCalibratedAnalysis.number(s.mean)+",\"raw_mean\":"+ObserveCalibratedAnalysis.number(s.umean)+
      ",\"stddev\":"+ObserveCalibratedAnalysis.number(s.stdDev)+",\"minimum\":"+ObserveCalibratedAnalysis.number(s.min)+
      ",\"maximum\":"+ObserveCalibratedAnalysis.number(s.max)+",\"median\":"+ObserveCalibratedAnalysis.number(s.median)+
      ",\"mode_index\":"+s.mode+",\"mode_value\":"+ObserveCalibratedAnalysis.number(s.dmode)+
      ",\"integrated_density\":"+ObserveCalibratedAnalysis.number(s.area*s.mean)+
      ",\"histogram\":"+ObserveCalibratedAnalysis.sparse(s.histogram)+",\"histogram16\":"+ObserveCalibratedAnalysis.sparse(s.histogram16)+"}");
  }
  public static void main(String[] args) {
    System.setProperty("java.awt.headless","true");
    for(String type:new String[]{"uint8","uint16","float32"}) {
      emit(type+"_empty",type,"raw","empty");
      emit(type+"_single",type,"raw","single");
      emit(type+"_constant",type,"raw","constant");
    }
    emit("uint8_nan_gap","uint8","nan_gap","gap");
    emit("uint16_nan_gap","uint16","nan_gap","gap");
    emit("uint16_signed","uint16","signed16","signed");
  }
}
