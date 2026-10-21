"""Independent horizontally/floating predicted tiled TIFF streams.

Only Python's byte/struct operations build these carriers. Logical samples use
TCZYX order; file directories use ImageJ's C-fast TZC order.
"""
from pathlib import Path
import hashlib
import json
import struct
import zlib


ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / 'tests/fixtures/scientific-tiff-predicted-tiles'


def lzw_encode(raw, padding_bad=False):
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
    if bits:out.append(((value<<(8-bits))&255)|(1 if padding_bad else 0))
    elif padding_bad:out.append(1)
    return bytes(out)

def fixed9(codes):
    text=''.join(f'{code:09b}' for code in codes);text+='0'*((-len(text))%8)
    return int(text,2).to_bytes(len(text)//8,'big')

def literal(raw,row_bytes):
    out=bytearray()
    for row in range(0,len(raw),row_bytes):
        for at in range(row,min(row+row_bytes,len(raw)),128):
            chunk=raw[at:min(at+128,row+row_bytes,len(raw))];out.append(len(chunk)-1);out.extend(chunk)
    return bytes(out)


def carrier(dtype, shape, tile, endian, padding, pattern="finite", bad=None, compression=8, entropy=False):
    t, c, z, y, x = shape
    tw, th = tile
    size=4 if dtype=="f32" else 2 if dtype=="u16" else 1;components=3 if dtype=="rgb8" else 1;mask=(1<<(size*8))-1
    pack = lambda fmt, *values: struct.pack(endian + fmt, *values)
    out = bytearray((b'II' if endian == '<' else b'MM') + pack('HI', 42, 0))

    def block(data):
        if len(out) % 2:
            out.append(0)
        offset = len(out)
        out.extend(data)
        return offset

    finite=(0,0x80000000,0x3f800000,0xbf800000,1,0x80000001,0x007fffff,0x7f7fffff,0xff7fffff)
    quiet=finite+(0x7fc01234,0xffc01234)
    ieee=quiet+(0x7f800000,0xff800000,0x7f800001,0xff800001)
    def sample(tt,cc,zz,yy,xx,ss):
        index=tt*31+cc*17+zz*7+yy*5+xx+ss*13
        if entropy:
            v=((((tt*c+cc)*z+zz)*y+yy)*x+xx)*components+ss+1
            v=(v*2654435761)&0xffffffff;v^=v>>16;v=(v*2246822519)&0xffffffff;v^=v>>13
            if dtype!='f32':return v&mask
            if pattern=='quiet' and v%11==0:return 0x7fc00000|(v&0x803fffff)
            if pattern=='ieee' and v%17==0:return (0x7f800000,0xff800000,0x7f800001,0xff800001)[v%4]
            return (v&0x807fffff)|(((v>>24)%254)<<23)
        if dtype=='f32':
            pool=ieee if pattern=='ieee' else quiet if pattern=='quiet' else finite
            return pool[index%len(pool)]
        return (7919*tt+4051*cc+2039*zz+257*yy+131*xx)&65535 if dtype=="u16" else (113*tt+71*cc+43*zz+17*yy+7*xx+89*ss)&255
    expected=b''.join(sample(tt,cc,zz,yy,xx,ss).to_bytes(size,'little') for tt in range(t) for cc in range(c) for zz in range(z) for yy in range(y) for xx in range(x) for ss in range(components))
    description = (f'ImageJ=1.54r\nimages={t*c*z}\nchannels={c}\nslices={z}\n'
                   f'frames={t}\nhyperstack=true\nunit=um\nspacing=2.75\n'
                   'finterval=0.125\ntunit=ms\n').encode() + b'\0'
    desc = block(description)
    rx, ry = block(pack('II', 2, 1)), block(pack('II', 4, 5))
    directories, payloads = [], []
    for tt in range(t):
        for zz in range(z):
            for cc in range(c):
                offsets, counts = [], []
                for top in range(0, y, th):
                    for left in range(0, x, tw):
                        raw=b''.join((sample(tt,cc,zz,top+dy,left+dx,ss) if top+dy<y and left+dx<x else padding).to_bytes(size,'little' if endian=='<' else 'big') for dy in range(th) for dx in range(tw) for ss in range(components))
                        predicted=bytearray()
                        row_bytes=tw*size*components
                        for at in range(0,len(raw),row_bytes):
                            words=[int.from_bytes(raw[i:i+size],'little' if endian=='<' else 'big') for i in range(at,at+row_bytes,size)]
                            if dtype=='f32':
                                shuffled=[(word>>shift)&255 for shift in [24,16,8,0] for word in words]
                                predicted.extend((v-(shuffled[i-1] if i else 0))&255 for i,v in enumerate(shuffled))
                            else:
                                predicted.extend(b''.join(((v-(words[i-components] if i>=components else 0))&mask).to_bytes(size,'little' if endian=='<' else 'big') for i,v in enumerate(words)))
                        raw=bytes(predicted)
                        encode=lambda data: zlib.compress(data) if compression in [8,32946] else lzw_encode(data)
                        compressed=encode(raw)
                        if not payloads:
                            if bad=='truncated-codec':compressed=compressed[:-3]
                            elif bad=='trailing-codec':compressed+=b'\0\0' if compression==32773 else b'\0'
                            elif bad=='over-expansion':compressed=encode(raw+b'x')
                            elif bad=='under-expansion':compressed=encode(raw[:-1])
                            elif bad=='empty-codec':compressed=b''
                            elif bad=='corrupt-deflate':compressed=bytes([compressed[0]^255])+compressed[1:]
                            elif bad=='concatenated-deflate':compressed+=zlib.compress(b'')
                            elif bad=='missing-repeat':compressed=b'\xff'
                            elif bad=='only-noops':compressed=b'\x80'*16
                            elif bad=='truncated-literal':compressed=b'\x7f'+bytes(3)
                            elif bad=='missing-clear':compressed=fixed9([0,257])
                            elif bad=='invalid-dictionary':compressed=fixed9([256,258,257])
                            elif bad=='missing-eoi':compressed=fixed9([256,0])
                            elif bad=='nonzero-padding':compressed=lzw_encode(raw,padding_bad=True)
                        raw=compressed
                        offsets.append(block(raw))
                        counts.append(len(raw))
                        payloads.append([offsets[-1], len(raw)])
                entries = {}

                def short(tag, value):
                    entries[tag] = [3, 1, pack('H', value).ljust(4, b'\0')]

                def long(tag, value):
                    entries[tag] = [4, 1, pack('I', value)]

                for tag, value in [(256, x), (257, y), (322, tw), (323, th)]:
                    long(tag, value)
                for tag, value in [(259, compression), (262, 2 if components==3 else 1), (274, 1),
                                   (277, components), (284, 1), (296, 1)]:
                    short(tag, value)
                short(317,3 if dtype=='f32' else 2)
                if components==3:
                    entries[258]=[3,3,pack('I',block(pack('HHH',8,8,8)))]
                    entries[339]=[3,3,pack('I',block(pack('HHH',1,1,1)))]
                else:
                    short(258,size*8);short(339,3 if dtype=='f32' else 1)
                if not directories:
                    entries[270] = [2, len(description), pack('I', desc)]
                for tag, values in [(324, offsets), (325, counts)]:
                    entries[tag] = [4, len(values), pack('I', values[0] if len(values) == 1
                                                       else block(pack('I'*len(values), *values)))]
                entries[282], entries[283] = [5, 1, pack('I', rx)], [5, 1, pack('I', ry)]
                directories.append(entries)
    first = directories[0]
    if bad in ['missing-322','missing-323','missing-324','missing-325']:
        del first[int(bad.split('-')[1])]
    elif bad == 'zero-width':
        first[322][2] = pack('I', 0)
    elif bad == 'nonmultiple-length':
        first[323][2] = pack('I', 15)
    elif bad == 'huge-width':
        first[322][2] = pack('I', 0xfffffff0)
    elif bad == 'table-count':
        first[324][1] -= 1
    elif bad in ['short-tile', 'long-tile']:
        count = first[325]
        value = payloads[0][1] + (-(3 if compression==32773 else 1) if bad == 'short-tile' else 1)
        if count[1] == 1:
            count[2] = pack('I', value)
        else:
            at = struct.unpack(endian+'I', count[2])[0]
            out[at:at+4] = pack('I', value)
    elif bad in ['overlap-tiles', 'metadata-payload', 'outside-payload']:
        at = struct.unpack(endian+'I', first[324][2])[0]
        value = (payloads[1][0] if bad == 'overlap-tiles' else desc
                 if bad == 'metadata-payload' else 0xfffffff0)
        out[at:at+4] = pack('I', value)
    elif bad == 'mixed-strips':
        first[273] = [4, 1, pack('I', payloads[0][0])]
        first[278] = [4, 1, pack('I', y)]
        first[279] = [4, 1, pack('I', x*y*size*components)]
    elif bad == 'mixed-compression':
        directories[1][259][2]=pack('H',1).ljust(4,b'\0')
    elif bad == 'wrong-prediction':
        first[317]=[3,1,pack('H',2 if dtype=='f32' else 3).ljust(4,b'\0')]
    elif bad == 'mixed-prediction':
        directories[1][317]=[3,1,pack('H',1).ljust(4,b'\0')]
    elif bad == 'unknown-prediction':
        first[317]=[3,1,pack('H',4).ljust(4,b'\0')]
    elif bad == 'mixed-geometry':
        directories[1][322][2] = pack('I', tw*2)
    if len(out) % 2:
        out.append(0)
    ifds = []
    for entries in directories:
        ifds.append(len(out))
        out.extend(bytes(6 + 12*len(entries)))
    out[4:8] = pack('I', ifds[0])
    for index, (offset, entries) in enumerate(zip(ifds, directories)):
        out[offset:offset+2] = pack('H', len(entries))
        for i, (tag, (kind, count, value)) in enumerate(sorted(entries.items())):
            out[offset+2+12*i:offset+14+12*i] = pack('HHI', tag, kind, count)+value
        nxt = ifds[index+1] if index+1 < len(ifds) else ifds[0] if bad == 'ifd-loop' else 0
        out[offset+2+12*len(entries):offset+6+12*len(entries)] = pack('I', nxt)
    if bad == 'truncated-ifd':
        del out[-3:]
    return bytes(out), dict(shape=[t,c,z,y,x,components], tile=[tw,th], padding=padding,
                            endian='little' if endian=='<' else 'big', dtype=dtype,pattern=pattern,compression=compression,predictor=3 if dtype=="f32" else 2,entropy=entropy,
                            hex=expected.hex(), payloads=payloads, spacing=[.5,1.25,2.75],
                            unit='um', frame_interval=.125, time_unit='ms',sample_policy='preserve_ieee_float32' if pattern=='ieee' else 'preserve_quiet_nan_float32' if pattern=='quiet' else 'finite_only')


def generate():
    DEST.mkdir(parents=True, exist_ok=True)
    records = []

    def save(name, data, meta, valid, defect=None):
        (DEST/name).write_bytes(data)
        records.append(dict(file=name, valid=valid, defect=defect, **meta,
                            bytes=len(data), sha256=hashlib.sha256(data).hexdigest()))

    geometries = [('one',(1,1,1,1,1),(16,16)), ('exact',(1,1,1,16,16),(16,16)),
                  ('edges',(2,2,2,17,35),(16,16)), ('wide',(1,2,3,19,65),(32,16)),
                  ('tall',(2,1,2,65,19),(16,32)), ('nonsquare',(1,2,2,33,47),(32,16)), ('entropy',(1,1,1,65,67),(64,64))]
    for dtype in ['u8','u16','rgb8','f32']:
        for name,shape,tile in geometries:
            for endian,order in [('<','le'),('>','be')]:
                for pattern in (['finite','quiet','ieee'] if dtype=='f32' else ['finite']):
                    for padding,label in [(0,'zero'),(0x7f800001 if dtype=='f32' else 0xa5,'poison')]:
                        for compression in [8,32946,5]:
                            data,meta=carrier(dtype,shape,tile,endian,padding,pattern,compression=compression,entropy=name=='entropy')
                            save(f'{dtype}-{name}-{order}-{pattern}-{label}-c{compression}.tif',data,meta,True)
        structural=['missing-322','missing-323','missing-324','missing-325','zero-width','nonmultiple-length','huge-width','table-count','short-tile','long-tile','overlap-tiles','metadata-payload','outside-payload','mixed-strips','mixed-geometry','ifd-loop','truncated-ifd','mixed-compression','wrong-prediction','mixed-prediction','unknown-prediction']
        common=['truncated-codec','trailing-codec','over-expansion','under-expansion','empty-codec']
        for compression in [8,32946,5]:
            defects=structural+common+(['missing-repeat','only-noops','truncated-literal'] if compression==32773 else ['missing-clear','invalid-dictionary','missing-eoi','nonzero-padding'] if compression==5 else ['corrupt-deflate','concatenated-deflate'])
            for defect in defects:
                data,meta=carrier(dtype,(2,2,2,17,35),(16,16),'<',0x7f800001 if dtype=='f32' else 0xa5,'finite',defect,compression)
                meta['refusal_phase']='decode' if defect in common+['short-tile','long-tile','missing-repeat','only-noops','truncated-literal','missing-clear','invalid-dictionary','missing-eoi','nonzero-padding','corrupt-deflate','concatenated-deflate'] and defect!='empty-codec' else 'inspect'
                if defect=='long-tile' and meta['payloads'][0][0]+meta['payloads'][0][1]+1>meta['payloads'][1][0]:meta['refusal_phase']='inspect'
                save(dtype+'-'+defect+'-c'+str(compression)+'.tif',data,meta,False,defect)
    generator = Path(__file__)
    manifest = dict(generator=generator.relative_to(ROOT).as_posix(),
                    generator_sha256=hashlib.sha256(generator.read_bytes()).hexdigest(),
                    scope='Classic predicted Deflate/LZW tiles; numerical row differences and MSB byte-plane differences; entropy/reset controls; raw IEEE words and poisoned edge padding; logical TCZYXS, C-fast file pages',
                    files=records)
    (DEST/'SOURCE.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')


if __name__ == '__main__':
    generate()
