"""Original narrowly mutated TIFF carriers and complete excluded layouts."""
from pathlib import Path
import hashlib
import json
import struct
from generate_scientific_tiff_fixtures import make

ROOT=Path(__file__).resolve().parents[1]
DEST=ROOT/'tests/fixtures/scientific-tiff-refusals'
DESCRIPTION='ImageJ=1.54r\nchannels=1\nslices=1\nframes=1\nhyperstack=true\nunit=um\nspacing=2.5\nfinterval=1\ntunit=ms\n'


def carrier(description=DESCRIPTION, shape=(1,1,1,3,5), rows=None):
    return make(description=description,shape=shape,rows=shape[3] if rows is None else rows,
                sample=lambda t,c,z,y,x:10000*t+1000*c+100*z+10*y+x)


def fields(raw,meta,page=0):
    return {tag:bytes(raw[at:at+12]) for tag,at in meta['fields'][page].items()}


def block(raw,data,alignment=2):
    while len(raw)%alignment:raw.append(0)
    offset=len(raw);raw.extend(data);return offset


def tag(raw,number,kind,count,data):
    assert len(data)==count*{1:1,2:1,3:2,4:4,12:8}[kind]
    value=data.ljust(4,b'\0') if len(data)<=4 else struct.pack('<I',block(raw,data))
    return struct.pack('<HHI',number,kind,count)+value


def directory(raw,entries,next_ifd=0):
    ordered=sorted(entries.items());assert all(struct.unpack_from('<H',value)[0]==key for key,value in ordered)
    return block(raw,struct.pack('<H',len(ordered))+b''.join(v for k,v in ordered)+struct.pack('<I',next_ifd))


def root_directory(raw,entries):
    offset=directory(raw,entries);struct.pack_into('<I',raw,4,offset);return offset


