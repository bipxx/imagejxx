"""Fresh actual native API values for the finite P06i canonical recipe.

This module imports no product and runs no pytest tests. The installed parent
passes its already qualified imagejxx and NumPy modules to ``capture``. Fixture
construction and declared-byte serialization are the only NumPy work here;
orientation, calibration, records and preparation all come from the native API.
No expected catalogue or original test-local state is consumed.
"""

import hashlib
import json
import math
from pathlib import Path
import sys


_CAP = 536870912
_MAX_OUTPUT = 16 * 1024 * 1024
_CALIBRATION = {
    "spacing": [.4, 1.2, 3.],
    "origin": [11., -7., 19.],
    "direction": [1., .25, -.5, .5, -1., .125, 0., .75, 1.5],
    "unit": "um",
    "frame_interval": .75,
    "time_origin": 2.25,
    "time_unit": "min",
}


def _pin(path):
    path = Path(path).resolve(strict=True)
    if not path.is_file() or path.stat().st_size > 128 * 1024 * 1024:
        raise ValueError("Actual API observation pin is not a bounded file")
    with path.open("rb") as stream:
        raw = stream.read(128 * 1024 * 1024 + 1)
    if len(raw) > 128 * 1024 * 1024:
        raise ValueError("Actual API observation pin grew beyond its bound")
    return {"path": str(path), "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}


def _runtime(ij, np):
    native = sys.modules.get(ij.__name__ + "._native")
    if native is None:
        raise RuntimeError("The supplied installed imagejxx has no actual loaded native module")
    return {
        "python": {"executable": str(Path(sys.executable).resolve(strict=True)), "version": sys.version},
        "imagejxx": _pin(ij.__file__),
        "_native": _pin(native.__file__),
        "numpy": {"version": np.__version__, "module": _pin(np.__file__)},
        "consumer_source": _pin(__file__),
        "desktop_already_loaded": ij.__name__ + "._desktop" in sys.modules,
    }


def _calibration(value):
    return {name: getattr(value, name) for name in _CALIBRATION}


def _counts(observer):
    actual = observer.counts()
    return {"plane_reads": actual.plane_reads, "region_reads": actual.region_reads,
            "overflowed": actual.overflowed}


def _array_bytes(np, value, axes):
    # Endianness/order normalization only; values are the actual public export.
    contiguous = np.ascontiguousarray(value.astype(value.dtype.newbyteorder("<"), copy=False))
    raw = contiguous.tobytes(order="C")
    return {"axes": axes, "shape": list(value.shape), "dtype": value.dtype.name,
            "hex": raw.hex(), "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}


def _pixels(np, dataset):
    actual = dataset.numpy("TCZYXS")
    result = _array_bytes(np, actual, "TCZYXS")
    result["planes"] = [
        {"t": t, "c": c, "z": z,
         "declared_hex": _array_bytes(np, actual[t, c, z], "YXS")["hex"]}
        for t in range(actual.shape[0])
        for c in range(actual.shape[1])
        for z in range(actual.shape[2])
    ]
    return result


def _selection(value):
    if value is None:
        return None
    return {"kind": value.kind, "points": [list(point) for point in value.points],
            "width": value.width, "attributes": value.attributes}


def _history(owner):
    actual = owner.history()
    storage = actual.pixels
    return {
        "undo_steps": actual.undo_steps, "redo_steps": actual.redo_steps,
        "step_limit": actual.step_limit, "pixel_budget": actual.pixel_budget,
        "pixels": {name: getattr(storage, name) for name in (
            "logical_bytes", "current_bytes", "retained_bytes", "obsolete_bytes",
            "retained_planes", "exact")},
    }


def _snapshot(ij, np, owner, snapshot=None, *, pixels=False):
    actual = owner.snapshot() if snapshot is None else snapshot
    dataset, revisions, display = actual.dataset, actual.revisions, actual.display
    metadata = actual.metadata
    result = {
        "document_id": actual.id, "data_identity": actual.data_identity,
        "shape": list(dataset.shape), "dtype": dataset.dtype.name,
        "sample_policy": ("finite_only" if dataset.sample_policy == ij.FloatingSamplePolicy.FINITE_ONLY
                          else "preserve_quiet_nan_float32"),
        "calibration": _calibration(dataset.calibration), "dirty": actual.dirty,
        "revisions": {name: getattr(revisions, name) for name in ("pixels", "metadata", "selection", "display")},
        "history": _history(owner),
        "metadata": {"title": metadata["title"], "properties": metadata["properties"],
                     "extensions": metadata["extensions"], "record_layout": actual.record_layout},
        "display": {"t": display.t, "c": display.c, "crosshair": display.crosshair,
                    "orthogonal": display.orthogonal, "linked_zoom": display.linked_zoom,
                    "zoom": display.zoom, "plane_zoom": display.plane_zoom, "pan": display.pan,
                    "revision": display.revision, "properties": display.properties},
        "selection": _selection(actual.selection), "previous_selection": _selection(actual.previous_selection),
        "rois": [{"name": row.name, "selection": _selection(row.selection), "t": row.t,
                  "c": row.c, "z": row.z, "attributes": row.attributes} for row in actual.rois],
        "overlays": actual.overlays, "spectrum_present": actual.spectrum is not None,
        "pixels_observed": pixels,
    }
    if pixels:
        result["pixels"] = _pixels(np, dataset)
    return result


def _layout(value):
    return {"shape": list(value.shape), "source_axes": list(value.source_axes),
            "plane_name": value.plane_name, "working_bytes": value.working_bytes,
            "calibration": _calibration(value.calibration), "geometry": value.geometry}


def _request_value(value):
    nested = value.range
    return {"plane": nested.plane, "first": nested.first, "last": nested.last,
            "step": nested.step, "t": nested.t, "c": nested.c,
            "max_working_bytes": nested.max_working_bytes,
            "reverse_order": value.reverse_order, "flip_vertical": value.flip_vertical,
            "transpose": value.transpose}


def _request(ij, *, rebind=False):
    nested = ij.AxisRangeResliceRequest(
        "yz" if rebind else "xz", 1, 8 if rebind else 6, 2,
        t=0 if rebind else 1, c=1 if rebind else 2, max_working_bytes=_CAP)
    return ij.AxisRangeOrientationRequest(nested, reverse_order=not rebind,
                                          flip_vertical=True, transpose=not rebind)


def _owner(ij, np, observer=None):
    # Exactly the owned UInt16/S1 A02 fixture inputs, not a filter/orientation oracle.
    shape = (2, 3, 5, 7, 9, 1)
    t, c, z, y, x, s = np.indices(shape, dtype=np.int64)
    pixels = (40000 + (97*t + 31*c + 11*z + 5*y + 3*x + s) % 251).astype(np.uint16)
    dataset = ij.Dataset(pixels, axes="TCZYXS", calibration=ij.SpatialCalibration(**_CALIBRATION))
    if observer is not None:
        dataset = dataset.observe_reads(observer)
    owner = ij.Document(dataset, "Owned scientific orientation")
    edit = owner.begin_edit()
    edit.set_property("source_only", {"opaque": [None, True, 17]})
    edit.set_extension("future_annotation", {"version": 37, "opaque": [3, 1, 4]})
    edit.set_record_layout({"source_presence": True})
    edit.select(ij.Selection("rectangle", [(1, 1), (5, 4)]))
    edit.set_previous_selection(ij.Selection("line", [(0, 0), (2, 2)]))
    edit.set_rois([ij.ROI("positioned", ij.Selection("rectangle", [(1, 1), (3, 3)]), t=0, c=0, z=0),
                   ij.ROI("wildcard", ij.Selection("point", [(2, 2)]), z=1)])
    edit.set_overlays([{"name": "source overlay", "points": [[1, 1], [2, 2]]}])
    owner.commit(edit)
    owner.mark_saved()
    edit = owner.begin_edit()
    edit.set_property("history_probe", True)
    owner.commit(edit)
    owner.undo()
    display = owner.snapshot().display
    display.t, display.c, display.crosshair = 1, 2, [4, 3, 2]
    display.zoom, display.linked_zoom, display.orthogonal = 1.25, False, True
    display.plane_zoom, display.pan = [1.25, 2., 3.], [[3., -2.], [1., 4.], [-5., 2.]]
    owner.set_display(display)
    return owner


def _refusal(function):
    try:
        function()
    except Exception as error:
        result = {"python_type": type(error).__name__, "python_module": type(error).__module__,
                  "message": str(error)}
        if hasattr(error, "native_category"):
            result["native_category"] = error.native_category
        return result
    raise RuntimeError("The declared native capped preparation did not refuse")


def _json_safe(value, depth=0):
    if depth > 64:
        raise ValueError("Actual API observation JSON exceeds64levels")
    if value is None or type(value) in (bool, int, str):
        return value
    if type(value) is float:
        if not math.isfinite(value):
            raise ValueError("Nonfinite actual API observation")
        return value
    if type(value) in (list, tuple):
        return [_json_safe(item, depth + 1) for item in value]
    if type(value) is dict:
        if any(type(key) is not str for key in value):
            raise TypeError("Actual API observation object keys must be strings")
        return {key: _json_safe(item, depth + 1) for key, item in value.items()}
    raise TypeError("An actual API observation retained a non-JSON object")


def capture(imagejxx_module, numpy_module):
    """Return detached fresh actual values; perform no file or artifact publication.

    The parent owns installed-module admission, before/after source freezing and
    the JSON output destination. This function does not claim the 288 descriptor
    matrix, 72 prepared matrix or any values retained from the A02 pytest body.
    """
    ij, np = imagejxx_module, numpy_module
    runtime_before = _runtime(ij, np)
    cases = {}

    # Descriptor/record metadata admission has a fresh observer uncontaminated
    # by this consumer's later deliberate public byte exports.
    observer = ij.DatasetReadObserver()
    metadata_owner = _owner(ij, np, observer)
    metadata_source = metadata_owner.snapshot()
    request = _request(ij)
    reads_before = _counts(observer)
    raw = ij.describe_axis_range_orientation(list(metadata_source.dataset.shape), "uint16",
                                             metadata_source.dataset.calibration, request)
    delegated = ij.describe_axis_range_orientation(metadata_source.dataset, request)
    command = ij.resolve_axis_orientation_command(metadata_source, request)
    ij.validate_axis_orientation_command_record(command.record())
    replayed = ij.axis_orientation_command_from_record(metadata_source, command.record())
    cases["S01"] = {
        "metadata_source": _snapshot(ij, np, metadata_owner, metadata_source),
        "request": _request_value(command.request), "operation": command.operation,
        "record": command.record(), "replayed_record": replayed.record(),
        "layout": _layout(command.layout), "raw_layout": _layout(raw), "dataset_layout": _layout(delegated),
        "provider_counts_before": reads_before, "provider_counts_after": _counts(observer),
    }
    positive_before = _counts(observer)
    plane = metadata_source.dataset.plane("xy", 0, t=0, c=0)
    plane_after = _counts(observer)
    region = metadata_source.dataset.region(1, 1, 2, 2, t=0, c=0, z=0)
    cases["S01_provider_positive_control"] = {
        "source_document_id": metadata_source.id,
        "scope": "Actual public plane and region reads after metadata-only observation",
        "before": positive_before, "after_plane": plane_after, "after_region": _counts(observer),
        "plane": _array_bytes(np, plane, "YX" if plane.ndim == 2 else "YXS"),
        "region": _array_bytes(np, region, "YX" if region.ndim == 2 else "YXS"),
    }

    owner = _owner(ij, np)
    before = owner.snapshot()
    source_before = _snapshot(ij, np, owner, before, pixels=True)
    numerical = ij.resolve_axis_orientation_command(before, _request(ij))
    prepared = ij.prepare_axis_orientation(numerical)
    output = prepared.prepared_new_document()
    output_snapshot = output.snapshot()
    cases["S02"] = {
        "source_before": source_before, "source_after": _snapshot(ij, np, owner, pixels=True),
        "source_shares_data_before_after": owner.snapshot().shares_data(before),
        "output_shares_pixels_with_source": output_snapshot.dataset.shares_pixels(before.dataset),
        "record": numerical.record(), "layout": _layout(numerical.layout),
        "output": _snapshot(ij, np, output, output_snapshot, pixels=True),
        "prepared_active": prepared.active,
        "scope": "Fresh prepared unregistered output; no Application publication or UI execution",
    }

    cap_observer = ij.DatasetReadObserver()
    cap_owner = _owner(ij, np, cap_observer)
    capped = ij.resolve_axis_orientation_command(cap_owner.snapshot(), _request(ij))
    cap_record = capped.record()
    cap_source = _snapshot(ij, np, cap_owner)
    cap_counts = _counts(cap_observer)
    callbacks = []

    def checkpoint():
        callbacks.append({"kind": "checkpoint"})

    def progress(completed, total):
        callbacks.append({"kind": "progress", "completed": completed, "total": total})

    execution_cap = capped.layout.working_bytes - 1
    error = _refusal(lambda: ij.prepare_axis_orientation(
        capped, execution_max_working_bytes=execution_cap, checkpoint=checkpoint, progress=progress))
    cases["S03_cap"] = {
        "source_before": cap_source, "source_after": _snapshot(ij, np, cap_owner),
        "execution_cap": execution_cap, "error": error,
        "record_before": cap_record, "record_after": capped.record(),
        "callback_count": len(callbacks), "callbacks": callbacks,
        "provider_counts_before": cap_counts, "provider_counts_after": _counts(cap_observer),
        "scope": "Actual native prepare call with an execution cap below its admitted descriptor",
    }

    replacement = _owner(ij, np)
    replacement_before = _snapshot(ij, np, replacement)
    complete = _request(ij, rebind=True)
    rebound = ij.rebind_axis_orientation_command(replacement.snapshot(), cap_record, complete)
    cases["S03_rebind"] = {
        "original_before": cap_record, "original_after": capped.record(),
        "new_record": rebound.record(), "request": _request_value(rebound.request),
        "layout": _layout(rebound.layout),
        "source_before": replacement_before, "source_after": _snapshot(ij, np, replacement),
        "scope": "Explicit complete request and fresh owner; original wire record retained unchanged",
    }

    runtime_after = _runtime(ij, np)
    if runtime_after != runtime_before:
        raise RuntimeError("Actual installed modules or consumer source changed during capture")
    envelope = {
        "schema_version": 1, "schema": "imagejxx.scientific-orientation-python-api-observations",
        "producer_id": "p06i.scientific-orientation-python-api.raw.001",
        "observation_state": "observed", "acceptance_claim": False,
        "consumer": "A02 canonical recipe newly observed; not retained original test locals",
        "runtime_before": runtime_before, "runtime_after": runtime_after, "cases": cases,
    }
    # Strict JSON round-trip both detaches all public DTOs and rejects NaN,
    # infinity, non-string object keys and accidental native/NumPy owners.
    encoded = json.dumps(_json_safe(envelope), ensure_ascii=False, allow_nan=False, separators=(",", ":"))
    if len(encoded.encode("utf-8")) > _MAX_OUTPUT:
        raise ValueError("Scientific orientation API observations exceed16MiB")
    return json.loads(encoded)
