"""Independent stdlib TIFF carriers: pixels precede metadata/IFDs; C-fast oracle."""
from pathlib import Path
import struct,json,hashlib
ROOT=Path(__file__).resolve().parents[1]
DEST=ROOT/'tests/fixtures/scientific-tiff-float32'

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
        pixels.append(values);offsets.append(block(pack('I'*len(values),*values)))
    desc=block(description.encode('ascii')+b'\0')
    other_desc=block(later_description.encode('ascii')+b'\0') if later_description else None
    strips=(y+rows-1)//rows;counts=[4*x*min(rows,y-i*rows) for i in range(strips)]
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
        short(258,32)
        if not basic:short(259,1)
        short(262,1)
        if not basic:field(269,2,4,optional[269])
        if page==0 or other_desc is not None:
            d=description if page==0 else later_description
            field(270,2,len(d)+1,pack('I',desc if page==0 else other_desc),True)
        strip_offsets=[offsets[page]+i*rows*x*4 for i in range(strips)]
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
            short(317,1);short(339,3)
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
    DEST.mkdir(parents=True,exist_ok=True)
    shape=(2,2,3,5,7)
    groups={
      'finite':[0,0x80000000,1,0x007fffff,0x00800000,0x3f800000,0xbf800000,0x7f7fffff,0xff7fffff],
      'quiet':[0,0x80000000,0x3f800000,0x7fc00000,0xffc12345,0x7fc00001],
      'ieee':[0,0x80000000,1,0x3f800000,0x7f800000,0xff800000,0x7f800001,0xff812345,0x7fc00001]}
    description='ImageJ=1.54r\nimages=12\nchannels=2\nslices=3\nframes=2\nhyperstack=true\nunit=um\nspacing=2.75\nfinterval=0.125\ntunit=ms\n'
    records=[]
    for group,words in groups.items():
      for endian in ['<','>']:
        def sample(t,c,z,y,x):return words[(t*13+c*11+z*7+y*7+x)%len(words)]
        for rows in [2,5]:
          raw,meta=make(endian=endian,shape=shape,rows=rows,sample=sample,description=description)
          filename=group+('-le' if endian=='<' else '-be')+'-r'+str(rows)+'.tif'
          (DEST/filename).write_bytes(raw)
          expected=[sample(t,c,z,y,x) for t in range(2) for c in range(2) for z in range(3) for y in range(5) for x in range(7)]
          records.append(dict(file=filename,valid=True,group=group,shape=meta['shape'],words_hex=struct.pack('<'+'I'*len(expected),*expected).hex(),sha256=hashlib.sha256(raw).hexdigest()))
    for label,pair in [('negative-range',('-1.0','7.0')),('infinite-range',('-Infinity','Infinity')),('nan-range',('NaN','NaN'))]:
      raw,meta=make(shape=shape,rows=2,sample=lambda *args:0x7f800001,description=description+'min='+pair[0]+'\nmax='+pair[1]+'\n')
      filename=label+'.tif';(DEST/filename).write_bytes(raw)
      records.append(dict(file=filename,valid=True,group='ieee',shape=meta['shape'],words_hex=struct.pack('<I',0x7f800001).hex()*420,sha256=hashlib.sha256(raw).hexdigest()))
    raw,meta=make(shape=shape,rows=2,sample=lambda *args:0,description=description)
    for name,tag,value,page in [('signed-integer',339,2,0),('missing-float-format',339,1,0),('mixed-type',258,16,1),('float64',258,64,0),('compressed',259,5,0),('rgb',277,3,0)]:
      data=bytearray(raw);slot=meta['fields'][page][tag];struct.pack_into('<H',data,slot+8,value)
      filename=name+'.tif';(DEST/filename).write_bytes(data);records.append(dict(file=filename,valid=False,sha256=hashlib.sha256(data).hexdigest()))
    (DEST/'SOURCE.json').write_text(json.dumps(dict(generator='scripts/generate_float32_tiff_fixtures.py',shape=list(shape),files=records),indent=2)+'\n')
if __name__=='__main__':generate()
