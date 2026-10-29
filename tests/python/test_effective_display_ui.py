"""Effective view geometry and cache invalidation in both shared ImGui hosts."""
import json
import os
from pathlib import Path

import numpy as np
import pytest


@pytest.mark.parametrize("context", ["imgui_cpp", "imgui_python"])
def test_effective_view_geometry_and_invalidation(context, tmp_path):
    if context == "imgui_cpp":
        report = json.loads(Path(os.environ["IMAGEJXX_EFFECTIVE_DISPLAY_CPP_REPORT"]).read_bytes())
    else:
        from imagejxx import _desktop
        editor = _desktop.Editor(str(tmp_path / "preferences.json"))
        report = json.loads(editor._effective_display_ui_case(str(tmp_path / "capture")))
    assert report["contract"] == "g034.effective-display.ui.001"
    assert report["context"] == context and report["state"] == "observed"
    assert not report["acceptance_claim"] and 0 < len(report["frames"]) <= 64
    for f in report["frames"]:
        assert f["events"] == f["rendered"]["input_frame"]["raw_inputs"]
        assert not f["rendered"]["window_error"]
    observations = report["observations"]
    expected = {
        "local": ([3, 4, 5], "px", 1),
        "global": ([.5, 1.25, 2], "um", 2),
        "steady": ([.5, 1.25, 2], "um", 2),
        "xyz": ([.5, 1.25, 2], "um", 2),
        "navigated": ([.5, 1.25, 2], "um", 2),
        "ignored": ([3, 4, 5], "px", 3),
        "ignored_global_change": ([3, 4, 5], "px", 3),
        "other_bypass": ([3, 4, 5], "px", 3),
        "unignored": ([.75, 1.25, 2], "um", 4),
        "cleared": ([3, 4, 5], "px", 5),
        "source_changed": ([6, 7, 8], "px", 6),
    }
    for name, (spacing, unit, builds) in expected.items():
        obs = observations[name]
        cal = obs["document_info"]["display_calibration"]
        assert cal["spacing"] == spacing and cal["unit"] == unit and cal["builds"] == builds
        local = obs["document_info"]["calibration"]
        assert [local[k] for k in ("pixel_width", "pixel_height", "pixel_depth")] == ([6, 7, 8] if name == "source_changed" else [3, 4, 5])
        info = obs["document_info"]
        assert info["undo_steps"] == (1 if name == "source_changed" else 0)
        for key in ("pixels", "selection"):
            assert info["revisions"][key] == observations["local"]["document_info"]["revisions"][key]
        if name != "source_changed":
            assert info["revisions"]["metadata"] == observations["local"]["document_info"]["revisions"]["metadata"]
            assert info["dirty"] == observations["local"]["document_info"]["dirty"]
        xyz = np.array(obs["view"]["crosshair"], dtype=float)
        world = np.array(cal["origin"]) + np.array(cal["direction"]).reshape(3, 3) @ (xyz * spacing)
        np.testing.assert_allclose(cal["world_xyz"], world, rtol=0, atol=0)
        if not obs["view"]["orthogonal"]:
            assert obs["ordinary_canvas"]["visible"]
            sx, sy = obs["ordinary_canvas"]["scale"]
            assert sy / sx == pytest.approx(spacing[1] / spacing[0], rel=2e-6)
        if obs["view"]["orthogonal"]:
            for plane, axes in zip(obs["orthogonal_planes"], [(0, 1), (0, 2), (1, 2)]):
                assert plane["spacing"] == [spacing[a] for a in axes]
                assert plane["scale"][1] / plane["scale"][0] == pytest.approx(spacing[axes[1]] / spacing[axes[0]], rel=2e-6)
    pixel_builds = [p["builds"] for p in observations["navigated"]["orthogonal_planes"]]
    for name in ("ignored", "ignored_global_change", "other_bypass", "unignored", "cleared"):
        assert [p["builds"] for p in observations[name]["orthogonal_planes"]] == pixel_builds
    assert observations["navigated"]["view"]["crosshair"] == [3, 2, 2]
    assert observations["xyz"]["view"]["orthogonal"]
    assert report["local_spatial"] == [6, 7, 8]
    assert report["workflow_count"] == 0 and report["undo"] == 1
    assert report["closed_document_count"] == 0
