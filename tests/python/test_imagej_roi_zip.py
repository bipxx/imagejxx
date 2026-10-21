"""Independent ZIP readers/writers, complete sets and adversarial containers."""
import io
import json
from pathlib import Path
import struct
import zipfile
import zlib
import pytest
import imagejxx as ij

ROOT=Path(__file__).parents[2]
def roi():
    return ij.encode_imagej_roi(ij.ImageJRoi(ij.ROI('region',ij.Selection('rectangle',[(1,2),(6,8)]))))
def entries():
    return [ij.ImageJRoiZipEntry('A.roi',roi()),ij.ImageJRoiZipEntry('a.roi',roi()),ij.ImageJRoiZipEntry('λ😀.roi',roi())]
def python_zip(pairs,method=zipfile.ZIP_DEFLATED,stream=False):
    class NonSeek(io.BytesIO):
        def seekable(self):return False
        def seek(self,*args):raise io.UnsupportedOperation('streaming')
    out=NonSeek() if stream else io.BytesIO()
    with zipfile.ZipFile(out,'w',compression=method) as z:
        for name,data in pairs:z.writestr(name,data)
    return out.getvalue()
def central(wire):return struct.unpack_from('<I',wire,len(wire)-6)[0]
def flat(decoded):return [(e.name,e.data) for e in decoded]

@pytest.mark.parametrize('compressed',[False,True])
def test_native_archive_read_by_independent_python(compressed):
    source=entries();wire=ij.encode_imagej_roi_zip(source,compress=compressed)
    with zipfile.ZipFile(io.BytesIO(wire)) as z:
        assert z.namelist()==[e.name for e in source]
        assert z.testzip() is None
        assert [(n,z.read(n)) for n in z.namelist()]==flat(source)
    assert flat(ij.decode_imagej_roi_zip(wire))==flat(source)

@pytest.mark.parametrize('method',[zipfile.ZIP_STORED,zipfile.ZIP_DEFLATED])
@pytest.mark.parametrize('stream',[False,True])
def test_independent_python_zip_including_data_descriptors(method,stream):
    pairs=flat(entries());wire=python_zip(pairs,method,stream)
    assert flat(ij.decode_imagej_roi_zip(wire))==pairs

def test_empty_set_and_comment_are_complete():
    assert ij.decode_imagej_roi_zip(ij.encode_imagej_roi_zip([]))==[]
    out=io.BytesIO()
    with zipfile.ZipFile(out,'w') as z:z.comment=b'owned comment';z.writestr('region.roi',roi())
    assert len(ij.decode_imagej_roi_zip(out.getvalue()))==1

@pytest.mark.parametrize('name',['../escape.roi','/absolute.roi','C:ads.roi','folder/x.roi','folder\\x.roi','bad\x00.roi','CON.roi','LPT9.roi','notes.txt','name.ROI','.roi'])
def test_unsafe_or_non_roi_names_refuse_without_omissions(name):
    with pytest.raises(ValueError):ij.encode_imagej_roi_zip([ij.ImageJRoiZipEntry(name,roi())])
    if '\x00' not in name:
        with pytest.raises(ValueError):ij.decode_imagej_roi_zip(python_zip([(name,roi())]))

def test_duplicates_and_non_roi_members_refuse_complete_archive():
    with pytest.raises(ValueError,match='Duplicate'):ij.encode_imagej_roi_zip([ij.ImageJRoiZipEntry('same.roi',roi())]*2)
    with pytest.warns(UserWarning):wire=python_zip([('same.roi',roi()),('same.roi',roi())])
    with pytest.raises(ValueError,match='Duplicate'):ij.decode_imagej_roi_zip(wire)
    with pytest.raises(ValueError):ij.decode_imagej_roi_zip(python_zip([('ok.roi',roi()),('junk.txt',b'junk')]))
    with pytest.raises(ValueError,match='header'):ij.decode_imagej_roi_zip(python_zip([('bad.roi',b'not ROI')]))

