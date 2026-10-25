"""Synthetic transport checks; these do not certify product/UI behavior."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
from types import SimpleNamespace

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('measurement_capture', ROOT/'scripts/ci_observations.py')
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)
CASES = ('measurement-names-edit-persist', 'measurement-names-reopen')


def test_catalog_environment_and_order(tmp_path):
    rows = [row for row in capture.cases() if row[0] == 'settings-measurements']
    assert [row[1] for row in rows] == list(CASES)
    assert all(row[2] == ['--settings-measurement-ui-case', row[1]] for row in rows)
    reports = json.loads(capture.reader_environment(tmp_path)['IMAGEJXX_SETTINGS_MEASUREMENT_CPP_REPORTS'])
    assert reports == {case: str(tmp_path/'reports/settings-measurements'/case/'case.json') for case in CASES}
    for selection in ([['settings-measurements', CASES[1]]],
                      [['settings-measurements', CASES[1]], ['settings-measurements', CASES[0]]],
                      [['settings-measurements', CASES[0]]] * 2,
                      [['settings-measurements', 'missing']]):
        with pytest.raises(ValueError):
            capture.select_cases(selection)


@pytest.mark.parametrize('fault', [None, 'failed-child', 'timeout', 'changed-product',
                                    'changed-reopen-file', 'schema', 'context', 'case',
                                    'claim', 'frames', 'states', 'parent', 'runtime'])
def test_capture_pair_identity_and_rejection(tmp_path, monkeypatch, fault):
    executable = tmp_path/'synthetic.exe'
    executable.write_bytes(b'synthetic transport only')
    executable.chmod(0o700)
    calls = []

    def run(argv, **kwargs):
        case, directory, preferences = argv[2], Path(argv[3]), Path(argv[4])
        calls.append((case, directory, preferences))
        assert kwargs['timeout'] <= 60
        assert directory.parent == preferences.parent and not directory.exists()
        directory.mkdir()
        if fault == 'failed-child':
            return SimpleNamespace(returncode=1)
        if fault == 'timeout':
            raise subprocess.TimeoutExpired(argv, kwargs['timeout'])
        if case == CASES[0]:
            assert not preferences.exists()
            preferences.write_bytes(b'{"format":"synthetic transport only"}')
        else:
            assert preferences.read_bytes() == b'{"format":"synthetic transport only"}'
            if fault == 'changed-reopen-file':
                preferences.write_bytes(b'{}')
        runtime = {'process_id': 12345, 'parent_process_id': os.getpid()}
        row = {'schema': 'cfg3.measurement-names.ui.001', 'case_id': case,
               'context': 'imgui_cpp', 'status': 'observed', 'error': '',
               'acceptance_claim': False, 'states': {'transport_only': {}},
               'frames': [{}], 'events': [{}], 'fixture': {'runtime': dict(runtime)}}
        if fault == 'schema': row['schema'] = 'cfg2.settings-selection.ui.001'
        if fault == 'context': row['context'] = 'imgui_python'
        if fault == 'case': row['case_id'] = 'stale'
        if fault == 'claim': row['acceptance_claim'] = True
        if fault == 'frames': row['frames'] *= 751
        if fault == 'states': row['states'] = {}
        if fault == 'parent':
            runtime['parent_process_id'] += 1
            row['fixture']['runtime'] = dict(runtime)
        if fault == 'runtime': runtime['process_id'] += 1
        (directory/'runtime.json').write_text(json.dumps(runtime), encoding='utf-8')
        (directory/'case.json').write_text(json.dumps(row), encoding='utf-8')
        if fault == 'changed-product': executable.write_bytes(b'changed')
        return SimpleNamespace(returncode=0)

    monkeypatch.setattr(capture.subprocess, 'run', run)
    output = tmp_path/'capture'
    selection = [['settings-measurements', case] for case in CASES]
    if fault:
        with pytest.raises((ValueError, subprocess.CalledProcessError, subprocess.TimeoutExpired)):
            capture.capture(output, executable, ROOT, selected_cases=selection)
    else:
        environment = capture.capture(output, executable, ROOT, selected_cases=selection)
        assert len(calls) == 2 and calls[0][1] != calls[1][1] and calls[0][2] == calls[1][2]
        reports = json.loads(environment['IMAGEJXX_SETTINGS_MEASUREMENT_CPP_REPORTS'])
        for case, directory, _ in calls:
            assert Path(reports[case]).read_bytes() == (directory/'case.json').read_bytes()
    receipt = json.loads((output/'receipt.json').read_bytes())
    assert receipt['state'] == ('failed' if fault else 'observed')
    assert receipt['acceptance_claim'] is False
