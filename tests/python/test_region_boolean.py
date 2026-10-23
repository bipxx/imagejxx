"""Full ImageJ ShapeRoi observations and bounded regional Boolean publication."""
import base64
import json
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij
from test_selection_region import sample

ROOT = Path(__file__).parents[1]/'fixtures'
REGIONS = json.loads((ROOT/'selection-region-boolean-imagej154r.json').read_bytes())
VECTORS = json.loads((ROOT/'selection-region-boolean-vectors-imagej154r.json').read_bytes())
CASES = [('regions',r) for r in REGIONS['cases']]+[('vectors',r) for r in VECTORS['cases']]


@pytest.mark.parametrize('family,row',CASES,ids=[f'{f}-k{r["kind"]}-v{r["variant"]}-o{r["operations"][0]}' for f,r in CASES])
def test_complete_region_boolean_sequences_match_actual_imagej(family,row):
    inputs = [ij.Selection.from_dict(v) if v['kind']=='region' else
              ij.Selection(v['kind'],v['points'],v['width'],attributes=v['attributes']) for v in row['inputs']]
    before = [s.to_dict() for s in inputs];result = inputs[0]
    for index,(operation,stage) in enumerate(zip(row['operations'],row['stages'])):
        estimate = ij.estimate_selection_region_boolean(9,9,[result,inputs[index+1]],operation)
        result = ij.combine_selection_regions(9,9,[result,inputs[index+1]],operation)
        assert result.is_region and estimate.pixels > 0
        region = ij.SelectionRegion(result,(9,9))
        x,y,w,h = stage['sample']
        expected = np.unpackbits(np.frombuffer(base64.b64decode(stage['mask']),np.uint8))[:w*h].reshape(h,w)
        np.testing.assert_array_equal(sample(region,stage['sample']),expected)
        assert np.count_nonzero(region.local_selection.mask(region.shape)) == stage['occupied']
        if stage['occupied']:
            left,top,width,height = stage['bounds']
            assert region.bounds == [left,top,left+width,top+height]
        else:
            assert region.bounds is None and result.mask_origin == [0,0] and result.to_dict()['shape'] == [1,1]
        assert result.attributes == {} and result.width == 1
    assert [s.to_dict() for s in inputs] == before


def one(origin=(0,0)):
    return ij.SelectionRegion(ij.Selection.from_mask(np.ones((1,1),np.uint8)),(1,1),origin).as_selection()


@pytest.mark.parametrize('operation',['intersection','subtraction'])
def test_distant_integer_extremes_do_not_inflate_intersection_or_subtraction(operation):
    a,b = one((-2**31,0)),one((2**31-1,0))
    estimate = ij.estimate_selection_region_boolean(9,9,[a,b],operation)
    assert estimate.pixels == 1
    result = ij.combine_selection_regions(9,9,[a,b],operation)
    assert result.mask_origin == ([0,0] if operation == 'intersection' else [-2**31,0])
    assert np.count_nonzero(ij.SelectionRegion(result,(9,9)).local_selection.mask((1,1))) == (operation == 'subtraction')


@pytest.mark.parametrize('operation',['union','xor'])
def test_unrepresentable_union_span_refuses_before_callbacks(operation):
    calls = []
    with pytest.raises((ValueError,OverflowError)):
        ij.combine_selection_regions(9,9,[one((-2**31,0)),one((2**31-1,0))],operation,checkpoint=lambda:calls.append(True))
    assert calls == []


@pytest.mark.parametrize('operation,count',[('union',3),('intersection',0),('subtraction',1),('xor',3)])
def test_three_ordered_operands_and_empty_intermediate_validation(operation,count):
    values = [one((-5,0)),one((3,0)),one((0,4))]
    result = ij.combine_selection_regions(9,9,values,operation)
    region = ij.SelectionRegion(result,(9,9))
    assert np.count_nonzero(region.local_selection.mask(region.shape)) == count
    if operation == 'subtraction':assert result.mask_origin == [-5,0]
    called = []
    with pytest.raises(ValueError):ij.combine_selection_regions(9,9,[*values[:2],ij.Selection('line',[(0,0),(1,1)])],
        'intersection',checkpoint=lambda:called.append(True))
    assert called == []


@pytest.mark.parametrize('limit',['pixels','working','vertices'])
def test_admission_limits_precede_callbacks(limit):
    a,b = one((-4,-4)),ij.Selection('polygon',[(0,0),(6,1),(2,7)])
    limits = ij.SelectionEditLimits()
    if limit == 'pixels': limits.max_xy_pixels = 1
    if limit == 'working': limits.max_working_bytes = 1
    if limit == 'vertices': limits.max_vertices = 1
    calls = []
    with pytest.raises((ValueError,OverflowError)):
        ij.combine_selection_regions(9,9,[a,b],limits=limits,checkpoint=lambda:calls.append(True))
    assert calls == []


def test_exact_working_cap_and_finish_failure_leave_inputs_untouched():
    a,b = one((-2,-3)),one((4,5));before = [a.to_dict(),b.to_dict()]
    cost = ij.estimate_selection_region_boolean(9,9,[a,b])
    limits = ij.SelectionEditLimits();limits.max_working_bytes = cost.working_bytes
    assert ij.combine_selection_regions(9,9,[a,b],limits=limits).is_region
    limits.max_working_bytes -= 1;called = []
    with pytest.raises((ValueError,OverflowError)): ij.combine_selection_regions(9,9,[a,b],limits=limits,checkpoint=lambda:called.append(True))
    assert called == []
    def fail(done,total):
        if done == total: raise RuntimeError('finish')
    with pytest.raises(RuntimeError,match='finish'): ij.combine_selection_regions(9,9,[a,b],progress=fail)
    assert [a.to_dict(),b.to_dict()] == before


