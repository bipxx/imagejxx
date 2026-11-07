"""Independent original Analyzer compositions through the maintained editor job."""
import csv
import json
from pathlib import Path

import numpy as np
import pytest
import imagejxx as ij
from imagejxx import _desktop

REFERENCE = json.loads((Path(__file__).parents[1]/"fixtures/mixed-measurements-imagej154r.json").read_bytes())
COLUMNS = ["Label", "Area", "mean", "X", "Y", "XM", "YM", "BX", "BY", "Width", "Height",
           "%Area", "Skew", "Kurt", "Length", "T", "C", "Z"]
POINTS = [
    ("rectangle", [[1, 1], [7, 5]]),
    ("line", [[1.25, 1.5], [5.5, 3.25]]),
    ("polyline", [[1, 1], [5, 3], [6, 2]]),
    ("angle", [[1, 1], [5, 3], [6, 2]]),
    ("point", [[2.5, 1.25]]),
    ("multipoint", [[1.25, 1.5], [5.5, 3.25]]),
]


def selection(index):
    kind, points = POINTS[index]
    return ij.Selection(kind, points)


def document(redirect=False):
    y, x = np.indices((6, 8))
    pixels = np.empty((2, 2, 2, 6, 8), np.uint8)
    for t in range(2):
        for c in range(2):
            for z in range(2):
                pixels[t, c, z] = x + 2*y + 1 + t*4 + z*2 + c + (20 if redirect else 0)
    spatial = ij.SpatialCalibration(spacing=[.75, 2, 1] if redirect else [2, .5, 1],
                                    origin=[0, -8.5, 0] if redirect else [0, 0, 0], unit="um")
    owner = ij.Document(ij.Dataset(pixels, axes="TCZYX", calibration=spatial))
    edit = owner.begin_edit()
    edit.set_title("sample,title" if redirect else "source,title")
    edit.set_property("plane_labels", [f"plane{t*4+z*2+c}.tif\nignored"
                                      for t in range(2) for c in range(2) for z in range(2)])
    edit.set_rois([ij.ROI(f"stored-{index}", selection(index)) for index in range(6)])
    owner.commit(edit)
    view = owner.snapshot().display
    view.t = view.c = 1
    view.crosshair = [4, 3, 1]
    view = ij.set_manual_threshold_state(view, owner.snapshot().dataset,
                                        ij.ManualThresholdRange(31, 37) if redirect else ij.ManualThresholdRange(11, 17),
                                        ij.PlanePosition(1, 1, 1))
    owner.set_display(view)
    owner.select(selection(0))
    return owner


def same(value, expected):
    if isinstance(expected, str):
        assert str(value).lower() == expected.lower()
    else:
        assert value == pytest.approx(expected, rel=2e-10, abs=2e-10)


def columns(case):
    # Keep explicitly unqualified non-area RECT/COM variants with T06/T04.
    area_only = {"XM", "YM", "BX", "BY", "Width", "Height"}
    return [key for key in COLUMNS if not case["manager"] or key not in area_only]


