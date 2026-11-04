"""Plane-local creation defaults; all work remains in the native core."""
import json
import math
import os
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest

KEY = "imagejxx.point_annotations"
ROWS = "imagejxx.planar_annotations"


def fixture():
    reads = ij.DatasetReadObserver()
    doc = ij.Document(ij.Dataset(np.zeros((2, 2, 8, 9, 10), np.uint8), axes="TCZYX").observe_reads(reads))
    view = doc.snapshot().display;view.t = 1;view.c = 1;view.crosshair = [4, 3, 6];doc.set_display(view)
    return doc, reads


def annotation(plane="xy", kind="multipoint", t=None, c=None, points=None, attributes=None):
    values = points if points is not None else ([(1.25, 1.75)] if kind == "point" else [(1.25, 1.75), (3.25, 4.75), (1.25, 1.75)])
    return ij.PlanarAnnotation("new", ij.Selection(kind, values, width=8, attributes=attributes or {"opaque": {"kept": [None, True, "μ"]}}),
        plane, 2, t, c, attributes={"row-opaque": [7, None]})


@pytest.mark.parametrize("plane", ["xy", "xz", "yz"])
@pytest.mark.parametrize("kind", ["point", "multipoint"])
@pytest.mark.parametrize("t,c", [(None, None), (0, 0), (1, None), (None, 1)])
@pytest.mark.parametrize("capture", [False, True])
@pytest.mark.parametrize("counter", [0, 99])
def test_exact_plane_counter_position_and_wildcards(plane, kind, t, c, capture, counter):
    doc, reads = fixture();source = doc.snapshot();row = annotation(plane, kind, t, c)
    value = ij.planar_point_creation_selection(source, row, counter, capture)
    assert value.points == row.selection.points and value.width == 8
    assert value.attributes["opaque"] == row.selection.attributes["opaque"]
    entries = ij.point_annotations(value);effective = counter if kind == "multipoint" else 0
    if not capture and effective == 0:
        assert not entries and value == row.selection
    else:
        assert [e.counter for e in entries] == [effective]*len(value.points)
        if capture:
            expected_z = [2]*len(value.points) if plane == "xy" else [math.floor(p[1]) for p in value.points]
            assert [(e.position.t, e.position.c, e.position.z) for e in entries] == [(1 if t is None else t, 1 if c is None else c, z) for z in expected_z]
        else:
            assert all(e.position is None for e in entries)
    assert KEY not in row.selection.attributes and row.attributes == {"row-opaque": [7, None]}
    assert doc.snapshot().shares_data(source) and doc.undo_steps == 0
    assert reads.counts().plane_reads == reads.counts().region_reads == 0


@pytest.mark.parametrize("plane", ["xy", "xz", "yz"])
def test_frozen_store_record_replay_session_and_independent_parent(plane):
    doc, reads = fixture();source = doc.snapshot();row = annotation(plane, t=0, c=None)
    value = ij.planar_point_creation_selection(source, row, 7, True)
    complete = ij.PlanarAnnotation(row.name, value, plane, row.coordinate, row.t, row.c, attributes=row.attributes)
    command = ij.resolve_planar_annotation_command(source, ij.PlanarAnnotationRequest("store", annotation=complete))
    record = json.loads(json.dumps(command.record()))
    ij.validate_planar_annotation_command_record(record)
    view = doc.snapshot().display;view.t = 0;view.c = 0;doc.set_display(view)
    replay = ij.planar_annotation_command_from_record(doc.snapshot(), record)
    doc.commit(ij.prepare_planar_annotation(replay))
    saved = ij.planar_annotations(doc.snapshot())[0]
    assert saved.selection == value and saved.t == 0 and saved.c is None and saved.coordinate == 2
    assert saved.attributes == row.attributes and doc.undo_steps == 1
    assert reads.counts().plane_reads == reads.counts().region_reads == 0
    doc.undo();assert ij.planar_annotations(doc.snapshot()) == []
    doc.redo();assert ij.planar_annotations(doc.snapshot())[0].selection == value
    restored = ij.decode_session(ij.encode_session(ij.SessionCapture([doc], workflow=[record])))
    assert restored.workflow[0] == record
    assert ij.planar_annotations(restored.documents[0].snapshot())[0].selection == value


@pytest.mark.parametrize("plane", ["xz", "yz"])
@pytest.mark.parametrize("vertical", [-0.01, 8.0, 1e100])
def test_z_edge_binding_refuses_with_explicit_unbound_recovery(plane, vertical):
    doc, reads = fixture();source = doc.snapshot();row = annotation(plane, points=[(1.25, vertical)])
    with pytest.raises(ValueError, match="Z cell"):
        ij.planar_point_creation_selection(source, row, 7, True)
    value = ij.planar_point_creation_selection(source, row, 7, False)
    assert ij.point_annotations(value)[0].position is None and doc.snapshot().shares_data(source)
    assert reads.counts().plane_reads == 0


