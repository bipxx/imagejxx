"""Native root paths through the desktop facade, controls and V5 sessions."""
import json
import struct
import zlib
import numpy as np
import pytest
import imagejxx as ij
from imagejxx import _desktop

COMMANDS = [0, 1.25, 1.125, 2, 3.25, .25, 5.25, 1.125,
            3, 6.25, 2, 6.25, 4, 5.25, 4, 1, 1.25, 4, 4]


def path():
    return ij.Selection.from_path(ij.ImageJCompositePath(COMMANDS)).with_attributes({'opaque': ['kept', 2**64-1]})


def source(carrier='all'):
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.arange(840, dtype=np.uint16).reshape(6, 10, 14), axes='ZYX').observe_reads(reads))
    edit = owner.snapshot().begin_edit()
    if carrier in ['all', 'active']: edit.select(path())
    if carrier in ['all', 'previous']: edit.set_previous_selection(path())
    if carrier in ['all', 'saved']: edit.set_rois([ij.ROI('saved curve', path(), attributes={'row': 'kept'})])
    owner.commit(edit)
    return owner, reads


def editor(tmp_path, owner):
    result = _desktop.Editor(preferences_path=str(tmp_path/'preferences.json'))
    result.add_native_document(owner)
    return result


def act(editor, name, **values):
    editor.action(name, json.dumps(values))


@pytest.mark.parametrize('carrier', ['active', 'previous', 'saved', 'all'])
def test_native_root_projection_keeps_exact_geometry_without_sample_reads(carrier, tmp_path):
    owner, reads = source(carrier); before = owner.snapshot(); ui = editor(tmp_path, owner)
    wire = json.loads(ui.document_snapshot())
    if carrier in ['active', 'all']: assert wire['selection']['path'] == COMMANDS
    if carrier in ['previous', 'all']: assert wire['previous_selection']['path'] == COMMANDS
    if carrier in ['saved', 'all']:
        assert wire['rois'][0]['selection']['path'] == COMMANDS
        assert wire['rois'][0]['selection']['attributes'] == {'opaque': ['kept', 2**64-1]}
    assert owner.snapshot().shares_data(before)
    assert reads.counts().plane_reads == reads.counts().region_reads == 0


def test_active_numeric_controls_keep_topology_and_previous_until_apply(tmp_path):
    owner, reads = source(); ui = editor(tmp_path, owner); before = owner.snapshot()
    points = [[1.25, 1.125], [3.7500000000000004, .8750000000000001], [5.25, 1.125],
              [6.25, 2], [6.25, 4], [5.25, 4], [1.25, 4]]
    act(ui, 'show_selection_properties'); act(ui, 'selection_properties_draft', points=points)
    assert owner.snapshot().shares_data(before)
    act(ui, 'apply_selection_properties')
    expected = list(COMMANDS); expected[4:6] = points[1]
    result = owner.snapshot()
    assert result.selection.path.commands == expected and result.previous_selection == before.selection
    assert result.selection.attributes == before.selection.attributes and result.rois[0].selection == before.rois[0].selection
    assert result.dataset.shares_pixels(before.dataset)
    assert reads.counts().plane_reads == reads.counts().region_reads == 0


def test_saved_numeric_curve_edit_preserves_other_carriers_and_history(tmp_path):
    owner, reads = source(); ui = editor(tmp_path, owner); before = owner.snapshot(); history = owner.undo_steps
    act(ui, 'edit_saved_roi_geometry', index=0); act(ui, 'show_selection_properties')
    act(ui, 'selection_properties_draft', translation=[.25, -.125])
    assert owner.snapshot().shares_data(before)
    act(ui, 'apply_selection_properties')
    after = owner.snapshot()
    expected = ij.SelectionEditor(path(), (10, 14)).moved([.25, -.125], keep_inside=False)
    assert after.rois[0].selection == expected and after.rois[0].attributes == {'row': 'kept'}
    assert after.selection == before.selection and after.previous_selection == before.previous_selection
    assert owner.undo_steps == history+1
    owner.undo(); assert owner.snapshot().shares_data(before)
    owner.redo(); assert owner.snapshot().rois[0].selection == expected
    assert reads.counts().plane_reads == reads.counts().region_reads == 0


