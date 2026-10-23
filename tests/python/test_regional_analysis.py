"""Canonical regional geometry at analysis and contrast consumption boundaries.

Small independent literal membership/mapping expectations accompany legacy
projection comparisons. Producer fixtures and historical Auto inputs stay fixed.
"""
import copy
import json
import numpy as np
import pytest
import imagejxx as ij
from test_contrast_apply import options, rgb_options
from test_auto_contrast import _request, _state


def owner(dtype=np.uint16, rgb=False, slices=1):
    values = np.arange(81, dtype=dtype).reshape(9, 9)
    pixels = np.repeat(values[..., None], 3, axis=-1) if rgb else values[..., None]
    pixels = np.repeat(pixels[None], slices, axis=0)
    reads = ij.DatasetReadObserver()
    doc = ij.Document(ij.Dataset(pixels, axes='ZYXS').observe_reads(reads))
    if not rgb:
        view = doc.snapshot().display; view.properties = {'channel_display': _state()}; doc.set_display(view)
    seed = np.zeros((9, 9), np.uint8); seed[0, 0] = 255
    region = ij.SelectionRegion(ij.Selection.from_mask(seed), (9, 9)).enlarged(2).as_selection()
    doc.select(region)
    return doc, reads, pixels.copy(), region


@pytest.mark.parametrize('dtype', [np.uint8, np.uint16, np.float32])
@pytest.mark.parametrize('uniform', [False, True])
def test_histogram_uses_visible_members_and_keeps_full_frame(dtype, uniform):
    doc, reads, pixels, region = owner(dtype)
    req = ij.DatasetHistogramRequest(); req.area_roi = region
    if uniform:
        req.binning = ij.HistogramBinning.UniformRange; req.bins = 4; req.range = (0, 20)
    estimate = ij.estimate_dataset_histogram(doc.snapshot().dataset, req)
    assert reads.counts().plane_reads == 0 and estimate.selection_bytes >= 7
    result = ij.accumulate_dataset_histogram(doc.snapshot().dataset, req)
    assert result.visited_count == result.finite_count == 8
    assert result.sum == 70 and (result.minimum, result.maximum) == (0, 19)
    assert result.plane_finite_count == 81 and result.plane_maximum == 80
    if uniform: assert result.histogram['counts'].tolist() == [3, 1, 2, 2]
    assert req.area_roi == doc.snapshot().selection == region
    assert region.mask_origin == [-3, -3] and region.to_dict()['shape'] == [7, 7]
    np.testing.assert_array_equal(doc.snapshot().dataset.numpy('ZYXS'), pixels)


@pytest.mark.parametrize('dtype', [np.uint8, np.uint16, np.float32])
@pytest.mark.parametrize('whole', [False, True])
def test_auto_v2_roundtrip_captures_full_geometry_without_changing_math(dtype, whole):
    doc, reads, pixels, region = owner(dtype)
    command = ij.resolve_auto_contrast_command(doc.snapshot(), _request(whole=whole))
    record = json.loads(json.dumps(command.record()))
    assert record['version'] == (1 if whole else 2)
    ij.validate_auto_contrast_command_record(record)
    replay = ij.auto_contrast_command_from_record(doc.snapshot(), record)
    assert replay.record() == command.record()
    if not whole:
        assert replay.selection_provenance == region
        assert list(replay.resolved_range) == [0, 255 if dtype == np.uint8 else 80]
        assert replay.cycle.next == 0
        bad = copy.deepcopy(record); bad['version'] = 1
        with pytest.raises(ValueError): ij.validate_auto_contrast_command_record(bad)
    assert doc.snapshot().selection == region and doc.undo_steps == 0
    np.testing.assert_array_equal(doc.snapshot().dataset.numpy('ZYXS'), pixels)


