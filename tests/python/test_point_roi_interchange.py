"""Explicit semantic point mapping; existing raw/envelope versions stay opaque."""
import numpy as np
import json
import os
from pathlib import Path
import pytest
import imagejxx as ij

KEY = "imagejxx.point_annotations"
EXTENT = [2, 2, 2, 5, 7, 1]


def mapping(extent=EXTENT, plane="xy"):
    value = ij.ImageJRoiPlaneMapping();value.extent = extent;value.plane = plane
    value.coordinate = 0;value.z_position = ij.ImageJZPositionMapping.FIXED_AXIS
    return value


def raw(words=(0, 2050, 258)):
    record = ij.ImageJRoi(ij.ROI("points", ij.Selection("multipoint", [(1.25, 1), (2, 2), (1.25, 1)])))
    record.point_counters = list(words);record.group = 7;record.properties = "original: λ\n"
    return record


def row(selection, plane="xy"):
    return ij.PlanarAnnotation("edited", selection, plane, 0, t=0, c=1,
        attributes={"opaque-row": [None, True, "μ"]})


@pytest.mark.parametrize("counter", [0, 2, 99])
@pytest.mark.parametrize("position", [0, 1, 4, 8])
def test_explicit_promotion_preserves_raw_source_and_projects_native_annotations(counter, position):
    record = raw([counter | position << 8, 0, 258]);wire = ij.encode_imagej_roi(record)
    bare = ij.decode_imagej_roi_mapped(wire, mapping())
    assert KEY not in bare.annotation.selection.attributes
    original = ij.encode_imagej_roi_envelope(bare)
    assert KEY not in ij.decode_imagej_roi_envelope(original).annotation.selection.attributes
    promoted = ij.annotated_imagej_point_mapping(bare)
    assert ij.imagej_point_counters(promoted.annotation.selection, EXTENT) == record.point_counters
    assert ij.encode_imagej_roi(promoted.source_record) == wire
    assert KEY not in bare.annotation.selection.attributes
    projection = ij.imagej_roi_annotation_projection(promoted.annotation, EXTENT, record)
    assert projection.point_counters == record.point_counters and projection.group == 7
    assert projection.properties == record.properties and KEY not in projection.roi.selection.attributes
    envelope = ij.encode_imagej_roi_native_envelope(promoted, projection)
    restored = ij.decode_imagej_roi_envelope(envelope)
    assert restored.annotation.selection.attributes == promoted.annotation.selection.attributes
    assert ij.encode_imagej_roi(restored.source_record) == wire
    with pytest.raises(ValueError, match="Rebound"):
        ij.encode_imagej_roi_envelope(promoted)


def test_native_edits_replace_stale_template_counters_and_keep_unrelated_metadata():
    source = ij.map_imagej_roi(raw(), mapping())
    promoted = ij.annotated_imagej_point_mapping(source)
    selected = ij.with_point_annotations(promoted.annotation.selection.with_attributes({"opaque": "kept"}),
        [ij.PointAnnotation(7, ij.PlanePosition(1, 1, 1)), ij.PointAnnotation(0), ij.PointAnnotation(7)])
    selected = ij.SelectionEditor(selected, (5, 7)).inserted_vertex(1, [4.125, 3])
    edited = ij.rebind_imagej_roi_mapping(promoted, EXTENT, row(selected))
    assert ij.annotated_imagej_point_mapping(edited).annotation.selection.attributes == selected.attributes
    template = source.source_record
    projected = ij.imagej_roi_annotation_projection(edited.annotation, EXTENT, template)
    assert projected.point_counters == [2055, 0, 0, 7]
    assert template.point_counters == [0, 2050, 258]
    assert projected.roi.selection.points == selected.points and projected.roi.selection.attributes == {}
    for compress in [False, True]:
        wire = ij.encode_imagej_roi_native_envelope(edited, projected)
        archive = ij.encode_imagej_roi_zip([ij.ImageJRoiZipEntry("edited.roi", wire)], compress=compress)
        reopened = ij.decode_imagej_roi_envelope(ij.decode_imagej_roi_zip(archive)[0].data)
        assert reopened.annotation.selection == selected
        assert reopened.annotation.attributes == row(selected).attributes
        assert reopened.source_record.point_counters == template.point_counters


def test_promotion_uses_original_extent_before_target_rebinding():
    source_extent = [1, 2, 2, 5, 7, 1]
    source = ij.map_imagej_roi(raw([1026, 0, 0]), mapping(source_extent))
    target = [2, 1, 2, 5, 7, 1]
    rebound = ij.rebind_imagej_roi_mapping(source, target, source.annotation)
    # Original index4 is C1/Z1/T0, not C0/Z1/T1 in the new shape.
    with pytest.raises(ValueError, match="extent"):
        ij.annotated_imagej_point_mapping(rebound)


@pytest.mark.parametrize("word", [100, 255, 9 << 8, 0x80000000, 0xffffffff])
def test_unsupported_semantics_still_roundtrip_raw_and_refuse_promotion(word):
    record = raw([word, 0, 0]);wire = ij.encode_imagej_roi(record)
    source = ij.decode_imagej_roi_mapped(wire, mapping())
    with pytest.raises(ValueError):ij.annotated_imagej_point_mapping(source)
    assert ij.encode_imagej_roi(source.source_record) == wire
    assert KEY not in source.annotation.selection.attributes


