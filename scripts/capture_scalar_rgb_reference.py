"""Capture ordinary ImageJ 1.54r scalar stack RGB range/LUT and C-fast layout.

Uses the preceding verified ImageJ class archive only as an owned build input.
No upstream classes, sources or runtime are shipped with the product.
"""
from pathlib import Path
import hashlib,json,subprocess,zipfile
from capture_binary_reference import ROOT, COMMIT, IMAGEJ_SHA, FIJI_SHA, verified, digest
WORK=ROOT/'build/p06zw-scalar-flatten001/reference002'
DRIVER=r'''
import ij.*;
import ij.process.*;
import java.awt.image.IndexColorModel;
public class ScalarRgbReference {
 static String values(ImageProcessor p) {
  StringBuilder b=new StringBuilder("[");for(int i=0;i<p.getPixelCount();i++){
   if(i>0)b.append(',');if(p instanceof ColorProcessor){int v=p.get(i);b.append((v>>16)&255).append(',').append((v>>8)&255).append(',').append(v&255);}else b.append(p.getf(i));
  }return b.append(']').toString();
 }
 static String planes(ImageStack s){StringBuilder b=new StringBuilder("[");for(int i=1;i<=s.size();i++){if(i>1)b.append(',');b.append(values(s.getProcessor(i)));}return b.append(']').toString();}
 static IndexColorModel palette(int kind){byte[] r=new byte[256],g=new byte[256],b=new byte[256];for(int i=0;i<256;i++){r[i]=(byte)(kind==1?255-i:i);g[i]=(byte)(kind==2?(17*i)&255:kind==1?255-i:i);b[i]=(byte)(kind==2?255-i:kind==1?255-i:i);}return new IndexColorModel(8,256,r,g,b);}
 public static void main(String[] args){
  for(String type:new String[]{"uint8","uint16","float32"})for(int[] d:new int[][]{{1,1,1},{1,3,1},{1,2,2},{2,2,1},{2,2,2},{3,1,2}})for(int range=0;range<4;range++)for(int lut=0;lut<3;lut++){
   int c=d[0],z=d[1],t=d[2];ImageStack stack=new ImageStack(3,2);
   for(int i=0;i<c*z*t;i++){
    ImageProcessor p;if(type.equals("uint8")){byte[] a=new byte[6];int[] base={0,17,63,128,200,255};for(int k=0;k<6;k++)a[k]=(byte)(base[k]+i*7);p=new ByteProcessor(3,2,a,null);}
    else if(type.equals("uint16")){short[] a=new short[6];int[] base={0,16,256,512,1024,4096};for(int k=0;k<6;k++)a[k]=(short)(base[k]+i*2000);p=new ShortProcessor(3,2,a,null);}
    else {float[] a={-10+i*100,-2+i*100,i*100,1+i*100,17+i*100,50+i*100};p=new FloatProcessor(3,2,a);}
    stack.addSlice("plane-"+i,p);
   }
   ImagePlus imp=new ImagePlus("scalar",stack);imp.setDimensions(c,z,t);imp.setPosition(c,z,t);
   imp.getProcessor().setColorModel(palette(lut));imp.getStack().setColorModel(palette(lut));
   if(range==1)imp.setDisplayRange(type.equals("uint8")?17:type.equals("uint16")?100:-5,type.equals("uint8")?200:type.equals("uint16")?6000:200);
   else if(range==2)imp.setDisplayRange(0,0); else if(range==3)imp.getProcessor().resetMinAndMax();
   double lo=imp.getDisplayRangeMin(),hi=imp.getDisplayRangeMax();String input=planes(imp.getStack());
   if(c*z*t==1)new ImageConverter(imp).convertToRGB();else new StackConverter(imp).convertToRGB();
   System.out.println("{\"type\":\""+type+"\",\"source_czt\":["+c+","+z+","+t+"],\"range_kind\":"+range+",\"lut\":"+lut+",\"opening_range\":["+lo+","+hi+"],\"input\":"+input+",\"output_czt\":["+imp.getNChannels()+","+imp.getNSlices()+","+imp.getNFrames()+"],\"output\":"+planes(imp.getStack())+"}");
  }
 }
}
'''
def main():
 assert not WORK.exists();WORK.mkdir(parents=True)
 held=json.loads((ROOT/'tests/fixtures/color-components-imagej154r.json').read_bytes())
 assert held['imagej_commit']==COMMIT and held['source_archive_sha256']==IMAGEJ_SHA and held['runtime_archive_sha256']==FIJI_SHA
 cleanup=json.loads((ROOT/'build/p06zs-color-stack001/cleanup001.json').read_bytes())
 archive=ROOT/'build/p06zs-color-stack001/reference003/classes001-scratch.zip'
 def find(value):
  if isinstance(value,dict):
   if value.get('path')==archive.relative_to(ROOT).as_posix():return value
   for v in value.values():
    if (r:=find(v)):return r
  if isinstance(value,list):
   for v in value:
    if (r:=find(v)):return r
 pin=find(cleanup);assert pin and archive.stat().st_size==pin['bytes'] and digest(archive)==pin['sha256']
 runtime=ROOT/'build/references/binary-imagej154r/jdk'
 for p in held['runtime_pins']:assert (runtime/p['name']).stat().st_size==p['bytes'] and digest(runtime/p['name'])==p['sha256']
 classes=WORK/'classes001';classes.mkdir()
 with zipfile.ZipFile(archive) as z:
  assert z.testzip() is None
  for item in z.infolist():
   if item.filename.startswith('ij/') or item.filename.endswith('.txt'):
    target=classes/item.filename;assert target.resolve().is_relative_to(classes.resolve())
    if not item.is_dir():target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(z.read(item))
 driver=WORK/'ScalarRgbReference.java';driver.write_text(DRIVER)
 for name,command in [('javac',[runtime/'bin/javac.exe','-cp',classes,'-d',classes,driver]),('java',[runtime/'bin/java.exe','-Djava.awt.headless=true','-cp',classes,'ScalarRgbReference'])]:
  result=subprocess.run([str(v) for v in command],capture_output=True,text=True,timeout=120)
  (WORK/(name+'.stdout')).write_text(result.stdout);(WORK/(name+'.stderr')).write_text(result.stderr);result.check_returncode()
 rows=[json.loads(line) for line in result.stdout.splitlines() if line.strip()];assert len(rows)==216
 payload=dict(schema_version=1,imagej_version='1.54r',imagej_commit=COMMIT,source_archive_sha256=IMAGEJ_SHA,runtime_archive_sha256=FIJI_SHA,classes_archive=pin,driver_sha256=digest(driver),generator='scripts/capture_scalar_rgb_reference.py',scope='Ordinary non-composite U8/U16/F32 stack RGB conversion with opening processor ranges, common LUT, ImageJ C-fast layout. Native annotation/calibration consent, channel-renderer extension and Float64 are separate contracts.',cases=rows)
 (WORK/'reference001.json').write_text(json.dumps(payload,indent=2)+'\n');print('Captured216 scalar stack RGB cases')
if __name__=='__main__':main()
