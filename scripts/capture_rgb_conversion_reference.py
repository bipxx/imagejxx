"""Capture pinned ImageJ processor RGB/LUT output; no upstream code is shipped.

Output stays in excluded build storage. Promote the reviewed JSON fixture only
after this bounded reference job terminates. Java/runtime/source scratch is
owned by the reference workflow and archived before removal.
"""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import zipfile

from capture_binary_reference import ROOT, COMMIT, IMAGEJ_SHA, FIJI_SHA, JDK_PREFIX, digest, verified, extract

def _stream_sha256(stream):
    """Hash remaining binary stream bytes with bounded memory on Python 3.10+."""
    digest = hashlib.sha256()
    for block in iter(lambda: stream.read(1024 * 1024), b""):
        digest.update(block)
    return digest.hexdigest()


WORK = ROOT / 'build/p06zn-rgb-conversion001/reference001'
DRIVER = r'''
import ij.process.*;
import java.awt.image.IndexColorModel;
public class RgbConversionReference {
  static final int N=16;
  static double[] gray(String type,int pattern) {
    double[] a = type.equals("uint8") ? new double[]{0,1,7,10,63,64,100,127,128,129,191,200,253,254,255,16}
      : type.equals("uint16") ? new double[]{0,1,10,255,256,512,1023,2048,4095,4096,8192,16383,32767,65533,65534,65535}
      : new double[]{-2,-1,-.25,0,.25,.5,1,1.5,2,10,50,99,100,1000,1024,4096};
    for(int i=0;i<N;i++) if(pattern==1) a[i]=7; else if(pattern==2) a[i]=10+i;
    return a;
  }
  static ImageProcessor processor(String type,double[] a) {
    if(type.equals("uint8")) {byte[] p=new byte[N];for(int i=0;i<N;i++)p[i]=(byte)a[i];return new ByteProcessor(8,2,p,null);}
    if(type.equals("uint16")) {short[] p=new short[N];for(int i=0;i<N;i++)p[i]=(short)a[i];return new ShortProcessor(8,2,p,null);}
    float[] p=new float[N];for(int i=0;i<N;i++)p[i]=(float)a[i];return new FloatProcessor(8,2,p);
  }
  static IndexColorModel palette(int id) {
    byte[] r=new byte[256],g=new byte[256],b=new byte[256];
    for(int i=0;i<256;i++) {
      r[i]=(byte)(id==0?i:id==1?255-i:(17*i+3)%256);
      g[i]=(byte)(id==0?i:id==1?255-i:255-i);
      b[i]=(byte)(id==0?i:id==1?255-i:(i*i+7)%256);
    }
    return new IndexColorModel(8,256,r,g,b);
  }
  static String array(double[] a) {
    StringBuilder b=new StringBuilder("[");for(int i=0;i<a.length;i++){if(i>0)b.append(',');b.append(a[i]);}return b.append(']').toString();
  }
  static double[] pixels(ImageProcessor p) {
    int[] rgb=(int[])p.getPixels();double[] a=new double[3*N];
    for(int i=0;i<N;i++){a[3*i]=(rgb[i]>>16)&255;a[3*i+1]=(rgb[i]>>8)&255;a[3*i+2]=rgb[i]&255;}return a;
  }
  public static void main(String[] args) {
    String[] names={"gray","inverse","custom"};
    double[][] ranges={null,{0,255},{10.3,1023.7},{0,0}};
    for(String type:new String[]{"uint8","uint16","float32"})
      for(int pattern=0;pattern<3;pattern++)for(int lut=0;lut<3;lut++)for(int range=0;range<4;range++) {
        ImageProcessor p=processor(type,gray(type,pattern));double[] input=new double[N];
        for(int i=0;i<N;i++)input[i]=p.getf(i);
        p.setColorModel(palette(lut));
        if(ranges[range]!=null)p.setMinAndMax(ranges[range][0],ranges[range][1]);
        System.out.println("{\"source_dtype\":\""+type+"\",\"source_samples\":1,\"shape\":[2,8],\"pattern\":"+pattern+
          ",\"lut\":\""+names[lut]+"\",\"display_range\":"+(ranges[range]==null?"null":array(ranges[range]))+
          ",\"source\":"+array(input)+",\"pixels\":"+array(pixels(p.convertToRGB()))+"}");
      }
    for(int pattern=0;pattern<3;pattern++) {
      int[] p=new int[N];for(int i=0;i<N;i++){int r=pattern==1?7:(17*i+3)%256,g=pattern==1?31:255-i,b=pattern==1?127:(i*i+7)%256;p[i]=(r<<16)|(g<<8)|b;}
      ImageProcessor rgb=new ColorProcessor(8,2,p);
      System.out.println("{\"source_dtype\":\"uint8\",\"source_samples\":3,\"shape\":[2,8],\"pattern\":"+pattern+
        ",\"lut\":null,\"display_range\":null,\"source\":"+array(pixels(rgb))+",\"pixels\":"+array(pixels(rgb.convertToRGB()))+"}");
    }
  }
}
'''


