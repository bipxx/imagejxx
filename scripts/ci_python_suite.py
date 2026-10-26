"""Run the complete installed Python suite with explicit native build inputs.

C++ observations come from fresh installed application runs, then independent
Python readers check them. Missing features are not accepted.
Missing bindings, failed tests and skipped tests all leave this profile failing.
The receipt records named environment contracts to guide portable fixture work.
"""
from __future__ import annotations

import argparse
import ast
import hashlib
import importlib
import importlib.metadata
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import sysconfig
import time
import xml.etree.ElementTree as ET

def pin(path):
    path = Path(path).resolve(strict=True)
    raw = path.read_bytes()
    return {"path": str(path), "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}


def _source_module(name, path):
    """Load current source bytes; callers still own frozen-source admission."""
    path = Path(path)
    before = pin(path)
    source = path.read_bytes()
    if len(source) != before["bytes"] or hashlib.sha256(source).hexdigest() != before["sha256"]:
        raise ValueError("Source helper changed while reading: " + str(path))
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    # SourceFileLoader may read a timestamp-valid .pyc even when -B is enabled.
    exec(compile(source, spec.origin, "exec", dont_inherit=True), module.__dict__)
    if pin(path) != before:
        raise ValueError("Source helper changed while loading: " + str(path))
    return module


_observations = _source_module(
    "imagejxx_ci_observations", Path(__file__).with_name("ci_observations.py"))
archive, capture, select_cases = _observations.archive, _observations.capture, _observations.select_cases


PROFILE_FIELDS = {
    "schema_version", "profile", "packet_id", "platform", "repository", "source_manifest",
    "toolchain", "dependencies", "native_cache", "products", "python_modules",
    "expected_collected_ids", "excluded_ids", "native_groups", "native_reports",
    "sdk_cases", "ui_cases", "scratch_root", "report_root", "timeouts", "resource_limits",
}


def checked_pin(value):
    if (not isinstance(value, dict) or set(value) != {"path", "bytes", "sha256"}
            or type(value["bytes"]) is not int or value["bytes"] < 0
            or not isinstance(value["sha256"], str)
            or not re.fullmatch(r"[0-9a-f]{64}", value["sha256"])):
        raise ValueError("Product/report identity requires path, bytes and SHA256")
    actual = pin(value["path"])
    if any(actual[key] != value[key] for key in ("bytes", "sha256")):
        raise ValueError(f"Product/report identity mismatch: {value['path']}")
    return actual


def nonempty_unique(values, name):
    if (not isinstance(values, list) or not values
            or any(not isinstance(value, str) or not value for value in values)
            or len(values) != len(set(values))):
        raise ValueError(f"{name} must contain nonempty unique strings")


def validate_profile(profile):
    """Admission only. A validated manifest earns no executed product credit."""
    if (not isinstance(profile, dict) or set(profile) != PROFILE_FIELDS
            or profile["schema_version"] != 1 or profile["profile"] != "focused"):
        raise ValueError("Unknown/incomplete focused profile schema")
    if not isinstance(profile["packet_id"], str) or not profile["packet_id"]:
        raise ValueError("Focused profile needs a packet identity")
    if profile["platform"] != sys.platform:
        raise ValueError("Focused profile platform does not match this interpreter")
    root = Path(profile["repository"]).resolve(strict=True)
    source_pin = checked_pin(profile["source_manifest"])
    sources = json.loads(Path(source_pin["path"]).read_bytes())
    if not isinstance(sources, dict) or not sources:
        raise ValueError("Source manifest must be a nonempty path-to-SHA256 map")
    for name, expected in sources.items():
        path = (root / name).resolve(strict=True)
        if (not path.is_relative_to(root) or not isinstance(expected, str)
                or pin(path)["sha256"] != expected):
            raise ValueError(f"Source identity mismatch: {name}")
    products = profile["products"]
    required = {"application", "interpreter", "provider", "imagejxx",
                "imagejxx._native", "imagejxx._desktop", "imagejxx._scientific_io"}
    if not isinstance(products, dict) or not required <= set(products):
        raise ValueError("Explicit installed application/interpreter/modules/provider are required")
    for value in products.values():
        checked_pin(value)
    nonempty_unique(profile["python_modules"], "Python modules")
    for name in profile["python_modules"]:
        path = (root / name).resolve(strict=True)
        if not path.is_relative_to(root / "tests/python") or name not in sources:
            raise ValueError("Each selected module must be a frozen repository Python test")
    nonempty_unique(profile["expected_collected_ids"], "Collected test IDs")
    nonempty_unique(profile["native_groups"], "Native groups")
    excluded = profile["excluded_ids"]
    if not isinstance(excluded, list):
        raise ValueError("Exclusions must be explicitly listed")
    seen = set()
    for row in excluded:
        if (not isinstance(row, dict) or set(row) != {"id", "reason"}
                or not all(isinstance(row[key], str) and row[key] for key in row)
                or row["id"] in seen or row["id"] in profile["expected_collected_ids"]):
            raise ValueError("Invalid/duplicate exclusion or selected/excluded overlap")
        seen.add(row["id"])
    if not isinstance(profile["ui_cases"], list) or not profile["ui_cases"]:
        raise ValueError("Focused profile needs explicit UI routes")
    for row in profile["ui_cases"]:
        if (not isinstance(row, dict) or set(row) != {"group", "case"}
                or any(not isinstance(value, str) for value in row.values())):
            raise ValueError("UI route requires group and case")
    select_cases([[row["group"], row["case"]] for row in profile["ui_cases"]])
    report = Path(profile["report_root"]).resolve()
    scratch = Path(profile["scratch_root"]).resolve()
    if report == root or not report.is_relative_to(root):
        raise ValueError("Report root must be inside the owned repository")
    if scratch == report or not scratch.is_relative_to(report):
        raise ValueError("Scratch must be strictly inside the owned report root")
    if report.exists() or scratch.exists():
        raise ValueError("Use fresh owned report/scratch roots")
    timeouts = profile["timeouts"]
    if (not isinstance(timeouts, dict) or set(timeouts) != {"pytest", "ui_case"}
            or any(type(value) is not int or not 1 <= value <= 7200
                   for value in timeouts.values()) or timeouts["ui_case"] > 3600):
        raise ValueError("Invalid focused timeouts")
    if not isinstance(profile["toolchain"], dict) or not profile["toolchain"]:
        raise ValueError("Toolchain identity is required")
    if not isinstance(profile["dependencies"], list) or not profile["dependencies"]:
        raise ValueError("Reviewed dependency identities are required")
    if not (Path(profile["native_cache"]).resolve(strict=True) / "CMakeCache.txt").is_file():
        raise ValueError("Existing configured native cache is required")
    if (not isinstance(profile["resource_limits"], dict)
            or set(profile["resource_limits"]) != {"max_working_bytes"}
            or type(profile["resource_limits"]["max_working_bytes"]) is not int
            or profile["resource_limits"]["max_working_bytes"] <= 0):
        raise ValueError("Explicit positive working-storage budget is required")
    native = []
    if not isinstance(profile["native_reports"], list) or not profile["native_reports"]:
        raise ValueError("Bound native reports are required")
    for value in profile["native_reports"]:
        checked_pin(value)
        cases = ET.parse(value["path"]).getroot().findall(".//testcase")
        for case in cases:
            if any(case.find(tag) is not None for tag in ("failure", "error", "skipped")):
                raise ValueError("Native prerequisite report has failed or skipped cases")
            native.append(case.get("name"))
    if any(native.count(name) != 1 for name in profile["native_groups"]):
        raise ValueError("Expected native group is missing or duplicated")
    sdk = profile["sdk_cases"]
    if (not isinstance(sdk, list) or not sdk
            or any(not isinstance(row, dict) or set(row) != {"id", "report"} for row in sdk)):
        raise ValueError("Explicit bound SDK cases are required")
    nonempty_unique([row["id"] for row in sdk], "SDK cases")
    for row in sdk:
        checked_pin(row["report"])
        report_value = json.loads(Path(row["report"]["path"]).read_bytes())
        if report_value.get("state") != "passed" or report_value.get("exit_code") != 0:
            raise ValueError("SDK prerequisite case did not pass")
    return root


class FrozenCollection:
    def __init__(self, expected, excluded):
        self.expected = set(expected)
        self.excluded = {row["id"] for row in excluded}
        self.observed = []

    def pytest_collection_modifyitems(self, config, items):
        actual = [item.nodeid for item in items]
        if len(actual) != len(set(actual)) or set(actual) != self.expected | self.excluded:
            import pytest
            required = self.expected | self.excluded
            raise pytest.UsageError("Collected IDs differ from the frozen focused profile; "
                                    f"missing={sorted(required - set(actual))[:20]}, "
                                    f"unexpected={sorted(set(actual) - required)[:20]}")
        deselected = [item for item in items if item.nodeid in self.excluded]
        items[:] = [item for item in items if item.nodeid in self.expected]
        self.observed = [item.nodeid for item in items]
        config.hook.pytest_deselected(items=deselected)


def run_focused(profile):
    root = validate_profile(profile)
    add_dependency_paths(profile)
    for name, value in profile["products"].items():
        if name in {"imagejxx", "imagejxx._native", "imagejxx._desktop", "imagejxx._scientific_io"}:
            module = importlib.import_module(name)
            if Path(module.__file__).resolve() != Path(value["path"]).resolve():
                raise ValueError(f"Module is not the frozen installed product: {name}")
    if Path(sys.executable).resolve() != Path(profile["products"]["interpreter"]["path"]).resolve():
        raise ValueError("Run with the declared installed verification interpreter")
    output = Path(profile["report_root"])
    output.mkdir(parents=True)
    receipt = {"schema_version": 1, "state": "running", "profile": "focused",
               "profile_scope_qualified": False, "packet_scope_qualified": False, "full_ci_qualified": False,
               "acceptance": False, "complete_requirement_coverage": False,
               "manifest": profile, "started": time.time(), "live_handle_or_none": os.getpid()}

    def save():
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")

    save()
    try:
        environment = os.environ.copy()
        environment.update(IMAGEJXX_RENDER_TESTS="1", IMAGEJXX_REQUIRE_RENDER_TESTS="1",
                           PYTHONDONTWRITEBYTECODE="1", PYTEST_DISABLE_PLUGIN_AUTOLOAD="1",
                           IMAGEJXX_SAVED_ROI_PROVIDER=profile["products"]["provider"]["path"],
                           IMAGEJXX_CPP_EXE=profile["products"]["application"]["path"],
                           IMAGEJXX_CPP_EXE_SHA256=profile["products"]["application"]["sha256"],
                           IMAGEJXX_DESKTOP_MODULE_SHA256=profile["products"]["imagejxx._desktop"]["sha256"])
        environment.update(capture(output / "cpp-observations", environment["IMAGEJXX_CPP_EXE"],
                                   root, environment, selected_cases=[
                                       [row["group"], row["case"]] for row in profile["ui_cases"]],
                                   case_timeout=profile["timeouts"]["ui_case"]))
        # Execute a fresh child so pytest/plugins cannot redirect this process's
        # module imports, cwd or environment while producer identity is checked.
        profile_path = output / "profile.json"
        profile_path.write_text(json.dumps(profile, indent=2) + "\n", encoding="utf-8")
        argv = [sys.executable, "-I", "-X", "utf8", "-B", str(Path(__file__).resolve()),
                "--focused-pytest-child", str(profile_path)]
        receipt["argv"] = argv
        save()
        result = subprocess.run(argv, cwd=root, env=environment, check=False,
                                timeout=profile["timeouts"]["pytest"])
        receipt["exit_code"] = result.returncode
        report = output / "pytest.xml"
        rows = ET.parse(report).getroot().findall(".//testcase") if report.exists() else []
        receipt["counts"] = {"cases": len(rows), **{
            tag: sum(row.find(tag) is not None for row in rows)
            for tag in ("failure", "error", "skipped")}}
        collected_path = output / "collection.json"
        collected = json.loads(collected_path.read_bytes()) if collected_path.exists() else []
        if (result.returncode or set(collected) != set(profile["expected_collected_ids"])
                or len(rows) != len(collected)
                or any(receipt["counts"][tag] for tag in ("failure", "error", "skipped"))):
            raise ValueError("Focused installed suite failed its frozen nonempty case contract")
        for value in profile["products"].values():
            checked_pin(value)
        sources = json.loads(Path(profile["source_manifest"]["path"]).read_bytes())
        checked_pin(profile["source_manifest"])
        if any(pin(root / name)["sha256"] != expected for name, expected in sources.items()):
            raise ValueError("Source inputs changed during verification")
        # This runner proves the manifest's selected assertions. Only the
        # packet's separate requirement/matrix audit can qualify the packet.
        receipt.update(state="focused-profile-passed", profile_scope_qualified=True)
    except BaseException as error:
        receipt.update(state="failed", error={"type": type(error).__name__, "message": str(error)})
        raise
    finally:
        receipt.update(ended=time.time(), live_handle_or_none=None)
        save()


def focused_pytest_child(path):
    profile = json.loads(Path(path).read_bytes())
    add_dependency_paths(profile)
    collector = FrozenCollection(profile["expected_collected_ids"], profile["excluded_ids"])
    root = Path(profile["repository"])
    os.chdir(root)
    import pytest
    result = pytest.main(["-q", "--tb=short", "--rootdir", str(root), "-p", "no:cacheprovider",
                          "--basetemp", profile["scratch_root"], "--junitxml",
                          str(Path(profile["report_root"]) / "pytest.xml"),
                          *profile["python_modules"]], plugins=[collector])
    (Path(profile["report_root"]) / "collection.json").write_text(
        json.dumps(collector.observed, indent=2) + "\n", encoding="utf-8")
    return result


def add_dependency_paths(profile):
    """Reuse explicitly reviewed dependency installs in no-pip verification envs."""
    for row in profile["dependencies"]:
        if not isinstance(row, dict):
            raise ValueError("Dependency identities must be objects")
        if "python_path" in row:
            path = Path(row["python_path"]).resolve(strict=True)
            if not path.is_dir():
                raise ValueError("Dependency Python path must be an existing directory")
            if str(path) not in sys.path:
                sys.path.append(str(path))


def installed_application(distribution, *, platform=None):
    """Resolve the platform application only from the installed RECORD."""
    platform = sys.platform if platform is None else platform
    basename = "imagejxx.exe" if platform == "win32" else "imagejxx"
    applications = [Path(distribution.locate_file(path)).resolve()
                    for path in distribution.files or [] if path.name == basename]
    applications = [path for path in applications if path.is_file() and os.access(path, os.X_OK)]
    if len(applications) != 1:
        raise ValueError(f"Expected one installed C++ desktop application, found {len(applications)}")
    return applications[0]


WINDOWS_RUNTIME_POLICY_REASON = "The exact reviewed delivery profile is Windows x64"
WINDOWS_RUNTIME_POLICY_IDS = tuple(
    "tests/python/test_tiff_runtime_policy.py::" + name
    for name in (
        *("test_cmake_fails_closed_for_runtime_header_and_linkage_changes[" + change + "]"
          for change in ("none", "missing_dll", "changed_dll", "changed_header", "missing_link", "wrong_headers", "foreign_zlib")),
        "test_recheck_refuses_changes_after_successful_configuration[bin/tiff.dll]",
        "test_recheck_refuses_changes_after_successful_configuration[include/tiffio.h]",
        "test_install_refuses_a_changed_dependency_before_copying_any_codec"))


def platform_test_selection(collected_ids, *, platform=None):
    """Exclude only the ten explicitly Windows-only delivery-policy controls."""
    nonempty_unique(collected_ids, "Complete collected test IDs")
    required = set(WINDOWS_RUNTIME_POLICY_IDS)
    if not required <= set(collected_ids):
        raise ValueError("Complete collection is missing required Windows runtime-policy IDs")
    platform = sys.platform if platform is None else platform
    excluded = [] if platform == "win32" else [
        {"id": name, "reason": WINDOWS_RUNTIME_POLICY_REASON}
        for name in collected_ids if name in required]
    omitted = {row["id"] for row in excluded}
    return [name for name in collected_ids if name not in omitted], excluded


def junit_identity(nodeid):
    """Match pytest's module/class naming while preserving opaque parameter IDs."""
    prefix, bracket, parameters = nodeid.partition("[")
    parts = prefix.split("::")
    module = parts[0].replace("/", ".")
    parts[0] = module[:-3] if module.endswith(".py") else module
    parts[-1] += bracket + parameters
    return ".".join(parts[:-1]), parts[-1]


HEADLESS_TEST_IDS = (
    "tests/python/test_history_settings.py::test_history_step_policy_effects",
    "tests/python/test_history_settings.py::test_history_pixel_budget_effects",
    "tests/python/test_history_settings.py::test_history_persist_reopen_adopt",
    "tests/python/test_history_settings.py::test_history_detached_settings_no_owner_effects",
    "tests/python/test_shortcuts.py::test_A01_base_registry_no_desktop",
)


def validate_selected_results(selected, junit_rows, phases):
    """Exact execution outcomes shared by original and explicitly routed cohorts."""
    expected = [junit_identity(name) for name in selected]
    actual = [(row.get("classname"), row.get("name")) for row in junit_rows]
    if (len(set(expected)) != len(expected) or actual != expected
            or any(row.find(tag) is not None for row in junit_rows
                   for tag in ("failure", "error", "skipped"))):
        raise ValueError("Complete terminal test identities/outcomes differ from collection")
    expected_phases = [(name, phase) for name in selected for phase in ("setup", "call", "teardown")]
    if (not isinstance(phases, list) or len(phases) != len(expected_phases)
            or any(not isinstance(row, dict) or set(row) != {"id", "phase", "outcome", "duration"}
                   for row in phases)
            or [(row["id"], row["phase"]) for row in phases] != expected_phases
            or any(row["outcome"] != "passed" or type(row["duration"]) not in (int, float)
                   or not math.isfinite(row["duration"]) or row["duration"] < 0 for row in phases)):
        raise ValueError("Complete setup/call/teardown phases are missing or unacceptable")


def validate_complete_results(collection, junit_rows, phases, *, platform=None):
    platform = sys.platform if platform is None else platform
    if (not isinstance(collection, dict)
            or set(collection) != {"schema", "platform", "collected", "selected", "excluded"}
            or collection["schema"] != "imagejxx.complete-collection.001"
            or collection["platform"] != platform):
        raise ValueError("Unknown or wrong-platform complete collection")
    selected, excluded = platform_test_selection(collection["collected"], platform=platform)
    if collection["selected"] != selected or collection["excluded"] != excluded:
        raise ValueError("Complete platform collection changed")
    validate_selected_results(selected, junit_rows, phases)


def validate_headless_results(collection, junit_rows, phases, expected_id, *, platform=None):
    platform = sys.platform if platform is None else platform
    if (expected_id not in HEADLESS_TEST_IDS or not isinstance(collection, dict)
            or set(collection) != {"schema", "platform", "collected", "selected", "excluded"}
            or collection["schema"] != "imagejxx.headless-collection.001"
            or collection["platform"] != platform or collection["collected"] != [expected_id]
            or collection["selected"] != [expected_id] or collection["excluded"] != []):
        raise ValueError("Headless child must collect exactly its original required test ID")
    validate_selected_results([expected_id], junit_rows, phases)


def routed_test_selection(collected_ids, *, platform=None):
    selected, excluded = platform_test_selection(collected_ids, platform=platform)
    required = set(HEADLESS_TEST_IDS)
    if not required <= set(selected):
        raise ValueError("Complete collection is missing required isolated headless IDs")
    return ([name for name in selected if name not in required], excluded,
            [name for name in selected if name in required])


def validate_routed_results(collection, junit_rows, phases, headless_reports, *, platform=None):
    platform = sys.platform if platform is None else platform
    if (not isinstance(collection, dict)
            or set(collection) != {"schema", "platform", "collected", "selected", "excluded", "routed"}
            or collection["schema"] != "imagejxx.complete-collection.002"
            or collection["platform"] != platform):
        raise ValueError("Unknown routed complete collection")
    selected, excluded, routed = routed_test_selection(collection["collected"], platform=platform)
    if (collection["selected"] != selected or collection["excluded"] != excluded
            or collection["routed"] != routed):
        raise ValueError("Routed complete collection changed its original obligations")
    validate_selected_results(selected, junit_rows, phases)
    if (not isinstance(headless_reports, list) or len(headless_reports) != len(HEADLESS_TEST_IDS)
            or any(not isinstance(row, dict) or set(row) != {
                "id", "collection", "junit_rows", "phases", "process"} for row in headless_reports)
            or [row["id"] for row in headless_reports] != list(HEADLESS_TEST_IDS)):
        raise ValueError("Missing, substituted or duplicated isolated headless results")
    for row in headless_reports:
        proof = row["process"]
        if (not isinstance(proof, dict) or proof.get("schema") != "imagejxx.owned-command.001"
                or proof.get("state") != "exited" or type(proof.get("exit_code")) is not int or proof.get("exit_code") != 0
                or proof.get("target_completed") is not True or proof.get("cleanup_complete") is not True):
            raise ValueError("Isolated headless child has no successful resolved owned exit")
        validate_headless_results(row["collection"], row["junit_rows"], row["phases"], row["id"], platform=platform)
    original, _ = platform_test_selection(collection["collected"], platform=platform)
    actual = selected + [row["id"] for row in headless_reports]
    if len(actual) != len(set(actual)) or set(actual) != set(original) or set(routed) != set(HEADLESS_TEST_IDS):
        raise ValueError("Broad and isolated results do not form the exact required disjoint union")


class CompleteCollection:
    """Keep exact collection, live-test and terminal phase evidence for CI."""
    def __init__(self, output):
        self.output = Path(output)
        self.phases = []
        self.selected = []

    def pytest_collection_modifyitems(self, config, items):
        collected = [item.nodeid for item in items]
        self.selected, excluded = platform_test_selection(collected)
        omitted = {row["id"] for row in excluded}
        deselected = [item for item in items if item.nodeid in omitted]
        items[:] = [item for item in items if item.nodeid not in omitted]
        config.hook.pytest_deselected(items=deselected)
        (self.output / "complete-collection.json").write_text(json.dumps({
            "schema": "imagejxx.complete-collection.001", "platform": sys.platform, "collected": collected,
            "selected": self.selected, "excluded": excluded}, indent=2) + "\n", encoding="utf-8")

    def pytest_runtest_logstart(self, nodeid, location):
        (self.output / "active-test.json").write_text(json.dumps({
            "state": "running", "id": nodeid, "started": time.time()}) + "\n", encoding="utf-8")

    def pytest_runtest_logreport(self, report):
        row = {"id": report.nodeid, "phase": report.when,
               "outcome": report.outcome, "duration": report.duration}
        self.phases.append(row)
        with (self.output / "test-phases.jsonl").open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(row) + "\n")

    def pytest_runtest_logfinish(self, nodeid, location):
        (self.output / "active-test.json").write_text(json.dumps({
            "state": "finished", "id": nodeid, "ended": time.time()}) + "\n", encoding="utf-8")


