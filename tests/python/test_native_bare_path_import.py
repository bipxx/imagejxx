"""Explicit native bare imports retain curves, source bytes and closed replay."""
import base64
import copy
import pytest
import imagejxx as ij
from test_imagej_roi_import import owner, mapping, bare, EXTENT, SOURCES
from test_path_wire import record, COMMANDS, no_raster
from test_imagej_annotation_import import display


def item(plane='xy', native=True, name='curve.roi'):
    source = record()
    source.roi = ij.ROI(source.roi.name, source.roi.selection, t=0, c=1)
    return ij.ImageJRoiImportItem(name, ij.encode_imagej_roi(source), mapping(plane), native_path=native)


def limits(mixed):
    # Mixed import retains its display-limit contract (positive budgets).
    # One pixel/work unit cannot rasterize this source on any chosen canvas.
    wire = ij.ImageJRoiLimits(max_mask_pixels=1, max_path_work=1) if mixed else no_raster()
    batch = ij.ImageJRoiImportLimits(roi=wire)
    return ij.ImageJAnnotationImportLimits(batch=batch) if mixed else batch


def resolve(doc, items, mixed, mode=ij.ImageJRoiImportMode.APPEND, no_mask=True):
    fn = ij.resolve_imagej_annotation_import if mixed else ij.resolve_imagej_roi_import
    return fn(doc.snapshot(), items, mode, limits(mixed)) if no_mask else fn(doc.snapshot(), items, mode)


def prepare(value, mixed):
    return (ij.prepare_imagej_annotation_import if mixed else ij.prepare_imagej_roi_import)(value)


@pytest.mark.parametrize('mixed', [False, True])
@pytest.mark.parametrize('plane', ['xy', 'xz', 'yz'])
def test_native_bare_roundtrip_history_source_replay_and_no_samples(mixed, plane):
    doc, reads = owner(); before = doc.snapshot(); original = item(plane)
    result = resolve(doc, [original], mixed)
    assert result.annotations[0].selection.path.commands == COMMANDS
    source = result.records[0].source_record
    assert source.roi.selection.is_path and source.composite_path == COMMANDS
    assert source.group == 7 and source.properties == 'owned: curves\n'
    captured = result.record()
    assert captured['version'] == (2 if mixed else 3)
    decode = ij.decode_imagej_annotation_import_request if mixed else ij.decode_imagej_roi_import_request
    assert decode(captured).items[0].native_path
    replay = ij.imagej_annotation_import_from_record if mixed else ij.imagej_roi_import_from_record
    replayed = replay(before, captured)
    assert replayed.annotations[0].selection.path.commands == COMMANDS
    doc.commit(prepare(result, mixed))
    saved = doc.snapshot().metadata['extensions'][SOURCES]
    assert saved['version'] == (6 if mixed else 5)
    entry = saved['entries'][0]['source'] if mixed else saved['entries'][0]
    assert entry['native_path'] is True and base64.b64decode(entry['data']) == original.data
    assert entry['mapping']['plane'] == plane and entry['native_replacement'] is None
    assert reads.counts().plane_reads == reads.counts().region_reads == 0
    restored = ij.decode_session(ij.encode_session(ij.SessionCapture([doc]))).documents[0]
    assert ij.planar_annotations(restored.snapshot())[0].selection.path.commands == COMMANDS
    assert ij.inspect_imagej_annotation_sources(restored.snapshot(), limits(True))[0].native_path
    assert doc.undo_steps == 1 and doc.snapshot().dataset.shares_pixels(before.dataset)
    doc.undo(); assert doc.snapshot().shares_data(before)
    doc.redo(); assert ij.planar_annotations(doc.snapshot())[0].selection.path.commands == COMMANDS


@pytest.mark.parametrize('mixed', [False, True])
def test_legacy_default_stays_mask_and_native_decoding_is_explicit(mixed):
    doc, _ = owner()
    result = resolve(doc, [item(native=False)], mixed, no_mask=False)
    assert result.annotations[0].selection.kind == 'mask'
    assert result.record()['version'] == 1
    result = resolve(doc, [item()], mixed)
    doc.commit(prepare(result, mixed))
    # Appending or replacing a legacy item after a native archive keeps exact
    # before/after version claims and independently recoverable source choices.
    result = resolve(doc, [bare('rect.roi')], mixed, no_mask=False)
    assert result.record()['version'] == (2 if mixed else 3)
    doc.commit(prepare(result, mixed))
    assert len(ij.planar_annotations(doc.snapshot())) == 2
    assert [v.native_path for v in ij.inspect_imagej_annotation_sources(doc.snapshot())] == [True, False]
    replaced = resolve(doc, [bare('replacement.roi')], mixed, ij.ImageJRoiImportMode.REPLACE, no_mask=False)
    assert replaced.record()['version'] == (2 if mixed else 3)
    (ij.validate_imagej_annotation_import_record if mixed else ij.validate_imagej_roi_import_record)(replaced.record())


