"""Native clone/name/presentation and effective C/Z/T contracts, not GUI certification."""
import json
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij

REFERENCE = json.loads((Path(__file__).parents[1]/"fixtures/measurement-overlay-imagej154r.json").read_bytes())
# Original invalid foreign parent positions remain explicit reference refusals.
CASES = [c for c in REFERENCE["cases"] if "original_refusal" not in c]
LAYOUTS = [(1,1,1),(1,1,3),(2,2,2),(1,2,1),(2,1,1),(1,2,2),
           (2,1,2),(2,2,1),(1,2,1),(2,2,2),(1,1,3)]
KEY = "imagejxx.measurement_overlay"
ROW = "imagejxx.measurement_overlay_row"


def rgba(argb):
    value = int(argb, 16)
    return [(value >> 16) & 255, (value >> 8) & 255, value & 255, value >> 24]


def selection(case):
    kind = case["kind"]
    if kind == "none":
        return None
    points = {"rectangle": [[1,1],[7,5]], "ellipse": [[1,1],[7,5]],
              "polygon": [[1,1],[7,1],[3,5]], "line": [[1.25,1.5],[5.5,3.25]],
              "polyline": [[1,1],[5,3],[6,2]], "angle": [[1,1],[5,3],[6,2]],
              "point": [[2.5,1.25]], "multipoint": [[1.25,1.5],[5.5,3.25]]}
    attrs = {"opaque": 'keep,"value'}
    name = case["source_before"]["name"]
    if name is not None:
        attrs["imagej_roi_name"] = name
    if kind == "compound":
        commands = []
        x, y = case["source_before"]["bounds"][:2]
        for segment in case["source_before"]["path"]:
            commands.append(segment[0])
            commands.extend(value + (x if i % 2 == 0 else y) for i, value in enumerate(segment[1:]))
        value = ij.Selection.from_path(ij.ImageJCompositePath(commands))
        value = ij.SelectionEditor(value, [6, 8]).properties(1, attrs)
    else:
        value = ij.Selection(kind, points[kind], attributes=attrs)
    if case["naming"]:
        # Original setFillColor clears stroke; visual width remains separate
        # from this editor's already accepted processing-width contract.
        value = ij.with_roi_style(value, {"stroke_color": None,
            "fill_color": "#" + case["source_before"]["fill"], "stroke_width": case["source_before"]["width"]})
    if kind in ["point", "multipoint"]:
        style = ij.PointStyle()
        style.size = case["source_before"]["point_size"]
        style.marker = ij.PointMarker.CROSS if case["source_before"]["point_type"] == 1 else ij.PointMarker.HYBRID
        value = ij.with_point_style(value, style)
        t, c, z = LAYOUTS[case["layout"]]
        positions = case["source_before"]["point_positions"]
        if all(position <= t*c*z for position in positions):
            words = [(position << 8) | counter for position, counter in zip(positions, case["source_before"]["point_counters"])]
            value = ij.with_imagej_point_counters(value, words, [t,c,z,6,8,1])
    return value


def source(case):
    t, c, z = LAYOUTS[case["layout"]]
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.ones((t,c,z,6,8), np.uint8), axes="TCZYX").observe_reads(reads))
    value = selection(case)
    if value is not None:
        owner.select(value)
    old = []
    if case["overlay_before"] is not None:
        old = [{"kind": "roi", "name": "old", "selection": {"kind": "rectangle", "points": [[0,0],[1,1]], "width": 1}, "color": "#ff00ff", "opaque": [None, True, 17]}]
        prior = case["overlay_before"]
        edit = owner.begin_edit()
        edit.set_overlays(old)
        edit.set_property(KEY, {"version": 1, "draw_labels": prior["labels"], "draw_names": prior["names"],
            "draw_backgrounds": prior["backgrounds"], "label_color": rgba(prior["label_color"])})
        owner.commit(edit)
    if case["manager_masked"]:
        edit = owner.begin_edit()
        edit.set_rois([ij.ROI("manager", value)])
        owner.commit(edit)
    view = owner.snapshot().display
    view.t, view.c = t - 1, c - 1
    view.crosshair = [4, 3, 1 if z > 1 else 0]
    owner.set_display(view)
    return owner, reads


