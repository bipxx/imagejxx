"""Shared histogram defaults: paired numeric recovery, publication and restart."""
import csv
import json
import os
from pathlib import Path
import subprocess
import numpy as np
import pytest
import imagejxx as ij

KEYS=('histogram_custom','histogram_bins','histogram_lower','histogram_upper','histogram_log','histogram_use_roi','histogram_scientific','histogram_roi')
def defaults(custom=False,bins=256,lower=0.,upper=1.,log=False,use_roi=True,scientific=False,roi='active_selection'):
    return dict(zip(KEYS,(custom,bins,lower,upper,log,use_roi,scientific,roi)))
def values(settings):return {k:settings[k] for k in KEYS}

def seed(path):
    payload=dict(format='imagejxx-preferences',version=1,values=dict(theme='light',future_histogram={'keep':[1,True,None]}),extras=dict(recent=[],plugin={'opaque':'µ'}),extension={'keep':42})
    path.write_text(json.dumps(payload,ensure_ascii=False),encoding='utf-8');return payload

def test_histogram_schema_has_eight_consumed_typed_defaults():
    schema={d['name']:d for d in ij.Settings.schema() if d['group']=='Histogram'}
    assert set(schema)==set(KEYS) and values(ij.Settings())==defaults()
    assert schema['histogram_roi']['choices']==['whole_plane','active_selection','positioned_rois']
    assert schema['histogram_bins']['minimum']==1 and schema['histogram_bins']['maximum']==65536

@pytest.mark.parametrize('custom',[False,True])
@pytest.mark.parametrize('bins',[1,8])
@pytest.mark.parametrize('log',[False,True])
@pytest.mark.parametrize('use_roi',[False,True])
@pytest.mark.parametrize('scientific',[False,True])
@pytest.mark.parametrize('roi',['whole_plane','active_selection','positioned_rois'])
def test_all_defaults_roundtrip_and_histogram_group_reset(custom,bins,log,use_roi,scientific,roi,tmp_path):
    expected=defaults(custom,bins,10.,20.,log,use_roi,scientific,roi);doc=ij.PreferencesDocument();doc.settings.update(expected);doc.settings['theme']='light'
    path=tmp_path/'preferences.json';assert doc.save(path);restored=ij.PreferencesDocument.load(path,recover=False)
    assert values(restored.settings)==expected
    restored.settings.reset('Histogram');assert values(restored.settings)==defaults() and restored.settings['theme']=='light'

@pytest.mark.parametrize('lower,upper',[(10.,20.),(-100.,-1.),(-np.finfo(float).max,np.finfo(float).max)])
def test_valid_bounds_migrate_as_a_pair(lower,upper):
    payload=dict(format='imagejxx-preferences',version=1,values=dict(histogram_lower=lower,histogram_upper=upper,histogram_bins=65536))
    doc=ij.PreferencesDocument.parse(json.dumps(payload),recover=True)
    assert not doc.write_blocked and doc.warnings==[] and doc.settings['histogram_lower']==lower and doc.settings['histogram_upper']==upper and doc.settings['histogram_bins']==65536

@pytest.mark.parametrize('key,value',[('histogram_custom',1),('histogram_bins',0),('histogram_bins',65537),('histogram_bins',1.5),('histogram_log','true'),('histogram_use_roi',0),('histogram_scientific',1),('histogram_roi','selection'),('histogram_lower',float('nan')),('histogram_upper',float('inf')),('histogram_lower',False)])
def test_bad_setting_patches_are_atomic(key,value):
    settings=ij.Settings();before=settings.to_dict()
    with pytest.raises(ValueError):settings.update(dict(theme='light',**{key:value}))
    assert settings.to_dict()==before

