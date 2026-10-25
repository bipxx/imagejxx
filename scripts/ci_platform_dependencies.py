"""Prepare explicit Windows/macOS source dependency profiles for disposable CI.

The old Windows byte-exact runtime profile and Linux builder are not inputs to
the new binary identity. Only their reviewed source/notice catalog is reused.
``prepared`` proves the recorded build/import checks, never full application or
hosted-suite qualification. Run this builder with Python 3.12+ in a native x64
MSVC developer environment, or a native AppleClang environment on macOS.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shutil
import stat
import struct
import subprocess
import sys
import tarfile
import time
import urllib.request


ROOT = Path(__file__).resolve().parents[1]
SOURCE_MANIFEST = "licenses/ci-linux-source-dependencies.json"
CERTIFICATE = "share/imagejxx-ci-build.json"
SCHEMA = "imagejxx.ci-platform-dependencies.001"
DEPENDENCIES = ("zlib", "libjpeg-turbo", "liblzma", "png", "glfw", "tiff", "stb")
PROFILES = {
    "windows-x64-ci-dynamic-source-001": ("win32", "x86_64", "shared"),
    "macos-arm64-ci-static-source-001": ("darwin", "arm64", "static"),
    "macos-x86_64-ci-static-source-001": ("darwin", "x86_64", "static"),
}
VERSIONS = {"zlib": "1.3.1", "libjpeg-turbo": "3.1.3", "liblzma": "5.8.2",
            "png": "1.6.50", "glfw": "3.4", "tiff": "4.7.1",
            "stb": "f75e8d1cad7d90d72ef7a4661f1b994ef78b4e31"}
LICENSES = {"zlib": "Zlib", "libjpeg-turbo": "BSD-3-Clause AND IJG AND Zlib",
            "liblzma": "0BSD", "png": "Libpng-2.0", "glfw": "Zlib",
            "tiff": "libtiff", "stb": "MIT"}


def pin(path: Path) -> dict:
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            size += len(block)
            digest.update(block)
    return {"bytes": size, "sha256": digest.hexdigest()}


def is_link(path: Path) -> bool:
    value = path.lstat()
    return stat.S_ISLNK(value.st_mode) or bool(
        getattr(value, "st_file_attributes", 0) & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0))


def regular(path: Path) -> None:
    if is_link(path) or not stat.S_ISREG(path.lstat().st_mode):
        raise ValueError(f"Expected a regular, non-linked file: {path}")


def checked_path(path: Path) -> Path:
    """Reject CMake list/control syntax and linked existing ancestors."""
    path = Path(os.path.abspath(path))
    if any(character in str(path) for character in (";", "\n", "\r", "\0", '"')):
        raise ValueError("Dependency paths must not contain CMake list/control syntax")
    for ancestor in (path, *path.parents):
        if ancestor.exists() or ancestor.is_symlink():
            if is_link(ancestor):
                raise ValueError(f"Dependency path has a linked ancestor: {ancestor}")
    return path


def profile_definition(name: str) -> dict:
    try:
        system, architecture, linkage = PROFILES[name]
    except KeyError as error:
        raise ValueError("Unknown source dependency profile") from error
    return {"name": name, "system": system, "architecture": architecture,
            "linkage": linkage, "deployment_target": "11.0" if system == "darwin" else None}


def require_host(profile: dict, system: str | None = None,
                 machine: str | None = None, pointer_bytes: int | None = None) -> None:
    system = sys.platform if system is None else system
    machine = platform.machine() if machine is None else machine
    architecture = {"AMD64": "x86_64", "amd64": "x86_64", "x86_64": "x86_64",
                    "arm64": "arm64", "aarch64": "arm64"}.get(machine)
    pointer_bytes = struct.calcsize("P") if pointer_bytes is None else pointer_bytes
    if system != profile["system"] or architecture != profile["architecture"] or pointer_bytes != 8:
        raise ValueError("Source profile requires its native 64-bit host; cross-compilation is not admitted")


def read_catalog(root: Path) -> tuple[dict, dict]:
    path = root / SOURCE_MANIFEST
    regular(path)
    manifest = json.loads(path.read_bytes())
    if (manifest.get("schema_version") != 1 or
            manifest.get("profile") != "linux-x86_64-ci-static-source-001" or
            set(manifest.get("dependencies", {})) != set(DEPENDENCIES)):
        raise ValueError("Reviewed source catalog membership/profile changed")
    dependencies = manifest["dependencies"]
    for name in DEPENDENCIES:
        value = dependencies[name]
        archive = value.get("archive", {})
        if (value.get("commercial_use") is not True or value.get("version") != VERSIONS[name] or
                value.get("selected_license") != LICENSES[name] or
                type(archive.get("bytes")) is not int or not 0 < archive["bytes"] <= 32 * 1024 * 1024 or
                not str(archive.get("url", "")).startswith("https://") or
                re.fullmatch(r"[0-9a-f]{64}", str(archive.get("sha256", ""))) is None or
                not value.get("notices")):
            raise ValueError(f"Unreviewed source dependency: {name}")
        for notice, expected in value["notices"].items():
            if PurePosixPath(notice).name != notice:
                raise ValueError("Notice must be one retained license filename")
            notice_path = root / "licenses" / notice
            regular(notice_path)
            if pin(notice_path)["sha256"] != expected:
                raise ValueError(f"Reviewed notice changed: {notice}")
    return dependencies, pin(path)


def read_json_vendor(root: Path) -> dict:
    folder = root / "third_party/nlohmann_json"
    manifest = folder / "SOURCE.json"
    regular(manifest)
    value = json.loads(manifest.read_bytes())
    inventory = json.loads((root / "licenses/native-dependencies.json").read_bytes())
    selected = inventory["dependencies"]["nlohmann-json"]
    if (value.get("version") != "3.12.0" or value.get("selected_license") != "MIT" or
            value.get("commercial_use") is not True or selected.get("commercial_use") is not True or
            selected.get("vendor_manifest_sha256") != pin(manifest)["sha256"]):
        raise ValueError("Reviewed nlohmann JSON source manifest changed")
    expected = value["files_sha256"]
    actual = {row["path"]: row["sha256"] for row in prefix_files(folder) if row["path"] != "SOURCE.json"}
    if actual != expected:
        raise ValueError("Reviewed nlohmann JSON header/notice membership changed")
    return {"repository_path": "third_party/nlohmann_json/SOURCE.json", **pin(manifest),
            "files_sha256": expected}


def library_layout(profile: dict) -> dict:
    if profile["system"] == "win32":
        return {name: {"link": f"lib/{link}.lib", "runtime": f"bin/{runtime}.dll"}
                for name, link, runtime in (
                    ("zlib", "zlib", "zlib1"), ("jpeg", "jpeg", "jpeg62"),
                    ("lzma", "lzma", "liblzma"), ("png", "libpng16", "libpng16"),
                    ("glfw", "glfw3dll", "glfw3"), ("tiff", "tiff", "tiff"))}
    return {name: {"link": f"lib/{library}.a"} for name, library in (
        ("zlib", "libz"), ("jpeg", "libjpeg"), ("lzma", "liblzma"),
        ("png", "libpng16"), ("glfw", "libglfw3"), ("tiff", "libtiff"))}


def common_definitions(profile: dict, prefix: Path) -> dict:
    values = {"CMAKE_BUILD_TYPE": "Release", "CMAKE_POSITION_INDEPENDENT_CODE": "ON",
              "CMAKE_INSTALL_LIBDIR": "lib", "CMAKE_INSTALL_PREFIX": str(prefix),
              "CMAKE_PREFIX_PATH": str(prefix), "CMAKE_FIND_USE_PACKAGE_REGISTRY": "OFF",
              "CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY": "OFF"}
    if profile["system"] == "win32":
        values.update(CMAKE_POLICY_DEFAULT_CMP0091="NEW",
                      CMAKE_MSVC_RUNTIME_LIBRARY="MultiThreadedDLL")
    else:
        values.update(CMAKE_OSX_ARCHITECTURES=profile["architecture"],
                      CMAKE_OSX_DEPLOYMENT_TARGET=profile["deployment_target"])
    return values


def dependency_definitions(profile: dict, prefix: Path, nasm: str | None) -> dict:
    shared = profile["linkage"] == "shared"
    switch = "ON" if shared else "OFF"
    static = "OFF" if shared else "ON"
    layout = library_layout(profile)
    zlib = {"ZLIB_USE_STATIC_LIBS": static,
            "ZLIB_LIBRARY": str(prefix / layout["zlib"]["link"]),
            "ZLIB_LIBRARY_RELEASE": str(prefix / layout["zlib"]["link"]),
            "ZLIB_INCLUDE_DIR": str(prefix / "include")}
    jpeg = {"ENABLE_SHARED": switch, "ENABLE_STATIC": static, "WITH_TURBOJPEG": "OFF",
            "WITH_TOOLS": "OFF", "WITH_TESTS": "OFF", "WITH_SIMD": "ON"}
    if profile["architecture"] == "x86_64":
        if not nasm:
            raise ValueError("The x86_64 JPEG SIMD source profile requires NASM")
        jpeg["CMAKE_ASM_NASM_COMPILER"] = nasm
    return {
        # zlib 1.3.1 always defines both library targets. On Windows this
        # explicit option selects the reviewed zlib1.dll name (not zlib.dll).
        "zlib": {"ZLIB_BUILD_EXAMPLES": "OFF",
                 **({"BUILD_SHARED_LIBS": "ON"} if profile["system"] == "win32" else {})},
        "libjpeg-turbo": jpeg,
        "liblzma": {"BUILD_SHARED_LIBS": switch, "BUILD_TESTING": "OFF", "XZ_TOOL_XZ": "OFF",
                    "XZ_TOOL_XZDEC": "OFF", "XZ_TOOL_LZMADEC": "OFF", "XZ_TOOL_LZMAINFO": "OFF",
                    "XZ_TOOL_SCRIPTS": "OFF"},
        "png": {"PNG_SHARED": switch, "PNG_STATIC": static, "PNG_TESTS": "OFF", "PNG_TOOLS": "OFF",
                **({"PNG_FRAMEWORK": "OFF"} if profile["system"] == "darwin" else {}), **zlib},
        "glfw": {"BUILD_SHARED_LIBS": switch, "GLFW_BUILD_DOCS": "OFF", "GLFW_BUILD_TESTS": "OFF",
                 "GLFW_BUILD_EXAMPLES": "OFF", **({"GLFW_BUILD_COCOA": "ON"} if profile["system"] == "darwin" else {})},
        "tiff": {"BUILD_SHARED_LIBS": switch, "tiff-tools": "OFF", "tiff-tests": "OFF",
                 "tiff-contrib": "OFF", "tiff-docs": "OFF", "tiff-install": "ON", "cxx": "OFF",
                 "zlib": "ON", "libdeflate": "OFF", "jpeg": "ON", "old-jpeg": "ON", "jbig": "OFF",
                 "lzma": "ON", "zstd": "OFF", "webp": "OFF", "lerc": "OFF", **zlib,
                 "JPEG_LIBRARY_RELEASE": str(prefix / layout["jpeg"]["link"]),
                 "JPEG_INCLUDE_DIR": str(prefix / "include"),
                 "LIBLZMA_LIBRARY_RELEASE": str(prefix / layout["lzma"]["link"]),
                 "LIBLZMA_INCLUDE_DIR": str(prefix / "include")},
    }


def definitions_argv(values: dict) -> list[str]:
    return [f"-D{name}={value}" for name, value in values.items()]


def cache_values(path: Path) -> dict:
    result = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith(("//", "#")) or "=" not in line or ":" not in line.split("=", 1)[0]:
            continue
        key, value = line.split("=", 1)
        result[key.split(":", 1)[0]] = value
    return result


def check_effective(values: dict, requested: dict) -> dict:
    result = {}
    for key, wanted in requested.items():
        actual = values.get(key)
        if actual is None or (actual.replace("\\", "/") != str(wanted).replace("\\", "/")):
            raise ValueError(f"Effective CMake setting changed: {key}: {actual!r}, expected {wanted!r}")
        result[key] = actual
    return result


def extract_source(archive: Path, destination: Path) -> Path:
    """Reviewed hashes do not waive extraction bounds or path/link checks."""
    with tarfile.open(archive, "r:gz") as bundle:
        members = []
        seen = set()
        size = 0
        for member in bundle:
            size += member.size
            if len(members) >= 20000 or member.size < 0 or size > 128 * 1024 * 1024:
                raise ValueError("Dependency archive exceeds the bounded source envelope")
            name = PurePosixPath(member.name)
            if (not member.name or name.is_absolute() or ".." in name.parts or
                    "\\" in member.name or ":" in member.name or name.as_posix().casefold() in seen or
                    not (member.isfile() or member.isdir())):
                raise ValueError(f"Unsafe or duplicate dependency archive member: {member.name}")
            seen.add(name.as_posix().casefold())
            members.append(member)
        destination.mkdir()
        bundle.extractall(destination, members=members, filter="data")
    entries = list(destination.iterdir())
    if len(entries) != 1 or not entries[0].is_dir() or is_link(entries[0]):
        raise ValueError("Expected exactly one non-linked dependency source root")
    return entries[0]


def acquire(name: str, dependency: dict, downloads: Path, sources: Path,
            archive_cache: Path | None = None) -> Path:
    expected = dependency["archive"]
    archive = downloads / (name + ".tar.gz")
    cached = archive_cache / archive.name if archive_cache else None
    wanted = {key: expected[key] for key in ("bytes", "sha256")}
    if cached is not None and cached.exists():
        regular(cached)
        if pin(cached) != wanted:
            raise ValueError(f"Cached dependency fingerprint changed: {name}")
        shutil.copyfile(cached, archive)
    else:
        with urllib.request.urlopen(expected["url"], timeout=60) as response:
            raw = response.read(expected["bytes"] + 1)
        if len(raw) != expected["bytes"] or hashlib.sha256(raw).hexdigest() != expected["sha256"]:
            raise ValueError(f"Downloaded dependency fingerprint changed: {name}")
        archive.write_bytes(raw)
    return extract_source(archive, sources / name)


def prefix_files(prefix: Path) -> list[dict]:
    result = []
    if is_link(prefix) or not prefix.is_dir():
        raise ValueError("Installed prefix root must be a regular directory")
    for directory, directories, files in os.walk(prefix, followlinks=False):
        base = Path(directory)
        for name in directories:
            if is_link(base / name):
                raise ValueError("Installed prefix contains a linked directory")
        for name in files:
            path = base / name
            relative = path.relative_to(prefix).as_posix()
            regular(path)
            if relative == CERTIFICATE:
                continue
            suffix = path.suffix.lower()
            kind = "runtime" if suffix == ".dll" else "link" if suffix in (".lib", ".a") else "header" if relative.startswith("include/") else "metadata"
            result.append({"path": relative, "kind": kind, **pin(path)})
    return sorted(result, key=lambda row: row["path"])


def check_delivery(profile: dict, files: list[dict]) -> None:
    paths = {row["path"] for row in files}
    layout = library_layout(profile)
    selected = {relative for row in layout.values() for relative in row.values()}
    if not selected <= paths:
        raise ValueError("Installed source prefix is missing a selected library role")
    dynamic = {path for path in paths if Path(path).suffix.lower() in (".dll", ".dylib", ".so")}
    expected_dynamic = {row["runtime"] for row in layout.values() if "runtime" in row}
    if dynamic != expected_dynamic or any(Path(path).suffix.lower() == ".exe" for path in paths):
        raise ValueError("Installed source prefix contains an unselected runtime/tool")


def normalize_static_prefix(prefix: Path) -> list[dict]:
    """Retire unselected macOS shared zlib outputs and three known PNG aliases.

    Upstream zlib builds both variants. The static profile never delivers those
    dylibs. All removals are individual files in this freshly owned prefix; no
    recursive deletion or traversal through a link is performed.
    """
    prefix = checked_path(prefix)
    changes = []
    library = prefix / "lib"
    if is_link(library) or not library.is_dir():
        raise ValueError("Static prefix library directory must not be linked")
    for path in sorted(library.iterdir()):
        if not is_link(path) and path.is_dir():
            continue
        if ".dylib" in path.name:
            if not re.fullmatch(r"libz(?:\.\d+)*\.dylib", path.name):
                raise ValueError(f"Unexpected shared library in static profile: {path.name}")
            record = {"path": path.relative_to(prefix).as_posix(), "action": "retire-unselected-shared-zlib"}
            record.update({"link_target": os.readlink(path)} if is_link(path) else pin(path))
            path.unlink()
            changes.append(record)
        elif is_link(path) and path.name != "libpng.a":
            raise ValueError(f"Unreviewed static archive alias: {path.name}")
    # libpng 1.6.50 installs these same-directory, one-hop aliases even when
    # PNG_TOOLS is OFF. Keep their bytes and mode without admitting any links
    # into the sealed prefix. Its final inventory still rejects all other links.
    aliases = (("lib/libpng.a", "libpng16.a"),
               ("bin/libpng-config", "libpng16-config"),
               ("lib/pkgconfig/libpng.pc", "libpng16.pc"))
    materialize = []
    for relative, target_name in aliases:
        path = prefix / relative
        parent = checked_path(path.parent)
        if not parent.exists():
            continue
        if not parent.is_dir():
            raise ValueError("Libpng alias parent must be a regular directory")
        if not path.exists() and not path.is_symlink():
            continue
        if not is_link(path):
            regular(path)
            continue
        raw = os.readlink(path)
        if raw != target_name:
            raise ValueError(f"Unreviewed libpng alias target: {relative}")
        target = parent / target_name
        regular(target)
        action = "materialize-static-alias" if relative.endswith(".a") else "materialize-libpng-metadata-alias"
        materialize.append((path, target, {"path": relative, "action": action,
                                         "link_target": raw, **pin(target)}))
    for path, target, record in materialize:
        path.unlink()
        shutil.copyfile(target, path)
        shutil.copymode(target, path)
        changes.append(record)
    return changes


def write_tiff_configs(prefix: Path, profile: dict) -> None:
    """Supply all FindTIFF spellings; do not replace codec implementation code."""
    folder = prefix / "lib/cmake/tiff"
    version = (folder / "TiffConfigVersion.cmake").read_bytes()
    layout = library_layout(profile)
    adapter = 'get_filename_component(_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)\n'
    for target, name in (("ZLIB::ZLIB", "zlib"), ("JPEG::JPEG", "jpeg"), ("liblzma::liblzma", "lzma")):
        row = layout[name]
        shared = "runtime" in row
        location = row.get("runtime", row["link"])
        adapter += (f'if(NOT TARGET {target})\n add_library({target} {"SHARED" if shared else "STATIC"} IMPORTED)\n'
                    f' set_target_properties({target} PROPERTIES IMPORTED_LOCATION "${{_prefix}}/{location}"'
                    ' INTERFACE_INCLUDE_DIRECTORIES "${_prefix}/include"')
        if shared:
            adapter += f' IMPORTED_IMPLIB "${{_prefix}}/{row["link"]}"'
        adapter += ')\nendif()\n'
    adapter += 'if(NOT TARGET CMath::CMath)\n add_library(CMath::CMath INTERFACE IMPORTED)\n'
    if profile["system"] != "win32":
        adapter += ' set_target_properties(CMath::CMath PROPERTIES INTERFACE_LINK_LIBRARIES m)\n'
    adapter += ('endif()\ninclude("${CMAKE_CURRENT_LIST_DIR}/TiffTargets.cmake")\n'
                'set(tiff_FOUND TRUE)\nset(TIFF_FOUND TRUE)\n')
    for spelling in ("tiff-config", "TiffConfig", "TIFFConfig"):
        (folder / (spelling + ".cmake")).write_text(adapter, encoding="utf-8")
        version_name = spelling + ("-version" if spelling == "tiff-config" else "Version")
        (folder / (version_name + ".cmake")).write_bytes(version)


def write_json_config(prefix: Path, root: Path) -> None:
    source = root / "third_party/nlohmann_json"
    shutil.copytree(source / "include", prefix / "include", dirs_exist_ok=True)
    folder = prefix / "share/cmake/nlohmann_json"
    folder.mkdir(parents=True)
    (folder / "nlohmann_jsonConfig.cmake").write_text(
        'if(NOT TARGET nlohmann_json::nlohmann_json)\n'
        ' add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)\n'
        ' get_filename_component(_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)\n'
        ' set_target_properties(nlohmann_json::nlohmann_json PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${_prefix}/include")\n'
        'endif()\nset(nlohmann_json_VERSION 3.12.0)\n', encoding="utf-8")
    (folder / "nlohmann_jsonConfigVersion.cmake").write_text(
        'set(PACKAGE_VERSION "3.12.0")\nif(PACKAGE_FIND_VERSION VERSION_EQUAL PACKAGE_VERSION)\n'
        ' set(PACKAGE_VERSION_EXACT TRUE)\n set(PACKAGE_VERSION_COMPATIBLE TRUE)\nendif()\n', encoding="utf-8")


def check_tiff_imports(prefix: Path, output: Path, profile: dict, run) -> None:
    source = output / "source"
    source.mkdir(parents=True)
    (source / "CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.18)\nproject(TiffImports C)\n'
        'find_package(${PACKAGE_NAME} 4.7.1 EXACT CONFIG REQUIRED CONFIGS ${CONFIG_FILE})\n'
        'add_executable(consumer consumer.c)\ntarget_link_libraries(consumer PRIVATE TIFF::tiff)\n', encoding="utf-8")
    (source / "consumer.c").write_text(
        '#include <tiffio.h>\n#include <string.h>\nint main(void) {\n'
        ' if (!strstr(TIFFGetVersion(), "LIBTIFF, Version 4.7.1")) return 1;\n'
        ' if (!TIFFIsCODECConfigured(COMPRESSION_LZW) || !TIFFIsCODECConfigured(COMPRESSION_PACKBITS) ||\n'
        '     !TIFFIsCODECConfigured(COMPRESSION_ADOBE_DEFLATE) || !TIFFIsCODECConfigured(COMPRESSION_JPEG) ||\n'
        '     !TIFFIsCODECConfigured(COMPRESSION_LZMA)) return 2;\nreturn 0;\n}\n', encoding="utf-8")
    for package, config in (("tiff", "tiff-config.cmake"), ("Tiff", "TiffConfig.cmake"), ("TIFF", "TIFFConfig.cmake")):
        cache = output / package
        label = "tiff-import-" + package
        values = {**common_definitions(profile, prefix), "PACKAGE_NAME": package, "CONFIG_FILE": config}
        run(label + "-configure", ["cmake", "-S", source, "-B", cache, "-G", "Ninja", *definitions_argv(values)])
        run(label + "-build", ["cmake", "--build", cache, "--parallel", "2"])
        run(label + "-run", [cache / ("consumer.exe" if profile["system"] == "win32" else "consumer")])


def build(output: Path, profile_name: str, *, archive_cache: Path | None = None,
          nasm: str | None = None, root: Path = ROOT) -> None:
    profile = profile_definition(profile_name)
    require_host(profile)
    if not hasattr(tarfile, "data_filter"):
        raise ValueError("Use Python 3.12+ with the safe tar extraction filter")
    dependencies, manifest_pin = read_catalog(root)
    json_vendor = read_json_vendor(root)
    output = checked_path(output)
    if output.exists():
        raise ValueError("Use a fresh owned source-dependency output directory")
    if archive_cache is not None:
        archive_cache = checked_path(archive_cache)
    if profile["architecture"] == "x86_64":
        nasm = nasm or shutil.which("nasm")
        if not nasm:
            raise ValueError("NASM is required for x86_64 JPEG SIMD")
        nasm = str(Path(shutil.which(nasm) or nasm).resolve(strict=True))
    elif nasm is not None:
        raise ValueError("The arm64 source profile does not execute an x86 NASM tool")
    tool_names = ("cmake", "ninja", "xcrun") if profile["system"] == "darwin" else ("cmake", "ninja")
    tools = {name: shutil.which(name) for name in tool_names}
    if not all(tools.values()):
        raise ValueError("CMake and Ninja are required before source acquisition")
    tools = {name: str(Path(path).resolve(strict=True)) for name, path in tools.items()}
    if profile["system"] == "win32" and not shutil.which("cl"):
        raise ValueError("Use an initialized native x64 MSVC developer environment")
    prefix, sources, downloads, logs = (output / name for name in ("prefix", "sources", "downloads", "logs"))
    output.mkdir(parents=True)
    for path in (prefix, sources, downloads, logs):
        path.mkdir()
    evidence = prefix / "share/imagejxx-ci"
    evidence.mkdir(parents=True)
    prefix.joinpath("include").mkdir()
    receipt = {"schema": SCHEMA, "state": "running", "acceptance": False, "profile": profile_name,
               "source_manifest_path": SOURCE_MANIFEST, "source_manifest_sha256": manifest_pin["sha256"],
               "sources": dependencies, "configuration": {"platform": profile,
                   "builder": {"repository_path": "scripts/ci_platform_dependencies.py",
                               "path": "share/imagejxx-ci/ci_platform_dependencies.py", **pin(Path(__file__))},
                   "source_catalog": {"path": "share/imagejxx-ci/source-catalog.json", **manifest_pin},
                   "vendored_json": json_vendor,
                   "toolchain": {"python": {"version": sys.version,
                       "path": str(Path(sys.executable).resolve()), **pin(Path(sys.executable).resolve())}},
                   "dependencies": {}, "libraries": library_layout(profile)},
               "commands": [], "installed_files": []}
    shutil.copyfile(root / SOURCE_MANIFEST, evidence / "source-catalog.json")
    shutil.copyfile(Path(__file__), evidence / "ci_platform_dependencies.py")
    shutil.copyfile(root / json_vendor["repository_path"], evidence / "nlohmann-json-SOURCE.json")
    receipt["configuration"]["vendored_json"]["path"] = "share/imagejxx-ci/nlohmann-json-SOURCE.json"

    def save():
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")

    environment = os.environ.copy()
    if profile["system"] == "win32":
        environment["PATH"] = str(prefix / "bin") + os.pathsep + environment.get("PATH", "")

    def run(label, argv):
        argv = [str(value) for value in argv]
        argv[0] = tools.get(argv[0], argv[0])
        row = {"label": label, "argv": argv, "state": "running", "started": time.time()}
        receipt["commands"].append(row)
        save()
        try:
            with (logs / (label + ".log")).open("wb") as log:
                with subprocess.Popen(argv, stdout=log, stderr=subprocess.STDOUT, env=environment) as process:
                    row["pid"] = process.pid
                    try:
                        code = process.wait(timeout=900)
                    except BaseException:
                        process.kill()
                        process.wait()
                        raise
                    finally:
                        row.update(exit_code=process.returncode, reaped=process.returncode is not None)
            if code:
                raise subprocess.CalledProcessError(code, argv)
            row["state"] = "passed"
        except BaseException as error:
            row.update(state="failed", error={"type": type(error).__name__, "message": str(error)})
            raise
        finally:
            row["ended"] = time.time()
            save()

    def keep(relative: str, path: Path) -> dict:
        target = evidence / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
        return {"path": target.relative_to(prefix).as_posix(), **pin(target)}

    save()
    try:
        for name, executable in {**tools, **({"nasm": nasm} if nasm else {})}.items():
            regular(Path(executable))
            run(name + "-version", [executable, "--version"])
            receipt["configuration"]["toolchain"][name] = {"path": str(Path(executable).resolve()),
                **pin(Path(executable)), "version_evidence": keep(name + "-version.txt", logs / (name + "-version.log"))}
        probe = output / "compiler-source"
        probe.mkdir()
        (probe / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.18)\nproject(ProfileCompiler C CXX)\n'
            'file(WRITE "${CMAKE_BINARY_DIR}/compiler.txt" "system=${CMAKE_SYSTEM_NAME}\\n'
            'pointer_bytes=${CMAKE_SIZEOF_VOID_P}\\nc_id=${CMAKE_C_COMPILER_ID}\\nc_version=${CMAKE_C_COMPILER_VERSION}\\n'
            'c=${CMAKE_C_COMPILER}\\ncxx_id=${CMAKE_CXX_COMPILER_ID}\\ncxx_version=${CMAKE_CXX_COMPILER_VERSION}\\n'
            'cxx=${CMAKE_CXX_COMPILER}\\narchitecture=${CMAKE_C_COMPILER_ARCHITECTURE_ID}\\n'
            'sysroot=${CMAKE_OSX_SYSROOT}\\n")\n', encoding="utf-8")
        probe_cache = output / "compiler-build"
        common = common_definitions(profile, prefix)
        common["CMAKE_MAKE_PROGRAM"] = tools["ninja"]
        if profile["system"] == "darwin":
            run("sdk-path", [tools["xcrun"], "--show-sdk-path"])
            run("sdk-version", [tools["xcrun"], "--show-sdk-version"])
            sdk = Path((logs / "sdk-path.log").read_text().strip()).resolve(strict=True)
            regular(sdk / "SDKSettings.json")
            common["CMAKE_OSX_SYSROOT"] = str(sdk)
            receipt["configuration"]["toolchain"]["sdk"] = {
                "path": str(sdk), "version": (logs / "sdk-version.log").read_text().strip(),
                "settings": keep("SDKSettings.json", sdk / "SDKSettings.json")}
        run("compiler-configure", [tools["cmake"], "-S", probe, "-B", probe_cache, "-G", "Ninja", *definitions_argv(common)])
        info = dict(line.split("=", 1) for line in (probe_cache / "compiler.txt").read_text().splitlines())
        wanted_id = "MSVC" if profile["system"] == "win32" else "AppleClang"
        wanted_system = "Windows" if profile["system"] == "win32" else "Darwin"
        if (info["system"] != wanted_system or info["pointer_bytes"] != "8" or
                info["c_id"] != wanted_id or info["cxx_id"] != wanted_id or
                (profile["system"] == "win32" and info["architecture"] not in ("x64", "X64", "AMD64", "x86_64"))):
            raise ValueError("Actual C/C++ toolchain does not match the native source profile")
        compiler = {**info, "metadata": keep("compiler.txt", probe_cache / "compiler.txt"),
                    "cache": keep("compiler-CMakeCache.txt", probe_cache / "CMakeCache.txt")}
        for language in ("c", "cxx"):
            executable = Path(info[language]).resolve(strict=True)
            regular(executable)
            compiler[language + "_identity"] = {"resolved_path": str(executable), **pin(executable)}
        receipt["configuration"]["toolchain"]["compiler"] = compiler
        options = dependency_definitions(profile, prefix, nasm)
        for name in DEPENDENCIES[:-1]:
            source = acquire(name, dependencies[name], downloads, sources, archive_cache)
            cache = output / (name + "-build")
            requested = {**common, **options[name]}
            run(name + "-configure", [tools["cmake"], "-S", source, "-B", cache, "-G", "Ninja", *definitions_argv(requested)])
            effective = check_effective(cache_values(cache / "CMakeCache.txt"), requested)
            run(name + "-build", [tools["cmake"], "--build", cache, "--parallel", "2"])
            run(name + "-install", [tools["cmake"], "--install", cache])
            receipt["configuration"]["dependencies"][name] = {
                "requested": requested, "effective": effective,
                "cache": keep("configuration/" + name + ".CMakeCache.txt", cache / "CMakeCache.txt")}
            save()
        if profile["system"] == "darwin":
            receipt["configuration"]["normalization"] = normalize_static_prefix(prefix)
        for name, row in library_layout(profile).items():
            for role, relative in row.items():
                regular(prefix / relative)
        write_tiff_configs(prefix, profile)
        check_tiff_imports(prefix, output / "tiff-import-check", profile, run)
        stb = acquire("stb", dependencies["stb"], downloads, sources, archive_cache)
        for header, expected in dependencies["stb"]["headers"].items():
            regular(stb / header)
            if pin(stb / header)["sha256"] != expected:
                raise ValueError(f"Reviewed stb header changed: {header}")
            shutil.copyfile(stb / header, prefix / "include" / header)
        write_json_config(prefix, root)
        for name, dependency in dependencies.items():
            folder = prefix / "share/licenses/imagejxx-ci" / name
            folder.mkdir(parents=True)
            for notice in dependency["notices"]:
                shutil.copyfile(root / "licenses" / notice, folder / notice)
        folder = prefix / "share/licenses/imagejxx-ci/nlohmann_json"
        folder.mkdir()
        shutil.copyfile(root / "third_party/nlohmann_json/LICENSE.MIT", folder / "LICENSE.MIT")
        receipt["installed_files"] = prefix_files(prefix)
        check_delivery(profile, receipt["installed_files"])
        receipt["state"] = "prepared"
        (prefix / CERTIFICATE).write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    except BaseException as error:
        receipt.update(state="failed", error={"type": type(error).__name__, "message": str(error)})
        raise
    finally:
        save()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", required=True, choices=tuple(PROFILES))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--archive-cache", type=Path)
    parser.add_argument("--nasm")
    args = parser.parse_args()
    build(args.output, args.profile, archive_cache=args.archive_cache, nasm=args.nasm)


if __name__ == "__main__":
    main()
