import ij.ImagePlus;
import ij.measure.Calibration;
import ij.measure.CurveFitter;
import ij.process.ShortProcessor;
public class ObserveIntensitySignedLinearFits {
  public static void main(String[] args) {
    double[][] xs={{0,65535},{0,32768,65535}};
    double[][] ys={{-32768,32767},{-32768,0,32767}};
    for(int i=0;i<2;i++) {
      CurveFitter fitter=new CurveFitter(xs[i],ys[i]);fitter.doFit(CurveFitter.STRAIGHT_LINE);
      double[] p=fitter.getParams();Calibration cal=new Calibration(new ImagePlus("raw",new ShortProcessor(1,1)));
      cal.setFunction(Calibration.STRAIGHT_LINE,p,"unit");
      System.out.println("{\"measured\":"+java.util.Arrays.toString(xs[i])+",\"known\":"+java.util.Arrays.toString(ys[i])+",\"coefficients\":["+p[0]+","+p[1]+"],\"signed16\":"+cal.isSigned16Bit()+"}");
    }
  }
}