@pytest.mark.parametrize('lower,upper',[(1,1),(20,10),(-1,-2)])
def test_invalid_bound_pairs_recover_together_and_block_implicit_rewrite(lower,upper,tmp_path):
    payload=dict(format='imagejxx-preferences',version=1,values=dict(histogram_lower=lower,histogram_upper=upper,histogram_custom=True,theme='light'))
    settings=ij.Settings();before=settings.to_dict()
    with pytest.raises(ValueError):settings.update(dict(theme='light',histogram_lower=lower,histogram_upper=upper))
    assert settings.to_dict()==before
    with pytest.raises(ValueError):ij.PreferencesDocument.parse(json.dumps(payload),recover=False)
    doc=ij.PreferencesDocument.parse(json.dumps(payload),recover=True)
    assert doc.write_blocked and len(doc.warnings)==1 and doc.settings['histogram_lower']==0 and doc.settings['histogram_upper']==1 and doc.settings['histogram_custom'] and doc.settings['theme']=='light'
    path=tmp_path/'preferences.json';path.write_text(json.dumps(payload));original=path.read_bytes();assert not doc.save(path) and path.read_bytes()==original


def editor_with_source(path):
    from imagejxx import _desktop
    editor=_desktop.Editor(str(path));owner=ij.Document(ij.Dataset(np.arange(128,dtype=np.uint8).reshape(2,8,8),axes='ZYX'));editor.add_native_document(owner)
    editor.action('show_histogram','{}');return editor,owner

def action(editor,name,**params):editor.action(name,json.dumps(params))
def settings(editor):return json.loads(editor.settings())

def test_success_remembers_used_options_and_raw_mode_preserves_last_custom_range(tmp_path):
    path=tmp_path/'preferences.json';editor,owner=editor_with_source(path);before=owner.snapshot()
    editor.update_settings(json.dumps(defaults(True,8,10.,20.,True,False)),True);action(editor,'show_histogram');action(editor,'calculate_histogram')
    assert values(settings(editor))==defaults(True,8,10.,20.,True,False)
    action(editor,'configure_histogram',custom=False,bins='invalid',lower='invalid',upper='invalid');action(editor,'calculate_histogram')
    assert values(settings(editor))==defaults(False,8,10.,20.,True,False)
    action(editor,'show_histogram');action(editor,'configure_histogram',custom=True);action(editor,'calculate_histogram')
    output=tmp_path/'custom.csv';action(editor,'export_histogram_csv',path=str(output));rows=list(csv.DictReader(output.open()));assert [int(r['count']) for r in rows]==[2,1,1,1,2,1,1,2]
    assert owner.snapshot().shares_data(before) and owner.undo_steps==0


def test_open_invalid_draft_and_close_do_not_save_defaults(tmp_path):
    path=tmp_path/'preferences.json';editor,owner=editor_with_source(path);assert not path.exists()
    action(editor,'configure_histogram',custom=True,bins='0',log=True)
    with pytest.raises(ValueError):action(editor,'calculate_histogram')
    action(editor,'close_histogram');assert not path.exists() and values(settings(editor))==defaults() and owner.undo_steps==0


def test_single_and_scientific_roi_defaults_are_independent(tmp_path):
    path=tmp_path/'preferences.json';editor,owner=editor_with_source(path)
    action(editor,'configure_histogram',roi=False);action(editor,'calculate_histogram');assert not settings(editor)['histogram_use_roi']
    action(editor,'configure_histogram_target',target=dict(scope='all_z',roi='positioned_rois'));action(editor,'calculate_histogram')
    assert not settings(editor)['histogram_use_roi'] and settings(editor)['histogram_scientific'] and settings(editor)['histogram_roi']=='positioned_rois'
    action(editor,'show_histogram');panel=json.loads(editor.render_headless())['histogram_panel']
    assert panel['scientific_target'] and not panel['roi'] and panel['target_panel']['target']['ranges']==[[0,1,1],[0,1,1],[0,1,1]]

@pytest.mark.parametrize('payload',[dict(format='imagejxx-preferences',version=99,values={}),dict(format='imagejxx-preferences',version=1,values=dict(histogram_custom=True,histogram_lower=20,histogram_upper=10)),dict(format='imagejxx-preferences',version=1,values=dict(histogram_log=1))])
def test_protected_preferences_are_preserved_while_memory_and_result_update(payload,tmp_path):
    path=tmp_path/'preferences.json';path.write_text(json.dumps(payload));original=path.read_bytes();editor,owner=editor_with_source(path)
    action(editor,'configure_histogram',custom=True,bins='8',lower='0',upper='64',log=True,roi=False);action(editor,'calculate_histogram')
    assert path.read_bytes()==original and list(tmp_path.glob('preferences.json.bak*'))==[]
    assert values(settings(editor))==defaults(True,8,0.,64.,True,False)
    output=tmp_path/'result.csv';action(editor,'export_histogram_csv',path=str(output));assert [int(r['count']) for r in csv.DictReader(output.open())]==[8]*8 and owner.undo_steps==0


