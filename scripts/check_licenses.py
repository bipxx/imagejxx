"""Check declared and resolved Python dependencies against the reviewed inventory.

This enforces this project's recorded dependency policy. It does not infer
commercial permission from a package name or audit native libraries in wheels.
"""

from __future__ import annotations
import argparse
import hashlib
from importlib import metadata
import json
import os
from pathlib import Path
import re
import sys

try:
    import tomllib
except ImportError:
    import tomli as tomllib
from packaging.requirements import Requirement
from packaging.utils import canonicalize_name

ROOT = Path(__file__).resolve().parents[1]
APPROVED = {
    "MIT",
    "MIT-CMU",
    "BSD-2-Clause",
    "BSD-3-Clause",
    "Apache-2.0",
    "MPL-2.0",
    "PSF-2.0",
    "Apache-2.0 OR BSD-2-Clause",
}
APPROVED_DESKTOP = {
    "MIT",
    "Zlib",
    "Libpng-2.0",
    "libtiff",
    "0BSD",
    "BSD-3-Clause AND IJG AND Zlib",
}


def check_tiff_runtime(prefix: Path | None = None) -> list[str]:
    """Check the exact reviewed optional Windows TIFF closure, separate from core."""
    expected = {
        "tiff": ("4.7.1", "libtiff"),
        "libjpeg-turbo": ("3.1.3", "BSD-3-Clause AND IJG AND Zlib"),
        "liblzma": ("5.8.2", "0BSD"),
        "zlib": ("1.3.1", "Zlib"),
    }
    runtime = {
        "bin/tiff.dll": "cf4a9d2a74f3370983bef5aacb8bb93db00328906da8e2c26cfed8d744fd9b07",
        "bin/jpeg62.dll": "874283fd82bab9e0a572c78befea478827bfddacb0310f9bc23382bb7b8becba",
        "bin/liblzma.dll": "a9a275c360d80013b8304b5ebde3a05a82aee4c2ccdc41dd3ee9cf6acb3d433c",
        "bin/zlib1.dll": "0389d9547623b8b05e576f01229a2b37f1a0709fac02a8a50b2da723da5ca514",
    }
    errors = []
    try:
        inventory = json.loads((ROOT / "licenses/io-dependencies.json").read_bytes())
        dependencies = inventory["dependencies"]
        if (inventory.get("schema_version") != 1
                or inventory.get("profile") != "windows-x64-dynamic-release-001"
                or set(dependencies) != set(expected)):
            return ["Unreviewed TIFF dependency closure"]
        for name, (version, license_id) in expected.items():
            entry = dependencies[name]
            if (entry.get("commercial_use") is not True
                    or entry.get("tested_version") != version
                    or entry.get("selected_license") != license_id):
                errors.append(f"Unreviewed TIFF dependency: {name}")
            archive = entry["source_archive"]
            if (not archive["url"].startswith("https://")
                    or re.fullmatch(r"[0-9a-f]{128}", archive["sha512"]) is None
                    or re.fullmatch(r"[0-9a-f]{64}", archive["sha256"]) is None
                    or not entry["port_files"]
                    or re.fullmatch(r"[0-9a-f]{64}", entry["binary_abi"]) is None):
                errors.append(f"Missing TIFF source/port/ABI identity: {name}")
            for notice, digest in entry["notices"].items():
                path = ROOT / "licenses" / notice
                if (path.parent != ROOT / "licenses" or path.is_symlink()
                        or hashlib.sha256(path.read_bytes()).hexdigest() != digest):
                    errors.append(f"TIFF retained notice changed: {notice}")
        files = inventory["files"]
        paths = {row["path"] for row in files}
        if paths != set(runtime) | {"lib/tiff.lib", "lib/zlib.lib", "include/tiff.h",
                                  "include/tiffio.h", "include/tiffconf.h", "include/tiffvers.h"} or len(paths) != len(files):
            errors.append("TIFF reviewed artifact membership changed")
        generated = "# Generated from licenses/io-dependencies.json; exact reviewed Windows x64 artifacts.\nset(IMAGEJXX_TIFF_REVIEWED_FILES\n"
        for row in files:
            name = row["path"]
            if name in runtime and row["sha256"] != runtime[name]:
                errors.append(f"Unreviewed TIFF runtime binary: {name}")
            if (row["kind"] not in {"runtime", "header", "link"}
                    or type(row["bytes"]) is not int or row["bytes"] <= 0
                    or re.fullmatch(r"[0-9a-f]{64}", row["sha256"]) is None):
                errors.append(f"Invalid TIFF artifact identity: {name}")
            generated += f'  "{row["kind"]}|{name}|{row["sha256"]}|{row["bytes"]}"\n'
            if prefix is not None and name in paths:
                target = (prefix / name).resolve()
                if (not target.is_relative_to(prefix.resolve()) or (prefix / name).is_symlink()
                        or not target.is_file() or target.stat().st_size != row["bytes"]
                        or hashlib.sha256(target.read_bytes()).hexdigest() != row["sha256"]):
                    errors.append(f"TIFF installed artifact changed/missing: {name}")
        generated += ")\n"
        manifest = ROOT / inventory["cmake_manifest"]
        if (manifest != ROOT / "cmake/TiffRuntimeManifest.cmake" or manifest.is_symlink()
                or manifest.read_bytes() != generated.encode()
                or hashlib.sha256(generated.encode()).hexdigest() != inventory["cmake_manifest_sha256"]):
            errors.append("TIFF CMake artifact manifest differs from reviewed inventory")
    except (OSError, ValueError, KeyError, TypeError) as error:
        errors.append(f"TIFF dependency review unavailable/malformed: {error}")
    return errors