@pytest.mark.parametrize("case", REFERENCE["cases"], ids=lambda case: case["id"])
def test_composed_original_rows_overlay_and_reopened_outputs(tmp_path, case):
    editor = _desktop.Editor(str(tmp_path/"preferences.json"))
    source, sample = document(), document(True)
    editor.add_native_document(source)
    editor.add_native_document(sample)
    editor.active_index = 0
    before, sample_before = source.snapshot(), sample.snapshot()
    def settings(**values):
        editor.update_settings(json.dumps(values), False)
    def action(name, **values):
        editor.action(name, json.dumps(values))
        editor.wait()
        assert not editor.error
    settings(measurements=["Area", "mean"], measurement_precision=3,
             measurement_invert_y=False, measurement_nan_empty_cells=False,
             measurement_add_to_overlay=False, measurement_scientific=False,
             measurement_limit_to_threshold=False)
    for _ in range(case["seed"]):
        action("measure")
    seed_rows = json.loads(editor.results)
    if case["enabled"]:
        settings(measurements=columns(case), measurement_invert_y=True, measurement_nan_empty_cells=True,
                 measurement_add_to_overlay=True, measurement_scientific=True, measurement_limit_to_threshold=True)
    if case["redirect"]:
        action("measurement_redirect", document_id=sample.id)
    if case["whole"]:
        source.select(None)
    before_measure = source.snapshot()
    undo_before = source.undo_steps
    action("measure_rois" if case["manager"] else "measure")
    if case["seed"]:
        source.select(selection(0))
        action("measure")
    rows = json.loads(editor.results)
    assert len(rows) == case["row_count"]
    assert rows[:case["seed"]] == seed_rows
    if case["seed"]:
        assert json.loads(editor.results_table_state)["capacity"] == 200
    for expected in case["rows"] + case["seed_cells"]:
        row = rows[expected["index"]]
        if expected["label"] is not None:
            assert row["Label"] == expected["label"]
        for name, value in expected["raw"].items():
            if expected["index"] < case["seed"] and name in ["X", "Y"]:
                # The maintained native row always contains raw centroid fields,
                # even when hidden. Original Analyzer allocated X/Y only later.
                # Preserve those existing raw values; they are not empty cells.
                continue
            # Native rows expose captured positions on every row under T/C/Z;
            # original Analyzer writes Ch/Slice/Frame only for point rows.
            if name in ["Ch", "Slice", "Frame"]:
                if value != "nan":
                    same(row[{"Ch": "C", "Slice": "Z", "Frame": "T"}[name]], value)
                continue
            key = "mean" if name == "Mean" else name
            same(json.loads(editor.results_cell(expected["index"], key)), value)
        assert [row[key] for key in ["T", "C", "Z"]] == [2, 2, 2]
        if case["redirect"] and expected["index"] >= case["seed"]:
            assert row["Redirect document"] == sample.id
            assert [row[key] for key in ["Redirect T", "Redirect C", "Redirect Z"]] == [2, 2, 2]
    overlays = source.snapshot().overlays
    assert len(overlays) == len(case["overlays"])
    for actual, expected in zip(overlays, case["overlays"]):
        assert actual["name"] == expected["name"]
        assert actual["z"] == expected["z"] - 1 and actual["t"] == expected["t"] - 1
        assert actual.get("c") is None
    assert sample.snapshot().shares_data(sample_before)
    assert source.snapshot().dataset.shape == before.dataset.shape
    for t in range(2):
        for c in range(2):
            for z in range(2):
                assert np.array_equal(source.snapshot().dataset.plane("xy", z, t=t, c=c),
                                      before.dataset.plane("xy", z, t=t, c=c))
    assert source.undo_steps == undo_before + bool(overlays)
    if not overlays:
        assert source.snapshot().shares_data(before_measure)
    else:
        numerical = editor.results
        action("undo")
        assert not source.snapshot().overlays and editor.results == numerical
        action("redo")
        assert source.snapshot().overlays == overlays and editor.results == numerical
    action("save_results", path=str(tmp_path/"measured.csv"))
    action("save_workspace", path=str(tmp_path/"measured.ijx"))
    reopened = _desktop.Editor(str(tmp_path/"reopened-preferences.json"))
    reopened.open_legacy_file(str(tmp_path/"measured.ijx"))
    assert json.loads(reopened.results) == rows
    assert json.loads(reopened.results_table_state) == json.loads(editor.results_table_state)
    reopened.update_settings(json.dumps({"measurements": columns(case) if case["enabled"] else ["Area", "mean"],
                                        "measurement_scientific": case["enabled"], "measurement_precision": 3}), False)
    reopened.action("save_results", json.dumps({"path": str(tmp_path/"reopened.csv")}))
    assert (tmp_path/"reopened.csv").read_bytes() == (tmp_path/"measured.csv").read_bytes()
    with (tmp_path/"measured.csv").open(encoding="utf-8", newline="") as stream:
        exported = list(csv.DictReader(stream))
    for expected in case["rows"]:
        if case["enabled"]:
            # Compare actual computed floating columns to the independent original;
            # original per-column AUTO and integer-kind presentation stay T09.
            for field in ["Area", "Mean", "%Area", "Skew", "Kurt"]:
                key = "mean" if field == "Mean" else field
                value = rows[expected["index"]].get(key)
                if isinstance(value, float):
                    assert exported[expected["index"]][key] == expected["text"][field]


def test_actual_mixed_controls_outputs_and_lifecycle(tmp_path):
    editor = _desktop.Editor(str(tmp_path/"preferences.json"))
    report = json.loads(editor._mixed_measurement_ui_case(str(tmp_path/"actual")))
    assert_actual_mixed_report(report)


def assert_actual_mixed_report(report):
    """Same original/output assertions for either maintained real host receipt."""
    assert report["state"] == "observed"
    assert len(report["rows"]) == 110 and report["table_state"]["capacity"] == 200
    assert report["late_column_default"] == "NaN"
    assert [row["name"] for row in report["overlays"]] == ["100", "109", "110"]
    assert all(report[key] for key in ["source_preserved", "undo_redo", "cancel_preserved", "stale_pose_refused",
        "sampling_close_preserved", "source_close_preserved", "clear_preserved", "resource_guards", "workspace_preserved"])
    assert set(report["stale_settings"]) == {"measurement_invert_y", "measurement_limit_to_threshold",
                                           "measurement_nan_empty_cells", "measurement_add_to_overlay"}
    assert report["live_presentation"] and set(report["stale_columns"]) == {"Label", "%Area", "moments"}
    assert report["csv"] == report["copied_csv"]
    selected = report["selected_redirect"]
    assert report["redirect"]["Redirect document"] == selected["document_id"]
    assert any(control["name"] == f'measurement_redirect.{selected["document_id"]}'
               and control["control"] == selected["control"] for control in report["controls"])
    cases = {case["id"]: case for case in REFERENCE["cases"]}
    for name, reference in [("current", "combined-current"), ("whole", "combined-whole"),
                             ("redirect", "combined-redirect"), ("manager", "combined-manager")]:
        actual = report[name] if name == "manager" else [report[name]]
        assert len(actual) == len(cases[reference]["rows"])
        for row, expected in zip(actual, cases[reference]["rows"]):
            assert row["Label"] == expected["label"]
            for field, value in expected["raw"].items():
                if field in ["Ch", "Slice", "Frame"]:
                    if value != "nan":
                        same(row[{"Ch": "C", "Slice": "Z", "Frame": "T"}[field]], value)
                    continue
                key = "mean" if field == "Mean" else field
                if field in ["BX", "BY", "Width", "Height"]:
                    # This mixed UI selects no ordinary-area-only rectangle option.
                    assert key not in row
                    continue
                if key in row:
                    same(row[key], value)
                else:
                    assert value == "nan", (name, field)
    exported = list(csv.DictReader(report["csv"].splitlines()))
    cells = report["results_table"]["cells"]
    assert cells
    for cell in cells:
        assert cell["text"] == exported[cell["row"]][cell["name"]]
