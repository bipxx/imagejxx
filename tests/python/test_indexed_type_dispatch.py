"""Pinned indexed/pseudocolor type semantics and byte-index filter behavior."""
import json
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij
REFERENCE=json.loads((Path(__file__).parents[1]/'fixtures/indexed-type-imagej154r.json').read_bytes())

def fixture(row):
    pixels=np.array(row['input'],np.uint8).reshape(1,1,row['planes'],4,8)
    observer=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(pixels,axes='TCZYX').observe_reads(observer))
    table=np.zeros((256,3),np.uint8);table[:len(row['source_palette'])]=row['source_palette']
    record=ij.encode_indexed_palette(table,len(row['source_palette']))
    tx=owner.begin_edit();tx.set_extension('imagejxx.indexed_color',record);tx.set_extension('future',{'kept':True});owner.commit(tx)
    view=owner.snapshot().display;view.crosshair=[2,1,row['planes']-1];properties={'custom_lut':table.tolist()}
    if row['range']:properties['display_range']=[1.,127.]
    view.properties=properties
    owner.set_display(view);owner=ij.Document(owner.snapshot())
    target=ij.TargetRequest(ij.TargetScope.RANGES,t=ij.AxisRange(0,1),c=ij.AxisRange(0,1),z=ij.AxisRange(0,row['planes']),roi=ij.RoiPolicy.WHOLE_PLANE)
    return owner,observer,record,target,pixels

@pytest.mark.parametrize('row',REFERENCE['cases'],ids=lambda r:f"p{r['palette_kind']}-z{r['planes']}-r{r['range']}-w{r['weighted']}-{r['operation']}")
def test_reference_types_classification_and_index_filters(row):
    owner,reads,palette,request,pixels=fixture(row);source=owner.snapshot();kind=ij.classify_indexed_palette(palette)
    assert {k:kind[k] for k in ['color','pseudo','inverted']}=={k:row['before'][k] for k in ['color','pseudo','inverted']}
    assert kind['imagej_type']==row['before']['image_type']
    op=row['operation'];target=ij.resolve_target(source,request)
    if op.startswith('gray'):
        name='convert_image';parameters={'dtype':{'gray8':'uint8','gray16':'uint16','gray32':'float32'}[op],'weighted_rgb':row['weighted']}
    elif op=='gaussian':name='gaussian_blur_imagej';parameters={'sigma':[1.,1.]}
    else:name='rank_filter_imagej';parameters={'method':'median','radius':1.}
    if row['error']:
        with pytest.raises(ValueError,match='8-bit grayscale'):ij.prepare_operation(name,target,parameters)
        assert reads.counts().plane_reads==0 and owner.undo_steps==0
        return
    prepared=ij.prepare_operation(name,target,parameters);result=prepared.preview
    np.testing.assert_array_equal(result.dataset.numpy('TCZYX').reshape(row['planes'],32),row['output'])
    out=ij.indexed_palette(result);assert out['color_count']==len(row['output_palette']) and out['palette'][:out['color_count']]==row['output_palette']
    assert result.metadata['extensions']['future']=={'kept':True} and not owner.undo_steps
    if op.startswith('gray'):
        assert prepared.begin_edit().dependencies==ij.Change.ALL
        assert result.metadata['extensions']['imagejxx.indexed_type_source']['input_palette']==palette
        if op=='gray16':assert 'display_range' not in result.display.properties
        else:assert result.display.properties.get('display_range',[0.,255.])==row['display_range']
    else:assert result.display.properties==source.display.properties and out==palette
    owner.commit(prepared.begin_edit());assert owner.undo_steps==1
    owner.undo();np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'),pixels);assert ij.indexed_palette(owner.snapshot())==palette
    owner.redo();np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX').reshape(row['planes'],32),row['output'])

def simple_indexed():
    pixels=np.array([[[255,0,0],[0,255,0]]],np.uint8)
    rgb=ij.Document(ij.Dataset(pixels,axes='YXS'))
    return ij.Document(ij.prepare_indexed_dataset(ij.resolve_indexed_dataset_command(rgb.snapshot(),max_colors=2)).preview)

def target():return ij.TargetRequest(ij.TargetScope.RANGES,t=ij.AxisRange(0,1),c=ij.AxisRange(0,1),z=ij.AxisRange(0,1),roi=ij.RoiPolicy.WHOLE_PLANE)

