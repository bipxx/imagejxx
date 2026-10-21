"""Pinned processor RGB output; dataset/editor conversion remains separate."""
import hashlib
import json
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest

REFERENCE = json.loads((Path(__file__).parent.parent / 'fixtures/rgb-conversion-imagej154r.json').read_bytes())
assert len(REFERENCE['cases']) == 111


def palette(name):
    i = np.arange(256, dtype=np.int64)
    if name == 'gray': return np.repeat(i[:, None], 3, axis=1).astype(np.uint8)
    if name == 'inverse': return np.repeat((255-i)[:, None], 3, axis=1).astype(np.uint8)
    return np.column_stack(((17*i+3)%256, 255-i, (i*i+7)%256)).astype(np.uint8)


@pytest.mark.parametrize('case', REFERENCE['cases'])
def test_pinned_imagej_rgb_pixels(case):
    shape = tuple(case['shape']) + (() if case['source_samples'] == 1 else (case['source_samples'],))
    source = np.array(case['source'], dtype=case['source_dtype']).reshape(shape)
    before = source.copy()
    actual = ij.convert_rgb(source, display_range=case['display_range'],
                            lut=None if case['lut'] is None else palette(case['lut']))
    expected = np.array(case['pixels'], np.uint8).reshape((*case['shape'], 3))
    assert actual.dtype == np.uint8
    np.testing.assert_array_equal(actual, expected)
    np.testing.assert_array_equal(source, before)


def test_reference_driver_and_source_identity():
    root = Path(__file__).resolve().parents[2]
    # Driver text is project-owned; upstream production remains in build storage.
    import ast
    tree = ast.parse((root / REFERENCE['generator']).read_text(encoding='utf-8'))
    driver = next(ast.literal_eval(n.value) for n in tree.body if isinstance(n, ast.Assign)
                  and any(isinstance(t, ast.Name) and t.id == 'DRIVER' for t in n.targets))
    # The pinned Windows capture wrote CRLF Java source. Preserve its exact
    # executed-file fingerprint while reproducing it on either host platform.
    assert hashlib.sha256(driver.replace('\n', '\r\n').encode()).hexdigest() == REFERENCE['driver_sha256']
    original = json.loads((root/'tests/fixtures/conversion-imagej154r.json').read_bytes())
    assert REFERENCE['imagej_commit'] == original['imagej_commit']
    assert REFERENCE['source_archive_sha256'] == original['source_archive_sha256']


@pytest.mark.parametrize('samples', [3, 4])
def test_rgb_copy_and_rgba_extension_have_independent_storage(samples):
    source = np.arange(4*5*samples, dtype=np.uint8).reshape(4,5,samples)
    actual = ij.convert_rgb(source)
    np.testing.assert_array_equal(actual, source[..., :3])
    actual[:] = 0
    assert source[0,0,1] == 1


def test_default_palette_and_float64_extension():
    source = np.array([[0., .5, 1.]], np.float64)
    expected = np.repeat(np.array([[0,128,255]], np.uint8)[...,None], 3, axis=-1)
    np.testing.assert_array_equal(ij.convert_rgb(source, display_range=(0,1)), expected)


def test_strided_readonly_source_and_palette():
    source = np.arange(80, dtype=np.uint8).reshape(8,10)[::-1,::2]; source.flags.writeable=False
    storage = np.zeros((256,6), np.uint8); storage[:,::2] = palette('custom')
    lut = storage[:,::2]; lut.flags.writeable=False
    np.testing.assert_array_equal(ij.convert_rgb(source, lut=lut), palette('custom')[source])


@pytest.mark.parametrize('dtype', [np.int64, np.float32, np.float64])
def test_numpy_display_range_vectors(dtype):
    source = np.array([[0,100,200,255]], np.uint8)
    expected = np.repeat(np.array([[0,128,255,255]], np.uint8)[...,None], 3, axis=-1)
    np.testing.assert_array_equal(ij.convert_rgb(source, display_range=np.array([0,200], dtype)), expected)


@pytest.mark.parametrize('value', [[1], [2,1], [0,float('inf')], [float('nan'),1], [False,1], ['0',1], '01', np.zeros((2,2)), np.array([False,True])])
def test_invalid_ranges_refuse(value):
    with pytest.raises((ValueError, TypeError)):
        ij.convert_rgb(np.zeros((2,3), np.uint8), display_range=value)


@pytest.mark.parametrize('value', [np.zeros((255,3), np.uint8), np.zeros((256,4), np.uint8), np.zeros((256,3), np.float32), np.zeros((256,3), np.int8), [[0,0,0]]])
def test_invalid_palettes_refuse_without_implicit_coercion(value):
    with pytest.raises(ValueError): ij.convert_rgb(np.zeros((2,3), np.uint8), lut=value)


@pytest.mark.parametrize('options', [dict(display_range=[0,255]), dict(lut=palette('gray'))])
def test_scalar_options_are_not_silently_ignored_on_rgb(options):
    with pytest.raises(ValueError): ij.convert_rgb(np.zeros((2,3,3), np.uint8), **options)


@pytest.mark.parametrize('source', [np.zeros((2,3,3), np.float32), np.zeros((2,3), np.int16), np.array([[np.nan]], np.float32)])
def test_unsupported_color_or_invalid_samples_refuse(source):
    with pytest.raises(ValueError): ij.convert_rgb(source)