@pytest.mark.parametrize("case", CASES, ids=lambda c: c["id"])
def test_original_clone_name_flags_and_effective_native_position_fields(case):
    owner, reads = source(case)
    app = ij.Application()
    app.adopt(owner)
    before = owner.snapshot()
    old = before.overlays
    selected = before.selection
    selected_attrs = selected.attributes if selected else None
    options = ij.MeasurementOverlayOptions()
    options.enabled, options.composite, options.draw_labels = case["enabled"], case["layout"] in [8,9], case["naming"] != 1
    counter = case["rows_before"]
    will_add = case["enabled"] and case["kind"] != "none" and not case["manager_masked"]
    for _ in range(case["repeats"]):
        analysis_options = ij.TargetAnalysisOptions()
        analysis_options.measurement = ij.TargetMeasurementMode.ROI_MANAGER if case["manager_masked"] else ij.TargetMeasurementMode.SELECTION
        request = ij.roi_measurement_target_request(owner.snapshot()) if case["manager_masked"] else ij.TargetRequest()
        analysis = ij.resolve_target_analysis(app.resolve_target(request), analysis_options)
        plan = ij.resolve_measurement_overlay(analysis, counter, options)
        counter += analysis.estimate.row_count
        assert plan.will_add == will_add
        tx = ij.prepare_measurement_overlay(plan)
        if will_add:
            assert plan.result_count == counter and plan.working_bytes > 0
            assert tx is not None
            assert owner.snapshot().overlays == old
            staged = tx.preview()
            assert staged.overlays[:len(old)] == old
            owner.commit(tx)
            old = owner.snapshot().overlays
            with pytest.raises(ij.RevisionConflict):
                plan.validate_source(owner.snapshot())
        else:
            assert tx is None and plan.working_bytes == 0
        assert reads.counts().plane_reads == 0
    assert counter == case["rows_after"]
    after = owner.snapshot()
    assert (after.selection.attributes if after.selection else None) == selected_attrs
    if will_add:
        expected = case["overlay_after"]
        assert [row["name"] for row in after.overlays] == [row["name"] for row in expected["rows"]]
        new = after.overlays[-1]
        # Foreign single-plane Roi header positions are not the native active
        # selection model. Their original facts stay available for T08; this
        # assertion qualifies captured stacked pose and native visible axes.
        if case["layout"] != 0:
            original = expected["rows"][-1]
            # Parent legacy slot is zero for a hyperstack. PointRoi.getPosition
            # instead derives a multipoint's uniform per-point slot; check those
            # independent point positions below rather than conflating them.
            expected_parent = 0 if case["kind"] == "multipoint" and original["hyper"] else original["position"]
            assert new[ROW]["position"] == expected_parent
            assert new[ROW]["hyperstack"] == original["hyper"]
        else:
            assert new[ROW]["position"] == 0 and new[ROW]["hyperstack"] is False
        t, c, z = LAYOUTS[case["layout"]]
        hyper = new[ROW]["hyperstack"]
        assert new["c"] == (c-1 if t*c*z > 1 and not hyper and c > 1 else None)
        expected_z = (1 if z > 1 else 0) if hyper else (1 if z > 1 else None)
        assert new["z"] == expected_z
        assert new["t"] == (t-1 if t*c*z > 1 and (hyper or t > 1) else None)
        encoded = new["selection"]
        attrs = encoded.get("attributes", encoded)
        assert attrs["imagej_roi_name"] == new["name"]
        for key, value in selected_attrs.items():
            if key not in ["imagej_roi_name", "imagejxx.point_annotations"]:
                assert attrs[key] == value
        if case["kind"] in ["point", "multipoint"]:
            # Classic setPosition rebinds every point; C/Z/T setPosition leaves
            # point-specific positions intact. Source foreign out-of-grid slots
            # have no valid native counterpart and remain T08, not false parity.
            expected_points = expected["rows"][-1]["point_positions"]
            source_points = case["source_before"]["point_positions"]
            if case["layout"] != 0 and not hyper or all(position <= t*c*z for position in source_points):
                clone = ij.Selection(case["kind"], encoded["points"], attributes={key: value for key, value in attrs.items() if key not in ["kind", "points", "width"]})
                words = ij.imagej_point_counters(clone, [t,c,z,6,8,1])
                assert [word >> 8 for word in words] == expected_points
                assert [word & 255 for word in words] == expected["rows"][-1]["point_counters"]
        presentation = ij.measurement_overlay_presentation(after)
        assert presentation.draw_labels == expected["labels"]
        assert presentation.draw_names == expected["names"]
        assert presentation.draw_backgrounds == expected["backgrounds"]
        assert presentation.label_color == rgba(expected["label_color"])
    else:
        assert after.shares_data(before)


