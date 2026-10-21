"""Scoped rank observation comparisons; no product imports or processing calls.

These 24 cases compare the 196 assertions adopted by G042 mapping adoption002.
They do not close any rank requirement or qualify an installed editor.  The
input manifest pins the original executions, raw values, source preimages and
products.  Missing evidence fails; historical reports are never relabelled.
"""

import base64
import hashlib
import importlib.util
import itertools
import json
import math
import os
from pathlib import Path
import re
import struct
import zipfile
import zlib

import pytest


ROOT = Path(__file__).resolve().parents[2]
STEMS = ("profile-method-radius", "targets-roi", "preview-publication",
         "lifecycle-resources", "record-replay-rebind", "persistence-surfaces")
CONTEXTS = ("cpp_api", "python_api", "imgui_cpp", "imgui_python")
PARAMETERS = tuple(itertools.product(STEMS, CONTEXTS))
PARAMETER_IDS = tuple(stem + "-" + context for stem, context in PARAMETERS)
EXPECTED_COLLECTION_IDS = tuple("test_rank_surface[" + name + "]" for name in PARAMETER_IDS)
CASE_IDS = tuple("R%02d" % index for index in range(1, 13))
MAX_JSON = 16 * 1024 * 1024
MAX_FILE = 200 * 1024 * 1024
RAW_PRODUCERS = {"sdk": "p06f.rank.sdk.raw.001", "host": "p06f.rank.host.raw.001",
                 "python": "p06f.rank.python.raw.001"}
SCOPES = (
    "196 adopted comparisons are a subset; complete_requirement_coverage remains false.",
    "Installed core-only SDK and development desktop host are separate raw producers.",
    "UI pixels are CPU cache/upload readbacks, not GPU or OS input qualification.",
    "RAO03 covers enabled-GIL handoff after initial progress, not kernel-entry cancellation.",
    "Synchronous prepare zero reservations do not prove async charged-lease release.",
    "Save/Open/close API observations receive no physical widget credit.",
    "Unsupported wheel events receive no formal physical-input credit.",
)
SDK_IDS = {
    "profile-method-radius": tuple(
        "sdk." + dtype + "." + method + ".published"
        for dtype in ("uint8", "uint16", "float32")
        for method in ("minimum", "maximum", "median", "mean")
    ) + ("sdk.default.method", "sdk.default.radius", "sdk.radius0.points", "sdk.metadata_refusal.reads"),
    "targets-roi": tuple("sdk.target." + mode + ".count" for mode in
                         ("current", "all_z", "all_TCZ", "strided", "positions"))
                   + ("sdk.roi.holed_maximum.u8",),
    "preview-publication": ("sdk.changed.mean", "sdk.changed.undo", "sdk.changed.redo",
                            "host.changed.workflow", "host.noop.redo", "host.noop.workflow"),
    "lifecycle-resources": tuple("sdk.final." + field for field in
                                 ("running", "queued", "running_bytes", "retained_bytes", "reserved_bytes"))
                           + ("sdk.ready.state", "sdk.inner_cancel.completed"),
    "record-replay-rebind": ("sdk.record.schema", "sdk.record.operation", "sdk.record.profile",
                             "sdk.rebind.radius", "sdk.rebind.positions", "host.rank.parameter_keys"),
    "persistence-surfaces": ("sdk.session.version", "sdk.session.documents", "sdk.session.scientific_planes",
                             "sdk.session.scientific_bytes", "sdk.session.reopened.undo", "sdk.session.unit"),
}
PYTHON_IDS = {
    "profile-method-radius": tuple(
        "python." + dtype + "." + method + ".published"
        for dtype in ("uint8", "uint16", "float32")
        for method in ("minimum", "maximum", "median", "mean")
    ) + ("python.default.method", "python.default.radius"),
    "targets-roi": ("python.captured.count", "python.captured.positions", "python.captured.holed_output",
                    "python.fresh_navigation"),
    "preview-publication": ("python.changed.mean", "python.changed.undo", "python.changed.workflow",
                            "python.noop.redo", "python.noop.workflow"),
    "lifecycle-resources": ("python.wrong_thread.jobs", "python.invalid.jobs", "python.cancel.workflow",
                            "python.close.members", "python.callback.reserved_bytes", "python.gil.thread_progress"),
    "record-replay-rebind": ("python.record.operation", "python.record.profile", "python.strict_repeat.accepted",
                             "python.reopened_strict.accepted", "python.rebound.radius", "python.rebound.count"),
    "persistence-surfaces": ("python.session.version", "python.scientific.planes",
                             "python.scientific.reopened_bytes", "python.scientific.unit"),
}
UI_CASES = {
    "profile-method-radius": ("R01", "R02", "R03", "R04", "R07", "R08"),
    "targets-roi": ("R04", "R05", "R06", "R12"),
    "preview-publication": ("R01", "R04", "R09", "R12"),
    "lifecycle-resources": CASE_IDS,
    "record-replay-rebind": ("R01", "R10", "R12"),
    "persistence-surfaces": ("R09", "R10"),
}


def _canonical(value):
    return json.dumps(value, ensure_ascii=False, allow_nan=False, sort_keys=True,
                      separators=(",", ":")).encode("utf-8")


def _sha(data):
    return hashlib.sha256(data).hexdigest()


def _pairs(items):
    result = {}
    for key, value in items:
        assert key not in result, "duplicate JSON key: " + key
        result[key] = value
    return result