@pytest.mark.parametrize('limits',[ij.ImageJRoiZipLimits(max_archive_bytes=22),ij.ImageJRoiZipLimits(max_entries=1),ij.ImageJRoiZipLimits(max_entry_bytes=64),ij.ImageJRoiZipLimits(max_total_bytes=100),ij.ImageJRoiZipLimits(max_name_bytes=2),ij.ImageJRoiZipLimits(max_working_bytes=100)])
def test_entry_aggregate_and_working_bounds(limits):
    with pytest.raises(ValueError):ij.encode_imagej_roi_zip(entries(),limits)
    with pytest.raises(ValueError):ij.decode_imagej_roi_zip(python_zip(flat(entries())),limits)

@pytest.mark.parametrize('offset,fmt,value',[(6,'H',45),(8,'H',1),(10,'H',99),(16,'I',1),(20,'I',0xffffffff),(24,'I',0xffffffff),(34,'H',1),(38,'I',0o120777<<16),(42,'I',1)])
def test_corrupt_central_metadata_refuses(offset,fmt,value):
    wire=bytearray(ij.encode_imagej_roi_zip(entries()[:1]));p=central(wire)
    struct.pack_into('<'+fmt,wire,p+offset,value)
    with pytest.raises(ValueError):ij.decode_imagej_roi_zip(bytes(wire))

@pytest.mark.parametrize('offset,fmt,value',[(4,'H',10),(6,'H',1),(8,'H',0),(14,'I',1),(18,'I',0),(22,'I',1)])
def test_local_central_disagreement_refuses(offset,fmt,value):
    wire=bytearray(ij.encode_imagej_roi_zip(entries()[:1]));struct.pack_into('<'+fmt,wire,offset,value)
    with pytest.raises(ValueError):ij.decode_imagej_roi_zip(bytes(wire))

def test_crc_payload_corruption_and_every_truncation():
    wire=ij.encode_imagej_roi_zip(entries()[:1],compress=False)
    for end in range(len(wire)):
        with pytest.raises(ValueError):ij.decode_imagej_roi_zip(wire[:end])
    corrupt=bytearray(wire);corrupt[30+len('A.roi')+10]^=1
    with pytest.raises(ValueError,match='CRC'):ij.decode_imagej_roi_zip(bytes(corrupt))
    with pytest.raises(ValueError):ij.decode_imagej_roi_zip(wire+b'unclaimed')

def test_all_existing_roi_fixture_families_preserved_as_bytes():
    paths=['imagej-roi-imagej154r.json','imagej-roi-rotated-imagej154r.json','imagej-roi-composite-imagej154r.json','imagej-roi-rounded-options-imagej154r.json','imagej-roi-arrow-imagej154r.json','imagej-roi-spline-imagej154r.json','imagej-roi-text-imagej154r.json','imagej-roi-image-annotation-imagej154r.json']
    # Independent complete list: two representatives per family, no discovery
    # from the native archive's output to define its expected membership.
    expected=[]
    for group,name in enumerate(paths):
        cases=json.loads((ROOT/'tests/fixtures'/name).read_text(encoding='utf-8'))['cases']
        for index in (0,len(cases)-1):expected.append((f'family{group}-{index}.roi',bytes.fromhex(cases[index]['hex'])))
    wire=python_zip(expected)
    assert flat(ij.decode_imagej_roi_zip(wire))==expected
    with zipfile.ZipFile(io.BytesIO(ij.encode_imagej_roi_zip([ij.ImageJRoiZipEntry(n,b) for n,b in expected]))) as z:
        assert [(n,z.read(n)) for n in z.namelist()]==expected

def test_container_does_not_claim_semantic_roi_acceptance():
    opaque=bytearray(64);opaque[:4]=b'Iout'
    wire=ij.encode_imagej_roi_zip([ij.ImageJRoiZipEntry('future.roi',bytes(opaque))])
    assert ij.decode_imagej_roi_zip(wire)[0].data==bytes(opaque)
    with pytest.raises(ValueError):ij.decode_imagej_roi(bytes(opaque))

