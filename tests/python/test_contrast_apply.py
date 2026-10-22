"""Native scalar Apply foundation; independent full-domain ImageJ output hashes."""
import hashlib
import json
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij

RGB_FIXTURE = json.loads((Path(__file__).parents[1] / "fixtures/rgb-contrast-apply-imagej154r.json").read_text())


def rgb_options(minimum=10.9, maximum=40.1, channels=7):
    result = ij.RgbContrastApplyOptions()
    result.minimum, result.maximum, result.channels = minimum, maximum, channels
    return result


def rgb_input():
    x = np.arange(256, dtype=np.uint32)
    pixels = np.empty((2, 256, 3), np.uint8)
    for y in range(2):
        pixels[y, :, 0], pixels[y, :, 1] = x, 255 - x
        pixels[y, :, 2] = (73 * x + 19 * y) & 255
    return pixels


@pytest.mark.parametrize("row", RGB_FIXTURE["rows"], ids=lambda r: f'rgb-r{r["range_index"]}-c{r["channels"]}-roi{r["roi"]}')
def test_actual_imagej_rgb_preview_commit_and_next_baseline(row):
    source = rgb_input()
    original = source.copy()
    low, high = RGB_FIXTURE["ranges"][row["range_index"]]
    opts = rgb_options(low, high, row["channels"])
    owner = ij.Document(ij.Dataset(source, axes="YXS"))
    if row["roi"]:
        mask = np.zeros((2, 256), np.uint8)
        mask[:, 17:236] = 1
        if row["roi"] == 2:
            y, x = np.indices(mask.shape)
            mask[(x + y) % 3 == 0] = 0
        owner.select(ij.Selection.from_mask(mask))
    view = owner.snapshot().display
    view.properties = {"display_range": [low, high], "lut": "Grays"}
    owner.set_display(view)
    before = owner.snapshot()
    target = ij.resolve_target(before)
    estimate = ij.estimate_rgb_contrast_apply(target, opts)
    assert estimate.pixels == 512 and estimate.samples == 1536
    prepared = ij.prepare_rgb_contrast_apply(target, opts)
    assert owner.snapshot().shares_data(before) and owner.undo_steps == 0
    owner.commit(prepared)
    applied = owner.snapshot().dataset.numpy("YXS")
    assert hashlib.sha256(applied.tobytes()).hexdigest() == row["sha256"]
    assert owner.snapshot().display.properties == {"display_range": [0., 255.], "lut": "Grays"}
    assert owner.undo_steps == 1
    if not row["roi"]:
        np.testing.assert_array_equal(applied, ij.apply_rgb_contrast_imagej(source, opts))
    second = ij.apply_rgb_contrast_imagej(applied, opts)
    assert hashlib.sha256(second.tobytes()).hexdigest() == row["next_sha256"]
    np.testing.assert_array_equal(source, original)
    owner.undo()
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("YXS"), original)
    assert owner.snapshot().display.properties == before.display.properties
    owner.redo()
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("YXS"), applied)


@pytest.mark.parametrize("scope", ["current", "all_z", "ranges", "positions"])
@pytest.mark.parametrize("roi", ["whole", "active", "positioned"])
@pytest.mark.parametrize("channels", [1, 4, 7])
def test_rgb_transaction_targets_scientific_axes_separately_from_components(scope, roi, channels):
    from test_native_workflow import contrast_target
    pixels = np.tile(np.array([[[10, 20, 30], [20, 30, 40], [30, 40, 50],
                               [40, 50, 60], [50, 60, 70], [60, 70, 80]]], np.uint8), (2, 2, 2, 2, 1, 1))
    owner = ij.Document(ij.Dataset(pixels, axes="TCZYXS"))
    owner.select(ij.Selection.from_mask(np.array([[0, 0, 1, 1, 0, 0]] * 2, np.uint8)))
    edit = owner.begin_edit()
    edit.set_rois([ij.ROI("rgb", ij.Selection("rectangle", [(1, 0), (5, 2)]), t=1, c=1, z=0)])
    owner.commit(edit)
    view = owner.snapshot().display
    view.t, view.c, view.crosshair = 1, 1, [0, 0, 1]
    view.properties = {"display_range": [10., 40.], "lut": "retained"}
    owner.set_display(view)
    before, undo = owner.snapshot(), owner.undo_steps
    request, positions = contrast_target(scope, roi)
    prepared = ij.prepare_rgb_contrast_apply(ij.resolve_target(before, request), rgb_options(10, 40, channels))
    expected = pixels.copy()
    changed = False
    for position in positions:
        if roi == "positioned" and position != (1, 1, 0): continue
        changed = True
        columns = slice(None) if roi == "whole" else slice(2, 4) if roi == "active" else slice(1, 5)
        for c, bit in enumerate([4, 2, 1]):
            if channels & bit:
                expected[position][:, columns, c] = np.clip(np.trunc((pixels[position][:, columns, c].astype(float) - 10) * 256 / 30), 0, 255)
    assert owner.snapshot().shares_data(before)
    owner.commit(prepared)
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYXS"), expected)
    assert owner.undo_steps == undo + changed
    assert owner.snapshot().display.properties["lut"] == "retained"
    assert owner.snapshot().display.properties["display_range"] == ([0., 255.] if changed else [10., 40.])


