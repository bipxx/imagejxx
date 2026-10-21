"""Held independent supplemental regressions; the original 139 stay separate.

Root owns matching installed qualification, declaration, per-worker finite
watchdogs, collection and runtime. Source preparation imports no product/tests.
"""

import base64
from copy import deepcopy
from dataclasses import fields, is_dataclass
import hashlib
import json
from pathlib import Path
import struct
import time
import zlib

import imagejxx as ij
from imagejxx import _native
import numpy as np
import pytest

from imagejxx.ui.model import Document
from imagejxx.ui import persistence


_FIXTURE_DIR = (
    Path(__file__).resolve().parents[1] / "fixtures" / "marked_session"
)
_BASELINE = _FIXTURE_DIR / "literal-cases-001.json"
_BASELINE_SHA256 = "21f98e965b3a3985e1f7bdb4c7c27f1bbd09523cf1e40d5141e996fb94c29337"
_BASELINE_BYTES = _BASELINE.read_bytes()
assert hashlib.sha256(_BASELINE_BYTES).hexdigest() == _BASELINE_SHA256
_LITERALS = json.loads(_BASELINE_BYTES)
_VALID = next(case for case in _LITERALS["native_fixtures"] if case["id"] == "named")
_TAIL_PATH = _FIXTURE_DIR / "tail-mask-literals-001.json"
_TAIL_BYTES = _TAIL_PATH.read_bytes()
assert hashlib.sha256(_TAIL_BYTES).hexdigest() == (
    "c9835f7377d723e2cb22a9ea5f90e1779001eec21a9c914d6c4a02d114b2eea4"
)
_TAIL = json.loads(_TAIL_BYTES)
_NONMASK_BYTES = (
    _FIXTURE_DIR / "nonmask-opaque-literals-001.json"
).read_bytes()
assert hashlib.sha256(_NONMASK_BYTES).hexdigest() == (
    "7d014acca06f2a32cfe02f1a9e7a14ed0f39dd1f0c5d3ac0ab1505d331388cec"
)
_NONMASK = json.loads(_NONMASK_BYTES)
_TRANSPORT = {
    "pixels", "dtype", "shape", "spectrum", "fft_shape", "spectrum_shape",
    "pixel_sample_policy",
}


def _typed(value):
    if value is None:
        return ("null",)
    if type(value) is bool:
        return ("bool", value)
    if type(value) is int:
        return ("integer", value)
    if type(value) is float:
        assert np.isfinite(value)
        return ("double", struct.pack("<d", value))
    if type(value) is str:
        return ("string", value)
    if type(value) in (list, tuple):
        return ("array", tuple(_typed(item) for item in value))
    assert type(value) is dict
    return ("object", tuple((key, _typed(value[key])) for key in sorted(value)))


def _state(value):
    """Observe every detached DATA/DISPLAY/history field without serialization."""
    if isinstance(value, np.ndarray):
        return ("ndarray", value.dtype.str, value.shape, value.tobytes())
    if isinstance(value, (bytes, bytearray)):
        return (type(value).__name__, bytes(value))
    if isinstance(value, ij.FloatingSamplePolicy):
        return ("sample-policy", str(value))
    if isinstance(value, Path):
        return ("path", str(value))
    if is_dataclass(value):
        declared = {f.name for f in fields(value)}
        extra = {
            key: item for key, item in getattr(value, "__dict__", {}).items()
            if key not in declared
        }
        return (
            "dataclass", type(value).__name__,
            tuple((f.name, _state(getattr(value, f.name))) for f in fields(value)),
            ("extra", _state(extra)),
        )
    if type(value) is dict:
        return ("dict", tuple((key, _state(value[key])) for key in sorted(value)))
    if type(value) in (list, tuple):
        return (type(value).__name__, tuple(_state(item) for item in value))
    if type(value) is float:
        return ("double", struct.pack("<d", value))
    assert value is None or type(value) in (bool, str, int), type(value)
    return (type(value).__name__, value)


def _fixture_root(case):
    return {
        "format": "imagejxx-session", "version": 2,
        "documents": [deepcopy(case["wire"])],
        "results": deepcopy(_LITERALS["results"]),
        "workflow": deepcopy(_LITERALS["workflow"]),
        "native_document_state": {
            "version": 1,
            "documents": [{
                "display_properties": deepcopy(case["display_keys"]),
                "metadata": deepcopy(case["native_metadata"]),
            }],
        },
    }


