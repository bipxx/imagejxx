"""Pinned ImageJ Opener reads native byte exports from all host contexts."""
from pathlib import Path
import json,hashlib,subprocess
ROOT=Path(__file__).resolve().parents[1];OWN=ROOT/'build/p08b-uint8-tiff001';WORK=OWN/'native-open001'
DRIVER=r'''import ij.*;import ij.io.*;
public class NativeByteOpenReference {
 public static void main(String[] args){
  for(int i=0;i<args.length;i+=3){ImagePlus image=new Opener().openImage(args[i+2]);if(image==null)throw new RuntimeException("Opener failed");
   System.out.println("{\"context\":\""+args[i]+"\",\"order\":\""+args[i+1]+"\",\"hex\":\""+ByteTiffReference.hex(image)+"\",\"shape\":["+image.getNFrames()+","+image.getNChannels()+","+image.getNSlices()+","+image.getHeight()+","+image.getWidth()+",1],\"spacing\":["+image.getCalibration().pixelWidth+","+image.getCalibration().pixelHeight+","+image.getCalibration().pixelDepth+"],\"frame_interval\":"+image.getCalibration().frameInterval+"}");image.close();
  }System.exit(0);
 }
}'''
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 assert not WORK.exists();WORK.mkdir()
 reference=json.loads((ROOT/'tests/fixtures/uint8-tiff-imagej154r/SOURCE.json').read_bytes())
 runtime=ROOT/'build/references/binary-imagej154r/jdk';classes=OWN/'reference001/classes001'
 for row in reference['runtime_pins']:
  p=runtime/row['name'];assert p.stat().st_size==row['bytes'] and digest(p)==row['sha256']
 driver=WORK/'NativeByteOpenReference.java';driver.write_text(DRIVER);args=[];inputs=[]
 for context,directory in [('source-cpp','byte-cpp001'),('installed-cpp','byte-installed-cpp001'),('source-python','byte-observations001/source-python'),('installed-python','byte-observations001/installed-python')]:
  for order in ['le','be']:
   p=OWN/directory/order/'capture001/output.tif';assert p.is_file();args.extend([context,order,str(p)]);inputs.append(dict(path=p.relative_to(ROOT).as_posix(),bytes=p.stat().st_size,sha256=digest(p)))
 for label,argv in [('javac',[runtime/'bin/javac.exe','-cp',classes,'-d',classes,driver]),('java',[runtime/'bin/java.exe','-Djava.awt.headless=false','-cp',classes,'NativeByteOpenReference',*args])]:
  with (WORK/(label+'.stdout')).open('w') as out,(WORK/(label+'.stderr')).open('w') as err:r=subprocess.run([str(a) for a in argv],stdout=out,stderr=err,timeout=120)
  r.check_returncode()
 rows=[json.loads(line) for line in (WORK/'java.stdout').read_text().splitlines() if line.strip()];assert len(rows)==8
 originals={r['file']:r for r in reference['files']}
 for row in rows:
  expected=originals['byte-'+row['order']+'.tif'];assert row['hex']==expected['source_hex'] and row['shape']==expected['shape'] and row['spacing']==expected['spacing'] and row['frame_interval']==.125
 (WORK/'reference001.json').write_text(json.dumps(dict(state='passed',imagej_commit=reference['imagej_commit'],inputs=inputs,driver_sha256=digest(driver),runtime_pins=reference['runtime_pins'],observations=rows),indent=2)+'\n');print('ImageJ reopened all eight native UInt8 exports exactly')
if __name__=='__main__':main()
