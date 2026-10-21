"""Display conversion and annotation rendering, independent of GUI widgets."""

from __future__ import annotations
import numpy as np
import imagejxx as ij
from .model import Selection, gray


LUT_NAMES = ("Grays", "Inverted", "Red", "Green", "Blue", "Fire", "Ice", "Spectrum")


def color_channels(color, dtype, channels):
    value = np.array([int(color[i : i + 2], 16) for i in (1, 3, 5)], np.uint8)
    if channels == 1:
        value = np.array([ij.grayscale(value.reshape(1, 1, 3))[0, 0]], np.uint8)
    elif channels == 4:
        value = np.append(value, np.uint8(255))
    maximum = (
        255
        if np.dtype(dtype) == np.uint8
        else 65535
        if np.dtype(dtype) == np.uint16
        else 1
    )
    return value.astype(float) * maximum / 255


def paint_stroke(image, points, color="#ffffff", width=8):
    if not points:
        return image.copy()
    channels = 1 if image.ndim == 2 else image.shape[2]
    values = color_channels(color, image.dtype, channels)
    planes = [image] if channels == 1 else ij.split_channels(image)
    for channel, plane in enumerate(planes):
        if len(points) == 1:
            planes[channel] = ij.draw_line(
                plane, points[0], points[0], values[channel], thickness=width
            )
        else:
            for start, end in zip(points[:-1], points[1:]):
                plane = ij.draw_line(
                    plane, start, end, values[channel], thickness=width
                )
            planes[channel] = plane
    return planes[0] if channels == 1 else ij.merge_channels(planes)


def fill_color(image, color="#ffffff"):
    channels = 1 if image.ndim == 2 else image.shape[2]
    values = color_channels(color, image.dtype, channels)
    planes = [image] if channels == 1 else ij.split_channels(image)
    planes = [ij.fill(plane, values[i]) for i, plane in enumerate(planes)]
    return planes[0] if channels == 1 else ij.merge_channels(planes)


def lookup_table(name="Grays"):
    ramp = np.arange(256, dtype=np.uint8)
    zeros = np.zeros(256, np.uint8)
    if name == "Grays":
        return np.column_stack([ramp] * 3)
    if name == "Inverted":
        return np.column_stack([255 - ramp] * 3)
    if name in ("Red", "Green", "Blue"):
        planes = [zeros.copy() for _ in range(3)]
        planes[("Red", "Green", "Blue").index(name)] = ramp
        return np.column_stack(planes)
    if name == "Spectrum":
        hsv = np.zeros((1, 256, 3), float)
        hsv[0, :, 0] = np.arange(256) / 256
        hsv[0, :, 1:] = 1
        return ij.hsv_to_rgb(hsv)[0]
    stops = {
        "Fire": [
            (0, 0, 0),
            (80, 0, 120),
            (220, 20, 20),
            (255, 180, 0),
            (255, 255, 255),
        ],
        "Ice": [
            (0, 0, 0),
            (0, 40, 120),
            (0, 160, 220),
            (160, 240, 255),
            (255, 255, 255),
        ],
    }
    if name not in stops:
        raise ValueError(f"Unknown display LUT: {name}")
    colors = np.asarray(stops[name], dtype=float)
    return (
        np.column_stack(
            [
                np.interp(ramp, np.linspace(0, 255, len(colors)), colors[:, i])
                for i in range(3)
            ]
        )
        .round()
        .astype(np.uint8)
    )


