"""Run the complete headless C++/installed-SDK CI profile without a shell.

IO and desktop have independent dependency and acceptance requirements. This
profile explicitly disables them; it does not select or filter native tests.
Each command must finish successfully before the next one is launched.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import time


def run_profile(source: Path, output: Path, cmake: str, ctest: str) -> None:
    source = source.resolve(strict=True)
    output = output.resolve()
    if output.exists():
        raise ValueError(f"Use a fresh CI output directory: {output}")
    output.mkdir(parents=True)
    cache, prefix, sdk = output / "native", output / "install", output / "sdk"
    receipt = {"state": "running", "profile": "headless-core-and-installed-sdk",
               "source": str(source), "output": str(output), "commands": []}

    def save():
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")

    def run(label, argv, timeout=1200):
        argv = [str(value) for value in argv]
        row = {"label": label, "argv": argv, "state": "running", "timeout_seconds": timeout}
        receipt["commands"].append(row)
        save()
        started = time.monotonic()
        print(json.dumps({"label": label, "argv": argv}), flush=True)
        try:
            with (output / f"{label}.log").open("wb") as log:
                completed = subprocess.run(argv, cwd=source, stdout=log, stderr=subprocess.STDOUT,
                                           timeout=timeout, check=False)
            row["exit_code"] = completed.returncode
            if completed.returncode:
                raise subprocess.CalledProcessError(completed.returncode, argv)
            row["state"] = "passed"
        except BaseException as error:
            row["state"] = "failed"
            row["error"] = {"type": type(error).__name__, "message": str(error)}
            log = output / f"{label}.log"
            if log.exists():
                print(log.read_text(encoding="utf-8", errors="replace")[-32768:], flush=True)
            raise
        finally:
            row["seconds"] = time.monotonic() - started
            save()

    save()
    try:
        # Do not hard-code a Visual Studio edition or generation. CMake's
        # installed-toolchain discovery works on both single/multi-config hosts.
        run("configure", [cmake, "-S", source, "-B", cache,
            "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=ON",
            "-DIMAGEJXX_BUILD_PYTHON=OFF", "-DIMAGEJXX_BUILD_IO=OFF",
            "-DIMAGEJXX_BUILD_CPP_UI=OFF", "-DIMAGEJXX_BUILD_PYTHON_UI=OFF"])
        run("build", [cmake, "--build", cache, "--config", "Release", "--parallel", "2"])
        run("test", [ctest, "--test-dir", cache, "-C", "Release", "--output-on-failure",
            "--no-tests=error", "--timeout", "180", "--output-junit", output / "native.xml"], 900)
        run("install", [cmake, "--install", cache, "--config", "Release", "--prefix", prefix])
        run("sdk-configure", [cmake, "-S", source / "examples/cpp", "-B", sdk,
            "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_PREFIX_PATH=" + str(prefix),
            "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF"])
        run("sdk-build", [cmake, "--build", sdk, "--config", "Release", "--parallel", "2"])
        run("sdk-test", [ctest, "--test-dir", sdk, "-C", "Release", "--output-on-failure",
            "--no-tests=error", "--timeout", "180", "--output-junit", output / "sdk.xml"], 900)
        receipt["state"] = "passed"
    except BaseException as error:
        receipt["state"] = "failed"
        receipt["error"] = {"type": type(error).__name__, "message": str(error)}
        raise
    finally:
        save()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cmake", default=shutil.which("cmake"))
    parser.add_argument("--ctest", default=shutil.which("ctest"))
    args = parser.parse_args()
    if not args.cmake or not args.ctest:
        parser.error("CMake and CTest must be installed")
    run_profile(args.source, args.output, args.cmake, args.ctest)


if __name__ == "__main__":
    main()