def _load_marked(tmp_path, *, retained_calibration_fields=None):
    case = deepcopy(_VALID)
    if retained_calibration_fields is not None:
        case["native_metadata"]["record_layout"]["calibration_fields"] = list(
            retained_calibration_fields
        )
    # The original physical/time fields are all nondefault; nativeMetadata
    # emits them even when the retained calibration layout only names "unit".
    # This is a consistent marked input, derived from independent baseline
    # literals and the unchanged native put(value != default) rule.
    payload = json.dumps(_fixture_root(case), allow_nan=False).encode("utf-8")
    assert len(payload) < 256 * 1024
    source = tmp_path / "valid-marked-source.ijx"
    source.write_bytes(
        b"IMAGEJXX2\n" + struct.pack(">I", len(payload)) + zlib.compress(payload)
    )
    documents, results, workflow = persistence.load_session(source)
    assert len(documents) == 1
    return documents[0], results, workflow


def _publication_guard(monkeypatch, tmp_path):
    destination = tmp_path / "retained.ijx"
    sentinel = b"original destination\x00\xff"
    destination.write_bytes(sentinel)
    unrelated = tmp_path / "retained.ijx.tmp"
    unrelated.write_bytes(b"unrelated temporary")
    original_factory = persistence.tempfile.NamedTemporaryFile
    calls = []

    def observe_factory(*args, **kwargs):
        calls.append((args, kwargs))
        return original_factory(*args, **kwargs)

    monkeypatch.setattr(
        persistence.tempfile, "NamedTemporaryFile", observe_factory
    )
    files_before = {
        path.name: path.read_bytes() for path in tmp_path.iterdir() if path.is_file()
    }
    return destination, sentinel, unrelated, calls, files_before


def _assert_no_publication(tmp_path, guard):
    destination, sentinel, unrelated, calls, files_before = guard
    assert calls == []
    assert destination.read_bytes() == sentinel
    assert unrelated.read_bytes() == b"unrelated temporary"
    assert not list(tmp_path.glob(".ijxs-*.tmp"))
    assert {
        path.name: path.read_bytes() for path in tmp_path.iterdir() if path.is_file()
    } == files_before


def test_R1_outgoing_sparse_calibration_rejects_before_temporary(
    tmp_path, monkeypatch
):
    # Supply an unsupported native record at the production save seam. A
    # normal known calibration edit repairs its layout and remains reloadable;
    # this explicit record instead omits four required positive scalar fields.
    native_metadata = {
        "title": "sparse-calibration", "properties": {},
        "extensions": {"is_stack": False, "labels": False},
        "record_layout": {
            "root_fields": ["calibration", "overlays", "rois", "title"],
            "null_fields": [], "calibration_fields": ["unit"],
            "roi_slice_fields": [],
        },
    }

    class RecordDocument(Document):
        def _session_native_state(self):
            return {
                "metadata": deepcopy(native_metadata),
                "display_properties": {
                    "display_range": None, "lut": "Grays", "custom_lut": None,
                },
            }

    document = RecordDocument(np.arange(6, dtype=np.float64).reshape(2, 3),
                              native_metadata["title"])
    results, workflow = [{"retained": 1}], [{"retained": True}]
    before = _state(document.__dict__)
    tables_before = _typed((results, workflow))
    guard = _publication_guard(monkeypatch, tmp_path)
    with pytest.raises(ValueError):
        persistence.save_session(
            guard[0], [document], results=results, workflow=workflow
        )
    _assert_no_publication(tmp_path, guard)
    assert _state(document.__dict__) == before
    assert _typed((results, workflow)) == tables_before


@pytest.mark.parametrize("table", ["results", "workflow"])
def test_R2_cyclic_tables_promptly_reject_before_temporary(
    table, tmp_path, monkeypatch
):
    document, results, workflow = _load_marked(tmp_path)
    before = _state(document.__dict__)
    if table == "results":
        cyclic = [{"sentinel": "retained"}]
        cyclic.append(cyclic)
        results = cyclic
    else:
        node = {"sentinel": "retained"}
        node["next"] = node
        cyclic = [node]
        workflow = cyclic
    guard = _publication_guard(monkeypatch, tmp_path)
    started = time.monotonic()
    with pytest.raises(ValueError):
        persistence.save_session(
            guard[0], [document], results=results, workflow=workflow
        )
    assert time.monotonic() - started < 2.0
    _assert_no_publication(tmp_path, guard)
    assert _state(document.__dict__) == before
    if table == "results":
        assert cyclic[1] is cyclic and cyclic[0] == {"sentinel": "retained"}
    else:
        assert cyclic[0]["next"] is cyclic[0]
        assert cyclic[0]["sentinel"] == "retained"


