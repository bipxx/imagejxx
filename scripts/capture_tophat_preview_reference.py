"""Capture direct RankFilters preview-mode calls; no physical UI claim.

No upstream runtime ships with the product. Pixel and final-display observations
are retained; editor publication is a separate qualification gate.
"""
from pathlib import Path
import hashlib,json,subprocess,zipfile
from capture_binary_reference import ROOT,COMMIT,IMAGEJ_SHA,FIJI_SHA,digest,verified

WORK = ROOT / 'build/p07n-tophat-command001/reference-preview003'
DRIVER = r'''
import ij.*;import ij.process.*;import ij.gui.*;import ij.plugin.filter.*;
public class IndexedTypeReference {
 static final int W=5,H=5;
 static void field(RankFilters f,String name,Object value)throws Exception{java.lang.reflect.Field p=RankFilters.class.getDeclaredField(name);p.setAccessible(true);p.set(f,value);}
 static String hex(ImageProcessor ip){StringBuilder b=new StringBuilder();int n=ip instanceof FloatProcessor?4:ip instanceof ByteProcessor?1:2;for(int i=0;i<W*H;i++){int v=n==4?Float.floatToRawIntBits(ip.getf(i)):ip.get(i);for(int j=0;j<n;j++)b.append(String.format("%02x",(v>>>(8*j))&255));}return b.toString();}
 static String threshold(ImageProcessor ip){return ip.getMinThreshold()==ImageProcessor.NO_THRESHOLD?"null":"["+ip.getMinThreshold()+","+ip.getMaxThreshold()+"]";}
 static String range(ImageProcessor ip){return "["+ip.getMin()+","+ip.getMax()+"]";}
 public static void main(String[]args)throws Exception{Prefs.setThreads(1);Prefs.useInvertingLut=false;
 for(int type=0;type<3;type++)for(int signal=0;signal<3;signal++)for(int dont=0;dont<2;dont++)for(int bounds=0;bounds<3;bounds++)for(int selection=0;selection<2;selection++)for(int light=0;light<2;light++){
 ImageProcessor ip=type==0?new ByteProcessor(W,H):type==1?new ShortProcessor(W,H):new FloatProcessor(W,H);
 for(int i=0;i<W*H;i++)ip.setf(i,signal==0?(i==12?80:20):signal==1?7:0);
 ImagePlus imp=new ImagePlus("preview-top-hat",ip);if(selection==1)imp.setRoi(new PolygonRoi(new int[]{0,4,2},new int[]{0,1,4},3,Roi.POLYGON));
 ip.setMinAndMax(bounds==0?7:bounds==1?7:0,bounds==0?127:bounds==1?7:0);ip.setThreshold(4,8,ImageProcessor.NO_LUT_UPDATE);
 String before=hex(ip),initial=range(ip),initialThreshold=threshold(ip);
 RankFilters f=new RankFilters();int flags=f.setup("tophat",imp);field(f,"radius",1.0);field(f,"lightBackground",light==1);field(f,"dontSubtract",dont==1);field(f,"previewing",true);field(f,"flags",dont==1?flags:flags|ExtendedPlugInFilter.FINAL_PROCESSING);
 ip.snapshot();f.run(ip);String processorPixels=hex(ip);if(selection==1)ip.reset(ip.getMask());
 System.out.println("{\"dtype\":\""+(type==0?"uint8":type==1?"uint16":"float32")+"\",\"signal\":"+signal+",\"dont_subtract\":"+(dont==1)+",\"light_background\":"+(light==1)+",\"bounds\":"+bounds+",\"selection\":"+selection+",\"input_hex\":[\""+before+"\"],\"processor_hex\":\""+processorPixels+"\",\"output_hex\":[\""+hex(ip)+"\"],\"initial_range\":"+initial+",\"initial_threshold\":"+initialThreshold+",\"final_range\":"+range(ip)+",\"final_threshold\":"+threshold(ip)+"}");
 imp.close();}System.exit(0);}
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
 rows=[json.loads(line) for line in (WORK/'java.stdout').read_text().splitlines() if line.strip()];assert len(rows)==216
 payload=dict(schema_version=1,imagej_version='1.54r',imagej_commit=COMMIT,source_archive_sha256=IMAGEJ_SHA,runtime_archive_sha256=FIJI_SHA,classes_archive=pin,source_members=members,runtime_pins=held['runtime_pins'],driver_sha256=digest(driver),generator='scripts/capture_tophat_preview_reference.py',scope='Direct RankFilters preview run: finite UInt8/UInt16/Float32, variable/constant/zero, light/dark background, subtraction/background, ordinary/equal/automatic source ranges, whole/polygon ROI, pre-reset processor pixels and attempted runner-style mask reset. No physical UI claim',cases=rows)
 (WORK/'reference001.json').write_text(json.dumps(payload,indent=2)+'\n');print('Captured 216 direct Top Hat preview plugin-runner cases')
 target=ROOT/'tests/fixtures/tophat-preview-imagej154r.json';assert not target.exists();target.write_bytes((WORK/'reference001.json').read_bytes())

if __name__ == '__main__':main()
