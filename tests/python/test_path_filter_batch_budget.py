"""Batch outer budget must constrain per-document path-mask planning."""
import struct

import imagejxx as ij
import numpy as np
import pytest


def test_batch_outer_cap_reaches_path_planner_before_prefix_publication(tmp_path):
    # Strongly curved, repeated subpaths have substantial flattening workspace
    # while the source session is small. Odd multiplicity preserves area. This
    # reaches path admission rather than an earlier file/decompression limit.
    commands = [0, 2.2, 1.2, 3, 100000000., 0., 0., 100000000.,
                9.2, 1.2, 1, 2.2, 8.2, 4] * 63
    document = ij.Document(ij.Dataset(np.full((17, 23), 10, np.float32), axes="YX"))
    document.select(ij.Selection.from_path(ij.ImageJCompositePath(commands)))
    before = document.snapshot()
    history = document.undo_steps
    source = tmp_path / "input.ijx"
    ij.save_session(str(source), ij.SessionCapture([before]))
    original = source.read_bytes()
    destination = tmp_path / "output"
    destination.mkdir()
    inputs = [ij.NativeWorkflowBatchInput(str(source), "filtered")]
    whole = ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE)

    def steps(roi):
        return [
            ij.NativeWorkflowOperation("add", {"value": 1}, whole),
            ij.NativeWorkflowOperation("gaussian_blur_imagej", {},
                ij.TargetRequest(roi=roi),
                filter_options=ij.GaussianBlurCommandOptions(0)),
        ]

    assert ij.NativeWorkflowLimits().max_working_bytes == (1 << (8 * struct.calcsize("P"))) - 1
    low = ij.NativeWorkflowBatchOptions(max_working_bytes=2 * 1024**2)
    control = ij.prepare_native_workflow_batch(
        inputs, steps(ij.RoiPolicy.WHOLE_PLANE), str(destination), low)
    assert control.items[0]["state"] == ij.NativeWorkflowBatchState.PREPARED
    assert not (destination / "filtered.ijx").exists()
    control.abandon()
    assert list(destination.iterdir()) == []

    # Batch file reads/status notifications happen before native planning. They
    # are expected; there is deliberately no blanket zero-callback/I/O claim.
    events = []
    with pytest.raises(ValueError, match="ImageJ shape exceeds working-byte limit"):
        ij.prepare_native_workflow_batch(inputs, steps(ij.RoiPolicy.ACTIVE_SELECTION),
            str(destination), low, progress=lambda index, state: events.append((index, state)))
    states = [state for index, state in events if index == 0]
    assert ij.NativeWorkflowBatchState.READING in states
    assert ij.NativeWorkflowBatchState.PLANNING in states
    assert states[-1] == ij.NativeWorkflowBatchState.FAILED
    assert not set(states).intersection({ij.NativeWorkflowBatchState.ENCODING,
        ij.NativeWorkflowBatchState.PREPARED, ij.NativeWorkflowBatchState.COMMITTED})
    assert list(destination.iterdir()) == []
    assert source.read_bytes() == original
    assert document.snapshot().shares_data(before) and document.undo_steps == history

    recovered = ij.prepare_native_workflow_batch(inputs,
        steps(ij.RoiPolicy.ACTIVE_SELECTION), str(destination),
        ij.NativeWorkflowBatchOptions(max_working_bytes=16 * 1024**2))
    assert not (destination / "filtered.ijx").exists()
    recovered.publish()
    assert recovered.items[0]["state"] == ij.NativeWorkflowBatchState.COMMITTED
    loaded = ij.load_session(str(destination / "filtered.ijx")).documents[0].snapshot()
    # Sigma zero changes no selected or unselected sample. The earlier whole
    # plane Add 1 therefore has an independent, exact all-11 output oracle.
    expected = np.full((17, 23), 11, np.float32)
    assert loaded.dataset.numpy("YX").tobytes() == expected.tobytes()
    assert loaded.selection.path.commands == commands
    assert source.read_bytes() == original
    assert document.snapshot().shares_data(before) and document.undo_steps == history
    assert {p.name for p in destination.iterdir()} == {"filtered.ijx"}
