"""Explicit axis semantics, plane sampling, immutable edits and calibration."""

import numpy as np
import pytest
import imagejxx as ij


@pytest.mark.parametrize("dtype", [np.uint8, np.uint16, np.float32, np.float64])
def test_axis_permutation_and_plane_sampling(dtype):
    pixels = np.arange(2 * 2 * 3 * 4 * 5).reshape(2, 2, 3, 4, 5).astype(dtype)
    data = ij.Dataset(pixels, axes="TCZYX")
    assert data.axis_sizes == dict(T=2, C=2, Z=3, Y=4, X=5, S=1)
    assert data.shape == (2, 2, 3, 4, 5, 1)
    assert data.dtype == np.dtype(dtype)
    np.testing.assert_array_equal(data.numpy("TCZYX"), pixels)
    np.testing.assert_array_equal(data.plane("xy", 2, t=1, c=1), pixels[1, 1, 2])
    np.testing.assert_array_equal(data.plane("xz", 2, t=1, c=1), pixels[1, 1, :, 2, :])
    np.testing.assert_array_equal(data.plane("yz", 3, t=1, c=1), pixels[1, 1, :, :, 3])
    np.testing.assert_array_equal(
        data.region(1, 1, 3, 2, t=1, c=1, z=2), pixels[1, 1, 2, 1:3, 1:4]
    )
    reordered = np.transpose(pixels, (4, 2, 1, 3, 0))
    other = ij.Dataset(reordered, axes="XZCYT")
    np.testing.assert_array_equal(other.numpy("TCZYX"), pixels)
    np.testing.assert_array_equal(other.numpy("XZCYT"), reordered)


def test_scientific_channels_are_distinct_from_samples():
    pixels = np.arange(4 * 5 * 3, dtype=np.uint16).reshape(4, 5, 3)
    scientific = ij.Dataset(pixels, axes="YXC")
    rgb = ij.Dataset(pixels, axes="YXS")
    assert scientific.axis_sizes["C"] == 3 and scientific.axis_sizes["S"] == 1
    assert rgb.axis_sizes["C"] == 1 and rgb.axis_sizes["S"] == 3
    np.testing.assert_array_equal(scientific.plane(c=2), pixels[:, :, 2])
    np.testing.assert_array_equal(rgb.plane(), pixels)
    np.testing.assert_array_equal(scientific.numpy("YXC"), pixels)
    np.testing.assert_array_equal(rgb.numpy("YXS"), pixels)
    with pytest.raises(TypeError):
        ij.Dataset(pixels)


def test_copy_on_write_and_noncontiguous_input_lifetime():
    pixels = np.arange(60, dtype=np.float64).reshape(3, 4, 5)
    data = ij.Dataset(pixels[:, :, ::-1], axes="ZYX")
    expected = pixels[:, :, ::-1].copy()
    pixels[:] = -1
    np.testing.assert_array_equal(data.numpy("ZYX"), expected)
    changed = data.with_plane(np.full((4, 5), 10.0), z=1)
    np.testing.assert_array_equal(data.numpy("ZYX"), expected)
    expected[1] = 10
    np.testing.assert_array_equal(changed.numpy("ZYX"), expected)
    extracted = changed.plane(coordinate=1)
    extracted[:] = 100
    assert changed.plane(coordinate=1)[0, 0] == 10


def test_calibration_and_physical_plane_basis():
    cal = ij.SpatialCalibration(
        spacing=(0.5, 2, 3),
        origin=(10, 20, 30),
        direction=(0, -1, 0, 1, 0, 0, 0, 0, 1),
        unit="um",
        frame_interval=0.25,
        time_origin=2,
    )
    np.testing.assert_allclose(cal.voxel_to_world((3, 2, 1)), (6, 21.5, 33))
    np.testing.assert_allclose(cal.world_to_voxel((6, 21.5, 33)), (3, 2, 1))
    assert cal.time(4) == 3
    data = ij.Dataset(np.zeros((3, 4, 5)), axes="ZYX", calibration=cal)
    geometry = data.plane_geometry("xz", 2)
    assert geometry["origin"] == [6, 20, 30]
    assert geometry["horizontal_step"] == [0, 0.5, 0]
    assert geometry["vertical_step"] == [0, 0, 3]
    assert geometry["horizontal_axis"] == "X" and geometry["vertical_axis"] == "Z"
    with pytest.raises(AttributeError):
        cal.spacing = (1, 1, 1)


@pytest.mark.parametrize(
    "axes,shape",
    [
        ("ZZX", (2, 3, 4)),
        ("QYX", (2, 3, 4)),
        ("ZY", (2, 3)),
        ("yx", (3, 4)),
        ("ZYX", (3, 4)),
        ("YX", (0, 4)),
        ("YXS", (3, 4, 2)),
    ],
)
def test_invalid_axes_and_dimensions(axes, shape):
    with pytest.raises(ValueError):
        ij.Dataset(np.zeros(shape), axes=axes)


def test_invalid_data_calibration_and_targets():
    for pixels in [np.zeros((3, 4), np.int32), np.full((3, 4), np.nan)]:
        with pytest.raises(ValueError):
            ij.Dataset(pixels, axes="YX")
    for options in [
        dict(spacing=(0, 1, 1)),
        dict(origin=(np.inf, 0, 0)),
        dict(direction=(0,) * 9),
        dict(frame_interval=0),
        dict(unit=""),
        dict(unit="   "),
    ]:
        with pytest.raises(ValueError):
            ij.SpatialCalibration(**options)
    data = ij.Dataset(np.zeros((2, 3, 4)), axes="ZYX")
    for view, coordinate in [("xy", 2), ("xz", -1), ("yz", 4)]:
        with pytest.raises(IndexError):
            data.plane(view, coordinate)
    with pytest.raises(IndexError):
        data.plane(c=1)
    with pytest.raises(IndexError):
        data.region(3, 1, 2, 1)
    with pytest.raises(ValueError):
        data.numpy("YX")
    with pytest.raises(ValueError):
        data.with_plane(np.zeros((4, 4)))
    with pytest.raises(ValueError):
        data.with_plane(np.zeros((3, 4), np.uint8))
