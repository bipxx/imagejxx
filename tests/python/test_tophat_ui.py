"""Top Hat real-input observations and explicit transient execution phase."""
import copy,json,os
from pathlib import Path
import imagejxx as ij
import numpy as np
import pytest

@pytest.mark.parametrize('case_id',['R38','R39','R40','R41','R42','R43','R44'])
def test_actual_top_hat_menu_both_hosts(tmp_path,case_id):
    if os.environ.get('IMAGEJXX_RENDER_TESTS')!='1':pytest.skip('render checks are opt in')
    from imagejxx import _desktop
    python=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._rank_ui_case(case_id,'imgui_python',str(tmp_path/'capture001')))
    cpp=json.loads((Path(os.environ['IMAGEJXX_TOPHAT_CPP_DIR'])/case_id/'case.json').read_bytes())
    for report in [python,cpp]:
        assert report['observation_state']=='observed'
        assert all(f['requested_events']==f['rendered']['input_frame']['raw_inputs'] for f in report['frames'])
        before=report['fixture_sources']['source'];states=report['states']
        configured=states['tophat.configured']['documents'][0]
        assert all(f['rendered']['rank_panel']['remembered_radii']['minimum']==2 for f in report['frames'])
        preview_owner=states['tophat.preview']['documents'][0]
        assert [p['declared_hex'] for p in preview_owner['planes']]==[p['declared_hex'] for p in before['planes']]
        assert preview_owner['undo_steps']==before['undo_steps'] and preview_owner['display']['properties']==before['display']['properties']
        if case_id=='R43':
            after=states['tophat.cancelled']['documents'][0]
            assert after['undo_steps']==0 and after['redo_steps']==before['redo_steps'] and not states['tophat.cancelled']['workflow']
            assert [p['declared_hex'] for p in after['planes']]==[p['declared_hex'] for p in before['planes']]
            assert after['display']['properties']==before['display']['properties']
            continue
        documents=states['tophat.applied']['documents']
        if case_id=='R41':
            assert len(documents)==2
            original=next(d for d in documents if d['document_id']==before['document_id'])
            assert original['undo_steps']==0 and original['display']['properties']==before['display']['properties']
            assert [p['declared_hex'] for p in original['planes']]==[p['declared_hex'] for p in before['planes']]
            after=next(d for d in documents if d['document_id']!=before['document_id'])
        else:after=documents[0]
        selected={0,1,2} if case_id=='R39' else {10 if case_id in ['R40','R41'] else 0 if case_id in ['R42','R44'] else 1}
        dtype=np.dtype(before['dtype'])
        for i,p in enumerate(after['planes']):
            if i not in selected:assert p['declared_hex']==before['planes'][i]['declared_hex'];continue
            t,c,z=p['position'];expected=np.full((5,5),10*t+20*c+30*z,dtype)
            if case_id=='R44':
                ref=json.loads((Path(__file__).parents[1]/'fixtures/tophat-mask-imagej154r.json').read_bytes())['cases'][0]
                expected=np.frombuffer(bytes.fromhex(ref['output_hex']),dtype=np.float32).reshape(5,5)
            elif case_id!='R42':expected[1:4,1:4]=0;expected[2,2]=80
            assert bytes.fromhex(p['declared_hex'])==expected.tobytes()
        expected_properties=copy.deepcopy(before['display']['properties'])
        if case_id=='R44':expected_properties['display_range']=[0.,6.97265625]
        if case_id=='R39':expected_properties['display_range']=[0.,80.]
        if case_id in ['R40','R41']:
            expected_properties['channel_display']['channels'][1]['range']=[0.,79.6875]
            expected_properties.pop('manual_threshold')
        assert after['display']['properties']==expected_properties
        assert after['undo_steps']==1
        record=states['tophat.applied']['workflow'][-1]
        assert record['operation']=='top_hat_imagej'
        assert record['options']=={'radius':1.,'light_background':case_id in ['R39','R40','R41'],'dont_subtract':case_id=='R42','inverted_lut':case_id in ['R39','R40','R41']}
        if case_id=='R41':assert record['target']['destination']=='new_document'
        undone=next(d for d in states['tophat.undo']['documents'] if d['document_id']==after['document_id'])
        redone=next(d for d in states['tophat.redo']['documents'] if d['document_id']==after['document_id'])
        assert [p['declared_hex'] for p in undone['planes']]==[p['declared_hex'] for p in before['planes']]
        assert undone['display']['properties']==before['display']['properties']
        assert [p['declared_hex'] for p in redone['planes']]==[p['declared_hex'] for p in after['planes']]
        assert redone['display']['properties']==after['display']['properties']
        readback=next(f['rendered']['ordinary_pixels']['readback']['source'] for f in report['frames'] if f['label']=='tophat.preview')
        expected=np.full((5,5),60. if case_id in ['R40','R41'] else 0. if case_id=='R42' else 30.)
        if case_id=='R44':
            ref=json.loads((Path(__file__).parents[1]/'fixtures/tophat-mask-imagej154r.json').read_bytes())['cases'][1]
            expected=np.frombuffer(bytes.fromhex(ref['output_hex']),dtype=np.float32).reshape(5,5)
        elif case_id!='R42':expected[1:4,1:4]=0;expected[2,2]=80
        assert readback['pixels']==expected.ravel().tolist()
        preview_pixels=next(f['rendered']['ordinary_pixels'] for f in report['frames'] if f['label']=='tophat.preview')
        if case_id=='R44':
            assert preview_pixels['range']==[7.,127.]
            applied_pixels=next(f['rendered']['ordinary_pixels'] for f in report['frames'] if f['label']=='tophat.applied')
            assert applied_pixels['range']==[0.,6.97265625]