class RoutedCompleteCollection(CompleteCollection):
    def pytest_collection_modifyitems(self, config, items):
        collected = [item.nodeid for item in items]
        self.selected, excluded, routed = routed_test_selection(collected)
        execute = set(self.selected)
        transferred = [item for item in items if item.nodeid not in execute]
        items[:] = [item for item in items if item.nodeid in execute]
        config.hook.pytest_deselected(items=transferred)
        (self.output / "complete-collection.json").write_bytes((json.dumps({
            "schema": "imagejxx.complete-collection.002", "platform": sys.platform,
            "collected": collected, "selected": self.selected, "excluded": excluded,
            "routed": routed}, indent=2) + "\n").encode("utf-8"))


class HeadlessCollection(CompleteCollection):
    def __init__(self, output, expected_id):
        super().__init__(output)
        if expected_id not in HEADLESS_TEST_IDS:
            raise ValueError("Unknown isolated headless ID")
        self.expected_id = expected_id

    def pytest_collection_modifyitems(self, config, items):
        if [item.nodeid for item in items] != [self.expected_id]:
            raise ValueError("Headless collection differs from its literal original ID")
        self.selected = [self.expected_id]
        (self.output / "complete-collection.json").write_bytes((json.dumps({
            "schema": "imagejxx.headless-collection.001", "platform": sys.platform,
            "collected": self.selected, "selected": self.selected, "excluded": []}, indent=2) + "\n").encode("utf-8"))


