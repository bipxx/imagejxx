"""Original struct fixtures for exact TU01/TU02/TU03 literals; no product imports."""
from pathlib import Path
import hashlib
import json
import struct
from generate_scientific_tiff_fixtures import make

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / 'tests/fixtures/scientific-tiff-acceptance'
LITERAL = [0, 1, 255, 256, 32768, 65535]


def wire(raw):
    """Independently traverse actual IFD/strip intervals, including MM input."""
    endian = '<' if raw[:2] == b'II' else '>'
    def take(fmt, at):
        return struct.unpack_from(endian + fmt, raw, at)
    assert take('H', 2) == (42,)
    offset = take('I', 4)[0]
    pages, seen = [], set()
    while offset:
        assert offset not in seen
        seen.add(offset)
        count = take('H', offset)[0]
        tags = {}
        for i in range(count):
            at = offset + 2 + 12*i
            tag, kind, n, value = take('HHII', at)
            size = n * {2: 1, 3: 2, 4: 4, 5: 8}[kind]
            start = at+8 if size <= 4 else value
            assert start+size <= len(raw) and tag not in tags
            tags[tag] = (raw[start:start+size-1].decode('ascii') if kind == 2 else
                         take(('H' if kind == 3 else 'I') * (n*(2 if kind == 5 else 1)), start))
        x, y = (tags[k][0] for k in (256, 257))
        rows = tags.get(278, (y,))[0]
        assert len(tags[273]) == len(tags[279]) == (y+rows-1)//rows
        pixels = []
        for strip, (start, size) in enumerate(zip(tags[273], tags[279])):
            assert size == 2*x*min(rows, y-strip*rows)
            assert start+size <= len(raw)
            pixels.extend(take('H'*(size//2), start))
        pages.append(dict(tags=tags, pixels=pixels))
        offset = take('I', offset+2+12*count)[0]
    return pages


def generate():
    DEST.mkdir(parents=True, exist_ok=True)
    records = []
    for name, endian, shape, rows, literal in [
        ('literal-ii', '<', (1,1,1,2,3), 2, True),
        ('literal-mm', '>', (1,1,1,2,3), 2, True),
        ('czt-exact', '<', (2,3,2,2,3), 2, False),
        ('rows-1', '<', (1,1,1,3,5), 1, False),
        ('rows-2', '<', (1,1,1,3,5), 2, False),
        ('rows-9', '<', (1,1,1,3,5), 9, False),
    ]:
        t,c,z,y,x = shape
        desc = (f'ImageJ=1.54r\nchannels={c}\nslices={z}\nframes={t}\n'
                'hyperstack=true\nunit=um\nspacing=2.5\nfinterval=2.5\ntunit=ms\n')
        sample = (lambda t,c,z,y,x: LITERAL[3*y+x]) if literal else (lambda t,c,z,y,x: 10000*t+1000*c+100*z+10*y+x)
        raw, unused = make(endian, desc, shape=shape, rows=rows, sample=sample)
        # Change the original builder's Y3 Z spacing only through description;
        # X/Y rationals remain independent literal (2,1) and (4,5).
        pages = wire(raw)
        assert len(pages) == t*c*z
        for p, page in enumerate(pages):
            tt,zz,cc = p//(c*z), (p//c)%z, p%c
            expected = LITERAL if literal else [10000*tt+1000*cc+100*zz+10*yy+xx for yy in range(y) for xx in range(x)]
            assert page['pixels'] == expected
            assert page['tags'][282] == (2,1) and page['tags'][283] == (4,5)
        if name == 'rows-2':
            assert pages[0]['tags'][279] == (20,10)
        path = DEST/(name+'.tif')
        path.write_bytes(raw)
        records.append(dict(file=path.name, bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest(),
                            shape=[*shape,1], rows_per_strip=rows, pages=len(pages),
                            strip_byte_counts=list(pages[0]['tags'][279])))
    manifest = dict(schema_version=1, origin='Original struct carriers and independent wire checks; no product writer',
                    tu01_literal=LITERAL, tu02_equation='10000*t+1000*c+100*z+10*y+x',
                    native_plane_to_file_page=[0,3,1,4,2,5,6,9,7,10,8,11], files=records)
    (DEST/'SOURCE.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print(json.dumps(dict(state='passed', independent_fixtures=len(records))))

if __name__ == '__main__':
    generate()
