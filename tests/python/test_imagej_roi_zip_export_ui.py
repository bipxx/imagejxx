"""Actual C++/Python ImGui input and independent archive/equation checks."""
import base64
import io
import json
import os
from pathlib import Path
import zipfile
import pytest
import imagejxx as ij
from imagejxx import _desktop


@pytest.fixture(scope='module')
def reports(tmp_path_factory):
    cpp=json.loads(Path(os.environ['IMAGEJXX_ROI_ZIP_CPP_REPORT']).read_text(encoding='utf-8'))
    directory=tmp_path_factory.mktemp('roi-zip-export-ui')/'capture001'
    editor=_desktop.Editor(str(directory.parent/'preferences.json'))
    python=json.loads(editor._roi_zip_export_ui_case(str(directory)))
    assert python==json.loads((directory/'case.json').read_text(encoding='utf-8'))
    assert {cpp['context'],python['context']}=={'imgui_cpp','imgui_python'}
    return cpp,python


def test_real_zip_controls_and_explicit_choices(reports):
    for r in reports:
        assert r['state']=='observed' and r['error']=='' and not r['acceptance_claim']
        assert r['contract']=='p06q.roi-zip-export.ui.001'
        assert all(f['events']==f['rendered']['input_frame']['raw_inputs'] for f in r['frames'])
        assert all(c['control']['enabled'] and c['control']['id'] for c in r['controls'])
        assert r['loaded']['zip_mode'] and not r['loaded']['ready'] and r['loaded']['queue_count']==0
        assert r['loaded']['source_index']==r['loaded']['annotation_index']==r['loaded']['metadata_choice']==-1
        assert 'Choose both' in r['unselected']['error']
        assert r['unsafe_name']['queue_count']==0 and r['unsafe_name']['error']
        assert r['duplicate_name']['queue_count']==1 and 'Duplicate' in r['duplicate_name']['error']
        assert r['removed']['queue_count']==2 and not r['removed']['ready']
        assert r['browser']['extensions']==['.zip']
        for frame,plane,coordinate in zip(r['previews'],['XZ','XY','YZ'],[4,0,3]):
            layers=[layer for pane in frame for layer in pane.get('import_preview',{}).get('layers',[])]
            assert any(layer['plane']==plane and layer['coordinate']==coordinate for layer in layers)


def test_zip_bytes_native_precision_and_workflow(reports):
    all_bytes=[]
    for r in reports:
        states=r['states'];assert states['before']==states['review']
        for key,count in [('exported',1),('overwritten',2),('stored',3)]:
            assert states[key]['metadata']==states['before']['metadata'] and states[key]['undo']==4
            assert len(states[key]['workflow'])==count
        assert states['recording_off']['workflow']==states['stored']['workflow']
        assert r['original_unchanged'] and r['source_data_shared']
        for key,method in [('output_file',8),('stored_file',0),('rebound_file',8)]:
            wire=Path(r[key]).read_bytes()
            with zipfile.ZipFile(io.BytesIO(wire)) as z:
                assert z.testzip() is None and all(v.compress_type==method for v in z.infolist())
                rows=[ij.decode_imagej_roi_native_envelope(z.read(v)).annotation for v in z.namelist()]
                if key!='rebound_file':
                    assert z.namelist()==['XZ-λ😀##.roi','XY.roi','YZ.roi']
                    assert [v.plane for v in rows]==['xz','xy','yz']
                    assert rows[0].selection.points==[[7.250000000001,.500000000001]]
                    assert rows[0].attributes=={'opaque':'kept'}
                else:assert z.namelist()==['rebound.roi']
        records=states['rebound']['workflow'];assert len(records)==4
        for record in records:ij.validate_imagej_roi_zip_export_record(record)
        assert records[0]==r['original'] and records[0]['source']['document_id']!=records[3]['source']['document_id']
        for index,key in [(0,'output_file'),(2,'stored_file'),(3,'rebound_file')]:
            assert base64.b64decode(records[index]['output'],validate=True)==Path(r[key]).read_bytes()
        all_bytes.append([Path(r[key]).read_bytes() for key in ('output_file','stored_file','rebound_file')])
    assert all_bytes[0]==all_bytes[1]


def test_zip_staged_cancel_race_recording_and_source_guards(reports):
    for r in reports:
        assert 'File exists' in r['overwrite_refusal']['error']
        assert not r['ready_canceled']['ready'] and 'canceled' in r['ready_canceled']['error']
        assert '😀' in r['destination_race']['error'] and 'raced-' in r['destination_race']['error']
        assert 'Workflow must be an array' in r['workflow_refusal']['error']
        assert r['temporary_files']==[]
        assert not r['stale']['visible'] and r['stale']['cancel_reason']=='source_changed'
        assert {v['stage'] for v in r['worker_read_checks']}=={'prepare','prepare-zip','stage-file'}
        assert all(v['before_plane']==v['after_plane'] and v['before_region']==v['after_region'] for v in r['worker_read_checks'])


def test_zip_captured_review_rebind_and_original_admission(reports):
    for r in reports:
        loaded=r['captured_loaded']
        assert loaded['workflow_review'] and not loaded['destination_confirmed'] and not loaded['overwrite']
        assert loaded['path']=='' and loaded['queue_count']==0
        assert loaded['source_index']==loaded['annotation_index']==loaded['metadata_choice']==-1
        assert 'Confirm the reviewed pairing' in r['destination_refusal']['error']
        for name in ('invalid_original','invalid_retry'):
            assert r[name]['error'] and r[name]['sources']==[] and not r[name]['ready']


def test_generic_zip_workflow_opens_native_review_and_batch_refuses(tmp_path):
    from test_imagej_roi_zip_export import fixture
    doc,reads,_,items=fixture();before=doc.snapshot()
    record=ij.resolve_imagej_roi_zip_export(before,items).record()
    editor=_desktop.Editor(str(tmp_path/'preferences.json'));editor.add_native_document(doc)
    editor.workflow=json.dumps([record]);original=editor.workflow
    editor.action('run_workflow','{}')
    assert editor.busy and doc.snapshot().shares_data(before)
    editor.action('cancel_native_workflow','{}')
    editor.action('batch','{}')
    assert editor.busy and doc.snapshot().shares_data(before)
    editor.action('cancel_native_workflow','{}')
    assert editor.workflow==original and not editor.busy and doc.snapshot().shares_data(before)
    assert reads.counts().plane_reads==reads.counts().region_reads==0
    assert not list(tmp_path.glob('*.zip'))
