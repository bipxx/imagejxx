import pytest
from imagejxx.ui.app import Editor

def test_render_resource_setting_and_rendered_control(tmp_path):
    path=tmp_path/'prefs.json';editor=Editor(preferences=path)
    assert editor.render_memory_info==dict(limit_bytes=1024*1048576,retained_bytes=0,working_bytes=0,peak_reserved_bytes=0,denied_requests=0)
    editor.update_settings(render_memory_mb=2,persist=True)
    assert editor.render_memory_info['limit_bytes']==2*1048576
    info=editor.render_memory_info;info['limit_bytes']=0
    assert editor.render_memory_info['limit_bytes']==2*1048576
    editor.action('show_preferences',search='Render surfaces')
    rendered=editor.render_headless()
    assert rendered['preferences_controls']==['render_memory_mb']
    reopened=Editor(preferences=path)
    assert reopened.render_memory_info['limit_bytes']==2*1048576

@pytest.mark.parametrize('value',[0,-1,True,1.5,'2',None,1048577])
def test_invalid_render_setting_is_atomic(value,tmp_path):
    editor=Editor(preferences=tmp_path/'prefs.json');before=editor.render_memory_info
    with pytest.raises((ValueError,TypeError,OverflowError)):
        editor.update_settings(render_memory_mb=value,persist=False)
    assert editor.render_memory_info==before

def test_failed_render_preference_write_preserves_budget(tmp_path):
    path=tmp_path/'blocked.json';editor=Editor(preferences=path);path.mkdir();before=editor.render_memory_info
    with pytest.raises((ValueError,RuntimeError,OSError)):
        editor.update_settings(render_memory_mb=1,persist=True)
    assert editor.render_memory_info==before
