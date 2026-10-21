"""Original struct/LZW carriers; no native writer or third-party TIFF code."""
from pathlib import Path
import hashlib,json,struct,random
ROOT=Path(__file__).resolve().parents[1]
DEST=ROOT/'tests/fixtures/scientific-tiff-lzw'
WORDS=[0,0x80000000,0x3f800000,0xbf800000,0x00000001,0x007fffff,
       0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc00001,
       0xffc12345,0x7f800001,0xff800001,0x7fffffff,0xffffffff]

def encode(raw):
    # Independent full-string dictionary. Widths follow a separate receiver
    # clock rather than the native encoder's insertion counter.
    table={bytes([i]):i for i in range(256)};next_code=258
    value=0;bits=0;out=bytearray();width=9;received=258;previous=False
    def put(code):
        nonlocal value,bits,width,received,previous
        value=(value<<width)|code;bits+=width
        while bits>=8:
            bits-=8;out.append((value>>bits)&255)
        if code==256:width=9;received=258;previous=False
        elif code!=257:
            if previous:
                received+=1
                if width<12 and received==(1<<width)-1:width+=1
            previous=True
    put(256);prefix=b''
    for v in raw:
        byte=bytes([v]);candidate=prefix+byte
        if candidate in table:prefix=candidate;continue
        put(table[prefix]);table[candidate]=next_code;next_code+=1
        if next_code==4095:
            put(256);table={bytes([i]):i for i in range(256)};next_code=258
        prefix=byte
    if prefix:put(table[prefix])
    put(257)
    if bits:out.append((value<<(8-bits))&255)
    return bytes(out)