@pytest.mark.parametrize("bad", [0, 8, -1, 2**40, True, np.bool_(True), 1.5, "red"])
def test_rgb_component_mask_validation_is_atomic(bad):
    source = rgb_input()
    opts = rgb_options()
    with pytest.raises((ValueError, TypeError, OverflowError)):
        opts.channels = bad
        ij.apply_rgb_contrast_imagej(source, opts)
    np.testing.assert_array_equal(source, rgb_input())


@pytest.mark.parametrize("low,high", [(float("nan"), 1), (0, float("inf")), (2, 1)])
def test_rgb_invalid_endpoints_refuse_without_mutation(low, high):
    source = rgb_input()
    with pytest.raises(ValueError): ij.apply_rgb_contrast_imagej(source, rgb_options(low, high))
    np.testing.assert_array_equal(source, rgb_input())


@pytest.mark.parametrize("dtype,samples", [(np.uint8, 1), (np.uint8, 4), (np.uint16, 3), (np.float32, 3)])
def test_rgb_profile_refuses_unqualified_sample_formats(dtype, samples):
    source = np.zeros((2, 4, samples), dtype)
    with pytest.raises((ValueError, TypeError)): ij.apply_rgb_contrast_imagej(source)


@pytest.mark.parametrize("failure", ["pixels", "bytes", "volume", "destination", "opaque"])
def test_rgb_admission_precedes_provider_reads(failure):
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(rgb_input(), axes="YXS").observe_reads(reads))
    request, limits = ij.TargetRequest(), ij.ContrastApplyLimits()
    if failure == "pixels": limits.max_pixels = 1
    elif failure == "bytes": limits.max_working_bytes = 1
    elif failure == "volume": request = ij.TargetRequest(mode=ij.ExecutionMode.VOLUME)
    elif failure == "destination": request = ij.TargetRequest(destination=ij.OutputDestination.NEW_DOCUMENT)
    else:
        view = owner.snapshot().display
        view.properties = {"channel_display": {"future": 1}}
        owner.set_display(view)
    before = owner.snapshot()
    target = ij.resolve_target(before, request)
    for call in [ij.estimate_rgb_contrast_apply, ij.prepare_rgb_contrast_apply]:
        with pytest.raises(ValueError): call(target, rgb_options(), limits)
    assert reads.counts().plane_reads == reads.counts().region_reads == 0
    assert owner.snapshot().shares_data(before) and owner.undo_steps == 0


def test_rgb_empty_mask_preserves_display_history_and_performs_no_reads():
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(rgb_input(), axes="YXS").observe_reads(reads))
    owner.select(ij.Selection.from_mask(np.zeros((2, 256), np.uint8)))
    view = owner.snapshot().display
    view.properties = {"display_range": [10., 40.]}
    owner.set_display(view)
    before = owner.snapshot()
    owner.commit(ij.prepare_rgb_contrast_apply(ij.resolve_target(before), rgb_options()))
    assert reads.counts().plane_reads == reads.counts().region_reads == 0
    assert owner.snapshot().shares_data(before) and owner.undo_steps == 0
    assert owner.snapshot().display.properties == before.display.properties