def complete_pytest_child(output, root, *, routed=False):
    if sys.flags.optimize:
        raise ValueError("Complete Python qualification requires enabled assertions")
    unexpected = [name for name in ("PYTHONOPTIMIZE", "PYTEST_ADDOPTS", "PYTEST_PLUGINS")
                  if os.environ.get(name)]
    if unexpected:
        raise ValueError("Complete Python child refuses inherited test overrides: " + ", ".join(unexpected))
    os.environ["PYTEST_DISABLE_PLUGIN_AUTOLOAD"] = "1"
    os.chdir(root)
    import pytest
    collector = RoutedCompleteCollection(output) if routed else CompleteCollection(output)
    return pytest.main(["tests/python", "--tb=short", "-p", "no:cacheprovider",
        "--basetemp", str(output / "pytest-temp"), "--junitxml", str(output / "pytest.xml")],
        plugins=[collector])


def headless_pytest_child(output, root, expected_id, *, dependency_site=None, install_manifest=None):
    if expected_id not in HEADLESS_TEST_IDS or sys.flags.optimize:
        raise ValueError("Explicit original headless ID and enabled assertions are required")
    if any(os.environ.get(name) for name in ("PYTHONOPTIMIZE", "PYTEST_ADDOPTS", "PYTEST_PLUGINS")):
        raise ValueError("Headless child refuses inherited test overrides")
    if "IMAGEJXX_SHORTCUTS_PYTHON_OBSERVATIONS" in os.environ:
        raise ValueError("Optional shortcut observation mode requires a separate routing contract")
    os.environ["PYTEST_DISABLE_PLUGIN_AUTOLOAD"] = "1"
    root = Path(root).resolve(strict=True)
    output = Path(output).resolve()
    os.chdir(root)
    receipt_path = output / "headless-receipt.json"
    if receipt_path.exists():
        raise ValueError("Use a fresh isolated headless child output")
    row = {"state": "running", "id": expected_id, "acceptance_claim": False,
           "scope": "one original installed headless test; not full product acceptance"}
    def save():
        receipt_path.write_bytes((json.dumps(row, indent=2) + "\n").encode("utf-8"))
    save()
    try:
        helper = _source_module(
            "ci_headless_origins", Path(__file__).with_name("ci_headless_origins.py"))
        sources = [Path(__file__), Path(__file__).with_name("ci_headless_origins.py"),
                   Path(root) / expected_id.split("::", 1)[0],
                   Path(root) / "tests/python/shortcut_api_observations.py"]
        row["sources_before"] = [pin(path) for path in sources]
        manifest_before = None
        if install_manifest is None:
            original_distribution = importlib.metadata.distribution("imagejxx")
            primary_package = Path(original_distribution.locate_file("imagejxx")).resolve(strict=True)
        else:
            # CMake installation is a separate explicit local delivery profile.
            # Its primary runtime package never comes from the dependency site.
            primary_package = (Path(sysconfig.get_path("purelib")) / "imagejxx").resolve(strict=True)
            manifest_before = pin(install_manifest)
            row["install_manifest"] = manifest_before
        if dependency_site is not None:
            site = Path(dependency_site).resolve(strict=True)
            if not site.is_dir() or site.is_relative_to(root / "src"):
                raise ValueError("An existing dependency site outside product source is required")
            # Append only: the primary runtime must already own the product.
            sys.path.append(str(site))
            row["dependency_site"] = str(site)
        package = importlib.import_module("imagejxx")
        if Path(package.__file__).resolve().parent != primary_package:
            raise ValueError("Dependency site substituted the primary installed product")
        native = importlib.import_module("imagejxx._native")
        numpy = importlib.import_module("numpy")
        origins = helper.prepare(output / "origins", package, native, numpy, root,
                                 install_manifest=install_manifest)
        os.environ.update(origins)
        row["origins"] = {name: pin(path) for name, path in origins.items()}
        save()
        import pytest
        if pytest.__version__ != "8.4.2":
            raise ValueError("Headless test dependency must be the reviewed pytest 8.4.2")
        if dependency_site is not None and not Path(pytest.__file__).resolve().is_relative_to(site):
            raise ValueError("Pytest did not load from the explicit existing dependency site")
        row["pytest"] = pin(pytest.__file__)
        code = int(pytest.main([expected_id, "--tb=short", "-p", "no:cacheprovider",
            "--basetemp", str(output / "pytest-temp"), "--junitxml", str(output / "pytest.xml")],
            plugins=[HeadlessCollection(output, expected_id)]))
        row["exit_code"] = code
        helper._headless_modules(package, native, numpy)
        helper._load_validator(Path(root)).origins(
            Path(origins["IMAGEJXX_SHORTCUTS_PYTHON_ORIGINS"]), "headless", loaded=True)
        if manifest_before is not None and pin(install_manifest) != manifest_before:
            raise ValueError("CMake installation manifest changed during headless execution")
        row["sources_after"] = [pin(path) for path in sources]
        if row["sources_after"] != row["sources_before"]:
            raise ValueError("Headless source/observer inputs changed during execution")
        if code:
            row["state"] = "failed"
        else:
            collection = json.loads((output / "complete-collection.json").read_bytes())
            phases = [json.loads(line) for line in (output / "test-phases.jsonl").read_text().splitlines()]
            validate_headless_results(collection, ET.parse(output / "pytest.xml").getroot().findall(".//testcase"),
                                      phases, expected_id)
            row["state"] = "passed"
        return code
    except BaseException as error:
        row.update(state="failed", error={"type": type(error).__name__, "message": str(error)})
        raise
    finally:
        save()


