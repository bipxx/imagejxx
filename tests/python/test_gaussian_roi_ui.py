"""Independent installed group19 readers; staged, unexecuted source.

Exact expected samples come from pinned capture002 inside the declared area and
independently generated original samples outside. No native/scipy result is an
upstream oracle. Observed renderer bytes are CPU upload input, not GPU readback.
"""

import hashlib
import importlib
import json
import math
import os
from pathlib import Path
import struct
import sysconfig

import pytest


def _stream_sha256(stream):
    """Hash remaining binary stream bytes with bounded memory on Python 3.10+."""
    digest = hashlib.sha256()
    for block in iter(lambda: stream.read(1024 * 1024), b""):
        digest.update(block)
    return digest.hexdigest()


GROUP = "P02a.shared_ui.19.roi-exact-bounds"
CONTEXTS = ("imgui_cpp", "imgui_python")
CONTRACT_SHA = "2652bcf1c0665e997fb1bbe55c7f616071359e6b99293b875b2b8555ff045255"
REFERENCE_SHA = "8885c41ded23f51e56b1c42e28245bf4b9320108dacfa09da631e14fbe9bf762"
NUMERICAL_SHA = "3b3f040e01b93642d2b0d3d84db9574f7a9ee525b3a7f29dac7ad21ea03ea6c3"
REFERENCE_COMMIT = "62db90a269cccc1ac79f658e4602043214ed055b"
REFERENCE_ARCHIVE_SHA = "09e31a88191d10c7f79c607cb6bb62342476bde733c5d932d5825fd553796665"
GAUSSIAN_SOURCE_SHA = "2e5827a3e4b3aca0d2e43b9151be5cf2f3d1f69fb5d3e167bdd10d94e101b782"
SPECS = (
    ("u8-irregular-l", "u8-roi-irregular-l-s2-runner-area-harness", "small_l"),
    ("rgb-irregular-l", "rgb-roi-irregular-l-s2-runner-area-harness", "small_l"),
    ("u16-clipped", "u16-roi-rectangle-clipped-s2-runner-area-harness", "clipped"),
    ("f32-clipped", "f32-roi-rectangle-clipped-s2-runner-area-harness", "clipped"),
    ("f32-large-s8p5", "f32-large-rectangle-offcenter-s8p5-runner-area-harness", "large_rectangle"),
    ("f32-large-s8p6", "f32-large-rectangle-offcenter-s8p6-runner-area-harness", "large_rectangle"),
    ("f32-large-s12", "f32-large-rectangle-offcenter-s12-runner-area-harness", "large_rectangle"),
    ("f32-large-irregular-s12", "f32-large-irregular-offcenter-s12-runner-area-harness", "large_l"),
    ("u8-line", "u8-nonarea-line-s2", "line"),
    ("u8-point", "u8-nonarea-point-s2", "point"),
)
VARIANTS = tuple(f"{GROUP}.{suffix}" for suffix, _, _ in SPECS)
ROWS = tuple((variant, context) for variant in VARIANTS for context in CONTEXTS)
ASSERTIONS = (
    "preview_raw_equals_owned_expected", "preview_apply_identical_target_and_pixels",
    "exact_owned_pixels", "source_exact_outside_selected_extent",
    "original_image_coordinate_extent", "roi_attributes_unchanged", "changed_one_history",
    "exact_undo_redo", "raw_reference_discrepancy_reported_separately",
)
DTYPES = {"U8": ("uint8", "B", 1, "UInt8"), "RGB": ("uint8", "B", 1, "UInt8"),
          "U16": ("uint16", "H", 2, "UInt16LE"),
          "F32": ("float32", "f", 4, "IEEE754 Float32LE")}
USAGE_FIELDS = {"running", "queued", "running_bytes", "retained_bytes", "reserved_bytes"}
PRESERVED = ("document_id", "shape_tcz_yxs", "dtype", "calibration", "selection",
             "previous_selection", "rois", "overlays", "metadata", "spectrum_present",
             "workflow", "results", "recording")
DISPLAY_PRESERVED = ("t", "c", "crosshair", "orthogonal", "properties")
FLAGS = ("acceptance_claim", "full61_verified", "full122_verified", "family_complete", "release_accepted")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def file_digest(path):
    with path.open("rb") as stream:
        return _stream_sha256(stream)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        assert key not in result, f"Duplicate JSON field: {key}"
        result[key] = value
    return result


def reject_constant(value):
    raise AssertionError(f"Nonfinite JSON constant: {value}")


def load_json(value):
    return json.loads(value, object_pairs_hook=unique_object, parse_constant=reject_constant)


def required():
    value = os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS", "0")
    assert value in {"0", "1"}, "IMAGEJXX_REQUIRE_RENDER_TESTS must be 0 or 1"
    return value == "1"


