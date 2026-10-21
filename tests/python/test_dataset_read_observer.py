"""Public native read observation without provider callbacks or private hooks.

Expected counts follow actual public request entries: XY requests one plane;
an orthogonal extraction requests one region per Z plane. Descriptor access and
rejected coordinates do not enter the provider. Concurrent assertions use a
quiescent final snapshot, not a transactional pair while reads are in flight.
"""

import copy
from concurrent.futures import ThreadPoolExecutor
import gc
import threading

import imagejxx as ij
import numpy as np
import pytest


def counts(observer):
    value = observer.counts()
    assert isinstance(value, ij.DatasetReadCounts)
    assert type(value.plane_reads) is int and value.plane_reads >= 0
    assert type(value.region_reads) is int and value.region_reads >= 0
    assert type(value.overflowed) is bool
    return value.plane_reads, value.region_reads, value.overflowed


def scientific_source():
    pixels = np.arange(2 * 2 * 3 * 4 * 5, dtype=np.uint16).reshape(2, 2, 3, 4, 5)
    calibration = ij.SpatialCalibration(
        spacing=(0.5, 2, 3), origin=(10, 20, 30), unit="um", frame_interval=0.25
    )
    return pixels, ij.Dataset(pixels, axes="TCZYX", calibration=calibration)


def test_observer_counts_real_plane_region_and_orthogonal_entries():
    pixels, source = scientific_source()
    observer = ij.DatasetReadObserver()
    observed = source.observe_reads(observer)
    assert counts(observer) == (0, 0, False)

    np.testing.assert_array_equal(observed.plane("xy", 2, t=1, c=1), pixels[1, 1, 2])
    assert counts(observer) == (1, 0, False)
    np.testing.assert_array_equal(
        observed.region(1, 1, 3, 2, t=1, c=0, z=1), pixels[1, 0, 1, 1:3, 1:4]
    )
    # Provider-internal fallback reads are not a second observed plane entry.
    assert counts(observer) == (1, 1, False)
    np.testing.assert_array_equal(observed.plane("xz", 1, t=1, c=1), pixels[1, 1, :, 1, :])
    assert counts(observer) == (1, 4, False)
    np.testing.assert_array_equal(observed.plane("yz", 2, t=0, c=1), pixels[0, 1, :, :, 2])
    assert counts(observer) == (1, 7, False)


def test_invalid_coordinates_and_metadata_do_not_acquire_pixels():
    _, source = scientific_source()
    observer = ij.DatasetReadObserver()
    observed = source.observe_reads(observer)
    assert observed.axes == "TCZYXS"
    assert observed.shape == (2, 2, 3, 4, 5, 1)
    assert observed.axis_sizes == {"T": 2, "C": 2, "Z": 3, "Y": 4, "X": 5, "S": 1}
    assert observed.dtype == np.dtype(np.uint16)
    assert observed.sample_policy == ij.FloatingSamplePolicy.FINITE_ONLY
    assert observed.calibration.spacing == [0.5, 2, 3]
    assert observed.plane_geometry("xy", 1)["origin"] == [10, 20, 33]
    assert observed.shares_pixels(source) and source.shares_pixels(observed)
    calibrated = observed.with_calibration(ij.SpatialCalibration(spacing=(2, 3, 4), unit="nm"))
    assert calibrated.shares_pixels(observed) and calibrated.calibration.unit == "nm"
    owner = ij.Document(observed, "Observed metadata only")
    snapshot = owner.snapshot()
    assert snapshot.dataset.shape == observed.shape and not snapshot.dirty
    assert owner.undo_steps == owner.redo_steps == 0
    assert owner.history().undo_steps == 0
    assert counts(observer) == (0, 0, False)

    for action in (
        lambda: observed.plane("xy", 3),
        lambda: observed.plane("xz", 4),
        lambda: observed.plane("yz", 5),
        lambda: observed.plane(t=2),
        lambda: observed.plane(c=-1),
        lambda: observed.region(-1, 0, 1, 1),
        lambda: observed.region(4, 0, 2, 1),
        lambda: observed.region(0, 0, 0, 1),
        lambda: observed.region(0, 0, 1, 1, z=3),
        lambda: observed.with_plane(np.zeros((4, 5), np.uint16), z=3),
    ):
        with pytest.raises(IndexError):
            action()
        assert counts(observer) == (0, 0, False)
    with pytest.raises(ValueError):
        observed.plane("not-a-plane")
    with pytest.raises(ValueError):
        observed.with_plane(np.zeros((4, 4), np.uint16))
    for invalid in (None, object(), True):
        with pytest.raises(TypeError):
            source.observe_reads(invalid)
    assert counts(observer) == (0, 0, False)


