"""Actual pinned ImageJ FileSaver/Opener Float32 carriers; no runtime shipping."""
from pathlib import Path
import hashlib,json,subprocess,zipfile,shutil
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from capture_binary_reference import ROOT,COMMIT,IMAGEJ_SHA,FIJI_SHA,digest,verified
WORK=ROOT/'build/p08a-float32-tiff-ui001/reference003'
DEST=ROOT/'tests/fixtures/float32-tiff-imagej154r'

DRIVER='\nimport ij.*;import ij.io.*;import ij.process.*;import ij.measure.*;import ij.plugin.filter.*;\npublic class FloatTiffReference {\n static String hex(ImagePlus imp){StringBuilder out=new StringBuilder();for(int t=1;t<=imp.getNFrames();t++)for(int c=1;c<=imp.getNChannels();c++)for(int z=1;z<=imp.getNSlices();z++){float[] a=(float[])imp.getStack().getPixels(imp.getStackIndex(c,z,t));for(float v:a){int w=Float.floatToRawIntBits(v);for(int b=0;b<4;b++)out.append(String.format("%02x",(w>>>(8*b))&255));}}return out.toString();}\n static ImagePlus input(int group){int[][] pools={{0,0x80000000,1,0x007fffff,0x00800000,0x3f800000,0xbf800000,0x7f7fffff,0xff7fffff},{0,0x80000000,0x3f800000,0x7fc00000,0xffc12345,0x7fc00001},{0,0x80000000,1,0x3f800000,0x7f800000,0xff800000,0x7f800001,0xff812345,0x7fc00001}};\n  ImageStack stack=new ImageStack(7,5);for(int t=0;t<2;t++)for(int z=0;z<3;z++)for(int c=0;c<2;c++){float[] a=new float[35];for(int i=0;i<35;i++)a[i]=Float.intBitsToFloat(pools[group][(t*13+c*11+z*7+i)%pools[group].length]);stack.addSlice(null,a);}\n  ImagePlus imp=new ImagePlus("Float32 TIFF reference",stack);imp.setDimensions(2,3,2);imp.setOpenAsHyperStack(true);\n  Calibration cal=imp.getCalibration();cal.pixelWidth=.5;cal.pixelHeight=1.25;cal.pixelDepth=2.75;cal.setUnit("um");cal.frameInterval=.125;cal.setTimeUnit("ms");return imp;\n }\n public static void main(String[] args)throws Exception{Thread.currentThread().setName("Run$_float-tiff-reference");Prefs.setThreads(1);for(int group=0;group<3;group++)for(int order=0;order<2;order++){\n  Prefs.intelByteOrder=order==0;ImagePlus imp=input(group);String source=hex(imp);String name=new String[]{"finite","quiet","ieee"}[group]+(order==0?"-le.tif":"-be.tif");\n  if(!new FileSaver(imp).saveAsTiffStack(args[0]+"/"+name))throw new RuntimeException("FileSaver failed");\n  ImagePlus opened=new Opener().openImage(args[0]+"/"+name);if(opened==null)throw new RuntimeException("Opener failed");String reopened=hex(opened);\n  opened.setPosition(1,1,1);WindowManager.setTempCurrentImage(opened);Macro.setOptions("radius=1 slice");if(Macro.getOptions()==null)throw new RuntimeException("Macro options not active");RankFilters filter=new RankFilters();new PlugInFilterRunner(filter,"Remove NaNs","nan");java.lang.reflect.Field radius=RankFilters.class.getDeclaredField("radius");radius.setAccessible(true);if(radius.getDouble(filter)!=1.0)throw new RuntimeException("Wrong radius "+radius.getDouble(filter));Macro.setOptions(null);WindowManager.setTempCurrentImage(null);\n  System.out.println("{\\"file\\":\\""+name+"\\",\\"group\\":\\""+new String[]{"finite","quiet","ieee"}[group]+"\\",\\"source_hex\\":\\""+source+"\\",\\"reopened_hex\\":\\""+reopened+"\\",\\"filtered_hex\\":\\""+hex(opened)+"\\",\\"shape\\":["+opened.getNFrames()+","+opened.getNChannels()+","+opened.getNSlices()+",5,7,1],\\"spacing\\":["+opened.getCalibration().pixelWidth+","+opened.getCalibration().pixelHeight+","+opened.getCalibration().pixelDepth+"]}");\n  opened.close();imp.close();\n }System.exit(0);}\n}\n'

def main():
 assert not WORK.exists() and not DEST.exists();WORK.mkdir(parents=True);DEST.mkdir()
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
 driver=WORK/'FloatTiffReference.java';driver.write_text(DRIVER)
 for name,args in [('javac',[runtime/'bin/javac.exe','-cp',classes,'-d',classes,driver]),('java',[runtime/'bin/java.exe','-Djava.awt.headless=false','-cp',classes,'FloatTiffReference',DEST])]:
  with (WORK/(name+'.stdout')).open('w') as out,(WORK/(name+'.stderr')).open('w') as err:r=subprocess.run([str(x) for x in args],stdout=out,stderr=err,text=True,timeout=120)
  r.check_returncode()
 rows=[json.loads(line) for line in (WORK/'java.stdout').read_text().splitlines() if line.strip()];assert len(rows)==6
 for row in rows:
  row['sha256']=digest(DEST/row['file']);row['bytes']=(DEST/row['file']).stat().st_size
 members=[]
 with zipfile.ZipFile(upstream) as z:
  for name in ['ij/io/FileSaver.java','ij/io/Opener.java','ij/io/ImageWriter.java','ij/io/ImageReader.java','ij/io/TiffEncoder.java','ij/io/TiffDecoder.java']:
   raw=z.read(f'ImageJ-{COMMIT}/'+name);assert raw==(ROOT/'build/references/binary-imagej154r/sources'/name).read_bytes();members.append(dict(name=name,bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest()))
 payload=dict(imagej_commit=COMMIT,source_archive_sha256=IMAGEJ_SHA,runtime_archive_sha256=FIJI_SHA,runtime_pins=held['runtime_pins'],source_members=members,classes_archive=pin,driver_sha256=digest(driver),generator='scripts/capture_float32_tiff_reference.py',scope='Actual FileSaver, Opener and Remove NaNs current-plane plugin runner; no UI parity claim',files=rows)
 (DEST/'SOURCE.json').write_text(json.dumps(payload,indent=2)+'\n');(WORK/'reference001.json').write_text(json.dumps(payload,indent=2)+'\n')
 print('Captured six actual ImageJ Float32 TIFF files')
if __name__=='__main__':main()