def test_R2_shared_acyclic_tables_are_accepted_without_input_mutation(tmp_path):
    document, _, _ = _load_marked(tmp_path)
    common = {
        "vendor": {
            "signed": 17, "unsigned": 18446744073709551615,
            "negative_zero": -0.0,
        },
        "values": [None, True, .5],
    }
    results = [common, common]
    workflow = [
        {"operation": "literal", "parameters": common},
        {"operation": "literal", "parameters": common},
    ]
    document_before = _state(document.__dict__)
    values_before = _typed((results, workflow))
    destination = tmp_path / "shared-acyclic.ijx"
    persistence.save_session(
        destination, [document], results=results, workflow=workflow
    )
    reopened, observed_results, observed_workflow = persistence.load_session(
        destination
    )
    assert len(reopened) == 1
    assert _typed((observed_results, observed_workflow)) == values_before
    assert _typed((results, workflow)) == values_before
    assert results[0] is results[1] is common
    assert workflow[0]["parameters"] is workflow[1]["parameters"] is common
    assert _state(document.__dict__) == document_before


def _inverse(wire):
    scalar = {
        key: value for key, value in wire.items() if key not in _TRANSPORT
    }
    return json.loads(_native._session_wire_metadata_json(
        json.dumps(_VALID["native_metadata"], allow_nan=False),
        json.dumps(scalar, allow_nan=False), 3, 2, 2,
    ))


def _overflow_selection(kind):
    return {
        "kind": kind, "points": [[-1e200, 0.0], [1e200, 1.0]],
        "width": 1.0, "vendor_geometry": {"retained": True},
    }


@pytest.mark.parametrize("kind", ["line", "polyline", "angle"])
def test_R3_current_segments_overflow_native_denominator_is_rejected(kind):
    wire = deepcopy(_VALID["wire"])
    wire["selection"] = _overflow_selection(kind)
    # Endpoints themselves are finite. The native draw_line denominator
    # overflows, so Selection.validate alone is insufficient admission.
    assert all(
        np.isfinite(coordinate)
        for point in wire["selection"]["points"] for coordinate in point
    )
    with pytest.raises(ValueError):
        _inverse(wire)


def test_R3_roi_line_overflow_native_denominator_is_rejected():
    wire = deepcopy(_VALID["wire"])
    wire["rois"][0]["selection"] = _overflow_selection("line")
    with pytest.raises(ValueError):
        _inverse(wire)


@pytest.mark.parametrize(
    "previous", ["rectangle-earlier-extent", "line-overflow-retained", "mask-earlier-shape"]
)
def test_R3_previous_selection_retains_earlier_extent_semantics(previous):
    wire = deepcopy(_VALID["wire"])
    if previous == "rectangle-earlier-extent":
        expected = {
            "kind": "rectangle", "points": [[10.0, 10.0], [12.0, 12.0]],
            "width": 2.0, "vendor_previous": {"id": 18446744073709551615},
        }
    elif previous == "line-overflow-retained":
        expected = _overflow_selection("line")
    else:
        expected = {
            "kind": "mask", "points": [], "width": 1.0, "shape": [3, 2],
            "mask": base64.b64encode(bytes([0x84])).decode("ascii"),
            "vendor_previous": {"negative_zero": -0.0},
        }
    wire["previous_selection"] = deepcopy(expected)
    projected = _inverse(wire)
    assert _typed(projected["previous_selection"]) == _typed(expected)
    # Current geometry/ROI/opaque records stay independent of old extent.
    assert _typed(projected["selection"]) == _typed(_VALID["wire"]["selection"])
    assert _typed(projected["rois"]) == _typed(_VALID["wire"]["rois"])


def _load_tail(tmp_path):
    content = json.dumps(_TAIL["root"], allow_nan=False).encode("utf-8")
    source = tmp_path / "tail-input.ijx"
    source.write_bytes(
        b"IMAGEJXX2\n" + struct.pack(">I", len(content)) + zlib.compress(content)
    )
    document = persistence.load_session(source)[0][0]
    assert document.selection.bitmap.shape == (2, 3)
    np.testing.assert_array_equal(
        document.selection.bitmap, _TAIL["independent_mask_bits"][0]["bitmap_yx"]
    )
    return document


