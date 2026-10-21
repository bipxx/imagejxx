"""Old rank defaults, aliases and portable records against owned literal pixels.

These two cases use the existing generic rank API, including its Float64
support. They do not use the ImageJ rank profile or another filter to compute
expected pixels. Execution and installed-package qualification are separate.
"""

from copy import deepcopy
import inspect
import json

import imagejxx as ij
import numpy as np

from imagejxx.ui import workflows
from imagejxx.ui.model import Document


# Radius-one disk neighborhoods contain five cells. An 80-valued cross has
# five high values only at its center; each arm has two, below the median.
CROSS = (
    (0, 0, 0, 0, 0),
    (0, 0, 80, 0, 0),
    (0, 80, 80, 80, 0),
    (0, 0, 80, 0, 0),
    (0, 0, 0, 0, 0),
)
MEDIAN = (
    (0, 0, 0, 0, 0),
    (0, 0, 0, 0, 0),
    (0, 0, 80, 0, 0),
    (0, 0, 0, 0, 0),
    (0, 0, 0, 0, 0),
)
MINIMUM = (
    (0, 0, 0, 0, 0),
    (0, 0, 0, 0, 0),
    (0, 0, 80, 0, 0),
    (0, 0, 0, 0, 0),
    (0, 0, 0, 0, 0),
)
MAXIMUM = (
    (0, 0, 80, 0, 0),
    (0, 80, 80, 80, 0),
    (80, 80, 80, 80, 80),
    (0, 80, 80, 80, 0),
    (0, 0, 80, 0, 0),
)
MEAN = (
    (0, 0, 16, 0, 0),
    (0, 32, 32, 32, 0),
    (16, 32, 80, 32, 16),
    (0, 32, 32, 32, 0),
    (0, 0, 16, 0, 0),
)
VARIANCE = (
    (0, 0, 1024, 0, 0),
    (0, 1536, 1536, 1536, 0),
    (1024, 1536, 0, 1536, 1024),
    (0, 1536, 1536, 1536, 0),
    (0, 0, 1024, 0, 0),
)


def _state(document):
    return {
        "shape": document.data.shape,
        "dtype": document.data.dtype,
        "pixels": document.data.tobytes(),
        "metadata": deepcopy(document.metadata()),
        "revision": document.revision,
        "dirty": document.dirty,
        "can_undo": document.can_undo,
        "can_redo": document.can_redo,
    }


def test_rlc01_old_rank_defaults_and_aliases_match_owned_literals():
    signature = inspect.signature(ij.rank_filter)
    defaults = {name: signature.parameters[name].default
                for name in ("radius", "method", "footprint", "border", "cval")}
    assert defaults == {
        "radius": 1, "method": "median", "footprint": "disk",
        "border": "reflect", "cval": 0,
    }
    for alias in (ij.median_filter, ij.minimum_filter, ij.maximum_filter,
                  ij.variance_filter):
        assert inspect.signature(alias).parameters["radius"].default == 1

    calls = (
        (ij.rank_filter, MEDIAN, None),
        (ij.median_filter, MEDIAN, None),
        (ij.minimum_filter, MINIMUM, None),
        (ij.maximum_filter, MAXIMUM, None),
        (ij.variance_filter, VARIANCE, np.float64),
    )
    for dtype in (np.uint8, np.uint16, np.float32, np.float64):
        source = np.array(CROSS, dtype=dtype)
        retained = source.tobytes()
        assert ij.operation_defaults("rank_filter", source) == {
            "radius": 1, "method": "median", "footprint": "disk",
            "border": "reflect", "cval": 0,
        }
        for call, literal, output_dtype in calls:
            result = call(source)
            expected = np.array(literal, dtype=dtype if output_dtype is None else output_dtype)
            assert result.shape == expected.shape and result.dtype == expected.dtype
            assert result.tobytes() == expected.tobytes()
            assert not np.shares_memory(result, source)
            assert source.tobytes() == retained
        result = ij.rank_filter(source, method="mean")
        expected = np.array(MEAN, dtype=dtype)
        assert result.shape == expected.shape and result.dtype == expected.dtype
        assert result.tobytes() == expected.tobytes()
        assert not np.shares_memory(result, source)
        assert source.tobytes() == retained


def test_rlc02_legacy_rank_record_roundtrip_and_run_preserve_source():
    document = Document(np.array(CROSS, dtype=np.uint16), "Legacy rank")
    before = _state(document)
    records = (
        ({"operation": "rank_filter", "parameters": {}}, MEDIAN),
        ({"operation": "rank_filter", "parameters": {
            "radius": 1, "method": "maximum", "footprint": "disk",
        }}, MAXIMUM),
    )
    for record, literal in records:
        steps = [deepcopy(record)]
        frozen = deepcopy(steps)
        text = workflows.dumps(steps)
        assert json.loads(text) == {
            "format": "imagejxx-workflow", "version": 1, "steps": frozen,
        }
        loaded = workflows.loads(text)
        assert loaded == frozen and "schema_version" not in loaded[0]
        progress = []
        # Untyped portable records need no source-bound rank rebind request.
        result = workflows.run(document, loaded,
                               progress=lambda completed, total: progress.append((completed, total)))
        expected = np.array(literal, dtype=np.uint16)
        assert result.shape == expected.shape and result.dtype == expected.dtype
        assert result.tobytes() == expected.tobytes()
        assert not np.shares_memory(result, document.data)
        assert progress == [(1, 1)]
        assert loaded == frozen and steps == frozen
        assert _state(document) == before
        loaded[0]["parameters"]["radius"] = 2
        assert steps == frozen


EXPECTED_COLLECTION_IDS = (
    "test_rlc01_old_rank_defaults_and_aliases_match_owned_literals",
    "test_rlc02_legacy_rank_record_roundtrip_and_run_preserve_source",
)
EXPECTED_COLLECTION_COUNT = 2
