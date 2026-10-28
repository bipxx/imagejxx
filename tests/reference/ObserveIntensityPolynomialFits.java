import ij.measure.CurveFitter;
import java.util.Locale;
/** Read-only polynomial fitting observation against pinned ImageJ1.54r. */
public class ObserveIntensityPolynomialFits {
  static String numbers(double[] a,int n){StringBuilder s=new StringBuilder();for(int i=0;i<n;i++){if(i>0)s.append(',');s.append(Double.toString(a[i]));}return "["+s+"]";}
  public static void main(String[] args){
    String[] names={"straight_line","poly2","poly3","poly4"};
    double[] x={0,1,2,4,6,8,10,12,16};
    for(int degree=1;degree<=4;degree++)for(boolean noisy:new boolean[]{false,true}) {
      double[] coefficients={5,2,-0.125,0.0078125,-0.000244140625};double[] y=new double[x.length];
      for(int i=0;i<x.length;i++){double v=coefficients[degree];for(int j=degree-1;j>=0;j--)v=v*x[i]+coefficients[j];y[i]=v+(noisy?((i*7)%5-2)*0.03:0);}
      CurveFitter fitter=new CurveFitter(x,y);fitter.doFit(degree-1);double[] p=fitter.getParams();
      System.out.println("{\"function\":\""+names[degree-1]+"\",\"noisy\":"+noisy+",\"measured\":"+numbers(x,x.length)+",\"known\":"+numbers(y,y.length)+",\"coefficients\":"+numbers(p,degree+1)+",\"r_squared\":"+fitter.getRSquared()+",\"status\":"+fitter.getStatus()+"}");
    }
  }
}
