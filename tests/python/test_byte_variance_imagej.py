"""Byte-preserving ImageJ variance, indexed display and recorded workflows."""
import copy,json,os
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij
from test_indexed_type_dispatch import fixture

REFERENCE=json.loads((Path(__file__).parents[1]/'fixtures/byte-variance-imagej154r.json').read_bytes())

def case(row):
    owner,reads,palette,request,pixels=fixture(row)
    if row['rectangle']:
        edit=owner.begin_edit();edit.select(ij.Selection('rectangle',[[1,1],[6,3]]));owner.commit(edit)
        owner=ij.Document(owner.snapshot())
        request=ij.TargetRequest(ij.TargetScope.RANGES,t=ij.AxisRange(0,1),c=ij.AxisRange(0,1),z=ij.AxisRange(0,row['planes']),roi=ij.RoiPolicy.ACTIVE_SELECTION)
    return owner,reads,palette,request,pixels

@pytest.mark.parametrize('row',REFERENCE['cases'],ids=lambda r:f"s{r['signal']}-p{r['palette_kind']}-z{r['planes']}-r{r['range']}-k{r['radius']}-a{r['rectangle']}")
def test_reference_byte_variance_and_final_display(row):
    owner,reads,palette,request,pixels=case(row);before=owner.snapshot()
    command=ij.resolve_rank_command(before,request,ij.RankFilterCommandOptions('variance',row['radius']))
    record=command.record();ij.validate_rank_command_record(record)
    assert ij.rank_command_from_record(before,record).parameters()==command.parameters() and reads.counts().plane_reads==0
    prepared=ij.prepare_operation('rank_filter_imagej',command.target,command.parameters());out=prepared.preview
    expected=np.array(row['output'],np.uint8).reshape(pixels.shape)
    np.testing.assert_array_equal(out.dataset.numpy('TCZYX'),expected)
    extent=(1,1,5,2) if row['rectangle'] else None
    for z in range(row['planes']):
        np.testing.assert_array_equal(ij.rank_filter_imagej(pixels[0,0,z],row['radius'],method=ij.RankFilterMethod.Variance,extent=extent),expected[0,0,z])
    assert ij.indexed_palette(out)==palette and out.display.properties==before.display.properties
    assert out.display.properties.get('display_range',[0.,255.])==row['display_range']
    assert prepared.begin_edit().dependencies==ij.Change.ALL and not owner.undo_steps
    owner.commit(prepared.begin_edit());owner.undo();np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'),pixels)
    owner.redo();np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'),expected)

@pytest.mark.parametrize('dtype',[np.uint16,np.float32,np.float64])
def test_scalar_variance_admission_and_float64_refusal(dtype):
    observer=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(np.ones((3,3),dtype),axes='YX').observe_reads(observer))
    if dtype is np.float64:
        with pytest.raises(ValueError):ij.resolve_rank_command(owner.snapshot(),options=ij.RankFilterCommandOptions('variance',1))
        with pytest.raises(ValueError):ij.prepare_operation('rank_filter_imagej',ij.resolve_target(owner.snapshot()),{'method':'variance','radius':1})
        with pytest.raises(ValueError):ij.rank_filter_imagej(np.ones((3,3),dtype),1,method='variance')
        assert observer.counts().plane_reads==0
    else:
        command=ij.resolve_rank_command(owner.snapshot(),options=ij.RankFilterCommandOptions('variance',1))
        assert observer.counts().plane_reads==0
        prepared=ij.prepare_operation('rank_filter_imagej',command.target,command.parameters())
        np.testing.assert_array_equal(prepared.preview.dataset.numpy('YX'),np.zeros((3,3),dtype))
        assert prepared.preview.display.properties['display_range']==[1.,1.]
    assert owner.undo_steps==0

def test_variance_program_authored_options_and_session_transport():
    row=REFERENCE['cases'][0];owner,_,palette,request,pixels=case(row);before=owner.snapshot()
    original=ij.resolve_rank_command(before,request,ij.RankFilterCommandOptions('median',2)).record()
    step=ij.NativeWorkflowOperation('rank_filter_imagej',{},request,original=original,filter_options=ij.RankFilterCommandOptions('variance',row['radius']))
    program=ij.NativeWorkflowProgram([step]);saved=copy.deepcopy(program.record());loaded=ij.NativeWorkflowProgram.from_json(program.json())
    prepared=ij.prepare_native_workflow(before,loaded.steps)
    np.testing.assert_array_equal(prepared.preview.dataset.numpy('TCZYX').reshape(row['planes'],32),row['output'])
    assert saved==program.record() and saved['steps'][0]['original']==original and ij.indexed_palette(prepared.preview)==palette

@pytest.mark.parametrize('indexed',[False,True])
def test_legacy_variance_preserves_range_records_and_guards_display(tmp_path,indexed):
    from imagejxx.ui.app import Editor
    row=next(r for r in REFERENCE['cases'] if r['planes']==1 and r['range']==1 and not r['rectangle'])
    owner,_,palette,_,_=case(row)
    if not indexed:
        edit=owner.begin_edit();edit.remove_extension('imagejxx.indexed_color');owner.commit(edit);owner=ij.Document(owner.snapshot())
    view=owner.snapshot().display.properties;editor=Editor(preferences=tmp_path/'prefs.json');editor.recording=True;editor.add_native_document(owner)
    editor.apply_operation('rank_filter_imagej',{'method':'variance','radius':row['radius']},asynchronous=False)
    assert owner.snapshot().display.properties==view and editor.workflow[-1]['parameters']['method']=='variance'
    if indexed:assert ij.indexed_palette(owner.snapshot())==palette
    editor.action('undo');assert owner.snapshot().display.properties==view
    editor.action('redo');assert owner.snapshot().display.properties==view
    editor.apply_operation('rank_filter_imagej',{'method':'variance','radius':1})
    display=owner.snapshot().display;display.zoom=3;owner.set_display(display)
    with pytest.raises(RuntimeError,match='Document changed'):editor.wait()