def fixed9(codes):
    text=''.join(f'{code:09b}' for code in codes);text+='0'*((-len(text))%8)
    return int(text,2).to_bytes(len(text)//8,'big')

def make(dtype,endian,rows,compression=5,*,bad=None,entropy=False,shape=None):
    t,c,z,y,x,s=2,2,2,130,7,3 if dtype=='rgb8' else 1
    if entropy:t,c,z,y,x=1,1,1,130,257
    if shape is not None:t,c,z,y,x=shape
    rng=random.Random(271828);noise=[rng.getrandbits(32) for _ in range(t*c*z*y*x*s)] if entropy else None
    size={'u8':1,'u16':2,'f32':4,'rgb8':1}[dtype]
    def sample(tt,cc,zz,yy,xx,ss):
        if noise is not None:
            v=noise[(((((tt*c+cc)*z+zz)*y+yy)*x+xx)*s+ss)];return v if dtype=='f32' else v%256 if size==1 else v%65536
        n=113*tt+71*cc+43*zz+17*yy+7*xx+89*ss
        return WORDS[n%len(WORDS)] if dtype=='f32' else n%256 if size==1 else (n*257)%65536
    expected=bytes(b for tt in range(t) for cc in range(c) for zz in range(z)
                   for yy in range(y) for xx in range(x) for ss in range(s)
                   for b in sample(tt,cc,zz,yy,xx,ss).to_bytes(size,'little'))
    pack=lambda fmt,*v:struct.pack(endian+fmt,*v)
    out=bytearray(b'II' if endian=='<' else b'MM');out+=pack('HI',42,0)
    def block(data):
        if len(out)%2:out.append(0)
        at=len(out);out.extend(data);return at
    description=(f'ImageJ=1.54r\nimages={t*c*z}\nchannels={c}\nslices={z}\nframes={t}\nhyperstack=true\nunit=um\nspacing=2.75\nfinterval=0.125\ntunit=ms\n').encode()+b'\0'
    desc=block(description);rx=block(pack('II',2,1));ry=block(pack('II',4,5))
    bits=block(pack('HHH',8,8,8)) if s==3 else None
    formats=block(pack('HHH',1,1,1)) if s==3 else None
    entries_by_page=[];encoded_ranges=[]
    for tt in range(t):
        for zz in range(z):
            for cc in range(c):
                offsets=[];counts=[]
                for first in range(0,y,rows):
                    raw=b''.join(sample(tt,cc,zz,yy,xx,ss).to_bytes(size,'little' if endian=='<' else 'big')
                                 for yy in range(first,min(y,first+rows)) for xx in range(x) for ss in range(s))
                    encoded=encode(raw)
                    if not encoded_ranges:
                        if bad=='truncated':encoded=encoded[:-1]
                        elif bad=='trailing':encoded+=b'\0'
                        elif bad=='missing-clear':encoded=fixed9([0,257])
                        elif bad=='invalid-code':encoded=fixed9([256,258,257])
                        elif bad=='expansion':encoded=encode(raw+b'x')
                        elif bad=='short-output':encoded=encode(raw[:-1])
                    at=block(encoded);offsets.append(at);counts.append(len(encoded));encoded_ranges.append([at,len(encoded)])
                entries=[]
                def field(tag,kind,count,value):entries.append((tag,kind,count,value))
                def short(tag,n):field(tag,3,1,pack('H',n).ljust(4,b'\0'))
                def long(tag,n):field(tag,4,1,pack('I',n))
                long(256,x);long(257,y)
                if s==3:field(258,3,3,pack('I',bits))
                else:short(258,size*8)
                short(259,compression if bad!='mixed-codec' or not entries_by_page else 8)
                short(262,2 if s==3 else 1)
                if not entries_by_page:field(270,2,len(description),pack('I',desc))
                for tag,values in [(273,offsets),(279,counts)]:
                    field(tag,4,len(values),pack('I',values[0] if len(values)==1 else block(pack('I'*len(values),*values))))
                short(274,1);short(277,s);long(278,rows)
                field(282,5,1,pack('I',rx));field(283,5,1,pack('I',ry))
                short(284,1);short(296,1);short(317,2 if bad=='predictor' else 1)
                if s==3:field(339,3,3,pack('I',formats))
                else:short(339,3 if dtype=='f32' else 1)
                entries_by_page.append(sorted(entries))
    if len(out)%2:out.append(0)
    ifds=[]
    for entries in entries_by_page:ifds.append(len(out));out.extend(bytes(6+12*len(entries)))
    out[4:8]=pack('I',ifds[0])
    for index,(at,entries) in enumerate(zip(ifds,entries_by_page)):
        out[at:at+2]=pack('H',len(entries))
        for i,(tag,kind,count,value) in enumerate(entries):out[at+2+12*i:at+14+12*i]=pack('HHI',tag,kind,count)+value
        out[at+2+12*len(entries):at+6+12*len(entries)]=pack('I',ifds[index+1] if index+1<len(ifds) else 0)
    return bytes(out),dict(shape=[t,c,z,y,x,s],hex=expected.hex(),strips=encoded_ranges,dtype=dtype,compression=compression,rows=rows)

def generate():
    DEST.mkdir(parents=True,exist_ok=True);records=[]
    for dtype in ['u8','u16','f32','rgb8']:
        for endian in ['<','>']:
            for rows in [1,64,130]:
                for codec in [5]:
                    data,meta=make(dtype,endian,rows,codec)
                    name=f'{dtype}-{("le" if endian=="<" else "be")}-r{rows}-c{codec}.tif'
                    (DEST/name).write_bytes(data);records.append(dict(file=name,valid=True,**meta,sha256=hashlib.sha256(data).hexdigest()))
    for dtype in ['u8','u16','f32','rgb8']:
        for endian in ['<','>']:
            for rows in [64,130]:
                data,meta=make(dtype,endian,rows,entropy=True);meta['entropy']=True
                name=f'entropy-{dtype}-{("le" if endian=="<" else "be")}-r{rows}-c5.tif'
                (DEST/name).write_bytes(data);records.append(dict(file=name,valid=True,**meta,sha256=hashlib.sha256(data).hexdigest()))
    for width in [253,254,255,256]:
        data,meta=make('u8','<',1,shape=(1,1,1,1,width));meta['boundary']=True
        name=f'edge-u8-x{width}-c5.tif';(DEST/name).write_bytes(data);records.append(dict(file=name,valid=True,**meta,sha256=hashlib.sha256(data).hexdigest()))
    for bad in ['truncated','trailing','missing-clear','invalid-code','expansion','short-output','mixed-codec','predictor']:
        data,meta=make('u16','<',64,bad=bad);name=bad+'.tif';(DEST/name).write_bytes(data)
        records.append(dict(file=name,valid=False,descriptor_valid=bad not in ['mixed-codec','predictor'],**meta,sha256=hashlib.sha256(data).hexdigest()))
    (DEST/'SOURCE.json').write_text(json.dumps(dict(generator='scripts/generate_lzw_tiff_fixtures.py',files=records),indent=2)+'\n')
if __name__=='__main__':generate()
