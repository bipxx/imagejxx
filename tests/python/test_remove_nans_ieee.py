"""Full IEEE Remove NaNs parity: actual runner bytes and source-bound execution."""
import copy
import json
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest

IEEE = ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32
ROWS = json.loads((Path(__file__).parents[1] / 'fixtures' /
                   'remove-nans-ieee-imagej154r.json').read_bytes())['cases']


def decode(row, key):
    return np.stack([np.frombuffer(bytes.fromhex(raw), '<u4').copy().view(np.float32).reshape(20, 25)
                     for raw in row[key + '_hex']])


def data(array):
    return ij.Dataset(array, axes='ZYX' if array.ndim == 3 else 'YX', sample_policy=IEEE)


def selection(owner, row):
    if row['selection']:
        edit = owner.begin_edit()
        edit.select(ij.Selection('rectangle', [[2, 3], [21, 16]]) if row['selection'] == 1 else
                    ij.Selection('polygon', [[2, 3], [20, 2], [18, 17], [5, 15]]))
        owner.commit(edit)


def label(row):
    return f"s{row['signal']}-r{row['radius']}-m{row['mode']}-a{row['selection']}"


@pytest.mark.parametrize('row', ROWS, ids=label)
def test_pixel_runner_words(row):
    source = decode(row, 'input')
    before = source.tobytes()
    owner = ij.Document(data(source))
    selection(owner, row)
    command = ij.resolve_remove_nans_command(owner.snapshot())
    region = ij.remove_nans_command_region(command.target, 0)
    result = source.copy()
    for z in range(3):
        if row['mode'] == 0 and z != 1:
            continue
        output = ij.remove_nans_imagej(source[z], row['radius'], extent=region['extent'])
        assert output.dtype == np.float32 and not np.shares_memory(output, source)
        if region['mask'] is not None:
            outside = ~region['mask'].astype(bool)
            output.view(np.uint32)[outside] = source[z].view(np.uint32)[outside]
        result[z].view(np.uint32)[:] = output.view(np.uint32)
    assert result.tobytes() == b''.join(bytes.fromhex(v) for v in row['output_hex'])
    assert source.tobytes() == before
    if row['selection'] == 0:
        registry = ij._native.run_operation('remove_nans_imagej', source[1], {'radius': row['radius']})
        assert registry.pixels.tobytes() == bytes.fromhex(row['output_hex'][1])


@pytest.mark.parametrize('legacy', [False, True], ids=['scientific', 'legacy'])
@pytest.mark.parametrize('row', ROWS, ids=label)
def test_command_runner_words_and_history(row, legacy):
    source = decode(row, 'input')
    owner = ij.Document(data(source))
    selection(owner, row)
    view = owner.snapshot().display
    view.crosshair = [12, 10, 1]
    view.properties = {'display_range': [7., 127.], 'manual_threshold':
                       {'version': 1, 'lower': 4., 'upper': 8., 't': 0, 'c': 0, 'z': 1}}
    owner.set_display(view)
    before = owner.snapshot()
    request = ij.TargetRequest()
    if row['mode'] == 1:
        request.scope = ij.TargetScope.ALL_Z
    command = ij.resolve_remove_nans_command(before, request, ij.RemoveNaNsCommandOptions(row['radius']))
    record = command.record()
    assert ij.rank_command_from_record(before, record).record() == record
    options = ij.ExecutionOptions()
    if legacy:
        options.dataset = ij.DatasetPolicy.LEGACY_DESKTOP
        options.geometry = ij.GeometryPolicy.LEGACY_DESKTOP
    result = ij.prepare_operation(command.operation, command.target, command.parameters(), options=options)
    preview = result.preview
    expected = b''.join(bytes.fromhex(v) for v in row['output_hex'])
    assert preview.dataset.sample_policy == IEEE and preview.dataset.numpy('ZYX').tobytes() == expected
    assert preview.display.properties == before.display.properties
    assert before.dataset.numpy('ZYX').tobytes() == source.tobytes()
    owner.commit(result.begin_edit())
    changed = expected != source.tobytes()
    assert owner.undo_steps == int(changed)
    if changed:
        owner.undo()
        assert owner.snapshot().dataset.numpy('ZYX').tobytes() == source.tobytes()
        owner.redo()
    assert owner.snapshot().dataset.numpy('ZYX').tobytes() == expected
    assert owner.snapshot().display.properties == before.display.properties
    assert source.tobytes() == before.dataset.numpy('ZYX').tobytes()


@pytest.mark.parametrize('layout', ['contiguous', 'transpose', 'reverse', 'fortran', 'big_endian', 'unaligned', 'readonly'])
def test_empty_write_extent_retains_every_source_word(layout):
    row = next(r for r in ROWS if r['signal'] == 7)
    words = decode(row, 'input')[1].view(np.uint32).copy()
    if layout == 'transpose':
        words = words.T
    elif layout == 'reverse':
        words = words[::-1, ::-1]
    elif layout == 'fortran':
        words = np.asfortranarray(words)
    elif layout == 'big_endian':
        words = words.astype('>u4')
    elif layout == 'unaligned':
        backing = bytearray(1 + words.nbytes)
        backing[1:] = words.tobytes()
        words = np.ndarray(words.shape, dtype='<u4', buffer=backing, offset=1)
    source = words.view(np.dtype(words.dtype.str.replace('u', 'f')))
    if layout == 'readonly':
        source.setflags(write=False)
    before = source.tobytes()
    expected = words.astype('<u4').tobytes()
    actual = ij.remove_nans_imagej(source, extent=[0, 0, 0, 0])
    assert actual.tobytes() == expected and source.tobytes() == before