def run_headless_tests(output, root, environment, *, deadline=None, on_start=None, dependency_site=None, install_manifest=None):
    """Five fresh children; each <=30s within the one complete-reader deadline."""
    if "IMAGEJXX_SHORTCUTS_PYTHON_OBSERVATIONS" in environment:
        raise ValueError("Optional shortcut observations are outside the five-ID routing contract")
    root = Path(root).resolve(strict=True)
    output = Path(output).resolve()
    if dependency_site is not None:
        dependency_site = Path(dependency_site).resolve(strict=True)
    if install_manifest is not None:
        install_manifest = Path(install_manifest).resolve(strict=True)
    if output.exists():
        raise ValueError("Use a fresh headless suite output")
    output.mkdir(parents=True)
    deadline = time.monotonic() + 2400 if deadline is None else deadline
    if type(deadline) not in (int, float) or not math.isfinite(deadline):
        raise ValueError("Headless suite requires a finite absolute reader deadline")
    receipt = {"schema": "imagejxx.headless-suite.001", "state": "running",
               "acceptance_claim": False, "ids": list(HEADLESS_TEST_IDS), "cases": []}
    def save():
        (output / "receipt.json").write_bytes((json.dumps(receipt, indent=2) + "\n").encode("utf-8"))
    save()
    reports = []
    try:
        for index, expected_id in enumerate(HEADLESS_TEST_IDS, 1):
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("Complete reader deadline exhausted before isolated headless child")
            directory = output / f"case-{index:02d}"
            directory.mkdir()
            argv = [sys.executable, "-I", "-X", "utf8", "-B", str(Path(__file__).resolve()),
                    "--headless-pytest-child", str(directory), "--repository", str(root),
                    "--headless-test-id", expected_id]
            if dependency_site is not None:
                argv.extend(["--headless-dependency-site", str(dependency_site)])
            if install_manifest is not None:
                argv.extend(["--headless-install-manifest", str(install_manifest)])
            item = {"id": expected_id, "state": "running", "argv": argv,
                    "timeout_seconds": min(30, remaining), "directory": directory.name}
            receipt["cases"].append(item)
            save()
            cancel_admission = on_start(directory / "process.json") if on_start is not None else None
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                item["state"] = "not_launched_deadline"
                if callable(cancel_admission):
                    cancel_admission()
                raise TimeoutError("Complete reader deadline exhausted during isolated admission")
            item["timeout_seconds"] = min(30, remaining)
            process = complete_command(argv, output=directory, root=root, environment=environment,
                                       timeout=item["timeout_seconds"])
            item["process"] = pin(directory / "process.json")
            if process["exit_code"] != 0:
                raise subprocess.CalledProcessError(process["exit_code"], argv)
            child = json.loads((directory / "headless-receipt.json").read_bytes())
            if child.get("state") != "passed" or child.get("id") != expected_id:
                raise ValueError("Headless child did not finish its original required test")
            report = {"id": expected_id,
                      "collection": json.loads((directory / "complete-collection.json").read_bytes()),
                      "junit_rows": ET.parse(directory / "pytest.xml").getroot().findall(".//testcase"),
                      "phases": [json.loads(line) for line in (directory / "test-phases.jsonl").read_text().splitlines()],
                      "process": json.loads((directory / "process.json").read_bytes())}
            validate_headless_results(report["collection"], report["junit_rows"], report["phases"], expected_id)
            proof = report["process"]
            if (proof.get("schema") != "imagejxx.owned-command.001" or proof.get("state") != "exited"
                    or type(proof.get("exit_code")) is not int or proof.get("exit_code") != 0 or proof.get("target_completed") is not True
                    or proof.get("cleanup_complete") is not True):
                raise ValueError("Headless child has no successful resolved owned exit")
            for name in ("headless-receipt.json", "complete-collection.json", "pytest.xml", "test-phases.jsonl"):
                item[name] = pin(directory / name)
            item["state"] = "passed"
            reports.append(report)
            save()
        receipt["state"] = "passed"
        return reports
    except BaseException as error:
        receipt.update(state="failed", error={"type": type(error).__name__, "message": str(error)})
        if receipt["cases"] and receipt["cases"][-1]["state"] == "running":
            receipt["cases"][-1]["state"] = "failed"
        raise
    finally:
        save()


def retain_headless_output(output):
    """Snapshot stopped original headless outputs; preserve explicit alias omissions.

    The caller must first resolve every started reader process. Required payloads
    are never selected by suffix: the existing retainer admits every regular file.
    """
    output = Path(output).resolve()
    helper = _source_module(
        "ci_headless_output_retention", Path(__file__).with_name("ci_python_diagnostics.py"))
    result = helper.retain_python_diagnostics(output, Path(str(output) + ".zip"), selection="headless-tests")
    allowed_cases = {f"case-{index:02d}" for index in range(1, 5)}
    for omission in result["omitted"]:
        parts = omission["path"].split("/")
        if (omission["reason"] != "linked_path" or len(parts) != 3
                or parts[0] not in allowed_cases or parts[1] != "pytest-temp"
                or not parts[2].endswith("current")):
            raise ValueError("Headless output retention omitted a required payload or unexpected entry")
    if (result["state"] not in {"complete", "incomplete"} or not result.get("archive")
            or result["matched_files"] != len(result["members"])
            or result["discovery"]["complete"] != (not result["omitted"])):
        raise ValueError("Headless output did not retain every bounded regular payload")
    return result


INSTALLED_SMOKE_IDS = (
    "tests/python/test_scientific_tiff_io.py::test_independent_carriers_and_admission[defaults.tif]",
    "tests/python/test_settings.py::test_settings_in_hidden_opengl_and_recreated_context",
)


def complete_command(argv, *, output, root, environment, timeout):
    """The complete lane owns command descendants before reading diagnostics.

    Load this seam only for the complete lane; focused profiles retain their
    original protocol. The Darwin backend requires registered first-party UI
    spawns and never falls back to a raw subprocess.
    """
    name = "ci_mac_process" if sys.platform == "darwin" else "ci_owned_process"
    owner = _source_module(name, Path(__file__).with_name(name + ".py"))
    execute = owner.run_mac_owned if sys.platform == "darwin" else owner.run_owned
    if sys.platform == "darwin":
        environment = dict(environment, IMAGEJXX_REQUIRE_MAC_PROCESS_OWNER="1")
    with (output / "pytest.log").open("xb") as log:
        row = execute(argv, cwd=root, env=environment, timeout=timeout,
                      stdout=log, stderr=subprocess.STDOUT,
                      receipt_path=output / "process.json")
    if not row.get("cleanup_complete") or row.get("state") != "exited":
        raise ValueError("Complete command has no resolved terminal process ownership")
    return row


def failed_python_diagnostics(output):
    """Retain bounded diagnostics only after a terminal owned-process receipt."""
    diagnostics = _source_module(
        "ci_python_diagnostics", Path(__file__).with_name("ci_python_diagnostics.py"))
    result = {}
    candidates = [("installed-smoke", output / "installed-smoke"), ("complete", output)]
    candidates.extend((f"headless-{index:02d}", output / "headless" / f"case-{index:02d}")
                      for index in range(1, len(HEADLESS_TEST_IDS) + 1)
                      if (output / "headless" / f"case-{index:02d}").is_dir())
    for name, directory in candidates:
        process = directory / "process.json"
        try:
            proof = json.loads(process.read_bytes())
            if (proof.get("schema") != "imagejxx.owned-command.001"
                    or proof.get("cleanup_complete") is not True
                    or proof.get("state") in {"starting", "running"}):
                result[name] = {"state": "unavailable", "reason": "process_cleanup_unresolved"}
                continue
            result[name] = diagnostics.retain_python_diagnostics(
                directory / "pytest-temp", output / (name + "-python-diagnostics.zip"))
        except FileNotFoundError:
            result[name] = {"state": "unavailable", "reason": "process_report_missing"}
        except Exception as error:
            result[name] = {"state": "retention_failed", "error": {
                "type": type(error).__name__, "message": str(error)}}
    return result


def installed_smoke(output, root, environment):
    """Exercise the required codec and real hidden-GL routes before full capture."""
    output.mkdir()
    argv = [sys.executable, "-I", "-X", "utf8", "-B", "-m", "pytest", *INSTALLED_SMOKE_IDS,
            "--tb=short", "-p", "no:cacheprovider", "--basetemp", str(output / "pytest-temp"),
            "--junitxml", str(output / "pytest.xml")]
    row = {"state": "running", "argv": argv, "selected": list(INSTALLED_SMOKE_IDS)}
    receipt = output / "receipt.json"
    def save():
        receipt.write_bytes((json.dumps(row, indent=2) + "\n").encode("utf-8"))
    save()
    try:
        result = complete_command(argv, output=output, root=root, environment=environment, timeout=120)
        row.update(exit_code=result["exit_code"], process=pin(output / "process.json"))
        if result["exit_code"]:
            raise subprocess.CalledProcessError(result["exit_code"], argv)
        report = output / "pytest.xml"
        cases = ET.parse(report).getroot().findall(".//testcase")
        if ([(case.get("classname"), case.get("name")) for case in cases]
                != [junit_identity(name) for name in INSTALLED_SMOKE_IDS]
                or any(case.find(tag) is not None for case in cases
                       for tag in ("failure", "error", "skipped"))):
            raise ValueError("Installed codec/render smoke requires both exact passing cases")
        row.update(state="passed", junit=pin(report))
    except BaseException as error:
        row.update(state="failed", error={"type": type(error).__name__, "message": str(error)})
        raise
    finally:
        save()
    return pin(receipt)


