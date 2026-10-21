"""Per-input intent, document selection, and exact reviewed-content boundaries."""
import hashlib
import numpy as np
import pytest
import imagejxx as ij
from test_native_workflow_batch import fixture


def program(value=9):
    target = ij.TargetRequest(ij.TargetScope.ALL_Z, roi=ij.RoiPolicy.WHOLE_PLANE)
    return ij.NativeWorkflowProgram([ij.NativeWorkflowOperation('add', {'value': value}, target)])


@pytest.mark.parametrize('large', [False, True])
def test_review_hash_snapshots_and_read_only_lifetime(tmp_path, large):
    _, capture, paths, _, _, _ = fixture(tmp_path)
    if large:
        values = np.random.default_rng(42).integers(0, 65536, (211, 257), dtype=np.uint16)
        capture.documents = [ij.Document(ij.Dataset(values, axes='YX')).snapshot()]
        ij.save_session(str(paths[0]), capture)
        assert paths[0].stat().st_size > 65536
    original = paths[0].read_bytes()
    reviewed = ij.review_native_workflow_batch_input(str(paths[0]))
    assert reviewed.fingerprint == hashlib.sha256(original).hexdigest()
    assert reviewed.working_bytes > 0
    reviewed.verify()
    snapshots = reviewed.documents
    assert len(snapshots) == (1 if large else 2)
    del reviewed
    assert snapshots[0].dataset.numpy('YX' if large else 'TCZYX').size > 0
    assert paths[0].read_bytes() == original


@pytest.mark.parametrize('all_documents', [False, True])
def test_input_program_and_selection_override_common_defaults(tmp_path, all_documents):
    _, capture, paths, inputs, output, common = fixture(tmp_path)
    reviewed = ij.review_native_workflow_batch_input(str(paths[1]))
    override = program()
    inputs[1] = ij.NativeWorkflowBatchInput(str(paths[1]), 'two', program=override,
        selection=ij.NativeWorkflowBatchSelection(all_documents=all_documents,
            document_index=0 if all_documents else 1), expected_fingerprint=reviewed.fingerprint)
    assert inputs[1].program.record() == override.record()
    assert inputs[1].selection.all_documents == all_documents
    prepared = ij.prepare_native_workflow_batch(inputs, common, str(output))
    prepared.publish()
    one = ij.load_session(str(output/'one.ijx'))
    two = ij.load_session(str(output/'two.ijx'))
    assert one.documents[0].snapshot().dataset.numpy('TCZYX')[1, 2, 0, 0, 0] == 15
    assert one.documents[1].snapshot().dataset.numpy('ZYX')[0, 0, 0] == 20
    assert two.documents[0].snapshot().dataset.numpy('TCZYX')[1, 2, 0, 0, 0] == (19 if all_documents else 10)
    assert two.documents[1].snapshot().dataset.numpy('ZYX')[0, 0, 0] == 29
    assert two.workflow == capture.workflow and two.results == capture.results
    assert two.extensions == capture.extensions


@pytest.mark.parametrize('boundary', ['review', 'prepare', 'publish'])
def test_changed_reviewed_content_refused_without_publication(tmp_path, boundary):
    _, _, paths, inputs, output, steps = fixture(tmp_path)
    reviewed = ij.review_native_workflow_batch_input(str(paths[1]))
    inputs[1] = ij.NativeWorkflowBatchInput(str(paths[1]), 'two', program=program(),
        expected_fingerprint=reviewed.fingerprint)
    prepared = ij.prepare_native_workflow_batch(inputs, steps, str(output)) if boundary == 'publish' else None
    raw = bytearray(paths[1].read_bytes()); raw[-1] ^= 1
    paths[1].write_bytes(raw)  # Equal length: a size-only guard would miss this.
    with pytest.raises((ValueError, RuntimeError), match='changed since review'):
        if boundary == 'review': reviewed.verify()
        elif boundary == 'prepare': ij.prepare_native_workflow_batch(inputs, steps, str(output))
        else: prepared.publish()
    assert list(output.iterdir()) == []
    if prepared:
        assert prepared.attempted
        assert [v['state'] for v in prepared.items] == [ij.NativeWorkflowBatchState.SKIPPED, ij.NativeWorkflowBatchState.FAILED]
        assert not any(v['published'] for v in prepared.outputs)
        with pytest.raises(ValueError): prepared.publish()