def pinned_input(key, expected, *, hash_key=None):
    name = os.environ.get(key)
    if not name:
        if required():
            pytest.fail(f"Required installed ROI input missing: {key}")
        pytest.skip(f"Installed ROI input is optional outside its required gate: {key}")
    if hash_key and hash_key in os.environ:
        assert os.environ[hash_key] == expected, f"Wrong adopted identity in {hash_key}"
    path = Path(name).resolve(strict=True)
    assert path.is_file() and file_digest(path) == expected, f"Pinned ROI input changed: {path}"
    return path


def integer(value, *, positive=False):
    assert type(value) is int and value >= (1 if positive else 0)
    return value


def member_path(root, name):
    root = root.resolve(strict=True)
    assert type(name) is str and name
    path = (root / name).resolve(strict=True)
    assert path.is_relative_to(root) and path.is_file(), "ROI member escapes its fresh case directory"
    return path


def raw(member, root, *, dtype, shape):
    path = Path(member["path"])
    if not path.is_absolute():
        path = root / path
    path = path.resolve(strict=True)
    assert path == member_path(root, member["member_id"])
    assert path.is_relative_to(root.resolve(strict=True))
    item = next(value for value in DTYPES.values() if value[0] == dtype)
    assert member["dtype"] == dtype and member["shape"] == shape and member["encoding"] == item[3]
    samples = math.prod(shape)
    assert member["sample_count"] == samples and member["byte_count"] == samples * item[2]
    data = path.read_bytes()
    assert len(data) == member["byte_count"] and digest(data) == member["sha256"]
    if dtype == "float32":
        assert all(math.isfinite(value[0]) for value in struct.iter_unpack("<f", data))
    return data


def independent_input(oracle):
    height, width = oracle["shape"][:2]
    values = []
    for y in range(height):
        for x in range(width):
            base = (37*x + 61*y + 13*x*y + 19*(x % 3) + 23*(y % 5)) % 256
            if oracle["dtype"] == "U8":
                values.append((base + (47 if x == 0 else 0) + (83 if y == height-1 else 0)) % 256)
            elif oracle["dtype"] == "U16":
                values.append((257*base + 97*x + 211*y + 331*(x % 7)) % 65536)
            elif oracle["dtype"] == "F32":
                values.append((base-127)/8 + (x-2*y)/16 + ((x*y) % 17)/256)
            else:
                assert oracle["dtype"] == "RGB"
                values.extend(((base+3*x) % 256, (5*base+11*y+17) % 256, (255-base+7*x+19*y) % 256))
    assert len(values) == math.prod(oracle["shape"])
    return b"".join(struct.pack("<" + DTYPES[oracle["dtype"]][1], value) for value in values)


def geometry(kind, width, height):
    if kind == "small_l":
        selection = ("polygon", [[5, 4], [16, 4], [16, 7], [10, 7], [10, 12], [5, 12]])
        extent = [5, 4, 11, 8]
        predicate = lambda x, y: 5 <= x < 16 and 4 <= y < 12 and (y < 7 or x < 10)
    elif kind == "clipped":
        selection = ("rectangle", [[-3, 2], [9, 13]])
        extent = [0, 2, 9, 11]
        predicate = lambda x, y: 0 <= x < 9 and 2 <= y < 13
    elif kind == "large_rectangle":
        selection = ("rectangle", [[97, 43], [118, 62]])
        extent = [97, 43, 21, 19]
        predicate = lambda x, y: 97 <= x < 118 and 43 <= y < 62
    elif kind == "large_l":
        selection = ("polygon", [[97, 43], [118, 43], [118, 50], [106, 50], [106, 62], [97, 62]])
        extent = [97, 43, 21, 19]
        predicate = lambda x, y: 97 <= x < 118 and 43 <= y < 62 and (y < 50 or x < 106)
    else:
        assert kind in {"line", "point"}
        selection = (kind, [[2, 3], [width-3, height-4]] if kind == "line" else [[7, 6]])
        extent = None
        predicate = lambda x, y: True
    selected = bytes(255 if predicate(x, y) else 0 for y in range(height) for x in range(width))
    return selection, extent, selected


