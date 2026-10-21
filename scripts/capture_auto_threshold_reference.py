"""Capture six missing histogram methods from pinned ImageJ 1.54r.

Reference Java/JDK stay under build only. Products contain no Java runtime or
source. Huang2 is present in the same pinned public ImageJ class; the driver
uses its static method with the same bilevel/range wrapper as getThreshold.
"""
from pathlib import Path
import hashlib, json, random, subprocess, zipfile
from capture_binary_reference import ROOT, COMMIT, IMAGEJ_SHA, FIJI_SHA, JDK_PREFIX, verified, extract

WORK = ROOT / "build/p09z-auto-threshold001/reference001"
OUTPUT = ROOT / "tests/fixtures/auto-threshold-imagej154r.json"
METHODS = ["default", "ij_isodata", "huang", "huang2", "renyi_entropy", "shanbhag"]
DRIVER = r'''
import ij.process.AutoThresholder;
import java.util.*;
import java.io.*;
public class MissingThresholdReference {
 static int huang2(int[] data) {
  AutoThresholder a=new AutoThresholder();int b=a.bilevel(data);if(b>=0)return b;
  int offset=0;int[] d=data;
  if(data.length>256) {
   int last=data.length-1;while(data[offset]==0)offset++;while(data[last]==0)last--;
   d=Arrays.copyOfRange(data,offset,last+1);
  }
  int t=AutoThresholder.Huang2(d);return (t==-1?0:t)+offset;
 }
 public static void main(String[] args)throws Exception {
  BufferedReader r=new BufferedReader(new InputStreamReader(System.in));String line;
  String[] methods={"Default","IJ_IsoData","Huang","Huang2","RenyiEntropy","Shanbhag"};
  while((line=r.readLine())!=null) {
   String[] fields=line.split(",");int[] h=new int[fields.length];
   for(int i=0;i<h.length;i++)h[i]=Integer.parseInt(fields[i]);
   for(int i=0;i<methods.length;i++) {
    if(i>0)System.out.print(",");
    System.out.print(i==3?huang2(h.clone()):new AutoThresholder().getThreshold(methods[i],h.clone()));
   }System.out.println();
  }
 }
}
'''

def cases():
    rows=[]
    def add(name, bins, pairs):
        rows.append(dict(id=name,bins=bins,nonzero={str(i):v for i,v in pairs if v}))
    for size in [256,257,65536]:
        for at in [0,1,size//2,size-1]:add(f"constant-{size}-{at}",size,[(at,7)])
        add(f"bilevel-edges-{size}",size,[(0,99),(size-1,1)])
        add(f"bilevel-neighbors-{size}",size,[(43,7),(44,11)])
    for size,offset in [(256,0),(257,0),(65536,12000),(65536,65000)]:
        width=min(size-offset,256)
        add(f"uniform-{size}-{offset}",size,[(i+offset,1) for i in range(width)])
        add(f"ramp-{size}-{offset}",size,[(i+offset,i+1) for i in range(width)])
        add(f"dominant-mode-{size}-{offset}",size,[(i+offset,900 if i==37 else 1+i%7) for i in range(width)])
        add(f"trimodal-{size}-{offset}",size,[(offset+17,89),(offset+57,73),(offset+231,47)])
    rng=random.Random(2043214)
    for index in range(24):
        size,offset=(256,0) if index<12 else (65536,10000+index*100)
        add(f"random-{index}",size,[(offset+i,rng.randrange(1,40)) for i in sorted(rng.sample(range(256),3+index*7))])
    return rows

def pin(path):
    b=path.read_bytes();return dict(bytes=len(b),sha256=hashlib.sha256(b).hexdigest())

def main():
    assert not WORK.exists();WORK.mkdir(parents=True)
    archive=verified(ROOT/f"build/references/downloads/imagej-{COMMIT}.zip",IMAGEJ_SHA)
    fiji=verified(ROOT/"build/references/downloads/fiji-latest-20261007-1617-win64-jdk.zip",FIJI_SHA)
    sources=WORK/"sources";classes=WORK/"classes";classes.mkdir()
    with zipfile.ZipFile(archive) as z:extract(z,f"ImageJ-{COMMIT}/",sources)
    runtime=ROOT/"build/references/roi-style-imagej154r/jdk"
    with zipfile.ZipFile(fiji) as z:
        for name in ["bin/java.exe","bin/javac.exe","lib/modules"]:
            assert pin(runtime/name)["sha256"]==hashlib.sha256(z.read(JDK_PREFIX+name)).hexdigest()
    driver=WORK/"MissingThresholdReference.java";driver.write_bytes(DRIVER.encode())
    args=WORK/"javac.args";args.write_bytes(("\n".join('"'+p.as_posix()+'"' for p in [*sorted((sources/'ij').rglob('*.java')),driver])+"\n").encode())
    with (WORK/"javac.log").open("xb") as f:
        subprocess.run([str(runtime/'bin/javac.exe'),'-encoding','UTF-8','-d',str(classes),'@'+str(args)],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=180)
    rows=cases();inputs=[]
    for row in rows:
        h=[0]*row['bins']
        for k,v in row['nonzero'].items():h[int(k)]=v
        inputs.append(','.join(map(str,h)))
    result=subprocess.run([str(runtime/'bin/java.exe'),'-Djava.awt.headless=true','-cp',str(classes),'MissingThresholdReference'],input=('\n'.join(inputs)+'\n').encode(),capture_output=True,check=True,timeout=300)
    (WORK/'stdout.txt').write_bytes(result.stdout);(WORK/'stderr.txt').write_bytes(result.stderr)
    lines=result.stdout.decode().splitlines();assert len(lines)==len(rows)
    for row,line in zip(rows,lines):
        values=list(map(int,line.split(',')));assert len(values)==6
        row['thresholds']=dict(zip(METHODS,values))
    source=sources/'ij/process/AutoThresholder.java'
    fixture=dict(schema_version=1,upstream=dict(commit=COMMIT,archive_sha256=IMAGEJ_SHA,source_sha256=pin(source)['sha256'],source_url=f'https://github.com/imagej/ImageJ/blob/{COMMIT}/ij/process/AutoThresholder.java',license_url='https://imagej.net/ij/disclaimer.html'),scope='Six histogram methods only; no complete plugin, image histogram scaling, dialog or scientific targeting qualification.',methods=METHODS,cases=rows)
    OUTPUT.write_bytes((json.dumps(fixture,indent=2)+'\n').encode())
    (WORK/'receipt.json').write_bytes((json.dumps(dict(state='captured',cases=len(rows),comparisons=len(rows)*6,fixture=pin(OUTPUT),driver=pin(driver),java=pin(runtime/'bin/java.exe'),javac=pin(runtime/'bin/javac.exe')),indent=2)+'\n').encode())
    print('Captured',len(rows)*6,'upstream histogram results')

if __name__=='__main__':main()
