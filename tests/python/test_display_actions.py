import json
import numpy as np
import pytest
import imagejxx as ij
from imagejxx.ui.app import Editor
from test_virtual_tiff import write


def counts(observer):
    value = observer.counts()
    return value.plane_reads, value.region_reads


@pytest.mark.parametrize('dtype', [np.uint8, np.uint16, np.float32, np.float64])
@pytest.mark.parametrize('samples', [1, 3, 4])
def test_manual_range_and_palette_actions_do_not_read_source(dtype, samples, tmp_path):
    pixels = np.arange(4 * 5 * samples).reshape(4, 5, samples).astype(dtype)
    observer = ij.DatasetReadObserver()
    data = ij.Dataset(pixels, axes='YXS').observe_reads(observer)
    owner = ij.Document(data)
    editor = Editor(preferences=tmp_path / 'preferences.json')
    editor._editor.add_virtual_document(owner)
    before = counts(observer)
    snapshot = owner.snapshot()
    editor.action('display_range', minimum=2, maximum=123)
    editor.action('lut', name='Red')
    path = tmp_path / 'palette.json'
    editor.action('save_lut', path=str(path))
    assert json.loads(path.read_text())['lut'] == [[i, 0, 0] for i in range(256)]
    table = [[i, 255 - i, i * 3 % 256] for i in range(256)]
    path.write_text(json.dumps({'lut': table}))
    editor.action('load_lut', path=str(path))
    assert counts(observer) == before
    after = owner.snapshot()
    assert after.display.properties['display_range'] == [2, 123]
    assert after.display.properties['custom_lut'] == table
    assert after.revisions.pixels == snapshot.revisions.pixels
    assert after.revisions.metadata == snapshot.revisions.metadata
    assert not after.dirty and owner.undo_steps == 0


def test_virtual_tiff_display_configuration_and_invalid_palette_are_metadata_only(tmp_path):
    path = tmp_path / 'volume.tif'
    pixels = np.arange(20, dtype=np.uint16).reshape(4, 5)
    write(path, pixels)
    observer = ij.DatasetReadObserver()
    data = ij.scientific_io.open_virtual_image_series(path).dataset.observe_reads(observer)
    owner = ij.Document(data)
    editor = Editor(preferences=tmp_path / 'preferences.json')
    editor._editor.add_virtual_document(owner)
    assert counts(observer) == (0, 0)
    editor.action('display_range', minimum=0, maximum=255)
    editor.action('lut', name='Ice')
    palette = tmp_path / 'palette.json'
    table = [[i, 255 - i, 0] for i in range(256)]
    palette.write_text(json.dumps({'lut': table}))
    editor.action('load_lut', path=str(palette))
    stable = owner.snapshot()
    bad = [row.copy() for row in table]
    bad[200][1] = 999
    palette.write_text(json.dumps({'lut': bad}))
    with pytest.raises((ValueError, RuntimeError)):
        editor.action('load_lut', path=str(palette))
    assert counts(observer) == (0, 0)
    assert owner.snapshot().display.properties == stable.display.properties
    assert owner.snapshot().revisions.display == stable.revisions.display
    np.testing.assert_array_equal(data.plane(), pixels)
    assert counts(observer) == (1, 0)


def test_invalid_manual_range_is_atomic_without_virtual_reads(tmp_path):
    path = tmp_path / 'input.tif'
    write(path, np.zeros((2, 3), dtype=np.uint16))
    observer = ij.DatasetReadObserver()
    data = ij.scientific_io.open_virtual_image_series(path).dataset.observe_reads(observer)
    owner = ij.Document(data)
    editor = Editor(preferences=tmp_path / 'preferences.json')
    editor._editor.add_virtual_document(owner)
    before = owner.snapshot()
    for minimum, maximum in [(9, 1), (9, 9)]:
        with pytest.raises((ValueError, RuntimeError)):
            editor.action('display_range', minimum=minimum, maximum=maximum)
    assert counts(observer) == (0, 0)
    assert owner.snapshot().display.properties == before.display.properties
    assert owner.snapshot().revisions.display == before.revisions.display


def test_image_menu_and_manual_form_submit_without_decoding_virtual_tiff(tmp_path):
    path = tmp_path / 'input.tif'
    write(path, np.arange(20, dtype=np.uint16).reshape(4, 5))
    observer = ij.DatasetReadObserver()
    data = ij.scientific_io.open_virtual_image_series(path).dataset.observe_reads(observer)
    owner = ij.Document(data)
    editor = Editor(preferences=tmp_path / 'preferences.json')
    editor._editor.add_virtual_document(owner)

    def click(point):
        return [{'type': 'position', 'space': 'screen', 'point': point},
                {'type': 'button', 'name': 'left', 'down': True},
                {'type': 'button', 'name': 'left', 'down': False}]

    report = editor.render_headless()
    image_menu = report['host_controls']['menu.Image']['point']
    report = editor.render_headless(events=click(image_menu))
    form = report['host_controls']['menu.form.display_range']['point']
    report = editor.render_headless(events=click(image_menu) + click(form))
    assert report['dialog']['values']['maximum'] == 65535
    # Generic modals must open within the event sequence. Their centering can
    # differ across contexts; navigate from the first field to OK by keyboard.
    fresh = Editor(preferences=tmp_path / 'fresh-preferences.json')
    fresh_owner = ij.Document(data)
    fresh._editor.add_virtual_document(fresh_owner)
    events = click(image_menu) + click(form) + [{'type': 'key', 'name': 'Shift', 'down': True}]
    for _ in range(2):
        events += [{'type': 'key', 'name': 'Tab', 'down': True},
                   {'type': 'key', 'name': 'Tab', 'down': False}]
    events += [{'type': 'key', 'name': 'Shift', 'down': False},
               {'type': 'key', 'name': 'Enter', 'down': True},
               {'type': 'key', 'name': 'Enter', 'down': False}]
    report = fresh.render_headless(events=events)
    assert not report['dialog']['open']
    assert fresh_owner.snapshot().display.properties['display_range'] == [0, 65535]
    assert counts(observer) == (0, 0)
