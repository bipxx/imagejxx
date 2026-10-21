"""Independent opt-in Float32 source capability; no Document/session/UI credit.

These staged tests require the adopted native bindings. Missing APIs fail rather
than skipping. Float32 samples and their expected double encodings are frozen
IEEE constants; NaN equality and producer-calculated expected values are unused.
The sole Document fixture exercises actual retained-result/publication detach.
"""

import gc
import struct

import imagejxx as ij
import numpy as np
import pytest


_PAYLOAD32 = 0x7FC00001
_PAYLOAD64 = 0x7FF8000020000000
_FINITE_BITS = (0x00000000, 0x80000000, 0x3F800000, 0xC0200000, 0x00000001, 0x40400000)
_NAN_BITS = _FINITE_BITS[:-1] + (_PAYLOAD32,)
_TRANSPORT = (
    (0x00000000, 0x0000000000000000),
    (0x80000000, 0x8000000000000000),
    (0x3F800000, 0x3FF0000000000000),
    (0xC0200000, 0xC004000000000000),
    (0x00000001, 0x36A0000000000000),
    (0x00800000, 0x3810000000000000),
    (0x7F7FFFFF, 0x47EFFFFFE0000000),
    (0xFF7FFFFF, 0xC7EFFFFFE0000000),
    (_PAYLOAD32, _PAYLOAD64),
    (0xFFC12345, 0xFFF82468A0000000),
)


@pytest.fixture
def preserve():
    return ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32


def _from64(bits):
    return struct.unpack("<d", struct.pack("<Q", bits))[0]


def _bits64(value):
    return struct.unpack("<Q", struct.pack("<d", value))[0]


def _fixture(*, nan=True):
    return np.array(_NAN_BITS if nan else _FINITE_BITS, dtype=np.uint32).reshape(2, 3).view(np.float32).copy()


def _assert_raw(actual, expected):
    assert isinstance(actual, np.ndarray)
    assert actual.dtype == expected.dtype == np.dtype(np.float32)
    assert actual.shape == expected.shape
    np.testing.assert_array_equal(actual.view(np.uint32), expected.view(np.uint32))
    assert np.ascontiguousarray(actual).tobytes() == np.ascontiguousarray(expected).tobytes()


def _policy(data, expected):
    assert data.sample_policy == expected
    with pytest.raises(AttributeError):
        data.sample_policy = expected


def _calibration(cal):
    return {
        "spacing": tuple(cal.spacing),
        "origin": tuple(cal.origin),
        "direction": tuple(cal.direction),
        "unit": cal.unit,
        "frame_interval": cal.frame_interval,
        "time_origin": cal.time_origin,
        "time_unit": cal.time_unit,
    }


@pytest.mark.parametrize(
    "f32,f64", _TRANSPORT,
    ids=["positive-zero", "negative-zero", "one", "negative-2.5", "min-subnormal",
         "min-normal", "max-finite", "min-finite", "qnan-payload1", "negative-qnan-payload"],
)
def test_P01_exact_source_transport(f32, f64, preserve):
    decoded = ij.float32_source_sample(f32, sample_policy=preserve)
    assert isinstance(decoded, float)
    assert _bits64(decoded) == f64
    encoded = ij.float32_source_sample_bits(_from64(f64), sample_policy=preserve)
    assert type(encoded) is int
    assert encoded == f32


@pytest.mark.parametrize("bits", [0x7FC00001, 0xFFC12345], ids=["positive", "negative"])
def test_P02_quiet_nan_requires_explicit_policy(bits):
    with pytest.raises(ValueError):
        ij.float32_source_sample(bits)
    represented = _from64(_PAYLOAD64 if bits == _PAYLOAD32 else 0xFFF82468A0000000)
    with pytest.raises(ValueError):
        ij.float32_source_sample_bits(represented)


@pytest.mark.parametrize("bits", [0x7F800000, 0xFF800000, 0x7F800001, 0xFF800001],
                         ids=["positive-inf", "negative-inf", "positive-snan", "negative-snan"])
def test_P03_raw_nonquiet_samples_rejected(bits, preserve):
    with pytest.raises(ValueError):
        ij.float32_source_sample(bits, sample_policy=preserve)


@pytest.mark.parametrize(
    "bits", [0x7FF0000000000000, 0xFFF0000000000000, 0x7FF0000020000000,
             0xFFF0000020000000, 0x7FF8000020000001],
    ids=["positive-inf", "negative-inf", "positive-snan", "negative-snan", "foreign-payload-tail"],
)
def test_P04_represented_nonquiet_or_lossy_payload_rejected(bits, preserve):
    with pytest.raises(ValueError):
        ij.float32_source_sample_bits(_from64(bits), sample_policy=preserve)