def check_native() -> list[str]:
    """Check reviewed private core inputs and their complete source fingerprints."""
    errors = []
    try:
        inventory = json.loads(
            (ROOT / "licenses/native-dependencies.json").read_text(encoding="utf-8")
        )
        dependencies = inventory["dependencies"]
        if set(dependencies) != {"nlohmann-json", "zlib"}:
            return ["Unreviewed or missing native core dependency"]
        reviewed = (
            ("nlohmann-json", "JSON", "3.12.0", "MIT",
             "https://github.com/nlohmann/json/blob/v3.12.0/LICENSE.MIT",
             "third_party/nlohmann_json/SOURCE.json", "nlohmann-json.txt", "LICENSE.MIT"),
            ("zlib", "zlib", "1.3.1", "Zlib",
             "https://www.zlib.net/zlib_license.html",
             "third_party/zlib/SOURCE.json", "zlib.txt", "LICENSE"),
        )
        for name, label, version, license_id, source, manifest_name, notice, vendor_notice in reviewed:
            entry = dependencies[name]
            if (entry.get("commercial_use") is not True
                    or entry.get("selected_license") != license_id
                    or entry.get("tested_version") != version
                    or entry.get("source") != source):
                errors.append(f"Missing reviewed commercial-use native {label} dependency")
            if entry.get("vendor_manifest") != manifest_name:
                errors.append(f"Invalid native {label} source manifest path")
                continue
            manifest_path = ROOT / manifest_name
            if manifest_path.is_symlink() or manifest_path.parent.is_symlink():
                errors.append(f"Native {label} source manifest must be a regular local file")
                continue
            manifest_bytes = manifest_path.read_bytes()
            if hashlib.sha256(manifest_bytes).hexdigest() != entry.get("vendor_manifest_sha256"):
                errors.append(f"Native {label} source manifest fingerprint changed")
            manifest = json.loads(manifest_bytes.decode("utf-8"))
            if (manifest.get("name") != name
                    or manifest.get("version") != version
                    or manifest.get("commercial_use") is not True
                    or manifest.get("selected_license") != license_id):
                errors.append(f"Native {label} source identity differs from reviewed dependency")
            if name == "zlib" and (
                    manifest.get("schema_version") != 1
                    or manifest.get("source") != "https://zlib.net/fossils/zlib-1.3.1.tar.gz"
                    or manifest.get("release_source") != "https://github.com/madler/zlib/releases/tag/v1.3.1"
                    or manifest.get("archive_sha256") != "9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23"
                    or manifest.get("notice") != "LICENSE"):
                errors.append("Native zlib source archive or retained notice identity changed")
            vendor = manifest_path.parent.resolve()
            files = manifest["files_sha256"]
            if not isinstance(files, dict) or not files:
                errors.append(f"Missing native {label} source fingerprints")
                continue
            members = list(vendor.rglob("*"))
            if any(path.is_symlink() for path in members):
                errors.append(f"Native {label} source tree contains an unreviewed symbolic link")
            actual = {path.relative_to(vendor).as_posix() for path in members
                      if path.is_file() and path != manifest_path.resolve()}
            if actual != set(files):
                errors.append(f"Native {label} source membership differs from reviewed complete tree")
            for member, expected in files.items():
                if (not isinstance(member, str) or "\\" in member or ":" in member
                        or Path(member).is_absolute()
                        or any(part in {"", ".", ".."} for part in member.split("/"))):
                    errors.append(f"Invalid native {label} source fingerprint path/value")
                    continue
                target = (vendor / member).resolve()
                if (not target.is_relative_to(vendor) or not isinstance(expected, str)
                        or re.fullmatch(r"[0-9a-f]{64}", expected) is None):
                    errors.append(f"Invalid native {label} source fingerprint path/value")
                elif not target.is_file() or hashlib.sha256(target.read_bytes()).hexdigest() != expected:
                    errors.append(f"Native {label} source fingerprint changed: {member}")
            if entry.get("notice") != notice:
                errors.append(f"Invalid native {label} retained notice path")
            else:
                retained_notice = ROOT / "licenses" / notice
                if (retained_notice.is_symlink()
                        or retained_notice.read_bytes() != (vendor / vendor_notice).read_bytes()):
                    errors.append(f"Native {label} source and retained license notice differ")
    except (OSError, ValueError, KeyError, TypeError) as error:
        errors.append(f"Native dependency review unavailable or malformed: {error}")
    return errors


