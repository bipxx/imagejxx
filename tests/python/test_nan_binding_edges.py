"""Additional typed Float32 bridge paths; source-only until root qualification.

Expectations are literal IEEE encodings. No NaN equality, native encoder oracle,
processing primitive, display, Document/session, or upstream parity is involved.
The separately held native9/Python74 files remain unchanged.
"""

import struct
import sys

import imagejxx as ij
import numpy as np
import pytest


_SHAPE = (2, 3)
_SAMPLE_IDS = ("finite", "quiet-nan", "signaling-nan", "infinity")
_ROUTE_IDS = ("dataset", "canonicalizer")
_ARRAY_BITS = {
    "finite": (0x00000000, 0x80000000, 0x3F800000, 0xC0200000, 0x00000001, 0x40400000),
    "quiet-nan": (0x7FC00001, 0x80000000, 0x3F800000, 0xC0200000, 0x00000001, 0xFFC12345),
    "signaling-nan": (0x00000000, 0x80000000, 0x3F800000, 0xC0200000, 0x00000001, 0x7F800001),
    "infinity": (0x00000000, 0x80000000, 0x3F800000, 0xC0200000, 0x00000001, 0xFF800000),
}
_ZERO_ROW_BITS = {
    "finite": (0x80000000, 0x3F800000, 0x00000001),
    "quiet-nan": (0x7FC00001, 0xFFC12345, 0x80000000),
    "signaling-nan": (0x80000000, 0x3F800000, 0x7F800001),
    "infinity": (0x80000000, 0x3F800000, 0x7F800000),
}
_ZERO_SCALAR_BITS = {
    "finite": 0x80000000,
    "quiet-nan": 0x7FC00001,
    "signaling-nan": 0x7F800001,
    "infinity": 0xFF800000,
}
_TRANSPORT_IDS = (
    "positive-zero", "negative-zero", "one", "negative-2.5", "min-subnormal",
    "min-normal", "max-finite", "min-finite", "qnan-payload1", "negative-qnan-payload",
)
# These pairs are independent fixed binary32/binary64 encodings, not values
# computed by either new native helper. Fraction bits expand by 29 places.
_TRANSPORT = (
    (0x00000000, 0x0000000000000000),
    (0x80000000, 0x8000000000000000),
    (0x3F800000, 0x3FF0000000000000),
    (0xC0200000, 0xC004000000000000),
    (0x00000001, 0x36A0000000000000),
    (0x00800000, 0x3810000000000000),
    (0x7F7FFFFF, 0x47EFFFFFE0000000),
    (0xFF7FFFFF, 0xC7EFFFFFE0000000),
    (0x7FC00001, 0x7FF8000020000000),
    (0xFFC12345, 0xFFF82468A0000000),
)
_NONQUIET_IDS = ("positive-inf", "negative-inf", "positive-snan", "negative-snan")
_NONQUIET32 = (0x7F800000, 0xFF800000, 0x7F800001, 0xFF800001)
_NONQUIET64 = (0x7FF0000000000000, 0xFFF0000000000000,
               0x7FF0000020000000, 0xFFF0000020000000)


def _preserve():
    return ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32


def _native32_bytes(bits):
    return b"".join(struct.pack("=I", item) for item in bits)


def _assert_owned_raw(actual, bits, source):
    assert isinstance(actual, np.ndarray)
    assert actual.dtype == np.dtype(np.float32)
    assert actual.dtype.isnative
    assert actual.shape == _SHAPE
    assert actual.tobytes(order="C") == _native32_bytes(bits)
    np.testing.assert_array_equal(actual.view(np.uint32).reshape(-1), np.array(bits, dtype=np.uint32))
    assert not np.shares_memory(actual, source)


def _exercise_array(route, sample, source, bits):
    assert source.shape == _SHAPE and source.dtype.kind == "f" and source.itemsize == 4
    source.flags.writeable = False
    original = source.tobytes(order="C")
    preserve = _preserve()
    for policy in (None, preserve):
        kwargs = {} if policy is None else {"sample_policy": policy}
        accepted = sample == "finite" or (sample == "quiet-nan" and policy is not None)
        if not accepted:
            with pytest.raises(ValueError):
                if route == "dataset":
                    ij.Dataset(source, axes="YX", **kwargs)
                else:
                    ij.canonical_float32_source(source, **kwargs)
        elif route == "dataset":
            data = ij.Dataset(source, axes="YX", **kwargs)
            expected_policy = ij.FloatingSamplePolicy.FINITE_ONLY if policy is None else preserve
            assert data.sample_policy == expected_policy
            assert data.shape == (1, 1, 1, 2, 3, 1)
            for exported in (data.plane(), data.numpy("YX"), data.region(0, 0, 3, 2)):
                _assert_owned_raw(exported, bits, source)
            # Returned arrays are owned; editing one cannot affect immutable
            # native pixels, including signed zeros and payload-bearing NaNs.
            exported.view(np.uint32)[:] = 0
            _assert_owned_raw(data.plane(), bits, source)
        else:
            _assert_owned_raw(ij.canonical_float32_source(source, **kwargs), bits, source)
        assert source.tobytes(order="C") == original


@pytest.mark.parametrize("route", _ROUTE_IDS, ids=_ROUTE_IDS)
@pytest.mark.parametrize("sample", _SAMPLE_IDS, ids=_SAMPLE_IDS)
def test_E01_opposite_endian_float32_arrays(route, sample):
    bits = _ARRAY_BITS[sample]
    byte_order = ">" if sys.byteorder == "little" else "<"
    source = np.array(bits, dtype=byte_order + "u4").reshape(_SHAPE).view(byte_order + "f4")
    assert not source.dtype.isnative
    _exercise_array(route, sample, source, bits)


