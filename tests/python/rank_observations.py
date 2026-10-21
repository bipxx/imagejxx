"""Optional raw Python rank observations; no expected pixels or acceptance flags.

Only tests that finish their real assertions call finish(). Stored observations
are detached JSON values/ndarray bytes, never native snapshots or job owners.
The environment-dependent qualification reader is outside default testpaths.
"""

import hashlib
import json
import math
import os
from pathlib import Path
import sys

import numpy as np


ENVIRONMENT = "IMAGEJXX_RANK_SURFACE_OBSERVATIONS_DIR"
PRODUCER_ID = "p06f.rank.python.raw.001"
MAX_BYTES = 16 * 1024 * 1024
USAGE_FIELDS = ("running", "queued", "running_bytes", "retained_bytes", "reserved_bytes")


def _canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=False, allow_nan=False).encode("utf-8")


def case_filename(case_id):
    return hashlib.sha256(case_id.encode("utf-8")).hexdigest() + ".json"


def calibration_value(calibration):
    return {name: getattr(calibration, name) for name in
            ("spacing", "origin", "direction", "unit", "frame_interval", "time_origin", "time_unit")}


def display_value(display):
    return {name: getattr(display, name) for name in
            ("t", "c", "crosshair", "orthogonal", "linked_zoom", "zoom", "plane_zoom", "pan", "revision", "properties")}


def usage_value(scheduler):
    usage = scheduler.usage
    return {name: getattr(usage, name) for name in USAGE_FIELDS}


class RawCase:
    def __init__(self, case_id):
        self.case_id = case_id
        self.enabled = bool(os.environ.get(ENVIRONMENT))
        self._observations = []
        self._ids = set()
        self._finished = False

    def _add(self, name, encoding, value):
        if not self.enabled:
            return
        if self._finished or not isinstance(name, str) or not name or name in self._ids:
            raise ValueError("Observation IDs must be unique and precede finish")
        # The roundtrip keeps only owned finite JSON, with typed scalars intact.
        detached = _canonical(value)
        if len(detached) > MAX_BYTES:
            raise ValueError("A raw rank observation exceeds16MiB")
        self._observations.append({"id": name, "encoding": encoding,
                                   "value": json.loads(detached)})
        self._ids.add(name)

    def scalar(self, name, value):
        if not self.enabled:
            return
        if type(value) not in (type(None), bool, int, float, str):
            raise TypeError("Raw scalar requires an actual typed JSON scalar")
        if type(value) is float and not math.isfinite(value):
            raise ValueError("Raw scalar must be finite")
        self._add(name, "scalar", value)

    def json(self, name, value):
        self._add(name, "canonical_json", value)

    def array(self, name, array):
        if not self.enabled:
            return
        if not isinstance(array, np.ndarray):
            raise TypeError("bytes_le requires an actual public ndarray")
        pixel_type = array.dtype.name
        # Float64 may describe an explicitly refused source; it never implies
        # that this rank profile admits Float64 processing.
        if pixel_type not in {"uint8", "uint16", "float32", "float64"}:
            raise ValueError("Unexpected ndarray storage in raw rank observation")
        if array.nbytes * 2 > MAX_BYTES:
            raise ValueError("Raw rank ndarray exceeds observation bound")
        little = np.ascontiguousarray(array, dtype=array.dtype.newbyteorder("<"))
        self._add(name, "bytes_le", {"pixel_type": pixel_type,
                                    "shape": list(array.shape),
                                    "hex": little.tobytes(order="C").hex()})

    def owner(self, prefix, owner):
        if not self.enabled:
            return
        snapshot, history = owner.snapshot(), owner.history()
        dataset, display = snapshot.dataset, snapshot.display
        self.array(prefix + ".dataset", dataset.numpy("TCZYXS"))
        self.json(prefix + ".state", {
            "document_id": snapshot.id, "data_identity": snapshot.data_identity,
            "shape": list(dataset.shape), "dirty": snapshot.dirty,
            "revisions": {name: getattr(snapshot.revisions, name)
                          for name in ("pixels", "metadata", "selection", "display")},
            "history": {name: getattr(history, name)
                        for name in ("undo_steps", "redo_steps", "step_limit", "pixel_budget")},
            "metadata": snapshot.metadata, "record_layout": snapshot.record_layout,
            "calibration": calibration_value(dataset.calibration),
            "display": display_value(display), "overlays": snapshot.overlays,
            "spectrum_present": snapshot.spectrum is not None,
        })
        for name in ("selection", "previous_selection"):
            selection = getattr(snapshot, name)
            self.json(prefix + "." + name, None if selection is None else {
                "kind": selection.kind, "points": selection.points,
                "width": selection.width, "attributes": selection.attributes})
            if selection is not None:
                self.array(prefix + "." + name + ".mask", selection.mask(tuple(dataset.shape)[3:5]))
        self.json(prefix + ".rois", [{"name": roi.name, "t": roi.t, "c": roi.c, "z": roi.z,
                                      "attributes": roi.attributes, "selection": {
                                          "kind": roi.selection.kind, "points": roi.selection.points,
                                          "width": roi.selection.width, "attributes": roi.selection.attributes}}
                                     for roi in snapshot.rois])
        for index, roi in enumerate(snapshot.rois):
            # Public mask() returns owned actual cells. Pack those observed
            # cells only for transport; this is no expected geometry/filter.
            mask = roi.selection.mask(tuple(dataset.shape)[3:5])
            key = prefix + ".rois." + str(index) + ".selection"
            self.array(key + ".mask", mask)
            self.json(key + ".packed_mask", {
                "shape": list(mask.shape), "packing": "row-major-msb0",
                "hex": np.packbits(mask.reshape(-1) != 0, bitorder="big").tobytes().hex()})

    def editor(self, prefix, editor):
        if not self.enabled:
            return
        self.json(prefix + ".editor", {
            "document_infos": editor.document_infos, "active_index": editor.active_index,
            "workflow": editor.workflow, "results": editor.results,
            "busy": editor.busy, "recording": editor.recording, "error": editor.error,
            "jobs": [{name: (str(getattr(job, name)).rsplit(".", 1)[-1].lower()
                             if name == "state" else getattr(job, name))
                      for name in ("id", "document_id", "state", "error", "completed", "total",
                                   "resource_bytes", "cancel_requested")}
                     for job in editor.jobs],
        })

    def finish(self):
        if not self.enabled:
            return
        if self._finished or not self._observations:
            raise ValueError("Finish requires one nonempty, unfinished raw case")
        origins = {name: str(Path(module.__file__).resolve())
                   for name in ("imagejxx", "imagejxx._native", "imagejxx._desktop")
                   if (module := sys.modules.get(name)) is not None and getattr(module, "__file__", None)}
        self.json("python.origins", origins)
        payload = _canonical({"schema_version": 1, "producer_id": PRODUCER_ID,
                              "complete": True,
                              "cases": [{"id": self.case_id,
                                         "observations": self._observations}]}) + b"\n"
        if len(payload) > MAX_BYTES:
            raise ValueError("Raw rank case exceeds16MiB")
        root = Path(os.environ[ENVIRONMENT])
        root.mkdir(exist_ok=True)
        if root.is_symlink() or not root.is_dir():
            raise ValueError("Raw rank output must be an owned real directory")
        root = root.resolve(strict=True)
        path = root / case_filename(self.case_id)
        with path.open("xb") as stream:
            stream.write(payload)
        self._finished = True
