"""Pinned ImageJ Opener reads independent and native LZW TIFFs."""
from pathlib import Path
import hashlib,json,subprocess,zipfile,sys
ROOT=Path(__file__).resolve().parents[1]
OWN=ROOT/'build/p08i-horizontal-tiff001';MODE=sys.argv[1] if len(sys.argv)>1 else 'full';assert MODE in ['core','full'];WORK=OWN/('reference-core001' if MODE=='core' else 'reference001')
DRIVER=r'''import ij.*;import ij.io.*;import java.nio.file.*;import java.nio.charset.*;
public class HorizontalOpenReference {
 static final char[] HEX="0123456789abcdef".toCharArray();
 static void byteHex(StringBuilder out,int value){out.append(HEX[(value>>>4)&15]);out.append(HEX[value&15]);}
 static String hex(ImagePlus imp){StringBuilder out=new StringBuilder();
  for(int t=1;t<=imp.getNFrames();t++)for(int c=1;c<=imp.getNChannels();c++)for(int z=1;z<=imp.getNSlices();z++){
   Object pixels=imp.getStack().getPixels(imp.getStackIndex(c,z,t));
   if(pixels instanceof byte[])for(byte v:(byte[])pixels)byteHex(out,v);
   else if(pixels instanceof short[])for(short v:(short[])pixels){byteHex(out,v);byteHex(out,v>>>8);}
   else if(pixels instanceof float[])for(float v:(float[])pixels){int word=Float.floatToRawIntBits(v);for(int b=0;b<4;b++)byteHex(out,word>>>(8*b));}
   else if(pixels instanceof int[])for(int v:(int[])pixels){byteHex(out,v>>>16);byteHex(out,v>>>8);byteHex(out,v);}
   else throw new RuntimeException("Unexpected pixel storage");
  }return out.toString();
 }
 public static void main(String[] args)throws Exception{
  Prefs.setThreads(1);
  for(String line:Files.readAllLines(Paths.get(args[0]),StandardCharsets.UTF_8)){
   String[] fields=line.split("\t");ImagePlus image=new Opener().openImage(fields[1]);if(image==null)throw new RuntimeException("Opener failed: "+fields[1]);
   System.out.println("{\"id\":\""+fields[0]+"\",\"hex\":\""+hex(image)+"\",\"shape\":["+image.getNFrames()+","+image.getNChannels()+","+image.getNSlices()+","+image.getHeight()+","+image.getWidth()+","+(image.getBitDepth()==24?3:1)+"],\"spacing\":["+image.getCalibration().pixelWidth+","+image.getCalibration().pixelHeight+","+image.getCalibration().pixelDepth+"],\"frame_interval\":"+image.getCalibration().frameInterval+"}");image.close();
  }System.exit(0);
 }
}'''
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def pin(p):return dict(path=p.relative_to(ROOT).as_posix(),bytes=p.stat().st_size,sha256=digest(p))
def main():
    assert not WORK.exists();WORK.mkdir()
    held=json.loads((ROOT/'tests/fixtures/rgb-tiff-imagej154r/SOURCE.json').read_bytes())
    archive=ROOT/held['classes_archive']['path'];assert archive.stat().st_size==held['classes_archive']['bytes'] and digest(archive)==held['classes_archive']['sha256']
    runtime=ROOT/'build/references/binary-imagej154r/jdk'
    for row in held['runtime_pins']:
        p=runtime/row['name'];assert p.stat().st_size==row['bytes'] and digest(p)==row['sha256']
    sources=[]
    for name in ['ij/io/ImageReader.java','ij/io/TiffDecoder.java','ij/io/Opener.java']:
        p=ROOT/'build/references/binary-imagej154r/sources'/name
        expected=next(row for row in held['source_members'] if row['name']==name)
        assert p.stat().st_size==expected['bytes'] and digest(p)==expected['sha256'];sources.append(pin(p))
    classes=WORK/'classes001';classes.mkdir()
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        for row in z.infolist():
            if row.filename.startswith('ij/') and not row.is_dir():
                p=classes/row.filename;assert p.resolve().is_relative_to(classes.resolve());p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(z.read(row))
    manifest=json.loads((ROOT/'tests/fixtures/scientific-tiff-horizontal/SOURCE.json').read_bytes())
    expected={};inputs=[];lines=[]
    for context,directory in [('independent',ROOT/'tests/fixtures/scientific-tiff-horizontal'),('native',OWN/'native-exports001')]:
        for row in manifest['files']:
            if not row['valid']:continue
            p=directory/row['file'];key=context+'/'+row['file'];expected[key]=row;inputs.append(pin(p));lines.append(key+'\t'+str(p))
    for context,directory in ([] if MODE=='core' else [('source-cpp',OWN/'horizontal-cpp001'),('installed-cpp',OWN/'horizontal-installed-cpp001'),('source-python',OWN/'horizontal-observations001/source-python'),('installed-python',OWN/'horizontal-observations001/installed-python')]):
        for row in manifest['files']:
            if not row['valid'] or row['shape'][:5]!=[2,2,2,5,7] or row['rows']!=2 or row['codec'] not in [8,5]:continue
            order,codec=row['file'].replace('.tif','').split('-r2-c');label=order+'-c'+codec;p=directory/label/'capture001/output.tif';key=context+'/'+row['file'];expected[key]=row;inputs.append(pin(p));lines.append(key+'\t'+str(p))
    paths=WORK/'paths001.txt';paths.write_text('\n'.join(lines)+'\n',encoding='utf-8')
    driver=WORK/'HorizontalOpenReference.java';driver.write_text(DRIVER)
    sys.path.insert(0,str(ROOT/'scripts'));from capture_windows_platform_workload import Win32,JobProcess
    import importlib.util,os
    base_path=ROOT/'build/p06i-001/native-development001/supervisor.py';assert digest(base_path)=='8e9868467c89343a734e75377819f5c96491b59a78e26ca7ad6edc4a72f8cfaa'
    spec=importlib.util.spec_from_file_location('bounded_reference',base_path);base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base);base.STAGE=WORK;m=base.helpers();m.LABELS=('javac','java');m.MAX_STAGE=512*1048576;m.MAX_FILES=8192;m.MAX_ENTRIES=16384
    result=dict(state='running',processes=[],failures=[]);api=Win32(None)
    try:
        for label,argv in [('javac',[runtime/'bin/javac.exe','-cp',classes,'-d',classes,driver]),('java',[runtime/'bin/java.exe','-Djava.awt.headless=false','-cp',classes,'HorizontalOpenReference',paths])]:
            base.run(m,api,JobProcess,[str(a) for a in argv],label,120,os.environ.copy(),result)
        observations=[json.loads(line) for line in (WORK/'java.stdout').read_text().splitlines() if line.strip()]
        assert len(observations)==(168 if MODE=='core' else 216)
        for row in observations:
            original=expected[row['id']];assert row['hex']==original['hex'],row['id'];assert row['shape']==original['shape'],row['id'];assert row['spacing']==[.5,1.25,2.75] and row['frame_interval']==.125
        observed=WORK/'observations001.json';observed.write_text(json.dumps(observations,indent=2))
        result.update(state='passed',imagej_commit=held['imagej_commit'],classes_archive=held['classes_archive'],runtime_pins=held['runtime_pins'],source_members=sources,inputs=inputs,driver=pin(driver),observations=pin(observed),observation_count=len(observations))
    except BaseException as e:result['state']='failed';result['failures'].append(m.error_value(e))
    finally:
        api.close();result['owned_job_cleanup_verified']=all(not p.get('owned_job_pids_after_cleanup',[1]) for p in result['processes']);m.write_result(result)
    print(json.dumps(dict(state=result['state'],failures=result['failures'])))
    return 0 if result['state']=='passed' else 1
if __name__=='__main__':sys.exit(main())