def check_imgui() -> list[str]:
    """Check the complete unmodified, commercially reviewed desktop source set."""
    try:
        entry=json.loads((ROOT/'licenses/desktop-dependencies.json').read_text(encoding='utf-8'))['dependencies']['imgui']
        manifest_path=ROOT/'third_party/imgui/SOURCE.json'
        raw=manifest_path.read_bytes();manifest=json.loads(raw)
        if (entry.get('vendor_manifest')!='third_party/imgui/SOURCE.json'
                or hashlib.sha256(raw).hexdigest()!=entry.get('vendor_manifest_sha256')
                or entry.get('compile_definition')!='IMGUI_USE_WCHAR32'
                or entry.get('commercial_use') is not True
                or manifest.get('commercial_use') is not True
                or manifest.get('selected_license')!='MIT'
                or manifest.get('name')!='dear-imgui'
                or manifest.get('version')!='1.91.9-docking'
                or manifest.get('archive_sha512')!='3a019533e638b2023e0c71d0455561ee9d589ff33b677c8135345e85c28edb7b83580271d49a787e0e838fd6217d1625baad65e6559ae7faec53f3a309917ecd'):
            return ['Unreviewed ImGui source identity, configuration or archive']
        vendor=manifest_path.parent
        def linked(p):
            return p.is_symlink() or getattr(p.lstat(),'st_file_attributes',0)&0x400
        if linked(vendor):
            return ['ImGui source tree is unbounded or contains a link']
        members=[]
        for directory,dirs,names in os.walk(vendor,followlinks=False):
            for name in dirs+names:
                p=Path(directory)/name
                if len(members)>=32 or linked(p):
                    return ['ImGui source tree is unbounded or contains a link']
                members.append(p)
        expected=manifest['files_sha256']
        actual={p.relative_to(vendor).as_posix() for p in members if p.is_file() and p!=manifest_path}
        if not isinstance(expected,dict) or len(expected)!=20 or actual!=set(expected):
            return ['ImGui complete source membership differs from review']
        for name,digest in expected.items():
            if (not isinstance(name,str) or '\\' in name or ':' in name or Path(name).is_absolute()
                    or any(p in ('','.','..') for p in name.split('/'))
                    or not isinstance(digest,str) or re.fullmatch('[a-f0-9]{64}',digest) is None):
                return ['Invalid reviewed ImGui member identity']
            p=vendor/name
            if p.stat().st_size>4*1024*1024 or hashlib.sha256(p.read_bytes()).hexdigest()!=digest:
                return ['Reviewed ImGui source bytes changed: '+name]
        if (vendor/'LICENSE.txt').read_bytes()!=(ROOT/'licenses/imgui.txt').read_bytes():
            return ['ImGui source and retained MIT notice differ']
    except (OSError,ValueError,KeyError,TypeError) as error:
        return ['ImGui source review unavailable or malformed: '+str(error)]
    return []