@pytest.mark.parametrize("route", _ROUTE_IDS, ids=_ROUTE_IDS)
@pytest.mark.parametrize("sample", _SAMPLE_IDS, ids=_SAMPLE_IDS)
def test_E02_zero_stride_float32_arrays(route, sample):
    row_bits = _ZERO_ROW_BITS[sample]
    row = np.array(row_bits, dtype=np.uint32).view(np.float32).reshape(1, 3)
    source = np.broadcast_to(row, _SHAPE)
    assert source.strides[0] == 0 and source.strides[1] == 4
    _exercise_array(route, sample, source, row_bits * 2)
    # Cover an actual zero byte stride on X as well as Y; no materialized copy
    # substitutes for the input view passed to the binding.
    scalar_bits = _ZERO_SCALAR_BITS[sample]
    scalar = np.array([scalar_bits], dtype=np.uint32).view(np.float32).reshape(1, 1)
    repeated = np.broadcast_to(scalar, _SHAPE)
    assert repeated.strides == (0, 0)
    _exercise_array(route, sample, repeated, (scalar_bits,) * 6)


@pytest.mark.parametrize("route", _ROUTE_IDS, ids=_ROUTE_IDS)
@pytest.mark.parametrize("sample", _SAMPLE_IDS, ids=_SAMPLE_IDS)
def test_E03_unaligned_float32_arrays(route, sample):
    bits = _ARRAY_BITS[sample]
    backing = bytearray(b"\xa5" + _native32_bytes(bits) + b"\x5a")
    original = bytes(backing)
    source = np.ndarray(_SHAPE, dtype=np.float32, buffer=backing, offset=1)
    assert not source.flags.aligned and source.ctypes.data % 4 == 1
    _exercise_array(route, sample, source, bits)
    assert bytes(backing) == original  # Includes the two non-pixel sentinels.


def _scalar32(bits):
    # Scalar indexing copies the stored IEEE value without arithmetic or a
    # Python float conversion. Inspect bytes before invoking the native helper.
    scalar = np.array([bits], dtype=np.uint32).view(np.float32)[0]
    assert type(scalar) is np.float32
    assert scalar.tobytes() == struct.pack("=I", bits)
    return scalar


def _scalar64(bits):
    scalar = np.array([bits], dtype=np.uint64).view(np.float64)[0]
    assert type(scalar) is np.float64
    assert scalar.tobytes() == struct.pack("=Q", bits)
    return scalar


def _exercise_scalar(sample, original_bits, expected_bits, quiet):
    original = sample.tobytes()
    for policy in (None, _preserve()):
        kwargs = {} if policy is None else {"sample_policy": policy}
        if quiet and policy is None:
            with pytest.raises(ValueError):
                ij.float32_source_sample_bits(sample, **kwargs)
        else:
            encoded = ij.float32_source_sample_bits(sample, **kwargs)
            assert type(encoded) is int and encoded == expected_bits
        assert sample.tobytes() == original == original_bits


@pytest.mark.parametrize("f32,f64", _TRANSPORT, ids=_TRANSPORT_IDS)
def test_E04_numpy_float32_scalar_exact_bits(f32, f64):
    sample = _scalar32(f32)
    quiet = (f32 & 0x7F800000) == 0x7F800000
    _exercise_scalar(sample, struct.pack("=I", f32), f32, quiet)


@pytest.mark.parametrize("bits", _NONQUIET32, ids=_NONQUIET_IDS)
def test_E05_numpy_float32_scalar_nonquiet_rejected_before_conversion(bits):
    sample = _scalar32(bits)
    original = sample.tobytes()
    for policy in (None, _preserve()):
        kwargs = {} if policy is None else {"sample_policy": policy}
        with pytest.raises(ValueError):
            ij.float32_source_sample_bits(sample, **kwargs)
        assert sample.tobytes() == original == struct.pack("=I", bits)


@pytest.mark.parametrize("f32,f64", _TRANSPORT, ids=_TRANSPORT_IDS)
def test_E06_numpy_float64_scalar_represented_float32_bits(f32, f64):
    sample = _scalar64(f64)
    quiet = (f64 & 0x7FF0000000000000) == 0x7FF0000000000000
    _exercise_scalar(sample, struct.pack("=Q", f64), f32, quiet)


@pytest.mark.parametrize("bits", _NONQUIET64, ids=_NONQUIET_IDS)
def test_E07_numpy_float64_scalar_nonquiet_rejected_before_conversion(bits):
    sample = _scalar64(bits)
    original = sample.tobytes()
    for policy in (None, _preserve()):
        kwargs = {} if policy is None else {"sample_policy": policy}
        with pytest.raises(ValueError):
            ij.float32_source_sample_bits(sample, **kwargs)
        assert sample.tobytes() == original == struct.pack("=Q", bits)


@pytest.mark.parametrize("bits", [0x7FF8000020000001], ids=["nonrepresentable-qnan-tail"])
def test_E08_numpy_float64_scalar_foreign_payload_rejected(bits):
    # Float64-origin payloads are not a new supported transport format: a tail
    # outside the exact binary32 representation must fail even under opt-in.
    sample = _scalar64(bits)
    original = sample.tobytes()
    for policy in (None, _preserve()):
        kwargs = {} if policy is None else {"sample_policy": policy}
        with pytest.raises(ValueError):
            ij.float32_source_sample_bits(sample, **kwargs)
        assert sample.tobytes() == original == struct.pack("=Q", bits)
