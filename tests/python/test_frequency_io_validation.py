from pathlib import Path
import subprocess
import sys
import numpy as np
import pytest
from numpy.testing import assert_allclose, assert_array_equal
import imagejxx as ij


@pytest.mark.parametrize("shape",[(1,1),(1,7),(7,1),(4,8),(7,13)])
def test_fft_complex_values_padding_inverse_and_spectrum(shape):
    image=np.random.default_rng(4).normal(size=shape)
    spectrum=ij.fft2(image)
    expected=np.fft.fft2(image,s=spectrum.shape)
    assert_allclose(spectrum,expected,atol=1e-12)
    assert_allclose(ij.ifft2(spectrum)[:shape[0],:shape[1]],image,atol=1e-12)
    power=np.abs(expected)**2
    assert_allclose(ij.power_spectrum(spectrum,log_scale=False,shift=False),power,atol=1e-10)
    assert_allclose(ij.power_spectrum(spectrum),np.log1p(np.fft.fftshift(power)),atol=1e-12)
    assert_allclose(ij.fft_bandpass(image),image,atol=1e-12)


def test_frequency_gain_units():
    x=np.arange(16); image=np.broadcast_to(np.cos(2*np.pi*x/8),(8,16)).copy()
    expected=image*np.exp(-(.125**2)/(2*.1**2))
    assert_allclose(ij.fft_bandpass(image,high_cutoff=.1),expected,atol=1e-12)
    assert_allclose(ij.fft_bandpass(np.ones((8,8)),low_cutoff=.1),0,atol=1e-12)


@pytest.mark.parametrize("dtype",[np.uint8,np.uint16])
@pytest.mark.parametrize("color",[False,True])
def test_native_pnm_round_trip_unicode_and_first_whitespace_sample(tmp_path,dtype,color):
    shape=(7,9,3) if color else (7,9)
    image=np.arange(np.prod(shape),dtype=dtype).reshape(shape)
    image.flat[0]=10
    if dtype==np.uint16: image.flat[-1]=65535
    path=tmp_path/("测试.ppm" if color else "测试.pgm")
    ij.imwrite(path,image)
    restored=ij.imread(path)
    assert_array_equal(restored,image)
    assert restored.dtype==image.dtype


def test_ascii_pnm_comments_truncation_and_numpy_io(tmp_path):
    filename=tmp_path/"ascii.pgm"
    filename.write_bytes(b"P2\n# comment\n3 2\n65535\n10 20 30\n40 50 65535\n")
    assert_array_equal(ij.imread(filename),[[10,20,30],[40,50,65535]])
    filename.write_bytes(b"P5\n3 2\n255\n\x00")
    with pytest.raises(RuntimeError,match="Truncated"): ij.imread(filename)
    image=np.arange(63,dtype=np.float32).reshape(7,9)
    ij.imwrite(tmp_path/"array.npy",image)
    assert_array_equal(ij.imread(tmp_path/"array.npy"),image)


def test_optional_png_and_volume_tiff_io(tmp_path):
    pytest.importorskip("PIL"); pytest.importorskip("tifffile")
    image=np.random.default_rng(1).integers(0,256,(7,9,4),dtype=np.uint8)
    ij.imwrite(tmp_path/"rgba.png",image)
    assert_array_equal(ij.imread(tmp_path/"rgba.png"),image)
    stack=np.arange(3*5*7,dtype=np.uint16).reshape(3,5,7)
    ij.imwrite(tmp_path/"stack.tif",stack,photometric="minisblack")
    assert_array_equal(ij.imread(tmp_path/"stack.tif"),stack)


