"""Synthetic CI-only GL staging controls; no provider is acquired or loaded.

The fixtures are deliberately tiny non-library bytes. Real hashing, path
admission and cleanup are exercised; HTTP, 7z and owned processes are transport
seams. These tests grant no Windows renderer or provider qualification.
"""
import copy
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path, PureWindowsPath
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("windows_gl_controls", ROOT / "scripts/ci_windows_gl.py")
gl = importlib.util.module_from_spec(SPEC)
exec(compile(Path(SPEC.origin).read_bytes(), SPEC.origin, "exec", dont_inherit=True), gl.__dict__)
SOURCE_CI_CONTEXT, SOURCE_DOWNLOAD, SOURCE_EXTRACT = gl._ci_context, gl._download, gl._extract

ASSET_BYTES = b"synthetic archive transport, not a 7z or provider\n"
MEMBER_BYTES = {
    "x64/opengl32.dll": b"synthetic OpenGL member; never loaded\n",
    "x64/libgallium_wgl.dll": b"synthetic Gallium member; never loaded\n",
}
NOTICE_BYTES = b"synthetic notices transport; not a license or grant\n"


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def pin(path):
    path = Path(path).resolve(strict=True)
    raw = path.read_bytes()
    return {"path": str(path), "bytes": len(raw), "sha256": digest(raw)}


class ClosedProfile(unittest.TestCase):
    def test_literal_reviewed_asset_and_only_two_library_members(self):
        self.assertEqual(gl.ASSET["asset_id"], 609112860)
        self.assertEqual(gl.ASSET["url"], "https://github.com/pal1000/mesa-dist-win/releases/download/26.2.4/mesa3d-26.2.4-release-msvc.7z")
        self.assertEqual(gl.ASSET["bytes"], 70257286)
        self.assertEqual(gl.ASSET["sha256"], "351fc8c8b695878ffb3eaa044b3ead08672a48b1a045e3c3e3975811df0f6695")
        self.assertEqual(gl.MEMBERS, {
            "x64/opengl32.dll": {"bytes": 139264, "sha256": "75b295fc80df0eaa8f1264c28663f6932c8b72d36513eb092556608858b7f5e0"},
            "x64/libgallium_wgl.dll": {"bytes": 61868032, "sha256": "53e49adf87590d5b0cab8553b1e98084812040cc1fca8c487695b17b7af3702e"},
        })

    def test_download_transport_is_bounded_and_closes_the_exact_response(self):
        class Response(io.BytesIO):
            headers = {"Content-Length": str(len(ASSET_BYTES))}
        response = Response(ASSET_BYTES)
        requests = []
        class Opener:
            def open(self, request, timeout):
                requests.append((request.full_url, timeout))
                return response
        with tempfile.TemporaryDirectory(prefix="imagejxx-gl-http-control-") as temporary, \
                patch.object(gl, "ASSET", {**gl.ASSET, "bytes": len(ASSET_BYTES)}), \
                patch.object(gl.urllib.request, "build_opener", return_value=Opener()):
            path = Path(temporary).resolve(strict=True) / "provider.7z"
            SOURCE_DOWNLOAD(path)
            self.assertEqual(path.read_bytes(), ASSET_BYTES)
            self.assertEqual(requests, [(gl.ASSET["url"], 60)])
        self.assertTrue(response.closed)

    def test_short_extra_and_wrong_declared_http_lengths_refuse_without_retry(self):
        for body, declared in ((ASSET_BYTES[:-1], None), (ASSET_BYTES + b"x", None),
                               (ASSET_BYTES, str(len(ASSET_BYTES) + 1))):
            class Response(io.BytesIO):
                headers = {} if declared is None else {"Content-Length": declared}
            response = Response(body)
            attempts = []
            class Opener:
                def open(self, request, timeout):
                    attempts.append(request.full_url)
                    return response
            with self.subTest(bytes=len(body), declared=declared), \
                    tempfile.TemporaryDirectory(prefix="imagejxx-gl-http-refusal-") as temporary, \
                    patch.object(gl, "ASSET", {**gl.ASSET, "bytes": len(ASSET_BYTES)}), \
                    patch.object(gl.urllib.request, "build_opener", return_value=Opener()):
                path = Path(temporary).resolve(strict=True) / "provider.7z"
                with self.assertRaises(ValueError):
                    SOURCE_DOWNLOAD(path)
                self.assertEqual(len(attempts), 1)
                self.assertTrue(response.closed)


