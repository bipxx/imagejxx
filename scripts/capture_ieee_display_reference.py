"""Capture Remove NaNs through the pinned ImageJ plugin runner.

Range policy qualification lives in the native tests; editor publication remains
separately guarded. No upstream runtime ships with the product.
"""
from pathlib import Path
import hashlib,json,subprocess,zipfile
from capture_binary_reference import ROOT,COMMIT,IMAGEJ_SHA,FIJI_SHA,digest,verified

WORK = ROOT / 'build/p07u-ieee-display001/reference001'
DRIVER = r'''
import ij.process.*;
import java.awt.image.BufferedImage;
public class IeeeDisplayReference {
 static final int W=8,H=4;
 static String bytes(byte[] raw){char[] d="0123456789abcdef".toCharArray();StringBuilder s=new StringBuilder();for(byte b:raw){int v=b&255;s.append(d[v>>>4]).append(d[v&15]);}return s.toString();}
 static String source(FloatProcessor p){byte[] raw=new byte[W*H*4];float[] a=(float[])p.getPixels();for(int i=0;i<a.length;i++){int w=Float.floatToRawIntBits(a[i]);for(int k=0;k<4;k++)raw[i*4+k]=(byte)(w>>>(8*k));}return bytes(raw);}
 static String doubleBits(double value){byte[] b=new byte[8];long w=Double.doubleToRawLongBits(value);for(int k=0;k<8;k++)b[k]=(byte)(w>>>(8*k));return bytes(b);}
 static FloatProcessor input(int signal){
  float[] a=new float[W*H];
  for(int i=0;i<a.length;i++){
   float v=(i-16)*.5f;
   if(signal==1)v=Float.intBitsToFloat(new int[]{0x7fc12345,0xff812345,0x7f800001,0xffc00001}[i%4]);
   if(signal==2 && i%3==0)v=Float.intBitsToFloat(0x7f800001);
   if(signal==3)v=Float.intBitsToFloat(new int[]{0xff800000,0x7f800000,0xff800001,0x7fc12345,0x00000000,0x80000000,0x3f800000,0xbf800000}[i%8]);
   if(signal==4 && i%4==0)v=Float.POSITIVE_INFINITY;
   if(signal==5 && i%4==0)v=Float.NEGATIVE_INFINITY;
   if(signal==6)v=Float.POSITIVE_INFINITY;
   if(signal==7)v=Float.NEGATIVE_INFINITY;
   if(signal==8)v=i%2==0?Float.POSITIVE_INFINITY:Float.NEGATIVE_INFINITY;
   if(signal==9)v=Float.intBitsToFloat(new int[]{0x00000000,0x80000000,0x7f800001,0xffc12345}[i%4]);
   a[i]=v;
  }return new FloatProcessor(W,H,a);
 }
 public static void main(String[] args){
  for(int signal=0;signal<10;signal++)for(int range=0;range<4;range++)for(int lut=0;lut<2;lut++)for(int threshold=0;threshold<3;threshold++){
   FloatProcessor p=input(signal);String before=source(p);
   if(range==0)p.resetMinAndMax();else if(range==1)p.setMinAndMax(0,1);else if(range==2)p.setMinAndMax(-16,16);else p.setMinAndMax(7,7);
   if(lut==1)p.invertLut();
   if(threshold==1)p.setThreshold(-1,1,ImageProcessor.RED_LUT);
   if(threshold==2)p.setThreshold(-1,1,ImageProcessor.BLACK_AND_WHITE_LUT);
   String low=doubleBits(p.getMin()),high=doubleBits(p.getMax());BufferedImage image=(BufferedImage)p.createImage();
   byte[] rgb=new byte[W*H*3];for(int i=0;i<W*H;i++){int c=image.getRGB(i%W,i/W);rgb[i*3]=(byte)(c>>>16);rgb[i*3+1]=(byte)(c>>>8);rgb[i*3+2]=(byte)c;}
   System.out.println("{\"signal\":"+signal+",\"range_mode\":"+range+",\"inverted_lut\":"+(lut==1)+",\"threshold_mode\":"+threshold+",\"min_f64le\":\""+low+"\",\"max_f64le\":\""+high+"\",\"source_before_hex\":\""+before+"\",\"source_after_hex\":\""+source(p)+"\",\"rgb_hex\":\""+bytes(rgb)+"\"}");
  }System.exit(0);
 }
}
''' 

