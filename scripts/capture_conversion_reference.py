"""Capture uncalibrated type conversion and unsigned inversion from ImageJ 1.54r.

The project-owned Java driver runs only in excluded build/reference directories.
It verifies/recompiles pinned production sources; no upstream implementation or
runtime is incorporated into the native library, desktop, wheel or source package.
"""

from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
import zipfile

from capture_binary_reference import (
    ROOT,
    COMMIT,
    IMAGEJ_SHA,
    FIJI_SHA,
    JDK_PREFIX,
    verified,
    extract,
    digest,
)

WORK = ROOT / "build/references/conversion-imagej154r"
OUTPUT = ROOT / "tests/fixtures/conversion-imagej154r.json"

DRIVER = r"""
import ij.ImagePlus;
import ij.Prefs;
import ij.process.*;

public class ConversionReference {
    static double[] gray(String type, int pattern) {
        double[] values = type.equals("uint8")
            ? new double[]{0,1,2,3,4,10,20,50,100,127,128,129,200,253,254,255}
            : type.equals("uint16")
                ? new double[]{0,1,10,255,256,512,1023,2048,4095,4096,8192,16383,32767,65533,65534,65535}
                : new double[]{-100f,-.5f,0f,.49999997f,.5f,.50000006f,1f,10.25f,20.75f,90.5f,254.49998f,254.5f,255f,300f,65534.5f,70000f};
        if (pattern == 1) for (int i = 0; i < values.length; ++i) values[i] = 7;
        if (pattern == 2) for (int i = 0; i < values.length; ++i) values[i] = type.equals("float32") ? (float)(i * .125 - 1) : i + 10;
        return values;
    }
    static double[] rgb(int pattern) {
        double[] values = new double[48];
        for (int i = 0; i < 16; ++i) {
            values[3*i] = (i*71+1) % 256;
            values[3*i+1] = (i*43+17) % 256;
            values[3*i+2] = (i*23+89) % 256;
        }
        if (pattern == 1) for (int i = 0; i < values.length; ++i) values[i] = 123;
        if (pattern == 2) for (int i = 0; i < 16; ++i)
            for (int c = 0; c < 3; ++c) values[3*i+c] = (i % 4 == c) ? 255 : 0;
        return values;
    }
    static ImageProcessor processor(String type, double[] values) {
        if (type.equals("rgb8")) {
            int[] pixels = new int[16];
            for (int i = 0; i < 16; ++i) pixels[i] = ((int)values[3*i] << 16) | ((int)values[3*i+1] << 8) | (int)values[3*i+2];
            return new ColorProcessor(8, 2, pixels);
        }
        if (type.equals("uint8")) {
            byte[] pixels = new byte[16]; for (int i = 0; i < 16; ++i) pixels[i] = (byte)values[i];
            return new ByteProcessor(8, 2, pixels, null);
        }
        if (type.equals("uint16")) {
            short[] pixels = new short[16]; for (int i = 0; i < 16; ++i) pixels[i] = (short)values[i];
            return new ShortProcessor(8, 2, pixels, null);
        }
        float[] pixels = new float[16]; for (int i = 0; i < 16; ++i) pixels[i] = (float)values[i];
        return new FloatProcessor(8, 2, pixels);
    }
    static String array(double[] values) {
        StringBuilder text = new StringBuilder("[");
        for (int i = 0; i < values.length; ++i) { if (i != 0) text.append(','); text.append(values[i]); }
        return text.append(']').toString();
    }
    static String pixels(ImageProcessor processor) {
        double[] values = new double[16];
        for (int i = 0; i < 16; ++i) values[i] = processor.getf(i);
        return array(values);
    }
    static void conversion(String type, String output, boolean scale, String policy, int pattern,
                           boolean weighted, double[] weights) {
        boolean rgb = type.equals("rgb8");
        double[] source = rgb ? rgb(pattern) : gray(type, pattern);
        ImageProcessor processor = processor(type, source);
        double minimum = 0, maximum = 1;
        if (policy.equals("dtype")) {
            maximum = type.equals("uint16") ? 65535 : type.equals("float32") ? 1 : 255;
            processor.setMinAndMax(minimum, maximum);
        } else if (policy.equals("explicit")) {
            minimum = 10.25; maximum = 90.5;
            processor.setMinAndMax(minimum, maximum);
        } else if (policy.equals("constant")) {
            minimum = 7; maximum = 7;
            processor.setMinAndMax(minimum, maximum);
        } else if (policy.equals("zero")) {
            minimum = 0; maximum = 0;
            processor.setMinAndMax(minimum, maximum);
        } else if (policy.equals("fractional")) {
            minimum = -.1; maximum = 90.49999999;
            processor.setMinAndMax(minimum, maximum);
        }
        ColorProcessor.setWeightingFactors(weighted ? weights[0] : 1d/3d,
            weighted ? weights[1] : 1d/3d, weighted ? weights[2] : 1d/3d);
        ImageProcessor result = output.equals("uint8") ? processor.convertToByte(scale)
            : output.equals("uint16") ? processor.convertToShort(scale) : processor.convertToFloat();
        System.out.println("{\"operation\":\"convert_image\",\"source_dtype\":\""+(rgb ? "uint8" : type)
            +"\",\"source_samples\":"+(rgb ? 3 : 1)+",\"shape\":[2,8"+(rgb ? ",3" : "")
            +"],\"pattern\":"+pattern+",\"source\":"+array(source)+",\"dtype\":\""+output
            +"\",\"parameters\":{\"scale\":"+scale+",\"weighted_rgb\":"+weighted
            +",\"red_weight\":"+weights[0]+",\"green_weight\":"+weights[1]+",\"blue_weight\":"+weights[2]
            +",\"range_policy\":\""+(policy.equals("constant") || policy.equals("zero") || policy.equals("fractional") ? "explicit" : policy)
            +"\",\"range_min\":"+minimum+",\"range_max\":"+maximum+"},\"pixels\":"+pixels(result)+"}");
    }
    static void inversion(int pattern, boolean full, int bits, boolean display) {
        double[] source = gray("uint16", pattern);
        ShortProcessor processor = (ShortProcessor)processor("uint16", source);
        if (display) processor.setMinAndMax(10, 20);
        Prefs.fullRange16bitInversions = full;
        ImagePlus.setDefault16bitRange(bits);
        processor.invert();
        System.out.println("{\"operation\":\"invert_uint16\",\"source_dtype\":\"uint16\",\"source_samples\":1"
            +",\"shape\":[2,8],\"pattern\":"+pattern+",\"source\":"+array(source)
            +",\"source_display_range\":"+(display ? "[10,20]" : "null")
            +",\"parameters\":{\"full_range_inversions\":"+full+",\"unsigned16_bits\":"+bits
            +"},\"pixels\":"+pixels(processor)+"}");
    }
    public static void main(String[] args) {
        if (!ij.IJ.getVersion().equals("1.54r")) throw new IllegalStateException("Unexpected reference version");
        String[] outputs = {"uint8", "uint16", "float32"};
        for (String type : new String[]{"uint8", "uint16", "float32"})
            for (String output : outputs)
                for (boolean scale : new boolean[]{false, true})
                    for (String policy : new String[]{"data", "dtype", "explicit", "constant"})
                        for (int pattern = 0; pattern < 3; ++pattern)
                            conversion(type, output, scale, policy, pattern, false, new double[]{.299,.587,.114});
        double[][] weights = {{.299,.587,.114}, {.299,.587,.114}, {1,0,0}, {.5,.25,.25}, {2,-.5,.75}};
        for (int w = 0; w < weights.length; ++w)
            for (String output : outputs)
                for (boolean scale : new boolean[]{false, true})
                    for (int pattern = 0; pattern < 3; ++pattern)
                        conversion("rgb8", output, scale, "data", pattern, w != 0, weights[w]);
        for (int bits : new int[]{0,8,10,12,14,15,16})
            for (boolean full : new boolean[]{false,true})
                for (int pattern = 0; pattern < 3; ++pattern)
                    for (boolean display : new boolean[]{false,true}) inversion(pattern, full, bits, display);
        // Preserve the original 390 cases and append display-range semantics.
        for (String type : new String[]{"uint16", "float32"})
            for (String output : outputs)
                for (boolean scale : new boolean[]{false,true})
                    for (String policy : new String[]{"zero", "fractional"})
                        for (int pattern = 0; pattern < 3; ++pattern)
                            conversion(type, output, scale, policy, pattern, false, new double[]{.299,.587,.114});
    }
}
"""


