"""Original struct carriers; numerical row differences precede file byte packing."""
from pathlib import Path
import hashlib,importlib.util,json,struct,zlib
ROOT=Path(__file__).resolve().parents[1];DEST=ROOT/'tests/fixtures/scientific-tiff-floating'
spec=importlib.util.spec_from_file_location('independent_lzw',ROOT/'scripts/generate_lzw_tiff_fixtures.py')
lzw=importlib.util.module_from_spec(spec);spec.loader.exec_module(lzw)
def make(dtype,endian,rows,codec,*,shape=(2,2,2,5,7),pattern='finite',bad=None):
    t,c,z,y,x=shape;s=1;size=4
    pools={'finite':[0,0x80000000,0x3f800000,0xbf800000,1,0x80000001,0x007fffff,0x807fffff,0x7f7fffff,0xff7fffff,0x00800000,0x80800000],
           'quiet':[0,0x80000000,0x7fc00000,0xffc00001,0x7fffffff,0xffffffff,0x3f800000,0xbf800000,1,0x007fffff],
           'ieee':[0,0x80000000,0x7f800000,0xff800000,0x7f800001,0xff800001,0x7fbfffff,0xffbfffff,0x7fc00000,0xffc01234,1,0x80000001,0x007fffff,0x7f7fffff]}
    def sample(tt,cc,zz,yy,xx,ss):
        return pools[pattern][(tt*11+cc*7+zz*5+yy*3+xx)%len(pools[pattern])]
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
                    raw=bytearray()
                    for yy in range(first,min(y,first+rows)):
                        # The byte-plane stream is most-significant-byte first regardless of TIFF endian.
                        shuffled=bytes((sample(tt,cc,zz,yy,xx,0)>>shift)&255 for shift in [24,16,8,0] for xx in range(x))
                        raw.extend(bytes([(v-(shuffled[i-1] if i else 0))&255 for i,v in enumerate(shuffled)]))
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
                short(317,1 if bad=='mixed' and directories else 2 if bad=='integer-predictor' else 4 if bad=='unknown' else 3)
                if s==3:field(339,3,3,pack('I',formats))
                else:short(339,1 if bad=='integer-type' else 3)
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
    for pattern in ['finite','quiet','ieee']:
        for endian in ['<','>']:
            for codec in [8,32946,5]:
                for rows in [1,2,5]:
                    data,meta=make('f32',endian,rows,codec,pattern=pattern);save(f'{pattern}-{("le" if endian=="<" else "be")}-r{rows}-c{codec}.tif',data,meta)
            for codec in [8,5]:
                for label,shape,rows in [('tall',(1,2,2,130,257),64),('one',(1,1,1,5,1),2)]:
                    data,meta=make('f32',endian,rows,codec,shape=shape,pattern=pattern);save(f'{label}-{pattern}-{("le" if endian=="<" else "be")}-c{codec}.tif',data,meta)
    for bad in ['mixed','integer-predictor','unknown','integer-type']:
        data,meta=make('f32','<',2,8,pattern='finite',bad=bad);save(bad+'.tif',data,meta,False)
    for codec in [1,32773]:
        data,meta=make('f32','<',2,codec,pattern='finite');save(f'codec-{codec}.tif',data,meta,False)
    (DEST/'SOURCE.json').write_text(json.dumps(dict(generator='scripts/generate_floating_tiff_fixtures.py',lzw_generator_sha256=hashlib.sha256((ROOT/'scripts/generate_lzw_tiff_fixtures.py').read_bytes()).hexdigest(),files=records),indent=2)+'\n')
if __name__=='__main__':generate()
