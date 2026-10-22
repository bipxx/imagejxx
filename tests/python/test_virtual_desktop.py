"""Native provider adaptation and shared ImGui integration."""
import json
import os
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij
from imagejxx import _desktop

@pytest.mark.parametrize('context',['imgui_cpp','imgui_python'])
@pytest.mark.parametrize('mode',['virtual_cancel','virtual_source','virtual_replay','virtual_xyz'])
def test_virtual_provider_histogram_ui(context,mode,tmp_path):
    case='histogram-async-'+mode
    if context=='imgui_cpp':
        report=json.loads((Path(os.environ['IMAGEJXX_HISTOGRAM_ASYNC_CPP_ROOT'])/case/'case.json').read_bytes())
    else:
        report=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._animation_ui_case(case,str(tmp_path/'capture')))
    assert report['contract']=='p10t.virtual-desktop.ui.001' and report['context']==context
    assert report['state']=='observed' and not report['acceptance_claim'] and not report['eager']
    assert all(f['events']==f['input_frame']['raw_inputs'] for f in report['frames'])
    assert report['attachment_reads']==0 and report['worker_reads_baseline']==1
    assert report['queued_busy'] and report['queued']['job']['state']=='queued'
    assert report['reserved_after']==0 and not report['processing_after']['active']
    expected,_=np.histogram(np.arange(16),bins=4,range=(0,48))
    assert report['baseline']['result']['counts']==expected.tolist()
    success=mode in ('virtual_replay','virtual_xyz')
    assert report['worker_reads_after']==1+int(success)
    if success:
        bins=4 if mode=='virtual_replay' else 6
        expected,_=np.histogram(np.arange(16),bins=bins,range=(0,48))
        assert report['after']['result']['counts']==expected.tolist()
        assert len(report['workflow_after'])==2
    else:
        assert report['after']['result']==report['baseline']['result']
        assert report['workflow_after']==report['workflow_before'] and report['defaults_after']==report['defaults_before']
    assert report['source']['undo']==int(mode=='virtual_source')
    if mode=='virtual_xyz':
        assert report['view']['orthogonal']
        assert len(report['xyz'])==3
        assert [v['width'] for v in report['xyz']]==[4,4,4]
        assert [v['height'] for v in report['xyz']]==[4,3,3]
        volume=np.arange(48,dtype=float).reshape(3,4,4)
        for view in report['xyz']:
            coordinate=view['coordinate'];plane=view['plane']
            expected=volume[coordinate] if plane=='XY' else volume[:,coordinate,:] if plane=='XZ' else volume[:,:,coordinate]
            readback=view['readback'];assert readback['available']
            assert readback['source']['pixels']==expected.ravel().tolist()
            rgb=np.repeat(np.clip(expected*17,0,255)[...,None],3,axis=2)
            assert readback['rendered_rgb']['pixels']==rgb.ravel().tolist()

@pytest.mark.parametrize('cap',[0,-1,True,None,1.5,'1024'])
def test_view_limit_admission_is_strict(cap,tmp_path):
    owner=ij.Document(ij.Dataset(np.zeros((2,2),dtype=np.uint16),axes='YX'))
    editor=_desktop.Editor(str(tmp_path/'prefs.json'))
    with pytest.raises((ValueError,TypeError,OverflowError)):
        editor.add_virtual_document(owner,max_view_bytes=cap)
    assert json.loads(editor.documents())==[]

def test_eager_discovery_and_explicit_adaptation(tmp_path):
    data=ij.Dataset(np.arange(24,dtype=np.uint16).reshape(3,2,4),axes='ZYX')
    assert data.has_eager_planes and data.observe_reads(ij.DatasetReadObserver()).has_eager_planes
    owner=ij.Document(data);editor=_desktop.Editor(str(tmp_path/'prefs.json'))
    editor.add_virtual_document(owner,max_view_bytes=1024)
    assert editor.native_snapshot().dataset.has_eager_planes
    np.testing.assert_array_equal(editor.plane('xy',2),np.arange(16,24,dtype=np.uint16).reshape(2,4))
