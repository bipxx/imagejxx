"""Installed worker API: independent bin counts, lifecycle and owning copies."""
import gc
import threading
import weakref
import numpy as np
import pytest
import imagejxx as ij

def fixture(dtype=np.uint16,roi='whole'):
    reads=ij.DatasetReadObserver()
    values=np.arange(48,dtype=dtype).reshape(2,2,3,2,2)
    owner=ij.Document(ij.Dataset(values,axes='TCZYX').observe_reads(reads));edit=owner.begin_edit()
    edit.select(ij.Selection('rectangle',[(0,0),(1,2)]))
    edit.set_rois([ij.ROI('middle',ij.Selection('rectangle',[(0,0),(1,2)]),z=1)])
    owner.commit(edit)
    request=ij.TargetRequest(ij.TargetScope.RANGES,t=ij.AxisRange(0,2),c=ij.AxisRange(1,2),z=ij.AxisRange(0,3),roi={'whole':ij.RoiPolicy.WHOLE_PLANE,'active':ij.RoiPolicy.ACTIVE_SELECTION,'positioned':ij.RoiPolicy.POSITIONED_ROIS}[roi])
    options=ij.TargetHistogramOptions();options.binning=ij.HistogramBinning.UniformRange;options.bins=6;options.range=(0.,48.)
    command=ij.resolve_histogram_command(owner.snapshot(),options,request,True)
    members=[]
    for t in range(2):
        for z in range(3):
            if roi=='positioned' and z!=1:continue
            a=values[t,1,z];members.extend((a if roi=='whole' else a[:,:1]).ravel().tolist())
    return owner,reads,command,members

@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float32])
@pytest.mark.parametrize('roi',['whole','active','positioned'])
def test_worker_results_are_detached_and_readonly_to_source(dtype,roi):
    owner,reads,command,members=fixture(dtype,roi);before=owner.snapshot();n=reads.counts().plane_reads
    scheduler=ij.JobScheduler();estimate=ij.estimate_histogram_job(command)
    assert reads.counts().plane_reads==n and estimate.working_bytes>estimate.processing.working_bytes
    job=ij.submit_histogram(scheduler,command);assert job.wait(3000) and job.ready and job.info.state==ij.JobState.READY
    result=job.result;assert result.histogram['counts'].tolist()==np.histogram(members,np.linspace(0,48,7))[0].tolist()
    assert result.finite_count==len(members) and result.sum==sum(members) and result.mean==pytest.approx(np.mean(members))
    assert result.stddev==pytest.approx(np.std(members)) and reads.counts().plane_reads-n==6
    assert scheduler.usage.retained_bytes==estimate.reservation_bytes and job.info.document_id==owner.id
    assert job.info.reservation_acquired and job.info.peak_reserved_bytes==estimate.reservation_bytes
    job.complete();assert job.info.state==ij.JobState.COMPLETED and scheduler.usage.reserved_bytes==0
    # Python owns a full copy, rather than an alias of a cleaned native cell.
    assert result.histogram['counts'].sum()==len(members)
    with pytest.raises(ValueError):_ = job.result
    assert owner.snapshot().shares_data(before) and owner.undo_steps==1
    scheduler.shutdown()

def test_publication_is_on_caller_thread_and_can_retain_an_owning_result():
    owner,reads,command,members=fixture();scheduler=ij.JobScheduler();job=ij.submit_histogram(scheduler,command);job.wait()
    caller=threading.get_ident();saved=[]
    def publish(value):
        assert threading.get_ident()==caller and job.info.state==ij.JobState.PUBLISHING
        assert not job.cancel();saved.append(value)
    job.complete(publish);assert job.info.state==ij.JobState.COMPLETED and scheduler.usage.reserved_bytes==0
    scheduler.shutdown();del job,command,owner;gc.collect()
    assert saved[0].sum==sum(members) and saved[0].finite_count==len(members)

def test_python_callback_error_fails_job_without_retaining_its_traceback():
    owner,reads,command,members=fixture();scheduler=ij.JobScheduler();job=ij.submit_histogram(scheduler,command);job.wait()
    class Marker:pass
    token=Marker();reference=weakref.ref(token)
    def publish(value,captured=token):raise LookupError('publication probe')
    with pytest.raises(LookupError,match='publication probe'):job.complete(publish)
    assert job.info.state==ij.JobState.FAILED and 'publication probe' in job.info.error
    assert scheduler.usage.reserved_bytes==0
    with pytest.raises(RuntimeError,match='publication probe'):_ = job.result
    del publish,token;gc.collect();assert reference() is None
    scheduler.shutdown()

