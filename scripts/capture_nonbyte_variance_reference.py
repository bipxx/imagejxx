"""Capture raw UInt16/Float32 variance from the pinned private ImageJ runtime.

Only pixel arithmetic is qualified here. Final display observations are retained
to inform the separately guarded editor integration. No upstream runtime ships.
"""
from pathlib import Path
import hashlib,json,subprocess,zipfile
from capture_binary_reference import ROOT,COMMIT,IMAGEJ_SHA,FIJI_SHA,digest,verified

WORK = ROOT / 'build/p07e-nonbyte-variance001/reference001'
DRIVER = r'''
import ij.*;
import ij.process.*;
import ij.plugin.filter.*;
public class IndexedTypeReference {
 static ImageProcessor input(int type,int signal,int w,int h) {
  float[] f=new float[w*h]; short[] s=new short[w*h];
  for(int i=0;i<f.length;i++) {
   int j=(i*5+3)%17;
   float value=j;
   if(signal==1)value=j*4095;
   if(signal==2)value=1234;
   if(signal==3)value=type==0?65000+j:1000000+j*.125f;
   if(signal==4)value=type==0?(j%2)*65535:(j-8)*Float.MIN_VALUE;
   if(signal==5)value=type==0?j*j*193:(j%2==0?Float.MAX_VALUE:-Float.MAX_VALUE);
   s[i]=(short)(int)value;f[i]=value;
  }
  return type==0?new ShortProcessor(w,h,s,null):new FloatProcessor(w,h,f);
 }
 static String bits(ImageProcessor ip) {
  StringBuilder b=new StringBuilder("[");
  for(int i=0;i<ip.getPixelCount();i++) {
   if(i>0)b.append(',');
   b.append(ip instanceof FloatProcessor?Integer.toUnsignedLong(Float.floatToRawIntBits(ip.getf(i))):ip.get(i));
  }return b.append(']').toString();
 }
 public static void main(String[] args) {
  for(int type=0;type<2;type++)for(int signal=0;signal<6;signal++)for(int geometry=0;geometry<3;geometry++)
  for(double radius:new double[]{0,1,1.5,2.5,2.85,5,20})for(int rectangle=0;rectangle<2;rectangle++) {
   int w=geometry==1?1:geometry==2?9:8,h=geometry==1?9:geometry==2?1:5;
   ImageProcessor ip=input(type,signal,w,h);String before=bits(ip);
   int x=w>1?1:0,y=h>1?1:0,rw=w>2?w-2:1,rh=h>2?h-2:1;
   if(rectangle==1)ip.setRoi(x,y,rw,rh);
   new RankFilters().rank(ip,radius,RankFilters.VARIANCE);
   String after=bits(ip);
   ImagePlus imp=new ImagePlus("variance",ip);
   new RankFilters().setup("final",imp);
   System.out.println("{\"dtype\":\""+(type==0?"uint16":"float32")+"\",\"signal\":"+signal+",\"width\":"+w+",\"height\":"+h+",\"radius\":"+radius+",\"extent\":"+(rectangle==0?"null":"["+x+","+y+","+rw+","+rh+"]")+",\"input_bits\":"+before+",\"output_bits\":"+after+",\"final_range_double_bits\":["+Double.doubleToRawLongBits(imp.getDisplayRangeMin())+","+Double.doubleToRawLongBits(imp.getDisplayRangeMax())+"]}");
  }
  System.exit(0);
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
  for name in ['ij/process/ImageConverter.java','ij/process/StackConverter.java','ij/process/ImageProcessor.java','ij/ImagePlus.java','ij/plugin/filter/PlugInFilterRunner.java','ij/plugin/filter/Filters.java','ij/plugin/filter/GaussianBlur.java','ij/plugin/filter/RankFilters.java','ij/plugin/LutLoader.java','ij/plugin/ContrastEnhancer.java','ij/process/ShortStatistics.java','ij/process/FloatStatistics.java']:
   raw=z.read(f'ImageJ-{COMMIT}/'+name);assert raw==(ROOT/'build/references/binary-imagej154r/sources'/name).read_bytes();members.append(dict(name=name,bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest()))
 classes=WORK/'classes001';classes.mkdir()
 with zipfile.ZipFile(archive) as z:
  assert z.testzip() is None
  for item in z.infolist():
   if item.filename.startswith('ij/') or item.filename.endswith('.txt'):
    target=classes/item.filename;assert target.resolve().is_relative_to(classes.resolve())
    if not item.is_dir():target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(z.read(item))
 driver=WORK/'IndexedTypeReference.java';driver.write_text(DRIVER)
 for name,args in [('javac',[runtime/'bin/javac.exe','-cp',classes,'-d',classes,driver]),('java',[runtime/'bin/java.exe','-Djava.awt.headless=true','-cp',classes,'IndexedTypeReference'])]:
  with (WORK/(name+'.stdout')).open('w') as out,(WORK/(name+'.stderr')).open('w') as err:r=subprocess.run([str(x) for x in args],stdout=out,stderr=err,text=True,timeout=120)
  r.check_returncode()
 rows=[json.loads(line) for line in (WORK/'java.stdout').read_text().splitlines() if line.strip()];assert len(rows)==504
 payload=dict(schema_version=1,imagej_version='1.54r',imagej_commit=COMMIT,source_archive_sha256=IMAGEJ_SHA,runtime_archive_sha256=FIJI_SHA,classes_archive=pin,source_members=members,runtime_pins=held['runtime_pins'],driver_sha256=digest(driver),generator='scripts/capture_nonbyte_variance_reference.py',scope='Raw UInt16/finite-input Float32 variance; whole/rectangular, 2D and single-axis, seven radii, six signals including overflow. Final display bits are observations only, not editor qualification.',cases=rows)
 (WORK/'reference001.json').write_text(json.dumps(payload,indent=2)+'\n');print('Captured 504 nonbyte variance cases')
 target=ROOT/'tests/fixtures/nonbyte-variance-imagej154r.json';assert not target.exists();target.write_bytes((WORK/'reference001.json').read_bytes())

if __name__ == '__main__':main()
