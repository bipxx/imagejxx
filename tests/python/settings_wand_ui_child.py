"""Fresh optional-Python-UI observation child; expected masks live in the reader."""
import hashlib
import json
import os
from pathlib import Path
import sys


def main():
    case, directory, preferences, dependency_paths = sys.argv[1:]
    for value in json.loads(dependency_paths):
        path = Path(value).resolve(strict=True)
        if path.name != "site-packages" or not path.is_dir():
            raise ValueError("Wand child dependency must be an installed site-packages directory")
        sys.path.append(str(path))
    import imagejxx
    from imagejxx import _desktop, _native
    from imagejxx.ui import app
    from imagejxx.ui.app import Editor

    def pin(module):
        path = Path(module.__file__).resolve(strict=True)
        return {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
    modules = (imagejxx, _desktop, _native, app)
    products = {module.__name__: pin(module) for module in modules}
    expected = os.environ.get("IMAGEJXX_DESKTOP_MODULE_SHA256")
    if expected and products["imagejxx._desktop"]["sha256"] != expected:
        raise ValueError("Wand child did not load the bound installed desktop module")
    expected_app = os.environ.get("IMAGEJXX_UI_APP_MODULE_SHA256")
    if expected_app and products["imagejxx.ui.app"]["sha256"] != expected_app:
        raise ValueError("Wand child did not load the bound installed Python UI wrapper")
    identity = {"process_id": os.getpid(), "parent_process_id": os.getppid(),
                "interpreter": sys.executable, "products": products}
    path = Path(directory).parent / (case + ".runtime.json")
    with path.open("x", encoding="utf-8") as stream:
        json.dump(identity, stream)
    editor = Editor(preferences=preferences)
    editor._editor._settings_wand_ui_case(case, directory)
    if products != {module.__name__: pin(module) for module in modules}:
        raise ValueError("Wand child installed products changed during observation")


if __name__ == "__main__":
    main()
