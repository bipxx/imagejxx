"""V3 full-region creation equations, independent ImageJ cells and replay."""
import base64
import copy
import json
import os
from pathlib import Path

import numpy as np
import pytest
import imagejxx as ij
from test_selection_region import sample

ROWS = json.loads((Path(__file__).parents[1] / 'fixtures' /
    'selection-region-boolean-vectors-imagej154r.json').read_bytes())['cases']
OP = dict(union='ADD', intersection='INTERSECT', subtraction='SUBTRACT', xor='XOR')
CASES = [('second', row) for row in ROWS] + [
    ('first', row) for row in ROWS if row['operations'][0] != 'subtraction']


def pixels(stage):
    _, _, w, h = stage['sample']
    return np.unpackbits(np.frombuffer(base64.b64decode(stage['mask']), np.uint8))[:w*h].reshape(h,w)


def vector(value):
    return ij.Selection(value['kind'], value['points'], value['width'], attributes=value['attributes'])


def region(bitmap, origin):
    shape = bitmap.shape
    return ij.SelectionRegion(ij.Selection.from_mask(bitmap), shape, origin).as_selection()


def document(base=None):
    result = ij.Document(ij.Dataset(np.arange(81,dtype=np.uint16).reshape(9,9), axes='YX'))
    if base is not None:
        result.select(base)
    return result


def request(operation='ADD', candidate=None, geometry=None):
    candidate = candidate if candidate is not None else ij.Selection('rectangle',[(-1,0),(2,1)])
    args = {} if geometry is None else {'geometry': geometry}
    return ij.SelectionCreationRequest(candidate, getattr(ij.SelectionCreationOperation,operation), **args)


def literal_base():
    return region(np.array([[1,0,1]],np.uint8),[-2,0])


@pytest.mark.parametrize('family,row', CASES,
    ids=[f'{f}-k{r["kind"]}-v{r["variant"]}-o{r["operations"][0]}' for f,r in CASES])
def test_creation_matches_independent_imagej_full_membership(family,row):
    if family == 'first':
        base = ij.Selection.from_dict(row['inputs'][1])
        candidate = vector(row['inputs'][0])
        index = 0
    else:
        stage = row['stages'][0]
        base = region(pixels(stage), stage['sample'][:2])
        candidate = vector(row['inputs'][2])
        index = 1
    owner = document(base)
    before = owner.snapshot()
    command = ij.resolve_selection_creation_command(before,request(OP[row['operations'][index]],candidate))
    record = command.record()
    assert record['version'] == 3 and record['base'] == base.to_dict()
    ij.validate_selection_creation_command_record(record)
    replay = ij.selection_creation_command_from_record(before,copy.deepcopy(record))
    assert replay.result == command.result and replay.request.geometry == ij.SelectionCreationGeometry.FULL_REGION_V3
    actual = ij.SelectionRegion(command.result,(9,9))
    stage = row['stages'][index]
    np.testing.assert_array_equal(sample(actual,stage['sample']),pixels(stage))
    assert np.count_nonzero(actual.local_selection.mask(actual.shape)) == stage['occupied']
    assert owner.snapshot().data_identity == before.data_identity


@pytest.mark.parametrize('operation,origin,shape,encoded',[
    ('ADD',[-2,0],[1,4],'8A=='),('SUBTRACT',[-2,0],[1,3],'gA=='),
    ('INTERSECT',[-1,0],[1,2],'QA=='),('XOR',[-2,0],[1,4],'0A==')])
def test_literal_off_image_cells_prepare_and_fresh_previous(operation,origin,shape,encoded):
    base = literal_base();owner = document(base);before = owner.snapshot();history = owner.undo_steps
    command = ij.resolve_selection_creation_command(before,request(operation))
    result = command.result.to_dict()
    assert result['origin'] == origin and result['shape'] == shape and result['mask'] == encoded
    owner.select(ij.Selection('point',[(8,8)]))
    current = owner.snapshot()
    replay = ij.selection_creation_command_from_record(current,command.record())
    owner.commit(ij.prepare_selection_creation(replay))
    after = owner.snapshot()
    assert after.selection == command.result and after.previous_selection == current.selection
    assert after.dataset.shares_pixels(before.dataset) and owner.undo_steps == history
    np.testing.assert_array_equal(after.dataset.numpy('YX'),np.arange(81,dtype=np.uint16).reshape(9,9))