def test_variance_empty_extent_and_constant_are_exact():
    pixels=np.full((3,4),77,np.uint8)
    np.testing.assert_array_equal(ij.rank_filter_imagej(pixels,2.85,method='variance'),np.zeros_like(pixels))
    np.testing.assert_array_equal(ij.rank_filter_imagej(pixels,2.85,method='variance',extent=(1,1,0,0)),pixels)

def test_actual_variance_rank_dialog_both_hosts(tmp_path):
    if os.environ.get('IMAGEJXX_RENDER_TESTS')!='1':pytest.skip('render checks are opt in')
    from imagejxx import _desktop
    report=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._rank_ui_case('R14','imgui_python',str(tmp_path/'capture001')))
    cpp=json.loads(Path(os.environ['IMAGEJXX_VARIANCE_CPP_REPORT']).read_bytes())
    for row in [report,cpp]:
        assert row['observation_state']=='observed' and row['case_id']=='R14'
        assert 'variance.applied' in row['states'] and 'variance.undo' in row['states'] and 'variance.redo' in row['states']
        assert any(f['label']=='variance.preview' for f in row['frames'])
        assert all(f['requested_events']==f['rendered']['input_frame']['raw_inputs'] for f in row['frames'])
        before=row['fixture_sources']['source'];applied=row['states']['variance.applied']['documents'][0]
        undone=row['states']['variance.undo']['documents'][0];redone=row['states']['variance.redo']['documents'][0]
        expected=np.array([[30,30,30,30,30],[30,0,255,0,30],[30,255,255,255,30],[30,0,255,0,30],[30,30,30,30,30]],np.uint8)
        preview=next(f['rendered']['ordinary_pixels']['readback']['source'] for f in row['frames'] if f['label']=='variance.preview')
        np.testing.assert_array_equal(np.array(preview['pixels']).reshape(5,5,1)[...,0],expected)
        np.testing.assert_array_equal(np.frombuffer(bytes.fromhex(applied['planes'][1]['declared_hex']),np.uint8).reshape(5,5),expected)
        assert applied['metadata']['extensions']['imagejxx.indexed_color']==before['metadata']['extensions']['imagejxx.indexed_color']
        assert applied['display']['properties']==before['display']['properties'] and applied['undo_steps']==1
        assert [p['declared_hex'] for p in undone['planes']]==[p['declared_hex'] for p in before['planes']]
        assert [p['declared_hex'] for p in redone['planes']]==[p['declared_hex'] for p in applied['planes']]
        assert row['states']['variance.applied']['workflow'][-1]['options']['method']=='variance'


@pytest.mark.parametrize('empty',[False,True])
def test_variance_irregular_or_empty_mask_restores_original_samples(empty):
    row=next(r for r in REFERENCE['cases'] if r['signal']==1 and r['palette_kind']==5 and r['planes']==1 and r['radius']==1 and r['rectangle']==1)
    owner,reads,palette,_,pixels=case(row);mask=np.zeros((4,8),np.uint8)
    if not empty:mask[1:3,1:6]=1;mask[1,2]=0
    edit=owner.begin_edit();edit.select(ij.Selection.from_mask(mask));owner.commit(edit);owner=ij.Document(owner.snapshot())
    command=ij.resolve_rank_command(owner.snapshot(),options=ij.RankFilterCommandOptions('variance',1))
    prepared=ij.prepare_operation('rank_filter_imagej',command.target,command.parameters())
    if empty:assert reads.counts().plane_reads==0 and prepared.outputs[0].skipped
    expected=np.array(row['output'],np.uint8).reshape(pixels.shape)
    expected[...,mask==0]=pixels[...,mask==0]
    np.testing.assert_array_equal(prepared.preview.dataset.numpy('TCZYX'),expected)
    assert ij.indexed_palette(prepared.preview)==palette
    if empty:
        owner.commit(prepared.begin_edit());assert owner.undo_steps==0

def test_variance_cancel_and_new_document_publication_are_isolated():
    row=next(r for r in REFERENCE['cases'] if r['signal']==1 and r['palette_kind']==5 and r['planes']==1 and r['radius']==1 and not r['rectangle'])
    owner,reads,palette,_,pixels=case(row);app=ij.Application();app.adopt(owner)
    request=ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE,destination=ij.OutputDestination.NEW_DOCUMENT)
    command=ij.resolve_rank_command(owner.snapshot(),request,ij.RankFilterCommandOptions('variance',1))
    cancellation=ij.CancellationToken();cancellation.cancel()
    with pytest.raises(ij.OperationCancelled):app.prepare('rank_filter_imagej',command.target,command.parameters(),cancellation=cancellation)
    assert reads.counts().plane_reads==0 and owner.undo_steps==0
    prepared=app.prepare('rank_filter_imagej',command.target,command.parameters());result=app.publish(prepared,title='Byte variance')
    assert result.created and result.document_id!=owner.id and owner.undo_steps==0
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'),pixels)
    np.testing.assert_array_equal(result.document.snapshot().dataset.numpy('TCZYX').reshape(1,32),row['output'])
    assert ij.indexed_palette(result.document.snapshot())==palette


def test_variance_catalog_matches_source_tooling_choices():
    shipped=json.loads((Path(__file__).parents[2]/"src/imagejxx/ui/commands.json").read_bytes())
    assert shipped==ij.operation_catalog()
    entry=next(row for row in shipped if row["id"]=="rank_filter_imagej")
    assert "variance" in next(p for p in entry["parameters"] if p["name"]=="method")["choices"]
