"""Capture direct RankFilters preview-mode calls; no physical UI claim.

No upstream runtime ships with the product. Pixel and final-display observations
are retained; editor publication is a separate qualification gate.
"""
from pathlib import Path
import hashlib,json,subprocess,zipfile
from capture_binary_reference import ROOT,COMMIT,IMAGEJ_SHA,FIJI_SHA,digest,verified

WORK = ROOT / 'build/p07p-tophat-ui001/reference-triangle002'
DRIVER = r'''
import ij.*;import ij.process.*;import ij.gui.*;import ij.plugin.filter.*;
public class IndexedTypeReference {
 static String hex(ImageProcessor ip){StringBuilder b=new StringBuilder();for(int i=0;i<25;i++){int v=Float.floatToRawIntBits(ip.getf(i));for(int j=0;j<4;j++)b.append(String.format("%02x",(v>>>(8*j))&255));}return b.toString();}
 static void field(RankFilters f,String name,Object value)throws Exception{java.lang.reflect.Field p=RankFilters.class.getDeclaredField(name);p.setAccessible(true);p.set(f,value);}
 static String range(ImageProcessor ip){return "["+ip.getMin()+","+ip.getMax()+"]";}
 public static void main(String[]args)throws Exception{Thread.currentThread().setName("Run$_top-hat-mask-reference");Prefs.setThreads(1);
  for(int preview=0;preview<2;preview++){
   FloatProcessor ip=new FloatProcessor(5,5);for(int i=0;i<25;i++)ip.setf(i,7);
   ImagePlus imp=new ImagePlus("top-hat-mask",ip);imp.setRoi(new PolygonRoi(new int[]{0,5,0},new int[]{0,0,5},3,Roi.POLYGON));
   ip.setMinAndMax(7,127);ip.setThreshold(4,8,ImageProcessor.NO_LUT_UPDATE);
   ip=(FloatProcessor)imp.getProcessor();ImageProcessor mask=imp.getMask();StringBuilder maskHex=new StringBuilder();for(int i=0;i<25;i++)maskHex.append(String.format("%02x",mask.get(i)));
   String input=hex(ip);
   if(preview==1){RankFilters f=new RankFilters();int flags=f.setup("tophat",imp);field(f,"radius",1.);field(f,"lightBackground",false);field(f,"dontSubtract",false);field(f,"previewing",true);field(f,"flags",flags|ExtendedPlugInFilter.FINAL_PROCESSING);ip.snapshot();f.run(ip);ip.reset(mask);}
   else{WindowManager.setTempCurrentImage(imp);Macro.setOptions("radius=1 slice");new PlugInFilterRunner(new RankFilters(),"Top Hat","tophat");Macro.setOptions(null);}
   System.out.println("{\"preview\":"+(preview==1)+",\"mask_hex\":\""+maskHex+"\",\"input_hex\":\""+input+"\",\"output_hex\":\""+hex(ip)+"\",\"final_range\":"+range(ip)+",\"threshold_reset\":"+(ip.getMinThreshold()==ImageProcessor.NO_THRESHOLD)+"}");
   WindowManager.setTempCurrentImage(null);imp.close();
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
 driver=WORK/'IndexedTypeReference.java';driver.write_text(DRIVER)
 for name,args in [('javac',[runtime/'bin/javac.exe','-cp',classes,'-d',classes,driver]),('java',[runtime/'bin/java.exe','-Djava.awt.headless=false','-cp',classes,'IndexedTypeReference'])]:
  with (WORK/(name+'.stdout')).open('w') as out,(WORK/(name+'.stderr')).open('w') as err:r=subprocess.run([str(x) for x in args],stdout=out,stderr=err,text=True,timeout=120)
  r.check_returncode()
 rows=[json.loads(line) for line in (WORK/'java.stdout').read_text().splitlines() if line.strip()];assert len(rows)==2
 payload=dict(schema_version=1,imagej_version='1.54r',imagej_commit=COMMIT,source_archive_sha256=IMAGEJ_SHA,runtime_archive_sha256=FIJI_SHA,classes_archive=pin,source_members=members,runtime_pins=held['runtime_pins'],driver_sha256=digest(driver),generator='scripts/capture_tophat_mask_reference.py',scope='Actual Top Hat apply plugin runner and direct preview flag with explicit stored-mask restore: constant Float32, whole-image-bounds triangle, setter/preview/apply range contrast. Direct preview is not physical UI qualification',cases=rows)
 (WORK/'reference001.json').write_text(json.dumps(payload,indent=2)+'\n');print('Captured 2 Top Hat full-bounds mask plugin-runner cases')
 target=ROOT/'tests/fixtures/tophat-mask-imagej154r.json';assert not target.exists();target.write_bytes((WORK/'reference001.json').read_bytes())

if __name__ == '__main__':main()