def main():
 assert not WORK.exists();WORK.mkdir(parents=True)
 held=json.loads((ROOT/'tests/fixtures/color-components-imagej154r.json').read_bytes());assert held['imagej_commit']==COMMIT and held['source_archive_sha256']==IMAGEJ_SHA and held['runtime_archive_sha256']==FIJI_SHA
 archive=ROOT/'build/p06zs-color-stack001/reference003/classes001-scratch.zip'
 cleanup=json.loads((ROOT/'build/p06zs-color-stack001/cleanup001.json').read_bytes())
 def find(v):
  if isinstance(v,dict):
   if v.get('path')==archive.relative_to(ROOT).as_posix():return v
   for x in v.values():
    if (r:=find(x)):return r
  elif isinstance(v,list):
   for x in v:
    if (r:=find(x)):return r
 pin=find(cleanup);assert pin and archive.stat().st_size==pin['bytes'] and digest(archive)==pin['sha256']
 runtime=ROOT/'build/references/binary-imagej154r/jdk'
 for p in held['runtime_pins']:assert (runtime/p['name']).stat().st_size==p['bytes'] and digest(runtime/p['name'])==p['sha256']
 upstream=verified(ROOT/'build/references/downloads'/f'imagej-{COMMIT}.zip',IMAGEJ_SHA)
 members=[]
 with zipfile.ZipFile(upstream) as z:
  for name in ['ij/process/ImageConverter.java','ij/process/StackConverter.java','ij/process/ImageProcessor.java','ij/ImagePlus.java','ij/plugin/filter/PlugInFilterRunner.java','ij/plugin/filter/Filters.java','ij/plugin/filter/GaussianBlur.java','ij/plugin/filter/RankFilters.java','ij/plugin/LutLoader.java','ij/plugin/ContrastEnhancer.java','ij/process/ShortStatistics.java','ij/process/FloatStatistics.java','ij/process/FloatProcessor.java','ij/process/ShortProcessor.java','ij/Macro.java','ij/gui/GenericDialog.java']:
   raw=z.read(f'ImageJ-{COMMIT}/'+name);assert raw==(ROOT/'build/references/binary-imagej154r/sources'/name).read_bytes();members.append(dict(name=name,bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest()))
 classes=WORK/'classes001';classes.mkdir()
 with zipfile.ZipFile(archive) as z:
  assert z.testzip() is None
  for item in z.infolist():
   if item.filename.startswith('ij/') or item.filename.endswith('.txt'):
    target=classes/item.filename;assert target.resolve().is_relative_to(classes.resolve())
    if not item.is_dir():target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(z.read(item))
 driver=WORK/'IeeeDisplayReference.java';driver.write_text(DRIVER)
 for name,args in [('javac',[runtime/'bin/javac.exe','-cp',classes,'-d',classes,driver]),('java',[runtime/'bin/java.exe','-Djava.awt.headless=true','-cp',classes,'IeeeDisplayReference'])]:
  with (WORK/(name+'.stdout')).open('w') as out,(WORK/(name+'.stderr')).open('w') as err:r=subprocess.run([str(x) for x in args],stdout=out,stderr=err,text=True,timeout=120)
  r.check_returncode()
 rows=[json.loads(line) for line in (WORK/'java.stdout').read_text().splitlines() if line.strip()];assert len(rows)==240
 payload=dict(schema_version=1,imagej_version='1.54r',imagej_commit=COMMIT,source_archive_sha256=IMAGEJ_SHA,runtime_archive_sha256=FIJI_SHA,classes_archive=pin,source_members=members,runtime_pins=held['runtime_pins'],driver_sha256=digest(driver),generator='scripts/capture_ieee_display_reference.py',scope='Actual FloatProcessor.createImage renderer: 10 source signals, auto/manual/equal ranges, normal/inverted LUTs, none/red/binary threshold display. Raw range bits include nonfinite endpoints. Source bits and RGB bytes are observations, with no implementation or UI parity claim',cases=rows)
 (WORK/'reference001.json').write_text(json.dumps(payload,indent=2)+'\n');print('Captured 240 IEEE display observations')
 target=ROOT/'tests/fixtures/ieee-display-imagej154r.json';assert not target.exists();target.write_bytes((WORK/'reference001.json').read_bytes())

if __name__ == '__main__':main()