def generate():
    DEST.mkdir(parents=True,exist_ok=True);records=[]
    def save(name,raw,intervals,reason,category='metadata',witness=None):
        for start,size in intervals:assert start>=8 and size and start+size<=len(raw)
        for item in witness or []:assert bytes(raw[item['offset']:item['offset']+len(bytes.fromhex(item['hex']))])==bytes.fromhex(item['hex'])
        path=DEST/(name+'.tif');path.write_bytes(raw)
        records.append(dict(file=path.name,bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest(),
          valid=False,pixel_intervals=intervals,reason=reason,error_category=category,witness=witness or []))
    base,meta=carrier();pixels=[[8,30]]
    def mutate(name,offset,value,reason,category='metadata',raw=None,intervals=None):
        out=bytearray(base if raw is None else raw);out[offset:offset+len(value)]=value
        save(name,out,pixels if intervals is None else intervals,reason,category,[dict(offset=offset,hex=value.hex())])
    mutate('bad-endian',0,b'ZZ','Invalid byte-order marker')
    mutate('bad-magic',2,struct.pack('<H',41),'Non-TIFF magic')
    mutate('ifd-entry-overflow',meta['ifds'][0],struct.pack('<H',65535),'IFD count exceeds bounded admission','resource')
    mutate('truncated-ifd-table',meta['ifds'][0],struct.pack('<H',24),'IFD table extends past EOF')
    mutate('strip-table-count-overflow',meta['fields'][0][273]+4,struct.pack('<I',0xffffffff),'Strip table external value exceeds file')
    mutate('strip-outside-file',meta['fields'][0][273]+8,struct.pack('<I',len(base)+100),'Declared strip is outside file')
    mutate('zero-rows',meta['fields'][0][278]+8,struct.pack('<I',0),'Zero RowsPerStrip')
    raw=bytearray(base);entries=fields(raw,meta);del entries[273];root_directory(raw,entries)
    save('missing-strip-offset',raw,pixels,'Required StripOffsets tag is absent')
    multi,mm=carrier(rows=2)
    for number,label in ((273,'offset'),(279,'bytecount')):
        mutate('strip-'+label+'-count',mm['fields'][0][number]+4,struct.pack('<I',1),'Strip table count disagrees with rows',raw=multi)
    for axis in ('x','y','z'):
        raw,_=carrier(DESCRIPTION+axis+'origin=1\n')
        save(axis+'-origin',raw,pixels,'Nonzero spatial origin')
    for name,description in [
        ('unsupported-space-unit',DESCRIPTION.replace('unit=um','unit=nm')),
        ('unsupported-time-unit',DESCRIPTION.replace('tunit=ms','tunit=hour')),
        ('mixed-axis-unit',DESCRIPTION+'yunit=mm\n'),
        ('density-calibration',DESCRIPTION+'cf=0\nc0=1\nc1=2\nvunit=gray\n'),
        ('nonidentity-description-direction',DESCRIPTION+'direction=-1,0,0,0,1,0,0,0,1\n'),
    ]:
        raw,_=carrier(description);save(name,raw,pixels,'Unsupported calibration semantics: '+name)
    hidden,hm=carrier(DESCRIPTION+'images=1\n',shape=(1,1,2,3,5))
    save('hidden-extra-page',hidden,[[8,60]],'Two explicit IFDs but dimensions/images claim one')
    # A genuine child IFD and pointer, not a missing/garbled SubIFDs payload.
    raw=bytearray(base);entries=fields(raw,meta);child=directory(raw,entries)
    entries[330]=tag(raw,330,4,1,struct.pack('<I',child));root_directory(raw,entries)
    save('subifd-child',raw,pixels,'SubIFDs are outside the admitted single-series profile')
    # Different document names distinguish two documents in a full IFD chain.
    raw,info=carrier(DESCRIPTION.replace('slices=1','slices=2'),shape=(1,1,2,3,5));first=fields(raw,info,0);second=fields(raw,info,1)
    second[269]=tag(raw,269,2,6,b'Other\0');tail=directory(raw,second);start=directory(raw,first,tail);struct.pack_into('<I',raw,4,start)
    save('multiple-document-names',raw,[[8,60]],'Conflicting later-page DocumentName')
    # Valid RGB tag counts and payload size; the profile remains scalar-only.
    raw=bytearray(base);entries=fields(raw,meta)
    payload=struct.pack('<'+'H'*45,*[v for v in range(15) for channel in range(3)])
    offset=block(raw,payload)
    for number,kind,count,data in [(258,3,3,struct.pack('<HHH',16,16,16)),(262,3,1,struct.pack('<H',2)),(277,3,1,struct.pack('<H',3)),(273,4,1,struct.pack('<I',offset)),(279,4,1,struct.pack('<I',len(payload)))]:entries[number]=tag(raw,number,kind,count,data)
    root_directory(raw,entries);save('rgb-complete',raw,[[offset,len(payload)]],'RGB with three 16-bit samples')
    # A complete 16-bit palette includes all 3*65536 SHORT colormap entries.
    raw=bytearray(base);entries=fields(raw,meta);entries[262]=tag(raw,262,3,1,struct.pack('<H',3))
    entries[320]=tag(raw,320,3,3*65536,struct.pack('<'+'H'*(3*65536),*(list(range(65536))*3)))
    root_directory(raw,entries);save('palette-complete',raw,pixels,'Palette interpretation with complete ColorMap')
    mutate('signed-samples',meta['fields'][0][339]+8,struct.pack('<H',2),'Signed 16-bit sample format')
    raw=bytearray(base);entries=fields(raw,meta);payload=struct.pack('<15f',*range(15));offset=block(raw,payload)
    for number,kind,count,data in [(258,3,1,struct.pack('<H',32)),(339,3,1,struct.pack('<H',3)),(273,4,1,struct.pack('<I',offset)),(279,4,1,struct.pack('<I',len(payload)))]:entries[number]=tag(raw,number,kind,count,data)
    root_directory(raw,entries);save('float-complete',raw,[[offset,len(payload)]],'IEEE Float32 with complete payload')
    raw,info=carrier(shape=(1,1,1,16,16));entries=fields(raw,info)
    for number in (273,278,279):del entries[number]
    for number,value in ((322,16),(323,16),(324,8),(325,512)):entries[number]=tag(raw,number,4,1,struct.pack('<I',value))
    root_directory(raw,entries);save('tiled-complete',raw,[[8,512]],'One full 16x16 tile with exact payload')
    # PackBits literal packets reset at each 10-byte row.
    raw=bytearray(base);entries=fields(raw,meta);payload=b''.join(bytes([9])+bytes(base[8+10*y:18+10*y]) for y in range(3));offset=block(raw,payload)
    for number,kind,value in ((259,3,32773),(273,4,offset),(279,4,len(payload))):entries[number]=tag(raw,number,kind,1,struct.pack('<H' if kind==3 else '<I',value))
    root_directory(raw,entries);save('packbits-complete',raw,[[offset,len(payload)]],'PackBits with complete per-row literal packets')
    raw=bytearray(base);entries=fields(raw,meta);transform=(-1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.)
    entries[34264]=tag(raw,34264,12,16,struct.pack('<16d',*transform));root_directory(raw,entries)
    save('model-transformation',raw,pixels,'Nonidentity ModelTransformation metadata')
    raw=bytearray(base);entries=fields(raw,meta);entries[50839]=tag(raw,50839,1,8,b'IJ-proof');root_directory(raw,entries)
    save('imagej-private-metadata',raw,pixels,'Unsupported ImageJ semantic metadata bytes')
    # Complete BigTIFF, including 64-bit counts/offsets and inline RATIONALs.
    raw=bytearray(struct.pack('<2sHHHQ',b'II',43,8,0,0))
    payload=struct.pack('<15H',*[10*y+x for y in range(3) for x in range(5)])
    raw.extend(payload);description=DESCRIPTION.encode('ascii')+b'\0';desc=block(raw,description,8)
    entries=[]
    for number,kind,count,data in [
        (256,4,1,struct.pack('<I',5)),(257,4,1,struct.pack('<I',3)),
        (258,3,1,struct.pack('<H',16)),(259,3,1,struct.pack('<H',1)),(262,3,1,struct.pack('<H',1)),
        (270,2,len(description),struct.pack('<Q',desc)),(273,16,1,struct.pack('<Q',16)),
        (277,3,1,struct.pack('<H',1)),(278,4,1,struct.pack('<I',3)),(279,16,1,struct.pack('<Q',30)),
        (282,5,1,struct.pack('<II',2,1)),(283,5,1,struct.pack('<II',4,5)),
        (284,3,1,struct.pack('<H',1)),(296,3,1,struct.pack('<H',1)),(339,3,1,struct.pack('<H',1)),
    ]:entries.append(struct.pack('<HHQ',number,kind,count)+data.ljust(8,b'\0'))
    offset=block(raw,struct.pack('<Q',len(entries))+b''.join(entries)+struct.pack('<Q',0),8)
    struct.pack_into('<Q',raw,8,offset)
    save('bigtiff-complete',raw,[[16,30]],'Complete BigTIFF is outside the classic TIFF profile')
    assert len(records)==30,len(records)
    manifest=dict(schema_version=1,origin='Original struct mutations and excluded-layout carriers; no product writer or incorporated third-party code',
      constants_reference='https://gitlab.com/libtiff/libtiff/-/raw/v4.7.1/libtiff/tiff.h',files=records)
    (DEST/'SOURCE.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps(dict(state='passed',independent_refusal_carriers=len(records))))

if __name__=='__main__':generate()
