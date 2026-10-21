"""Capture fixed, direct ShapeRoi Boolean masks from pinned ImageJ 1.54r.

Reference tooling only: reuse verified extracted sources/JDK, compile into a
fresh excluded directory, and emit a raw corpus for later independent review.
This does not execute RoiManager, certify GUI behavior, decide commercial
eligibility, or incorporate Java sources/runtime into the product.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import threading
import time
import zipfile

from capture_binary_reference import (
    COMMIT, FIJI_SHA, IMAGEJ_SHA, JDK_PREFIX, ROOT, digest, verified,
)

REUSE = ROOT / "build/references/roi-style-imagej154r"
STAGE = ROOT / "build/references/roi-boolean-imagej154r-001"
MAX_OUTPUT = 16 * 1024 * 1024
MAX_BUILD = 128 * 1024 * 1024
MAX_FILES = 8192
SOURCE_LIMIT = 64 * 1024 * 1024
JDK_LIMIT = 512 * 1024 * 1024
VERIFY_SECONDS = 180
CASE_IDS = (
    "overlap_rectangles", "overlap_rectangles_reversed",
    "disconnected_rectangle_ellipse", "disconnected_rectangle_ellipse_reversed",
    "ring_and_inner_island", "ring_and_partial_hole_fill", "disconnected_rings",
    "negative_edges", "right_bottom_edges", "entirely_outside",
    "empty_area_and_rectangle", "empty_area_and_rectangle_reversed",
    "two_empty_areas", "fractional_rectangle_ellipse",
    "fractional_rectangle_ellipse_reversed", "fractional_polygon_rectangle",
    "fractional_polygon_rectangle_reversed", "duplicate_polygon",
)
OPERATORS = ("union", "intersection", "subtraction", "xor")
METHODS = dict(zip(OPERATORS, ("ShapeRoi.or", "ShapeRoi.and", "ShapeRoi.not", "ShapeRoi.xor")))
SOURCE_PINS = {
    "LICENSE.txt": "618725ff649e43605de0fec0a778b4aad24b28c6be9b3cde16295764022f668c",
    "pom.xml": "cc95c64d2da10810f4dfbec191212cf9bfbcbfabec8633e3f0ad1ae4a81c67b3",
    "ij/IJ.java": "8c620842fedd63392268b4fee191e4f6e9ef749ce96bf14a3e9f936e7cea06b4",
    "ij/gui/Roi.java": "200ad9f501a5860fe1486a3d2a8ed6cf6f82f7c7562a81e11d91ee7b7a04497e",
    "ij/gui/ShapeRoi.java": "8ab4a507fed1ddd09391c5663d482350c9f035e17788d915b440915277fc419e",
    "ij/gui/OvalRoi.java": "f065f9a82d7e7d9de5c84615e8885465071f41076f872941f1b4259f93d76a38",
    "ij/gui/PolygonRoi.java": "1c5b398b5423c7f763144f12b95cfa23e08ef9e1cc07a93ba9873411f0f76038",
    "ij/process/PolygonFiller.java": "2d6c43bb6fed5bf206fa552bf0f60883117436884fcd9869ec4b9474ef7c5968",
    "ij/process/ByteProcessor.java": "fc23c95778bd7154eba1ea56d9ea943e7d1d9ea568fe20cf9bf9c6db1db3ad94",
    "ij/process/ImageProcessor.java": "f98e87d25eb6b1f2c5d8557b73dd33e36fef0494d903d6d250206371dccc279c",
    "ij/plugin/frame/RoiManager.java": "2cf7cbd08a780dd05f6f160ce0915eb4524b568410b5078a48afcbe326a3f020",
}

# Independently authored fixed-input driver. No target implementation is used.
DRIVER = r"""import java.awt.Rectangle;
import java.awt.geom.Area;
import java.awt.geom.Rectangle2D;
import ij.gui.OvalRoi;
import ij.gui.PolygonRoi;
import ij.gui.Roi;
import ij.gui.ShapeRoi;
import ij.process.ImageProcessor;

public final class RoiBooleanReference {
    static final int WIDTH = 13;
    static final int HEIGHT = 9;

