"""Actual pinned ImageJ reads and Remove NaNs on PackBits Float32 carriers."""
from pathlib import Path
import hashlib,json,zipfile,sys,importlib.util,os
ROOT=Path(__file__).resolve().parents[1]
OWN=ROOT/'build/p08g-packbits-tiff001';WORK=OWN/'float-reference001'
DRIVER=r'''import ij.*;import ij.io.*;import ij.plugin.filter.*;import java.nio.file.*;import java.nio.charset.*;
public class PackBitsFloatReference {
 static final char[] HEX="0123456789abcdef".toCharArray();
 static String hex(ImagePlus imp){StringBuilder out=new StringBuilder();
  for(int t=1;t<=imp.getNFrames();t++)for(int c=1;c<=imp.getNChannels();c++)for(int z=1;z<=imp.getNSlices();z++)
   for(float v:(float[])imp.getStack().getPixels(imp.getStackIndex(c,z,t))){int word=Float.floatToRawIntBits(v);for(int b=0;b<4;b++){int value=word>>>(8*b);out.append(HEX[(value>>>4)&15]);out.append(HEX[value&15]);}}
  return out.toString();
 }
 public static void main(String[] args)throws Exception{
  Thread.currentThread().setName("Run$_packbits-float-reference");Prefs.setThreads(1);
  for(String line:Files.readAllLines(Paths.get(args[0]),StandardCharsets.UTF_8)){
   String[] fields=line.split("\t");ImagePlus image=new Opener().openImage(fields[2]);if(image==null)throw new RuntimeException("Opener failed: "+fields[2]);String source=hex(image);
   if(fields[1].equals("filter")){
    image.setPosition(1,1,1);WindowManager.setTempCurrentImage(image);Macro.setOptions("radius=1 slice");if(Macro.getOptions()==null)throw new RuntimeException("Missing macro options");
    RankFilters filter=new RankFilters();new PlugInFilterRunner(filter,"Remove NaNs","nan");java.lang.reflect.Field radius=RankFilters.class.getDeclaredField("radius");radius.setAccessible(true);if(radius.getDouble(filter)!=1.0)throw new RuntimeException("Wrong radius");Macro.setOptions(null);WindowManager.setTempCurrentImage(null);
   }
   System.out.println("{\"id\":\""+fields[0]+"\",\"source_hex\":\""+source+"\",\"filtered_hex\":\""+hex(image)+"\",\"shape\":["+image.getNFrames()+","+image.getNChannels()+","+image.getNSlices()+","+image.getHeight()+","+image.getWidth()+",1],\"spacing\":["+image.getCalibration().pixelWidth+","+image.getCalibration().pixelHeight+","+image.getCalibration().pixelDepth+"],\"frame_interval\":"+image.getCalibration().frameInterval+"}");image.close();
  }System.exit(0);
 }
}'''
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def pin(p):return dict(path=p.relative_to(ROOT).as_posix(),bytes=p.stat().st_size,sha256=digest(p))
def main():
    assert not WORK.exists();WORK.mkdir()
    held=json.loads((ROOT/'tests/fixtures/float32-tiff-imagej154r/SOURCE.json').read_bytes())
    archive=ROOT/held['classes_archive']['path'];assert archive.stat().st_size==held['classes_archive']['bytes'] and digest(archive)==held['classes_archive']['sha256']
    runtime=ROOT/'build/references/binary-imagej154r/jdk'
    for row in held['runtime_pins']:
        p=runtime/row['name'];assert p.stat().st_size==row['bytes'] and digest(p)==row['sha256']
    source_manifest=json.loads((ROOT/'tests/fixtures/packbits-float32-tiff-imagej154r/SOURCE.json').read_bytes())
    source_pin=source_manifest['source_manifest'];assert pin(ROOT/source_pin['path'])==source_pin
    sources=[]
    for name in ['ij/io/ImageReader.java','ij/io/TiffDecoder.java','ij/io/Opener.java','ij/plugin/filter/RankFilters.java','ij/plugin/filter/PlugInFilterRunner.java']:
        p=ROOT/'build/references/binary-imagej154r/sources'/name;sources.append(pin(p))
    # Validate all referenced sources against the pinned original source ZIP.
    upstream=ROOT/'build/references/downloads'/('imagej-'+held['imagej_commit']+'.zip');assert digest(upstream)==held['source_archive_sha256']
    with zipfile.ZipFile(upstream) as z:
        for row in sources:
            relative=row['path'].split('/sources/',1)[1];raw=z.read('ImageJ-'+held['imagej_commit']+'/'+relative);assert hashlib.sha256(raw).hexdigest()==row['sha256']
    classes=WORK/'classes001';classes.mkdir()
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        for row in z.infolist():
            if row.filename.startswith('ij/') and not row.is_dir():
                p=classes/row.filename;assert p.resolve().is_relative_to(classes.resolve());p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(z.read(row))
    expected={};inputs=[];lines=[];originals={r['file']:r for r in source_manifest['files']}
    for name,row in originals.items():
        p=ROOT/'tests/fixtures/packbits-float32-tiff-imagej154r'/name;key='fixture/'+name;expected[key]=(row,True);inputs.append(pin(p));lines.append(key+'\tfilter\t'+str(p))
    cases={'TF01':'finite-le.tif','TF02':'quiet-be.tif','TF03':'ieee-le.tif','TF04':'ieee-be.tif'}
    for context,directory in [('source-cpp',OWN/'float-cpp002'),('installed-cpp',OWN/'float-installed-cpp001'),('source-python',OWN/'float-observations001/source-python'),('installed-python',OWN/'float-observations001/installed-python')]:
        for case,name in cases.items():
            p=directory/case/'output.tif';key=context+'/'+case;expected[key]=(originals[name],False);inputs.append(pin(p));lines.append(key+'\tread\t'+str(p))
    paths=WORK/'paths001.txt';paths.write_text('\n'.join(lines)+'\n',encoding='utf-8');driver=WORK/'PackBitsFloatReference.java';driver.write_text(DRIVER)
    sys.path.insert(0,str(ROOT/'scripts'));from capture_windows_platform_workload import Win32,JobProcess
    base_path=ROOT/'build/p06i-001/native-development001/supervisor.py';assert digest(base_path)=='8e9868467c89343a734e75377819f5c96491b59a78e26ca7ad6edc4a72f8cfaa'
    spec=importlib.util.spec_from_file_location('bounded_reference',base_path);base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base);base.STAGE=WORK;m=base.helpers();m.LABELS=('javac','java');m.MAX_STAGE=512*1048576;m.MAX_FILES=8192;m.MAX_ENTRIES=16384
    result=dict(state='running',processes=[],failures=[]);api=Win32(None)
    try:
        for label,argv in [('javac',[runtime/'bin/javac.exe','-cp',classes,'-d',classes,driver]),('java',[runtime/'bin/java.exe','-Djava.awt.headless=false','-cp',classes,'PackBitsFloatReference',paths])]:base.run(m,api,JobProcess,[str(a) for a in argv],label,120,os.environ.copy(),result)
        observations=[json.loads(line) for line in (WORK/'java.stdout').read_text().splitlines() if line.strip()];assert len(observations)==22
        for row in observations:
            original,filtered=expected[row['id']];assert row['source_hex']==original['source_hex' if filtered else 'filtered_hex'],row['id'];assert row['filtered_hex']==original['filtered_hex'] and row['shape']==original['shape'] and row['spacing']==[.5,1.25,2.75] and row['frame_interval']==.125
        observed=WORK/'observations001.json';observed.write_text(json.dumps(observations,indent=2))
        result.update(state='passed',imagej_commit=held['imagej_commit'],source_archive_sha256=held['source_archive_sha256'],classes_archive=held['classes_archive'],runtime_pins=held['runtime_pins'],source_members=sources,inputs=inputs,driver=pin(driver),observations=pin(observed),observation_count=len(observations))
    except BaseException as e:result['state']='failed';result['failures'].append(m.error_value(e))
    finally:
        api.close();result['owned_job_cleanup_verified']=all(not p.get('owned_job_pids_after_cleanup',[1]) for p in result['processes']);m.write_result(result)
    print(json.dumps(dict(state=result['state'],failures=result['failures'])))
    return 0 if result['state']=='passed' else 1
if __name__=='__main__':sys.exit(main())
