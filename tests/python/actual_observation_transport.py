"""Decode lossless, source-bound actual UI observations without product imports."""

import hashlib
import json
import math
import re


def _pairs(items):
    result = {}
    for key, value in items:
        assert key not in result, "Duplicate actual observation key"
        result[key] = value
    return result


def _nonfinite(value):
    raise AssertionError("Nonfinite actual observation: " + value)


def decode_actual_observations(row, *, max_expanded_bytes=64 * 1024 * 1024):
    """Expand complete captured JSON values; never obtain values from expectations.

    Legacy reports stay valid. New reports store repeated actual frame fields and
    pixels once, with SHA256 references. The retained raw report is pinned before
    this decoder runs. Expansion has a separate bound and rejects orphan payloads.
    """
    assert type(row) is dict
    if "actual_blob_transport" not in row:
        return row
    transport = row["actual_blob_transport"]
    assert type(transport) is dict and set(transport) == {"version", "encoding", "blobs"}
    assert type(transport["version"]) is int and transport["version"] == 1
    assert transport["encoding"] == "utf8-json"
    blobs = transport["blobs"]
    assert type(blobs) is dict and len(blobs) <= 8192
    assert type(max_expanded_bytes) is int and max_expanded_bytes > 0
    for key, payload in blobs.items():
        assert type(key) is str and re.fullmatch(r"[0-9a-f]{64}", key)
        assert type(payload) is str
        assert hashlib.sha256(payload.encode("utf-8")).hexdigest() == key
    used = set()
    expanded = references = 0

    def walk(value, depth=0, *, payload=False):
        nonlocal expanded, references
        assert depth <= 64, "Actual observation nesting exceeds64"
        if type(value) is dict:
            if "$actual_blob" in value:
                assert not payload, "Nested actual payload reference"
                assert set(value) == {"$actual_blob"}
                key = value["$actual_blob"]
                assert type(key) is str and key in blobs
                references += 1
                assert references <= 8192
                text = blobs[key]
                expanded += len(text.encode("utf-8"))
                assert expanded <= max_expanded_bytes, "Actual observation expansion exceeds bound"
                used.add(key)
                actual = json.loads(text, object_pairs_hook=_pairs, parse_constant=_nonfinite)
                return walk(actual, depth + 1, payload=True)
            return {key: walk(item, depth + 1, payload=payload) for key, item in value.items()}
        if type(value) is list:
            return [walk(item, depth + 1, payload=payload) for item in value]
        if type(value) is float:
            assert math.isfinite(value), "Nonfinite actual observation number"
        return value

    result = {key: walk(value) for key, value in row.items() if key != "actual_blob_transport"}
    assert used == set(blobs), "Orphan actual observation payload"
    return result
