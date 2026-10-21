import concurrent.futures
import numpy as np
import pytest
from numpy.testing import assert_allclose, assert_array_equal
from scipy import ndimage as ndi
import imagejxx as ij


def saturate(values, dtype):
    if np.dtype(dtype).kind == "u":
        maximum = np.iinfo(dtype).max
        return np.floor(np.clip(values, 0, maximum) + 0.5).astype(dtype)
    return np.asarray(values, dtype=dtype)


@pytest.mark.parametrize("dtype", [np.uint8, np.uint16, np.float32, np.float64])
@pytest.mark.parametrize("operation,operand,reference", [
    ("add", 17, lambda x: x+17), ("subtract", 23, lambda x: x-23),
    ("multiply", 2.1, lambda x: x*2.1), ("divide", 3, lambda x: x/3),
    ("minimum", 20, lambda x: np.minimum(x,20)), ("maximum", 20, lambda x: np.maximum(x,20)),
    ("abs", 0, np.abs), ("square", 0, np.square), ("sqrt", 0, np.sqrt),
    ("log1p", 0, np.log1p), ("set", 18, lambda x: np.full_like(x,18)),
])
def test_scalar_math(dtype, operation, operand, reference):
    source = np.array([[0,1,10,100],[200,254,255,23]], dtype=dtype)
    original = source.copy()
    actual = ij.math(source, operation, operand)
    assert_allclose(actual, saturate(reference(source.astype(np.float64)), dtype), rtol=1e-6, atol=1e-6)
    assert_array_equal(source, original)
    assert not np.shares_memory(actual, source)


@pytest.mark.parametrize("dtype,maximum", [(np.uint8,255), (np.uint16,65535), (np.float64,1)])
def test_invert_gamma_and_conversion(dtype, maximum):
    source = saturate(np.array([[0,.1,.5,1]])*maximum, dtype)
    assert_array_equal(ij.invert(source), saturate(maximum-source.astype(float), dtype))
    assert_allclose(ij.gamma(source, 2), saturate(maximum*(source.astype(float)/maximum)**2, dtype))
    assert_allclose(ij.convert(source, np.float64, scale=True), source.astype(float)/maximum)
    assert_array_equal(ij.convert(np.array([[-1.,1.4,1.5,999.]]), np.uint8), [[0,1,2,255]])


@pytest.mark.parametrize("operation", ["add","subtract","multiply","divide","minimum","maximum","average","difference","copy"])
def test_calculator_float_output(operation):
    a = np.array([[2,255,0,4]], dtype=np.uint8)
    b = np.array([[4,3,0,0]], dtype=np.uint8)
    x,y = a.astype(float),b.astype(float)
    refs = {"add":x+y, "subtract":x-y, "multiply":x*y,
            "divide":np.divide(x,y,out=np.zeros_like(x),where=y!=0), "minimum":np.minimum(x,y),
            "maximum":np.maximum(x,y), "average":(x+y)/2, "difference":abs(x-y), "copy":y}
    assert_allclose(ij.calculate(a,b,operation,dtype=np.float64), refs[operation])


@pytest.mark.parametrize("op,reference", [("and",np.bitwise_and),("or",np.bitwise_or),("xor",np.bitwise_xor)])
def test_bitwise(op, reference):
    a = np.array([[0,1,15,255]],dtype=np.uint8)
    b = np.array([[255,2,13,4]],dtype=np.uint8)
    assert_array_equal(ij.calculate(a,b,op), reference(a,b))
    assert_array_equal(ij.math(a,op,13), reference(a,13))


@pytest.mark.parametrize("border", ["reflect","nearest","wrap","constant"])
@pytest.mark.parametrize("color", [False,True])
def test_convolution_orientation_and_borders(border, color):
    image = np.random.default_rng(7).normal(size=(5,7,3) if color else (5,7))
    kernel = np.array([[1,2,3],[4,-9,6],[7,8,9]],dtype=float)
    expected = np.stack([ndi.convolve(image[...,c],kernel,mode=border,cval=2) for c in range(3)],axis=-1) if color else ndi.convolve(image,kernel,mode=border,cval=2)
    assert_allclose(ij.convolve(image,kernel,border=border,cval=2), expected, atol=1e-12)


@pytest.mark.parametrize("border", ["reflect","nearest","wrap","constant"])
def test_separable_gaussian_and_box(border):
    image = np.random.default_rng(10).normal(size=(7,9))
    sigma = (1.3,.7)
    radius = tuple(int(np.ceil(4*s)) for s in sigma)
    assert_allclose(ij.gaussian_blur(image,sigma,border=border), ndi.gaussian_filter(image,sigma,mode=border,radius=radius), atol=1e-12)
    assert_allclose(ij.smooth(image,2,border=border), ndi.uniform_filter(image,5,mode=border), atol=1e-12)


