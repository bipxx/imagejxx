"""Independent stdlib TIFF carriers: pixels precede metadata/IFDs; C-fast oracle."""
from pathlib import Path
import struct,json,hashlib
ROOT=Path(__file__).resolve().parents[1]
DEST=ROOT/'tests/fixtures/scientific-tiff-u16'

def make(endian='<',description=None,*,basic=False,shared=False,later_description=None,partial=False,conflict=False,typed_overlap=False,shape=None,rows=None,sample=None):
    if shape is None:c,z,t,y,x=(1,1,1,1,2) if basic else (3,2,2,3,5)
    else:t,c,z,y,x=shape
    count=c*z*t;rows=(y if basic else 2) if rows is None else rows
    description=description if description is not None else ('ImageJ=1.54r\n' if basic else 'ImageJ=1.54r\nimages=12\nchannels=3\nslices=2\nframes=2\nhyperstack=true\nunit=um\nspacing=2.75\nfinterval=0.125\ntunit=ms\n')
    out=bytearray(b'II' if endian=='<' else b'MM');out+=struct.pack(endian+'HI',42,0)
    def pack(fmt,*v):return struct.pack(endian+fmt,*v)
    def block(data):
        if len(out)%2:out.append(0)
        at=len(out);out.extend(data);return at
    pixels=[];offsets=[]
    for page in range(count):
        cc=page%c;zz=(page//c)%z;tt=page//(c*z)
        values=[sample(tt,cc,zz,yy,xx) if sample else 10000*tt+1000*cc+100*zz+yy*x+xx for yy in range(y) for xx in range(x)]
        pixels.append(values);offsets.append(block(pack('H'*len(values),*values)))
    desc=block(description.encode('ascii')+b'\0')
    other_desc=block(later_description.encode('ascii')+b'\0') if later_description else None
    strips=(y+rows-1)//rows;counts=[2*x*min(rows,y-i*rows) for i in range(strips)]
    byte_counts=block(pack('I'*strips,*counts)) if strips>1 else None
    rx=block(pack('II',2,1)) if not basic else None
    ry=rx if shared else block(pack('II',4,5)) if not basic else None
    later_rx=block(pack('II',3,1)) if conflict else rx
    if typed_overlap:rx=byte_counts
    optional={269:b'Lab\0',305:b'Independent struct fixture\0',306:b'2026:10:18 12:34:56\0',315:b'Fixture author\0'}
    optional_offsets={tag:block(data) for tag,data in optional.items() if len(data)>4} if not basic else {}
    directories=[]
    for page in range(count):
        entries=[]
        def field(tag,kind,n,encoded,external=False):
            assert len(encoded)<=4
            entries.append((tag,kind,n,encoded.ljust(4,b'\0')))
        def short(tag,v):field(tag,3,1,pack('H',v))
        def long(tag,v):field(tag,4,1,pack('I',v))
        if basic:
            short(256,x);short(257,y)
        else:
            long(254,0);long(256,x);long(257,y)
        short(258,16)
        if not basic:short(259,1)
        short(262,1)
        if not basic:field(269,2,4,optional[269])
        if page==0 or other_desc is not None:
            d=description if page==0 else later_description
            field(270,2,len(d)+1,pack('I',desc if page==0 else other_desc),True)
        strip_offsets=[offsets[page]+i*rows*x*2 for i in range(strips)]
        if basic:short(273,strip_offsets[0])
        elif strips==1:long(273,strip_offsets[0])
        else:field(273,4,strips,pack('I',block(pack('I'*strips,*strip_offsets))),True)
        if not basic:short(274,1);short(277,1);long(278,rows)
        if basic:short(279,counts[0])
        elif strips==1:long(279,counts[0])
        else:field(279,4,strips,pack('I',byte_counts),True)
        if not basic:
            field(282,5,1,pack('I',rx if page==0 else later_rx),True)
            if not (partial and page==1):field(283,5,1,pack('I',ry),True)
            short(284,1)
            if not (partial and page==1):short(296,1)
            field(297,3,2,pack('HH',page,count))
            for tag in [305,306,315]:field(tag,2,len(optional[tag]),pack('I',optional_offsets[tag]),True)
            short(317,1);short(339,1)
        entries.sort()
        directories.append(entries)
    if len(out)%2:out.append(0)
    ifds=[]
    for entries in directories:ifds.append(len(out));out.extend(bytes(6+12*len(entries)))
    out[4:8]=pack('I',ifds[0])
    fields=[]
    for page,(at,entries) in enumerate(zip(ifds,directories)):
        out[at:at+2]=pack('H',len(entries));mapping={}
        for i,(tag,kind,n,value) in enumerate(entries):
            slot=at+2+12*i;out[slot:slot+12]=pack('HHI',tag,kind,n)+value;mapping[tag]=slot
        out[at+2+12*len(entries):at+6+12*len(entries)]=pack('I',ifds[page+1] if page+1<count else 0)
        fields.append(mapping)
    return out,dict(endian=endian,shape=[t,c,z,y,x,1],ifds=ifds,fields=fields,description_offset=desc,description_bytes=len(description)+1,pixels=pixels,strip_counts_offset=byte_counts)

def generate():
    DEST.mkdir(parents=True,exist_ok=True);records=[]
    def save(name,data,meta,valid=True,reason=''):
        path=DEST/name;path.write_bytes(data)
        records.append(dict(file=name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),valid=valid,reason=reason,shape=meta['shape']))
    for name,kwargs in [('little.tif',{}),('big.tif',{'endian':'>'}),('defaults.tif',{'basic':True}),('shared-values.tif',{'shared':True})]:
        data,meta=make(**kwargs);save(name,data,meta)
    imagej_description='ImageJ=1.54r\nimages=12\nchannels=3\nslices=2\nframes=2\nhyperstack=true\nunit=\\u00B5m\nfinterval=0.125\ntunit=ms\nspacing=2.75\nloop=false\nmin=0.0\nmax=14.0\n'
    value,info=make(description=imagej_description);save('imagej-description.tif',value,info)
    for name,description in [
        ('loop-true',imagej_description.replace('loop=false','loop=true')),
        ('display-min-only',imagej_description.replace('max=14.0\n','')),
        ('display-max-only',imagej_description.replace('min=0.0\n','')),
        ('display-order',imagej_description.replace('min=0.0','min=15.0')),
        ('display-nonfinite',imagej_description.replace('max=14.0','max=NaN')),
        ('display-range',imagej_description.replace('max=14.0','max=65536')),
        ('unit-escape',imagej_description.replace('\\u00B5m','\\u006dm')),
        ('other-escape',imagej_description.replace('ImageJ=1.54r','ImageJ=1.54\\u0072'))]:
        value,info=make(description=description);save(name+'.tif',value,info,False,name)
    data,meta=make();order='<'
    def modified(name,change):
        value=bytearray(data);change(value);save(name,value,meta,False,name)
    def scalar_change(name,tag,value,page=0):
        slot=meta['fields'][page][tag];modified(name,lambda b:b.__setitem__(slice(slot+8,slot+10),struct.pack('<H',value)))
    for tag,value,name in [(258,8,'bits'),(259,5,'compression'),(262,0,'white'),(274,2,'orientation'),(277,3,'rgb'),(284,2,'planar'),(339,3,'float')]:scalar_change(name+'.tif',tag,value)
    modified('bigtiff.tif',lambda b:b.__setitem__(slice(2,4),struct.pack('<H',43)))
    modified('unknown-tag.tif',lambda b:b.__setitem__(slice(meta['fields'][0][339],meta['fields'][0][339]+2),struct.pack('<H',50839)))
    modified('ifd-loop.tif',lambda b:b.__setitem__(slice(len(b)-4,len(b)),struct.pack('<I',meta['ifds'][0])))
    modified('truncated-value.tif',lambda b:b.__setitem__(slice(meta['fields'][0][270]+8,meta['fields'][0][270]+12),struct.pack('<I',len(b)+12)))
    modified('unterminated.tif',lambda b:b.__setitem__(meta['description_offset']+meta['description_bytes']-1,65))
    modified('interior-nul.tif',lambda b:b.__setitem__(meta['description_offset']+2,0))
    value=bytearray(data);slot=meta['fields'][0][297]+10;value[slot:slot+2]=b'\0\0';save('zero-page-total.tif',value,meta,False,'explicit PageNumber total zero')
    modified('strip-byte-count.tif',lambda b:b.__setitem__(slice(meta['strip_counts_offset'],meta['strip_counts_offset']+4),struct.pack('<I',1)))
    offsets=struct.unpack_from('<I',data,meta['fields'][0][273]+8)[0]
    modified('overlapping-pixels.tif',lambda b:b.__setitem__(slice(offsets+4,offsets+8),b[offsets:offsets+4]))
    modified('pixels-in-metadata.tif',lambda b:b.__setitem__(slice(offsets,offsets+4),struct.pack('<I',meta['ifds'][0])))
    modified('heterogeneous.tif',lambda b:b.__setitem__(slice(meta['fields'][1][256]+8,meta['fields'][1][256]+12),struct.pack('<I',6)))
    for name,kwargs in [('unknown-key.tif',{'description':'ImageJ=1.54r\nunknown=1\n'}),('duplicate-key.tif',{'description':'ImageJ=1.54r\nchannels=3\nchannels=3\n'}),('dimensions.tif',{'description':'ImageJ=1.54r\nchannels=3\nslices=2\nframes=3\nhyperstack=true\nfinterval=1\n'}),('hidden-images.tif',{'basic':True,'description':'ImageJ=1.54r\nimages=2\n'}),('later-text.tif',{'later_description':'ImageJ=1.54s\n'}),('partial-calibration.tif',{'partial':True}),('conflicting-calibration.tif',{'conflict':True}),('typed-overlap.tif',{'typed_overlap':True})]:
        value,info=make(**kwargs);save(name,value,info,False,name)
    # The expected values use the mathematical page/voxel equation, not the
    # native writer or the native table ordering.
    manifest=dict(schema_version=1,origin='Original independent Python struct carriers; no ImageJ/Fiji code incorporated.',pixel_equation='10000*t+1000*c+100*z+y*X+x',file_page_order='((t*Z)+z)*C+c',files=records)
    (DEST/'SOURCE.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print({'fixtures':len(records)})
if __name__=='__main__':generate()
