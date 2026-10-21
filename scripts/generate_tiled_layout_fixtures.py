"""Independent UInt8/RGB8/Float32 classic-TIFF tiles and raw IEEE edge padding.

Only Python's byte/struct operations build these carriers. Logical samples use
TCZYX order; file directories use ImageJ's C-fast TZC order.
"""
from pathlib import Path
import hashlib
import json
import struct

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / 'tests/fixtures/scientific-tiff-tiled-layouts'


def carrier(dtype, shape, tile, endian, padding, pattern="finite", bad=None):
    t, c, z, y, x = shape
    tw, th = tile
    size=4 if dtype=="f32" else 1;components=3 if dtype=="rgb8" else 1;mask=(1<<(size*8))-1
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
        if dtype=='f32':
            pool=ieee if pattern=='ieee' else quiet if pattern=='quiet' else finite
            return pool[index%len(pool)]
        return (113*tt+71*cc+43*zz+17*yy+7*xx+89*ss)&255
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
                for tag, value in [(259, 1), (262, 2 if components==3 else 1), (274, 1),
                                   (277, components), (284, 1), (296, 1)]:
                    short(tag, value)
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
    if bad and bad.startswith('missing-'):
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
        value = tw*th*size*components + (-1 if bad == 'short-tile' else 1)
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
                            endian='little' if endian=='<' else 'big', dtype=dtype,pattern=pattern,
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
                  ('tall',(2,1,2,65,19),(16,32)), ('nonsquare',(1,2,2,33,47),(32,16))]
    for dtype in ['u8','rgb8','f32']:
        for name,shape,tile in geometries:
            for endian,order in [('<','le'),('>','be')]:
                for pattern in (['finite','quiet','ieee'] if dtype=='f32' else ['finite']):
                    for padding,label in [(0,'zero'),(0x7f800001 if dtype=='f32' else 0xa5,'poison')]:
                        data,meta=carrier(dtype,shape,tile,endian,padding,pattern)
                        save(f'{dtype}-{name}-{order}-{pattern}-{label}.tif',data,meta,True)
        defects=['missing-322','missing-323','missing-324','missing-325','zero-width','nonmultiple-length','huge-width','table-count','short-tile','long-tile','overlap-tiles','metadata-payload','outside-payload','mixed-strips','mixed-geometry','ifd-loop','truncated-ifd']
        for defect in defects:
            data,meta=carrier(dtype,(2,2,2,17,35),(16,16),'<',0x7f800001 if dtype=='f32' else 0xa5,'finite',defect)
            save(dtype+'-'+defect+'.tif',data,meta,False,defect)
    generator = Path(__file__)
    manifest = dict(generator=generator.relative_to(ROOT).as_posix(),
                    generator_sha256=hashlib.sha256(generator.read_bytes()).hexdigest(),
                    scope='Classic uncompressed UInt8/RGB8/Float32 tiles; raw IEEE words and poisoned edge padding; logical TCZYXS, C-fast file pages',
                    files=records)
    (DEST/'SOURCE.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')


if __name__ == '__main__':
    generate()
