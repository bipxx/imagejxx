import ij.measure.CurveFitter;
/** Read-only pinned ImageJ nonlinear fitting observations, retaining failed fits. */
public class ObserveIntensityNonlinearFits {
  static String numbers(double[] a,int n){StringBuilder s=new StringBuilder();for(int i=0;i<n;i++){if(i>0)s.append(',');double v=a[i];s.append(Double.isFinite(v)?Double.toString(v):"null");}return "["+s+"]";}
  public static void main(String[] args) {
    String[] names={"exponential","power","log","rodbard","gamma_variate","log2","rodbard2","exp_with_offset","gaussian","exp_recovery"};
    double[][] coefficients={{3,.08},{2,.7},{2,.3},{20,1.7,7,2},{1,2,2,3},{2,3,1},{1,1.6,3,30},{10,.2,2},{1,10,10,3},{10,.2,2}};
    for(int model=0;model<10;model++)for(boolean noisy:new boolean[]{false,true}) {
      int id=model+4;double[] x=new double[21],y=new double[21];
      double offset=(id==5||id==6)?1:(id==9||id==10)?2:0;
      double scale=1;for(int i=0;i<x.length;i++){x[i]=offset+i;y[i]=CurveFitter.f(id,coefficients[model],x[i]);scale=Math.max(scale,Math.abs(y[i]));}
      if(noisy)for(int i=0;i<y.length;i++)y[i]+=((i*7)%5-2)*0.001*scale;
      CurveFitter cf=new CurveFitter(x,y);cf.doFit(id);double[] p=cf.getParams();int n=coefficients[model].length;
      double sse=0;for(int i=0;i<x.length;i++){double prediction=id==10?CurveFitter.f(7,p,y[i]):CurveFitter.f(id,p,x[i]);double r=prediction-(id==10?x[i]:y[i]);sse+=r*r;}
      double r2=cf.getRSquared();
      System.out.println("{\"function\":\""+names[model]+"\",\"noisy\":"+noisy+",\"measured\":"+numbers(x,x.length)+",\"known\":"+numbers(y,y.length)+",\"true_coefficients\":"+numbers(coefficients[model],n)+",\"coefficients\":"+numbers(p,n)+",\"sse\":"+(Double.isFinite(sse)?Double.toString(sse):"null")+",\"r_squared\":"+(Double.isFinite(r2)?Double.toString(r2):"null")+",\"status\":"+cf.getStatus()+"}");
    }
  }
}
