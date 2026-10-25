"""Bounded fresh-process producer; independent expected pixels live in tests."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

from settings_wand_ui_runner import terminate_process_tree

CASES = ('manual-workflow-current-path', 'manual-workflow-all-z-path',
         'manual-workflow-refusal-recovery')


def produce_python_reports(root):
    root = Path(root).resolve(strict=True)
    dependencies = sorted({str(Path(p).resolve()) for p in sys.path
                           if p and Path(p).is_dir() and Path(p).name == 'site-packages'})
    child_script = Path(__file__).with_name('manual_threshold_workflow_ui_child.py')
    receipt = {'state': 'running', 'acceptance_claim': False, 'runs': [], 'reports': {}}
    def save():
        (root / 'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf-8')
    save()
    try:
        for case in CASES:
            parent = root / case
            parent.mkdir()
            directory = parent / 'capture'
            runtime = parent / (case + '.runtime.json')
            argv = [sys.executable, '-I', '-X', 'utf8', '-B', str(child_script), case,
                    str(directory), str(parent / 'preferences.json'), json.dumps(dependencies)]
            env = os.environ.copy()
            for key in ('PYTHONHOME', 'PYTHONPATH', 'PYTHONUSERBASE', 'VIRTUAL_ENV', 'CONDA_PREFIX'):
                env.pop(key, None)
            entry = {'case': case, 'argv': argv, 'state': 'running', 'started': time.time()}
            receipt['runs'].append(entry)
            save()
            with (parent / 'process.log').open('xb') as log:
                child = subprocess.Popen(argv, cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT,
                                         start_new_session=os.name != 'nt')
                entry['launcher_process_id'] = child.pid
                save()
                try:
                    code = child.wait(timeout=60)
                except subprocess.TimeoutExpired:
                    entry['timeout_cleanup'] = terminate_process_tree(child, runtime)
                    entry.update(state='timed-out', exit_code=child.returncode)
                    save()
                    raise
            entry.update(exit_code=code, ended=time.time(), launcher_reaped=True)
            if code:
                raise RuntimeError('Manual workflow producer failed: ' + case)
            identity = json.loads(runtime.read_bytes())
            if identity['process_id'] != child.pid and identity['parent_process_id'] != child.pid:
                raise ValueError('Cannot bind launched Python interpreter')
            report = directory / 'case.json'
            if not 0 < report.stat().st_size < 16*1024**2:
                raise ValueError('Manual workflow report exceeds bound')
            row = json.loads(report.read_bytes())
            observed_runtime = json.loads((directory / 'runtime.json').read_bytes())
            if (row.get('schema') != 'manual-threshold.workflow.ui.001'
                    or row.get('case_id') != case or row.get('context') != 'imgui_python'
                    or row.get('status') != 'observed' or row.get('acceptance_claim') is not False
                    or observed_runtime['process_id'] != identity['process_id']
                    or observed_runtime['parent_process_id'] != identity['parent_process_id']):
                raise ValueError('Manual workflow producer identity mismatch')
            entry.update(state='observed', runtime=identity,
                         report_sha256=hashlib.sha256(report.read_bytes()).hexdigest())
            receipt['reports'][case] = str(report)
            save()
        receipt['state'] = 'observed'
        return receipt['reports']
    except BaseException as error:
        receipt.update(state='failed', error=str(error))
        if receipt['runs'] and receipt['runs'][-1]['state'] != 'observed':
            receipt['runs'][-1].update(state='failed', ended=time.time())
        raise
    finally:
        save()
