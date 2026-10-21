"""Physical queued ROI cancellation under declared native scheduler contention."""
import json
import os
from pathlib import Path
import pytest
import imagejxx as ij
from imagejxx import _desktop

@pytest.fixture(scope='module')
def reports(tmp_path_factory):
    cpp=json.loads(Path(os.environ['IMAGEJXX_ROI_QUEUE_CPP_REPORT']).read_text(encoding='utf-8'))
    directory=tmp_path_factory.mktemp('roi-queue-ui')/'capture001'
    editor=_desktop.Editor(str(directory.parent/'preferences.json'))
    python=json.loads(editor._roi_import_queue_ui_case(str(directory)))
    assert python==json.loads((directory/'case.json').read_text(encoding='utf-8'))
    return [cpp,python]

def rows(state):return state['metadata'].get('imagejxx.planar_annotations',{}).get('rows',[])

def test_actual_queued_jobs_never_acquire_workspace_or_execute(reports):
    for r,context in zip(reports,['imgui_cpp','imgui_python']):
        assert r['contract']=='p06q.roi-import-queue.ui.001' and r['context']==context
        assert r['state']=='observed' and r['error']=='' and not r['acceptance_claim']
        assert 'not a stalled ROI read' in r['contention_scope']
        assert all(f['events']==f['rendered']['input_frame']['raw_inputs'] for f in r['frames'])
        assert all(c['control']['id'] and c['control']['enabled'] for c in r['controls'])
        for key in ['queued_load','queued_validation','source_closed','draft_reopened','queued_workflow']:
            case=r[key];queued=case['queued'];terminal=case['terminal']
            assert queued['state']==int(ij.JobState.QUEUED) and terminal['state']==int(ij.JobState.CANCELED)
            assert not queued['cancel_requested'] and terminal['cancel_requested']
            assert queued['id']==terminal['id']!=case['blocker']['id']
            assert queued['checkpoints']==terminal['checkpoints']==0
            assert not queued['reservation_acquired'] and not terminal['reservation_acquired']
            assert queued['peak_reserved_bytes']==terminal['peak_reserved_bytes']==0
            assert case['blocker']['state']==int(ij.JobState.RUNNING) and case['blocker']['document_id']==0
            assert case['usage']['running']==case['usage']['queued']==1
            assert case['usage']['reserved_bytes']==case['blocker']['resource_bytes']>0
            assert case['panel_before']['worker_active'] and not case['panel_before']['ready']

def test_jobs_and_form_cancel_queued_work_without_document_changes(reports):
    for r in reports:
        assert r['queued_load']['queued']['label']=='Load ImageJ ROI members'
        assert r['queued_validation']['queued']['label']=='Prepare ImageJ ROI preview'
        assert r['queued_workflow']['queued']['label']=='Admit captured ROI import'
        assert r['queued_workflow']['panel_after']['controls']['retry_inputs']['enabled']
        assert r['queued_load']['panel_after']['cancel_reason']=='job_canceled'
        assert not r['queued_validation']['panel_after']['visible']
        assert r['queued_validation']['panel_after']['cancel_reason']=='canceled'
        for key in ['before','queued_load','queued_validation']:
            s=r['states'][key];assert rows(s)==[] and 'imagejxx.imagej_roi_import_sources' not in s['metadata']
            assert s['undo']==0 and s['workflow']==[]
        assert r['queued_load']['still_blocked']['reserved_bytes']>0
        assert not any(r['queued_load']['released'].values())
        assert {'cancel queued load in Jobs','cancel queued validation in form','cancel queued workflow in Jobs'} <= {c['name'] for c in r['controls']}

def test_queued_source_close_and_same_owner_reopen_do_not_publish_obsolete_payloads(reports):
    for r in reports:
        closed=r['source_closed'];assert closed['removed_source_unchanged']
        assert not closed['panel_after']['visible'] and closed['panel_after']['cancel_reason']=='source_changed'
        reopened=r['draft_reopened']['after_release']
        assert reopened['visible'] and not reopened['worker_active'] and not reopened['ready']
        assert reopened['path']=='' and reopened['entries']==[] and reopened['error']==''
        assert r['source_closed']['queued']['document_id']!=r['states']['applied']['id']
        assert r['draft_reopened']['queued']['document_id']==r['states']['applied']['id']
        for key in ['source_closed','draft_reopened','queued_workflow','final']:
            assert r['states'][key]==r['states']['applied']

def test_retry_succeeds_once_and_all_fixture_and_roi_resources_finish(reports):
    expected=[10*y+x for y in range(9) for x in range(13)]
    for r in reports:
        applied=r['states']['applied'];assert len(rows(applied))==1 and applied['undo']==1 and len(applied['workflow'])==1
        assert rows(applied)[0]['name']=='queued'
        ij.validate_imagej_roi_import_record(applied['workflow'][0])
        assert r['apply_job']['state']==int(ij.JobState.COMPLETED) and r['apply_job']['reservation_acquired']
        assert r['workflow_retry']['workflow_rebind'] and len(r['workflow_retry']['entries'])==1
        assert not r['workflow_retry']['worker_active'] and not r['workflow_retry']['destination_confirmed']
        assert all(s['pixels']==expected for s in r['states'].values())
        assert not any(r['final_usage'].values())
        assert all(j['state'] in [int(ij.JobState.COMPLETED),int(ij.JobState.CANCELED)] for j in r['all_jobs'])
        blockers=[j for j in r['all_jobs'] if j['label']=='Owned queue contention fixture']
        assert len(blockers)==5 and all(j['state']==int(ij.JobState.COMPLETED) for j in blockers)
