"""Reversible editing sessions that dispatch image work to native functions."""
from __future__ import annotations
import numpy as np


class Editor:
    """Owned image, optional ROI, and bounded undo/redo history.

    Use apply(imagejxx.some_function, ...). The pixels property returns a copy.
    A selected ROI constrains writes after processing the complete image.
    """
    def __init__(self, pixels, *, history_limit: int = 8):
        from . import convert
        if not isinstance(history_limit, int) or history_limit < 1:
            raise ValueError("history_limit must be a positive integer")
        array = np.asarray(pixels)
        self._pixels = convert(array, array.dtype)
        self._limit = history_limit
        self._undo: list[np.ndarray] = []
        self._redo: list[np.ndarray] = []
        self._roi: np.ndarray | None = None

    @property
    def pixels(self) -> np.ndarray:
        return self._pixels.copy()

    def select(self, mask=None) -> Editor:
        from . import _mask, fill
        selection = _mask(mask)
        if selection is not None:
            fill(self._pixels, 0, mask=selection)  # Native shape and finite-value validation.
        self._roi = None if selection is None else selection.copy()
        return self

    def apply(self, operation, *args, **kwargs) -> Editor:
        from . import convert, paste
        result = np.asarray(operation(self._pixels.copy(), *args, **kwargs))
        result = convert(result, result.dtype)
        if self._roi is not None:
            if result.shape != self._pixels.shape:
                raise ValueError("An ROI edit must preserve the image shape")
            result = paste(convert(self._pixels, result.dtype), result, mask=self._roi)
        self._undo.append(self._pixels)
        self._undo = self._undo[-self._limit:]
        self._redo.clear()
        self._pixels = result
        return self

    def undo(self) -> Editor:
        if not self._undo:
            raise IndexError("No edit to undo")
        self._redo.append(self._pixels)
        self._pixels = self._undo.pop()
        self._roi = None
        return self

    def redo(self) -> Editor:
        if not self._redo:
            raise IndexError("No edit to redo")
        self._undo.append(self._pixels)
        self._pixels = self._redo.pop()
        self._roi = None
        return self