@pytest.mark.parametrize('rgb', [False, True])
@pytest.mark.parametrize('positioned', [False, True])
@pytest.mark.parametrize('slices', [1, 3])
def test_apply_v2_strict_replay_and_undo_retain_geometry(rgb, positioned, slices):
    doc, reads, pixels, region = owner(np.uint8 if rgb else np.uint16, rgb, slices)
    target = ij.TargetRequest(ij.TargetScope.ALL_Z,
        roi=ij.RoiPolicy.POSITIONED_ROIS if positioned else ij.RoiPolicy.ACTIVE_SELECTION)
    if positioned:
        edit = doc.begin_edit(); edit.set_rois([ij.ROI('regional', region)]); doc.commit(edit)
    resolve = ij.resolve_rgb_contrast_apply_command if rgb else ij.resolve_contrast_apply_command
    validate = ij.validate_rgb_contrast_apply_command_record if rgb else ij.validate_contrast_apply_command_record
    decode = ij.rgb_contrast_apply_command_from_record if rgb else ij.contrast_apply_command_from_record
    execute = ij.execute_rgb_contrast_apply_command if rgb else ij.execute_contrast_apply_command
    command = resolve(doc.snapshot(), rgb_options(0, 80, 4) if rgb else options(0, 80, 8), target)
    assert reads.counts().plane_reads == 0
    record = json.loads(json.dumps(command.record())); assert record['schema_version'] == 2
    validate(record); strict = decode(doc.snapshot(), record)
    bad = copy.deepcopy(record); bad['schema_version'] = 1
    with pytest.raises(ValueError): validate(bad)
    before = doc.snapshot(); undo_before = doc.undo_steps; doc.commit(execute(strict))
    actual = doc.snapshot().dataset.numpy('ZYXS')
    expected = pixels.copy()
    mask = np.array([[1,1,1,0,0,0,0,0,0], [1,1,1,0,0,0,0,0,0],
                     [1,1,0,0,0,0,0,0,0]]+[[0]*9]*6, bool)
    # ImageJ retains its separate UInt16 and RGB mapping equations.
    mapped = np.floor(np.arange(81).reshape(9,9)*(256 if rgb else 255)/80).clip(0,255)
    for z in range(slices): expected[z,...,0][mask] = mapped[mask]
    np.testing.assert_array_equal(actual, expected)
    assert doc.snapshot().selection == region and doc.undo_steps == undo_before+1
    doc.undo(); assert doc.snapshot().shares_data(before)


@pytest.mark.parametrize('scope', [ij.TargetScope.CURRENT_PLANE, ij.TargetScope.ALL_Z])
def test_histogram_workflow_after_full_enlargement_is_stage_relative(scope):
    doc, _, _, region = owner(slices=3)
    target = ij.TargetRequest(scope, roi=ij.RoiPolicy.ACTIVE_SELECTION)
    opts = ij.TargetHistogramOptions(); opts.binning = ij.HistogramBinning.UniformRange
    opts.bins = 4; opts.range = (0, 20)
    record = ij.resolve_histogram_command(doc.snapshot(), opts, target).record()
    # Histogram V1 records intent/revisions, rather than a geometry payload.
    assert record['schema_version'] == 1
    prepared = ij.prepare_native_workflow(doc.snapshot(), [ij.NativeWorkflowHistogram('region.csv', target, record)])
    result = prepared.histograms[0]['result']
    factor = 3 if scope == ij.TargetScope.ALL_Z else 1
    assert result.visited_count == 8*factor and result.sum == 70*factor
    assert prepared.preview.selection == region and doc.undo_steps == 0


@pytest.mark.parametrize('rgb', [False, True])
def test_regional_admission_caps_precede_reads_and_callbacks(rgb):
    doc, reads, _, _ = owner(np.uint8 if rgb else np.uint16, rgb)
    limits = ij.ContrastApplyCommandLimits()
    limits.max_record_bytes = 1
    resolve = ij.resolve_rgb_contrast_apply_command if rgb else ij.resolve_contrast_apply_command
    with pytest.raises(ValueError):
        resolve(doc.snapshot(), rgb_options(0,80) if rgb else options(0,80,8), limits=limits)
    assert reads.counts().plane_reads == 0