def _assert_tail_transport(document, expected_order):
    item = persistence.serialize_document(document)
    assert item["selection"]["mask"] == "pw=="
    assert item["selection"]["shape"] == [2, 3]
    assert _typed(item["selection"]["points"]) == _typed([])
    assert _typed(item["selection"]["width"]) == _typed(1.0)
    assert [roi["name"] for roi in item["rois"]] == expected_order
    assert {
        roi["name"]: roi["selection"]["mask"] for roi in item["rois"]
    } == _TAIL["expected_roi_mask_tokens"]
    for roi in item["rois"]:
        assert roi["selection"]["shape"] == [2, 3]
        assert roi["selection"]["points"] == []
        assert _typed(roi["selection"]["width"]) == _typed(1.0)
    return item


def _read_root(path):
    wire = path.read_bytes()
    assert 14 <= len(wire) < 256 * 1024 and wire[:10] == b"IMAGEJXX2\n"
    length = struct.unpack(">I", wire[10:14])[0]
    assert length < 256 * 1024
    decoder = zlib.decompressobj()
    content = decoder.decompress(wire[14:], length + 1)
    assert len(content) == length and decoder.eof and not decoder.unused_data
    return json.loads(content)


def test_R4_unused_packed_mask_tail_bits_survive_untouched_resave(tmp_path):
    document = _load_tail(tmp_path)
    _assert_tail_transport(document, _TAIL["expected_roi_order"])
    before = _state(document.__dict__)
    output = tmp_path / "tail-resaved.ijx"
    persistence.save_session(output, [document])
    item = _read_root(output)["documents"][0]
    assert item["selection"]["mask"] == "pw=="
    assert {
        roi["name"]: roi["selection"]["mask"] for roi in item["rois"]
    } == _TAIL["expected_roi_mask_tokens"]
    assert _state(document.__dict__) == before
    reopened = persistence.load_session(output)[0][0]
    _assert_tail_transport(reopened, _TAIL["expected_roi_order"])


def test_R4_unused_tail_bits_survive_attribute_and_style_edits(tmp_path):
    document = _load_tail(tmp_path)
    document.selection.attributes["vendor_mask"]["edited"] = True
    document.selection.attributes["imagejxx.roi_style"] = {
        "schema_version": 1, "stroke_width": 2.0,
    }
    document.rois[0].selection.attributes["imagejxx.roi_style"] = {
        "schema_version": 1, "stroke_width": 3.0,
    }
    item = _assert_tail_transport(document, _TAIL["expected_roi_order"])
    assert item["selection"]["vendor_mask"]["edited"] is True
    assert item["selection"]["imagejxx.roi_style"]["stroke_width"] == 2.0
    assert item["rois"][0]["selection"]["imagejxx.roi_style"]["stroke_width"] == 3.0
    assert "edited" not in item["rois"][0]["selection"]["vendor_mask"]
    output = tmp_path / "tail-style-resaved.ijx"
    persistence.save_session(output, [document])
    _assert_tail_transport(
        persistence.load_session(output)[0][0], _TAIL["expected_roi_order"]
    )


def test_R4_mask_templates_follow_roi_reorder_deepcopy_and_data_history(tmp_path):
    original = _load_tail(tmp_path)
    original_before = _state(original.__dict__)
    copied = deepcopy(original)
    assert not np.shares_memory(copied.data, original.data)
    assert not np.shares_memory(
        copied.selection.bitmap, original.selection.bitmap
    )
    copied.rois.reverse()
    _assert_tail_transport(copied, ["tail-second", "tail-first"])
    copied.commit((copied.data.copy() + 1).astype(copied.data.dtype))
    copied.rois.reverse()
    _assert_tail_transport(copied, ["tail-first", "tail-second"])
    copied.undo()
    _assert_tail_transport(copied, ["tail-second", "tail-first"])
    copied.redo()
    _assert_tail_transport(copied, ["tail-first", "tail-second"])
    assert _state(original.__dict__) == original_before
    _assert_tail_transport(original, ["tail-first", "tail-second"])


