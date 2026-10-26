"""Independent literal C2 fixtures and GL sample expectations; no product imports."""
import math


ACCEPTED_SETTINGS = {
    "theme": "light", "ui_scale": 1.5, "auto_dpi": True,
    "canvas_background": "#123456", "default_zoom": 2.0,
    "interpolation": "linear",
}
STEP_WIDTH, STEP_HEIGHT = 2, 4
STEP_WORDS = (0, 128, 0, 128, 0, 128, 0, 128)
CHECKER_WORDS = (0, 128, 128, 0, 128, 0, 0, 128)
BACKGROUND_RGB = (18, 52, 86)


def checker_plane(fixed_index):
    """Any canonical 2x2 spatial plane of the symmetric 2x2x2 fixture."""
    assert type(fixed_index) is int and fixed_index in (0, 1)
    return tuple(128 * ((x + y + fixed_index) % 2)
                 for y in range(2) for x in range(2))


def expected_gl_sample(words, width, height, u, v, interpolation,
                       wrap_s="clamp_to_edge", wrap_t="clamp_to_edge"):
    """Unrounded grayscale for normalized UVs and explicit wrap modes.

    The reader obtains UV from the actual framebuffer pixel center and submitted
    image geometry. It must separately validate the actual wrap/filter state.
    This models sampling of the literal texture, not CPU renderer output.
    """
    assert type(width) is int and type(height) is int and width > 0 and height > 0
    assert len(words) == width * height
    assert all(type(value) is int and 0 <= value <= 255 for value in words)
    assert all(type(value) in (int, float) and math.isfinite(value)
               and 0 <= value <= 1 for value in (u, v))
    assert interpolation in ("nearest", "linear")
    assert wrap_s in ("repeat", "clamp_to_edge") and wrap_t in ("repeat", "clamp_to_edge")

    def texel(x, y):
        x = x % width if wrap_s == "repeat" else min(width - 1, max(0, x))
        y = y % height if wrap_t == "repeat" else min(height - 1, max(0, y))
        return words[y * width + x]

    if interpolation == "nearest":
        return float(texel(math.floor(u * width), math.floor(v * height)))
    x, y = u * width - .5, v * height - .5
    left, top = math.floor(x), math.floor(y)
    fx, fy = x - left, y - top
    return ((1 - fy) * ((1 - fx) * texel(left, top) + fx * texel(left + 1, top))
            + fy * ((1 - fx) * texel(left, top + 1) + fx * texel(left + 1, top + 1)))


def framebuffer_uv(pixel, image_rect):
    """Pixel-center UV; all coordinates use the top-left framebuffer system."""
    assert len(pixel) == 2 and all(type(value) is int for value in pixel)
    assert len(image_rect) == 4
    assert all(type(value) in (int, float) and math.isfinite(value) for value in image_rect)
    left, top, right, bottom = image_rect
    assert right > left and bottom > top
    u = (pixel[0] + .5 - left) / (right - left)
    v = (pixel[1] + .5 - top) / (bottom - top)
    assert 0 <= u <= 1 and 0 <= v <= 1, "Sample pixel center must lie in the image"
    return u, v