@pytest.mark.parametrize('depth,accepted', [(123, True), (124, True), (125, False)])
@pytest.mark.parametrize('strings', [False, True])
def test_auto_complete_transport_depth_is_admitted_before_io(depth, accepted, strings):
    doc, reads, _, region = owner()
    nested = ['text'] if strings else 0
    for _ in range(depth-int(strings)): nested = [nested]
    doc.select(ij.SelectionEditor(region, (9,9)).properties(1, {'deep': nested}))
    if accepted:
        record = ij.resolve_auto_contrast_command(doc.snapshot()).record()
        ij.validate_auto_contrast_command_record(json.loads(json.dumps(record)))
    else:
        with pytest.raises(ValueError): ij.resolve_auto_contrast_command(doc.snapshot())
        assert reads.counts().plane_reads == 0


@pytest.mark.parametrize('stack', [False, True])
@pytest.mark.parametrize('live_regional', [False, True])
def test_rgb_session_historical_regions_are_versioned_independently_of_live_selection(stack, live_regional):
    from test_rgb_contrast_session import step, visit, plan
    doc, reads, pixels, region = owner(np.uint8, True, 2)
    if not live_regional: doc.select(None)
    request = plan([visit((0,0,0), [step(0,80,4,region)])], stack)
    command = ij.resolve_rgb_contrast_session_command(doc.snapshot(), request)
    assert reads.counts().plane_reads == 0
    record = json.loads(json.dumps(command.record()))
    assert record['schema_version'] == 2
    assert record['capture']['schema_version'] == (2 if live_regional else 1)
    ij.validate_rgb_contrast_session_command_record(record)
    replay = ij.rgb_contrast_session_command_from_record(doc.snapshot(), record)
    assert replay.plan.visits[0].stages[0].selection == region
    bad = copy.deepcopy(record); bad['schema_version'] = 1
    with pytest.raises(ValueError): ij.validate_rgb_contrast_session_command_record(bad)
    result = ij.execute_rgb_contrast_session_command(replay).preview()
    values = result.dataset.numpy('ZYXS')
    assert values[0,0,1,0] == 3 and values[0,0,1,1] == 1
    assert values[0,8,8,0] == 80
    assert values[1,0,1,0] == (3 if stack else 1)
    assert result.selection == doc.snapshot().selection
    np.testing.assert_array_equal(doc.snapshot().dataset.numpy('ZYXS'), pixels)


@pytest.mark.parametrize('rgb', [False, True])
@pytest.mark.parametrize('depth,accepted', [(124,True), (125,True), (126,False)])
@pytest.mark.parametrize('strings', [False, True])
def test_apply_complete_transport_depth_precedes_io(rgb, depth, accepted, strings):
    doc, reads, _, region = owner(np.uint8 if rgb else np.uint16, rgb)
    nested = ['text'] if strings else 0
    for _ in range(depth-int(strings)): nested = [nested]
    doc.select(ij.SelectionEditor(region, (9,9)).properties(1, {'deep': nested}))
    resolve = ij.resolve_rgb_contrast_apply_command if rgb else ij.resolve_contrast_apply_command
    validate = ij.validate_rgb_contrast_apply_command_record if rgb else ij.validate_contrast_apply_command_record
    if accepted:
        record = resolve(doc.snapshot(), rgb_options(0,80) if rgb else options(0,80,8)).record()
        validate(json.loads(json.dumps(record)))
    else:
        with pytest.raises(ValueError): resolve(doc.snapshot(), rgb_options(0,80) if rgb else options(0,80,8))
    assert reads.counts().plane_reads == 0


@pytest.mark.parametrize('offset', [(100,100), (-100,-100), (2147483600,0), (-2147483600,0)])
def test_off_image_regional_histogram_has_no_visible_members(offset):
    doc, _, _, region = owner()
    moved = ij.SelectionEditor(region,(9,9)).moved(offset)
    req = ij.DatasetHistogramRequest(); req.area_roi = moved
    result = ij.accumulate_dataset_histogram(doc.snapshot().dataset, req)
    assert result.visited_count == 0 and result.empty_finite
    assert req.area_roi == moved and result.plane_finite_count == 81