def test_counts_snapshots_are_readonly_and_observer_handles_share_state():
    pixels, source = scientific_source()
    observer = ij.DatasetReadObserver()
    constructor_copy = ij.DatasetReadObserver(observer)
    protocol_copy = copy.copy(observer)
    initial = observer.counts()
    for name, value in (("plane_reads", 7), ("region_reads", 9), ("overflowed", True)):
        with pytest.raises(AttributeError):
            setattr(initial, name, value)
    observed = source.observe_reads(constructor_copy)
    np.testing.assert_array_equal(observed.plane(), pixels[0, 0, 0])
    assert counts(observer) == counts(constructor_copy) == counts(protocol_copy) == (1, 0, False)
    assert (initial.plane_reads, initial.region_reads, initial.overflowed) == (0, 0, False)
    independent = ij.DatasetReadObserver()
    assert counts(independent) == (0, 0, False)
    del observer, constructor_copy
    gc.collect()
    observed.region(0, 0, 1, 1)
    assert counts(protocol_copy) == (1, 1, False)


def test_deduped_observation_chains_keep_storage_and_lifetime():
    pixels, source = scientific_source()
    first, second = ij.DatasetReadObserver(), ij.DatasetReadObserver()
    retained_first = copy.copy(first)
    retained_second = ij.DatasetReadObserver(second)
    once = source.observe_reads(first)
    twice = once.observe_reads(second)
    chain = twice.observe_reads(ij.DatasetReadObserver(first))
    calibrated = chain.with_calibration(chain.calibration)
    assert all(item.shares_pixels(source) for item in (once, twice, chain, calibrated))
    # The new Dataset does not instrument reads made through the old Dataset.
    np.testing.assert_array_equal(source.plane(), pixels[0, 0, 0])
    assert counts(first) == counts(second) == (0, 0, False)
    del source, first, second, once, twice, chain
    gc.collect()
    np.testing.assert_array_equal(calibrated.plane("xy", 2, t=1, c=0), pixels[1, 0, 2])
    assert counts(retained_first) == counts(retained_second) == (1, 0, False)
    np.testing.assert_array_equal(calibrated.region(1, 1, 2, 2), pixels[0, 0, 0, 1:3, 1:3])
    assert counts(retained_first) == counts(retained_second) == (1, 1, False)
    del calibrated
    gc.collect()
    assert counts(retained_first) == counts(retained_second) == (1, 1, False)