def _decode(data):
    def nonfinite(value):
        raise AssertionError("nonfinite JSON constant: " + value)
    return json.loads(data.decode("utf-8-sig"), object_pairs_hook=_pairs, parse_constant=nonfinite)


def _positive(value, maximum=MAX_FILE):
    assert type(value) is int and 0 < value <= maximum
    return value


def _path(value):
    assert isinstance(value, str) and value and "\x00" not in value
    candidate = Path(value)
    candidate = (ROOT / candidate).resolve() if not candidate.is_absolute() else candidate.resolve()
    assert candidate.is_relative_to(ROOT), "evidence path escapes workspace"
    return candidate


def _pin_size(pin):
    value = pin.get("bytes", pin.get("byte_count"))
    assert type(value) is int and 0 <= value <= MAX_FILE
    if "bytes" in pin and "byte_count" in pin:
        assert pin["bytes"] == pin["byte_count"]
    assert re.fullmatch(r"[0-9a-f]{64}", pin["sha256"])
    if "logical_path" in pin:
        _path(pin["logical_path"])
    return value


def _pin_identity(pin):
    return (_path(pin["path"]), _pin_size(pin), pin["sha256"])


class RankEvidence:
    """Only the five fixed rank producers and their concrete extraction routes."""

    def __init__(self, manifest_path, output):
        self.verified = set()
        self.admitted_pins = {}
        self.archived = {}
        self.raw = {key: {} for key in RAW_PRODUCERS}
        self.ui_rows = {}
        self.ui_values = {}
        self.output = output
        self.written_bytes = 0
        assert not output.exists() or (output.is_dir() and not any(output.iterdir())), "output must be fresh and empty"
        output.mkdir(parents=True, exist_ok=True)
        self.manifest_path = manifest_path
        self.manifest = self.read_path(manifest_path)
        m = self.manifest
        assert m["schema_version"] == 1
        assert isinstance(m["run_id"], str) and re.fullmatch(r"[A-Za-z0-9_.-]+", m["run_id"])
        assert set(m["receipts"]) == {"sdk", "host", "python", "paired", "paired_reader"}
        assert set(m["raw"]) == {"sdk", "host"}
        assert set(m["producers"]) == {"sdk", "host", "python", "imgui_cpp", "imgui_python"}
        self.manifest_pin = self.local_pin(manifest_path)
        self.verify(m["reader_source"])
        assert _path(m["reader_source"]["path"]) == Path(__file__).resolve()
        if "product_archive_index" in m:
            self.load_archive(m["product_archive_index"])
        self.adoption = self.read_pin(m["adoption"])
        self.proposal = self.read_pin(m["proposal"])
        assert self.adoption["state"] == "adopted_scoped_comparison_mapping_no_option_acceptance"
        assert self.adoption["acceptance"] is False
        assert _pin_identity(self.adoption["proposal"]) == _pin_identity(m["proposal"])
        assert self.adoption["scope"] == {"requirements": 6, "contexts": 4,
                                           "expected_assertions": 196, "complete_requirement_coverage": False}
        adopted = {row["id"]: row for row in self.adoption["contracts"]}
        self.contracts = {}
        for contract in self.proposal["proposed_surface_contracts"]:
            stem = contract["requirement_id"].removeprefix("G042.rank-imagej.")
            assert stem in STEMS and stem not in self.contracts
            assert set(contract["contexts"]) == set(CONTEXTS)
            frozen = {key: contract[key] for key in ("id", "requirement_id", "snapshot", "contexts")}
            assert contract["sha256"] == adopted[contract["id"]]["sha256"] == _sha(_canonical(frozen))
            assert adopted[contract["id"]]["requirement_id"] == contract["requirement_id"]
            for context, assertions in contract["contexts"].items():
                ids = [row["id"] for row in assertions]
                assert len(ids) == len(set(ids)) and ids
                if context == "cpp_api":
                    assert set(ids) == set(SDK_IDS[stem])
                elif context == "python_api":
                    assert set(ids) == set(PYTHON_IDS[stem])
            self.contracts[stem] = contract
        assert set(self.contracts) == set(STEMS) and len(adopted) == 6
        assert sum(len(c["contexts"][context]) for c in self.contracts.values() for context in CONTEXTS) == 196
        assert {_pin_identity(p) for p in self.adoption["carriers"]} == {
            _pin_identity(p) for p in m["receipts"].values()}
        self.receipts = {key: self.read_pin(pin) for key, pin in m["receipts"].items()}
        self.validate_sources()
        self.load_api()
        reader_pin = m["paired_reader_source"]
        assert _pin_identity(reader_pin) == _pin_identity(self.receipts["paired_reader"]["reader"])
        self.verify(reader_pin)
        assert self.receipts["paired_reader"]["actual_outcomes"] == {
            "cases": 24, "failures": 0, "errors": 0, "skipped": 0}
        spec = importlib.util.spec_from_file_location("_rank_paired_frozen_reader", _path(reader_pin["path"]))
        assert spec is not None and spec.loader is not None
        self.paired = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.paired)
        assert len(self.paired.EXPECTED_COLLECTION_IDS) == 24 and set(self.paired.READERS) == set(CASE_IDS)
        runs = self.receipts["paired"]["runs"]
        self.ui_runs = {(row["context"], row["case_id"]): row for row in runs}
        assert len(runs) == len(self.ui_runs) == 24
        assert set(self.ui_runs) == set(itertools.product(CONTEXTS[2:], CASE_IDS))

    @staticmethod
    def local_pin(path):
        data = path.read_bytes()
        return {"path": path.relative_to(ROOT).as_posix(), "bytes": len(data), "sha256": _sha(data)}

    @staticmethod
    def read_path(path):
        assert path.is_file() and 0 < path.stat().st_size <= MAX_JSON
        return _decode(path.read_bytes())

    def verify(self, pin, *, artifact=False):
        identity = _pin_identity(pin)
        self.admitted_pins[identity] = (pin, artifact or self.admitted_pins.get(identity, ({}, False))[1])
        if identity in self.verified:
            return
        if artifact and identity in self.archived:
            self.verified.add(identity)
            return
        path, size, expected = identity
        assert path.is_file() and path.stat().st_size == size, "missing or changed pinned file: " + str(path)
        digest, count = hashlib.sha256(), 0
        with path.open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                count += len(chunk)
                assert count <= MAX_FILE
                digest.update(chunk)
        assert count == size and digest.hexdigest() == expected, "pinned bytes differ: " + str(path)
        self.verified.add(identity)

    def read_pin(self, pin):
        self.verify(pin)
        assert _pin_size(pin) <= MAX_JSON
        return self.read_path(_path(pin["path"]))

    def load_archive(self, index_pin):
        index = self.read_pin(index_pin)
        archive_pin = index["archive"]
        self.verify(archive_pin)
        rows = index["rows"]
        assert len(rows) == 9
        expected = {row["archive_member"]: row for row in rows}
        assert len(expected) == 9
        for member in expected:
            assert Path(member).name == member and member not in (".", "..")
        with zipfile.ZipFile(_path(archive_pin["path"]), "r") as archive:
            infos = archive.infolist()
            assert len(infos) == 9 and {info.filename for info in infos} == set(expected)
            assert sum(info.file_size for info in infos) <= MAX_FILE
            for info in infos:
                row = expected[info.filename]
                original = row["original"]
                assert not info.is_dir() and not (info.flag_bits & 1)
                assert info.file_size == _pin_size(original) == row["verified_uncompressed_bytes"]
                digest, count, crc = hashlib.sha256(), 0, 0
                with archive.open(info, "r") as source:
                    for chunk in iter(lambda: source.read(1024 * 1024), b""):
                        count += len(chunk)
                        assert count <= info.file_size
                        digest.update(chunk)
                        crc = zlib.crc32(chunk, crc)
                assert count == info.file_size and crc & 0xffffffff == info.CRC
                assert digest.hexdigest() == original["sha256"] == row["verified_uncompressed_sha256"]
                identity = _pin_identity(original)
                assert identity not in self.archived
                self.archived[identity] = {"index": index_pin, "archive": archive_pin,
                                           "member": info.filename, "original": original}

    def validate_sources(self):
        m = self.manifest
        index = self.read_pin(m["build014_source_index"])
        # Source preimages retain original logical names but are verified at their
        # preserved physical paths; do not substitute newer normal-loop sources.
        rows = index["rows"]
        assert isinstance(rows, list) and len(rows) == 5
        all_sources = []
        for key, producer in m["producers"].items():
            assert producer["context"] == ("cpp_api" if key in ("sdk", "host") else
                                             "python_api" if key == "python" else key)
            assert producer["id"] == (RAW_PRODUCERS[key] if key in RAW_PRODUCERS else
                                        "p06f.rank-ui.001." + key + ".build014")
            assert isinstance(producer["sources"], list) and producer["sources"]
            assert isinstance(producer["artifacts"], list) and producer["artifacts"]
            assert len(producer["sources"]) <= 512 and len(producer["artifacts"]) <= 128
            for pin in producer["sources"]:
                self.verify(pin)
                all_sources.append(pin)
            for pin in producer["artifacts"]:
                self.verify(pin, artifact=True)
        for row in rows:
            assert set(row) == {"source", "preserved", "bytes", "sha256"}
            original_path = row["source"]
            preserved = {"path": row["preserved"], "bytes": row["bytes"], "sha256": row["sha256"]}
            self.verify(preserved)
            matches = [pin for pin in all_sources if pin.get("logical_path") == original_path]
            assert matches, "missing build014 logical source preimage: " + str(original_path)
            assert all(_pin_identity(pin) == _pin_identity(preserved) for pin in matches)

    def add_raw(self, kind, pin, *, only_case=None, observation_ids=None):
        raw = self.read_pin(pin)
        assert raw["schema_version"] == 1 and raw["complete"] is True
        assert raw["producer_id"] == RAW_PRODUCERS[kind]
        cases = raw["cases"]
        assert isinstance(cases, list) and 0 < len(cases) <= 46
        if only_case is not None:
            assert len(cases) == 1 and cases[0]["id"] == only_case
        ids = [case["id"] for case in cases]
        assert len(ids) == len(set(ids))
        for case in cases:
            seen = set()
            for observation in case["observations"]:
                oid = observation["id"]
                assert oid not in seen and observation["encoding"] in ("scalar", "canonical_json", "bytes_le")
                seen.add(oid)
                assert set(observation) == {"id", "encoding", "value"}
                self.raw[kind].setdefault(oid, []).append({"observation": observation,
                    "source_case": case["id"], "raw": pin, "producer_id": RAW_PRODUCERS[kind]})
            if observation_ids is not None:
                assert seen == set(observation_ids) and len(seen) == len(observation_ids)
        return cases

    def load_api(self):
        m, receipts = self.manifest, self.receipts
        assert _pin_identity(m["raw"]["sdk"]) == _pin_identity(receipts["sdk"]["raw_observations"])
        host_run, = [run for run in receipts["host"]["runs"] if run["label"] == "rank_host"]
        assert _pin_identity(m["raw"]["host"]) == _pin_identity(host_run["raw_observations"])
        sdk = self.add_raw("sdk", m["raw"]["sdk"])
        host = self.add_raw("host", m["raw"]["host"])
        assert len(sdk) == 31 and len(self.raw["sdk"]) == 43
        assert len(host) == 35 and set(self.raw["host"]) == {
            "host.changed.workflow", "host.noop.redo", "host.noop.workflow", "host.rank.parameter_keys"}
        assert sum(not case["observations"] for case in sdk) == 5
        assert sum(not case["observations"] for case in host) == 32
        python = receipts["python"]
        assert python["actual_outcomes"] == {"cases": 46, "failures": 0, "errors": 0, "skipped": 0}
        assert isinstance(python["actual_cases"], dict) and len(python["actual_cases"]) == 46
        for case_id, descriptor in python["actual_cases"].items():
            assert descriptor["observation_count"] == len(descriptor["observation_ids"])
            self.add_raw("python", descriptor["raw"], only_case=case_id,
                         observation_ids=descriptor["observation_ids"])

    def api_value(self, oid):
        kind = "sdk" if oid.startswith("sdk.") else "host" if oid.startswith("host.") else "python"
        rows = self.raw[kind][oid]
        observations = [row["observation"] for row in rows]
        assert all(_canonical(row) == _canonical(observations[0]) for row in observations)
        observation = observations[0]
        value = observation["value"]
        details = {"encoding": observation["encoding"], "origins": [
            {key: row[key] for key in ("source_case", "raw", "producer_id")} for row in rows]}
        if observation["encoding"] == "bytes_le":
            _api_pixel_layout(oid, value)
            data = _declared_bytes(value)
            details.update({"pixel_type": value["pixel_type"], "shape": value["shape"],
                            "byte_count": len(data), "declared_bytes_sha256": _sha(data)})
            return _sha(data), details
        assert observation["encoding"] == "scalar", "frozen scalar must come from actual scalar, not a description"
        assert value is None or type(value) in (str, bool, int, float)
        if type(value) is float:
            assert math.isfinite(value)
        return value, details

    def python_lifecycle_support(self, oid, details):
        case_id = ("test_rao03_rank_releases_gil_for_cross_thread_cancel_after_initial_progress"
                   if oid == "python.gil.thread_progress" else
                   "test_rao01_rank_progress_exception_releases_callback_and_preserves_source")

        def actual(observation_id):
            rows = [row for row in self.raw["python"][observation_id] if row["source_case"] == case_id]
            assert len(rows) == 1
            return rows[0]["observation"]["value"]

        assert {row["source_case"] for row in self.raw["python"][oid]} == {case_id}
        if oid == "python.callback.reserved_bytes":
            assert actual("callback.alive_after_return") is False
            assert actual("callback.calls") == [[0, 1]]
            usage = actual("synchronous_prepare.scheduler_usage")
            assert usage == dict.fromkeys(("running", "queued", "running_bytes", "retained_bytes", "reserved_bytes"), 0)
            details["supporting_actuals"] = {"callback.alive_after_return": actual("callback.alive_after_return"),
                "callback.calls": actual("callback.calls"), "callback.error": actual("callback.error"),
                "synchronous_prepare.scheduler_usage": usage}
            details["scope"] = "synchronous callback lifetime/atomicity; no async charged-lease claim"
            return
        profile = {name.removeprefix("runtime.gil_profile."): actual(name)
                   for name in self.raw["python"] if name.startswith("runtime.gil_profile.")}
        assert profile["implementation"] == "cpython"
        assert type(profile["runtime_check_available"]) is bool
        if profile["runtime_check_available"]:
            assert profile["runtime_enabled"] is True
        else:
            assert profile["runtime_enabled"] is None
            assert profile["version_major"] == 3 and profile["version_minor"] == 12
            assert profile["cache_tag"] == "cpython-312" and profile["py_gil_disabled"] in (None, 0)
            assert profile["extension_suffix"] == ".cp312-win_amd64.pyd"
            assert self.receipts["python"]["native"]["path"].endswith(profile["extension_suffix"])
        handoffs = actual("gil.actual_handoffs")
        assert len(handoffs) == 1 and handoffs[0]["callback_active"] is False and handoffs[0]["native_returned"] is False
        _positive(handoffs[0]["thread_id"], 2**63 - 1)
        assert actual("rank.progress_calls") == [[0, 1]]
        details["supporting_actuals"] = {"runtime.gil_profile": profile, "gil.actual_handoffs": handoffs,
                                         "rank.progress_calls": actual("rank.progress_calls")}
        details["scope"] = "enabled-GIL native handoff after initial progress; positive-work/kernel-entry cancellation unproven"

    def ui(self, context, case):
        key = context, case
        if key not in self.ui_rows:
            run = self.ui_runs[key]
            assert run["step"]["exit_code"] == 0 and run["state"] == "raw_observations_emitted"
            index = self.read_pin(run["index"])
            row = self.read_pin(run["row"])
            assert index["schema_version"] == 1 and index["contract"] == "p06f.rank-ui.001"
            assert index["context"] == context and index["case_count"] == 1
            assert index["observation_state"] == "observed" and index["acceptance_claim"] is False
            descriptor, = index["cases"]
            assert descriptor["id"] == case and descriptor["member"] == "case.json"
            assert descriptor["byte_count"] == _pin_size(run["row"])
            assert descriptor["sha256"] == run["row"]["sha256"]
            assert _path(run["row"]["path"]) == _path(run["index"]["path"]).parent / descriptor["member"]
            assert row["contract"] == "p06f.rank-ui.001" and row["context"] == context and row["case_id"] == case
            self.paired._common(row, case)
            self.paired.READERS[case](row, _path(run["row"]["path"]).parent)
            self.ui_rows[key] = row
        return self.ui_rows[key]

    def ui_extract(self, context, stem):
        if (context, stem) in self.ui_values:
            return self.ui_values[context, stem]
        p = self.paired
        values = {}

        def put(oid, value, case, label, extraction):
            assert oid not in values
            cases = case if isinstance(case, tuple) else (case,)
            origins = [{"source_case": item, "raw": self.ui_runs[context, item]["row"]} for item in cases]
            values[oid] = (value, {"origins": origins, "checkpoint": label, "extraction": extraction,
                                  "producer_id": self.manifest["producers"][context]["id"]})

        def row(case):
            return self.ui(context, case)

        def source(case, label):
            return p._source(row(case), label)

        def active(case, label):
            return p._doc(p._state(row(case), label))

        def render(case, label):
            return p._render(row(case), label)

        if stem == "profile-method-radius":
            for oid, case, label in (("minimum.u8.preview", "R01", "minimum.preview"),
                                     ("maximum.u16.preview", "R02", "maximum.preview"),
                                     ("median.f32.preview", "R03", "median.preview"),
                                     ("mean.u16.preview", "R04", "mean.shown")):
                actual = render(case, label)["ordinary_pixels"]["readback"]["source"]
                put(oid, _sha(_readback_bytes(actual)), case, label, "ordinary_pixels.readback.source declared bytes")
            for method in ("minimum", "maximum", "mean", "median"):
                label = "memory." + method + (".default" if method == "mean" else ".reopened")
                put("session." + method + ".radius", render("R08", label)["rank_panel"]["radius"],
                    "R08", label, "rank_panel.radius")
                label = "memory." + method + ".boundary"
                put("boundary." + method + ".radius", render("R08", label)["rank_panel"]["radius"],
                    "R08", label, "rank_panel.radius; draft admission only")
            for oid, field in (("invalid.median.text", "radius_text"), ("invalid.median.draft_valid", "draft_valid")):
                put(oid, render("R08", "memory.median.invalid")["rank_panel"][field], "R08", "memory.median.invalid", "rank_panel." + field)
            put("destination.reopened", render("R08", "memory.destination.reopened")["rank_panel"]["new_document"],
                "R08", "memory.destination.reopened", "rank_panel.new_document")
        elif stem == "targets-roi":
            target = render("R04", "mean.target_ready")["rank_panel"]["target"]
            put("full_target.positions", _position_string(p._positions(target)), "R04", "mean.target_ready", "actual captured target ranges/positions")
            preview = render("R04", "mean.shown")["rank_panel"]["preview_target"]
            put("preview.target.position", _position_string(p._positions(preview)), "R04", "mean.shown", "contained preview target")
            put("preview.ordinal", row("R04")["preview_selection"]["selected_zero_based_ordinal"], "R04", "mean.shown", "actual target-derived selected ordinal")
            mask = source("R04", "initial")["rois"][0]["selection"]["mask"]
            put("roi.mask_msb", base64.b64decode(mask, validate=True).hex(), "R04", "initial", "saved ROI packed mask bytes")
            put("full_target.holed_output", _sha(_document_bytes(source("R04", "mean.applied"))), "R04", "mean.applied", "all TCZ declared plane bytes")
            for choice, case in (("yes", "R05"), ("no", "R06")):
                publication, = row(case)["publication_observations"]
                target = publication["apply_lane"]["command_record"]["target"]
                put("stack." + choice + ".count", len(p._positions(target)), case, "stack." + choice, "actual accepted publication target count")
        elif stem == "preview-publication":
            for oid, label in (("changed.minimum.bytes", "minimum.applied"), ("undo.source.bytes", "minimum.undo"),
                               ("redo.output.bytes", "minimum.redo")):
                put(oid, _sha(_document_bytes(source("R01", label), (0, 0, 1))), "R01", label, "captured TCZ001 declared plane bytes")
            put("changed.undo", source("R01", "minimum.applied")["undo_steps"], "R01", "minimum.applied", "native owner undo_steps")
            put("changed.workflow", len(p._state(row("R01"), "minimum.applied")["workflow"]), "R01", "minimum.applied", "actual recorded workflow count")
            put("new_document.members", len(p._state(row("R09"), "new.applied")["document_ids"]), "R09", "new.applied", "native/facade owner membership")
            put("new_document.output.bytes", _sha(_document_bytes(active("R09", "new.applied"))), "R09", "new.applied", "active new owner all TCZ declared bytes")
            put("noop.redo", source("R12", "unchanged.applied")["redo_steps"], "R12", "unchanged.applied", "native owner redo_steps")
            put("empty.workflow", len(p._state(row("R12"), "empty.applied")["workflow"]), "R12", "empty.applied", "actual workflow count")
        elif stem == "lifecycle-resources":
            for oid, label in (("queued.apply.state", "busy.queued"), ("cancelled.apply.state", "busy.cancelled")):
                put(oid, render("R11", label)["gaussian_jobs"]["apply"]["job"]["state"], "R11", label, "actual shared Apply lane job state")
            blocker = p._api(row("R11"), "busy_worker_release", accepted=True)["blocker_job"]
            put("generic_blocker.returned.state_code", blocker["state_code"], "R11", "busy_worker_release", "API-returned generic blocker state; not rank completion")
            for field in ("running", "queued", "running_bytes", "retained_bytes", "reserved_bytes"):
                put("new_document.cleaned." + field, p._state(row("R09"), "terminal.cleaned")["usage"][field],
                    "R09", "terminal.cleaned", "actual Application resource usage." + field)
            closed = [row(case)["lifecycle"][-1]["state"] for case in CASE_IDS]
            assert all(item["context_id"] == item["current_context_id"] == 0 for item in closed)
            put("context.closed.id", closed[0]["context_id"], CASE_IDS, "lifecycle.closed", "all12 actual closed context IDs checked")
        elif stem == "record-replay-rebind":
            original = row("R10")["original_record"]
            for oid, field in (("record.schema", "schema_version"), ("record.operation", "operation"), ("record.profile", "profile")):
                put(oid, original[field], "R10", "replay.before", "actual original typed command." + field)
            events = render("R10", "replay.rebound")["workflow_replay"]["events"]
            resolved, = [event for event in events if event["phase"] == "command_resolved"]
            parameters = resolved["resolved_command_parameters"]
            for field in ("method", "radius"):
                put("rebound." + field, parameters[field], "R10", "replay.rebound", "typed rank observer actual parameters." + field)
            put("rebound.target.count", len(p._positions(resolved["resolved_command_record"]["target"])), "R10", "replay.rebound", "actual complete rebind target")
            owners = {row("R10")["original_owner_id"], row("R10")["reopened_owner_id"],
                      events[0]["captured_snapshot_owner_id"], events[0]["working_copy_id"]}
            put("rebound.distinct_owners", len(owners), "R10", "replay.rebound", "relational original/reopened/capture/scratch owner count")
            put("rebound.phases", ",".join(event["phase"] for event in events), "R10", "replay.rebound", "actual workflow observer event order")
            put("rebound.parameter_keys", ",".join(sorted(parameters)), "R10", "replay.rebound", "actual typed rank parameter keys")
        elif stem == "persistence-surfaces":
            for prefix, case, key, label in (("ordinary", "R10", "native_session", "session.reopened"),
                                             ("scientific", "R09", "new_document_session", "new.reopened")):
                capture = _session_capture(row(case)["files"][key])
                put(prefix + ".session.version", capture["version"], case, label, "actual bounded NativeV3 file decode; API Save/Open")
            reopened = active("R10", "session.reopened")
            put("ordinary.reopened.z", reopened["shape"][2], "R10", "session.reopened", "fresh owner Z axis")
            put("ordinary.reopened.undo", reopened["undo_steps"], "R10", "session.reopened", "fresh owner undo_steps")
            reopened = active("R09", "new.reopened")
            put("scientific.reopened_bytes", _sha(_document_bytes(reopened)), "R09", "new.reopened", "fresh scientific owner all TCZ declared bytes")
            put("scientific.reopened.unit", reopened["calibration"]["unit"], "R09", "new.reopened", "actual fresh owner calibration unit")
        else:
            raise AssertionError("unknown fixed rank requirement")
        self.ui_values[context, stem] = values
        return values

    def inputs(self, context, cases):
        result, unsupported = [], []
        for case in cases:
            row = self.ui(context, case)
            for frame in row["frames"]:
                actual = frame["rendered"]["input_frame"]
                assert _canonical(actual["raw_inputs"]) == _canonical(frame["requested_events"])
                provenance = {"source_case": case, "frame_ordinal": frame["ordinal"],
                              "frame_label": frame["label"], "raw_sha256": self.ui_runs[context, case]["row"]["sha256"]}
                for event in frame["requested_events"]:
                    kind = event["type"]
                    translated = []
                    if kind == "button":
                        button = event["name"]
                        assert button in ("left", "right", "middle") and type(event["down"]) is bool
                        assert actual["mouse_down"][("left", "right", "middle").index(button)] is event["down"]
                        translated = [{"type": "pointer", "position": actual["mouse_position"],
                                       "button": button, "down": event["down"]}]
                    elif kind in ("key", "keys"):
                        keys = [event] if kind == "key" else event["keys"]
                        for key in keys:
                            name, down = key["name"], key["down"]
                            assert _named_key(name) and type(down) is bool
                            assert (name in actual["keys_down"]) is down
                            translated.append({"type": "key", "key": name, "down": down})
                    elif kind == "text":
                        assert isinstance(event["value"], str) and event["value"]
                        translated = [{"type": "text", "text": event["value"]}]
                    elif kind == "wheel":
                        unsupported.append({"event": event, **provenance, "credit": False})
                    else:
                        assert kind == "position"
                    for item in translated:
                        result.append({**item, "provenance": provenance})
        assert result, "UI surface requires actual nonempty physical input observations"
        return result, unsupported

    def emit(self, stem, context, assertions, origins, inputs, unsupported):
        m = self.manifest
        producer_keys = ("sdk", "host") if context == "cpp_api" else ("python",) if context == "python_api" else (context,)
        dependencies = [m["producers"][key] for key in producer_keys]
        sources = [m["reader_source"]]
        if context.startswith("imgui_"):
            sources.append(m["paired_reader_source"])
        original_artifacts = []
        for producer in dependencies:
            sources.extend(producer["sources"])
            original_artifacts.extend(producer["artifacts"])
        sources = _unique_pins(sources)
        original_artifacts = _unique_pins(original_artifacts)
        retained = [self.archived[_pin_identity(pin)] for pin in original_artifacts if _pin_identity(pin) in self.archived]
        artifacts = []
        for pin in original_artifacts:
            archived = self.archived.get(_pin_identity(pin))
            artifacts.extend((archived["archive"], archived["index"]) if archived else (pin,))
        artifacts = _unique_pins(artifacts)
        producer_id = "p06f.rank.surface." + context + ".001"
        case_id = "case.rank." + stem + "." + context
        report = {"suite": "p06f_rank_surfaces", "classname": "tests.qualification.test_rank_surface_reader",
                  "name": "test_rank_surface[" + stem + "-" + context + "]"}
        contract = self.contracts[stem]
        case = {"id": case_id, "requirement_id": contract["requirement_id"], "context": context,
                "contract_id": contract["id"], "contract_sha256": contract["sha256"],
                "report_role": "python", "report_case_identity": report, "assertions": assertions}
        if context.startswith("imgui_"):
            # Formal input events have exact keys; source data is retained in a
            # separate indexed provenance array rather than invented event fields.
            case["inputs"] = [{key: value for key, value in event.items() if key != "provenance"} for event in inputs]
        trace = {"schema_version": 1, "run_id": m["run_id"], "producer_id": producer_id,
                 "acceptance_claim": False, "complete_requirement_coverage": False, "cases": [case],
                 "raw_producers": dependencies, "observation_origins": origins,
                 "input_provenance": [{"input_index": index, **event["provenance"]} for index, event in enumerate(inputs)],
                 "input_manifest": self.manifest_pin, "retained_products": retained,
                 "unsupported_inputs": unsupported, "scope_limits": list(SCOPES)}
        trace_path = self.output / (stem + "-" + context + ".trace.json")
        trace_pin = self.write_fresh(trace_path, trace)
        catalogue = {"id": "trace.rank." + stem + "." + context, "path": trace_pin["path"],
                     "sha256": trace_pin["sha256"], "trace_case_id": case_id,
                     "report_role": "python", "report_case_identity": report,
                     "producer": {"id": producer_id, "context": context, "sources": sources, "artifacts": artifacts}}
        self.write_fresh(self.output / (stem + "-" + context + ".catalogue.json"),
                         {"schema_version": 1, "run_id": m["run_id"], "acceptance_claim": False,
                          "complete_requirement_coverage": False, "trace": catalogue,
                          "raw_producers": dependencies, "retained_products": retained})

    def write_fresh(self, path, value):
        data = _canonical(value) + b"\n"
        assert len(data) <= MAX_JSON
        self.written_bytes += len(data)
        assert self.written_bytes <= 64 * 1024 * 1024
        with path.open("xb") as target:
            target.write(data)
        assert path.read_bytes() == data
        return {"path": path.relative_to(ROOT).as_posix(), "bytes": len(data), "sha256": _sha(data)}

    def recheck(self):
        # Detect a changed admitted input after comparisons as well as before.
        pins = list(self.admitted_pins.values())
        self.verified.clear()
        for pin, artifact in pins:
            self.verify(pin, artifact=artifact)
        assert self.local_pin(self.manifest_path) == self.manifest_pin


