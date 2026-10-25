"""Owned-file tests for bounded Python diagnostic retention, not product proof.

No symlinks, junctions, environments, executables or dependency builds are
created. Link/reparse refusals use explicit metadata injection on real owned
directories and assert those directories were not traversed.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import stat
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zipfile


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "ci_python_diagnostics_under_test", ROOT / "scripts/ci_python_diagnostics.py")
diagnostics = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(diagnostics)


# Literal diagnostic-only observations from the retained forty-case Windows
# probe: tools001/diagnostics-windows-identity-probe.json, attempts 5/8/9/14/24/34.
# Each tuple is device, inode, bytes, mtime_ns, ctime_ns. The source bytes were
# b"original". Tests do not depend on local retained evidence being available.
WINDOWS_PATH_HANDLE_IDENTITIES = (
    (5, (15157005065845190331, 844424939177183, 8, 1792932097413706600, 1792932097413244700),
        (15157005065845190331, 844424939177183, 8, 1792932097413706600, 1792932097413706600)),
    (8, (15157005065845190331, 844424939177195, 8, 1792932097455982200, 1792932097454976500),
        (15157005065845190331, 844424939177195, 8, 1792932097455982200, 1792932097455982200)),
    (9, (15157005065845190331, 844424939177199, 8, 1792932097469747400, 1792932097468745700),
        (15157005065845190331, 844424939177199, 8, 1792932097469747400, 1792932097469747400)),
    (14, (15157005065845190331, 844424939177219, 8, 1792932097529825300, 1792932097528914100),
         (15157005065845190331, 844424939177219, 8, 1792932097529825300, 1792932097529825300)),
    (24, (15157005065845190331, 844424939177259, 8, 1792932097655525300, 1792932097654520700),
         (15157005065845190331, 844424939177259, 8, 1792932097655525300, 1792932097655525300)),
    (34, (15157005065845190331, 844424939177299, 8, 1792932097784467200, 1792932097782954700),
         (15157005065845190331, 844424939177299, 8, 1792932097784467200, 1792932097784467200)),
)


class PythonDiagnosticRetention(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="imagejxx python diagnostics ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.source = self.root / "pytest-temp"
        self.source.mkdir()
        self.serial = 0

    def write(self, relative, payload):
        path = self.source / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)
        return path

    def destination(self):
        self.serial += 1
        return self.root / ("diagnostics-" + str(self.serial) + ".zip")

    def retain(self, **limits):
        destination = self.destination()
        receipt = diagnostics.retain_python_diagnostics(self.source, destination, **limits)
        return destination, receipt

    def inspect(self, destination, receipt):
        raw = destination.read_bytes()
        self.assertEqual(receipt["archive"], {"path": str(destination), "bytes": len(raw),
                                             "sha256": hashlib.sha256(raw).hexdigest()})
        with zipfile.ZipFile(destination) as bundle:
            manifest_bytes = bundle.read("manifest.json")
            manifest = json.loads(manifest_bytes)
            self.assertEqual(receipt["manifest"], {"member": "manifest.json", "bytes": len(manifest_bytes),
                "sha256": hashlib.sha256(manifest_bytes).hexdigest()})
            self.assertEqual(manifest, {key: value for key, value in receipt.items()
                                        if key not in ("archive", "manifest")})
            self.assertEqual(set(bundle.namelist()), {"manifest.json", *[row["member"] for row in manifest["members"]]})
            for row in manifest["members"]:
                payload = bundle.read(row["member"])
                self.assertEqual(row["bytes"], len(payload))
                self.assertEqual(row["sha256"], hashlib.sha256(payload).hexdigest())
        self.assertEqual(manifest["schema"], "imagejxx.ci-python-diagnostics.001")
        self.assertIs(manifest["acceptance_claim"], False)
        return manifest

    def capture_identity_sequence(self, platform, expected, path_before, open_before, path_after, open_after):
        # Real owned bytes are read and spooled; only metadata observations are
        # injected, keeping both Windows and POSIX policies deterministic.
        source = self.write("identity/case.json", b"original")
        def info(identity):
            return SimpleNamespace(st_mode=stat.S_IFREG | 0o600, st_file_attributes=0,
                st_dev=identity[0], st_ino=identity[1], st_size=identity[2],
                st_mtime_ns=identity[3], st_ctime_ns=identity[4])
        with tempfile.TemporaryFile(mode="w+b", dir=self.root) as spool:
            with patch.object(diagnostics, "sys", SimpleNamespace(platform=platform)), \
                    patch.object(diagnostics, "_unlinked_ancestors",
                                 side_effect=[info(path_before), info(path_after)]) as path_stats, \
                    patch.object(diagnostics.os, "fstat",
                                 side_effect=[info(open_before), info(open_after)]) as handle_stats:
                result = diagnostics._capture_source(source, info(expected), spool, 8)
            captured = spool.read()
        self.assertEqual(source.read_bytes(), b"original")
        return result, captured, path_stats.call_count, handle_stats.call_count

    def test_windows_path_handle_ctime_semantics_retain_all_six_observed_payloads(self):
        for attempt, path_identity, handle_identity in WINDOWS_PATH_HANDLE_IDENTITIES:
            with self.subTest(attempt=attempt):
                result, captured, path_count, handle_count = self.capture_identity_sequence(
                    "win32", path_identity, path_identity, handle_identity, path_identity, handle_identity)
                self.assertEqual(result, {"bytes": 8,
                    "sha256": "0682c5f2076f099c34cfdd15a9e063849ed437a49677e6fcc5b4198c76575be5"})
                self.assertEqual(captured, b"original")
                self.assertEqual((path_count, handle_count), (2, 2))

    def test_windows_cross_api_comparison_still_refuses_each_shared_field_change(self):
        _, path_identity, handle_identity = WINDOWS_PATH_HANDLE_IDENTITIES[0]
        for index, field in enumerate(("device", "inode", "bytes", "mtime_ns")):
            changed = list(handle_identity)
            changed[index] += 1
            with self.subTest(field=field), self.assertRaises(diagnostics._SourceChanged) as caught:
                self.capture_identity_sequence(
                    "win32", path_identity, path_identity, tuple(changed), path_identity, tuple(changed))
            self.assertEqual(caught.exception.stage, "after_open")
            self.assertEqual(caught.exception.identities["open_before"], tuple(changed))
            self.assertIn(field, caught.exception.differing_fields)

    def test_posix_cross_api_comparison_keeps_all_five_fields(self):
        _, path_identity, handle_identity = WINDOWS_PATH_HANDLE_IDENTITIES[0]
        for platform in ("linux", "darwin"):
            with self.subTest(platform=platform):
                result, captured, path_count, handle_count = self.capture_identity_sequence(
                    platform, path_identity, path_identity, path_identity, path_identity, path_identity)
                self.assertEqual(result["bytes"], 8)
                self.assertEqual(captured, b"original")
                self.assertEqual((path_count, handle_count), (2, 2))
                with self.assertRaises(diagnostics._SourceChanged) as caught:
                    self.capture_identity_sequence(
                        platform, path_identity, path_identity, handle_identity, path_identity, handle_identity)
                self.assertEqual(caught.exception.stage, "after_open")
                self.assertEqual(caught.exception.differing_fields, ["ctime_ns"])

    def test_windows_same_path_ctime_changes_are_refused_before_and_after_read(self):
        _, path_identity, handle_identity = WINDOWS_PATH_HANDLE_IDENTITIES[0]
        changed = (*path_identity[:4], path_identity[4] + 100)
        for stage, path_before, path_after in (("before_open", changed, path_identity),
                                              ("after_read", path_identity, changed)):
            with self.subTest(stage=stage), self.assertRaises(diagnostics._SourceChanged) as caught:
                self.capture_identity_sequence(
                    "win32", path_identity, path_before, handle_identity, path_after, handle_identity)
            self.assertEqual(caught.exception.stage, stage)
            self.assertEqual(caught.exception.identities["expected"], path_identity)
            key = "path_before" if stage == "before_open" else "path_after"
            self.assertEqual(caught.exception.identities[key], changed)
            self.assertEqual(caught.exception.differing_fields, ["ctime_ns"])

    def test_windows_same_handle_ctime_change_is_refused_after_read(self):
        _, path_identity, handle_identity = WINDOWS_PATH_HANDLE_IDENTITIES[0]
        changed = (*handle_identity[:4], handle_identity[4] + 100)
        with self.assertRaises(diagnostics._SourceChanged) as caught:
            self.capture_identity_sequence(
                "win32", path_identity, path_identity, handle_identity, path_identity, changed)
        self.assertEqual(caught.exception.stage, "after_read")
        self.assertEqual(caught.exception.identities, {"expected": path_identity, "path_before": path_identity,
            "open_before": handle_identity, "open_after": changed, "path_after": path_identity})
        self.assertEqual(caught.exception.differing_fields, ["ctime_ns"])

    def test_selected_files_are_lossless_and_unrelated_payloads_are_not_copied(self):
        selected = {
            "failed/producer.log": b"raw stdout\r\n\x00\xff\n",
            "failed/producer.runtime.json": b'{"pid":123}\r\n',
            "failed/capture/runtime.json": b'{"parent":122}\n',
            "failed/capture/failure.json": b'  {"error":"literal failure"}\r\n',
            "failed/receipt.json": b'{"state":"failed"}\n',
            "passed/case.json": b"deliberately not parsed as JSON\x00\xff",
            "events/test-phases.jsonl": b'{"phase":"setup"}\n{"phase":"call"}\n',
        }
        ignored = {"environment/site-packages/module.py": b"not an environment archive",
                   "environment/native.dll": b"not a library archive", "generated/result.tif": b"not all scratch",
                   "preferences.json": b"not a named diagnostic", "producer.log.bak": b"not the actual log"}
        for relative, payload in {**selected, **ignored}.items():
            self.write(relative, payload)
        before = {relative: (self.source / relative).read_bytes() for relative in (*selected, *ignored)}
        destination, receipt = self.retain()
        manifest = self.inspect(destination, receipt)
        self.assertEqual(manifest["state"], "complete")
        self.assertTrue(manifest["discovery"]["complete"])
        self.assertEqual(manifest["omitted"], [])
        self.assertEqual(manifest["matched_files"], len(selected))
        self.assertEqual({row["path"] for row in manifest["members"]}, set(selected))
        self.assertEqual(manifest["retained_bytes"], sum(map(len, selected.values())))
        with zipfile.ZipFile(destination) as bundle:
            for relative, payload in selected.items():
                self.assertEqual(bundle.read("diagnostics/" + relative), payload)
        self.assertEqual({relative: (self.source / relative).read_bytes() for relative in before}, before)

    def test_large_member_crosses_stream_chunks_without_truncation(self):
        payload = bytes(range(256)) * 769 + b"last bytes"
        self.write("failure.json", payload)
        destination, receipt = self.retain()
        manifest = self.inspect(destination, receipt)
        self.assertEqual(manifest["state"], "complete")
        self.assertEqual(manifest["members"][0]["bytes"], len(payload))
        with zipfile.ZipFile(destination) as bundle:
            self.assertEqual(bundle.read("diagnostics/failure.json"), payload)

    def test_empty_regular_diagnostic_is_retained(self):
        self.write("producer.log", b"")
        destination, receipt = self.retain()
        manifest = self.inspect(destination, receipt)
        self.assertEqual(manifest["state"], "complete")
        self.assertEqual(manifest["retained_bytes"], 0)
        self.assertEqual(manifest["members"][0]["sha256"], hashlib.sha256(b"").hexdigest())

    def test_file_size_limit_omits_the_whole_payload_and_preserves_source(self):
        self.write("a/failure.json", b"too large!")
        self.write("b/receipt.json", b"ok")
        destination, receipt = self.retain(max_file_bytes=3)
        manifest = self.inspect(destination, receipt)
        self.assertEqual(manifest["state"], "incomplete")
        self.assertEqual(manifest["omitted"], [{"path": "a/failure.json", "reason": "file_size_limit", "bytes": 10}])
        self.assertEqual([row["path"] for row in manifest["members"]], ["b/receipt.json"])
        self.assertEqual((self.source / "a/failure.json").read_bytes(), b"too large!")

    def test_total_size_limit_never_creates_a_partial_member(self):
        self.write("a.log", b"1234")
        self.write("b.log", b"5678")
        destination, receipt = self.retain(max_total_bytes=6)
        manifest = self.inspect(destination, receipt)
        self.assertEqual(manifest["state"], "incomplete")
        self.assertEqual(manifest["retained_bytes"], 4)
        self.assertEqual(manifest["omitted"], [{"path": "b.log", "reason": "total_size_limit", "bytes": 4}])
        self.assertEqual([row["member"] for row in manifest["members"]], ["diagnostics/a.log"])

    def test_file_count_limit_records_omitted_identity_and_keeps_every_source(self):
        self.write("a/case.json", b"first")
        self.write("b/case.json", b"second")
        destination, receipt = self.retain(max_files=1)
        manifest = self.inspect(destination, receipt)
        self.assertEqual(manifest["state"], "incomplete")
        self.assertEqual(manifest["omitted"], [{"path": "b/case.json", "reason": "file_count_limit", "bytes": 6}])
        self.assertEqual((self.source / "b/case.json").read_bytes(), b"second")

    def test_discovery_limit_reports_unknown_remainder_instead_of_empty_success(self):
        for index in range(5):
            self.write(str(index) + "/failure.json", b"owned diagnostic")
        destination, receipt = self.retain(max_entries=2)
        manifest = self.inspect(destination, receipt)
        self.assertEqual(manifest["state"], "incomplete")
        self.assertFalse(manifest["discovery"]["complete"])
        self.assertEqual(manifest["discovery"]["entries_scanned"], 2)
        self.assertIn("entry_limit", {row["reason"] for row in manifest["omitted"]})

    def test_missing_source_is_unavailable_and_does_not_create_an_empty_archive(self):
        destination = self.destination()
        receipt = diagnostics.retain_python_diagnostics(self.root / "not-created", destination)
        self.assertEqual(receipt["state"], "unavailable")
        self.assertIsNone(receipt["archive"])
        self.assertIsNone(receipt["manifest"])
        self.assertFalse(receipt["discovery"]["complete"])
        self.assertEqual(receipt["omitted"], [{"path": ".", "reason": "source_missing"}])
        self.assertFalse(destination.exists())

    def test_unreadable_source_is_incomplete_and_retains_the_error_record(self):
        original = Path.lstat
        def denied(path):
            if path == self.source:
                raise PermissionError("synthetic unreadable owned scratch")
            return original(path)
        with patch.object(Path, "lstat", denied):
            destination, receipt = self.retain()
        manifest = self.inspect(destination, receipt)
        self.assertEqual(manifest["state"], "incomplete")
        self.assertFalse(manifest["discovery"]["complete"])
        self.assertEqual(manifest["omitted"][0]["reason"], "source_unreadable")
        self.assertEqual(manifest["omitted"][0]["error"]["type"], "PermissionError")

    def test_unreadable_selected_file_is_recorded_and_other_diagnostics_survive(self):
        denied = self.write("a/failure.json", b"preserved original")
        self.write("b/producer.log", b"retained log")
        original = Path.open
        def guarded(path, mode="r", *args, **kwargs):
            if path == denied and mode == "rb":
                raise PermissionError("synthetic read failure")
            return original(path, mode, *args, **kwargs)
        with patch.object(Path, "open", guarded):
            destination, receipt = self.retain()
        manifest = self.inspect(destination, receipt)
        self.assertEqual(manifest["state"], "incomplete")
        self.assertEqual([row["path"] for row in manifest["members"]], ["b/producer.log"])
        self.assertEqual(manifest["omitted"][0]["reason"], "source_unreadable")
        self.assertEqual(denied.read_bytes(), b"preserved original")

    def test_changing_source_is_not_published_as_a_partial_or_immutable_member(self):
        source = self.write("failure.json", b"original")
        original_open = Path.open
        original_stat = source.stat()
        class ChangingReader:
            def __init__(self, stream):
                self.stream = stream
                self.changed = False
            def __enter__(self):
                return self
            def __exit__(self, *args):
                self.stream.close()
            def fileno(self):
                return self.stream.fileno()
            def read(self, size):
                block = self.stream.read(size)
                if not self.changed:
                    self.changed = True
                    with original_open(source, "wb") as writer:
                        writer.write(b"modified")
                    os.utime(source, ns=(original_stat.st_atime_ns, original_stat.st_mtime_ns + 1_000_000_000))
                return block
        def opened(path, mode="r", *args, **kwargs):
            stream = original_open(path, mode, *args, **kwargs)
            return ChangingReader(stream) if path == source and mode == "rb" else stream
        with patch.object(Path, "open", opened):
            destination, receipt = self.retain()
        manifest = self.inspect(destination, receipt)
        self.assertEqual(manifest["state"], "incomplete")
        self.assertEqual(manifest["members"], [])
        self.assertEqual(manifest["omitted"][0]["reason"], "source_changed")
        self.assertEqual(source.read_bytes(), b"modified", manifest["omitted"])

    def test_pre_read_identity_refusal_reports_exact_stage_and_differing_field(self):
        source = self.write("failure.json", b"untouched")
        original_lstat, original_open = Path.lstat, Path.open
        source_stats, source_reads = [], []
        def observed(path):
            info = original_lstat(path)
            if path == source:
                source_stats.append(info)
                if len(source_stats) == 2:
                    # Controlled metadata fault after discovery, before open;
                    # this is not an explanation of the retained Windows run.
                    initial = source_stats[0]
                    return SimpleNamespace(
                        st_mode=initial.st_mode, st_file_attributes=getattr(initial, "st_file_attributes", 0),
                        st_dev=initial.st_dev, st_ino=initial.st_ino, st_size=initial.st_size,
                        st_mtime_ns=initial.st_mtime_ns + 100, st_ctime_ns=initial.st_ctime_ns)
            return info
        def opened(path, mode="r", *args, **kwargs):
            if path == source and mode == "rb":
                source_reads.append(path)
            return original_open(path, mode, *args, **kwargs)
        with patch.object(Path, "lstat", observed), patch.object(Path, "open", opened):
            destination, receipt = self.retain()
        manifest = self.inspect(destination, receipt)
        self.assertEqual(manifest["state"], "incomplete")
        self.assertEqual(manifest["members"], [])
        self.assertEqual(len(source_stats), 2)
        self.assertEqual(source_reads, [])
        refusal = manifest["omitted"][0]
        self.assertEqual((refusal["path"], refusal["reason"]), ("failure.json", "source_changed"))
        error = refusal["error"]
        self.assertEqual(error["stage"], "before_open")
        self.assertEqual(error["identity_fields"], ["device", "inode", "bytes", "mtime_ns", "ctime_ns"])
        initial = source_stats[0]
        expected = [initial.st_dev, initial.st_ino, initial.st_size, initial.st_mtime_ns, initial.st_ctime_ns]
        changed = expected.copy()
        changed[3] += 100
        self.assertEqual(error["identities"], {"expected": expected, "path_before": changed,
                                             "open_before": None, "open_after": None, "path_after": None})
        self.assertEqual(error["differing_fields"], ["mtime_ns"])
        self.assertIn("stage=before_open", error["message"])
        self.assertIn(repr(tuple(changed)), error["message"])
        self.assertEqual(source.read_bytes(), b"untouched")

    def test_linked_metadata_is_refused_without_creating_or_following_a_link(self):
        directory = self.source / "linked-directory"
        self.write("linked-directory/secret.log", b"never traversed")
        ordinary = self.write("plain.log", b"allowed")
        original_lstat, original_scandir = Path.lstat, os.scandir
        visited = []
        def injected(path):
            info = original_lstat(path)
            if path == directory:
                return SimpleNamespace(st_mode=info.st_mode, st_file_attributes=0x400)
            return info
        def scanned(path):
            visited.append(Path(path))
            return original_scandir(path)
        with patch.object(Path, "lstat", injected), patch.object(diagnostics.os, "scandir", scanned):
            destination, receipt = self.retain()
        manifest = self.inspect(destination, receipt)
        self.assertNotIn(directory, visited)
        self.assertEqual(manifest["state"], "incomplete")
        self.assertEqual([row["path"] for row in manifest["members"]], [ordinary.name], manifest["omitted"])
        self.assertEqual(manifest["omitted"], [{"path": "linked-directory", "reason": "linked_path"}])
        self.assertEqual((directory / "secret.log").read_bytes(), b"never traversed")

    def test_source_and_destination_ancestor_link_metadata_refuse_before_output(self):
        parent = self.root / "output-parent"
        parent.mkdir()
        original = Path.lstat
        for target, destination in ((self.source, self.destination()), (parent, parent / "capture.zip")):
            with self.subTest(target=str(target)):
                def injected(path):
                    info = original(path)
                    return SimpleNamespace(st_mode=stat.S_IFLNK | 0o777) if path == target else info
                with patch.object(Path, "lstat", injected), self.assertRaises(ValueError):
                    diagnostics.retain_python_diagnostics(self.source, destination)
                self.assertFalse(destination.exists())

    def test_destination_cannot_alias_source_or_replace_existing_artifact(self):
        self.write("receipt.json", b"source receipt")
        for destination in (self.source, self.source / "archive.zip"):
            with self.subTest(destination=str(destination)), self.assertRaises(ValueError):
                diagnostics.retain_python_diagnostics(self.source, destination)
        destination = self.destination()
        destination.write_bytes(b"prior artifact")
        with self.assertRaises(ValueError):
            diagnostics.retain_python_diagnostics(self.source, destination)
        self.assertEqual(destination.read_bytes(), b"prior artifact")

    def test_limits_must_be_positive_integer_values(self):
        for name in ("max_file_bytes", "max_total_bytes", "max_files", "max_entries"):
            for value in (0, -1, True, 1.5, None):
                with self.subTest(name=name, value=value), self.assertRaises(ValueError):
                    diagnostics.retain_python_diagnostics(self.source, self.destination(), **{name: value})

    def test_stream_verification_rejects_resigned_zip_payload_mutation(self):
        self.write("case.json", b"original")
        destination, receipt = self.retain()
        initial = self.inspect(destination, receipt)
        self.assertEqual(initial["state"], "complete", initial["omitted"])
        self.assertEqual([row["member"] for row in initial["members"]], ["diagnostics/case.json"])
        with zipfile.ZipFile(destination) as bundle:
            self.assertEqual(bundle.read("diagnostics/case.json"), b"original")
            manifest_bytes = bundle.read("manifest.json")
        manifest = json.loads(manifest_bytes)
        # A valid ZIP CRC for changed same-size bytes is still not the retained
        # source digest. The verifier must inspect payloads, not just headers.
        with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED) as bundle:
            bundle.writestr("diagnostics/case.json", b"modified")
            bundle.writestr("manifest.json", manifest_bytes)
        with self.assertRaisesRegex(ValueError, "fingerprint changed"):
            diagnostics._verify_archive(destination, manifest, manifest_bytes)
        self.assertEqual((self.source / "case.json").read_bytes(), b"original")

    def test_stream_verification_rejects_extra_archive_members(self):
        self.write("receipt.json", b"original")
        destination, receipt = self.retain()
        initial = self.inspect(destination, receipt)
        self.assertEqual(initial["state"], "complete", initial["omitted"])
        self.assertEqual([row["member"] for row in initial["members"]], ["diagnostics/receipt.json"])
        with zipfile.ZipFile(destination) as bundle:
            self.assertEqual(bundle.read("diagnostics/receipt.json"), b"original")
            manifest_bytes = bundle.read("manifest.json")
        manifest = json.loads(manifest_bytes)
        with zipfile.ZipFile(destination, "a", compression=zipfile.ZIP_DEFLATED) as bundle:
            bundle.writestr("unselected-library.dll", b"must not be present")
        with self.assertRaisesRegex(ValueError, "membership changed"):
            diagnostics._verify_archive(destination, manifest, manifest_bytes)

    def test_headless_selection_retains_all_regular_raw_origin_session_and_xml_bytes(self):
        selected = {
            "case-01/pytest-temp/history/work/history-settings-observations.json":
                b'{"actual":"8000000000000000","state":"failed"}\r\n',
            "case-01/origins/history-origins.json": b'{"kind":"literal-history-origin"}\n',
            "case-01/origins/shortcuts-origins.json": b'{"kind":"literal-shortcut-origin"}\n',
            "case-01/pytest-temp/history/work/pair-preferences.json": b'{"future":"\xce\xbb"}\r\n',
            "case-01/pytest-temp/history/work/step-session.ijx": b"\x00IJX\xff\r\n\x00owned session bytes",
            "case-01/pytest.xml": b'<?xml version="1.0"?><testsuite tests="1"/>\r\n',
            "case-01/headless-receipt.json": b'{"state":"failed","acceptance_claim":false}\n',
            "case-01/complete-collection.json": b'{"collected":["original-id"]}\n',
            "case-01/pytest.log": b"original selected log\x00\xff\n",
            "case-01/producer.runtime.json": b'{"pid":123}\n',
            "case-01/runtime.json": b'{"pid":123}\r\n',
            "case-01/failure.json": b'{"error":"preserved"}\n',
            "case-01/receipt.json": b'{"state":"failed"}\n',
            "case-01/case.json": b"unparsed original diagnostic\x00\xff",
            "case-01/test-phases.jsonl": b'{"phase":"setup"}\n{"phase":"call"}\n',
            "case-01/payload.bin": b"future original raw payload\x00\xff",
            "case-01/notes.txt": b"future original diagnostics\r\n",
            "case-01/session.ijx.bak": b"all regular output is preserved",
            "case-01/original.json.bak": b"no silent suffix filtering",
            "case-01/pytest.xml.bak": b"another complete retained payload",
        }
        for relative, payload in selected.items():
            self.write(relative, payload)
        before = {relative: (self.source / relative).read_bytes() for relative in selected}
        destination, receipt = self.retain(selection="headless-tests")
        manifest = self.inspect(destination, receipt)
        self.assertEqual(manifest["selection"], "headless-tests")
        self.assertEqual(manifest["state"], "complete", manifest["omitted"])
        self.assertEqual(manifest["omitted"], [])
        self.assertTrue(manifest["discovery"]["complete"])
        self.assertEqual(manifest["matched_files"], len(selected))
        self.assertEqual({row["path"] for row in manifest["members"]}, set(selected))
        self.assertEqual(manifest["retained_bytes"], sum(map(len, selected.values())))
        with zipfile.ZipFile(destination) as bundle:
            for relative, payload in selected.items():
                self.assertEqual(bundle.read("diagnostics/" + relative), payload)
        self.assertEqual({relative: (self.source / relative).read_bytes() for relative in before}, before)

    def test_default_and_explicit_diagnostics_ignore_headless_extras_without_new_manifest_field(self):
        self.write("producer.log", b"existing diagnostic\n")
        for name in ("history-settings-observations.json", "history-origins.json", "preferences.json",
                     "headless-receipt.json", "step-session.ijx", "pytest.xml", "payload.bin", "notes.txt"):
            self.write(name, b"additional headless evidence\x00\xff")
        destination, receipt = self.retain()
        implicit = self.inspect(destination, receipt)
        destination, receipt = self.retain(selection="diagnostics")
        explicit = self.inspect(destination, receipt)
        self.assertEqual(implicit, explicit)
        self.assertNotIn("selection", implicit)
        self.assertEqual(implicit["state"], "complete")
        self.assertEqual([row["path"] for row in implicit["members"]], ["producer.log"])
        self.assertEqual(implicit["matched_files"], 1)
        self.assertEqual(implicit["omitted"], [])

    def test_unknown_or_nonstr_selection_refuses_before_filesystem_discovery_or_output(self):
        for selection in (None, False, 0, [], {}, "", "all", "HEADLESS-TESTS", "headless-tests "):
            destination = self.destination()
            with self.subTest(selection=selection):
                with patch.object(diagnostics, "_unlinked_ancestors",
                                  side_effect=AssertionError("Invalid selection must not inspect paths")):
                    with patch.object(diagnostics.os, "scandir",
                                      side_effect=AssertionError("Invalid selection must not traverse")):
                        with self.assertRaisesRegex(ValueError, "selection profile"):
                            diagnostics.retain_python_diagnostics(self.source, destination, selection=selection)
                        with self.assertRaisesRegex(ValueError, "selection profile"):
                            diagnostics._discover(self.source, 10, selection=selection)
                self.assertFalse(destination.exists())
        self.assertEqual(list(self.source.iterdir()), [])

    def test_headless_selection_reports_unexpected_link_metadata_without_following_it(self):
        directory = self.source / "unexpected-alias"
        self.write("unexpected-alias/private.json", b"never traversed")
        ordinary = self.write("history-settings-observations.json", b"actual values\r\n")
        original_lstat, original_scandir = Path.lstat, os.scandir
        visited = []
        def injected(path):
            info = original_lstat(path)
            if path == directory:
                return SimpleNamespace(st_mode=info.st_mode, st_file_attributes=0x400)
            return info
        def scanned(path):
            visited.append(Path(path))
            return original_scandir(path)
        with patch.object(Path, "lstat", injected), patch.object(diagnostics.os, "scandir", scanned):
            destination, receipt = self.retain(selection="headless-tests")
        manifest = self.inspect(destination, receipt)
        self.assertEqual(manifest["selection"], "headless-tests")
        self.assertNotIn(directory, visited)
        self.assertEqual(manifest["state"], "incomplete")
        self.assertEqual(manifest["omitted"], [{"path": "unexpected-alias", "reason": "linked_path"}])
        self.assertEqual([row["path"] for row in manifest["members"]], [ordinary.name], manifest["omitted"])
        self.assertEqual((directory / "private.json").read_bytes(), b"never traversed")

    def test_headless_extra_members_keep_original_whole_payload_byte_caps(self):
        self.write("a.ijx", b"1234")
        self.write("b.xml", b"5678")
        destination, receipt = self.retain(selection="headless-tests", max_total_bytes=6)
        manifest = self.inspect(destination, receipt)
        self.assertEqual(manifest["selection"], "headless-tests")
        self.assertEqual(manifest["state"], "incomplete")
        self.assertEqual(manifest["retained_bytes"], 4)
        self.assertEqual(manifest["omitted"], [{"path": "b.xml", "reason": "total_size_limit", "bytes": 4}])
        self.assertEqual([row["member"] for row in manifest["members"]], ["diagnostics/a.ijx"])
        with zipfile.ZipFile(destination) as bundle:
            self.assertEqual(bundle.read("diagnostics/a.ijx"), b"1234")
        self.assertEqual((self.source / "b.xml").read_bytes(), b"5678")

    def test_missing_headless_source_stays_unavailable_and_records_its_profile(self):
        destination = self.destination()
        receipt = diagnostics.retain_python_diagnostics(self.root / "missing-headless", destination,
                                                       selection="headless-tests")
        self.assertEqual(receipt["selection"], "headless-tests")
        self.assertEqual(receipt["state"], "unavailable")
        self.assertIsNone(receipt["archive"])
        self.assertIsNone(receipt["manifest"])
        self.assertEqual(receipt["omitted"], [{"path": ".", "reason": "source_missing"}])
        self.assertFalse(destination.exists())


if __name__ == "__main__":
    unittest.main()
