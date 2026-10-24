"""A real owned interpreter tree must stop before timeout scratch can retire."""
import ctypes
import json
import os
from pathlib import Path
import subprocess
import sys
import time

import pytest

from settings_wand_ui_runner import terminate_process_tree


def stopped(pid):
    if os.name == "nt":
        from ctypes import wintypes
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        kernel.OpenProcess.restype = wintypes.HANDLE
        kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        handle = kernel.OpenProcess(0x00100000, False, pid)
        if not handle:
            assert ctypes.get_last_error() == 87
            return True
        try:
            return kernel.WaitForSingleObject(handle, 0) == 0
        finally:
            kernel.CloseHandle(handle)
    # An exited zombie has no executing code or pending userspace file writes.
    stat = Path("/proc") / str(pid) / "stat"
    try:
        return stat.read_text().rsplit(")", 1)[1].strip().split()[0] == "Z"
    except FileNotFoundError:
        pass
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return True
    return False


@pytest.mark.parametrize("runtime_kind", ["valid", "partial", "unbound"])
def test_timeout_cleanup_reaps_launcher_and_stops_actual_interpreter_tree(tmp_path, runtime_kind):
    script = tmp_path / "owned_tree.py"
    runtime = tmp_path / "runtime.json"
    script.write_text(
        "import json,os,subprocess,sys,time\n"
        "from pathlib import Path\n"
        "leaf=subprocess.Popen([sys.executable,'-I','-c','import time;time.sleep(30)'])\n"
        "identity={'process_id':os.getpid(),'parent_process_id':os.getppid(),'leaf_id':leaf.pid}\n"
        "Path(sys.argv[1]+'.fixture').write_text(json.dumps(identity))\n"
        "payload=json.dumps(identity) if sys.argv[2]=='valid' else '{\"process_id\":' if sys.argv[2]=='partial' "
        "else json.dumps({'process_id':os.getpid()+1,'parent_process_id':1})\n"
        "Path(sys.argv[1]).write_text(payload)\n"
        "Path(sys.argv[1]+'.ready').write_text('ready')\n"
        "time.sleep(30)\n", encoding="utf-8")
    with (tmp_path / "tree.log").open("xb") as log:
        child = subprocess.Popen([sys.executable, "-I", str(script), str(runtime), runtime_kind],
                                 stdout=log, stderr=subprocess.STDOUT,
                                 start_new_session=os.name != "nt")
        try:
            deadline = time.monotonic() + 5
            while not Path(str(runtime) + ".ready").exists() and time.monotonic() < deadline:
                assert child.poll() is None
                time.sleep(.02)
            assert Path(str(runtime) + ".ready").exists(), "Owned timeout fixture failed to start"
            identity = json.loads(Path(str(runtime) + ".fixture").read_bytes())
            assert identity["process_id"] == child.pid or identity["parent_process_id"] == child.pid
            cleanup = terminate_process_tree(child, runtime)
            assert cleanup["launcher_reaped"] and child.poll() is not None
            assert bool(cleanup["runtime_metadata_error"]) == (runtime_kind != "valid")
            deadline = time.monotonic() + 5
            while not stopped(identity["leaf_id"]) and time.monotonic() < deadline:
                time.sleep(.02)
            assert stopped(identity["process_id"]) and stopped(identity["leaf_id"])
            (tmp_path / "cleanup.json").write_text(json.dumps(cleanup), encoding="utf-8")
        finally:
            if child.poll() is None:
                terminate_process_tree(child, runtime)