def _unique_pins(pins):
    result, seen = [], {}
    for pin in pins:
        identity = _pin_identity(pin)
        path = identity[0]
        if path not in seen:
            seen[path] = identity
            result.append(pin)
        else:
            assert seen[path] == identity, "one path cannot identify different producer bytes"
    return result


def _api_pixel_layout(oid, value):
    if oid.startswith("sdk."):
        shape = [2, 2, 3, 5, 5, 1] if oid == "sdk.session.scientific_bytes" else [5, 5, 1]
        dtype = (oid.split(".")[1] if oid.endswith(".published") else
                 "uint8" if oid == "sdk.roi.holed_maximum.u8" else "uint16")
    else:
        shape = ([2, 2, 3, 3, 3] if oid == "python.captured.holed_output" else
                 [2, 2, 3, 5, 5, 1] if oid == "python.scientific.reopened_bytes" else [3, 3])
        dtype = oid.split(".")[1] if oid.endswith(".published") else "uint16"
    assert value["shape"] == shape and value["pixel_type"] == dtype, oid + ": declared pixel layout differs"


def _declared_bytes(value):
    pixel_type, shape = value["pixel_type"], value["shape"]
    assert pixel_type in ("uint8", "uint16", "float32")
    assert isinstance(shape, list) and 1 <= len(shape) <= 6
    count = math.prod(_positive(size, 1024) for size in shape)
    assert count <= 1024 * 1024
    encoded = value["hex"]
    assert isinstance(encoded, str) and re.fullmatch(r"[0-9a-f]*", encoded)
    data = bytes.fromhex(encoded)
    assert len(data) == count * {"uint8": 1, "uint16": 2, "float32": 4}[pixel_type]
    if pixel_type == "float32":
        assert all(math.isfinite(item[0]) for item in struct.iter_unpack("<f", data))
    return data


