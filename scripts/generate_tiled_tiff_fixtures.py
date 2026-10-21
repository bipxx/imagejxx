"""Independent classic-TIFF tiles, edge padding and deliberately malformed IFDs.

Only Python's byte/struct operations build these carriers. Logical samples use
TCZYX order; file directories use ImageJ's C-fast TZC order.
"""
from pathlib import Path
import hashlib
import json
import struct

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / 'tests/fixtures/scientific-tiff-tiled'


def carrier(shape, tile, endian, padding, bad=None):
    t, c, z, y, x = shape
    tw, th = tile
    pack = lambda fmt, *values: struct.pack(endian + fmt, *values)
    out = bytearray((b'II' if endian == '<' else b'MM') + pack('HI', 42, 0))

    def block(data):
        if len(out) % 2:
            out.append(0)
        offset = len(out)
        out.extend(data)
        return offset

    def sample(tt, cc, zz, yy, xx):
        pool = (0, 65535, 1, 32768, 32767, 65534, 3)
        return (tt * 31111 + cc * 17771 + zz * 6997 + yy * 257 +
                pool[xx % len(pool)] + 7919 * (xx // len(pool))) & 65535

    expected = b''.join(sample(tt, cc, zz, yy, xx).to_bytes(2, 'little')
                        for tt in range(t) for cc in range(c) for zz in range(z)
                        for yy in range(y) for xx in range(x))
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
                        raw = b''.join(pack('H', sample(tt, cc, zz, top+dy, left+dx)
                                            if top+dy < y and left+dx < x else padding)
                                       for dy in range(th) for dx in range(tw))
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
                for tag, value in [(258, 16), (259, 1), (262, 1), (274, 1),
                                   (277, 1), (284, 1), (296, 1), (339, 1)]:
                    short(tag, value)
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
        value = tw*th*2 + (-2 if bad == 'short-tile' else 2)
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
        first[279] = [4, 1, pack('I', x*y*2)]
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
    return bytes(out), dict(shape=[t,c,z,y,x,1], tile=[tw,th], padding=padding,
                            endian='little' if endian=='<' else 'big', dtype='u16',
                            hex=expected.hex(), payloads=payloads, spacing=[.5,1.25,2.75],
                            unit='um', frame_interval=.125, time_unit='ms')


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
    for name, shape, tile in geometries:
        for endian, order in [('<','le'),('>','be')]:
            for padding, label in [(0,'zero'),(0xa5c3,'poison')]:
                data, meta = carrier(shape, tile, endian, padding)
                save(f'{name}-{order}-{label}.tif', data, meta, True)
    defects = ['missing-322','missing-323','missing-324','missing-325','zero-width',
               'nonmultiple-length','huge-width','table-count','short-tile','long-tile',
               'overlap-tiles','metadata-payload','outside-payload','mixed-strips',
               'mixed-geometry','ifd-loop','truncated-ifd']
    for defect in defects:
        data, meta = carrier((2,2,2,17,35),(16,16),'<',0xa5c3,defect)
        save(defect+'.tif', data, meta, False, defect)
    generator = Path(__file__)
    manifest = dict(generator=generator.relative_to(ROOT).as_posix(),
                    generator_sha256=hashlib.sha256(generator.read_bytes()).hexdigest(),
                    scope='Classic uncompressed scalar UInt16 tiles; logical TCZYX, C-fast file pages',
                    files=records)
    (DEST/'SOURCE.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')


if __name__ == '__main__':
    generate()
