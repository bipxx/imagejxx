"""UI-independent documents, selection geometry, calibrated measurements and history."""

from __future__ import annotations

import base64
import json
import math
import struct
from copy import deepcopy
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Callable

import numpy as np
import imagejxx as ij


@dataclass
class Calibration:
    pixel_width: float = 1
    pixel_height: float = 1
    pixel_depth: float = 1
    unit: str = "px"
    frame_interval: float = 1
    time_unit: str = "s"
    origin: tuple[float, float, float] = (0, 0, 0)
    direction: tuple[float, ...] = (1, 0, 0, 0, 1, 0, 0, 0, 1)
    time_origin: float = 0

    def validate(self):
        values = (
            self.pixel_width,
            self.pixel_height,
            self.pixel_depth,
            self.frame_interval,
        )
        if not np.isfinite(values).all() or min(values) <= 0:
            raise ValueError(
                "Pixel sizes and frame interval must be finite and positive"
            )
        if not self.unit.strip() or not self.time_unit.strip():
            raise ValueError("Calibration units cannot be empty")
        native = ij.SpatialCalibration(
            spacing=(self.pixel_width, self.pixel_height, self.pixel_depth),
            origin=self.origin,
            direction=self.direction,
            unit=self.unit,
            frame_interval=self.frame_interval,
            time_origin=self.time_origin,
            time_unit=self.time_unit,
        )
        self.origin = tuple(native.origin)
        self.direction = tuple(native.direction)
        return self


@dataclass
class Selection:
    kind: str
    points: list[tuple[float, float]] = field(default_factory=list)
    width: float = 1
    bitmap: np.ndarray | None = None
    attributes: dict = field(default_factory=dict)
    origin: tuple[int, int] = (0, 0)

    def clone(self):
        return deepcopy(self)

    def native(self):
        """Adapt geometry and attributes without reimplementing native ROI styles."""
        if self.kind in ("mask", "region"):
            if self.kind == "region" and self.points:
                raise ValueError("Packed region must not contain vector points")
            if self.bitmap is None:
                raise ValueError("Selection mask is missing")
            bitmap = np.asarray(self.bitmap)
            if bitmap.dtype.kind == "b":
                bitmap = bitmap.astype(np.uint8)
            elif bitmap.dtype.kind in "iuf" and bitmap.dtype not in (
                np.dtype(np.uint8), np.dtype(np.uint16),
                np.dtype(np.float32), np.dtype(np.float64),
            ):
                # Convert storage only; the C++ mask constructor decides
                # membership and validates finite samples. Real integer and
                # float16 values retain their zero/nonzero meaning in float64.
                bitmap = bitmap.astype(np.float64)
            native = ij.Selection.from_mask(bitmap)
            h, w = self.bitmap.shape
            styled = ij.SelectionEditor(native, (h, w)).properties(
                self.width, self.attributes
            )
            return ij.SelectionRegion(styled, (h, w), self.origin).as_selection() if self.kind == "region" else styled
        return ij.Selection(
            self.kind, self.points, self.width, attributes=self.attributes
        )

    def mask(self, shape):
        height, width = shape[:2]
        return self.native().mask((height, width))

    def bounds(self, shape):
        if self.kind == "region":
            bounds = ij.SelectionRegion(self.native(), shape[:2]).bounds
            if bounds is None:
                raise ValueError("Selection contains no pixels")
            return (int(bounds[0]), int(bounds[1]), int(bounds[2]-bounds[0]), int(bounds[3]-bounds[1]))
        yy, xx = np.nonzero(self.mask(shape))
        if not len(xx):
            raise ValueError("Selection contains no pixels")
        return (
            int(xx.min()),
            int(yy.min()),
            int(xx.max() - xx.min() + 1),
            int(yy.max() - yy.min() + 1),
        )

    def moved(self, dx, dy, shape):
        selection = self.clone()
        if self.kind == "region":
            moved = ij.SelectionEditor(self.native(), shape[:2]).moved((dx, dy))
            return Selection.from_dict(moved.to_dict())
        if self.kind == "mask":
            selection.bitmap = ij.translate(
                self.mask(shape), dx, dy, interpolation="nearest"
            )
        else:
            selection.points = [(x + dx, y + dy) for x, y in self.points]
        selection.mask(shape)
        return selection

    def to_dict(self):
        if self.kind == "region":
            return self.native().to_dict()
        reserved = {"kind", "points", "width"}
        if self.bitmap is not None or self.kind == "mask":
            reserved |= {"shape", "mask"}
        if reserved & self.attributes.keys():
            raise ValueError("Selection attributes contain geometry/storage fields")
        result = {
            **deepcopy(self.attributes),
            "kind": self.kind,
            "points": deepcopy(self.points),
            "width": self.width,
        }
        if self.bitmap is not None:
            result["shape"] = list(self.bitmap.shape)
            result["mask"] = base64.b64encode(np.packbits(self.bitmap != 0)).decode(
                "ascii"
            )
            stored = getattr(self, "_session_mask_storage", None)
            if stored is not None and (
                result["shape"] == stored["canonical_shape"]
                and result["mask"] == stored["canonical_mask"]
            ):
                # Native packed masks retain unused final-byte bits. Bitmap
                # geometry and annotation edits must not rewrite those bits.
                result["shape"] = deepcopy(stored["shape"])
                result["mask"] = stored["mask"]
        return result

    @classmethod
    def _from_session_dict(cls, value):
        if value["kind"] == "region":
            return cls.from_dict(value)
        if value["kind"] != "mask":
            # Native non-mask shape/mask members are opaque attributes. The
            # legacy attached-bitmap heuristic must not change their geometry.
            return cls(
                value["kind"], [tuple(point) for point in value["points"]], value["width"],
                attributes={key: deepcopy(item) for key, item in value.items()
                            if key not in {"kind", "points", "width"}},
            )
        result = cls.from_dict(value)
        if result.bitmap is not None:
            result._session_mask_storage = {
                "shape": deepcopy(value["shape"]),
                "mask": value["mask"],
                "canonical_shape": list(result.bitmap.shape),
                "canonical_mask": base64.b64encode(
                    np.packbits(result.bitmap != 0)
                ).decode("ascii"),
            }
        return result

    @classmethod
    def from_dict(cls, value):
        if value["kind"] == "region":
            canonical = ij.Selection.from_dict(value).to_dict()
            shape = tuple(canonical["shape"])
            bits = np.unpackbits(np.frombuffer(base64.b64decode(canonical["mask"]), np.uint8))
            return cls("region", [], canonical["width"],
                       bits[:shape[0]*shape[1]].reshape(shape).astype(np.uint8)*255,
                       deepcopy(canonical["attributes"]), tuple(canonical["origin"]))
        dimensions = value.get("shape")
        # Old Python records could attach a packed bitmap to another kind.
        # Retain that encoding without treating arbitrary metadata named
        # "mask" as a bitmap (the native model reserves it only for masks).
        has_storage = value["kind"] == "mask" or (
            isinstance(value.get("mask"), str)
            and isinstance(dimensions, (list, tuple))
            and len(dimensions) == 2
        )
        reserved = {"kind", "points", "width"}
        if has_storage:
            reserved |= {"shape", "mask"}
        selection = cls(
            value["kind"],
            [tuple(p) for p in value.get("points", [])],
            value.get("width", 1),
            attributes={
                key: deepcopy(item)
                for key, item in value.items()
                if key not in reserved
            },
        )
        if has_storage and "mask" in value:
            shape = tuple(int(n) for n in value["shape"])
            if len(shape) != 2 or min(shape) <= 0:
                raise ValueError("Invalid saved ROI dimensions")
            bits = np.unpackbits(
                np.frombuffer(base64.b64decode(value["mask"], validate=True), np.uint8)
            )
            if bits.size < shape[0] * shape[1]:
                raise ValueError("Truncated saved ROI mask")
            selection.bitmap = (
                bits[: shape[0] * shape[1]].reshape(shape).astype(np.uint8) * 255
            )
        return selection