def _readback_bytes(actual):
    dtype, shape, pixels = actual["dtype"], actual["shape"], actual["pixels"]
    assert shape == [5, 5, 1] and len(pixels) == 25
    assert dtype in ("uint8", "uint16", "float32")
    assert all(type(item) in (int, float) and math.isfinite(item) for item in pixels)
    if dtype != "float32":
        maximum = 255 if dtype == "uint8" else 65535
        assert all(item == int(item) and 0 <= item <= maximum for item in pixels)
        return struct.pack("<25" + ("B" if dtype == "uint8" else "H"), *(int(item) for item in pixels))
    return struct.pack("<25f", *pixels)


def _document_bytes(document, position=None):
    shape = document["shape"]
    assert shape[3:] == [5, 5, 1]
    planes = document["planes"]
    positions = list(itertools.product(*(range(size) for size in shape[:3])))
    assert [tuple(plane["position"]) for plane in planes] == positions
    selected = planes if position is None else [plane for plane in planes if tuple(plane["position"]) == position]
    assert selected and (position is None or len(selected) == 1)
    return b"".join(_declared_bytes({"pixel_type": document["dtype"], "shape": [5, 5, 1],
                                     "hex": plane["declared_hex"]}) for plane in selected)


def _position_string(positions):
    return ";".join(",".join(str(value) for value in position) for position in positions)