def test_preference_save_failure_preserves_previous_result_and_defaults(tmp_path):
    path=tmp_path/'preferences.json';editor,owner=editor_with_source(path);action(editor,'calculate_histogram');previous=tmp_path/'previous.csv';action(editor,'export_histogram_csv',path=str(previous));before=owner.snapshot();old=settings(editor)
    action(editor,'configure_histogram',custom=True,bins='8',lower='0',upper='64',log=True,roi=False);path.unlink();path.mkdir()
    with pytest.raises((ValueError,RuntimeError,OSError)):action(editor,'calculate_histogram')
    assert settings(editor)==old and owner.snapshot().shares_data(before) and owner.undo_steps==0 and path.is_dir()
    current=tmp_path/'current.csv';action(editor,'export_histogram_csv',path=str(current));assert current.read_bytes()==previous.read_bytes() and list(tmp_path.glob('preferences.json.tmp.*'))==[]


def test_histogram_defaults_are_exposed_by_shared_preferences_controls(tmp_path):
    editor,_=editor_with_source(tmp_path/'preferences.json');action(editor,'show_preferences',search='histogram');rendered=json.loads(editor.render_headless())
    assert {k for k in rendered['preferences_controls'] if k.startswith('histogram_')}==set(KEYS)

@pytest.mark.parametrize('context',['imgui_cpp','imgui_python'])
def test_actual_save_close_and_host_restart_restore_defaults_without_source_ranges(context,tmp_path):
    path=tmp_path/'preferences.json';original=seed(path)
    def observe(mode):
        capture=tmp_path/mode
        if context=='imgui_cpp':
            subprocess.run([os.environ['IMAGEJXX_CPP_EXE'],'--animation-ui-case','histogram-defaults-'+mode,str(capture)],check=True,timeout=45)
            return json.loads((capture/'case.json').read_bytes())
        from imagejxx import _desktop
        return json.loads(_desktop.Editor(str(path))._animation_ui_case('histogram-defaults-'+mode,str(capture)))
    first=observe('configure');expected=defaults(True,8,0.,128.,True,True,True,'positioned_rois')
    assert first['contract']=='p10k.histogram-defaults.ui.001' and first['context']==context and first['state']=='observed'
    assert values(first['settings_before']['values'])==defaults() and values(first['settings_after']['values'])==values(first['settings_after_close']['values'])==expected
    result=first['calculated']['result'];assert result['plane_count']==8 and result['finite']==32 and result['counts']==[4,4,8,4,4,8,0,0] and result['mean']==pytest.approx(50.5,rel=0,abs=1e-12)
    saved=json.loads(path.read_bytes());assert saved['values']['future_histogram']==original['values']['future_histogram'] and saved['extension']==original['extension'] and saved['extras']==original['extras']
    after_save=path.read_bytes();second=observe('inspect');assert path.read_bytes()==after_save
    opened=second['opened'];assert opened['scientific_target'] and opened['custom'] and opened['log'] and opened['bins']=='8' and opened['lower']=='0' and opened['upper']=='128' and opened['result'] is None
    assert second['target']['target']['ranges']==[[1,2,1],[1,2,1],[2,3,1]] and second['target']['roi_policy']=='positioned_rois' and second['target']['selected_plane_count']==1
    for report in [first,second]:
        assert {k:v for k,v in report['before'].items() if k!='workflow'}=={k:v for k,v in report['after'].items() if k!='workflow'}
        assert report['after']['undo']==0 and not report['after']['dirty']
        assert report['after']['workflow']==([first['calculated']['command']] if report is first else [])
        assert not report['acceptance_claim'] and len(report['frames'])<256
