"""Observation transport guards; synthetic processes are not product evidence."""
import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("path_filter_capture",ROOT/"scripts/ci_observations.py")
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)


def test_path_filter_catalog_and_reader_transport_are_complete(tmp_path):
    routes = [row for row in capture.cases() if row[0] == "path-filters"]
    assert len(routes) == len({row[1] for row in routes}) == 64
    assert [row[1] for row in routes] == list(capture.PATH_FILTER_CASES)
    assert all(row[2] == ["--path-filter-ui-case",row[1]] for row in routes)
    paths = json.loads(capture.reader_environment(tmp_path)["IMAGEJXX_PATH_FILTER_CPP_REPORTS"])
    assert set(paths) == set(capture.PATH_FILTER_CASES)
    assert all(Path(paths[case]) == tmp_path/"reports/path-filters"/case/"case.json" for case in paths)
    with pytest.raises(ValueError):
        capture.select_cases([["path-filters","unimplemented"]])


@pytest.mark.parametrize("fault", [None,"case","context","unfinished","claim","no-controls","frames","bytes"])
def test_path_filter_capture_refuses_wrong_or_unbounded_envelope(tmp_path,monkeypatch,fault):
    executable = tmp_path/"synthetic.exe"
    executable.write_bytes(b"synthetic, not executed")
    executable.chmod(0o700)
    def run(argv,**kwargs):
        directory = Path(argv[-1]);directory.mkdir()
        observed = {"contract":"p13i.path-filter.ui.001","case_id":"draw-gaussian",
                    "context":"imgui_cpp","state":"observed","error":"",
                    "acceptance_claim":False,"frames":[{}],"controls":[{}]}
        if fault == "case":observed["case_id"] = "other"
        if fault == "context":observed["context"] = "imgui_python"
        if fault == "unfinished":observed["state"] = "running"
        if fault == "claim":observed["acceptance_claim"] = True
        if fault == "no-controls":observed["controls"] = []
        if fault == "frames":observed["frames"] = [{}]*751
        if fault == "bytes":observed["oversized"] = "x"*(16*1024*1024)
        (directory/"case.json").write_text(json.dumps(observed))
        return SimpleNamespace(returncode=0)
    monkeypatch.setattr(capture.subprocess,"run",run)
    output = tmp_path/"capture"
    if fault:
        with pytest.raises(ValueError):
            capture.capture(output,executable,ROOT,selected_cases=[["path-filters","draw-gaussian"]])
    else:
        env = capture.capture(output,executable,ROOT,selected_cases=[["path-filters","draw-gaussian"]])
        copied = Path(json.loads(env["IMAGEJXX_PATH_FILTER_CPP_REPORTS"])["draw-gaussian"])
        assert copied.read_bytes() == (output/"private/path-filters/draw-gaussian/capture/case.json").read_bytes()
    receipt = json.loads((output/"receipt.json").read_bytes())
    assert receipt["state"] == ("failed" if fault else "observed")
    assert receipt["acceptance_claim"] is False
