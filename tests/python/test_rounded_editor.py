"""Rounded configuration, saved ROI records and actual ImGui observations."""
from copy import deepcopy
from pathlib import Path
import json
import os
import numpy as np
import pytest
import imagejxx as ij

def rounded(diameter=8):
    return ij.Selection('rounded_rectangle', [(6,5),(24,19)], attributes={
        'corner_diameter':diameter,'label':'kept','future':{'values':[True,9]}})

@pytest.mark.parametrize('value',[0,3,8,2**31-1])
def test_rounded_setting_bounds(value):
    settings=ij.Settings();assert settings['rounded_corner_diameter']==8
    settings['rounded_corner_diameter']=value
    assert settings['rounded_corner_diameter']==value
    settings.reset('Drawing');assert settings['rounded_corner_diameter']==8

@pytest.mark.parametrize('value',[-1,2**31,True,1.5,'8'])
def test_invalid_rounded_default_is_atomic(value):
    settings=ij.Settings();before=settings.to_dict()
    with pytest.raises((ValueError,TypeError,OverflowError)):
        settings.update({'rounded_corner_diameter':value,'line_width':3})
    assert settings.to_dict()==before

def saved_owner(rotated=False):
    value=rounded()
    if rotated:value=ij.SelectionEditor(value,(33,41)).rotated(23)
    owner=ij.Document(ij.Dataset(np.arange(33*41,dtype=np.uint16).reshape(33,41),axes='YX'))
    edit=owner.begin_edit();edit.set_rois([ij.ROI('rounded',value,attributes={'row':7})]);owner.commit(edit)
    return owner,value

@pytest.mark.parametrize('rotated',[False,True])
@pytest.mark.parametrize('diameter',[0,3,2**31-1])
def test_saved_rounded_corner_is_geometry_with_strict_v2_record(rotated,diameter):
    owner,value=saved_owner(rotated)
    replacement=ij.SelectionEditor(value,(33,41)).properties(value.width,{'corner_diameter':diameter})
    command=ij.resolve_saved_roi_geometry(owner.snapshot(),ij.SavedRoiGeometryRequest(0,replacement))
    record=command.record();assert record['version']==2
    ij.validate_saved_roi_geometry_record(record)
    replay=ij.saved_roi_geometry_from_record(owner.snapshot(),record)
    assert replay.replacement==replacement
    old=deepcopy(record);old['version']=1
    with pytest.raises(ValueError):ij.validate_saved_roi_geometry_record(old)
    for patch in [{'label':'changed'},{'future':None}]:
        bad=ij.SelectionEditor(replacement,(33,41)).properties(value.width,patch)
        with pytest.raises(ValueError):ij.resolve_saved_roi_geometry(owner.snapshot(),ij.SavedRoiGeometryRequest(0,bad))

def check_report(report):
    assert report['observation_state']=='observed' and report['acceptance_claim'] is False
    assert not report['window_error']
    for frame in report['frames']:
        assert frame['requested_events']==frame['rendered']['input_frame']['raw_inputs']
    states=report['states']
    initial=states['initial']['documents'][0]
    for name in ['rounded.replace.published','rounded.final.published','rounded.properties.published']:
        current=states[name]['documents'][0]
        assert current['undo_steps']==initial['undo_steps'] and current['redo_steps']==initial['redo_steps']
        assert current['data_identity']==initial['data_identity'] and current['dirty']==initial['dirty']
        assert current['planes']==initial['planes']
        assert current['metadata']==initial['metadata'] and current['rois']==initial['rois']
        assert current['revisions']['pixels']==initial['revisions']['pixels']
    created=states['rounded.replace.published']['documents'][0]
    assert created['selection']['kind']=='rounded_rectangle' and created['selection']['corner_diameter']==6
    np.testing.assert_array_equal(created['selection']['points'],[[8,5],[24,18]])
    # Independent ellipse-corner membership well away from the cubic boundary.
    rows=created['selection_observation']['mask_rows']
    assert rows[5][8]=='0' and rows[5][16]=='1' and rows[11][8]=='1' and rows[18][16]=='0'
    invalid=next(f['rendered']['selection_properties'] for f in report['frames'] if f['label']=='rounded.properties.invalid')
    assert invalid['error'] and not invalid['preview']
    final=states['rounded.properties.published']['documents'][0]
    assert final['selection']['corner_diameter']==3
    assert final['previous_selection']['corner_diameter']==6
    assert report['rounded_settings']['rounded_corner_diameter']==6
    records=[r for r in report['rounded_workflow'] if r.get('schema')=='imagejxx.selection-creation-command']
    assert len(records)==6 and all(r['version']==2 for r in records)
    assert [r['operation'] for r in records]==['replace','add','subtract','intersect','xor','replace']
    for record in records:ij.validate_selection_creation_command_record(record)
    assert any(c['selector']=='tools.tool.rounded_rectangle' for c in report['control_observations'])
    assert any(c['selector']=='selection_properties.Corner diameter' for c in report['control_observations'])

def test_actual_rounded_input_in_both_imgui_launches(tmp_path):
    from imagejxx import _desktop
    directory=tmp_path/'capture001'
    report=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._selection_creation_ui_case('R01','imgui_python',str(directory)))
    check_report(report)
    cpp=Path(os.environ['IMAGEJXX_ROUNDED_CPP_CASE'])
    check_report(json.loads((cpp/'case.json').read_bytes()))

def test_diameter_default_survives_native_preferences_restart(tmp_path):
    from imagejxx import _desktop
    path=tmp_path/'preferences.json'
    editor=_desktop.Editor(str(path));editor.update_settings(json.dumps({'rounded_corner_diameter':17}),True)
    assert json.loads(_desktop.Editor(str(path)).settings())['rounded_corner_diameter']==17
