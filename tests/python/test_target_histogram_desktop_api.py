import csv
import json
import numpy as np
import pytest
import imagejxx as ij
from imagejxx import _desktop
from test_target_histogram import fixture
from test_target_histogram_ui import expected


def action(editor,name,**params):editor.action(name,json.dumps(params))

def grid(policy):return dict(scope='ranges',t=dict(start=0,stop=2),c=dict(start=1,stop=2),z=dict(start=1,stop=4,step=2),roi={'whole':'whole_plane','active':'active_selection','positioned':'positioned_rois'}[policy])

@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float32])
@pytest.mark.parametrize('policy',['whole','active','positioned'])
@pytest.mark.parametrize('custom',[False,True])
def test_configured_grid_calculation_and_detached_export(dtype,policy,custom,tmp_path):
    owner,reads,target,pixels,values=fixture(dtype,policy);editor=_desktop.Editor(str(tmp_path/'prefs.json'));editor.add_native_document(owner);before=owner.snapshot();n=reads.counts().plane_reads
    action(editor,'show_histogram');action(editor,'configure_histogram_target',target=grid(policy));assert reads.counts().plane_reads==n
    if custom:
        low=2056 if dtype==np.uint16 else -6 if dtype==np.float32 else 8;high=12336 if dtype==np.uint16 else 4 if dtype==np.float32 else 48
        action(editor,'configure_histogram',custom=True,bins='8',lower=str(low),upper=str(high))
    display=owner.snapshot().display;display.t=1;display.c=0;display.crosshair=[0,0,2];owner.set_display(display)
    action(editor,'calculate_histogram');assert reads.counts().plane_reads-n==(4 if custom or dtype==np.uint8 else 8)
    path=tmp_path/'pooled.csv';action(editor,'export_histogram_csv',path=str(path));rows=list(csv.DictReader(path.open()))
    counts,edges=expected(values.astype(np.float64),{np.uint8:'u8',np.uint16:'u16',np.float32:'f32'}[dtype],custom)
    assert [int(r['count']) for r in rows]==counts and [float(r['lower']) for r in rows]==edges[:-1] and [float(r['upper']) for r in rows]==edges[1:]
    action(editor,'configure_histogram',scientific=False);second=tmp_path/'retained.csv';action(editor,'export_histogram_csv',path=str(second));assert second.read_bytes()==path.read_bytes()
    assert owner.snapshot().shares_data(before) and owner.undo_steps==0

@pytest.mark.parametrize('target',[None,dict(extra='x'),dict(scope='positions',positions=[[0,0,0]]),dict(mode='volume'),dict(destination='new_document'),dict(scope='ranges',z=dict(start=2,stop=2)),dict(scope='ranges',z=dict(step=0)),dict(scope='ranges',z=dict(stop=5)),dict(scope='ranges',t=dict(start=-1)),dict(scope='ranges',c=dict(start=True)),dict(scope='ranges',z=dict(unknown=2)),dict(roi='bad'),dict(z=dict(start=1))])
def test_bad_configured_target_is_atomic_and_does_not_read(target,tmp_path):
    owner,reads,_,_,_=fixture();editor=_desktop.Editor(str(tmp_path/'prefs.json'));editor.add_native_document(owner);action(editor,'show_histogram');action(editor,'configure_histogram_target',target=grid('whole'));n=reads.counts().plane_reads
    with pytest.raises((ValueError,IndexError)):action(editor,'configure_histogram_target',target=target)
    assert reads.counts().plane_reads==n
    action(editor,'calculate_histogram');assert reads.counts().plane_reads-n==4
    path=tmp_path/'unchanged-target.csv';action(editor,'export_histogram_csv',path=str(path));rows=list(csv.DictReader(path.open()));assert sum(int(r['count']) for r in rows)==64 and owner.undo_steps==0


def test_inactive_basic_fields_do_not_override_scientific_target(tmp_path):
    owner,reads,_,_,_=fixture();editor=_desktop.Editor(str(tmp_path/'prefs.json'));editor.add_native_document(owner);action(editor,'show_histogram');action(editor,'configure_histogram_target',target=grid('positioned'))
    action(editor,'configure_histogram',t='0',z='invalid',roi=False);action(editor,'calculate_histogram');path=tmp_path/'pooled.csv';action(editor,'export_histogram_csv',path=str(path));assert sum(int(r['count']) for r in csv.DictReader(path.open()))==40
    action(editor,'configure_histogram',scientific=False);n=reads.counts().plane_reads
    with pytest.raises(ValueError):action(editor,'calculate_histogram')
    assert reads.counts().plane_reads==n
    retained=tmp_path/'retained.csv';action(editor,'export_histogram_csv',path=str(retained));assert retained.read_bytes()==path.read_bytes()


def test_unmatched_positioned_target_is_empty_and_stale_refusal_preserves_export(tmp_path):
    owner,reads,_,_,_=fixture(np.uint16);editor=_desktop.Editor(str(tmp_path/'prefs.json'));editor.add_native_document(owner);action(editor,'show_histogram')
    target=dict(scope='ranges',t=dict(start=1,stop=2),c=dict(start=1,stop=2),z=dict(start=3,stop=4),roi='positioned_rois')
    action(editor,'configure_histogram_target',target=target);n=reads.counts().plane_reads;action(editor,'calculate_histogram');assert reads.counts().plane_reads==n+1
    path=tmp_path/'empty.csv';action(editor,'export_histogram_csv',path=str(path));rows=list(csv.DictReader(path.open()));assert len(rows)==256 and sum(int(r['count']) for r in rows)==0
    edit=owner.begin_edit();edit.set_property('external_histogram_edit',True);owner.commit(edit);n=reads.counts().plane_reads
    with pytest.raises(RuntimeError):action(editor,'calculate_histogram')
    assert reads.counts().plane_reads==n
    retained=tmp_path/'retained.csv';action(editor,'export_histogram_csv',path=str(retained));assert retained.read_bytes()==path.read_bytes()