    static final class Operand {
        final String geometry;
        final ShapeRoi roi;
        Operand(String geometry, ShapeRoi roi) {
            this.geometry = geometry;
            this.roi = roi;
        }
    }

    static String numbers(double... values) {
        StringBuilder result = new StringBuilder("[");
        for (int i = 0; i < values.length; ++i) {
            if (i != 0) result.append(',');
            result.append(Double.toString(values[i]));
        }
        return result.append(']').toString();
    }

    static Operand rectangle(double x, double y, double w, double h) {
        return new Operand("{\"kind\":\"rectangle\",\"xywh\":" + numbers(x,y,w,h)
            + ",\"constructor\":\"ShapeRoi(Roi(double,double,double,double))\"}",
            new ShapeRoi(new Roi(x,y,w,h)));
    }

    static Operand ellipse(double x, double y, double w, double h) {
        return new Operand("{\"kind\":\"ellipse\",\"xywh\":" + numbers(x,y,w,h)
            + ",\"constructor\":\"ShapeRoi(OvalRoi(double,double,double,double))\"}",
            new ShapeRoi(new OvalRoi(x,y,w,h)));
    }

    static Operand polygon(float[] xs, float[] ys) {
        StringBuilder points = new StringBuilder("[");
        for (int i = 0; i < xs.length; ++i) {
            if (i != 0) points.append(',');
            points.append('[').append(Float.toString(xs[i])).append(',')
                .append(Float.toString(ys[i])).append(']');
        }
        points.append(']');
        return new Operand("{\"kind\":\"polygon\",\"points\":" + points
            + ",\"constructor\":\"ShapeRoi(PolygonRoi(float[],float[],n,Roi.POLYGON))\"}",
            new ShapeRoi(new PolygonRoi(xs,ys,xs.length,Roi.POLYGON)));
    }

    static Operand ring(double x, double y, double w, double h,
                        double hx, double hy, double hw, double hh) {
        Area area = new Area(new Rectangle2D.Double(x,y,w,h));
        area.subtract(new Area(new Rectangle2D.Double(hx,hy,hw,hh)));
        return new Operand("{\"kind\":\"ring\",\"outer_xywh\":" + numbers(x,y,w,h)
            + ",\"hole_xywh\":" + numbers(hx,hy,hw,hh)
            + ",\"constructor\":\"ShapeRoi(Area(outer).subtract(Area(hole)))\"}",
            new ShapeRoi(area));
    }

    static Operand empty() {
        return new Operand("{\"kind\":\"empty_area\",\"constructor\":\"ShapeRoi(new Area())\"}",
            new ShapeRoi(new Area()));
    }

    // Read the reference mask, then clip its translated coordinates to canvas.
    // Do not substitute contains(), Java2D painting, or pixelwise Boolean OR.
    static String mask(ShapeRoi roi) {
        Rectangle bounds = roi.getBounds();
        ImageProcessor pixels = roi.getMask();
        if (pixels == null || pixels.getWidth() != bounds.width
                || pixels.getHeight() != bounds.height)
            throw new IllegalStateException("Unexpected ShapeRoi mask dimensions");
        StringBuilder rows = new StringBuilder("[");
        for (int y = 0; y < HEIGHT; ++y) {
            if (y != 0) rows.append(',');
            rows.append('"');
            for (int x = 0; x < WIDTH; ++x) {
                int mx = x - bounds.x;
                int my = y - bounds.y;
                boolean inside = mx >= 0 && my >= 0
                    && mx < bounds.width && my < bounds.height;
                int value = inside ? pixels.get(mx,my) : 0;
                if (value != 0 && value != 255)
                    throw new IllegalStateException("Nonbinary reference mask");
                rows.append(value == 0 ? '0' : '1');
            }
            rows.append('"');
        }
        return rows.append(']').toString();
    }

    static String bounds(ShapeRoi roi) {
        Rectangle b = roi.getBounds();
        return "[" + b.x + "," + b.y + "," + b.width + "," + b.height + "]";
    }

    static String operand(Operand value, String rows) {
        return "{\"geometry\":" + value.geometry + ",\"bounds_xywh\":" + bounds(value.roi)
            + ",\"vector_area_empty\":" + new Area(value.roi.getShape()).isEmpty()
            + ",\"mask_rows\":" + rows + "}";
    }

