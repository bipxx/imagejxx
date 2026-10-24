"""Independent pinned path masks/selected Gaussian words via installed API."""
import gzip
import json
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = json.loads(gzip.decompress(
    (ROOT / "tests/fixtures/path-filter-targets-imagej154r.json.gz").read_bytes()))
CASES = [row for row in FIXTURE["cases"] + FIXTURE["degenerate_cases"]
         if row["request"]["mode"] == "small-mask"]


def canvas_mask(observed, width, height):
    result = np.zeros((height, width), dtype=np.uint8)
    full = observed["roi_mask"]
    if full is None:
        return result
    x, y = full["image_origin"]
    w, h = full["dimensions"]
    pixels = bytes.fromhex(full["u8_hex"])
    for j in range(max(y, 0), min(y+h, height)):
        for i in range(max(x, 0), min(x+w, width)):
            result[j, i] = pixels[(j-y)*w+i-x]
    return result


@pytest.mark.parametrize("row", CASES, ids=[row["id"] for row in CASES])
def test_pinned_path_target_mask_and_selected_gaussian_words(row):
    request, observed = row["request"], row["observation"]
    w, h = request["width"], request["height"]
    original_words = np.array([int(value, 16) for value in observed["input"]["f32_words"]],
                              dtype=np.uint32).reshape(h, w)
    original = original_words.view(np.float32).copy()
    document = ij.Document(ij.Dataset(original, axes="YX"))
    selection = ij.Selection.from_path(ij.ImageJCompositePath(request["commands"]))
    document.select(selection)
    before = document.snapshot()
    steps = document.undo_steps
    command = ij.resolve_gaussian_command(before,
        options=ij.GaussianBlurCommandOptions(request["sigma"], False))
    region = ij.gaussian_command_region(command.target)
    expected_mask = canvas_mask(observed, w, h)
    np.testing.assert_array_equal(region["mask"], expected_mask)
    if np.count_nonzero(expected_mask):
        processor = observed["processor_roi_before_mask"]
        assert region["extent"] == tuple(processor["image_origin"] + processor["dimensions"])
    else:
        assert region["extent"][2] * region["extent"][3] == 0
    prepared = ij.prepare_operation("gaussian_blur_imagej", command.target, command.parameters())
    actual = prepared.preview.dataset.numpy("YX")
    expected_words = original_words.copy()
    filtered_words = np.array([int(value, 16) for value in
        observed["gaussian"]["after_mask_reset_f32_words"]], dtype=np.uint32).reshape(h, w)
    selected = expected_mask != 0
    expected_words[selected] = filtered_words[selected]
    # Both selected and unselected comparisons are exact F32 words. In particular
    # Java's recorded outside-ROI boundary writes are not inherited by the target.
    np.testing.assert_array_equal(actual.view(np.uint32), expected_words)
    assert before.shares_data(document.snapshot()) and document.undo_steps == steps
    assert document.snapshot().selection.path.commands == request["commands"]
    np.testing.assert_array_equal(document.snapshot().dataset.numpy("YX").view(np.uint32), original_words)
