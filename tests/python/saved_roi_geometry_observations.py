"""Optional detached actual-value transport for saved-ROI API assertions.

Call finish only after the real case passes. This module contains no expected
values, product imports, geometry or native result owners.
"""

import hashlib
import json
import math
import os
from pathlib import Path


ENVIRONMENT = "IMAGEJXX_SAVED_ROI_SURFACE_OBSERVATIONS_DIR"
PRODUCER_ID = "p06g.saved-roi.python.raw.001"
MAX_BYTES = 16 * 1024 * 1024


def _canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=False, allow_nan=False).encode("utf-8")


class RawCase:
    def __init__(self, case_id):
        self.case_id = case_id
        self._configured = os.environ.get(ENVIRONMENT)
        self._observations = []
        self._ids = set()
        self._finished = False

    def _add(self, name, encoding, value):
        if self._configured is None:
            return
        if self._finished or type(name) is not str or not name or name in self._ids:
            raise ValueError("Raw observation IDs must be unique and precede finish")
        payload = _canonical(value)
        if len(payload) > MAX_BYTES:
            raise ValueError("Raw observation exceeds16MiB")
        self._observations.append({"id": name, "encoding": encoding,
                                   "value": json.loads(payload)})
        self._ids.add(name)

    def scalar(self, name, value):
        if type(value) not in (type(None), bool, int, float, str):
            raise TypeError("Raw scalar requires an actual typed JSON scalar")
        if type(value) is float and not math.isfinite(value):
            raise ValueError("Raw scalar must be finite")
        self._add(name, "scalar", value)

    def json(self, name, value):
        self._add(name, "canonical_json", value)

    def finish(self):
        if self._finished:
            raise ValueError("Raw case is already finished")
        self._finished = True
        if self._configured is None:
            return
        directory = Path(self._configured)
        if not self._configured or not directory.is_absolute() or not directory.is_dir():
            raise ValueError("Raw observations require an explicit existing absolute directory")
        directory = directory.resolve(strict=True)
        payload = _canonical({"schema_version": 1, "producer_id": PRODUCER_ID,
                              "complete": True, "cases": [{"id": self.case_id,
                                  "observations": self._observations}]}) + b"\n"
        if len(payload) > MAX_BYTES:
            raise ValueError("Raw case exceeds16MiB")
        name = hashlib.sha256(self.case_id.encode("utf-8")).hexdigest() + ".json"
        with (directory / name).open("xb") as stream:
            if stream.write(payload) != len(payload):
                raise OSError("Incomplete raw observation write")
