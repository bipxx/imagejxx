"""Original struct/zlib carriers using pinned ImageJ's exact Float32 corpus."""
from pathlib import Path
import hashlib,json,struct
ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'tests/fixtures/float32-tiff-imagej154r/SOURCE.json'
DEST=ROOT/'tests/fixtures/packbits-float32-tiff-imagej154r'
def literal(raw,row_bytes):
    out=bytearray()
    for row in range(0,len(raw),row_bytes):
        for at in range(row,min(row+row_bytes,len(raw)),128):
            chunk=raw[at:min(at+128,row+row_bytes,len(raw))];out.append(len(chunk)-1);out.extend(chunk)
    return bytes(out)

def make(row):
    t,c,z,y,x,s=row['shape'];assert s==1
    endian='<' if '-le.' in row['file'] else '>'
    pack=lambda fmt,*v:struct.pack(endian+fmt,*v)
    out=bytearray(b'II' if endian=='<' else b'MM');out+=pack('HI',42,0)
    def block(raw):
        if len(out)%2:out.append(0)
        at=len(out);out.extend(raw);return at
    desc=(f'ImageJ=1.54r\nimages={t*c*z}\nchannels={c}\nslices={z}\nframes={t}\nhyperstack=true\nunit=um\nspacing=2.75\nfinterval=0.125\ntunit=ms\n').encode()+b'\0'
    desc_offset=block(desc);rx=block(pack('II',2,1));ry=block(pack('II',4,5))
    source=bytes.fromhex(row['source_hex']);plane=y*x*4;entries_by_page=[]
    for tt in range(t):
        for zz in range(z):
            for cc in range(c):
                raw=source[((tt*c+cc)*z+zz)*plane:((tt*c+cc)*z+zz+1)*plane]
                if endian=='>':raw=b''.join(raw[i:i+4][::-1] for i in range(0,len(raw),4))
                offsets=[];counts=[]
                for first in range(0,y,2):
                    encoded=b'\x80'+literal(raw[first*x*4:min(y,first+2)*x*4],x*4)+b'\x80';offsets.append(block(encoded));counts.append(len(encoded))
                entries=[]
                def field(tag,kind,count,value):entries.append((tag,kind,count,value))
                def short(tag,v):field(tag,3,1,pack('H',v).ljust(4,b'\0'))
                def long(tag,v):field(tag,4,1,pack('I',v))
                long(256,x);long(257,y);short(258,32);short(259,32773);short(262,1)
                if not entries_by_page:field(270,2,len(desc),pack('I',desc_offset))
                field(273,4,len(offsets),pack('I',block(pack('I'*len(offsets),*offsets))))
                short(274,1);short(277,1);long(278,2)
                field(279,4,len(counts),pack('I',block(pack('I'*len(counts),*counts))))
                field(282,5,1,pack('I',rx));field(283,5,1,pack('I',ry));short(284,1);short(296,1);short(317,1);short(339,3)
                entries_by_page.append(sorted(entries))
    if len(out)%2:out.append(0)
    ifds=[]
    for entries in entries_by_page:ifds.append(len(out));out.extend(bytes(6+12*len(entries)))
    out[4:8]=pack('I',ifds[0])
    for index,(at,entries) in enumerate(zip(ifds,entries_by_page)):
        out[at:at+2]=pack('H',len(entries))
        for i,(tag,kind,n,value) in enumerate(entries):out[at+2+12*i:at+14+12*i]=pack('HHI',tag,kind,n)+value
        out[at+2+12*len(entries):at+6+12*len(entries)]=pack('I',ifds[index+1] if index+1<len(ifds) else 0)
    return bytes(out)
def generate():
    DEST.mkdir(parents=True,exist_ok=True);source=json.loads(SOURCE.read_bytes());rows=[]
    for original in source['files']:
        data=make(original);(DEST/original['file']).write_bytes(data)
        rows.append(dict(file=original['file'],shape=original['shape'],source_hex=original['source_hex'],filtered_hex=original['filtered_hex'],spacing=[.5,1.25,2.75],bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
    (DEST/'SOURCE.json').write_text(json.dumps(dict(generator='scripts/generate_packbits_float_tiff_fixtures.py',source_manifest=dict(path=SOURCE.relative_to(ROOT).as_posix(),bytes=SOURCE.stat().st_size,sha256=hashlib.sha256(SOURCE.read_bytes()).hexdigest()),imagej_commit=source['imagej_commit'],scope='Original PackBits carriers; exact source/filter words inherited from actual pinned ImageJ corpus',files=rows),indent=2)+'\n')
if __name__=='__main__':generate()
