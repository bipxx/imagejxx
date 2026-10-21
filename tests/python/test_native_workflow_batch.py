"""Scientific session batch plans, retained metadata, conflict and failure policy."""
import copy
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij


def fixture(tmp_path):
    source = ij.Document(ij.Dataset(np.full((2, 3, 2, 4, 5), 10, np.uint16), axes='TCZYX'))
    view = source.snapshot().display; view.t = 1; view.c = 2; source.set_display(view)
    edit = source.begin_edit(); edit.set_extension('opaque', {'uint': 2**64-1}); source.commit(edit)
    other = ij.Document(ij.Dataset(np.full((3, 3, 7), 20, np.uint16), axes='ZYX'))
    capture = ij.SessionCapture([source.snapshot(), other.snapshot()])
    capture.results = [{'result': 42}]; capture.workflow = [{'schema': 'never.execute'}]
    capture.extensions = {'opaque': [True, 2**64-1]}
    paths = [tmp_path/'first.ijx', tmp_path/'second.ijx']
    for p in paths: ij.save_session(str(p), capture)
    output = tmp_path/'output'; output.mkdir()
    inputs = [ij.NativeWorkflowBatchInput(str(p), name) for p, name in zip(paths, ['one', 'two'])]
    request = ij.TargetRequest(ij.TargetScope.ALL_Z, roi=ij.RoiPolicy.WHOLE_PLANE)
    steps = [ij.NativeWorkflowOperation('add', {'value': 5}, request)]
    return source, capture, paths, inputs, output, steps


@pytest.mark.parametrize('all_documents', [False, True])
def test_session_batch_preserves_axes_other_documents_and_session_tables(tmp_path, all_documents):
    source, capture, paths, inputs, output, steps = fixture(tmp_path)
    original = [p.read_bytes() for p in paths]; before = source.snapshot()
    prepared = ij.prepare_native_workflow_batch(inputs, steps, str(output),
        ij.NativeWorkflowBatchOptions(all_documents=all_documents))
    assert not any((output/n).exists() for n in ['one.ijx','two.ijx'])
    assert not prepared.attempted and prepared.encoded_bytes > 0
    assert all(i['state'] == ij.NativeWorkflowBatchState.PREPARED for i in prepared.items)
    prepared.publish()
    assert all(i['state'] == ij.NativeWorkflowBatchState.COMMITTED for i in prepared.items)
    assert all(o['published'] and o['session'] and not o['failed'] for o in prepared.outputs)
    for name in ['one', 'two']:
        decoded = ij.load_session(str(output/(name+'.ijx')))
        assert decoded.results == capture.results and decoded.workflow == capture.workflow
        assert decoded.extensions == capture.extensions
        a, b = [d.snapshot() for d in decoded.documents]
        assert a.dataset.shape == (2,3,2,4,5,1) and b.dataset.shape == (1,1,3,3,7,1)
        expected = np.full((2,3,2,4,5),10,np.uint16); expected[1,2] = 15
        np.testing.assert_array_equal(a.dataset.numpy('TCZYX'),expected)
        np.testing.assert_array_equal(b.dataset.numpy('ZYX'),np.full((3,3,7),25 if all_documents else 20,np.uint16))
        assert a.metadata['extensions']['opaque'] == {'uint':2**64-1}
    assert [p.read_bytes() for p in paths] == original and source.snapshot().shares_data(before)
    with pytest.raises(ValueError): prepared.publish()


def test_selected_index_preserves_other_document_and_nonfinite_sample_bits(tmp_path):
    source, capture, paths, inputs, output, steps = fixture(tmp_path)
    raw = np.array([[0x7fc12345,0x3f800000]],np.uint32).view(np.float32)
    quiet = ij.Document(ij.Dataset(raw, axes='YX', sample_policy=ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32))
    capture.documents = [quiet.snapshot(), source.snapshot()]
    ij.save_session(str(paths[0]),capture)
    prepared = ij.prepare_native_workflow_batch(inputs[:1],steps,str(output),ij.NativeWorkflowBatchOptions(document_index=1))
    prepared.publish(); decoded = ij.load_session(str(output/'one.ijx'))
    untouched = decoded.documents[0].snapshot().dataset.numpy('YX')
    np.testing.assert_array_equal(untouched.view(np.uint32),raw.view(np.uint32))
    assert decoded.documents[0].snapshot().dataset.sample_policy == quiet.snapshot().dataset.sample_policy


@pytest.mark.parametrize('bad', ['duplicate_input','duplicate_output','existing','missing_input','bad_late_session','index','tiny_limit','stem','missing_directory'])
def test_whole_preparation_refuses_before_output_or_directory_creation(tmp_path,bad):
    _, _, paths, inputs, output, steps = fixture(tmp_path)
    options = ij.NativeWorkflowBatchOptions()
    if bad == 'duplicate_input': inputs[1] = ij.NativeWorkflowBatchInput(str(paths[0]),'two')
    elif bad == 'duplicate_output': inputs[1] = ij.NativeWorkflowBatchInput(str(paths[1]),'one')
    elif bad == 'existing': (output/'two.ijx').write_bytes(b'winner')
    elif bad == 'missing_input': inputs[1] = ij.NativeWorkflowBatchInput(str(tmp_path/'missing.ijx'),'two')
    elif bad == 'bad_late_session': paths[1].write_bytes(b'bad')
    elif bad == 'index': options = ij.NativeWorkflowBatchOptions(document_index=99)
    elif bad == 'tiny_limit': options = ij.NativeWorkflowBatchOptions(max_working_bytes=1)
    elif bad == 'stem': inputs[1] = ij.NativeWorkflowBatchInput(str(paths[1]),'../bad')
    elif bad == 'missing_directory': output = tmp_path/'missing-output'
    existing = {p.name:p.read_bytes() for p in output.iterdir()} if output.exists() else None
    with pytest.raises((ValueError,RuntimeError,IndexError)):
        ij.prepare_native_workflow_batch(inputs,steps,str(output),options)
    assert ({p.name:p.read_bytes() for p in output.iterdir()} if output.exists() else None) == existing


