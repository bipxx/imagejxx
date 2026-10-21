"""IEEE channel V3: pinned display bytes, independent scientific geometry/wire."""
import copy
import json
import struct
import zlib
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest

IEEE = ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32
FIXTURE = json.loads((Path(__file__).parents[1] / 'fixtures' /
                      'ieee-display-imagej154r.json').read_bytes())
ROWS = [row for row in FIXTURE['cases'] if row['threshold_mode'] == 0]


def source(row):
    return np.frombuffer(bytes.fromhex(row['source_before_hex']), '<u4').copy().view(np.float32).reshape(4, 8)


def dataset(row):
    return ij.Dataset(source(row), axes='YX', sample_policy=IEEE)


def observed_bounds(row):
    values = [struct.unpack('<d', bytes.fromhex(row[field]))[0]
              for field in ['min_f64le', 'max_f64le']]
    words = np.array(values, np.float32).view(np.uint32).tolist()
    if any((word & 0x7f800000) == 0x7f800000 for word in words):
        return {'float32_bits': words}
    return values


@pytest.mark.parametrize('row', ROWS, ids=lambda r:
                         f"s{r['signal']}-r{r['range_mode']}-i{int(r['inverted_lut'])}")
def test_pinned_channel_mapping_and_sessions(row):
    data = dataset(row)
    state = ij.default_channel_display(data, mode='color')
    assert state['version'] == 3 and state['projection'] == 'sum'
    if row['range_mode'] == 0:
        assert state['channels'][0]['range'] == observed_bounds(row)
    if row['range_mode']:
        state['channels'][0]['range'] = [None, [0, 1], [-16, 16], [7, 7]][row['range_mode']]
    if row['inverted_lut']:
        ramp = np.arange(255, -1, -1, dtype=np.uint8)
        state['channels'][0]['lut'] = {'table': np.repeat(ramp[:, None], 3, axis=1).tolist()}
    else:
        state['channels'][0]['lut'] = {'name': 'Grays'}
    ij.validate_channel_display(state, data)
    assert ij.normalize_channel_display(state, data) == state
    assert ij.compose_channel_display(data, state).tobytes() == bytes.fromhex(row['rgb_hex'])
    owner = ij.Document(data)
    original = owner.snapshot().dataset.numpy('YX').tobytes()
    command = ij.resolve_channel_display_command(owner.snapshot(), state)
    record = command.record()
    assert record['version'] == 3 and record['state'] == state
    assert record['source']['sample_policy'] == 'preserve_ieee_float32'
    assert record['display_dependency'] is None and record['navigation'] is None
    ij.validate_channel_display_command_record(record)
    replay = ij.channel_display_command_from_record(owner.snapshot(), record)
    owner.set_display(ij.command_display_state(replay, owner.snapshot()))
    assert owner.undo_steps == owner.redo_steps == 0
    encoded = ij.encode_session(ij.SessionCapture([owner.snapshot()], workflow=[record]))
    literal = json.loads(zlib.decompress(encoded[18:]))
    json.dumps(literal, allow_nan=False)
    assert literal['documents'][0]['display']['properties']['channel_display'] == state
    restored = ij.decode_session(encoded)
    reopened = restored.documents[0].snapshot()
    assert reopened.dataset.numpy('YX').tobytes() == original
    assert ij.channel_display_state(reopened.display, reopened.dataset) == state
    assert restored.workflow == [record]


WORDS = np.array([0xff800000, 0, 0x3f800000, 0x7f800000,
                  0x7fc12345, 0xff800001, 0x80000000, 0x3f000000], np.uint32)
INDICES = np.array([0, 0, 255, 255, 0, 0, 0, 128], np.uint8)


def scientific():
    t, c, z, y, x = np.indices((2, 2, 2, 2, 4))
    positions = (t * 3 + c * 5 + z * 2 + y * 4 + x) % 8
    a = WORDS[positions].view(np.float32)
    data = ij.Dataset(a, axes='TCZYX', sample_policy=IEEE)
    state = ij.default_channel_display(data)
    for c in range(2):
        state['channels'][c]['range'] = [0, 1]
        state['channels'][c]['lut'] = {'table':
            [[i, 255-i, 17] if c == 0 else [255-i, i, 231] for i in range(256)]}
    return a, positions, data, state


@pytest.mark.parametrize('plane', ['xy', 'xz', 'yz'])
@pytest.mark.parametrize('t', [0, 1])
@pytest.mark.parametrize('active', [0, 1])
@pytest.mark.parametrize('projection', ['sum', 'max', 'min'])
def test_scientific_composition(plane, t, active, projection):
    a, positions, data, state = scientific()
    before = a.tobytes()
    state['projection'] = projection
    expected_channels = []
    for c in range(2):
        volume = positions[t, c]
        selected = volume[1] if plane == 'xy' else volume[:, 1, :] if plane == 'xz' else volume[:, :, 1]
        indices = INDICES[selected]
        table = np.array([[i, 255-i, 17] if c == 0 else [255-i, i, 231] for i in range(256)], np.uint16)
        expected_channels.append(table[indices])
    first, second = expected_channels
    expected = (np.minimum(first + second, 255) if projection == 'sum' else
                np.maximum(first, second) if projection == 'max' else np.minimum(first, second))
    actual = ij.compose_channel_display(data, state, plane, 1, t, active)
    assert actual.tobytes() == expected.astype(np.uint8).tobytes()
    assert data.numpy('TCZYX').tobytes() == a.tobytes() == before