def check_desktop() -> list[str]:
    """Validate the recorded native UI review and retained notices, not binary ABIs."""
    directory = ROOT / "licenses"
    inventory = json.loads(
        (directory / "desktop-dependencies.json").read_text(encoding="utf-8")
    )
    errors = []
    for name, entry in inventory["dependencies"].items():
        if (
            entry.get("commercial_use") is not True
            or entry.get("selected_license") not in APPROVED_DESKTOP
            or not entry.get("source", "").startswith("https://")
            or not entry.get("tested_version")
        ):
            errors.append(f"Missing approved commercial-use desktop review: {name}")
        notice = directory / entry.get("notice", "")
        if (
            notice.parent != directory
            or not notice.is_file()
            or not notice.read_bytes().strip()
        ):
            errors.append(f"Missing retained desktop notice: {name}")
    return errors + check_imgui() + check_tiff_runtime()


def approved(entry: dict) -> bool:
    return (
        entry.get("commercial_use") is True
        and entry.get("license") in APPROVED
        and entry.get("source", "").startswith("https://")
    )


def check(
    *, installed: bool = False, extras: tuple[str, ...] = (), build: bool = False
) -> list[str]:
    project = tomllib.loads((ROOT / "pyproject.toml").read_text(encoding="utf-8"))
    inventory = json.loads(
        (ROOT / "licenses/dependencies.json").read_text(encoding="utf-8")
    )["dependencies"]
    errors = []
    declared = project["project"]["dependencies"] + project["build-system"]["requires"]
    for requirements in project["project"].get("optional-dependencies", {}).values():
        declared += requirements
    for specification in declared:
        requirement = Requirement(specification)
        name = canonicalize_name(requirement.name)
        entry = inventory.get(name)
        if entry is None:
            errors.append(f"Unreviewed dependency: {name}")
        elif not approved(entry):
            errors.append(f"Missing approved commercial-use license evidence: {name}")
        elif str(requirement.specifier) != f"=={entry['version']}":
            errors.append(
                f"Dependency must pin reviewed version {entry['version']}: {specification}"
            )
    if not installed:
        return errors

    active = list(project["project"]["dependencies"])
    for extra in extras:
        if extra not in project["project"].get("optional-dependencies", {}):
            errors.append(f"Unknown extra: {extra}")
        else:
            active += project["project"]["optional-dependencies"][extra]
    if build:
        active += project["build-system"]["requires"]
    queue = [Requirement(text) for text in active]
    seen = set()
    while queue:
        requirement = queue.pop()
        if requirement.marker and not any(
            requirement.marker.evaluate({"extra": extra}) for extra in ("", *extras)
        ):
            continue
        name = canonicalize_name(requirement.name)
        key = (name, tuple(sorted(requirement.extras)))
        try:
            distribution = metadata.distribution(name)
        except metadata.PackageNotFoundError:
            errors.append(f"Dependency not installed: {name}")
            continue
        entry = inventory.get(name)
        if entry is None:
            errors.append(f"Unreviewed transitive dependency: {name}")
            continue
        if not approved(entry):
            errors.append(f"Missing approved commercial-use license evidence: {name}")
        if distribution.version != entry[
            "version"
        ] or not requirement.specifier.contains(distribution.version):
            errors.append(
                f"Unreviewed or incompatible installed version: {name}=={distribution.version}; reviewed {entry['version']}"
            )
        if key in seen:
            continue
        seen.add(key)
        for specification in distribution.requires or ():
            dependency = Requirement(specification)
            if dependency.marker and not any(
                dependency.marker.evaluate({"extra": extra})
                for extra in ("", *requirement.extras)
            ):
                continue
            dependency.marker = (
                None  # Already evaluated in the owning package's extra context.
            )
            queue.append(dependency)
    return sorted(set(errors))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--installed", action="store_true")
    parser.add_argument("--extras", default="", help="Comma-separated installed extras")
    parser.add_argument(
        "--build",
        action="store_true",
        help="Include build dependencies in the installed graph",
    )
    parser.add_argument(
        "--desktop",
        action="store_true",
        help="Also validate the native UI review and notices",
    )
    parser.add_argument("--tiff-runtime-prefix", type=Path,
                        help="Verify the exact reviewed optional Windows TIFF artifacts")
    args = parser.parse_args()
    errors = check(
        installed=args.installed,
        extras=tuple(x for x in args.extras.split(",") if x),
        build=args.build,
    )
    errors += check_native()
    if args.desktop:
        errors += check_desktop()
    if args.tiff_runtime_prefix is not None:
        errors += check_tiff_runtime(args.tiff_runtime_prefix)
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("Reviewed commercial-use dependency inventory passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
