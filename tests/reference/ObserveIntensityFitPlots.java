import ij.measure.CurveFitter;
import ij.gui.Plot;
/** Read-only ImageJ1.54r getPlot observations. Never shows a window. */
public class ObserveIntensityFitPlots {
  static String numbers(double[] a,int n){StringBuilder s=new StringBuilder("[");for(int i=0;i<n;i++){if(i>0)s.append(',');s.append(Double.isFinite(a[i])?Double.toString(a[i]):"null");}return s.append(']').toString();}
  static String numbers(float[] a){double[] d=new double[a.length];for(int i=0;i<a.length;i++)d[i]=a[i];return numbers(d,d.length);}
  static void observe(String name,int id,double[] x,double[] y,int points) {
    CurveFitter cf=new CurveFitter(x,y);cf.doFit(id);Plot plot=cf.getPlot(points);
    double[] p=cf.getParams();int arity=CurveFitter.getNumParams(id);
    System.out.println("{\"function\":\""+name+"\",\"points\":"+points+",\"measured\":"+numbers(x,x.length)+",\"known\":"+numbers(y,y.length)+",\"coefficients\":"+numbers(p,arity)+",\"curve_x\":"+numbers(plot.getXValues())+",\"curve_y\":"+numbers(plot.getYValues())+",\"limits\":"+numbers(plot.getLimits(),4)+",\"status\":"+cf.getStatus()+"}");
  }
  public static void main(String[] args) {
    String[] names={"straight_line","poly2","poly3","poly4","exponential","power","log","rodbard","gamma_variate","log2","rodbard2","exp_with_offset","gaussian","exp_recovery"};
    double[][] coefficients={{1,2},{1,2,.1},{1,2,.1,.01},{1,2,.1,.01,.001},{3,.08},{2,.7},{2,.3},{20,1.7,7,2},{1,2,2,3},{2,3,1},{1,1.6,3,30},{10,.2,2},{1,10,10,3},{10,.2,2}};
    for(int id=0;id<14;id++) {
      double[] x=new double[21],y=new double[21];double offset=(id==5||id==6)?1:(id==9||id==10)?2:0;
      for(int i=0;i<x.length;i++){x[i]=offset+i*i/20.0;y[i]=CurveFitter.f(id,coefficients[id],x[i]);}
      for(int points:new int[]{2,100,256})observe(names[id],id,x,y,points);
    }
    double[] x=new double[1001],y=new double[1001];for(int i=0;i<x.length;i++){x[i]=i/50.;y[i]=1+2*x[i];}
    observe("straight_line",0,x,y,2);
  }
}
