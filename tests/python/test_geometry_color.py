import colorsys
import numpy as np
import pytest
from numpy.testing import assert_allclose, assert_array_equal
import imagejxx as ij


@pytest.mark.parametrize("turns",[-5,-1,0,1,2,3,4,7])
@pytest.mark.parametrize("color",[False,True])
def test_rotation_and_flips(turns,color):
    a=np.arange(5*7*3 if color else 5*7,dtype=np.uint16).reshape((5,7,3) if color else (5,7))
    assert_array_equal(ij.rotate90(a,turns),np.rot90(a,turns))
    assert_array_equal(ij.flip(a,horizontal=True,vertical=True),a[::-1,::-1])
    assert_array_equal(ij.rotate(a,90,expand=True,interpolation="nearest"),np.rot90(a))


def test_crop_masked_paste_canvas_and_roi():
    image=np.arange(35,dtype=np.uint8).reshape(5,7)
    assert_array_equal(ij.crop(image,2,1,3,2),image[1:3,2:5])
    source=np.full((3,4),200,np.uint8)
    selection=np.array([[1,0,1,0],[0,1,0,1],[1,1,0,0]],bool)
    expected=image.copy(); expected[:2,:3]=np.where(selection[1:,1:],source[1:,1:],image[:2,:3])
    assert_array_equal(ij.paste(image,source,-1,-1,mask=selection),expected)
    canvas=np.full((8,10),23,np.uint8); canvas[2:7,1:8]=image
    assert_array_equal(ij.resize_canvas(image,10,8,offset=(1,2),value=23),canvas)
    rectangle=ij.rectangle_mask(image.shape,1,2,3,2)
    expected=image.copy(); expected[2:4,1:4]=255
    assert_array_equal(ij.fill(image,255,mask=rectangle),expected)
    assert_array_equal(ij.polygon_mask(image.shape,[(1,2),(4,2),(4,4),(1,4)]),rectangle)
    ellipse=ij.ellipse_mask((7,9),center=(4.5,3.5),radii=(3,2))
    yy,xx=np.mgrid[:7,:9]
    assert_array_equal(ellipse,(((xx-4)/3)**2+((yy-3)/2)**2<=1).astype(np.uint8)*255)


def test_pixel_center_resampling_and_translation():
    image=np.array([[0.,10.],[20.,30.]])
    assert_allclose(ij.resize(image,3,3),[[0,5,10],[10,15,20],[20,25,30]])
    assert_allclose(ij.resize(image,3,3,interpolation="nearest"),[[0,10,10],[20,30,30],[20,30,30]])
    assert_allclose(ij.resize(image,2,2,interpolation="cubic"),image)
    assert_array_equal(ij.affine_transform(image,[[1,0,0],[0,1,0]]),image)
    source=np.arange(35,dtype=np.uint16).reshape(5,7)
    expected=np.full_like(source,42); expected[1:,2:]=source[:-1,:-2]
    assert_array_equal(ij.translate(source,2,1,interpolation="nearest",cval=42),expected)
    assert_allclose(ij.line_profile(source,(0,2),(6,2)),source[2])
    assert_allclose(ij.line_profile(image,(0,0),(1,1),samples=3),[0,15,30])
    line=ij.draw_line(np.zeros((5,7),np.uint8),(1,2),(5,2),123)
    expected=np.zeros((5,7),np.uint8); expected[2,1:6]=123
    assert_array_equal(line,expected)


@pytest.mark.parametrize("method",["minimum","maximum","mean","sum"])
def test_partial_edge_binning(method):
    source=np.arange(35,dtype=float).reshape(5,7)
    operation={"minimum":np.min,"maximum":np.max,"mean":np.mean,"sum":np.sum}[method]
    expected=np.array([[operation(source[y:y+3,x:x+2]) for x in range(0,7,2)] for y in range(0,5,3)])
    assert_allclose(ij.binning(source,(3,2),method=method),expected)


@pytest.mark.parametrize("dtype",[np.uint8,np.uint16,np.float64])
def test_hsv_round_trip_and_luminance(dtype):
    normalized=np.random.default_rng(9).random((4,7,3))
    maximum=1 if dtype==np.float64 else np.iinfo(dtype).max
    rgb=(normalized*maximum).astype(dtype)
    hsv=ij.rgb_to_hsv(rgb)
    expected=np.array([colorsys.rgb_to_hsv(*pixel) for pixel in (rgb.astype(float)/maximum).reshape(-1,3)]).reshape(rgb.shape)
    assert_allclose(hsv,expected,atol=1e-12)
    restored=ij.hsv_to_rgb(hsv,dtype=dtype)
    if dtype==np.float64: assert_allclose(restored,rgb,atol=1e-12)
    else: assert_array_equal(restored,rgb)
    assert_array_equal(ij.merge_channels(ij.split_channels(rgb)),rgb)
    primaries=np.eye(3).reshape(1,3,3)
    assert_allclose(ij.grayscale(primaries),[[.299,.587,.114]])
    assert_array_equal(ij.grayscale((primaries*255).astype(np.uint8)),[[76,150,29]])


def test_color_ranges_alpha_and_stain_unmixing():
    rgba=np.array([[[255,0,0,33],[0,255,0,77],[0,0,255,99]]],np.uint8)
    assert_array_equal(ij.in_range(rgba,[250,0,0,0],[255,5,5,255]),[[255,0,0]])
    assert_array_equal(ij.grayscale(rgba),[[76,150,29]])
    assert ij.rgb_to_hsv(rgba).shape==(1,3,3)
    stains=np.array([[1,.2,.1],[.1,1,.2],[.2,.1,1.]])
    normalized=stains/np.linalg.norm(stains,axis=1,keepdims=True)
    concentrations=np.random.default_rng(7).random((4,5,3))*.3
    optical_density=concentrations@normalized
    pixels=(256/255)*np.exp(-optical_density)-1/255
    assert_allclose(ij.color_deconvolution(pixels,stains),concentrations,atol=1e-12)


def test_editor_ownership_roi_undo_redo_and_shape_changes():
    source=np.arange(35,dtype=np.uint8).reshape(5,7); original=source.copy()
    editor=ij.Editor(source,history_limit=2)
    source[:]=0
    assert_array_equal(editor.pixels,original)
    selection=ij.rectangle_mask(original.shape,1,1,3,2)
    editor.select(selection).apply(ij.add,10)
    expected=original.copy(); expected[1:3,1:4]+=10
    assert_array_equal(editor.pixels,expected)
    editor.undo(); assert_array_equal(editor.pixels,original)
    editor.redo(); assert_array_equal(editor.pixels,expected)
    editor.apply(ij.crop,1,1,3,2)
    assert_array_equal(editor.pixels,expected[1:3,1:4])
    editor.undo(); assert_array_equal(editor.pixels,expected)
    exported=editor.pixels; exported[:]=0
    assert_array_equal(editor.pixels,expected)
    editor.select(selection)
    with pytest.raises(ValueError): editor.apply(ij.crop,1,1,3,2)
    assert_array_equal(editor.pixels,expected)
