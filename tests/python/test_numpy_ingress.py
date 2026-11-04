"""Owned processing ingress: layouts, casting, refusal and temporary allocations."""
import tracemalloc

import numpy as np
import pytest
import imagejxx._native as native


def layout(array, variant):
    if variant == "transpose":
        return array.swapaxes(-2, -1)
    if variant == "reverse":
        return array[::-1, ::-1]
    if variant == "step":
        return array[::2, ::2]
    if variant == "broadcast":
        return np.broadcast_to(array[:1], array.shape)
    if variant == "unaligned":
        out = np.ndarray(array.shape, array.dtype,
                         buffer=bytearray(array.nbytes + 1), offset=1)
        out[...] = array
        return out
    if variant == "readonly":
        array.flags.writeable = False
    return array


@pytest.mark.parametrize("dtype", ["u1", "u2", "f4", "f8"])
@pytest.mark.parametrize("order", ["=", "S"])
@pytest.mark.parametrize("variant", ["contiguous", "transpose", "reverse", "step",
                                     "broadcast", "unaligned", "readonly"])
def test_image_ingress(dtype, order, variant):
    pixels = layout(np.arange(48).astype(np.dtype(dtype).newbyteorder(order)).reshape(6, 8), variant)
    before = pixels.copy()
    expected = pixels.astype(np.float64) * 2 + 1
    result = native.linear(pixels, 2, 1)
    np.testing.assert_array_equal(result, expected)
    assert result.dtype == np.dtype(dtype)
    assert not np.shares_memory(result, pixels)
    np.testing.assert_array_equal(pixels, before)
    result[...] = 0
    np.testing.assert_array_equal(pixels, before)


@pytest.mark.parametrize("channels", [3, 4])
@pytest.mark.parametrize("dtype", ["u1", ">u2", ">f4", "f8"])
def test_interleaved_channel_and_stack_strides(channels, dtype):
    pixels = np.arange(3 * 4 * 5 * channels).reshape(3, 4, 5, channels) % 40
    pixels = pixels.astype(dtype)[::-1, ::-1, ::2, ::-1]
    before = pixels.copy()
    result = native.project(pixels, "mean")
    np.testing.assert_array_equal(result, pixels.astype(np.float64).mean(axis=0))
    np.testing.assert_array_equal(native.linear(pixels[0], 2, 1),
                                  pixels[0].astype(np.float64) * 2 + 1)
    np.testing.assert_array_equal(pixels, before)


@pytest.mark.parametrize("dtype", ["u1", ">u2", ">f4", "f8"])
@pytest.mark.parametrize("variant", ["contiguous", "transpose", "reverse", "step",
                                     "broadcast", "unaligned", "readonly"])
def test_stack_and_optional_mask_ingress(dtype, variant):
    pixels = layout(np.arange(120).astype(dtype).reshape(3, 5, 8), variant)
    np.testing.assert_array_equal(native.project(pixels, "mean"),
                                  pixels.astype(np.float64).mean(axis=0))
    image = pixels[0]
    mask = np.broadcast_to(np.array([1, 0], dtype=">f4"), (image.size // 2, 2)).reshape(image.shape)
    stats = native.statistics(image, mask)
    assert stats["count"] == np.count_nonzero(mask)
    assert stats["sum"] == image[mask != 0].astype(np.float64).sum()


@pytest.mark.parametrize("value", [np.nan, np.inf, -np.inf])
@pytest.mark.parametrize("dtype", ["f4", ">f4", "f8", ">f8"])
def test_nonfinite_refusal(dtype, value):
    pixels = np.zeros((2, 3), dtype=dtype)
    pixels[1, 2] = value
    with pytest.raises(ValueError, match="finite"):
        native.linear(pixels, 1, 0)
    with pytest.raises(ValueError, match="finite"):
        native.project(pixels[None], "mean")


@pytest.mark.parametrize("dtype", ["i1", "i2", "i4", "u4", "c8", "bool", "O"])
def test_unsupported_dtype_refusal(dtype):
    with pytest.raises(ValueError, match="uint8, uint16, float32 or float64"):
        native.linear(np.zeros((2, 3), dtype=dtype), 1, 0)


def test_no_full_sized_numpy_double_temporary():
    pixels = np.zeros((1024, 1024), dtype=np.uint8)
    started = not tracemalloc.is_tracing()
    if started:
        tracemalloc.start()
    try:
        baseline = tracemalloc.get_traced_memory()[0]
        tracemalloc.reset_peak()
        counts, _ = native.histogram(pixels, 256, 0., 256.)
        peak = tracemalloc.get_traced_memory()[1] - baseline
    finally:
        if started:
            tracemalloc.stop()
    assert counts.sum() == pixels.size
    # The former double temporary alone needed 8 MiB. Native-owned Image
    # vectors and total RSS are outside this Python/NumPy allocation bound.
    assert peak < 1024 * 1024