@dataclass
class NamedROI:
    name: str
    selection: Selection
    slice_index: int | None = None
    attributes: dict = field(default_factory=dict)

    def to_dict(self):
        reserved = {"name", "selection", "slice_index"}
        if reserved & self.attributes.keys():
            raise ValueError("ROI attributes contain reserved fields")
        result = {
            **deepcopy(self.attributes),
            "name": self.name,
            "selection": self.selection.to_dict(),
            "slice_index": self.slice_index,
        }
        if getattr(self, "_session_slice_present", True) is False and self.slice_index is None:
            result.pop("slice_index")
        return result

    @classmethod
    def from_dict(cls, value):
        reserved = {"name", "selection", "slice_index"}
        return cls(
            str(value["name"]),
            Selection.from_dict(value["selection"]),
            value.get("slice_index"),
            attributes={
                key: deepcopy(item)
                for key, item in value.items()
                if key not in reserved
            },
        )


@dataclass
class Overlay:
    kind: str
    points: list[tuple[float, float]] = field(default_factory=list)
    color: str = "#ffcc00"
    width: float = 1
    text: str = ""
    font_size: int = 16
    slice_index: int | None = None
    font_path: str | None = None
    attributes: dict = field(default_factory=dict)

    def to_dict(self):
        result = asdict(self)
        result.pop("attributes")
        template = getattr(self, "_session_record", None)
        if template is not None:
            # Native overlays are opaque records. Keep omitted and null known
            # values until that effective editable field actually changes.
            raw = deepcopy(template)
            baseline = self._session_baseline
            for key, value in result.items():
                if not _session_equal(_session_plain(value), baseline[key]):
                    raw[key] = _session_plain(value)
            for key in tuple(raw):
                if key not in result:
                    del raw[key]
            if self.attributes.keys() & result.keys():
                raise ValueError("Overlay attributes contain reserved fields")
            raw.update(_session_plain(self.attributes))
            return raw
        if self.attributes.keys() & result.keys():
            raise ValueError("Overlay attributes contain reserved fields")
        return {**deepcopy(self.attributes), **result}

    @classmethod
    def from_dict(cls, value):
        fields = {
            "kind", "points", "color", "width", "text", "font_size",
            "slice_index", "font_path",
        }
        return cls(
            **{key: deepcopy(item) for key, item in value.items() if key in fields},
            attributes={
                key: deepcopy(item) for key, item in value.items() if key not in fields
            },
        )

    @classmethod
    def _from_session_dict(cls, value):
        defaults = asdict(cls(""))
        defaults.pop("attributes")
        known = {
            key: deepcopy(value[key])
            if key in value and value[key] is not None else deepcopy(default)
            for key, default in defaults.items()
        }
        result = cls(
            **known,
            attributes={key: deepcopy(item) for key, item in value.items()
                        if key not in defaults},
        )
        result._session_record = deepcopy(value)
        result._session_baseline = {
            key: _session_plain(item) for key, item in known.items()
        }
        return result

    def validate_style(self):
        if "imagejxx.roi_style" in self.attributes:
            ij.roi_style(Selection.from_dict(self.to_dict()).native())
        geometry = self.selection_record()
        if "imagejxx.roi_style" in geometry:
            ij.roi_style(Selection.from_dict(geometry).native())

    def selection_record(self):
        if self.kind == "roi":
            selection = self.attributes.get("selection")
            if not isinstance(selection, dict):
                raise ValueError("ROI overlay selection must be an object")
            return deepcopy(selection)
        return self.to_dict()


