"""Prepare actual installed origins for the five existing headless pytest IDs.

The caller supplies the modules loaded by its fresh isolated child. This module
does not import a product, launch a test, or assert build/qualification evidence.
The existing shortcut carrier validates the written pins and headless state.
"""
from __future__ import annotations

import hashlib
import importlib.machinery
import importlib.metadata
import importlib.util
import json
from pathlib import Path
import sys


MIB = 1048576


def _require(condition, message):
    if not condition:
        raise ValueError(message)


def _load_validator(root):
    source = root / "tests/python/shortcut_api_observations.py"
    _require(source.is_file() and not source.is_symlink(),
             "Authored headless origin validator is unavailable")
    _require(source.resolve(strict=True).is_relative_to(root),
             "Authored headless origin validator escapes the source root")
    data = source.read_bytes()
    _require(0 < len(data) <= MIB, "Authored origin validator exceeds its bound")
    spec = importlib.util.spec_from_file_location("imagejxx_ci_headless_origin_validator", source)
    _require(spec is not None and spec.loader is not None, "Cannot load authored origin validator")
    module = importlib.util.module_from_spec(spec)
    # Execute only the trusted repository source; do not reuse/write bytecode.
    exec(compile(data, str(source), "exec", dont_inherit=True), module.__dict__)
    return module


def _headless_modules(package, native_module, numpy_module):
    supplied = {"imagejxx": package, "imagejxx._native": native_module, "numpy": numpy_module}
    for name, module in supplied.items():
        _require(sys.modules.get(name) is module and module is not None,
                 "Supplied module is not the actual loaded module: " + name)
    forbidden = ("imagejxx._desktop", "imagejxx.ui", "imgui_bundle")
    _require(not any(name == prefix or name.startswith(prefix + ".")
                     for name in sys.modules for prefix in forbidden),
             "Headless origin preparation imported desktop/UI")


def _module_path(module, label):
    value = getattr(module, "__file__", None)
    _require(type(value) is str and value and Path(value).is_absolute(),
             "Actual absolute module path is required: " + label)
    path = Path(value)
    _require(not path.is_symlink(), "Linked module path is not an origin: " + label)
    path = path.resolve(strict=True)
    _require(path.is_file(), "Actual module is not a file: " + label)
    return path


def _distribution_owns(name, paths, *, version=None):
    try:
        distribution = importlib.metadata.distribution(name)
    except importlib.metadata.PackageNotFoundError as error:
        raise ValueError("Installed distribution metadata is unavailable: " + name) from error
    _require(distribution.files is not None, "Installed distribution has no file inventory: " + name)
    if version is not None:
        _require(distribution.version == version, "Installed distribution version differs: " + name)
    counts = {path: 0 for path in paths}
    for member in distribution.files:
        located = Path(distribution.locate_file(member)).resolve()
        if located in counts:
            counts[located] += 1
    _require(all(count == 1 for count in counts.values()),
             "Installed distribution must own each actual module exactly once: " + name)


