"""Rounded primitive promotion is explicit, reviewed and reversible in both hosts."""
import base64
import json
import math
import os
from pathlib import Path
import pytest
import imagejxx as ij


def rows(state):
    return state['metadata'].get('imagejxx.planar_annotations',{}).get('rows',[])


def check(r,case):
    assert r['state']=='observed' and r['error']=='' and not r['acceptance_claim']
    assert r['contract']=='p08t.rounded-roi-import.ui.001' and r['case_id']==case
    assert all(f['events']==f['rendered']['input_frame']['raw_inputs'] for f in r['frames'])
    assert all(c['control']['enabled'] and c['control']['id'] for c in r['controls'])
    assert r['unmapped']['error'] and r['unmapped']['replacement_editor'] is None
    assert r['default_review']['review'][0]['kind']=='mask'
    assert r['default_seed']['replacement_editor']['kind']=='mask'
    assert r['promoted_seed']['replacement_editor']['kind']=='rounded_rectangle'
    assert r['promoted_seed']['replacement_editor']['corner_diameter']=='6'
    assert 'Confirm use' in r['unconfirmed']['replacement_editor']['error']
    assert 'integer' in r['invalid_diameter']['replacement_editor']['error']
    assert r['review']['ready'] and r['review']['entries'][0]['mapping_frozen']
    s=r['states'];assert s['before']==s['review']
    applied=s['applied'];assert applied['undo']==1 and len(applied['workflow'])==1
    row=rows(applied)[0];selection=row['selection']
    diameter=0 if case=='zero' else 2**31-1 if case=='oversized' else 3
    assert selection['kind']==('rotated_rounded_rectangle' if case=='rotated' else 'rounded_rectangle')
    assert selection['attributes']['corner_diameter']==diameter
    assert selection['attributes']['imagejxx.roi_style']=={'schema_version':1,'stroke_color':[0,255,0,255],'fill_color':[255,0,0,128],'stroke_width':2.5}
    assert (row['name'],row['plane'],row['coordinate'],row['t'],row['c'])==('rounded λ😀','XZ',4,2,1)
    if case=='rotated':
        c,u,v=selection['points']
        assert c==pytest.approx([6.5,3.875])
        assert math.dist(c,u)==pytest.approx(4.25) and math.dist(c,v)==pytest.approx(2.375)
        assert math.atan2(u[1]-c[1],u[0]-c[0])==pytest.approx(math.radians(23))
    else:assert selection['points']==[[2.25,1.5],[10.75,6.25]]
    archive=applied['metadata']['imagejxx.imagej_roi_import_sources'];assert archive['version']==4
    source=archive['entries'][0]['source']
    assert base64.b64decode(source['data'])==Path(r['original_file']).read_bytes()
    assert source['mapping']['plane']=='xz' and source['mapping']['extent']==[3,2,8,10,14,1]
    assert source['native_replacement']['annotation']==row
    original=ij.decode_imagej_roi(base64.b64decode(source['data']),mask_width=14,mask_height=8)
    assert original.rounded_rectangle==[2.25,1.5,8.5,4.75,6] and original.group==7
    assert original.properties=='source: λ😀\n'
    ij.validate_imagej_annotation_import_record(applied['workflow'][0])
    assert rows(s['undone'])==[] and 'imagejxx.imagej_roi_import_sources' not in s['undone']['metadata']
    assert s['redone']['metadata']==applied['metadata']==s['canceled']['metadata']
    assert s['canceled']['workflow']==applied['workflow'] and s['canceled']['undo']==1
    assert r['captured_seed']['replacement_editor']['kind']==selection['kind']
    assert r['captured_seed']['replacement_editor']['corner_diameter']==str(diameter)
    assert r['discard_review']['review']==r['review']['review']
    assert r['reset_review']['review'][0]['kind']=='mask'
    assert r['original_unchanged'] and r['pixels_shared']


@pytest.mark.parametrize('case',['axis','rotated','zero','oversized'])
def test_rounded_import_actual_controls_both_hosts(case,tmp_path):
    from imagejxx import _desktop
    directory=tmp_path/'capture001'
    r=json.loads(_desktop.Editor(str(tmp_path/'preferences.json'))._rounded_roi_import_ui_case(case,str(directory)))
    check(r,case)
    check(json.loads((Path(os.environ['IMAGEJXX_ROUNDED_IMPORT_CPP_BASE'])/case/'case.json').read_bytes()),case)
