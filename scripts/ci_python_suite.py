"""Run the complete installed Python suite with explicit native build inputs.

This does not manufacture C++ observation reports or accept missing features.
Missing bindings, failed tests and skipped tests all leave this profile failing.
The receipt records named environment contracts to guide portable fixture work.
"""
from __future__ import annotations

import argparse
import ast
import hashlib
import importlib
import importlib.metadata
import json
import os
from pathlib import Path
import subprocess
import sys
import sysconfig
import xml.etree.ElementTree as ET


def pin(path):
    path = Path(path).resolve(strict=True)
    raw = path.read_bytes()
    return {"path": str(path), "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}


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
        for name in ("imagejxx._native", "imagejxx._scientific_io", "imagejxx._desktop"):
            module = importlib.import_module(name)
            receipt["modules"][name] = pin(module.__file__)
        environment = os.environ.copy()
        environment.update(IMAGEJXX_RENDER_TESTS="1", IMAGEJXX_REQUIRE_RENDER_TESTS="1",
                           PYTHONDONTWRITEBYTECODE="1", PYTEST_DISABLE_PLUGIN_AUTOLOAD="1")
        suffix = sysconfig.get_config_var("EXT_SUFFIX")
        providers = list((root / "build/wheel").rglob("_imagejxx_test_saved_roi_source" + suffix))
        if len(providers) != 1:
            raise ValueError(f"Expected one built test-only saved-ROI provider, found {len(providers)}")
        environment["IMAGEJXX_SAVED_ROI_PROVIDER"] = str(providers[0].resolve())
        receipt["test_provider"] = pin(providers[0])
        distribution = importlib.metadata.distribution("imagejxx")
        applications = [Path(distribution.locate_file(path)).resolve()
                        for path in distribution.files or [] if path.name == "imagejxx"]
        applications = [path for path in applications if path.is_file() and os.access(path, os.X_OK)]
        if len(applications) != 1:
            raise ValueError(f"Expected one installed C++ desktop application, found {len(applications)}")
        receipt["cpp_application"] = pin(applications[0])
        environment["IMAGEJXX_CPP_EXE"] = str(applications[0])
        environment["IMAGEJXX_CPP_EXE_SHA256"] = receipt["cpp_application"]["sha256"]
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
        save()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    run(args.output, Path(__file__).resolve().parents[1])


if __name__ == "__main__":
    main()
