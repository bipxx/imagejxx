"""Unbinned batch summaries and CSV publication, independent of bin centers."""
import copy
import csv
import io
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij


def fixture(tmp_path, *, roi=False):
    a=ij.Document(ij.Dataset(np.arange(12,dtype=np.uint16).reshape(3,2,2),axes='ZYX'))
    if roi:
        e=a.begin_edit();e.select(ij.Selection('rectangle',[(0,0),(1,2)]));a.commit(e)
    b=ij.Document(ij.Dataset(np.full((2,2),17,np.uint16),axes='YX'))
    capture=ij.SessionCapture([a.snapshot(),b.snapshot()]);capture.results=[{'kept':42}];capture.workflow=[{'do_not_execute':True}]
    paths=[tmp_path/'first.ijx',tmp_path/'second.ijx']
    for p in paths:ij.save_session(str(p),capture)
    target=ij.TargetRequest(ij.TargetScope.ALL_Z,roi=ij.RoiPolicy.ACTIVE_SELECTION if roi else ij.RoiPolicy.WHOLE_PLANE)
    options=ij.TargetHistogramOptions();options.binning=ij.HistogramBinning.UniformRange;options.bins=1;options.range=(0.,10.)
    choices=ij.NativeWorkflowHistogramOptions(options,False)
    steps=[ij.NativeWorkflowHistogram('before.csv',target,authored_options=choices),
        ij.NativeWorkflowOperation('add',{'value':5},target),
        ij.NativeWorkflowHistogram('after.csv',target,authored_options=choices)]
    inputs=[ij.NativeWorkflowBatchInput(str(p),stem) for p,stem in zip(paths,['one','two'])]
    output=tmp_path/'out';output.mkdir()
    return a,capture,paths,inputs,output,steps


@pytest.mark.parametrize('all_documents',[False,True])
@pytest.mark.parametrize('export',[False,True])
def test_statistics_are_unbinned_ordered_detached_and_opt_in(tmp_path,all_documents,export):
    owner,capture,paths,inputs,out,steps=fixture(tmp_path)
    originals=[p.read_bytes() for p in paths];source=owner.snapshot()
    batch=ij.prepare_native_workflow_batch(inputs,steps,str(out),ij.NativeWorkflowBatchOptions(all_documents=all_documents,histogram_statistics=export))
    rows=batch.histograms
    assert len(rows)==(8 if all_documents else 4)
    assert rows[0] is not batch.histograms[0]
    assert [(r.input_index,r.document_index,r.step_index) for r in rows]==[(i,d,s) for i in range(2) for d in range(2 if all_documents else 1) for s in (0,2)]
    for r in rows:
        values=np.arange(12,dtype=float) if r.document_index==0 else np.full(4,17.)
        if r.step_index==2:values+=5
        assert r.plane_count==(3 if r.document_index==0 else 1)
        assert r.pixel_type=='uint16' and r.sample_policy=='finite_only' and not r.log_counts
        assert r.finite_count==r.visited_count==len(values) and r.nan_count==0
        assert r.included_count==sum((values>=0)&(values<=10))
        assert r.overflow_count==sum(values>10) and r.underflow_count==0
        assert r.minimum==values.min() and r.maximum==values.max() and r.sum==values.sum()
        assert r.mean==pytest.approx(values.mean()) and r.stddev==pytest.approx(values.std())
    assert rows[0].mean==5.5 and rows[0].stddev>3 and rows[0].included_count==11
    rows[0].options.bins=99;assert batch.histograms[0].options.bins==1
    text=ij.native_workflow_batch_histogram_csv(rows);table=list(csv.DictReader(io.StringIO(text)))
    assert len(table)==len(rows) and table[0]['mean']=='5.5' and table[0]['finite_count']=='12'
    assert float(table[0]['stddev'])==pytest.approx(np.arange(12).std())
    assert not any(Path(o['path']).exists() for o in batch.outputs)
    batch.publish()
    expected_size=sum(Path(o['path']).stat().st_size for o in batch.outputs)
    assert batch.encoded_bytes==expected_size
    assert sum(o['histogram_statistics'] for o in batch.outputs)==(2 if export else 0)
    for i,stem in enumerate(['one','two']):
        stats=out/(stem+'.histograms.csv');assert stats.exists()==export
        if export:assert stats.read_text()==ij.native_workflow_batch_histogram_csv([r for r in rows if r.input_index==i])
        decoded=ij.load_session(str(out/(stem+'.ijx')))
        assert decoded.results==capture.results and decoded.workflow==capture.workflow
    assert [p.read_bytes() for p in paths]==originals and owner.snapshot().shares_data(source)
    del batch
    assert rows[0].mean==5.5 and rows[-1].sum==(88 if all_documents else 126)


