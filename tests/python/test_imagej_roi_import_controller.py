"""Shared desktop controller publication/recording; no file dialog/UI claim."""
import json
import numpy as np
import pytest
import imagejxx as ij
from imagejxx import _desktop
from test_imagej_roi_import import owner,bare,envelope,archive

def editor(tmp_path,doc):
    value=_desktop.Editor(str(tmp_path/'preferences.json'))
    value.add_native_document(doc);value.recording=True
    return value

def test_shared_controller_replay_records_complete_import_and_undo(tmp_path):
    doc,reads=owner();view=editor(tmp_path,doc);before=doc.snapshot()
    command=ij.resolve_imagej_roi_import(before,[bare(),envelope()]);record=command.record()
    workflow=json.loads(view.workflow);initial_reads=reads.counts()
    view.action('replay_imagej_roi_import',json.dumps({'record':record}))
    assert len(ij.planar_annotations(doc.snapshot()))==2 and doc.undo_steps==1
    assert archive(doc)==record['after_sources']
    assert json.loads(view.workflow)==workflow+[record]
    assert doc.snapshot().dataset.shares_pixels(before.dataset)
    assert reads.counts().plane_reads==initial_reads.plane_reads and reads.counts().region_reads==initial_reads.region_reads
    with pytest.raises((ij.RevisionConflict,ValueError,RuntimeError)):
        view.action('replay_imagej_roi_import',json.dumps({'record':record}))
    assert doc.undo_steps==1 and json.loads(view.workflow)==workflow+[record]
    view.action('undo','{}');assert doc.snapshot().shares_data(before) and archive(doc) is None
    view.action('redo','{}');assert archive(doc)==record['after_sources']

def test_noop_replace_preserves_history_and_workflow(tmp_path):
    doc,_=owner();view=editor(tmp_path,doc);items=[bare()]
    first=ij.resolve_imagej_roi_import(doc.snapshot(),items,ij.ImageJRoiImportMode.REPLACE)
    view.action('replay_imagej_roi_import',json.dumps({'record':first.record()}))
    before=doc.snapshot();history=doc.undo_steps;workflow=json.loads(view.workflow)
    repeated=ij.resolve_imagej_roi_import(before,items,ij.ImageJRoiImportMode.REPLACE)
    view.action('replay_imagej_roi_import',json.dumps({'record':repeated.record()}))
    assert doc.snapshot().shares_data(before) and doc.undo_steps==history
    assert json.loads(view.workflow)==workflow

def test_visual_budget_refusal_is_atomic_for_rows_archive_and_workflow(tmp_path):
    doc=ij.Document(ij.Dataset(np.zeros((103,103),dtype=np.uint8),axes='YX'))
    view=editor(tmp_path,doc);view.update_settings(json.dumps({'processing_memory_mb':1}),False)
    context=ij.ImageJRoiPlaneMapping();context.extent=[1,1,1,103,103,1];context.coordinate=0
    wire=ij.encode_imagej_roi(ij.ImageJRoi(ij.ROI('area',ij.Selection('rectangle',[(1,1),(3,3)]))))
    items=[ij.ImageJRoiImportItem(name,wire,context) for name in ('one.roi','two.roi')]
    before=doc.snapshot();workflow=json.loads(view.workflow)
    command=ij.resolve_imagej_roi_import(before,items)
    with pytest.raises(ValueError,match='visual estimate'):
        view.action('replay_imagej_roi_import',json.dumps({'record':command.record()}))
    assert doc.snapshot().shares_data(before) and doc.undo_steps==0
    assert json.loads(view.workflow)==workflow and archive(doc) is None

def test_recording_disabled_and_unknown_parameters(tmp_path):
    doc,_=owner();view=editor(tmp_path,doc);before=doc.snapshot()
    record=ij.resolve_imagej_roi_import(before,[bare()]).record();workflow=json.loads(view.workflow)
    with pytest.raises(ValueError,match='Unknown ROI import replay parameter'):
        view.action('replay_imagej_roi_import',json.dumps({'record':record,'guess_mapping':True}))
    assert doc.snapshot().shares_data(before) and json.loads(view.workflow)==workflow
    view.recording=False
    view.action('replay_imagej_roi_import',json.dumps({'record':record}))
    assert len(ij.planar_annotations(doc.snapshot()))==1 and json.loads(view.workflow)==workflow

def test_inactive_scientific_target_is_refused(tmp_path):
    doc,_=owner();view=editor(tmp_path,doc)
    record=ij.resolve_imagej_roi_import(doc.snapshot(),[bare()]).record()
    other,_=owner();view.add_native_document(other)
    before=doc.snapshot();other_before=other.snapshot();workflow=json.loads(view.workflow)
    with pytest.raises((ValueError,RuntimeError)):
        view.action('replay_imagej_roi_import',json.dumps({'record':record,'document_id':doc.id}))
    assert doc.snapshot().shares_data(before) and other.snapshot().shares_data(other_before)
    assert json.loads(view.workflow)==workflow

def test_inactive_ordinary_import_form_target_is_refused(tmp_path):
    doc=ij.Document(ij.Dataset(np.zeros((9,13),dtype=np.uint8),axes='YX'))
    view=editor(tmp_path,doc)
    other=ij.Document(ij.Dataset(np.zeros((9,13),dtype=np.uint8),axes='YX'))
    view.add_native_document(other)
    before=doc.snapshot();other_before=other.snapshot();workflow=json.loads(view.workflow)
    with pytest.raises(ValueError,match='active destination'):
        view.action('show_roi_import',json.dumps({'document_id':doc.id}))
    assert doc.snapshot().shares_data(before) and other.snapshot().shares_data(other_before)
    assert json.loads(view.workflow)==workflow


def test_record_review_rejects_inactive_destination_and_unknown_choices(tmp_path):
    doc,_=owner();view=editor(tmp_path,doc)
    record=ij.resolve_imagej_roi_import(doc.snapshot(),[bare()]).record()
    other=ij.Document(ij.Dataset(np.zeros((9,13),dtype=np.uint8),axes='YX'));view.add_native_document(other)
    before=doc.snapshot();other_before=other.snapshot();workflow=json.loads(view.workflow)
    with pytest.raises((ValueError,RuntimeError)):
        view.action('show_roi_import_record',json.dumps({'record':record,'document_id':doc.id}))
    with pytest.raises(ValueError,match='Unknown ROI import review parameter'):
        view.action('show_roi_import_record',json.dumps({'record':record,'reuse_mapping':True}))
    assert doc.snapshot().shares_data(before) and other.snapshot().shares_data(other_before)
    assert json.loads(view.workflow)==workflow