def test_public_observer_counts_concurrent_native_reads_once():
    pixels, source = scientific_source()
    observer = ij.DatasetReadObserver()
    observed = source.observe_reads(observer)
    workers, iterations = 4, 16
    gate = threading.Barrier(workers + 1)

    def read(index):
        local = observed.with_calibration(observed.calibration)
        t, c = index % 2, index // 2
        total = 0
        gate.wait(timeout=5)
        for iteration in range(iterations):
            z = iteration % 3
            plane = local.plane("xy", z, t=t, c=c)
            region = local.region(1, 1, 2, 2, t=t, c=c, z=z)
            np.testing.assert_array_equal(plane, pixels[t, c, z])
            np.testing.assert_array_equal(region, pixels[t, c, z, 1:3, 1:3])
            total += int(plane.sum()) + int(region.sum())
        return total

    with ThreadPoolExecutor(max_workers=workers) as pool:
        futures = [pool.submit(read, index) for index in range(workers)]
        gate.wait(timeout=5)
        actual = [future.result(timeout=10) for future in futures]
    expected = [sum(int(pixels[index % 2, index // 2, iteration % 3].sum()) +
                    int(pixels[index % 2, index // 2, iteration % 3, 1:3, 1:3].sum())
                    for iteration in range(iterations)) for index in range(workers)]
    assert actual == expected
    assert counts(observer) == (workers * iterations, workers * iterations, False)


def test_observer_preserves_immutable_plane_replacements():
    pixels = np.arange(3 * 4 * 5, dtype=np.uint16).reshape(3, 4, 5)
    source = ij.Dataset(pixels, axes="ZYX")
    observer = ij.DatasetReadObserver()
    observed = source.observe_reads(observer)
    changed = observed.with_plane(np.full((4, 5), 77, np.uint16), z=1)
    assert counts(observer) == (0, 0, False)
    assert observed.shares_pixels(source) and not changed.shares_pixels(observed)
    np.testing.assert_array_equal(changed.plane("xy", 1), 77)
    np.testing.assert_array_equal(observed.plane("xy", 1), pixels[1])
    np.testing.assert_array_equal(changed.plane("xy", 0), pixels[0])
    assert counts(observer) == (3, 0, False)
    calibrated = changed.with_calibration(ij.SpatialCalibration(spacing=(2, 3, 4)))
    assert calibrated.shares_pixels(changed)
    assert counts(observer) == (3, 0, False)
    np.testing.assert_array_equal(calibrated.plane("xy", 2), pixels[2])
    assert counts(observer) == (4, 0, False)


def test_native_edits_undo_redo_and_retained_snapshots_keep_observation():
    pixels = np.arange(3 * 4 * 5, dtype=np.uint16).reshape(3, 4, 5)
    observer = ij.DatasetReadObserver()
    observed = ij.Dataset(pixels, axes="ZYX").observe_reads(observer)
    owner = ij.Document(observed, "Observed native history")
    original = owner.snapshot()
    assert counts(observer) == (0, 0, False)
    edit = owner.begin_edit()
    edit.replace_plane(np.full((4, 5), 77, np.uint16), z=1)
    edit.set_property("owned_edit", True)
    owner.commit(edit)
    changed = owner.snapshot()
    assert changed.revisions.pixels == changed.revisions.metadata == 1
    assert owner.undo_steps == 1 and changed.dirty and not changed.shares_data(original)
    before_read = counts(observer)
    np.testing.assert_array_equal(changed.dataset.plane("xy", 1), 77)
    assert counts(observer) == (before_read[0] + 1, before_read[1], False)

    owner.undo()
    assert not owner.snapshot().dirty and owner.redo_steps == 1
    before_read = counts(observer)
    np.testing.assert_array_equal(owner.snapshot().dataset.plane("xy", 1), pixels[1])
    np.testing.assert_array_equal(changed.dataset.plane("xy", 1), 77)
    assert counts(observer) == (before_read[0] + 2, before_read[1], False)
    owner.redo()
    owner.mark_saved()
    assert not owner.snapshot().dirty and owner.undo_steps == 1
    retained = owner.snapshot()
    del owner, observed, original, changed, edit
    gc.collect()
    before_read = counts(observer)
    np.testing.assert_array_equal(retained.dataset.region(1, 1, 2, 2, z=1), 77)
    assert counts(observer) == (before_read[0], before_read[1] + 1, False)
    assert retained.metadata["properties"]["owned_edit"] is True


def test_observation_keeps_quiet_nan_bits_policy_and_eager_editability():
    bits = np.array([
        0x3F800000, 0x7FC01234, 0x80000000, 0xFFC05678, 0x40000000, 0x40400000,
        0x40800000, 0x40A00000, 0x7FC0ABCD, 0x00000000, 0xBF800000, 0x40C00000,
    ], dtype=np.uint32).reshape(2, 2, 3)
    policy = ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32
    source = ij.Dataset(bits.view(np.float32), axes="ZYX", sample_policy=policy)
    observer = ij.DatasetReadObserver()
    observed = source.observe_reads(observer)
    assert observed.sample_policy == policy and observed.dtype == np.dtype(np.float32)
    assert observed.shares_pixels(source) and counts(observer) == (0, 0, False)
    np.testing.assert_array_equal(observed.plane("xy", 0).view(np.uint32), bits[0])
    np.testing.assert_array_equal(observed.region(1, 0, 2, 2, z=1).view(np.uint32), bits[1, :, 1:])
    assert counts(observer) == (1, 1, False)
    calibrated = observed.with_calibration(ij.SpatialCalibration(spacing=(2, 3, 4), unit="um"))
    assert calibrated.sample_policy == policy and calibrated.shares_pixels(observed)
    assert counts(observer) == (1, 1, False)

    replacement_bits = np.array([
        0x7FC01111, 0x3F800000, 0x80000000, 0x40000000, 0xFFC02222, 0x40400000,
    ], dtype=np.uint32).reshape(2, 3)
    replacement = replacement_bits.view(np.float32)
    replaced = calibrated.with_plane(replacement, z=0)
    assert replaced.sample_policy == policy and counts(observer) == (1, 1, False)
    owner = ij.Document(replaced, "Observed quiet NaN")
    ordinary = source.with_calibration(calibrated.calibration).with_plane(replacement, z=0)
    ordinary_owner = ij.Document(ordinary, "Ordinary quiet NaN")
    refusals = []
    invalid_bits = bits[0].copy()
    invalid_bits[0, 0] = 0x7F800001  # Signaling NaN is outside the quiet-only policy.
    for candidate_owner in (ordinary_owner, owner):
        before = candidate_owner.snapshot()
        before_counts = counts(observer)
        edit = candidate_owner.begin_edit()
        # Both observed and ordinary owners honor their explicit quiet-NaN
        # policy and atomically refuse a signaling payload outside that policy.
        with pytest.raises(ValueError) as refusal:
            edit.replace_plane(invalid_bits.view(np.float32), z=0)
        refusals.append(refusal.value)
        after = candidate_owner.snapshot()
        assert edit.active and after.shares_data(before)
        assert edit.preview().dataset.shares_pixels(before.dataset)
        assert not after.dirty and candidate_owner.undo_steps == candidate_owner.redo_steps == 0
        assert (after.revisions.pixels, after.revisions.metadata,
                after.revisions.selection, after.revisions.display) == (
                    before.revisions.pixels, before.revisions.metadata,
                    before.revisions.selection, before.revisions.display)
        assert after.dataset.sample_policy == policy
        history = candidate_owner.history()
        assert history.pixels.exact and history.pixels.current_bytes == 96
        assert history.pixels.retained_bytes == 96 and history.pixels.obsolete_bytes == 0
        assert counts(observer) == before_counts
    assert type(refusals[0]) is type(refusals[1]) is ValueError
    assert str(refusals[0]) == str(refusals[1])

    # Dataset.with_plane honors the source's explicit quiet-NaN admission policy;
    # publish that immutable Dataset through the still-active observed transaction.
    accepted = owner.snapshot().dataset.with_plane(bits[0].view(np.float32), z=0)
    assert accepted.sample_policy == policy and counts(observer) == (1, 1, False)
    edit.replace_plane(bits[0].view(np.float32), z=0)
    assert edit.preview().dataset.sample_policy == accepted.sample_policy
    owner.commit(edit)
    assert not edit.active and owner.undo_steps == 1
    before_read = counts(observer)
    np.testing.assert_array_equal(owner.snapshot().dataset.plane("xy", 0).view(np.uint32), bits[0])
    assert counts(observer) == (before_read[0] + 1, before_read[1], False)
    owner.undo()
    before_read = counts(observer)
    np.testing.assert_array_equal(owner.snapshot().dataset.plane("xy", 0).view(np.uint32), replacement_bits)
    assert counts(observer) == (before_read[0] + 1, before_read[1], False)
    owner.redo()
    assert owner.snapshot().dataset.sample_policy == policy
