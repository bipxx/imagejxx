"""Additive Python mask/working-limit contracts and pre-callback refusal."""
import struct

import imagejxx as ij
import numpy as np
import pytest

SIZE_MAX = (1 << (8 * struct.calcsize("P"))) - 1


def owner():
    document = ij.Document(ij.Dataset(np.arange(17*23, dtype=np.float32).reshape(17, 23), axes="YX"))
    document.select(ij.Selection.from_path(ij.ImageJCompositePath(
        [0, 2.2, 1.2, 1, 9.2, 1.2, 1, 2.2, 8.2, 4])))
    return document


def test_additive_limits_preserve_old_construction_and_separate_accounting():
    options = ij.ExecutionOptions(ij.DatasetPolicy.PRESERVE_UNTARGETED,
                                  ij.GeometryPolicy.REQUIRE_TRANSFORM)
    assert options.max_target_mask_bytes == SIZE_MAX
    options.max_target_mask_bytes = 0
    assert options.max_target_mask_bytes == 0
    assert ij.ExecutionOptions(max_target_mask_bytes=123).max_target_mask_bytes == 123
    old = ij.NativeWorkflowLimits(4, 4096)
    assert (old.max_steps, old.max_retained_bytes, old.max_working_bytes) == (4, 4096, SIZE_MAX)
    new = ij.NativeWorkflowLimits(4, 4096, max_working_bytes=8192)
    assert (new.max_retained_bytes, new.max_working_bytes) == (4096, 8192)
    with pytest.raises(AttributeError):
        new.max_working_bytes = 16384


@pytest.mark.parametrize("value,error", [
    (True, TypeError), (False, TypeError), (1.5, TypeError), ("12", TypeError),
    (-1, OverflowError), (SIZE_MAX+1, OverflowError),
])
def test_mask_byte_limit_constructor_and_setter_reject_non_sizes(value, error):
    with pytest.raises(error):
        ij.ExecutionOptions(max_target_mask_bytes=value)
    options = ij.ExecutionOptions(max_target_mask_bytes=123)
    with pytest.raises(error):
        options.max_target_mask_bytes = value
    assert options.max_target_mask_bytes == 123


@pytest.mark.parametrize("value,error", [
    (True, TypeError), (False, TypeError), (1.5, TypeError), ("12", TypeError),
    (-1, OverflowError), (SIZE_MAX+1, OverflowError), (0, ValueError),
])
def test_workflow_working_limit_rejects_invalid_sizes(value, error):
    with pytest.raises(error):
        ij.NativeWorkflowLimits(max_working_bytes=value)


def test_direct_mask_refusal_precedes_progress_and_preserves_owner():
    document = owner()
    before, steps = document.snapshot(), document.undo_steps
    command = ij.resolve_gaussian_command(before, options=ij.GaussianBlurCommandOptions(1))
    progress = []
    with pytest.raises(ValueError, match="working-byte|workspace|mask"):
        ij.prepare_operation("gaussian_blur_imagej", command.target, command.parameters(),
            options=ij.ExecutionOptions(max_target_mask_bytes=0),
            progress=lambda *values: progress.append(values))
    assert progress == []
    assert before.shares_data(document.snapshot()) and document.undo_steps == steps


@pytest.mark.parametrize("prefix", [False, True])
def test_workflow_working_refusal_precedes_prefix_progress_and_preserves_owner(prefix):
    document = owner()
    before, steps = document.snapshot(), document.undo_steps
    target = ij.TargetRequest(roi=ij.RoiPolicy.ACTIVE_SELECTION)
    filtered = ij.NativeWorkflowOperation("gaussian_blur_imagej", {}, target,
                                        filter_options=ij.GaussianBlurCommandOptions(1))
    operations = ([ij.NativeWorkflowOperation("add", {"value": 1}, target)] if prefix else []) + [filtered]
    progress = []
    with pytest.raises(ValueError, match="working|phase|mask"):
        ij.prepare_native_workflow(before, operations,
            limits=ij.NativeWorkflowLimits(max_working_bytes=1),
            progress=lambda *values: progress.append(values))
    assert progress == []
    assert before.shares_data(document.snapshot()) and document.undo_steps == steps