@pytest.mark.parametrize("sample", [0.1, 1e40, -1e40],
                         ids=["unrepresented", "positive-overflow", "negative-overflow"])
def test_P05_encoder_never_implicitly_rounds_or_overflows(sample, preserve):
    with pytest.raises(ValueError):
        ij.float32_source_sample_bits(sample, sample_policy=preserve)


@pytest.mark.parametrize("bits", [True, np.bool_(False), -1, 0x100000000, 1.0, "1"],
                         ids=["bool", "numpy-bool", "negative", "uint32-overflow", "float", "text"])
def test_P06_raw_bit_argument_is_a_checked_integer(bits, preserve):
    with pytest.raises((TypeError, ValueError, OverflowError)):
        ij.float32_source_sample(bits, sample_policy=preserve)


def test_P07_numpy_integer_bits_and_finite_defaults():
    assert _bits64(ij.float32_source_sample(np.uint32(0x80000000))) == 0x8000000000000000
    assert ij.float32_source_sample_bits(-0.0) == 0x80000000
    finite = _fixture(nan=False)
    data = ij.Dataset(finite, axes="YX")
    _policy(data, ij.FloatingSamplePolicy.FINITE_ONLY)
    _assert_raw(data.plane(), finite)
    _assert_raw(ij.canonical_float32_source(finite), finite)


def test_P08_owned_quiet_nan_admission_and_copy_independence(preserve):
    source = _fixture()
    original = source.copy()
    source.flags.writeable = False
    canonical = ij.canonical_float32_source(source, sample_policy=preserve)
    _assert_raw(canonical, original)
    _assert_raw(source, original)
    assert not np.shares_memory(canonical, source)
    data = ij.Dataset(source, axes="YX", sample_policy=preserve)
    _policy(data, preserve)
    assert data.shape == (1, 1, 1, 2, 3, 1)
    _assert_raw(data.plane(), original)
    owned = data.plane()
    owned.view(np.uint32)[:] = 0
    canonical.view(np.uint32)[:] = 0
    _assert_raw(data.plane(), original)
    _assert_raw(data.numpy("YX"), original)


@pytest.mark.parametrize("route", ["dataset", "canonicalizer"], ids=["dataset", "canonicalizer"])
def test_P09_default_source_routes_reject_quiet_nan(route):
    pixels = _fixture()
    original = pixels.copy()
    with pytest.raises(ValueError):
        if route == "dataset":
            ij.Dataset(pixels, axes="YX")
        else:
            ij.canonical_float32_source(pixels)
    _assert_raw(pixels, original)


@pytest.mark.parametrize("bits", [0x7F800000, 0xFF800000, 0x7F800001, 0xFF800001],
                         ids=["positive-inf", "negative-inf", "positive-snan", "negative-snan"])
@pytest.mark.parametrize("route", ["dataset", "canonicalizer"], ids=["dataset", "canonicalizer"])
@pytest.mark.parametrize("policy", ["default", "preserve"], ids=["default", "preserve"])
def test_P10_original_float32_bits_checked_before_conversion(bits, route, policy, preserve):
    # An sNaN can become a qNaN during a float-to-double cast. Classification
    # must inspect these ORIGINAL raw bits before forcecast/NumPy conversion.
    source = _fixture(nan=False)
    source.view(np.uint32)[1, 2] = bits
    before = source.view(np.uint32).copy()
    kwargs = {} if policy == "default" else {"sample_policy": preserve}
    with pytest.raises(ValueError):
        if route == "dataset":
            ij.Dataset(source, axes="YX", **kwargs)
        else:
            ij.canonical_float32_source(source, **kwargs)
    np.testing.assert_array_equal(source.view(np.uint32), before)


@pytest.mark.parametrize("dtype", [np.uint8, np.uint16, np.float64],
                         ids=["uint8", "uint16", "float64"])
@pytest.mark.parametrize("route", ["dataset", "canonicalizer"], ids=["dataset", "canonicalizer"])
def test_P11_opt_in_rejects_other_declared_types(dtype, route, preserve):
    pixels = np.zeros((2, 3), dtype=dtype)
    with pytest.raises((TypeError, ValueError)):
        if route == "dataset":
            ij.Dataset(pixels, axes="YX", sample_policy=preserve)
        else:
            ij.canonical_float32_source(pixels, sample_policy=preserve)


@pytest.mark.parametrize("channels", [3, 4], ids=["rgb", "rgba"])
def test_P12_opt_in_is_grayscale_only(channels, preserve):
    pixels = np.zeros((2, 3, channels), dtype=np.float32)
    with pytest.raises(ValueError):
        ij.Dataset(pixels, axes="YXS", sample_policy=preserve)
    with pytest.raises((TypeError, ValueError)):
        ij.canonical_float32_source(pixels, sample_policy=preserve)