def audit_static_codecs(path):
    """Selected static C archives must not export interposable codec APIs."""
    symbols = subprocess.check_output(["nm", "-D", "--defined-only", str(path)], text=True)
    forbidden = re.compile(r"^(TIFF|_TIFF|png_|jpeg_|lzma_|glfw|_glfw|"
        r"zlibVersion$|zlibCompileFlags$|zError$|deflate|inflate|adler32|crc32|get_crc_table$)")
    exported = [line.split()[-1] for line in symbols.splitlines() if line.split()]
    leaked = [name for name in exported if forbidden.match(name)]
    if leaked:
        raise ValueError(f"Static codec symbols are exposed by {path}: {leaked}")
    dynamic = subprocess.check_output(["readelf", "-d", str(path)], text=True)
    needed = re.findall(r"\(NEEDED\).*?\[([^\]]+)\]", dynamic)
    codecs = [name for name in needed if name.startswith(
        ("libtiff.", "libjpeg.", "liblzma.", "libpng", "libglfw.", "libz."))]
    if codecs:
        raise ValueError(f"Static CI profile linked shared codec libraries: {codecs}")
    return {"state": "passed", "defined_symbols": len(exported),
            "exposed_codec_symbols": 0, "direct_dependencies": needed}


def current_capture_inventory(output):
    """Bound the entire stopped output, including root logs and failed captures."""
    helper = _source_module(
        "ci_current_observation_inventory", Path(__file__).with_name("ci_current_observations.py"))
    return helper.inventory(output)


def current_producer_cache(executable, root):
    """Admit the hosted single-config cache, not a claim of build execution."""
    root = Path(root).resolve(strict=True)
    wheel = root / "build/wheel"
    parents = [parent for parent in executable.parents if parent.is_relative_to(wheel)]
    candidates = [parent / "CMakeCache.txt" for parent in parents if (parent / "CMakeCache.txt").is_file()]
    if len(candidates) != 1 or candidates[0].is_symlink():
        raise ValueError("RGB producer requires one regular containing primary CMake cache")
    cache = candidates[0]
    if cache.stat().st_size > 4 * 1024 * 1024:
        raise ValueError("RGB producer cache exceeds its metadata bound")
    validated_pin = pin(cache)
    values = {}
    for line in cache.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith(("#", "//")) or "=" not in line or ":" not in line.split("=", 1)[0]:
            continue
        key, value = line.split("=", 1)
        key = key.split(":", 1)[0]
        if key in values:
            raise ValueError("Duplicate RGB producer cache field: " + key)
        values[key] = value
    expected = {"CMAKE_GENERATOR": "Ninja", "CMAKE_BUILD_TYPE": "Release", "BUILD_TESTING": "ON",
                "IMAGEJXX_BUILD_PYTHON": "ON", "IMAGEJXX_BUILD_IO": "ON",
                "IMAGEJXX_BUILD_CPP_UI": "ON", "IMAGEJXX_BUILD_PYTHON_UI": "ON"}
    if any(values.get(name) != value for name, value in expected.items()):
        raise ValueError("RGB producer cache is not the complete hosted Release profile")
    for name, expected_path in (("CMAKE_HOME_DIRECTORY", root), ("CMAKE_CACHEFILE_DIR", cache.parent)):
        if not values.get(name) or Path(values[name]).resolve(strict=True) != expected_path:
            raise ValueError("RGB producer cache source/build directory differs: " + name)
    if pin(cache) != validated_pin:
        raise ValueError("RGB producer cache changed during configuration admission")
    return cache, expected, validated_pin


def capture_current_observations(output, application, root, environment):
    """Run current producers using the one existing wheel-build native cache."""
    name = "imagejxx_rgb_combined_input_tests" + (".exe" if sys.platform == "win32" else "")
    candidates = list((Path(root) / "build/wheel").rglob(name))
    if len(candidates) != 1:
        raise ValueError(f"Expected one built RGB combined-input producer, found {len(candidates)}")
    executable = candidates[0]
    if executable.is_symlink() or not executable.is_file():
        raise ValueError("RGB combined-input producer must be a regular existing build output")
    cache, configuration, cache_before = current_producer_cache(executable, root)
    helper = _source_module(
        "ci_current_observations", Path(__file__).with_name("ci_current_observations.py"))
    result = helper.capture_current(application, executable, output, environment, root=root)
    cache_after = pin(cache)
    identity = {"scope": "containing hosted cache identity only; not build execution authentication",
                "before": cache_before, "after": cache_after, "required_configuration": configuration}
    with (Path(output) / "producer-cache.json").open("xb") as stream:
        stream.write((json.dumps(identity, indent=2) + "\n").encode("utf-8"))
    if cache_before != cache_after:
        raise ValueError("RGB producer cache changed during current capture")
    names = {"IMAGEJXX_RGB_COMBINED_INPUT_REPORT", "IMAGEJXX_H1_CPP_REPORT", "IMAGEJXX_R16_CPP_REPORT"}
    if (result.get("state") != "captured" or result.get("acceptance_claim") is not False
            or set(result.get("reader_environment", {})) != names):
        raise ValueError("Current producers did not provide their exact original reader paths")
    return result["reader_environment"]


# Original independent readers own these identities. This early gate covers
# known required reference inputs only; it is not exhaustive fixture acceptance.
REQUIRED_REFERENCE_INPUTS = (
    ("IMAGEJXX_GAUSSIAN_UI_CONTRACT_JSON", "IMAGEJXX_GAUSSIAN_UI_CONTRACT_SHA256", None,
     "2652bcf1c0665e997fb1bbe55c7f616071359e6b99293b875b2b8555ff045255", None),
    ("IMAGEJXX_GAUSSIAN_CONTRACT_JSON", "IMAGEJXX_GAUSSIAN_CONTRACT_SHA256", None,
     "3b3f040e01b93642d2b0d3d84db9574f7a9ee525b3a7f29dac7ad21ea03ea6c3", None),
    ("IMAGEJXX_GAUSSIAN_REFERENCE_JSONL", "IMAGEJXX_GAUSSIAN_REFERENCE_SHA256", None,
     "8885c41ded23f51e56b1c42e28245bf4b9320108dacfa09da631e14fbe9bf762", None),
    ("IMAGEJXX_XYZ_NUMERIC_ZOOM_CONTRACT_JSON", None, None,
     "3b4c1840d00a773da7b3e5e7fffba08c2292dce00b395ab0dc18da124137480b", None),
    (None, None, "build/plan-implementation/p05f-axis-plane-export-001/next-range-reference-preparation-001/capture-001/corpus.json",
     "8ddde981534e0ccf55840316485c29c3dbcf61eef03dacee291b83477b4a8347", 7876991),
    (None, None, "build/plan-implementation/p05f-axis-plane-export-001/next-range-reference-preparation-001/formula-verification-001.json",
     "b88c4b86a14bf16f8db84adbfbe99dc1aee454f6ae739d123e4c897cdf79ae79", 15181),
)


def reference_readiness(root, environment):
    """Read-only prerequisite inventory before product imports or bulk capture.

    No fallback, generated replacement, hash migration or skipped test is admitted.
    Successful bytes establish availability only; original readers still execute.
    """
    rows = []
    for name, hash_name, relative, expected, size in REQUIRED_REFERENCE_INPUTS:
        configured = environment.get(name) if name else str(Path(root) / relative)
        row = {"environment": name, "original_path": relative,
               "expected_sha256": expected, "state": "unavailable"}
        rows.append(row)
        if not configured:
            row["reason"] = "required explicit reference path is not configured"
            continue
        row["path"] = str(configured)
        if hash_name and environment.get(hash_name) != expected:
            row.update(state="invalid", reason="required original hash setting is missing or changed")
            continue
        path = Path(configured)
        if not path.is_absolute():
            path = Path(root) / path
        try:
            if path.is_symlink() or not path.is_file():
                row["reason"] = "required original is unavailable as a regular file"
                continue
            before = path.stat()
            if before.st_size > 64 * 1024 * 1024 or (size is not None and before.st_size != size):
                row.update(state="invalid", reason="reference size differs or exceeds the bounded read")
                continue
            observed = pin(path)
            after = path.stat()
            row["observed"] = observed
            if ((before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns)
                    != (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns)):
                row.update(state="invalid", reason="reference changed during inspection")
            elif observed["sha256"] != expected:
                row.update(state="invalid", reason="reference bytes differ from original reader pin")
            else:
                row.update(state="available", path=str(path.absolute()))
        except OSError as error:
            row["reason"] = str(error)
    return {"state": "ready" if all(row["state"] == "available" for row in rows) else "unavailable",
            "scope": "six known required original reference inputs; not exhaustive fixture acceptance",
            "acceptance_claim": False, "inputs": rows}


HISTORY_IDS = tuple(
    "tests/python/test_history_settings_ui.py::test_installed_history_settings[" + case + "-" + context + "]"
    for case in ("HU01", "HU02", "HU03", "HU04") for context in ("imgui_cpp", "imgui_python"))
APPEARANCE_IDS = tuple(
    "tests/python/test_settings_appearance_ui.py::test_installed_settings_appearance[" + case + "-" + context + "]"
    for case in ("appearance-persist", "appearance-reopen") for context in ("imgui_cpp", "imgui_python"))


def _ui_packet(kind):
    if kind == "history":
        return {"title": "History", "prefix": "p06h", "schema": "imagejxx.history-suite.001",
                "cases": ("HU01", "HU02", "HU03", "HU04"), "ids": HISTORY_IDS, "count": "eight",
                "environment": "IMAGEJXX_HISTORY_SETTINGS_UI_CASE_ROOT", "capture_timeout": 90,
                "scope": "original eight paired history UI observations only; full platform/scientific/SDK gates remain required"}
    if kind == "appearance":
        return {"title": "Appearance", "prefix": "c2", "schema": "imagejxx.appearance-suite.001",
                "cases": ("appearance-persist", "appearance-reopen"), "ids": APPEARANCE_IDS, "count": "four",
                "environment": "IMAGEJXX_SETTINGS_APPEARANCE_UI_CASE_ROOT", "capture_timeout": 330,
                "scope": "four paired appearance preferences/GL observations only; full platform/scientific/SDK gates remain required"}
    raise ValueError("Only the fixed history or appearance UI packet is supported")