    static void captureOperation(String id, String operation, Operand left, Operand right) {
        String leftRows = mask(left.roi);
        String rightRows = mask(right.roi);
        ShapeRoi result = (ShapeRoi)left.roi.clone();
        String method;
        if (operation.equals("union")) { result.or(right.roi); method = "ShapeRoi.or"; }
        else if (operation.equals("intersection")) { result.and(right.roi); method = "ShapeRoi.and"; }
        else if (operation.equals("subtraction")) { result.not(right.roi); method = "ShapeRoi.not"; }
        else if (operation.equals("xor")) { result.xor(right.roi); method = "ShapeRoi.xor"; }
        else throw new IllegalArgumentException(operation);
        String resultRows = mask(result);
        if (!leftRows.equals(mask(left.roi)) || !rightRows.equals(mask(right.roi)))
            throw new IllegalStateException("Boolean operation changed an original operand");
        System.out.println("{\"id\":\"" + id + "__" + operation
            + "\",\"operand_set\":\"" + id + "\",\"operator\":\"" + operation
            + "\",\"operation\":\"" + method + "\",\"operand_order\":\"left_then_right\","
            + "\"canvas_wh\":[13,9],\"operands\":[" + operand(left,leftRows) + ","
            + operand(right,rightRows) + "],\"result\":{\"bounds_xywh\":" + bounds(result)
            + ",\"vector_area_empty\":" + new Area(result.getShape()).isEmpty()
            + ",\"mask_rows\":" + resultRows + "}}");
    }

    static void capture(String id, Operand left, Operand right) {
        for (String operation : new String[]{"union","intersection","subtraction","xor"})
            captureOperation(id,operation,left,right);
    }

