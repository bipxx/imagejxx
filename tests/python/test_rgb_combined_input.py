"""Independent reader of bytes and controls from actual combined ImGui input."""
import hashlib
import json
import os
from pathlib import Path
import pytest

REFERENCE = json.loads((Path(__file__).parents[1]/"fixtures/rgb-contrast-combined-imagej154r.json").read_bytes())
ROWS = [row for row in REFERENCE["cases"] if row["roi"] == 8]


def identity(row):
    return tuple(row[k] for k in ("axis", "origin", "first_channels", "second_channels", "automatic"))


@pytest.fixture(scope="module")
def observations():
    path = Path(os.environ["IMAGEJXX_RGB_COMBINED_INPUT_REPORT"]).resolve(strict=True)
    assert 0 < path.stat().st_size <= 64*1024*1024
    report = json.loads(path.read_bytes())
    assert report["state"] == "passed" and report["cases"] == len(report["results"]) == 504
    rows = {identity(row): row for row in report["results"]}
    assert len(rows) == 504 and set(rows) == {identity(row) for row in ROWS}
    return path.parent, rows


@pytest.mark.parametrize("reference", ROWS, ids=lambda r:"-".join(map(str, identity(r))))
def test_real_canvas_navigation_range_reset_auto_and_apply_match_imagej(reference, observations):
    root, rows = observations
    row = rows[identity(reference)]
    name = row["pixels_file"]
    assert Path(name).name == name and name.endswith("-applied.bin")
    path = root/name
    assert not path.is_symlink() and not getattr(path, "is_junction", lambda: False)()
    assert path.resolve(strict=True).is_relative_to(root)
    raw = path.read_bytes()
    assert len(raw) == 2*2*2*8*256*3
    assert hashlib.sha256(raw).hexdigest() == row["actual_stack_sha256"] == reference["applied_stack_sha256"]
    assert row["range"] == [reference["minimum"], reference["maximum"]]
    assert row["auto_divisor"] == reference["auto_threshold"]
    assert row["current_channels"] == reference["current_channels"]
    assert row["physical_text_fields"] == (10 if reference["automatic"] else 12)
    selectors = [v["selector"] for v in row["controls"]]
    for name in ("roi.create_roi", "tools.tool.ellipse", "tools.tool.rectangle",
                 "menu.action.select_all", "menu.action.select_none", "menu.action.restore_selection",
                 "contrast.minimum", "contrast.maximum", "contrast.reset", "contrast.apply",
                 ("navigation.time", "navigation.channel", "image.slice")[reference["axis"]]):
        assert name in selectors
    assert selectors.count("contrast.auto") == reference["automatic"]
    for components in (reference["first_channels"], reference["second_channels"], reference["current_channels"]):
        assert f"contrast.rgb_components_{components}" in selectors
    record = row["record"]
    assert record["schema"] == "imagejxx.rgb-contrast-session-command" and record["entire_stack"]
    assert len(record["visits"]) == 3 and len(record["visits"][0]["stages"]) == 3
    first = record["visits"][0]["stages"]
    assert first[0]["selection"]["kind"] == "ellipse" and first[1]["selection"] is None
    assert record["visits"][1]["stages"][0]["selection"]["kind"] == "rectangle"
    assert record["visits"][-1]["stages"][-1]["whole_plane"] == bool(reference["automatic"])
