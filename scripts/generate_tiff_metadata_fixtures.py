"""Independent TIFF calibration refusals and literal default ledgers."""
from pathlib import Path
import hashlib
import json
import struct
from generate_scientific_tiff_fixtures import make
from generate_tiff_acceptance_fixtures import wire

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT/'tests/fixtures/scientific-tiff-metadata'
ORIGINS = ['xorigin', 'yorigin', 'zorigin']


def generate():
    DEST.mkdir(parents=True, exist_ok=True)
    records = []
    def save(name, raw, shape, valid, defaults=None, spacing=None, unit='px', frame=1, time='s', reason=''):
        pages = wire(raw)
        assert len(pages) == shape[0]*shape[1]*shape[2]
        for p,page in enumerate(pages):
            t,z,c = p//(shape[1]*shape[2]), (p//shape[1])%shape[2], p%shape[1]
            assert page['pixels'] == [10000*t+1000*c+100*z+10*y+x for y in range(shape[3]) for x in range(shape[4])]
        path = DEST/(name+'.tif'); path.write_bytes(raw)
        # All fixture pixels precede every metadata payload/IFD, in one interval.
        samples = 1
        for n in shape:samples *= n
        records.append(dict(file=path.name, bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest(),
            valid=valid, shape=[*shape,1], pixel_interval=[8,2*samples], reason=reason,
            defaulted_fields=defaults, spacing=spacing, unit=unit, frame_interval=frame, time_unit=time,
            description=pages[0]['tags'][270]))
    def carrier(description, shape=(1,1,1,2,3), **kwargs):
        return make(description=description, shape=shape,
                    sample=lambda t,c,z,y,x:10000*t+1000*c+100*z+10*y+x, **kwargs)
    minimal='ImageJ=1.54r\n'
    all_defaults=['legacy_z_from_ifds','channels','slices','frames','hyperstack','unit',
                  'spacing_x','spacing_y','spacing','finterval','tunit',*ORIGINS]
    raw,_=carrier(minimal, (1,1,3,2,3), basic=True)
    save('legacy-ledger',raw,(1,1,3,2,3),True,all_defaults,[1,1,1])
    explicit=minimal+'channels=1\nslices=1\nframes=1\n'
    raw,_=carrier(explicit,basic=True)
    save('singleton-ledger',raw,(1,1,1,2,3),True,all_defaults[4:],[1,1,1])
    t1=explicit+'hyperstack=true\nunit=pixel\nspacing=1\n'
    raw,_=carrier(t1)
    save('t1-missing-interval',raw,(1,1,1,2,3),True,['finterval','tunit',*ORIGINS],[.5,1.25,1])
    t2=minimal+'channels=1\nslices=1\nframes=2\nhyperstack=true\nunit=mm\nspacing=2.5\nfinterval=0.25\ntunit=sec\n'
    raw,_=carrier(t2,(2,1,1,2,3))
    save('t2-explicit-interval',raw,(2,1,1,2,3),True,ORIGINS,[.5,1.25,2.5],'mm',.25)
    full=(minimal+'channels=3\nslices=2\nframes=2\nhyperstack=true\nunit=um\nspacing=2.5\nfinterval=2.5\ntunit=ms\nxorigin=0\nyorigin=0\nzorigin=0\n')
    repeated=full.replace('\n','\r\n')
    raw,_=carrier(repeated,(2,3,2,2,3),later_description=repeated)
    save('repeated-description',raw,(2,3,2,2,3),True,[],[.5,1.25,2.5],'um',2.5,'ms')
    escaped=t2.replace('unit=mm','unit=\\u00B5m').replace('tunit=sec','tunit=ms')
    raw,_=carrier(escaped,(2,1,1,2,3))
    save('escaped-unit-ledger',raw,(2,1,1,2,3),True,ORIGINS,[.5,1.25,2.5],'um',.25,'ms')
    base=explicit+'hyperstack=true\nunit=um\nspacing=2.5\nfinterval=2.5\ntunit=ms\n'
    raw,meta=carrier(base)
    for tag,label in ((282,'x'),(283,'y')):
        slot=meta['fields'][0][tag];offset=struct.unpack_from('<I',raw,slot+8)[0]
        for component,part in enumerate(('numerator','denominator')):
            invalid=bytearray(raw);struct.pack_into('<I',invalid,offset+4*component,0)
            save(f'{label}-zero-{part}',invalid,(1,1,1,2,3),False,reason='RATIONAL zero '+part)
    for field in ('spacing','finterval'):
        for label,value in [('zero','0'),('negative','-1'),('nan','NaN'),('inf','Infinity'),('underflow','1e-9999'),('overflow','1e9999')]:
            invalid,_=carrier(base.replace(field+'=2.5',field+'='+value))
            save(field+'-'+label,invalid,(1,1,1,2,3),False,reason=field+' invalid finite/positive boundary')
    invalid,_=carrier(t2.replace('finterval=0.25\n',''),(2,1,1,2,3))
    save('t2-missing-interval',invalid,(2,1,1,2,3),False,reason='T>1 requires explicit interval')
    manifest=dict(schema_version=1,origin='Original independent struct carriers and literal ledgers, no product imports',
                  pixel_equation='10000*t+1000*c+100*z+10*y+x',files=records)
    (DEST/'SOURCE.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps(dict(state='passed',fixtures=len(records),valid=sum(r['valid'] for r in records))))

if __name__=='__main__':generate()
