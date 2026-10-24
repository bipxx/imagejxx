"""Independent native owner/pixel/lease checks for real path UI lifecycle input."""
import copy
import json
import os
import struct
from pathlib import Path
import pytest
from test_path_filter_ui import PATH, retained, reference, expected_words, rank_expected
from test_path_specialized_filter_ui import samples

KINDS=('supersede','invalid-editor','owner-replace','selection-revision','pixel-revision','calibration-revision')
CASES=[f'lifecycle-{operation}-{kind}' for operation in ('gaussian','rank-mean') for kind in KINDS]
CASES+=['lifecycle-gaussian-invalid-external']
BASE={'before','source_saved','drawn'}
STATES={
 'supersede':{'running','middle_requested','latest_requested','latest_ready','cancelled'},
 'invalid-editor':{'running','invalid_running','old_settled','ready_recovery','invalid_ready','recovered','cancelled'},
 'invalid-external':{'ready','external_refused','cancelled'},
 'owner-replace':{'running','owner_replaced','old_settled','owner_restored','recovered','cancelled'},
 'selection-revision':{'running','selection_changed','old_settled','selection_restored','recovered','cancelled'},
 'pixel-revision':{'running','pixel_changed','old_settled','recovered','cancelled'},
 'calibration-revision':{'running','calibration_changed','old_settled','recovered','cancelled'},
}


def words_from_le(value):
    return ['0x'+bytes.fromhex(value[i:i+8])[::-1].hex() for i in range(0,len(value),8)]


def filter_words(rank,value):
    return words_from_le(rank_expected('mean',value) if rank else expected_words(value))


def displayed_samples(observation,words):
    value=observation['ordinary_pixels']['readback']['source']
    assert value['dtype']=='float32' and value['shape']==[17,23,1]
    assert len(value['pixels'])==391
    actual=''.join(struct.pack('<f',sample).hex() for sample in value['pixels'])
    expected=''.join(bytes.fromhex(word[2:])[::-1].hex() for word in words)
    assert actual==expected


def owner(row,name):
    docs=row['document_states'][name]
    found=[state for state in docs['documents'] if state['document_id']==row['source_document_id']]
    assert len(found)==1 and len(docs['documents']) in (1,2)
    assert len(docs['document_ids'])==len(set(docs['document_ids']))==len(docs['documents'])
    return found[0]


def actual_job(observation,job_id):
    found=[job for job in observation['actual_native_jobs'] if job['id']==job_id]
    assert len(found)==1
    return found[0]


