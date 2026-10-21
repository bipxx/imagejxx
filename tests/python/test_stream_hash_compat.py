"""Independent hash vectors and corruption checks for Python 3.10 evidence readers.

These load the real tooling/readers. Synthetic executable-shaped files test the
identity guard only; separate installed C++ runs establish actual UI behavior.
"""
import hashlib
import importlib.util
import io
from pathlib import Path
import sys
import zipfile

import pytest

ROOT=Path(__file__).resolve().parents[2]
SOURCES=[
 'scripts/capture_binary_reference.py',
 'scripts/capture_channel_display_projection_ui.py',
 'scripts/capture_color_stack_reference.py',
 'scripts/capture_rgb_conversion_reference.py',
 'scripts/reference_inventory.py',
 'tests/python/test_channel_display_ui.py',
 'tests/python/test_gaussian_controls_ui.py',
 'tests/python/test_gaussian_roi_ui.py',
 'tests/python/test_manual_threshold_ui.py',
 'tests/python/test_roi_boolean_ui.py',
 'tests/python/test_scientific_navigation_ui.py',
 'tests/python/test_scientific_operation_targets_ui.py',
 'tests/python/test_scientific_session_ui.py',
 'tests/python/test_scientific_slicing_ui.py',
 'tests/python/test_selection_creation_ui.py',
]

@pytest.fixture(params=SOURCES,ids=lambda x:Path(x).stem)
def reader(request,monkeypatch):
 monkeypatch.syspath_prepend(str(ROOT/'scripts'))
 path=ROOT/request.param
 spec=importlib.util.spec_from_file_location('stream_hash_contract_'+path.stem,path)
 module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
 return module


@pytest.mark.parametrize('data,expected',[
 (b'', 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855'),
 (b'abc','ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad'),
 (b'a'*1000000,'cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0'),
],ids=['empty','abc','million-a'])
def test_published_sha256_vectors(reader,data,expected):
 stream=io.BytesIO(data)
 assert reader._stream_sha256(stream)==expected
 assert not stream.closed and stream.read()==b''
 if sys.version_info[:2]==(3,10):assert not hasattr(hashlib,'file_digest')


def test_short_nonseekable_reads_are_bounded(reader):
 payload=bytes(range(256))*8192+b'final'
 class ShortReads:
  def __init__(self):self.source=io.BytesIO(payload);self.calls=0
  def read(self,size):
   assert 0<size<=1024*1024
   self.calls+=1;return self.source.read(min(size,4093))
  def seek(self,*args):raise AssertionError('Hashing must not seek')
 stream=ShortReads()
 assert reader._stream_sha256(stream)==hashlib.sha256(payload).hexdigest()
 assert stream.calls>500


def test_current_stream_position_is_respected(reader):
 stream=io.BytesIO(b'prefixabc');stream.seek(6)
 assert reader._stream_sha256(stream)=='ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad'


@pytest.mark.parametrize('compression',[zipfile.ZIP_STORED,zipfile.ZIP_DEFLATED])
def test_zip_member_hash(reader,compression):
 payload=bytes(range(256))*8192+b'zip tail';output=io.BytesIO()
 with zipfile.ZipFile(output,'w',compression=compression) as archive:archive.writestr('member.bin',payload)
 with zipfile.ZipFile(output) as archive,archive.open('member.bin') as stream:
  assert reader._stream_sha256(stream)==hashlib.sha256(payload).hexdigest()
  assert not stream.closed


def test_read_errors_do_not_return_a_partial_digest(reader):
 class Broken:
  calls=0
  def read(self,size):
   self.calls+=1
   if self.calls==1:return b'prefix'
   raise OSError('owned read failure')
 with pytest.raises(OSError,match='owned read failure'):reader._stream_sha256(Broken())


def test_zip_crc_failure_is_preserved(reader):
 output=io.BytesIO()
 with zipfile.ZipFile(output,'w',compression=zipfile.ZIP_STORED) as archive:archive.writestr('member.bin',b'abc')
 raw=bytearray(output.getvalue());raw[30+len('member.bin')]^=1
 with zipfile.ZipFile(io.BytesIO(raw)) as archive,archive.open('member.bin') as stream:
  with pytest.raises(zipfile.BadZipFile):reader._stream_sha256(stream)


def test_text_stream_is_refused(reader):
 with pytest.raises(TypeError):reader._stream_sha256(io.StringIO('abc'))


GUARDS=[Path(x).stem for x in SOURCES if x.startswith('tests/') and 'gaussian' not in x]
@pytest.mark.parametrize('name',GUARDS)
def test_real_reader_identity_guards_keep_rejecting_corruption(name,tmp_path,monkeypatch):
 spec=importlib.util.spec_from_file_location('hash_guard_'+name,ROOT/'tests/python'/f'{name}.py')
 module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
 path=tmp_path/'bin/imagejxx.exe';path.parent.mkdir();path.write_bytes(b'owned identity-only artifact')
 expected=hashlib.sha256(path.read_bytes()).hexdigest()
 monkeypatch.setenv('IMAGEJXX_REQUIRE_RENDER_TESTS','1');monkeypatch.setenv('IMAGEJXX_CPP_EXE',str(path))
 monkeypatch.setenv('IMAGEJXX_CPP_EXE_SHA256',expected)
 assert module.installed_cpp_executable.__wrapped__()==path.resolve()
 monkeypatch.setenv('IMAGEJXX_CPP_EXE_SHA256','0'*64)
 with pytest.raises(AssertionError):module.installed_cpp_executable.__wrapped__()
 monkeypatch.setenv('IMAGEJXX_CPP_EXE_SHA256',expected);path.write_bytes(b'owned altered artifact')
 with pytest.raises(AssertionError):module.installed_cpp_executable.__wrapped__()
