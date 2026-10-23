"""Historical selections must remain visible in eager/virtual XY and linked XYZ."""
import time
import json
import numpy as np
import pytest
import imagejxx as ij
from imagejxx.ui.app import Editor


@pytest.mark.parametrize("virtual", [False, True])
@pytest.mark.parametrize("xyz", [False, True])
@pytest.mark.parametrize("asynchronous", [False, True])
def test_session_history_is_baked_for_all_linked_views_and_masks(virtual, xyz, asynchronous, tmp_path):
    pixels = np.tile(np.array([20, 30, 40], np.uint8), (2, 2, 3, 3, 4, 1))
    dataset = ij.Dataset(pixels, axes="TCZYXS")
    if virtual:
        from test_virtual_tiff import write
        from imagejxx import scientific_io as io
        path = tmp_path/"stack.tif"
        write(path, pixels, io.ImageFileFormat.SCIENTIFIC_TIFF_RGB8_V1)
        dataset = io.open_virtual_image_series(path).dataset
    owner = ij.Document(dataset)
    owner.select(ij.Selection("rectangle", [(1, 0), (3, 2)]))
    view = owner.snapshot().display
    view.t = view.c = 1
    view.crosshair = [1, 1, 0]
    view.orthogonal = xyz
    owner.set_display(view)
    editor = Editor(preferences=tmp_path/"prefs.json")
    editor.update_settings(async_viewport=asynchronous, persist=False)
    editor._editor.add_virtual_document(owner)
    try:
        editor.action("show_contrast")
        editor.action("contrast_range", minimum=10, maximum=40)
        editor.action("contrast_rgb_channels", channels=4)
        editor.action("contrast_range", minimum=10, maximum=40)
        editor.action("wait_jobs")
        view = owner.snapshot().display
        view.crosshair = [1, 1, 1]
        owner.set_display(view)
        editor.action("wait_jobs")
        owner.select(ij.Selection("rectangle", [(0, 1), (2, 3)]))
        editor.action("wait_jobs")
        editor.action("contrast_range", minimum=0, maximum=80)
        editor.action("wait_jobs")
        view = owner.snapshot().display
        view.crosshair = [1, 1, 0]
        owner.set_display(view)
        editor.action("wait_jobs")
        editor.action("contrast_reset")
        editor.action("wait_jobs")
        editor.action("show_channel_display")
        expected = pixels.copy()
        expected[1, 1, 0, :2, 1:3] = [255, 170, 255]
        expected[1, 1, 1, 1:3, :2, 0] = 64
        deadline = time.monotonic()+10
        while True:
            report = editor.render_headless(frames=8)
            planes = report["orthogonal_planes"] if xyz else [report["ordinary_pixels"]]
            if len(planes) == (3 if xyz else 1) and all(p["readback"]["available"] for p in planes):
                break
            if time.monotonic() >= deadline:
                (tmp_path/"timeout.json").write_text(json.dumps(report, indent=2))
                pytest.fail("Linked preview timed out; see timeout.json")
        assert report["contrast_panel"]["rgb"]["session_visits"] == 3
        for index, plane in enumerate(planes):
            wanted = expected[1, 1, 0] if index == 0 else expected[1, 1, :, 1] if index == 1 else expected[1, 1, :, :, 1]
            for field in ["source", "rendered_rgb"]:
                assert plane["readback"][field]["pixels"] == wanted.ravel().tolist()
        np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYXS"), pixels)
        before = owner.snapshot()
        editor.action("contrast_apply")
        editor.action("wait_jobs")
        np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYXS"), expected)
        owner.undo()
        assert owner.snapshot().shares_data(before)
    finally:
        editor.action("close_contrast")
