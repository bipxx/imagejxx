"""Capture direct color/ROI setter states from the verified ImageJ 1.54r pin.

The owned driver is reference tooling only. Java sources and the Fiji JDK remain
under excluded build/reference directories; no upstream code/runtime enters the
native library, UI, Python wheel, source package or JSON corpus. This captures
setter states, not dialog interaction, processing masks or reference raster parity.
"""

from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
import zipfile

from capture_binary_reference import (
    ROOT, COMMIT, IMAGEJ_SHA, FIJI_SHA, JDK_PREFIX, verified, extract, digest,
)

WORK = ROOT / "build/references/roi-style-imagej154r"
OUTPUT = ROOT / "tests/fixtures/roi-style-imagej154r.json"

DRIVER = r"""
import java.awt.Color;
import ij.gui.Roi;
import ij.gui.OvalRoi;
import ij.gui.PolygonRoi;
import ij.gui.Line;
import ij.gui.PointRoi;
import ij.plugin.Colors;

public class RoiStyleReference {
    static String rgba(Color value) {
        if (value == null) return "null";
        return "[" + value.getRed() + "," + value.getGreen() + ","
            + value.getBlue() + "," + value.getAlpha() + "]";
    }
    static Roi roi(String kind) {
        if (kind.equals("rectangle")) return new Roi(2, 2, 5, 5);
        if (kind.equals("ellipse")) return new OvalRoi(2, 2, 5, 5);
        if (kind.equals("polygon"))
            return new PolygonRoi(new int[]{2,7,4}, new int[]{2,2,7}, 3, Roi.POLYGON);
        if (kind.equals("line")) return new Line(2, 4, 6, 4);
        if (kind.equals("polyline"))
            return new PolygonRoi(new int[]{2,6,6}, new int[]{4,4,7}, 3, Roi.POLYLINE);
        if (kind.equals("multipoint"))
            return new PointRoi(new float[]{2,7}, new float[]{2,7}, 2);
        throw new IllegalArgumentException(kind);
    }
    static void state(String operation, String kind, String name,
                      String patches, Roi roi) {
        System.out.println("{\"operation\":\"" + operation + "\",\"kind\":\""
            + kind + "\",\"case\":\"" + name + "\",\"patches\":" + patches
            + ",\"stroke\":" + rgba(roi.getStrokeColor())
            + ",\"fill\":" + rgba(roi.getFillColor())
            + ",\"width\":" + roi.getStrokeWidth() + "}");
    }
    static void setters(String kind) {
        Color fill = Colors.decode("#80123456", null);
        Color stroke = Colors.decode("#80403020", null);
        Roi value = roi(kind);
        value.setStrokeColor(Color.red); value.setFillColor(fill);
        state("setters", kind, "fill_only",
            "[{\"stroke_color\":\"red\"},{\"fill_color\":\"#80123456\"}]", value);
        value = roi(kind);
        value.setFillColor(fill); value.setStrokeColor(stroke);
        state("setters", kind, "fill_then_stroke",
            "[{\"fill_color\":\"#80123456\",\"stroke_color\":\"#80403020\"}]", value);
        value = roi(kind);
        value.setStrokeColor(stroke); value.setFillColor(fill);
        state("setters", kind, "stroke_then_fill",
            "[{\"stroke_color\":\"#80403020\"},{\"fill_color\":\"#80123456\"}]", value);
        value = roi(kind);
        value.setStrokeColor(Color.red); value.setFillColor(fill); value.setFillColor(null);
        state("setters", kind, "clear_fill",
            "[{\"stroke_color\":\"red\"},{\"fill_color\":\"#80123456\"},{\"fill_color\":null}]", value);
        value = roi(kind);
        value.setStrokeColor(Color.red); value.setFillColor(Colors.decode("#00123456", null));
        state("setters", kind, "transparent_fill",
            "[{\"stroke_color\":\"red\"},{\"fill_color\":\"#00123456\"}]", value);
        value = roi(kind);
        value.setStrokeColor(Color.red); value.setStrokeColor(null);
        state("setters", kind, "null_stroke_no_fill",
            "[{\"stroke_color\":\"red\"},{\"stroke_color\":null}]", value);
        for (double width : new double[]{0, .5, 4}) {
            value = roi(kind); value.setStrokeWidth(width);
            state("width", kind, "width_" + width,
                "[{\"stroke_width\":" + width + "}]", value);
        }
    }
    public static void main(String[] args) {
        if (!ij.IJ.getVersion().equals("1.54r"))
            throw new IllegalStateException("Unexpected reference version");
        for (String text : new String[]{"red","green","blue","magenta","cyan",
                "yellow","orange","black","white","gray","grey","lightgray",
                "darkgray","pink","#123456","#80123456","#00123456","#FFABCDEF","none"})
            System.out.println("{\"operation\":\"color\",\"input\":\"" + text
                + "\",\"rgba\":" + rgba(Colors.decode(text, null)) + "}");
        for (String kind : new String[]{"rectangle","ellipse","polygon","line","polyline","multipoint"})
            setters(kind);
    }
}
"""


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
    driver = WORK / "RoiStyleReference.java"
    driver.write_text(DRIVER, encoding="utf-8")
    args = WORK / "javac.args"
    args.write_text(
        "\n".join('"' + path.as_posix() + '"' for path in sorted((sources / "ij").rglob("*.java")) + [driver]) + "\n",
        encoding="utf-8",
    )
    compiled = subprocess.run(
        [str(runtime / "bin/javac.exe"), "-encoding", "UTF-8", "-d", str(classes), "@" + str(args)],
        capture_output=True, text=True, timeout=180, check=False,
    )
    (WORK / "javac.log").write_text(compiled.stdout + compiled.stderr, encoding="utf-8")
    compiled.check_returncode()
    for resource in sources.glob("*.txt"):
        shutil.copyfile(resource, classes / resource.name)
    runtime_version = subprocess.run(
        [str(runtime / "bin/java.exe"), "-version"], capture_output=True, text=True, check=True,
    ).stderr.strip()
    result = subprocess.run(
        [str(runtime / "bin/java.exe"), "-Djava.awt.headless=true", "-cp", str(classes), "RoiStyleReference"],
        capture_output=True, text=True, timeout=120, check=False,
    )
    (WORK / "reference.jsonl").write_text(result.stdout, encoding="utf-8")
    (WORK / "java.log").write_text(result.stderr, encoding="utf-8")
    result.check_returncode()
    cases = [json.loads(line) for line in result.stdout.splitlines() if line.strip()]
    if len(cases) != 73:
        raise ValueError(f"Expected 73 direct ROI/color states, received {len(cases)}")
    members = ["ij/plugin/Colors.java", "ij/gui/Roi.java", "ij/gui/RoiProperties.java", "ij/gui/PolygonRoi.java", "ij/gui/Line.java", "ij/gui/PointRoi.java"]
    payload = {
        "schema_version": 1,
        "scope": "Direct Colors.decode and Roi setter state for names/alpha-first hex/null, fill/stroke order and finite zero/positive widths. Not RoiProperties GUI, processing-mask or display/antialias raster certification. Native rejection rules and huge finite widths are extensions outside this corpus.",
        "imagej_version": "1.54r", "imagej_commit": COMMIT,
        "source_url": f"https://codeload.github.com/imagej/ImageJ/zip/{COMMIT}",
        "source_archive_sha256": IMAGEJ_SHA,
        "fiji_runtime_archive_sha256": FIJI_SHA,
        "source_members_sha256": {member: digest(sources / member) for member in members},
        "driver_sha256": hashlib.sha256(DRIVER.encode("utf-8")).hexdigest(),
        "runtime_version": runtime_version,
        "generator": "scripts/capture_roi_style_reference.py", "cases": cases,
    }
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(json.dumps(payload, separators=(",", ":")) + "\n", encoding="utf-8")
    print(f"Captured {len(cases)} pinned ImageJ 1.54r ROI/color states; {OUTPUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