@pytest.mark.parametrize("border", ["reflect","nearest","wrap","constant"])
@pytest.mark.parametrize("footprint", ["disk","square"])
@pytest.mark.parametrize("method,reference", [("minimum",np.min),("maximum",np.max),("median",np.median),("mean",np.mean),("variance",np.var),("stddev",np.std)])
def test_rank_filters(border, footprint, method, reference):
    source = np.random.default_rng(2).normal(size=(5,7))
    yy,xx = np.mgrid[-2:3,-2:3]
    window = xx*xx+yy*yy <= 4 if footprint=="disk" else np.ones((5,5),bool)
    expected = ndi.generic_filter(source,reference,footprint=window,mode=border,cval=2)
    assert_allclose(ij.rank_filter(source,2,method=method,footprint=footprint,border=border,cval=2), expected, atol=1e-12)


@pytest.mark.parametrize("method", ["erode","dilate","open","close","gradient","top_hat","black_hat"])
def test_morphology(method):
    image = np.random.default_rng(3).normal(size=(9,11))
    yy,xx = np.mgrid[-2:3,-2:3]; footprint=xx*xx+yy*yy<=4
    erode = lambda a: ndi.grey_erosion(a,footprint=footprint,mode="reflect")
    dilate = lambda a: ndi.grey_dilation(a,footprint=footprint,mode="reflect")
    expected = {"erode":erode(image),"dilate":dilate(image),"open":dilate(erode(image)),
                "close":erode(dilate(image)),"gradient":dilate(image)-erode(image),
                "top_hat":image-dilate(erode(image)),"black_hat":erode(dilate(image))-image}
    assert_allclose(ij.morphology(image,method,2),expected[method],atol=1e-12)


def test_derivatives_and_unsharp():
    source=np.random.default_rng(10).normal(size=(11,13))
    expected=np.hypot(ndi.sobel(source,axis=0),ndi.sobel(source,axis=1))
    assert_allclose(ij.find_edges(source),expected,atol=1e-12)
    assert_allclose(ij.unsharp_mask(source,1,2),source+2*(source-ndi.gaussian_filter(source,1)),atol=1e-12)
    assert_allclose(ij.laplacian(source),ndi.laplace(source),atol=1e-12)
    kernel=np.array([[-1,-1,-1],[-1,12,-1],[-1,-1,-1]],dtype=float)/4
    assert_allclose(ij.sharpen(source),ndi.convolve(source,kernel),atol=1e-12)


def test_edge_preserving_filters():
    source=np.zeros((9,13),dtype=np.uint8); source[:,6:]=255
    assert_array_equal(ij.bilateral_filter(source,2,sigma_spatial=2,sigma_range=1),source)
    assert_array_equal(ij.kuwahara_filter(source,2),source)
    constant=np.full((7,9,3),.3)
    assert_allclose(ij.bilateral_filter(constant,2,sigma_range=.1),constant,atol=1e-14)
    assert_allclose(ij.kuwahara_filter(constant),constant,atol=1e-14)
    image=np.random.default_rng(1).random((9,11))
    result=ij.anisotropic_diffusion(image,5,kappa=100)
    assert result.min() >= image.min() and result.max() <= image.max()
    assert result.std() < image.std()
    assert_allclose(result.sum(),image.sum(),atol=1e-12)


def test_clahe_invariants_and_clip_limit():
    constant=np.full((7,11),123,dtype=np.uint16)
    assert_array_equal(ij.clahe(constant,tile_size=(3,5)),constant)
    image=np.random.default_rng(5).integers(40,100,(17,23),dtype=np.uint8)
    a=ij.clahe(image,tile_size=(7,5),clip_limit=2)
    b=ij.clahe(image.T,tile_size=(5,7),clip_limit=2).T
    assert_array_equal(a,b)
    assert a.shape==image.shape and a.dtype==image.dtype and np.unique(a).size>1
    assert not np.array_equal(a,ij.clahe(image,tile_size=(7,5),clip_limit=20))


@pytest.mark.parametrize("method", ij.LOCAL_THRESHOLD_METHODS)
def test_local_threshold_formulas(method):
    source=np.random.default_rng(12).random((9,11))
    mean=ndi.uniform_filter(source,5)
    std=np.sqrt(np.maximum(0,ndi.uniform_filter(source**2,5)-mean**2))
    minimum=ndi.minimum_filter(source,5); maximum=ndi.maximum_filter(source,5)
    k=.2; offset=.03; r=.5
    thresholds={"mean":mean-offset,"median":ndi.median_filter(source,5)-offset,
                "midgrey":(minimum+maximum)/2-offset,"niblack":mean+k*std-offset,
                "sauvola":mean*(1+k*(std/r-1))-offset,
                "phansalkar":mean*(1+2*np.exp(-10*mean)+k*(std/r-1))-offset}
    if method=="contrast": expected=maximum-source < source-minimum
    elif method=="bernsen":
        mid=(minimum+maximum)/2-offset
        expected=np.where(maximum-minimum<.07,mid>=.5,source>mid)
    else: expected=source>thresholds[method]
    assert_array_equal(ij.local_threshold(source,method,radius=2,k=k,offset=offset,dynamic_range=r,contrast_threshold=.07),expected.astype(np.uint8)*255)


