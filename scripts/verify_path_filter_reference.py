"""Independent structural/mask-restoration checks for reference observations.

No imagejxx imports or numerical filter implementation. This validates captured
Java outputs, not product parity. Numerical outputs remain reference data.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import struct


def require(condition, message):
    if not condition:
        raise ValueError(message)


def words(values, count):
    require(isinstance(values, list) and len(values) == count, "Wrong F32 word count")
    require(all(isinstance(x, str) and re.fullmatch(r"0x[0-9a-f]{8}", x) for x in values),
            "Malformed F32 word")
    return values


def f32(value):
    return "0x" + struct.pack(">f", value).hex()


def rectangle(value):
    x, y = value["image_origin"]
    w, h = value["dimensions"]
    require(all(type(v) is int for v in (x, y, w, h)) and w >= 0 and h >= 0,
            "Invalid rectangle")
    return x, y, w, h


def mask_values(mask):
    x, y, w, h = rectangle(mask)
    require(w * h <= 1024 * 1024, "Unbounded reference mask")
    encoded = mask["u8_hex"]
    require(isinstance(encoded, str) and len(encoded) == 2 * w * h,
            "Wrong mask byte count")
    require(re.fullmatch(r"[0-9a-f]*", encoded) is not None, "Malformed mask hex")
    values = bytes.fromhex(encoded)
    require(set(values) <= {0, 255}, "Nonbinary mask")
    return (x, y, w, h), values


def project_mask(mask, width, height):
    result = bytearray(width * height)
    if mask is None:
        return result
    (x, y, w, h), values = mask_values(mask)
    for j in range(max(y, 0), min(y + h, height)):
        for i in range(max(x, 0), min(x + w, width)):
            result[j * width + i] = values[(j-y) * w + i-x]
    return result


def validate_observation(actual, request, old_mask=None):
    width, height = request["width"], request["height"]
    require(actual["schema"] == "imagejxx.reference.path-filter-target"
            and actual["version"] == 1, "Unknown observation schema")
    for field in ("name", "mode", "sigma"):
        require(actual[field] == request[field], "Request mismatch: " + field)
    require(actual["imagej_version"] == "1.54r" and actual["threads"] == 1,
            "Wrong reference version/thread count")
    require(actual["commands_f32_words"] == [f32(v) for v in request["commands"]],
            "Input path words differ")
    require(rectangle(actual["input"]) == (0, 0, width, height), "Input shape differs")
    expected_input = [f32(((x*37+y*53+(x*y)%17)%257-128)/16)
                      for y in range(height) for x in range(width)]
    expected_input[0] = "0x80000000"
    if len(expected_input) > 1:
        expected_input[1] = "0x00000000"
    original = words(actual["input"]["f32_words"], width * height)
    require(original == expected_input, "Input sample words differ")
    require(actual["mask_absent_before_explicit_request"] is True,
            "Attachment unexpectedly allocated a mask")
    require(actual["roi_before_attachment"] == actual["roi_after_attachment"],
            "Retained ROI geometry changed during attachment")
    stored = rectangle(actual["roi_before_attachment"]["bounds"])
    clipped = rectangle(actual["processor_roi_before_mask"])
    if request["mode"] == "metadata-only":
        require(not ({"roi_mask", "processor_mask", "gaussian"} & actual.keys()),
                "Metadata-only observation constructed processing data")
        require(stored == (-1000000000, 1, 1000000050, 5)
                and clipped == (0, 1, 50, 5), "Extreme bounds differ")
        return {"pixels": width * height, "selected": None, "metadata_only": True}
    require(request["mode"] == "small-mask", "Unknown observation mode")
    full_mask, processor_mask = actual["roi_mask"], actual["processor_mask"]
    if full_mask is not None:
        require(rectangle(full_mask) == stored, "Full mask does not use stored bounds")
    if processor_mask is not None:
        require(rectangle(processor_mask) == rectangle(actual["processor_roi_after_mask"]),
                "Processor mask does not use clipped bounds")
        projected = project_mask(processor_mask, width, height)
        full_projected = project_mask(full_mask, width, height)
        require(projected == full_projected, "Full/clipped masks disagree on canvas")
    if old_mask is not None:
        require(project_mask(full_mask, width, height) ==
                bytes(255 if v == "1" else 0 for v in old_mask),
                "Historical composite mask differs")
    gaussian = actual["gaussian"]
    require(gaussian["sigma_x"] == request["sigma"]
            and gaussian["sigma_y"] == request["sigma"]
            and gaussian["accuracy"] == 0.0002, "Gaussian options differ")
    before = words(gaussian["before_mask_reset_f32_words"], width * height)
    after = words(gaussian["after_mask_reset_f32_words"], width * height)
    require(gaussian["processor_mask_is_null"] == (processor_mask is None)
            and gaussian["mask_reset_performed"] == (processor_mask is not None),
            "Null-mask restoration contract differs")
    x, y, w, h = rectangle(actual["processor_roi_after_mask"])
    selected = project_mask(processor_mask, width, height)
    if processor_mask is None:
        for j in range(y, y+h):
            for i in range(x, x+w):
                selected[j*width+i] = 255
    outside_changes = 0
    for index, admitted in enumerate(selected):
        i, j = index % width, index // width
        in_rectangle = x <= i < x+w and y <= j < y+h
        restored = processor_mask is not None and in_rectangle and not admitted
        require(after[index] == (original[index] if restored else before[index]),
                "Snapshot restoration/untouched word differs at " + str(index))
        outside_changes += not admitted and after[index] != original[index]
    return {"pixels": width * height, "selected": sum(bool(v) for v in selected),
            "metadata_only": False, "raw_reference_unselected_changes": outside_changes}


def validate_capture(receipt_path, composite_path):
    receipt_path = Path(receipt_path)
    receipt = json.loads(receipt_path.read_bytes())
    require(receipt["schema"] == "p13i.reference.capture.001"
            and receipt["state"] == "observed", "Capture is not terminal observed")
    required, selected = receipt["required_cases"], receipt["selected_cases"]
    ids = [row["id"] for row in receipt["cases"]]
    catalog_id = receipt.get("catalog_id", "primary")
    require(catalog_id in ("primary", "degenerate"), "Unknown reference catalog")
    if catalog_id == "degenerate":
        expected = [f"{name}-sigma{sigma}" for name in
                    ("horizontal-zero-height", "vertical-zero-width") for sigma in (1, 12)]
    else:
        names = [f"composite-{i:02}" for i in range(56)] + [
            "fractional-triangle", "curve-control-hull", "outside-canvas", "consecutive-moves"]
        expected = [f"{name}-sigma{sigma}" for name in names for sigma in (1, 12)]
        expected.append("negative-billion-metadata")
    require(required == expected, "Declared reference catalog membership differs")
    require(len(set(required)) == len(required) and len(set(ids)) == len(ids),
            "Duplicate reference cases")
    require(ids == selected == required and receipt["complete_catalog_observed"] is True,
            "Reference catalog is incomplete")
    composite = json.loads(Path(composite_path).read_bytes())["cases"]
    results = []
    for row in receipt["cases"]:
        path = receipt_path.parent / (row["id"] + ".json")
        raw = path.read_bytes()
        require(row["exit_code"] == 0 and len(raw) == row["report"]["bytes"]
                and hashlib.sha256(raw).hexdigest() == row["report"]["sha256"],
                "Report identity/exit differs")
        old = None
        if row["id"].startswith("composite-"):
            old = composite[int(row["id"].split("-")[1])]["mask"]
        results.append({"id": row["id"], **validate_observation(
            json.loads(raw), row["request"], old)})
    return results


def rank_input(request):
    width, height = request["width"], request["height"]
    kind, pattern = request["pixel_type"], request["input_pattern"]
    if kind == "f32":
        result = [f32(((x*37+y*53+(x*y)%17)%257-128)/16)
                  for y in range(height) for x in range(width)]
        result[0:2] = ["0x80000000", "0x00000000"]
        if pattern == "quiet_nan":
            replacements = [(0, "0x7fc12345"), (width*height-1, "0xffc02345"),
                            (2*width+3, "0x7fc01111"), (3*width+5, "0xffc02222")]
        elif pattern == "ieee":
            replacements = [(0, "0x7f800000"), (width*height-1, "0xff800000"),
                            (2*width+3, "0x7f800001"), (3*width+5, "0xffc02222")]
        else:
            require(pattern == "finite", "Unknown rank input pattern")
            replacements = []
        for index, word in replacements:
            result[index] = word
        return result
    if kind == "rgb":
        require(pattern == "finite", "Unsupported RGB input pattern")
        return [f"0x{seed%256:02x}{(seed*3+17)%256:02x}{(seed*5+29)%256:02x}"
                for y in range(height) for x in range(width)
                for seed in [(x*37+y*53+(x*y)%17)%257]]
    require(kind in ("u8", "u16") and pattern == "finite", "Unsupported rank input type/pattern")
    digits = 2 if kind == "u8" else 4
    return [f"0x{(seed % 256 if kind == 'u8' else seed*251):0{digits}x}"
            for y in range(height) for x in range(width)
            for seed in [(x*37+y*53+(x*y)%17)%257]]


def rank_samples(value, request):
    w, h, kind = request["width"], request["height"], request["pixel_type"]
    require(rectangle(value) == (0, 0, w, h) and value["pixel_type"] == kind,
            "Rank sample raster differs")
    result = value["sample_words"]
    digits = {"u8": 2, "u16": 4, "f32": 8, "rgb": 6}[kind]
    require(isinstance(result, list) and len(result) == w*h, "Rank word count differs")
    require(all(isinstance(word, str) and re.fullmatch(r"0x[0-9a-f]{"+str(digits)+"}", word)
                for word in result), "Malformed rank sample word")
    return result


def validate_rank_observation(actual, request, old_mask=None):
    require(actual["schema"] == "imagejxx.reference.path-rank-target" and actual["version"] == 1,
            "Unknown rank schema")
    require(actual["name"] == request["name"] and actual["imagej_version"] == "1.54r"
            and actual["threads"] == 1, "Rank reference envelope differs")
    require(actual["commands_f32_words"] == [f32(value) for value in request["commands"]],
            "Rank path input words differ")
    for field in ("operation", "pixel_type", "radius", "which_outliers", "threshold",
                  "light_background", "dont_subtract", "inverted_lut", "input_pattern"):
        require(actual["options"][field] == request[field], "Rank option differs: " + field)
    original = rank_samples(actual["input"], request)
    require(original == rank_input(request), "Rank input generator differs")
    require(actual["roi_before_attachment"] == actual["roi_after_attachment"],
            "Rank attachment changed original geometry")
    require(actual["mask_absent_before_explicit_request"] is True,
            "Rank attachment unexpectedly created mask")
    width, height = request["width"], request["height"]
    full, mask = actual["roi_mask"], actual["processor_mask"]
    if full is not None:
        require(rectangle(full) == rectangle(actual["roi_before_attachment"]["bounds"]),
                "Rank full mask origin differs")
    if mask is not None:
        require(rectangle(mask) == rectangle(actual["processor_roi_after_mask"]),
                "Rank clipped mask origin differs")
        require(project_mask(mask, width, height) == project_mask(full, width, height),
                "Rank full/clipped masks differ")
    if old_mask is not None:
        require(project_mask(full, width, height) ==
                bytes(255 if value == "1" else 0 for value in old_mask),
                "Historical rank target mask differs")
    selected = project_mask(mask, width, height)
    x, y, w, h = rectangle(actual["processor_roi_after_mask"])
    if mask is None:
        for j in range(y, y+h):
            for i in range(x, x+w):
                selected[j*width+i] = 255
    before = rank_samples(actual["before_mask_reset"], request)
    after = rank_samples(actual["after_mask_reset"], request)
    require(actual["mask_reset_performed"] == (mask is not None), "Rank reset null-mask contract differs")
    outside = 0
    for index, active in enumerate(selected):
        i, j = index % width, index // width
        reset = mask is not None and x <= i < x+w and y <= j < y+h and not active
        require(after[index] == (original[index] if reset else before[index]),
                "Rank snapshot restoration differs at " + str(index))
        outside += not active and after[index] != original[index]
    return {"pixels": width*height, "selected": sum(bool(v) for v in selected),
            "raw_reference_unselected_changes": outside}


def validate_rank_capture(receipt_path, composite_path):
    receipt_path = Path(receipt_path)
    receipt = json.loads(receipt_path.read_bytes())
    require(receipt["schema"] == "p13i.reference.capture.001" and receipt["state"] == "observed"
            and receipt["catalog_id"] in ("rank", "rank-scopes"), "Rank capture is not terminal observed")
    ids = [row["id"] for row in receipt["cases"]]
    scoped = receipt["catalog_id"] == "rank-scopes"
    require(len(ids) == (27 if scoped else 253) and len(set(ids)) == len(ids)
            and ids == receipt["required_cases"] == receipt["selected_cases"]
            and receipt["complete_catalog_observed"] is True, "Rank catalog incomplete/duplicated")
    prefix = "rank-scope" if scoped else "rank"
    require(all(name.startswith(f"{prefix}-{i:03}-") for i, name in enumerate(ids)),
            "Rank catalog order differs")
    old = json.loads(Path(composite_path).read_bytes())["cases"]
    old_masks = {tuple(row["path"]): row["mask"] for row in old}
    result = []
    for row in receipt["cases"]:
        raw = (receipt_path.parent / (row["id"]+".json")).read_bytes()
        require(row["exit_code"] == 0 and len(raw) == row["report"]["bytes"]
                and hashlib.sha256(raw).hexdigest() == row["report"]["sha256"],
                "Rank output identity differs")
        result.append({"id": row["id"], **validate_rank_observation(json.loads(raw), row["request"],
            old_masks.get(tuple(row["request"]["commands"])))})
    return result


def validate_typed_gaussian_observation(actual, request):
    require(actual["schema"] == "imagejxx.reference.typed-path-gaussian-target"
            and actual["version"] == 1, "Unknown typed Gaussian schema")
    require(actual["name"] == request["name"] and actual["imagej_version"] == "1.54r"
            and actual["threads"] == 1, "Typed Gaussian reference envelope differs")
    require(actual["commands_f32_words"] == [f32(v) for v in request["commands"]],
            "Typed Gaussian path input differs")
    for field in ("pixel_type", "sigma_x", "sigma_y", "accuracy"):
        require(actual["options"][field] == request[field], "Typed Gaussian option differs: " + field)
    original = rank_samples(actual["input"], request)
    require(original == rank_input(dict(request, input_pattern="finite")),
            "Typed Gaussian original words differ")
    require(actual["roi_before_attachment"] == actual["roi_after_attachment"],
            "Typed Gaussian attachment changed geometry")
    require(actual["mask_absent_before_explicit_request"] is True,
            "Typed Gaussian attachment allocated mask")
    width, height = request["width"], request["height"]
    full, mask = actual["roi_mask"], actual["processor_mask"]
    if full is not None:
        require(rectangle(full) == rectangle(actual["roi_before_attachment"]["bounds"]),
                "Typed Gaussian full mask origin differs")
    if mask is not None:
        require(rectangle(mask) == rectangle(actual["processor_roi_after_mask"]),
                "Typed Gaussian processor mask origin differs")
        require(project_mask(mask, width, height) == project_mask(full, width, height),
                "Typed Gaussian mask clipping differs")
    selected = project_mask(mask, width, height)
    x, y, w, h = rectangle(actual["processor_roi_after_mask"])
    if mask is None:
        for j in range(y, y+h):
            for i in range(x, x+w):
                selected[j*width+i] = 255
    before = rank_samples(actual["before_mask_reset"], request)
    after = rank_samples(actual["after_mask_reset"], request)
    require(actual["mask_reset_performed"] == (mask is not None), "Typed Gaussian reset contract differs")
    outside = 0
    for index, active in enumerate(selected):
        i, j = index % width, index // width
        reset = mask is not None and x <= i < x+w and y <= j < y+h and not active
        require(after[index] == (original[index] if reset else before[index]),
                "Typed Gaussian snapshot restoration differs at " + str(index))
        outside += not active and after[index] != original[index]
    if request["sigma_x"] == request["sigma_y"] == 0:
        require(after == original, "Zero sigma changed sample words")
    return {"pixels": width*height, "selected": sum(bool(v) for v in selected),
            "raw_reference_unselected_changes": outside}


def validate_typed_gaussian_capture(receipt_path):
    receipt_path = Path(receipt_path)
    receipt = json.loads(receipt_path.read_bytes())
    require(receipt["schema"] == "p13i.reference.capture.001" and receipt["state"] == "observed"
            and receipt["catalog_id"] == "gaussian-typed", "Typed Gaussian capture not observed")
    ids = [row["id"] for row in receipt["cases"]]
    require(len(ids) == len(set(ids)) == 80 and ids == receipt["selected_cases"] == receipt["required_cases"]
            and receipt["complete_catalog_observed"] is True, "Typed Gaussian capture incomplete")
    require(all(name.startswith(f"gaussian-{index:03}-") for index, name in enumerate(ids)),
            "Typed Gaussian catalog order differs")
    result = []
    for row in receipt["cases"]:
        raw = (receipt_path.parent/(row["id"]+".json")).read_bytes()
        require(row["exit_code"] == 0 and len(raw) == row["report"]["bytes"]
                and hashlib.sha256(raw).hexdigest() == row["report"]["sha256"],
                "Typed Gaussian output identity differs")
        result.append({"id": row["id"], **validate_typed_gaussian_observation(json.loads(raw),row["request"])})
    return result


def validate_scientific_observation(actual, request):
    require(actual["schema"] == "imagejxx.reference.scientific-path-filter-target"
            and actual["version"] == 1 and actual["name"] == request["name"],
            "Unknown scientific observation")
    require(actual["imagej_version"] == "1.54r" and actual["threads"] == 1,
            "Scientific reference version/threads differ")
    options = actual["options"]
    for field in ("operation", "input_offset", "region"):
        require(options[field] == request[field], "Scientific request differs: " + field)
    require(options["sigma"] == (1 if request["operation"] == "gaussian" else None)
            and options["radius"] == (2.5 if request["operation"] == "mean" else None),
            "Scientific kernel options differ")
    w, h = request["width"], request["height"]
    offset = request["input_offset"]
    require(offset == 16*request["position"][0]+64*request["position"][1]+256*request["position"][2],
            "Scientific offset/plane mapping differs")
    expected = [f32(((x*37+y*53+(x*y)%17)%257-128)/16+offset)
                for y in range(h) for x in range(w)]
    if offset == 0:
        expected[0:2] = ["0x80000000", "0x00000000"]
    original = words(actual["input_f32_words"], w*h)
    require(original == expected, "Scientific input word differs")
    require(len(actual["operands"]) == len(request["paths"]) == (2 if request["region"] == "union" else 1),
            "Scientific operands differ")
    combined = bytearray(w*h)
    for index, (operand, path) in enumerate(zip(actual["operands"], request["paths"])):
        require(operand["commands_f32_words"] == [f32(v) for v in path], "Scientific operand words differ")
        dx, dy = (0,0) if index == 0 else (3,1)
        require(rectangle(operand["geometry"]["bounds"]) == (2+dx,1+dy,8,8),
                "Scientific operand stored bounds differ")
        mask = project_mask(operand["mask"], w, h)
        literal = bytes(255 if 1 <= y-dy <= 7 and x-dx >= 2 and x-dx+y-dy <= 9 else 0
                        for y in range(h) for x in range(w))
        require(mask == literal, "Scientific operand mask differs from literal cells")
        for at, value in enumerate(mask):
            combined[at] |= value
    require(sum(bool(v) for v in combined) == (50 if request["region"] == "union" else 28),
            "Scientific OR occupancy differs")
    rectangle_expected = (2,1,11,9) if request["region"] == "union" else (2,1,8,8)
    require(rectangle(actual["processor_roi"]) == rectangle_expected,
            "Scientific processor bounds differ")
    require(project_mask(actual["processor_mask"],w,h) == combined,
            "Scientific processor mask is not OR of operands")
    before = words(actual["before_mask_reset_f32_words"],w*h)
    after = words(actual["after_mask_reset_f32_words"],w*h)
    x0,y0,rw,rh = rectangle_expected
    for index, active in enumerate(combined):
        x,y = index%w,index//w
        restored = x0 <= x < x0+rw and y0 <= y < y0+rh and not active
        require(after[index] == (original[index] if restored else before[index]),
                "Scientific snapshot restoration differs")
    return {"pixels":w*h,"selected":sum(bool(v) for v in combined),
            "raw_reference_unselected_changes":sum(not active and after[i] != original[i]
                                                    for i,active in enumerate(combined))}


def validate_scientific_capture(receipt_path):
    receipt_path = Path(receipt_path)
    receipt = json.loads(receipt_path.read_bytes())
    require(receipt["state"] == "observed" and receipt["catalog_id"] == "scientific",
            "Scientific capture not observed")
    ids = [row["id"] for row in receipt["cases"]]
    expected = [name for operation in ("gaussian","mean")
                for name in [*(f"scientific-{operation}-{t}{c}{z}-active"
                               for t in (0,1) for c in (0,2) for z in (1,3)),
                             f"scientific-{operation}-103-union"]]
    require(ids == receipt["required_cases"] == receipt["selected_cases"] == expected
            and receipt["complete_catalog_observed"] is True, "Scientific catalog incomplete")
    rows = []
    for row in receipt["cases"]:
        raw = (receipt_path.parent/(row["id"]+".json")).read_bytes()
        require(row["exit_code"] == 0 and len(raw) == row["report"]["bytes"]
                and hashlib.sha256(raw).hexdigest() == row["report"]["sha256"],
                "Scientific output identity differs")
        rows.append({"id":row["id"],**validate_scientific_observation(json.loads(raw),row["request"])})
    return rows


def validate_top_hat_display_observation(actual, request, rank_row):
    require(actual["schema"] == "imagejxx.reference.path-top-hat-display-target"
            and actual["version"] == 1 and actual["name"] == request["name"], "Unknown Top Hat display observation")
    require(actual["imagej_version"] == "1.54r" and actual["threads"] == 1, "Top Hat display version/threads differ")
    require(actual["phase"] == request["phase"], "Top Hat display phase differs")
    keys = ("radius", "light_background", "dont_subtract", "inverted_lut")
    options = {key: request[key] for key in keys}
    require(actual["options"] == actual["effective_options"] == options, "Top Hat display actual/requested options differ")
    require(request["width"] == 23 and request["height"] == 17 and request["radius"] == 2, "Unknown Top Hat display extent")
    require(rectangle(actual["roi"]["bounds"]) == rectangle(actual["processor_roi"]) == (2,1,8,8),
            "Top Hat display stored/processor bounds differ")
    mask = project_mask(actual["processor_mask"],23,17)
    literal = bytes(255 if 1 <= y <= 7 and x >= 2 and x+y <= 9 else 0 for y in range(17) for x in range(23))
    require(mask == literal and sum(bool(v) for v in mask) == 28, "Top Hat display mask differs from literal triangle")
    original = words(actual["input_f32_words"],391)
    require(original == rank_input({"width":23,"height":17,"pixel_type":"f32","input_pattern":"finite"}),
            "Top Hat display original words differ")
    require(actual["initial_range"] == [-8,8] and actual["initial_threshold"] is None, "Top Hat initial contrast differs")
    require(all(rank_row["request"][key] == request[key] for key in keys)
            and rank_row["request"]["operation"] == "top_hat" and rank_row["request"]["pixel_type"] == "f32"
            and rank_row["request"]["width"] == 23 and rank_row["request"]["commands"] == request["commands"],
            "Top Hat independent pixel fixture binding differs")
    filtered = words(actual["after_mask_reset_f32_words"],391)
    independent = rank_row["observation"]["after_mask_reset"]["sample_words"]
    require(all(filtered[i] == independent[i] for i,value in enumerate(mask) if value),
            "Top Hat display selected words differ from independent rank capture")
    reset = request["phase"] == "apply" or request["dont_subtract"]
    require(actual["mask_reset_performed"] is reset, "Top Hat actual runner mask-restoration phase differs")
    require(rectangle(actual["processor_roi_after_processing"]) == ((2,1,8,8) if request["dont_subtract"] else (0,0,23,17)),
            "Top Hat display processor ROI side effect differs")
    after_mask = actual["processor_mask_after_processing"]
    require((after_mask is not None) == request["dont_subtract"], "Top Hat contrast mask side effect differs")
    if after_mask is not None:
        require(project_mask(after_mask,23,17) == literal, "Top Hat retained processor mask differs")
    if request["phase"] == "preview":
        before = words(actual["before_mask_reset_f32_words"],391)
        require(actual["range_before_mask_reset"] == actual["final_range"], "Top Hat mask reset changed display range")
        for i,active in enumerate(mask):
            x,y = i%23,i//23
            restore = reset and 2 <= x < 10 and 1 <= y < 9 and not active
            require(filtered[i] == (original[i] if restore else before[i]), "Top Hat preview mask restoration differs")
    else:
        require(actual["before_mask_reset_f32_words"] is None and actual["range_before_mask_reset"] is None,
                "Actual plugin runner cannot claim a before-reset observation")
    if reset:
        require(all(filtered[i] == original[i] for i,value in enumerate(mask) if not value),
                "Top Hat restored runner changed unselected words")
    bounds = actual["final_range"]
    require(isinstance(bounds,list) and len(bounds) == 2 and all(isinstance(v,(float,int))
            and not isinstance(v,bool) and -float("inf") < v < float("inf") for v in bounds)
            and bounds[0] <= bounds[1] and actual["final_threshold"] is None, "Malformed Top Hat final range/threshold")
    if request["dont_subtract"]:
        require(bounds == [-8,8], "Don't-subtract changed source display range")
    return {"pixels":391,"selected":28,
            "raw_reference_unselected_changes":sum(not active and filtered[i] != original[i] for i,active in enumerate(mask))}