@pytest.mark.parametrize('operation',['union','intersection','subtraction','xor'])
def test_manager_region_publication_retains_rows_pixels_and_previous(operation):
    from test_saved_regional_geometry import fixture
    _,doc,reads,pixels,region,_ = fixture('ieee')
    before = doc.snapshot();history = doc.undo_steps
    result = ij.combine_selection_regions(9,9,[r.selection for r in before.rois],operation)
    estimate = ij.estimate_roi_region_boolean(before,[0,1],operation)
    assert estimate.working_bytes > 0 and reads.counts().plane_reads == 0
    transaction = ij.prepare_roi_region_boolean(before,[0,1],operation)
    assert doc.snapshot().shares_data(before)
    doc.commit(transaction);after = doc.snapshot()
    assert after.selection == result and after.previous_selection == before.selection
    assert [r.selection for r in after.rois] == [r.selection for r in before.rois]
    assert after.dataset.shares_pixels(before.dataset) and doc.undo_steps == history
    assert reads.counts().plane_reads == reads.counts().region_reads == 0
    restored = ij.decode_session(ij.encode_session(ij.SessionCapture([after]))).documents[0].snapshot()
    assert restored.selection == result and restored.dataset.numpy('ZYXS').tobytes() == pixels.tobytes()


@pytest.mark.parametrize('indices,origin',[([0,1],[-5,0]),([1,0],[3,0])])
def test_manager_subtraction_uses_the_callers_order(indices,origin):
    doc = ij.Document(ij.Dataset(np.zeros((9,9),np.uint8),axes='YX'))
    edit = doc.begin_edit();edit.set_rois([ij.ROI('a',one((-5,0))),ij.ROI('b',one((3,0)))]);doc.commit(edit)
    doc.commit(ij.prepare_roi_region_boolean(doc.snapshot(),indices,'subtraction'))
    assert doc.snapshot().selection.mask_origin == origin


def test_editor_dispatches_regional_operands_and_explicit_full_vector_mode(tmp_path):
    from imagejxx.ui.app import Editor
    from test_saved_regional_geometry import fixture
    _,doc,reads,_,region,_ = fixture()
    editor = Editor(preferences=tmp_path/'preferences.json');editor._editor.add_virtual_document(doc)
    editor.action('combine_rois',indices=[0,1],operation='union')
    assert doc.snapshot().selection.is_region and reads.counts().plane_reads == 0
    assert ij.SelectionRegion(doc.snapshot().selection,(9,9)).bounds == ij.SelectionRegion(region,(9,9)).bounds
    with pytest.raises(ValueError): editor.action('combine_rois',indices=[0,1],geometry='canvas_v1')
    edit = doc.begin_edit();edit.set_rois([ij.ROI('left',ij.Selection('rectangle',[(-5,-4),(-2,1)])),
        ij.ROI('right',ij.Selection('ellipse',[(2,2),(7,8)]))]);doc.commit(edit)
    editor.live_document().combine_rois([0,1],geometry='full_region_v2')
    assert doc.snapshot().selection.is_region and doc.snapshot().selection.mask_origin[0] == -5


@pytest.mark.parametrize('geometry',[None,'full_region_v2'])
def test_detached_python_model_retains_intrinsic_bitmap_origin(geometry):
    from imagejxx.ui.model import Document,NamedROI,Selection
    document = Document(np.zeros((9,9),np.uint8))
    document.selection = Selection('point',[(4,4)])
    previous = document.selection.to_dict()
    document.rois = [NamedROI('a',Selection.from_dict(one((-5,0)).to_dict())),
        NamedROI('b',Selection.from_dict(one((3,0)).to_dict()))]
    result = document.combine_rois([0,1],geometry=geometry)
    assert result.origin == (-5,0) and result.bitmap.shape == (1,9)
    assert np.count_nonzero(result.bitmap) == 2 and document.previous_selection.to_dict() == previous
    assert document.selection.native() == ij.combine_selection_regions(9,9,[one((-5,0)),one((3,0))])


def test_actual_roi_manager_ctrl_selection_and_union_keeps_off_image_cells(tmp_path):
    from imagejxx.ui.app import Editor
    from test_imgui_roi_style import click,key
    from test_saved_regional_geometry import fixture
    _,doc,_,pixels,region,_ = fixture()
    before = doc.snapshot();history = doc.undo_steps
    editor = Editor(preferences=tmp_path/'preferences.json');editor._editor.add_virtual_document(doc)
    frame = editor.render_headless(frames=3)
    frame = editor.render_headless(events=click(frame['roi_manager']['controls']['tab']['point']))
    assert frame['roi_manager']['visible']
    for index in [0,1]:
        control = frame['roi_manager']['controls'][f'row.{index}']
        assert control['enabled'] and control['id']
        frame = editor.render_headless(events=[key('Ctrl',True),*click(control['point']),key('Ctrl',False)])
    assert frame['roi_manager']['selected_indices'] == [0,1]
    union = frame['roi_manager']['controls']['union'];assert union['enabled'] and union['id']
    frame = editor.render_headless(events=click(union['point']))
    (tmp_path/'regional-manager-union.json').write_text(json.dumps(frame))
    after = doc.snapshot();assert after.selection.is_region and after.previous_selection == before.selection
    full = ij.SelectionRegion(after.selection,(9,9))
    assert full.bounds == [-2,-2,3,3]
    assert np.count_nonzero(full.local_selection.mask(full.shape)) == 21
    assert np.count_nonzero(after.selection.mask((9,9))) == 8
    assert [r.selection for r in after.rois] == [r.selection for r in before.rois]
    assert doc.undo_steps == history and after.dataset.numpy('ZYXS').tobytes() == pixels.tobytes()