@pytest.mark.parametrize("method", ij.THRESHOLD_METHODS)
def test_global_threshold_bimodal(method):
    image=np.zeros((9,12),np.uint8); image[:,6:]=200
    threshold=ij.auto_threshold(image,method)
    assert 0 <= threshold < 200
    assert_array_equal(ij.auto_mask(image,method),np.where(image>0,255,0).astype(np.uint8))


def test_otsu_independent_histogram_objective():
    image=np.random.default_rng(19).integers(0,256,(37,29),dtype=np.uint8)
    histogram=np.bincount(image.ravel(),minlength=256).astype(float)
    weights=histogram.cumsum(); moments=(histogram*np.arange(256)).cumsum()
    mean0=np.divide(moments,weights,out=np.zeros(256),where=weights>0)
    remaining=weights[-1]-weights
    mean1=np.divide(moments[-1]-moments,remaining,out=np.zeros(256),where=remaining>0)
    objective=weights*remaining*(mean0-mean1)**2
    assert ij.auto_threshold(image)==np.argmax(objective)


def test_statistics_histogram_and_equalization():
    source=np.random.default_rng(4).normal(size=(13,17))
    mask=source>0; values=source[mask]
    stats=ij.statistics(source,mask=mask)
    assert stats["count"]==values.size
    assert_allclose([stats[k] for k in ["min","max","mean","stddev","sum","median"]],
                    [values.min(),values.max(),values.mean(),values.std(),values.sum(),np.median(values)])
    actual,edges=ij.histogram(source,17,range=(-2,2)); expected,reference_edges=np.histogram(source,17,range=(-2,2))
    assert_array_equal(actual,expected); assert_allclose(edges,reference_edges)
    ints=np.array([[0,1,65535]],dtype=np.uint16)
    counts,edges=ij.histogram(ints)
    assert counts.size==65536 and counts.sum()==3 and counts[65535]==1
    assert_array_equal(ij.equalize_histogram(np.array([[0,1,2,3]],np.uint8)),[[0,85,170,255]])
    assert_array_equal(ij.equalize_histogram(np.full((3,4),17,np.uint8)),np.full((3,4),17,np.uint8))


@pytest.mark.parametrize("method,reference", [("maximum",np.max),("minimum",np.min),("mean",np.mean),("sum",np.sum),("median",np.median),("stddev",np.std)])
def test_stack_projection(method, reference):
    stack=np.random.default_rng(4).integers(0,256,(4,5,7,3),dtype=np.uint8)
    assert_allclose(ij.project(stack,method),reference(stack,axis=0))


def test_volume_blur_and_slice_processing():
    stack=np.random.default_rng(8).normal(size=(4,7,9,3)); sigma=(.7,1.1,.9)
    expected=ndi.gaussian_filter(stack,sigma,axes=(0,1,2),radius=tuple(int(np.ceil(4*s)) for s in sigma))
    assert_allclose(ij.gaussian_blur3d(stack,sigma),expected,atol=1e-12)
    assert_allclose(ij.map_stack(stack,ij.multiply,2),stack*2)


def test_noncontiguous_endian_and_concurrent_calls():
    image=np.arange(63,dtype=np.uint16).reshape(7,9)
    assert_array_equal(ij.add(image.T[::-1,::2],3),image.T[::-1,::2]+3)
    assert_array_equal(ij.add(image.astype(">u2"),3),image+3)
    source=np.random.default_rng(1).random((64,71)); original=source.copy()
    with concurrent.futures.ThreadPoolExecutor(4) as pool:
        results=list(pool.map(lambda _:ij.gaussian_blur(source,1.3),range(8)))
    expected=ndi.gaussian_filter(source,1.3,radius=6)
    for result in results: assert_allclose(result,expected,atol=1e-12)
    assert_array_equal(source,original)


@pytest.mark.parametrize("radius",[0,1,2.7])
@pytest.mark.parametrize("light",[False,True])
def test_spherical_background_against_nonflat_morphology(radius,light):
    source=np.random.default_rng(14).random((11,13))*100
    r=int(np.ceil(radius)); yy,xx=np.mgrid[-r:r+1,-r:r+1]
    footprint=xx*xx+yy*yy<=radius*radius
    ball=np.sqrt(np.maximum(0,radius*radius-xx*xx-yy*yy))
    operation=ndi.grey_closing if light else ndi.grey_opening
    expected=operation(source,footprint=footprint,structure=ball,mode="reflect")
    assert_allclose(ij.rolling_ball_background(source,radius,light_background=light),expected,atol=1e-12)
    residual=expected-source if light else source-expected
    assert_allclose(ij.subtract_rolling_ball(source,radius,light_background=light),residual,atol=1e-12)


@pytest.mark.parametrize("bright",[True,False])
def test_outlier_replacement_direction(bright):
    source=np.random.default_rng(16).random((11,13)); source[5,6]=100; source[3,4]=-100
    yy,xx=np.mgrid[-2:3,-2:3]; median=ndi.median_filter(source,footprint=xx*xx+yy*yy<=4)
    delta=source-median if bright else median-source
    expected=np.where(delta>2,median,source)
    assert_array_equal(ij.remove_outliers(source,2,2,bright=bright),expected)
