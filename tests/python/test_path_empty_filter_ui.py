"""Literal empty-region UI and isolated processing-provider read evidence."""
import json
import os
import struct
from pathlib import Path
import pytest
from test_path_filter_ui import PATH, retained, reference, little_words, scientific_original, volume_words

COMMANDS={
 'active-zero-area':[0,1,1,4],
 'active-one-axis-zero':[0,1,2,1,6,2,4],
 'active-off-canvas':[0,26.25,2.5,1,31.25,2.5,1,26.25,8.5,4],
}
CASES=[f'empty-{operation}-{kind}' for operation in ('gaussian','rank-mean')
       for kind in (*COMMANDS,'positioned-unmatched')]
POSITIONS=[[t,c,z] for t in (0,1) for c in (0,2) for z in (1,3)]


def words(state,scientific):
    if scientific:
        assert volume_words(state)==scientific_original()
    else:
        assert state['shape']==[1,1,1,17,23,1]
        assert len(state['planes'])==1 and state['planes'][0]['position']==[0,0,0]
        assert state['planes'][0]['declared_f32_hex']==little_words(reference(1)['input']['f32_words'])
    expected=scientific_original() if scientific else {(0,0,0):little_words(reference(1)['input']['f32_words'])}
    for plane in state['planes']:
        raw=bytes.fromhex(expected[tuple(plane['position'])])
        doubles=''.join(struct.pack('<d',value[0]).hex() for value in struct.iter_unpack('<f',raw))
        assert plane['storage_f64_hex']==doubles