@pytest.mark.parametrize('mutation',['pixels','working','record','budget','shutdown'])
def test_admission_refuses_before_reads_or_queue_mutation(mutation):
    owner,reads,command,members=fixture();estimate=ij.estimate_histogram_job(command);limits=ij.HistogramCommandLimits()
    budget=estimate.reservation_bytes-1 if mutation=='budget' else 0;scheduler=ij.JobScheduler(ij.JobLimits(workers=1,memory_budget=budget))
    if mutation=='pixels':limits.processing.max_pixels=23
    if mutation=='working':limits.processing.max_working_bytes=estimate.working_bytes-1
    if mutation=='record':limits.max_record_bytes=1
    if mutation=='shutdown':scheduler.shutdown()
    n=reads.counts().plane_reads
    with pytest.raises(ValueError):ij.submit_histogram(scheduler,command,limits)
    assert reads.counts().plane_reads==n and scheduler.jobs==[] and scheduler.usage.reserved_bytes==0
    scheduler.shutdown()

def test_ready_cancellation_drops_native_result_and_python_copies_survive():
    owner,reads,command,members=fixture();scheduler=ij.JobScheduler();job=ij.submit_histogram(scheduler,command);job.wait()
    copy=job.result;assert job.cancel() and job.info.state==ij.JobState.CANCELED and scheduler.usage.reserved_bytes==0
    with pytest.raises(ij.OperationCancelled):_ = job.result
    with pytest.raises(ij.OperationCancelled):job.complete()
    assert copy.mean==pytest.approx(np.mean(members));scheduler.shutdown()

@pytest.mark.parametrize('timeout',[True,-1,1.5,'10'])
def test_wait_timeout_is_a_strict_nonnegative_integer(timeout):
    owner,reads,command,members=fixture();scheduler=ij.JobScheduler();job=ij.submit_histogram(scheduler,command)
    with pytest.raises((TypeError,ValueError,OverflowError)):job.wait(timeout)
    with pytest.raises((TypeError,ValueError,OverflowError)):job.wait_for(timeout)
    job.wait();job.cancel();scheduler.shutdown()

def test_dropping_job_releases_its_ready_reservation():
    owner,reads,command,members=fixture();scheduler=ij.JobScheduler();job=ij.submit_histogram(scheduler,command);job.wait()
    assert scheduler.usage.retained_bytes>0
    del job;gc.collect();assert scheduler.usage.reserved_bytes==0
    assert scheduler.jobs[-1].state==ij.JobState.CANCELED;scheduler.shutdown()

def test_invalid_publication_argument_does_not_change_ready_state():
    owner,reads,command,members=fixture();scheduler=ij.JobScheduler();job=ij.submit_histogram(scheduler,command);job.wait()
    with pytest.raises(TypeError):job.complete(7)
    assert job.info.state==ij.JobState.READY
    job.complete();scheduler.shutdown()

def test_explicit_over_reservation_and_job_label_are_honored():
    owner,reads,command,members=fixture();native=ij.estimate_histogram_job(command);options=ij.JobOptions(label='pooled histogram µ',resource_bytes=native.reservation_bytes+8192)
    estimate=ij.estimate_histogram_job(command,job_options=options);scheduler=ij.JobScheduler()
    job=ij.submit_histogram(scheduler,command,job_options=options);options.label='changed';options.resource_bytes=0
    job.wait();assert job.info.label=='pooled histogram µ' and job.info.resource_bytes==estimate.reservation_bytes
    job.complete();scheduler.shutdown()

def test_quiet_nan_worker_preserves_source_payload_bits():
    bits=np.array([[0x7fc12345,0x80000000,0x3f800000,0xffc54321]],dtype=np.uint32)
    data=ij.Dataset(bits.view(np.float32),axes='YX',sample_policy=ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32)
    owner=ij.Document(data);command=ij.resolve_histogram_command(owner.snapshot());scheduler=ij.JobScheduler()
    job=ij.submit_histogram(scheduler,command);job.wait();result=job.result;job.complete()
    assert result.finite_count==result.nan_count==2 and result.mean==.5 and result.sum==1.
    assert data.numpy('YX').view(np.uint32).tobytes()==bits.tobytes();scheduler.shutdown()