def _session_capture(descriptor):
    raw = bytes.fromhex(descriptor["hex"])
    assert len(raw) == descriptor["byte_count"] and _sha(raw) == descriptor["sha256"]
    assert raw[:10] == b"IMAGEJXX3\n" and len(raw) > 18
    declared, = struct.unpack("<Q", raw[10:18])
    assert 0 < declared <= 8 * 1024 * 1024
    inflater = zlib.decompressobj()
    decoded = inflater.decompress(raw[18:], declared + 1)
    assert inflater.eof and not inflater.unused_data and not inflater.unconsumed_tail and len(decoded) == declared
    capture = _decode(decoded)
    assert capture["format"] == "imagejxx-session" and type(capture["version"]) is int
    return capture


def _named_key(name):
    return isinstance(name, str) and (name in {"Ctrl", "Shift", "Alt", "Super", "Enter", "Escape", "Tab",
        "Backspace", "Delete", "Space", "Home", "End", "PageUp", "PageDown", "Left", "Right", "Up", "Down"}
        or bool(re.fullmatch(r"(?:[A-Za-z0-9]|F(?:[1-9]|1[0-9]|2[0-4]))", name)))


def _check_assertion(frozen, observed):
    rule = frozen["rule"]
    if rule == "sha256_equal":
        assert isinstance(observed, str) and re.fullmatch(r"[0-9a-f]{64}", observed)
    else:
        assert rule == "scalar_equal"
        kind = frozen["value_type"]
        assert ((kind == "boolean" and type(observed) is bool)
                or (kind == "integer" and type(observed) is int)
                or (kind == "number" and type(observed) in (int, float) and math.isfinite(observed))
                or (kind == "string" and isinstance(observed, str))
                or (kind == "null" and observed is None)), frozen["id"] + ": wrong actual scalar type"
    assert _canonical(observed) == _canonical(frozen["expected"]), frozen["id"] + ": actual differs from frozen expected"