@pytest.mark.parametrize("failure", ["cancel", "callback", "stale"])
def test_rgb_failure_and_stale_display_reject_atomic_publication(failure):
    owner = ij.Document(ij.Dataset(rgb_input(), axes="YXS"))
    before = owner.snapshot()
    token = ij.CancellationToken()
    def progress(done, total):
        if failure == "cancel": token.cancel()
        elif failure == "callback": raise RuntimeError("controlled RGB callback")
    if failure == "stale":
        edit = ij.prepare_rgb_contrast_apply(ij.resolve_target(before), rgb_options())
        view = before.display
        view.zoom = 2.
        owner.set_display(view)
        with pytest.raises(ij.RevisionConflict): owner.commit(edit)
    else:
        with pytest.raises((RuntimeError, ij.OperationCancelled)):
            ij.prepare_rgb_contrast_apply(ij.resolve_target(before), rgb_options(), cancellation=token, progress=progress)
    assert owner.snapshot().shares_data(before) and owner.undo_steps == 0


def test_rgb_reentrant_controls_cannot_change_captured_mapping():
    source = rgb_input()
    opts = rgb_options()
    expected = ij.apply_rgb_contrast_imagej(source, opts)
    calls = []
    def checkpoint():
        calls.append(True)
        opts.minimum, opts.maximum, opts.channels = 0., 1., 1
    actual = ij.apply_rgb_contrast_imagej(source, opts, checkpoint=checkpoint)
    np.testing.assert_array_equal(actual, expected)
    assert calls

FIXTURE = json.loads((Path(__file__).parents[1] / "fixtures/contrast-apply-imagej154r.json").read_text())


def options(minimum=10.9, maximum=40.9, bits=0):
    value = ij.ContrastApplyOptions()
    value.minimum, value.maximum, value.unsigned16_bits = minimum, maximum, bits
    return value


@pytest.mark.parametrize("case", FIXTURE["rows"], ids=lambda c: f'u{c["depth"]}-b{c["bits"]}-r{c["range_index"]}')
def test_full_domain_actual_imagej_apply(case):
    dtype = np.uint8 if case["depth"] == 8 else np.uint16
    source = np.arange(2**case["depth"], dtype=dtype).reshape(1, -1)
    original = source.copy()
    low, high = FIXTURE["ranges"][case["range_index"]]
    actual = ij.apply_contrast_imagej(source, options(low, high, case["bits"]))
    assert actual.dtype == dtype and actual.shape == source.shape
    assert hashlib.sha256(actual.astype(np.dtype(dtype).newbyteorder('<')).tobytes()).hexdigest() == case["sha256"]
    np.testing.assert_array_equal(source, original)


def test_masked_multiplane_apply_pairs_pixels_and_display_history():
    pixels = np.tile(np.array([10, 20, 30, 40], np.uint16), (2, 1, 1))
    owner = ij.Document(ij.Dataset(pixels, axes="ZYX"))
    owner.select(ij.Selection.from_mask(np.array([[0, 1, 1, 0]], np.uint8)))
    app = ij.Application()
    app.adopt(owner)
    view = owner.snapshot().display
    view.properties = {"display_range": [10., 40.], "retained": {"exact": 42}}
    owner.set_display(view)
    before = owner.snapshot()
    steps = owner.undo_steps
    target = app.resolve_target(ij.TargetRequest(ij.TargetScope.ALL_Z))
    transaction = ij.prepare_contrast_apply(target, options(10, 40, 12))
    assert owner.snapshot().shares_data(before)
    app.commit(transaction)
    expected = np.tile(np.array([10, 1365, 2730, 40], np.uint16), (2, 1, 1))
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("ZYX"), expected)
    assert owner.snapshot().display.properties == {"display_range": [0., 4095.], "retained": {"exact": 42}}
    assert owner.undo_steps == steps + 1 and owner.snapshot().dirty
    owner.undo()
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("ZYX"), pixels)
    assert owner.snapshot().display.properties == before.display.properties
    owner.redo()
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("ZYX"), expected)
    assert owner.snapshot().display.properties["display_range"] == [0., 4095.]