@pytest.mark.parametrize("plane", ["xz", "yz"])
def test_oblique_axis_metadata_is_not_reinterpreted(plane):
    source = ij.map_imagej_roi(raw(), mapping(plane=plane))
    with pytest.raises(ValueError, match="XY"):
        ij.annotated_imagej_point_mapping(source)
    selected = ij.with_point_annotations(ij.Selection("point", [(1, 1)]), [ij.PointAnnotation(1)])
    with pytest.raises(ValueError, match="XY"):
        ij.imagej_roi_annotation_projection(row(selected, plane), EXTENT)


def test_changed_unannotated_vertices_refuse_original_index_association():
    source = ij.map_imagej_roi(raw(), mapping())
    moved = ij.SelectionEditor(source.annotation.selection, (5, 7)).moved([1, 0])
    rebound = ij.rebind_imagej_roi_mapping(source, EXTENT, row(moved))
    with pytest.raises(ValueError, match="aligned"):
        ij.annotated_imagej_point_mapping(rebound)


def test_no_counter_sidecar_stays_untagged_and_other_geometry_uses_shared_projection():
    source = ij.map_imagej_roi(raw([]), mapping())
    assert KEY not in ij.annotated_imagej_point_mapping(source).annotation.selection.attributes
    selection = ij.Selection("line", [(1, 1), (3, 2)], attributes={"opaque": "native only"})
    projected = ij.imagej_roi_annotation_projection(row(selection), EXTENT)
    assert projected.roi.selection.points == selection.points and not projected.roi.selection.attributes


def test_projection_rejects_bad_annotations_and_vertex_cap_before_output():
    source = ij.annotated_imagej_point_mapping(ij.map_imagej_roi(raw(), mapping()))
    with pytest.raises(ValueError, match="vertex"):
        ij.imagej_roi_annotation_projection(source.annotation, EXTENT, limits=ij.ImageJRoiLimits(max_vertices=2))
    invalid = ij.Selection("point", [(1, 1)], attributes={KEY: {"version": 2, "points": []}})
    with pytest.raises(ValueError):ij.imagej_roi_annotation_projection(row(invalid), EXTENT)
    outside = ij.with_point_annotations(ij.Selection("point", [(1, 1)]), [ij.PointAnnotation(1, ij.PlanePosition(2, 0, 0))])
    with pytest.raises(ValueError, match="extent"):
        ij.imagej_roi_annotation_projection(row(outside), EXTENT)


def test_mixed_import_preserves_original_and_explicit_annotation_in_existing_v4_archive():
    record = raw();wire = ij.encode_imagej_roi(record);mapped = ij.annotated_imagej_point_mapping(ij.map_imagej_roi(record, mapping()))
    item = ij.ImageJRoiImportItem("points.roi", wire, mapping(),
        native_replacement=ij.ImageJRoiNativeReplacement(EXTENT, mapped.annotation))
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.zeros((2, 2, 2, 5, 7), np.uint8), axes="TCZYX").observe_reads(reads))
    before = owner.snapshot()
    command = ij.resolve_imagej_annotation_import(before, [item])
    assert command.provenance()["version"] == 4 and reads.counts().plane_reads == 0
    assert owner.snapshot().shares_data(before)
    owner.commit(ij.prepare_imagej_annotation_import(command))
    assert reads.counts().plane_reads == 0 and owner.snapshot().dataset.shares_pixels(before.dataset)
    imported = ij.planar_annotations(owner.snapshot())[0]
    assert imported.selection.attributes == mapped.annotation.selection.attributes
    saved = ij.inspect_imagej_annotation_sources(owner.snapshot())[0]
    assert saved.data == wire and saved.native_replacement.annotation.selection == imported.selection
    restored = ij.decode_session(ij.encode_session(ij.SessionCapture([owner]))).documents[0]
    assert ij.planar_annotations(restored.snapshot())[0].selection == imported.selection
    owner.undo();assert owner.snapshot().shares_data(before)


@pytest.mark.parametrize("context", ["imgui_cpp", "imgui_python"])
def test_actual_point_promotion_vertex_edit_retained_metadata_export_and_reopen(context, tmp_path):
    from imagejxx import _desktop
    if context == "imgui_cpp":
        path = Path(os.environ["IMAGEJXX_POINT_ROI_INTERCHANGE_CPP_REPORT"])
        report = json.loads(path.read_bytes())
    else:
        path = tmp_path / "capture/case.json"
        editor = _desktop.Editor(str(tmp_path / "prefs.json"))
        report = json.loads(editor._point_roi_interchange_ui_case(str(path.parent)))
    assert report["state"] == "observed" and not report["acceptance_claim"] and report["context"] == context
    assert report["contract"] == "g066.point-roi-interchange.ui.001" and len(report["frames"]) < 700
    assert all(f["events"] == f["input_frame"]["raw_inputs"] for f in report["frames"])
    assert all(c["control"]["id"] and c["control"]["enabled"] for c in report["controls"])
    assert {"promote_points", "replacement.confirm", "replacement.vertex_u", "replacement.add_vertex", "apply", "export"} <= {c["name"] for c in report["controls"]}
    assert report["unpublished"] and report["original_retained"] and report["pixels_shared"]
    assert report["archive_version"] == 4 and report["import_undo"] == 1
    assert report["imported_words"] == report["restored_words"] == report["outer_words"] == [2050, 0, 258, 0]
    assert report["imported_points"][0] == [3.125, 1] and len(report["imported_points"]) == 4
    assert report["export_preserved_source"] and report["workflow_count"] == 2
    wire = path.with_name("edited.roi").read_bytes()
    restored = ij.decode_imagej_roi_envelope(wire)
    assert restored.annotation.selection.points == report["imported_points"]
    assert ij.imagej_point_counters(restored.annotation.selection, EXTENT) == report["outer_words"]
    assert ij.encode_imagej_roi(restored.source_record) == path.with_name("source.roi").read_bytes()
