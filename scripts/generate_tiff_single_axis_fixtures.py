"""Independent carriers for the runtime-qualified single-axis flag omission."""
from pathlib import Path
import hashlib,json
from generate_scientific_tiff_fixtures import make

ROOT=Path(__file__).resolve().parents[1]
DEST=ROOT/'tests/fixtures/scientific-tiff-single-axis'
def generate():
    DEST.mkdir(parents=True,exist_ok=True);records=[]
    base='ImageJ=1.54r\nunit=um\nspacing=2.5\nfinterval=0.125\ntunit=ms\n'
    cases=[('channels-only',(1,3,1,2,3),'channels=3\n',True,['slices','frames','hyperstack','xorigin','yorigin','zorigin']),
      ('time-only',(2,1,1,2,3),'frames=2\n',True,['channels','slices','hyperstack','xorigin','yorigin','zorigin']),
      ('mixed-cz',(1,3,2,2,3),'channels=3\nslices=2\n',False,[]),
      ('mixed-ct',(2,3,1,2,3),'channels=3\nframes=2\n',False,[]),
      ('false-channels',(1,3,1,2,3),'channels=3\nhyperstack=false\n',False,[]),
      ('false-time',(2,1,1,2,3),'frames=2\nhyperstack=false\n',False,[])]
    for name,shape,axes,valid,defaults in cases:
        raw,meta=make(description=base+axes,shape=shape,rows=2,sample=lambda t,c,z,y,x:10000*t+1000*c+100*z+10*y+x)
        path=DEST/(name+'.tif');path.write_bytes(raw)
        count=2
        for n in shape:count*=n
        records.append(dict(file=path.name,bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest(),valid=valid,shape=[*shape,1],defaulted_fields=defaults,pixel_interval=[8,count]))
    (DEST/'SOURCE.json').write_text(json.dumps(dict(schema_version=1,origin='Original independent struct carriers; no ImageJ code incorporated',files=records),indent=2)+'\n')
    print(json.dumps(dict(state='passed',fixtures=len(records))))
if __name__=='__main__':generate()