@pytest.mark.parametrize('operation', ['invert', 'rank_filter_imagej', 'gaussian_blur_imagej', 'top_hat_imagej', 'remove_outliers_imagej'])
def test_other_ieee_processing_remains_read_free_refusal(operation):
    source = decode(ROWS[-1], 'input')
    reads = ij.DatasetReadObserver()
    owner = ij.Document(data(source).observe_reads(reads))
    target = ij.resolve_target(owner.snapshot(), operation=operation)
    with pytest.raises(ValueError, match='qualified operation profile'):
        ij.prepare_operation(operation, target)
    assert reads.counts().plane_reads == reads.counts().region_reads == 0
    assert ij.resolve_remove_nans_command(owner.snapshot()).operation == 'remove_nans_imagej'
    assert reads.counts().plane_reads == 0


@pytest.mark.parametrize('word,changed', [(0x7fc00000, False), (0x7f800001, True), (0xff812345, True),
                                        (0x7f800000, False), (0xff800000, False)])
def test_bitwise_noop_and_canonicalization(word, changed):
    source = np.full((3, 3), word, np.uint32).view(np.float32)
    owner = ij.Document(data(source))
    owner.mark_saved()
    command = ij.resolve_remove_nans_command(owner.snapshot())
    result = ij.prepare_operation(command.operation, command.target, command.parameters())
    owner.commit(result.begin_edit())
    expected = np.full((3, 3), 0x7fc00000 if changed else word, np.uint32)
    assert owner.snapshot().dataset.numpy('YX').tobytes() == expected.tobytes()
    assert owner.undo_steps == int(changed)
    if changed:
        owner.undo()
        assert owner.snapshot().dataset.numpy('YX').tobytes() == source.tobytes()
        assert not owner.snapshot().dirty


def test_record_metadata_rebind_session_and_output_words():
    source = decode(next(r for r in ROWS if r['signal'] == 7 and r['selection'] == 2), 'input')
    reads = ij.DatasetReadObserver()
    owner = ij.Document(data(source).observe_reads(reads))
    command = ij.resolve_remove_nans_command(owner.snapshot())
    record = command.record()
    assert ij.rank_command_from_record(owner.snapshot(), record).record() == record
    assert reads.counts().plane_reads == 0
    other = ij.Document(data(source))
    with pytest.raises(ValueError):
        ij.rank_command_from_record(other.snapshot(), record)
    rebound = ij.rebind_rank_command(other.snapshot(), record, ij.TargetRequest())
    assert rebound.operation == 'remove_nans_imagej'
    frozen = copy.deepcopy(record)
    result = ij.prepare_operation(rebound.operation, rebound.target, rebound.parameters())
    other.commit(result.begin_edit())
    encoded = ij.encode_session(ij.SessionCapture([other.snapshot()], workflow=[rebound.record()]))
    loaded = ij.decode_session(encoded).documents[0].snapshot()
    assert loaded.dataset.sample_policy == IEEE
    assert loaded.dataset.numpy('ZYX').tobytes() == other.snapshot().dataset.numpy('ZYX').tobytes()
    assert record == frozen and owner.snapshot().dataset.numpy('ZYX').tobytes() == source.tobytes()


@pytest.mark.parametrize('scientific', [False, True], ids=['legacy', 'scientific'])
@pytest.mark.parametrize('word', [0x7f800001, 0xff812345, 0x7fc12345, 0xffc00001])
def test_python_editor_ieee_inplace_and_new_document(tmp_path, scientific, word):
    from imagejxx.ui.app import Editor
    source = np.full((5, 5), 7, np.float32)
    source.view(np.uint32)[2, 2] = word
    source.view(np.uint32)[0, 0] = 0x7f800000
    source.view(np.uint32)[4, 4] = 0xff800000
    source.view(np.uint32)[1, 1] = 0x80000000
    if scientific:
        source = np.stack([source for _ in range(12)]).reshape(2, 2, 3, 5, 5)
    axes = 'TCZYX' if scientific else 'YX'
    owner = ij.Document(ij.Dataset(source, axes=axes, sample_policy=IEEE))
    editor = Editor(preferences=tmp_path / 'preferences.json')
    editor.add_native_document(owner)
    editor.action('show_operation', operation='remove_nans_imagej')
    editor.apply_remove_nans(options=ij.RemoveNaNsCommandOptions(1), wait=True)
    expected = source.copy()
    if scientific:
        expected[0, 0, 0, 2, 2] = 7
    else:
        expected[2, 2] = 7
    assert owner.snapshot().dataset.numpy(axes).tobytes() == expected.tobytes()
    assert owner.undo_steps == 1
    owner.undo()
    assert owner.snapshot().dataset.numpy(axes).tobytes() == source.tobytes()
    owner.redo()
    assert owner.snapshot().dataset.numpy(axes).tobytes() == expected.tobytes()
    owner.undo()
    editor.apply_remove_nans(ij.TargetRequest(destination=ij.OutputDestination.NEW_DOCUMENT),
                             ij.RemoveNaNsCommandOptions(1), wait=True)
    assert owner.snapshot().dataset.numpy(axes).tobytes() == source.tobytes()
    assert editor._editor.native_snapshot().dataset.sample_policy == IEEE
    assert editor._editor.native_snapshot().dataset.numpy(axes).tobytes() == expected.tobytes()
