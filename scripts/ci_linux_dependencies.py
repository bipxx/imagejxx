"""Build the pinned Linux scientific-IO/ImGui dependency prefix for CI.

Requires CMake, Ninja, NASM and the system X11/OpenGL development prerequisites.
Python 3.12+ supplies the safe tar extraction filter. No existing SDK, local
build receipt or vcpkg binary is an input. Windows runtime checks are untouched.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import time
import urllib.request


def pin(path):
    raw = path.read_bytes()
    return {"bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}


def build(output: Path, *, archive_cache: Path | None = None,
          platform_include: Path | None = None, nasm: str | None = None):
    root = Path(__file__).resolve().parents[1]
    if sys.platform != "linux" or platform.machine() not in ("x86_64", "AMD64"):
        raise ValueError("This reviewed CI dependency profile is Linux x86_64 only")
    if not hasattr(tarfile, "data_filter"):
        raise ValueError("Use Python with the tarfile data extraction filter (3.12+)")
    nasm = nasm or shutil.which("nasm")
    if not nasm:
        raise ValueError("NASM is required for the reviewed JPEG SIMD configuration")
    manifest_path = root / "licenses/ci-linux-source-dependencies.json"
    manifest = json.loads(manifest_path.read_bytes())
    dependencies = manifest["dependencies"]
    if manifest["profile"] != "linux-x86_64-ci-static-source-001":
        raise ValueError("Unknown dependency profile")
    # Validate notices before any network access or compiler invocation.
    for name, dependency in dependencies.items():
        if dependency["commercial_use"] is not True:
            raise ValueError(f"Dependency is not commercially eligible: {name}")
        for notice, expected in dependency["notices"].items():
            if pin(root / "licenses" / notice)["sha256"] != expected:
                raise ValueError(f"Reviewed notice changed: {notice}")
    output = output.resolve()
    if output.exists():
        raise ValueError(f"Use a fresh dependency output directory: {output}")
    output.mkdir(parents=True)
    prefix, sources = output / "prefix", output / "sources"
    prefix.mkdir(); sources.mkdir()
    downloads = output / "downloads"; downloads.mkdir()
    logs = output / "logs"; logs.mkdir()
    receipt = {"state": "running", "profile": manifest["profile"],
               "manifest": pin(manifest_path), "commands": [], "sources": {}}

    def save():
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")

    def run(label, argv):
        argv = [str(value) for value in argv]
        row = {"label": label, "argv": argv, "state": "running"}
        receipt["commands"].append(row); save()
        print(json.dumps({"label": label, "argv": argv}), flush=True)
        started = time.monotonic()
        try:
            with (logs / f"{label}.log").open("wb") as log:
                result = subprocess.run(argv, stdout=log, stderr=subprocess.STDOUT,
                                        check=False, timeout=900)
            row["exit_code"] = result.returncode
            if result.returncode:
                print((logs / f"{label}.log").read_text(errors="replace")[-32768:], flush=True)
                raise subprocess.CalledProcessError(result.returncode, argv)
            row["state"] = "passed"
        except BaseException as error:
            row.update(state="failed", error={"type": type(error).__name__, "message": str(error)})
            raise
        finally:
            row["seconds"] = time.monotonic() - started; save()

    def source(name):
        expected = dependencies[name]["archive"]
        archive = downloads / (name + ".tar.gz")
        cached = archive_cache / archive.name if archive_cache else None
        if cached and cached.is_file():
            if pin(cached) != {key: expected[key] for key in ("bytes", "sha256")}:
                raise ValueError(f"Cached dependency fingerprint changed: {name}")
            shutil.copyfile(cached, archive)
        else:
            with urllib.request.urlopen(expected["url"], timeout=60) as response:
                raw = response.read(expected["bytes"] + 1)
            if len(raw) != expected["bytes"] or hashlib.sha256(raw).hexdigest() != expected["sha256"]:
                raise ValueError(f"Downloaded dependency fingerprint changed: {name}")
            archive.write_bytes(raw)
        destination = sources / name; destination.mkdir()
        with tarfile.open(archive) as bundle:
            bundle.extractall(destination, filter="data")
        entries = list(destination.iterdir())
        if len(entries) != 1 or not entries[0].is_dir() or entries[0].is_symlink():
            raise ValueError(f"Expected one reviewed source root: {name}")
        receipt["sources"][name] = {"archive": pin(archive), "url": expected["url"]}; save()
        return entries[0]

    def compile_library(name, directory, options):
        cache = output / (name + "-build")
        run(name + "-configure", ["cmake", "-S", directory, "-B", cache, "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
            "-DCMAKE_INSTALL_LIBDIR=lib", "-DCMAKE_INSTALL_PREFIX=" + str(prefix),
            "-DCMAKE_PREFIX_PATH=" + str(prefix), *options])
        run(name + "-build", ["cmake", "--build", cache, "--parallel", "2"])
        run(name + "-install", ["cmake", "--install", cache])
        receipt["sources"][name]["configuration"] = pin(cache / "CMakeCache.txt"); save()

    save()
    try:
        run("cmake-version", ["cmake", "--version"])
        run("nasm-version", [nasm, "--version"])
        compile_library("zlib", source("zlib"), ["-DZLIB_BUILD_EXAMPLES=OFF"])
        compile_library("libjpeg-turbo", source("libjpeg-turbo"), [
            "-DENABLE_SHARED=OFF", "-DENABLE_STATIC=ON", "-DWITH_TURBOJPEG=OFF",
            "-DWITH_TOOLS=OFF", "-DWITH_TESTS=OFF", "-DWITH_SIMD=ON",
            "-DCMAKE_ASM_NASM_COMPILER=" + str(nasm)])
        compile_library("liblzma", source("liblzma"), ["-DBUILD_SHARED_LIBS=OFF",
            "-DBUILD_TESTING=OFF", "-DXZ_TOOL_XZ=OFF", "-DXZ_TOOL_XZDEC=OFF",
            "-DXZ_TOOL_LZMADEC=OFF", "-DXZ_TOOL_LZMAINFO=OFF", "-DXZ_TOOL_SCRIPTS=OFF"])
        zlib = ["-DZLIB_USE_STATIC_LIBS=ON", "-DZLIB_LIBRARY=" + str(prefix / "lib/libz.a"),
                "-DZLIB_INCLUDE_DIR=" + str(prefix / "include")]
        compile_library("png", source("png"), ["-DPNG_SHARED=OFF", "-DPNG_STATIC=ON",
            "-DPNG_TESTS=OFF", "-DPNG_TOOLS=OFF", *zlib])
        x11 = []
        if platform_include:
            x11 = ["-DX11_" + key + "_INCLUDE_PATH=" + str(platform_include.resolve())
                   for key in ("X11", "Xrandr", "Xinerama", "Xcursor", "Xi", "Xshape")]
        compile_library("glfw", source("glfw"), ["-DBUILD_SHARED_LIBS=OFF",
            "-DGLFW_BUILD_DOCS=OFF", "-DGLFW_BUILD_TESTS=OFF", "-DGLFW_BUILD_EXAMPLES=OFF",
            "-DGLFW_BUILD_WAYLAND=OFF", "-DGLFW_BUILD_X11=ON", *x11])
        compile_library("tiff", source("tiff"), ["-DBUILD_SHARED_LIBS=OFF",
            "-Dtiff-tools=OFF", "-Dtiff-tests=OFF", "-Dtiff-contrib=OFF", "-Dtiff-docs=OFF",
            "-Dtiff-install=ON", "-Dzlib=ON", "-Dlibdeflate=OFF", "-Djpeg=ON", "-Dold-jpeg=ON",
            "-Djbig=OFF", "-Dlzma=ON", "-Dzstd=OFF", "-Dwebp=OFF", "-Dlerc=OFF", *zlib,
            "-DJPEG_LIBRARY_RELEASE=" + str(prefix / "lib/libjpeg.a"),
            "-DJPEG_INCLUDE_DIR=" + str(prefix / "include"),
            "-DLIBLZMA_LIBRARY_RELEASE=" + str(prefix / "lib/liblzma.a"),
            "-DLIBLZMA_INCLUDE_DIR=" + str(prefix / "include")])
        # Upstream's static TIFF config omits its private codec imports. This
        # relocatable adapter supplies those exact libraries, with no codec code.
        config = prefix / "lib/cmake/tiff"
        adapter = 'get_filename_component(_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)\n'
        for target, library in [("ZLIB::ZLIB", "libz.a"), ("JPEG::JPEG", "libjpeg.a"),
                                ("liblzma::liblzma", "liblzma.a")]:
            adapter += (f'if(NOT TARGET {target})\n add_library({target} STATIC IMPORTED)\n'
                f' set_target_properties({target} PROPERTIES IMPORTED_LOCATION "${{_prefix}}/lib/{library}"'
                ' INTERFACE_INCLUDE_DIRECTORIES "${_prefix}/include")\nendif()\n')
        adapter += ('if(NOT TARGET CMath::CMath)\n add_library(CMath::CMath INTERFACE IMPORTED)\n'
            ' set_target_properties(CMath::CMath PROPERTIES INTERFACE_LINK_LIBRARIES m)\nendif()\n'
            'include("${CMAKE_CURRENT_LIST_DIR}/TiffTargets.cmake")\nset(tiff_FOUND TRUE)\nset(TIFF_FOUND TRUE)\n')
        (config / "tiff-config.cmake").write_text(adapter, encoding="utf-8")
        shutil.copyfile(config / "TiffConfigVersion.cmake", config / "tiff-config-version.cmake")
        stb = source("stb")
        for header, expected in dependencies["stb"]["headers"].items():
            if pin(stb / header)["sha256"] != expected:
                raise ValueError(f"Reviewed stb header changed: {header}")
            shutil.copyfile(stb / header, prefix / "include" / header)
        # Use the repository's already reviewed, complete JSON header tree.
        vendored = root / "third_party/nlohmann_json"
        shutil.copytree(vendored / "include", prefix / "include", dirs_exist_ok=True)
        config = prefix / "share/cmake/nlohmann_json"; config.mkdir(parents=True)
        (config / "nlohmann_jsonConfig.cmake").write_text(
            'if(NOT TARGET nlohmann_json::nlohmann_json)\n'
            ' add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)\n'
            ' get_filename_component(_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)\n'
            ' set_target_properties(nlohmann_json::nlohmann_json PROPERTIES'
            ' INTERFACE_INCLUDE_DIRECTORIES "${_prefix}/include")\nendif()\n'
            'set(nlohmann_json_VERSION 3.12.0)\n', encoding="utf-8")
        (config / "nlohmann_jsonConfigVersion.cmake").write_text(
            'set(PACKAGE_VERSION "3.12.0")\nif(PACKAGE_FIND_VERSION VERSION_EQUAL PACKAGE_VERSION)\n'
            ' set(PACKAGE_VERSION_EXACT TRUE)\n set(PACKAGE_VERSION_COMPATIBLE TRUE)\nendif()\n', encoding="utf-8")
        for name, dependency in dependencies.items():
            notices = prefix / "share/licenses/imagejxx-ci" / name; notices.mkdir(parents=True)
            for notice in dependency["notices"]:
                shutil.copyfile(root / "licenses" / notice, notices / notice)
        notices = prefix / "share/licenses/imagejxx-ci/nlohmann_json"; notices.mkdir()
        shutil.copyfile(vendored / "LICENSE.MIT", notices / "LICENSE.MIT")
        for library in ("libz.a", "libjpeg.a", "liblzma.a", "libpng16.a", "libglfw3.a", "libtiff.a"):
            if not (prefix / "lib" / library).is_file():
                raise ValueError(f"Selected static library missing: {library}")
        receipt.update(state="passed", prefix=str(prefix),
            installed_files=[{"path": path.relative_to(prefix).as_posix(), **pin(path)}
                             for path in sorted(prefix.rglob("*")) if path.is_file()])
        (prefix / "share/imagejxx-ci-sources.json").write_text(
            json.dumps({"profile": manifest["profile"], "dependencies": dependencies}, indent=2) + "\n", encoding="utf-8")
    except BaseException as error:
        receipt.update(state="failed", error={"type": type(error).__name__, "message": str(error)})
        raise
    finally:
        save()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--archive-cache", type=Path)
    parser.add_argument("--platform-include", type=Path)
    parser.add_argument("--nasm")
    args = parser.parse_args()
    build(args.output, archive_cache=args.archive_cache,
          platform_include=args.platform_include, nasm=args.nasm)


if __name__ == "__main__":
    main()