@pytest.mark.parametrize('case_id',CASES)
@pytest.mark.parametrize('context',['imgui_cpp','imgui_python'])
def test_actual_path_filter_lifecycle_generations_mutations_and_release(case_id,context,tmp_path):
    if context=='imgui_cpp':
        path=Path(json.loads(os.environ['IMAGEJXX_PATH_FILTER_CPP_REPORTS'])[case_id])
        assert path.stat().st_size < 16*1024*1024
        row=json.loads(path.read_bytes())
    else:
        from imagejxx import _desktop
        editor=_desktop.Editor(preferences_path=str(tmp_path/'preferences.json'))
        row=editor._path_filter_ui_case(case_id,str(tmp_path/'capture'))
        if isinstance(row,str):row=json.loads(row)
    assert row['contract']=='p13i.path-filter.ui.001' and row['state']=='observed'
    assert row['error']=='' and row['acceptance_claim'] is False
    assert row['case_id']==case_id and row['context']==context
    rank=case_id.startswith('lifecycle-rank-mean-')
    kind=case_id.removeprefix('lifecycle-rank-mean-' if rank else 'lifecycle-gaussian-')
    latest=2.5 if rank else 12
    assert row['recipe']=={'operation':'rank_filter_imagej' if rank else 'gaussian_blur_imagej',
        'method':'mean' if rank else None,'initial_value':1,'latest_value':latest,
        'destination':'in_place','roi_policy':'active_selection','workers':1}
    required=BASE|STATES[kind]
    assert set(row['states'])==set(row['observations'])==set(row['document_states'])==required
    obs=row['observations'];drawn=owner(row,'drawn');original=reference(1)['input']['f32_words']
    assert drawn['selection']['path']==PATH
    changed={'pixel-revision':'pixel_changed','calibration-revision':'calibration_changed'}.get(kind)
    ready_names={'latest_ready'} if kind=='supersede' else {'ready_recovery','recovered'} if kind=='invalid-editor' else {'ready','external_refused'} if kind=='invalid-external' else {'recovered'}

    for name in required:
        state=owner(row,name);expected=list(original);opaque=copy.deepcopy(retained(owner(row,'before')))
        is_changed=changed is not None and obs[name]['frame'] >= obs[changed]['frame']
        if is_changed and kind=='pixel-revision':expected[365]='0x40a00000'
        if is_changed and kind=='calibration-revision':opaque['calibration']['spacing'][0]=0.75
        samples(state,expected)
        if name not in ready_names:
            assert obs[name]['preview'] is None,name
            assert obs[name]['ordinary_pixels']['pixels_source']=='live',name
            displayed_samples(obs[name],expected)

        assert retained(state)==opaque,name
        if name not in BASE-{'drawn'}:
            assert state['undo']==drawn['undo']+int(is_changed),name
            # File/Open restores the saved session's global workflow table;
            # it is not per-owner history and Preview must not append to it.
            expected_workflow=owner(row,'source_saved')['workflow'] if kind=='owner-replace' and obs[name]['frame']>=obs['owner_replaced']['frame'] else drawn['workflow']
            assert state['workflow']==expected_workflow,name
        assert obs[name]['gaussian_jobs']['apply_callback_count']==0,name
    final=obs['cancelled']
    assert final['preview'] is None and final['usage']['reserved_bytes']==0
    assert final['ordinary_pixels']['contrast_source']=='live'
    assert final['ordinary_pixels']['preview_display_effects']==[]

    assert row['final_usage']['reserved_bytes']==0
    assert final['gaussian_jobs']['installed_preview_storage'] is None
    observer=row['lifecycle_observer'];events=row['worker_events']
    assert 0 < len(events) <= 4096
    assert observer['worker_event_count']==len(events)
    assert 0 < len(observer['weak_guards']) <= 32
    assert all(guard['expired'] is True and guard['expiration_transitions']==1 for guard in observer['weak_guards'])
    for index,event in enumerate(events,1):
        assert event['sequence']==index and event['role']=='native_worker'
        assert event['operation']==row['recipe']['operation']
    if kind!='invalid-external':
        running=obs['running'];held=running['lifecycle_observer'];job_id=held['job_id']
        assert held['armed'] is True and held['entered'] is True and held['released'] is False
        assert actual_job(running,job_id)['state']=='Running'
        assert held['generation']==running['gaussian_jobs']['preview']['request_generation']
        assert observer['released'] is True and observer['aborted'] is False
        assert [entry['mode'] for entry in row['barrier_inputs']]==['arm','release']
        assert actual_job(final,job_id)['state']=='Canceled'
        held_event=events[held['worker_event_count']-1]
        assert held_event['guard_id']==held['held_guard_id']
        assert held_event['phase']=='stage_progress' and held_event['completed']>0
        assert held_event['stage']==('ImageJ rank source copy' if rank else 'ImageJ Gaussian source copy')
    else:
        assert row['barrier_inputs']==[] and observer['armed'] is False and observer['entered'] is False
    ready_values={}
    if kind=='supersede':ready_values={'latest_ready':latest}
    elif kind=='invalid-editor':ready_values={'ready_recovery':1,'recovered':latest}
    elif kind=='invalid-external':ready_values={'ready':1,'external_refused':1}
    else:ready_values={'recovered':1}
    for name,value in ready_values.items():
        preview=obs[name]['preview'];assert preview is not None
        expected=filter_words(rank,value)
        if kind=='pixel-revision':expected[365]='0x40a00000'
        samples(preview,expected)
        displayed_samples(obs[name],expected)
        assert preview['request_generation']==obs[name]['gaussian_jobs']['active_installed_generation']
        assert obs[name]['ordinary_pixels']['pixels_source']=='preview'
    if kind=='supersede':
        middle=obs['middle_requested']['gaussian_jobs']['preview'];last=obs['latest_requested']['gaussian_jobs']['preview']
        assert obs['running']['lifecycle_observer']['generation'] < middle['request_generation'] < last['request_generation']
        assert row['observations']['latest_ready']['preview']['request_generation']==last['request_generation']
        assert actual_job(obs['latest_requested'],job_id)['cancel_requested'] is True
        middle_job=actual_job(final,middle['job']['id'])
        assert middle_job['state']=='Canceled' and middle_job['checkpoints']==0
        assert middle_job['reservation_acquired'] is False
    if kind=='invalid-editor':
        panel='rank_panel' if rank else 'gaussian_panel'
        for name in ('invalid_running','invalid_ready'):
            assert obs[name]['preview'] is None
            assert obs[name][panel]['draft_valid'] is False and obs[name][panel]['preview'] is True
            assert obs[name][panel]['error'] or obs[name][panel]['preview_error']
        assert obs['invalid_ready']['usage']['reserved_bytes']==0
    if 'old_settled' in required:
        settled=obs['old_settled']
        assert settled['preview'] is None and settled['usage']['reserved_bytes']==0
        assert settled['gaussian_jobs']['installed_preview_storage'] is None
    injections=row['lifecycle_injections']
    if kind=='invalid-external':
        assert len(injections)==1 and injections[0]['kind']=='invalid_external_request'
        assert injections[0]['request']=={'sigma':-1,'scaled_units':False,'target':'current_plane/active_selection'}
        assert injections[0]['error'] and injections[0]['before']==injections[0]['after']
        ready,refused=obs['ready'],obs['external_refused']
        for field in ('preview','usage','limits','settings','actual_native_jobs'):
            assert ready[field]==refused[field],field
        assert ready['gaussian_jobs']['installed_preview_storage']==refused['gaussian_jobs']['installed_preview_storage']
        assert ready['gaussian_jobs']['active_installed_generation']==refused['gaussian_jobs']['active_installed_generation']
        assert ready['lifecycle_observer']['worker_event_count']==refused['lifecycle_observer']['worker_event_count']
    elif kind in ('pixel-revision','calibration-revision'):
        assert len(injections)==1 and injections[0]['kind']==kind.replace('-','_')
        injection=injections[0];assert injection['error']==''
        assert 'no actual UI execution credit' in injection['role']
        assert injection['after']['undo']==injection['before']['undo']+1
        if kind=='pixel-revision':
            assert injection['request']=={'position':[0,0,0],'xy':[20,15],'index':365,'f32_word':0x40a00000}
            assert injection['after']['pixel']['f32_word']==0x40a00000
            assert injection['after']['revisions']['pixels']==injection['before']['revisions']['pixels']+1
        else:
            assert injection['request']=={'spacing_x':0.75}
            assert injection['after']['calibration']['spacing'][0]==0.75
            assert injection['after']['revisions']['metadata']==injection['before']['revisions']['metadata']+1
    else:assert injections==[]
    if kind=='selection-revision':
        changed_state=owner(row,'selection_changed')
        assert changed_state['selection'] is None and changed_state['previous']['path']==PATH
        assert changed_state['revisions']['selection']==drawn['revisions']['selection']+1
        assert owner(row,'selection_restored')['selection']['path']==PATH
    if kind=='owner-replace':
        for name in ('owner_replaced','old_settled'):
            assert row['states'][name]['document_id']!=row['source_document_id']
            assert len(row['document_states'][name]['documents'])==2
        assert row['states']['owner_restored']['document_id']==row['source_document_id']
    assert set(row['files'])=={'source'}
    assert len(bytes.fromhex(row['files']['source']['hex']))==row['files']['source']['bytes']
    assert 0 < len(row['frames']) <= 750
    for index,frame in enumerate(row['frames'],1):
        assert frame['ordinal']==index and frame['events']==frame['rendered']['input_frame']['raw_inputs']
    for control in row['controls']:
        assert control['hovered_id']==control['item']['id'] != 0 and control['item']['enabled'] is True
    assert [hit['image'] for hit in row['canvas_hits']]==[[2.2,1.2],[9.2,1.2],[2.2,8.2]]