def test_original_refusals_are_not_counted_as_native_parity():
    refused = [c for c in REFERENCE["cases"] if "original_refusal" in c]
    assert len(refused) == 8 and all(c["layout"] == 8 and c["naming"] == 1 for c in refused)
    assert all(c["original_refusal"].endswith("n out of range: 3") for c in refused)
    assert len(REFERENCE["static_multi"]) == 22 and all(c["overlay_unchanged"] for c in REFERENCE["static_multi"])


def test_public_plan_ownership_limits_types_and_shared_default():
    case = next(c for c in CASES if c["kind"] == "rectangle" and c["layout"] == 0 and c["naming"] == 0 and c["enabled"] and not c["redirect"])
    owner, reads = source(case)
    app = ij.Application()
    app.adopt(owner)
    analysis_options = ij.TargetAnalysisOptions()
    analysis_options.measurement = ij.TargetMeasurementMode.SELECTION
    analysis = ij.resolve_target_analysis(app.resolve_target(), analysis_options)
    options = ij.MeasurementOverlayOptions()
    assert not options.enabled and ij.Settings()["measurement_add_to_overlay"] is False
    assert ij.prepare_measurement_overlay(ij.resolve_measurement_overlay(analysis, 0, options)) is None
    options.enabled = True
    plan = ij.resolve_measurement_overlay(analysis, 0, options)
    options.enabled = False
    exposed = plan.options
    exposed.enabled = False
    assert plan.options.enabled
    with pytest.raises(AttributeError):
        plan.will_add = False
    for count in [True, 1.0, "1"]:
        with pytest.raises(TypeError):
            ij.resolve_measurement_overlay(analysis, count, options)
    with pytest.raises(OverflowError):
        ij.resolve_measurement_overlay(analysis, -1, options)
    before = owner.snapshot()
    with pytest.raises(ValueError):
        ij.prepare_measurement_overlay(plan, plan.working_bytes - 1)
    options.enabled = True
    limits = ij.MeasurementOverlayLimits()
    limits.max_name_bytes = 1
    with pytest.raises(ValueError):
        ij.resolve_measurement_overlay(analysis, 99, options, limits)
    limits = ij.MeasurementOverlayLimits()
    limits.max_results_rows = 3
    with pytest.raises(ValueError):
        ij.resolve_measurement_overlay(analysis, 3, options, limits)
    assert owner.snapshot().shares_data(before) and reads.counts().plane_reads == 0



def test_actual_overlay_preferences_outputs_history_and_guards(tmp_path):
    from imagejxx import _desktop
    editor = _desktop.Editor(str(tmp_path/"preferences.json"))
    report = json.loads(editor._measurement_overlay_ui_case(str(tmp_path/"actual")))
    assert report["state"] == "observed"
    assert all(report[key] for key in ["workspace_preserved", "zoom_correct_labels", "undo_redo", "redirect_owner",
        "manager_noop", "no_selection_noop", "stale_setting_refused", "cancel_preserved",
        "sampling_close_preserved", "clear_preserved", "resource_guards",
        "source_close_preserved", "stale_pose_refused", "stale_composite_refused"])
    assert report["csv"] == report["copied_csv"]
    assert [layer["caption"]["text"] for layer in report["visible_layers"]] == ["old", "2"]
    assert len(report["overlays"]) == 3 and len(report["rows"]) == 4
    assert report["flattened"]["shape"] == [80, 96, 3]