def gray(pixels):
    return ij.grayscale(pixels) if pixels.ndim == 3 else pixels


_QUIET_NAN_POLICY_TOKEN = "preserve_quiet_nan_float32"
_IEEE_POLICY_TOKEN = "preserve_ieee_float32"


def _checked_sample_policy(policy):
    if not isinstance(policy, ij.FloatingSamplePolicy) or policy not in (
        ij.FloatingSamplePolicy.FINITE_ONLY,
        ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32,
        ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32,
    ):
        raise ValueError("Invalid floating sample policy")
    return policy


def _sample_policy_from_metadata(value):
    if "pixel_sample_policy" not in value:
        return ij.FloatingSamplePolicy.FINITE_ONLY
    token = value["pixel_sample_policy"]
    if type(token) is not str:
        raise ValueError("Invalid session pixel sample policy")
    if token == _QUIET_NAN_POLICY_TOKEN:return ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32
    if token == _IEEE_POLICY_TOKEN:return ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32
    raise ValueError("Invalid session pixel sample policy")


def checked_pixels(
    pixels, is_stack=False, *, sample_policy=ij.FloatingSamplePolicy.FINITE_ONLY
):
    policy = _checked_sample_policy(sample_policy)
    array = np.asarray(pixels)
    if policy in (ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32, ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32):
        dimensions = 3 if is_stack else 2
        if (
            array.dtype.kind != "f" or array.itemsize != 4
            or array.ndim != dimensions or any(n <= 0 for n in array.shape)
        ):
            raise ValueError("Quiet-NaN source policy requires grayscale Float32")
        if is_stack:
            return np.stack([
                ij.canonical_float32_source(frame, sample_policy=policy)
                for frame in array
            ])
        return ij.canonical_float32_source(array, sample_policy=policy)
    if array.dtype.kind == "b":
        array = array.astype(np.uint8) * 255
    if array.dtype.kind == "i":
        array = array.astype(np.float64)
    if is_stack:
        if array.ndim not in (3, 4) or not len(array):
            raise ValueError("Stacks must be nonempty (Z,H,W) or (Z,H,W,3/4) arrays")
        return np.stack([ij.convert(frame, frame.dtype) for frame in array])
    return ij.convert(array, array.dtype)


_SESSION_DISPLAY_FIELDS = ("display_range", "lut", "custom_lut")
_SESSION_TRANSPORT_FIELDS = frozenset({
    "pixels", "dtype", "shape", "spectrum", "fft_shape", "spectrum_shape",
    "pixel_sample_policy",
})
_SESSION_CALIBRATION_FIELDS = frozenset(Calibration.__dataclass_fields__)


def _session_plain(value, _active=None):
    """Own an exact JSON tree; convert only explicit UI sequence containers."""
    active = set() if _active is None else _active
    container = isinstance(value, np.ndarray) or type(value) in (list, tuple, dict)
    if container:
        identity = id(value)
        if identity in active:
            raise ValueError("Session values cannot contain reference cycles")
        active.add(identity)
        try:
            if isinstance(value, np.ndarray):
                return _session_plain(value.tolist(), active)
            if type(value) in (list, tuple):
                return [_session_plain(item, active) for item in value]
            if any(type(key) is not str for key in value):
                raise ValueError("Session object keys must be strings")
            return {key: _session_plain(item, active) for key, item in value.items()}
        finally:
            active.remove(identity)
    if value is None or type(value) in (bool, str):
        return value
    if type(value) is int:
        if not -(2**63) <= value <= 2**64 - 1:
            raise ValueError("Session integer is outside the native domain")
        return value
    if type(value) is float and math.isfinite(value):
        return value
    raise ValueError("Session values must contain finite native JSON types")


def _session_exact_json(value, _active=None):
    """Incoming marked protocol trees require actual JSON container types."""
    active = set() if _active is None else _active
    if type(value) in (dict, list):
        identity = id(value)
        if identity in active:
            raise ValueError("Session values cannot contain reference cycles")
        active.add(identity)
        try:
            if type(value) is dict:
                if any(type(key) is not str for key in value):
                    raise ValueError("Session object keys must be strings")
                return {key: _session_exact_json(item, active)
                        for key, item in value.items()}
            return [_session_exact_json(item, active) for item in value]
        finally:
            active.remove(identity)
    if value is None or type(value) in (bool, str, int, float):
        return _session_plain(value)
    raise ValueError("Marked protocol values must use exact JSON types")


def _session_equal(left, right):
    """Compare JSON types and double bits, including the sign of zero."""
    if type(left) is not type(right):
        return False
    if type(left) is dict:
        return left.keys() == right.keys() and all(
            _session_equal(value, right[key]) for key, value in left.items()
        )
    if type(left) is list:
        return len(left) == len(right) and all(
            _session_equal(a, b) for a, b in zip(left, right)
        )
    if type(left) is float:
        return struct.pack(">d", left) == struct.pack(">d", right)
    return left == right


def _session_json(value):
    return json.dumps(_session_plain(value), ensure_ascii=True, allow_nan=False,
                      separators=(",", ":"))


def _session_legacy_state(value, plane_count):
    # Classification, default merging and record groups remain owned by C++.
    return json.loads(ij._native._legacy_session_state_json(
        _session_json(value), plane_count,
    ))


def _session_project(metadata, wire, shape, planes):
    return json.loads(ij._native._session_wire_metadata_json(
        _session_json(metadata), _session_json(wire), int(shape[1]),
        int(shape[0]), int(planes),
    ))


