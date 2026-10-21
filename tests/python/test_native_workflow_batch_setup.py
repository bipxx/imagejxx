"""Closed Batch setup intent, bounded codecs and explicit file publication."""
import copy
import json
import numpy as np
import pytest
import imagejxx as ij
from test_native_workflow_batch import fixture


def programs():
    target=ij.TargetRequest(ij.TargetScope.ALL_Z,roi=ij.RoiPolicy.WHOLE_PLANE)
    common=ij.NativeWorkflowProgram([ij.NativeWorkflowOperation('add',{'value':5},target)])
    override=ij.NativeWorkflowProgram([
        ij.NativeWorkflowOperation('add',{'value':9},target),
        ij.NativeWorkflowOperation('gaussian_blur_imagej',{},target,
            filter_options=ij.GaussianBlurCommandOptions(0,False)),
        ij.NativeWorkflowOperation('add',{'value':2**64-1},target,enabled=False)])
    return common,override


def setup():
    common,override=programs()
    rows=[ij.NativeWorkflowBatchInput('missing/first.ijx','one'),
          ij.NativeWorkflowBatchInput('missing/λ😀.ijx','two',program=override,
            selection=ij.NativeWorkflowBatchSelection(document_index=1),expected_fingerprint='a'*64)]
    return ij.NativeWorkflowBatchSetup(common,rows,'missing/output',
        ij.NativeWorkflowBatchSelection(all_documents=True))


@pytest.mark.parametrize('transport',['record','json'])
def test_closed_roundtrip_retains_explicit_inheritance_without_fingerprints_or_execution(transport):
    s=setup()
    loaded=ij.NativeWorkflowBatchSetup.from_record(s.record()) if transport=='record' else ij.NativeWorkflowBatchSetup.from_json(s.json())
    assert loaded.record()==s.record() and loaded.json()==s.json()
    assert loaded.common_program.record()==programs()[0].record()
    assert loaded.selection.all_documents and loaded.selection.document_index==0
    assert loaded.output_directory=='missing/output'
    assert loaded.inputs[0].program is None and loaded.inputs[0].selection is None
    assert loaded.inputs[1].program.record()==programs()[1].record()
    assert loaded.inputs[1].selection.document_index==1
    assert all(i.expected_fingerprint=='' for i in loaded.inputs)
    assert set(s.record())=={'schema','version','common_program','selection','output_directory','inputs'}
    assert all(set(row)=={'path','output_stem','program','selection'} for row in s.record()['inputs'])
    assert 'fingerprint' not in s.json() and 'consent' not in s.record()


def test_empty_input_draft_roundtrips_and_copies_remain_immutable():
    common,_=programs();s=ij.NativeWorkflowBatchSetup(common,[])
    assert ij.NativeWorkflowBatchSetup.from_json(s.json()).inputs==[]
    record=s.record();record['common_program']['steps'].clear()
    assert len(s.common_program.steps)==1
    with pytest.raises(AttributeError): s.output_directory='other'


@pytest.mark.parametrize('kind',['array','image'])
def test_array_and_image_parameters_originals_and_disabled_state_survive(kind):
    target=ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE)
    if kind=='array':
        op=ij.NativeWorkflowOperation('convolve',{'kernel':np.eye(3),'normalize':False},target,enabled=False)
    else:
        # Reuse the public image parameter wire without imposing an execution op.
        r=programs()[0].record();step=r['steps'][0]
        step['operation']='calculate';step['parameters']={'other':{'kind':'image','value':{
            'width':2,'height':1,'channels':1,'type':1,'values':[2,7]}}}
        op=ij.NativeWorkflowProgram.from_record(r).steps[0]
    p=ij.NativeWorkflowProgram([op]);s=ij.NativeWorkflowBatchSetup(p,[])
    assert ij.NativeWorkflowBatchSetup.from_json(s.json()).common_program.record()==p.record()


@pytest.mark.parametrize('mutate',[
    lambda r:r.update(version=2), lambda r:r.update(version=True),lambda r:r.update(schema='other'),
    lambda r:r.update(fingerprint='a'*64),lambda r:r.update(consent=True),lambda r:r.update(prepared={}),
    lambda r:r['selection'].update(all_documents=1),lambda r:r['selection'].update(document_index=1),
    lambda r:r['inputs'][0].update(path=''),lambda r:r['inputs'][0].update(path='bad\0.ijx'),
    lambda r:r['inputs'][0].update(output_stem='../escape'),lambda r:r['inputs'][1].update(expected_fingerprint='a'*64),
    lambda r:r['inputs'][1]['program']['steps'][-1].update(original={'schema':'bad'}),
    lambda r:r['inputs'][1]['selection'].update(document_index=-1),
    lambda r:r['inputs'][1].update(selection={'all_documents':False,'document_index':True}),
])
def test_malformed_closed_setup_rejected(mutate):
    r=setup().record();mutate(r)
    with pytest.raises((ValueError,RuntimeError,IndexError)): ij.NativeWorkflowBatchSetup.from_record(r)
    with pytest.raises((ValueError,RuntimeError,IndexError)): ij.NativeWorkflowBatchSetup.from_json(json.dumps(r))


