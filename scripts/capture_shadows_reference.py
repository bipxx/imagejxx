"""Capture ImageJ 1.54r directional shadows; upstream tools stay under build/."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import zipfile

from capture_binary_reference import ROOT, COMMIT, IMAGEJ_SHA, FIJI_SHA, digest, verified

WORK = ROOT / "build/p09g-shadows001/reference002"
DRIVER = r'''
import ij.*;
import ij.process.*;
import ij.plugin.filter.Shadows;
public class ShadowsReference {
 static ImageProcessor image(String type,int w,int h,int pattern) {
  int n=w*h;
  if(type.equals("rgb")) {
   int[] p=new int[n];
   for(int i=0;i<n;i++) {
    int r=value(i,pattern),g=value(i+3,pattern),b=value(i+7,pattern);
    p[i]=0xff000000|(r<<16)|(g<<8)|b;
   }
   return new ColorProcessor(w,h,p);
  }
  if(type.equals("float")) {
   float[] p=new float[n];
   for(int i=0;i<n;i++)p[i]=pattern==0?17.25f:pattern==1?((i*37)%251-123)/7f:
     i%3==0?1e8f:i%3==1?-1e8f:1f+((i*13)%11)/8f;
   return new FloatProcessor(w,h,p);
  }
  if(type.equals("short")) {
   short[] p=new short[n];for(int i=0;i<n;i++)p[i]=(short)(value(i,pattern)*257);
   return new ShortProcessor(w,h,p,null);
  }
  byte[] p=new byte[n];for(int i=0;i<n;i++)p[i]=(byte)value(i,pattern);
  return new ByteProcessor(w,h,p,null);
 }
 static int value(int i,int pattern) {
  return pattern==0?113:pattern==1?(i*37+13)%256:new int[]{0,255,1,254,127,128}[i%6];
 }
 static String pixels(ImageProcessor ip) {
  StringBuilder s=new StringBuilder("[");
  for(int i=0;i<ip.getPixelCount();i++) {
   if(i>0)s.append(',');
   if(ip instanceof ColorProcessor) {
    int v=((int[])ip.getPixels())[i];s.append((v>>16)&255).append(',').append((v>>8)&255).append(',').append(v&255);
   } else s.append(ip.getf(i));
  }
  return s.append(']').toString();
 }
 public static void main(String[] args) {
  for(String type:new String[]{"byte","short","float","rgb"})
   for(int[] size:new int[][]{{7,5},{1,1},{1,7},{7,1},{2,2},{3,3},{1,2},{1,3},{2,7},{7,2}})
    for(int pattern=0;pattern<3;pattern++)for(boolean rectangle:new boolean[]{false,true})
     for(String direction:new String[]{"north","northeast","east","southeast","south","southwest","west","northwest"}) {
      int w=size[0],h=size[1],x=0,y=0,rw=w,rh=h;
      if(rectangle&&w>=3&&h>=3){x=y=1;rw=w-2;rh=h-2;}
      ImageProcessor ip=image(type,w,h,pattern);String input=pixels(ip);
      ip.setRoi(x,y,rw,rh);Shadows filter=new Shadows();
      String error="null";
      try {filter.setup(direction,new ImagePlus("reference",ip));filter.run(ip);}
      catch(RuntimeException e){error="\""+e.getClass().getSimpleName()+"\"";}
      System.out.println("{\"type\":\""+type+"\",\"direction\":\""+direction+"\",\"width\":"+w+
       ",\"height\":"+h+",\"pattern\":"+pattern+",\"rectangle\":"+rectangle+
       ",\"extent\":["+x+","+y+","+rw+","+rh+"],\"source\":"+input+",\"output\":"+pixels(ip)+",\"reference_error\":"+error+"}");
     }
 }
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=WORK)
    parser.add_argument("--fixture-output", type=Path, default=ROOT / "tests/fixtures/shadows-imagej154r.json")
    args = parser.parse_args()
    work, target = args.output.resolve(), args.fixture_output.resolve()
    if work.exists() or target.exists():
        raise ValueError("Use fresh reference and fixture output paths")
    review = json.loads((ROOT / "docs/parity/shadows-commercial-source-review-001.json").read_bytes())
    if review["decision"] != "eligible":
        raise ValueError("Commercial reference review is not eligible")
    upstream = verified(ROOT / f"build/references/downloads/imagej-{COMMIT}.zip", IMAGEJ_SHA)
    held = json.loads((ROOT / "tests/fixtures/color-components-imagej154r.json").read_bytes())
    if (held["imagej_commit"] != COMMIT or held["source_archive_sha256"] != IMAGEJ_SHA
            or held["runtime_archive_sha256"] != FIJI_SHA):
        raise ValueError("Pinned runtime/source provenance changed")
    runtime = ROOT / "build/references/binary-imagej154r/jdk"
    for item in held["runtime_pins"]:
        path = runtime / item["name"]
        if path.stat().st_size != item["bytes"] or digest(path) != item["sha256"]:
            raise ValueError(f"Pinned runtime changed: {item['name']}")
    sources, classes = work / "sources", work / "classes"
    sources.mkdir(parents=True); classes.mkdir()
    with zipfile.ZipFile(upstream) as archive:
        for item in review["source_members"]:
            raw = archive.read(f"ImageJ-{COMMIT}/" + item["name"])
            if len(raw) != item["bytes"] or hashlib.sha256(raw).hexdigest() != item["sha256"]:
                raise ValueError("Reviewed reference member changed")
        prefix = f"ImageJ-{COMMIT}/"
        for name in archive.namelist():
            if not name.startswith(prefix) or not name.endswith(".java"):
                continue
            member = Path(name[len(prefix):])
            if member.parts[0] != "ij":
                continue
            destination = sources / member
            if not destination.resolve().is_relative_to(sources.resolve()):
                raise ValueError("Reference member escapes its owned directory")
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(archive.read(name))
    driver = work / "ShadowsReference.java"; driver.write_text(DRIVER, encoding="utf-8")
    arguments = work / "javac.args"
    arguments.write_text("\n".join('"' + p.as_posix() + '"' for p in
                         [*sorted(sources.rglob("*.java")), driver]), encoding="utf-8")
    commands = [
        ("javac", [runtime / "bin/javac.exe", "-encoding", "UTF-8", "-d", classes, "@" + str(arguments)]),
        ("java", [runtime / "bin/java.exe", "-Djava.awt.headless=true", "-cp", classes, "ShadowsReference"]),
    ]
    for label, argv in commands:
        with (work / (label + ".stdout")).open("wb") as out, (work / (label + ".stderr")).open("wb") as err:
            subprocess.run([str(v) for v in argv], stdout=out, stderr=err, check=True, timeout=180)
    raw = (work / "java.stdout").read_bytes()
    if len(raw) > 16 * 1024 * 1024:
        raise ValueError("Reference payload exceeds budget")
    cases = [json.loads(line) for line in raw.splitlines() if line.strip()]
    if len(cases) != 1920:
        raise ValueError(f"Expected 1920 reference cases, got {len(cases)}")
    result = dict(schema_version=1, imagej_version="1.54r", imagej_commit=COMMIT,
        source_archive_sha256=IMAGEJ_SHA, runtime_archive_sha256=FIJI_SHA, source_members=review["source_members"],
        runtime_pins=held["runtime_pins"], driver_sha256=digest(driver),
        generator="scripts/capture_shadows_reference.py",
        scope="Shadows setup/run for eight directions, byte/short/finite-float/RGB, full and rectangular extents; no demo, irregular mask, UI or full-family claim.", cases=cases)
    target.parent.mkdir(parents=True, exist_ok=True)
    with target.open("x", encoding="utf-8") as stream:
        stream.write(json.dumps(result, indent=2) + "\n")
    print(f"Captured {len(cases)} ImageJ shadow cases")


if __name__ == "__main__":
    main()
