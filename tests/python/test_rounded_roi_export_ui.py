"""Actual export/replay/overwrite controls with rounded native annotations."""
import base64
import json
import math
import os
from pathlib import Path
import struct
import numpy as np
import pytest
import imagejxx as ij

CASES=['axis','rotated','oversized','zero','precision']

def projection(wire):
    outer=ij.decode_imagej_roi(wire,mask_width=14,mask_height=8)
    raw=base64.b64decode(outer.properties.split(': ',1)[1]);offset=4
    def blob():
        nonlocal offset
        size=int.from_bytes(raw[offset:offset+4],'big');offset+=4
        value=raw[offset:offset+size];offset+=size;return value
    blob();offset+=24;blob();binding=blob();assert offset==len(raw)
    return ij.decode_imagej_roi(binding,mask_width=14,mask_height=8),binding

def check(report,case):
    assert report['state']=='observed' and report['error']=='' and report['acceptance_claim'] is False
    assert report['contract']=='p08s.rounded-roi-export.ui.001' and report['case_id']==case
    assert 0 < len(report['frames']) <= 550
    assert report['worker_waits']
    for wait in report['worker_waits']:
        assert wait['poll_milliseconds'] == 20 and wait['deadline_seconds'] == 10
        assert type(wait['polls']) is int and wait['polls'] >= 0
        assert wait['polls'] * .020 - 1e-6 <= wait['elapsed_seconds'] <= 10
    assert all(f['events']==f['rendered']['input_frame']['raw_inputs'] for f in report['frames'])
    assert all(c['control']['id'] and c['control']['enabled'] for c in report['controls'])
    assert report['review']['ready'] and report['review']['projection_group']==9
    wire=Path(report['output_file']).read_bytes()
    assert wire==Path(report['existing_file']).read_bytes()
    mapped=ij.decode_imagej_roi_native_envelope(wire);row=mapped.annotation
    diameter=2**31-1 if case=='oversized' else 0 if case=='zero' else 6
    assert row.selection.kind==('rotated_rounded_rectangle' if case=='rotated' else 'rounded_rectangle')
    assert row.selection.attributes=={'future-selection':'native','corner_diameter':diameter}
    assert row.name=='edited λ😀' and row.attributes=={'opaque':'kept'}
    assert (row.plane,row.coordinate,row.t,row.c)==('xz',4,2,1)
    left=4.99999999 if case=='precision' else 2.25
    points=[[left,1.5],[10.75,6.25]]
    if case=='rotated':
        center=np.array([6.5,3.875]);angle=math.radians(23)
        u=np.array([math.cos(angle),math.sin(angle)])*4.25
        v=np.array([-math.sin(angle),math.cos(angle)])*2.375
        points=[center,center+u,center+v]
    np.testing.assert_allclose(row.selection.points,points,rtol=0,atol=1e-12)
    standard,binding=projection(wire)
    np.testing.assert_array_equal(standard.roi.selection.mask((8,14)),row.selection.mask((8,14)))
    assert bool(standard.rounded_rectangle)==(case=='axis')
    assert bool(standard.composite_path)==(case!='axis')
    assert struct.unpack_from('>H',binding,54)[0]==(6 if case=='axis' else 0)
    assert standard.properties=='project: λ😀\n' and standard.group==9
    assert report['source_data_shared_after_exports'] and report['temporary_files']==[]
    assert report['worker_read_checks']
    assert all(r['before_plane']==r['after_plane'] and r['before_region']==r['after_region'] for r in report['worker_read_checks'])
    assert report['states']['before']['metadata']==report['states']['exported']['metadata']

@pytest.mark.parametrize('case',CASES)
def test_rounded_export_actual_controls_both_hosts(case,tmp_path):
    from imagejxx import _desktop
    directory=tmp_path/'capture001'
    python=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._rounded_roi_export_ui_case(case,str(directory)))
    check(python,case)
    cpp=json.loads((Path(os.environ['IMAGEJXX_ROUNDED_EXPORT_CPP_BASE'])/case/'case.json').read_bytes())
    check(cpp,case)
