"""Independent checks of native replacement controls in both real ImGui hosts."""
import base64
import json
import os
from pathlib import Path
import pytest
import imagejxx as ij
from imagejxx import _desktop


@pytest.fixture(scope='module')
def reports(tmp_path_factory):
    cpp=json.loads(Path(os.environ['IMAGEJXX_ROI_REPLACEMENT_CPP_REPORT']).read_text(encoding='utf-8'))
    directory=tmp_path_factory.mktemp('replacement-ui')/'capture001'
    editor=_desktop.Editor(str(directory.parent/'preferences.json'))
    python=json.loads(editor._roi_import_replacement_ui_case(str(directory)))
    assert python==json.loads((directory/'case.json').read_text(encoding='utf-8'))
    return [cpp,python]


def rows(state):
    return state['metadata'].get('imagejxx.planar_annotations',{}).get('rows',[])


def sources(state):
    return state['metadata'].get('imagejxx.imagej_roi_import_sources')


def test_real_controls_refusal_preview_and_discard(reports):
    for r,context in zip(reports,['imgui_cpp','imgui_python']):
        assert r['contract']=='p06q.roi-import-replacement.ui.001'
        assert r['context']==context and r['state']=='observed' and r['error']==''
        assert r['extent_refusal']['error'] and not r['extent_refusal']['ready']
        assert 'Confirm use' in r['unconfirmed']['replacement_editor']['error']
        assert r['review']['ready'] and r['review']['review']==[{'name':'edited λ','kind':'point','plane':1,'coordinate':4,'t':2,'c':1}]
        assert r['captured_seed']['replacement_editor']['name']=='edited λ'
        assert r['captured_seed']['replacement_editor']['kind']=='point'
        assert r['discard_review']['review']==r['review']['review']
        xz=next(p for p in r['preview_render'] if p['plane']=='XZ')
        assert xz['import_preview']['active'] and not xz['import_preview']['replace']
        assert xz['import_preview']['layers'][0]['geometry']['points']==[[7.250000000001,0.500000000001]]
        assert all(f['events']==f['rendered']['input_frame']['raw_inputs'] for f in r['frames'])
        assert all(c['control']['enabled'] and c['control']['id'] for c in r['controls'])
        assert {'keep point','discard edits','keep box','undo','redo','workflow review'} <= {c['name'] for c in r['controls']}
        for name in ['before','extent_refusal','review']:
            assert rows(r['states'][name])==[] and sources(r['states'][name]) is None and r['states'][name]['undo']==0


def test_atomic_native_replacement_and_original_transport(reports):
    for r in reports:
        s=r['states'];applied=s['applied'];row=rows(applied)[0]
        assert applied['undo']==1 and len(applied['workflow'])==1
        assert (row['name'],row['plane'],row['coordinate'],row['t'],row['c'])==('edited λ','XZ',4,2,1)
        assert row['attributes']=={'opaque':'kept'}
        assert row['selection']['points']==[[7.250000000001,0.500000000001]]
        assert rows(s['undone'])==[] and sources(s['undone']) is None
        assert rows(s['redone'])==rows(applied) and sources(s['redone'])==sources(applied)
        entries=sources(s['box_applied'])['entries']
        assert len(entries)==2 and entries[0]['data']==entries[1]['data']
        assert sources(s['box_applied'])['version']==2
        assert entries[0]['native_replacement']['extent']==[3,2,8,10,14,1]
        original=ij.decode_imagej_roi_envelope(base64.b64decode(entries[0]['data']))
        assert list(original.target_extent)==[2,2,6,9,13,1]
        assert original.source_record.group==7 and original.source_record.properties=='source: λ😀\n'
        assert s['box_applied']['undo']==2 and len(s['box_applied']['workflow'])==2
        for command in s['box_applied']['workflow']:
            assert command['version']==2
            ij.validate_imagej_roi_import_record(command)
        assert r['original_unchanged'] and r['pixels_shared']


def test_rotated_geometry_style_and_precision(reports):
    import math
    for r in reports:
        box=rows(r['states']['box_applied'])[1]
        assert box['attributes']=={'opaque':'kept'}
        selection=box['selection']
        assert selection['kind']=='rotated_rectangle'
        # Native rotated boxes store center and the two half-axis endpoints.
        points=selection['points']
        assert len(points)==3
        assert points[0]==pytest.approx([5.500000000001,4],abs=1e-13)
        assert 2*math.dist(points[0],points[1])==pytest.approx(4.25)
        assert 2*math.dist(points[0],points[2])==pytest.approx(2.5)
        assert math.atan2(points[1][1]-points[0][1],points[1][0]-points[0][0])==pytest.approx(math.pi/6)
        assert box['plane']=='XZ' and box['coordinate']==4 and box['t']==2 and box['c']==1
        assert selection['attributes']['imagejxx.roi_style']=={'schema_version':1,'stroke_color':[0,255,0,255],'fill_color':None,'stroke_width':2.5}


def test_mask_loading_rejects_wrong_canvas_and_cancel_is_atomic(reports):
    for r in reports:
        assert r['mask_refusal']['replacement_editor']['error']
        assert not r['mask_refusal']['ready']
        assert r['mask_review']['ready'] and r['mask_review']['mode']==1
        assert r['mask_review']['review']==[{'name':'destination mask','kind':'mask','plane':2,'coordinate':4,'t':2,'c':1}]
        before=r['states']['mask_before'];after=r['states']['mask_canceled']
        assert after==before and after['undo']==1 and len(rows(after))==1
        assert rows(after)[0]['attributes']=={'mask-origin':'destination'}
        assert sources(after) is None
        yz=next(p for p in r['mask_preview_render'] if p['plane']=='YZ')
        assert yz['import_preview']['active'] and yz['import_preview']['replace']
        assert len(yz['import_preview']['layers'])==1
