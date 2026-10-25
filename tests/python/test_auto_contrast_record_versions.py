"""Outer Auto record versions, independently of channel-display versions."""
import copy

import imagejxx as ij
import numpy as np
import pytest

from test_auto_contrast import _request, _state


@pytest.mark.parametrize('selection', ['whole', 'rectangle', 'mask'])
def test_auto_v2_retains_legacy_geometry_and_unknown_version_refuses_quietly(selection):
    pixels = np.repeat([1, 2, 3, 4], [1, 10, 11, 78]).astype(np.uint8).reshape(1, 100)
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(pixels, axes='YX').observe_reads(reads))
    display = owner.snapshot().display
    display.properties = {'channel_display': _state()}
    owner.set_display(display)
    if selection == 'rectangle':
        owner.select(ij.Selection('rectangle', [(0, 0), (100, 1)]))
    elif selection == 'mask':
        owner.select(ij.Selection.from_mask(np.full((1, 100), 255, np.uint8)))
    before = owner.snapshot()
    command = ij.resolve_auto_contrast_command(before, _request(whole=selection == 'whole'))
    original = command.record()
    assert original['version'] == 1
    assert command.resolved_range == [1., 2.]
    reads_before = reads.counts().plane_reads
    assert reads_before == 1
    version_two = copy.deepcopy(original)
    version_two['version'] = 2
    ij.validate_auto_contrast_command_record(version_two)
    replay = ij.auto_contrast_command_from_record(before, version_two)
    assert replay.record() == version_two
    assert replay.resolved_range == [1., 2.]
    assert replay.selection_provenance == command.selection_provenance
    assert command.record() == original
    bad = copy.deepcopy(version_two)
    bad['version'] = 3
    calls = []
    with pytest.raises(ValueError):
        ij.validate_auto_contrast_command_record(bad)
    with pytest.raises(ValueError):
        ij.auto_contrast_command_from_record(before, bad)
    with pytest.raises(ValueError):
        ij.rebind_auto_contrast_command(before, bad, _request(), checkpoint=lambda: calls.append(True))
    assert calls == [] and reads.counts().plane_reads == reads_before
    assert owner.snapshot().shares_data(before)
    assert owner.undo_steps == owner.redo_steps == 0
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('YX'), pixels)
