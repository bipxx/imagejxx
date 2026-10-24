"""Exact typed/anisotropic path Gaussian comparisons, including RGB samples."""
import gzip
import json
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest

from test_path_rank_filter_commands import sample_array, selection_mask

ROOT = Path(__file__).resolve().parents[2]
CASES = json.loads(gzip.decompress(
    (ROOT/"tests/fixtures/path-typed-gaussian-targets-imagej154r.json.gz").read_bytes()))["cases"]


@pytest.mark.parametrize("row",CASES,ids=lambda row:row["id"])
def test_exact_typed_anisotropic_gaussian_path_words(row):
    request,observed = row["request"],row["observation"]
    original = sample_array(observed["input"])
    axes = "YXS" if request["pixel_type"] == "rgb" else "YX"
    calibration = ij.SpatialCalibration(spacing=(.5,1,3),origin=(11,-7,19),unit="um")
    owner = ij.Document(ij.Dataset(original,axes=axes,calibration=calibration))
    owner.select(ij.Selection.from_path(ij.ImageJCompositePath(request["commands"])))
    before,history = owner.snapshot(),owner.undo_steps
    if request["sigma_x"] == request["sigma_y"]:
        options = ij.GaussianBlurCommandOptions(request["sigma_x"],False)
    else:
        assert (request["sigma_x"],request["sigma_y"]) == (2,1)
        options = ij.GaussianBlurCommandOptions(1,True)
    command = ij.resolve_gaussian_command(before,options=options)
    assert (command.sigma_x,command.sigma_y) == (request["sigma_x"],request["sigma_y"])
    mask = selection_mask(observed,request["width"],request["height"])
    region = ij.gaussian_command_region(command.target)
    np.testing.assert_array_equal(region["mask"],mask.astype(np.uint8)*255)
    extent = observed["processor_roi_before_mask"]
    assert region["extent"] == tuple(extent["image_origin"]+extent["dimensions"])
    proposal = ij.prepare_operation("gaussian_blur_imagej",command.target,command.parameters())
    actual = proposal.preview.dataset.numpy(axes)
    expected = original.copy()
    expected[mask] = sample_array(observed["after_mask_reset"])[mask]
    word_type = np.uint32 if request["pixel_type"] == "f32" else actual.dtype
    np.testing.assert_array_equal(actual.view(word_type),expected.view(word_type))
    assert owner.snapshot().shares_data(before) and owner.undo_steps == history
    assert owner.snapshot().selection.path.commands == request["commands"]
    assert owner.snapshot().dataset.numpy(axes).tobytes() == original.tobytes()
