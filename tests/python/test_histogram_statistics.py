import csv
import io
import math
import numpy as np
import pytest
import imagejxx as ij

@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float32])
@pytest.mark.parametrize('roi',[False,True])
@pytest.mark.parametrize('uniform',[False,True])
def test_exact_sample_statistics_are_independent_of_bins(dtype,roi,uniform):
    data=np.array([[0,1,2,9]],dtype=dtype)
    source=ij.Dataset(data,axes='YX');request=ij.DatasetHistogramRequest()
    if roi:request.area_roi=ij.Selection('rectangle',[(0,0),(2,1)])
    if uniform:
        request.binning=ij.HistogramBinning.UniformRange;request.range=(1,3);request.bins=2
    result=ij.accumulate_dataset_histogram(source,request)
    assert result.mean==(0.5 if roi else 3)
    assert result.sum==(1 if roi else 12)
    assert result.stddev==pytest.approx(0.5 if roi else math.sqrt(12.5))
    text=ij.dataset_histogram_csv(result);rows=list(csv.DictReader(io.StringIO(text)))
    assert list(rows[0])==['bin','lower','upper','count']
    arrays=result.histogram
    assert len(rows)==len(arrays['counts'])
    assert [int(r['count']) for r in rows]==list(arrays['counts'])
    assert [float(r['lower']) for r in rows]==list(arrays['edges'][:-1])
    assert [float(r['upper']) for r in rows]==list(arrays['edges'][1:])
    assert source.numpy('YX').tobytes()==data.tobytes()

@pytest.mark.parametrize('values',[[7,7,7],[-3,0,3],[-np.finfo(np.float32).max,np.finfo(np.float32).max]])
def test_float_statistics_constants_and_extrema(values):
    a=np.array([values],dtype=np.float32)
    r=ij.accumulate_dataset_histogram(ij.Dataset(a,axes='YX'))
    assert r.mean==pytest.approx(float(a.astype(np.float64).mean()),abs=1e-14)
    assert r.sum==pytest.approx(float(a.astype(np.float64).sum()),abs=1e-14)
    assert r.stddev==pytest.approx(float(a.astype(np.float64).std()))

def test_empty_roi_has_absent_statistics_and_zero_sum():
    request=ij.DatasetHistogramRequest();request.area_roi=ij.Selection.from_mask(np.zeros((2,2),dtype=np.uint8))
    result=ij.accumulate_dataset_histogram(ij.Dataset(np.zeros((2,2),dtype=np.uint8),axes='YX'),request)
    assert result.mean is result.stddev is None
    assert result.sum==0 and result.finite_count==0
    assert len(ij.dataset_histogram_csv(result).splitlines())==257