    public static void main(String[] args) {
        if (!ij.IJ.getVersion().equals("1.54r"))
            throw new IllegalStateException("Unexpected ImageJ reference version");
        if (!java.awt.GraphicsEnvironment.isHeadless())
            throw new IllegalStateException("Headless reference required");
        Operand overlapA = rectangle(1,1,6,4);
        Operand overlapB = rectangle(4,3,6,4);
        capture("overlap_rectangles",overlapA,overlapB);
        capture("overlap_rectangles_reversed",overlapB,overlapA);
        Operand detachedA = rectangle(1,1,3,2);
        Operand detachedB = ellipse(8,5,4,3);
        capture("disconnected_rectangle_ellipse",detachedA,detachedB);
        capture("disconnected_rectangle_ellipse_reversed",detachedB,detachedA);
        Operand donut = ring(1,1,10,7,3,3,6,3);
        capture("ring_and_inner_island",donut,rectangle(5,4,2,1));
        capture("ring_and_partial_hole_fill",donut,rectangle(3,3,2,2));
        capture("disconnected_rings",ring(0,0,5,5,1,1,3,3),ring(7,3,6,6,8,4,4,4));
        capture("negative_edges",rectangle(-3.25,-2.5,7.5,6.25),ellipse(-1.25,5.25,7.5,6.25));
        capture("right_bottom_edges",polygon(new float[]{9,15,14,8},new float[]{1,2,11,8}),
            ellipse(10.25,6.5,5.5,4.25));
        capture("entirely_outside",rectangle(-7,-6,3,2),ellipse(15,10,4,3));
        Operand zero = empty();
        Operand nonempty = rectangle(3,2,4,3);
        capture("empty_area_and_rectangle",zero,nonempty);
        capture("empty_area_and_rectangle_reversed",nonempty,zero);
        capture("two_empty_areas",empty(),empty());
        Operand fractionA = rectangle(1.25,0.75,5.5,4.25);
        Operand fractionB = ellipse(4.5,2.25,6.25,5.5);
        capture("fractional_rectangle_ellipse",fractionA,fractionB);
        capture("fractional_rectangle_ellipse_reversed",fractionB,fractionA);
        Operand fractionP = polygon(new float[]{-0.25f,8.75f,10.5f,2.25f},
            new float[]{1.5f,0.25f,7.75f,6.5f});
        Operand fractionR = rectangle(6.75,3.5,5.5,4.25);
        capture("fractional_polygon_rectangle",fractionP,fractionR);
        capture("fractional_polygon_rectangle_reversed",fractionR,fractionP);
        capture("duplicate_polygon",fractionP,fractionP);
    }
}
"""


def _reference_path(path: Path) -> Path:
    resolved = path.resolve()
    base = (ROOT / "build/references").resolve()
    if not base.is_relative_to(ROOT.resolve()) or resolved == base or not resolved.is_relative_to(base):
        raise ValueError("Capture outputs must stay under build/references")
    for parent in (path, *path.parents):
        if parent == ROOT.parent:
            break
        if parent.is_symlink():
            raise ValueError("Symlink reference paths are not supported")
    return resolved


class _BoundedReader:
    """Give the shared digest/verified helpers a finite read budget."""

    def __init__(self, path: Path, size: int, deadline: float):
        self.stream = path.open("rb")
        self.size, self.deadline, self.read = size, deadline, 0

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.stream.close()

    def readable(self):
        return True

    def readinto(self, buffer):
        if time.monotonic() > self.deadline:
            raise TimeoutError("Reference hashing exceeded its deadline")
        count = self.stream.readinto(buffer)
        self.read += count
        if self.read > self.size or time.monotonic() > self.deadline:
            raise ValueError("Reference file grew or hashing exceeded its deadline")
        if count == 0 and self.read != self.size:
            raise ValueError("Reference file changed size while hashing")
        return count


class _BoundedFile:
    def __init__(self, path: Path, size: int, deadline: float):
        self.path, self.size, self.deadline = path, size, deadline
        self.name = path.name

    def open(self, mode):
        if mode != "rb":
            raise ValueError("Read-only reference verifier")
        return _BoundedReader(self.path, self.size, self.deadline)


def _files(directory: Path, limit: int, deadline: float | None = None) -> dict[str, dict]:
    if deadline is None:
        deadline = time.monotonic() + VERIFY_SECONDS
    result = {}
    total = 0
    entries = 0
    for path in directory.rglob("*"):
        entries += 1
        if entries > MAX_FILES * 2 or time.monotonic() > deadline:
            raise ValueError("Reference directory scan exceeded its bound")
        if path.is_symlink() or not path.resolve().is_relative_to(directory.resolve()):
            raise ValueError("Reference tree contains an escaping path")
        if not path.is_file():
            continue
        size = path.stat().st_size
        total += size
        if total > limit or len(result) >= MAX_FILES:
            raise ValueError("Reference tree exceeds its storage/member bound")
        result[path.relative_to(directory).as_posix()] = {
            "bytes": size, "sha256": digest(_BoundedFile(path, size, deadline)),
        }
    return dict(sorted(result.items()))


def _verify_tree(archive_path: Path, prefix: str, directory: Path, limit: int) -> dict:
    started = time.monotonic()
    if not directory.is_dir():
        raise ValueError(f"Existing extracted reference tree required: {directory}")
    actual = _files(directory, limit, started + VERIFY_SECONDS)
    expected = {}
    total = 0
    with zipfile.ZipFile(archive_path) as archive:
        for entry in archive.infolist():
            if entry.is_dir() or not entry.filename.startswith(prefix):
                continue
            name = entry.filename[len(prefix):]
            relative = PurePosixPath(name)
            if not name or relative.is_absolute() or ".." in relative.parts or "\\" in name:
                raise ValueError("Unsafe pinned archive member")
            if name in expected or len(expected) >= MAX_FILES:
                raise ValueError("Duplicate or excessive pinned archive members")
            total += entry.file_size
            if total > limit or entry.file_size > limit:
                raise ValueError("Pinned reference tree exceeds its byte bound")
            sha = hashlib.sha256()
            read = 0
            with archive.open(entry) as stream:
                while chunk := stream.read(1024 * 1024):
                    read += len(chunk)
                    if read > entry.file_size or time.monotonic() - started > VERIFY_SECONDS:
                        raise ValueError("Pinned archive verification exceeded its bound")
                    sha.update(chunk)
            expected[name] = {"bytes": read, "sha256": sha.hexdigest()}
    if time.monotonic() - started > VERIFY_SECONDS or actual != expected or not expected:
        raise ValueError(f"Extracted tree does not exactly match pinned archive: {directory}")
    return actual


def _storage(directory: Path) -> tuple[int, int]:
    size = count = entries = 0
    for path in directory.rglob("*"):
        entries += 1
        if entries > MAX_FILES * 2:
            raise ValueError("Fresh capture exceeds its directory entry bound")
        if path.is_file():
            size += path.stat().st_size
            count += 1
        if size > MAX_BUILD or count > MAX_FILES:
            raise ValueError("Fresh capture exceeds its storage/member bound")
    return size, count


def _run(command: list[str], directory: Path, label: str, seconds: int) -> dict:
    stdout = directory / f"{label}.stdout"
    stderr = directory / f"{label}.stderr"
    environment = os.environ.copy()
    for key in ("JAVA_TOOL_OPTIONS", "JDK_JAVA_OPTIONS", "_JAVA_OPTIONS", "CLASSPATH"):
        environment.pop(key, None)
    started = time.monotonic()
    logs = list(directory.glob("*.stdout")) + list(directory.glob("*.stderr"))
    written = sum(path.stat().st_size for path in logs)
    if written > MAX_OUTPUT:
        raise ValueError("Previous reference output exceeds 16 MiB")
    lock = threading.Lock()
    overflow = threading.Event()
    reader_failed = threading.Event()

    def drain(source, destination):
        nonlocal written
        try:
            while chunk := source.read(65536):
                with lock:
                    remaining = MAX_OUTPUT - written
                    if len(chunk) > remaining:
                        overflow.set()
                    allowed = chunk[:remaining]
                    destination.write(allowed)
                    written += len(allowed)
        except (OSError, ValueError):
            reader_failed.set()
        finally:
            source.close()

    with stdout.open("xb") as out, stderr.open("xb") as err:
        process = subprocess.Popen(
            command, cwd=directory, env=environment, stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=False,
        )
        readers = [
            threading.Thread(target=drain, args=(process.stdout, out), daemon=True),
            threading.Thread(target=drain, args=(process.stderr, err), daemon=True),
        ]
        for reader in readers:
            reader.start()
        try:
            while True:
                if overflow.is_set():
                    raise ValueError("Reference subprocess output exceeds 16 MiB")
                if reader_failed.is_set():
                    raise ValueError("Reference output reader failed")
                _storage(directory)
                if time.monotonic() - started > seconds:
                    raise TimeoutError(f"Reference {label} exceeded {seconds} seconds")
                code = process.poll()
                if code is not None:
                    break
                time.sleep(0.1)
        finally:
            if process.poll() is None:
                process.kill()
            process.wait(timeout=10)
            deadline = time.monotonic() + 10
            for reader in readers:
                reader.join(timeout=max(0, deadline - time.monotonic()))
            if any(reader.is_alive() for reader in readers):
                raise TimeoutError("Reference output reader cleanup exceeded 10 seconds")
        if overflow.is_set() or reader_failed.is_set():
            raise ValueError("Reference output exceeded its cap or could not be read")
    _storage(directory)
    if code:
        raise RuntimeError(f"Reference {label} exited {code}; inspect {stderr}")
    return {
        "argv": command, "timeout_seconds": seconds, "exit_code": code,
        "stdout": {"path": stdout.relative_to(ROOT).as_posix(), "sha256": digest(stdout), "bytes": stdout.stat().st_size},
        "stderr": {"path": stderr.relative_to(ROOT).as_posix(), "sha256": digest(stderr), "bytes": stderr.stat().st_size},
    }


def _cases(raw: bytes) -> list[dict]:
    if len(raw) > MAX_OUTPUT:
        raise ValueError("Reference corpus output exceeds 16 MiB")
    cases = [json.loads(line) for line in raw.decode("utf-8").splitlines() if line.strip()]
    expected = [(name, operator) for name in CASE_IDS for operator in OPERATORS]
    if tuple(case.get("id") for case in cases) != tuple(f"{name}__{operator}" for name, operator in expected):
        raise ValueError("Reference did not emit the exact ordered 72 cases")
    for case, (name, operator) in zip(cases, expected):
        if (case.get("operation") != METHODS[operator] or case.get("canvas_wh") != [13, 9]
                or case.get("operand_set") != name or case.get("operator") != operator
                or case.get("operand_order") != "left_then_right"):
            raise ValueError("Unexpected Boolean reference operation/canvas")
        operands = case.get("operands")
        if not isinstance(operands, list) or len(operands) != 2:
            raise ValueError("Each reference case requires two explicit operands")
        for value in [*operands, case.get("result", {})]:
            rows = value.get("mask_rows")
            if not isinstance(rows, list) or len(rows) != 9 or any(
                not isinstance(row, str) or len(row) != 13 or set(row) - {"0", "1"}
                for row in rows
            ):
                raise ValueError("Reference masks must be nine binary rows of width 13")
            bounds = value.get("bounds_xywh")
            if not isinstance(bounds, list) or len(bounds) != 4 or any(type(v) is not int for v in bounds):
                raise ValueError("Invalid reference bounds")
            if type(value.get("vector_area_empty")) is not bool:
                raise ValueError("Missing reference empty-area state")
        if any(not isinstance(value.get("geometry"), dict) for value in operands):
            raise ValueError("Missing operand construction arguments")
        if name in ("entirely_outside", "two_empty_areas") and any(
            row != "0" * 13 for value in [*operands, case["result"]] for row in value["mask_rows"]
        ):
            raise ValueError("Outside/empty operand sets must capture explicit zero canvas masks")
        for operand in operands:
            if operand["geometry"].get("kind") == "empty_area" and (
                not operand["vector_area_empty"] or any(row != "0" * 13 for row in operand["mask_rows"])
            ):
                raise ValueError("Empty Area operand did not produce an explicit zero mask")
    return cases


def capture(output: Path, run_dir: Path) -> dict:
    """Run the fixed raw reference capture; never overwrite a previous attempt."""
    output, run_dir = _reference_path(output), _reference_path(run_dir)
    if run_dir.exists() or output.exists() or output.parent != run_dir or output.suffix != ".json":
        raise ValueError("Require a fresh run directory and a direct JSON output within it")
    downloads = ROOT / "build/references/downloads"
    imagej_path = downloads / f"imagej-{COMMIT}.zip"
    fiji_path = downloads / "fiji-latest-20261007-1617-win64-jdk.zip"
    if imagej_path.stat().st_size != 6146010 or fiji_path.stat().st_size != 705099776:
        raise ValueError("Unexpected pinned reference archive size")
    verified(_BoundedFile(imagej_path, 6146010, time.monotonic() + VERIFY_SECONDS), IMAGEJ_SHA)
    verified(_BoundedFile(fiji_path, 705099776, time.monotonic() + VERIFY_SECONDS), FIJI_SHA)
    imagej, fiji = imagej_path, fiji_path
    sources, runtime = _reference_path(REUSE / "sources"), _reference_path(REUSE / "jdk")
    source_members = _verify_tree(imagej, f"ImageJ-{COMMIT}/", sources, SOURCE_LIMIT)
    runtime_members = _verify_tree(fiji, JDK_PREFIX, runtime, JDK_LIMIT)
    if any(source_members.get(name, {}).get("sha256") != sha for name, sha in SOURCE_PINS.items()):
        raise ValueError("Known ImageJ source/license pins differ")
    run_dir.mkdir(parents=True, exist_ok=False)
    classes = run_dir / "classes"
    classes.mkdir()
    driver = run_dir / "RoiBooleanReference.java"
    driver.write_bytes(DRIVER.encode("utf-8"))
    java_sources = [sources / name for name in source_members if name.startswith("ij/") and name.endswith(".java")]
    args = run_dir / "javac.args"
    args.write_text("\n".join('"' + path.as_posix() + '"' for path in [*java_sources, driver]) + "\n", encoding="utf-8")
    compiled = _run([
        str(runtime / "bin/javac.exe"), "-J-Xmx512m", "-encoding", "UTF-8", "-proc:none",
        "-classpath", str(classes), "-d", str(classes), "@" + str(args),
    ], run_dir, "javac", 180)
    for name in source_members:
        if "/" not in name and name.endswith(".txt"):
            shutil.copyfile(sources / name, classes / name)
    version = _run([str(runtime / "bin/java.exe"), "-Xmx256m", "-version"], run_dir, "version", 10)
    result = _run([
        str(runtime / "bin/java.exe"), "-Xmx256m", "-Djava.awt.headless=true", "-cp", str(classes), "RoiBooleanReference",
    ], run_dir, "reference", 120)
    cases = _cases((run_dir / "reference.stdout").read_bytes())
    if source_members != _verify_tree(imagej, f"ImageJ-{COMMIT}/", sources, SOURCE_LIMIT):
        raise ValueError("Source tree changed during capture")
    if runtime_members != _verify_tree(fiji, JDK_PREFIX, runtime, JDK_LIMIT):
        raise ValueError("JDK tree changed during capture")
    input_contract = {
        "canvas_wh": [13, 9], "operators": METHODS, "operand_order": "left_then_right",
        "operand_sets": {case["operand_set"]: [value["geometry"] for value in case["operands"]]
                         for case in cases if case["operator"] == "union"},
    }
    payload = {
        "schema_version": 1,
        "scope": "Direct ShapeRoi.or/and/not/xor geometric area Boolean operations followed by ShapeRoi.getMask and clipping to a 13x9 canvas. Ordered subtraction is left minus right. Empty operand is ShapeRoi(new java.awt.geom.Area()), not an imported pixel-mask ROI. No RoiManager, T/C/Z applicability, manager selection, GUI, style, undo/history, or target parity credit.",
        "commercial_eligibility": "not_decided",
        "empty_area_operand_supported": True,
        "imagej_version": "1.54r", "imagej_commit": COMMIT,
        "source_url": f"https://codeload.github.com/imagej/ImageJ/zip/{COMMIT}",
        "source_archive_sha256": IMAGEJ_SHA, "fiji_runtime_archive_sha256": FIJI_SHA,
        "license_members": {name: source_members[name] for name in ("LICENSE.txt", "pom.xml")},
        "license_facts": {"archive_license": "ImageJ public-domain disclaimer", "pom_license_name": "Public domain", "pom_license_url": "https://imagej.net/ij/disclaimer.html", "ShapeRoi_source_declaration": "This code is in the public domain."},
        "known_source_members": {name: source_members[name] for name in SOURCE_PINS},
        "verified_source_tree": source_members, "verified_runtime_tree": runtime_members,
        "compiled_classpath": {"path": classes.relative_to(ROOT).as_posix(), "members": _files(classes, MAX_BUILD)},
        "driver_sha256": digest(driver), "driver_bytes": driver.stat().st_size,
        "input_contract": input_contract,
        "input_contract_sha256": hashlib.sha256(json.dumps(input_contract, sort_keys=True, separators=(",", ":")).encode("utf-8")).hexdigest(),
        "exact_ordered_case_ids": [case["id"] for case in cases],
        "generator": "scripts/capture_roi_boolean_reference.py", "generator_sha256": digest(Path(__file__)),
        "verification_helper_sha256": digest(ROOT / "scripts/capture_binary_reference.py"),
        "runtime_version": (run_dir / "version.stderr").read_text(encoding="utf-8").strip(),
        "processes": {"compile": compiled, "version": version, "reference": result},
        "bounds": {"compile_seconds": 180, "reference_seconds": 120, "javac_heap_mib": 512, "java_heap_mib": 256, "output_bytes": MAX_OUTPUT, "build_bytes": MAX_BUILD, "max_files": MAX_FILES},
        "cases": cases,
    }
    encoded = (json.dumps(payload, separators=(",", ":")) + "\n").encode("utf-8")
    logs_size = sum(path.stat().st_size for path in run_dir.glob("*.stdout")) + sum(path.stat().st_size for path in run_dir.glob("*.stderr"))
    if len(encoded) + logs_size > MAX_OUTPUT:
        raise ValueError("Corpus and process output exceed 16 MiB combined")
    size, count = _storage(run_dir)
    if size + len(encoded) > MAX_BUILD or count + 1 > MAX_FILES:
        raise ValueError("Adding corpus would exceed the capture storage/member bound")
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("xb") as stream:
        stream.write(encoded)
    _storage(run_dir)
    return payload


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-dir", type=Path, default=STAGE / "capture-001")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    payload = capture(args.output, args.run_dir)
    print(f"Captured {len(payload['cases'])} raw ImageJ 1.54r ShapeRoi Boolean cases: {args.output}")


if __name__ == "__main__":
    main()
