"""Physical native session batch controls and independent output equations."""
import json
import os
from pathlib import Path
import zipfile
import numpy as np
import pytest
import imagejxx as ij
from imagejxx import _desktop
from test_display_annotations_ui import png_rgb

SOURCES='imagejxx.imagej_roi_import_sources'
ROWS='imagejxx.planar_annotations'

@pytest.fixture(scope='module')
def reports(tmp_path_factory):
    cpp=json.loads(Path(os.environ['IMAGEJXX_NATIVE_BATCH_CPP_REPORT']).read_bytes())
    directory=tmp_path_factory.mktemp('native-batch-ui')/'capture001'
    editor=_desktop.Editor(str(directory.parent/'preferences.json'))
    python=json.loads(editor._native_workflow_batch_ui_case(str(directory)))
    assert python==json.loads((directory/'case.json').read_bytes())
    return cpp,python

def test_shared_batch_physical_confirmation_and_no_live_edit(reports):
    for r in reports:
        assert r['state']=='observed' and r['error']=='' and not r['acceptance_claim']
        assert all(f['events']==f['rendered']['input_frame']['raw_inputs'] for f in r['frames'])
        assert all(c['control']['id'] and c['control']['enabled'] for c in r['controls'])
        s=r['states'];assert s['before']==s['review']==s['published']
        assert s['published']['undo']==0 and r['inputs_unchanged']
        assert r['review']['batch'] and r['review']['ready']
        assert all(not f['published'] for f in r['review']['files'])
        assert [i['state'] for i in r['published']['items']]==['Committed','Committed']
        assert all(f['published'] for f in r['published']['files'])
        assert {'browser.path','browser.submit','batch.browse_input','batch.browse_directory'} <= {c['name'] for c in r['controls']}

def test_native_output_sessions_preserve_scientific_axes_and_unprocessed_documents(reports):
    for r in reports:
        directory=Path(r['directory'])
        for name,base,tc in [('first',10,(0,0)),('second',20,(1,2))]:
            original=ij.load_session(str(directory/(name+'.ijx')))
            decoded=ij.load_session(str(directory/'success'/(name+'-processed.ijx')))
            assert decoded.results==original.results==[{'result':42}]
            assert decoded.workflow==original.workflow==[{'schema':'unexecuted.input.workflow'}]
            assert decoded.extensions==original.extensions=={'opaque':[True,2**64-1]}
            selected,other=[d.snapshot() for d in decoded.documents]
            assert selected.dataset.shape==(2,3,2,6,8,1) and other.dataset.shape==(1,1,3,4,5,3)
            expected=np.full((2,3,2,6,8),base,np.uint16);expected[tc[0],tc[1]]+=5
            np.testing.assert_array_equal(selected.dataset.numpy('TCZYX'),expected)
            np.testing.assert_array_equal(other.dataset.numpy('ZYXS'),original.documents[1].snapshot().dataset.numpy('ZYXS'))
            archive=selected.metadata['extensions'][SOURCES]
            assert archive['version']==4 and len(archive['entries'])==2
            row=selected.metadata['extensions'][ROWS]['rows'][0]
            assert row['plane']=='YZ' and row['coordinate']==1 and row['attributes']=={'opaque':2**64-1}
            assert row['selection']['points']==[[1.250000000001,.500000000001]]
            with zipfile.ZipFile(directory/'success'/(name+'-processed.d1.step-2.zip')) as z:
                assert z.namelist()==['geometry.roi','text.roi']
                assert ij.decode_imagej_roi_native_envelope(z.read('geometry.roi')).annotation.name=='Edited geometry'

def test_partial_publication_keeps_committed_outputs_and_foreign_winner(reports):
    for r in reports:
        p=r['partial'];assert p['error'] and p['terminal']
        assert [i['state'] for i in p['items']]==['Committed','Partial']
        assert [f['published'] for f in p['files']]==[True,True,True,False]
        assert p['files'][-1]['failed']
        directory=Path(r['directory'])/'race'
        assert (directory/'second-processed.ijx').read_bytes()==b'\x01\x02\x03\x04'
        assert ij.load_session(str(directory/'first-processed.ijx')).documents

def test_cancel_stale_and_late_bad_input_remove_uncommitted_files(reports):
    for r in reports:
        assert r['canceled']['error'] and not any(f['published'] for f in r['canceled']['files'])
        assert not r['stale']['visible'] and r['stale']['cancel_reason']=='source_changed'
        assert r['bad']['error'] and not r['bad']['ready']
        assert [i['state'] for i in r['bad']['items']]==['Prepared','Failed']
        for name in ['cancel','stale','bad']:
            assert list((Path(r['directory'])/name).iterdir())==[]

def test_explicit_all_document_mode_adapts_to_each_document(reports):
    for r in reports:
        assert r['all_documents']['message']=='Batch published'
        for name,base,tc in [('first',10,(0,0)),('second',20,(1,2))]:
            decoded=ij.load_session(str(Path(r['directory'])/'all'/(name+'-processed.ijx')))
            a,b=[d.snapshot() for d in decoded.documents]
            expected=np.full((2,3,2,6,8),base,np.uint16);expected[tc[0],tc[1]]+=5
            np.testing.assert_array_equal(a.dataset.numpy('TCZYX'),expected)
            np.testing.assert_array_equal(b.dataset.numpy('ZYXS'),np.full((3,4,5,3),35,np.uint16))
            assert a.metadata['extensions']=={}

