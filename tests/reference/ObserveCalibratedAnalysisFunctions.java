import ij.ImagePlus;
import ij.measure.Calibration;
import ij.measure.Measurements;
import ij.process.ImageStatistics;

/** Function median uses getCValue(double precision), not the Float32 table. */
public class ObserveCalibratedAnalysisFunctions {
  public static void main(String[] args) {
    System.setProperty("java.awt.headless","true");
    for(String type:new String[]{"uint8","uint16"})for(String mode:new String[]{"line","poly","clip"}) {
      ImagePlus image=ObserveCalibratedAnalysis.image(type,false);Calibration c=image.getLocalCalibration();
      double[] coefficients=mode.equals("line")?new double[]{.123456789,.987654321}:
        mode.equals("poly")?new double[]{.123456789,.987654321,.111111111}:new double[]{-10,1};
      c.setFunction(mode.equals("poly")?Calibration.POLY2:Calibration.STRAIGHT_LINE,coefficients,"OD",mode.equals("clip"));
      ImageStatistics s=image.getStatistics(Measurements.MEAN|Measurements.MEDIAN|Measurements.STD_DEV|Measurements.MIN_MAX);
      System.out.println("{\"id\":"+ObserveCalibratedAnalysis.q(type+"_"+mode)+",\"type\":"+ObserveCalibratedAnalysis.q(type)+
        ",\"mode\":"+ObserveCalibratedAnalysis.q(mode)+",\"mean\":"+ObserveCalibratedAnalysis.number(s.mean)+
        ",\"median\":"+ObserveCalibratedAnalysis.number(s.median)+",\"stddev\":"+ObserveCalibratedAnalysis.number(s.stdDev)+
        ",\"minimum\":"+ObserveCalibratedAnalysis.number(s.min)+",\"maximum\":"+ObserveCalibratedAnalysis.number(s.max)+"}");
    }
  }
}
