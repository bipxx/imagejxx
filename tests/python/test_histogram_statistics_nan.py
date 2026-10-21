"""Statistics exclude quiet NaNs without changing preserved source payload bits."""
import numpy as np
import imagejxx as ij
import pytest

@pytest.mark.parametrize('members',[(1,1,1,1),(1,0,1,0),(0,0,0,0)])
def test_nan_population_statistics_and_source_bits(members):
    bits=np.array([[0x7fc12345,0x80000000,0x3f800000,0xffc54321]],dtype=np.uint32)
    source=ij.Dataset(bits.view(np.float32),axes='YX',sample_policy=ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32)
    request=ij.DatasetHistogramRequest();request.area_roi=ij.Selection.from_mask(np.array([members],dtype=np.uint8))
    result=ij.accumulate_dataset_histogram(source,request)
    expected=[v for v,member in zip([float('nan'),0.,1.,float('nan')],members) if member and np.isfinite(v)]
    if expected:
        assert result.mean==np.mean(expected)
        assert result.stddev==np.std(expected)
        assert result.sum==sum(expected)
    else:
        assert result.mean is result.stddev is None
        assert result.sum==0
    assert result.finite_count==len(expected)
    assert source.numpy('YX').view(np.uint32).tobytes()==bits.tobytes()