def test_admission_and_cancellation_preserve_source():
    observer = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.arange(48, dtype=np.uint8).reshape(6, 8), axes="YX").observe_reads(observer))
    app = ij.Application()
    app.adopt(owner)
    target = app.resolve_target()
    before = owner.snapshot()
    limits = ij.ContrastApplyLimits()
    limits.max_working_bytes = 1
    with pytest.raises(ValueError):
        ij.prepare_contrast_apply(target, options(), limits)
    assert observer.counts().plane_reads == 0
    token = ij.CancellationToken()
    with pytest.raises(ij.OperationCancelled):
        ij.prepare_contrast_apply(target, options(), cancellation=token,
                                  progress=lambda done, total: token.cancel())
    assert owner.snapshot().shares_data(before) and not owner.snapshot().dirty
    transaction = ij.prepare_contrast_apply(target, options())
    view = owner.snapshot().display
    view.zoom = 2.
    owner.set_display(view)
    stable = owner.snapshot()
    with pytest.raises(ij.RevisionConflict):
        app.commit(transaction)
    assert stable.shares_data(owner.snapshot())
    assert owner.snapshot().display.revision == stable.display.revision


@pytest.mark.parametrize("dtype,samples", [(np.float32, 1), (np.float64, 1), (np.uint8, 3), (np.uint8, 4)])
def test_unsupported_formats_refuse_without_input_changes(dtype, samples):
    source = np.ones((2, 3, samples), dtype=dtype)
    with pytest.raises(ValueError):
        ij.apply_contrast_imagej(source, options())
    assert np.all(source == 1)


@pytest.mark.parametrize("value", [True, np.bool_(False), 1.5, -1, 2**100])
@pytest.mark.parametrize("field", ["max_pixels", "max_working_bytes"])
def test_limits_reject_invalid_python_integers(field, value):
    limits = ij.ContrastApplyLimits()
    before = getattr(limits, field)
    with pytest.raises((TypeError, ValueError, OverflowError)):
        setattr(limits, field, value)
    assert getattr(limits, field) == before


RGB_AUTO_FIXTURE = json.loads((Path(__file__).parents[1] / "fixtures/rgb-histogram-auto-imagej154r.json").read_text())


def rgb_auto_input(scenario):
    y,x=np.indices((8,256));pixels=np.stack([x,(73*x+19*y)&255,255-x],axis=-1).astype(np.uint8)
    if scenario==1:pixels[:]=[20,40,80]
    if scenario>=2:
        pixels[:]=0
        for x in range(32 if scenario==2 else 16):pixels[0,x]=[100+x,110+x,120+x] if scenario==2 else [160]*3
    return pixels


def rgb_auto_selection(roi):
    if roi==4:return ij.Selection('line',[(0,0),(255,7)])
    if roi==0:return None
    mask=np.zeros((8,256),np.uint8)
    if roi!=3:
        mask[:,17:236]=1
        if roi==2:
            y,x=np.indices(mask.shape);mask[(x+y)%3==0]=0
    return ij.Selection.from_mask(mask)


def rgb_histogram_options(weights,channels=7):
    result=ij.RgbHistogramOptions();result.weights=weights
    result.component={4:ij.RgbHistogramComponent.RED,2:ij.RgbHistogramComponent.GREEN,1:ij.RgbHistogramComponent.BLUE}.get(channels,ij.RgbHistogramComponent.INTENSITY)
    return result


def histogram_hash(result):return hashlib.sha256(np.array(result.histogram["counts"],dtype='<u8').tobytes()).hexdigest()


