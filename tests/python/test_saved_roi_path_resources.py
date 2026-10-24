"""An admitted saved-path capture must fit its advertised resource bounds."""

import imagejxx as ij
import numpy as np
import pytest


@pytest.mark.parametrize("segments", [1024, 8192])
def test_admitted_large_path_capture_fits_estimate_and_record_replays(segments):
    commands = [0, 2, 2]
    corners = [(2, 2), (6, 2), (6, 6), (2, 6)]
    for index in range(segments):
        commands.extend([1, *corners[index % 4]])
    commands.append(4)
    selection = ij.Selection.from_path(ij.ImageJCompositePath(commands))
    selection = selection.with_attributes({"opaque": ["retained", 7]})
    owner = ij.Document(ij.Dataset(np.zeros((8, 8), np.uint16), axes="YX"))
    edit = owner.begin_edit()
    edit.set_rois([ij.ROI("large path", selection, attributes={"row": "keep"})])
    owner.commit(edit)
    source = owner.snapshot()
    request = ij.SavedRoiGeometryRequest(0, selection)
    estimate = ij.estimate_saved_roi_geometry(source, request)
    limits = ij.SavedRoiGeometryLimits(
        max_working_bytes=estimate.working_bytes,
        max_record_bytes=estimate.record_wire_bytes,
    )
    resolved = ij.resolve_saved_roi_geometry(source, request, limits)
    # Import retains and validates the complete supplied record in addition to
    # capture state. Its separate admission is not bounded by capture's working
    # estimate; keep the same advertised wire cap and the default working cap.
    replay_limits = ij.SavedRoiGeometryLimits(max_record_bytes=estimate.record_wire_bytes)
    replay = ij.saved_roi_geometry_from_record(source, resolved.record(), replay_limits)
    assert resolved.row_index == replay.row_index == 0
    assert list(resolved.replacement.path.commands) == commands
    assert list(replay.replacement.path.commands) == commands
    assert resolved.replacement.attributes == selection.attributes
    assert replay.original_roi.attributes == {"row": "keep"}
    assert owner.snapshot().rois[0].selection.attributes == selection.attributes

    for field, bound in [("max_working_bytes", estimate.working_bytes),
                         ("max_record_bytes", estimate.record_wire_bytes)]:
        insufficient = ij.SavedRoiGeometryLimits(**{field: bound - 1})
        with pytest.raises((ValueError, RuntimeError), match="resource limit"):
            ij.resolve_saved_roi_geometry(source, request, insufficient)
