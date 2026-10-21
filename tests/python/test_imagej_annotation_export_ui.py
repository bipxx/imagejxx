"""Shared mixed export controls, real inputs, file equations and typed previews."""
import base64
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
    cpp=json.loads(Path(os.environ['IMAGEJXX_MIXED_EXPORT_CPP_REPORT']).read_text(encoding='utf-8'))
    directory=tmp_path_factory.mktemp('mixed-export-ui')/'capture001'
    editor=_desktop.Editor(str(directory.parent/'preferences.json'))
    python=json.loads(editor._mixed_export_ui_case(str(directory)))
    assert python==json.loads((directory/'case.json').read_text(encoding='utf-8'))
    return cpp,python

def test_explicit_choices_losses_pure_review_and_closed_export_equations(reports):
    for r in reports:
        assert r['state']=='observed' and r['error']=='' and not r['acceptance_claim']
        assert all(f['events']==f['rendered']['input_frame']['raw_inputs'] for f in r['frames'])
        assert all(c['control']['id'] and c['control']['enabled'] for c in r['controls'])
        assert r['loaded']['source_index']==r['loaded']['annotation_index']==-1
        assert r['unselected']['error'] and r['loss_refusal']['error'] and r['font_refusal']['error']
        assert r['states']['before']==r['states']['review']
        assert r['shared_pixels'] and r['states']['before']['metadata']==r['states']['exported']['metadata']
        assert r['states']['before']['undo']==r['states']['exported']['undo']
        for record in r['states']['recording_off']['workflow']:
            ij.validate_imagej_annotation_export_record(record)
            assert record['version']==2 and record['sources'] is None
            assert all(m['source_kind']=='generated' and m['source_index'] is None for m in record['members'])

def test_complete_zip_and_exact_native_rgba_and_precision(reports):
    for r in reports:
        directory=Path(r['directory'])
        workflow=r['states']['zip_exported']['workflow'];record=workflow[-1]
        assert (directory/'mixed.zip').read_bytes()==base64.b64decode(record['output'])
        with zipfile.ZipFile(io.BytesIO(base64.b64decode(record['output']))) as z:
            assert z.testzip() is None and z.namelist()==['geometry.roi','text.roi','image.roi']
            assert all(i.compress_type==zipfile.ZIP_STORED for i in z.infolist())
            text=ij.decode_imagej_display_roi_envelope(z.read('text.roi'))
            assert text.annotation.payload.font_size==2.500000000001 and text.source_record.font_size==3
            image=ij.decode_imagej_display_roi_envelope(z.read('image.roi'))
            assert image.annotation.payload.bounds==[2.250000000001,2.500000000001,6,3]
            assert image.annotation.payload.pixels==bytes([255,0,0,128,0,0,0,255])
            assert image.annotation.attributes=={'opaque':'image'}
        assert r['duplicate_refusal']['error'] and r['duplicate_refusal']['queue_count']==1
        assert r['zip_review']['queue_count']==3 and r['zip_review']['ready']

def test_destination_reservation_recording_off_and_staleness(reports):
    for r in reports:
        assert r['captured_loaded']['workflow_review'] and r['captured_loaded']['path']==''
        assert r['destination_refusal']['error'] and not r['destination_refusal']['ready']
        assert r['reservation_refusal']['error'] and not (Path(r['directory'])/'reservation.roi').exists()
        assert r['exists_refusal']['error'] and len(r['states']['exported']['workflow'])==1
        assert r['ready_canceled']['error'] and not r['ready_canceled']['ready']
        assert r['race_refusal']['error']
        assert (Path(r['directory'])/'raced.roi').read_bytes()==b'competing destination'
        assert not (Path(r['directory'])/'canceled.roi').exists()
        assert not (Path(r['directory'])/'stale.roi').exists()
        assert r['states']['zip_exported']['workflow']==r['states']['recording_off']['workflow']
        assert not r['stale']['visible'] and r['stale']['cancel_reason']=='source_changed'

def test_native_standard_previews_and_opengl_red_image(reports):
    for r in reports:
        def layers(key):return [v for p in r[key] for v in p['display_annotations']]
        native=layers('native_layers');standard=layers('standard_layers')
        assert len(native)==len(standard)==1 and native[0]['preview'] and standard[0]['preview']
        assert native[0]['rgba_prefix']==[255,0,0,100,0,0,0,0]
        assert standard[0]['rgba_prefix']==[255,0,0,200,0,0,0,0]
        pixels=png_rgb(Path(r['directory'])/'draft-gl.png').astype('int32')
        assert pixels.shape[1]>=1000 and pixels.shape[0]>=700
        assert ((pixels[:,:,0]>pixels[:,:,1]+25)&(pixels[:,:,0]>pixels[:,:,2]+25)).sum()>50

def test_archived_metadata_regeneration_overrides_and_original_first_admission(reports):
    for r in reports:
        assert r['category_refusal']['error'] and r['tiff_refusal']['error']
        assert r['group_refusal']['error'] and r['original_refusal']['error']
        assert r['representation_refusal']['error'] and r['browse_routes_expire']
        record=r['states']['archived']['workflow'][-1]
        assert record['version']==1 and 'source_kind' not in record['members'][0]
        ij.validate_imagej_annotation_export_record(record)
        image=ij.decode_imagej_display_roi_envelope((Path(r['directory'])/'archived-image.roi').read_bytes())
        assert image.source_record.group==7 and image.source_record.properties=='retained original\n'
        full=ij.decode_imagej_display_roi_envelope_full((Path(r['directory'])/'archived-image.roi').read_bytes())
        assert full.standard_projection.group==9 and full.standard_projection.properties=='reviewed properties\n'
        assert image.annotation.payload.pixels==bytes([255,0,0,128,0,0,0,255])
        reset=ij.decode_imagej_roi_native_envelope((Path(r['directory'])/'captured-geometry.roi').read_bytes())
        assert reset.source_record.group==9 and reset.source_record.overlay_label_color==0xff112233 and reset.source_record.overlay_font_size==12