def main():
    assert not WORK.exists(), 'Use a fresh owned reference directory'
    WORK.mkdir(parents=True)
    downloads = ROOT / 'build/references/downloads'
    source = verified(downloads / f'imagej-{COMMIT}.zip', IMAGEJ_SHA)
    runtime_archive = verified(downloads / 'fiji-latest-20261007-1617-win64-jdk.zip', FIJI_SHA)
    runtime = ROOT / 'build/references/binary-imagej154r/jdk'
    # Verify the shared runtime against its pinned archive before executing it.
    runtime_pins = []
    with zipfile.ZipFile(runtime_archive) as z:
        for member in z.infolist():
            if not member.is_dir() and member.filename.startswith(JDK_PREFIX):
                name = member.filename[len(JDK_PREFIX):]
                path = runtime / name
                with z.open(member) as stream:
                    expected = _stream_sha256(stream)
                assert path.is_file() and path.stat().st_size == member.file_size and digest(path) == expected, name
                runtime_pins.append(dict(name=name, bytes=member.file_size, sha256=expected))
    sources, classes = WORK / 'sources001', WORK / 'classes001'
    with zipfile.ZipFile(source) as z:
        extract(z, f'ImageJ-{COMMIT}/', sources)
    classes.mkdir()
    driver = WORK / 'RgbConversionReference.java'; driver.write_text(DRIVER, encoding='utf-8')
    args = WORK / 'javac.args'
    args.write_text('\n'.join('"'+p.as_posix()+'"' for p in [*sorted((sources/'ij').rglob('*.java')), driver])+'\n', encoding='utf-8')
    for label, command in [('javac', [runtime/'bin/javac.exe', '-encoding', 'UTF-8', '-d', classes, '@'+str(args)]),
                           ('java', [runtime/'bin/java.exe', '-Djava.awt.headless=true', '-cp', classes, 'RgbConversionReference'])]:
        result = subprocess.run([str(v) for v in command], capture_output=True, text=True, timeout=180)
        (WORK/(label+'.stdout')).write_text(result.stdout, encoding='utf-8')
        (WORK/(label+'.stderr')).write_text(result.stderr, encoding='utf-8')
        result.check_returncode()
        if label == 'javac':
            for p in sources.glob('*.txt'): shutil.copyfile(p, classes/p.name)
    cases = [json.loads(line) for line in result.stdout.splitlines() if line.strip()]
    assert len(cases) == 111
    members = ['ij/process/ImageProcessor.java','ij/process/TypeConverter.java','ij/process/ByteProcessor.java',
               'ij/process/ShortProcessor.java','ij/process/FloatProcessor.java','ij/process/ColorProcessor.java']
    payload = dict(schema_version=1, scope='Pinned direct uncalibrated ImageProcessor.convertToRGB with grayscale/inverse/custom LUT and processor display ranges; not composite-axis, threshold, density, stack/editor or indexed quantization certification. Float64/RGBA are owned extensions.',
                   imagej_version='1.54r', imagej_commit=COMMIT, source_archive_sha256=IMAGEJ_SHA,
                   runtime_archive_sha256=FIJI_SHA, runtime_pins=runtime_pins,
                   source_members_sha256={n:digest(sources/n) for n in members},
                   driver_sha256=digest(driver), generator='scripts/capture_rgb_conversion_reference.py', cases=cases)
    (WORK/'reference001.json').write_text(json.dumps(payload, indent=2)+'\n', encoding='utf-8')
    print('Captured 111 pinned ImageJ processor RGB cases')


if __name__ == '__main__': main()