def main():
    downloads = ROOT / "build/references/downloads"
    imagej = verified(downloads / f"imagej-{COMMIT}.zip", IMAGEJ_SHA)
    fiji = verified(downloads / "fiji-latest-20261007-1617-win64-jdk.zip", FIJI_SHA)
    shared = ROOT / "build/references/binary-imagej154r"
    sources, runtime, classes = shared / "sources", shared / "jdk", WORK / "classes"
    with zipfile.ZipFile(imagej) as archive:
        extract(archive, f"ImageJ-{COMMIT}/", sources)
    with zipfile.ZipFile(fiji) as archive:
        extract(archive, JDK_PREFIX, runtime)
    classes.mkdir(parents=True, exist_ok=True)
    driver = WORK / "ConversionReference.java"
    driver.write_text(DRIVER, encoding="utf-8")
    args = WORK / "javac.args"
    args.write_text(
        "\n".join(
            '"' + p.as_posix() + '"'
            for p in sorted((sources / "ij").rglob("*.java")) + [driver]
        )
        + "\n",
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
    )
    (WORK / "javac.log").write_text(compiled.stdout + compiled.stderr, encoding="utf-8")
    compiled.check_returncode()
    for resource in sources.glob("*.txt"):
        shutil.copyfile(resource, classes / resource.name)
    result = subprocess.run(
        [
            str(runtime / "bin/java.exe"),
            "-Djava.awt.headless=true",
            "-cp",
            str(classes),
            "ConversionReference",
        ],
        capture_output=True,
        text=True,
        timeout=120,
    )
    (WORK / "reference.jsonl").write_text(result.stdout, encoding="utf-8")
    (WORK / "java.log").write_text(result.stderr, encoding="utf-8")
    result.check_returncode()
    cases = [json.loads(line) for line in result.stdout.splitlines() if line.strip()]
    assert len(cases) == 462
    members = [
        "ij/process/TypeConverter.java",
        "ij/process/FloatProcessor.java",
        "ij/process/ShortProcessor.java",
        "ij/process/ColorProcessor.java",
    ]
    payload = {
        "schema_version": 1,
        "scope": "Direct uncalibrated ImageProcessor type conversions and unsigned ShortProcessor inversion; not density-calibrated/signed/LUT/threshold/stack/GUI or plugin certification. Float64/RGBA are native extensions outside this corpus.",
        "imagej_version": "1.54r",
        "imagej_commit": COMMIT,
        "source_url": f"https://codeload.github.com/imagej/ImageJ/zip/{COMMIT}",
        "source_archive_sha256": IMAGEJ_SHA,
        "fiji_runtime_archive_sha256": FIJI_SHA,
        "source_members_sha256": {
            member: digest(sources / member) for member in members
        },
        "driver_sha256": hashlib.sha256(DRIVER.encode("utf-8")).hexdigest(),
        "generator": "scripts/capture_conversion_reference.py",
        "cases": cases,
    }
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(
        json.dumps(payload, separators=(",", ":")) + "\n", encoding="utf-8"
    )
    print(
        f"Captured {len(cases)} pinned ImageJ 1.54r conversion/inversion cases; {OUTPUT.relative_to(ROOT)}"
    )


if __name__ == "__main__":
    main()