@dataclass
class _State:
    pixels: np.ndarray
    is_stack: bool
    selection: Selection | None
    rois: list[NamedROI]
    overlays: list[Overlay]
    calibration: Calibration
    labels: bool
    spectrum: np.ndarray | None
    spectrum_shape: tuple | None
    identity: int
    sample_policy: ij.FloatingSamplePolicy
    marked_data: dict | None = None
    marked_title: str | None = None
    marked_previous_selection: Selection | None = None


class Document:
    def __setattr__(self, name, value):
        if name in _SESSION_DISPLAY_FIELDS and "_session_data" in self.__dict__:
            owned = _session_plain(value)
            _session_legacy_state({name: owned}, self.slices)
            if name == "display_range" and owned is not None:
                owned = tuple(owned)
            elif name == "custom_lut" and owned is not None:
                # Object elements keep null, integer and fractional entries.
                owned = np.asarray(owned, dtype=object)
            present = tuple(key for key in _SESSION_DISPLAY_FIELDS
                            if key in self._session_display_keys or key == name)
            object.__setattr__(self, name, owned)
            object.__setattr__(self, "_session_display_keys", present)
            return
        object.__setattr__(self, name, value)

    def __init__(
        self, pixels, title="Untitled", *, is_stack=False, path=None, history_limit=8,
        sample_policy=ij.FloatingSamplePolicy.FINITE_ONLY,
    ):
        if history_limit < 1:
            raise ValueError("History limit must be positive")
        self._sample_policy = _checked_sample_policy(sample_policy)
        self._data = checked_pixels(pixels, is_stack, sample_policy=self._sample_policy)
        self.title, self.path = title, None if path is None else Path(path)
        self.is_stack, self.slice_index = is_stack, 0
        self.selection: Selection | None = None
        self.previous_selection: Selection | None = None
        self.rois: list[NamedROI] = []
        self.overlays: list[Overlay] = []
        self.calibration = Calibration()
        self.labels = False
        self.spectrum = None
        self.spectrum_shape = None
        self.display_range: tuple[float, float] | None = None
        self.lut = "Grays"
        self.custom_lut = None
        self.view_state = {}
        self.zoom = 1.0
        self.history_limit = int(history_limit)
        self._undo: list[_State] = []
        self._redo: list[_State] = []
        self._identity = self._next_identity = self._saved_identity = self.revision = 0

    @property
    def sample_policy(self):
        """Immutable native source admission policy; processing remains finite-only."""
        return self._sample_policy

    def __deepcopy__(self, memo):
        # Native enum values are immutable; keep their identity while copying
        # detached pixel/history arrays and every ordinary Python attribute.
        memo[id(self._sample_policy)] = self._sample_policy
        result = type(self).__new__(type(self))
        memo[id(self)] = result
        result.__dict__.update(deepcopy(self.__dict__, memo))
        return result

    @property
    def data(self):
        view = self._data.view()
        view.flags.writeable = False
        return view

    @property
    def frame(self):
        view = (self._data[self.slice_index] if self.is_stack else self._data).view()
        view.flags.writeable = False
        return view

    @property
    def slices(self):
        return len(self._data) if self.is_stack else 1

    @property
    def shape(self):
        return self.frame.shape[:2]

    @property
    def dirty(self):
        return self._identity != self._saved_identity

    @property
    def can_undo(self):
        return bool(self._undo)

    @property
    def can_redo(self):
        return bool(self._redo)

    def _snapshot(self):
        return _State(
            self._data.copy(),
            self.is_stack,
            deepcopy(self.selection),
            deepcopy(self.rois),
            deepcopy(self.overlays),
            deepcopy(self.calibration),
            self.labels,
            None if self.spectrum is None else self.spectrum.copy(),
            self.spectrum_shape,
            self._identity,
            self._sample_policy,
            deepcopy(getattr(self, "_session_data", None)),
            self.title if "_session_data" in self.__dict__ else None,
            deepcopy(self.previous_selection)
            if "_session_data" in self.__dict__ else None,
        )

    def _begin_edit(self):
        self._undo.append(self._snapshot())
        self._undo = self._undo[-self.history_limit :]
        self._redo.clear()
        self._next_identity += 1
        self._identity = self._next_identity
        self.revision += 1

    def _restore(self, state):
        if state.sample_policy != self._sample_policy:
            raise ValueError("History cannot change the document sample policy")
        self._data, self.is_stack = state.pixels, state.is_stack
        self.selection, self.rois, self.overlays = (
            state.selection,
            state.rois,
            state.overlays,
        )
        self.calibration, self.labels = state.calibration, state.labels
        self.spectrum, self.spectrum_shape = state.spectrum, state.spectrum_shape
        self._identity = state.identity
        self.slice_index = min(self.slice_index, self.slices - 1)
        if state.marked_data is None:
            self.display_range = None
        else:
            # Display presence, values and camera belong to the current view,
            # not the restored document-data history entry.
            self._session_data = deepcopy(state.marked_data)
            self.title = state.marked_title
            self.previous_selection = deepcopy(state.marked_previous_selection)
        self.revision += 1

    def undo(self):
        if not self._undo:
            raise IndexError("No edit to undo")
        self._redo.append(self._snapshot())
        self._restore(self._undo.pop())

    def redo(self):
        if not self._redo:
            raise IndexError("No edit to redo")
        self._undo.append(self._snapshot())
        self._restore(self._redo.pop())

    def mark_saved(self, path=None):
        if path is not None:
            self.path = Path(path)
            self.title = self.path.name
        self._saved_identity = self._identity

    def select(self, selection):
        if selection is not None:
            selection.mask(self.shape)
        if self.selection is not None:
            self.previous_selection = self.selection.clone()
        self.selection = None if selection is None else selection.clone()

    def combine_rois(self, indices, operation="union", *, geometry=None):
        """Combine saved areas through native canvas or complete-region algebra."""
        selected = list(indices)
        if (
            len(selected) < 2
            or any(type(index) is not int for index in selected)
            or len(set(selected)) != len(selected)
            or any(index < 0 or index >= len(self.rois) for index in selected)
        ):
            raise ValueError("Choose at least two distinct valid ROI indices")
        height, width = self.shape
        operands = [self.rois[index].selection.native() for index in selected]
        if geometry not in (None, "canvas_v1", "full_region_v2"):
            raise ValueError("Choose canvas_v1 or full_region_v2 Boolean geometry")
        full = any(value.is_region for value in operands) if geometry is None else geometry == "full_region_v2"
        combine = ij.combine_selection_regions if full else ij.combine_selections
        native = combine(width, height, operands, operation)
        result = Selection.from_dict(native.to_dict()) if native.is_region else Selection(
            "mask", width=native.width, bitmap=native.mask(self.shape),
            attributes=native.attributes,
        )
        if self.selection is not None and self.selection.native() == native:
            return self.selection.clone()
        previous = None if self.selection is None else self.selection.clone()
        self.selection, self.previous_selection = result, previous
        return result.clone()

    def commit(self, pixels, *, is_stack=None, keep_geometry=False):
        stack = self.is_stack if is_stack is None else is_stack
        result = checked_pixels(pixels, stack, sample_policy=self._sample_policy)
        old_shape = self.shape
        new_shape = result.shape[1:3] if stack else result.shape[:2]
        marked = "_session_data" in self.__dict__
        if marked and (stack != self.is_stack or result.shape != self._data.shape):
            raise ValueError("Marked session geometry changes require a supported reconstruction")
        self._begin_edit()
        self._data, self.is_stack = result, stack
        self.slice_index = min(self.slice_index, self.slices - 1)
        self.labels, self.spectrum, self.spectrum_shape = False, None, None
        if not marked:
            self.display_range = None
        if not keep_geometry and old_shape != new_shape:
            self.selection = None
            self.rois.clear()
            self.overlays.clear()

    def compute(self, operation: Callable, *, params=None, all_slices=False):
        """Compute a complete proposed edit without mutating the document."""
        params = params or {}
        source = self._data.copy()
        selection = None if self.selection is None else self.selection.mask(self.shape)
        slice_index = self.slice_index

        def apply(frame):
            result = checked_pixels(
                operation(frame.copy(), **params), sample_policy=self._sample_policy
            )
            if selection is not None:
                if result.shape != frame.shape:
                    raise ValueError(
                        "Deselect the ROI before changing image dimensions or channels"
                    )
                result = ij.paste(
                    ij.convert(frame, result.dtype), result, mask=selection
                )
            return result

        if not self.is_stack:
            return apply(source)
        if all_slices:
            return np.stack([apply(frame) for frame in source])
        result = apply(source[slice_index])
        if result.shape != source[slice_index].shape:
            raise ValueError(
                "Choose All slices before changing stack dimensions or channels"
            )
        if source.dtype != result.dtype:
            source = np.stack([ij.convert(frame, result.dtype) for frame in source])
        source[slice_index] = result
        return source

    def apply(self, operation, *, params=None, all_slices=False):
        self.commit(self.compute(operation, params=params, all_slices=all_slices))

    def add_roi(self, name=None, *, all_slices=False):
        if self.selection is None:
            raise ValueError("Draw a selection first")
        roi = NamedROI(
            name or f"ROI {len(self.rois) + 1}",
            self.selection.clone(),
            None if all_slices or not self.is_stack else self.slice_index,
        )
        if "_session_data" in self.__dict__:
            self._session_validate_known_change({"rois": [
                *[item.to_dict() for item in self.rois], roi.to_dict(),
            ]})
        self._begin_edit()
        self.rois.append(roi)
        return roi

    def set_calibration(self, calibration):
        calibration.validate()
        if "_session_data" in self.__dict__:
            proposed = deepcopy(calibration)
            self._session_validate_known_change({"calibration": asdict(proposed)})
            self._begin_edit()
            self.calibration = proposed
            return
        self._begin_edit()
        self.calibration = deepcopy(calibration)

    def add_overlay(self, overlay):
        if (
            not np.isfinite(np.asarray(overlay.points)).all()
            or overlay.width <= 0
            or overlay.font_size <= 0
        ):
            raise ValueError("Overlay coordinates, width and font size must be valid")
        overlay.validate_style()
        if "_session_data" in self.__dict__:
            self._session_validate_known_change({"overlays": [
                *[item.to_dict() for item in self.overlays], overlay.to_dict(),
            ]})
        self._begin_edit()
        self.overlays.append(deepcopy(overlay))

    def measure(self, selection=None, *, slice_index=None):
        selection = self.selection if selection is None else selection
        frame = (
            self.frame
            if slice_index is None or not self.is_stack
            else self._data[slice_index]
        )
        mask = None if selection is None else selection.mask(frame.shape[:2])
        values = ij.statistics(gray(frame), mask=mask)
        c = self.calibration.validate()
        row = {
            "Image": self.title,
            "Slice": (self.slice_index if slice_index is None else slice_index) + 1,
            **values,
            "Area": values["count"] * c.pixel_width * c.pixel_height,
            "Unit": c.unit,
        }
        if selection is not None:
            row["ROI"] = selection.kind
            row["Bounds"] = selection.bounds(frame.shape[:2])
            if selection.kind in ("line", "polyline", "angle"):
                delta = np.diff(selection.points, axis=0) * [
                    c.pixel_width,
                    c.pixel_height,
                ]
                row["Length"] = float(np.linalg.norm(delta, axis=1).sum())
            if selection.kind == "angle" and len(selection.points) == 3:
                points = np.asarray(selection.points) * [c.pixel_width, c.pixel_height]
                a, b = points[0] - points[1], points[2] - points[1]
                norm = np.linalg.norm(a) * np.linalg.norm(b)
                row["Angle"] = (
                    float(np.degrees(np.arccos(np.clip(np.dot(a, b) / norm, -1, 1))))
                    if norm
                    else 0
                )
        return row

    def _session_shape(self):
        return self._data.shape[1:3] if self.is_stack else self._data.shape[:2]

    def _session_known(self):
        return _session_plain({
            "title": self.title,
            "is_stack": self.is_stack,
            "calibration": asdict(self.calibration),
            "selection": None if self.selection is None else self.selection.to_dict(),
            "previous_selection": None if self.previous_selection is None
            else self.previous_selection.to_dict(),
            "rois": [roi.to_dict() for roi in self.rois],
            "overlays": [overlay.to_dict() for overlay in self.overlays],
            "labels": self.labels,
        })

    @staticmethod
    def _session_updated_layout(metadata, wire, changed, baseline, planes):
        layout = metadata["record_layout"]
        if not layout or not changed:
            return  # Empty native layout requests canonical known fields.
        inferred = _session_legacy_state(wire, planes)["metadata"]["record_layout"]
        roots = list(layout.get("root_fields") or [])
        nulls = list(layout.get("null_fields") or [])
        roots_changed = False
        for key in changed:
            if key not in inferred["root_fields"]:
                continue
            roots_changed = True
            if key not in roots:
                roots.append(key)
            if wire.get(key) is None:
                if key not in nulls:
                    nulls.append(key)
            elif key in nulls:
                nulls.remove(key)
        if roots_changed:
            layout["root_fields"], layout["null_fields"] = roots, nulls
        if "calibration" in changed:
            fields = list(layout.get("calibration_fields") or [])
            for key in inferred["calibration_fields"]:
                if not _session_equal(wire["calibration"].get(key),
                                      baseline.get("calibration", {}).get(key)):
                    if key not in fields:
                        fields.append(key)
            layout["calibration_fields"] = fields
        if "rois" in changed:
            layout["roi_slice_fields"] = deepcopy(inferred["roi_slice_fields"])

    def _session_projection(self):
        data = self._session_data
        metadata = deepcopy(data["metadata"])
        known = self._session_known()
        wire = deepcopy(data["scalar_wire"])
        changed = [key for key in known
                   if not _session_equal(known[key], data["known_baseline"][key])]
        wire.update({key: deepcopy(known[key]) for key in changed})
        metadata["title"] = known["title"]
        for key in ("labels", "is_stack"):
            if not _session_equal(known[key], data["known_baseline"][key]):
                metadata["extensions"][key] = deepcopy(known[key])
        extra = metadata["extensions"].get("calibration", {})
        wire["calibration"].update(deepcopy(extra))
        self._session_updated_layout(metadata, wire, changed,
                                     data["known_baseline"], self.slices)
        for key in _SESSION_DISPLAY_FIELDS:
            wire.pop(key, None)
            if key in self._session_display_keys:
                wire[key] = _session_plain(getattr(self, key))
        wire["slice_index"] = self.slice_index
        wire["view_state"] = _session_plain(self.view_state)
        generated = _session_project(metadata, wire, self._session_shape(), self.slices)
        if not _session_equal(generated["slice_index"], wire["slice_index"]):
            raise ValueError("Marked slice index contradicts the current view crosshair")
        regenerated = _session_project(metadata, generated, self._session_shape(), self.slices)
        if not _session_equal(generated, regenerated):
            raise ValueError("Marked metadata cannot be regenerated exactly")
        return metadata, generated

    def _session_validate_known_change(self, changes):
        metadata, wire = self._session_projection()
        baseline = deepcopy(wire)
        wire.update(_session_plain(changes))
        if "calibration" in changes:
            wire["calibration"].update(deepcopy(metadata["extensions"].get("calibration", {})))
        self._session_updated_layout(metadata, wire, list(changes), baseline, self.slices)
        generated = _session_project(metadata, wire, self._session_shape(), self.slices)
        regenerated = _session_project(metadata, generated, self._session_shape(), self.slices)
        if not _session_equal(generated, regenerated):
            raise ValueError("Marked data edit cannot be regenerated exactly")

    def _session_native_state(self):
        """Return an owned scalar marker record, or None for a legacy owner."""
        if "_session_data" not in self.__dict__:
            return None
        metadata, wire = self._session_projection()
        return {
            "metadata": metadata,
            "display_properties": {key: deepcopy(wire[key])
                                   for key in _SESSION_DISPLAY_FIELDS if key in wire},
        }

    def _prepare_session_native_state(self, metadata, scalar_wire):
        metadata, wire = _session_exact_json(metadata), _session_exact_json(scalar_wire)
        if type(metadata) is not dict or type(wire) is not dict:
            raise ValueError("Marked document state must contain objects")
        if _SESSION_TRANSPORT_FIELDS & wire.keys():
            raise ValueError("Marked scalar state contains pixel transport fields")
        generated = _session_project(metadata, wire, self._session_shape(), self.slices)
        if not _session_equal(generated, wire):
            raise ValueError("Marked metadata contradicts the serialized document")
        if type(wire.get("is_stack")) is not bool or wire["is_stack"] != self.is_stack:
            raise ValueError("Marked metadata contradicts the document stack shape")
        calibration_record = wire.get("calibration", {})
        calibration = Calibration(**{
            key: deepcopy(value) for key, value in calibration_record.items()
            if key in _SESSION_CALIBRATION_FIELDS
        }).validate()
        selection = None if wire.get("selection") is None else Selection._from_session_dict(wire["selection"])
        previous = None if wire.get("previous_selection") is None else Selection._from_session_dict(wire["previous_selection"])
        rois = [NamedROI(
            value["name"], Selection._from_session_dict(value["selection"]),
            value.get("slice_index"),
            {key: deepcopy(item) for key, item in value.items()
             if key not in {"name", "selection", "slice_index"}},
        ) for value in wire.get("rois") or []]
        for roi, value in zip(rois, wire.get("rois") or []):
            roi._session_slice_present = "slice_index" in value
        overlays = [Overlay._from_session_dict(value) for value in wire.get("overlays") or []]
        fields = {
            "title": metadata["title"], "calibration": calibration,
            "selection": selection, "previous_selection": previous,
            "rois": rois, "overlays": overlays,
            "labels": wire.get("labels", False),
            "slice_index": wire["slice_index"],
            "view_state": deepcopy(wire["view_state"]),
            "display_range": None if wire.get("display_range") is None
            else tuple(wire["display_range"]),
            "lut": "Grays" if wire.get("lut") is None else wire["lut"],
            "custom_lut": None if wire.get("custom_lut") is None
            else np.asarray(wire["custom_lut"], dtype=object),
        }
        # An explicitly null LUT stays null in the getter and serialized map;
        # only absence supplies the legacy-compatible visual fallback.
        if "lut" in wire:
            fields["lut"] = wire["lut"]
        known = _session_plain({
            "title": fields["title"], "is_stack": self.is_stack,
            "calibration": asdict(calibration),
            "selection": None if selection is None else selection.to_dict(),
            "previous_selection": None if previous is None else previous.to_dict(),
            "rois": [roi.to_dict() for roi in rois],
            "overlays": [overlay.to_dict() for overlay in overlays],
            "labels": fields["labels"],
        })
        template = {key: deepcopy(item) for key, item in wire.items()
                    if key not in _SESSION_DISPLAY_FIELDS
                    and key not in {"view_state", "slice_index"}}
        data = {"metadata": deepcopy(metadata), "scalar_wire": template,
                "known_baseline": known}
        keys = tuple(key for key in _SESSION_DISPLAY_FIELDS if key in wire)
        return fields, data, keys

    def _publish_session_native_state(self, prepared):
        fields, data, keys = prepared
        # Every conversion and C++ validation has completed. Bypass tracked
        # assignments to publish the supplied exact presence set atomically.
        self.__dict__.update(fields)
        self.__dict__["_session_data"] = data
        self.__dict__["_session_display_keys"] = keys

    def _adopt_session_native_state(self, metadata, scalar_wire):
        """Validate before atomically adopting a detached marked scalar state."""
        prepared = self._prepare_session_native_state(metadata, scalar_wire)
        self._publish_session_native_state(prepared)

    def _session_checked_spectrum_shape(self, value):
        if value is None:
            return None
        if type(value) not in (list, tuple) or len(value) != 2 or any(
            type(size) is not int or not 0 < size <= 2**31 - 1
            for size in value
        ):
            raise ValueError("Original spectrum shape must contain two positive int32 dimensions")
        spectrum = self.spectrum
        if (
            not isinstance(spectrum, np.ndarray) or spectrum.ndim != 2
            or spectrum.dtype.kind not in "biufc"
            or any(size <= 0 for size in spectrum.shape)
            or not np.isfinite(spectrum).all()
        ):
            raise ValueError("Original spectrum shape requires a finite 2D spectrum")
        if any(size > bound for size, bound in zip(value, spectrum.shape)):
            raise ValueError("Original spectrum shape exceeds the current spectrum")
        return tuple(value)

    def _load_session_metadata(self, value):
        spectrum_shape = self.spectrum_shape
        if type(value) is dict and "spectrum_shape" in value:
            spectrum_shape = self._session_checked_spectrum_shape(value["spectrum_shape"])
        value = _session_plain(value)
        if type(value) is not dict:
            raise ValueError("Document metadata must be an object")
        if "pixel_sample_policy" in value and _sample_policy_from_metadata(value) != self._sample_policy:
            raise ValueError("Metadata cannot change the document sample policy")
        metadata, wire = self._session_projection()
        current = deepcopy(wire)
        for key in _SESSION_DISPLAY_FIELDS:
            wire.pop(key, None)
        updates = {key: deepcopy(item) for key, item in value.items()
                   if key not in _SESSION_TRANSPORT_FIELDS}
        if "calibration" in updates:
            supplied = updates["calibration"]
            if type(supplied) is not dict:
                raise ValueError("Calibration must be an object")
            # Known calibration fields can be partial. Explicit calibration
            # input replaces its opaque intensity/extras compartment.
            known = {key: deepcopy(item) for key, item in current["calibration"].items()
                     if key in _SESSION_CALIBRATION_FIELDS}
            known.update(supplied)
            updates["calibration"] = known
        wire.update(updates)
        if "slice_index" in updates and "view_state" not in updates:
            wire["view_state"] = deepcopy(wire["view_state"])
            wire["view_state"]["crosshair"][2] = updates["slice_index"]
        # Full scalar input can carry canonical values omitted by the native
        # record layout. Only a value or presence change updates that layout.
        changed = [key for key in updates if key not in current
                   or not _session_equal(updates[key], current[key])]
        inferred = _session_legacy_state(wire, self.slices)["metadata"]
        metadata["title"] = inferred["title"]
        if "native_properties" in updates:
            metadata["properties"] = deepcopy(inferred["properties"])
        for key, item in inferred["extensions"].items():
            if key in changed:
                metadata["extensions"][key] = deepcopy(item)
        if "calibration" in updates:
            metadata["extensions"].pop("calibration", None)
            if "calibration" in inferred["extensions"]:
                metadata["extensions"]["calibration"] = deepcopy(inferred["extensions"]["calibration"])
        self._session_updated_layout(metadata, wire, changed, current, self.slices)
        generated = _session_project(metadata, wire, self._session_shape(), self.slices)
        if not _session_equal(generated["slice_index"], wire["slice_index"]):
            raise ValueError("Marked slice index contradicts the supplied view crosshair")
        prepared = self._prepare_session_native_state(metadata, generated)
        self._publish_session_native_state(prepared)
        self.spectrum_shape = spectrum_shape

    def metadata(self):
        if "_session_data" in self.__dict__:
            _, scalar = self._session_projection()
            # Public Python geometry retains tuple coordinates; the owned
            # native projection and opaque records keep their JSON form.
            selections = [scalar.get("selection"), scalar.get("previous_selection")]
            selections.extend(roi["selection"] for roi in scalar.get("rois") or [])
            for selection in selections:
                if selection is not None and selection["kind"] != "mask":
                    selection["points"] = [tuple(point) for point in selection["points"]]
            for key in ("origin", "direction"):
                value = scalar["calibration"].get(key)
                if value is not None:
                    scalar["calibration"][key] = tuple(value)
            if self.spectrum_shape is not None:
                scalar["spectrum_shape"] = _session_plain(self.spectrum_shape)
            if self._sample_policy != ij.FloatingSamplePolicy.FINITE_ONLY:
                scalar["pixel_sample_policy"] = _IEEE_POLICY_TOKEN if self._sample_policy == ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32 else _QUIET_NAN_POLICY_TOKEN
            return scalar
        result = {
            "title": self.title,
            "is_stack": self.is_stack,
            "slice_index": self.slice_index,
            "calibration": asdict(self.calibration),
            "selection": None if self.selection is None else self.selection.to_dict(),
            "previous_selection": (
                None if self.previous_selection is None
                else self.previous_selection.to_dict()
            ),
            "rois": [roi.to_dict() for roi in self.rois],
            "overlays": [o.to_dict() for o in self.overlays],
            "display_range": self.display_range,
            "lut": self.lut,
            "labels": self.labels,
            "custom_lut": None if self.custom_lut is None else self.custom_lut.tolist(),
            "spectrum_shape": self.spectrum_shape,
            "view_state": deepcopy(self.view_state),
        }
        if self._sample_policy != ij.FloatingSamplePolicy.FINITE_ONLY:
            result["pixel_sample_policy"] = _IEEE_POLICY_TOKEN if self._sample_policy == ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32 else _QUIET_NAN_POLICY_TOKEN
        return result

    def load_metadata(self, value):
        if "channel_display" in value:
            raise ValueError("Channel display requires the native document/session interface")
        if "_session_data" in self.__dict__:
            self._load_session_metadata(value)
            return
        # Prepare every potentially failing conversion and style/geometry
        # check before assigning any live field. A rejected style is atomic.
        if "pixel_sample_policy" in value and (
            _sample_policy_from_metadata(value) != self._sample_policy
        ):
            raise ValueError("Metadata cannot change the document sample policy")
        view_state = deepcopy(value.get("view_state", {}))
        slice_index = min(
            max(int(value.get("slice_index", 0)), 0), self.slices - 1
        )
        calibration = Calibration(**value.get("calibration", {})).validate()
        selection = (
            None
            if value.get("selection") is None
            else Selection.from_dict(value["selection"])
        )
        if selection is not None:
            selection.mask(self.shape)
        if "previous_selection" in value:
            previous = (
                None if value["previous_selection"] is None
                else Selection.from_dict(value["previous_selection"])
            )
            # Previous geometry can refer to an earlier extent after crop.
            if previous is not None and "imagejxx.roi_style" in previous.attributes:
                ij.roi_style(previous.native())
        else:
            previous = deepcopy(
                self.selection if self.selection is not None else self.previous_selection
            )
        rois = [NamedROI.from_dict(roi) for roi in value.get("rois", [])]
        for roi in rois:
            roi.selection.mask(self.shape)
            if roi.slice_index is not None and not 0 <= roi.slice_index < self.slices:
                raise ValueError("Saved ROI slice is out of range")
        overlays = [Overlay.from_dict(o) for o in value.get("overlays", [])]
        for overlay in overlays:
            overlay.validate_style()
            geometry = overlay.selection_record()
            if "imagejxx.roi_style" in geometry and geometry["kind"] != "text":
                Selection.from_dict(geometry).native().mask(self.shape)
        display_range = (
            None
            if value.get("display_range") is None
            else tuple(value["display_range"])
        )
        lut, labels = (
            value.get("lut", "Grays"),
            bool(value.get("labels", False)),
        )
        custom_lut = (
            None
            if value.get("custom_lut") is None
            else np.asarray(value["custom_lut"], dtype=np.uint8)
        )
        spectrum_shape = (
            None
            if value.get("spectrum_shape") is None
            else tuple(value["spectrum_shape"])
        )
        self.view_state, self.slice_index, self.calibration = (
            view_state, slice_index, calibration
        )
        self.selection, self.previous_selection = selection, previous
        self.rois, self.overlays = rois, overlays
        self.display_range, self.lut, self.labels = display_range, lut, labels
        self.custom_lut, self.spectrum_shape = custom_lut, spectrum_shape