def test_shared_imgui_contrast_panel_consumes_regional_selection(tmp_path):
    from imagejxx.ui.app import Editor
    from test_imgui_roi_style import click
    doc, _, _, region = owner(np.uint8)
    editor = Editor(preferences=tmp_path/'preferences.json')
    editor._editor.add_virtual_document(doc)
    editor.action('show_contrast'); editor.action('wait_jobs')
    frame = editor.render_headless(frames=3)
    # The contrast plot has an independent whole-plane policy; Auto defaults
    # to the active area and its captured provenance must retain all geometry.
    assert frame['contrast_panel']['histogram']['finite_count'] == 81
    editor.recording = True
    assert frame['contrast_panel']['controls']['auto']['enabled']
    editor.render_headless(events=click(frame['contrast_panel']['controls']['auto']['point']))
    editor.action('wait_jobs')
    assert doc.snapshot().selection == region
    assert editor.workflow[-1]['version'] == 2
    assert editor.workflow[-1]['roi']['selection']['origin'] == [-3,-3]
    editor.action('contrast_range', minimum=0., maximum=80.)
    frame = editor.render_headless(frames=3)
    assert frame['contrast_panel']['controls']['apply']['enabled']
    editor.render_headless(events=click(frame['contrast_panel']['controls']['apply']['point']))
    editor.action('wait_jobs')
    frame = editor.render_headless(frames=3)
    assert doc.snapshot().selection == region and doc.undo_steps == 1
    assert doc.snapshot().dataset.numpy('ZYXS')[0,0,1,0] == 3
    assert any(r.get('schema_version') == 2 for r in editor.workflow)
    (tmp_path/'regional-contrast-panel.json').write_text(json.dumps(frame))
    editor.action('close_contrast')


@pytest.mark.parametrize('field,value', [('origin',[False,-3]), ('origin',[1.5,-3]),
    ('origin',[2**31,-3]), ('shape',[7,8]), ('points',[[0,0]]), ('extra',1)])
def test_auto_region_wire_is_closed_and_exact(field, value):
    doc, _, _, _ = owner()
    record = ij.resolve_auto_contrast_command(doc.snapshot()).record()
    record['roi']['selection'][field] = value
    with pytest.raises((ValueError, TypeError)): ij.validate_auto_contrast_command_record(record)


def test_quiet_nan_membership_preserves_source_words():
    _, _, _, region = owner()
    words = np.arange(81, dtype=np.float32).reshape(9,9).view(np.uint32)
    words[0,0] = 0x7fc12345; words[8,8] = 0xffc23456
    ds = ij.Dataset(words.view(np.float32), axes='YX',
        sample_policy=ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32)
    req = ij.DatasetHistogramRequest(); req.area_roi = region
    result = ij.accumulate_dataset_histogram(ds,req)
    assert result.visited_count == 8 and result.nan_count == 1 and result.finite_count == 7
    assert result.sum == 70 and result.plane_nan_count == 2
    assert ds.numpy('YX').tobytes() == words.tobytes()


def test_rgb_stack_full_geometry_roundtrip_and_preview():
    doc, reads, _, region = owner(np.uint8,True,2)
    stage = ij.RgbContrastStackStep(); stage.options = rgb_options(0,80,4)
    command = ij.resolve_rgb_contrast_stack_command(doc.snapshot(),[stage])
    assert reads.counts().plane_reads == 0
    record = json.loads(json.dumps(command.record()))
    assert record['schema_version'] == record['capture']['schema_version'] == 2
    ij.validate_rgb_contrast_stack_command_record(record)
    strict = ij.rgb_contrast_stack_command_from_record(doc.snapshot(),record)
    result = ij.execute_rgb_contrast_stack_command(strict).preview()
    assert result.selection == region
    values = result.dataset.numpy('ZYXS')
    assert values[0,0,1,0] == values[1,0,1,0] == 3
    assert values[1,8,8,0] == 80 and values[0,0,1,1] == 1
