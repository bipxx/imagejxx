import ij.measure.ResultsTable;
import java.lang.reflect.Field;
import java.nio.file.Path;

/** Original allocation-time defaults, not a native-generated oracle. */
public class ObserveNaNEmptyCells {
  static boolean first=true;
  static final int[] probes={0,1,49,98,99,100,198,199,200,204};
  static final Field capacity;
  static {try {capacity=ResultsTable.class.getDeclaredField("maxRows");capacity.setAccessible(true);}catch(Exception e){throw new RuntimeException(e);}}
  static String q(String s) {
    if(s==null)return "null";
    return "\""+s.replace("\\","\\\\").replace("\"","\\\"").replace("\r","\\r").replace("\n","\\n").replace("\t","\\t")+"\"";
  }
  static String number(double v) {return Double.isFinite(v)?Double.toString(v):q(Double.isNaN(v)?"NaN":v>0?"Infinity":"-Infinity");}
  static void emit(String text) {if(!first)System.out.print(',');first=false;System.out.print(text);}
  static void observe(boolean initial,int toggle,int late,String directory)throws Exception {
    ResultsTable rt=new ResultsTable();rt.setNaNEmptyCells(initial);rt.setPrecision(3);
    StringBuilder growth=new StringBuilder("[");boolean sep=false;
    for(int row=0;row<205;row++) {
      if(row==toggle)rt.setNaNEmptyCells(!initial);
      int before=capacity.getInt(rt);rt.incrementCounter();
      if(capacity.getInt(rt)!=before){if(sep)growth.append(',');sep=true;growth.append('[').append(row).append(',').append(capacity.getInt(rt)).append(']');}
      rt.addValue("Counter",row);
      if(row==0)rt.addValue("Early",1.25);
      if(row==late)rt.addValue("Late",7.25);
    }
    growth.append(']');
    StringBuilder out=new StringBuilder("{\"id\":"+q("initial"+initial+"-toggle"+toggle+"-late"+late)+",\"initial\":"+initial+",\"toggle_before\":"+toggle+",\"late_at\":"+late+",\"rows\":205,\"capacity\":"+capacity.getInt(rt)+",\"growth\":"+growth+",\"cells\":[");
    sep=false;
    for(int row:probes) {
      if(sep)out.append(',');sep=true;
      out.append("{\"row\":").append(row).append(",\"early\":").append(number(rt.getValue("Early",row))).append(",\"late\":").append(number(rt.getValue("Late",row))).append(",\"early_text\":").append(q(rt.getStringValue("Early",row))).append(",\"late_text\":").append(q(rt.getStringValue("Late",row))).append('}');
    }
    ResultsTable clone=(ResultsTable)rt.clone();clone.incrementCounter();clone.addValue("Counter",205);
    out.append("],\"clone_next_early\":").append(number(clone.getValue("Early",205))).append(",\"clone_next_late\":").append(number(clone.getValue("Late",205)));
    if(toggle==99 && late==100) {
      Path file=Path.of(directory,"allocation-"+initial+".csv");rt.saveAs(file.toString());ResultsTable read=ResultsTable.open(file.toString());
      out.append(",\"csv_reopened\":[").append(number(read.getValue("Early",100))).append(',').append(number(read.getValue("Late",0))).append(']');
    }
    rt.incrementCounter();rt.addValue("Counter",205);
    out.append(",\"next_early\":").append(number(rt.getValue("Early",205))).append(",\"next_late\":").append(number(rt.getValue("Late",205)));
    emit(out.append('}').toString());
  }
  static void reset(boolean flag) {
    ResultsTable rt=new ResultsTable();rt.setNaNEmptyCells(flag);
    for(int n=0;n<105;n++){rt.incrementCounter();rt.addValue("Old",n);}
    rt.reset();for(int n=0;n<4;n++){rt.incrementCounter();if(n==3)rt.addValue("New",3.25);}
    emit("{\"id\":"+q("reset-"+flag)+",\"reset\":true,\"initial\":"+flag+",\"rows\":4,\"capacity\":"+getCapacity(rt)+",\"old_column\":"+rt.columnExists("Old")+",\"missing\":"+number(rt.getValue("New",0))+"}");
  }
  static int getCapacity(ResultsTable rt) {try{return capacity.getInt(rt);}catch(Exception e){throw new RuntimeException(e);}}
  static String stringCase(boolean flag) {
    ResultsTable rt=new ResultsTable();rt.setNaNEmptyCells(flag);rt.setPrecision(3);
    for(int row=0;row<4;row++){rt.incrementCounter();if(row==0)rt.addValue("Mixed","word");if(row==2)rt.addValue("Mixed",4.5);if(row==3){rt.addValue("Mixed","");rt.addLabel("label");}}
    StringBuilder out=new StringBuilder("{\"initial\":"+flag+",\"values\":[");
    for(int row=0;row<4;row++){if(row>0)out.append(',');out.append(q(rt.getStringValue("Mixed",row)));}
    return out.append("],\"label\":").append(q(rt.getLabel(3))).append('}').toString();
  }
  public static void main(String[] args)throws Exception {
    System.out.print("{\"schema\":\"imagejxx.original-nan-empty-cells.001\",\"reference\":\"ImageJ1.54r\",\"cases\":[");
    for(boolean initial:new boolean[]{false,true})for(int toggle:new int[]{-1,0,1,49,98,99,100,198,199,200})for(int late:new int[]{0,1,49,99,100,199})observe(initial,toggle,late,args[0]);
    reset(false);reset(true);
    System.out.print("],\"string_cases\":["+stringCase(false)+","+stringCase(true)+"]}");
  }
}
