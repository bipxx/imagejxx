"""Capture raw ByteProcessor fixtures from the verified ImageJ 1.54r source pin.

The pinned Fiji JDK is used only under build/references. No upstream Java source
or runtime is incorporated into the core, desktop, Python package or fixture.
Fixtures cover full-plane byte morphology, not plugin LUT/ROI/UI behavior.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
COMMIT = "62db90a269cccc1ac79f658e4602043214ed055b"
IMAGEJ_SHA = "09e31a88191d10c7f79c607cb6bb62342476bde733c5d932d5825fd553796665"
FIJI_SHA = "8e33a11c7e79eb02c055bcee40de806d16e59f0b42e58c4689bd23eb47d990bb"
JDK_PREFIX = "Fiji/java/win64/zulu21.42.19-ca-jdk21.0.7-win_x64/"
WORK = ROOT / "build/references/binary-imagej154r"
OUTPUT = ROOT / "tests/fixtures/binary-imagej154r.json"

# Project-owned reference driver. Its test inputs and option combinations are
# explicit so the Java and C++ executions do not share an implementation.
DRIVER = r"""
import ij.Prefs;
import ij.process.ByteProcessor;

public class BinaryReference {
    static byte[] pixels(int width, int height, int pattern) {
        byte[] result = new byte[width * height];
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                boolean foreground;
                if (pattern == 0) foreground = ((x * 3 + y * 7) % 11 < 6) || (x == 4 && y > 1);
                else if (pattern == 1) foreground = true;
                else foreground = x >= 1 && y >= 1 && x < width-1 && y < height-1
                    && !(x >= 3 && x <= 4 && y >= 2 && y <= 3);
                result[y * width + x] = (byte)(foreground ? 255 : 0);
            }
        return result;
    }
    static String values(byte[] pixels) {
        StringBuilder result = new StringBuilder("[");
        for (int i = 0; i < pixels.length; ++i) {
            if (i != 0) result.append(',');
            result.append(pixels[i] & 255);
        }
        return result.append(']').toString();
    }
    static void repeat(ByteProcessor image, boolean erode, int iterations, int count, int background) {
        for (int i = 0; i < iterations; ++i) {
            if (erode) image.erode(count, background);
            else image.dilate(count, background);
        }
    }
    static void capture(String operation, int iterations, int count, boolean black, boolean pad,
                        int width, int height, int pattern) {
        byte[] source = pixels(width, height, pattern);
        ByteProcessor image = new ByteProcessor(width, height, source.clone(), null);
        Prefs.padEdges = pad;
        int background = black ? 0 : 255;
        boolean erosion = operation.equals("erode") || operation.equals("open");
        try {
            repeat(image, erosion, iterations, count, background);
            if (operation.equals("open") || operation.equals("close"))
                repeat(image, !erosion, iterations, count, background);
            System.out.println("{\"operation\":\""+operation+"\",\"iterations\":"+iterations
                +",\"neighbors\":"+count+",\"black_background\":"+black+",\"pad_edges\":"+pad
                +",\"shape\":["+height+","+width+"],\"pattern\":"+pattern
                +",\"source\":"+values(source)+",\"pixels\":"+values((byte[])image.getPixels())+"}");
        } catch (RuntimeException error) {
            System.out.println("{\"operation\":\""+operation+"\",\"iterations\":"+iterations
                +",\"neighbors\":"+count+",\"black_background\":"+black+",\"pad_edges\":"+pad
                +",\"shape\":["+height+","+width+"],\"pattern\":"+pattern
                +",\"source\":"+values(source)+",\"reference_error\":\""
                +error.getClass().getSimpleName()+"\"}");
        }
    }
    public static void main(String[] args) {
        String[] operations = {"erode", "dilate", "open", "close"};
        for (String operation : operations)
            for (int iterations = 1; iterations <= 3; ++iterations)
                for (int count = 1; count <= 8; ++count)
                    for (boolean black : new boolean[]{false, true})
                        for (boolean pad : new boolean[]{false, true})
                            for (int pattern = 0; pattern < 3; ++pattern)
                                capture(operation, iterations, count, black, pad, 9, 7, pattern);
        for (String operation : operations)
            for (boolean pad : new boolean[]{false, true})
                for (int[] size : new int[][]{{1,1},{1,7},{7,1},{2,2}})
                    capture(operation, 1, 1, true, pad, size[0], size[1], 0);
    }
}
"""


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def verified(path, expected):
    if digest(path) != expected:
        raise ValueError(f"Pinned archive verification failed: {path.name}")
    return path


def extract(archive, prefix, destination):
    destination.mkdir(parents=True, exist_ok=True)
    for member in archive.infolist():
        if not member.filename.startswith(prefix) or member.is_dir():
            continue
        relative = PurePosixPath(member.filename[len(prefix) :])
        if relative.is_absolute() or ".." in relative.parts or "\\" in str(relative):
            raise ValueError("Unsafe path in pinned archive")
        target = destination.joinpath(*relative.parts)
        if not target.resolve().is_relative_to(destination.resolve()):
            raise ValueError("Pinned archive path escapes its reference directory")
        target.parent.mkdir(parents=True, exist_ok=True)
        with archive.open(member) as source, target.open("wb") as output:
            shutil.copyfileobj(source, output)


def main():
    downloads = ROOT / "build/references/downloads"
    imagej = verified(downloads / f"imagej-{COMMIT}.zip", IMAGEJ_SHA)
    fiji = verified(downloads / "fiji-latest-20261007-1617-win64-jdk.zip", FIJI_SHA)
    sources, runtime, classes = (WORK / name for name in ("sources", "jdk", "classes"))
    with zipfile.ZipFile(imagej) as archive:
        extract(archive, f"ImageJ-{COMMIT}/", sources)
    with zipfile.ZipFile(fiji) as archive:
        extract(archive, JDK_PREFIX, runtime)
    classes.mkdir(parents=True, exist_ok=True)
    driver = WORK / "BinaryReference.java"
    driver.write_text(DRIVER, encoding="utf-8")
    args = WORK / "javac.args"
    java_sources = sorted((sources / "ij").rglob("*.java")) + [driver]
    args.write_text(
        "\n".join('"' + path.as_posix() + '"' for path in java_sources) + "\n",
        encoding="utf-8",
    )
    compiled = subprocess.run(
        [
            str(runtime / "bin/javac.exe"),
            "-encoding",
            "UTF-8",
            "-d",
            str(classes),
            "@" + str(args),
        ],
        capture_output=True,
        text=True,
        timeout=180,
        check=False,
    )
    (WORK / "javac.log").write_text(compiled.stdout + compiled.stderr, encoding="utf-8")
    compiled.check_returncode()
    for resource in sources.glob("*.txt"):
        shutil.copyfile(resource, classes / resource.name)
    runtime_version = subprocess.run(
        [str(runtime / "bin/java.exe"), "-version"],
        capture_output=True,
        text=True,
        check=True,
    ).stderr.strip()
    result = subprocess.run(
        [
            str(runtime / "bin/java.exe"),
            "-Djava.awt.headless=true",
            "-cp",
            str(classes),
            "BinaryReference",
        ],
        capture_output=True,
        text=True,
        timeout=120,
        check=True,
    )
    (WORK / "reference.jsonl").write_text(result.stdout, encoding="utf-8")
    cases = [json.loads(line) for line in result.stdout.splitlines() if line.strip()]
    assert len(cases) == 1184
    payload = {
        "schema_version": 1,
        "scope": "Direct full-plane ByteProcessor.erode/dilate with explicit background and Prefs.padEdges; open/close stage composition. Not Binary plugin LUT/ROI/UI/EDM certification.",
        "imagej_version": "1.54r",
        "imagej_commit": COMMIT,
        "source_url": f"https://codeload.github.com/imagej/ImageJ/zip/{COMMIT}",
        "source_archive_sha256": IMAGEJ_SHA,
        "fiji_runtime_archive_sha256": FIJI_SHA,
        "source_member": f"ImageJ-{COMMIT}/ij/process/ByteProcessor.java",
        "source_member_sha256": digest(sources / "ij/process/ByteProcessor.java"),
        "driver_sha256": hashlib.sha256(DRIVER.encode("utf-8")).hexdigest(),
        "runtime_version": runtime_version,
        "generator": "scripts/capture_binary_reference.py",
        "cases": cases,
    }
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(
        json.dumps(payload, separators=(",", ":")) + "\n", encoding="utf-8"
    )
    errors = sum("reference_error" in case for case in cases)
    print(
        f"Captured {len(cases)} pinned ImageJ 1.54r cases ({errors} reference errors); {OUTPUT.relative_to(ROOT)}"
    )


if __name__ == "__main__":
    main()