def display_pixels(
    pixels, *, display_range=None, lut="Grays", custom_lut=None, labels=False,
    sample_policy=ij.FloatingSamplePolicy.FINITE_ONLY,
):
    array = np.asarray(pixels)
    if sample_policy != ij.FloatingSamplePolicy.FINITE_ONLY:
        from imagejxx import _desktop
        data = ij.Dataset(array, axes="YX", sample_policy=sample_policy)
        metadata = {"lut": lut, "labels": labels}
        if display_range is not None: metadata["display_range"] = list(display_range)
        if custom_lut is not None: metadata["custom_lut"] = np.asarray(custom_lut).tolist()
        return _desktop.render_dataset_plane(data, metadata)
    if labels:
        indices = ((array.astype(np.int64) * 37) % 256).astype(np.uint8)
        table = lookup_table("Spectrum")
        table[0] = 0
        return ij.merge_channels([ij.apply_lut(indices, table[:, i]) for i in range(3)])
    if display_range is None:
        if array.dtype == np.uint8:
            low, high = 0, 255
        elif array.dtype == np.uint16:
            low, high = 0, 65535
        elif array.ndim == 3 and np.min(array) >= 0 and np.max(array) <= 1:
            low, high = 0, 1
        else:
            low, high = float(np.min(array)), float(np.max(array))
            if high == low:
                high = low + 1
    else:
        low, high = display_range
    if not np.isfinite([low, high]).all():
        raise ValueError("Display endpoints must be finite")
    if high == low and array.ndim == 2 and array.dtype in (np.uint16, np.float32):
        # Keep typed scaling in the C++ processor composer. Stored bounds and
        # scientific samples remain unchanged in this detached export path.
        table = lookup_table(lut) if custom_lut is None else np.asarray(custom_lut)
        if table.shape != (256, 3):
            raise ValueError("A display LUT must contain 256 RGB rows")
        state = {"schema": "imagejxx.channel-display", "version": 1,
                 "mode": "color", "channels": [{"visible": True,
                 "range": [float(low), float(high)], "lut": {"table": table.tolist()}}]}
        return ij.compose_channel_display(ij.Dataset(array, axes="YX"), state)
    if high <= low:
        raise ValueError("Display maximum must exceed display minimum")
    work = ij.convert(array, np.float64)
    result = ij.convert(
        ij.adjust_brightness_contrast(
            work, -low * 255 / (high - low), 255 / (high - low)
        ),
        np.uint8,
    )
    if result.ndim == 3:
        if result.shape[2] == 4:
            alpha = ij.convert(array[:, :, 3], np.uint8, scale=True)
            rgb = result[:, :, :3].astype(float)
            checker = ((np.indices(alpha.shape).sum(axis=0) // 12) % 2) * 24 + 210
            rgb = rgb * alpha[:, :, None] / 255 + checker[:, :, None] * (
                1 - alpha[:, :, None] / 255
            )
            result = rgb.round().astype(np.uint8)
        return result
    table = lookup_table(lut) if custom_lut is None else np.asarray(custom_lut)
    if table.shape != (256, 3):
        raise ValueError("A display LUT must contain 256 RGB rows")
    return ij.merge_channels([ij.apply_lut(result, table[:, i]) for i in range(3)])


def render_document(document, *, overlays=True):
    """Render the current slice for export, leaving scientific source data intact."""
    from PIL import Image, ImageDraw, ImageFont

    pixels = display_pixels(
        document.frame,
        display_range=document.display_range,
        lut=document.lut,
        custom_lut=document.custom_lut,
        labels=document.labels,
        sample_policy=document.sample_policy,
    )
    image = Image.fromarray(pixels)
    if not overlays:
        return image
    draw = ImageDraw.Draw(image)
    for overlay in document.overlays:
        if (
            overlay.slice_index is not None
            and overlay.slice_index != document.slice_index
        ):
            continue
        geometry = overlay.selection_record()
        if "imagejxx.roi_style" in geometry and geometry["kind"] != "text":
            selection = Selection.from_dict(geometry).native()
            legacy = ij.Selection("line", attributes={"color": overlay.color})
            fallback = ij.resolve_roi_style(legacy, (255, 204, 0, 255)).stroke_color
            image = Image.fromarray(
                ij.render_roi_style(np.asarray(image), selection, fallback)
            )
            draw = ImageDraw.Draw(image)
            continue
        points, width = (
            [tuple(p) for p in overlay.points],
            max(1, int(round(overlay.width))),
        )
        if overlay.kind == "text":
            draw.text(
                points[0],
                overlay.text,
                fill=overlay.color,
                font=(
                    ImageFont.truetype(overlay.font_path, size=overlay.font_size)
                    if overlay.font_path
                    else ImageFont.load_default(size=overlay.font_size)
                ),
            )
        elif overlay.kind == "rectangle":
            p = np.asarray(points)
            draw.rectangle(
                [*p.min(axis=0), *p.max(axis=0)], outline=overlay.color, width=width
            )
        elif overlay.kind == "ellipse":
            p = np.asarray(points)
            draw.ellipse(
                [*p.min(axis=0), *p.max(axis=0)], outline=overlay.color, width=width
            )
        elif overlay.kind in ("polygon", "freehand"):
            draw.line([*points, points[0]], fill=overlay.color, width=width)
        elif overlay.kind in ("line", "polyline", "angle"):
            draw.line(points, fill=overlay.color, width=width)
        else:
            for x, y in points:
                draw.ellipse(
                    (x - width, y - width, x + width, y + width),
                    outline=overlay.color,
                    width=1,
                )
    return image


def profile(document):
    selection = document.selection
    if selection is None or selection.kind not in ("line", "polyline", "angle"):
        raise ValueError("Draw a line or polyline to create a profile")
    image = gray(document.frame)
    c = document.calibration
    distances, values, offset = [], [], 0.0
    for i, (start, end) in enumerate(zip(selection.points[:-1], selection.points[1:])):
        segment = ij.line_profile(image, start, end)
        delta = (np.asarray(end) - start) * [c.pixel_width, c.pixel_height]
        length = float(np.linalg.norm(delta))
        x = np.linspace(offset, offset + length, len(segment))
        distances.extend(x[bool(i) :])
        values.extend(segment[bool(i) :])
        offset += length
    return np.asarray(distances), np.asarray(values)