@pytest.mark.parametrize('operation',['REPLACE','ADD','SUBTRACT','INTERSECT','XOR'])
def test_explicit_full_and_legacy_modes_are_versioned(operation):
    owner = document(ij.Selection('rectangle',[(-3,-2),(1,2)]))
    canvas = ij.resolve_selection_creation_command(owner.snapshot(),request(operation))
    assert canvas.record()['version'] == 1
    full = ij.resolve_selection_creation_command(owner.snapshot(),request(operation,
        geometry=ij.SelectionCreationGeometry.FULL_REGION_V3))
    assert full.record()['version'] == 3
    assert full.result.is_region == (operation != 'REPLACE')
    ij.validate_selection_creation_command_record(full.record())
    assert ij.selection_creation_command_from_record(owner.snapshot(),full.record()).result == full.result
    regional = document(literal_base())
    calls = []
    with pytest.raises(ValueError):
        ij.resolve_selection_creation_command(regional.snapshot(),request(operation,
            geometry=ij.SelectionCreationGeometry.CANVAS_V1),checkpoint=lambda:calls.append(True))
    assert calls == []


@pytest.mark.parametrize('field,value',[
    ('version',2),('version',True),('version',3.0),('extra',False),
    ('base.origin',[0.5,0]),('base.origin',[True,0]),('base.origin',[-2**31-1,0]),
    ('base.shape',[0,3]),('base.shape',[1,2**31]),('base.mask','oQ=='),
    ('candidate.extra',0),('candidate.attributes',None),('result.mask','AA=='),
    ('result.origin',[0,0]),('result.shape',[1,3]),('result.points',[[0,0]])])
def test_closed_v3_geometry_and_forged_equations_refuse(field,value):
    owner = document(literal_base());before = owner.snapshot()
    record = ij.resolve_selection_creation_command(before,request()).record()
    parts = field.split('.');node = record
    for name in parts[:-1]:node = node[name]
    node[parts[-1]] = value
    for action in [lambda:ij.validate_selection_creation_command_record(record),
        lambda:ij.selection_creation_command_from_record(before,record),
        lambda:ij.rebind_selection_creation_command(before,record,request())]:
        with pytest.raises((ValueError,OverflowError,TypeError)):action()
    assert owner.snapshot().data_identity == before.data_identity


def test_full_frame_and_working_caps_refuse_without_callbacks():
    owner = document(region(np.ones((1,1),np.uint8),[-100,0]));calls=[]
    limits = ij.SelectionCreationLimits();limits.max_pixels = 90
    with pytest.raises((ValueError,OverflowError)):
        ij.compose_selection_creation(owner.snapshot(),request(),limits,checkpoint=lambda:calls.append(True))
    assert calls == []

    owner = document(literal_base());estimate=ij.estimate_selection_creation(owner.snapshot(),request())
    limits = ij.SelectionCreationLimits();limits.max_working_bytes=estimate.working_bytes
    ij.resolve_selection_creation_command(owner.snapshot(),request(),limits)
    limits.max_working_bytes-=1
    with pytest.raises(ValueError):
        ij.resolve_selection_creation_command(owner.snapshot(),request(),limits,checkpoint=lambda:calls.append(True))
    assert calls == []

