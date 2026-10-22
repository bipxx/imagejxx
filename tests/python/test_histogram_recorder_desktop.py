"""Recorded panel analysis and failure boundaries through the installed host."""
import csv
import io
import json
import numpy as np
import pytest
import imagejxx as ij
from imagejxx import _desktop

def action(e,name,**p):
    e.action(name,json.dumps(p))
    if name in ["calculate_histogram","replay_histogram"]:e.action("wait_histogram","{}")
def workflow(e):return json.loads(e.workflow)
def counts(e,path):
    action(e,'export_histogram_csv',path=str(path))
    return [int(row['count']) for row in csv.DictReader(io.StringIO(path.read_text()))]
def source(base=0):
    observer=ij.DatasetReadObserver()
    owner=ij.Document(ij.Dataset((np.arange(24,dtype=np.uint16)+base).reshape(3,2,4),axes='ZYX').observe_reads(observer))
    edit=owner.begin_edit();edit.select(ij.Selection('rectangle',[(0,0),(1,2)]))
    edit.set_rois([ij.ROI('middle',ij.Selection('rectangle',[(0,0),(2,2)]),z=1)])
    owner.commit(edit)
    return owner,observer
def setup(tmp_path):
    owner,observer=source();e=_desktop.Editor(str(tmp_path/'prefs.json'));e.add_native_document(owner)
    e.recording=True;action(e,'show_histogram');action(e,'configure_histogram',custom=True,bins='6',lower='0',upper='24',roi=False,log=True)
    return e,owner,observer

@pytest.mark.parametrize('scientific',[False,True])
@pytest.mark.parametrize('policy',['whole_plane','active_selection','positioned_rois'])
def test_record_save_load_strict_replay_and_frozen_navigation(scientific,policy,tmp_path):
    e,owner,reads=setup(tmp_path)
    if scientific:action(e,'configure_histogram_target',target=dict(scope='all_z',roi=policy))
    else:action(e,'configure_histogram',roi=policy!='whole_plane')
    action(e,'calculate_histogram');steps=workflow(e);assert len(steps)==1
    record=steps[0];ij.validate_histogram_command_record(record)
    assert record['options']==dict(binning='uniform_range',bins=6,range=[0.,24.]) and record['log_counts']
    expected=counts(e,tmp_path/'before.csv');prefs=(tmp_path/'prefs.json').read_bytes()
    path=tmp_path/'workflow.json';action(e,'save_workflow',path=str(path));e.workflow='[]';action(e,'load_workflow',path=str(path));assert workflow(e)==steps
    view=owner.snapshot().display;view.crosshair=[0,0,2];owner.set_display(view)
    n=reads.counts().plane_reads;action(e,'replay_histogram',record=record)
    assert counts(e,tmp_path/'after.csv')==expected and (tmp_path/'prefs.json').read_bytes()==prefs
    assert reads.counts().plane_reads-n==(3 if scientific else 1)
    assert workflow(e)==[record,record] and owner.undo_steps==1
    assert owner.snapshot().display.crosshair[2]==2

@pytest.mark.parametrize('failure',['wrong_owner','stale','malformed','extra','out_of_bounds'])
def test_replay_failure_keeps_result_workflow_preferences_and_reads(failure,tmp_path):
    e,owner,reads=setup(tmp_path);action(e,'calculate_histogram');record=workflow(e)[0]
    expected=counts(e,tmp_path/'before.csv');prefs=(tmp_path/'prefs.json').read_bytes();params=dict(record=record)
    if failure=='wrong_owner':
        other,other_reads=source(40);e.add_native_document(other)
    elif failure=='stale':owner.select(ij.Selection('rectangle',[(1,0),(2,2)]));record['target']['roi_policy']='active_selection'
    elif failure=='malformed':record['options']['bins']=True
    elif failure=='extra':params['extra']=1
    else:params['histogram_rebind']=dict(scope='positions',positions=[[0,0,99]],roi='whole_plane')
    before=workflow(e);n=reads.counts().plane_reads
    with pytest.raises((ValueError,RuntimeError,IndexError)):action(e,'replay_histogram',**params)
    assert workflow(e)==before and counts(e,tmp_path/'after.csv')==expected
    assert reads.counts().plane_reads==n and (tmp_path/'prefs.json').read_bytes()==prefs

@pytest.mark.parametrize('policy',['whole_plane','active_selection','positioned_rois'])
def test_rebind_review_keeps_numeric_intent_and_uses_destination_geometry(policy,tmp_path):
    e,owner,reads=setup(tmp_path);action(e,'calculate_histogram');record=workflow(e)[0]
    old=counts(e,tmp_path/'old.csv');other,other_reads=source(40);e.add_native_document(other)
    before=other.snapshot();n=other_reads.counts().plane_reads
    action(e,'show_histogram_rebind',record=record)
    assert other_reads.counts().plane_reads==n and counts(e,tmp_path/'retained.csv')==old
    with pytest.raises(ValueError):action(e,'configure_histogram',bins='2')
    action(e,'configure_histogram_target',target=dict(scope='all_z',roi=policy));action(e,'calculate_histogram')
    rebound=workflow(e)[-1];assert rebound['options']==record['options'] and rebound['log_counts']
    assert rebound['target']['document_id']==other.id and rebound['target']['roi_policy']==policy
    result=ij.execute_histogram_command(ij.histogram_command_from_record(other.snapshot(),rebound))
    assert result.finite_count=={'whole_plane':24,'active_selection':6,'positioned_rois':4}[policy]
    assert counts(e,tmp_path/'new.csv')==[0]*6 and other.snapshot().shares_data(before) and other.undo_steps==1

def test_record_and_defaults_failure_preserves_previous_result(tmp_path):
    e,owner,reads=setup(tmp_path);action(e,'calculate_histogram');before=workflow(e);old=counts(e,tmp_path/'before.csv')
    saved=e.settings();path=tmp_path/'prefs.json';path.unlink();path.mkdir()
    action(e,'configure_histogram',bins='3')
    with pytest.raises(RuntimeError):action(e,'calculate_histogram')
    assert workflow(e)==before and e.settings()==saved and counts(e,tmp_path/'after.csv')==old

@pytest.mark.parametrize('operation',['run_workflow','batch','native_batch'])
def test_run_all_opens_histogram_review_without_processing(operation,tmp_path):
    e,owner,reads=setup(tmp_path);action(e,'calculate_histogram');before=workflow(e);n=reads.counts().plane_reads
    action(e,operation)
    assert workflow(e)==before and reads.counts().plane_reads==n and owner.undo_steps==1

def test_recording_off_and_programmatic_explicit_rebind(tmp_path):
    e,owner,reads=setup(tmp_path);action(e,'calculate_histogram');record=workflow(e)[0];e.recording=False
    view=owner.snapshot().display;view.crosshair=[0,0,2];owner.set_display(view)
    action(e,'replay_histogram',record=record,histogram_rebind=dict(scope='current_plane',roi='whole_plane'))
    assert counts(e,tmp_path/'rebound.csv')==[0,0,0,0,4,4] and workflow(e)==[record]
