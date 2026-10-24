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
import os
from pathlib import Path
import re
import subprocess
import sys
import sysconfig
import time
import xml.etree.ElementTree as ET

_observation_spec = importlib.util.spec_from_file_location(
    "imagejxx_ci_observations", Path(__file__).with_name("ci_observations.py"))
_observations = importlib.util.module_from_spec(_observation_spec)
_observation_spec.loader.exec_module(_observations)
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


def pin(path):
    path = Path(path).resolve(strict=True)
    raw = path.read_bytes()
    return {"path": str(path), "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}


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


def run(output: Path, root: Path):
    output = output.resolve()
    if output.exists():
        raise ValueError(f"Use a fresh Python CI output directory: {output}")
    output.mkdir(parents=True)
    receipt = {"state": "running", "profile": "complete-installed-python-scientific-desktop",
               "python": sys.executable, "modules": {}, "environment_contracts": []}

    def save():
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")

    save()
    try:
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
        applications = [Path(distribution.locate_file(path)).resolve()
                        for path in distribution.files or [] if path.name == "imagejxx"]
        applications = [path for path in applications if path.is_file() and os.access(path, os.X_OK)]
        if len(applications) != 1:
            raise ValueError(f"Expected one installed C++ desktop application, found {len(applications)}")
        receipt["cpp_application"] = pin(applications[0])
        environment["IMAGEJXX_CPP_EXE"] = str(applications[0])
        environment["IMAGEJXX_CPP_EXE_SHA256"] = receipt["cpp_application"]["sha256"]
        environment.update(capture(output / "cpp-observations", applications[0], root, environment))
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
        argv = [sys.executable, "-B", "-m", "pytest", "tests/python", "--tb=short",
                "-p", "no:cacheprovider", "--basetemp", str(output / "pytest-temp"),
                "--junitxml", str(output / "pytest.xml")]
        receipt["argv"] = argv; save()
        result = subprocess.run(argv, cwd=root, env=environment, check=False, timeout=2400)
        receipt["exit_code"] = result.returncode
        report = output / "pytest.xml"
        if report.exists():
            cases = ET.parse(report).getroot().findall(".//testcase")
            receipt["counts"] = {"cases": len(cases), **{tag: sum(case.find(tag) is not None for case in cases)
                for tag in ("failure", "error", "skipped")}}
        if result.returncode:
            raise subprocess.CalledProcessError(result.returncode, argv)
        counts = receipt.get("counts", {})
        if not counts.get("cases") or any(counts[tag] for tag in ("failure", "error", "skipped")):
            raise ValueError("Complete Python acceptance requires nonempty tests with no failures, errors or skips")
        receipt["state"] = "passed"
    except BaseException as error:
        receipt.update(state="failed", error={"type": type(error).__name__, "message": str(error)})
        raise
    finally:
        captures = output / "cpp-observations"
        if captures.is_dir():
            try:
                receipt["cpp_observation_archive"] = archive(captures)
            except Exception as error:
                receipt["state"] = "failed"
                receipt["archive_error"] = {"type": type(error).__name__, "message": str(error)}
                save()
                raise
        save()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    selected = parser.add_mutually_exclusive_group()
    selected.add_argument("--profile-manifest", type=Path)
    selected.add_argument("--focused-pytest-child", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.profile_manifest or args.focused_pytest_child:
        if args.output:
            parser.error("Focused output is declared in the profile, not --output")
        if args.focused_pytest_child:
            raise SystemExit(focused_pytest_child(args.focused_pytest_child))
        run_focused(json.loads(args.profile_manifest.read_bytes()))
    else:
        if args.output is None:
            parser.error("--output is required for the complete installed profile")
        run(args.output, Path(__file__).resolve().parents[1])


if __name__ == "__main__":
    main()