@pytest.mark.parametrize('kind',['u8','u16','rgb','float','quiet','ieee'])
def test_full_creation_is_metadata_only_for_scientific_sources(kind):
    from test_selection_enlargement_command import source
    owner,reads,original=source(kind,shape=(2,2,2,9,9,1))
    pixels=owner.snapshot().dataset
    for operation in ['ADD','SUBTRACT','INTERSECT','XOR']:
        owner.select(literal_base());before=owner.snapshot()
        command=ij.resolve_selection_creation_command(before,request(operation))
        ij.validate_selection_creation_command_record(command.record())
        replay=ij.selection_creation_command_from_record(before,command.record())
        owner.commit(ij.prepare_selection_creation(replay))
        after=owner.snapshot()
        assert after.dataset.shares_pixels(pixels) and after.previous_selection==before.selection
        assert after.display.t==after.display.c==after.display.crosshair[2]==1
        assert reads.counts().plane_reads==reads.counts().region_reads==0
        assert not after.dirty and owner.undo_steps==0
    assert owner.snapshot().dataset.numpy('TCZYXS').tobytes()==original.tobytes()



@pytest.mark.parametrize('operation',['REPLACE','ADD','SUBTRACT','INTERSECT','XOR'])
def test_v3_record_survives_native_session_round_trip(operation,tmp_path):
    owner = document(literal_base())
    command=ij.resolve_selection_creation_command(owner.snapshot(),request(operation))
    owner.commit(ij.prepare_selection_creation(command))
    path = tmp_path/'full-creation.ijx'
    ij.save_session(str(path),ij.SessionCapture([owner],workflow=[command.record()]))
    assert path.read_bytes().startswith(b'IMAGEJXX4\n')
    restored = ij.load_session(str(path))
    reopened = restored.documents[0]
    assert restored.workflow == [command.record()]
    assert reopened.snapshot().selection == command.result
    with pytest.raises(ValueError,match='owner'):
        ij.selection_creation_command_from_record(reopened.snapshot(),restored.workflow[0])
    rebound = ij.rebind_selection_creation_command(reopened.snapshot(),restored.workflow[0],request(operation))
    assert rebound.record()['source']['id'] == reopened.id


@pytest.mark.parametrize('depth,accepted',[(125,True),(126,False)])
def test_whole_v3_metadata_envelope_depth_before_callbacks(depth,accepted):
    nested = 'leaf'
    for _ in range(depth):nested = [nested]
    candidate = ij.Selection('rectangle',[(-1,0),(2,1)],attributes={'opaque':nested})
    owner = document(literal_base());calls=[]
    action = lambda:ij.resolve_selection_creation_command(owner.snapshot(),request(candidate=candidate),
        checkpoint=lambda:calls.append(True))
    if accepted:
        command = action()
        ij.validate_selection_creation_command_record(command.record())
        assert calls
    else:
        with pytest.raises(ValueError):action()
        assert not calls


@pytest.mark.parametrize('scientific',[False,True])
def test_shared_editor_publication_recording_and_explicit_replay(scientific,tmp_path):
    from imagejxx.ui.app import Editor
    if scientific:
        owner = ij.Document(ij.Dataset(np.arange(2*2*81,dtype=np.uint16).reshape(2,2,9,9),axes='TZYX'))
        owner.select(literal_base())
    else:
        owner = document(literal_base())
    editor = Editor(preferences=tmp_path/'preferences.json')
    editor.add_native_document(owner);editor.recording=True
    if scientific:editor.action('create_roi')
    before=owner.snapshot();history=owner.undo_steps
    editor.apply_selection_creation(request())
    assert owner.snapshot().selection.is_region and owner.snapshot().previous_selection == before.selection
    record=editor.workflow[-1];assert record['version']==3
    ij.validate_selection_creation_command_record(record)
    owner.select(ij.Selection('point',[(8,8)]));fresh=owner.snapshot()
    editor.replay_selection_creation(record)
    assert owner.snapshot().previous_selection == fresh.selection
    assert owner.undo_steps == history and owner.snapshot().dataset.shares_pixels(before.dataset)
    assert editor.workflow[-1]==record


