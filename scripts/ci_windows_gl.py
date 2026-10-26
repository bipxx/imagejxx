"""Temporarily supply the reviewed Mesa WGL tool to one owned Windows CI run.

Run only after pip/wheel/SDK packaging. This does not modify product packaging,
install a system driver, execute vendor deployment scripts, or qualify OpenGL.
The command must run the unchanged independent GL readers. Only receipts, logs
and the pinned notice bundle belong in the output; provider bytes never do.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import importlib.util
import json
import math
import ntpath
import os
from pathlib import Path
import re
import shutil
import stat
import sys
import tempfile
import time
import urllib.parse
import urllib.request


SCHEMA = "imagejxx.windows-gl.001"
ASSET = {
    "url": "https://github.com/pal1000/mesa-dist-win/releases/download/26.2.4/mesa3d-26.2.4-release-msvc.7z",
    "asset_id": 609112860, "bytes": 70257286,
    "sha256": "351fc8c8b695878ffb3eaa044b3ead08672a48b1a045e3c3e3975811df0f6695",
}
MEMBERS = {
    "x64/opengl32.dll": {"bytes": 139264, "sha256": "75b295fc80df0eaa8f1264c28663f6932c8b72d36513eb092556608858b7f5e0"},
    "x64/libgallium_wgl.dll": {"bytes": 61868032, "sha256": "53e49adf87590d5b0cab8553b1e98084812040cc1fca8c487695b17b7af3702e"},
}
NOTICE_NAME = "imagejxx-ci-mesa-notices.zip"
MAX_NOTICE_BYTES = 24 * 1024 * 1024
MAX_LOG_BYTES = 128 * 1024 * 1024  # Terminal check, not a claimed live limit.
OVERRIDES = ("MESA_GL_VERSION_OVERRIDE", "MESA_GLSL_VERSION_OVERRIDE", "MESA_LOADER_DRIVER_OVERRIDE")


def _require(condition, message):
    if not condition:
        raise ValueError(message)


def _path(path, *, exists=True):
    path = Path(os.path.abspath(os.fspath(path)))
    for part in reversed((path, *path.parents)):
        try:
            info = part.lstat()
        except FileNotFoundError:
            _require(not exists and part == path, "Missing path ancestor: " + str(part))
            continue
        _require(not stat.S_ISLNK(info.st_mode) and not getattr(info, "st_file_attributes", 0) & 0x400,
                 "Linked/reparse path refused: " + str(part))
        if part != path:
            _require(stat.S_ISDIR(info.st_mode), "Non-directory ancestor: " + str(part))
    return path


def _pin(path):
    path = _path(path)
    before = path.stat()
    _require(stat.S_ISREG(before.st_mode) and before.st_nlink == 1, "Expected single-link regular file: " + str(path))
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        opened = os.fstat(stream.fileno())
        _require((opened.st_dev, opened.st_ino) == (before.st_dev, before.st_ino), "File changed while opening")
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
        after = os.fstat(stream.fileno())
    _require((before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) ==
             (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns), "File changed while hashing")
    return {"path": str(path), "bytes": after.st_size, "sha256": digest.hexdigest()}


def _identity(path):
    info = _path(path).stat()
    return [info.st_dev, info.st_ino]


def _save(path, row):
    raw = (json.dumps(row, indent=2, sort_keys=True, allow_nan=False) + "\n").encode("utf-8")
    _require(len(raw) <= 1024 * 1024, "Provider receipt exceeds 1 MiB")
    temporary = path.with_name(path.name + ".writing")
    with temporary.open("xb") as stream:
        stream.write(raw)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def _source_module(root, name):
    path = _path(root / "scripts" / (name + ".py"))
    before = _pin(path)
    source = path.read_bytes()
    _require(len(source) == before["bytes"] and hashlib.sha256(source).hexdigest() == before["sha256"],
             "Helper changed while reading")
    spec = importlib.util.spec_from_file_location("imagejxx_gl_" + name, path)
    module = importlib.util.module_from_spec(spec)
    exec(compile(source, str(path), "exec", dont_inherit=True), module.__dict__)
    _require(_pin(path) == before, "Helper changed while loading")
    return module, before


def _load_owner(root):
    module, identity = _source_module(root, "ci_owned_process")
    return module.run_owned, identity


def _ci_context(root):
    _require(sys.platform == "win32" and os.environ.get("GITHUB_ACTIONS") == "true"
             and os.environ.get("RUNNER_OS") == "Windows" and os.environ.get("RUNNER_ARCH") == "X64"
             and os.environ.get("RUNNER_ENVIRONMENT") == "github-hosted" and sys.maxsize > 2**32,
             "Mesa staging is restricted to disposable Windows x64 GitHub CI")
    _require(os.environ.get("GITHUB_WORKSPACE") and _path(os.environ["GITHUB_WORKSPACE"]) == root,
             "Repository differs from the CI workspace")
    _require(os.environ.get("RUNNER_TEMP"), "CI runner temporary directory is unavailable")
    temporary = _path(os.environ["RUNNER_TEMP"])
    _require(temporary.is_dir() and not temporary.is_relative_to(root) and not root.is_relative_to(temporary),
             "Runner temporary storage must be separate from the checkout")
    return temporary


def _executables(application):
    # Windows venv python.exe can be a launcher. Stage its actual base interpreter
    # too; neither spelling alone establishes the complete process load path.
    result = []
    for value in (sys.executable, getattr(sys, "_base_executable", sys.executable), application):
        item = _pin(value)
        if item["path"] not in [row["path"] for row in result]:
            result.append(item)
    return result


def _download(archive_path):
    class Redirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, request, fp, code, message, headers, newurl):
            parsed = urllib.parse.urlparse(newurl)
            _require(parsed.scheme == "https" and parsed.hostname in {
                "github.com", "release-assets.githubusercontent.com", "objects.githubusercontent.com"},
                "Unapproved asset redirect")
            return super().redirect_request(request, fp, code, message, headers, newurl)
    deadline = time.monotonic() + 300
    opener = urllib.request.build_opener(Redirect())
    request = urllib.request.Request(ASSET["url"], headers={"User-Agent": "imagejxx-ci-renderer-intake"})
    with opener.open(request, timeout=60) as response, archive_path.open("xb") as stream:
        length = response.headers.get("Content-Length")
        _require(length is None or int(length) == ASSET["bytes"], "Asset Content-Length differs")
        count = 0
        while True:
            _require(time.monotonic() <= deadline, "Asset download exceeded deadline")
            data = response.read(min(1024 * 1024, ASSET["bytes"] + 1 - count))
            if not data:
                break
            count += len(data)
            _require(count <= ASSET["bytes"], "Asset download exceeds reviewed size")
            stream.write(data)
        _require(count == ASSET["bytes"], "Incomplete asset download")


def _proof(path, *, argv=None, cwd=None, helper=None):
    _require(_pin(path)["bytes"] <= 1024 * 1024, "Owned proof exceeds bound")
    row = json.loads(path.read_text(encoding="utf-8"))
    _require(row.get("schema") == "imagejxx.owned-command.001" and row.get("state") in {
        "exited", "timed_out", "launch_failed", "ownership_failed", "supervisor_failed", "unsupported"}
             and row.get("cleanup_complete") is True,
             "Owned process cleanup is unresolved")
    _require((argv is None or row.get("argv") == argv) and (cwd is None or row.get("cwd") == str(cwd))
             and (helper is None or row.get("helper") == helper), "Owned proof identity differs")
    return row


def _command(owner, argv, root, env, output, name, timeout, commands):
    receipt = output / (name + "-process.json")
    log = output / (name + ".log")
    command = {"name": name, "path": str(receipt), "argv": argv, "log_path": str(log)}
    commands.append(command)
    _save(output / "commands.json", {"commands": commands})
    with log.open("xb") as stream:
        returned = owner(argv, cwd=root, env=env, timeout=timeout, stdout=stream, stderr=stream,
                         receipt_path=receipt)
    proof = _proof(receipt, argv=argv, cwd=root)
    _require(proof == returned,
             "Owned process proof differs from command")
    command.update(process=_pin(receipt), log=_pin(log))
    _require(command["log"]["bytes"] <= MAX_LOG_BYTES, "Owned command log exceeds terminal byte limit")
    _require(proof.get("state") == "exited" and proof.get("exit_code") == 0 and
             proof.get("target_completed") is True and proof.get("timed_out") is False,
             "Owned command did not complete successfully")
    return proof


def _extract(root, archive_path, scratch, output, seven_zip, owner, env, commands):
    extracted = scratch / "extracted"
    extracted.mkdir()
    argv = [str(seven_zip), "x", str(archive_path), "-y", "-r-", "-spd", "-o" + str(extracted), *MEMBERS]
    _command(owner, argv, root, env, output, "extract", 180, commands)


def _selected_files(scratch):
    extracted = _path(scratch / "extracted")
    _require({item.name for item in extracted.iterdir()} == {"x64"}, "Unexpected extraction root members")
    directory = _path(extracted / "x64")
    _require(directory.is_dir() and {item.name for item in directory.iterdir()} ==
             {Path(name).name for name in MEMBERS}, "Unexpected extracted files")
    rows = []
    for name, expected in MEMBERS.items():
        actual = _pin(extracted / name)
        _require({key: actual[key] for key in ("bytes", "sha256")} == expected,
                 "Extracted provider file differs: " + name)
        rows.append(actual)
    return rows


def verify_c2_runtime(stage_manifest, decoded_reports):
    """Join four authenticated C2 reports to the immutable staged provider.

    Pure data validation: the caller authenticates the stage JSON pin, current
    no-link file identities/content and the existing C2 reader/process proofs.
    No DLL is opened, loaded or rehashed here; scientific observations remain
    the responsibility of the unchanged appearance reader.
    """
    def pin_key(pin):
        _require(isinstance(pin, dict) and set(pin) == {"path", "bytes", "sha256"}, "Invalid runtime artifact pin")
        path = pin["path"]
        _require(isinstance(path, str) and path and "\0" not in path and
                 not any(part in {".", ".."} for part in path.replace("\\", "/").split("/")),
                 "Runtime path contains a dot/parent component")
        drive, tail = ntpath.splitdrive(path)
        _require(drive and tail.startswith(("/", "\\")) and not path.endswith(("/", "\\"))
                 and ":" not in tail, "Runtime artifact requires an absolute Windows file path")
        _require(type(pin["bytes"]) is int and 0 < pin["bytes"] <= 128 * 1024 * 1024 and
                 isinstance(pin["sha256"], str) and re.fullmatch(r"[0-9a-f]{64}", pin["sha256"]),
                 "Invalid runtime artifact size/hash")
        return ntpath.normcase(ntpath.normpath(path))

    def same_pin(left, right):
        return pin_key(left) == pin_key(right) and all(left[key] == right[key] for key in ("bytes", "sha256"))

    _require(isinstance(stage_manifest, dict) and stage_manifest.get("schema") == "imagejxx.windows-gl-stage.001"
             and stage_manifest.get("state") == "staged" and stage_manifest.get("asset") == ASSET
             and stage_manifest.get("members") == MEMBERS, "Stage does not name the exact reviewed provider")
    executable_rows = stage_manifest.get("executables")
    _require(isinstance(executable_rows, list) and 1 <= len(executable_rows) <= 3, "Invalid staged executable set")
    executables = {pin_key(pin): pin for pin in executable_rows}
    _require(len(executables) == len(executable_rows), "Duplicate staged executable")
    directories = {ntpath.dirname(path) for path in executables}
    notices = stage_manifest.get("notices")
    pin_key(notices)
    _require(notices["bytes"] <= MAX_NOTICE_BYTES, "Stage notices exceed admitted bound")
    file_rows = stage_manifest.get("staged")
    _require(isinstance(file_rows, list) and len(file_rows) == 3 * len(directories), "Incomplete app-local stage")
    staged = {pin_key(pin): pin for pin in file_rows}
    expected_files = {ntpath.join(directory, ntpath.basename(name)) for directory in directories for name in MEMBERS}
    expected_files.update(ntpath.join(directory, NOTICE_NAME) for directory in directories)
    _require(len(staged) == len(file_rows) and set(staged) == expected_files, "Staged file membership differs")
    source_members = {ntpath.basename(name): expected for name, expected in MEMBERS.items()}
    for path, pin in staged.items():
        expected = source_members.get(ntpath.basename(path), notices)
        _require(all(pin[key] == expected[key] for key in ("bytes", "sha256")), "Staged artifact differs from source pin")

    cases = ("appearance-persist", "appearance-reopen")
    contexts = ("imgui_cpp", "imgui_python")
    _require(isinstance(decoded_reports, (list, tuple)) and len(decoded_reports) == 4, "Exactly four C2 reports required")
    reports = {}
    for report in decoded_reports:
        _require(isinstance(report, dict) and report.get("schema") == "c2.settings-appearance.ui.001"
                 and report.get("state") == "observed", "C2 report was not observed")
        key = (report.get("case_id"), report.get("context"))
        _require(key[0] in cases and key[1] in contexts and key not in reports, "C2 case/context membership differs")
        reports[key] = report
    proof = {"schema": "imagejxx.windows-gl-runtime.001", "state": "verified",
             "scope": "Provider identity in four authenticated C2 reports; scientific/process acceptance belongs to existing readers",
             "asset": dict(ASSET), "source_members": {name: dict(pin) for name, pin in MEMBERS.items()},
             "cases": [], "product_redistribution_admitted": False}
    gl_fields = ("vendor", "renderer", "version", "shading_language_version")
    for case in cases:
        for context in contexts:
            report = reports[case, context]
            runtime_before = report.get("runtime_before")
            _require(isinstance(runtime_before, dict), "Missing actual executable observation")
            executable = runtime_before.get("executable")
            path = pin_key(executable)
            _require(path in executables and same_pin(executable, executables[path]), "Actual executable differs from stage")
            before, after = report.get("gl_modules_before"), report.get("gl_modules_after")
            _require(isinstance(before, dict) and before == after and set(before) == {"platform", "modules"}
                     and before["platform"] == "win32" and isinstance(before["modules"], dict)
                     and set(before["modules"]) == set(source_members), "Loaded GL module observations differ")
            loaded = {}
            for name, observation in before["modules"].items():
                _require(isinstance(observation, dict) and set(observation) == {"state", "artifact"}
                         and observation["state"] == "loaded", "Required GL module is not loaded")
                artifact = observation["artifact"]
                expected_path = ntpath.join(ntpath.dirname(path), name)
                _require(pin_key(artifact) == expected_path and same_pin(artifact, staged[expected_path]),
                         "Loaded GL module is not the exact app-local staged file")
                loaded[name] = dict(artifact)
            states = report.get("states")
            _require(isinstance(states, dict) and 0 < len(states) <= 64, "Missing bounded C2 appearance states")
            identities = set()
            for state in states.values():
                _require(isinstance(state, dict) and isinstance(state.get("observations"), dict), "Missing C2 state observations")
                runtime = state["observations"].get("appearance_runtime")
                _require(isinstance(runtime, dict) and all(isinstance(runtime.get(name), str) and
                         0 < len(runtime[name]) <= 4096 and runtime[name].strip() and "\0" not in runtime[name]
                         for name in gl_fields), "Missing actual GL identity strings")
                _require(re.match(r"^llvmpipe(?:\s|\(|$)", runtime["renderer"], flags=re.IGNORECASE)
                         and re.search(r"(?:^|\s)Mesa\s+26\.2\.4(?=$|[\s(])", runtime["version"]),
                         "Actual GL renderer/version is not llvmpipe Mesa 26.2.4")
                identities.add(tuple(runtime[name] for name in gl_fields))
            proof["cases"].append({"case_id": case, "context": context, "executable": dict(executable),
                                   "loaded_modules": loaded, "appearance_state_count": len(states),
                                   "gl_identities": [dict(zip(gl_fields, identity)) for identity in sorted(identities)]})
    return proof


def _copy_owned(source, destination, staged, save):
    expected = _pin(source)
    # Record creation identity and the successfully written prefix. A partial
    # write with an unknown extent is deliberately left for explicit recovery.
    row = None
    digest = hashlib.sha256()
    try:
        with destination.open("xb") as target:
            row = {"path": str(destination), "identity": _identity(destination), "removed": False,
                   "bytes": 0, "sha256": digest.hexdigest()}
            staged.append(row)
            save()
            with source.open("rb") as stream:
                for data in iter(lambda: stream.read(1024 * 1024), b""):
                    written = target.write(data)
                    digest.update(data[:written])
                    row.update(bytes=row["bytes"] + written, sha256=digest.hexdigest())
                    _require(written == len(data), "Incomplete app-local write")
            target.flush()
            os.fsync(target.fileno())
    finally:
        if row is not None:
            save()
    actual = _pin(destination)
    _require(_pin(source) == expected and actual["bytes"] == expected["bytes"] and
             actual["sha256"] == expected["sha256"], "Provider copy changed")


def run_with_provider(root, output, application, argv, *, timeout=3600, seven_zip=None,
                      notices=None, notices_sha256=None):
    """Stage once, run one owned qualification command, then remove owned copies.

    Failures raise after writing receipt.json. Unresolved owners or changed files
    prevent cleanup and success; their exact paths remain in that receipt.
    """
    root = _path(root)
    temporary = _ci_context(root)  # Before output creation, acquisition or staging.
    _require(not any(os.environ.get(name) for name in OVERRIDES), "Renderer capability overrides are forbidden")
    _require(type(timeout) in (int, float) and math.isfinite(timeout) and 0 < timeout <= 3600,
             "Qualification timeout must be in (0, 3600]")
    output = _path(output, exists=False)
    _require(output == root / "build/ci-windows-gl" and not output.exists(), "Use fresh build/ci-windows-gl output")
    _require(notices is not None and isinstance(notices_sha256, str) and len(notices_sha256) == 64,
             "A reviewed pinned notice ZIP is required")
    notice_pin = _pin(notices)
    _require(0 < notice_pin["bytes"] <= MAX_NOTICE_BYTES and notice_pin["sha256"] == notices_sha256,
             "Notice bundle differs from its reviewed pin")
    if application is None:
        suite, _ = _source_module(root, "ci_python_suite")
        application = suite.installed_application(importlib.metadata.distribution("imagejxx"), platform="win32")
    executables = _executables(application)
    _require(isinstance(argv, (list, tuple)) and argv and all(isinstance(item, str) and "\0" not in item for item in argv),
             "Qualification argv must be a nonempty string list")
    executable = _path(shutil.which(argv[0]) or argv[0])
    _require(os.path.samefile(executable, sys.executable), "Qualification must use the executing Python")
    argv = [str(executable), *argv[1:]]
    directories = sorted({Path(row["path"]).parent for row in executables}, key=str)
    for directory in directories:
        _require(not directory.is_relative_to(output) and
                 (not directory.is_relative_to(root) or directory.is_relative_to(root / "build/wheel")),
                 "Provider stage intersects checkout source, capture, dependency or SDK output")
        for name in [*(Path(member).name for member in MEMBERS), NOTICE_NAME]:
            candidate = _path(directory / name, exists=False)
            _require(not candidate.exists(), "Refusing existing app-local file: " + str(candidate))
    seven_zip = _path(seven_zip or shutil.which("7z.exe") or Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "7-Zip/7z.exe")
    seven_zip_pin = _pin(seven_zip)
    owner, owner_pin = _load_owner(root)
    env = os.environ.copy()
    env["GALLIUM_DRIVER"] = "llvmpipe"
    output.mkdir()
    scratch = Path(tempfile.mkdtemp(prefix="imagejxx-ci-mesa-", dir=temporary))
    row = {"schema": SCHEMA, "state": "running", "scope": "disposable CI tool use only",
           "runtime_qualified": False, "product_redistribution_admitted": False,
           "helper": _pin(__file__), "environment_overrides": {"GALLIUM_DRIVER": "llvmpipe"},
           "asset": dict(ASSET), "members": MEMBERS, "notices": notice_pin,
           "executables": executables, "argv": argv, "owner_helper": owner_pin,
           "seven_zip": seven_zip_pin, "scratch": str(scratch), "commands": [], "staged": [],
           "cleanup_complete": False, "binaries_retained": True,
           "storage": {"asset_bytes": ASSET["bytes"], "extracted_bytes": sum(item["bytes"] for item in MEMBERS.values()),
                       "destination_count": len(directories), "terminal_log_limit_bytes": MAX_LOG_BYTES,
                       "live_log_byte_limit_claim": False}}
    def save():
        _save(output / "receipt.json", row)
    save()
    error = None
    try:
        shutil.copyfile(notice_pin["path"], output / "notices.zip")
        _require(_pin(output / "notices.zip")["sha256"] == notices_sha256, "Retained notices changed")
        archive_path = scratch / "provider.7z"
        _download(archive_path)
        actual = _pin(archive_path)
        _require(actual["bytes"] == ASSET["bytes"] and actual["sha256"] == ASSET["sha256"], "Release asset differs")
        row["download"] = actual
        save()
        _extract(root, archive_path, scratch, output, seven_zip, owner, env, row["commands"])
        selected = _selected_files(scratch)
        row["extracted"] = selected
        _require(_pin(archive_path) == actual and _pin(seven_zip) == seven_zip_pin, "Intake input/tool changed")
        archive_path.unlink()
        row["archive_retired"] = True
        save()
        for directory in directories:
            for source in selected:
                _copy_owned(Path(source["path"]), directory / Path(source["path"]).name, row["staged"], save)
            _copy_owned(Path(notice_pin["path"]), directory / NOTICE_NAME, row["staged"], save)
        stage_files = []
        for staged in row["staged"]:
            actual = _pin(staged["path"])
            _require(actual == {key: staged[key] for key in ("path", "bytes", "sha256")} and
                     _identity(staged["path"]) == staged["identity"], "Stage changed before command")
            stage_files.append(actual)
        stage = {"schema": "imagejxx.windows-gl-stage.001", "state": "staged", "asset": dict(ASSET),
                 "members": MEMBERS, "executables": executables, "notices": notice_pin, "staged": stage_files}
        stage_path = output / "stage.json"
        stage_bytes = (json.dumps(stage, indent=2, sort_keys=True, allow_nan=False) + "\n").encode("utf-8")
        _require(len(stage_bytes) <= 1024 * 1024, "Immutable stage metadata exceeds bound")
        with stage_path.open("xb") as stream:
            stream.write(stage_bytes)
            stream.flush()
            os.fsync(stream.fileno())
        row["stage"] = _pin(stage_path)
        env["IMAGEJXX_CI_WINDOWS_GL_STAGE"] = json.dumps(row["stage"], sort_keys=True)
        row["process_path"] = str(output / "qualification-process.json")
        save()
        _command(owner, argv, root, env, output, "qualification", timeout, row["commands"])
        _require([_pin(item["path"]) for item in executables] == executables, "Executing products changed")
        _require(_pin(owner_pin["path"]) == owner_pin and _pin(notice_pin["path"]) == notice_pin, "Helper/notices changed")
        _require(_pin(__file__) == row["helper"], "Provider wrapper changed during execution")
    except BaseException as caught:
        error = caught
        row["error"] = {"type": type(caught).__name__, "message": str(caught)}
    finally:
        if row.get("stage"):
            try:
                _require(_pin(row["stage"]["path"]) == row["stage"], "Immutable provider stage changed")
            except BaseException as stage_error:
                row["stage_error"] = {"type": type(stage_error).__name__, "message": str(stage_error)}
                if error is None:
                    error = stage_error
        try:
            if row.get("process_path") and Path(row["process_path"]).exists():
                row["process"] = _pin(row["process_path"])
            for command in row["commands"]:
                _proof(Path(command["path"]), argv=command["argv"], cwd=root, helper=owner_pin)
                command["process"] = _pin(command["path"])
                if command.get("log_path") and Path(command["log_path"]).exists():
                    command["log"] = _pin(command["log_path"])
            for staged in reversed(row["staged"]):
                path = Path(staged["path"])
                current = _pin(path)
                _require(_identity(path) == staged["identity"] and current["bytes"] == staged.get("bytes")
                         and current["sha256"] == staged.get("sha256"), "Owned stage changed; cleanup refused: " + str(path))
                path.unlink()
                staged["removed"] = True
                save()
            # Only known exact scratch files; no recursive cleanup or unknown links.
            for path in [scratch / "provider.7z", *(scratch / "extracted" / name for name in MEMBERS)]:
                if path.exists() or path.is_symlink():
                    identity = _pin(path)
                    expected = next((item for item in row.get("extracted", []) if item["path"] == str(path)), None)
                    _require(expected is None or identity == expected, "Extracted file changed; cleanup refused")
                    row.setdefault("retired_scratch_files", []).append(identity)
                    path.unlink()
            for directory in (scratch / "extracted/x64", scratch / "extracted", scratch):
                if directory.exists():
                    _path(directory).rmdir()  # Unexpected members prevent cleanup.
            row["cleanup_complete"] = True
            row["binaries_retained"] = False
        except BaseException as cleanup_error:
            row["cleanup_error"] = {"type": type(cleanup_error).__name__, "message": str(cleanup_error)}
            if error is None:
                error = cleanup_error
        row["state"] = "passed" if error is None and row["cleanup_complete"] else "failed"
        save()
    if error is not None:
        raise error
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--application", type=Path)
    parser.add_argument("--notices", type=Path, required=True)
    parser.add_argument("--notices-sha256", required=True)
    parser.add_argument("--seven-zip", type=Path)
    parser.add_argument("--timeout", type=float, default=3600)
    parser.add_argument("argv", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    argv = args.argv[1:] if args.argv[:1] == ["--"] else args.argv
    run_with_provider(Path(__file__).resolve().parents[1], args.output, args.application, argv,
                      timeout=args.timeout, seven_zip=args.seven_zip, notices=args.notices,
                      notices_sha256=args.notices_sha256)


if __name__ == "__main__":
    main()
