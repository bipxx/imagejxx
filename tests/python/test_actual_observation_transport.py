"""Transport integrity checks; no ImageJxx or renderer import."""

import hashlib
import json

import pytest

from actual_observation_transport import decode_actual_observations


def _report(payload, references=1):
    key = hashlib.sha256(payload.encode("utf-8")).hexdigest()
    return {"frames": [{"value": {"$actual_blob": key}} for _ in range(references)],
            "actual_blob_transport": {"version": 1, "encoding": "utf8-json", "blobs": {key: payload}}}


def test_complete_actual_values_round_trip_without_aliases():
    actual = {"pixels": {"hex": "00ff", "storage_f64le_hex": "0000000000000000"},
              "control": {"id": 17, "visible": True, "label": "µm"}}
    raw = _report(json.dumps(actual, ensure_ascii=False, separators=(",", ":")), 3)
    result = decode_actual_observations(raw)
    assert result["frames"] == [{"value": actual}] * 3
    result["frames"][0]["value"]["control"]["id"] = 19
    assert result["frames"][1]["value"]["control"]["id"] == 17
    assert "$actual_blob" in raw["frames"][0]["value"]


@pytest.mark.parametrize("payload", ['{"a":1,"a":2}', '{"a":NaN}', '{"a":Infinity}', '{"a":1e999}'])
def test_invalid_actual_payload_is_refused(payload):
    with pytest.raises(AssertionError):
        decode_actual_observations(_report(payload))


def test_tampered_missing_or_orphan_payload_is_refused():
    raw = _report('{"actual":17}')
    key = next(iter(raw["actual_blob_transport"]["blobs"]))
    raw["actual_blob_transport"]["blobs"][key] = '{"actual":18}'
    with pytest.raises(AssertionError):
        decode_actual_observations(raw)
    raw = _report('{"actual":17}')
    raw["frames"][0]["value"]["$actual_blob"] = "0" * 64
    with pytest.raises(AssertionError):
        decode_actual_observations(raw)
    with pytest.raises(AssertionError):
        decode_actual_observations(_report('{"actual":17}', 0))


def test_expansion_and_payload_reference_bounds_are_enforced():
    text = '{"actual":17}'
    with pytest.raises(AssertionError):
        decode_actual_observations(_report(text, 2), max_expanded_bytes=len(text))
    with pytest.raises(AssertionError):
        decode_actual_observations(_report('{"$actual_blob":"' + "0" * 64 + '"}'))
    with pytest.raises(AssertionError):
        decode_actual_observations(_report(text, 8193))


def test_legacy_actual_report_is_preserved():
    actual = {"frames": [{"value": 19}]}
    assert decode_actual_observations(actual) is actual