def test_path_properties_cancel_and_topology_refusal_keep_owner_unchanged(tmp_path):
    owner, _ = source(); ui = editor(tmp_path, owner); before = owner.snapshot()
    act(ui, 'show_selection_properties')
    with pytest.raises(ValueError, match='topology'): act(ui, 'selection_properties_draft', points=[[1, 2]])
    act(ui, 'selection_properties_draft', translation=[.25, .5]); act(ui, 'cancel_selection_properties')
    assert owner.snapshot().shares_data(before)


@pytest.mark.parametrize('carrier', ['active', 'previous', 'saved', 'all'])
def test_standard_desktop_open_and_save_native_v5_root_paths(carrier, tmp_path):
    owner, _ = source(carrier); filename = tmp_path/'native.ijx'
    ij.save_session(str(filename), ij.SessionCapture([owner]))
    assert filename.read_bytes().startswith(b'IMAGEJXX5\n')
    ui = _desktop.Editor(preferences_path=str(tmp_path/'preferences.json')); ui.open_file(str(filename))
    before = ui.native_snapshot()
    saved = tmp_path/'saved.ijx'; act(ui, 'save_document', path=str(saved))
    assert saved.read_bytes().startswith(b'IMAGEJXX5\n')
    restored = ij.load_session(str(saved)).documents[0].snapshot()
    for a, b in [(restored.selection, before.selection), (restored.previous_selection, before.previous_selection)]:
        assert a == b
        if a is not None: assert struct.pack('<19d', *a.path.commands) == struct.pack('<19d', *COMMANDS)
    assert len(restored.rois) == len(before.rois)
    for a, b in zip(restored.rois, before.rois):
        assert a.name == b.name and a.selection == b.selection and a.attributes == b.attributes


def test_optional_python_editor_detached_snapshot_preserves_root_paths(tmp_path):
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    owner, _ = source()
    ui = Editor(preferences=Preferences(tmp_path/'preferences.json'))
    ui.add_native_document(owner)
    detached = ui.document_snapshot()
    assert detached.selection.native() == path()
    assert detached.previous_selection.native() == path()
    assert detached.rois[0].selection.native() == path()
    assert ui.live_document().native_owner is owner


def test_optional_python_model_v5_save_load_and_native_transform(tmp_path):
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences, save_session, load_session
    owner, _ = source(); ui = Editor(preferences=Preferences(tmp_path/'preferences.json'))
    ui.add_native_document(owner); detached = ui.document_snapshot()
    moved = detached.selection.moved(.25, -.125, detached.shape)
    assert moved.native() == ij.SelectionEditor(path(), (10, 14)).moved([.25, -.125])
    filename = tmp_path/'optional.ijx'; save_session(filename, [detached], workflow=[{'opaque': [True, 2**64-1]}])
    assert filename.read_bytes().startswith(b'IMAGEJXX5\n')
    documents, _, workflow = load_session(filename)
    restored = documents[0]
    assert restored.selection.native() == path() and restored.previous_selection.native() == path()
    assert restored.rois[0].selection.native() == path() and restored.rois[0].attributes == {'row': 'kept'}
    assert workflow == [{'opaque': [True, 2**64-1]}]
    assert restored.data.tobytes() == detached.data.tobytes()


