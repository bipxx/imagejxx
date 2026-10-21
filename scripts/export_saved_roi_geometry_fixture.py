"""Transport the reviewed, independently authored saved-ROI literals as JSONL.

This exporter does not calculate an expectation or execute ImageJ/imagejxx. The
single pinned corpus is the source of every emitted input and expected value.
The JSONL is the ordered raw-output transport required by owned_cases_v1; an
external runner records the real interpreter, source/input pins and exit status.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path


INPUT_BYTES = 102293
INPUT_SHA256 = "4364cf3ef2e241ba69541d4d55ed629372f6773265f7e7dab9cc9ed9f84b4c01"
CASE_IDS = tuple(f"PX{index:02d}" for index in range(1, 11))
MAX_INPUT_BYTES = 128 * 1024
MAX_OUTPUT_BYTES = 256 * 1024
MAX_DEPTH = 32
MAX_NODES = 65536
MAX_CONTAINER_ITEMS = 4096
MAX_OBJECT_KEYS = 128
MAX_STRING_BYTES = 8192


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def _object(pairs: list[tuple[str, object]]) -> dict:
    _require(len(pairs) <= MAX_OBJECT_KEYS, "JSON object exceeds the key bound")
    result = {}
    for key, value in pairs:
        _require(key not in result, "Duplicate JSON key: " + key)
        result[key] = value
    return result


def _nonfinite(token: str) -> None:
    raise ValueError("Nonfinite JSON token: " + token)


def _bounded_json(value: object) -> None:
    """Check finite typed JSON without interpreting a geometry or pixel value."""
    pending, nodes = [(value, 0)], 0
    while pending:
        item, depth = pending.pop()
        nodes += 1
        _require(nodes <= MAX_NODES and depth <= MAX_DEPTH, "JSON structure exceeds its bounds")
        kind = type(item)
        if item is None or kind is bool:
            continue
        if kind is int:
            _require(-(2**63) <= item < 2**64, "JSON integer exceeds the metadata range")
        elif kind is float:
            _require(math.isfinite(item), "JSON number must be finite")
        elif kind is str:
            _require(len(item.encode("utf-8")) <= MAX_STRING_BYTES, "JSON string exceeds its bound")
        elif kind is list:
            _require(len(item) <= MAX_CONTAINER_ITEMS, "JSON array exceeds its bound")
            pending.extend((child, depth + 1) for child in item)
        elif kind is dict:
            _require(len(item) <= MAX_OBJECT_KEYS, "JSON object exceeds its key bound")
            for key, child in item.items():
                _require(type(key) is str and 0 < len(key.encode("utf-8")) <= 128,
                         "JSON object key must be bounded nonempty text")
                pending.append((child, depth + 1))
        else:
            raise ValueError("Unsupported JSON value type")


def _case_structure(corpus: dict) -> list[dict]:
    _require(type(corpus) is dict, "Corpus must be an object")
    _require(set(corpus) == {"schema_version", "format", "origin", "reference_execution",
                              "scope", "tolerances", "cases"}, "Corpus fields differ from the held source")
    _require(type(corpus["schema_version"]) is int and corpus["schema_version"] == 1,
             "Unsupported corpus schema version")
    _require(corpus["format"] == "owned_cases_v1" and corpus["origin"] == "owned_deterministic",
             "Corpus must contain project-owned literal cases")
    _require(corpus["reference_execution"] is False, "This transport cannot claim reference execution")
    _require(type(corpus["scope"]) is dict and bool(corpus["scope"]), "Corpus scope is missing")
    _require(type(corpus["tolerances"]) is dict and bool(corpus["tolerances"]), "Corpus tolerances are missing")
    cases = corpus["cases"]
    _require(type(cases) is list and len(cases) == len(CASE_IDS), "Exactly ten literal cases are required")
    _require(all(type(case) is dict for case in cases), "Each literal case must be an object")
    _require(tuple(case.get("id") for case in cases) == CASE_IDS,
             "Literal case IDs must be unique and ordered PX01 through PX10")
    for case in cases:
        _require(set(case) == {"id", "input", "expected"}, "Literal case fields changed")
        source, expected = case["input"], case["expected"]
        _require(type(source) is dict and set(source) == {"source", "steps"},
                 "Literal input must contain the complete source and steps")
        _require(type(source["source"]) is dict and bool(source["source"]), "Literal source is missing")
        _require(type(source["steps"]) is list and 0 < len(source["steps"]) <= MAX_CONTAINER_ITEMS,
                 "Literal steps must be a bounded nonempty array")
        shape = source["source"].get("shape")
        _require(type(shape) is list and len(shape) == 6 and
                 all(type(size) is int and 0 < size <= 16 for size in shape),
                 "Literal source requires six bounded positive integer dimensions")
        _require(source["source"].get("dtype") in {"uint8", "uint16", "float32"},
                 "Literal source dtype is unsupported")
        _require(source["source"].get("sample_policy") == "finite_only", "Literal source policy changed")
        _require(type(expected) is dict and {"common", "checkpoints"} <= expected.keys(),
                 "Literal expected state is incomplete")
        _require(type(expected["common"]) is dict and bool(expected["common"]), "Common expected state is missing")
        checkpoints = expected["checkpoints"]
        _require(type(checkpoints) is dict and {"initial", "final", "terminal.cleaned"} <= checkpoints.keys(),
                 "Required literal checkpoints are missing")
    return cases


def export(input_path: Path, output_path: Path) -> dict:
    """Verify one immutable literal input and exclusively create its JSONL form."""
    source = input_path.resolve(strict=True)
    _require(source.is_file(), "Literal input must be a regular file")
    _require(source.stat().st_size == INPUT_BYTES <= MAX_INPUT_BYTES, "Literal input byte count changed")
    with source.open("rb") as stream:
        raw = stream.read(MAX_INPUT_BYTES + 1)
    _require(len(raw) == INPUT_BYTES, "Literal input byte count changed during reading")
    digest = hashlib.sha256(raw).hexdigest()
    _require(digest == INPUT_SHA256, "Literal input SHA256 differs from the reviewed source")
    corpus = json.loads(raw.decode("utf-8"), object_pairs_hook=_object, parse_constant=_nonfinite)
    _bounded_json(corpus)
    cases = _case_structure(corpus)

    # Serialize the original rows directly: no defaults, geometry operations,
    # fixture adapter, product imports or rewritten expected values are used.
    chunks, output_bytes = [], 0
    for case in cases:
        line = (json.dumps(case, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n").encode("utf-8")
        output_bytes += len(line)
        _require(output_bytes <= MAX_OUTPUT_BYTES, "JSONL output exceeds its declared bound")
        chunks.append(line)
    payload = b"".join(chunks)
    destination = output_path.resolve(strict=False)
    _require(destination != source, "Output cannot replace the literal input")
    _require(destination.parent.is_dir(), "Fresh output parent directory must already exist")
    # Exclusive creation refuses every existing file. A failed write remains
    # visible to the supervising runner; this exporter does not delete evidence.
    with destination.open("xb") as stream:
        written = stream.write(payload)
        _require(written == len(payload), "Incomplete JSONL write")
    return {
        "schema_version": 1,
        "kind": "owned_literal_transport_v1",
        "reference_execution": False,
        "case_count": len(cases),
        "input": {"path": str(source), "bytes": len(raw), "sha256": digest},
        "output": {"path": str(destination), "bytes": len(payload),
                   "sha256": hashlib.sha256(payload).hexdigest()},
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True, help="Exact reviewed literal corpus")
    parser.add_argument("--output", type=Path, required=True, help="Fresh ordered JSONL output")
    arguments = parser.parse_args()
    observed = export(arguments.input, arguments.output)
    print(json.dumps(observed, ensure_ascii=False, allow_nan=False, separators=(",", ":")))


if __name__ == "__main__":
    main()