@pytest.mark.parametrize('row', [r for r in ROWS if r['range_mode'] == 0 and not r['inverted_lut']],
                         ids=lambda r: f"signal{r['signal']}")
def test_reset_and_rebind_nonfinite_observations(row):
    data = dataset(row)
    state = ij.default_channel_display(data)
    original = copy.deepcopy(state)
    state['channels'][0]['range'] = [0, 1]
    reset, observations = ij.reset_channel_display_ranges(data, state)
    words = source(row).view(np.uint32)
    empty = bool(np.all((words & 0x7f800000) == 0x7f800000))
    assert reset == original
    assert observations == [{'range': observed_bounds(row), 'empty_finite': empty}]
    json.dumps(observations, allow_nan=False)
    owner = ij.Document(data)
    record = ij.resolve_channel_display_command(owner.snapshot(), state, 'reset_range').record()
    assert record['reset'] == {'channels': [0], 'empty_finite': [empty]}
    ij.validate_channel_display_command_record(record)
    other = ij.Document(dataset(row))
    with pytest.raises(ValueError):
        ij.channel_display_command_from_record(other.snapshot(), record)
    rebound = ij.rebind_channel_display_command(other.snapshot(), record, state, 'reset_range')
    assert rebound.state == reset and rebound.record()['version'] == 3


@pytest.mark.parametrize('action', ['select_max', 'select_min', 'disable'])
def test_metadata_only_intent_and_replay(action):
    a, _, data, state = scientific()
    reads = ij.DatasetReadObserver()
    owner = ij.Document(data.observe_reads(reads))
    owner.set_display(ij.with_channel_display(owner.snapshot().display, data, state))
    command = ij.resolve_channel_display_command(owner.snapshot(), action=action)
    assert reads.counts().plane_reads == reads.counts().region_reads == 0
    assert command.record()['version'] == 3
    if action != 'disable':
        assert command.state['version'] == 3
    ij.validate_channel_display_command_record(command.record())
    replay = ij.channel_display_command_from_record(owner.snapshot(), command.record())
    owner.set_display(ij.command_display_state(replay, owner.snapshot()))
    assert reads.counts().plane_reads == reads.counts().region_reads == 0
    assert owner.undo_steps == owner.redo_steps == 0
    assert owner.snapshot().dataset.numpy('TCZYX').tobytes() == a.tobytes()


BAD_WORDS = [[0x7f800001, 0x7fc00000], [0x7fc00001, 0x7fc00000],
             [0xffc00000, 0xffc00000], [0x7fc00000, 0], [0x7f800000, 0xff800000],
             [False, 0x7f800000], [0., 0x7f800000], [-1, 0x7f800000],
             [2**32, 0x7f800000], [0x7f800000], [0, 0x3f800000]]


@pytest.mark.parametrize('words', BAD_WORDS)
def test_strict_raw_range_words(words):
    _, _, data, state = scientific()
    state['channels'][0]['range'] = {'float32_bits': words}
    with pytest.raises((ValueError, TypeError, OverflowError)):
        ij.validate_channel_display(state, data)


@pytest.mark.parametrize('version', [1, 2, 4, True, 3.])
def test_ieee_never_silently_uses_other_schemas(version):
    _, _, data, state = scientific()
    state['version'] = version
    with pytest.raises(ValueError):
        ij.validate_channel_display(state, data)


@pytest.mark.parametrize('policy', [ij.FloatingSamplePolicy.FINITE_ONLY,
                                   ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32])
def test_v3_does_not_widen_old_source_policies(policy):
    data = ij.Dataset(np.zeros((2, 3), np.float32), axes='YX', sample_policy=policy)
    state = ij.default_channel_display(data)
    assert state['version'] == 1
    state['version'] = 3
    state['projection'] = 'sum'
    with pytest.raises(ValueError):
        ij.validate_channel_display(state, data)


def test_limits_and_cancellation_refuse_before_source_reads():
    _, _, data, state = scientific()
    reads = ij.DatasetReadObserver()
    observed = data.observe_reads(reads)
    limits = ij.ChannelDisplayLimits()
    limits.max_pixels = 1
    with pytest.raises(ValueError):
        ij.compose_channel_display(observed, state, limits=limits)
    token = ij.CancellationToken()
    token.cancel()
    with pytest.raises(ij.OperationCancelled):
        ij.compose_channel_display(observed, state, cancellation=token)
    with pytest.raises(ij.OperationCancelled):
        ij.reset_channel_display_ranges(observed, state, cancellation=token)
    assert reads.counts().plane_reads == reads.counts().region_reads == 0


def test_literal_zero_interval_is_not_an_automatic_reset():
    data = ij.Dataset(np.array([[-1, 0, 1]], np.float32), axes='YX', sample_policy=IEEE)
    state = ij.default_channel_display(data, mode='grayscale')
    state['channels'][0]['range'] = [0, 0]
    assert ij.compose_channel_display(data, state).tobytes() == bytes([0]*6 + [255]*3)
