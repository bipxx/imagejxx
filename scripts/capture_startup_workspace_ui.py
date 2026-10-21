"""Capture paired P06j actual ImGui observations through the retained supervisor.

The parent imports only standard-library capture code. Product imports occur
only in the explicitly isolated installed Python child. This script collects
raw observations; independent test_startup_ui.py owns assertions.
"""
from __future__ import annotations

from pathlib import Path
import types

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "scripts/capture_shortcuts_history_ui.py"
supervisor = types.ModuleType("startup_ui_capture_supervisor")
supervisor.__file__ = str(SOURCE)
exec(compile(SOURCE.read_bytes(), str(SOURCE), "exec", dont_inherit=True), supervisor.__dict__)

CASES = tuple(f"SU{i:02}" for i in range(1, 11))
supervisor.COMMON = {
    **supervisor.COMMON,
    "python_launcher": "scripts/capture_startup_workspace_ui.py",
    "capture_supervisor": "scripts/capture_shortcuts_history_ui.py",
    "reader_primitives": "tests/python/startup_observation_reader.py",
    "window_header": "src/desktop/window.hpp",
    "startup_header": "include/imagejxx/startup.hpp",
    "startup_core": "src/native/startup.cpp",
    "startup_binding": "src/native/startup_bindings.cpp",
    "native_preferences_header": "include/imagejxx/preferences.hpp",
    "native_settings_header": "include/imagejxx/settings.hpp",
    "desktop_main": "src/desktop/main.cpp",
}
supervisor.ORIENTATION_CONTRACT = {
    "path": str(ROOT / "build/p06j-001/entry-adoption001/contract.json"),
    "bytes": 23982,
    "sha256": "5a22fb7872a9e505173d620657f5a66f3aa97d4d03c29d8677938cd94a27a014",
}
supervisor.SPEC = {
    "startup-workspace": {
        "stem": "startup-workspace", "prefix": "p06j",
        "cases": CASES, "development_cases": CASES,
        "flag": "--startup-workspace-ui-case", "method": "_startup_workspace_ui_case",
        "schema": "imagejxx.startup-workspace-ui-observations",
        "contract": "p06j.saved-workspace-startup.001",
        "raw_name": "case.json",
        "reader_kind": "p06j.startup-workspace-ui-reader-inputs.001",
        "build_kind": "p06j.startup-workspace-source-product-binding.001",
        "python_origin_kind": "p06j.startup-workspace-python-launcher-origin.001",
        "limits": {"parent_seconds": 120, "context_seconds": 60,
                   "paired_raw_bytes": 32 * 1024 * 1024,
                   "stage_bytes": 64 * 1024 * 1024},
        "roles": {
            "producer": "src/desktop/startup_ui_probe.cpp",
            "producer_header": "src/desktop/startup_ui_probe.hpp",
            "startup_panel": "src/desktop/startup_panel.cpp",
            "startup_panel_header": "src/desktop/startup_panel.hpp",
            "reader": "tests/python/test_startup_ui.py",
            "session": "src/native/session.cpp",
            "session_header": "include/imagejxx/session.hpp",
            "application": "src/native/application.cpp",
            "application_header": "include/imagejxx/application.hpp",
            "settings_io": "src/desktop/settings_io.cpp",
            "settings_io_header": "src/desktop/settings_io.hpp",
        },
    },
}

if __name__ == "__main__":
    supervisor.main()
