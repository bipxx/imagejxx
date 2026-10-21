"""Shared command catalog for menus, parameter dialogs, recorder and batch jobs."""

from __future__ import annotations

from dataclasses import dataclass
import json

import numpy as np
import imagejxx as ij


@dataclass(frozen=True)
class Parameter:
    name: str
    label: str
    default: object
    kind: str
    choices: tuple = ()
    optional: bool = False

    def parse(self, text):
        if self.optional and str(text).strip().lower() in (
            "",
            "none",
            "auto",
            "preserve",
        ):
            return None
        if self.kind == "bool":
            return (
                bool(text)
                if isinstance(text, bool)
                else str(text).lower() in ("true", "1", "yes")
            )
        if self.kind == "int":
            return int(text)
        if self.kind == "float":
            return float(text)
        if self.kind == "json":
            return json.loads(text)
        return str(text)


CHOICES = {
    "border": ("reflect", "nearest", "wrap", "constant"),
    "interpolation": ("nearest", "linear", "cubic"),
    "dtype": ("uint8", "uint16", "float32", "float64"),
    "footprint": ("disk", "square"),
    "connectivity": (4, 8),
}
METHODS = {
    "rank_filter": ("median", "mean", "minimum", "maximum", "variance", "stddev"),
    "morphology": (
        "erode",
        "dilate",
        "open",
        "close",
        "gradient",
        "top_hat",
        "black_hat",
    ),
    "auto_threshold": ij.THRESHOLD_METHODS,
    "auto_mask": ij.THRESHOLD_METHODS,
    "local_threshold": ij.LOCAL_THRESHOLD_METHODS,
    "binning": ("mean", "sum", "minimum", "maximum"),
    "project": ("maximum", "minimum", "mean", "sum", "median", "stddev"),
}
MATH = (
    "invert",
    "add",
    "subtract",
    "multiply",
    "divide",
    "minimum",
    "maximum",
    "set",
    "abs",
    "square",
    "sqrt",
    "log",
    "log1p",
    "exp",
    "gamma",
    "and",
    "or",
    "xor",
)
CALCULATOR = (
    "add",
    "subtract",
    "multiply",
    "divide",
    "minimum",
    "maximum",
    "average",
    "difference",
    "and",
    "or",
    "xor",
    "copy",
)


@dataclass(frozen=True)
class Operation:
    name: str
    title: str
    menu: tuple[str, ...]

    @property
    def function(self):
        if self.name in ("paint_stroke", "fill_color"):
            from . import rendering

            return getattr(rendering, self.name)
        return getattr(ij, self.name)

    @property
    def description(self):
        return _NATIVE_CATALOG[self.name]["description"]

    def parameters(self, document=None):
        defaults = ij.operation_defaults(
            self.name,
            None if document is None else document.frame,
            legacy_python=True,
        )
        return [
            Parameter(
                value["name"],
                value["label"],
                defaults[value["name"]],
                value["kind"],
                tuple(value["choices"]),
                value["optional"],
            )
            for value in _NATIVE_CATALOG[self.name]["parameters"]
        ]

    def execute(self, image, **params):
        params = params.copy()
        # The legacy Python color helper always wrote opaque RGBA. Preserve
        # recorded Python workflows; native operations accept explicit alpha.
        if self.name in ("fill_color", "paint_stroke"):
            color = params.get("color")
            if isinstance(color, str) and len(color) == 9 and color.startswith("#"):
                params["color"] = color[:7]
        if params.get("dtype") == "preserve":
            params["dtype"] = None
        for name in ("other", "source"):
            if name in params:
                pixels = np.asarray(params[name])
                if pixels.dtype.name not in ("uint8", "uint16", "float32", "float64"):
                    pixels = pixels.astype(np.float64)
                params[name] = ij.convert(pixels, image.dtype)
        return ij.execute_operation(self.name, image, params)


# The compiled C++ registry owns menu and parameter contracts. The JSON file is
# an export for older tooling, not a build or runtime authority.
_NATIVE_CATALOG = {entry["id"]: entry for entry in ij.operation_catalog()}
GROUPS = {}
for _entry in _NATIVE_CATALOG.values():
    GROUPS.setdefault(tuple(_entry["menu"]), []).append(_entry["id"])
TITLES = {name: entry["title"] for name, entry in _NATIVE_CATALOG.items()}
OPERATIONS = {
    name: Operation(name, entry["title"], tuple(entry["menu"]))
    for name, entry in _NATIVE_CATALOG.items()
}


def json_values(value):
    if isinstance(value, np.ndarray):
        return value.tolist()
    if isinstance(value, np.generic):
        return value.item()
    if isinstance(value, dict):
        return {k: json_values(v) for k, v in value.items()}
    if isinstance(value, (tuple, list)):
        return [json_values(v) for v in value]
    return value