def test_roi_statistics_use_members_not_plane_extrema_or_bin_centers(tmp_path):
    _,_,_,inputs,out,steps=fixture(tmp_path,roi=True)
    batch=ij.prepare_native_workflow_batch(inputs[:1],steps,str(out),ij.NativeWorkflowBatchOptions(histogram_statistics=True))
    first,last=batch.histograms
    assert first.finite_count==6 and first.minimum==0 and first.maximum==10 and first.sum==30
    assert first.mean==5 and first.stddev==pytest.approx(np.arange(0,12,2).std())
    assert last.mean==10 and last.maximum==15 and last.sum==60
    assert last.included_count==3 and last.overflow_count==3


@pytest.mark.parametrize('empty',[False,True])
def test_quiet_nan_and_empty_populations_export_absent_statistics(tmp_path,empty):
    raw=np.array([0x7fc12345,0x3f800000,0x7fc23456,0x40400000],np.uint32)
    if empty:raw[:]=0x7fc12345
    owner=ij.Document(ij.Dataset(raw.view(np.float32).reshape(2,2),axes='YX',sample_policy=ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32))
    source=tmp_path/'q.ijx';ij.save_session(str(source),ij.SessionCapture([owner.snapshot()]));out=tmp_path/'out';out.mkdir()
    options=ij.TargetHistogramOptions();options.binning=ij.HistogramBinning.UniformRange;options.bins=1;options.range=(0.,2.)
    step=ij.NativeWorkflowHistogram('q.csv',ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE),authored_options=ij.NativeWorkflowHistogramOptions(options))
    batch=ij.prepare_native_workflow_batch([ij.NativeWorkflowBatchInput(str(source),'q')],[step],str(out),ij.NativeWorkflowBatchOptions(histogram_statistics=True))
    row=batch.histograms[0];assert row.visited_count==4 and row.nan_count==(4 if empty else 2)
    text=list(csv.DictReader(io.StringIO(ij.native_workflow_batch_histogram_csv([row]))))[0]
    if empty:
        assert row.mean is row.stddev is row.minimum is row.maximum is None and row.sum==0
        assert all(text[k]=='' for k in ['minimum','maximum','mean','stddev'])
    else:assert row.mean==2 and row.stddev==1 and row.minimum==1 and row.maximum==3 and row.sum==4
    batch.publish();decoded=ij.load_session(str(out/'q.ijx'))
    np.testing.assert_array_equal(decoded.documents[0].snapshot().dataset.numpy('YX').view(np.uint32).ravel(),raw)


@pytest.mark.parametrize('race',[False,True])
def test_statistics_destination_conflict_keeps_foreign_file_and_partial_policy(tmp_path,race):
    owner,_,paths,inputs,out,steps=fixture(tmp_path);source=owner.snapshot();original=[p.read_bytes() for p in paths]
    def prepare():return ij.prepare_native_workflow_batch(inputs,steps,str(out),ij.NativeWorkflowBatchOptions(histogram_statistics=True))
    if race:batch=prepare()
    foreign=out/'one.histograms.csv';foreign.write_bytes(b'foreign winner')
    with pytest.raises((ValueError,RuntimeError)):
        if race:batch.publish()
        else:prepare()
    assert foreign.read_bytes()==b'foreign winner' and not (out/'one.ijx').exists() and not (out/'two.ijx').exists()
    if race:
        assert [i['state'] for i in batch.items]==[ij.NativeWorkflowBatchState.PARTIAL,ij.NativeWorkflowBatchState.SKIPPED]
        assert (out/'one.d1.before.csv').exists() and (out/'one.d1.after.csv').exists()
        assert len(batch.histograms)==4
        with pytest.raises(ValueError):batch.publish()
    else:assert list(out.iterdir())==[foreign]
    assert owner.snapshot().shares_data(source) and [p.read_bytes() for p in paths]==original


