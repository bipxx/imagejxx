"""Actual shared typed queue controls, draft rendering and guarded publication."""
import base64
import json
import os
from pathlib import Path
import pytest
import imagejxx as ij
from imagejxx import _desktop
from test_display_annotations_ui import png_rgb

DISPLAY='imagejxx.planar_display_annotations'
ROWS='imagejxx.planar_annotations'
SOURCES='imagejxx.imagej_roi_import_sources'

@pytest.fixture(scope='module')
def reports(tmp_path_factory):
    cpp=json.loads(Path(os.environ['IMAGEJXX_MIXED_IMPORT_CPP_REPORT']).read_text(encoding='utf-8'))
    directory=tmp_path_factory.mktemp('mixed-import-ui')/'capture001'
    editor=_desktop.Editor(str(directory.parent/'preferences.json'))
    python=json.loads(editor._mixed_import_ui_case(str(directory)))
    assert python==json.loads((directory/'case.json').read_text(encoding='utf-8'))
    return cpp,python

def display_layers(panes):return [v for p in panes for v in p['display_annotations']]

def test_actual_shared_controls_atomic_publication_and_color_interpretation(reports):
    for r in reports:
        assert r['contract']=='p06q.mixed-import.ui.001' and r['state']=='observed' and r['error']==''
        assert not r['acceptance_claim'] and all(f['events']==f['rendered']['input_frame']['raw_inputs'] for f in r['frames'])
        assert all(c['control']['id'] and c['control']['enabled'] for c in r['controls'])
        assert [e['display'] for e in r['loaded']['entries']]==[False,True,True]
        assert r['missing_color']['error'] and not r['missing_color']['ready']
        assert r['invalid_color']['error'] and not r['invalid_color']['ready']
        s=r['states'];assert s['before']==s['review']
        rows=s['applied']['metadata'][DISPLAY]['rows'];assert len(rows)==3
        assert rows[1]['payload']['color']==0xff3366cc and rows[1]['payload']['text']=='Hello'
        assert rows[2]['payload']['bounds']==[2.250000000001,2,6,3]
        assert base64.b64decode(rows[2]['payload']['pixels'])==bytes([255,0,0,128,0,0,0,255])
        assert s['applied']['undo']==s['before']['undo']+1 and len(s['applied']['workflow'])==1
        record=s['applied']['workflow'][0];ij.validate_imagej_annotation_import_record(record)
        assert record['after_sources']['version']==3 and record['after_sources']['entries'][1]['source']['mapping']['text_default_color']==0xff3366cc

def test_draft_renderer_alpha_source_guards_and_undo(reports):
    for r in reports:
        shown=display_layers(r['draft_layers']);hidden=display_layers(r['hidden_layers'])
        assert any(v.get('preview') and v['kind']=='text' for v in shown)
        image=next(v for v in shown if v.get('preview') and v['kind']=='image')
        assert image['rgba_prefix']==[255,0,0,100,0,0,0,0] and image['clipped_to_canvas']
        xy=next(p for p in r['draft_layers'] if p['plane']=='XY')
        geometry_end=max(v['draw_index_end'] for v in xy['import_preview']['layers'])
        assert all(v['draw_index_begin']>=geometry_end for v in xy['display_annotations'] if v.get('preview'))
        assert not any(v.get('preview') for v in hidden)
        assert r['states']['undone']['metadata']==r['states']['before']['metadata']
        assert r['states']['redone']['metadata']==r['states']['applied']['metadata']
        assert r['shared_pixels'] and r['processing_selection_absent']

def test_captured_review_explicit_native_replacement_and_reservation(reports):
    for r in reports:
        assert r['unconfirmed']['error'] and not r['unconfirmed']['ready']
        assert r['replacement']['ready'] and r['replacement']['review'][2]['plane']==1 and r['replacement']['review'][2]['coordinate']==1
        assert r['reservation_refusal']['error'] and not r['reservation_refusal']['ready']
        layers=display_layers(r['replacement_layers']);assert layers and all(v.get('preview') for v in layers)
        s=r['states'];rows=s['replaced']['metadata'][DISPLAY]['rows']
        assert len(rows)==2 and len(s['replaced']['metadata'][ROWS]['rows'])==1
        assert rows[1]['plane']=='XZ' and rows[1]['coordinate']==1 and rows[1]['attributes']=={'opaque':2**64-1}
        original=s['applied']['workflow'][0]
        replacement=s['replaced']['workflow'][-1];ij.validate_imagej_annotation_import_record(replacement)
        assert replacement['mode']=='replace' and replacement['after_sources']['entries'][2]['source']['data']==original['after_sources']['entries'][2]['source']['data']
        assert replacement['after_sources']['entries'][2]['source']['native_replacement']['extent']==[1,1,4,12,16,1]

def test_recording_off_late_failure_stale_source_and_pixel_free_publication(reports):
    for r in reports:
        s=r['states'];assert len(s['recording_off']['workflow'])==1
        assert s['bad']==s['recording_off'] and r['bad']['error'] and not r['bad']['ready']
        assert not r['stale']['visible'] and r['stale']['cancel_reason']=='source_changed'
        counts=r['publication_reads'];assert counts['before_plane']==counts['after_plane'] and counts['before_region']==counts['after_region']
        assert s['final']['metadata']==s['reopened']['metadata']
        for record in s['replaced']['workflow']:ij.validate_imagej_annotation_import_record(record)

def test_native_opengl_draft_red_pixels(reports):
    for r in reports:
        # The raw probe owns its screenshot alongside its input files; source
        # RGB data are grayscale except the pre-existing blue display layer.
        source=r['loaded']['path'];path=Path(source).parent/'draft-gl.png'
        pixels=png_rgb(path);height,width=pixels.shape[:2];rgb=pixels.astype('int32')
        red=int(((rgb[:,:,0]>rgb[:,:,1]+35)&(rgb[:,:,0]>rgb[:,:,2]+35)).sum())
        assert width>=1000 and height>=700 and red>100