@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float32])
def test_python_editor_top_hat_history_and_new_output(tmp_path,dtype):
    from imagejxx.ui.app import Editor
    source=np.full((5,5),30,dtype);source[2,2]=110
    owner=ij.Document(ij.Dataset(source,axes='YX'));editor=Editor(preferences=tmp_path/'prefs.json');editor.add_native_document(owner)
    editor.apply_top_hat(options=ij.TopHatCommandOptions(1),wait=True)
    expected=np.zeros((5,5),dtype);expected[2,2]=80
    assert owner.snapshot().dataset.numpy('YX').tobytes()==expected.tobytes() and owner.undo_steps==1
    owner.undo();assert owner.snapshot().dataset.numpy('YX').tobytes()==source.tobytes()
    editor.apply_top_hat(ij.TargetRequest(destination=ij.OutputDestination.NEW_DOCUMENT),ij.TopHatCommandOptions(1,False,True),wait=True)
    assert owner.snapshot().dataset.numpy('YX').tobytes()==source.tobytes()

@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float32])
def test_preview_proposal_is_transient(dtype):
    source=np.full((5,5),7,dtype);owner=ij.Document(ij.Dataset(source,axes='YX'))
    edit=owner.begin_edit();edit.select(ij.Selection('polygon',[[0,0],[4,1],[2,4]]));owner.commit(edit)
    view=owner.snapshot().display;view.properties={'display_range':[7.,127.]};owner.set_display(view)
    command=ij.resolve_top_hat_command(owner.snapshot(),options=ij.TopHatCommandOptions(1))
    options=ij.ExecutionOptions();assert options.phase==ij.ExecutionPhase.APPLY
    options.phase=ij.ExecutionPhase.PREVIEW
    proposal=ij.prepare_operation(command.operation,command.target,command.parameters(),options=options)
    with pytest.raises(ValueError,match='Preview'):proposal.begin_edit()
    with pytest.raises(ValueError,match='Preview'):proposal.stage_new_document()
    app=ij.Application();app.adopt(owner)
    with pytest.raises(ValueError,match='Preview'):app.publish(proposal)
    assert owner.snapshot().dataset.numpy('YX').tobytes()==source.tobytes()

def test_full_bbox_polygon_preview_and_apply_ranges_differ():
    owner=ij.Document(ij.Dataset(np.full((5,5),7,np.float32),axes='YX'))
    edit=owner.begin_edit();edit.select(ij.Selection('polygon',[[0,0],[5,0],[0,5]]));owner.commit(edit)
    view=owner.snapshot().display;view.properties={'display_range':[7.,127.]};owner.set_display(view)
    command=ij.resolve_top_hat_command(owner.snapshot(),options=ij.TopHatCommandOptions(1))
    options=ij.ExecutionOptions();options.phase=ij.ExecutionPhase.PREVIEW
    preview=ij.prepare_operation(command.operation,command.target,command.parameters(),options=options)
    apply=ij.prepare_operation(command.operation,command.target,command.parameters())
    assert preview.preview.display.properties['display_range']==[7.,127.]
    assert apply.preview.display.properties['display_range']==[0.,6.97265625]
    assert preview.preview.dataset.numpy('YX').tobytes()==apply.preview.dataset.numpy('YX').tobytes()
