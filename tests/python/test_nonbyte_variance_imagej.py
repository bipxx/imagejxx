"""Pixel-only nonbyte variance; editor admission stays separately guarded."""
import json
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij

REFERENCE = json.loads((Path(__file__).parents[1] / 'fixtures/nonbyte-variance-imagej154r.json').read_bytes())


@pytest.mark.parametrize('row', REFERENCE['cases'], ids=lambda r: f"{r['dtype']}-s{r['signal']}-{r['width']}x{r['height']}-r{r['radius']}-e{r['extent']}")
def test_nonbyte_variance_reference_bits_or_finite_overflow(row):
    dtype = np.dtype(row['dtype'])
    words = np.uint16 if dtype == np.uint16 else np.uint32
    source = np.array(row['input_bits'], dtype=words).view(dtype).reshape(row['height'], row['width'])
    expected = np.array(row['output_bits'], dtype=words).view(dtype).reshape(source.shape)
    before = source.tobytes()
    if np.isfinite(expected).all():
        result = ij.rank_filter_imagej(source, row['radius'], method='variance', extent=row['extent'])
        assert result.dtype == dtype
        assert result.tobytes() == expected.tobytes()
    else:
        with pytest.raises(ValueError, match='finite Float32 output'):
            ij.rank_filter_imagej(source, row['radius'], method='variance', extent=row['extent'])
    assert source.tobytes() == before


@pytest.mark.parametrize('dtype', [np.uint16, np.float32])
def test_nonbyte_variance_editor_admission_and_empty_extent(dtype):
    source = np.arange(12, dtype=dtype).reshape(3, 4)
    observer = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(source, axes='YX').observe_reads(observer))
    command = ij.resolve_rank_command(owner.snapshot(), options=ij.RankFilterCommandOptions('variance', 1))
    assert observer.counts().plane_reads == 0 and owner.undo_steps == 0
    prepared = ij.prepare_operation('rank_filter_imagej', command.target, command.parameters())
    np.testing.assert_array_equal(prepared.preview.dataset.numpy('YX'), ij.rank_filter_imagej(source, 1, method='variance'))
    result = ij.rank_filter_imagej(source, 1000, method='variance', extent=(4, 3, 0, 0))
    assert result.tobytes() == source.tobytes()

def test_overflow_outside_write_extent_is_not_an_output_failure():
    source = np.zeros((7, 7), np.float32)
    source[6, 6] = np.finfo(np.float32).max
    result = ij.rank_filter_imagej(source, 0, method='variance', extent=(0, 0, 1, 1))
    assert result.tobytes() == source.tobytes()
    with pytest.raises(ValueError, match='finite Float32 output'):
        ij.rank_filter_imagej(source, 0, method='variance')
