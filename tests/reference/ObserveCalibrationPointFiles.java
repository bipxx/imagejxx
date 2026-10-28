import ij.plugin.TextReader;
import ij.process.ImageProcessor;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;

/** Observe the actual ImageJ TextReader used by Calibrator.open; no GUI. */
public class ObserveCalibrationPointFiles {
  static String quote(String s) {
    return "\""+s.replace("\\","\\\\").replace("\"","\\\"").replace("\n","\\n").replace("\r","\\r").replace("\t","\\t")+"\"";
  }
  public static void main(String[] args) throws Exception {
    System.setProperty("java.awt.headless","true");
    Path root=Paths.get(args[0]);Files.createDirectories(root);
    String[][] cases={
      {"two","1\t2\n3\t4\n"}, {"single","5\n7"},
      {"header","Measured,Known\r\n1,2\r\n3,4\r\n"},
      {"rounding","16777217,0.1\n+0x1.8p1f,-0\n"},
      {"blank","\n1 2\n\n3 4\n"}, {"tiny","1e-9999\n-1e-9999"},
      {"overflow","1e9999\n1\n2"}, {"nan","NaN\n1\n2"},
      {"bad_interior","1\ninvalid\n2"}, {"jagged","1 2\n3"},
      {"one_row","1\n"}, {"all_header","Measured Known\n"}
    };
    for(String[] entry:cases) {
      Path path=root.resolve(entry[0]+".txt");Files.write(path,entry[1].getBytes(StandardCharsets.UTF_8));
      TextReader reader=new TextReader();reader.hideErrorMessages();ImageProcessor ip=null;String error=null;
      try {ip=reader.open(path.toString());}catch(RuntimeException e){error=e.getClass().getName()+": "+e.getMessage();}
      String row="{\"id\":"+quote(entry[0])+",\"text\":"+quote(entry[1])+",\"image\":";
      if(ip==null)row+="null";
      else {
        row+="{\"columns\":"+ip.getWidth()+",\"rows\":"+ip.getHeight()+",\"words\":[";
        for(int y=0;y<ip.getHeight();y++)for(int x=0;x<ip.getWidth();x++) {
          if(y!=0 || x!=0)row+=",";
          row+=Integer.toUnsignedLong(Float.floatToRawIntBits(ip.getf(x,y)));
        }
        row+="]}";
      }
      System.out.println(row+",\"error\":"+(error==null?"null":quote(error))+"}");
    }
  }
}
