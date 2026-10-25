"""Exercise the installed scientific IO SDK from one existing full wheel build.

Only the three new, explicit Windows/macOS source profiles are admitted. The
producer cache and dependency prefix are reused, never configured or rebuilt.
This is one installed IO round-trip qualification, not complete IO, platform,
Fiji, or dependency qualification. Linux IO SDK acceptance remains separate.

The fresh output retains receipts, logs, XML, inventories and the tiny written
carrier. Successful SDK/consumer scratch is retired after its hashes are saved;
failed scratch remains identified for diagnosis. No environment is serialized.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import time
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
SCHEMA = "imagejxx.scientific-sdk.001"
PROFILES = ("windows-x64-ci-dynamic-source-001",
            "macos-arm64-ci-static-source-001", "macos-x86_64-ci-static-source-001")
COMPILER_OVERRIDES = ("CMAKE_TOOLCHAIN_FILE", "CMAKE_CXX_COMPILER_LAUNCHER",
                      "CMAKE_CXX_LINKER_LAUNCHER", "CMAKE_PROJECT_TOP_LEVEL_INCLUDES",
                      "CMAKE_PROJECT_INCLUDE", "CMAKE_PROJECT_INCLUDE_BEFORE",
                      "CMAKE_USER_MAKE_RULES_OVERRIDE", "CMAKE_USER_MAKE_RULES_OVERRIDE_CXX")
CASE = "imagejxx_scientific_sdk_roundtrip"
CARRIER = "tests/fixtures/scientific-tiff-u16/defaults.tif"
CARRIER_PIN = {"bytes": 116,
               "sha256": "c5865779efc1945ed75db444d28f0545f8669dec7828bac8c53239d24fabbb64"}
MAX_FILES = 16384
MAX_TREE_BYTES = 4 * 1024**3


def regular(path):
    value = Path(path).lstat()
    if (not stat.S_ISREG(value.st_mode) or
            getattr(value, "st_file_attributes", 0) & 0x400):
        raise ValueError(f"Expected a regular non-linked file: {path}")


def pin(path):
    path = Path(path)
    regular(path)
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            size += len(block)
            digest.update(block)
    return {"bytes": size, "sha256": digest.hexdigest()}


def load_script(name):
    """Load only a named, source-controlled sibling; never a prefix helper."""
    path = ROOT / "scripts" / (name + ".py")
    regular(path)
    spec = importlib.util.spec_from_file_location("imagejxx_sdk_" + name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def read_json(path, maximum=4 * 1024 * 1024):
    regular(path)
    if path.stat().st_size > maximum:
        raise ValueError(f"JSON input exceeds its bound: {path}")
    return json.loads(path.read_bytes())


def write_json(path, value):
    raw = (json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n").encode("utf-8")
    temporary = path.with_name(path.name + ".writing")
    with temporary.open("xb") as stream:
        stream.write(raw)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def tree_files(directory):
    """Inventory without following a linked directory or retaining payloads."""
    rows = []
    total = 0

    def fail(error):
        raise error

    for parent, directories, names in os.walk(directory, followlinks=False, onerror=fail):
        for name in directories:
            value = (Path(parent) / name).lstat()
            if not stat.S_ISDIR(value.st_mode) or getattr(value, "st_file_attributes", 0) & 0x400:
                raise ValueError("Linked/non-directory member in SDK scratch")
        for name in names:
            path = Path(parent) / name
            regular(path)
            total += path.stat().st_size
            if len(rows) >= MAX_FILES or total > MAX_TREE_BYTES:
                raise ValueError("SDK file inventory exceeds its bound")
            rows.append({"path": path.relative_to(directory).as_posix(), **pin(path)})
    return sorted(rows, key=lambda row: row["path"])


def wheel_cache(root, builder):
    wheel = builder.checked_path(root / "build/wheel")
    if not wheel.is_dir():
        raise ValueError("The existing full pip wheel build cache is unavailable")
    found = []
    visited = 0

    def fail(error):
        raise error

    for parent, directories, names in os.walk(wheel, followlinks=False, onerror=fail):
        visited += 1
        if visited > MAX_FILES:
            raise ValueError("Wheel cache discovery exceeds its directory bound")
        for name in directories:
            if builder.is_link(Path(parent) / name):
                raise ValueError("A linked directory is not an admitted wheel cache")
        if "CMakeCache.txt" in names:
            found.append(Path(parent) / "CMakeCache.txt")
    if len(found) != 1:
        raise ValueError(f"Expected exactly one build/wheel/**/CMakeCache.txt, found {len(found)}")
    regular(found[0])
    return found[0]


def admit_cache(path, profile, builder):
    values = builder.cache_values(path)
    expected = {"IMAGEJXX_BUILD_IO": "ON", "CMAKE_BUILD_TYPE": "Release",
                "CMAKE_GENERATOR": "Ninja", "IMAGEJXX_CI_SOURCE_PROFILE": profile,
                "CMAKE_INSTALL_LIBDIR": "lib", "CMAKE_INSTALL_INCLUDEDIR": "include"}
    builder.check_effective(values, expected)
    if values.get("CMAKE_CONFIGURATION_TYPES"):
        raise ValueError("A multi-configuration producer cache is not admitted")
    if (not values.get("CMAKE_HOME_DIRECTORY") or
            Path(values["CMAKE_HOME_DIRECTORY"]).resolve(strict=True) != ROOT):
        raise ValueError("The wheel cache does not belong to this repository")
    if any(values.get(name) for name in COMPILER_OVERRIDES):
        raise ValueError("The wheel cache contains an unadmitted toolchain/launcher override")
    compiler = Path(values["CMAKE_CXX_COMPILER"]).resolve(strict=True)
    regular(compiler)
    return values, {**expected, "CMAKE_HOME_DIRECTORY": str(ROOT),
                    "CMAKE_CXX_COMPILER": values["CMAKE_CXX_COMPILER"]}


def tool(name):
    path = shutil.which(name)
    if not path:
        raise ValueError(f"Required SDK tool is unavailable: {name}")
    resolved = Path(path).resolve(strict=True)
    return {"path": str(resolved), **pin(resolved)}


def exact_junit(path):
    regular(path)
    if path.stat().st_size > 1024 * 1024:
        raise ValueError("SDK XML exceeds its byte bound")
    root = ET.fromstring(path.read_bytes())
    cases = list(root.iter("testcase"))
    if (root.tag not in ("testsuite", "testsuites") or len(cases) != 1 or cases[0].get("name") != CASE or
            cases[0].get("status", "run") not in ("run", "passed")):
        raise ValueError("CTest did not execute the one exact installed IO SDK case")
    if any(list(root.iter(tag)) for tag in ("failure", "error", "skipped")):
        raise ValueError("Installed IO SDK case failed, errored or skipped")
    for suite in root.iter("testsuite"):
        for field in ("failures", "errors", "skipped", "disabled"):
            if int(suite.get(field, "0")) != 0:
                raise ValueError("CTest reports an unqualified SDK case")
    return {"case_ids": [CASE], "passed": 1, "failed": 0, "errors": 0, "skipped": 0}


def runtime_libraries(path, system):
    """Read actual tool output, not link-command guesses or Python imports."""
    regular(path)
    if path.stat().st_size > 4 * 1024 * 1024:
        raise ValueError("Runtime dependency observation exceeds its byte bound")
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    if system == "win32":
        libraries = [match.group(1) for line in lines
                     if (match := re.fullmatch(r"\s+([A-Za-z0-9_.-]+\.dll)\s*", line, re.I))]
    else:
        libraries = [line.strip().split(" (", 1)[0] for line in lines
                     if line[:1].isspace() and " (" in line]
    if not libraries or any("python" in value.casefold() for value in libraries):
        raise ValueError("Native consumer runtime dependencies are absent or contain Python")
    return libraries


def retire_work(work, builder, output):
    """Only this fresh work root; validate all members before any deletion."""
    checked = builder.checked_path(work)
    if checked != output / "work" or not checked.is_relative_to(output):
        raise ValueError("SDK retirement escaped its owned work root")
    rows = tree_files(checked)
    write_json(output / "retired-work-files.json", rows)
    manifest = pin(output / "retired-work-files.json")
    # No linked members survived the complete inventory. Recheck each entry at
    # use; do not delegate traversal to shutil.rmtree or another shell.
    for parent, directories, names in os.walk(checked, topdown=False, followlinks=False):
        for name in names:
            path = builder.checked_path(Path(parent) / name)
            regular(path)
            path.unlink()
        for name in directories:
            builder.checked_path(Path(parent) / name).rmdir()
    checked.rmdir()
    return {"state": "retired", "files": len(rows), "bytes": sum(row["bytes"] for row in rows),
            "inventory": {"path": "retired-work-files.json", **manifest}}


def run_profile(output, profile, dependency_prefix):
    # Importing these source modules does not invoke their build/download entry
    # points. Their existing closed-prefix validation remains authoritative.
    sys.dont_write_bytecode = True
    builder = load_script("ci_platform_dependencies")
    output = builder.checked_path(output)
    if output.exists() or not output.is_relative_to(ROOT) or output == ROOT:
        raise ValueError("Use a fresh explicitly owned output below the repository")
    if output.is_relative_to(ROOT / "build/wheel"):
        raise ValueError("SDK output must not modify the existing producer cache")
    dependency_prefix = builder.checked_path(dependency_prefix)
    if (dependency_prefix.is_relative_to(output) or output.is_relative_to(dependency_prefix)):
        raise ValueError("SDK output and original dependency prefix must be disjoint")
    output.mkdir(parents=True)
    receipt = {"schema": SCHEMA, "state": "admitting", "profile": profile,
               "scope": "one installed scientific IO SDK round-trip",
               "full_platform_qualification": False, "repository": str(ROOT),
               "output": str(output), "commands": [], "scratch": {"state": "not_created"},
               "environment_values_recorded": False}

    def save():
        write_json(output / "receipt.json", receipt)

    save()
    try:
        if profile not in PROFILES:
            raise ValueError("This SDK slice admits only the new Windows/macOS source profiles")
        builder.require_host(builder.profile_definition(profile))
        if sys.version_info[:2] not in ((3, 10), (3, 12)):
            raise ValueError("Use the CI matrix's Python 3.10 or 3.12 metadata interpreter")
        verifier = load_script("verify_ci_platform_dependencies")
        catalog = ROOT / "licenses/ci-linux-source-dependencies.json"
        producer = ROOT / "scripts/ci_platform_dependencies.py"
        certificate = verifier.validate_prefix(dependency_prefix, profile, catalog=catalog, builder=producer)
        certificate_path = dependency_prefix / verifier.CERTIFICATE
        dependency_before = tree_files(dependency_prefix)
        shutil.copyfile(certificate_path, output / "dependency-certificate.json")
        if pin(output / "dependency-certificate.json") != pin(certificate_path):
            raise ValueError("Dependency certificate retention changed its bytes")
        receipt["dependency_preparation"] = {
            "state": certificate["state"], "acceptance": certificate["acceptance"],
            "prefix": str(dependency_prefix), "certificate": pin(certificate_path),
            "retained_certificate": "dependency-certificate.json",
            "source_manifest": {"path": str(catalog), **pin(catalog)}}

        names = ["examples/cpp-io/CMakeLists.txt", "examples/cpp-io/main.cpp",
                 "scripts/ci_scientific_sdk.py", "scripts/ci_owned_process.py",
                 "scripts/ci_platform_dependencies.py", "scripts/verify_ci_platform_dependencies.py",
                 "CMakeLists.txt", "cmake/imagejxxConfig.cmake.in", "cmake/SourceTiffRuntime.cmake",
                 "cmake/ReviewedTiffRuntime.cmake", "cmake/TiffRuntimeManifest.cmake",
                 "licenses/ci-linux-source-dependencies.json",
                 "tests/fixtures/scientific-tiff-u16/SOURCE.json", CARRIER]
        if sys.platform == "darwin":
            names.append("scripts/ci_mac_process.py")
        inputs = [{"path": name, **pin(ROOT / name)} for name in sorted(names)]
        write_json(output / "source-inputs.json", inputs)
        receipt["source_inputs"] = {"path": "source-inputs.json", **pin(output / "source-inputs.json")}
        carrier = ROOT / CARRIER
        if pin(carrier) != CARRIER_PIN:
            raise ValueError("The original defaults.tif fingerprint changed")
        fixture_rows = [row for row in read_json(carrier.parent / "SOURCE.json")["files"]
                        if row.get("file") == "defaults.tif"]
        if (len(fixture_rows) != 1 or fixture_rows[0].get("valid") is not True or
                fixture_rows[0].get("shape") != [1, 1, 1, 1, 2, 1] or
                any(fixture_rows[0].get(key) != value for key, value in CARRIER_PIN.items())):
            raise ValueError("The original carrier declaration changed")
        receipt["carrier"] = {"path": CARRIER, **CARRIER_PIN,
                              "shape": [1, 1, 1, 1, 2, 1], "pixel_type": "UInt16", "samples": [0, 1]}

        cache_path = wheel_cache(ROOT, builder)
        values, effective = admit_cache(cache_path, profile, builder)
        cache = cache_path.parent
        suffix = ".lib" if sys.platform == "win32" else ".a"
        stem = "" if sys.platform == "win32" else "lib"
        archives = [stem + "imagejxx_" + name + suffix for name in ("core", "io")]
        cache_names = ["CMakeCache.txt", "cmake_install.cmake", "imagejxxConfig.cmake",
                       "imagejxxConfigVersion.cmake", "imagejxx-source-dependencies.cmake",
                       "imagejxx-source-dependencies-verify.cmake", *archives]
        cache_inputs = {name: pin(cache / name) for name in cache_names}
        generated_prefix = "set(IMAGEJXX_CI_SOURCE_DEP_PREFIX " + verifier.quote(dependency_prefix) + ")"
        if generated_prefix not in (cache / "imagejxx-source-dependencies.cmake").read_text(encoding="utf-8").splitlines():
            raise ValueError("The existing wheel cache selected a different source dependency prefix")
        receipt["producer_cache"] = {"path": str(cache), "effective": effective,
                                     "inputs": cache_inputs, "reconfigured": False, "rebuilt": False}
        tools = {name: tool(name) for name in ("cmake", "ctest")}
        generator = "Ninja" if sys.platform == "win32" else "Unix Makefiles"
        if sys.platform == "win32":
            tools["build"] = tool("ninja")
        else:
            make = Path("/usr/bin/make")
            tools["build"] = {"path": str(make), "resolved_path": str(make.resolve(strict=True)),
                              **pin(make.resolve(strict=True))}
        dependency_tool = "dumpbin" if sys.platform == "win32" else "otool"
        tools[dependency_tool] = tool(dependency_tool)
        compiler = Path(values["CMAKE_CXX_COMPILER"]).resolve(strict=True)
        tools["compiler"] = {"path": str(compiler), **pin(compiler)}
        interpreter = Path(sys.executable).resolve(strict=True)
        tools["metadata_python"] = {"path": str(interpreter), **pin(interpreter),
                                     "version": list(sys.version_info[:3])}
        receipt["tools"] = tools
        owner = load_script("ci_mac_process" if sys.platform == "darwin" else "ci_owned_process")
        execute = owner.run_mac_owned if sys.platform == "darwin" else owner.run_owned
        for name in (*COMPILER_OVERRIDES, "CXXFLAGS", "CPPFLAGS", "LDFLAGS", "MAKEFLAGS", "MFLAGS",
                     "MAKEFILES", "GNUMAKEFLAGS", "CMAKE_GENERATOR_TOOLSET", "CMAKE_GENERATOR_PLATFORM",
                     "CMAKE_GENERATOR_INSTANCE"):
            if os.environ.get(name):
                raise ValueError("Unadmitted inherited build override: " + name)
        environment = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")

        def run(label, argv, timeout=300):
            command = [str(value) for value in argv]
            row = {"label": label, "argv": command, "state": "running", "timeout_seconds": timeout}
            receipt["commands"].append(row)
            save()
            started = time.monotonic()
            print(json.dumps({"label": label, "argv": command}), flush=True)
            log_path = output / (label + ".log")
            process_path = output / (label + ".process.json")
            try:
                with log_path.open("xb") as log:
                    process = execute(command, cwd=ROOT, env=environment, timeout=timeout,
                                      stdout=log, stderr=subprocess.STDOUT, receipt_path=process_path)
                row["exit_code"] = process.get("exit_code")
                row["cleanup_complete"] = process.get("cleanup_complete")
                if (process.get("state") != "exited" or process.get("target_completed") is not True or
                        process.get("cleanup_complete") is not True or process.get("exit_code") != 0):
                    raise ValueError(f"SDK command has no successful, resolved owned exit: {label}")
                row["state"] = "passed"
            except BaseException as error:
                row.update(state="failed", error={"type": type(error).__name__, "message": str(error)})
                raise
            finally:
                row["seconds"] = time.monotonic() - started
                for key, path in (("log", log_path), ("process", process_path)):
                    if path.is_file():
                        row[key] = {"path": path.name, **pin(path)}
                save()

        work = output / "work"
        work.mkdir()
        sdk, consumer = work / "sdk", work / "consumer"
        receipt["scratch"] = {"state": "retained", "path": str(work)}
        receipt["state"] = "running"
        save()
        cmake, ctest = tools["cmake"]["path"], tools["ctest"]["path"]
        run("build-tool-version", [tools["build"]["path"], "--version"], 30)
        if sys.platform == "darwin":
            version_lines = (output / "build-tool-version.log").read_text(encoding="utf-8").splitlines()
            if not version_lines or version_lines[0] != "GNU Make 3.81":
                raise ValueError("The selected system Make does not match the reviewed GNU Make 3.81 route")
            receipt["build_ownership"] = {
                "generator": generator, "program": "/usr/bin/make", "parallel_jobs": 1,
                "scope": "trusted CMake-generated one-translation-unit recipe; no custom user jobs",
                "source_review": "https://github.com/apple-oss-distributions/gnumake/blob/main/job.c",
                "host_lifecycle_qualification": "reported separately by the macOS ownership tests"}
        for component in ("development", "io-development"):
            run("install-" + component, [cmake, "--install", cache, "--config", "Release",
                                        "--component", component, "--prefix", sdk])
        delivered = sdk / "share/imagejxx/ci-dependencies"
        copied = verifier.validate_prefix(delivered, profile, catalog=catalog, builder=producer)
        if copied != certificate or tree_files(delivered) != dependency_before:
            raise ValueError("Installed SDK dependency closure differs from the admitted transfer")
        for name in archives:
            if pin(sdk / "lib" / name) != cache_inputs[name]:
                raise ValueError("Installed SDK archive differs from the existing wheel build")
        package = sdk / "lib/cmake/imagejxx"
        helpers = {"SourceTiffRuntime.cmake": "cmake/SourceTiffRuntime.cmake",
                   "ReviewedTiffRuntime.cmake": "cmake/ReviewedTiffRuntime.cmake",
                   "TiffRuntimeManifest.cmake": "cmake/TiffRuntimeManifest.cmake",
                   "verify_ci_platform_dependencies.py": "scripts/verify_ci_platform_dependencies.py",
                   "ci_platform_dependencies.py": "scripts/ci_platform_dependencies.py",
                   "ci-linux-source-dependencies.json": "licenses/ci-linux-source-dependencies.json"}
        for name, original in helpers.items():
            if pin(package / name) != pin(ROOT / original):
                raise ValueError("Installed SDK helper differs from its recorded source: " + name)
        for name in ("imagejxxConfig.cmake", "imagejxxConfigVersion.cmake"):
            if pin(package / name) != cache_inputs[name]:
                raise ValueError("Installed package configuration differs from the wheel cache")
        sdk_before = tree_files(sdk)
        write_json(output / "sdk-files.json", sdk_before)
        receipt["installed_sdk"] = {"prefix": str(sdk), "files": len(sdk_before),
                                    "bytes": sum(row["bytes"] for row in sdk_before),
                                    "inventory": {"path": "sdk-files.json", **pin(output / "sdk-files.json")}}

        options = {"CMAKE_BUILD_TYPE": "Release", "BUILD_TESTING": "ON",
                   "CMAKE_MAKE_PROGRAM": tools["build"]["path"],
                   "CMAKE_CXX_COMPILER": values["CMAKE_CXX_COMPILER"],
                   "CMAKE_PREFIX_PATH": sdk.as_posix(),
                   "CMAKE_FIND_USE_PACKAGE_REGISTRY": "OFF",
                   "CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY": "OFF",
                   "CMAKE_FIND_USE_CMAKE_ENVIRONMENT_PATH": "OFF",
                   "IMAGEJXX_CI_SOURCE_PROFILE": profile,
                   "IMAGEJXX_SDK_PREFIX": sdk.as_posix(),
                   "IMAGEJXX_SDK_DEPENDENCY_PREFIX": delivered.as_posix(),
                   "IMAGEJXX_SDK_FIXTURE": carrier.as_posix(),
                   "IMAGEJXX_SDK_OUTPUT": (output / "roundtrip.tif").as_posix(),
                   "Python3_EXECUTABLE": str(interpreter)}
        for spelling in ("TIFF_DIR", "Tiff_DIR", "tiff_DIR"):
            options[spelling] = (delivered / "lib/cmake/tiff").as_posix()
        if sys.platform == "darwin":
            options.update(CMAKE_OSX_ARCHITECTURES=builder.profile_definition(profile)["architecture"],
                           CMAKE_OSX_DEPLOYMENT_TARGET="11.0")
            if values.get("CMAKE_OSX_SYSROOT"):
                options["CMAKE_OSX_SYSROOT"] = values["CMAKE_OSX_SYSROOT"]
        run("consumer-configure", [cmake, "-S", ROOT / "examples/cpp-io", "-B", consumer,
                                   "-G", generator, *builder.definitions_argv(options)])
        actual = builder.cache_values(consumer / "CMakeCache.txt")
        builder.check_effective(actual, options)
        if actual.get("CMAKE_CONFIGURATION_TYPES") or actual.get("CMAKE_GENERATOR") != generator:
            raise ValueError("The SDK consumer changed its single-configuration generator")
        if any(actual.get(name) for name in COMPILER_OVERRIDES):
            raise ValueError("The SDK consumer acquired an unadmitted toolchain/launcher override")
        imports = read_json(consumer / "sdk-imports.json")
        if (imports.get("schema") != "imagejxx.scientific-sdk-imports.001" or
                imports.get("IMAGEJXX_CI_SOURCE_PROFILE") != profile):
            raise ValueError("Missing or changed installed-target observation")
        for key, path in (("core_link", sdk / "lib" / archives[0]),
                          ("io_link", sdk / "lib" / archives[1]),
                          ("tiff_link", delivered / copied["configuration"]["libraries"]["tiff"]["link"])):
            if Path(imports[key]).resolve(strict=True) != path:
                raise ValueError("Consumer import escaped the installed SDK: " + key)
        # The IO guard intentionally selects only TIFF's four runtime roles.
        # PNG/GLFW remain in the separately sealed full source prefix.
        roles = copied["configuration"]["libraries"]
        runtime_paths = sorted(delivered / roles[name]["runtime"] for name in ("tiff", "jpeg", "lzma", "zlib")
                               if "runtime" in roles[name])
        actual_runtime = sorted(Path(value).resolve(strict=True) for value in
                                imports.get("IMAGEJXX_IO_RUNTIME_FILES", "").split(";") if value)
        if actual_runtime != runtime_paths:
            raise ValueError("Consumer runtime list differs from the closed dependency certificate")
        shutil.copyfile(consumer / "sdk-imports.json", output / "imports.json")
        receipt["imports"] = {"path": "imports.json", **pin(output / "imports.json")}
        receipt["consumer_configuration"] = {"requested": options,
                                             "cache": pin(consumer / "CMakeCache.txt")}
        run("consumer-build", [cmake, "--build", consumer, "--config", "Release",
                               "--target", "imagejxx_scientific_sdk", "--parallel", "1", "--verbose"], 600)
        binary = consumer / "bin" / ("imagejxx_scientific_sdk.exe" if sys.platform == "win32" else "imagejxx_scientific_sdk")
        receipt["consumer"] = {"path": str(binary), **pin(binary)}
        subjects = [binary]
        for original in runtime_paths:
            copied_runtime = binary.parent / original.name
            if pin(copied_runtime) != pin(original):
                raise ValueError("Delivered runtime DLL changed during consumer transport")
            subjects.append(copied_runtime)
        receipt["runtime_dependencies"] = []
        for index, subject in enumerate(subjects):
            label = "runtime-dependencies-" + str(index)
            argv = ([tools[dependency_tool]["path"], "/DEPENDENTS", subject] if sys.platform == "win32"
                    else [tools[dependency_tool]["path"], "-L", subject])
            run(label, argv, 60)
            receipt["runtime_dependencies"].append({"subject": subject.name, **pin(subject),
                                                    "libraries": runtime_libraries(output / (label + ".log"), sys.platform)})
        run("consumer-test", [ctest, "--test-dir", consumer, "-C", "Release", "--output-on-failure",
                              "--no-tests=error", "--timeout", "30", "--output-junit", output / "sdk.xml"], 90)
        receipt["results"] = exact_junit(output / "sdk.xml")
        receipt["xml"] = {"path": "sdk.xml", **pin(output / "sdk.xml")}
        receipt["roundtrip"] = {"path": "roundtrip.tif", **pin(output / "roundtrip.tif")}

        if ({name: pin(cache / name) for name in cache_names} != cache_inputs or
                [{"path": row["path"], **pin(ROOT / row["path"])} for row in inputs] != inputs or
                tree_files(dependency_prefix) != dependency_before or tree_files(sdk) != sdk_before):
            raise ValueError("An admitted producer, source, SDK or dependency input changed during qualification")
        verifier.validate_prefix(delivered, profile, catalog=catalog, builder=producer)
        receipt["inputs_unchanged"] = True
        receipt["scratch"] = retire_work(work, builder, output)
        receipt["state"] = "passed"
    except BaseException as error:
        receipt.update(state="failed", error={"type": type(error).__name__, "message": str(error)})
        raise
    finally:
        save()
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--profile", choices=PROFILES, required=True)
    parser.add_argument("--dependency-prefix", type=Path, required=True)
    args = parser.parse_args()
    run_profile(args.output, args.profile, args.dependency_prefix)


if __name__ == "__main__":
    main()