@pytest.fixture(scope="module")
def rank_evidence():
    inputs = os.environ.get("IMAGEJXX_RANK_SURFACE_RUN_INPUTS")
    output = os.environ.get("IMAGEJXX_RANK_SURFACE_OUTPUT_DIR")
    assert inputs and output, "both rank surface qualification environment paths are required"
    evidence = RankEvidence(_path(inputs), _path(output))
    yield evidence
    evidence.recheck()


@pytest.mark.parametrize("stem,context", PARAMETERS, ids=PARAMETER_IDS)
def test_rank_surface(stem, context, rank_evidence):
    evidence = rank_evidence
    frozen = evidence.contracts[stem]["contexts"][context]
    assertions, origins, inputs, unsupported = [], {}, [], []
    if context.startswith("imgui_"):
        # Re-run the exact independent assertions over actual rows, including
        # consumer attribution, target/ROI/history/lease and NativeV3 checks.
        for case in UI_CASES[stem]:
            evidence.ui(context, case)
        values = evidence.ui_extract(context, stem)
        assert set(values) == {assertion["id"] for assertion in frozen}
        inputs, unsupported = evidence.inputs(context, UI_CASES[stem])
    for assertion in frozen:
        oid = assertion["id"]
        observed, provenance = values[oid] if context.startswith("imgui_") else evidence.api_value(oid)
        if oid in ("python.callback.reserved_bytes", "python.gil.thread_progress"):
            evidence.python_lifecycle_support(oid, provenance)
        _check_assertion(assertion, observed)
        assertions.append({**assertion, "observed": observed})
        origins[oid] = provenance
    assert assertions
    evidence.emit(stem, context, assertions, origins, inputs, unsupported)
