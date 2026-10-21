"""Core rank bytes against independently captured, owned ImageJ workload files.

These twelve cases establish pixel/target/publication correctness, not latency,
renderer, platform or complete rank-family acceptance. Expected filtered samples
come only from the retained original ImageJ rectangle outputs and the separately
qualified source-byte mask adapter. No product filter generates an expectation.
Both evidence roots are required explicitly; missing evidence is an error.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path, PurePosixPath

import imagejxx as ij
import numpy as np
import pytest


SHAPE = (2, 3, 4, 128, 192, 1)
POSITIONS = tuple((t, c, z) for t in range(2) for c in range(3) for z in range(4))
PREVIEW_POSITION = (1, 1, 2)
METHODS = ("minimum", "maximum", "median", "mean")
DTYPES = ("uint8", "uint16", "float32")
CASES = tuple((method, dtype) for method in METHODS for dtype in DTYPES)
CASE_IDS = tuple(f"{method}-{dtype}" for method, dtype in CASES)
EXPECTED_COLLECTION_COUNT = 12
EXPECTED_COLLECTION_IDS = tuple(
    f"test_rank_benchmark_exact_masked_pixels[{case}]" for case in CASE_IDS
)

FIXTURE_PREFIX = PurePosixPath("build/p06f-001/benchmark-observer-prep001/fixture001")
REFERENCE_PREFIX = PurePosixPath("build/references/rank-benchmark-imagej154r-001")
FIXTURE_MANIFEST_PIN = (4913, "0ea7a9dd3344ee0e7a3d619ac150a783599130f1c3e6747ea21e135ea634f3a3")
EXPECTATIONS_PIN = (143656, "d1a8144cd1dc035eaba437328aa68f32a3cf696142ce313e920e21f9179bd311")
RESULT_PIN = (5346, "526558166083ef35d050b98cefd78ff020f9142894cc8d018f32705af691a1c8")
INPUTS_PIN = (9161, "c9c1e027734bc8f4cb81419fc4b55ce1beb825edf5967b731a92e78636bcd408")
RAW_PIN = (168117, "98d0529a9324568f8755d8a736bbff2c9648199db3a42c620dce181977105c8b")
SOURCE_PINS = {
    "uint8": (589824, "c18cd10081861cd9f69e1dd65e7ec0436f6e2eeae110973a39274c2e00c443a2"),
    "uint16": (1179648, "0abab605727d65fc0f3c154e88de3d9e5a1226c4f2d2679a25c5166288451a02"),
    "float32": (2359296, "600c6e0149453742da3910789caaf5502509a53571407f91d149eb53dd1d4dcd"),
}
ROI_PIN = (3072, "7e2ff2cc0daf4f627e2ad9437dc88bfca7628ee4d44d105c06f80db613a5c9f5")
CALIBRATION = {
    "spacing": [.5, 1.25, 2.5], "origin": [11., -7., 19.],
    "direction": [1., 0., 0., 0., 1., 0., 0., 0., 1.], "unit": "um",
    "frame_interval": .75, "time_origin": 2.25, "time_unit": "s",
}
NP_TYPES = {"uint8": np.dtype("u1"), "uint16": np.dtype("<u2"), "float32": np.dtype("<f4")}


def _sha(raw):
    return hashlib.sha256(raw).hexdigest()


def _required_root(name):
    value = os.environ.get(name)
    assert value, f"Required evidence root is absent: {name}"
    root = Path(value).resolve(strict=True)
    assert root.is_dir(), (name, root)
    return root


def _pinned(path, pin, *, maximum=3 * 1024 * 1024):
    count, digest = pin
    assert type(count) is int and 0 < count <= maximum
    assert path.is_file() and path.stat().st_size == count, path
    raw = path.read_bytes()
    assert len(raw) == count and _sha(raw) == digest, path
    return raw


def _member(root, prefix, descriptor):
    declared = PurePosixPath(descriptor["path"])
    assert not declared.is_absolute() and ".." not in declared.parts
    relative = declared.relative_to(prefix)
    assert relative.parts
    return _pinned(root.joinpath(*relative.parts),
                   (descriptor["bytes"], descriptor["sha256"]))


@pytest.fixture(scope="session")
def benchmark_evidence():
    fixture_root = _required_root("IMAGEJXX_RANK_BENCHMARK_FIXTURE_ROOT")
    reference_root = _required_root("IMAGEJXX_RANK_BENCHMARK_REFERENCE_ROOT")
    fixture_bytes = _pinned(fixture_root / "fixtures.json", FIXTURE_MANIFEST_PIN)
    fixtures = json.loads(fixture_bytes)
    expectations = json.loads(_pinned(reference_root / "expectations.json", EXPECTATIONS_PIN))
    result = json.loads(_pinned(reference_root / "result001.json", RESULT_PIN))
    assert fixtures["kind"] == "materialized_owned_synthetic_inputs_only"
    assert fixtures["workload_id"] == "G042.rank-imagej.synthetic-czt-roi.001"
    assert fixtures["fixture_spec"]["shape_tczyxs"] == list(SHAPE)
    assert fixtures["fixture_spec"]["active_tcz"] == list(PREVIEW_POSITION)
    assert fixtures["fixture_spec"]["calibration"] == CALIBRATION
    assert fixtures["method_order"] == list(METHODS) and fixtures["radius"] == 2.0
    assert fixtures["target"]["scope"] == "Ranges"
    assert [fixtures["target"][axis] for axis in ("t", "c", "z")] == [[0, 2, 1], [0, 3, 1], [0, 4, 1]]
    assert fixtures["target"]["execution"] == "Planes"
    assert fixtures["target"]["destination"] == "InPlace"
    assert fixtures["target"]["preview_tcz"] == list(PREVIEW_POSITION)
    assert expectations["kind"] == "actual_imagej_pixel_expectations_with_explicit_owned_mask_adapter"
    assert expectations["imagej_version"] == "1.54r"
    assert expectations["imagej_commit"] == "62db90a269cccc1ac79f658e4602043214ed055b"
    assert expectations["input_manifest"]["sha256"] == FIXTURE_MANIFEST_PIN[1]
    assert expectations["input_manifest"]["bytes"] == len(fixture_bytes)
    assert (expectations["cell_count"], expectations["plane_count"], expectations["variants_per_plane"]) == (12, 288, 2)
    assert [(cell["dtype"], cell["method"]) for cell in expectations["cells"]] == [
        (dtype, method) for dtype in DTYPES for method in METHODS
    ]
    assert result["state"] == "pass_actual_imagej_expectations_and_independent_reader_only"
    assert result["source_tool_input_preservation"] is True
    assert result["product_or_benchmark_timing_executed"] is False
    assert result["acceptance"] == dict.fromkeys(("product", "benchmark", "platform", "family", "release"), False)
    assert all(result["processes"][kind]["exit_code"] == 0 for kind in ("compile", "capture", "reader"))
    assert (result["expectations"]["bytes"], result["expectations"]["sha256"]) == EXPECTATIONS_PIN
    before_inputs = _pinned(reference_root / "inputs-before001.json", INPUTS_PIN)
    assert _pinned(reference_root / "inputs-after001.json", INPUTS_PIN) == before_inputs
    lineage = json.loads(before_inputs)
    assert lineage["reference"]["source_archive_sha256"] == "09e31a88191d10c7f79c607cb6bb62342476bde733c5d932d5825fd553796665"
    assert lineage["known_behavioral_source_members"]["ij/plugin/filter/RankFilters.java"]["sha256"] == "284e7647e6619a3cc62887c57992056ccfd1dd99661868571fff005afff7d6b7"
    raw = _member(reference_root, REFERENCE_PREFIX, expectations["raw_observations"])
    assert (len(raw), _sha(raw)) == RAW_PIN
    rows = [json.loads(line) for line in raw.splitlines() if line.strip()]
    assert len(rows) == 302 and rows[0] == expectations["header"]
    assert rows[-1] == {"kind": "complete", "cells": 12, "planes": 288, "output_variants": 2}
    assert rows[0]["rank_declaring_class"] == "ij.plugin.filter.RankFilters"
    assert rows[0]["line_radii"] == [-1, 1, -2, 2, -2, 2, -2, 2, -1, 1, 21, 2]
    assert rows[0]["threads"] == 1 and rows[0]["radius"] == 2.0
    sources = {}
    for descriptor, dtype in zip(fixtures["sources"], DTYPES, strict=True):
        assert descriptor["dtype"].lower() == dtype
        assert descriptor["shape"] == list(SHAPE) and descriptor["axes"] == "TCZYXS"
        assert descriptor["byte_order"] == "little" and descriptor["sample_policy"] == "FiniteOnly"
        assert (descriptor["bytes"], descriptor["sha256"]) == SOURCE_PINS[dtype]
        sources[dtype] = _member(fixture_root, FIXTURE_PREFIX, descriptor)
    packed = _member(fixture_root, FIXTURE_PREFIX, fixtures["roi"])
    assert (len(packed), _sha(packed)) == ROI_PIN
    # Decode owned input geometry only. This is never a filtering algorithm.
    mask = np.unpackbits(np.frombuffer(packed, dtype=np.uint8), bitorder="big").reshape(128, 192).astype(bool)
    y, x = np.indices((128, 192))
    rectangle = (16 <= x) & (x < 176) & (12 <= y) & (y < 116)
    hole = (64 <= x) & (x < 96) & (40 <= y) & (y < 72)
    assert np.array_equal(mask, rectangle & ~hole)
    assert (int(mask.sum()), int(hole.sum()), int((~rectangle).sum())) == (15616, 1024, 7936)
    assert fixtures["roi"]["true_write_members_per_plane"] == 15616
    assert fixtures["roi"]["full_target_true_write_members"] == 374784
    assert len(EXPECTED_COLLECTION_IDS) == EXPECTED_COLLECTION_COUNT == 12
    return {
        "root": reference_root, "fixtures": fixtures, "cells": expectations["cells"],
        "raw_rows": rows, "sources": sources, "packed": packed, "mask": mask,
        "outside": ~rectangle, "hole": hole,
    }


def _calibration(value):
    return {key: getattr(value, key) for key in CALIBRATION}


def _display(value):
    return {key: getattr(value, key) for key in (
        "t", "c", "crosshair", "orthogonal", "linked_zoom", "zoom", "plane_zoom", "pan", "properties", "revision"
    )}


def _positions(target):
    return tuple((point.t, point.c, point.z) for point in (
        target.position(index) for index in range(target.position_count)
    ))


def _dataset_bytes(dataset, dtype):
    assert dataset.shape == SHAPE and dataset.dtype == NP_TYPES[dtype]
    assert dataset.sample_policy == ij.FloatingSamplePolicy.FINITE_ONLY
    assert _calibration(dataset.calibration) == CALIBRATION
    array = dataset.numpy("TCZYXS")
    assert array.shape == SHAPE and array.dtype == NP_TYPES[dtype]
    return array.tobytes(order="C")


def _assert_source(owner, before, source, dtype):
    now = owner.snapshot()
    assert now.shares_data(before) and now.data_identity == before.data_identity
    assert now.revisions.matches(before.revisions, ij.Change.ALL)
    assert now.dirty == before.dirty is False
    assert owner.undo_steps == owner.redo_steps == 0
    assert now.metadata == before.metadata
    assert now.selection == before.selection and now.previous_selection == before.previous_selection
    assert _display(now.display) == _display(before.display)
    assert _dataset_bytes(now.dataset, dtype) == source


def _assert_planes(actual, original, expected, dtype, *, contained=False):
    plane_bytes = 128 * 192 * NP_TYPES[dtype].itemsize
    assert len(actual) == len(original) == len(expected) == 24 * plane_bytes
    for index, position in enumerate(POSITIONS):
        start, stop = index * plane_bytes, (index + 1) * plane_bytes
        wanted = expected if not contained or position == PREVIEW_POSITION else original
        assert actual[start:stop] == wanted[start:stop], (dtype, position, "contained" if contained else "full")


@pytest.mark.parametrize("case", CASES, ids=CASE_IDS)
def test_rank_benchmark_exact_masked_pixels(case, benchmark_evidence):
    method, dtype = case
    evidence = benchmark_evidence
    cell_index = DTYPES.index(dtype) * len(METHODS) + METHODS.index(method)
    cell = evidence["cells"][cell_index]
    assert (cell["dtype"], cell["method"], cell["radius"]) == (dtype, method, 2.0)
    assert cell["shape"] == list(SHAPE) and cell["axes"] == "TCZYXS"
    original = evidence["sources"][dtype]
    expected = _member(evidence["root"], REFERENCE_PREFIX, cell["masked_output"])
    rectangle = _member(evidence["root"], REFERENCE_PREFIX, cell["rectangle_output"])
    assert cell["preview_tcz"] == list(PREVIEW_POSITION) and cell["preview_plane_index"] == 18
    assert cell["changed_true_write_members"] > 0
    assert len(cell["planes"]) == 24
    plane_bytes = 128 * 192 * NP_TYPES[dtype].itemsize
    for index, position in enumerate(POSITIONS):
        row = cell["planes"][index]
        observed = evidence["raw_rows"][1 + cell_index * 25 + index]
        start, stop = index * plane_bytes, (index + 1) * plane_bytes
        assert (row["index"], row["t"], row["c"], row["z"], row["offset_bytes"], row["bytes"]) == (index, *position, start, plane_bytes)
        assert (observed["index"], observed["t"], observed["c"], observed["z"]) == (index, *position)
        assert observed["cell"] == f"{dtype}.{method}" and observed["actual_rank_returned"] is True
        assert observed["roi_preserved"] is True and observed["mask_was_null"] is True
        assert observed["restored_member_count"] == 8960
        assert _sha(original[start:stop]) == row["source_sha256"] == observed["source_before_sha256"] == observed["source_after_sha256"]
        assert _sha(rectangle[start:stop]) == row["rectangle_sha256"] == observed["rectangle_sha256"]
        assert _sha(expected[start:stop]) == row["masked_sha256"] == observed["masked_sha256"]
    assert _sha(expected[18 * plane_bytes:19 * plane_bytes]) == cell["preview_masked_sha256"]
    source_values = np.frombuffer(original, dtype=NP_TYPES[dtype]).reshape(SHAPE)
    expected_values = np.frombuffer(expected, dtype=NP_TYPES[dtype]).reshape(SHAPE)
    # Explicitly retain every hole/exterior sample, in addition to whole-plane bytes.
    for position in POSITIONS:
        for excluded in (evidence["hole"], evidence["outside"]):
            assert expected_values[position][..., 0][excluded].tobytes() == source_values[position][..., 0][excluded].tobytes()
    owner = ij.Document(ij.Dataset(source_values.copy(), axes="TCZYXS",
                                  calibration=ij.SpatialCalibration(**CALIBRATION)),
                        "Owned rank benchmark source")
    # from_mask is the public native packed-area constructor in this binding.
    owner.select(ij.Selection.from_mask(evidence["mask"].astype(np.uint8)))
    owner.mark_saved()
    view = owner.snapshot().display
    view.t, view.c = 1, 1
    view.crosshair = [view.crosshair[0], view.crosshair[1], 2]
    owner.set_display(view)
    before = owner.snapshot()
    assert np.packbits(before.selection.mask((128, 192)) != 0, bitorder="big").tobytes() == evidence["packed"]
    app = ij.Application()
    app.adopt(owner)
    try:
        options = ij.RankFilterCommandOptions(method, 2.0)
        preview_request = ij.TargetRequest(ij.TargetScope.POSITIONS,
            positions=[ij.PlanePosition(*PREVIEW_POSITION)],
            roi=ij.RoiPolicy.ACTIVE_SELECTION, mode=ij.ExecutionMode.PLANES,
            destination=ij.OutputDestination.IN_PLACE)
        preview_command = ij.resolve_rank_command(before, preview_request, options)
        assert _positions(preview_command.target) == (PREVIEW_POSITION,)
        prepared_preview = app.prepare("rank_filter_imagej", preview_command.target, preview_command.parameters())
        assert prepared_preview.active and len(prepared_preview.outputs) == 1
        assert prepared_preview.outputs[0].skipped is False
        _assert_planes(_dataset_bytes(prepared_preview.preview.dataset, dtype), original, expected, dtype, contained=True)
        _assert_source(owner, before, original, dtype)
        del prepared_preview
        request = ij.TargetRequest(ij.TargetScope.RANGES,
            t=ij.AxisRange(0, 2, 1), c=ij.AxisRange(0, 3, 1), z=ij.AxisRange(0, 4, 1),
            roi=ij.RoiPolicy.ACTIVE_SELECTION, mode=ij.ExecutionMode.PLANES,
            destination=ij.OutputDestination.IN_PLACE)
        resolved = ij.resolve_rank_command(before, request, options)
        assert _positions(resolved.target) == POSITIONS
        prepared = app.prepare("rank_filter_imagej", resolved.target, resolved.parameters())
        assert prepared.active and len(prepared.outputs) == 24
        assert tuple((row.position.t, row.position.c, row.position.z) for row in prepared.outputs) == POSITIONS
        assert all(row.skipped is False for row in prepared.outputs)
        _assert_planes(_dataset_bytes(prepared.preview.dataset, dtype), original, expected, dtype)
        _assert_source(owner, before, original, dtype)
        published = app.publish(prepared)
        assert published.document is owner and published.document_id == owner.id
        assert published.created is False and not prepared.active
        assert app.document_ids == [owner.id] and app.active_document_id == owner.id
        after = owner.snapshot()
        _assert_planes(_dataset_bytes(after.dataset, dtype), original, expected, dtype)
        assert after.metadata == before.metadata and after.selection == before.selection
        assert _display(after.display) == _display(before.display)
        assert after.revisions.pixels == before.revisions.pixels + 1
        assert after.revisions.metadata == before.revisions.metadata
        assert after.revisions.selection == before.revisions.selection
        assert after.dirty and owner.undo_steps == 1 and owner.redo_steps == 0
        # The retained immutable capture still contains the exact input bytes.
        assert _dataset_bytes(before.dataset, dtype) == original
        owner.undo()
        assert _dataset_bytes(owner.snapshot().dataset, dtype) == original
        assert not owner.snapshot().dirty and owner.undo_steps == 0 and owner.redo_steps == 1
        owner.redo()
        _assert_planes(_dataset_bytes(owner.snapshot().dataset, dtype), original, expected, dtype)
        assert owner.undo_steps == 1 and owner.redo_steps == 0
    finally:
        app.job_scheduler.shutdown()

