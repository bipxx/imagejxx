import time
import numpy as np
import pytest
import imagejxx as ij
from imagejxx.ui.app import Editor
from test_virtual_tiff import write


def test_background_view_setting_and_preference_control(tmp_path):
    schema = next(s for s in ij.Settings.schema() if s['name'] == 'async_viewport')
    assert schema['group'] == 'Resources' and schema['default'] is False
    path = tmp_path / 'preferences.json'
    editor = Editor(preferences=path)
    editor.update_settings(async_viewport=True, persist=True)
    assert Editor(preferences=path).settings['async_viewport'] is True
    editor.action('show_preferences', search='Background virtual views')
    report = editor.render_headless()
    assert report['preferences_controls'] == ['async_viewport']
    assert report['settings']['async_viewport'] is True


@pytest.mark.parametrize('value', [0, 1, 'true', None, [], 0.5])
def test_background_view_setting_rejects_non_boolean(value, tmp_path):
    editor = Editor(preferences=tmp_path / 'preferences.json')
    with pytest.raises((ValueError, TypeError, OverflowError)):
        editor.update_settings(async_viewport=value, persist=False)
    assert editor.settings['async_viewport'] is False


@pytest.mark.parametrize('xyz', [False, True])
def test_file_backed_background_views_use_captured_scientific_pixels(xyz, tmp_path):
    # Independent wire writer and a real virtual TIFF reader; neither display
    # result nor expected orientation is derived from the viewport renderer.
    values = (np.arange(2)[:, None, None, None, None] * 80
              + np.arange(2)[None, :, None, None, None] * 40
              + np.arange(4)[None, None, :, None, None] * 10
              + np.arange(6)[None, None, None, :, None] * 8
              + np.arange(8)[None, None, None, None, :]).astype(np.uint16)
    path = tmp_path / 'volume.tif'
    write(path, values)
    data = ij.scientific_io.open_virtual_image_series(path).dataset
    assert not data.has_eager_planes
    owner = ij.Document(data)
    editor = Editor(preferences=tmp_path / 'preferences.json')
    editor.update_settings(async_viewport=True, persist=False)
    editor._editor.add_virtual_document(owner)
    document = editor.live_document()
    display = document.native_snapshot().display
    display.properties = {**display.properties, 'display_range': [0.0, 255.0]}
    document.set_display(display)
    document.set_view(t=1, c=1, crosshair=[2, 3, 2], orthogonal=xyz)
    # The existing channel-panel diagnostic capture exposes actual CPU builds.
    editor.action("show_channel_display")
    deadline = time.monotonic() + 10
    while True:
        report = editor.render_headless(frames=30)
        planes = report['orthogonal_planes'] if xyz else [report['ordinary_pixels']]
        if all(p['readback']['available'] for p in planes):
            break
        assert time.monotonic() < deadline
    assert report['viewport_jobs']['published'] >= len(planes)
    for i, plane in enumerate(planes):
        expected = values[1, 1, 2] if i == 0 else values[1, 1, :, 3, :] if i == 1 else values[1, 1, :, :, 2]
        assert plane['readback']['source_tcz'] == [1, 1, 2]
        assert plane['readback']['source']['pixels'] == expected.ravel().tolist()
        assert plane['readback']['rendered_rgb']['pixels'] == np.repeat(expected[..., None], 3, axis=-1).ravel().tolist()
