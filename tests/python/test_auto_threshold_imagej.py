"""Independent pinned Java results plus native API/editor contracts."""
import hashlib
import json
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij

FIXTURE=Path(__file__).parents[1]/"fixtures/auto-threshold-imagej154r.json"
DATA=json.loads(FIXTURE.read_bytes())
METHODS=("default","ij_isodata","huang","huang2","renyi_entropy","shanbhag")

def counts(row):
    values=[0]*row['bins']
    for k,v in row['nonzero'].items():values[int(k)]=v
    return values

@pytest.mark.parametrize('method',METHODS)
@pytest.mark.parametrize('row',DATA['cases'],ids=lambda r:r['id'])
def test_pinned_histogram_results(row,method):
    h=counts(row);original=h.copy()
    assert ij.auto_threshold_histogram_imagej(h,method)==row['thresholds'][method]
    assert h==original

@pytest.mark.parametrize('method',METHODS)
@pytest.mark.parametrize('dtype,bins',[(np.uint8,256),(np.uint16,65536),(np.float32,256)])
def test_pixel_adapter_and_masks(method,dtype,bins):
    row=next(r for r in DATA['cases'] if r['id']=='random-4')
    values=np.concatenate([np.full(v,int(k),np.uint8) for k,v in row['nonzero'].items()])
    if dtype==np.uint16:values=values.astype(dtype)+12000
    elif dtype==np.float32:values=values.astype(dtype)*.25-17
    image=values.reshape(1,-1);before=image.tobytes()
    histogram=ij.histogram(image,bins=bins)
    # The image adapter uses the declared histogram edges/bin centers. This is
    # deliberately distinct from upstream image/display-range conversion.
    h,edges=histogram
    t=ij.auto_threshold_histogram_imagej(h.tolist(),method)
    expected=(edges[t]+edges[t+1])/2
    assert ij.auto_threshold(image,method,bins=bins)==expected
    for bright in [True,False]:
        np.testing.assert_array_equal(ij.auto_mask(image,method,bright=bright,bins=bins),((image>expected)==bright).astype(np.uint8)*255)
    assert image.tobytes()==before

@pytest.mark.parametrize('method',METHODS)
def test_registered_command_and_workflow(method,tmp_path):
    from imagejxx.ui.model import Document
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    image=np.arange(256,dtype=np.uint8).reshape(16,16)
    editor=Editor(preferences=Preferences(tmp_path/'prefs.json'))
    editor.add_document(Document(image));editor.recording=True
    frame=editor.render_headless();assert frame['vertices']>0
    editor.apply_operation('auto_mask',{'method':method,'bins':256,'bright':True},asynchronous=False)
    np.testing.assert_array_equal(editor.document.frame,ij.auto_mask(image,method))
    assert editor.workflow[-1]['parameters']['method']==method
    editor.action('undo');np.testing.assert_array_equal(editor.document.frame,image)
    editor.action('redo');np.testing.assert_array_equal(editor.document.frame,ij.auto_mask(image,method))

@pytest.mark.parametrize('method',METHODS)
def test_finite_and_bin_validation(method):
    for image in [np.array([[0,np.nan]],np.float32),np.array([[0,np.inf]],np.float64)]:
        with pytest.raises(ValueError):ij.auto_threshold(image,method)
    for n in [-1,1,65537]:
        with pytest.raises(ValueError):ij.auto_threshold(np.array([[1,2]],np.uint8),method,bins=n)
    for h in [[],[1],[0,0],[2**31,1],[1]*65537]:
        with pytest.raises(ValueError):ij.auto_threshold_histogram_imagej(h,method)

def test_unknown_method_and_count_conversion():
    with pytest.raises(ValueError):ij.auto_threshold_histogram_imagej([1,2],'typo')
    for h in [[-1,2],[.5,2],[2**64,1]]:
        with pytest.raises((TypeError,OverflowError)):ij.auto_threshold_histogram_imagej(h,'huang')

@pytest.mark.parametrize('method',METHODS)
def test_cancellation_and_checkpoint_failure(method):
    token=ij.CancellationToken();token.cancel()
    with pytest.raises(ij.OperationCancelled):ij.auto_threshold_histogram_imagej([1]*256,method,cancellation=token)
    token=ij.CancellationToken();seen=[]
    def cancel():
        seen.append(1)
        if len(seen)==4:token.cancel()
    with pytest.raises(ij.OperationCancelled):ij.auto_threshold_histogram_imagej([1]*256,method,cancellation=token,checkpoint=cancel)
    assert len(seen)>=4
    def fail():raise RuntimeError('checkpoint test')
    with pytest.raises(RuntimeError,match='checkpoint test'):ij.auto_threshold_histogram_imagej([1]*256,method,checkpoint=fail)

def test_catalog_methods_and_snapshot():
    from imagejxx.ui.commands import OPERATIONS
    param=next(p for p in OPERATIONS['auto_mask'].parameters() if p.name=='method')
    assert all(m in param.choices and m in ij.THRESHOLD_METHODS for m in METHODS)
    assert len(param.choices)==len(set(param.choices))==18
    assert DATA['upstream']['commit']=='62db90a269cccc1ac79f658e4602043214ed055b'
    assert hashlib.sha256(FIXTURE.read_bytes()).hexdigest()=='e8f76e8e82c0cbcce98307bf9374e175439bef531ae17a2f3a0de3289ae70db8'

@pytest.mark.parametrize('method',METHODS)
def test_progress_result_and_callback_reentry(method):
    events=[]
    def progress(done,total):
        events.append((done,total))
        assert ij.auto_threshold_histogram_imagej([1,2],'huang')==0
    ij.auto_threshold_histogram_imagej([1]*256,method,progress=progress)
    assert events==[(0,1),(1,1)]
    token=ij.CancellationToken()
    def cancel(done,total):
        if done==total:token.cancel()
    with pytest.raises(ij.OperationCancelled):ij.auto_threshold_histogram_imagej([1]*256,method,cancellation=token,progress=cancel)

@pytest.mark.parametrize('method',METHODS)
def test_histogram_workspace_admitted_before_publication(method):
    source=np.array([[0,17,99],[4,5,6]],np.uint16)
    owner=ij.Document(ij.Dataset(source,axes='YX'));app=ij.Application();app.adopt(owner)
    before=owner.snapshot()
    app.job_scheduler.set_limits(ij.JobLimits(workers=1,memory_budget=1024*1024))
    with pytest.raises(ValueError,match='memory budget'):
        app.submit('auto_mask',app.resolve_target(),{'method':method,'bins':65536})
    assert owner.snapshot().shares_data(before) and owner.undo_steps==0