@pytest.mark.parametrize('row',RGB_AUTO_FIXTURE['rows'],ids=lambda r:f'rgb-h-s{r["scenario"]}-w{r["weights"]}-r{r["roi"]}-c{r["channels"]}-p{r["previous"]}')
def test_actual_imagej_rgb_histogram_auto_plot_and_whole_plane_mapping(row):
    pixels=rgb_auto_input(row['scenario']);reads=ij.DatasetReadObserver();doc=ij.Document(ij.Dataset(pixels,axes='YXS').observe_reads(reads));doc.select(rgb_auto_selection(row['roi']))
    weights=RGB_AUTO_FIXTURE['weights'][row['weights']]
    plot_options=rgb_histogram_options(weights,row['channels']);auto_options=rgb_histogram_options(weights)
    area=row['roi'] not in (0,4)
    auto_request=ij.TargetRequest(roi=ij.RoiPolicy.ACTIVE_SELECTION if area else ij.RoiPolicy.WHOLE_PLANE)
    plot_request=ij.TargetRequest(roi=ij.RoiPolicy.ACTIVE_SELECTION if area and row['channels'] not in (1,2,4) else ij.RoiPolicy.WHOLE_PLANE)
    target=ij.resolve_target(doc.snapshot(),auto_request);estimate=ij.estimate_rgb_histogram(target,auto_options)
    assert estimate.plane_pixels==2048 and estimate.plane_count==1 and estimate.plane_bytes==2048*3*8 and reads.counts().plane_reads==0
    before=doc.snapshot();plot=ij.accumulate_rgb_histogram(ij.resolve_target(before,plot_request),plot_options)
    auto=ij.accumulate_rgb_histogram(target,auto_options)
    assert histogram_hash(plot)==row['plot_before_sha256'] and histogram_hash(auto)==row['auto_histogram_sha256']
    decision=ij.rgb_auto_contrast_from_histogram(auto,row['previous'])
    assert decision.range==row['range'] and decision.cycle.previous==row['previous'] and decision.cycle.next==row['next']
    assert decision.cycle.used==(5000 if row['previous']<10 else row['previous']//2)
    doc.commit(ij.prepare_rgb_contrast_apply(ij.resolve_target(before,ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE)),rgb_options(*decision.range,row['channels'])))
    applied=doc.snapshot().dataset.numpy('YXS');assert hashlib.sha256(applied.tobytes()).hexdigest()==row['pixels_sha256']
    plot=ij.accumulate_rgb_histogram(ij.resolve_target(doc.snapshot(),plot_request),plot_options)
    assert histogram_hash(plot)==row['plot_after_sha256'] and before.dataset.numpy('YXS').tobytes()==pixels.tobytes()


@pytest.mark.parametrize('bad',[[True,0,0],[1,False,0],[1,0,float('nan')],[1,0,float('inf')],[1,0],[1,0,0,0],['red',0,0]])
def test_rgb_histogram_weight_setter_is_strict_and_atomic(bad):
    options=ij.RgbHistogramOptions();before=options.weights
    with pytest.raises((ValueError,TypeError,OverflowError)):options.weights=bad
    assert options.weights==before


@pytest.mark.parametrize('failure',['memory','pixels','pre_cancel','callback','outside_roi_invalid_source'])
def test_rgb_histogram_admission_callbacks_and_invalid_samples_preserve_source(failure):
    reads=ij.DatasetReadObserver();pixels=rgb_input();doc=ij.Document(ij.Dataset(pixels,axes='YXS').observe_reads(reads));before=doc.snapshot()
    limits=ij.DatasetHistogramLimits();token=ij.CancellationToken()
    if failure=='memory':limits.max_working_bytes=1
    if failure=='pixels':limits.max_pixels=1
    if failure=='pre_cancel':token.cancel()
    options=ij.RgbHistogramOptions()
    if failure=='outside_roi_invalid_source':
        options.weights=[-1,0,0]
    def checkpoint():
        if failure=='callback':raise RuntimeError('controlled RGB histogram callback failure')
    with pytest.raises((ValueError,RuntimeError,ij.OperationCancelled)):
        ij.accumulate_rgb_histogram(ij.resolve_target(before),options,limits,cancellation=token,checkpoint=checkpoint)
    if failure in ['memory','pixels','pre_cancel','callback']:assert reads.counts().plane_reads==0
    assert before.shares_data(doc.snapshot()) and doc.undo_steps==0


def test_rgb_histogram_custom_indices_outside_roi_do_not_reject_valid_members():
    pixels=np.array([[[0,20,30],[255,0,0]]],np.uint8);doc=ij.Document(ij.Dataset(pixels,axes='YXS'));doc.select(ij.Selection.from_mask(np.array([[1,0]],np.uint8)))
    options=ij.RgbHistogramOptions();options.weights=[-1,1,1]
    result=ij.accumulate_rgb_histogram(ij.resolve_target(doc.snapshot()),options)
    assert result.histogram["counts"][50]==1 and result.finite_count==1 and result.plane_minimum==-254