@pytest.mark.parametrize('text',['{"schema":"one","schema":"two"}', '{"x":'+ '['*129+'0'+']'*129+'}', '{"x":NaN}', '{"x":"\ud800"}'])
def test_duplicate_deep_nonfinite_and_invalid_unicode_json_rejected(text):
    with pytest.raises((ValueError,RuntimeError)): ij.NativeWorkflowBatchSetup.from_json(text)


@pytest.mark.parametrize('limits',[
    {'max_inputs':1}, {'max_steps_per_program':2}, {'max_total_steps':3}, {'max_record_bytes':100},
])
def test_limits_apply_to_builder_and_both_decoders(limits):
    s=setup();l=ij.NativeWorkflowBatchSetupLimits(**limits)
    with pytest.raises(ValueError): ij.NativeWorkflowBatchSetup(s.common_program,s.inputs,s.output_directory,s.selection,l)
    with pytest.raises(ValueError): ij.NativeWorkflowBatchSetup.from_record(s.record(),l)
    with pytest.raises(ValueError): ij.NativeWorkflowBatchSetup.from_json(s.json(),l)


def test_python_cycles_and_strict_limit_types():
    r=setup().record();r['inputs'].append(r)
    with pytest.raises(ValueError): ij.NativeWorkflowBatchSetup.from_record(r)
    for kw in [{'max_inputs':True},{'max_record_bytes':1.5},{'max_total_steps':0}]:
        with pytest.raises((ValueError,TypeError)): ij.NativeWorkflowBatchSetupLimits(**kw)


def test_file_save_load_replacement_and_cancellation_do_not_open_inputs(tmp_path):
    s=setup();path=tmp_path/'setup-λ😀.json'
    ij.save_native_workflow_batch_setup(str(path),s)
    assert ij.load_native_workflow_batch_setup(str(path)).record()==s.record()
    original=path.read_bytes()
    with pytest.raises((ValueError,RuntimeError)): ij.save_native_workflow_batch_setup(str(path),s)
    assert path.read_bytes()==original and len(list(tmp_path.iterdir()))==1
    ij.save_native_workflow_batch_setup(str(path),s,replace_existing=True)
    token=ij.CancellationToken();token.cancel()
    for action in [lambda:ij.load_native_workflow_batch_setup(str(path),cancellation=token),
                   lambda:ij.save_native_workflow_batch_setup(str(path),s,True,cancellation=token),
                   lambda:ij.NativeWorkflowBatchSetup.from_json(s.json(),cancellation=token)]:
        with pytest.raises(RuntimeError):action()
    assert path.read_bytes()==original and len(list(tmp_path.iterdir()))==1
    missing=tmp_path/'missing'/'setup.json'
    with pytest.raises((ValueError,RuntimeError)):ij.save_native_workflow_batch_setup(str(missing),s)
    assert not missing.parent.exists()
    with pytest.raises(ValueError):ij.load_native_workflow_batch_setup(str(path),ij.NativeWorkflowBatchSetupLimits(max_record_bytes=10))


def test_loaded_mixed_setup_outputs_independent_equations_preserve_inputs_and_tables(tmp_path):
    _,capture,paths,_,output,_=fixture(tmp_path);common,override=programs()
    rows=[ij.NativeWorkflowBatchInput(str(paths[0]),'one'),ij.NativeWorkflowBatchInput(str(paths[1]),'two',
        program=override,selection=ij.NativeWorkflowBatchSelection(document_index=1))]
    saved=tmp_path/'batch.json';s=ij.NativeWorkflowBatchSetup(common,rows,str(output))
    ij.save_native_workflow_batch_setup(str(saved),s);loaded=ij.load_native_workflow_batch_setup(str(saved))
    originals=[p.read_bytes() for p in paths]
    reviewed=ij.review_native_workflow_batch_input(loaded.inputs[1].path)
    second=loaded.inputs[1]
    ready_rows=[loaded.inputs[0],ij.NativeWorkflowBatchInput(second.path,second.output_stem,
        program=second.program,selection=second.selection,expected_fingerprint=reviewed.fingerprint)]
    prepared=ij.prepare_native_workflow_batch(ready_rows,loaded.common_program.steps,loaded.output_directory)
    prepared.publish()
    a=ij.load_session(str(output/'one.ijx'));b=ij.load_session(str(output/'two.ijx'))
    assert a.documents[0].snapshot().dataset.numpy('TCZYX')[1,2,0,0,0]==15
    assert a.documents[1].snapshot().dataset.numpy('ZYX')[0,0,0]==20
    assert b.documents[0].snapshot().dataset.numpy('TCZYX')[1,2,0,0,0]==10
    np.testing.assert_array_equal(b.documents[1].snapshot().dataset.numpy('ZYX'),np.full((3,3,7),29,np.uint16))
    assert a.workflow==b.workflow==capture.workflow and a.results==b.results==capture.results
    assert [p.read_bytes() for p in paths]==originals
