"""Runner admission/omission tests; synthetic receipts are not product evidence."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET

import pytest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("ci_suite_under_test", ROOT / "scripts/ci_python_suite.py")
suite = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(suite)


def frozen(path):
    return {"path": str(path), "bytes": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


@pytest.fixture
def profile(tmp_path):
    (tmp_path / "tests/python").mkdir(parents=True)
    test = tmp_path / "tests/python/test_example.py"
    test.write_text("def test_sample(): pass\n")
    sources = tmp_path / "sources.json"
    sources.write_text(json.dumps({"tests/python/test_example.py": frozen(test)["sha256"]}))
    product = tmp_path / "imagejxx.exe"
    product.write_bytes(b"synthetic Windows-name product, not executable proof")
    native = tmp_path / "native.xml"
    native.write_text('<testsuites><testsuite><testcase name="native_path_resize"/></testsuite></testsuites>')
    sdk = tmp_path / "sdk.json"
    sdk.write_text(json.dumps({"state": "passed", "exit_code": 0}))
    cache = tmp_path / "cache"
    cache.mkdir()
    (cache / "CMakeCache.txt").write_text("CMAKE_HOME_DIRECTORY:INTERNAL=" + str(tmp_path))
    return {"schema_version": 1, "profile": "focused", "packet_id": "admission-only",
            "platform": sys.platform,
            "repository": str(tmp_path), "source_manifest": frozen(sources),
            "toolchain": {"id": "test"}, "dependencies": [{"id": "reviewed-test", "python_path": str(Path(pytest.__file__).resolve().parents[1])}],
            "native_cache": str(cache), "products": {
                key: frozen(product) for key in ["application", "interpreter", "provider",
                    "imagejxx", "imagejxx._native", "imagejxx._desktop", "imagejxx._scientific_io"]},
            "python_modules": ["tests/python/test_example.py"],
            "expected_collected_ids": ["tests/python/test_example.py::test_sample"],
            "excluded_ids": [], "native_groups": ["native_path_resize"],
            "native_reports": [frozen(native)], "sdk_cases": [{"id": "path", "report": frozen(sdk)}],
            "ui_cases": [{"group": "path-segments", "case": "active"}],
            "report_root": str(tmp_path / "reports"), "scratch_root": str(tmp_path / "reports/scratch"),
            "timeouts": {"pytest": 10, "ui_case": 10},
            "resource_limits": {"max_working_bytes": 1024}}


def test_explicit_windows_executable_and_test_provider_do_not_need_build_wheel(profile):
    assert suite.validate_profile(profile) == Path(profile["repository"])
    assert not (Path(profile["repository"]) / "build").exists()


@pytest.mark.parametrize("role", ["application", "provider", "imagejxx._native", "imagejxx._desktop"])
def test_missing_product_is_refused_before_launch(profile, role):
    profile["products"][role]["path"] += ".missing"
    with pytest.raises(FileNotFoundError):
        suite.validate_profile(profile)


def test_changed_compiled_product_is_refused(profile):
    Path(profile["products"]["imagejxx._native"]["path"]).write_bytes(b"stale")
    with pytest.raises(ValueError, match="identity mismatch"):
        suite.validate_profile(profile)


def test_source_drift_is_refused(profile):
    (Path(profile["repository"]) / profile["python_modules"][0]).write_text("changed")
    with pytest.raises(ValueError, match="Source identity mismatch"):
        suite.validate_profile(profile)


@pytest.mark.parametrize("key,value", [
    ("native_groups", ["missing_group"]), ("expected_collected_ids", []),
    ("ui_cases", [{"group": "path-segments", "case": "missing"}]),
    ("excluded_ids", [{"id": "tests/python/test_example.py::test_sample", "reason": "overlap"}]),
])
def test_omitted_or_inconsistent_requirements_fail_admission(profile, key, value):
    profile[key] = value
    with pytest.raises(ValueError):
        suite.validate_profile(profile)


def test_unknown_fields_cannot_silently_weaken_profile(profile):
    profile["allow_failures"] = True
    with pytest.raises(ValueError, match="schema"):
        suite.validate_profile(profile)


def test_other_platform_profile_cannot_qualify_this_runtime(profile):
    profile["platform"] = "linux" if sys.platform != "linux" else "win32"
    with pytest.raises(ValueError, match="platform"):
        suite.validate_profile(profile)


def test_scratch_cannot_escape_owned_report_root(profile):
    profile["scratch_root"] = str(Path(profile["repository"]) / "elsewhere")
    with pytest.raises(ValueError, match="Scratch"):
        suite.validate_profile(profile)


def test_partial_indexed_group_is_refused():
    with pytest.raises(ValueError, match="must be complete"):
        suite.select_cases([["auto-contrast", "U01"]])


def test_native_skips_do_not_qualify_a_focused_profile(profile):
    path = Path(profile["native_reports"][0]["path"])
    path.write_text('<testsuites><testsuite><testcase name="native_path_resize"><skipped/></testcase></testsuite></testsuites>')
    profile["native_reports"] = [frozen(path)]
    with pytest.raises(ValueError, match="failed or skipped"):
        suite.validate_profile(profile)


def test_duplicate_ui_routes_cannot_hide_a_missing_mode(profile):
    profile["ui_cases"] *= 2
    with pytest.raises(ValueError, match="Duplicate"):
        suite.validate_profile(profile)


def test_default_observation_selection_still_contains_the_complete_catalog():
    assert suite.select_cases() == list(suite._observations.cases())


def test_collection_contract_keeps_documented_exclusion_visible():
    selected = "tests/python/test_owner.py::test_new"
    excluded = "tests/python/test_owner.py::test_old_guard"
    collector = suite.FrozenCollection([selected], [{"id": excluded, "reason": "original assertion retained"}])
    class Item:
        def __init__(self, nodeid): self.nodeid = nodeid
    class Hook:
        def pytest_deselected(self, items): self.deselected = [row.nodeid for row in items]
    class Config:
        hook = Hook()
    config = Config()
    items = [Item(excluded), Item(selected)]
    collector.pytest_collection_modifyitems(config, items)
    assert collector.observed == [selected]
    assert config.hook.deselected == [excluded]
    assert [row.nodeid for row in items] == [selected]


def test_unlisted_or_missing_collected_case_is_a_failure():
    collector = suite.FrozenCollection(["required"], [])
    with pytest.raises(pytest.UsageError, match="Collected IDs differ"):
        collector.pytest_collection_modifyitems(None, [])


@pytest.mark.parametrize("missing_case", [False, True])
def test_isolated_child_enforces_actual_collection_and_emits_raw_junit(profile, missing_case):
    if missing_case:
        profile["expected_collected_ids"] = ["tests/python/test_example.py::missing"]
    root = Path(profile["repository"])
    output = Path(profile["report_root"])
    output.mkdir()
    manifest = root / "focused.json"
    manifest.write_text(json.dumps(profile))
    result = subprocess.run([sys.executable, "-I", "-B", str(ROOT / "scripts/ci_python_suite.py"),
                             "--focused-pytest-child", str(manifest)],
                            env={**os.environ, "PYTEST_DISABLE_PLUGIN_AUTOLOAD": "1"},
                            capture_output=True, text=True, timeout=30)
    collection = json.loads((output / "collection.json").read_bytes())
    if missing_case:
        assert result.returncode != 0 and collection == []
    else:
        assert result.returncode == 0, result.stdout + result.stderr
        assert collection == profile["expected_collected_ids"]
        rows = ET.parse(output / "pytest.xml").getroot().findall(".//testcase")
        assert len(rows) == 1 and rows[0].get("name") == "test_sample"
    # The internal collector emits observations, never a qualification receipt.
    assert not (output / "receipt.json").exists()
