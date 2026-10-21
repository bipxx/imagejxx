"""Native PNM I/O and optional permissively licensed image codecs."""
from __future__ import annotations
from pathlib import Path
import numpy as np
from . import _native


def imread(path) -> np.ndarray:
    """Read PGM/PPM natively, NPY with NumPy, TIFF with tifffile, or Pillow formats.

    TIFF dimensions are preserved; use explicit stack APIs for volumetric data.
    Paletted and monochrome Pillow images become RGB and uint8 grayscale.
    """
    filename = Path(path)
    suffix = filename.suffix.lower()
    if suffix in (".pgm", ".ppm", ".pnm"):
        return _native.read_pnm(str(filename))
    if suffix == ".npy":
        return np.load(filename, allow_pickle=False)
    if suffix in (".tif", ".tiff"):
        try:
            import tifffile
        except ImportError as error:
            raise ImportError("TIFF support requires pip install 'imagejxx[io]'") from error
        return tifffile.imread(filename)
    try:
        from PIL import Image
    except ImportError as error:
        raise ImportError("This image format requires pip install 'imagejxx[io]'") from error
    with Image.open(filename) as image:
        if image.mode == "P":
            image = image.convert("RGBA" if "transparency" in image.info else "RGB")
        elif image.mode == "1":
            image = image.convert("L")
        elif image.mode not in ("L", "RGB", "RGBA", "I", "I;16", "I;16B", "I;16L", "F"):
            image = image.convert("RGB")
        return np.array(image)


def imwrite(path, pixels, **options) -> None:
    """Write PNM natively, NPY, TIFF (uncompressed by default), or Pillow formats."""
    filename = Path(path)
    suffix = filename.suffix.lower()
    array = np.asarray(pixels)
    if suffix in (".pgm", ".ppm", ".pnm"):
        if options:
            raise ValueError("PNM writing accepts no codec options")
        _native.write_pnm(array, str(filename))
        return
    if suffix == ".npy":
        if options:
            raise ValueError("NPY writing accepts no codec options")
        np.save(filename, array, allow_pickle=False)
        return
    if suffix in (".tif", ".tiff"):
        try:
            import tifffile
        except ImportError as error:
            raise ImportError("TIFF support requires pip install 'imagejxx[io]'") from error
        tifffile.imwrite(filename, array, **options)
        return
    try:
        from PIL import Image
    except ImportError as error:
        raise ImportError("This image format requires pip install 'imagejxx[io]'") from error
    Image.fromarray(array).save(filename, **options)