def unsigned_descriptor(wire):
    wire=bytearray(wire);c=central(wire);assert wire[c-16:c-12]==b'PK\x07\x08'
    del wire[c-16:c-12];struct.pack_into('<I',wire,len(wire)-6,c-4)
    return bytes(wire)

@pytest.mark.parametrize('method',[zipfile.ZIP_STORED,zipfile.ZIP_DEFLATED])
def test_unsigned_data_descriptors(method):
    source=[('stream.roi',roi())]
    assert flat(ij.decode_imagej_roi_zip(unsigned_descriptor(python_zip(source,method,True))))==source

def test_unsigned_descriptor_crc_equal_to_descriptor_signature():
    prefix=roi();base=zlib.crc32(prefix+b'\0'*4);basis={}
    for bit in range(32):
        v=zlib.crc32(prefix+(1<<bit).to_bytes(4,'little'))^base;mask=1<<bit
        while v:
            pivot=v.bit_length()-1
            if pivot not in basis:basis[pivot]=(v,mask);break
            q,m=basis[pivot];v^=q;mask^=m
    target=0x08074b50^base;solution=0
    while target:
        v,m=basis[target.bit_length()-1];target^=v;solution^=m
    payload=prefix+solution.to_bytes(4,'little');assert zlib.crc32(payload)==0x08074b50
    wire=unsigned_descriptor(python_zip([('crc.roi',payload)],zipfile.ZIP_STORED,True))
    assert ij.decode_imagej_roi_zip(wire)[0].data==payload

def test_cp437_and_unicode_extra_names():
    wire=bytearray(python_zip([('X.roi',roi())],zipfile.ZIP_STORED));c=central(wire)
    wire[30]=0x82;wire[c+46]=0x82
    assert ij.decode_imagej_roi_zip(bytes(wire))[0].name=='é.roi'
    out=io.BytesIO();raw=b'X.roi';name='λ😀.roi'.encode('utf-8')
    info=zipfile.ZipInfo(raw.decode());body=b'\1'+struct.pack('<I',zlib.crc32(raw))+name
    info.extra=struct.pack('<HH',0x7075,len(body))+body
    with zipfile.ZipFile(out,'w') as z:z.writestr(info,roi())
    assert ij.decode_imagej_roi_zip(out.getvalue())[0].name=='λ😀.roi'

def test_incorrect_unicode_extra_crc_refuses():
    out=io.BytesIO();info=zipfile.ZipInfo('X.roi');body=b'\1'+struct.pack('<I',0)+'λ.roi'.encode('utf-8')
    info.extra=struct.pack('<HH',0x7075,len(body))+body
    with zipfile.ZipFile(out,'w') as z:z.writestr(info,roi())
    with pytest.raises(ValueError,match='Unicode path CRC'):ij.decode_imagej_roi_zip(out.getvalue())

def test_zip64_extra_and_links_refuse():
    for extra,attrs in [(struct.pack('<HH',1,0),0),(b'',0o120777<<16)]:
        out=io.BytesIO();info=zipfile.ZipInfo('X.roi');info.extra=extra;info.create_system=3;info.external_attr=attrs
        with zipfile.ZipFile(out,'w') as z:z.writestr(info,roi())
        with pytest.raises(ValueError):ij.decode_imagej_roi_zip(out.getvalue())

