"""Executable array-only segmentation and reversible editing example."""
import numpy as np
import imagejxx as ij

yy, xx = np.mgrid[:96, :128]
pixels = np.full((96, 128), 20, dtype=np.uint8)
pixels[(xx - 40)**2 + (yy - 45)**2 < 14**2] = 200
pixels[(xx - 88)**2 + (yy - 45)**2 < 11**2] = 170

smoothed = ij.gaussian_blur(pixels, 1)
mask = ij.remove_small_objects(ij.fill_holes(ij.auto_mask(smoothed)), 20)
objects = ij.analyze_particles(mask, intensity=pixels, exclude_edges=True)
assert len(objects) == 2
print(f"Detected {len(objects)} particles: {[p['pixels'] for p in objects]} pixels")

roi = ij.rectangle_mask(pixels.shape, 20, 25, 45, 40)
editor = ij.Editor(pixels).select(roi).apply(ij.add, 10)
editor.undo()
assert np.array_equal(editor.pixels, pixels)
print("ROI edit and undo passed")

spectrum = ij.fft2(pixels)
restored = ij.ifft2(spectrum)[:96, :128]
assert np.allclose(restored, pixels)
print("Native FFT round trip passed")
