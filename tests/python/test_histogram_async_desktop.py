"""Submission is nonblocking; only caller polling publishes results/defaults."""
import csv
import io
import json
import threading
import numpy as np
import pytest
import imagejxx as ij
from imagejxx import _desktop

def action(e,name,**p):e.action(name,json.dumps(p))
def setup(tmp_path):
    owner=ij.Document(ij.Dataset(np.arange(48,dtype=np.uint16).reshape(3,4,4),axes='ZYX'))
    e=_desktop.Editor(str(tmp_path/'prefs.json'));e.add_native_document(owner);e.recording=True
    action(e,'show_histogram');action(e,'configure_histogram',custom=True,bins='4',lower='0',upper='48',roi=False)
    return e,owner
def result_counts(e,path):
    action(e,'export_histogram_csv',path=str(path))
    return [int(r['count']) for r in csv.DictReader(io.StringIO(path.read_text()))]

@pytest.mark.parametrize('scientific',[False,True])
def test_submission_does_not_publish_until_poll(scientific,tmp_path):
    e,owner=setup(tmp_path)
    if scientific:action(e,'configure_histogram_target',target=dict(scope='all_z',roi='whole_plane'))
    action(e,'calculate_histogram')
    assert e.busy and json.loads(e.workflow)==[] and not (tmp_path/'prefs.json').exists()
    assert e.job_info.label=='Histogram' and e.job_info.document_id==owner.id
    action(e,'wait_histogram')
    assert not e.busy and len(json.loads(e.workflow))==1
    assert result_counts(e,tmp_path/'result.csv')==([12]*4 if scientific else [12,4,0,0])
    assert owner.undo_steps==0

@pytest.mark.parametrize('change',['cancel','generic_cancel','thread_cancel','draft','selection','metadata','close_panel','close_source'])
def test_stale_or_cancelled_ready_work_never_replaces_prior_output(change,tmp_path):
    e,owner=setup(tmp_path);action(e,'calculate_histogram');action(e,'wait_histogram')
    old=result_counts(e,tmp_path/'old.csv');prefs=(tmp_path/'prefs.json').read_bytes();workflow=json.loads(e.workflow)
    action(e,'configure_histogram',bins='6');action(e,'calculate_histogram')
    if change=='cancel':action(e,'cancel_histogram')
    if change=='generic_cancel':assert e.cancel_job(e.job_info.id)
    if change=='thread_cancel':
        t=threading.Thread(target=e.cancel);t.start();t.join()
    if change=='draft':action(e,'configure_histogram',bins='0')
    if change=='selection':owner.select(ij.Selection('rectangle',[(0,0),(1,4)]))
    if change=='metadata':
        edit=owner.begin_edit();edit.set_property('external',True);owner.commit(edit)
    if change=='close_panel':action(e,'close_histogram')
    if change=='close_source':action(e,'close_document',discard=True)
    action(e,'wait_histogram');assert not e.busy
    assert json.loads(e.workflow)==workflow and (tmp_path/'prefs.json').read_bytes()==prefs
    if change!='close_panel':assert result_counts(e,tmp_path/'after.csv')==old
    else:
        with pytest.raises(ValueError):result_counts(e,tmp_path/'absent.csv')

@pytest.mark.parametrize('initial',[False,True])
def test_recording_flag_is_captured_at_submission(initial,tmp_path):
    e,owner=setup(tmp_path);e.recording=initial;action(e,'calculate_histogram');e.recording=not initial
    action(e,'wait_histogram');assert len(json.loads(e.workflow))==int(initial)

@pytest.mark.parametrize('params',[dict(timeout_ms=True),dict(timeout_ms=0),dict(timeout_ms=60001),dict(extra=1)])
def test_wait_arguments_are_strict(params,tmp_path):
    e,owner=setup(tmp_path)
    with pytest.raises(ValueError):action(e,'wait_histogram',**params)

def test_second_submission_refuses_until_first_is_published(tmp_path):
    e,owner=setup(tmp_path);action(e,'calculate_histogram')
    with pytest.raises(ValueError):action(e,'calculate_histogram')
    action(e,'wait_histogram');assert len(json.loads(e.workflow))==1


def test_binding_activation_remains_guarded_while_busy(tmp_path):
    e,owner=setup(tmp_path);other=ij.Document(ij.Dataset(np.zeros((4,4),dtype=np.uint16),axes='YX'));e.add_native_document(other);e.active_index=0
    action(e,'calculate_histogram')
    with pytest.raises(ValueError,match='current edit'):e.active_index=1
    assert e.active_index==0 and json.loads(e.workflow)==[]
    action(e,'cancel_histogram');action(e,'wait_histogram');e.active_index=1
    assert json.loads(e.workflow)==[]


def test_live_navigation_does_not_retarget_the_worker(tmp_path):
    e,owner=setup(tmp_path);action(e,'calculate_histogram')
    view=owner.snapshot().display;view.crosshair=[0,0,2];owner.set_display(view)
    action(e,'wait_histogram')
    assert result_counts(e,tmp_path/'captured.csv')==[12,4,0,0] and owner.snapshot().display.crosshair[2]==2 and owner.undo_steps==0