def test_explicit_path_projection_does_not_open_legacy_codecs(tmp_path):
    from imagejxx.ui.persistence import serialize_document, deserialize_document
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    owner, _ = source()
    with pytest.raises(ValueError, match='V5'): ij._native._session_legacy_record_json(owner.snapshot())
    ui = Editor(preferences=Preferences(tmp_path/'preferences.json')); ui.add_native_document(owner)
    item = serialize_document(ui.document_snapshot())
    with pytest.raises(ValueError, match='V5'): deserialize_document(item)
    raw = json.dumps({'format': 'imagejxx-session', 'version': 2, 'documents': [item], 'results': [], 'workflow': []}).encode()
    blob = b'IMAGEJXX2\n'+struct.pack('>I', len(raw))+zlib.compress(raw)
    with pytest.raises(ValueError, match='V5'): ij.decode_session(blob)
    upgraded = ij._native._upgrade_path_legacy_session(blob)
    assert upgraded.startswith(b'IMAGEJXX5\n')
    assert ij.decode_session(upgraded).documents[0].snapshot().selection == path()


def test_typed_saved_path_publication_upgrades_legacy_facade(tmp_path):
    donor = ij.Document(ij.Dataset(np.zeros((10, 14), dtype=np.uint8), axes='YX'))
    edit = donor.snapshot().begin_edit()
    rectangle = ij.Selection('rectangle', [[1, 1], [4, 3]], attributes=path().attributes)
    edit.set_rois([ij.ROI('legacy row', rectangle)]); donor.commit(edit)
    donor_ui = editor(tmp_path/'donor', donor)
    ui = _desktop.Editor(preferences_path=str(tmp_path/'legacy.json'))
    ui.add_document(donor_ui.document_snapshot())
    owner = ui.native_document(); before = owner.snapshot()
    invalid = before.begin_edit(); invalid.select(path())
    with pytest.raises(ValueError, match='path'): owner.commit(invalid)
    result = ui.apply_saved_roi_geometry(ij.SavedRoiGeometryRequest(0, path()))
    if isinstance(result, str): result = json.loads(result)
    assert result['changed'] is True and owner.snapshot().rois[0].selection == path()
    assert owner.snapshot().selection is None and owner.snapshot().previous_selection is None


def test_optional_model_path_binary64_and_closed_shape_fields(tmp_path):
    from imagejxx.ui.model import Selection, Document
    from imagejxx.ui.persistence import save_session, load_session
    commands = list(COMMANDS); commands[1] = -0.; commands[4] = 3.7500000000000004
    record = {'kind': 'path', 'points': [], 'width': 1., 'attributes': {'opaque': [True, 2**64-1]}, 'path': commands}
    selection = Selection.from_dict(record)
    with pytest.raises(ValueError): Selection.from_dict(dict(record, ignored=True))
    with pytest.raises(ValueError): Selection.from_dict(dict(record, points=[[0, 0]]))
    document = Document(np.zeros((10, 14), dtype=np.uint8)); document.selection = selection
    filename = tmp_path/'plain-path.ijx'; save_session(filename, [document])
    restored = load_session(filename)[0][0].selection
    assert struct.pack('<19d', *restored.native().path.commands) == struct.pack('<19d', *commands)
    assert restored.attributes == {'opaque': [True, 2**64-1]}


@pytest.mark.parametrize('marked', [False, True])
def test_public_python_v2_reader_stays_closed_to_path_records(marked, tmp_path):
    from imagejxx.ui.persistence import load_session
    owner, _ = source()
    record = json.loads(ij._native._session_path_record_json(owner.snapshot()))
    manifest = {'format': 'imagejxx-session', 'version': 2, 'documents': [record['document']], 'results': [], 'workflow': []}
    if marked: manifest['native_document_state'] = {'version': 1, 'documents': [record['native_state']]}
    data = json.dumps(manifest, allow_nan=False).encode()
    blob = b'IMAGEJXX2\n'+struct.pack('>I', len(data))+zlib.compress(data)
    filename = tmp_path/'forged-v2.ijx'; filename.write_bytes(blob)
    with pytest.raises(ValueError, match='V5'): load_session(filename)
    with pytest.raises(ValueError, match='V5'): ij.decode_session(blob)
    with pytest.raises(ValueError, match='V5'): ij._native._upgrade_regional_legacy_session(blob)
    assert ij._native._upgrade_path_legacy_session(blob).startswith(b'IMAGEJXX5\n')
