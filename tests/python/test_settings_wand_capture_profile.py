"""Synthetic capture faults test transport only, never product/UI correctness."""
import importlib.util
import json
from pathlib import Path
import subprocess
from types import SimpleNamespace

import pytest


ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("settings_wand_capture", ROOT / "scripts/ci_observations.py")
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)


def envelope(case):
    return {"schema": "cfg1.settings-wand.ui.001", "case_id": case, "context": "imgui_cpp",
            "status": "observed", "error": "", "acceptance_claim": False,
            "states": {name: {} for name in capture.SETTINGS_WAND_STATES[case]},
            "frames": [{}], "events": [{}]}


def test_wand_catalog_and_persistence_process_order(tmp_path):
    rows = [row for row in capture.cases() if row[0] == "settings-wand"]
    assert [r[1] for r in rows] == ["wand-consume", "wand-drafts", "wand-persist", "wand-reopen"]
    assert all(row[2] == ["--settings-wand-ui-case", row[1]] for row in rows)
    reports = json.loads(capture.reader_environment(tmp_path)["IMAGEJXX_SETTINGS_WAND_CPP_REPORTS"])
    assert set(reports) == set(capture.SETTINGS_WAND_CASES)
    for case, path in reports.items():
        assert Path(path) == tmp_path / "reports/settings-wand" / case / "case.json"
    for selection in (
        [["settings-wand", "missing"]],
        [["settings-wand", "wand-consume"]] * 2,
        [["settings-wand", "wand-reopen"]],
        [["settings-wand", "wand-reopen"], ["settings-wand", "wand-persist"]],
    ):
        with pytest.raises(ValueError):
            capture.select_cases(selection)


@pytest.mark.parametrize("fault", [None, "case", "context", "schema", "unfinished", "claim",
                                   "states", "frames", "events", "duplicate", "nonfinite", "bytes"])
def test_wand_envelope_rejects_incomplete_identity_and_bounds(fault):
    row = envelope("wand-consume")
    if fault == "case": row["case_id"] = "wand-drafts"
    if fault == "context": row["context"] = "imgui_python"
    if fault == "schema": row["schema"] = "unreviewed"
    if fault == "unfinished": row["status"] = "running"
    if fault == "claim": row["acceptance_claim"] = True
    if fault == "states": row["states"].pop("four_selected")
    if fault == "frames": row["frames"] *= 751
    if fault == "events": row["events"] = []
    if fault == "nonfinite": row["invalid"] = float("nan")
    if fault == "bytes": row["oversized"] = "x" * (16 * 1024**2)
    raw = json.dumps(row).encode()
    if fault == "duplicate": raw = raw[:-1] + b',"case_id":"wand-consume"}'
    if fault:
        with pytest.raises(ValueError):
            capture.settings_wand_observation(raw, "wand-consume")
    else:
        assert capture.settings_wand_observation(raw, "wand-consume") == row


@pytest.mark.parametrize("fault", [None, "failed-child", "timeout", "changed-product", "changed-reopen-file"])
def test_wand_capture_binds_shared_file_and_separate_child_runs(tmp_path, monkeypatch, fault):
    executable = tmp_path / "synthetic.exe"
    executable.write_bytes(b"synthetic transport only")
    executable.chmod(0o700)
    calls = []
    def run(argv, **kwargs):
        case, directory, preferences = argv[2], Path(argv[3]), Path(argv[4])
        calls.append((case, directory, preferences))
        assert directory.parent == preferences.parent
        assert kwargs["timeout"] > 0
        directory.mkdir()
        if fault == "failed-child":
            return SimpleNamespace(returncode=1)
        if fault == "timeout":
            raise subprocess.TimeoutExpired(argv, kwargs["timeout"])
        payload = json.loads(preferences.read_bytes())
        if case == "wand-persist":
            assert (payload["values"]["wand_tolerance"], payload["values"]["wand_connectivity"]) == (0, 4)
            payload["values"].update(wand_tolerance=1, wand_connectivity=8)
            preferences.write_text(json.dumps(payload), encoding="utf-8")
        else:
            assert (payload["values"]["wand_tolerance"], payload["values"]["wand_connectivity"]) == (1, 8)
            if fault == "changed-reopen-file": preferences.write_text("{}", encoding="utf-8")
        (directory / "case.json").write_text(json.dumps(envelope(case)), encoding="utf-8")
        if fault == "changed-product": executable.write_bytes(b"different product")
        return SimpleNamespace(returncode=0)
    monkeypatch.setattr(capture.subprocess, "run", run)
    output = tmp_path / "capture"
    selection = [["settings-wand", "wand-persist"], ["settings-wand", "wand-reopen"]]
    if fault:
        with pytest.raises((ValueError, subprocess.CalledProcessError, subprocess.TimeoutExpired)):
            capture.capture(output, executable, ROOT, selected_cases=selection)
    else:
        env = capture.capture(output, executable, ROOT, selected_cases=selection)
        assert calls[0][2] == calls[1][2] and calls[0][1] != calls[1][1]
        for case in ("wand-persist", "wand-reopen"):
            copied = Path(json.loads(env["IMAGEJXX_SETTINGS_WAND_CPP_REPORTS"])[case])
            assert copied.read_bytes() == (next(path for name, path, _ in calls if name == case) / "case.json").read_bytes()
    receipt = json.loads((output / "receipt.json").read_bytes())
    assert receipt["state"] == ("failed" if fault else "observed")
    assert receipt["acceptance_claim"] is False