@pytest.mark.parametrize('when', ['before','after_first','callback'])
def test_cancellation_and_callback_failure_create_no_outputs(tmp_path,when):
    _, _, _, inputs, output, steps = fixture(tmp_path)
    token = ij.CancellationToken(); events = []
    if when == 'before': token.cancel()
    def progress(index,state):
        events.append((index,state))
        if state == ij.NativeWorkflowBatchState.PREPARED:
            if when == 'callback': raise RuntimeError('deliberate callback failure')
            token.cancel()
    with pytest.raises(RuntimeError):
        ij.prepare_native_workflow_batch(inputs,steps,str(output),cancellation=token,progress=progress)
    assert list(output.iterdir()) == []
    if when != 'before': assert (0,ij.NativeWorkflowBatchState.PREPARED) in events


def test_publication_race_retains_committed_item_and_skips_later_items(tmp_path):
    _, _, paths, inputs, output, steps = fixture(tmp_path)
    third = tmp_path/'third.ijx'; third.write_bytes(paths[0].read_bytes())
    inputs.append(ij.NativeWorkflowBatchInput(str(third),'three'))
    prepared = ij.prepare_native_workflow_batch(inputs,steps,str(output))
    (output/'two.ijx').write_bytes(b'foreign winner')
    with pytest.raises((ValueError,RuntimeError)): prepared.publish()
    assert [i['state'] for i in prepared.items] == [ij.NativeWorkflowBatchState.COMMITTED,ij.NativeWorkflowBatchState.FAILED,ij.NativeWorkflowBatchState.SKIPPED]
    assert [o['published'] for o in prepared.outputs] == [True,False,False]
    assert (output/'two.ijx').read_bytes() == b'foreign winner' and not (output/'three.ijx').exists()
    assert {p.name for p in output.iterdir()} == {'one.ijx','two.ijx'}


def test_abandon_removes_only_owned_staged_files(tmp_path):
    _, _, _, inputs, output, steps = fixture(tmp_path)
    prepared = ij.prepare_native_workflow_batch(inputs,steps,str(output))
    (output/'foreign.txt').write_text('keep')
    prepared.abandon()
    assert [p.name for p in output.iterdir()] == ['foreign.txt']
    assert all(i['state'] == ij.NativeWorkflowBatchState.CANCELED for i in prepared.items)
    with pytest.raises(ValueError): prepared.publish()


def test_mixed_v4_import_rgba_session_and_partial_export_publication(tmp_path):
    from test_imagej_bare_native_edits import fixture as edited_fixture
    from test_imagej_roi_zip_export import projection
    import zipfile
    import base64
    doc, _, items, rows = edited_fixture()
    source=tmp_path/'input.ijx'; ij.save_session(str(source),ij.SessionCapture([doc.snapshot()]))
    original=source.read_bytes(); output=tmp_path/'out'; output.mkdir()
    choice=ij.ImageJAnnotationExportItem('geometry.roi',0,ij.ImageJAnnotationKind.SELECTION,0,projection(rows[0]))
    steps=[ij.NativeWorkflowImport(items),ij.NativeWorkflowExport('annotations.zip',[choice])]
    prepared=ij.prepare_native_workflow_batch([ij.NativeWorkflowBatchInput(str(source),'partial')],steps,str(output))
    (output/'partial.ijx').write_bytes(b'winner')
    with pytest.raises((ValueError,RuntimeError)): prepared.publish()
    assert prepared.items[0]['state']==ij.NativeWorkflowBatchState.PARTIAL
    assert [v['published'] for v in prepared.outputs]==[True,False]
    assert (output/'partial.ijx').read_bytes()==b'winner'
    with zipfile.ZipFile(output/'partial.d1.annotations.zip') as z:
        assert ij.decode_imagej_roi_native_envelope(z.read('geometry.roi')).annotation.name==rows[0].name
    good=ij.prepare_native_workflow_batch([ij.NativeWorkflowBatchInput(str(source),'complete')],steps,str(output))
    good.publish(); snapshot=ij.load_session(str(output/'complete.ijx')).documents[0].snapshot()
    assert snapshot.metadata['extensions']['imagejxx.imagej_roi_import_sources']['version']==4
    image=snapshot.metadata['extensions']['imagejxx.planar_display_annotations']['rows'][1]['payload']
    assert image['channels']==4 and base64.b64decode(image['pixels'])==rows[2].payload.pixels
    assert source.read_bytes()==original


def test_cancellation_after_final_prepared_reports_canceled(tmp_path):
    _, _, _, inputs, output, steps = fixture(tmp_path)
    token = ij.CancellationToken()
    events = []
    def progress(index, state):
        events.append((index, state))
        if index == len(inputs) - 1 and state == ij.NativeWorkflowBatchState.PREPARED:
            token.cancel()
    with pytest.raises(RuntimeError):
        ij.prepare_native_workflow_batch(inputs, steps, str(output), cancellation=token, progress=progress)
    assert events[-2:] == [(0, ij.NativeWorkflowBatchState.CANCELED), (1, ij.NativeWorkflowBatchState.SKIPPED)]
    assert list(output.iterdir()) == []