@pytest.mark.parametrize('case_id',CASES)
@pytest.mark.parametrize('context',['imgui_cpp','imgui_python'])
def test_actual_empty_paths_and_all_unmatched_targets_never_fall_back(case_id,context,tmp_path):
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
    rank=case_id.startswith('empty-rank-mean-')
    kind=case_id.removeprefix('empty-rank-mean-' if rank else 'empty-gaussian-')
    scientific=kind=='positioned-unmatched'
    recipe={'operation':'rank_filter_imagej' if rank else 'gaussian_blur_imagej','method':'mean' if rank else None,
        'sigma':None if rank else 1,'radius':2.5 if rank else None,'destination':'in_place',
        'roi_policy':'positioned_rois' if scientific else 'active_selection','workers':1,'fixture_geometry_authoring_credit':False}
    if scientific:recipe.update(ui_ranges=[[1,2,1],[1,3,2],[2,4,2]],preview_ordinal=6,initial_position=[1,1,2])
    else:recipe['fixture_path']=COMMANDS[kind]
    assert row['recipe']==recipe
    required={'before','source_saved','cancelled','panel_reopened','applied','filtered_saved','filtered_reopened'}
    required|={'drawn','target_configured','preview_hidden','preview_shown'} if scientific else {'configured','preview_ready'}
    states,obs=row['states'],row['observations']
    assert set(states)==set(obs)==set(row['document_states'])==required
    baseline=states['drawn'] if scientific else states['before']
    assert baseline['selection']['path']==(PATH if scientific else COMMANDS[kind])
    phase_start=obs['drawn']['frame'] if scientific else obs['before']['frame']
    for name,state in states.items():
        words(state,scientific)
        assert retained(state)==retained(states['before']),name
        if obs[name]['frame']>=phase_start:
            assert state['selection']==baseline['selection'] and state['previous']==baseline['previous'],name
        if name not in {'filtered_reopened','before','source_saved'}:
            assert state['data_identity']==baseline['data_identity'],name
            assert state['undo']==baseline['undo'] and state['redo']==baseline['redo'],name
            for field in ('pixels','metadata'):
                assert state['revisions'][field]==baseline['revisions'][field],(name,field)
        if name in {'applied','filtered_saved','filtered_reopened'}:
            assert len(state['workflow'])==len(baseline['workflow'])+int(not rank),name
            if not rank:
                record=state['workflow'][-1]
                assert record['operation']=='gaussian_blur_imagej'
                assert record['options']=={'sigma':1.0,'scaled_units':False}
                assert record['target']['roi_policy']==recipe['roi_policy']
        else:
            expected=baseline['workflow'] if obs[name]['frame']>=phase_start else states['before']['workflow']
            assert state['workflow']==expected,name
    for name,snapshot in row['document_states'].items():
        documents=snapshot['documents'];ids=snapshot['document_ids']
        assert len(ids)==len(set(ids))==len(documents)==(2 if name=='filtered_reopened' else 1)
        assert set(ids)=={document['document_id'] for document in documents}
        assert row['source_document_id'] in ids
        assert snapshot['active_document_id']==states[name]['document_id']
        for document in documents:
            words(document,scientific)
            assert retained(document)==retained(states['before'])
            for field in ('selection','previous','workflow'):
                assert document[field]==states[name][field],(name,field)
            original=document['document_id']==row['source_document_id']
            expected_history=states['filtered_saved'] if name=='filtered_reopened' and original else states[name]
            assert document['undo']==(expected_history['undo'] if original else 0)
            assert document['redo']==(expected_history['redo'] if original else 0)
            for field in ('pixels','metadata'):
                assert document['revisions'][field]==states['before']['revisions'][field]
    preview_name='preview_hidden' if scientific else 'preview_ready'
    preview=obs[preview_name]['preview'];assert preview is not None
    words(preview,scientific)
    target=preview['target']
    if scientific:
        assert target['scope']=='positions' and target['positions']==[[1,0,3]]
    else:
        # Legacy facade expresses its one plane through ranges, not explicit positions.
        assert target['scope']=='ranges' and target['positions']==[]
        assert target['ranges']==[[0,1,1],[0,1,1],[0,1,1]]
    assert obs['cancelled']['preview'] is None and obs['cancelled']['usage']['reserved_bytes']==0
    if scientific:
        for name in ('preview_hidden','preview_shown'):
            words(obs[name]['preview'],True)
        assert obs['preview_hidden']['ordinary_pixels']['pixels_source']=='live'
        assert obs['preview_shown']['ordinary_pixels']['pixels_source']=='preview'
        assert obs['preview_hidden']['preview']['request_generation']==obs['preview_shown']['preview']['request_generation']
        assert all(roi['c']==1 for roi in baseline['rois'])
        assert baseline['rois'][0]['t'] is None and baseline['rois'][0]['z'] is None
    assert set(row['read_fences'])=={'preview','apply'}
    for phase,fence in row['read_fences'].items():
        assert fence['state']=='observed' and fence['source_document_id']==row['source_document_id']
        assert fence['eager_source'] is True
        assert fence['creator_frame_complete_before'] is True and fence['creator_frame_complete_after'] is True
        assert fence['submit_frame_before'] < fence['frame_before']==fence['frame_after']
        assert fence['counts_before']==fence['counts_after']
        assert fence['counts_before']['overflowed'] is False
        assert set(fence['counts_before'])=={'plane_reads','region_reads','overflowed'}
        viewport=fence['viewport_jobs_at_entry']
        assert viewport is None or all(viewport[key]==0 for key in ('queued','running','retiring','ready','slots'))
        before,after=fence['fence_before'],fence['fence_after']
        assert before['entered'] is True and before['released'] is False
        assert after['released'] is True and after['aborted'] is False
        assert before['job_id']==after['job_id'] and before['guard_address']
        actual=[job for job in fence['actual_native_jobs_ready'] if job['id']==before['job_id']]
        assert len(actual)==1 and actual[0]['state']=='Ready'
        assert fence['prepared_operation']==recipe['operation']
        assert fence['execution_options']=={'dataset_policy':0,'phase':1 if phase=='preview' else 0}
        assert fence['shares_source_pixels'] is True
        expected_positions=([[1,0,3]] if scientific else [[0,0,0]]) if phase=='preview' else POSITIONS if scientific else [[0,0,0]]
        assert [output['position'] for output in fence['outputs']]==expected_positions
        assert all(output['skipped'] is True and output['label_image'] is False and output['label_count'] is None for output in fence['outputs'])
        assert fence['target']['roi_policy']==recipe['roi_policy']
        assert 0 <= fence['elapsed_ms'] < 15000
        assert 0 < len(fence['worker_events']) <= 4096
        assert any(event['phase']=='worker_entered' for event in fence['worker_events'])
    assert row['final_usage']['reserved_bytes']==0
    assert set(row['files'])=={'source','filtered'}
    for file in row['files'].values():assert len(bytes.fromhex(file['hex']))==file['bytes']
    assert 0 < len(row['frames']) <= 750
    for index,frame in enumerate(row['frames'],1):
        assert frame['ordinal']==index and frame['events']==frame['rendered']['input_frame']['raw_inputs']
    for control in row['controls']:
        assert control['hovered_id']==control['item']['id'] != 0 and control['item']['enabled'] is True
    if not scientific:
        # The shared producer emits this collection only after actual canvas hits.
        assert 'canvas_hits' not in row
    else:
        assert [hit['image'] for hit in row['canvas_hits']]==[[2.2,1.2],[9.2,1.2],[2.2,8.2]]
        assert all(hit['hovered_id']==hit['canvas_id'] != 0 for hit in row['canvas_hits'])