def test_gray8_then_float_workflow_retires_color_interpretation():
    owner=simple_indexed();program=ij.NativeWorkflowProgram([
        ij.NativeWorkflowOperation('convert_image',{'dtype':'uint8'},target()),
        ij.NativeWorkflowOperation('convert_image',{'dtype':'float32'},target())])
    prepared=ij.prepare_native_workflow(owner.snapshot(),program.steps)
    np.testing.assert_array_equal(prepared.preview.dataset.numpy('TCZYX'),[[[[[85.,85.]]]]])
    assert ij.classify_indexed_palette(ij.indexed_palette(prepared.preview))['color'] is False
    assert owner.undo_steps==0

def test_invalid_late_indexed_conversion_refuses_before_prefix_samples():
    owner=simple_indexed();reads=ij.DatasetReadObserver();owner=ij.Document(owner.snapshot().dataset.observe_reads(reads))
    # Restore palette metadata after constructing the observed provider.
    source=simple_indexed();tx=owner.begin_edit();tx.set_extension('imagejxx.indexed_color',ij.indexed_palette(source.snapshot()));owner.commit(tx)
    with pytest.raises(ValueError):ij.prepare_native_workflow(owner.snapshot(),[
        ij.NativeWorkflowOperation('add',{'value':1},target()),ij.NativeWorkflowOperation('convert_image',{'dtype':'float32'},target())])
    assert reads.counts().plane_reads==0

def test_rgb_to_gray_retires_old_indexed_definition():
    owner=simple_indexed();rgb=ij.prepare_indexed_rgb(ij.resolve_indexed_rgb_command(owner.snapshot())).preview
    prepared=ij.prepare_operation('convert_image',ij.resolve_target(rgb,target()),{'dtype':'uint8'})
    assert ij.indexed_palette(prepared.preview) is None and 'imagejxx.retired_indexed_palette' in prepared.preview.metadata['extensions']
    assert 'custom_lut' not in prepared.preview.display.properties

def test_quantize_dynamic_palette_gray8_then_float_is_safe():
    owner=simple_indexed();rgb=ij.prepare_indexed_rgb(ij.resolve_indexed_rgb_command(owner.snapshot())).preview
    original=ij.resolve_indexed_dataset_command(rgb,max_colors=2).record()
    steps=[ij.NativeWorkflowOperation('indexed_dataset',{},target(),original=original),ij.NativeWorkflowOperation('convert_image',{'dtype':'uint8'},target()),ij.NativeWorkflowOperation('convert_image',{'dtype':'float32'},target())]
    result=ij.prepare_native_workflow(rgb,steps).preview
    np.testing.assert_array_equal(result.dataset.numpy('TCZYX'),[[[[[85.,85.]]]]])

def test_quantize_dynamic_unsupported_palette_never_publishes():
    owner=simple_indexed();rgb=ij.prepare_indexed_rgb(ij.resolve_indexed_rgb_command(owner.snapshot())).preview;original=ij.resolve_indexed_dataset_command(rgb,max_colors=2).record()
    source=ij.Document(rgb)
    with pytest.raises(ValueError):ij.prepare_native_workflow(source.snapshot(),[ij.NativeWorkflowOperation('indexed_dataset',{},target(),original=original),ij.NativeWorkflowOperation('convert_image',{'dtype':'float32'},target())])
    assert source.undo_steps==0 and source.snapshot().dataset.shape[-1]==3

def test_forged_pending_palette_metadata_cannot_bypass_validation():
    owner=simple_indexed();tx=owner.begin_edit();tx.set_extension('imagejxx.workflow_pending_indexed_palette',True);owner.commit(tx)
    with pytest.raises(ValueError):ij.prepare_operation('convert_image',ij.resolve_target(owner.snapshot(),target()),{'dtype':'float32'})

def test_conversion_guard_includes_display_even_when_pixels_do_not_change():
    row=next(r for r in REFERENCE['cases'] if r['palette_kind']==2 and r['operation']=='gray8' and r['planes']==1 and not r['weighted'])
    owner,_,_,request,_=fixture(row);prepared=ij.prepare_operation('convert_image',ij.resolve_target(owner.snapshot(),request),{'dtype':'uint8'})
    view=owner.snapshot().display;view.zoom=3;owner.set_display(view)
    with pytest.raises(ij.RevisionConflict):owner.commit(prepared.begin_edit())

def test_actual_standard_type_menu_uses_indexed_policy_in_both_hosts(tmp_path):
    import os
    from imagejxx import _desktop
    cpp=json.loads(Path(os.environ['IMAGEJXX_RGB_CPP_REPORT']).read_bytes())
    python=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._rgb_dataset_ui_case(str(tmp_path/'capture001')))
    for report in [cpp,python]:
        data=report['indexed_type_conversion'];assert data['gray_pixels']==data['pixels']==data['redo_pixels']==[85.,85.]
        assert data['type']==2 and data['undo_steps']==2  # Native PixelType::Float32 in the captured report.
        assert data['gray_palette']['palette']==[[i,i,i] for i in range(256)]
        assert data['undo_palette']['color_count']==2 and data['record']['operation']=='convert_image'

