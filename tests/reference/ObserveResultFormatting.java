import ij.measure.ResultsTable;
/** Original numeric presentation only; no image kernels or runtime dependency. */
public class ObserveResultFormatting {
  static String quote(String s) {return "\""+s.replace("\\","\\\\").replace("\"","\\\"")+"\"";}
  public static void main(String[] args) {
    double[] values={0.0,-0.0,1,-1,1.25,1.35,2.5,-2.5,3.5,-3.5,2.675,1.005,1.23456789,-1.23456789,
      9.999999999,9.9995,9.999999999999998,0.0001,Math.nextDown(0.0001),Math.nextUp(0.0001),
      1e-10,1e-100,Double.MIN_VALUE,Double.MIN_NORMAL,Double.MAX_VALUE,Float.MAX_VALUE,
      -(double)Float.MAX_VALUE,999999999999d,Math.nextUp(999999999999d),1e12,1e20,1e100,
      Double.NaN,Double.POSITIVE_INFINITY,Double.NEGATIVE_INFINITY};
    double[] expanded=java.util.Arrays.copyOf(values,values.length+160);int at=values.length;
    for(long bits=1;bits<=32;bits++){expanded[at++]=Double.longBitsToDouble(bits);expanded[at++]=-Double.longBitsToDouble(bits);}
    expanded[at++]=Math.nextDown(Double.MIN_NORMAL);expanded[at++]=-Math.nextDown(Double.MIN_NORMAL);
    expanded[at++]=1e-320;expanded[at++]=-1e-320;
    for(int bit=6;bit<52;bit++){expanded[at++]=Double.longBitsToDouble(1L<<bit);expanded[at++]=-Double.longBitsToDouble(1L<<bit);}
    values=expanded;
    System.out.print("{\"schema\":\"imagejxx.original-result-format.001\",\"reference\":\"ImageJ1.54r\",\"cases\":[");
    boolean first=true;
    for(int precision=0;precision<=9;precision++)for(double value:values) {
      if(!first)System.out.print(",");first=false;
      String bits=Long.toHexString(Double.doubleToRawLongBits(value));
      ResultsTable auto=new ResultsTable();auto.setPrecision(-precision);auto.incrementCounter();auto.addValue("n",value);
      ResultsTable explicit=new ResultsTable();explicit.setPrecision(-precision);explicit.incrementCounter();explicit.addValue("n",value);explicit.setDecimalPlaces(explicit.getColumnIndex("n"),-precision);
      System.out.print("{\"bits\":"+quote(bits)+",\"precision\":"+precision+",\"scientific\":"+quote(ResultsTable.d2s(value,-precision))+",\"ordinary\":"+quote(ResultsTable.d2s(value,precision))+",\"table_auto\":"+quote(auto.getStringValue("n",0))+",\"table_explicit\":"+quote(explicit.getStringValue("n",0))+"}");
    }
    System.out.println("]}");
  }
}
