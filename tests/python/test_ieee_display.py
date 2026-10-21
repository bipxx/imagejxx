"""Read-only FloatProcessor display parity, independent of editor admission."""
import json
import struct
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest

FIXTURE = json.loads((Path(__file__).parents[1] / 'fixtures' /
                      'ieee-display-imagej154r.json').read_bytes())
IEEE = ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32


def options(row):
    value = ij.Float32DisplayOptions()
    value.range = [None, [0, 1], [-16, 16], [7, 7]][row['range_mode']]
    value.inverted = row['inverted_lut']
    value.threshold_mode = [ij.Float32ThresholdDisplay.NONE,
                            ij.Float32ThresholdDisplay.RED,
                            ij.Float32ThresholdDisplay.BLACK_AND_WHITE][row['threshold_mode']]
    value.threshold = [-1, 1] if row['threshold_mode'] else None
    return value


@pytest.mark.parametrize('row', FIXTURE['cases'], ids=lambda row:
                         f"s{row['signal']}-r{row['range_mode']}-i{int(row['inverted_lut'])}-t{row['threshold_mode']}")
def test_actual_imagej_display(row):
    original = bytes.fromhex(row['source_before_hex'])
    source = np.frombuffer(original, dtype='<u4').copy().view(np.float32).reshape(4, 8)
    result = ij.render_float32_display(source, options(row), sample_policy=IEEE)
    assert result.rgb.shape == (4, 8, 3) and result.rgb.dtype == np.uint8
    assert result.rgb.tobytes() == bytes.fromhex(row['rgb_hex'])
    for word, field in zip(result.range_bits, ['min_f64le', 'max_f64le']):
        endpoint = ij.float32_source_sample(word, sample_policy=IEEE)
        assert struct.pack('<d', endpoint).hex() == row[field]
    assert source.tobytes() == original == bytes.fromhex(row['source_after_hex'])
    copy = result.rgb
    copy[:] = 123
    assert result.rgb.tobytes() == bytes.fromhex(row['rgb_hex'])


@pytest.mark.parametrize('layout', ['transpose', 'reverse', 'fortran', 'big_endian', 'unaligned', 'readonly'])
def test_source_layouts(layout):
    row = next(r for r in FIXTURE['cases'] if r['signal'] == 3 and r['range_mode'] == 2
               and not r['inverted_lut'] and r['threshold_mode'] == 0)
    words = np.frombuffer(bytes.fromhex(row['source_before_hex']), dtype='<u4').copy().reshape(4, 8)
    expected = np.frombuffer(bytes.fromhex(row['rgb_hex']), np.uint8).reshape(4, 8, 3)
    if layout == 'transpose':
        words = words.T
        expected = expected.transpose(1, 0, 2)
    elif layout == 'reverse':
        words = words[::-1, ::-1]
        expected = expected[::-1, ::-1]
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
    result = ij.render_float32_display(source, options(row), sample_policy=IEEE)
    assert result.rgb.tobytes() == expected.tobytes() and source.tobytes() == before


@pytest.mark.parametrize('word', [0x7f800000, 0xff800000, 0x7f800001, 0xff812345, 0x7fc12345])
def test_admission_is_explicit(word):
    source = np.array([[word]], np.uint32).view(np.float32)
    with pytest.raises(ValueError):
        ij.render_float32_display(source)
    assert ij.render_float32_display(source, sample_policy=IEEE).rgb.shape == (1, 1, 3)
    quiet = ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32
    if word == 0x7fc12345:
        assert ij.render_float32_display(source, sample_policy=quiet).rgb.sum() == 0
    else:
        with pytest.raises(ValueError):
            ij.render_float32_display(source, sample_policy=quiet)


@pytest.mark.parametrize('bounds', [[2, 1], [0, float('inf')], [float('nan'), 1], [0, 1e100]])
def test_invalid_range(bounds):
    value = ij.Float32DisplayOptions()
    value.range = bounds
    with pytest.raises(ValueError):
        ij.render_float32_display(np.zeros((2, 3), np.float32), value)


@pytest.mark.parametrize('dtype', [np.float64, np.uint8, np.uint16])
def test_invalid_source_type(dtype):
    with pytest.raises(ValueError):
        ij.render_float32_display(np.zeros((2, 3), dtype))


def test_manual_rounding_and_zero_reset():
    value = ij.Float32DisplayOptions()
    value.range = [0.1, 0.2]
    source = np.array([[1, 2]], np.float32)
    actual = ij.render_float32_display(source, value)
    assert actual.range_bits == np.array([0.1, 0.2], np.float32).view(np.uint32).tolist()
    value.range = [0, -0.0]
    assert ij.render_float32_display(source, value).range_bits == source.view(np.uint32).ravel().tolist()


@pytest.mark.parametrize('mode,interval', [
    (ij.Float32ThresholdDisplay.NONE, [0, 1]),
    (ij.Float32ThresholdDisplay.RED, None),
    (ij.Float32ThresholdDisplay.BLACK_AND_WHITE, [1, 0]),
    (ij.Float32ThresholdDisplay.RED, [float('nan'), 1]),
])
def test_invalid_threshold(mode, interval):
    value = ij.Float32DisplayOptions()
    value.threshold_mode = mode
    value.threshold = interval
    with pytest.raises(ValueError):
        ij.render_float32_display(np.zeros((2, 3), np.float32), value)