@pytest.mark.parametrize('limit',['outputs','memory','csv'])
def test_limits_refuse_without_publication(tmp_path,limit):
    _,_,_,inputs,out,steps=fixture(tmp_path)
    if limit=='csv':
        batch=ij.prepare_native_workflow_batch(inputs[:1],steps,str(out))
        with pytest.raises(ValueError):ij.native_workflow_batch_histogram_csv(batch.histograms,max_working_bytes=1)
        batch.abandon()
    else:
        options=ij.NativeWorkflowBatchOptions(histogram_statistics=True,**({'max_outputs':3} if limit=='outputs' else {'max_working_bytes':1}))
        with pytest.raises(ValueError):ij.prepare_native_workflow_batch(inputs[:1],steps,str(out),options)
    assert list(out.iterdir())==[]


@pytest.mark.parametrize('when',['before','prepared'])
def test_cancel_discards_all_private_statistics_outputs(tmp_path,when):
    _,_,_,inputs,out,steps=fixture(tmp_path);token=ij.CancellationToken()
    if when=='before':token.cancel()
    def progress(index,state):
        if state==ij.NativeWorkflowBatchState.PREPARED:token.cancel()
    with pytest.raises(RuntimeError):ij.prepare_native_workflow_batch(inputs,steps,str(out),ij.NativeWorkflowBatchOptions(histogram_statistics=True),cancellation=token,progress=progress)
    assert list(out.iterdir())==[]


def test_setup_v2_roundtrip_resets_consent_and_v1_remains_unchanged(tmp_path):
    _,_,_,inputs,out,steps=fixture(tmp_path);program=ij.NativeWorkflowProgram(steps)
    v1=ij.NativeWorkflowBatchSetup(program,inputs,str(out));assert v1.record()['version']==1 and not v1.histogram_statistics
    v2=v1.with_histogram_statistics(True);assert v2.record()['version']==2 and v2.record()['histogram_statistics'] is True
    assert v1.record()['version']==1
    restored=ij.NativeWorkflowBatchSetup.from_json(v2.json());assert restored.histogram_statistics and restored.record()==v2.record()
    assert restored.with_histogram_statistics(False).record()==v1.record()
    assert ij.NativeWorkflowBatchSetup(program,inputs,histogram_statistics=True).histogram_statistics


@pytest.mark.parametrize('mutation',[lambda r:r.update(histogram_statistics=1),lambda r:r.pop('histogram_statistics'),lambda r:r.update(version=3),lambda r:r.update(version=1),lambda r:r.update(extra=1)])
def test_closed_setup_v2(mutation,tmp_path):
    _,_,_,inputs,out,steps=fixture(tmp_path);wire=ij.NativeWorkflowBatchSetup(ij.NativeWorkflowProgram(steps),inputs,str(out),histogram_statistics=True).record();mutation(wire)
    with pytest.raises((ValueError,KeyError)):ij.NativeWorkflowBatchSetup.from_record(wire)


@pytest.mark.parametrize('flag',[1,None,'true'])
def test_export_flag_is_strict_boolean(flag):
    with pytest.raises(TypeError):ij.NativeWorkflowBatchOptions(histogram_statistics=flag)


def test_no_histograms_produces_no_empty_statistics_file(tmp_path):
    _,_,_,inputs,out,steps=fixture(tmp_path)
    batch=ij.prepare_native_workflow_batch(inputs[:1],[steps[1]],str(out),ij.NativeWorkflowBatchOptions(histogram_statistics=True))
    assert batch.histograms==[] and len(batch.outputs)==1 and batch.outputs[0]['session']
    batch.publish();assert {p.name for p in out.iterdir()}=={'one.ijx'}
