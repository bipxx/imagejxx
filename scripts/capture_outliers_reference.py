"""Capture Remove Outliers through the pinned ImageJ plugin runner.

Range policy qualification lives in the native tests; editor publication remains
separately guarded. No upstream runtime ships with the product.
"""
from pathlib import Path
import hashlib,json,subprocess,zipfile
from capture_binary_reference import ROOT,COMMIT,IMAGEJ_SHA,FIJI_SHA,digest,verified

WORK = ROOT / 'build/p07k-outliers001/reference003'
DRIVER = r'''
import ij.*;
import ij.process.*;
import ij.gui.*;
import ij.plugin.filter.*;
public class IndexedTypeReference {
 static final int W=25,H=20;
 static String hex(ImageProcessor ip) {
  char[] digits="0123456789abcdef".toCharArray();StringBuilder b=new StringBuilder();
  int bytes=ip instanceof FloatProcessor?4:ip instanceof ByteProcessor?1:2;
  for(int i=0;i<W*H;i++) {
   int word=bytes==4?Float.floatToRawIntBits(ip.getf(i)):ip.get(i);
   for(int j=0;j<bytes;j++){int v=(word>>>(j*8))&255;b.append(digits[v>>>4]).append(digits[v&15]);}
  }return b.toString();
 }
 static String pixels(ImagePlus imp) {
  StringBuilder b=new StringBuilder("[");
  for(int p=1;p<=imp.getStackSize();p++){if(p>1)b.append(',');b.append('"').append(hex(imp.getStack().getProcessor(p))).append('"');}
  return b.append(']').toString();
 }
 static ImagePlus input(int type,int signal) {
  ImageStack stack=new ImageStack(W,H);
  for(int p=0;p<3;p++) {
   ImageProcessor ip=type==0?new ByteProcessor(W,H):type==1?new ShortProcessor(W,H):new FloatProcessor(W,H);
   for(int i=0;i<W*H;i++) {
    int j=(i*37+p*11)%127;
    float value=signal==0?j:signal==1?(j%3==0?0:j%7==0?255:80):signal==2?7:type==2?(j-63)*.25f:j*(type==0?2:503);
    ip.setf(i,value);
   }stack.addSlice(ip);
  }
  ImagePlus imp=new ImagePlus("variance-final",stack);imp.setSlice(2);return imp;
 }
 static void roi(ImagePlus imp,int kind) {
  if(kind==1)imp.setRoi(new Roi(2,3,19,13));
  if(kind==2)imp.setRoi(new PolygonRoi(new int[]{2,20,18,5},new int[]{3,2,17,15},4,Roi.POLYGON));
 }
 static String threshold(ImagePlus imp) {ImageProcessor ip=imp.getProcessor();return ip.getMinThreshold()==ImageProcessor.NO_THRESHOLD?"null":"["+ip.getMinThreshold()+","+ip.getMaxThreshold()+"]";}
 static String range(ImagePlus imp) {return "["+imp.getDisplayRangeMin()+","+imp.getDisplayRangeMax()+"]";}
 public static void main(String[] args) {
  Thread.currentThread().setName("Run$_variance-reference");Prefs.setThreads(1);
  for(int type=0;type<3;type++)for(int signal=0;signal<4;signal++)for(int variant=0;variant<8;variant++)
  for(int selection=0;selection<3;selection++) {
   int mode=variant%2,which=(variant/2)%2;boolean inverted=variant>=4;
   double radius=new double[]{0,0.5,1.75,4.25}[variant%4];
   double limit=signal==0?0:signal==1?50:signal==2?0.125:16.000001;
   ImagePlus imp=input(type,signal);roi(imp,selection);imp.setDisplayRange(7,127);
   if(inverted){imp.getProcessor().invertLut();imp.getStack().setColorModel(imp.getProcessor().getColorModel());}
   if(imp.getProcessor().isInvertedLut()!=inverted)throw new RuntimeException("Processor inversion setup failed");
   imp.getProcessor().setThreshold(4,8,ImageProcessor.NO_LUT_UPDATE);
   String before=pixels(imp),initialRange=range(imp);
   boolean observedInverted=imp.getProcessor().isInvertedLut();
   for(int p=1;p<=imp.getStackSize();p++)if(imp.getStack().getProcessor(p).isInvertedLut()!=inverted)throw new RuntimeException("Stack inversion setup failed");
   String initialThreshold=threshold(imp);
   WindowManager.setTempCurrentImage(imp);Macro.setOptions("radius="+radius+" threshold="+limit+" which="+(which==0?"Bright":"Dark")+" "+(mode==1?"stack":"slice"));
   new PlugInFilterRunner(new RankFilters(),"Remove Outliers","outliers");Macro.setOptions(null);
   System.out.println("{\"dtype\":\""+(type==0?"uint8":type==1?"uint16":"float32")+"\",\"signal\":"+signal+",\"mode\":"+mode+",\"selection\":"+selection+",\"radius\":"+radius+",\"threshold\":"+limit+",\"which\":\""+(which==0?"bright":"dark")+"\",\"inverted_lut\":"+observedInverted+",\"input_hex\":"+before+",\"output_hex\":"+pixels(imp)+",\"initial_range\":"+initialRange+",\"initial_threshold\":"+initialThreshold+",\"final_threshold\":"+threshold(imp)+",\"final_range\":"+range(imp)+",\"current_slice\":"+imp.getCurrentSlice()+",\"roi_retained\":"+(imp.getRoi()!=null)+"}");
   WindowManager.setTempCurrentImage(null);imp.close();
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
 rows=[json.loads(line) for line in (WORK/'java.stdout').read_text().splitlines() if line.strip()];assert len(rows)==288
 payload=dict(schema_version=1,imagej_version='1.54r',imagej_commit=COMMIT,source_archive_sha256=IMAGEJ_SHA,runtime_archive_sha256=FIJI_SHA,classes_archive=pin,source_members=members,runtime_pins=held['runtime_pins'],driver_sha256=digest(driver),generator='scripts/capture_outliers_reference.py',scope='Actual Remove Outliers plugin runner: finite UInt8/UInt16/Float32, 8 radius/scope/polarity/LUT variants, 4 signals, whole/rectangular/polygon selections. No UI/command parity claim',cases=rows)
 (WORK/'reference001.json').write_text(json.dumps(payload,indent=2)+'\n');print('Captured 288 Remove Outliers plugin-runner cases')
 target=ROOT/'tests/fixtures/outliers-imagej154r.json';assert not target.exists();target.write_bytes((WORK/'reference001.json').read_bytes())

if __name__ == '__main__':main()