@pytest.mark.parametrize('modifier,expected_count',[('Shift',18),('Alt',1)])
def test_actual_modifier_drawing_retains_regional_base(modifier,expected_count,tmp_path):
    from imagejxx.ui.app import Editor
    from test_imgui_roi_style import key
    owner=document(literal_base());before=owner.snapshot();history=owner.undo_steps
    editor=Editor(preferences=tmp_path/'preferences.json')
    editor.add_native_document(owner);editor.recording=True
    editor.render_headless(frames=3)
    # Shift also constrains to round(hypot(3,3))=4. Alt selects subtraction;
    # the [0,0]..[2,2] candidate removes the base's sole visible cell.
    start,end=([2,2],[5,5]) if modifier=='Shift' else ([0,0],[2,2])
    events=[key(modifier,True),
        {'type':'position','point':start,'space':'image'},
        {'type':'button','name':'left','down':True},
        {'type':'position','point':end,'space':'image'},
        {'type':'button','name':'left','down':False},key(modifier,False)]
    frame=editor.render_headless(events=events)
    (tmp_path/f'regional-creation-{modifier}.json').write_text(json.dumps(frame))
    after=owner.snapshot()
    assert after.selection.is_region and after.previous_selection == before.selection
    full=ij.SelectionRegion(after.selection,(9,9))
    assert np.count_nonzero(full.local_selection.mask(full.shape))==expected_count
    assert full.origin[0] == -2 and owner.undo_steps==history
    assert editor.workflow[-1]['version']==3
    assert editor.workflow[-1]['operation']==('add' if modifier=='Shift' else 'subtract')
    assert after.dataset.shares_pixels(before.dataset)


@pytest.mark.parametrize('case',['Q01','Q02','Q03'])
@pytest.mark.parametrize('context',['imgui_cpp','imgui_python'])
def test_paired_actual_full_region_creation(case,context,tmp_path):
    if context=='imgui_cpp':
        root=Path(os.environ['IMAGEJXX_REGIONAL_CREATION_CPP_BASE'])/case
        row=json.loads((root/'case.json').read_bytes())
    else:
        from imagejxx import _desktop
        editor=_desktop.Editor(preferences_path=str(tmp_path/'preferences.json'))
        row=editor._selection_creation_ui_case(case,context,str(tmp_path/'case'))
        if isinstance(row,str):row=json.loads(row)
    assert row['contract_id']=='p12q.regional-creation-ui.001' and row['context']==context
    assert row['observation_state']=='observed' and row['window_error']==''
    before=row['states']['initial']['documents'][0]
    after=row['states']['final']['documents'][0]
    geometry=after['selection']
    assert geometry['kind']=='region' and geometry['origin']==[-2,0]
    # The unchanged S02 oracle pins the held-Shift growth to an 8x8 square:
    # round(hypot(25-18,10-6))=8, hence [18,6]..[26,14].
    assert geometry['shape']==[14,28]
    bits=np.unpackbits(np.frombuffer(base64.b64decode(geometry['mask']),np.uint8))[:392].reshape(14,28)
    literal=np.zeros((14,28),np.uint8);literal[0,0]=literal[0,2]=1
    if case=='Q03':literal[0,1]=1
    literal[6:14,20:28]=1
    np.testing.assert_array_equal(bits,literal)
    assert np.count_nonzero(bits)==(67 if case=='Q03' else 66)
    assert sum(r.count('1') for r in after['selection_observation']['mask_rows'])==65
    assert after['previous_selection']==before['selection']
    for field in ['dirty','undo_steps','redo_steps','metadata','rois','overlays','calibration','spectrum_present']:
        assert after[field]==before[field]
    assert after['planes']==before['planes']
    record=row['states']['final']['workflow'][-1]
    assert record['version']==3 and record['operation']=='add'
    assert record['base']==before['selection'] or (case=='Q03' and record['base']['kind']=='rectangle')
    assert record['result']['mask']==geometry['mask']
    # Both previews must originate from actual held-button input while live data
    # still matches the initial current/Previous and source sample identities.
    live=[f for f in row['frames'] if f['label']=='regional.add.preview']
    assert len(live)==1
    assert live[0]['native_state']['documents'][0]['selection']==before['selection']
    preview=live[0]['draft_observation']['selection_creation']['draft']['preview']
    assert preview['kind']=='region' and preview['origin'][0]==-2
    assert row['gesture_observations'] and row['control_observations']