def test_P13_strided_axis_permutation_preserves_declared_payload(preserve):
    source = np.stack([_fixture(), _fixture(nan=False)])
    original = source.copy()
    # Negative X stride and reordered axes exercise raw-bit ingestion before
    # a contiguous double forcecast, and the caller's array remains unmodified.
    transformed = np.transpose(source[:, :, ::-1], (2, 0, 1))
    data = ij.Dataset(transformed, axes="XZY", sample_policy=preserve)
    expected = original[:, :, ::-1].copy()
    _assert_raw(data.numpy("ZYX"), expected)
    _assert_raw(data.numpy("XZY"), np.transpose(expected, (2, 0, 1)))
    source.view(np.uint32)[:] = 0
    _assert_raw(data.numpy("ZYX"), expected)


def test_P14_calibration_preserves_policy_storage_and_all_fields(preserve):
    original = _fixture()
    source = ij.Dataset(original, axes="YX", sample_policy=preserve)
    calibration = ij.SpatialCalibration(
        spacing=(0.5, 2, 3), origin=(11, -7, 19),
        direction=(0, -1, 0, 1, 0, 0, 0, 0, 1), unit="um",
        frame_interval=0.25, time_origin=-2, time_unit="min",
    )
    changed = source.with_calibration(calibration)
    assert changed.shares_pixels(source)
    _policy(changed, preserve)
    assert _calibration(changed.calibration) == {
        "spacing": (0.5, 2, 3), "origin": (11, -7, 19),
        "direction": (0, -1, 0, 1, 0, 0, 0, 0, 1), "unit": "um",
        "frame_interval": 0.25, "time_origin": -2, "time_unit": "min",
    }
    _assert_raw(changed.plane(), original)
    _assert_raw(source.plane(), original)


def test_P15_plane_replacement_inherits_policy_and_preserves_original(preserve):
    source_pixels = np.stack([_fixture(), _fixture(nan=False)])
    source = ij.Dataset(source_pixels, axes="ZYX", sample_policy=preserve)
    changed = source.with_plane(_fixture(), z=1)
    _policy(changed, preserve)
    expected = np.stack([_fixture(), _fixture()])
    _assert_raw(changed.numpy("ZYX"), expected)
    _assert_raw(source.numpy("ZYX"), source_pixels)
    _assert_raw(changed.region(2, 1, 1, 1), expected[0, 1:2, 2:3])
    _assert_raw(changed.plane("xz", 1), expected[:, 1, :])
    _assert_raw(changed.plane("yz", 2), expected[:, :, 2])
    with pytest.raises(TypeError):
        source.with_plane(_fixture(), z=1, sample_policy=preserve)
    default_source = ij.Dataset(_fixture(nan=False), axes="YX")
    with pytest.raises(ValueError):
        default_source.with_plane(_fixture())
    _assert_raw(default_source.plane(), _fixture(nan=False))


@pytest.mark.parametrize("bits", [0x7F800000, 0x7F800001], ids=["inf", "snan"])
def test_P16_failed_replacement_is_atomic(bits, preserve):
    source_pixels = np.stack([_fixture(), _fixture(nan=False)])
    source = ij.Dataset(source_pixels, axes="ZYX", sample_policy=preserve)
    replacement = _fixture(nan=False)
    replacement.view(np.uint32)[0, 0] = bits
    replacement_before = replacement.view(np.uint32).copy()
    with pytest.raises(ValueError):
        source.with_plane(replacement, z=1)
    _assert_raw(source.numpy("ZYX"), source_pixels)
    np.testing.assert_array_equal(replacement.view(np.uint32), replacement_before)
    _policy(source, preserve)


@pytest.mark.parametrize("route", ["generic-add", "generic-gaussian", "gaussian-default", "imagej-zero", "imagej-default"],
                         ids=["generic-add", "generic-gaussian", "gaussian-default", "imagej-zero", "imagej-default"])
def test_P17_existing_processing_remains_finite_only(route, preserve):
    data = ij.Dataset(_fixture(), axes="YX", sample_policy=preserve)
    source = data.plane()
    original = source.copy()
    with pytest.raises(ValueError):
        if route == "generic-add":
            ij.execute_operation("add", source, {"value": 1})
        elif route == "generic-gaussian":
            ij.execute_operation("gaussian_blur", source)
        elif route == "gaussian-default":
            ij.gaussian_blur(source)
        elif route == "imagej-zero":
            ij.gaussian_blur_imagej(source, 0)
        else:
            ij.gaussian_blur_imagej(source)
    _assert_raw(source, original)
    _assert_raw(data.plane(), original)


