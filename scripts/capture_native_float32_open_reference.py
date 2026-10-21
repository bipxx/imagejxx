"""Pinned ImageJ Opener observes native exports; no application runtime added."""
from pathlib import Path
import hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[1];OWN=ROOT/'build/p08a-float32-tiff-ui001';WORK=OWN/'native-open002'
DRIVER=r'''import ij.*;import ij.io.*;
public class NativeFloatOpenReference {
 public static void main(String[] args){
  for(int i=0;i<args.length;i+=3){ImagePlus image=new Opener().openImage(args[i+2]);if(image==null)throw new RuntimeException("Opener failed "+args[i+2]);
   System.out.println("{\"context\":\""+args[i]+"\",\"case_id\":\""+args[i+1]+"\",\"hex\":\""+FloatTiffReference.hex(image)+"\",\"shape\":["+image.getNFrames()+","+image.getNChannels()+","+image.getNSlices()+",5,7,1],\"spacing\":["+image.getCalibration().pixelWidth+","+image.getCalibration().pixelHeight+","+image.getCalibration().pixelDepth+"],\"frame_interval\":"+image.getCalibration().frameInterval+"}");image.close();
  }System.exit(0);
 }
}'''
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 assert not WORK.exists();WORK.mkdir()
 manifest=json.loads((ROOT/'tests/fixtures/float32-tiff-imagej154r/SOURCE.json').read_bytes())
 runtime=ROOT/'build/references/binary-imagej154r/jdk'
 for row in manifest['runtime_pins']:
  p=runtime/row['name'];assert p.stat().st_size==row['bytes'] and digest(p)==row['sha256']
 classes=OWN/'reference003/classes001';driver=WORK/'NativeFloatOpenReference.java';driver.write_text(DRIVER)
 args=[];inputs=[]
 for context,directory in [('source','float-cpp002'),('installed','float-installed-cpp001'),('source-python','file-observations001/source-python'),('installed-python','file-observations001/installed-python')]:
  for case in ['TF01','TF02','TF03','TF04']:
   p=OWN/directory/case/'output.tif';assert p.is_file();args.extend([context,case,str(p)]);inputs.append(dict(path=p.relative_to(ROOT).as_posix(),sha256=digest(p),bytes=p.stat().st_size))
 for label,argv in [('javac',[runtime/'bin/javac.exe','-cp',classes,'-d',classes,driver]),('java',[runtime/'bin/java.exe','-Djava.awt.headless=false','-cp',classes,'NativeFloatOpenReference',*args])]:
  with (WORK/(label+'.stdout')).open('w') as out,(WORK/(label+'.stderr')).open('w') as err:r=subprocess.run([str(a) for a in argv],stdout=out,stderr=err,timeout=120)
  r.check_returncode()
 rows=[json.loads(line) for line in (WORK/'java.stdout').read_text().splitlines() if line.strip()];assert len(rows)==16
 expected={r['file']:r for r in manifest['files']};names={'TF01':'finite-le.tif','TF02':'quiet-be.tif','TF03':'ieee-le.tif','TF04':'ieee-be.tif'}
 for row in rows:
  oracle=expected[names[row['case_id']]];assert row['hex']==oracle['filtered_hex'] and row['shape']==oracle['shape'] and row['spacing']==oracle['spacing'] and row['frame_interval']==.125
 result=dict(state='passed',imagej_commit=manifest['imagej_commit'],inputs=inputs,driver_sha256=digest(driver),runtime_pins=manifest['runtime_pins'],observations=rows)
 (WORK/'reference001.json').write_text(json.dumps(result,indent=2)+'\n');print('ImageJ reopened all sixteen native exports with exact words/axes/calibration')
if __name__=='__main__':main()
