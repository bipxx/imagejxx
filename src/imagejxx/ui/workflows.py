"""Explicit, portable native-operation workflows for recording and batch processing."""

from __future__ import annotations
import json
from pathlib import Path

import imagejxx as ij

from .commands import OPERATIONS, json_values
from .model import Document, Selection


def validate_steps(steps):
    if not isinstance(steps, list):
        raise ValueError("A workflow must be a list of operation steps")
    for step in steps:
        if not isinstance(step, dict) or step.get("operation") not in OPERATIONS:
            raise ValueError(
                f"Unknown workflow operation: {step.get('operation') if isinstance(step, dict) else step}"
            )
        if "schema_version" in step:
            if step["operation"] == "gaussian_blur_imagej":
                ij.validate_gaussian_command_record(step)
            elif step["operation"] in ("rank_filter_imagej", "remove_outliers_imagej", "top_hat_imagej"):
                ij.validate_rank_command_record(step)
            else:
                raise ValueError(f"Unsupported typed workflow operation: {step['operation']}")
            continue
        parameters = step.get("parameters", {})
        if not isinstance(parameters, dict):
            raise ValueError("Workflow parameters must be an object")
        ij.validate_operation_record(step["operation"], parameters)
        if "selection" in step and step["selection"] is not None:
            Selection.from_dict(step["selection"])
    return steps


def dumps(steps):
    return json.dumps(
        {
            "format": "imagejxx-workflow",
            "version": 1,
            "steps": json_values(validate_steps(steps)),
        },
        indent=2,
    )


def loads(text):
    value = json.loads(text)
    if (not isinstance(value, dict)
            or value.get("format") != "imagejxx-workflow"
            or type(value.get("version")) is not int or value["version"] != 1):
        raise ValueError("Unsupported workflow file")
    return validate_steps(value["steps"])


def load(path):
    return loads(Path(path).read_text(encoding="utf-8"))


def _run_typed(working, step, request, cancelled):
    """Adapt a model's current state; all command execution/publication is native."""
    calibration = working.calibration
    spatial = ij.SpatialCalibration(
        spacing=(calibration.pixel_width, calibration.pixel_height,
                 calibration.pixel_depth),
        origin=calibration.origin, direction=calibration.direction,
        unit=calibration.unit, frame_interval=calibration.frame_interval,
        time_origin=calibration.time_origin, time_unit=calibration.time_unit,
    )
    axes = ("Z" if working.is_stack else "") + "YX"
    if working.frame.ndim == 3:
        axes += "S"
    application = ij.Application()
    owner_id = application.create(
        ij.Dataset(working.data, axes=axes, calibration=spatial,
                   sample_policy=working.sample_policy), working.title
    )
    edit = application.begin_edit(owner_id)
    edit.select(None if working.selection is None else working.selection.native())
    edit.set_rois([
        ij.ROI(roi.name, roi.selection.native(), z=roi.slice_index,
               attributes=roi.attributes)
        for roi in working.rois
    ])
    application.commit(edit)
    display = application.snapshot(owner_id).display
    crosshair = list(display.crosshair)
    crosshair[2] = working.slice_index
    display.crosshair = crosshair
    application.set_display(display, owner_id)
    resolver = {
        "gaussian_blur_imagej": ij.rebind_gaussian_command,
        "rank_filter_imagej": ij.rebind_rank_command,
        "remove_outliers_imagej": ij.rebind_rank_command,
        "top_hat_imagej": ij.rebind_rank_command,
    }[step["operation"]]
    command = resolver(application.snapshot(owner_id), step, request)

    def check_cancelled(completed, total):
        if cancelled():
            raise InterruptedError("Workflow cancelled")

    prepared = application.prepare(step["operation"], command.target,
                                   command.parameters(), progress=check_cancelled)
    if cancelled():
        raise InterruptedError("Workflow cancelled")
    published = application.publish(prepared)
    working.commit(published.document.snapshot().dataset.numpy(axes))


def run(document, steps, *, cancelled=lambda: False, progress=lambda i, total: None,
        gaussian_rebind=None, rank_rebind=None):
    """Return a proposal; typed records require an explicit TargetRequest rebind.

    Both replay paths create working owners with new identities. Numeric legacy
    steps remain portable; source-bound Gaussian/rank records require their
    own explicit request, and the source survives every failing step. This
    detached model adapter retains its legacy [Z,]YXS axis contract.
    """
    validate_steps(steps)
    requests = {
        "gaussian_blur_imagej": (gaussian_rebind, "Gaussian"),
        "rank_filter_imagej": (rank_rebind, "rank"),
        "remove_outliers_imagej": (rank_rebind, "rank"),
        "top_hat_imagej": (rank_rebind, "rank"),
    }
    for operation, (request, name) in requests.items():
        if any("schema_version" in step and step["operation"] == operation
               for step in steps):
            if not isinstance(request, ij.TargetRequest):
                raise TypeError(
                    f"Typed {name} workflow replay requires an explicit TargetRequest"
                )
    working = Document(
        document.data, document.title, is_stack=document.is_stack, history_limit=1,
        sample_policy=document.sample_policy,
    )
    working.load_metadata(document.metadata())
    for i, step in enumerate(steps):
        if cancelled():
            raise InterruptedError("Workflow cancelled")
        if "schema_version" in step:
            _run_typed(working, step, requests[step["operation"]][0], cancelled)
            progress(i + 1, len(steps))
            continue
        if "selection" in step:
            working.select(
                None
                if step["selection"] is None
                else Selection.from_dict(step["selection"])
            )
        index = int(step.get("slice_index", working.slice_index))
        if not 0 <= index < working.slices:
            raise ValueError("Recorded slice is outside this stack")
        working.slice_index = index
        saved_selection = working.selection
        if step["operation"] == "convert_image":
            working.select(None)
        working.apply(
            OPERATIONS[step["operation"]].execute,
            params=step.get("parameters", {}),
            all_slices=step["operation"] == "convert_image"
            or bool(step.get("all_slices", False)),
        )
        if step["operation"] == "convert_image":
            working.select(saved_selection)
        progress(i + 1, len(steps))
    return working.data.copy()
