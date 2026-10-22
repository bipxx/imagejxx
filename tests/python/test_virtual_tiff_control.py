"""IO-provider work must honor scopes owned by the separate native extension."""
import numpy as np
import pytest
import imagejxx as ij
from imagejxx import scientific_io as io
from test_virtual_tiff import write

def source(tmp_path,rows,wrapper):
    a=np.zeros((1,1,2,rows,4),np.float32);a[0,0,1,-1,-1]=np.nan
    path=tmp_path/(str(rows)+wrapper+'.tif')
    write(path,a,io.ImageFileFormat.SCIENTIFIC_TIFF_FLOAT32_V1,ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32)
    data=io.open_virtual_image_series(path).dataset
    if wrapper=='observed':data=data.observe_reads(ij.DatasetReadObserver())
    if wrapper=='overlay':data=data.with_plane(np.ones((rows,4),np.float32),z=0)
    request=ij.DatasetHistogramRequest();request.position=ij.PlanePosition(0,0,1)
    return data,request

@pytest.mark.parametrize('wrapper',['plain','observed','overlay'])
def test_cross_extension_checkpoints_reach_reader_rows(wrapper,tmp_path):
    counts=[]
    for rows in (4,64):
        data,request=source(tmp_path,rows,wrapper);calls=[]
        with pytest.raises(ValueError):ij.accumulate_dataset_histogram(data,request,checkpoint=lambda:calls.append(1))
        counts.append(len(calls))
    # The provider throws at its final sample, before the histogram kernel can
    # run. Extra row checkpoints therefore prove propagation into native IO.
    assert counts[1]>counts[0]+32

@pytest.mark.parametrize('wrapper',['plain','observed','overlay'])
def test_cross_extension_cancellation_wins_inside_provider(wrapper,tmp_path):
    small,request=source(tmp_path,4,wrapper);calls=[]
    with pytest.raises(ValueError):ij.accumulate_dataset_histogram(small,request,checkpoint=lambda:calls.append(1))
    threshold=len(calls)+5;data,request=source(tmp_path,64,wrapper);token=ij.CancellationToken();seen=[]
    def cancel():
        seen.append(1)
        if len(seen)>=threshold:token.cancel()
    with pytest.raises(ij.OperationCancelled):ij.accumulate_dataset_histogram(data,request,cancellation=token,checkpoint=cancel)
    assert len(seen)>=threshold
    np.testing.assert_array_equal(data.region(0,0,2,2,z=1),np.zeros((2,2),np.float32))
