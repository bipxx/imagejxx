"""Indexed plugin admission, canonical rank pixels and native dtype retirement."""
import json
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij
from test_indexed_type_dispatch import fixture, simple_indexed, target

REFERENCE=json.loads((Path(__file__).parents[1]/'fixtures/indexed-filter-imagej154r.json').read_bytes())
RANK={'minimum','maximum','mean','median'}

@pytest.mark.parametrize('row',REFERENCE['cases'],ids=lambda r:f"p{r['palette_kind']}-z{r['planes']}-r{r['range']}-{r['operation']}")
def test_pinned_plugin_eligibility_and_rank_pixels(row):
    owner,reads,palette,request,pixels=fixture(row);source=owner.snapshot()
    operation=row['operation'];name='rank_filter_imagej' if operation in RANK else operation
    parameters={'method':operation,'radius':1.} if operation in RANK else {}
    if row['error']:
        with pytest.raises(ValueError,match='does not support indexed color'):
            ij.prepare_operation(name,ij.resolve_target(source,request),parameters)
        assert reads.counts().plane_reads==0 and owner.undo_steps==0
        return
    prepared=ij.prepare_operation(name,ij.resolve_target(source,request),parameters);out=prepared.preview
    if operation in RANK:
        np.testing.assert_array_equal(out.dataset.numpy('TCZYX').reshape(row['planes'],32),row['output'])
        assert ij.indexed_palette(out)==palette and out.display.properties==source.display.properties
    elif operation=='find_edges':
        assert ij.indexed_palette(out) is None and out.metadata['extensions']['imagejxx.retired_indexed_palette']==palette
    else:
        assert ij.indexed_palette(out)==palette and prepared.begin_edit().dependencies==ij.Change.ALL
    assert not owner.undo_steps
    owner.commit(prepared.begin_edit());assert owner.undo_steps==1
    owner.undo();np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'),pixels);assert ij.indexed_palette(owner.snapshot())==palette
    owner.redo();np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'),out.dataset.numpy('TCZYX'))

@pytest.mark.parametrize('name,parameters',[('variance_filter',{'radius':1}),('rank_filter',{'radius':1,'method':'variance'}),('laplacian',{}),('difference_of_gaussians',{})])
@pytest.mark.parametrize('partial',[False,True])
def test_nonbyte_native_filters_retire_palette_with_paired_history(name,parameters,partial):
    row=next(r for r in REFERENCE['cases'] if r['palette_kind']==5 and r['planes']==3)
    owner,_,palette,request,pixels=fixture(row)
    if partial:request=ij.TargetRequest(ij.TargetScope.POSITIONS,positions=[ij.PlanePosition(0,0,1)],roi=ij.RoiPolicy.WHOLE_PLANE)
    source=owner.snapshot();prepared=ij.prepare_operation(name,ij.resolve_target(source,request),parameters);out=prepared.preview
    assert out.dataset.numpy('TCZYX').dtype==np.float64
    assert ij.indexed_palette(out) is None and out.metadata['extensions']['imagejxx.retired_indexed_palette']==palette
    assert out.metadata['extensions']['imagejxx.indexed_filter_source']['operation']==name
    assert not {'custom_lut','lut','display_range','channel_display'} & out.display.properties.keys()
    assert out.display.crosshair==source.display.crosshair and out.metadata['extensions']['future']=={'kept':True}
    assert prepared.begin_edit().dependencies==ij.Change.ALL
    if partial:np.testing.assert_array_equal(out.dataset.numpy('TCZYX')[0,0,0],pixels[0,0,0])
    owner.commit(prepared.begin_edit());owner.undo();assert ij.indexed_palette(owner.snapshot())==palette and owner.snapshot().display.properties==source.display.properties
    owner.redo();assert ij.indexed_palette(owner.snapshot()) is None

@pytest.mark.parametrize('name',['invert','smooth','sharpen','find_edges'])
def test_known_ineligible_workflow_refuses_before_prefix_samples(name):
    row=next(r for r in REFERENCE['cases'] if r['palette_kind']==5 and r['planes']==1)
    owner,reads,_,request,_=fixture(row)
    with pytest.raises(ValueError,match='does not support indexed color'):
        ij.prepare_native_workflow(owner.snapshot(),[ij.NativeWorkflowOperation('add',{'value':1},request),ij.NativeWorkflowOperation(name,{},request)])
    assert reads.counts().plane_reads==0 and owner.undo_steps==0

def test_retirement_workflow_preflight_matches_execution_domain():
    owner=simple_indexed();steps=[ij.NativeWorkflowOperation('variance_filter',{'radius':1},target()),ij.NativeWorkflowOperation('convert_image',{'dtype':'float32'},target())]
    prepared=ij.prepare_native_workflow(owner.snapshot(),steps)
    assert ij.indexed_palette(prepared.preview) is None and owner.undo_steps==0

def test_dynamic_quantized_ineligible_filter_refuses_atomically():
    indexed=simple_indexed();rgb=ij.prepare_indexed_rgb(ij.resolve_indexed_rgb_command(indexed.snapshot())).preview
    original=ij.resolve_indexed_dataset_command(rgb,max_colors=2).record();owner=ij.Document(rgb)
    with pytest.raises(ValueError,match='does not support indexed color'):
        ij.prepare_native_workflow(owner.snapshot(),[ij.NativeWorkflowOperation('indexed_dataset',{},target(),original=original),ij.NativeWorkflowOperation('invert',{},target())])
    assert owner.undo_steps==0 and owner.snapshot().dataset.shape[-1]==3

def test_allowed_filter_is_guarded_against_palette_display_changes():
    row=next(r for r in REFERENCE['cases'] if r['palette_kind']==2 and r['planes']==1)
    owner,_,_,request,_=fixture(row);prepared=ij.prepare_operation('invert',ij.resolve_target(owner.snapshot(),request),{})
    view=owner.snapshot().display;view.zoom=2;owner.set_display(view)
    with pytest.raises(ij.RevisionConflict):owner.commit(prepared.begin_edit())
    assert owner.undo_steps==0

def test_legacy_editor_retirement_and_recording(tmp_path):
    from imagejxx.ui.app import Editor
    owner=simple_indexed();palette=ij.indexed_palette(owner.snapshot());editor=Editor(preferences=tmp_path/'prefs.json');editor.recording=True;editor.add_native_document(owner)
    editor.apply_operation('variance_filter',{'radius':1},asynchronous=False)
    assert ij.indexed_palette(owner.snapshot()) is None and editor.workflow[-1]['operation']=='variance_filter'
    editor.action('undo');assert ij.indexed_palette(owner.snapshot())==palette
    editor.action('redo');assert ij.indexed_palette(owner.snapshot()) is None
