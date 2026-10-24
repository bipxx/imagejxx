"""Bounded, lossless P13h frame payloads; no product or renderer imports."""

import hashlib
import json
import math
import re
from types import MappingProxyType


class _ReadOnlyArray(tuple):
    def __eq__(self, other):
        return (isinstance(other, (list, tuple)) and len(self) == len(other)
                and all(a == b for a, b in zip(self, other)))

    def __ne__(self, other):
        return not self == other

    __hash__ = None


def _pairs(values):
    result = {}
    for key, value in values:
        assert key not in result, "Duplicate path observation key"
        result[key] = value
    return result


def _nonfinite(value):
    raise AssertionError("Nonfinite path observation: " + value)


def decode_path_authoring_observations(row, *, max_wire_bytes=64 * 1024 * 1024,
                                      max_payload_bytes=64 * 1024 * 1024):
    """Validate wire bounds and share immutable, independently hashed arrays.

    Only frame rendered.linked_saved_handles can reference blobs. Each unique
    payload is parsed once; repeated frames share that immutable value. The
    unique resident JSON byte budget is distinct from hypothetical duplication
    across frames and does not claim to measure Python object overhead or RSS.
    Existing generic transport readers and their expansion limits are unchanged.
    """
    assert type(row) is dict
    assert type(max_wire_bytes) is int and max_wire_bytes > 0
    assert type(max_payload_bytes) is int and max_payload_bytes > 0
    assert len(json.dumps(row, ensure_ascii=False, allow_nan=False,
                          separators=(",", ":")).encode("utf-8")) < max_wire_bytes
    transport = row.get("actual_blob_transport")
    blobs = {}
    if "actual_blob_transport" in row:
        assert type(transport) is dict and set(transport) == {"version", "encoding", "blobs"}
        assert type(transport["version"]) is int and transport["version"] == 1
        assert transport["encoding"] == "utf8-json"
        blobs = transport["blobs"]
        assert type(blobs) is dict and len(blobs) <= 8192
    cache = {}
    charged = references = 0
    used = set()

    def freeze(value, depth=0):
        assert depth <= 64, "Path observation payload nesting exceeds64"
        if type(value) is dict:
            assert "$actual_blob" not in value, "Nested path observation reference"
            return MappingProxyType({key: freeze(item, depth + 1) for key, item in value.items()})
        if type(value) is list:
            return _ReadOnlyArray(freeze(item, depth + 1) for item in value)
        if type(value) is float:
            assert math.isfinite(value), "Nonfinite path observation number"
        return value

    # Validate every stored payload, including membership, before exposing data.
    for key, text in blobs.items():
        assert type(key) is str and re.fullmatch(r"[0-9a-f]{64}", key)
        assert type(text) is str
        encoded = text.encode("utf-8")
        charged += len(encoded)
        assert charged <= max_payload_bytes, "Path observation unique payload exceeds bound"
        assert hashlib.sha256(encoded).hexdigest() == key, "Path observation digest mismatch"
        payload = json.loads(text, object_pairs_hook=_pairs, parse_constant=_nonfinite)
        assert type(payload) is list, "Linked saved handles payload must be an array"
        cache[key] = freeze(payload)

    def walk(value, path=(), depth=0):
        nonlocal references
        assert depth <= 64, "Path observation nesting exceeds64"
        if type(value) is dict:
            if "$actual_blob" in value:
                assert set(value) == {"$actual_blob"}
                assert (len(path) == 4 and path[0] == "frames" and type(path[1]) is int
                        and path[2:] == ("rendered", "linked_saved_handles")), "Unexpected path blob location"
                key = value["$actual_blob"]
                assert type(key) is str and key in cache, "Missing path observation payload"
                references += 1
                assert references <= 8192, "Too many path observation references"
                used.add(key)
                return cache[key]
            return {key: walk(item, path + (key,), depth + 1) for key, item in value.items()}
        if type(value) is list:
            return [walk(item, path + (index,), depth + 1) for index, item in enumerate(value)]
        if type(value) is float:
            assert math.isfinite(value), "Nonfinite path observation number"
        return value

    result = {key: walk(value, (key,)) for key, value in row.items() if key != "actual_blob_transport"}
    assert used == set(blobs), "Orphan path observation payload"
    return result
