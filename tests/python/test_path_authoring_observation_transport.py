"""Integrity and bounded sharing checks for complete P13h observations."""

import hashlib
import json

import pytest

from path_authoring_observations import decode_path_authoring_observations as decode


def report(text, count=2):
    key = hashlib.sha256(text.encode("utf-8")).hexdigest()
    return {"frames": [{"rendered": {"linked_saved_handles": {"$actual_blob": key}},
                        "events": [{"kind": "pointer", "x": index}]} for index in range(count)],
            "actual_blob_transport": {"version": 1, "encoding": "utf8-json", "blobs": {key: text}}}


def test_unique_payload_is_complete_shared_immutable_and_charged_once():
    actual = [{"id": 17, "points": [[1, 2], [3, 4]], "label": "µm"}]
    text = json.dumps(actual, ensure_ascii=False, separators=(",", ":"))
    raw = report(text, 20)
    decoded = decode(raw, max_payload_bytes=len(text.encode("utf-8")))
    first = decoded["frames"][0]["rendered"]["linked_saved_handles"]
    assert first == actual and not first != actual
    assert decoded["frames"][19]["rendered"]["linked_saved_handles"] is first
    assert [frame["events"] for frame in decoded["frames"]] == [frame["events"] for frame in raw["frames"]]
    with pytest.raises(TypeError):
        first[0]["id"] = 99
    with pytest.raises(TypeError):
        first[0]["points"][0][0] = 99
    assert "$actual_blob" in raw["frames"][0]["rendered"]["linked_saved_handles"]


@pytest.mark.parametrize("text", ['[{"a":1,"a":2}]', '[NaN]', '[Infinity]', '[1e999]',
                                 '[{"$actual_blob":"' + "0" * 64 + '"}]', '{}'])
def test_corrupt_or_non_array_payload_refuses(text):
    with pytest.raises((AssertionError, ValueError)):
        decode(report(text))


def test_digest_missing_orphan_and_wrong_location_refuse():
    raw = report('[{"id":17}]')
    key = next(iter(raw["actual_blob_transport"]["blobs"]))
    raw["actual_blob_transport"]["blobs"][key] = '[{"id":18}]'
    with pytest.raises(AssertionError, match="digest"):
        decode(raw)
    raw = report('[]')
    raw["frames"][0]["rendered"]["linked_saved_handles"]["$actual_blob"] = "0" * 64
    with pytest.raises(AssertionError, match="Missing"):
        decode(raw)
    with pytest.raises(AssertionError, match="Orphan"):
        decode(report('[]', 0))
    raw = report('[]')
    raw["other"] = raw["frames"][0]["rendered"].pop("linked_saved_handles")
    with pytest.raises(AssertionError, match="location"):
        decode(raw)


def test_wire_payload_reference_and_depth_bounds_refuse():
    raw = report('[1,2,3]')
    size = len(json.dumps(raw, ensure_ascii=False, separators=(",", ":")).encode("utf-8"))
    with pytest.raises(AssertionError):
        decode(raw, max_wire_bytes=size)
    with pytest.raises(AssertionError, match="payload exceeds"):
        decode(raw, max_payload_bytes=6)
    with pytest.raises(AssertionError, match="references"):
        decode(report('[]', 8193))
    with pytest.raises(AssertionError, match="nesting"):
        decode(report('[' * 66 + '0' + ']' * 66))


def test_plain_reports_still_reject_references_and_preserve_values():
    raw = {"frames": [{"events": [], "rendered": {"linked_saved_handles": [1, 2]}}]}
    assert decode(raw) == raw
    with pytest.raises(AssertionError, match="Missing"):
        decode({"frames": [{"rendered": {"linked_saved_handles": {"$actual_blob": "0" * 64}}}]})


@pytest.mark.parametrize("transport", [None, {}, {"version": True, "encoding": "utf8-json", "blobs": {}}])
def test_malformed_transport_envelope_refuses(transport):
    with pytest.raises(AssertionError):
        decode({"frames": [], "actual_blob_transport": transport})
