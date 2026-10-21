"""Rounded creation algebra, strict record versions and native publication."""
from copy import deepcopy
import numpy as np
import pytest
import imagejxx as ij

SHAPE = (21, 29)
OPERATIONS = ['REPLACE', 'ADD', 'SUBTRACT', 'INTERSECT', 'XOR']

def rounded(diameter=7):
    return ij.Selection('rounded_rectangle', [(8, 6), (25, 18)], attributes={
        'corner_diameter': diameter, 'future': {'values': [False, 9]}, 'label': 'draft'})

def owner(base_kind='rectangle'):
    pixels = np.arange(21*29, dtype=np.uint16).reshape(SHAPE)
    document = ij.Document(ij.Dataset(pixels, axes='YX'))
    if base_kind is not None:
        base = ij.Selection('rectangle', [(2, 3), (17, 14)])
        if 'rounded' in base_kind:
            base = ij.Selection('rounded_rectangle', base.points, attributes={'corner_diameter': 9})
            if base_kind.startswith('rotated_'):
                base = ij.SelectionEditor(base, SHAPE).rotated(23)
        document.select(base)
    return document, pixels

def request(operation='REPLACE', candidate=None):
    return ij.SelectionCreationRequest(rounded() if candidate is None else candidate,
                                       getattr(ij.SelectionCreationOperation, operation))

@pytest.mark.parametrize('diameter', [0, 1, 7, 2**31-1])
@pytest.mark.parametrize('operation', OPERATIONS)
@pytest.mark.parametrize('base_kind', [None, 'rectangle', 'rounded_rectangle', 'rotated_rounded_rectangle'])
def test_rounded_creation_operations_and_round_trip(base_kind, operation, diameter):
    document, pixels = owner(base_kind)
    before = document.snapshot(); candidate = rounded(diameter)
    req = request(operation, candidate)
    if base_kind is None and operation not in ['REPLACE', 'ADD']:
        with pytest.raises(ValueError): ij.resolve_selection_creation_command(before, req)
        return
    command = ij.resolve_selection_creation_command(before, req)
    assert command.record()['version'] == 2
    ij.validate_selection_creation_command_record(command.record())
    replay = ij.selection_creation_command_from_record(before, command.record())
    assert replay.result == command.result
    result = ij.compose_selection_creation(before, req)
    if operation == 'REPLACE' or base_kind is None:
        assert result == candidate and result.kind == 'rounded_rectangle'
    else:
        a, b = before.selection.mask(SHAPE) != 0, candidate.mask(SHAPE) != 0
        expected = {'ADD': a | b, 'SUBTRACT': a & ~b, 'INTERSECT': a & b, 'XOR': a ^ b}[operation]
        assert result.kind == 'mask'
        np.testing.assert_array_equal(result.mask(SHAPE) != 0, expected)
    tx = ij.prepare_selection_creation(command); document.commit(tx)
    assert document.snapshot().selection == result
    assert document.snapshot().previous_selection == before.selection
    assert document.undo_steps == 0 and document.redo_steps == 0
    np.testing.assert_array_equal(document.snapshot().dataset.numpy('YX'), pixels)

@pytest.mark.parametrize('base_kind', [None, 'rectangle', 'rounded_rectangle', 'rotated_rounded_rectangle'])
def test_legacy_candidate_with_rounded_base_versions_record_explicitly(base_kind):
    document, _ = owner(base_kind)
    candidate = ij.Selection('rectangle', [(8, 6), (25, 18)], attributes={'corner_diameter': 7})
    record = ij.resolve_selection_creation_command(document.snapshot(), request(candidate=candidate)).record()
    assert record['version'] == (2 if base_kind and 'rounded' in base_kind else 1)
    ij.validate_selection_creation_command_record(record)
    forged = deepcopy(record); forged['version'] = 3-record['version']
    calls = []
    with pytest.raises(ValueError):
        ij.validate_selection_creation_command_record(forged, checkpoint=lambda: calls.append(True))
    assert not calls

@pytest.mark.parametrize('version', [0, 1, 3, True, 2.0, '2'])
def test_rounded_record_does_not_accept_old_unknown_or_coerced_version(version):
    document, _ = owner()
    record = ij.resolve_selection_creation_command(document.snapshot(), request()).record()
    record['version'] = version
    with pytest.raises((ValueError, TypeError)):
        ij.validate_selection_creation_command_record(record)

def test_replay_retains_original_equation_and_rebind_uses_explicit_request():
    document, _ = owner('rounded_rectangle')
    command = ij.resolve_selection_creation_command(document.snapshot(), request('SUBTRACT'))
    record = command.record()
    fresh = ij.Selection('ellipse', [(1, 1), (6, 6)])
    document.select(fresh)
    replay = ij.selection_creation_command_from_record(document.snapshot(), record)
    assert replay.result == command.result
    tx = ij.prepare_selection_creation(replay); document.commit(tx)
    assert document.snapshot().previous_selection == fresh
    rebound = ij.rebind_selection_creation_command(document.snapshot(), record, request('ADD', rounded(3)))
    assert rebound.record()['version'] == 2 and rebound.request.candidate.corner_diameter == 3
    forged = deepcopy(record); forged['result']['mask'] = 'AA=='
    other, _ = owner()
    with pytest.raises(ValueError):
        ij.rebind_selection_creation_command(other.snapshot(), forged, request())

@pytest.mark.parametrize('diameter', [None, True, -1, 2.5, 2**31])
def test_invalid_diameter_fails_before_callbacks(diameter):
    document, _ = owner(); calls = []
    with pytest.raises(ValueError):
        ij.resolve_selection_creation_command(document.snapshot(), request('ADD', rounded(diameter)),
                                              checkpoint=lambda: calls.append(True))
    assert not calls

def test_boolean_curve_scratch_is_charged_before_callbacks():
    document, _ = owner(); req = request('ADD')
    estimate = ij.estimate_selection_creation(document.snapshot(), req)
    assert estimate.working_bytes > 512*1024 + (21*29+7)//8 + 21*29*8
    limits = ij.SelectionCreationLimits(); limits.max_working_bytes = 512*1024-1
    calls = []
    with pytest.raises(ValueError):
        ij.resolve_selection_creation_command(document.snapshot(), req, limits, checkpoint=lambda: calls.append(True))
    record = ij.resolve_selection_creation_command(document.snapshot(), req).record()
    with pytest.raises(ValueError):
        ij.validate_selection_creation_command_record(record, limits, checkpoint=lambda: calls.append(True))
    assert not calls