def _install_manifest_owns(manifest, paths):
    """Explicit CMake delivery metadata; never synthesize wheel/dist-info files."""
    original = Path(manifest)
    _require(original.is_absolute() and not original.is_symlink(),
             "Actual absolute non-link CMake install manifest is required")
    path = original.resolve(strict=True)
    _require(path == original and path.is_file(), "Canonical regular install manifest required")
    for parent in (original, *original.parents):
        _require(not parent.is_symlink() and not getattr(parent.lstat(), "st_file_attributes", 0) & 0x400,
                 "Linked install manifest ancestor refused")
    _require(0 < path.stat().st_size <= MIB, "Install manifest exceeds its metadata bound")
    raw = path.read_bytes()
    _require(0 < len(raw) <= MIB, "Install manifest changed beyond its metadata bound")
    counts = {member: 0 for member in paths}
    for value in raw.decode("utf-8").splitlines():
        if not value:
            continue
        member = Path(value)
        _require(member.is_absolute(), "Install manifest member must be absolute")
        resolved = member.resolve()
        if resolved in counts:
            _require(member.is_file() and not member.is_symlink(), "Linked or missing installed target refused")
            for parent in (member, *member.parents):
                _require(not parent.is_symlink() and not getattr(parent.lstat(), "st_file_attributes", 0) & 0x400,
                         "Linked installed target ancestor refused")
            counts[resolved] += 1
    _require(all(count == 1 for count in counts.values()),
             "CMake install manifest must own each actual product module exactly once")
    return {"path": str(path), "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}


def prepare(output, package, native_module, numpy_module, root, *, install_manifest=None):
    """Write two fresh original-schema envelopes and return their environment map.

    ``package``, ``native_module`` and ``numpy_module`` must be the actual loaded
    imagejxx, imagejxx._native and NumPy objects. The child launch/isolation and
    original five pytest IDs remain the caller's separately verified boundary.
    Failure leaves any already-written diagnostic inputs intact.
    """
    root = Path(root).resolve(strict=True)
    _require(root.is_dir(), "Source root is not a directory")
    output = Path(output)
    _require(output.is_absolute() and not output.exists() and not output.is_symlink(),
             "Headless origins require a fresh absolute output directory")
    _require(output.parent.resolve(strict=True) == output.parent,
             "Headless origin output parent must be canonical")
    _headless_modules(package, native_module, numpy_module)
    package_file = _module_path(package, "imagejxx")
    native_file = _module_path(native_module, "imagejxx._native")
    numpy_file = _module_path(numpy_module, "numpy")
    package_root = package_file.parent
    _require(package_root.name == "imagejxx" and package_file.name == "__init__.py",
             "Actual installed imagejxx package initializer is required")
    _require(native_file.parent == package_root and any(
        native_file.name == "_native" + suffix for suffix in importlib.machinery.EXTENSION_SUFFIXES),
        "Actual installed native extension path differs")
    _require(numpy_file.parent.name == "numpy" and numpy_file.name == "__init__.py",
             "Actual installed NumPy initializer is required")
    source_root = root / "src"
    _require(not package_root.is_relative_to(source_root)
             and not numpy_file.is_relative_to(source_root), "Source bootstrap refused")
    _require(getattr(numpy_module, "__version__", None) == "2.2.6",
             "Approved actual NumPy 2.2.6 is required")
    delivery = None
    if install_manifest is None:
        _distribution_owns("imagejxx", (package_file, native_file))
    else:
        delivery = _install_manifest_owns(install_manifest, (package_file, native_file))
    _distribution_owns("numpy", (numpy_file,), version="2.2.6")
    validator = _load_validator(root)
    modules = {"imagejxx": validator.pin(package_file), "_native": validator.pin(native_file)}
    numpy = {"site_packages": str(numpy_file.parent.parent), "version": "2.2.6",
             "module": validator.pin(numpy_file)}
    history = {"schema_version": 1, "kind": "p06h_history_python_runtime_origins_v1",
               "package_root": str(package_root), "modules": modules}
    shortcuts = {"schema_version": 1, "kind": "p06h_shortcuts_python_runtime_origins_v1",
                 "package_root": str(package_root), "modules": modules, "numpy": numpy}
    output.mkdir()
    history_path = output / "history-origins.json"
    shortcut_path = output / "shortcuts-origins.json"
    for path, value in ((history_path, history), (shortcut_path, shortcuts)):
        data = (json.dumps(value, sort_keys=True, ensure_ascii=False, allow_nan=False,
                           indent=2) + "\n").encode("utf-8")
        _require(0 < len(data) <= MIB, "Headless origin envelope exceeds its bound")
        with path.open("xb") as stream:
            stream.write(data)
    # Reuse the original authored pin, layout, NumPy and loaded-origin checks.
    checked = validator.origins(shortcut_path, "headless", loaded=True)
    _headless_modules(package, native_module, numpy_module)
    _require(checked["modules"] == history["modules"] and checked["numpy"] == numpy,
             "Actual origins changed while preparing envelopes")
    _require(json.loads(history_path.read_bytes()) == history,
             "Written history origin envelope changed")
    if delivery is not None:
        _require(_install_manifest_owns(install_manifest, (package_file, native_file)) == delivery,
                 "CMake install manifest changed during origin preparation")
    return {"IMAGEJXX_HISTORY_PYTHON_ORIGINS": str(history_path),
            "IMAGEJXX_SHORTCUTS_PYTHON_ORIGINS": str(shortcut_path)}