def history_helper():
    return _source_module(
        "ci_installed_history_capture", Path(__file__).with_name("capture_shortcuts_history_ui.py"))


def history_builder():
    return _source_module(
        "ci_authenticated_history_build", Path(__file__).with_name("ci_history_build.py"))


def history_build_inputs(path, root, *, kind="history"):
    """Reapply the producer's strict contract at the serialized receipt boundary."""
    _ui_packet(kind)
    build = history_builder().verify_receipt(path, root)
    if kind == "appearance":
        helper = history_helper()
        freeze = helper.read_json(Path(helper.verify(build["source_freeze"])["path"]))
        frozen = {r["logical_path"] for r in freeze["sources"]}
        missing = set((helper.COMMON | helper.SPEC[kind]["roles"]).values()) - frozen
        if missing:
            raise ValueError("Appearance build is missing frozen source roles: " + ", ".join(sorted(missing)))
    return build


def history_bindings(build_receipt, application, package, root, *, kind="history"):
    """Join real installed objects to the existing strict capture contract."""
    packet = _ui_packet(kind)
    title = packet["title"]
    helper = history_helper()
    options = {} if kind == "history" else {"kind": kind}
    build = history_build_inputs(build_receipt, root, **options)
    modules = {}
    for name in helper.MODULES:
        module = importlib.import_module(name)
        actual = helper.pin(Path(module.__file__).resolve(strict=True))
        if not Path(actual["path"]).is_relative_to(package):
            raise ValueError(title + " imported a module outside the installed package: " + name)
        modules[name] = actual
    cpp = helper.pin(application)
    for installed, compiled in ((cpp, build["built_products"]["cpp"]),
                               (modules["imagejxx._native"], build["built_products"]["_native"]),
                               (modules["imagejxx._desktop"], build["built_products"]["_desktop"])):
        if any(installed[key] != compiled[key] for key in ("bytes", "sha256")):
            raise ValueError(title + " installed product differs from the authenticated build")
    numpy = importlib.import_module("numpy")
    if numpy.__version__ != "2.2.6":
        raise ValueError(title + " requires the existing pinned NumPy runtime")
    numpy_module = helper.pin(Path(numpy.__file__).resolve(strict=True))
    engine = helper.pin(helper.engine(portable=True))
    return {"schema_version": 1, "kind": packet["prefix"] + ".installed-ui-capture-bindings.001", "kind_selection": kind,
            "build_receipt": helper.pin(build_receipt), "source_freeze": build["source_freeze"],
            "built_products": build["built_products"], "installed_modules": modules, "python_engine": engine,
            "products": {"imgui_cpp": {role: cpp for role in helper.RUNTIME_ROLES},
                         "imgui_python": {"executable": engine, "producer_artifact": modules["imagejxx._desktop"],
                                          "native_artifact": modules["imagejxx._desktop"]}},
            "numpy": {"version": numpy.__version__, "site_packages": str(Path(numpy_module["path"]).parent.parent),
                      "module": numpy_module}}


def validate_history_output(output, root, application, modules, *, kind="history"):
    """Admit the completed current packet without replaying its UI producers."""
    packet = _ui_packet(kind)
    title = packet["title"]
    helper = history_helper()
    output = Path(output).resolve(strict=True)
    row = helper.read_json(output / "receipt.json")
    if row.get("schema") != packet["schema"] or row.get("state") != "passed":
        raise ValueError("Required installed " + kind + " packet has not passed")
    bindings = helper.read_json(Path(helper.verify(row["product_bindings"])["path"]))
    options = {} if kind == "history" else {"kind": kind}
    if kind == "appearance" and (row.get("acceptance_claim") is not False
            or bindings.get("kind_selection") != kind
            or bindings.get("kind") != packet["prefix"] + ".installed-ui-capture-bindings.001"):
        raise ValueError("Appearance packet binding contract differs")
    build = history_build_inputs(Path(helper.verify(bindings["build_receipt"])["path"]), root, **options)
    if bindings["source_freeze"] != build["source_freeze"] or bindings["built_products"] != build["built_products"]:
        raise ValueError(title + " bindings differ from the original build")
    for value in bindings["installed_modules"].values():
        helper.verify(value)
    if (bindings["products"]["imgui_cpp"]["executable"] != helper.pin(application)
            or not {"imagejxx._native", "imagejxx._desktop"} <= set(modules)
            or any(any(bindings["installed_modules"][name][key] != modules[name][key]
                       for key in ("path", "bytes", "sha256"))
                   for name in modules if name in bindings["installed_modules"])):
        raise ValueError(title + " packet belongs to different installed products")
    capture_path = Path(helper.verify(row["capture"])["path"])
    if capture_path != output / "capture/capture.json":
        raise ValueError(title + " capture is outside its owning packet")
    capture_row = helper.read_json(capture_path)
    expected = [(case, context) for case in packet["cases"]
                for context in ("imgui_cpp", "imgui_python")]
    if kind == "appearance" and (capture_row.get("kind_selection") != kind
                                or capture_row.get("acceptance_claim") is not False):
        raise ValueError("Appearance capture kind or acceptance boundary differs")
    if (capture_row.get("state") != "passed"
            or [(case["id"], case["context"]) for case in capture_row["cases"]] != expected
            or capture_row["inputs"]["product_bindings"] != row["product_bindings"]):
        raise ValueError(title + " capture lacks the original complete paired membership")
    for case in capture_row["cases"]:
        helper.verify(case["raw"])
        if case["context"] == "imgui_python":
            helper.verify(case["launcher_origin"])
    for process in capture_row["processes"]:
        proof = helper.read_json(Path(helper.verify(process["owned_process_receipt"])["path"]))
        if (proof.get("schema") != "imagejxx.owned-command.001" or proof.get("state") != "exited"
                or proof.get("exit_code") != 0 or proof.get("target_completed") is not True
                or proof.get("cleanup_complete") is not True):
            raise ValueError(title + " producer ownership is unresolved")
    if len(capture_row["processes"]) != len(packet["ids"]):
        raise ValueError(title + " requires " + packet["count"] + " resolved producers")
    helper.verify(row["provenance"])
    cases = ET.parse(helper.verify(row["reader_junit"])["path"]).getroot().findall(".//testcase")
    if ([(c.get("classname"), c.get("name")) for c in cases] != [junit_identity(n) for n in packet["ids"]]
            or any(c.find(tag) is not None for c in cases for tag in ("failure", "error", "skipped"))):
        raise ValueError(title + " requires the unchanged " + packet["count"] + " passing reader identities")
    for name in ("capture_process", "reader_process"):
        proof = helper.read_json(Path(helper.verify(row[name])["path"]))
        if (proof.get("schema") != "imagejxx.owned-command.001" or proof.get("state") != "exited"
                or proof.get("exit_code") != 0 or proof.get("target_completed") is not True
                or proof.get("cleanup_complete") is not True):
            raise ValueError(title + " packet process ownership is unresolved")
    return {packet["environment"]: str(output / "capture")}


def run_history_suite(output, root, build_receipt, *, kind="history"):
    """Independent bounded packet; it cannot pass or replace the full suite."""
    packet = _ui_packet(kind)
    title = packet["title"]
    options = {} if kind == "history" else {"kind": kind}
    root, output = Path(root).resolve(strict=True), Path(output).resolve()
    if output.exists() or not output.is_relative_to(root) or output == root:
        raise ValueError(title + " suite needs a fresh owned repository output")
    output.mkdir(parents=True)
    row = {"schema": packet["schema"], "state": "running", "acceptance_claim": False,
           "started_stages": [],
           "scope": packet["scope"]}
    helper = history_helper()
    def save():
        (output / "receipt.json").write_text(json.dumps(row, indent=2) + "\n", encoding="utf-8")
    save()
    try:
        if sys.flags.optimize or any(os.environ.get(n) for n in ("PYTHONOPTIMIZE", "PYTEST_ADDOPTS", "PYTEST_PLUGINS")):
            raise ValueError(title + " suite requires the original assertion and pytest profile")
        distribution = importlib.metadata.distribution("imagejxx")
        package = Path(distribution.locate_file("imagejxx")).resolve(strict=True)
        application = installed_application(distribution)
        bindings = history_bindings(build_receipt, application, package, root, **options)
        bindings_path = output / "product-bindings.json"
        helper.write(bindings_path, bindings)
        row["product_bindings"] = helper.pin(bindings_path)
        environment = os.environ.copy()
        environment.update(IMAGEJXX_RENDER_TESTS="1", IMAGEJXX_REQUIRE_RENDER_TESTS="1",
                           PYTHONDONTWRITEBYTECODE="1", PYTEST_DISABLE_PLUGIN_AUTOLOAD="1")
        for name in ("IMAGEJXX_MAC_BROKER_SOCKET", "IMAGEJXX_MAC_BROKER_TOKEN", "IMAGEJXX_REQUIRE_MAC_PROCESS_OWNER"):
            environment.pop(name, None)
        environment.pop(packet["environment"], None)
        row["installed_smoke"] = installed_smoke(output / "installed-smoke", root, environment)
        command = output / "capture-owner"
        command.mkdir()
        argv = [sys.executable, "-I", "-X", "utf8", "-B", str(root / "scripts/capture_shortcuts_history_ui.py"),
                "--kind", kind, "--output", str(output / "capture"), "--cpp-prefix", str(application.parent.parent),
                "--python-site-packages", str(package.parent), "--numpy-site-packages", bindings["numpy"]["site_packages"],
                "--build-receipt", str(build_receipt), "--source-freeze", bindings["source_freeze"]["path"],
                "--product-bindings", str(bindings_path)]
        row["started_stages"].append("capture-owner")
        save()
        proof = complete_command(argv, output=command, root=root, environment=environment, timeout=packet["capture_timeout"])
        row["capture_process"] = pin(command / "process.json")
        if proof["exit_code"] != 0 or not proof["target_completed"] or not proof["cleanup_complete"]:
            raise ValueError("Installed paired " + kind + " capture failed")
        row["capture"] = pin(output / "capture/capture.json")
        row["provenance"] = pin(output / "capture/provenance.json")
        environment[packet["environment"]] = str(output / "capture")
        command = output / "reader"
        command.mkdir()
        argv = [sys.executable, "-I", "-X", "utf8", "-B", "-m", "pytest", *packet["ids"],
                "--tb=short", "-p", "no:cacheprovider", "--basetemp", str(command / "pytest-temp"),
                "--junitxml", str(command / "pytest.xml")]
        row["started_stages"].append("reader")
        save()
        proof = complete_command(argv, output=command, root=root, environment=environment, timeout=120)
        row["reader_process"] = pin(command / "process.json")
        row["reader_junit"] = pin(command / "pytest.xml")
        if proof["exit_code"] != 0 or not proof["target_completed"] or not proof["cleanup_complete"]:
            raise ValueError("Original " + kind + " reader failed")
        row["state"] = "passed"
        save()
        validate_history_output(output, root, application, bindings["installed_modules"], **options)
    except BaseException as error:
        row.update(state="failed", error={"type": type(error).__name__, "message": str(error)})
        raise
    finally:
        pending_error = sys.exc_info()[1]
        save()
        capture_root = output / "capture"
        if capture_root.exists():
            try:
                proofs = [helper.read_json(output / name / "process.json") for name in row["started_stages"]]
                captures = helper.read_json(capture_root / "capture.json")
                if (proofs and all(p.get("schema") == "imagejxx.owned-command.001"
                                   and p.get("state") not in {"starting", "running"}
                                   and p.get("cleanup_complete") is True for p in proofs) and all(
                        helper.read_json(Path(helper.verify(p["owned_process_receipt"])["path"])).get("cleanup_complete") is True
                        for p in captures["processes"])):
                    row["capture_archive"] = archive(capture_root)
                    save()
                else:
                    row["capture_archive_unavailable"] = "capture/reader ownership unresolved"
                    save()
            except BaseException as error:
                row.update(state="failed", retention_error={"type": type(error).__name__, "message": str(error)})
                save()
                if pending_error is None:
                    raise


