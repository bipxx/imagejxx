"""Verify wheel, source archive and optional installed files against this checkout."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def verify(wheel, sdist, installed=False):
    checked = []
    with zipfile.ZipFile(wheel) as archive:
        def match(source, target, artifact="wheel", actual=None):
            payload = archive.read(target) if actual is None else actual
            require(payload == source.read_bytes(), f"{artifact}: {target} differs from {source}")
            checked.append({"artifact": artifact, "member": target})

        for source in sorted((ROOT / "include/imagejxx").rglob("*.hpp")):
            match(source, source.relative_to(ROOT).as_posix())
        for source in sorted((ROOT / "src/imagejxx").rglob("*")):
            if source.is_file() and source.suffix in {".py", ".json", ".typed"}:
                match(source, source.relative_to(ROOT / "src").as_posix())
        for source in (ROOT / "LICENSE", ROOT / "THIRD_PARTY_NOTICES.md"):
            match(source, f"imagejxx/licenses/{source.name}")
        for source in sorted((ROOT / "licenses").glob("*")):
            if source.is_file():
                match(source, f"imagejxx/licenses/{source.name}")
        require(
            b"find_dependency(Threads)" in archive.read("lib/cmake/imagejxx/imagejxxConfig.cmake"),
            "Installed SDK must resolve its Threads dependency",
        )
        if installed:
            import imagejxx
            package = Path(imagejxx.__file__).parent
            for target in archive.namelist():
                path = PurePosixPath(target)
                if path.parts[0] != "imagejxx" or path.suffix not in {
                    ".py", ".json", ".typed", ".pyd", ".so", ".dll", ".dylib", ".txt"
                }:
                    continue
                match(package.joinpath(*path.parts[1:]), target, "installed")
    with tarfile.open(sdist) as archive:
        members = [member for member in archive.getmembers() if member.isfile()]
        prefixes = {PurePosixPath(member.name).parts[0] for member in members}
        require(len(prefixes) == 1, "Source archive must have one root directory")
        prefix = prefixes.pop() + "/"
        names = {member.name.removeprefix(prefix) for member in members}
        excluded = {"build", "dist", ".tools", ".venv", ".cache", "__pycache__"}
        require(
            not any(set(PurePosixPath(name).parts) & excluded for name in names),
            "Source archive contains generated/environment directories",
        )
        require("cmake/command_catalog.hpp.in" not in names, "Obsolete generated catalog template is present")
        sources = []
        for subtree in ("include", "src", "tests", "examples", "scripts", "docs", "licenses", "cmake", ".github", "third_party"):
            sources.extend(source for source in (ROOT / subtree).rglob("*")
                           if source.is_file() and "__pycache__" not in source.parts)
        sources.extend(ROOT / name for name in (
            "CMakeLists.txt", "pyproject.toml", "README.md", "LICENSE", "THIRD_PARTY_NOTICES.md"
        ))
        for source in sorted(sources):
            target = source.relative_to(ROOT).as_posix()
            require(target in names, f"sdist: missing {target}")
            payload = archive.extractfile(prefix + target)
            require(payload is not None and payload.read() == source.read_bytes(),
                    f"sdist: {target} differs from the checkout")
            checked.append({"artifact": "sdist", "member": target})
    return {
        "schema_version": 1,
        "wheel": str(wheel),
        "wheel_sha256": hashlib.sha256(wheel.read_bytes()).hexdigest(),
        "sdist": str(sdist),
        "sdist_sha256": hashlib.sha256(sdist.read_bytes()).hexdigest(),
        "verified_members": checked,
        "excluded_generated_or_environment_directories": sorted(excluded),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wheel", type=Path, required=True)
    parser.add_argument("--sdist", type=Path, required=True)
    parser.add_argument("--installed", action="store_true")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        record = verify(args.wheel, args.sdist, args.installed)
    except (ValueError, OSError, KeyError, tarfile.TarError, zipfile.BadZipFile) as error:
        parser.exit(1, f"Distribution check failed: {error}\n")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(f"Verified {len(record['verified_members'])} wheel/installed/source members against current files.")


if __name__ == "__main__":
    main()
