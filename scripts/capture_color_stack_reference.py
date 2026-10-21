"""Capture pinned component arithmetic and ImageJ stack layouts in owned scratch.

No upstream source/runtime is shipped. Promote JSON only after this job ends.
"""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import zipfile
from capture_binary_reference import ROOT, COMMIT, IMAGEJ_SHA, FIJI_SHA, JDK_PREFIX, digest, verified, extract

WORK = ROOT / 'build/p06zs-color-stack001/reference003'
DRIVER = r'''
import ij.*;
import ij.process.*;
import ij.plugin.CompositeConverter;
import ij.macro.Interpreter;
import ij.measure.Calibration;
public class ColorStackReference {
  static String array(double[] a) {
    StringBuilder s=new StringBuilder("[");for(int i=0;i<a.length;i++){if(i>0)s.append(',');s.append(a[i]);}return s.append(']').toString();
  }
  static double[] pixels(ImageProcessor p) {
    int n=p.getWidth()*p.getHeight();double[] a=new double[n*(p instanceof ColorProcessor?3:1)];
    for(int i=0;i<n;i++)if(p instanceof ColorProcessor){int c=p.get(i);a[3*i]=(c>>16)&255;a[3*i+1]=(c>>8)&255;a[3*i+2]=c&255;}else a[i]=p.getf(i);
    return a;
  }
  static String planes(ImageStack s) {
    StringBuilder b=new StringBuilder("[");for(int i=1;i<=s.size();i++){if(i>1)b.append(',');b.append(array(pixels(s.getProcessor(i))));}return b.append(']').toString();
  }
  static int[] colors(int pattern) {
    if(pattern==0)return new int[]{0,0xffffff,0xff0000,0x00ff00,0x0000ff,0xffff00,0xff00ff,0x00ffff,0x010101,0x7f7f7f,0x808080,0xfefefe,0x010203,0xfffe01,0xfeff01,0x01feff};
    if(pattern==1){int[] a=new int[256];for(int i=0;i<256;i++)a[i]=(i<<16)|(i<<8)|i;return a;}
    if(pattern==2){int[] a=new int[343];int i=0;for(int r:new int[]{0,1,2,127,128,254,255})for(int g:new int[]{0,1,2,127,128,254,255})for(int b:new int[]{0,1,2,127,128,254,255})a[i++]=(r<<16)|(g<<8)|b;return a;}
    int[] a=new int[1024];int state=0x13579bdf;for(int i=0;i<a.length;i++){state=1664525*state+1013904223;a[i]=state&0xffffff;}return a;
  }
  static ImageStack split(ColorProcessor cp,String space) {
    ImagePlus imp=new ImagePlus("component",cp.duplicate());ImageConverter c=new ImageConverter(imp);
    if(space.equals("rgb"))c.convertToRGBStack();else if(space.equals("hsb8"))c.convertToHSB();else c.convertToHSB32();return imp.getStack();
  }
  static void merge(ImageStack stack,String space,boolean inverse,String pattern) {
    String input=planes(stack);ImagePlus imp=new ImagePlus("merge",stack);
    if(inverse){imp.getProcessor().invertLut();stack.setColorModel(imp.getProcessor().getColorModel());}
    ImageConverter c=new ImageConverter(imp);
    if(space.equals("rgb"))c.convertRGBStackToRGB();else if(space.equals("hsb8"))c.convertHSBToRGB();else c.convertHSB32ToRGB();
    System.out.println("{\"kind\":\"merge\",\"space\":\""+space+"\",\"pattern\":\""+pattern+"\",\"inverse\":"+inverse+",\"shape\":["+stack.getHeight()+","+stack.getWidth()+"],\"input\":"+input+",\"output\":"+array(pixels(imp.getProcessor()))+"}");
  }
  static void components() {
    for(int pattern=0;pattern<4;pattern++){
      int[] values=colors(pattern);ColorProcessor cp=new ColorProcessor(values.length,1,values);
      for(String space:new String[]{"rgb","hsb8","hsb32"}){
        ImageStack stack=split(cp,space);
        System.out.println("{\"kind\":\"split\",\"space\":\""+space+"\",\"pattern\":\"rgb-"+pattern+"\",\"shape\":[1,"+values.length+"],\"input\":"+array(pixels(cp))+",\"output\":"+planes(stack)+"}");
        if(space.equals("rgb"))for(int count:new int[]{2,3})for(boolean inverse:new boolean[]{false,true}){
          ImageStack input=new ImageStack(cp.getWidth(),1);for(int k=1;k<=count;k++)input.addSlice(stack.getProcessor(k).duplicate());merge(input,space,inverse,"rgb-"+pattern+"-"+count);
        }else merge(stack,space,false,"roundtrip-"+pattern);
      }
    }
    for(int pattern=0;pattern<3;pattern++){
      int n=pattern==2?512:256;ImageStack stack=new ImageStack(n,1);
      for(int c=0;c<3;c++){byte[] p=new byte[n];for(int i=0;i<n;i++)p[i]=(byte)(pattern==0?(c==0?i:255):pattern==1?(c==0?37:c==1?0:i):(i*(c==0?17:c==1?31:61)+c*13));stack.addSlice(new ByteProcessor(n,1,p,null));}
      merge(stack,"hsb8",false,"authored-"+pattern);
    }
    float[] hues={-3f,-2.0000002f,-1f,-Float.MIN_VALUE,0f,Float.MIN_VALUE,Math.nextDown(1f/6f),1f/6f,Math.nextUp(1f/6f),Math.nextDown(.5f),.5f,Math.nextUp(.5f),Math.nextDown(1f),1f,Math.nextUp(1f),Float.MAX_VALUE,-Float.MAX_VALUE};
    float[] sv={-Float.MAX_VALUE,-1f,-.01f,-0f,Float.MIN_VALUE,.2f,.5f,1f,1.5f,Float.MAX_VALUE};
    int n=hues.length*sv.length*sv.length;float[][] p=new float[3][n];int i=0;
    for(float h:hues)for(float s:sv)for(float b:sv){p[0][i]=h;p[1][i]=s;p[2][i]=b;i++;}
    ImageStack stack=new ImageStack(n,1);for(int c=0;c<3;c++)stack.addSlice(new FloatProcessor(n,1,p[c]));merge(stack,"hsb32",false,"authored-boundaries");
    p=new float[3][1024];int state=0x2468ace;
    for(int c=0;c<3;c++)for(i=0;i<1024;i++){state=1664525*state+1013904223;p[c][i]=c==0?((state>>>8)%16384)/1024f-8f:((state>>>8)%5120)/1024f-2f;}
    stack=new ImageStack(1024,1);for(int c=0;c<3;c++)stack.addSlice(new FloatProcessor(1024,1,p[c]));merge(stack,"hsb32",false,"authored-random");
  }
  static String calibration(ImagePlus imp) {
    Calibration c=imp.getCalibration();return "{\"spacing\":["+c.pixelWidth+","+c.pixelHeight+","+c.pixelDepth+"],\"world_origin\":["+c.getX(0)+","+c.getY(0)+","+c.getZ(0)+"],\"unit\":\""+c.getUnit().replace("\u00b5","\\u00b5")+"\",\"frame_interval\":"+c.frameInterval+",\"time_unit\":\""+c.getTimeUnit()+"\"}";
  }
  static void layout(String operation,int c,int z,int t,String type) {
    ImageStack stack=new ImageStack(2,1);int count=c*z*t;
    for(int i=0;i<count;i++){
      ImageProcessor ip;
      if(type.equals("rgb"))ip=new ColorProcessor(2,1,new int[]{((i+1)<<16)|((i+21)<<8)|(i+41),((i+61)<<16)|((i+81)<<8)|(i+101)});
      else if(type.equals("uint8"))ip=new ByteProcessor(2,1,new byte[]{(byte)(i+1),(byte)(i+51)},null);
      else if(type.equals("uint16"))ip=new ShortProcessor(2,1,new short[]{(short)(i+1),(short)(i+1051)},null);
      else ip=new FloatProcessor(2,1,new float[]{i-2.5f,i+7.5f});
      stack.addSlice("plane-"+i,ip);
    }
    ImagePlus imp=new ImagePlus("layout",stack);imp.setDimensions(c,z,t);imp.setPosition(c,z,t);
    Calibration cal=imp.getCalibration();cal.pixelWidth=.5;cal.pixelHeight=1.5;cal.pixelDepth=2.5;cal.xOrigin=2;cal.yOrigin=3;cal.zOrigin=4;cal.setUnit("um");cal.frameInterval=.125;cal.setTimeUnit("ms");imp.setCalibration(cal);
    String source=planes(stack),before=calibration(imp);WindowManager.setTempCurrentImage(imp);
    ImagePlus result=imp;
    if(operation.equals("gray_to_rgb")){if(count==1)new ImageConverter(imp).convertToRGB();else new StackConverter(imp).convertToRGB();}
    else if(operation.equals("rgb_components"))result=CompositeConverter.makeComposite(imp);
    else if(count==1){if(operation.equals("hsb8_components"))new ImageConverter(imp).convertToHSB();else new ImageConverter(imp).convertToHSB32();}
    else {if(operation.equals("hsb8_components"))new StackConverter(imp).convertToHSBHyperstack();else new StackConverter(imp).convertToHSB32Hyperstack();result=Interpreter.getLastBatchModeImage();}
    StringBuilder labels=new StringBuilder("[");for(int k=1;k<=result.getStackSize();k++){if(k>1)labels.append(',');String label=result.getStack().getSliceLabel(k);labels.append(label==null?"null":"\""+label+"\"");}labels.append(']');
    System.out.println("{\"kind\":\"layout\",\"operation\":\""+operation+"\",\"source_type\":\""+type+"\",\"source_czt\":["+c+","+z+","+t+"],\"source\":"+source+",\"source_calibration\":"+before+",\"output_czt\":["+result.getNChannels()+","+result.getNSlices()+","+result.getNFrames()+"],\"bit_depth\":"+result.getBitDepth()+",\"labels\":"+labels+",\"output\":"+planes(result.getStack())+",\"output_calibration\":"+calibration(result)+"}");
    WindowManager.setTempCurrentImage(null);for(int id:Interpreter.getBatchModeImageIDs())Interpreter.removeBatchModeImage(Interpreter.getBatchModeImage(id));
  }
  public static void main(String[] args) {
    Interpreter.batchMode=true;components();
    for(String type:new String[]{"uint8","uint16","float32"})for(int[] d:new int[][]{{1,1,1},{1,3,1},{1,2,2},{2,2,1},{2,2,2},{3,1,2}})layout("gray_to_rgb",d[0],d[1],d[2],type);
    for(String operation:new String[]{"rgb_components","hsb8_components","hsb32_components"})for(int[] d:new int[][]{{1,1,1},{1,3,1},{1,1,3},{1,2,2}})layout(operation,d[0],d[1],d[2],"rgb");
  }
}
'''


