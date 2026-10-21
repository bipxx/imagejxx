"""Explicit resource controls and maximum 1Mi-pixel native payload UI paths."""
import io
import json
import os
from pathlib import Path
import zipfile
import pytest
import imagejxx as ij
from imagejxx import _desktop
from test_display_annotations_ui import png_rgb

@pytest.fixture(scope='module')
def reports(tmp_path_factory):
    cpp=json.loads(Path(os.environ['IMAGEJXX_IMPORT_LIMITS_CPP_REPORT']).read_text(encoding='utf-8'))
    directory=tmp_path_factory.mktemp('import-limits-ui')/'capture001'
    editor=_desktop.Editor(str(directory.parent/'preferences.json'))
    python=json.loads(editor._roi_import_limits_ui_case(str(directory)))
    assert python==json.loads((directory/'case.json').read_text(encoding='utf-8'))
    return cpp,python

def test_exact_limits_invalid_integer_refusal_and_no_partial_publication(reports):
    for r in reports:
        assert r['state']=='observed' and r['error']=='' and not r['acceptance_claim']
        assert all(f['events']==f['rendered']['input_frame']['raw_inputs'] for f in r['frames'])
        assert all(c['control']['id'] and c['control']['enabled'] for c in r['controls'])
        assert r['defaults']['input']==8*1024*1024 and r['defaults']['decoded']==32*1024*1024
        assert r['zero_refusal']['error'] and r['overflow_refusal']['error']
        assert r['file_refusal']['entries']==[] and r['file_refusal']['error']
        assert not r['decoded_refusal']['ready'] and r['decoded_refusal']['error']
        assert r['rgb_review']['limits']['decoded']==128*1024*1024 and r['rgb_review']['ready']
        assert r['native_round_trip_exact'] and r['shared_source_pixels'] and r['workflow_unchanged']
        assert r['zip_import_noop'] and r['zip_default_refusal']['error']
        assert r['native_pixel_bytes']==4*1024*1024 and r['undo_steps']==3

def test_maximum_native_rgba_original_basis_and_rgb_standard_projection(reports):
    limits=ij.ImageJDisplayRoiLimits(wire=ij.ImageJRoiLimits(max_bytes=64*1024*1024,max_text_bytes=64*1024*1024),max_retained_bytes=256*1024*1024)
    for r in reports:
        directory=Path(r['directory']);raw=(directory/'maximum-rgba.roi').read_bytes()
        assert len(raw)==r['rgba_bytes'] and len(raw)>16*1024*1024
        full=ij.decode_imagej_display_roi_envelope_full(raw,limits)
        assert full.mapped.source_bytes==(directory/'maximum-rgb.roi').read_bytes()
        row=full.mapped.annotation
        assert row.payload.width==row.payload.height==1024 and row.payload.channels==4
        assert row.payload.pixels==bytes([255,0,0,128])*(1024*1024)
        assert row.payload.bounds==[0,0,16,12] and row.attributes=={'maximum':'native RGBA'}
        assert full.standard_projection.pixels.shape==(1024,1024,3)
        assert (full.standard_projection.pixels[:,:,:,] == [255,0,0]).all()
        assert full.options.drop_image_alpha and full.options.reset_image_scale
        assert r['envelope_default_refusal']['error'] and not r['envelope_default_refusal']['entries']

def test_maximum_zip_transport_and_native_pixel_equation(reports):
    limits=ij.ImageJDisplayRoiLimits(wire=ij.ImageJRoiLimits(max_bytes=64*1024*1024,max_text_bytes=64*1024*1024),max_retained_bytes=256*1024*1024)
    for r in reports:
        data=(Path(r['directory'])/'maximum.zip').read_bytes()
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            assert z.testzip() is None and z.namelist()==['maximum-rgba.roi']
            image=ij.decode_imagej_display_roi_envelope(z.read('maximum-rgba.roi'),limits)
            assert image.annotation.payload.pixels==bytes([255,0,0,128])*(1024*1024)

def test_large_preview_gl_and_native_alpha(reports):
    for r in reports:
        rgb=[v for p in r['rgb_preview'] for v in p['display_annotations'] if v.get('preview')]
        rgba=[v for p in r['rgba_preview'] for v in p['display_annotations'] if v.get('preview')]
        assert len(rgb)==len(rgba)==1 and rgb[0]['rgba_prefix'][:4]==[255,0,0,255]
        assert rgba[0]['rgba_prefix'][:4]==[255,0,0,128]
        pixels=png_rgb(Path(r['directory'])/'maximum-gl.png').astype('int32')
        assert ((pixels[:,:,0]>pixels[:,:,1]+50)&(pixels[:,:,0]>pixels[:,:,2]+50)).sum()>100

def test_captured_exact_limits_explicit_rebind_and_original_admission(reports):
    for r in reports:
        assert r['captured_exact']['fields']['decoded']==str(128*1024*1024-17)
        assert r['captured_refusal']['error'] and not r['captured_refusal']['ready']
        assert r['captured_review']['ready'] and r['captured_review']['destination_confirmed']
        assert r['captured_original_unchanged']
        assert r['original_refusal']['error'] and not r['original_refusal']['controls']['limits']['enabled']