@pytest.mark.parametrize('fingerprint', ['a', 'G'*64, 'A'*64, '0'*63, '0'*65])
def test_bad_fingerprint_refused(fingerprint):
    with pytest.raises(ValueError):
        ij.NativeWorkflowBatchInput('missing.ijx', 'one', expected_fingerprint=fingerprint)


def test_review_cancellation_and_invalid_selector(tmp_path):
    _, _, paths, _, _, _ = fixture(tmp_path)
    with pytest.raises(ValueError): ij.NativeWorkflowBatchSelection(all_documents=True, document_index=1)
    token = ij.CancellationToken(); token.cancel()
    with pytest.raises(RuntimeError):
        ij.review_native_workflow_batch_input(str(paths[0]), cancellation=token)


@pytest.mark.parametrize('keyword', ['program','selection'])
def test_wrong_override_type_refused(keyword):
    with pytest.raises(TypeError): ij.NativeWorkflowBatchInput('missing.ijx','one',**{keyword: []})


@pytest.mark.parametrize('disabled', [False, True])
def test_typed_filter_and_disabled_override_retains_canonical_intent(tmp_path, disabled):
    _, _, paths, inputs, output, common = fixture(tmp_path)
    target=ij.TargetRequest(ij.TargetScope.ALL_Z,roi=ij.RoiPolicy.WHOLE_PLANE)
    p=ij.NativeWorkflowProgram([
        ij.NativeWorkflowOperation('add',{'value':9},target,enabled=not disabled),
        ij.NativeWorkflowOperation('gaussian_blur_imagej',{},target,
            filter_options=ij.GaussianBlurCommandOptions(0,False))])
    inputs[0]=ij.NativeWorkflowBatchInput(str(paths[0]),'one',program=p)
    assert inputs[0].program.record()==p.record()
    result=ij.prepare_native_workflow_batch(inputs[:1],common,str(output));result.publish()
    doc=ij.load_session(str(output/'one.ijx')).documents[0].snapshot()
    assert doc.dataset.numpy('TCZYX')[1,2,0,0,0]==(10 if disabled else 19)


def test_selector_only_and_all_disabled_program_override(tmp_path):
    _, _, paths, inputs, output, steps = fixture(tmp_path)
    inputs[0] = ij.NativeWorkflowBatchInput(str(paths[0]), 'one', selection=ij.NativeWorkflowBatchSelection(document_index=1))
    off = ij.NativeWorkflowProgram([ij.NativeWorkflowOperation('add', {'value':9},
        ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE), enabled=False)])
    inputs[1] = ij.NativeWorkflowBatchInput(str(paths[1]), 'two', program=off)
    result = ij.prepare_native_workflow_batch(inputs, steps, str(output)); result.publish()
    one = ij.load_session(str(output/'one.ijx')); two = ij.load_session(str(output/'two.ijx'))
    assert one.documents[0].snapshot().dataset.numpy('TCZYX')[1,2,0,0,0] == 10
    assert one.documents[1].snapshot().dataset.numpy('ZYX')[0,0,0] == 25
    assert two.documents[0].snapshot().dataset.numpy('TCZYX')[1,2,0,0,0] == 10


def test_aggregate_override_admission_deduplicates_shared_storage_before_input_open(tmp_path):
    target=ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE)
    p=ij.NativeWorkflowProgram([ij.NativeWorkflowOperation('convolve',
        {'kernel':np.ones((1,2001)), 'normalize':False}, target, enabled=False)])
    separate=ij.NativeWorkflowProgram.from_json(p.json())
    found=False
    for cap in [128,256,512,1024,2048,4096]:
        messages=[]
        for second in [p,separate]:
            rows=[ij.NativeWorkflowBatchInput(str(tmp_path/'missing.ijx'),'one',program=p),
                  ij.NativeWorkflowBatchInput(str(tmp_path/'other.ijx'),'two',program=second)]
            with pytest.raises((ValueError,RuntimeError)) as error:
                ij.prepare_native_workflow_batch(rows,[],str(tmp_path),
                    ij.NativeWorkflowBatchOptions(max_working_bytes=cap*1024))
            messages.append(str(error.value))
        if 'missing.ijx' in messages[0] and 'limit' in messages[1].lower() and 'missing.ijx' not in messages[1]:
            found=True;break
    assert found, messages
    assert list(tmp_path.iterdir())==[]
