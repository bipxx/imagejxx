"""Native-path manual threshold V3; legacy V1/V2 grammars remain strict."""
import copy
import struct

import imagejxx as ij
import numpy as np
import pytest

from test_path_filter_admission_ui import pinned_members
from test_path_filter_ui import PATH


def source(dtype=np.uint8, primitive=False):
    z, y, x = np.indices((2, 17, 23))
    pixels = np.where((x < 11) & (y < 11), 100 + z, 240).astype(dtype)
    owner = ij.Document(ij.Dataset(pixels, axes='ZYX'))
    selection = ij.Selection('rectangle', [(2, 1), (10, 9)]) if primitive else (
        ij.Selection.from_path(ij.ImageJCompositePath(PATH)).with_attributes(
            {'opaque': ['retain', True, 2**64-1]}))
    owner.select(selection)
    view = owner.snapshot().display
    view.crosshair = [11, 8, 1]
    owner.set_display(view)
    return owner, pixels


def request(all_z):
    return ij.TargetRequest(scope=ij.TargetScope.ALL_Z if all_z else ij.TargetScope.CURRENT_PLANE,
                            roi=ij.RoiPolicy.ACTIVE_SELECTION)


@pytest.mark.parametrize('dtype', [np.uint8, np.uint16, np.float32, np.float64])
@pytest.mark.parametrize('all_z', [False, True])
def test_path_v3_exact_record_strict_replay_and_inclusive_pixels(dtype, all_z):
    owner, pixels = source(dtype)
    before = owner.snapshot()
    command = ij.resolve_manual_threshold_command(before, ij.ManualThresholdRange(101, 255), request(all_z))
    record = command.record()
    assert set(record) == {'schema_version', 'operation', 'profile', 'lower', 'upper', 'target', 'selection'}
    assert (record['schema_version'], record['operation'], record['profile']) == (3, 'threshold', 'manual_raw_inclusive_v3')
    assert record['lower'] == 101 and record['upper'] == 255
    assert record['selection'] == {'kind': 'path', 'points': [], 'width': 1.0,
                                    'attributes': {'opaque': ['retain', True, 2**64-1]}, 'path': PATH}
    assert [struct.pack('<d', value) for value in record['selection']['path']] == [struct.pack('<d', value) for value in PATH]
    original_record = copy.deepcopy(record)
    ij.validate_manual_threshold_command_record(record)
    assert ij.manual_threshold_command_from_record(before, record).record() == record

    # A later camera must not retarget the immutable command.
    view = owner.snapshot().display
    view.crosshair = [0, 0, 0]
    owner.set_display(view)
    replay = ij.manual_threshold_command_from_record(owner.snapshot(), record)
    app = ij.Application()
    app.adopt(owner)
    prepared = app.prepare('threshold', replay.target, replay.parameters())
    app.publish(prepared)
    expected = pixels.copy()
    for z in (0, 1) if all_z else (1,):
        for x, y in pinned_members():
            expected[z, y, x] = 255 if 101 <= pixels[z, y, x] <= 255 else 0
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('ZYX'), expected)
    assert owner.undo_steps == 1
    assert owner.snapshot().selection == before.selection
    assert record == original_record
    owner.undo()
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('ZYX'), pixels)
    owner.redo()
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('ZYX'), expected)


@pytest.mark.parametrize('all_z', [False, True])
def test_primitive_manual_threshold_records_keep_original_versions(all_z):
    owner, _ = source(primitive=True)
    command = ij.resolve_manual_threshold_command(owner.snapshot(), ij.ManualThresholdRange(101, 255), request(all_z))
    record = command.record()
    version = 2 if all_z else 1
    assert record['schema_version'] == version
    assert record['profile'] == f'manual_raw_inclusive_v{version}'
    assert record['selection']['kind'] == 'rectangle'
    ij.validate_manual_threshold_command_record(record)
    assert ij.manual_threshold_command_from_record(owner.snapshot(), record).record() == record


@pytest.mark.parametrize('fault', ['null', 'primitive', 'nonactive', 'profile', 'bool-version',
                                  'unknown-field', 'empty-path', 'nan-path', 'legacy-v1', 'legacy-v2'])
def test_manual_path_v3_malformed_and_legacy_downgrades_are_refused(fault):
    owner, _ = source()
    before = owner.snapshot()
    record = ij.resolve_manual_threshold_command(before, ij.ManualThresholdRange(101, 255)).record()
    if fault == 'null':
        record['selection'] = None
    elif fault == 'primitive':
        record['selection'] = {'kind': 'rectangle', 'points': [[2, 1], [10, 9]], 'width': 1, 'attributes': {}}
    elif fault == 'nonactive':
        record['target']['roi_policy'] = 'whole_plane'
    elif fault == 'profile':
        record['profile'] = 'manual_raw_inclusive_v2'
    elif fault == 'bool-version':
        record['schema_version'] = True
    elif fault == 'unknown-field':
        record['unknown'] = 'refuse'
    elif fault == 'empty-path':
        record['selection']['path'] = []
    elif fault == 'nan-path':
        record['selection']['path'][1] = float('nan')
    else:
        version = int(fault[-1])
        record['schema_version'] = version
        record['profile'] = f'manual_raw_inclusive_v{version}'
    with pytest.raises((ValueError, TypeError, OverflowError)):
        ij.validate_manual_threshold_command_record(record)
    with pytest.raises((ValueError, TypeError, OverflowError)):
        ij.rebind_manual_threshold_command(before, record, request(False))
    assert owner.snapshot().shares_data(before) and owner.undo_steps == 0


def test_path_record_requires_exact_source_and_explicit_rebind_uses_destination_selection():
    owner, _ = source()
    record = ij.resolve_manual_threshold_command(owner.snapshot(), ij.ManualThresholdRange(101, 255)).record()
    original = copy.deepcopy(record)
    other, _ = source(primitive=True)
    before = other.snapshot()
    with pytest.raises((ValueError, ij.RevisionConflict)):
        ij.manual_threshold_command_from_record(before, record)
    rebound = ij.rebind_manual_threshold_command(before, record, request(False)).record()
    assert rebound['schema_version'] == 1 and rebound['selection']['kind'] == 'rectangle'
    assert other.snapshot().selection.kind == 'rectangle' and other.snapshot().shares_data(before)
    assert record == original
    owner.select(ij.Selection('ellipse', [(0, 0), (3, 3)]))
    with pytest.raises((ValueError, ij.RevisionConflict)):
        ij.manual_threshold_command_from_record(owner.snapshot(), record)
