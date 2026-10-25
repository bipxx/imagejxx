"""Tiny module/file fixtures for origin transport; no installed product proof.

The authored validator is copied unchanged into an owned temporary source root.
Module objects and distribution metadata are synthetic; actual files/hashes are
checked. No original pytest ID, product extension, renderer or subprocess runs.
"""
import hashlib
import importlib.machinery
import importlib.util
import json
from pathlib import Path, PurePosixPath
import sys
import tempfile
from types import ModuleType, SimpleNamespace
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("ci_headless_origins_under_test",
                                            ROOT / "scripts/ci_headless_origins.py")
origins = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(origins)


class HeadlessOriginInputs(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name).resolve()
        self.root = self.directory / "repository"
        authored = self.root / "tests/python/shortcut_api_observations.py"
        authored.parent.mkdir(parents=True)
        authored.write_bytes((ROOT / "tests/python/shortcut_api_observations.py").read_bytes())
        self.site = self.directory / "installed site"
        self.numpy_site = self.directory / "numpy site"
        self.package_file = self.site / "imagejxx/__init__.py"
        self.native_file = self.site / "imagejxx" / ("_native" + importlib.machinery.EXTENSION_SUFFIXES[0])
        self.numpy_file = self.numpy_site / "numpy/__init__.py"
        for path, data in ((self.package_file, b"synthetic package bytes\n"),
                           (self.native_file, b"synthetic native file\x00\xff"),
                           (self.numpy_file, b"synthetic numpy bytes\n")):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        self.package = self.module("imagejxx", self.package_file)
        self.native = self.module("imagejxx._native", self.native_file)
        self.numpy = self.module("numpy", self.numpy_file)
        self.numpy.__version__ = "2.2.6"
        self.modules = {"imagejxx": self.package, "imagejxx._native": self.native, "numpy": self.numpy}
        loaded = patch.dict(sys.modules, self.modules)
        loaded.start(); self.addCleanup(loaded.stop)
        self.distributions = {
            "imagejxx": self.distribution(self.site, (self.package_file, self.native_file), "0.2.0"),
            "numpy": self.distribution(self.numpy_site, (self.numpy_file,), "2.2.6"),
        }
        metadata = patch.object(origins.importlib.metadata, "distribution",
                                side_effect=lambda name: self.distributions[name])
        metadata.start(); self.addCleanup(metadata.stop)
        self.output = self.directory / "origins"

    @staticmethod
    def module(name, path):
        module = ModuleType(name)
        module.__file__ = str(path)
        return module

    @staticmethod
    def distribution(site, paths, version):
        return SimpleNamespace(version=version,
            files=[PurePosixPath(path.relative_to(site).as_posix()) for path in paths],
            locate_file=lambda path: site / path)

    def invoke(self):
        return origins.prepare(self.output, self.package, self.native, self.numpy, self.root)

    def test_explicit_cmake_manifest_owns_product_without_inventing_distribution(self):
        manifest = self.directory / "install_manifest_processing-python.txt"
        manifest.write_text(str(self.package_file) + "\n" + str(self.native_file) + "\n", encoding="utf-8")
        def metadata(name):
            if name == "imagejxx":
                raise origins.importlib.metadata.PackageNotFoundError(name)
            return self.distributions[name]
        with patch.object(origins.importlib.metadata, "distribution", side_effect=metadata):
            result = origins.prepare(self.output, self.package, self.native, self.numpy, self.root,
                                     install_manifest=manifest)
        self.assertEqual(len(result), 2)
        self.assertFalse(any(self.site.glob("*.dist-info")))

    def test_default_without_distribution_metadata_does_not_infer_manifest_route(self):
        (self.directory / "install_manifest_processing-python.txt").write_text(
            str(self.package_file) + "\n" + str(self.native_file) + "\n", encoding="utf-8")
        with patch.object(origins.importlib.metadata, "distribution",
                          side_effect=origins.importlib.metadata.PackageNotFoundError("imagejxx")):
            with self.assertRaisesRegex(ValueError, "distribution metadata"):
                self.invoke()
        self.assertFalse(self.output.exists())

    def test_foreign_missing_or_duplicate_install_manifest_members_refuse(self):
        manifest = self.directory / "install_manifest_processing-python.txt"
        foreign = self.directory / "older product/imagejxx"
        foreign.mkdir(parents=True)
        (foreign / "__init__.py").write_bytes(b"old product")
        (foreign / self.native_file.name).write_bytes(b"old native")
        for members in ((foreign / "__init__.py", foreign / self.native_file.name),
                        (self.package_file,), (self.package_file, self.native_file, self.package_file),
                        (Path("relative/imagejxx/__init__.py"), self.native_file)):
            with self.subTest(members=members):
                manifest.write_text("\n".join(str(value) for value in members) + "\n", encoding="utf-8")
                with self.assertRaises(ValueError):
                    origins.prepare(self.output, self.package, self.native, self.numpy, self.root,
                                    install_manifest=manifest)
                self.assertFalse(self.output.exists())

    def test_install_manifest_mutation_after_validation_refuses(self):
        manifest = self.directory / "install_manifest_processing-python.txt"
        raw = str(self.package_file) + "\n" + str(self.native_file) + "\n"
        manifest.write_text(raw, encoding="utf-8")
        validator = origins._load_validator(self.root)
        original = validator.origins
        def changed(*args, **kwargs):
            result = original(*args, **kwargs)
            manifest.write_text(raw + str(self.numpy_file) + "\n", encoding="utf-8")
            return result
        with patch.object(validator, "origins", side_effect=changed), \
             patch.object(origins, "_load_validator", return_value=validator):
            with self.assertRaisesRegex(ValueError, "manifest changed"):
                origins.prepare(self.output, self.package, self.native, self.numpy, self.root,
                                install_manifest=manifest)

    def test_two_original_schemas_pin_actual_files_without_product_execution(self):
        path_before = list(sys.path)
        environment = self.invoke()
        self.assertEqual(set(environment), {"IMAGEJXX_HISTORY_PYTHON_ORIGINS",
                                           "IMAGEJXX_SHORTCUTS_PYTHON_ORIGINS"})
        self.assertEqual(sorted(path.name for path in self.output.iterdir()),
                         ["history-origins.json", "shortcuts-origins.json"])
        history = json.loads(Path(environment["IMAGEJXX_HISTORY_PYTHON_ORIGINS"]).read_bytes())
        shortcuts = json.loads(Path(environment["IMAGEJXX_SHORTCUTS_PYTHON_ORIGINS"]).read_bytes())
        self.assertEqual(set(history), {"schema_version", "kind", "package_root", "modules"})
        self.assertEqual(set(shortcuts), set(history) | {"numpy"})
        self.assertEqual(history["schema_version"], 1)
        self.assertEqual(history["kind"], "p06h_history_python_runtime_origins_v1")
        self.assertEqual(shortcuts["kind"], "p06h_shortcuts_python_runtime_origins_v1")
        self.assertEqual(history["package_root"], str(self.package_file.parent))
        self.assertEqual(history["modules"], shortcuts["modules"])
        for role, path in (("imagejxx", self.package_file), ("_native", self.native_file)):
            raw = path.read_bytes()
            self.assertEqual(history["modules"][role], {"path": str(path), "bytes": len(raw),
                              "sha256": hashlib.sha256(raw).hexdigest()})
        raw = self.numpy_file.read_bytes()
        self.assertEqual(shortcuts["numpy"], {"site_packages": str(self.numpy_site), "version": "2.2.6",
            "module": {"path": str(self.numpy_file), "bytes": len(raw),
                       "sha256": hashlib.sha256(raw).hexdigest()}})
        self.assertEqual(sys.path, path_before)
        self.assertNotIn("imagejxx._desktop", sys.modules)

    def test_unloaded_or_different_module_object_is_refused(self):
        for name in self.modules:
            with self.subTest(name=name), patch.dict(sys.modules, {name: ModuleType(name)}):
                with self.assertRaisesRegex(ValueError, "actual loaded module"):
                    self.invoke()
                self.assertFalse(self.output.exists())

    def test_desktop_ui_and_imgui_imports_are_refused_before_writing(self):
        for name in ("imagejxx._desktop", "imagejxx.ui", "imagejxx.ui.app",
                     "imgui_bundle", "imgui_bundle.imgui"):
            with self.subTest(name=name), patch.dict(sys.modules, {name: ModuleType(name)}):
                with self.assertRaisesRegex(ValueError, "desktop/UI"):
                    self.invoke()
                self.assertFalse(self.output.exists())

    def test_source_bootstrap_is_refused_even_with_distribution_metadata(self):
        source_package = self.root / "src/imagejxx"
        source_package.mkdir(parents=True)
        source_init = source_package / "__init__.py"
        source_native = source_package / self.native_file.name
        source_init.write_bytes(self.package_file.read_bytes())
        source_native.write_bytes(self.native_file.read_bytes())
        self.package.__file__ = str(source_init)
        self.native.__file__ = str(source_native)
        self.distributions["imagejxx"] = self.distribution(
            source_package.parent, (source_init, source_native), "0.2.0")
        with self.assertRaisesRegex(ValueError, "Source bootstrap"):
            self.invoke()
        self.assertFalse(self.output.exists())

    def test_missing_or_duplicate_distribution_ownership_is_refused(self):
        for name in ("imagejxx", "numpy"):
            distribution = self.distributions[name]
            original = distribution.files
            for mode in ("missing", "duplicate", "no_inventory"):
                with self.subTest(name=name, mode=mode):
                    distribution.files = (original[1:] if mode == "missing" else
                                          original + [original[0]] if mode == "duplicate" else None)
                    try:
                        with self.assertRaisesRegex(ValueError, "distribution"):
                            self.invoke()
                        self.assertFalse(self.output.exists())
                    finally:
                        distribution.files = original

    def test_wrong_numpy_module_and_distribution_versions_are_refused(self):
        for target in (self.numpy, self.distributions["numpy"]):
            field = "__version__" if target is self.numpy else "version"
            with self.subTest(field=field), patch.object(target, field, "2.3.0"):
                with self.assertRaisesRegex(ValueError, "NumPy|version"):
                    self.invoke()
                self.assertFalse(self.output.exists())

    def test_noncanonical_native_filename_is_refused_before_writing(self):
        other = self.native_file.with_name("not_native.bin")
        other.write_bytes(self.native_file.read_bytes())
        self.native.__file__ = str(other)
        with self.assertRaisesRegex(ValueError, "native extension path"):
            self.invoke()
        self.assertFalse(self.output.exists())

    def test_absent_module_file_is_refused(self):
        self.native_file.unlink()
        with self.assertRaises(FileNotFoundError):
            self.invoke()
        self.assertFalse(self.output.exists())

    def test_existing_output_is_not_reused_or_modified(self):
        self.output.mkdir()
        marker = self.output / "preserved.txt"
        marker.write_bytes(b"original")
        with self.assertRaisesRegex(ValueError, "fresh absolute"):
            self.invoke()
        self.assertEqual(marker.read_bytes(), b"original")
        self.assertEqual(list(self.output.iterdir()), [marker])

    def test_relative_output_is_refused(self):
        with self.assertRaisesRegex(ValueError, "fresh absolute"):
            origins.prepare(Path("origins"), self.package, self.native, self.numpy, self.root)

    def test_authored_validator_refuses_bytes_changed_after_initial_pin(self):
        validator = origins._load_validator(self.root)
        original = validator.origins
        def changed(path, mode, *, loaded):
            self.native_file.write_bytes(b"changed native bytes")
            return original(path, mode, loaded=loaded)
        with patch.object(validator, "origins", side_effect=changed) as checked:
            with patch.object(origins, "_load_validator", return_value=validator):
                with self.assertRaisesRegex(AssertionError, "Pinned installed bytes differ"):
                    self.invoke()
        checked.assert_called_once_with(self.output / "shortcuts-origins.json", "headless", loaded=True)
        self.assertTrue((self.output / "history-origins.json").is_file())
        self.assertTrue((self.output / "shortcuts-origins.json").is_file())


if __name__ == "__main__":
    unittest.main()
