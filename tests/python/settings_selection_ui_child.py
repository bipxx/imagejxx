"""Fresh optional Python UI host; expected geometry stays in the reader."""
import hashlib
import json
import os
from pathlib import Path
import sys


def main():
    case, directory, preferences, dependencies = sys.argv[1:]
    for name in json.loads(dependencies):
        path = Path(name).resolve(strict=True)
        if path.name != 'site-packages' or not path.is_dir():
            raise ValueError('Expected an installed dependency directory')
        sys.path.append(str(path))
    import imagejxx
    from imagejxx import _native, _desktop
    from imagejxx.ui import app
    modules = (imagejxx, _native, _desktop, app)

    def pins():
        return {m.__name__: {'path': str(Path(m.__file__).resolve()),
                            'sha256': hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest()}
                for m in modules}
    products = pins()
    for module, variable in [('imagejxx._native', 'IMAGEJXX_NATIVE_MODULE_SHA256'),
                             ('imagejxx._desktop', 'IMAGEJXX_DESKTOP_MODULE_SHA256'),
                             ('imagejxx.ui.app', 'IMAGEJXX_UI_APP_MODULE_SHA256')]:
        if os.environ.get(variable) and products[module]['sha256'] != os.environ[variable]:
            raise ValueError('Installed product identity mismatch: ' + module)
    identity = {'process_id': os.getpid(), 'parent_process_id': os.getppid(),
                'interpreter': sys.executable, 'products': products}
    with (Path(directory).parent/(case+'.runtime.json')).open('x', encoding='utf-8') as stream:
        json.dump(identity, stream)
    editor = app.Editor(preferences=preferences)
    editor._editor._settings_selection_ui_case(case, directory)
    if products != pins():
        raise ValueError('Installed products changed during observation')


if __name__ == '__main__':
    main()
