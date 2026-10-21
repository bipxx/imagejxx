"""Original struct carriers; numerical row differences precede file byte packing."""
from pathlib import Path
import hashlib,importlib.util,json,struct,zlib
ROOT=Path(__file__).resolve().parents[1];DEST=ROOT/'tests/fixtures/scientific-tiff-horizontal'
spec=importlib.util.spec_from_file_location('independent_lzw',ROOT/'scripts/generate_lzw_tiff_fixtures.py')
lzw=importlib.util.module_from_spec(spec);spec.loader.exec_module(lzw)
def make(dtype,endian,rows,codec,*,shape=(2,2,2,5,7),pattern='wrap',bad=None):
    t,c,z,y,x=shape;s=3 if dtype=='rgb8' else 1;size=2 if dtype=='u16' else 1;mask=(1<<(size*8))-1
    def sample(tt,cc,zz,yy,xx,ss):
        base=113*tt+71*cc+43*zz+17*yy+89*ss if size==1 else 31111*tt+17771*cc+6997*zz+257*yy+8227*ss
        pool=[0,mask,1,mask-1,mask//2+1,mask//2,3]
        return (base+(pool[xx%7] if pattern=='wrap' else xx*(7919 if size==2 else 37) if pattern=='ramp' else 0))&mask
    expected=b''.join(sample(tt,cc,zz,yy,xx,ss).to_bytes(size,'little') for tt in range(t) for cc in range(c) for zz in range(z) for yy in range(y) for xx in range(x) for ss in range(s))
    pack=lambda fmt,*v:struct.pack(endian+fmt,*v)
    out=bytearray(b'II' if endian=='<' else b'MM');out+=pack('HI',42,0)
    def block(data):
        if len(out)%2:out.append(0)
        at=len(out);out.extend(data);return at
    desc=(f'ImageJ=1.54r\nimages={t*c*z}\nchannels={c}\nslices={z}\nframes={t}\nhyperstack=true\nunit=um\nspacing=2.75\nfinterval=0.125\ntunit=ms\n').encode()+b'\0'
    desc_at=block(desc);rx=block(pack('II',2,1));ry=block(pack('II',4,5))
    bits=block(pack('HHH',8,8,8)) if s==3 else None;formats=block(pack('HHH',1,1,1)) if s==3 else None
    directories=[]
    for tt in range(t):
        for zz in range(z):
            for cc in range(c):
                offsets=[];counts=[]
                for first in range(0,y,rows):
                    values=[]
                    for yy in range(first,min(y,first+rows)):
                        for xx in range(x):
                            for ss in range(s):
                                v=sample(tt,cc,zz,yy,xx,ss)
                                previous=sample(tt,cc,zz,yy,xx-1,ss) if xx else 0
                                values.append((v-previous)&mask)
                    raw=b''.join(v.to_bytes(size,'little' if endian=='<' else 'big') for v in values)
                    encoded=lzw.encode(raw) if codec==5 else zlib.compress(raw);offsets.append(block(encoded));counts.append(len(encoded))
                entries=[]
                def field(tag,kind,n,value):entries.append((tag,kind,n,value))
                def short(tag,v):field(tag,3,1,pack('H',v).ljust(4,b'\0'))
                def long(tag,v):field(tag,4,1,pack('I',v))
                long(256,x);long(257,y)
                if s==3:field(258,3,3,pack('I',bits))
                else:short(258,size*8)
                short(259,codec);short(262,2 if s==3 else 1)
                if not directories:field(270,2,len(desc),pack('I',desc_at))
                for tag,values in [(273,offsets),(279,counts)]:field(tag,4,len(values),pack('I',values[0] if len(values)==1 else block(pack('I'*len(values),*values))))
                short(274,1);short(277,s);long(278,rows);field(282,5,1,pack('I',rx));field(283,5,1,pack('I',ry));short(284,1);short(296,1)
                short(317,1 if bad=='mixed' and directories else 3 if bad=='floating' else 4 if bad=='unknown' else 2)
                if s==3:field(339,3,3,pack('I',formats))
                else:short(339,3 if bad=='float-type' else 1)
                directories.append(sorted(entries))
    if len(out)%2:out.append(0)
    ifds=[]
    for entries in directories:ifds.append(len(out));out.extend(bytes(6+12*len(entries)))
    out[4:8]=pack('I',ifds[0])
    for i,(at,entries) in enumerate(zip(ifds,directories)):
        out[at:at+2]=pack('H',len(entries))
        for j,(tag,kind,n,value) in enumerate(entries):out[at+2+12*j:at+14+12*j]=pack('HHI',tag,kind,n)+value
        out[at+2+12*len(entries):at+6+12*len(entries)]=pack('I',ifds[i+1] if i+1<len(ifds) else 0)
    return bytes(out),dict(shape=[t,c,z,y,x,s],hex=expected.hex(),dtype=dtype,codec=codec,rows=rows,pattern=pattern)
def generate():
    DEST.mkdir(parents=True,exist_ok=True);records=[]
    def save(name,data,meta,valid=True):
        (DEST/name).write_bytes(data);records.append(dict(file=name,valid=valid,**meta,bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
    for dtype in ['u8','u16','rgb8']:
        for endian in ['<','>']:
            for codec in [8,32946,5]:
                for rows in [1,2,5]:
                    data,meta=make(dtype,endian,rows,codec);save(f'{dtype}-{("le" if endian=="<" else "be")}-r{rows}-c{codec}.tif',data,meta)
            for codec in [8,5]:
                data,meta=make(dtype,endian,64,codec,shape=(1,2,2,130,257),pattern='ramp');save(f'tall-{dtype}-{("le" if endian=="<" else "be")}-c{codec}.tif',data,meta)
                data,meta=make(dtype,endian,2,codec,shape=(1,1,1,5,1));save(f'one-{dtype}-{("le" if endian=="<" else "be")}-c{codec}.tif',data,meta)
        for codec in [8,5]:
            data,meta=make(dtype,'<',3,codec,shape=(1,1,1,3,129),pattern='constant');save(f'constant-{dtype}-c{codec}.tif',data,meta)
    for bad in ['mixed','floating','unknown','float-type']:
        data,meta=make('u16','<',2,8,bad=bad);save(bad+'.tif',data,meta,False)
    # Unsupported predictor/codec pairings are structural refusals.
    for codec in [1,32773]:
        data,meta=make('u16','<',2,codec);save(f'codec-{codec}.tif',data,meta,False)
    (DEST/'SOURCE.json').write_text(json.dumps(dict(generator='scripts/generate_horizontal_tiff_fixtures.py',lzw_generator_sha256=hashlib.sha256((ROOT/'scripts/generate_lzw_tiff_fixtures.py').read_bytes()).hexdigest(),files=records),indent=2)+'\n')
if __name__=='__main__':generate()