def _ledger(scheduler, retained):
    usage = scheduler.usage
    assert usage.running_bytes == 0
    assert usage.retained_bytes == retained
    assert usage.reserved_bytes == usage.running_bytes + usage.retained_bytes


def test_P18_real_retained_result_publication_detaches_without_losing_payload(preserve):
    # Existing Document merely contains the opted dataset for a real public
    # resource path. Document/v2/display/history qualification is out of scope.
    original = np.stack([_fixture(), _fixture(nan=False)])
    data = ij.Dataset(original, axes="ZYX", sample_policy=preserve)
    owner = ij.Document(data)
    app = ij.Application()
    app.adopt(owner)
    scheduler = app.job_scheduler
    scheduler.set_limits(ij.JobLimits(workers=1, memory_budget=0, capacity=8))
    try:
        choice = ij.TargetRequest(
            scope=ij.TargetScope.POSITIONS,
            positions=[ij.PlanePosition(z=1)], roi=ij.RoiPolicy.WHOLE_PLANE,
        )
        target = app.resolve_target(choice, document_id=owner.id, operation="add")
        job = app.submit("add", target, {"value": 1},
                         job_options=ij.JobOptions(retain_result_resources=True))
        assert job.wait(5000), "Required native finite-Z1 job did not terminate"
        assert job.info.state == ij.JobState.READY
        charge = job.info.resource_bytes
        assert charge > 0
        _ledger(scheduler, charge)
        prepared = job.prepared
        snapshot = prepared.preview
        exported = snapshot.dataset
        _policy(exported, preserve)
        recalibrated = exported.with_calibration(ij.SpatialCalibration(unit="um"))
        replaced = exported.with_plane(_fixture(nan=False), z=1)
        _policy(recalibrated, preserve)
        _policy(replaced, preserve)
        _assert_raw(replaced.numpy("ZYX"), original)
        expected = original.copy()
        expected[1] = np.array([[1, 1, 2], [-1.5, 1, 4]], dtype=np.float32)
        owned_numpy = exported.numpy("ZYX")
        _assert_raw(owned_numpy, expected)
        _assert_raw(recalibrated.numpy("ZYX"), expected)
        _assert_raw(data.numpy("ZYX"), original)
        published = app.publish(job)
        assert published.document_id == owner.id
        assert job.info.state == ij.JobState.COMPLETED
        scheduler.shutdown()  # Joins workers before the final alias/drop claim.
        _ledger(scheduler, charge)
        live = owner.snapshot().dataset
        _policy(live, preserve)
        _assert_raw(live.numpy("ZYX"), expected)
        prepared = snapshot = recalibrated = replaced = None
        gc.collect()
        _ledger(scheduler, charge)
        exported = None
        gc.collect()
        _ledger(scheduler, 0)
        # Native published data is detached and copied NumPy bytes are owned;
        # neither should keep the actual retained job reservation alive.
        _assert_raw(live.numpy("ZYX"), expected)
        _assert_raw(owned_numpy, expected)
        _assert_raw(data.numpy("ZYX"), original)
        assert scheduler.usage.running == 0
    finally:
        scheduler.shutdown()


@pytest.mark.parametrize("dtype", [np.uint8, np.uint16, np.float32, np.float64],
                         ids=["uint8", "uint16", "float32", "float64"])
def test_P19_legacy_finite_type_admission_defaults(dtype):
    source = np.arange(6, dtype=dtype).reshape(2, 3)
    data = ij.Dataset(source, axes="YX")
    _policy(data, ij.FloatingSamplePolicy.FINITE_ONLY)
    assert data.dtype == source.dtype
    assert data.plane().tobytes() == source.tobytes()
    replacement = np.full((2, 3), 5, dtype=dtype)
    changed = data.with_plane(replacement)
    _policy(changed, ij.FloatingSamplePolicy.FINITE_ONLY)
    assert changed.plane().dtype == replacement.dtype
    assert changed.plane().tobytes() == replacement.tobytes()
    assert data.plane().tobytes() == source.tobytes()


def test_P20_scientific_channels_are_not_interleaved_samples(preserve):
    source = np.stack([_fixture(), _fixture(nan=False), _fixture(nan=False), _fixture()])
    source = source.reshape(2, 2, 1, 2, 3)  # T,C,Z,Y,X; each plane is grayscale.
    data = ij.Dataset(source, axes="TCZYX", sample_policy=preserve)
    assert data.shape == (2, 2, 1, 2, 3, 1)
    _policy(data, preserve)
    _assert_raw(data.numpy("TCZYX"), source)
    _assert_raw(data.plane(t=0, c=0), _fixture())
    _assert_raw(data.plane(t=0, c=1), _fixture(nan=False))
    _assert_raw(data.plane(t=1, c=0), _fixture(nan=False))
    _assert_raw(data.plane(t=1, c=1), _fixture())
