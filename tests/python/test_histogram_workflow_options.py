"""Authored analysis intent preserves history and shares native validation."""
import copy
import csv
import io
import json
import numpy as np
import pytest
import imagejxx as ij
from test_histogram_workflow import fixture


def choices(bins=5, bounds=(0.,10.), log=False):
    options=ij.TargetHistogramOptions()
    options.binning=ij.HistogramBinning.UniformRange
    options.bins=bins;options.range=bounds
    return ij.NativeWorkflowHistogramOptions(options,log)


@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float32])
@pytest.mark.parametrize('captured',[False,True])
@pytest.mark.parametrize('bins',[1,5,65536])
def test_explicit_choices_roundtrip_and_independent_histogram(dtype,captured,bins):
    owner,reads,target,record=fixture(dtype)
    original=copy.deepcopy(record) if captured else None
    step=ij.NativeWorkflowHistogram('authored.csv',target,original,authored_options=choices(bins))
    program=ij.NativeWorkflowProgram([step]);wire=program.record()
    assert wire['version']==5 and wire['steps'][0]['original']==original
    assert wire['steps'][0]['options']==dict(binning='uniform_range',bins=bins,range=[0.,10.],log_counts=False)
    restored=ij.NativeWorkflowProgram.from_json(program.json())
    assert restored.record()==wire and reads.counts().plane_reads==0
    prepared=ij.prepare_native_workflow(owner.snapshot(),restored.steps)
    result=prepared.histograms[0]
    assert result['record']['options']==dict(binning='uniform_range',bins=bins,range=[0.,10.])
    assert not result['record']['log_counts']
    expected=np.histogram(np.arange(12),bins=np.linspace(0,10,bins+1))[0]
    np.testing.assert_array_equal(result['result'].histogram['counts'],expected)
    assert result['result'].finite_count==12 and result['result'].included_count==11
    assert result['result'].overflow_count==1 and result['result'].sum==66
    assert owner.undo_steps==0


def test_options_and_history_are_owning_values_not_mutable_aliases():
    owner,_,target,record=fixture()
    original=copy.deepcopy(record)
    raw=ij.TargetHistogramOptions();raw.binning=ij.HistogramBinning.UniformRange;raw.bins=5;raw.range=(0.,10.)
    authored=ij.NativeWorkflowHistogramOptions(raw,False)
    step=ij.NativeWorkflowHistogram('h.csv',target,record,authored_options=authored)
    raw.bins=100;authored.histogram.bins=1;record['profile']='broken'
    program=ij.NativeWorkflowProgram([step]);wire=program.record()
    assert wire['steps'][0]['original']==original and wire['steps'][0]['options']['bins']==5
    wire['steps'][0]['options']['bins']=99
    assert program.record()['steps'][0]['options']['bins']==5
    assert ij.prepare_native_workflow(owner.snapshot(),program.steps).histograms[0]['result'].histogram['counts'].tolist()==[2,2,2,2,3]


def test_mixed_v5_retained_and_authored_steps_see_ordered_stage():
    owner,_,target,record=fixture();before=owner.snapshot()
    steps=[ij.NativeWorkflowHistogram('before.csv',target,record),
           ij.NativeWorkflowOperation('add',{'value':5},target),
           ij.NativeWorkflowHistogram('after.csv',target,authored_options=choices())]
    program=ij.NativeWorkflowProgram(steps);wire=program.record()
    assert wire['version']==5 and wire['steps'][0]['options'] is None
    assert wire['steps'][2]['original'] is None
    plan=ij.prepare_native_workflow(before,ij.NativeWorkflowProgram.from_record(wire).steps)
    first,last=plan.histograms
    assert first['result'].histogram['counts'].tolist()==[5,5,2,0]
    assert last['result'].histogram['counts'].tolist()==np.histogram(np.arange(12)+5,bins=np.linspace(0,10,6))[0].tolist()
    assert last['result'].sum==126 and last['result'].overflow_count==6
    assert owner.snapshot().shares_data(before) and owner.undo_steps==0
    owner.commit(plan.begin_edit());assert owner.undo_steps==1
    owner.undo();assert owner.snapshot().shares_data(before)