def _with_spectrum(document):
    # Independent finite complex values; no transform or producer output.
    components = [
        (1.0, -0.0), (2.0, .5), (-3.0, 1.25),
        (0.0, -2.0), (.25, 4.0), (5.0, 0.0),
    ]
    raw = b"".join(struct.pack("<dd", real, imag) for real, imag in components)
    document.spectrum = np.frombuffer(raw, dtype="<c16").reshape(2, 3).copy()
    document.spectrum_shape = (2, 3)
    return raw


@pytest.mark.parametrize(
    "invalid", ["nonpositive", "three-dimensions", "no-spectrum", "out-of-bounds"]
)
def test_R5_invalid_spectrum_shape_bulk_adoption_is_atomic(invalid, tmp_path):
    document, _, _ = _load_marked(tmp_path)
    if invalid != "no-spectrum":
        _with_spectrum(document)
    before = _state(document.__dict__)
    value = document.metadata()
    value["title"] = "Must not publish"
    value["native_properties"] = {"attempt": True}
    value["vendor"] = {"attempt": "must not publish"}
    value["calibration"]["pixel_width"] = 2.0
    value["lut"] = "Ice"
    value["view_state"]["zoom"] = 8.5
    value["spectrum_shape"] = {
        "nonpositive": [0, -1], "three-dimensions": [1, 1, 1],
        "no-spectrum": [1, 1], "out-of-bounds": [3, 4],
    }[invalid]
    with pytest.raises(ValueError):
        document.load_metadata(value)
    assert _state(document.__dict__) == before


def test_R5_valid_supplied_spectrum_shape_bulk_adoption_succeeds(tmp_path):
    document, _, _ = _load_marked(tmp_path)
    raw = _with_spectrum(document)
    pixels_before = document.data.tobytes()
    value = document.metadata()
    value["title"] = "Accepted spectrum shape"
    value["vendor"] = {"updated": "accepted"}
    value["spectrum_shape"] = [1, 2]
    document.load_metadata(value)
    assert document.spectrum_shape == (1, 2)
    assert document.spectrum.shape == (2, 3)
    assert document.spectrum.dtype == np.dtype(np.complex128)
    assert document.spectrum.tobytes() == raw
    assert document.data.tobytes() == pixels_before
    assert document.title == "Accepted spectrum shape"
    assert document.metadata()["vendor"] == {"updated": "accepted"}


@pytest.mark.parametrize("case_id", [
    "selection-storage-looking", "selection-numeric-mask",
    "selection-invalid-storage-looking", "roi-storage-looking",
    "roi-numeric-mask", "roi-invalid-storage-looking",
])
def test_R6_nonmask_opaque_storage_names_preserve_effective_geometry(
    case_id, tmp_path
):
    location, variant_id = case_id.split("-", 1)
    variant = next(
        value for value in _NONMASK["variants"] if value["id"] == variant_id
    )
    root = _fixture_root(_VALID)
    if location == "selection":
        root["documents"][0]["selection"] = deepcopy(variant["record"])
    else:
        root["documents"][0]["rois"][0]["selection"] = deepcopy(
            variant["record"]
        )
    payload = json.dumps(root, allow_nan=False).encode("utf-8")
    source = tmp_path / "opaque-line-input.ijx"
    source.write_bytes(
        b"IMAGEJXX2\n" + struct.pack(">I", len(payload)) + zlib.compress(payload)
    )
    document = persistence.load_session(source)[0][0]
    selected = document.selection if location == "selection" else (
        document.rois[0].selection
    )
    assert selected.kind == "line" and selected.bitmap is None
    assert _typed(selected.attributes) == _typed(variant["attributes"])
    native = selected.native()
    assert native.kind == "line"
    assert _typed(native.attributes) == _typed(variant["attributes"])
    np.testing.assert_array_equal(
        native.mask((2, 3)), _NONMASK["independent_native_raster_yx"]
    )
    item = persistence.serialize_document(document)
    observed = item["selection"] if location == "selection" else (
        item["rois"][0]["selection"]
    )
    assert _typed(observed) == _typed(variant["record"])
    output = tmp_path / "opaque-line-resaved.ijx"
    persistence.save_session(output, [document])
    reopened = persistence.load_session(output)[0][0]
    selected_again = reopened.selection if location == "selection" else (
        reopened.rois[0].selection
    )
    assert selected_again.kind == "line" and selected_again.bitmap is None
    assert _typed(selected_again.attributes) == _typed(variant["attributes"])
