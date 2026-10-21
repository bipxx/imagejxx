"""Capture ImageJ 1.54r indexed median-cut and global stack palettes.

Uses the preceding verified ImageJ class archive only as an owned build input.
No upstream classes, sources or runtime are shipped with the product.
"""
from pathlib import Path
import hashlib,json,subprocess,zipfile
from capture_binary_reference import ROOT, COMMIT, IMAGEJ_SHA, FIJI_SHA, verified, digest
WORK=ROOT/'build/p06zx-indexed-core001/reference002'
DRIVER=r'''
import ij.*;
import ij.process.*;
import java.awt.image.IndexColorModel;
public class IndexedColorReference {
 static int[] pixels(int pattern){
  int n=pattern==0?17:pattern==1?16:pattern==2?256:pattern==3?343:pattern==7?16384:1024;int[] a=new int[n];int state=0x13579bdf;
  int[] basic={0,0xffffff,0xff0000,0x00ff00,0x0000ff,0xffff00,0xff00ff,0x00ffff,0x010203,0xfefdfc,0x7f7f7f,0x808080,0x070707,0x080808,0x0f0f0f,0x101010};
  for(int i=0;i<n;i++){
   state=1664525*state+1013904223;
   if(pattern==0)a[i]=0x0d1113;
   else if(pattern==1)a[i]=basic[i];
   else if(pattern==2)a[i]=(i<<16)|(i<<8)|i;
   else if(pattern==3){int[] v={0,1,2,127,128,254,255};a[i]=(v[i/49]<<16)|(v[(i/7)%7]<<8)|v[i%7];}
   else if(pattern==4)a[i]=state&0xffffff;
   else if(pattern==5)a[i]=0x606060|(state&0x070707);
   else if(pattern==6)a[i]=i<900?0x3a6b9c:state&0xffffff;
   else a[i]=i<8000?0x03fe03:i<16000?0xfe0303:state&0xffffff;
  }return a;
 }
 static String rgb(int[] a){StringBuilder b=new StringBuilder("[");for(int i=0;i<a.length;i++){if(i>0)b.append(',');b.append((a[i]>>16)&255).append(',').append((a[i]>>8)&255).append(',').append(a[i]&255);}return b.append(']').toString();}
 static String indices(ImageProcessor p){StringBuilder b=new StringBuilder("[");for(int i=0;i<p.getPixelCount();i++){if(i>0)b.append(',');b.append(p.get(i));}return b.append(']').toString();}
 static String palette(IndexColorModel cm){StringBuilder b=new StringBuilder("[");for(int i=0;i<cm.getMapSize();i++){if(i>0)b.append(',');b.append('[').append(cm.getRed(i)).append(',').append(cm.getGreen(i)).append(',').append(cm.getBlue(i)).append(']');}return b.append(']').toString();}
 public static void main(String[] args){
  for(int pattern=0;pattern<8;pattern++)for(int colors:new int[]{2,3,4,7,16,31,64,128,256}){
   int[] a=pixels(pattern);ImageProcessor result=new MedianCut(a,a.length,1).convertToByte(colors);IndexColorModel cm=(IndexColorModel)result.getColorModel();
   System.out.println("{\"kind\":\"plane\",\"pattern\":"+pattern+",\"max_colors\":"+colors+",\"shape\":[1,"+a.length+"],\"input\":["+rgb(a)+"],\"indices\":["+indices(result)+"],\"palette\":"+palette(cm)+"}");
  }
  for(int count:new int[]{1,2,4,8})for(int colors:new int[]{2,3,7,16,31,64,256}){
   ImageStack stack=new ImageStack(8,4);StringBuilder input=new StringBuilder("[");
   for(int p=0;p<count;p++){int[] a=new int[32];for(int i=0;i<32;i++)a[i]=((p*29+i*17)&255)<<16|((p*51+i*7)&255)<<8|((p*13+i*31)&255);stack.addSlice("p"+p,new ColorProcessor(8,4,a));if(p>0)input.append(',');input.append(rgb(a));}
   input.append(']');ImagePlus imp=new ImagePlus("global",stack);if(count==1)new ImageConverter(imp).convertRGBtoIndexedColor(colors);else new StackConverter(imp).convertToIndexedColor(colors);IndexColorModel cm=(IndexColorModel)imp.getProcessor().getColorModel();StringBuilder output=new StringBuilder("[");
   for(int p=1;p<=count;p++){if(p>1)output.append(',');output.append(indices(imp.getStack().getProcessor(p)));}output.append(']');
   System.out.println("{\"kind\":\"stack\",\"pattern\":"+count+",\"max_colors\":"+colors+",\"shape\":[4,8],\"input\":"+input+",\"indices\":"+output+",\"palette\":"+palette(cm)+"}");
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
 driver=WORK/'IndexedColorReference.java';driver.write_text(DRIVER)
 for name,command in [('javac',[runtime/'bin/javac.exe','-cp',classes,'-d',classes,driver]),('java',[runtime/'bin/java.exe','-Djava.awt.headless=true','-cp',classes,'IndexedColorReference'])]:
  result=subprocess.run([str(v) for v in command],capture_output=True,text=True,timeout=120)
  (WORK/(name+'.stdout')).write_text(result.stdout);(WORK/(name+'.stderr')).write_text(result.stderr);result.check_returncode()
 rows=[json.loads(line) for line in result.stdout.splitlines() if line.strip()];assert len(rows)==100
 payload=dict(schema_version=1,imagej_version='1.54r',imagej_commit=COMMIT,source_archive_sha256=IMAGEJ_SHA,runtime_archive_sha256=FIJI_SHA,classes_archive=pin,driver_sha256=digest(driver),generator='scripts/capture_indexed_color_reference.py',scope='Exact ImageJ 1.54r MedianCut palettes and indices, including global StackConverter palettes. No dataset metadata/editor/format certification.',cases=rows)
 (WORK/'reference001.json').write_text(json.dumps(payload,indent=2)+'\n');print('Captured100 indexed median-cut/global-stack cases')
if __name__=='__main__':main()
