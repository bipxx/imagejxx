import json
import pytest
import imagejxx as ij
from imagejxx.ui.app import Editor

def test_cache_resource_schema_and_persistence(tmp_path):
    setting=next(s for s in ij.Settings.schema() if s['name']=='viewport_cache_mb')
    assert setting['group']=='Resources' and setting['default']==128
    path=tmp_path/'prefs.json'
    editor=Editor(preferences=path)
    assert editor.virtual_view_cache_info==dict(budget_bytes=128*1048576,retained_bytes=0,entries=0,evictions=0)
    editor.update_settings(viewport_cache_mb=3,persist=True)
    assert editor.virtual_view_cache_info['budget_bytes']==3*1048576
    copy=editor.virtual_view_cache_info;copy['budget_bytes']=0
    assert editor.virtual_view_cache_info['budget_bytes']==3*1048576
    reopened=Editor(preferences=path)
    assert reopened.settings['viewport_cache_mb']==3
    assert reopened.virtual_view_cache_info['budget_bytes']==3*1048576

@pytest.mark.parametrize('value',[0,-1,True,1.5,'2',None,1048577])
def test_invalid_cache_setting_is_atomic(value,tmp_path):
    editor=Editor(preferences=tmp_path/'prefs.json')
    before=editor.virtual_view_cache_info
    with pytest.raises((ValueError,TypeError,OverflowError)):
        editor.update_settings(viewport_cache_mb=value,persist=False)
    assert editor.virtual_view_cache_info==before
    assert editor.settings['viewport_cache_mb']==128

def test_cache_resource_control_is_rendered(tmp_path):
    editor=Editor(preferences=tmp_path/'prefs.json')
    editor.action('show_preferences',search='Virtual XY cache')
    info=editor.render_headless()
    assert info['preferences_controls']==['viewport_cache_mb']
    assert info['settings']['viewport_cache_mb']==128
    editor.update_settings(viewport_cache_mb=2,persist=False)
    editor.action('show_preferences',search='Virtual XY cache')
    assert editor.render_headless()['settings']['viewport_cache_mb']==2
    assert editor.virtual_view_cache_info['budget_bytes']==2*1048576

def test_failed_preference_write_preserves_cache_budget(tmp_path):
    path=tmp_path/'blocked.json'
    editor=Editor(preferences=path)
    path.mkdir()
    before=editor.virtual_view_cache_info
    with pytest.raises((ValueError,RuntimeError,OSError)):
        editor.update_settings(viewport_cache_mb=1,persist=True)
    assert editor.virtual_view_cache_info==before
    assert editor.settings['viewport_cache_mb']==128