def run(output: Path, root: Path, *, history_output=None, appearance_output=None):
    output = output.resolve()
    if output.exists():
        raise ValueError(f"Use a fresh Python CI output directory: {output}")
    output.mkdir(parents=True)
    receipt = {"state": "running", "profile": "complete-installed-python-scientific-desktop",
               "python": sys.executable, "modules": {}, "environment_contracts": [],
               "complete_reader_started": False, "reader_processes": []}

    def save():
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")

    save()
    try:
        if sys.flags.optimize:
            raise ValueError("Complete Python qualification requires enabled assertions")
        if "IMAGEJXX_SHORTCUTS_PYTHON_OBSERVATIONS" in os.environ:
            raise ValueError("Optional shortcut observation mode requires a separate routing contract")
        receipt["reference_readiness"] = reference_readiness(root, os.environ)
        save()
        if receipt["reference_readiness"]["state"] != "ready":
            raise ValueError("Complete Python reference prerequisites are unavailable; see reference_readiness")
        distribution = importlib.metadata.distribution("imagejxx")
        expected_package = Path(distribution.locate_file("imagejxx")).resolve(strict=True)
        package = importlib.import_module("imagejxx")
        if Path(package.__file__).resolve().parent != expected_package:
            raise ValueError("Python tests must import the installed imagejxx package")
        for name in ("imagejxx._native", "imagejxx._scientific_io", "imagejxx._desktop"):
            module = importlib.import_module(name)
            if Path(module.__file__).resolve().parent != expected_package:
                raise ValueError(f"Native module is outside the installed package: {name}")
            receipt["modules"][name] = pin(module.__file__)
            if sys.platform == "linux" and name != "imagejxx._native":
                receipt["modules"][name]["static_codec_audit"] = audit_static_codecs(module.__file__)
        environment = os.environ.copy()
        for name in ("PYTHONOPTIMIZE", "PYTEST_ADDOPTS", "PYTEST_PLUGINS",
                     "IMAGEJXX_MAC_BROKER_SOCKET", "IMAGEJXX_MAC_BROKER_TOKEN",
                     "IMAGEJXX_REQUIRE_MAC_PROCESS_OWNER", "IMAGEJXX_SETTINGS_APPEARANCE_UI_CASE_ROOT"):
            environment.pop(name, None)
        environment.update(IMAGEJXX_RENDER_TESTS="1", IMAGEJXX_REQUIRE_RENDER_TESTS="1",
                           PYTHONDONTWRITEBYTECODE="1", PYTEST_DISABLE_PLUGIN_AUTOLOAD="1")
        environment["IMAGEJXX_DESKTOP_MODULE_SHA256"] = receipt["modules"]["imagejxx._desktop"]["sha256"]
        receipt["python_ui_adapter"] = pin(expected_package / "ui/app.py")
        environment["IMAGEJXX_UI_APP_MODULE_SHA256"] = receipt["python_ui_adapter"]["sha256"]
        suffix = sysconfig.get_config_var("EXT_SUFFIX")
        providers = list((root / "build/wheel").rglob("_imagejxx_test_saved_roi_source" + suffix))
        if len(providers) != 1:
            raise ValueError(f"Expected one built test-only saved-ROI provider, found {len(providers)}")
        environment["IMAGEJXX_SAVED_ROI_PROVIDER"] = str(providers[0].resolve())
        receipt["test_provider"] = pin(providers[0])
        provider_spec = importlib.util.spec_from_file_location(
            "_imagejxx_test_saved_roi_source", providers[0])
        provider = importlib.util.module_from_spec(provider_spec)
        provider_spec.loader.exec_module(provider)
        sizes = provider.abi_sizes()
        if (type(sizes) is not dict or set(sizes) != {"image", "shared_const_image"}
                or any(type(value) is not int or not 1 <= value <= 9999
                       for value in sizes.values())):
            raise ValueError("Invalid independent native ABI measurements")
        receipt["native_abi_sizes"] = sizes
        abi = f"{sizes['image']},{sizes['shared_const_image']}"
        environment["IMAGEJXX_AXIS_RANGE_SPACING_ABI_SIZES"] = abi
        environment["IMAGEJXX_AXIS_RANGE_SLICER_RESIZE_ABI_SIZES"] = abi
        application = installed_application(distribution)
        receipt["cpp_application"] = pin(application)
        environment["IMAGEJXX_CPP_EXE"] = str(application)
        environment["IMAGEJXX_CPP_EXE_SHA256"] = receipt["cpp_application"]["sha256"]
        if sys.platform == "win32":
            environment["IMAGEJXX_REVIEWED_WINDOWS_TIFF_PREFIX"] = str(root / "tests/fixtures/windows-tiff-runtime-reviewed")
        receipt["installed_smoke"] = installed_smoke(output / "installed-smoke", root, environment)
        save()
        if history_output is not None:
            environment.update(validate_history_output(history_output, root, application, receipt["modules"]))
            receipt["history_packet"] = pin(Path(history_output) / "receipt.json")
        else:
            receipt["history_packet"] = {"state": "unavailable", "reason": "authentic paired history packet not supplied; original readers remain required"}
        if appearance_output is not None:
            environment.update(validate_history_output(appearance_output, root, application, receipt["modules"], kind="appearance"))
            receipt["appearance_packet"] = pin(Path(appearance_output) / "receipt.json")
        else:
            receipt["appearance_packet"] = {"state": "unavailable", "reason": "qualified paired appearance packet not supplied; appearance readers remain required"}
        environment.update(capture_current_observations(output / "current-observations", application, root, environment))
        receipt["current_observations"] = pin(output / "current-observations/receipt.json")
        save()
        environment.update(capture(output / "cpp-observations", application, root, environment))
        receipt["cpp_observations"] = str(output / "cpp-observations/receipt.json")
        tests = sorted((root / "tests/python").glob("test_*.py"))
        if not tests:
            raise ValueError("No Python test modules found")
        for path in tests:
            tree = ast.parse(path.read_text(encoding="utf-8-sig"))
            names = sorted({node.value for node in ast.walk(tree)
                if isinstance(node, ast.Constant) and isinstance(node.value, str)
                and node.value.startswith("IMAGEJXX_")})
            receipt["environment_contracts"].append({"module": path.relative_to(root).as_posix(),
                "named_contracts": [{"name": name, "configured": name in environment} for name in names]})
        receipt["test_modules"] = len(tests)
        reader_deadline = time.monotonic() + 2400
        def reader_started(path):
            receipt["reader_processes"].append(str(path))
            save()
            def cancel_admission():
                receipt["reader_processes"].remove(str(path))
                receipt.setdefault("reader_admission_refusals", []).append({
                    "path": str(path), "reason": "absolute deadline elapsed before launch"})
                if path == output / "process.json":
                    receipt["complete_reader_started"] = False
                save()
            return cancel_admission
        headless_reports = run_headless_tests(output / "headless", root, environment,
                                             deadline=reader_deadline, on_start=reader_started)
        receipt["headless_tests"] = pin(output / "headless/receipt.json")
        argv = [sys.executable, "-I", "-X", "utf8", "-B", str(Path(__file__).resolve()),
                "--routed-complete-pytest-child", str(output), "--repository", str(root)]
        remaining = reader_deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("Complete reader deadline exhausted before broad cohort")
        receipt["argv"] = argv
        receipt["complete_reader_started"] = True
        cancel_admission = reader_started(output / "process.json")
        remaining = reader_deadline - time.monotonic()
        if remaining <= 0:
            cancel_admission()
            raise TimeoutError("Complete reader deadline exhausted during broad admission")
        result = complete_command(argv, output=output, root=root, environment=environment, timeout=remaining)
        receipt.update(exit_code=result["exit_code"], process=pin(output / "process.json"))
        report = output / "pytest.xml"
        if report.exists():
            cases = ET.parse(report).getroot().findall(".//testcase")
            receipt["counts"] = {"cases": len(cases), **{tag: sum(case.find(tag) is not None for case in cases)
                for tag in ("failure", "error", "skipped")}}
        if result["exit_code"]:
            raise subprocess.CalledProcessError(result["exit_code"], argv)
        counts = receipt.get("counts", {})
        if not counts.get("cases") or any(counts[tag] for tag in ("failure", "error", "skipped")):
            raise ValueError("Complete Python acceptance requires nonempty tests with no failures, errors or skips")
        collection = json.loads((output / "complete-collection.json").read_bytes())
        phases = [json.loads(line) for line in (output / "test-phases.jsonl").read_text().splitlines()]
        validate_routed_results(collection, cases, phases, headless_reports)
        receipt["broad_counts"] = dict(receipt["counts"])
        receipt["counts"]["cases"] += len(HEADLESS_TEST_IDS)
        receipt["collection"] = pin(output / "complete-collection.json")
        receipt["test_phases"] = pin(output / "test-phases.jsonl")
        receipt["state"] = "passed"
    except BaseException as error:
        receipt.update(state="failed", error={"type": type(error).__name__, "message": str(error)})
        raise
    finally:
        if receipt["state"] != "passed":
            try:
                receipt["python_diagnostics"] = failed_python_diagnostics(output)
            except Exception as error:
                receipt["python_diagnostics"] = {"state": "retention_failed", "error": {
                    "type": type(error).__name__, "message": str(error)}}
        pending_error = sys.exc_info()[1]
        retention_failure = None
        readers_resolved = True
        for process_path in receipt["reader_processes"]:
            try:
                path = Path(process_path)
                if not path.resolve().is_relative_to(output):
                    raise ValueError("Reader process proof is outside its owned output")
                proof = json.loads(path.read_bytes())
                resolved = (proof.get("schema") == "imagejxx.owned-command.001"
                    and proof.get("cleanup_complete") is True
                    and proof.get("state") in {"exited", "timed_out", "launch_failed",
                                              "ownership_failed", "supervisor_failed", "unsupported"})
                readers_resolved = readers_resolved and resolved
            except (OSError, ValueError, TypeError):
                readers_resolved = False
        if not readers_resolved:
            receipt["state"] = "failed"
            retention_failure = ValueError("Complete reader processes are unresolved; capture archives withheld")
        headless = output / "headless"
        if headless.is_dir():
            if not readers_resolved:
                receipt["headless_observation_archive"] = {
                    "state": "unavailable", "reason": "reader processes are unresolved"}
            else:
                try:
                    receipt["headless_observation_archive"] = retain_headless_output(headless)
                except Exception as error:
                    receipt["state"] = "failed"
                    receipt["headless_archive_error"] = {"type": type(error).__name__, "message": str(error)}
                    retention_failure = error
        current = output / "current-observations"
        if current.is_dir():
            try:
                evidence = json.loads((current / "receipt.json").read_bytes())
                if (not readers_resolved or evidence.get("state") not in ("captured", "failed")
                        or any(command.get("cleanup_complete") is not True
                               for command in evidence.get("commands", []))):
                    receipt["current_observation_archive"] = {
                        "state": "unavailable", "reason": "owned current-producer processes are unresolved"}
                    receipt["state"] = "failed"
                    retention_failure = ValueError("Current producer/reader processes are unresolved; archive withheld")
                else:
                    members = current_capture_inventory(current)
                    receipt["current_observation_archive"] = archive(current)
                    if current_capture_inventory(current) != members:
                        raise ValueError("Current capture output changed during archival")
            except Exception as error:
                receipt["state"] = "failed"
                receipt["current_observation_archive"] = {
                    "state": "unavailable", "reason": "bounded no-follow inventory or archival failed"}
                receipt["current_archive_error"] = {"type": type(error).__name__, "message": str(error)}
                retention_failure = error
        captures = output / "cpp-observations"
        if captures.is_dir() and not readers_resolved:
            receipt["cpp_observation_archive"] = {
                "state": "unavailable", "reason": "complete reader processes are unresolved"}
        elif captures.is_dir():
            try:
                receipt["cpp_observation_archive"] = archive(captures)
            except Exception as error:
                receipt["state"] = "failed"
                receipt["archive_error"] = {"type": type(error).__name__, "message": str(error)}
                save()
                raise
        save()
        if retention_failure is not None and pending_error is None:
            raise retention_failure


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    selected = parser.add_mutually_exclusive_group()
    selected.add_argument("--profile-manifest", type=Path)
    selected.add_argument("--focused-pytest-child", type=Path, help=argparse.SUPPRESS)
    selected.add_argument("--complete-pytest-child", type=Path, help=argparse.SUPPRESS)
    selected.add_argument("--routed-complete-pytest-child", type=Path, help=argparse.SUPPRESS)
    selected.add_argument("--headless-pytest-child", type=Path, help=argparse.SUPPRESS)
    selected.add_argument("--headless-suite", action="store_true")
    selected.add_argument("--history-suite", action="store_true",
                          help="Qualify only the original paired history UI packet, independently of full-suite reference readiness")
    selected.add_argument("--appearance-suite", action="store_true",
                          help="Qualify only paired appearance preferences/GL observations using the maintained capture path")
    selected.add_argument("--reference-readiness-only", action="store_true",
                          help="Check original reference availability before building; this is not acceptance")
    parser.add_argument("--headless-test-id", help=argparse.SUPPRESS)
    parser.add_argument("--headless-dependency-site", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--headless-install-manifest", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--repository", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--history-build-receipt", type=Path)
    parser.add_argument("--history-output", type=Path)
    parser.add_argument("--appearance-build-receipt", type=Path)
    parser.add_argument("--appearance-output", type=Path)
    args = parser.parse_args()
    if args.history_suite:
        if (args.output is None or args.history_build_receipt is None or args.history_output is not None
                or args.appearance_build_receipt is not None or args.appearance_output is not None):
            parser.error("History suite requires fresh --output and --history-build-receipt")
        run_history_suite(args.output, args.repository or Path(__file__).resolve().parents[1], args.history_build_receipt)
        return
    if args.appearance_suite:
        if (args.output is None or args.appearance_build_receipt is None or args.appearance_output is not None
                or args.history_build_receipt is not None or args.history_output is not None):
            parser.error("Appearance suite requires fresh --output and --appearance-build-receipt")
        run_history_suite(args.output, args.repository or Path(__file__).resolve().parents[1],
                          args.appearance_build_receipt, kind="appearance")
        return
    if args.history_build_receipt is not None:
        parser.error("Build receipts are consumed only by --history-suite")
    if args.appearance_build_receipt is not None:
        parser.error("Appearance build receipts are consumed only by --appearance-suite")
    if args.appearance_output is not None and any((
            args.profile_manifest, args.focused_pytest_child, args.complete_pytest_child,
            args.routed_complete_pytest_child, args.headless_pytest_child, args.headless_suite,
            args.reference_readiness_only)):
        parser.error("--appearance-output is consumed only by the complete installed profile")
    if args.headless_pytest_child:
        if args.output or args.repository is None or args.headless_test_id is None:
            parser.error("Headless child requires only its directory, repository and literal ID")
        raise SystemExit(headless_pytest_child(args.headless_pytest_child, args.repository, args.headless_test_id,
                                             dependency_site=args.headless_dependency_site,
                                             install_manifest=args.headless_install_manifest))
    if args.headless_install_manifest is not None and not args.headless_suite:
        parser.error("CMake installation manifests are allowed only for the isolated headless suite")
    if args.headless_dependency_site is not None and not args.headless_suite:
        parser.error("Explicit dependency sites are allowed only for the isolated headless suite")
    if args.headless_test_id is not None:
        parser.error("Headless test ID is valid only for an isolated child")
    if args.reference_readiness_only:
        if args.output is None:
            parser.error("Reference readiness requires a fresh --output directory")
        repository = (args.repository or Path(__file__).resolve().parents[1]).resolve(strict=True)
        report = reference_readiness(repository, os.environ)
        args.output.mkdir(parents=True, exist_ok=False)
        with (args.output / "reference-readiness.json").open("xb") as stream:
            stream.write((json.dumps(report, indent=2) + "\n").encode("utf-8"))
        raise SystemExit(0 if report["state"] == "ready" else 1)
    if args.headless_suite:
        if args.output is None:
            parser.error("Headless suite requires a fresh --output")
        environment = os.environ.copy()
        for name in ("PYTHONOPTIMIZE", "PYTEST_ADDOPTS", "PYTEST_PLUGINS",
                     "IMAGEJXX_MAC_BROKER_SOCKET", "IMAGEJXX_MAC_BROKER_TOKEN", "IMAGEJXX_REQUIRE_MAC_PROCESS_OWNER"):
            environment.pop(name, None)
        run_headless_tests(args.output, args.repository or Path(__file__).resolve().parents[1], environment,
                           dependency_site=args.headless_dependency_site,
                           install_manifest=args.headless_install_manifest)
        retained = retain_headless_output(args.output)
        retention_path = Path(str(Path(args.output).resolve()) + "-retention.json")
        with retention_path.open("xb") as stream:
            stream.write((json.dumps(retained, indent=2) + "\n").encode("utf-8"))
    elif args.routed_complete_pytest_child:
        if args.output or args.repository is None:
            parser.error("Routed complete child requires only its output and repository")
        raise SystemExit(complete_pytest_child(args.routed_complete_pytest_child, args.repository, routed=True))
    elif args.complete_pytest_child:
        if args.output or args.repository is None:
            parser.error("Complete child requires only its output and repository")
        raise SystemExit(complete_pytest_child(args.complete_pytest_child, args.repository))
    elif args.profile_manifest or args.focused_pytest_child:
        if args.output:
            parser.error("Focused output is declared in the profile, not --output")
        if args.focused_pytest_child:
            raise SystemExit(focused_pytest_child(args.focused_pytest_child))
        run_focused(json.loads(args.profile_manifest.read_bytes()))
    else:
        if args.output is None:
            parser.error("--output is required for the complete installed profile")
        options = {} if args.appearance_output is None else {"appearance_output": args.appearance_output}
        run(args.output, Path(__file__).resolve().parents[1], history_output=args.history_output, **options)


if __name__ == "__main__":
    main()