@pytest.mark.parametrize("operation",[
    lambda a:ij.divide(a,0), lambda a:ij.gamma(a,-1), lambda a:ij.crop(a,4,0,2,2),
    lambda a:ij.resize(a,0,5), lambda a:ij.convolve(a,np.ones((2,2))),
    lambda a:ij.convolve(a,[[1,0,-1]],normalize=True), lambda a:ij.rank_filter(a,-1),
    lambda a:ij.rank_filter(a,1,footprint="invalid"), lambda a:ij.gaussian_blur(a,-1),
    lambda a:ij.smooth(a,border="invalid"), lambda a:ij.clahe(a,tile_size=0),
    lambda a:ij.bilateral_filter(a,sigma_range=0), lambda a:ij.anisotropic_diffusion(a,step=.3),
    lambda a:ij.threshold(a,10,2), lambda a:ij.local_threshold(a,"invalid"),
    lambda a:ij.auto_threshold(a,"invalid"), lambda a:ij.calculate(a,a[:2],"add"),
    lambda a:ij.statistics(a,mask=np.zeros(a.shape,bool)), lambda a:ij.histogram(a,0,range=(1,1)),
    lambda a:ij.label(a,connectivity=6), lambda a:ij.watershed(a,np.zeros(a.shape,np.int32)),
    lambda a:ij.fft2(a,pad=False), lambda a:ij.distance_transform(a,spacing=(0,1)),
    lambda a:ij.math(a,"invalid"), lambda a:ij.math(a,"and",1.2),
    lambda a:ij.remove_small_objects(a,-1),lambda a:ij.fft_bandpass(a,.2,.1),
    lambda a:ij.resize(a,2**31-1,1),
])
def test_invalid_arguments_raise_without_input_mutation(operation):
    image=np.arange(15,dtype=np.uint8).reshape(3,5); original=image.copy()
    with pytest.raises(ValueError): operation(image)
    assert_array_equal(image,original)


@pytest.mark.parametrize("dtype",[np.int8,np.int32,np.float16,np.complex128])
def test_unsupported_pixel_types(dtype):
    with pytest.raises(ValueError): ij.add(np.ones((3,5),dtype=dtype),1)


@pytest.mark.parametrize("image",[np.empty((0,3)),np.ones(3),np.ones((3,5,2)),np.array([[np.nan]]),np.array([[np.inf]])])
def test_invalid_array_layout_and_nonfinite_pixels(image):
    with pytest.raises(ValueError): ij.gaussian_blur(image)


def test_domain_color_and_label_validation():
    with pytest.raises(ValueError): ij.sqrt(np.array([[-1.]]))
    with pytest.raises(ValueError): ij.log(np.array([[0.]]))
    with pytest.raises(ValueError): ij.exp(np.array([[999.]]))
    with pytest.raises(ValueError): ij.rgb_to_hsv(np.ones((3,4,3))*2)
    with pytest.raises(ValueError): ij.hsv_to_rgb(np.ones((3,4,3))*2)
    with pytest.raises(ValueError): ij.color_deconvolution(np.ones((3,4,3)),np.ones((3,3)))
    with pytest.raises(ValueError): ij.region_properties(np.array([[-1]],np.int32))
    with pytest.raises(ValueError): ij.region_properties(np.array([[2**32]],np.uint64))
    with pytest.raises(ValueError): ij.fill(np.zeros((3,4)),1,mask=np.ones((2,4)))
    with pytest.raises(ValueError): ij.watershed(np.zeros((3,4)),np.ones((3,4),np.int32),mask=np.zeros((3,4),bool))


def test_noise_repeatability_and_license_gate():
    image=np.full((9,11),100,np.uint8)
    assert_array_equal(ij.add_noise(image,5,seed=12),ij.add_noise(image,5,seed=12))
    assert not np.array_equal(ij.add_noise(image,5,seed=12),ij.add_noise(image,5,seed=13))
    assert_array_equal(ij.add_noise(image,0),image)
    root=Path(__file__).resolve().parents[2]
    result=subprocess.run([sys.executable,str(root/"scripts/check_licenses.py")],capture_output=True,text=True)
    assert result.returncode==0,result.stderr