def test_rgb_histogram_freezes_reentrant_options_target_and_limits():
    pixels=rgb_input();doc=ij.Document(ij.Dataset(pixels,axes='YXS'));options=rgb_histogram_options([1,0,0]);limits=ij.DatasetHistogramLimits()
    def checkpoint():options.weights=[0,0,0];limits.max_pixels=1
    result=ij.accumulate_rgb_histogram(ij.resolve_target(doc.snapshot()),options,limits,checkpoint=checkpoint)
    np.testing.assert_array_equal(result.histogram["counts"],[2]*256)
    assert result.rgb_options.weights==[1.,0.,0.]


@pytest.mark.parametrize('cancel',[False,True])
def test_rgb_histogram_worker_retains_typed_result_reservation(cancel):
    import gc,time
    doc=ij.Document(ij.Dataset(rgb_input(),axes='YXS'));scheduler=ij.JobScheduler();target=ij.resolve_target(doc.snapshot())
    estimate=ij.estimate_rgb_histogram_job(target);job=ij.submit_rgb_histogram(scheduler,target)
    assert job.wait(5000);value=job.rgb_result();reservation=job.info.resource_bytes
    assert value.rgb_options.component==ij.RgbHistogramComponent.INTENSITY and reservation==estimate.reservation_bytes
    # Python getters intentionally copy out of native reservations.
    if cancel:job.cancel()
    else:job.complete()
    scheduler.shutdown();assert sum(value.histogram["counts"])==512
    assert scheduler.usage.reserved_bytes==0


@pytest.mark.parametrize('bad',[-1,10,11,True,1.5,2**64])
def test_rgb_auto_divisor_rejects_noncanonical_values(bad):
    doc=ij.Document(ij.Dataset(rgb_input(),axes='YXS'));value=ij.accumulate_rgb_histogram(ij.resolve_target(doc.snapshot()))
    with pytest.raises((ValueError,TypeError,OverflowError)):ij.rgb_auto_contrast_from_histogram(value,bad)


STACK_FIXTURE=json.loads((Path(__file__).parents[1]/"fixtures/rgb-stack-apply-imagej154r.json").read_text())

def rgb_stack_input():
    t,c,z,y,x=np.indices((3,3,3,8,256))
    return np.stack([(x+11*c+23*z+41*t)&255,(73*x+19*y+17*c+31*z+47*t)&255,(255-x+53*c+59*z+61*t)&255],axis=-1).astype(np.uint8)

STACK_PIXELS=rgb_stack_input();STACK_PIXELS.flags.writeable=False

def rgb_stack_selection(roi):
    if roi in [0,1,2,3]:return rgb_auto_selection(roi)
    points=[(20.25,1.25),(90.75,3.5),(35.5,5.75)]
    if roi==4:return ij.Selection("line",[(20.25,1.25),(90.75,5.75)])
    if roi==5:return ij.Selection("multipoint",[(20.25,1.25),(90.75,5.75)],width=9)
    if roi in [6,7]:return ij.Selection("polyline" if roi==6 else "angle",points)
    return ij.Selection("ellipse",[(17,0),(236,8)])

def rgb_stack_steps(row):
    result=[]
    if row["prefix"]:
        step=ij.RgbContrastStackStep();step.options=rgb_options(*STACK_FIXTURE["prefix_range"],7);result.append(step)
    step=ij.RgbContrastStackStep();step.options=rgb_options(*STACK_FIXTURE["ranges"][row["range_index"]],row["channels"]);step.whole_plane=row["whole_plane"];result.append(step)
    return result

def rgb_stack_owner(row,observer=None):
    dataset=ij.Dataset(STACK_PIXELS,axes="TCZYXS")
    if observer is not None:dataset=dataset.observe_reads(observer)
    owner=ij.Document(dataset);owner.select(rgb_stack_selection(row["roi"]))
    t,c,z=STACK_FIXTURE["anchors"][row["anchor"]];view=owner.snapshot().display;view.t,view.c,view.crosshair=t,c,[0,0,z];view.properties={"display_range":[12.,220.]};owner.set_display(view)
    return owner