def test_existing_tags_unknown_metadata_invalid_kinds_and_early_cap():
    doc, reads = fixture();source = doc.snapshot()
    for version in [1, 99]:
        row = annotation(attributes={KEY: {"version": version, "points": []}})
        with pytest.raises(ValueError, match="unannotated"):
            ij.planar_point_creation_selection(source, row)
    with pytest.raises(ValueError, match="point candidate"):
        ij.planar_point_creation_selection(source, annotation(kind="line", points=[(1, 1), (2, 2)]))
    with pytest.raises((ValueError, OverflowError), match="working"):
        ij.planar_point_creation_selection(source, annotation(points=[(1, 1)]*1024), max_working_bytes=65536)
    row = annotation(attributes={"opaque": "x"*2_000_000})
    with pytest.raises((ValueError, OverflowError), match="working"):
        ij.planar_point_creation_selection(source, row, max_working_bytes=65536)
    assert doc.snapshot().shares_data(source) and reads.counts().plane_reads == 0


@pytest.mark.parametrize("field,value", [("counter", True), ("counter", 1.0), ("counter", -1), ("counter", 100), ("capture_position", 1), ("max_working_bytes", True), ("max_working_bytes", -1)])
def test_strict_public_scalars(field, value):
    doc, _ = fixture()
    with pytest.raises((TypeError, ValueError, OverflowError)):
        ij.planar_point_creation_selection(doc.snapshot(), annotation(), **{field: value})


@pytest.mark.parametrize("plane", ["xy", "xz", "yz"])
def test_singleton_points_stay_unbound(plane):
    doc = ij.Document(ij.Dataset(np.zeros((1, 1, 1), np.uint8), axes="ZYX"))
    row = ij.PlanarAnnotation("one", ij.Selection("multipoint", [(0.25, 0.25)]), plane, 0)
    assert ij.point_annotations(ij.planar_point_creation_selection(doc.snapshot(), row, 7))[0].position is None


@pytest.mark.parametrize("plane", ["XY", "XZ", "YZ"])
@pytest.mark.parametrize("context", ["imgui_cpp", "imgui_python"])
def test_actual_plane_point_defaults(plane, context, tmp_path):
    case = plane+".point_defaults"
    if context == "imgui_cpp":
        report = json.loads((Path(os.environ["IMAGEJXX_PLANAR_MATRIX_CPP_ROOT"])/case/"case.json").read_bytes())
    else:
        from imagejxx import _desktop
        editor = _desktop.Editor(str(tmp_path/"preferences.json"))
        report = json.loads(editor._planar_annotation_matrix_ui_case(case, str(tmp_path/"capture")))
    assert report["state"] == "observed" and report["context"] == context and report["case"] == case
    assert not report["acceptance_claim"] and not report["error"]
    assert 0 < len(report["frames"]) < 160
    for frame in report["frames"]:assert frame["events"] == frame["rendered"]["input_frame"]["raw_inputs"]
    states = report["states"];before = states["before"]
    for state in states.values():assert state["pixels"] == before["pixels"] and state["selection"] is None
    def rows(name):return states[name]["metadata"][ROWS]["rows"]
    bound = rows("bound")[0];view = ("XY", "XZ", "YZ").index(plane)
    assert (bound["plane"], bound["coordinate"], bound["t"], bound["c"]) == (plane, (2,3,4)[view], 1, 1)
    assert bound["selection"]["points"] == [[1,1], [5,3]]
    z = [2,2] if plane == "XY" else [1,3]
    assert bound["selection"]["attributes"][KEY]["points"] == [{"counter":7, "position":[1,1,v]} for v in z]
    assert rows("unbound")[1]["selection"]["attributes"][KEY]["points"] == [{"counter":7, "position":None}]*2
    single = rows("single")[2]["selection"]
    assert single["kind"] == "point" and single["attributes"][KEY]["points"] == [{"counter":0, "position":[1,1,2 if plane == "XY" else 1]}]
    assert report["point_panels"]["defaults_cancelled"]["last_cancel"] == "point_defaults_changed"
    assert rows("defaults_cancelled") == rows("single")
    recovered = rows("recovered");assert len(recovered) == 4
    assert [e["counter"] for e in recovered[3]["selection"]["attributes"][KEY]["points"]] == [2,2]
    for name in ["cap_cancelled", "source_cancelled", "navigation_cancelled", "escape_cancelled"]:
        assert not report["point_panels"][name]["active"] and rows(name) == recovered
        assert not report["point_panels"][name]["error"]
    assert report["point_panels"]["cap_cancelled"]["last_cancel"] == "point_limit_changed"
    assert states["recovered"]["undo"] == len(states["recovered"]["workflow"]) == 4
    assert states["after"]["undo"] == 5 and len(states["after"]["workflow"]) == 4
    assert states["closed"] == states["after"]
    for record in states["after"]["workflow"]:ij.validate_planar_annotation_command_record(record)