class StagingControls(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="imagejxx-gl-synthetic-")
        self.addCleanup(temporary.cleanup)
        self.area = Path(temporary.name).resolve(strict=True)
        self.root = self.area / "checkout"
        self.root.mkdir()
        (self.root / "build").mkdir()
        self.runner = self.area / "runner-temp"
        self.runner.mkdir()
        self.output = self.root / "build/ci-windows-gl"
        self.application = self.write("installed/app/imagejxx.exe", b"synthetic application; never executed\n")
        self.python = self.write("installed/venv/Scripts/python.exe", b"synthetic launcher; never executed\n")
        self.base = self.write("installed/python/python.exe", b"synthetic interpreter; never executed\n")
        self.notices = self.write("reviewed/notices.zip", NOTICE_BYTES)
        self.helper = self.write("checkout/scripts/ci_owned_process.py", b"synthetic helper identity; never imported\n")
        self.seven_zip = self.write("tools/7z.exe", b"synthetic extraction tool; never executed\n")
        self.argv = [str(self.python), "-I", "-B", "fixed-first-party-suite.py"]
        self.calls = []
        self.started = []
        self.owner_hook = None
        self.asset = {"url": "https://invalid.example/never-fetched.7z", "asset_id": 609112860,
                      "bytes": len(ASSET_BYTES), "sha256": digest(ASSET_BYTES)}
        self.members = {name: {"bytes": len(raw), "sha256": digest(raw)} for name, raw in MEMBER_BYTES.items()}
        self.start_patch(patch.object(gl, "ASSET", self.asset))
        self.start_patch(patch.object(gl, "MEMBERS", self.members))
        self.start_patch(patch.object(gl, "_ci_context", return_value=self.runner))
        self.start_patch(patch.object(gl.sys, "executable", str(self.python)))
        self.start_patch(patch.object(gl.sys, "_base_executable", str(self.base), create=True))
        self.start_patch(patch.dict(os.environ, {}, clear=True))
        self.start_patch(patch.object(gl, "_load_owner", return_value=(self.owner, pin(self.helper))))
        self.start_patch(patch.object(gl, "_download", side_effect=self.download))
        self.start_patch(patch.object(gl, "_extract", side_effect=self.extract))

    def start_patch(self, value):
        result = value.start()
        self.addCleanup(value.stop)
        return result

    def write(self, relative, raw):
        path = self.area / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(raw)
        return path

    def download(self, archive):
        self.calls.append("download")
        Path(archive).write_bytes(ASSET_BYTES)

    def extract(self, root, archive, scratch, output, seven_zip, owner, env, commands):
        self.calls.append("extract")
        for name, raw in MEMBER_BYTES.items():
            path = Path(scratch) / "extracted" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(raw)

    def owner(self, argv, *, cwd, env, timeout, stdout, stderr, receipt_path):
        self.calls.append("owner")
        self.started.append({"argv": list(argv), "cwd": str(cwd), "env": dict(env), "timeout": timeout})
        row = {"schema": "imagejxx.owned-command.001", "state": "exited", "exit_code": 0,
               "target_completed": True, "cleanup_complete": True, "timed_out": False,
               "argv": list(argv), "cwd": str(cwd), "helper": pin(self.helper),
               "descendants_remained_after_target_completion": False}
        Path(receipt_path).write_text(json.dumps(row), encoding="utf-8")
        if self.owner_hook is not None:
            return self.owner_hook(row, Path(receipt_path))
        return row

    def run_provider(self, **kwargs):
        return gl.run_with_provider(self.root, self.output, self.application, self.argv,
                                    timeout=19, seven_zip=self.seven_zip, notices=self.notices,
                                    notices_sha256=digest(NOTICE_BYTES), **kwargs)

    def receipt(self):
        return json.loads((self.output / "receipt.json").read_text(encoding="utf-8"))

    def destination_files(self):
        return [directory / name for directory in {self.python.parent, self.base.parent, self.application.parent}
                for name in ("opengl32.dll", "libgallium_wgl.dll", gl.NOTICE_NAME)]

    def link(self, link, target, *, directory=False):
        try:
            link.symlink_to(target, target_is_directory=directory)
        except (OSError, NotImplementedError) as error:
            self.skipTest("This host cannot create the owned synthetic symlink: " + str(error))

    def test_success_stages_actual_launcher_base_and_app_then_removes_only_owned_files(self):
        before = [pin(path) for path in (self.python, self.base, self.application, self.helper, self.notices)]
        ambient = {"PRESERVED_CONTROL": "kept", "GALLIUM_DRIVER": "foreign-driver"}
        stage_identity = []
        def observe(row, receipt_path):
            self.assertTrue(all(path.is_file() for path in self.destination_files()))
            for directory in {self.python.parent, self.base.parent, self.application.parent}:
                self.assertEqual((directory / "opengl32.dll").read_bytes(), MEMBER_BYTES["x64/opengl32.dll"])
                self.assertEqual((directory / "libgallium_wgl.dll").read_bytes(), MEMBER_BYTES["x64/libgallium_wgl.dll"])
                self.assertEqual((directory / gl.NOTICE_NAME).read_bytes(), NOTICE_BYTES)
            self.assertFalse(any(path.suffix == ".dll" for path in self.output.rglob("*")))
            stage_pin = json.loads(self.started[-1]["env"]["IMAGEJXX_CI_WINDOWS_GL_STAGE"])
            stage_path = self.output / "stage.json"
            self.assertEqual(stage_pin, pin(stage_path))
            stage = json.loads(stage_path.read_text(encoding="utf-8"))
            self.assertEqual(set(stage), {"schema", "state", "asset", "members", "executables", "notices", "staged"})
            self.assertEqual((stage["schema"], stage["state"]), ("imagejxx.windows-gl-stage.001", "staged"))
            self.assertEqual((stage["asset"], stage["members"], stage["notices"]),
                             (self.asset, self.members, pin(self.notices)))
            self.assertEqual(stage["executables"], before[:3])
            self.assertEqual({r["path"] for r in stage["staged"]}, {str(p) for p in self.destination_files()})
            self.assertTrue(all(item == pin(item["path"]) for item in stage["staged"]))
            stage_identity.append((stage_pin, stage_path.read_bytes()))
            return row
        self.owner_hook = observe
        with patch.dict(os.environ, ambient):
            result = self.run_provider()
            self.assertEqual(os.environ["GALLIUM_DRIVER"], "foreign-driver")
        self.assertEqual(self.calls, ["download", "extract", "owner"])
        self.assertEqual(len(self.started), 1)
        stage_env = self.started[0]["env"]["IMAGEJXX_CI_WINDOWS_GL_STAGE"]
        self.assertEqual(self.started, [{"argv": self.argv, "cwd": str(self.root), "timeout": 19,
                                        "env": {**ambient, "GALLIUM_DRIVER": "llvmpipe",
                                                "IMAGEJXX_CI_WINDOWS_GL_STAGE": stage_env}}])
        self.assertEqual(result, self.receipt())
        self.assertEqual((result["schema"], result["state"]), ("imagejxx.windows-gl.001", "passed"))
        self.assertTrue(result["cleanup_complete"])
        self.assertFalse(result["binaries_retained"] or result["runtime_qualified"] or result["product_redistribution_admitted"])
        self.assertEqual(len(result["staged"]), 9)
        self.assertTrue(all(item["removed"] for item in result["staged"]))
        self.assertTrue(all(not path.exists() for path in self.destination_files()))
        self.assertFalse(Path(result["scratch"]).exists())
        self.assertEqual([pin(path) for path in (self.python, self.base, self.application, self.helper, self.notices)], before)
        self.assertEqual((self.output / "notices.zip").read_bytes(), NOTICE_BYTES)
        self.assertEqual(result["stage"], stage_identity[0][0])
        self.assertEqual(pin(self.output / "stage.json"), stage_identity[0][0])
        self.assertEqual((self.output / "stage.json").read_bytes(), stage_identity[0][1])
        self.assertEqual({p.name for p in self.output.iterdir()},
                         {"receipt.json", "stage.json", "commands.json", "notices.zip", "qualification.log", "qualification-process.json"})

    def test_executables_are_deduplicated_by_destination_directory(self):
        self.application = self.write("installed/venv/Scripts/imagejxx.exe", b"synthetic same-directory app\n")
        with patch.object(gl.sys, "_base_executable", str(self.python)):
            result = self.run_provider()
        self.assertEqual({r["path"] for r in result["executables"]}, {str(self.python), str(self.application)})
        self.assertEqual(result["storage"]["destination_count"], 1)
        self.assertEqual(len(result["staged"]), 3)

    def test_real_ci_guard_refuses_local_or_nonwindows_before_output_or_acquisition(self):
        environment = {"GITHUB_ACTIONS": "true", "RUNNER_OS": "Windows", "RUNNER_ARCH": "X64",
                       "RUNNER_ENVIRONMENT": "github-hosted",
                       "GITHUB_WORKSPACE": str(self.root), "RUNNER_TEMP": str(self.runner)}
        for platform, change in (("linux", {}), ("win32", {"GITHUB_ACTIONS": "false"}),
                                 ("win32", {"RUNNER_ARCH": "ARM64"}),
                                 ("win32", {"RUNNER_ENVIRONMENT": "self-hosted"}),
                                 ("win32", {"GITHUB_WORKSPACE": str(self.area)}),
                                 ("win32", {"RUNNER_TEMP": str(self.root / "build")})):
            with self.subTest(platform=platform, change=change), patch.object(gl, "_ci_context", SOURCE_CI_CONTEXT), \
                    patch.object(gl.sys, "platform", platform), patch.dict(os.environ, {**environment, **change}, clear=True):
                with self.assertRaises(ValueError):
                    self.run_provider()
                self.assertEqual(self.calls, [])
                self.assertFalse(self.output.exists())

    def test_real_ci_guard_accepts_only_separate_reviewed_synthetic_roots(self):
        with patch.object(gl.sys, "platform", "win32"), patch.dict(os.environ, {
                "GITHUB_ACTIONS": "true", "RUNNER_OS": "Windows", "RUNNER_ARCH": "X64",
                "RUNNER_ENVIRONMENT": "github-hosted",
                "GITHUB_WORKSPACE": str(self.root), "RUNNER_TEMP": str(self.runner)}, clear=True):
            self.assertEqual(SOURCE_CI_CONTEXT(self.root), self.runner)

    def test_renderer_version_or_loader_overrides_refuse_before_fetch(self):
        for name in ("MESA_GL_VERSION_OVERRIDE", "MESA_GLSL_VERSION_OVERRIDE", "MESA_LOADER_DRIVER_OVERRIDE"):
            with self.subTest(name=name), patch.dict(os.environ, {name: "unreviewed"}):
                with self.assertRaisesRegex(ValueError, "capability overrides"):
                    self.run_provider()
        self.assertFalse(self.output.exists())
        self.assertEqual(self.calls, [])

    def test_existing_output_is_preserved_and_never_reused(self):
        self.output.mkdir()
        marker = self.output / "previous.txt"
        marker.write_bytes(b"preserve existing output")
        with self.assertRaisesRegex(ValueError, "fresh build/ci-windows-gl"):
            self.run_provider()
        self.assertEqual(marker.read_bytes(), b"preserve existing output")
        self.assertEqual(self.calls, [])

    def test_source_capture_dependency_and_sdk_destinations_refuse_before_fetch(self):
        (self.root / "build/wheel").mkdir()
        for relative in ("src/imagejxx/imagejxx.exe", "build/capture/imagejxx.exe",
                         "build/ci-prefix/bin/imagejxx.exe", "build/sdk/bin/imagejxx.exe",
                         "build/wheel/../../src/imagejxx/imagejxx.exe"):
            self.application = self.write("checkout/" + relative, b"synthetic forbidden destination")
            with self.subTest(relative=relative), self.assertRaisesRegex(ValueError, "intersects checkout"):
                self.run_provider()
        self.assertEqual(self.calls, [])
        self.assertFalse(self.output.exists())

    def test_existing_library_or_notices_are_never_replaced_even_if_bytes_match(self):
        for name, raw in (("opengl32.dll", MEMBER_BYTES["x64/opengl32.dll"]),
                          (gl.NOTICE_NAME, b"foreign existing notices")):
            path = self.application.parent / name
            path.write_bytes(raw)
            try:
                with self.subTest(name=name), self.assertRaisesRegex(ValueError, "existing app-local file"):
                    self.run_provider()
                self.assertEqual(path.read_bytes(), raw)
                self.assertEqual(self.calls, [])
            finally:
                path.unlink()  # Exact synthetic fixture created by this method.

    def test_missing_base_executable_and_foreign_qualification_executable_refuse(self):
        with patch.object(gl.sys, "_base_executable", str(self.area / "missing.exe")):
            with self.assertRaises(ValueError):
                self.run_provider()
        self.argv[0] = str(self.application)
        with self.assertRaisesRegex(ValueError, "executing Python"):
            self.run_provider()
        self.assertEqual(self.calls, [])
        self.assertFalse(self.output.exists())

    def test_wrong_notice_hash_refuses_before_output_or_fetch(self):
        self.notices.write_bytes(b"changed original notices")
        with self.assertRaisesRegex(ValueError, "Notice bundle differs"):
            self.run_provider()
        self.assertEqual(self.calls, [])
        self.assertFalse(self.output.exists())

    def test_linked_executable_parent_is_refused_without_touching_target(self):
        alias = self.area / "alias-app"
        self.link(alias, self.application.parent, directory=True)
        original = pin(self.application)
        self.application = alias / "imagejxx.exe"
        with self.assertRaisesRegex(ValueError, "Linked/reparse"):
            self.run_provider()
        self.assertEqual(pin(self.application), original)
        self.assertEqual(self.calls, [])

    def test_dangling_staged_filename_link_is_not_treated_as_absent(self):
        link = self.application.parent / "opengl32.dll"
        self.link(link, self.area / "does-not-exist")
        with self.assertRaisesRegex(ValueError, "Linked/reparse"):
            self.run_provider()
        self.assertTrue(link.is_symlink())
        self.assertEqual(self.calls, [])

    def test_asset_bytes_are_verified_after_the_download_transport(self):
        def wrong(archive):
            self.calls.append("download")
            Path(archive).write_bytes(b"X" + ASSET_BYTES[1:])
        with patch.object(gl, "_download", side_effect=wrong), self.assertRaisesRegex(ValueError, "Release asset differs"):
            self.run_provider()
        self.assertEqual(self.calls, ["download"])
        self.assertEqual(self.receipt()["state"], "failed")
        self.assertTrue(self.receipt()["cleanup_complete"])
        self.assertTrue(all(not path.exists() for path in self.destination_files()))

    def test_member_bytes_are_verified_after_the_extraction_transport(self):
        def wrong(*args):
            self.extract(*args)
            (Path(args[2]) / "extracted/x64/libgallium_wgl.dll").write_bytes(b"tampered member")
        with patch.object(gl, "_extract", side_effect=wrong), self.assertRaisesRegex(ValueError, "Extracted provider file differs"):
            self.run_provider()
        self.assertEqual(self.calls, ["download", "extract"])
        self.assertEqual(self.receipt()["staged"], [])
        self.assertTrue(self.receipt()["cleanup_complete"])

    def test_unexpected_archive_member_is_preserved_and_cannot_claim_cleanup(self):
        def extra(*args):
            self.extract(*args)
            (Path(args[2]) / "extracted/x64/foreign.dll").write_bytes(b"unselected payload")
        with patch.object(gl, "_extract", side_effect=extra), self.assertRaisesRegex(ValueError, "Unexpected extracted files"):
            self.run_provider()
        row = self.receipt()
        self.assertFalse(row["cleanup_complete"])
        self.assertTrue(row["binaries_retained"])
        self.assertEqual((Path(row["scratch"]) / "extracted/x64/foreign.dll").read_bytes(), b"unselected payload")
        self.assertEqual(row["staged"], [])
        self.assertEqual(self.started, [])

    def test_extraction_uses_only_literal_selected_members_and_owned_process(self):
        scratch = self.runner / "synthetic-extract"
        scratch.mkdir()
        archive = scratch / "provider.7z"
        archive.write_bytes(ASSET_BYTES)
        self.output.mkdir()
        commands = []
        SOURCE_EXTRACT(self.root, archive, scratch, self.output, self.seven_zip, self.owner, {}, commands)
        self.assertEqual(self.started[0]["argv"], [str(self.seven_zip), "x", str(archive), "-y", "-r-", "-spd",
            "-o" + str(scratch / "extracted"), "x64/opengl32.dll", "x64/libgallium_wgl.dll"])
        self.assertEqual(self.started[0]["timeout"], 180)
        ledger = json.loads((self.output / "commands.json").read_text())["commands"]
        self.assertEqual(ledger, [{key: commands[0][key] for key in ("name", "path", "argv", "log_path")}])
        self.assertEqual(commands[0]["process"], pin(ledger[0]["path"]))
        self.assertEqual(commands[0]["log"], pin(ledger[0]["log_path"]))

    def test_nonzero_qualification_remains_failed_after_complete_cleanup(self):
        def nonzero(row, path):
            row["exit_code"] = 7
            path.write_text(json.dumps(row), encoding="utf-8")
            return row
        self.owner_hook = nonzero
        with self.assertRaisesRegex(ValueError, "did not complete successfully"):
            self.run_provider()
        row = self.receipt()
        self.assertEqual(row["state"], "failed")
        self.assertTrue(row["cleanup_complete"])
        self.assertEqual(row["process"], pin(row["process_path"]))
        self.assertEqual(json.loads(Path(row["process_path"]).read_text())["exit_code"], 7)
        self.assertTrue(all(not path.exists() for path in self.destination_files()))

    def test_zero_exit_without_target_completion_is_not_accepted(self):
        def incomplete(row, path):
            row["target_completed"] = False
            path.write_text(json.dumps(row), encoding="utf-8")
            return row
        self.owner_hook = incomplete
        with self.assertRaisesRegex(ValueError, "did not complete successfully"):
            self.run_provider()
        self.assertEqual(self.receipt()["state"], "failed")
        self.assertTrue(self.receipt()["cleanup_complete"])

    def test_returned_owner_row_must_equal_the_durable_proof(self):
        self.owner_hook = lambda row, path: {**row, "exit_code": 7}
        with self.assertRaisesRegex(ValueError, "proof differs from command"):
            self.run_provider()
        self.assertEqual(self.receipt()["state"], "failed")
        self.assertTrue(self.receipt()["cleanup_complete"])

    def test_missing_started_process_receipt_preserves_stage(self):
        def no_receipt(row, path):
            path.unlink()
            raise RuntimeError("synthetic owner failed before terminal receipt")
        self.owner_hook = no_receipt
        with self.assertRaisesRegex(RuntimeError, "before terminal receipt"):
            self.run_provider()
        row = self.receipt()
        self.assertEqual(row["state"], "failed")
        self.assertFalse(row["cleanup_complete"])
        self.assertTrue(all(path.is_file() for path in self.destination_files()))
        self.assertEqual([c["name"] for c in json.loads((self.output / "commands.json").read_text())["commands"]], ["qualification"])

    def test_unresolved_owner_cannot_remove_dlls_or_claim_success(self):
        def unresolved(row, path):
            row.update(state="ownership_failed", cleanup_complete=False, target_completed=False)
            path.write_text(json.dumps(row), encoding="utf-8")
            return row
        self.owner_hook = unresolved
        with self.assertRaisesRegex(ValueError, "cleanup is unresolved"):
            self.run_provider()
        row = self.receipt()
        self.assertFalse(row["cleanup_complete"])
        self.assertEqual(row["state"], "failed")
        self.assertTrue(all(path.is_file() for path in self.destination_files()))

    def test_foreign_owner_helper_identity_withholds_cleanup(self):
        def foreign(row, path):
            row["helper"] = {**row["helper"], "sha256": "0" * 64}
            path.write_text(json.dumps(row), encoding="utf-8")
            return row
        self.owner_hook = foreign
        with self.assertRaisesRegex(ValueError, "proof identity differs"):
            self.run_provider()
        self.assertFalse(self.receipt()["cleanup_complete"])
        self.assertTrue(all(path.is_file() for path in self.destination_files()))

    def test_changed_staged_bytes_are_preserved_on_refused_cleanup(self):
        changed = self.application.parent / "opengl32.dll"
        def mutate(row, path):
            changed.write_bytes(b"changed while process ran")
            return row
        self.owner_hook = mutate
        with self.assertRaisesRegex(ValueError, "Owned stage changed"):
            self.run_provider()
        self.assertEqual(changed.read_bytes(), b"changed while process ran")
        self.assertEqual(self.receipt()["state"], "failed")
        self.assertFalse(self.receipt()["cleanup_complete"])

    def test_changed_executable_fails_but_is_never_removed(self):
        def mutate(row, path):
            self.application.write_bytes(b"changed application")
            return row
        self.owner_hook = mutate
        with self.assertRaisesRegex(ValueError, "Executing products changed"):
            self.run_provider()
        self.assertEqual(self.application.read_bytes(), b"changed application")
        self.assertEqual(self.receipt()["state"], "failed")
        self.assertTrue(self.receipt()["cleanup_complete"])
        self.assertTrue(all(not path.exists() for path in self.destination_files()))

    def test_changed_immutable_stage_refuses_qualification_after_safe_cleanup(self):
        def mutate(row, path):
            stage_pin = json.loads(self.started[-1]["env"]["IMAGEJXX_CI_WINDOWS_GL_STAGE"])
            stage = Path(stage_pin["path"])
            stage.write_bytes(stage.read_bytes() + b"\n")
            return row
        self.owner_hook = mutate
        with self.assertRaisesRegex(ValueError, "Immutable provider stage changed"):
            self.run_provider()
        row = self.receipt()
        self.assertEqual(row["state"], "failed")
        self.assertIn("Immutable provider stage changed", row["stage_error"]["message"])
        self.assertTrue(row["cleanup_complete"])
        self.assertTrue(all(not path.exists() for path in self.destination_files()))
        self.assertNotEqual(pin(self.output / "stage.json"), row["stage"])

    def partial_write(self, *, unknown_extent):
        real_open = Path.open
        class PartialOutput:
            def __init__(self, stream):
                self.stream = stream
            def __enter__(self):
                return self
            def __exit__(self, *exception):
                return self.stream.__exit__(*exception)
            def __getattr__(self, name):
                return getattr(self.stream, name)
            def write(self, data):
                count = self.stream.write(data[:7])
                if unknown_extent:
                    raise OSError("synthetic write failed after an unknown prefix")
                return count
        def opening(path, mode="r", *args, **kwargs):
            stream = real_open(path, mode, *args, **kwargs)
            if mode == "xb" and path.name == "opengl32.dll":
                return PartialOutput(stream)
            return stream
        with patch.object(Path, "open", opening):
            with self.assertRaises((ValueError, OSError)):
                self.run_provider()
        self.assertEqual(self.started, [])
        row = self.receipt()
        self.assertEqual(row["state"], "failed")
        self.assertEqual(len(row["staged"]), 1)
        return row, row["staged"][0]

    def test_known_partial_copy_is_journaled_then_safely_removed(self):
        row, staged = self.partial_write(unknown_extent=False)
        self.assertEqual((staged["bytes"], staged["sha256"]),
                         (7, digest(MEMBER_BYTES["x64/opengl32.dll"][:7])))
        self.assertTrue(row["cleanup_complete"] and staged["removed"])
        self.assertFalse(Path(staged["path"]).exists())
        self.assertIn("Incomplete app-local write", row["error"]["message"])

    def test_unknown_partial_copy_is_preserved_and_cleanup_is_explicitly_incomplete(self):
        row, staged = self.partial_write(unknown_extent=True)
        self.assertFalse(row["cleanup_complete"] or staged["removed"])
        self.assertTrue(row["binaries_retained"])
        self.assertEqual(staged["bytes"], 0)
        self.assertEqual(Path(staged["path"]).read_bytes(), MEMBER_BYTES["x64/opengl32.dll"][:7])
        self.assertIn("Owned stage changed", row["cleanup_error"]["message"])