def main():
    assert not WORK.exists()
    WORK.mkdir(parents=True)
    source=verified(ROOT/'build/references/downloads'/f'imagej-{COMMIT}.zip',IMAGEJ_SHA)
    archive=verified(ROOT/'build/references/downloads/fiji-latest-20261007-1617-win64-jdk.zip',FIJI_SHA)
    runtime=ROOT/'build/references/binary-imagej154r/jdk'
    pins=[]
    with zipfile.ZipFile(archive) as z:
        for m in z.infolist():
            if not m.is_dir() and m.filename.startswith(JDK_PREFIX):
                name=m.filename[len(JDK_PREFIX):];p=runtime/name
                with z.open(m) as f:sha=hashlib.file_digest(f,'sha256').hexdigest()
                assert p.stat().st_size==m.file_size and digest(p)==sha,name
                pins.append(dict(name=name,bytes=m.file_size,sha256=sha))
    sources=WORK/'sources001';classes=WORK/'classes001';classes.mkdir()
    with zipfile.ZipFile(source) as z:extract(z,f'ImageJ-{COMMIT}/',sources)
    driver=WORK/'ColorStackReference.java';driver.write_text(DRIVER,encoding='utf-8')
    args=WORK/'javac.args';args.write_text('\n'.join('"'+p.as_posix()+'"' for p in [*sorted((sources/'ij').rglob('*.java')),driver])+'\n')
    for name,command in [('javac',[runtime/'bin/javac.exe','-encoding','UTF-8','-d',classes,'@'+str(args)]),('java',[runtime/'bin/java.exe','-Djava.awt.headless=true','-cp',classes,'ColorStackReference'])]:
        result=subprocess.run([str(v) for v in command],capture_output=True,text=True,timeout=180)
        (WORK/(name+'.stdout')).write_text(result.stdout,encoding='utf-8');(WORK/(name+'.stderr')).write_text(result.stderr,encoding='utf-8');result.check_returncode()
        if name=='javac':
            for p in sources.glob('*.txt'):shutil.copyfile(p,classes/p.name)
    rows=[json.loads(line) for line in result.stdout.splitlines() if line.strip()]
    cases=[v for v in rows if v['kind']!='layout'];layouts=[v for v in rows if v['kind']=='layout']
    assert len(cases)==41 and len(layouts)==30,(len(cases),len(layouts))
    members=['ij/process/ColorProcessor.java','ij/process/ImageConverter.java','ij/process/StackConverter.java','ij/plugin/CompositeConverter.java','ij/ImagePlus.java']
    with zipfile.ZipFile(runtime/'lib/src.zip') as z:color_sha=hashlib.sha256(z.read('java.desktop/java/awt/Color.java')).hexdigest()
    payload=dict(schema_version=1,imagej_version='1.54r',imagej_commit=COMMIT,source_archive_sha256=IMAGEJ_SHA,runtime_archive_sha256=FIJI_SHA,runtime_pins=pins,
        source_members_sha256={n:digest(sources/n) for n in members},jdk_color_source_sha256=color_sha,driver_sha256=digest(driver),generator='scripts/capture_color_stack_reference.py',
        scope='Direct uncalibrated RGB/HSB8/HSB32 component arithmetic, including finite authored float HSB values; observed ImageJ stack C/Z/T, labels and calibration layouts. Not editor, annotation, flattening-consent or indexed quantization certification.',cases=cases,layout_cases=layouts)
    (WORK/'reference001.json').write_text(json.dumps(payload,indent=2)+'\n')
    print('Captured41 component and30 layout cases')


if __name__=='__main__':main()