def test_decompression_bomb_size_lies_and_trailing_stream_refuse():
    # Highly compressed content must not allocate past either actual or declared
    # expansion bounds, even with CRC/header fields deliberately made consistent.
    payload=b'Iout'+b'\0'*(2*1024*1024);wire=python_zip([('large.roi',payload)])
    with pytest.raises(ValueError,match='entry byte limit'):ij.decode_imagej_roi_zip(wire,ij.ImageJRoiZipLimits(max_entry_bytes=1024))
    lie=bytearray(wire);c=central(lie);struct.pack_into('<I',lie,22,64);struct.pack_into('<I',lie,c+24,64)
    with pytest.raises(ValueError,match='expansion exceeds'):ij.decode_imagej_roi_zip(bytes(lie))
    wire=bytearray(ij.encode_imagej_roi_zip(entries()[:1]));c=central(wire)
    wire[c:c]=b'JUNK';struct.pack_into('<I',wire,18,struct.unpack_from('<I',wire,18)[0]+4)
    struct.pack_into('<I',wire,c+4+20,struct.unpack_from('<I',wire,c+4+20)[0]+4);struct.pack_into('<I',wire,len(wire)-6,c+4)
    with pytest.raises(ValueError,match='trailing stream'):ij.decode_imagej_roi_zip(bytes(wire))

def test_central_order_preserved_and_unreferenced_local_bytes_refuse():
    source=entries()[:2];wire=bytearray(ij.encode_imagej_roi_zip(source));c=central(wire)
    n=46+len(source[0].name);first=bytes(wire[c:c+n]);wire[c:c+2*n]=wire[c+n:c+2*n]+first
    assert flat(ij.decode_imagej_roi_zip(bytes(wire)))==flat(source[::-1])
    wire=bytearray(ij.encode_imagej_roi_zip(source[:1]));c=central(wire);wire[c:c]=b'\0';struct.pack_into('<I',wire,len(wire)-6,c+1)
    with pytest.raises(ValueError,match='unreferenced'):ij.decode_imagej_roi_zip(bytes(wire))

@pytest.mark.parametrize('value',[True,1.5,-1,2**100])
def test_zip_limit_types_are_strict(value):
    with pytest.raises((ValueError,TypeError,OverflowError)):ij.ImageJRoiZipLimits(max_entries=value)

@pytest.mark.parametrize('code',range(128,256))
def test_complete_cp437_high_half_matches_independent_codec(code):
    wire=bytearray(python_zip([('X.roi',roi())],zipfile.ZIP_STORED));c=central(wire)
    wire[30]=code;wire[c+46]=code
    assert ij.decode_imagej_roi_zip(bytes(wire))[0].name==bytes([code]).decode('cp437')+'.roi'

def test_omitted_central_member_is_not_silently_lost():
    wire=bytearray(ij.encode_imagej_roi_zip(entries()[:2]));c=central(wire)
    first_size=46+len(entries()[0].name);second_size=46+len(entries()[1].name)
    del wire[c+first_size:c+first_size+second_size]
    end=len(wire)-22;struct.pack_into('<HH',wire,end+8,1,1);struct.pack_into('<I',wire,end+12,first_size)
    # An independent central-directory reader now sees just one member, despite
    # a second local member remaining physically present in the archive.
    with zipfile.ZipFile(io.BytesIO(wire)) as z:assert z.namelist()==['A.roi']
    with pytest.raises(ValueError,match='unreferenced'):ij.decode_imagej_roi_zip(bytes(wire))

def test_decoded_name_expansion_and_ambiguous_end_records_refuse():
    name='X'*1020+'.roi';wire=bytearray(python_zip([(name,roi())],zipfile.ZIP_STORED));c=central(wire)
    wire[30:30+1020]=b'\xb3'*1020;wire[c+46:c+46+1020]=b'\xb3'*1020
    with pytest.raises(ValueError,match='decoded name byte limit'):ij.decode_imagej_roi_zip(bytes(wire))
    wire=bytearray(ij.encode_imagej_roi_zip([]));fake=bytes(wire);comment=b'prefix'+fake
    struct.pack_into('<H',wire,20,len(comment));wire+=comment
    with pytest.raises(ValueError,match='ambiguous'):ij.decode_imagej_roi_zip(bytes(wire))