class RuntimeIdentityControls(unittest.TestCase):
    """Pure decoded-metadata controls, not GL or scientific acceptance.

    The caller authenticates the original C2 reports and immutable stage file.
    No paths below are opened, and the deliberately small reports only contain
    the independent runtime validator's input projection.
    """
    def setUp(self):
        def artifact(path, text):
            raw = text.encode("ascii")
            return {"path": path, "bytes": len(raw), "sha256": digest(raw)}
        self.launcher = artifact(r"C:\ci\venv\Scripts\python.exe", "synthetic launcher identity")
        self.base = artifact(r"C:\hostedtoolcache\Python\3.12.8\x64\python.exe", "synthetic base identity")
        self.application = artifact(r"C:\ci\installed\bin\imagejxx.exe", "synthetic app identity")
        self.notices = artifact(r"C:\ci\checkout\.github\ci-data\notices.zip", "synthetic notice identity")
        self.stage = {"schema": "imagejxx.windows-gl-stage.001", "state": "staged",
                      "asset": copy.deepcopy(gl.ASSET), "members": copy.deepcopy(gl.MEMBERS),
                      "executables": [self.launcher, self.base, self.application],
                      "notices": self.notices, "staged": []}
        for executable in self.stage["executables"]:
            directory = PureWindowsPath(executable["path"]).parent
            for basename, expected in (
                    ("opengl32.dll", gl.MEMBERS["x64/opengl32.dll"]),
                    ("libgallium_wgl.dll", gl.MEMBERS["x64/libgallium_wgl.dll"]),
                    ("imagejxx-ci-mesa-notices.zip", self.notices)):
                self.stage["staged"].append({"path": str(directory / basename),
                    "bytes": expected["bytes"], "sha256": expected["sha256"]})
        identity = {"vendor": "Mesa", "renderer": "llvmpipe (LLVM 20.1.8, 256 bits)",
                    "version": "4.5 (Core Profile) Mesa 26.2.4", "shading_language_version": "4.50"}
        self.reports = []
        for case in ("appearance-persist", "appearance-reopen"):
            for context, executable in (("imgui_cpp", self.application), ("imgui_python", self.base)):
                directory = PureWindowsPath(executable["path"]).parent
                modules = {"platform": "win32", "modules": {}}
                for basename in ("opengl32.dll", "libgallium_wgl.dll"):
                    path = str(directory / basename)
                    item = next(row for row in self.stage["staged"] if row["path"] == path)
                    modules["modules"][basename] = {"state": "loaded", "artifact": copy.deepcopy(item)}
                self.reports.append({"schema": "c2.settings-appearance.ui.001", "state": "observed",
                    "case_id": case, "context": context,
                    "runtime_before": {"executable": copy.deepcopy(executable)},
                    "runtime_after": {"executable": copy.deepcopy(executable)},
                    "gl_modules_before": modules, "gl_modules_after": copy.deepcopy(modules),
                    "states": {name: {"observations": {"appearance_runtime": copy.deepcopy(identity)}}
                               for name in ("before", "sampled")}})

    def test_four_reports_bind_base_interpreter_and_actual_loaded_module_directory(self):
        originals = copy.deepcopy((self.stage, self.reports))
        result = gl.verify_c2_runtime(self.stage, list(reversed(self.reports)))
        self.assertEqual((result["schema"], result["state"]), ("imagejxx.windows-gl-runtime.001", "verified"))
        self.assertFalse(result["product_redistribution_admitted"])
        self.assertEqual((result["asset"], result["source_members"]), (gl.ASSET, gl.MEMBERS))
        self.assertEqual([(row["case_id"], row["context"]) for row in result["cases"]],
                         [(case, context) for case in ("appearance-persist", "appearance-reopen")
                          for context in ("imgui_cpp", "imgui_python")])
        for row, original in zip(result["cases"], self.reports):
            self.assertEqual(row["executable"], original["runtime_before"]["executable"])
            self.assertEqual(row["loaded_modules"],
                             {name: value["artifact"] for name, value in original["gl_modules_before"]["modules"].items()})
            self.assertEqual(row["appearance_state_count"], 2)
            expected = original["states"]["before"]["observations"]["appearance_runtime"]
            self.assertTrue(row["gl_identities"])
            self.assertTrue(all(value == expected for value in row["gl_identities"]))
        self.assertEqual((self.stage, self.reports), originals)

    def test_matching_module_bytes_from_other_staged_executable_directory_refuse(self):
        # The launcher pair is real staged metadata too, but the observed Python
        # process is the base interpreter and must load from its own directory.
        reports = copy.deepcopy(self.reports)
        borrowed = str(PureWindowsPath(self.launcher["path"]).parent / "opengl32.dll")
        for phase in ("gl_modules_before", "gl_modules_after"):
            reports[1][phase]["modules"]["opengl32.dll"]["artifact"]["path"] = borrowed
        with self.assertRaises(ValueError):
            gl.verify_c2_runtime(self.stage, reports)

    def test_path_spelling_normalizes_case_slashes_but_refuses_relative_or_dot_components(self):
        reports = copy.deepcopy(self.reports)
        for report in reports:
            for phase in ("runtime_before", "runtime_after"):
                artifact = report[phase]["executable"]
                artifact["path"] = artifact["path"].upper().replace("\\", "/")
            for phase in ("gl_modules_before", "gl_modules_after"):
                for module in report[phase]["modules"].values():
                    module["artifact"]["path"] = module["artifact"]["path"].upper().replace("\\", "/")
        result = gl.verify_c2_runtime(self.stage, reports)
        self.assertEqual(result["cases"][0]["executable"], reports[0]["runtime_before"]["executable"])
        for path in (r"ci\installed\bin\opengl32.dll", r"C:ci\installed\bin\opengl32.dll",
                     r"C:\ci\installed\bin\.\opengl32.dll", r"C:\ci\installed\bin\other\..\opengl32.dll"):
            reports = copy.deepcopy(self.reports)
            for phase in ("gl_modules_before", "gl_modules_after"):
                reports[0][phase]["modules"]["opengl32.dll"]["artifact"]["path"] = path
            with self.subTest(path=path), self.assertRaisesRegex(ValueError, "path"):
                gl.verify_c2_runtime(self.stage, reports)

    def test_loaded_module_wrong_hash_or_size_refuses_even_when_before_after_agree(self):
        for field, value in (("sha256", "0" * 64), ("bytes", 139265)):
            reports = copy.deepcopy(self.reports)
            for phase in ("gl_modules_before", "gl_modules_after"):
                reports[0][phase]["modules"]["opengl32.dll"]["artifact"][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                gl.verify_c2_runtime(self.stage, reports)

    def test_missing_duplicate_or_unloaded_runtime_records_refuse(self):
        cases = []
        cases.append(("missing report", copy.deepcopy(self.reports[:-1])))
        cases.append(("duplicate identity", copy.deepcopy([*self.reports[:3], self.reports[0]])))
        for kind in ("missing module", "unloaded module"):
            reports = copy.deepcopy(self.reports)
            for phase in ("gl_modules_before", "gl_modules_after"):
                if kind == "missing module":
                    del reports[0][phase]["modules"]["libgallium_wgl.dll"]
                else:
                    reports[0][phase]["modules"]["libgallium_wgl.dll"]["state"] = "unavailable"
            cases.append((kind, reports))
        for kind, reports in cases:
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                gl.verify_c2_runtime(self.stage, reports)

    def test_unstaged_executable_cannot_borrow_matching_executable_bytes(self):
        reports = copy.deepcopy(self.reports)
        for phase in ("runtime_before", "runtime_after"):
            reports[0][phase]["executable"]["path"] = r"C:\unrelated\imagejxx.exe"
        with self.assertRaises(ValueError):
            gl.verify_c2_runtime(self.stage, reports)

    def test_each_state_requires_reviewed_mesa_renderer_version_and_nonempty_identity(self):
        for field, value in (("renderer", "GDI Generic"), ("renderer", "notllvmpipe"),
                             ("version", "4.5 (Core Profile) Mesa 26.2.40"),
                             ("vendor", ""), ("shading_language_version", "")):
            reports = copy.deepcopy(self.reports)
            reports[-1]["states"]["sampled"]["observations"]["appearance_runtime"][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                gl.verify_c2_runtime(self.stage, reports)

    def test_module_change_after_initial_live_context_refuses(self):
        reports = copy.deepcopy(self.reports)
        reports[-1]["gl_modules_after"]["modules"]["libgallium_wgl.dll"]["artifact"]["sha256"] = "1" * 64
        with self.assertRaises(ValueError):
            gl.verify_c2_runtime(self.stage, reports)

    def test_wrong_source_asset_or_incomplete_stage_cannot_authenticate_reports(self):
        for change in ("asset", "member", "missing staged file"):
            stage = copy.deepcopy(self.stage)
            if change == "asset":
                stage["asset"]["sha256"] = "0" * 64
            elif change == "member":
                stage["members"]["x64/opengl32.dll"]["bytes"] += 1
            else:
                stage["staged"].pop()
            with self.subTest(change=change), self.assertRaises(ValueError):
                gl.verify_c2_runtime(stage, self.reports)


if __name__ == "__main__":
    unittest.main()