def test_native_batch_review_layout(reports):
    for r in reports:
        pixels=png_rgb(Path(r['directory'])/'batch-review.png')
        assert pixels.shape[0]>=700 and pixels.shape[1]>=1000


def test_input_program_authoring_uses_selected_document_and_preserves_common_program(reports):
    for r in reports:
        assert r['input_open']['input_review']
        draft=r['input_open']['input_editor']
        assert draft['draft_mode'] and 'prepare' not in draft['controls']
        assert r['input_preview']['input_editor']['preview_only']
        row=r['input_kept']['inputs'][1]
        assert row['program_override'] and row['selection_override'] and row['document_index']==1
        assert len(row['fingerprint'])==64
        assert r['input_published']['message']=='Batch published'
        directory=Path(r['directory'])/'overrides'
        a=ij.load_session(str(directory/'first-processed.ijx'))
        b=ij.load_session(str(directory/'second-processed.ijx'))
        expected=np.full((2,3,2,6,8),10,np.uint16); expected[0,0]+=5
        np.testing.assert_array_equal(a.documents[0].snapshot().dataset.numpy('TCZYX'),expected)
        np.testing.assert_array_equal(a.documents[1].snapshot().dataset.numpy('ZYXS'),np.full((3,4,5,3),30,np.uint16))
        expected=np.full((2,3,2,6,8),20,np.uint16)
        np.testing.assert_array_equal(b.documents[0].snapshot().dataset.numpy('TCZYX'),expected)
        np.testing.assert_array_equal(b.documents[1].snapshot().dataset.numpy('ZYXS'),np.full((3,4,5,3),39,np.uint16))
        assert a.workflow==b.workflow==[{'schema':'unexecuted.input.workflow'}]


def test_changed_input_keep_and_publication_guards(reports):
    for r in reports:
        failed=r['input_changed_keep']
        assert 'changed since review' in failed['error'] and not failed['inputs'][0]['program_override']
        failed=r['input_changed_publish']
        assert 'changed since review' in failed['error'] and failed['terminal']
        assert not any(v['published'] for v in failed['files'])
        assert [v['state'] for v in failed['items']]==['Failed','Skipped']
        for folder in ['review-changed','publish-changed']:
            assert list((Path(r['directory'])/folder).iterdir())==[]
        assert png_rgb(Path(r['directory'])/'input-review.png').shape[0]>=700


def test_restore_inheritance_and_off_thread_input_review_cancellation(reports):
    for r in reports:
        row=r['input_inherited']['inputs'][0]
        assert not row['program_override'] and not row['selection_override'] and row['fingerprint']==''
        assert not r['input_canceled']['input_review'] and not r['input_canceled']['working']
        assert not r['input_canceled']['inputs'][0]['program_override']
        assert list((Path(r['directory'])/'inheritance').iterdir())==[]


def test_setup_save_load_closed_intent_and_fresh_input_review(reports):
    for r in reports:
        directory=Path(r['directory']);record=json.loads((directory/'saved-batch.json').read_bytes())
        assert record['schema']=='imagejxx.native-workflow-batch-setup' and record['version']==1
        assert record['inputs'][0]['program'] is None and record['inputs'][0]['selection'] is None
        assert record['inputs'][1]['program']['steps'][0]['parameters']['value']=={'kind':'int64','value':9}
        assert record['inputs'][1]['selection']=={'all_documents':False,'document_index':1}
        assert all('expected_fingerprint' not in v for v in record['inputs'])
        assert r['setup_saved']['error']=='' and r['setup_existing']['error']==''
        assert r['setup_replaced']['error']==''
        assert {'setup.browser.replace','setup.browser.replace_cancel'} <= {v['name'] for v in r['controls']}
        assert r['setup_bad']['error'] and r['setup_bad']['inputs']==r['setup_replaced']['inputs']
        loaded=r['setup_loaded'];assert not loaded['file_consent'] and not loaded['ready']
        assert loaded['inputs'][1]['fingerprint']=='' and loaded['inputs'][1]['program_override']
        assert not r['setup_needs_review']['controls']['prepare']['enabled']
        assert png_rgb(directory/'batch-setup-loaded.png').shape[0]>=700


def test_setup_job_cancellation_preserves_draft_and_stale_save_publishes_nothing(reports):
    for r in reports:
        canceled=r['setup_canceled']
        assert canceled['visible'] and not canceled['working']
        assert len(canceled['inputs'])==2 and all(not i['program_override'] for i in canceled['inputs'])
        assert not r['setup_stale']['visible'] and r['setup_stale']['cancel_reason']=='source_changed'
        directory=Path(r['directory'])
        assert not (directory/'stale-setup.json').exists() and list((directory/'setup-cancel').iterdir())==[]
