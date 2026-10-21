"""Real shared ImGui Jobs cancellation during native polygon rasterization."""
import json
import os
from pathlib import Path
import pytest
import imagejxx as ij
from imagejxx import _desktop

@pytest.fixture(scope='module')
def reports(tmp_path_factory):
    cpp=json.loads(Path(os.environ['IMAGEJXX_ROI_JOBS_CPP_REPORT']).read_text(encoding='utf-8'))
    directory=tmp_path_factory.mktemp('roi-jobs-ui')/'capture001'
    editor=_desktop.Editor(str(directory.parent/'preferences.json'))
    python=json.loads(editor._roi_import_jobs_ui_case(str(directory)))
    assert python==json.loads((directory/'case.json').read_text(encoding='utf-8'))
    return [cpp,python]

def rows(state):return state['metadata'].get('imagejxx.planar_annotations',{}).get('rows',[])

def test_actual_native_work_and_physical_jobs_controls(reports):
    for r,context in zip(reports,['imgui_cpp','imgui_python']):
        assert r['context']==context and r['contract']=='p06q.roi-import-jobs.ui.001'
        assert r['state']=='observed' and r['error']=='' and not r['acceptance_claim']
        assert r['fixture']=={'rows':8,'polygon_vertices':8192,'canvas':[32,4096],'injected_gate':False}
        assert all(f['events']==f['rendered']['input_frame']['raw_inputs'] for f in r['frames'])
        assert all(c['control']['id'] and c['control']['enabled'] for c in r['controls'])
        assert {'toggle Jobs','jobs_running_cancel','panel_running_cancel','jobs_ready_cancel','apply completed import'} <= {c['name'] for c in r['controls']}
        for key in ['running_observation','panel_running_observation']:
            assert r[key]['state']==int(ij.JobState.RUNNING)
            assert r[key]['stage']=='Area polygon rasterization' and r[key]['checkpoints']>0
        for job in r['load_jobs']:
            assert job['state']==int(ij.JobState.COMPLETED) and job['label']=='Load ImageJ ROI members'
            assert job['reservation_acquired'] and job['resource_bytes']>0

def test_running_cancellation_is_bounded_and_preserves_complete_document(reports):
    expected=[(x+3*y)%256 for y in range(4096) for x in range(32)]
    for r in reports:
        for key in ['jobs_running_cancel','panel_running_cancel']:
            c=r[key]
            assert c['at_press']['state']==int(ij.JobState.RUNNING) and not c['at_press']['cancel_requested']
            assert c['after_release']['cancel_requested']
            assert c['terminal']['state']==int(ij.JobState.CANCELED) and c['terminal']['cancel_requested']
            assert 0<=c['cancel_to_finalized_ms']<2000
            assert not c['panel']['ready'] and not c['panel']['worker_active']
            state=r['states'][key]
            assert rows(state)==[] and state['metadata'].get('imagejxx.imagej_roi_import_sources') is None
            assert state['undo']==0 and state['workflow']==[] and state['pixels']==expected
        assert r['jobs_running_cancel']['panel']['cancel_reason']=='job_canceled'
        assert r['panel_running_cancel']['panel']['cancel_reason']=='canceled'
        assert not r['panel_running_cancel']['panel']['visible']
        assert r['jobs_after_running_cancel']['usage']['reserved_bytes']==0

def test_cancel_ready_preview_then_retry_publishes_one_completed_job(reports):
    for r in reports:
        assert r['ready_review']['ready']
        canceled=r['jobs_ready_cancel'];assert canceled['at_press']['state']==int(ij.JobState.READY)
        assert canceled['terminal']['state']==int(ij.JobState.CANCELED)
        assert not canceled['panel']['ready'] and not canceled['panel']['controls']['apply']['enabled']
        assert rows(r['states']['jobs_ready_cancel'])==[] and r['states']['jobs_ready_cancel']['undo']==0
        applied=r['states']['applied'];assert len(rows(applied))==1 and applied['undo']==1 and len(applied['workflow'])==1
        assert rows(applied)[0]['name']=='small'
        ij.validate_imagej_roi_import_record(applied['workflow'][0])
        assert r['apply_job']['state']==int(ij.JobState.COMPLETED) and not r['apply_job']['cancel_requested']
        assert r['apply_job']['id']!=canceled['terminal']['id']
        assert applied['pixels']==r['states']['before']['pixels']

def test_failures_are_global_jobs_without_partial_publication_or_workspace_leaks(reports):
    for r in reports:
        assert r['failed_job']['state']==int(ij.JobState.FAILED) and r['failed_job']['error']
        assert r['failed']['error']==r['failed_job']['error'] and not r['failed']['ready']
        assert r['states']['failed']==r['states']['applied']
        final=r['final_jobs_panel'];assert final['visible']
        assert final['usage']==dict(running=0,queued=0,reserved_bytes=0,running_bytes=0,retained_bytes=0)
        observed={j['id']:j for j in final['jobs']}
        assert observed[r['failed_job']['id']]['state']=='Failed'
        assert observed[r['apply_job']['id']]['state']=='Completed'
        for key in ['jobs_running_cancel','panel_running_cancel','jobs_ready_cancel']:
            assert observed[r[key]['terminal']['id']]['state']=='Canceled'
            assert not observed[r[key]['terminal']['id']]['cancel']['enabled']
