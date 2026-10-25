"""Bounded fresh-process producer with a real persisted preferences pair."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

from settings_wand_ui_runner import terminate_process_tree

CASES = ('measurement-names-edit-persist', 'measurement-names-reopen')


def produce_python_reports(root):
    root = Path(root).resolve(strict=True)
    dependencies = sorted({str(Path(p).resolve()) for p in sys.path
                           if p and Path(p).is_dir() and Path(p).name == 'site-packages'})
    script = Path(__file__).with_name('settings_measurement_ui_child.py')
    receipt = {'state': 'running', 'acceptance_claim': False, 'runs': [], 'reports': {}}

    def save():
        (root/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf-8')
    save()
    try:
        for case in CASES:
            paired = True
            parent = root/('persisted-pair' if paired else case)
            parent.mkdir(exist_ok=paired)
            directory = parent/('persist_capture' if case == 'measurement-names-edit-persist' else
                                'reopen_capture' if case == 'measurement-names-reopen' else 'capture')
            preferences = parent/'preferences.json'
            if case == 'measurement-names-reopen' and not preferences.is_file():
                raise ValueError('Actual persisted preferences are unavailable')
            before = preferences.read_bytes() if preferences.exists() else None
            if case == 'measurement-names-edit-persist' and before is not None:
                raise ValueError('Persistence must start without an authored preference fixture')
            runtime = parent/(case+'.runtime.json')
            argv = [sys.executable, '-I', '-X', 'utf8', '-B', str(script), case,
                    str(directory), str(preferences), json.dumps(dependencies)]
            environment = os.environ.copy()
            for name in ('PYTHONHOME', 'PYTHONPATH', 'PYTHONUSERBASE', 'VIRTUAL_ENV', 'CONDA_PREFIX'):
                environment.pop(name, None)
            entry = {'case': case, 'argv': argv, 'state': 'running', 'started': time.time()}
            receipt['runs'].append(entry); save()
            with (parent/(case+'.log')).open('xb') as log:
                child = subprocess.Popen(argv, cwd=root, env=environment, stdout=log,
                                         stderr=subprocess.STDOUT, start_new_session=os.name != 'nt')
                entry['launcher_process_id'] = child.pid; save()
                try:
                    code = child.wait(timeout=60)
                except subprocess.TimeoutExpired:
                    entry['timeout_cleanup'] = terminate_process_tree(child, runtime)
                    entry.update(state='timed-out', exit_code=child.returncode); save()
                    raise
            entry.update(exit_code=code, ended=time.time(), launcher_reaped=True)
            if code:
                raise RuntimeError('Measurement settings UI producer failed: ' + case)
            identity = json.loads(runtime.read_bytes())
            if identity['process_id'] != child.pid and identity['parent_process_id'] != child.pid:
                raise ValueError('Cannot bind the launched Python interpreter')
            report = directory/'case.json'
            if not 0 < report.stat().st_size < 16*1024**2:
                raise ValueError('Measurement settings report exceeds bound')
            row = json.loads(report.read_bytes())
            observed = json.loads((directory/'runtime.json').read_bytes())
            if (row.get('schema') != 'cfg3.measurement-names.ui.001' or row.get('case_id') != case
                    or row.get('context') != 'imgui_python' or row.get('status') != 'observed'
                    or row.get('error') != '' or row.get('acceptance_claim') is not False
                    or observed['process_id'] != identity['process_id']
                    or observed['parent_process_id'] != identity['parent_process_id']
                    or row.get('fixture', {}).get('runtime') != observed):
                raise ValueError('Measurement settings producer identity mismatch')
            after = preferences.read_bytes() if preferences.exists() else None
            if case == 'measurement-names-reopen' and after != before:
                raise ValueError('Fresh consumer rewrote the persisted preference bytes')
            if case == 'measurement-names-edit-persist' and not after:
                raise ValueError('Actual UI did not save preferences')
            entry.update(state='observed', runtime=identity,
                         report_sha256=hashlib.sha256(report.read_bytes()).hexdigest())
            receipt['reports'][case] = str(report); save()
        receipt['state'] = 'observed'
        return receipt['reports']
    except BaseException as error:
        receipt.update(state='failed', error=str(error))
        if receipt['runs'] and receipt['runs'][-1]['state'] != 'observed':
            receipt['runs'][-1].update(state='failed', ended=time.time())
        raise
    finally:
        save()
