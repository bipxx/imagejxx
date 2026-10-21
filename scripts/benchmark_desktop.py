"""Measure targeted native editor calls and retained shared pixel buffers."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import platform
import time

import imagejxx as ij
from imagejxx.ui.app import Editor
from imagejxx.ui.model import Document
from imagejxx.ui.persistence import Preferences
import numpy as np


def summarize(samples):
    return {
        "median_ms": float(np.median(samples)),
        "p95_ms": float(np.percentile(samples, 95)),
        "maximum_ms": float(max(samples)),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repeats", type=int, default=100)
    args = parser.parse_args()
    if args.repeats < 10:
        parser.error("Use at least ten repeats")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    shape = (64, 128, 192)
    pixels = np.arange(np.prod(shape), dtype=np.uint16).reshape(shape)
    editor = Editor(
        preferences=Preferences(args.output.parent / "desktop-benchmark-prefs.json")
    )
    editor.action("preferences", history_limit=8)
    editor.add_document(Document(pixels, is_stack=True))
    live = editor.live_document()
    live.set_view(crosshair=[96, 64, 32])
    result = {
        "schema_version": 1,
        "platform": platform.platform(),
        "processor": platform.processor(),
        "python": platform.python_version(),
        "imagejxx": ij.__version__,
        "fixture": {"axes": "ZYX", "shape": shape, "dtype": "uint16"},
        "repeats": args.repeats,
        "history_limit": 8,
        "scope": (
            "Targeted native editor API calls without rendering; storage counters "
            "cover unique live/undo/redo pixel buffers, not total process memory, "
            "worker snapshots, GUI latency or upstream parity."
        ),
        "initial_storage": live.info["pixel_storage"],
        "latency": {},
    }
    samples = []
    for _ in range(args.repeats):
        start = time.perf_counter()
        live.apply_operation("add", {"value": 1}, asynchronous=False)
        samples.append((time.perf_counter() - start) * 1000)
    result["latency"]["local_plane_edit"] = summarize(samples)
    result["edited_storage"] = live.info["pixel_storage"]
    samples = []
    for _ in range(args.repeats):
        start = time.perf_counter()
        live.info
        samples.append((time.perf_counter() - start) * 1000)
    result["latency"]["document_metadata"] = summarize(samples)
    samples = []
    for _ in range(args.repeats):
        start = time.perf_counter()
        editor.action("undo")
        editor.action("redo")
        samples.append((time.perf_counter() - start) * 1000)
    result["latency"]["undo_redo_pair"] = summarize(samples)
    result["final_storage"] = live.info["pixel_storage"]
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(
        json.dumps(
            {"latency": result["latency"], "storage": result["final_storage"]}, indent=2
        )
    )


if __name__ == "__main__":
    main()