@pytest.mark.parametrize("row",STACK_FIXTURE["rows"],ids=lambda r:f'stack-r{r["range_index"]}-c{r["channels"]}-roi{r["roi"]}-a{r["anchor"]}-p{int(r["prefix"])}-w{int(r["whole_plane"])}')
def test_actual_imagej_entire_rgb_stack_preserves_active_preview_and_narrows_other_planes(row):
    reads=ij.DatasetReadObserver();owner=rgb_stack_owner(row,reads);before=owner.snapshot();steps=rgb_stack_steps(row)
    estimate=ij.estimate_rgb_contrast_stack(before,steps);assert estimate.planes==27 and estimate.active_stages==len(steps) and estimate.pixels==2048*(26+len(steps)) and reads.counts().plane_reads==0
    edit=ij.prepare_rgb_contrast_stack(before,steps);expected_reads=0 if row["roi"]==3 and not row["whole_plane"] else 1 if row["roi"]==3 else 27
    assert reads.counts().plane_reads==expected_reads and before.shares_data(owner.snapshot()) and owner.undo_steps==0
    preview=edit.preview();pixels=preview.dataset.numpy("TCZYXS");assert hashlib.sha256(pixels.tobytes()).hexdigest()==row["stack_sha256"]
    t,c,z=STACK_FIXTURE["anchors"][row["anchor"]];assert hashlib.sha256(pixels[t,c,z].tobytes()).hexdigest()==row["active_sha256"]
    assert preview.selection==before.selection
    owner.commit(edit);assert owner.undo_steps==int(expected_reads!=0)
    if expected_reads:
        assert owner.snapshot().display.properties=={"display_range":[0.,255.]};owner.undo();assert owner.snapshot().shares_data(before)
        owner.redo();np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYXS"),pixels)
    else:assert owner.snapshot().shares_data(before) and owner.snapshot().display.properties==before.display.properties

@pytest.mark.parametrize("failure",["empty","stages","memory","pixels","metadata","pre_cancel","callback","late_callback"])
def test_rgb_stack_failures_leave_owner_and_history_unchanged(failure):
    row=STACK_FIXTURE["rows"][0];reads=ij.DatasetReadObserver();owner=rgb_stack_owner(row,reads);before=owner.snapshot();steps=rgb_stack_steps(row);limits=ij.ContrastApplyCommandLimits();token=ij.CancellationToken()
    if failure=="empty":steps=[]
    elif failure=="stages":steps*=65
    elif failure=="memory":limits.processing.max_working_bytes=1
    elif failure=="pixels":limits.processing.max_pixels=1
    elif failure=="metadata":limits.max_record_bytes=1
    elif failure=="pre_cancel":token.cancel()
    def checkpoint():
        if failure=="callback" or failure=="late_callback" and reads.counts().plane_reads>1:raise RuntimeError("controlled stack callback failure")
    with pytest.raises((ValueError,RuntimeError,ij.OperationCancelled)):ij.prepare_rgb_contrast_stack(before,steps,limits,cancellation=token,checkpoint=checkpoint)
    assert before.shares_data(owner.snapshot()) and owner.undo_steps==0
    if failure!="late_callback":assert reads.counts().plane_reads==0
    else:assert reads.counts().plane_reads>1

@pytest.mark.parametrize("change",["selection","display","metadata","pixels"])
def test_rgb_stack_stale_transaction_never_overwrites_newer_owner(change):
    owner=rgb_stack_owner(STACK_FIXTURE["rows"][0]);source=owner.snapshot();edit=ij.prepare_rgb_contrast_stack(source,rgb_stack_steps(STACK_FIXTURE["rows"][0]))
    if change=="selection":owner.select(ij.Selection("rectangle",[(0,0),(5,5)]))
    elif change=="display":
        view=source.display;view.zoom=2;owner.set_display(view)
    elif change=="metadata":
        newer=owner.begin_edit();newer.set_title("newer title");owner.commit(newer)
    else:owner.commit(ij.prepare_rgb_contrast_apply(ij.resolve_target(source),rgb_options()))
    stable=owner.snapshot();steps=owner.undo_steps
    with pytest.raises(ij.RevisionConflict):owner.commit(edit)
    assert stable.shares_data(owner.snapshot()) and owner.undo_steps==steps

