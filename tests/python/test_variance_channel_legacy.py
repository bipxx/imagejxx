"""Single-channel desktop adapter preserves canonical display/history pairs."""
import copy
import imagejxx as ij
import numpy as np
import pytest


@pytest.mark.parametrize('dtype', [np.uint16, np.float32])
@pytest.mark.parametrize('mode', ['composite', 'color', 'grayscale'])
@pytest.mark.parametrize('version,projection', [(1,None), (2,'max'), (2,'min')])
def test_legacy_channel_variance_publication(tmp_path, dtype, mode, version, projection):
    from imagejxx.ui.app import Editor
    pixels = np.zeros((5,5), dtype)
    pixels[2,2] = 80
    owner = ij.Document(ij.Dataset(pixels, axes='YX'))
    channel = {'schema':'imagejxx.channel-display', 'version':version, 'mode':mode,
               'channels':[{'visible':False, 'range':[1.,127.], 'lut':{'name':'Green'}}]}
    if projection is not None:
        channel['projection'] = projection
    display = owner.snapshot().display
    display.properties = {'display_range':[99.,199.], 'lut':'Inverted',
                          'manual_threshold':{'version':1, 'lower':4., 'upper':8., 't':0, 'c':0, 'z':0}}
    owner.set_display(ij.with_channel_display(display, owner.snapshot().dataset, channel))
    before = owner.snapshot()
    editor = Editor(preferences=tmp_path/'prefs.json')
    editor.add_native_document(owner)
    editor.apply_operation('rank_filter_imagej', {'method':'variance','radius':0}, asynchronous=False)
    expected = copy.deepcopy(before.display.properties)
    expected['channel_display']['channels'][0]['range'] = [0.,1024. if dtype==np.uint16 else 1020.]
    expected.pop('manual_threshold')
    assert owner.snapshot().display.properties == expected and owner.undo_steps == 1
    owner.undo()
    assert owner.snapshot().display.properties == before.display.properties
    assert owner.snapshot().dataset.numpy('YX').tobytes() == pixels.tobytes()
    owner.redo()
    assert owner.snapshot().display.properties == expected


@pytest.mark.parametrize('dtype', [np.uint16, np.float32])
def test_legacy_channel_empty_mask_stays_read_free(tmp_path, dtype):
    from imagejxx.ui.app import Editor
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.zeros((4,4), dtype), axes='YX').observe_reads(reads))
    edit = owner.begin_edit()
    edit.select(ij.Selection.from_mask(np.zeros((4,4), np.uint8)))
    owner.commit(edit)
    channel = {'schema':'imagejxx.channel-display','version':1,'mode':'grayscale',
               'channels':[{'visible':True,'range':[0.,0.],'lut':{'name':'Grays'}}]}
    owner.set_display(ij.with_channel_display(owner.snapshot().display, owner.snapshot().dataset, channel))
    owner = ij.Document(owner.snapshot())
    before = owner.snapshot()
    editor = Editor(preferences=tmp_path/'prefs.json')
    editor.add_native_document(owner)
    editor.apply_operation('rank_filter_imagej', {'method':'variance','radius':0}, asynchronous=False)
    assert reads.counts().plane_reads == 0 and owner.undo_steps == 0
    assert owner.snapshot().display.properties == before.display.properties
