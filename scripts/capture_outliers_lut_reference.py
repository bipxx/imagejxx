"""Capture Remove Outliers through the pinned ImageJ plugin runner.

Range policy qualification lives in the native tests; editor publication remains
separately guarded. No upstream runtime ships with the product.
"""
from pathlib import Path
import hashlib,json,subprocess,zipfile
from capture_binary_reference import ROOT,COMMIT,IMAGEJ_SHA,FIJI_SHA,digest,verified

WORK = ROOT / 'build/p07l-outliers-editor001/reference001'
DRIVER = r'''
import ij.*;
import ij.gui.*;
import ij.process.*;
import ij.plugin.filter.*;
import java.awt.image.IndexColorModel;
public class IndexedTypeReference {
 static byte[] red=new byte[256],green=new byte[256],blue=new byte[256];
 static LUT table(int kind){
  for(int i=0;i<256;i++){
   int r=kind==0?i:kind==2?0:255-i,g=r,b=r;
   if(kind==3 && i==255)r=g=b=255;
   if(kind==4 && i==254)r=g=b=255;
   if(kind==5){g=0;b=0;}
   if(kind==6){r=255-i;g=i;b=0;}
   if(kind==7 && i==1)r=g=b=255;
   red[i]=(byte)r;green[i]=(byte)g;blue[i]=(byte)b;
  }return new LUT(red,green,blue);
 }
 static String palette(){StringBuilder b=new StringBuilder("[");for(int i=0;i<256;i++){if(i>0)b.append(',');b.append('[').append(red[i]&255).append(',').append(green[i]&255).append(',').append(blue[i]&255).append(']');}return b.append(']').toString();}
 static ImageProcessor image(int type){ImageProcessor ip=type==0?new ByteProcessor(3,3):type==1?new ShortProcessor(3,3):new FloatProcessor(3,3);for(int i=0;i<9;i++)ip.setf(i,i==4?100:40);return ip;}
 static String values(ImageProcessor ip){StringBuilder b=new StringBuilder("[");for(int i=0;i<9;i++){if(i>0)b.append(',');b.append(ip.getf(i));}return b.append(']').toString();}
 static void capture(ImagePlus imp,int type,int kind,int mode,int channel,int threshold){
  ImageProcessor ip=imp.getProcessor();
  if(threshold==1)ip.setThreshold(4,8,ImageProcessor.RED_LUT);
  boolean inverted=ip.isInvertedLut();String before=values(ip);
  new RankFilters().rank(ip,1,RankFilters.OUTLIERS,RankFilters.BRIGHT_OUTLIERS,50f);
  System.out.println("{\"dtype\":\""+(type==0?"uint8":type==1?"uint16":"float32")+"\",\"table\":"+kind+",\"mode\":"+mode+",\"channel\":"+channel+",\"threshold\":"+threshold+",\"palette\":"+palette()+",\"inverted\":"+inverted+",\"before\":"+before+",\"after\":"+values(ip)+"}");
 }
 public static void main(String[] args){Prefs.setThreads(1);
  for(int type=0;type<3;type++)for(int kind=0;kind<8;kind++)for(int threshold=0;threshold<2;threshold++){
   ImageProcessor ip=image(type);ip.setColorModel(table(kind));capture(new ImagePlus("lut",ip),type,kind,0,0,threshold);
  }
  for(int mode=1;mode<=3;mode++)for(int channel=1;channel<=2;channel++)for(int threshold=0;threshold<2;threshold++){
   ImageStack stack=new ImageStack(3,3);stack.addSlice(image(1));stack.addSlice(image(1));
   ImagePlus base=new ImagePlus("channels",stack);base.setDimensions(2,1,1);base.setOpenAsHyperStack(true);
   CompositeImage imp=new CompositeImage(base,mode);imp.setChannelLut(table(0),1);imp.setChannelLut(table(1),2);imp.setMode(mode);imp.setPosition(channel,1,1);table(channel-1);
   capture(imp,1,channel-1,mode,channel-1,threshold);
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
  for name in ['ij/process/ImageConverter.java','ij/process/StackConverter.java','ij/process/ImageProcessor.java','ij/ImagePlus.java','ij/plugin/filter/PlugInFilterRunner.java','ij/plugin/filter/Filters.java','ij/plugin/filter/GaussianBlur.java','ij/plugin/filter/RankFilters.java','ij/plugin/LutLoader.java','ij/plugin/ContrastEnhancer.java','ij/process/ShortStatistics.java','ij/process/FloatStatistics.java','ij/process/FloatProcessor.java','ij/process/ShortProcessor.java','ij/Macro.java','ij/gui/GenericDialog.java']:
   raw=z.read(f'ImageJ-{COMMIT}/'+name);assert raw==(ROOT/'build/references/binary-imagej154r/sources'/name).read_bytes();members.append(dict(name=name,bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest()))
 classes=WORK/'classes001';classes.mkdir()
 with zipfile.ZipFile(archive) as z:
  assert z.testzip() is None
  for item in z.infolist():
   if item.filename.startswith('ij/') or item.filename.endswith('.txt'):
    target=classes/item.filename;assert target.resolve().is_relative_to(classes.resolve())
    if not item.is_dir():target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(z.read(item))
 driver=WORK/'IndexedTypeReference.java';driver.write_text(DRIVER)
 for name,args in [('javac',[runtime/'bin/javac.exe','-cp',classes,'-d',classes,driver]),('java',[runtime/'bin/java.exe','-Djava.awt.headless=false','-cp',classes,'IndexedTypeReference'])]:
  with (WORK/(name+'.stdout')).open('w') as out,(WORK/(name+'.stderr')).open('w') as err:r=subprocess.run([str(x) for x in args],stdout=out,stderr=err,text=True,timeout=120)
  r.check_returncode()
 rows=[json.loads(line) for line in (WORK/'java.stdout').read_text().splitlines() if line.strip()];assert len(rows)==60
 payload=dict(schema_version=1,imagej_version='1.54r',imagej_commit=COMMIT,source_archive_sha256=IMAGEJ_SHA,runtime_archive_sha256=FIJI_SHA,classes_archive=pin,source_members=members,runtime_pins=held['runtime_pins'],driver_sha256=digest(driver),generator='scripts/capture_outliers_lut_reference.py',scope='Actual ImageJ processor LUT inversion and rank observations: eight explicit custom tables, threshold off/red overlay, scalar U8/U16/F32 and active C0/C1 in composite/color/grayscale. Scientific multi-plane frozen active polarity is an owned extension',cases=rows)
 (WORK/'reference001.json').write_text(json.dumps(payload,indent=2)+'\n');print('Captured 60 processor LUT / outlier observations')
 target=ROOT/'tests/fixtures/outliers-lut-imagej154r.json';assert not target.exists();target.write_bytes((WORK/'reference001.json').read_bytes())

if __name__ == '__main__':main()