def test_rgb_stack_freezes_steps_and_limits_before_callbacks():
    row=next(r for r in STACK_FIXTURE["rows"] if r["range_index"]==0 and r["channels"]==4 and r["roi"]==5 and r["anchor"]==1 and r["prefix"] and r["whole_plane"])
    owner=rgb_stack_owner(row);steps=rgb_stack_steps(row);limits=ij.ContrastApplyCommandLimits()
    def checkpoint():steps.clear();limits.processing.max_pixels=1
    edit=ij.prepare_rgb_contrast_stack(owner.snapshot(),steps,limits,checkpoint=checkpoint)
    assert hashlib.sha256(edit.preview().dataset.numpy("TCZYXS").tobytes()).hexdigest()==row["stack_sha256"]

@pytest.mark.parametrize("bad",[1,0,"true"])
def test_rgb_stack_whole_plane_field_requires_boolean(bad):
    step=ij.RgbContrastStackStep()
    with pytest.raises(TypeError):step.whole_plane=bad
    assert not step.whole_plane


def test_rgb_stack_empty_point_geometry_refuses_before_reads():
    reads=ij.DatasetReadObserver();owner=rgb_stack_owner(STACK_FIXTURE["rows"][0],reads);owner.select(ij.Selection("point"));step=ij.RgbContrastStackStep()
    with pytest.raises(ValueError):ij.prepare_rgb_contrast_stack(owner.snapshot(),[step])
    assert reads.counts().plane_reads==0


def test_rgb_stack_single_plane_keeps_fractional_active_mapping():
    pixels=np.array([[[20,30,40],[30,40,50]]],np.uint8);owner=ij.Document(ij.Dataset(pixels,axes="YXS"));step=ij.RgbContrastStackStep();step.options=rgb_options(10.9,40.1,4)
    edit=ij.prepare_rgb_contrast_stack(owner.snapshot(),[step]);assert edit.preview().dataset.plane()[0,0,0]==87
    expected=ij.apply_rgb_contrast_imagej(pixels,step.options);np.testing.assert_array_equal(edit.preview().dataset.numpy("YXS"),expected)

@pytest.mark.parametrize("pixels",[np.zeros((2,4),np.uint8),np.zeros((2,4,3),np.uint16),np.zeros((2,4,4),np.uint8)])
def test_rgb_stack_unsupported_storage_refuses_before_reads(pixels):
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(pixels,axes="YX" if pixels.ndim==2 else "YXS").observe_reads(reads))
    with pytest.raises(ValueError):ij.prepare_rgb_contrast_stack(owner.snapshot(),[ij.RgbContrastStackStep()])
    assert reads.counts().plane_reads==0


def test_rgb_stack_empty_membership_reports_terminal_progress_without_reads():
    row=next(r for r in STACK_FIXTURE["rows"] if r["roi"]==3 and r["anchor"]==2 and not r["whole_plane"])
    reads=ij.DatasetReadObserver();owner=rgb_stack_owner(row,reads);events=[]
    edit=ij.prepare_rgb_contrast_stack(owner.snapshot(),rgb_stack_steps(row),progress=lambda done,total:events.append((done,total)))
    assert events and events[-1]==(27,27) and reads.counts().plane_reads==0
    stable=owner.snapshot();owner.commit(edit);assert owner.snapshot().shares_data(stable) and owner.undo_steps==0


def test_rgb_stack_metadata_admission_precedes_callbacks_and_reads():
    reads=ij.DatasetReadObserver();row=STACK_FIXTURE["rows"][0];owner=rgb_stack_owner(row,reads)
    view=owner.snapshot().display;view.properties={"large":"x"*4096};owner.set_display(view)
    limits=ij.ContrastApplyCommandLimits();limits.max_record_bytes=1024;events=[]
    with pytest.raises(ValueError):ij.prepare_rgb_contrast_stack(owner.snapshot(),rgb_stack_steps(row),limits,checkpoint=lambda:events.append(True))
    assert not events and reads.counts().plane_reads==0 and owner.undo_steps==0