@pytest.mark.parametrize('mixed', [False, True])
@pytest.mark.parametrize('mutation', ['archive-old', 'command-old', 'false', 'integer', 'null', 'no-mapping', 'unknown'])
def test_native_choice_forgery_refuses_atomically(mixed, mutation):
    doc, _ = owner(); before = doc.snapshot()
    record_ = copy.deepcopy(resolve(doc, [item()], mixed).record())
    archive = record_['after_sources']
    entry = archive['entries'][0]['source'] if mixed else archive['entries'][0]
    if mutation == 'archive-old': archive['version'] = 3 if mixed else 2
    elif mutation == 'command-old': record_['version'] = 1
    elif mutation == 'false': entry['native_path'] = False
    elif mutation == 'integer': entry['native_path'] = 1
    elif mutation == 'null': entry['native_path'] = None
    elif mutation == 'no-mapping': entry['mapping'] = None
    else: entry['native_paths'] = entry.pop('native_path')
    validate = ij.validate_imagej_annotation_import_record if mixed else ij.validate_imagej_roi_import_record
    with pytest.raises(ValueError): validate(record_)
    assert doc.snapshot().shares_data(before) and doc.undo_steps == 0


@pytest.mark.parametrize('mixed', [False, True])
@pytest.mark.parametrize('bad', ['primitive', 'missing-mapping', 'bad-tail'])
def test_bad_native_sources_refuse_complete_batch(mixed, bad):
    doc, _ = owner(); before = doc.snapshot(); original = item()
    data, m = original.data, original.mapping
    if bad == 'primitive': data = bare().data
    elif bad == 'missing-mapping': m = None
    else: data = data[:-1]
    invalid = ij.ImageJRoiImportItem('bad.roi', data, m, native_path=True)
    with pytest.raises(ValueError): resolve(doc, [original, invalid], mixed)
    assert doc.snapshot().shares_data(before) and doc.undo_steps == 0


def test_native_archive_migrates_to_mixed_with_independent_edited_row():
    doc, _ = owner(); original = item()
    doc.commit(prepare(resolve(doc, [original], False), False))
    row = ij.PlanarAnnotation('edited', ij.Selection.from_path(ij.ImageJCompositePath(
        [0, 1.1234567890123, 1, 2, 3, 5, 7, 1, 4])), 'yz', 1)
    edited = ij.ImageJRoiImportItem('edited.roi', original.data, original.mapping,
        ij.ImageJRoiNativeReplacement(EXTENT, row), native_path=True)
    result = resolve(doc, [edited, display()], True, no_mask=False)
    doc.commit(prepare(result, True))
    saved = ij.inspect_imagej_annotation_sources(doc.snapshot())
    assert [v.native_path for v in saved[:2]] == [True, True]
    assert saved[1].data == original.data
    restored = saved[1].native_replacement.annotation
    assert restored.selection == row.selection
    assert (restored.name, restored.plane, restored.coordinate, restored.t, restored.c, restored.attributes) == (
        row.name, row.plane, row.coordinate, row.t, row.c, row.attributes)
    assert ij.planar_annotations(doc.snapshot())[1].selection == row.selection


def test_native_bare_source_pairing_and_export_keep_native_original():
    doc, reads = owner(); original = item()
    doc.commit(prepare(resolve(doc, [original], False), False))
    pair = ij.resolve_imagej_roi_pairing(ij.inspect_imagej_roi_sources(doc.snapshot(), limits(False)), 0, 0)
    assert pair.source_item.native_path and pair.source_item.data == original.data
    assert pair.mapped.source_record.roi.selection.path.commands == COMMANDS
    ij.validate_imagej_roi_pairing_record(pair.record())
    row = pair.mapped.annotation
    projection = ij.ImageJRoi(ij.ROI(row.name, row.selection, t=row.t, c=row.c, z=row.coordinate))
    wire = ij.encode_imagej_roi_pairing_envelope(doc.snapshot(), pair, projection, no_raster())
    restored = ij.decode_imagej_roi_native_envelope(wire, no_raster())
    assert restored.source_record.roi.selection.is_path
    assert restored.annotation.selection.path.commands == COMMANDS
    assert reads.counts().plane_reads == reads.counts().region_reads == 0