def test_legacy_editor_preserves_palette_only_type_edit(tmp_path):
    from imagejxx.ui.app import Editor
    row=next(r for r in REFERENCE['cases'] if r['palette_kind']==2 and r['operation']=='gray8' and r['planes']==1 and not r['weighted'])
    owner,_,palette,_,pixels=fixture(row);editor=Editor(preferences=tmp_path/'prefs.json');editor.recording=True;editor.add_native_document(owner)
    editor.apply_operation('convert_image',{'dtype':'uint8'},asynchronous=False)
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'),pixels)
    assert ij.indexed_palette(owner.snapshot())['palette']==[[i,i,i] for i in range(256)] and owner.undo_steps==1
    assert editor.workflow[-1]['operation']=='convert_image'
    editor.action('undo');assert ij.indexed_palette(owner.snapshot())==palette
    editor.action('redo');assert ij.classify_indexed_palette(ij.indexed_palette(owner.snapshot()))['color'] is False

def test_legacy_type_publication_rejects_changed_display(tmp_path):
    from imagejxx.ui.app import Editor
    owner=simple_indexed();before=ij.indexed_palette(owner.snapshot());editor=Editor(preferences=tmp_path/'prefs.json');editor.recording=True;editor.add_native_document(owner)
    editor.apply_operation('convert_image',{'dtype':'uint8'})
    view=owner.snapshot().display;view.zoom=3;owner.set_display(view)
    with pytest.raises(RuntimeError,match='Document changed'):editor.wait()
    assert owner.undo_steps==0 and not editor.workflow and ij.indexed_palette(owner.snapshot())==before

@pytest.mark.parametrize('future',[False,True])
def test_unsupported_channel_palette_type_refuses_before_samples(future):
    row=next(r for r in REFERENCE['cases'] if r['palette_kind']==2 and r['operation']=='gray8' and r['planes']==1 and not r['weighted'])
    owner,reads,_,request,_=fixture(row);view=owner.snapshot().display;props=view.properties
    props['channel_display']={'schema':'imagejxx.channel-display','version':99} if future else {'schema':'imagejxx.channel-display','version':1,'mode':'color','channels':[{'visible':True,'range':[0,255],'lut':{'name':'Red'}}]}
    view.properties=props;owner.set_display(view)
    with pytest.raises(ValueError):ij.prepare_operation('convert_image',ij.resolve_target(owner.snapshot(),request),{'dtype':'uint8'})
    assert reads.counts().plane_reads==0

def test_indexed_type_keeps_hyperstack_calibration_selection_and_xyz_annotations():
    from test_color_stack_workflow import fixture as hyperstack
    owner,_,_=hyperstack();indexed=ij.prepare_indexed_dataset(ij.resolve_indexed_dataset_command(owner.snapshot(),max_colors=2)).preview
    codes=indexed.dataset.numpy('TCZYX');palette=np.array(ij.indexed_palette(indexed)['palette'],np.uint8)
    expected=np.floor(palette[codes].astype(np.float64).mean(axis=-1)+.5).astype(np.uint8)
    owner=ij.Document(indexed);request=ij.TargetRequest(ij.TargetScope.RANGES,t=ij.AxisRange(0,2),c=ij.AxisRange(0,2),z=ij.AxisRange(0,2),roi=ij.RoiPolicy.WHOLE_PLANE)
    prepared=ij.prepare_operation('convert_image',ij.resolve_target(owner.snapshot(),request),{'dtype':'uint8'})
    result=prepared.preview;np.testing.assert_array_equal(result.dataset.numpy('TCZYX'),expected)
    assert result.dataset.shape==(2,2,2,2,3,1) and result.dataset.calibration.spacing==[2,3,5] and result.dataset.calibration.time_origin==7
    assert len(result.rois)==len(indexed.rois) and result.selection.kind==indexed.selection.kind
    assert len(ij.planar_annotations(result))==len(ij.planar_annotations(indexed)) and len(ij.planar_display_annotations(result))==len(ij.planar_display_annotations(indexed))
    assert result.display.t==indexed.display.t and result.display.c==indexed.display.c and result.display.crosshair==indexed.display.crosshair
    owner.commit(prepared.begin_edit());assert owner.undo_steps==1;owner.undo();assert ij.indexed_palette(owner.snapshot())==ij.indexed_palette(indexed)
    owner.redo();np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'),expected)
