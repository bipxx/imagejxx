"""Pinned ImageJ indexed/pseudocolor classification, type conversion and filters.

Upstream classes and the JDK remain private build inputs, outside product assets.
"""
from pathlib import Path
import hashlib,json,subprocess,zipfile
from capture_binary_reference import ROOT,COMMIT,IMAGEJ_SHA,FIJI_SHA,digest,verified
WORK=ROOT/'build/p07b-indexed-dispatch001/reference004'
DRIVER=r'''
import ij.*;
import ij.process.*;
import ij.plugin.filter.*;
import java.awt.image.*;
public class IndexedTypeReference {
 static IndexColorModel palette(int kind){
  if(kind==8)return ij.plugin.LutLoader.getLut("3-3-2 RGB");
  int n=kind==5||kind==6?2:256;byte[] r=new byte[n],g=new byte[n],b=new byte[n];int state=0x24681357;
  for(int i=0;i<n;i++){
   int rr=i,gg=i,bb=i;
   if(kind==1)rr=gg=bb=255-i;
   else if(kind==2){rr=i;gg=bb=0;}
   else if(kind==3){state=1664525*state+1013904223;rr=(state>>16)&255;gg=(state>>8)&255;bb=state&255;}
   else if(kind==4){rr=(i&1)*255;gg=((i>>1)&1)*255;bb=((i>>2)&1)*255;}
   else if(kind==5){rr=i==0?255:0;gg=i==0?0:255;bb=0;}
   else if(kind==6)rr=gg=bb=i*255;
   else if(kind==7){rr=(i>>5)*255/7;gg=((i>>2)&7)*255/7;bb=(i&3)*255/3;}
   r[i]=(byte)rr;g[i]=(byte)gg;b[i]=(byte)bb;
  }return new IndexColorModel(8,n,r,g,b);
 }
 static byte[] input(int plane){byte[] p=new byte[32];int[] values={0,1,2,3,7,15,31,63,64,100,127,128,150,180,200,254,255};for(int i=0;i<p.length;i++)p[i]=(byte)values[(i*5+plane*3)%values.length];return p;}
 static ImagePlus image(int kind,int count,int range){ImageStack s=new ImageStack(8,4);for(int p=0;p<count;p++)s.addSlice("p"+p,new ByteProcessor(8,4,input(p),palette(kind)));ImagePlus imp=new ImagePlus("indexed",s);imp.setDimensions(1,count,1);imp.setSlice(count);if(range==1)imp.setDisplayRange(1,127);imp.setTypeToColor256();return imp;}
 static String table(IndexColorModel cm){if(cm==null)return "null";StringBuilder s=new StringBuilder("[");for(int i=0;i<cm.getMapSize();i++){if(i>0)s.append(',');s.append('[').append(cm.getRed(i)).append(',').append(cm.getGreen(i)).append(',').append(cm.getBlue(i)).append(']');}return s.append(']').toString();}
 static String pixels(ImagePlus imp){StringBuilder s=new StringBuilder("[");for(int p=1;p<=imp.getStackSize();p++){if(p>1)s.append(',');s.append('[');ImageProcessor ip=imp.getStack().getProcessor(p);for(int i=0;i<32;i++){if(i>0)s.append(',');s.append(ip.getf(i));}s.append(']');}return s.append(']').toString();}
 static String classification(ImagePlus imp){ImageProcessor ip=imp.getProcessor();return "\"image_type\":"+imp.getType()+",\"color\":"+ip.isColorLut()+",\"pseudo\":"+ip.isPseudoColorLut()+",\"inverted\":"+ip.isInvertedLut();}
 public static void main(String[] args){
  Prefs.useInvertingLut=false;Prefs.calibrateConversions=false;ImageConverter.setDoScaling(true);
  for(int kind=0;kind<9;kind++)for(int count:new int[]{1,3})for(int range=0;range<2;range++)for(boolean weighted:new boolean[]{false,true})for(String operation:new String[]{"gray8","gray16","gray32","gaussian","median"}){
   ColorProcessor.setWeightingFactors(weighted?.299:1./3,weighted?.587:1./3,weighted?.114:1./3);
   ImagePlus imp=image(kind,count,range);String original=classification(imp);String before=pixels(imp),sourcePalette=table((IndexColorModel)imp.getProcessor().getColorModel());String error="null";
   try{
    if(operation.equals("gray8"))new ImageConverter(imp).convertToGray8();
    else if(operation.equals("gray16"))new ImageConverter(imp).convertToGray16();
    else if(operation.equals("gray32"))new ImageConverter(imp).convertToGray32();
    else for(int p=1;p<=count;p++){ImageProcessor ip=imp.getStack().getProcessor(p);if(operation.equals("gaussian"))new GaussianBlur().blurGaussian(ip,1,1,.002);else new RankFilters().rank(ip,1,RankFilters.MEDIAN);}
   }catch(IllegalArgumentException e){error="\"unsupported\"";}
   System.out.println("{\"palette_kind\":"+kind+",\"planes\":"+count+",\"range\":"+range+",\"weighted\":"+weighted+",\"operation\":\""+operation+"\",\"before\":{"+original+"},\"input\":"+before+",\"source_palette\":"+sourcePalette+",\"error\":"+error+",\"after\":{"+classification(imp)+"},\"output\":"+pixels(imp)+",\"display_range\":["+imp.getDisplayRangeMin()+","+imp.getDisplayRangeMax()+"],\"output_palette\":"+table((IndexColorModel)imp.getProcessor().getColorModel())+"}");
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
  for name in ['ij/process/ImageConverter.java','ij/process/StackConverter.java','ij/process/ImageProcessor.java','ij/ImagePlus.java','ij/plugin/filter/PlugInFilterRunner.java','ij/plugin/filter/Filters.java','ij/plugin/filter/GaussianBlur.java','ij/plugin/filter/RankFilters.java','ij/plugin/LutLoader.java']:
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
 rows=[json.loads(line) for line in (WORK/'java.stdout').read_text().splitlines() if line.strip()];assert len(rows)==360
 payload=dict(schema_version=1,imagej_version='1.54r',imagej_commit=COMMIT,source_archive_sha256=IMAGEJ_SHA,runtime_archive_sha256=FIJI_SHA,classes_archive=pin,source_members=members,runtime_pins=held['runtime_pins'],driver_sha256=digest(driver),generator='scripts/capture_indexed_type_reference.py',scope='LUT classification, gray8/16/32 processor conversion and direct byte Gaussian/median filters. No composite, threshold, arbitrary plugin or file-format certification.',cases=rows)
 (WORK/'reference001.json').write_text(json.dumps(payload,indent=2)+'\n');print('Captured360 indexed type/filter cases')
if __name__=='__main__':main()
