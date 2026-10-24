"""Exact typed rank-path comparisons against independent pinned Java outputs."""
import gzip
import json
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = json.loads(gzip.decompress(
    (ROOT / "tests/fixtures/path-rank-targets-imagej154r.json.gz").read_bytes()))
CASES = FIXTURE["cases"]


def sample_array(value):
    if value["pixel_type"] == "rgb":
        w,h = value["dimensions"]
        packed = np.array([int(word,16) for word in value["sample_words"]],dtype=np.uint32).reshape(h,w)
        return np.stack([(packed>>16)&255,(packed>>8)&255,packed&255],axis=-1).astype(np.uint8)
    dtype = {"u8": np.uint8, "u16": np.uint16, "f32": np.uint32}[value["pixel_type"]]
    w, h = value["dimensions"]
    result = np.array([int(word, 16) for word in value["sample_words"]], dtype=dtype).reshape(h, w)
    return result.view(np.float32) if value["pixel_type"] == "f32" else result


def selection_mask(observed, width, height):
    mask = np.zeros((height, width), dtype=np.bool_)
    source = observed["roi_mask"]
    if source is None:
        return mask
    x, y = source["image_origin"]
    w, h = source["dimensions"]
    values = bytes.fromhex(source["u8_hex"])
    for j in range(max(y, 0), min(y+h, height)):
        for i in range(max(x, 0), min(x+w, width)):
            mask[j, i] = bool(values[(j-y)*w+i-x])
    return mask


def resolved(document, request):
    source = document.snapshot()
    operation, radius = request["operation"], request["radius"]
    if operation == "remove_nans":
        return ij.resolve_remove_nans_command(source, options=ij.RemoveNaNsCommandOptions(radius))
    if operation == "outliers":
        return ij.resolve_remove_outliers_command(source, options=ij.RemoveOutliersCommandOptions(
            radius, request["threshold"], "bright" if request["which_outliers"] == 0 else "dark"))
    if operation == "top_hat":
        return ij.resolve_top_hat_command(source, options=ij.TopHatCommandOptions(
            radius, request["light_background"], request["dont_subtract"]))
    return ij.resolve_rank_command(source, options=ij.RankFilterCommandOptions(
        "median" if operation == "despeckle" else operation, radius))


@pytest.mark.parametrize("row", CASES, ids=[row["id"] for row in CASES])
def test_pinned_typed_rank_path_pixels_and_mask(row):
    request, observed = row["request"], row["observation"]
    original = sample_array(observed["input"])
    policy = {"finite": ij.FloatingSamplePolicy.FINITE_ONLY,
              "quiet_nan": ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32,
              "ieee": ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32}[request["input_pattern"]]
    document = ij.Document(ij.Dataset(original, axes="YX", sample_policy=policy))
    document.select(ij.Selection.from_path(ij.ImageJCompositePath(request["commands"])))
    view = document.snapshot().display
    view.properties = {"lut": "Inverted" if request["inverted_lut"] else "Grays"}
    document.set_display(view)
    before, undo = document.snapshot(), document.undo_steps
    command = resolved(document, request)
    expected_mask = selection_mask(observed, request["width"], request["height"])
    np.testing.assert_array_equal(command.region()["mask"], expected_mask.astype(np.uint8)*255)
    prepared = ij.prepare_operation(command.operation, command.target, command.parameters())
    actual = prepared.preview.dataset.numpy("YX")
    expected = original.copy()
    expected[expected_mask] = sample_array(observed["after_mask_reset"])[expected_mask]
    # Viewing integer words avoids float NaN equality/canonicalization and
    # preserves both zero signs and every outside-target NaN payload exactly.
    dtype = {"u8": np.uint8, "u16": np.uint16, "f32": np.uint32}[request["pixel_type"]]
    np.testing.assert_array_equal(actual.view(dtype), expected.view(dtype))
    assert before.shares_data(document.snapshot()) and document.undo_steps == undo
    assert document.snapshot().selection.path.commands == request["commands"]
    assert document.snapshot().dataset.numpy("YX").tobytes() == original.tobytes()