@pytest.mark.parametrize('mutation',[
    lambda r:r.update(version=4),lambda r:r.update(version=6),
    lambda r:r['steps'][0]['options'].update(extra=1),
    lambda r:r['steps'][0]['options'].update(bins=True),
    lambda r:r['steps'][0]['options'].update(bins=5.),
    lambda r:r['steps'][0]['options'].update(bins=0),
    lambda r:r['steps'][0]['options'].update(bins=65537),
    lambda r:r['steps'][0]['options'].update(log_counts=1),
    lambda r:r['steps'][0]['options'].update(binning='unknown'),
    lambda r:r['steps'][0]['options'].update(range=[1.,1.]),
    lambda r:r['steps'][0]['options'].update(range=[0.]),
    lambda r:r['steps'][0]['options'].update(range=[True,10.]),
    lambda r:r['steps'][0]['options'].update(range=[2**53+1,2**54]),
    lambda r:r['steps'][0].update(options=None),
])
def test_closed_v5_grammar_and_options_refuse_without_reads(mutation):
    owner,reads,target,_=fixture()
    wire=ij.NativeWorkflowProgram([ij.NativeWorkflowHistogram('h.csv',target,authored_options=choices())]).record()
    mutation(wire)
    with pytest.raises((ValueError,TypeError,KeyError)):ij.NativeWorkflowProgram.from_record(wire)
    assert reads.counts().plane_reads==0 and owner.undo_steps==0


@pytest.mark.parametrize('disabled',[False,True])
def test_original_and_authored_options_both_validate_even_when_disabled(disabled):
    owner,reads,target,record=fixture();record['profile']='unknown'
    step=ij.NativeWorkflowHistogram('h.csv',target,record,enabled=not disabled,authored_options=choices())
    with pytest.raises(ValueError):ij.NativeWorkflowProgram([step])
    with pytest.raises(ValueError):ij.prepare_native_workflow(owner.snapshot(),[step])
    invalid=ij.TargetHistogramOptions();invalid.bins=1
    step=ij.NativeWorkflowHistogram('h.csv',target,enabled=not disabled,authored_options=ij.NativeWorkflowHistogramOptions(invalid))
    with pytest.raises(ValueError):ij.prepare_native_workflow(owner.snapshot(),[step])
    assert reads.counts().plane_reads==0


def test_new_intent_requires_explicit_choices_and_raw_choices_have_no_range():
    owner,reads,target,_=fixture()
    with pytest.raises(ValueError):ij.NativeWorkflowProgram([ij.NativeWorkflowHistogram('h.csv',target)])
    step=ij.NativeWorkflowHistogram('raw.csv',target,authored_options=ij.NativeWorkflowHistogramOptions())
    p=ij.prepare_native_workflow(owner.snapshot(),[step]);value=p.histograms[0]['result']
    assert len(value.histogram['counts'])==256 and value.finite_count==12
    np.testing.assert_array_equal(value.histogram['counts'],np.histogram(np.arange(12),bins=np.linspace(0,12,257))[0])


def test_typed_authored_intent_runs_in_native_batch_and_setup(tmp_path):
    owner,_,target,_=fixture();source=tmp_path/'source.ijx';output=tmp_path/'out';output.mkdir()
    capture=ij.SessionCapture([owner.snapshot()]);capture.results=[{'kept':42}]
    ij.save_session(str(source),capture)
    program=ij.NativeWorkflowProgram([ij.NativeWorkflowHistogram('h.csv',target,authored_options=choices())])
    inputs=[ij.NativeWorkflowBatchInput(str(source),'authored',program=program)]
    setup=ij.NativeWorkflowBatchSetup(program,inputs,str(output))
    restored=ij.NativeWorkflowBatchSetup.from_json(setup.json())
    assert restored.common_program.record()==program.record()
    assert restored.inputs[0].program.record()==program.record()
    prepared=ij.prepare_native_workflow_batch(inputs,[],str(output));prepared.publish()
    rows=list(csv.DictReader(io.StringIO((output/'authored.d1.h.csv').read_text())))
    assert [int(r['count']) for r in rows]==[2,2,2,2,3]
    decoded=ij.load_session(str(output/'authored.ijx'));assert decoded.results==[{'kept':42}]
    np.testing.assert_array_equal(decoded.documents[0].snapshot().dataset.numpy('ZYX'),np.arange(12).reshape(3,2,2))


@pytest.mark.parametrize('log',[1,None,'false'])
def test_log_choice_is_strict_boolean(log):
    with pytest.raises(TypeError):ij.NativeWorkflowHistogramOptions(log_counts=log)