def compose_expected(oracle, captured, kind):
    source = independent_input(oracle)
    assert digest(source) == oracle["input_sha256"] == captured["input_sha256"]
    assert source == bytes.fromhex(captured["input_raw_hex"])
    reference = bytes.fromhex(captured["output_raw_hex"])
    assert len(reference) == len(source)
    assert digest(reference) == oracle["output_sha256"] == captured["output_sha256"]
    height, width = oracle["shape"][:2]
    selection, extent, selected = geometry(kind, width, height)
    channels = 3 if oracle["dtype"] == "RGB" else 1
    stride = channels * DTYPES[oracle["dtype"]][2]
    expected = bytearray(source)
    changes = []
    for index, is_selected in enumerate(selected):
        offset = index * stride
        if is_selected:
            expected[offset:offset+stride] = reference[offset:offset+stride]
        elif source[offset:offset+stride] != reference[offset:offset+stride]:
            changes.append((index % width, index // width, source[offset:offset+stride], reference[offset:offset+stride]))
    assert digest(expected) == oracle["owned_expected_output_sha256"]
    assert bytes(expected) != source, "Every group19 case must demonstrate changed pixels"
    assert captured["roi"] == oracle["roi"]
    assert captured["roi"]["is_area"] is (extent is not None)
    bounds = extent if extent is not None else [0, 0, width, height]
    assert captured["roi"]["processor_bounds"] == bounds
    mask = captured["roi"]["mask_pixels"]
    if kind in {"small_l", "large_l"}:
        x, y, ew, eh = bounds
        assert captured["mask_restoration_applied"] is True
        assert captured["roi"]["mask_shape"] == [eh, ew]
        assert mask == [selected[py*width+px] for py in range(y, y+eh) for px in range(x, x+ew)]
    else:
        assert mask == [] and captured["roi"]["mask_shape"] is None
        assert captured["mask_restoration_applied"] is False
    assert len(changes) == captured["outside_changed_pixels"] == oracle["raw_reference_outside_changed_pixels"]
    assert captured["outside_checked_pixels"] == selected.count(0)
    # A whole-plane/line/point reference has no outside samples; the capture
    # represents that inapplicable diagnostic as null, with zero counts.
    assert captured["outside_preserved"] is ((not changes) if selected.count(0) else None)
    assert len(captured["outside_changes"]) == len(changes)
    for actual, (x, y, original, output) in zip(captured["outside_changes"], changes):
        assert actual["coordinate_xy"] == [x, y]
        assert actual["input_raw_hex"] == original.hex() and actual["output_raw_hex"] == output.hex()
        fmt = "<" + DTYPES[oracle["dtype"]][1]
        # Java emits shortest Float32 decimals. Compare their declared wire
        # type exactly, including signed zero, instead of promoted doubles.
        assert b"".join(struct.pack(fmt, value) for value in actual["input_values"]) == original
        assert b"".join(struct.pack(fmt, value) for value in actual["output_values"]) == output
    assert oracle["raw_reference_full_output_equals_owned_expected"] is (reference == expected)
    assert len(changes) == (17 if kind == "clipped" else 0)
    if kind == "clipped":
        assert [(x, y) for x, y, _, _ in changes] == [(9, y) for y in range(height)]
        assert reference != expected
    return {"oracle": oracle, "captured": captured, "source": source, "expected": bytes(expected),
            "selection": selection, "extent": extent, "selected": selected, "stride": stride}


@pytest.fixture(scope="module")
def reference_oracles():
    contract_path = pinned_input("IMAGEJXX_GAUSSIAN_UI_CONTRACT_JSON", CONTRACT_SHA,
                                 hash_key="IMAGEJXX_GAUSSIAN_UI_CONTRACT_SHA256")
    numerical_path = pinned_input("IMAGEJXX_GAUSSIAN_CONTRACT_JSON", NUMERICAL_SHA,
                                  hash_key="IMAGEJXX_GAUSSIAN_CONTRACT_SHA256")
    capture_path = pinned_input("IMAGEJXX_GAUSSIAN_REFERENCE_JSONL", REFERENCE_SHA,
                                hash_key="IMAGEJXX_GAUSSIAN_REFERENCE_SHA256")
    contract = load_json(contract_path.read_bytes())
    numerical = load_json(numerical_path.read_bytes())
    assert numerical["comparison"]["expected_cases"] == 332
    assert numerical["comparison"]["expected_owned_output_policy"] == "reference-inside-write-extent/source-outside"
    groups = [item for item in contract["cases"] if item["id"] == GROUP]
    assert len(groups) == 1 and len(contract["numeric_oracles"]) == 19
    declared = groups[0]["variants"]
    assert [item["id"] for item in declared] == list(VARIANTS)
    for variant, (_, oracle_id, _) in zip(declared, SPECS):
        assert variant["required_contexts"] == list(CONTEXTS)
        assert variant["setup"]["input_oracle"] == variant["output_oracle"] == oracle_id
        assert variant["assertions"] == list(ASSERTIONS)
    ids = {oracle for _, oracle, _ in SPECS}
    records = {}
    count = 0
    with capture_path.open("r", encoding="utf-8") as stream:
        for line in stream:
            record = load_json(line)
            count += 1
            if record["id"] in ids:
                assert record["id"] not in records
                records[record["id"]] = record
    assert count == 332 and set(records) == ids
    result = {}
    for suffix, oracle_id, kind in SPECS:
        oracle = contract["numeric_oracles"][oracle_id]
        record = records[oracle_id]
        assert record["status"] == "unadopted_reference_numeric_observation"
        assert record["raw_reference_output_unmodified"] is True and record["source_preserved"] is True
        assert record["full_plugin_runner_executed"] is False and record["dialog_executed"] is False
        assert record["schema_version"] == 1 and record["dtype"] == oracle["dtype"]
        for key in ["shape", "sigma_xy", "scalar_sigma", "scaled_units", "pixel_spacing_xy", "calibration_unit", "method"]:
            assert record[key] == oracle[key]
        reference = record["reference"]
        assert reference["version"] == "1.54r" and reference["commit"] == REFERENCE_COMMIT
        assert reference["archive_sha256"] == REFERENCE_ARCHIVE_SHA
        assert reference["source_members"]["ij/plugin/filter/GaussianBlur.java"] == GAUSSIAN_SOURCE_SHA
        assert reference["threads"] == 1 and reference["headless"] == "true"
        result[f"{GROUP}.{suffix}"] = compose_expected(oracle, record, kind)
    return result


def usage(value, *, empty=False):
    assert set(value) == USAGE_FIELDS
    for item in value.values():
        integer(item)
    assert value["reserved_bytes"] == value["running_bytes"] + value["retained_bytes"]
    if empty:
        assert not any(value.values())


def state_pixels(state, root, evidence, expected):
    oracle = evidence["oracle"]
    height, width = oracle["shape"][:2]
    channels = 3 if oracle["dtype"] == "RGB" else 1
    dtype = DTYPES[oracle["dtype"]][0]
    integer(state["document_id"], positive=True)
    integer(state["data_identity"])
    assert state["shape_tcz_yxs"] == [1, 1, 1, height, width, channels] and state["dtype"] == dtype
    assert len(state["planes"]) == 1 and state["planes"][0]["position"] == [0, 0, 0]
    plane = state["planes"][0]
    assert raw(plane["raw"], root, dtype=dtype, shape=oracle["shape"]) == expected
    assert all(type(plane[key]) is str and int(plane[key], 16) > 0 for key in ("image_address", "buffer_address"))
    assert type(state["dirty"]) is bool
    integer(state["undo_steps"])
    integer(state["redo_steps"])
    assert set(state["revisions"]) == {"pixels", "metadata", "selection", "display"}
    for revision in state["revisions"].values():
        integer(revision)
    history = state["history"]
    assert history["undo_steps"] == state["undo_steps"] and history["redo_steps"] == state["redo_steps"]
    integer(history["step_limit"])
    integer(history["pixel_budget"])
    for key, value in history["pixels"].items():
        if key == "exact":
            assert type(value) is bool
        else:
            integer(value)
    assert history["pixels"]["exact"] is True
    info = state["document_info"]
    assert info["id"] == state["document_id"] and info["dtype"] == dtype
    assert info["dirty"] is state["dirty"]
    assert info["undo_steps"] == state["undo_steps"] and info["redo_steps"] == state["redo_steps"]


def seeded_state(before, evidence, variant):
    height, width = evidence["oracle"]["shape"][:2]
    assert before["calibration"] == {
        "spacing": [1., 1., 1.], "origin": [0., 0., 0.],
        "direction": [1., 0., 0., 0., 1., 0., 0., 0., 1.],
        "unit": "pixels", "time_unit": "s", "frame_interval": 1., "time_origin": 0.,
    }
    kind, points = evidence["selection"]
    assert before["selection"] == {"kind": kind, "points": points, "width": 1.,
                                   "roi_shape_unknown": "shape-attribute-kept"}
    assert before["rois"] == [{"name": "Gaussian ROI fixture", "selection": before["selection"],
                               "attributes": {"roi_unknown": "roi-attribute-kept"}, "t": None, "c": None, "z": 0}]
    assert before["overlays"] == [{"kind": "point", "points": [[1., 1.]], "color": "#ffcc0000",
                                   "roi_name": "Gaussian ROI fixture", "overlay_unknown": "overlay-attribute-kept"}]
    assert before["metadata"] == {"title": "Owned Gaussian ROI " + variant.removeprefix(GROUP + "."),
                                   "properties": {"roi_fixture": "owned-asymmetric-analytic-v1"},
                                   "extensions": {"roi_unknown": "preserved"}, "record_layout": {}}
    assert before["previous_selection"] is None and before["spectrum_present"] is False
    assert before["workflow"] == before["results"] == [] and before["recording"] is False
    display = before["display"]
    expected_range = [-32., 64.] if evidence["oracle"]["dtype"] == "F32" else [0., 65535.] if evidence["oracle"]["dtype"] == "U16" else [0., 255.]
    assert display["properties"]["display_range"] == expected_range
    assert display["t"] == display["c"] == 0 and display["orthogonal"] is False
    assert display["crosshair"] == [width//2, height//2, 0]
    assert before["dirty"] is False and before["undo_steps"] == before["redo_steps"] == 0


def preserved(state, before):
    for field in PRESERVED:
        assert state[field] == before[field], f"Gaussian ROI changed preserved native {field}"
    for field in DISPLAY_PRESERVED:
        assert state["display"][field] == before["display"][field], f"Gaussian ROI changed display {field}"


def cpu_readback(rendered, root, evidence, expected, *, preview):
    oracle = evidence["oracle"]
    height, width = oracle["shape"][:2]
    channels = 3 if oracle["dtype"] == "RGB" else 1
    dtype, fmt, _, _ = DTYPES[oracle["dtype"]]
    ordinary = rendered["ordinary_pixels"]
    assert ordinary["pixels_source"] == ("preview" if preview else "live")
    assert ordinary["contrast_source"] == "live"
    readback = ordinary["readback"]
    assert readback["available"] is True
    source = readback["source"]
    assert source["shape"] == [height, width, channels] and source["dtype"] == dtype
    assert "pixels" not in source and "no GPU readback" in source["observation_scope"]
    assert raw(source["raw"], root, dtype=dtype, shape=[height, width, channels]) == expected
    rgb = readback["rendered_rgb"]
    assert "pixels" not in rgb and "no GPU readback" in rgb["observation_scope"]
    assert rgb["shape"] == [height, width, 3] and rgb["dtype"] == "uint8"
    actual = raw(rgb["raw"], root, dtype="uint8", shape=[height, width, 3])
    low, high = [-32., 64.] if dtype == "float32" else [0., 65535.] if dtype == "uint16" else [0., 255.]
    scale, offset = 255/(high-low), -low*255/(high-low)
    independent = bytearray()
    for sample, in struct.iter_unpack("<" + fmt, expected):
        value = math.floor(min(255., max(0., scale*sample+offset)) + .5)
        independent.extend([value] * (1 if channels == 3 else 3))
    assert actual == bytes(independent), "Actual CPU cache RGB differs from independent fixed-range mapping"


def actual_control(rendered, selector):
    if selector.startswith("gaussian."):
        name = selector.removeprefix("gaussian.")
        value = dict(rendered["gaussian_panel"]["control_states"][name])
        value["rect"] = rendered["gaussian_panel"]["control_rects"][name]
        value["point"] = rendered["gaussian_panel"]["controls"][name]
        return value
    return rendered["host_controls"][selector]


def physical_frames(row, root, evidence):
    frames = row["frames"]
    assert 0 < len(frames) <= 1200
    assert [frame["ordinal"] for frame in frames] == list(range(1, len(frames)+1))
    sessions = []
    for frame in frames:
        assert load_json(member_path(root, f"frame-{frame['ordinal']}.json").read_bytes()) == frame
        usage(frame["usage"])
        rendered = frame["rendered"]
        session = rendered["session"]
        sessions.append(session)
        assert session["open"] is True
        integer(session["session_id"], positive=True)
        integer(session["context_id"], positive=True)
        assert session["current_context_id"] == session["context_id"]
        observed = rendered["input_frame"]
        assert observed["raw_inputs"] == frame["requested_events"]
        for event in frame["requested_events"]:
            assert event["type"] in {"position", "button", "keys", "text"}, "Observed UI action was substituted by an API setter"
            if event["type"] == "position":
                assert set(event) == {"type", "space", "point"} and event["space"] == "screen"
                assert observed["mouse_position"] == [math.floor(value) for value in event["point"]]
            elif event["type"] == "button":
                assert set(event) == {"type", "name", "down"} and event["name"] == "left"
                assert type(event["down"]) is bool and observed["mouse_down"][0] is event["down"]
            elif event["type"] == "keys":
                assert set(event) == {"type", "keys"}
                for atom in event["keys"]:
                    assert set(atom) == {"name", "down"} and type(atom["down"]) is bool
                    assert (atom["name"] in observed["keys_down"]) is atom["down"]
            else:
                assert set(event) == {"type", "value"} and type(event["value"]) is str
    assert len({item["session_id"] for item in sessions}) == len({item["context_id"] for item in sessions}) == 1
    for previous, current in zip(sessions, sessions[1:]):
        assert current["imgui_frame"] == previous["imgui_frame"]+1 and current["imgui_time"] > previous["imgui_time"]
    selectors = [item["selector"] for item in row["control_observations"]]
    assert selectors == ["menu.Process", "menu.Process.Filters", "menu.command.gaussian_blur_imagej",
                          "gaussian.sigma", "gaussian.preview", "gaussian.ok", "menu.Edit", "menu.Edit.Undo",
                          "menu.Edit", "menu.Edit.Redo"]
    for observation in row["control_observations"]:
        previous = integer(observation["before_frame_ordinal"], positive=True)
        actual = observation["control"]
        assert actual == actual_control(frames[previous-1]["rendered"], observation["selector"])
        integer(actual["id"], positive=True)
        assert actual["enabled"] is True and type(actual["active"]) is type(actual["focused"]) is bool
        rect, point = actual["rect"], observation["position"]
        assert len(rect) == 4 and len(point) == 2
        assert all(type(item) in {int, float} and math.isfinite(item) for item in rect)
        x, y = point
        assert [x, y] == [math.floor(item) for item in actual["point"]]
        assert rect[0] <= x < rect[2] and rect[1] <= y < rect[3]
        triple = frames[previous:previous+3]
        assert [item["label"] for item in triple] == [observation["selector"]+suffix for suffix in (".position", ".down", ".up")]
        assert [item["requested_events"] for item in triple] == [
            [{"type": "position", "space": "screen", "point": point}],
            [{"type": "button", "name": "left", "down": True}],
            [{"type": "button", "name": "left", "down": False}],
        ]
    def labelled(label):
        matches = [frame for frame in frames if frame["label"] == label]
        assert len(matches) == 1, f"Missing/duplicate physical ROI observation {label}"
        return matches[0]
    opened = labelled("Gaussian_initial_focus")["rendered"]["gaussian_panel"]
    assert opened["open"] is True and opened["sigma"] == 2. and opened["preview"] is False
    assert opened["scaled_available"] is False and opened["scaled_units"] is False
    clicked_sigma = next(item for item in row["control_observations"] if item["selector"] == "gaussian.sigma")
    focused = frames[clicked_sigma["before_frame_ordinal"]+2]["rendered"]["input_frame"]
    assert focused["focus"]["active_id"] == focused["input_text"]["id"] == clicked_sigma["control"]["id"]
    for suffix, down in ((".select_all.down", True), (".select_all.up", False)):
        assert labelled("gaussian.sigma"+suffix)["requested_events"] == [{"type": "keys", "keys": [
            {"name": "Ctrl", "down": down}, {"name": "A", "down": down}]}]
    sigma = evidence["oracle"]["scalar_sigma"]
    text = format(sigma, ".15g")
    inserted = labelled("gaussian.sigma.text")
    assert inserted["requested_events"] == [{"type": "text", "value": text}]
    observed = inserted["rendered"]["input_frame"]
    sigma_control = inserted["rendered"]["gaussian_panel"]["control_states"]["sigma"]
    assert observed["focus"]["active_id"] == observed["input_text"]["id"] == sigma_control["id"] == clicked_sigma["control"]["id"]
    assert sigma_control["active"] is True and sigma_control["enabled"] is True
    settled = labelled("gaussian.sigma.text_settled")
    assert settled["requested_events"] == []
    panel = settled["rendered"]["gaussian_panel"]
    assert panel["sigma_text"] == text and panel["sigma"] == sigma and panel["draft_valid"] is True
    assert panel["scaled_available"] is False and panel["scaled_units"] is False
    assert "scaled" not in panel["controls"] and panel["preview"] is False
    assert labelled("gaussian.ok.up")["rendered"]["gaussian_panel"]["stack_prompt"] is False
    assert labelled("history_Undo_settled")["requested_events"] == labelled("history_Redo_settled")["requested_events"] == []
    return frames


def typed_record(value, before, evidence):
    sigma = evidence["oracle"]["scalar_sigma"]
    assert set(value) == {"schema_version", "operation", "options", "calibration", "effective_sigma_xy", "target"}
    assert value["schema_version"] == 1 and value["operation"] == "gaussian_blur_imagej"
    assert value["options"] == {"sigma": sigma, "scaled_units": False}
    assert value["calibration"] == before["calibration"] and value["effective_sigma_xy"] == [sigma, sigma]
    target = value["target"]
    assert target == {"schema_version": 1, "document_id": before["document_id"],
                      "source_revisions": [before["revisions"][key] for key in ("pixels", "metadata", "selection")],
                      "source_shape": before["shape_tcz_yxs"], "scope": "ranges",
                      "ranges": [[0, 1, 1], [0, 1, 1], [0, 1, 1]], "positions": [],
                      "roi_policy": "active_selection", "execution_mode": "planes", "destination": "in_place"}


def assert_row(row, variant, context, root, evidence):
    assert row["schema_version"] == 1 and row["group_id"] == GROUP and row["case_id"] == variant
    assert row["context"] == context and row["status"] == "passed" and row["contract_sha256"] == CONTRACT_SHA
    assert all(row[key] is False for key in FLAGS)
    assert row["comparison_scope"] == "Exact capture002 inside selected area and original source outside; no tolerance or native/scipy oracle"
    assert row["observation_scope"] == "Actual CPU texture-upload input/cache, native committed planes and job lineage; no GPU readback"
    assert load_json(member_path(root, "case.json").read_bytes()) == row
    prior = load_json(member_path(root, "case-unverified.json").read_bytes())
    assert prior["status"] == "unverified"
    for key, value in prior.items():
        if key == "status":
            continue
        if key in {"frames", "checkpoints", "lifecycle", "api_inputs"}:
            assert row[key][:len(value)] == value, f"Pre-assertion ROI evidence changed: {key}"
        else:
            assert row[key] == value, f"Pre-assertion ROI evidence changed: {key}"
    assert "final_cleanup" not in prior
    oracle = evidence["oracle"]
    dtype = DTYPES[oracle["dtype"]][0]
    original, expected = evidence["source"], evidence["expected"]
    assert raw(row["fixture_input_raw"], root, dtype=dtype, shape=oracle["shape"]) == original
    assert load_json(member_path(root, "fixture-input.json").read_bytes()) == row["fixture_input_raw"]
    assert raw(row["original_after_raw"], root, dtype=dtype, shape=oracle["shape"]) == original
    assert row["oracle"] == {"id": oracle["id"], "reference_capture_sha256": REFERENCE_SHA,
                            "input_sha256": digest(original), "owned_expected_output_sha256": digest(expected),
                            "raw_reference_output_sha256": oracle["output_sha256"],
                            "pinned_raw_reference_outside_changed_pixels": oracle["raw_reference_outside_changed_pixels"],
                            "expected_original_coordinate_extent": evidence["extent"],
                            "oracle_reference_executed_by_this_UI_driver": False}
    assert (row["oracle"]["raw_reference_output_sha256"] == row["oracle"]["owned_expected_output_sha256"]) is oracle["raw_reference_full_output_equals_owned_expected"]
    assert row["api_inputs"] == [
        {"purpose": "explicit fixture/source/ROI/type/default preparation only",
         "methods": ["addNativeDocument", "setNativeDisplay", "mark_saved"], "recording": False,
         "fixture_formula": "owned-asymmetric-analytic-v1", "tested_changes_use_API_setters": False, "input_oracle": oracle["id"]},
        {"purpose": "bounded final/failure native-job cleanup only", "methods": ["Window.cancel", "action(wait_jobs,timeout_ms=5000)", "Window.poll"]},
    ]
    frames = physical_frames(row, root, evidence)
    names = ["baseline", "preview", "applied", "undone", "redone", "cleanup"]
    assert [item["name"] for item in row["checkpoints"]] == names
    points = dict(zip(names, row["checkpoints"]))
    before = row["before"]
    assert before == points["baseline"]["native_state"]
    seeded_state(before, evidence, variant)
    last_ordinal = 0
    for name in names:
        point = points[name]
        assert load_json(member_path(root, f"checkpoint-{name}.json").read_bytes()) == point
        ordinal = integer(point["frame_ordinal"], positive=True)
        assert last_ordinal < ordinal <= len(frames)
        last_ordinal = ordinal
        assert point["rendered"] == frames[ordinal-1]["rendered"]
        usage(point["usage"])
        native = point["native_state"]
        target_pixels = original if name in {"baseline", "preview", "undone"} else expected
        state_pixels(native, root, evidence, target_pixels)
        preserved(native, before)
        cache_pixels = expected if name in {"preview", "applied", "redone", "cleanup"} else original
        cpu_readback(point["rendered"], root, evidence, cache_pixels, preview=(name == "preview"))
        for index, is_selected in enumerate(evidence["selected"]):
            if not is_selected:
                offset = index * evidence["stride"]
                assert cache_pixels[offset:offset+evidence["stride"]] == original[offset:offset+evidence["stride"]]
    usage(points["baseline"]["usage"], empty=True)
    preview = points["preview"]
    for key in ("data_identity", "revisions", "dirty", "undo_steps", "redo_steps", "history", "document_info"):
        assert preview["native_state"][key] == before[key], f"Preview changed committed {key}"
    assert [(plane["image_address"], plane["buffer_address"]) for plane in preview["native_state"]["planes"]] == [
        (plane["image_address"], plane["buffer_address"]) for plane in before["planes"]]
    held = preview["usage"]
    assert held["running"] == held["queued"] == held["running_bytes"] == 0
    assert held["retained_bytes"] == held["reserved_bytes"] > 0
    jobs = preview["rendered"]["gaussian_jobs"]
    lane = jobs["preview"]
    assert lane["owns_job"] is True and lane["job"]["state"] == "ready"
    assert lane["job"]["document_id"] == lane["source_id"] == before["document_id"]
    assert lane["job"]["cancel_requested"] is False and lane["published_document_id"] is None
    integer(lane["job"]["id"], positive=True)
    assert lane["job"]["resource_bytes"] == held["retained_bytes"]
    assert lane["request_generation"] == jobs["active_installed_generation"]
    integer(lane["request_generation"], positive=True)
    assert preview["rendered"]["ordinary_pixels"]["preview_generation"] == lane["request_generation"]
    assert lane["command_record"] == row["preview_command_record"]
    assert lane["target"] == row["preview_command_record"]["target"]
    assert preview["rendered"]["gaussian_panel"]["preview"] is True
    typed_record(row["preview_command_record"], before, evidence)
    assert row["preview_command_record"] == row["apply_command_record"]
    region = row["captured_region"]
    assert region["extent"] == evidence["extent"] and region["captured_target"] == row["preview_command_record"]["target"]
    if evidence["extent"] is None:
        assert region["mask"] is None
        assert all(evidence["selected"]), "Nonarea ROI must process the whole plane"
    else:
        assert raw(region["mask"], root, dtype="uint8", shape=oracle["shape"][:2]) == evidence["selected"]
    apply_lane = points["applied"]["rendered"]["gaussian_jobs"]["apply"]
    assert apply_lane["command_record"] == row["apply_command_record"] and apply_lane["exit"] == "published"
    assert apply_lane["published_document_id"] == before["document_id"] and apply_lane["source_id"] == before["document_id"]
    assert apply_lane["target"] == row["apply_command_record"]["target"]
    integer(apply_lane["job"]["id"], positive=True)
    assert apply_lane["job"]["id"] != lane["job"]["id"]
    assert apply_lane["request_generation"] is None
    for name in ("applied", "redone", "cleanup"):
        native = points[name]["native_state"]
        assert native["undo_steps"] == 1 and native["redo_steps"] == 0 and native["dirty"] is True
    undo = points["undone"]["native_state"]
    assert undo["undo_steps"] == 0 and undo["redo_steps"] == 1 and undo["dirty"] is False
    assert points["applied"]["native_state"]["data_identity"] != before["data_identity"]
    assert points["applied"]["native_state"]["revisions"]["pixels"] > before["revisions"]["pixels"]
    state_pixels(row["after"], root, evidence, expected)
    preserved(row["after"], before)
    assert row["after"]["undo_steps"] == 1 and row["after"]["redo_steps"] == 0 and row["after"]["dirty"] is True
    lifecycle = row["lifecycle"]
    assert [item["label"] for item in lifecycle] == ["opened", "before_close", "closed"]
    opened, closing, closed = [item["observed"] for item in lifecycle]
    assert opened["open"] is closing["open"] is True and closed["open"] is False
    assert opened["session_id"] == closing["session_id"] == closed["session_id"] == frames[0]["rendered"]["session"]["session_id"]
    assert opened["context_id"] == closing["context_id"] == frames[0]["rendered"]["session"]["context_id"] > 0
    assert closed["context_id"] == closed["current_context_id"] == 0
    assert closing["imgui_frame"] == closed["imgui_frame"] == frames[-1]["rendered"]["session"]["imgui_frame"]
    final = row["final_cleanup"]
    usage(final["usage"], empty=True)
    usage(points["cleanup"]["usage"], empty=True)
    assert final["lifecycle"] == closed and final["Gaussian_renderer_owns_native_jobs"] is False
    assert final["observer_retained_result_aliases"] == 0
    assert points["cleanup"]["rendered"]["gaussian_jobs"]["installed_preview_storage"] is None
    assert points["cleanup"]["rendered"]["gaussian_jobs"]["active_installed_generation"] is None


@pytest.fixture(scope="module")
def cpp_report():
    name = os.environ.get("IMAGEJXX_ROI_CPP_REPORT")
    if not name:
        if required():
            pytest.fail("Required installed ROI report missing: IMAGEJXX_ROI_CPP_REPORT")
        pytest.skip("Installed C++ ROI report is optional outside the mandatory desktop gate")
    path = Path(name).resolve(strict=True)
    assert path.name == "imgui-cpp.json" and path.is_file()
    report = load_json(path.read_bytes())
    assert report["schema_version"] == 1 and report["group_id"] == GROUP
    assert report["context"] == "imgui_cpp" and report["status"] == "passed"
    assert report["contract_sha256"] == CONTRACT_SHA and all(report[key] is False for key in FLAGS)
    rows = report["cases"]
    assert len(rows) == 10 and [row["case_id"] for row in rows] == list(VARIANTS)
    assert all(row["context"] == "imgui_cpp" for row in rows)
    return {row["case_id"]: row for row in rows}, path.parent


def python_row(variant, directory):
    if required():
        desktop = importlib.import_module("imagejxx._desktop")
    else:
        desktop = pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    module = Path(desktop.__file__).resolve(strict=True)
    assert module.suffix in {".pyd", ".so"}, "ROI Python launcher must use the installed native module"
    if required():
        roots = {Path(sysconfig.get_path(key)).resolve() / "imagejxx" for key in ("platlib", "purelib")}
        assert any(module.is_relative_to(root) for root in roots), "Required ROI gate imported a checkout module"
    assert not directory.exists(), "ROI Python driver requires a fresh owned case directory"
    editor = desktop.Editor(preferences_path=str(directory / "preferences.json"))
    return load_json(editor._gaussian_roi_case(variant=variant, context="imgui_python", directory=str(directory)))


@pytest.mark.parametrize("variant,context", ROWS, ids=[f"{variant}-{context}" for variant, context in ROWS])
def test_installed_gaussian_roi_reference_ui(variant, context, reference_oracles, request, tmp_path):
    if context == "imgui_cpp":
        rows, parent = request.getfixturevalue("cpp_report")
        row = rows[variant]
        root = parent / variant.removeprefix(GROUP + ".")
    else:
        # tmp_path is already unique per exact variant/context. Keep room for
        # native checkpoint names and the atomic writer's temporary suffix.
        root = tmp_path / "run"
        row = python_row(variant, root)
    assert_row(row, variant, context, root, reference_oracles[variant])
