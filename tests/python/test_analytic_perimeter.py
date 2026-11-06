"""Pinned Rectangle/Oval/Analyzer perimeter through captured Measure and hosts.

Area membership remains the native raster policy. Huge original oval masks were
not observed; their direct getLength results are explicitly separate evidence.
"""
import csv
import json
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest

REFERENCE = json.loads((Path(__file__).parents[1] / "fixtures/analytic-perimeter-imagej154r.json").read_bytes())


def owner_for(case):
    reads = ij.DatasetReadObserver()
    spatial = ij.SpatialCalibration(spacing=[*case["spacing"], 1], unit="um")
    owner = ij.Document(ij.Dataset(np.zeros((40, 40), dtype=np.uint8), axes="YX",
        calibration=spatial).observe_reads(reads))
    if case["kind"] != "whole_image":
        x, y, width, height = case["geometry_bounds"]
        owner.select(ij.Selection(case["kind"], [[x, y], [x + width, y + height]]))
    app = ij.Application()
    app.adopt(owner)
    return app, owner, reads


@pytest.mark.parametrize("case", REFERENCE["cases"], ids=lambda case: case["id"])
def test_original_analytic_perimeter_and_captured_units(case):
    if not case["positive_extent"]:
        # Java's zero-bound constructor clamps its integer fields; native boxes
        # deliberately require positive spans. This is a refusal, not parity.
        with pytest.raises(ValueError):
            app, owner, reads = owner_for(case)
            options = ij.TargetAnalysisOptions()
            options.measurement = ij.TargetMeasurementMode.SELECTION
            ij.resolve_target_analysis(app.resolve_target(), options)
        return
    app, owner, reads = owner_for(case)
    options = ij.TargetAnalysisOptions()
    options.measurement = ij.TargetMeasurementMode.SELECTION
    before = reads.counts().plane_reads
    command = ij.resolve_target_analysis(app.resolve_target(), options)
    assert reads.counts().plane_reads == before
    assert command.record["version"] == 10
    whole = case["kind"] == "whole_image"
    captured = command.record["options"]["rows"][0]
    assert captured["analytic_type"] == case["kind"]
    assert captured["analytic_extent"] == ([40, 40] if whole else case["integer_bounds"][2:])
    # Detached command execution retains its geometry and calibration.
    owner.select(ij.Selection("rectangle", [[0, 0], [1, 1]]))
    snapshot, undo = owner.snapshot(), owner.undo_steps
    result = ij.execute_target_analysis(command)
    row = result.planes[0]
    expected = case["rows"][0]["Perim."] if whole else case["length"]
    assert row.perimeter == pytest.approx(expected, rel=1e-12, abs=1e-11)
    assert row.perimeter_in_pixels is whole
    with pytest.raises(AttributeError):
        row.perimeter_in_pixels = False
    assert row.length is None
    assert reads.counts().plane_reads == before + 1
    assert owner.snapshot().shares_data(snapshot) and owner.undo_steps == undo


@pytest.mark.parametrize("kind", ["rectangle", "ellipse"])
def test_reversed_corners_and_frozen_runtime_admission(kind):
    case = next(c for c in REFERENCE["cases"] if c["id"] == f"shape0-{kind}-double-2.0-0.5")
    app, owner, reads = owner_for(case)
    owner.select(ij.Selection(kind, [[13, 11], [3, 3]]))
    options = ij.TargetAnalysisOptions()
    options.measurement = ij.TargetMeasurementMode.SELECTION
    command = ij.resolve_target_analysis(app.resolve_target(), options)
    before = reads.counts().plane_reads
    calls = []
    with pytest.raises(ValueError):
        ij.execute_target_analysis(command, runtime_max_working_bytes=command.estimate.working_bytes-1,
            checkpoint=lambda: calls.append(1))
    assert not calls and reads.counts().plane_reads == before
    row = ij.execute_target_analysis(command).planes[0]
    assert row.perimeter == pytest.approx(case["length"])
    assert not row.perimeter_in_pixels


@pytest.mark.parametrize("profile,expected", [(22,48), (23,42.01120851869869),
    (24,53), (25,46.295306614080765), (26,4.288751483104749), (27,160)])
def test_analytic_measure_physical_controls_csv_and_workspace(tmp_path, profile, expected):
    from imagejxx import _desktop as desktop
    editor = desktop.Editor(str(tmp_path / "preferences.json"))
    directory = tmp_path / "analytic-measure"
    report = json.loads(editor._subtype_measurement_ui_case(str(directory), profile))
    unit = "pixel" if profile == 27 else "um"
    assert report["rows"][0]["Perim."] == pytest.approx(expected, rel=1e-12)
    assert report["rows"][0]["Perim. unit"] == unit
    assert report["cancel_preserved"] and report["workspace_preserved"]
    assert report["source_unchanged_by_measure"] and report["additional_measure_reads"] == 1
    assert not report["fitted"] and not report["lifecycle"]
    if profile == 22:
        assert report["stale_constructor_checked"]
    if profile == 27:
        assert report["selection"] is None
    with (directory / "measure.csv").open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    assert len(rows) == 1 and rows[0]["Perim. unit"] == unit
    assert float(rows[0]["Perim."]) == pytest.approx(expected, abs=1e-6)
