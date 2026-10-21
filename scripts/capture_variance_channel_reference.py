"""Capture composite-channel variance through the pinned ImageJ plugin runner.

Range policy qualification lives in the native tests; editor publication remains
separately guarded. No upstream runtime ships with the product.
"""
from pathlib import Path
import hashlib,json,subprocess,zipfile
from capture_binary_reference import ROOT,COMMIT,IMAGEJ_SHA,FIJI_SHA,digest,verified

WORK = ROOT / 'build/p07i-variance-channel001/reference003'
DRIVER = r'''
import ij.*;
import ij.process.*;
import ij.gui.*;
import ij.plugin.filter.*;
import java.awt.image.BufferedImage;
public class IndexedTypeReference {
 static final int W=5,H=5;
 static class HiddenStackWindow extends StackWindow {
  HiddenStackWindow(ImagePlus imp){super(imp,new ImageCanvas(imp));}
  public void show(){} public void setVisible(boolean visible){super.setVisible(false);}
 }
 static String hex(ImageProcessor ip){StringBuilder b=new StringBuilder();int n=ip instanceof FloatProcessor?4:2;
  for(int i=0;i<W*H;i++){int v=n==4?Float.floatToRawIntBits(ip.getf(i)):ip.get(i);for(int j=0;j<n;j++)b.append(String.format("%02x",(v>>>(8*j))&255));}return b.toString();}
 static String pixels(ImagePlus imp){StringBuilder b=new StringBuilder("[");for(int i=1;i<=4;i++){if(i>1)b.append(',');b.append('"').append(hex(imp.getStack().getProcessor(i))).append('"');}return b.append(']').toString();}
 static String range(ImageProcessor ip){return "["+ip.getMin()+","+ip.getMax()+"]";}
 static String ranges(CompositeImage imp){LUT[] l=imp.getLuts();return "[["+l[0].min+","+l[0].max+"],["+l[1].min+","+l[1].max+"]]";}
 static String threshold(ImageProcessor ip){return ip.getMinThreshold()==ImageProcessor.NO_THRESHOLD?"null":"["+ip.getMinThreshold()+","+ip.getMaxThreshold()+"]";}
 static String rgb(CompositeImage imp){BufferedImage b=imp.getBufferedImage();StringBuilder s=new StringBuilder();for(int y=0;y<H;y++)for(int x=0;x<W;x++)s.append(String.format("%06x",b.getRGB(x,y)&0xffffff));return s.toString();}
 public static void main(String[]args){Thread.currentThread().setName("Run$_channel-variance-reference");Prefs.setThreads(1);Prefs.autoContrast=false;Prefs.useInvertingLut=false;
  ImageJ app=new ImageJ(ImageJ.NO_SHOW);
  for(int type=0;type<2;type++)for(int mode=1;mode<=3;mode++)for(int signal=0;signal<2;signal++)for(int all=0;all<2;all++)for(int hosted=0;hosted<2;hosted++){
   ImageStack stack=new ImageStack(W,H);
   for(int z=0;z<2;z++)for(int c=0;c<2;c++){ImageProcessor ip=type==0?new ShortProcessor(W,H):new FloatProcessor(W,H);for(int i=0;i<W*H;i++)ip.setf(i,signal==1?7:z*30+c*20+(i==12?80:0));stack.addSlice(ip);}
   ImagePlus source=new ImagePlus("variance-channel",stack);source.setDimensions(2,2,1);
   CompositeImage imp=new CompositeImage(source,mode);HiddenStackWindow window=hosted==1?new HiddenStackWindow(imp):null;
   for(int c=1;c<=2;c++){LUT lut=LUT.createLutFromColor(c==1?java.awt.Color.RED:java.awt.Color.GREEN);lut.min=c;lut.max=c*127;imp.setChannelLut(lut,c);}
   if(hosted==1)imp.setPosition(2,2,1);else imp.setPositionWithoutUpdate(2,2,1);
   imp.setChannelLut((LUT)imp.getChannelLut(2).clone());imp.setMode(mode);imp.updateImage();imp.setDisplayRange(signal==1?7:2,signal==1?7:254);imp.updateImage();imp.setDisplayRange(signal==1?7:2,signal==1?7:254);imp.updateImage();
   imp.getProcessor().setThreshold(4,8,ImageProcessor.NO_LUT_UPDATE);
   String before=pixels(imp),initial=range(imp.getProcessor()),initialLuts=ranges(imp);
   WindowManager.setTempCurrentImage(imp);if(window!=null)WindowManager.setCurrentWindow(window);
   Macro.setOptions("radius=0 "+(all==1?"stack":"slice"));
   if(imp.getMode()!=mode)throw new IllegalStateException("Display mode changed during setup");
   if(!Macro.getOptions().contains(all==1?"stack":"slice"))throw new IllegalStateException("Missing runner options");new PlugInFilterRunner(new RankFilters(),"Variance...","variance");Macro.setOptions(null);
   String immediate=range(imp.getProcessor()),finalThreshold=threshold(imp.getProcessor()),stored=ranges(imp),output=pixels(imp);
   imp.updateImage();String rendered=range(imp.getProcessor()),colors=rgb(imp);
   imp.setPosition(1,2,1);imp.updateImage();imp.setPosition(2,2,1);imp.updateImage();String switched=range(imp.getProcessor());
   System.out.println("{\"dtype\":\""+(type==0?"uint16":"float32")+"\",\"mode\":"+mode+",\"signal\":"+signal+",\"all\":"+all+",\"hosted\":"+hosted+",\"input_hex\":"+before+",\"output_hex\":"+output+",\"initial_processor_range\":"+initial+",\"initial_lut_ranges\":"+initialLuts+",\"final_processor_range\":"+immediate+",\"final_lut_ranges\":"+stored+",\"final_threshold\":"+finalThreshold+",\"rendered_processor_range\":"+rendered+",\"rgb_hex\":\""+colors+"\",\"switched_processor_range\":"+switched+"}");
   WindowManager.setTempCurrentImage(null);imp.changes=false;imp.close();
  }app.dispose();System.exit(0);
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
  for name in ['ij/CompositeImage.java','ij/gui/StackWindow.java','ij/gui/ImageCanvas.java','ij/process/ImageConverter.java','ij/process/StackConverter.java','ij/process/ImageProcessor.java','ij/ImagePlus.java','ij/plugin/filter/PlugInFilterRunner.java','ij/plugin/filter/Filters.java','ij/plugin/filter/GaussianBlur.java','ij/plugin/filter/RankFilters.java','ij/plugin/LutLoader.java','ij/plugin/ContrastEnhancer.java','ij/process/ShortStatistics.java','ij/process/FloatStatistics.java','ij/process/FloatProcessor.java','ij/process/ShortProcessor.java','ij/Macro.java','ij/gui/GenericDialog.java']:
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
 rows=[json.loads(line) for line in (WORK/'java.stdout').read_text().splitlines() if line.strip()];assert len(rows)==48
 payload=dict(schema_version=1,imagej_version='1.54r',imagej_commit=COMMIT,source_archive_sha256=IMAGEJ_SHA,runtime_archive_sha256=FIJI_SHA,classes_archive=pin,source_members=members,runtime_pins=held['runtime_pins'],driver_sha256=digest(driver),generator='scripts/capture_variance_channel_reference.py',scope='Actual PlugInFilterRunner variance: UInt16/Float32, composite/color/grayscale, current/all planes, variable/constant signal, hidden StackWindow and windowless. Captures transient processor bounds, stored LUT bounds, RGB rendering and channel-switch bounds separately.',cases=rows)
 (WORK/'reference001.json').write_text(json.dumps(payload,indent=2)+'\n');print('Captured 48 channel-display variance runner cases')
 target=ROOT/'tests/fixtures/variance-channel-imagej154r.json';assert not target.exists();target.write_bytes((WORK/'reference001.json').read_bytes())

if __name__ == '__main__':main()
