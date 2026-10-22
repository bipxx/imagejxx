"""Exception and reentry boundaries across the native/IO extension bridge."""
import numpy as np
import pytest
import imagejxx as ij
from test_virtual_tiff_control import source

def test_provider_callback_preserves_original_python_exception(tmp_path):
    data,request=source(tmp_path,4,'plain');observer=ij.DatasetReadObserver();data=data.observe_reads(observer)
    class ReaderStop(Exception):pass
    def stop_after_entry():
        if observer.counts().plane_reads:raise ReaderStop('inside provider')
    with pytest.raises(ReaderStop,match='inside provider'):
        ij.accumulate_dataset_histogram(data,request,checkpoint=stop_after_entry)
    np.testing.assert_array_equal(data.region(0,0,2,2,z=1),np.zeros((2,2),np.float32))

def test_reentrant_provider_reads_do_not_redispatch_the_same_callback(tmp_path):
    data,request=source(tmp_path,4,'plain');observer=ij.DatasetReadObserver();data=data.observe_reads(observer)
    active=False;nested=[]
    def inspect():
        nonlocal active
        assert not active,'Provider bridge recursively dispatched its callback'
        if observer.counts().plane_reads:
            active=True
            try:nested.append(data.region(0,0,1,1,z=1).item())
            finally:active=False
    with pytest.raises(ValueError):ij.accumulate_dataset_histogram(data,request,checkpoint=inspect)
    assert nested and nested==[0.]*len(nested)
    np.testing.assert_array_equal(data.region(0,0,2,2,z=1),np.zeros((2,2),np.float32))
