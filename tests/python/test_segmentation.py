import numpy as np
import pytest
from numpy.testing import assert_allclose, assert_array_equal
from scipy import ndimage as ndi
import imagejxx as ij


@pytest.mark.parametrize("connectivity",[4,8])
def test_labels_agree_with_scipy(connectivity):
    mask=np.random.default_rng(21).random((27,31))>.7
    structure=ndi.generate_binary_structure(2,1 if connectivity==4 else 2)
    expected,count=ndi.label(mask,structure)
    actual,actual_count=ij.label(mask,connectivity=connectivity,return_num=True)
    assert count==actual_count
    assert_array_equal(actual,expected)


def test_calibrated_region_properties_sparse_ids_and_filtering():
    labels=np.zeros((7,9),np.int32); labels[2:5,3:7]=1000000
    intensity=np.arange(63,dtype=np.float64).reshape(7,9)
    regions=ij.region_properties(labels,intensity=intensity,spacing=(2,3))
    assert len(regions)==1
    p=regions[0]
    assert p["label"]==1000000 and p["pixels"]==12 and p["bbox"]==(3,2,4,3)
    assert p["area"]==72 and p["perimeter"]==36 and p["touches_border"] is False
    assert_allclose(p["centroid"],(6,13.5))
    assert_allclose(p["mean"],intensity[2:5,3:7].mean())
    assert_allclose(p["integrated_density"],intensity[2:5,3:7].sum()*6)
    mask=labels>0; mask[0,0]=True
    assert len(ij.analyze_particles(mask,min_area=3,exclude_edges=True))==1
    assert ij.analyze_particles(mask,min_area=13)==[]


@pytest.mark.parametrize("spacing",[(1,1),(.7,2.3),(4,.5)])
def test_exact_distance_transform(spacing):
    mask=np.random.default_rng(1).random((19,23))>.1
    expected=ndi.distance_transform_edt(mask,sampling=spacing)
    assert_allclose(ij.distance_transform(mask,spacing=spacing),expected,atol=1e-12)
    mask=np.ones((7,9),np.uint8); mask[3,4]=0
    yy,xx=np.mgrid[:7,:9]
    assert_allclose(ij.distance_transform(mask,spacing=spacing),np.hypot((yy-3)*spacing[0],(xx-4)*spacing[1]),atol=1e-12)
    assert np.isinf(ij.distance_transform(np.ones((3,4),bool))).all()
    assert_array_equal(ij.distance_transform(np.zeros((3,4),bool)),0)


@pytest.mark.parametrize("connectivity",[4,8])
def test_fill_holes_and_component_cleanup(connectivity):
    mask=np.zeros((13,17),bool); mask[2:11,3:14]=True; mask[4:9,5:12]=False
    structure=ndi.generate_binary_structure(2,1 if connectivity==4 else 2)
    assert_array_equal(ij.fill_holes(mask,connectivity=connectivity),ndi.binary_fill_holes(mask,structure).astype(np.uint8)*255)
    filled=ij.fill_holes(mask); filled[0,0]=255
    expected=filled.copy(); expected[0,0]=0
    assert_array_equal(ij.remove_small_objects(filled,4,connectivity=connectivity),expected)
    assert_array_equal(ij.clear_border(filled,connectivity=connectivity),expected)
    outline=expected.astype(bool)&~ndi.binary_erosion(expected,structure=np.ones((3,3)),border_value=0)
    assert_array_equal(ij.binary_outline(expected),outline.astype(np.uint8)*255)


@pytest.mark.parametrize("shape", [(7, 4096), (4096, 7), (1, 4096), (4096, 1)])
def test_distance_scratch_reuse_across_empty_lines_and_axis_lengths(shape):
    mask = np.ones(shape, dtype=bool)
    mask[::2, ::3] = False
    spacing = (0.75, 1.5)
    expected = ndi.distance_transform_edt(mask, sampling=spacing)
    assert_allclose(ij.distance_transform(mask, spacing=spacing), expected, atol=1e-12)


def test_skeleton_topology_and_idempotence():
    mask=np.zeros((21,27),bool); mask[4:17,5:22]=True; mask[8:13,10:17]=False
    skeleton=ij.skeletonize(mask)
    assert ((skeleton!=0)<=mask).all()
    assert 0<np.count_nonzero(skeleton)<np.count_nonzero(mask)
    assert ij.label(skeleton,return_num=True)[1]==1
    assert np.any(ij.fill_holes(skeleton)!=skeleton)
    assert_array_equal(ij.skeletonize(skeleton),skeleton)
    line=np.zeros((7,9),np.uint8); line[3,2:7]=255
    assert_array_equal(ij.skeletonize(line),line)


def test_watershed_ridge_seed_ids_and_mask():
    height=np.zeros((11,13)); height[:,6]=100
    seeds=np.zeros(height.shape,np.int32); seeds[5,2]=1; seeds[5,10]=7
    result=ij.watershed(height,seeds,connectivity=4)
    assert_array_equal(result[:,:6],1); assert_array_equal(result[:,7:],7)
    assert result[5,2]==1 and result[5,10]==7 and np.isin(result,[1,7]).all()
    mask=np.ones(height.shape,bool); mask[0,:]=False
    masked=ij.watershed(height,seeds,mask=mask)
    assert_array_equal(masked[0],0)
    mask[:,6]=False; seeds[5,10]=0
    unreachable=ij.watershed(height,seeds,mask=mask)
    assert_array_equal(unreachable[:,7:],0)


def test_threshold_manual_boundaries_and_maxima():
    source=np.array([[0,1,2,3,4]],np.uint8)
    assert_array_equal(ij.threshold(source,1,3),[[0,255,255,255,0]])
    assert_array_equal(ij.auto_mask(source,"mean"),[[0,0,0,255,255]])
    assert_array_equal(ij.auto_mask(source,"mean",bright=False),[[255,255,255,0,0]])
    assert_array_equal(ij.local_maxima(np.ones((5,5))),0)
    impulse=np.zeros((5,5)); impulse[2,2]=1
    assert_array_equal(ij.local_maxima(impulse),impulse.astype(np.uint8)*255)


@pytest.mark.parametrize("connectivity",[4,8])
def test_automatic_binary_watershed_touching_objects_and_fallback_seeds(connectivity):
    yy,xx=np.mgrid[:33,:43]
    mask=((xx-16)**2+(yy-16)**2<=64)|((xx-26)**2+(yy-16)**2<=64)
    separated=ij.binary_watershed(mask,connectivity=connectivity)
    assert np.unique(separated[mask]).size==2
    assert_array_equal(separated!=0,mask)
    assert_array_equal(ij.binary_watershed(np.ones((2,2),bool),connectivity=connectivity),1)
    assert_array_equal(ij.binary_watershed(np.zeros((3,5),bool),connectivity=connectivity),0)
    mask[3,3]=True
    fallback=ij.binary_watershed(mask,maxima_radius=20,connectivity=connectivity)
    assert_array_equal(fallback!=0,mask)
    assert fallback[3,3]>0
