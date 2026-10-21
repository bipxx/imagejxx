"""The Preview reader must distinguish storage validation from kernel work."""

import pytest

from test_gaussian_preview_ui import SAMPLES, worker_events


def _event(sequence, stage="", completed=0, total=0, *, phase="stage_progress", guard="0x1234"):
    return {"sequence": sequence, "phase": phase, "role": "native_worker",
            "operation": "gaussian_blur_imagej", "source_id": 17, "stage": stage,
            "completed": completed, "total": total, "resource_guard_address": guard}


def _kernel_prefix():
    return [_event(1, phase="worker_entered"),
            _event(2, "ImageJ Gaussian source copy", SAMPLES, SAMPLES)]


def test_exact_immutable_validation_is_allowed_after_actual_gaussian_work():
    worker_events(_kernel_prefix() + [
        _event(3, "Immutable plane sample validation", 0, SAMPLES),
        _event(4, "Immutable plane sample validation", SAMPLES, SAMPLES),
    ], 17)


@pytest.mark.parametrize("stage", [
    "Immutable plane sample validation extra", "immutable plane sample validation",
    "Immutable plane validation", "Atomic file write", "Canonical pixels",
    "Unrelated operation progress",
])
def test_unrecognized_storage_or_other_progress_stage_is_rejected(stage):
    with pytest.raises(AssertionError):
        worker_events(_kernel_prefix() + [_event(3, stage, 0, SAMPLES)], 17)


@pytest.mark.parametrize("completed,total", [(0, SAMPLES - 1), (0, SAMPLES + 1),
                                             (1, SAMPLES), (SAMPLES + 1, SAMPLES)])
def test_immutable_validation_requires_exact_fixture_extent_and_block_endpoints(completed, total):
    with pytest.raises(AssertionError):
        worker_events(_kernel_prefix() + [
            _event(3, "Immutable plane sample validation", completed, total)], 17)


@pytest.mark.parametrize("prefix,guard", [
    ([_event(1, phase="worker_entered")], "0x1234"),
    ([_event(1, phase="worker_entered"),
      _event(2, "ImageJ Gaussian source copy", 0, SAMPLES)], "0x1234"),
    (_kernel_prefix(), "0x5678"),
    (_kernel_prefix(), None),
    (_kernel_prefix() + [_event(3, phase="worker_entered")], "0x1234"),
])
def test_immutable_validation_cannot_supply_or_borrow_gaussian_work_evidence(prefix, guard):
    with pytest.raises(AssertionError):
        worker_events(prefix + [_event(len(prefix) + 1, "Immutable plane sample validation",
                                      0, SAMPLES, guard=guard)], 17)
