"""Shared Run review controls, staged files and atomic scientific publication."""
import io
import json
import os
from pathlib import Path
import zipfile
import numpy as np
import pytest
import imagejxx as ij
from imagejxx import _desktop
from test_display_annotations_ui import png_rgb

SOURCES = 'imagejxx.imagej_roi_import_sources'
ROWS = 'imagejxx.planar_annotations'
DISPLAY = 'imagejxx.planar_display_annotations'


@pytest.fixture(scope='module')
def reports(tmp_path_factory):
    cpp = json.loads(Path(os.environ['IMAGEJXX_NATIVE_WORKFLOW_CPP_REPORT']).read_bytes())
    directory = tmp_path_factory.mktemp('native-workflow-ui') / 'capture001'
    editor = _desktop.Editor(str(directory.parent / 'preferences.json'))
    python = json.loads(editor._native_workflow_ui_case(str(directory)))
    assert python == json.loads((directory / 'case.json').read_bytes())
    return cpp, python


def test_physical_run_step_confirmation_and_detached_preparation(reports):
    for r in reports:
        assert r['contract'] == 'p06q.native-workflow.run.ui.001'
        assert r['state'] == 'observed' and r['error'] == '' and not r['acceptance_claim']
        assert all(f['events'] == f['rendered']['input_frame']['raw_inputs'] for f in r['frames'])
        assert all(c['control']['id'] and c['control']['enabled'] for c in r['controls'])
        assert not r['unconfirmed']['controls']['prepare']['enabled']
        assert r['review']['ready'] and all(e['confirmed'] for e in r['review']['entries'])
        assert r['states']['before'] == r['states']['review']
        reads = r['admission_reads']
        assert reads['before_plane'] == reads['after_plane'] and reads['before_region'] == reads['after_region']


def test_atomic_scientific_import_filter_and_frozen_target(reports):
    for r in reports:
        s = r['states']; metadata = s['applied']['metadata']
        assert s['applied']['shape'] == [2, 3, 2, 6, 8, 1]
        assert r['plane_values'] == [15, 15] + [10] * 10
        assert r['display'] == {'t': 1, 'c': 2, 'crosshair': [1, 2, 1]}
        assert metadata[SOURCES]['version'] == 4 and len(metadata[SOURCES]['entries']) == 2
        row = metadata[ROWS]['rows'][0]
        assert row['plane'] == 'YZ' and row['coordinate'] == 1
        assert row['selection']['points'] == [[1.250000000001, 2.500000000001]]
        assert row['attributes'] == {'opaque': 2**64 - 1}
        assert metadata[DISPLAY]['rows'][0]['payload']['text'] == 'Workflow λ😀'
        assert s['applied']['undo'] == s['before']['undo'] + 1
        assert s['undone']['metadata'] == s['before']['metadata']
        assert s['redone']['metadata'] == metadata
        assert s['applied']['workflow'] == s['before']['workflow']


def test_explicit_output_is_complete_mixed_zip_with_original_and_native_rows(reports):
    for r in reports:
        assert r['published']['terminal'] and r['published']['files'][0]['published']
        path = Path(r['directory']) / 'complete.zip'
        with zipfile.ZipFile(path) as z:
            assert z.namelist() == ['geometry.roi', 'text.roi']
            geometry = ij.decode_imagej_roi_native_envelope(z.read('geometry.roi'))
            text = ij.decode_imagej_display_roi_envelope(z.read('text.roi'))
        assert geometry.annotation.name == 'Edited geometry' and geometry.annotation.coordinate == 1
        assert geometry.source_record.roi.name == 'Original geometry'
        assert geometry.annotation.attributes == {'opaque': 2**64 - 1}
        assert text.annotation.payload.text == 'Workflow λ😀'


def test_no_overwrite_race_reports_partial_files_without_document_publication(reports):
    for r in reports:
        assert r['partial']['error'] and r['partial']['attempted'] and r['partial']['terminal']
        assert [f['published'] for f in r['partial']['files']] == [True, False]
        assert r['states']['race_before'] == r['states']['race_after']
        assert (Path(r['directory']) / 'race-second.zip').read_bytes() == b'\x01\x02\x03\x04'
        with zipfile.ZipFile(Path(r['directory']) / 'race.zip') as z:
            assert z.namelist() == ['geometry.roi', 'text.roi']
        assert not r['partial']['controls']['apply']['enabled']


def test_cancel_reservation_stale_cap_and_unsupported_batch_leave_no_outputs(reports):
    for r in reports:
        assert not r['alias_refusal']['ready']
        assert 'same file' in r['alias_refusal']['error']
        assert not any(f['published'] for f in r['alias_refusal']['files'])
        assert r['offthread_cancel']['error'] and not any(f['published'] for f in r['offthread_cancel']['files'])
        assert r['reservation_refusal']['error']
        assert not any(f['published'] for f in r['reservation_refusal']['files'])
        assert r['states']['reservation_before'] == r['states']['reservation_after']
        assert not r['cancelled']['visible']
        assert not r['stale']['visible'] and r['stale']['cancel_reason'] == 'source_changed'
        assert not r['cap']['visible'] and r['cap']['cancel_reason'] == 'workflow_context_changed'
        assert r['unsupported_error'] and r['batch_error']
        assert 'memory setting' in r['memory_refusal']
        directory = Path(r['directory'])
        for name in ['cancelled.zip', 'offthread-cancel.zip', 'reservation.zip', 'stale.zip', 'cap.zip', 'not-created']:
            assert not (directory / name).exists()
        allowed = {'workflow-review.png', 'complete.zip', 'race.zip', 'race-second.zip',
                   'recording-off.zip', 'workflow.ijx', 'reopened.json', 'authoring001'}
        assert set(r['directory_members']) <= allowed


def test_recording_off_session_transport_and_native_ui_render(reports):
    for r in reports:
        assert r['recording_off']['message'] == 'Workflow published'
        assert all(f['published'] for f in r['recording_off']['files'])
        s = r['states']; assert s['final']['workflow'] == s['before']['workflow']
        assert s['final']['metadata'] == s['reopened']['metadata']
        capture = ij.load_session(str(Path(r['directory']) / 'workflow.ijx'))
        snapshot = capture.documents[0].snapshot()
        assert snapshot.dataset.shape == (2, 3, 2, 6, 8, 1)
        expected = np.full((2, 3, 2, 6, 8), 10, dtype=np.uint16)
        expected[0, 0] = 15; expected[1, 2] = 15
        np.testing.assert_array_equal(snapshot.dataset.numpy('TCZYX'), expected)
        pixels = png_rgb(Path(r['directory']) / 'workflow-review.png')
        assert pixels.shape[0] >= 700 and pixels.shape[1] >= 1000


def test_private_prefix_requires_confirmation_and_keeps_typed_choices(reports):
    for r in reports:
        a = r['authoring']
        assert not a['prefix_refusal']['controls']['edit_choices']['enabled']
        assert a['import_open']['prefix_steps'] == 0 and a['import_open']['editor']['draft_mode']
        assert a['import_open']['prefix_bytes'] > 0
        assert a['import_kept']['entries'][0]['authored']
        assert not any(e['confirmed'] for e in a['import_kept']['entries'])
        assert not a['import_kept']['prefix_ready'] and a['import_kept']['editor'] == {}


def test_export_editor_receives_prior_import_and_filter_without_file_controls(reports):
    for r in reports:
        a = r['authoring']; p = a['export_open']; e = p['editor']
        assert p['prefix_steps'] == 2 and p['prefix_bytes'] > 0
        assert e['draft_mode'] and e['annotation_count'] == 1 and e['display_count'] == 1
        assert 'path' not in e['controls'] and 'export' not in e['controls']
        assert a['export_review']['ready'] and a['export_review']['queue_count'] == 1
        assert not a['export_review']['compress'] and a['member_review']['projection_group'] == 7
        assert a['export_kept']['entries'][2]['authored'] and not a['export_kept']['entries'][2]['confirmed']
        assert [e['confirmed'] for e in a['target_changed']['entries']] == [True, False, False]
        assert png_rgb(Path(r['directory']) / 'authoring001' / 'choice-review.png').shape[0] >= 700


def test_authored_workflow_preserves_original_wire_and_precise_native_row(reports):
    for r in reports:
        a = r['authoring']; m = a['applied']['metadata']
        row = m[ROWS]['rows'][0]
        assert row['name'] == 'Workflow-authored point'
        assert row['selection']['points'] == [[1.250000000001, 2.500000000001]]
        assert row['attributes'] == {'opaque': 2**64 - 1}
        assert m[SOURCES]['version'] == 4
        text = m[DISPLAY]['rows'][0]
        assert text['name'] == 'Workflow-authored text'
        assert text['payload']['text'] == 'Authored λ😀\nSecond line'
        assert text['payload']['font_size'] == 2.500000000001
        assert a['published']['terminal'] and a['published']['files'][0]['published']
        assert a['applied']['workflow'] == r['portable']['table']
        with zipfile.ZipFile(Path(r['directory']) / 'authoring001' / 'reviewed.zip') as z:
            assert z.namelist() == ['reviewed-point.roi']
            assert z.getinfo('reviewed-point.roi').compress_type == zipfile.ZIP_STORED
            wire = z.read('reviewed-point.roi')
            envelope = ij.decode_imagej_roi_native_envelope(wire)
        assert envelope.annotation.name == 'Workflow-authored point'
        assert envelope.annotation.attributes == {'opaque': 2**64 - 1}
        assert envelope.source_record.roi.name == 'Original geometry'
        assert ij.decode_imagej_roi(wire).group == 7


def test_canceled_nested_draft_retains_parent_and_live_document(reports):
    for r in reports:
        a = r['authoring']; p = a['child_canceled']
        assert a['anchor_unchanged'] and p['visible'] and not p['prefix_ready']
        assert p['editor'] == {} and not p['entries'][0]['authored']
        assert not p['ready'] and not p['files']
        assert a['reopened_import']['replacement_editor']['name'] == 'Workflow-authored point'
        assert a['reopened_canceled']['entries'][0]['authored']
        assert not a['reopened_canceled']['prefix_ready']


def test_offthread_and_stale_nested_review_discard_prefix_resources(reports):
    for r in reports:
        a = r['authoring']; p = a['offthread_canceled']
        assert p['error'] and not p['prefix_ready'] and p['editor'] == {}
        assert not a['stale']['visible'] and not a['stale']['prefix_ready']
        assert a['stale']['editor'] == {} and a['stale']['cancel_reason'] == 'source_changed'


def test_portable_program_preparation_and_cancellation_leave_source_and_table(reports):
    for r in reports:
        p = r['portable']
        assert p['prepared']['program_ready'] and p['prepared']['files'] == []
        assert png_rgb(Path(r['directory']) / 'authoring001' / 'program-review.png').shape[0] >= 700
        assert p['prepared']['entries'][2]['path'] == ''
        assert p['canceled']['error'] and p['canceled']['attempted']
        root = p['table'][0]
        assert root['schema'] == 'imagejxx.native-workflow-program' and root['version'] == 1
        assert [s['kind'] for s in root['steps']] == ['import', 'operation', 'export']
        assert root['steps'][1]['target'] == {'scope': 2, 'roi': 0, 'mode': 0,
            'destination': 'in_place', 't': {'start': 1, 'stop': 2, 'step': 1},
            'c': {'start': 1, 'stop': 2, 'step': 1}, 'z': {'start': 0, 'stop': 2, 'step': 1}, 'positions': []}
        assert 'path' not in root['steps'][2] and 'file_consent' not in root['steps'][2]


def test_physical_workflow_save_clear_load_and_invalid_load_retains_table(reports):
    for r in reports:
        p = r['portable']; path = Path(r['directory']) / 'authoring001' / 'program-λ.json'
        assert json.loads(path.read_bytes()) == p['table'][0] == p['file']
        assert ij.NativeWorkflowProgram.from_json(path.read_text(encoding='utf-8')).record() == p['file']
        assert p['save_picker']['open'] and p['load_picker']['open']
        assert p['load_error'] and p['legacy_restored']
        migrated = p['legacy_migrated']['steps']
        assert migrated[0]['original']['schema'] == 'imagejxx.imagej-roi-import-command'
        assert migrated[1]['original'] == r['states']['before']['workflow'][1]
        assert migrated[2]['original']['schema'] == 'imagejxx.imagej-roi-export'
        assert json.loads((path.parent / 'legacy-portable.json').read_bytes()) == p['legacy_migrated']
        assert p['legacy_reedited']['steps'][0]['original'] == migrated[0]['original']
        assert p['legacy_reedited']['steps'][0]['inputs']['version'] == 4
        envelope = ij.decode_imagej_roi_native_envelope((path.parent / 'legacy-reedited.roi').read_bytes())
        assert envelope.annotation.name == 'Legacy authored row'
        assert envelope.source_record.roi.name == 'Original geometry'
        names = {c['name'] for c in r['controls']}
        assert {'workflow.save', 'workflow.clear', 'workflow.load', 'workflow.browser.path', 'workflow.browser.submit'} <= names


def test_loaded_portable_choices_reopen_without_private_owner_binding(reports):
    for r in reports:
        p = r['portable']
        assert p['loaded_import']['editor']['draft_mode']
        assert p['loaded_geometry']['replacement_editor']['name'] == 'Workflow-authored point'
        assert p['loaded_export']['editor']['queue_count'] == 1
        assert not p['loaded_export']['editor']['compress']
        assert 'path' not in p['loaded_export']['editor']['controls']
        root = p['file']; inputs = root['steps'][0]['inputs']
        assert inputs['version'] == 4
        row = inputs['entries'][1]['source']['native_replacement']['rows'][0]
        assert row['payload']['text'] == 'Authored λ😀\nSecond line'
        assert row['payload']['font_size'] == 2.500000000001
        target = p['retargeted_table'][0]['steps'][1]['target']
        assert target['scope'] == 1 and target['t'] is target['c'] is target['z'] is None
        assert target['positions'] == []
        assert p['high_budget'] == 8192
        assert json.loads((Path(r['directory']) / 'authoring001' / 'program-high-budget.json').read_bytes()) == p['retargeted_table'][0]


def test_loaded_program_executes_exact_ranges_and_preserves_workflow_table(reports):
    for r in reports:
        p = r['portable']
        assert p['applied_values'] == [10] * 8 + [15, 15, 10, 10]
        assert r['authoring']['applied']['workflow'] == p['table']
        assert r['authoring']['published']['files'][0]['published']


def test_portable_program_reuse_in_native_session_batch_preserves_input_tables(reports):
    for r in reports:
        p = r['portable']; ready = p['batch_prepared']; done = p['batch_published']
        assert ready['ready'] and all(not f['published'] for f in ready['files'])
        assert [i['state'] for i in done['items']] == ['Committed']
        assert all(f['published'] for f in done['files'])
        session = next(f['path'] for f in done['files'] if f['session'])
        output = ij.load_session(session); snapshot = output.documents[0].snapshot()
        expected = np.full((2, 3, 2, 6, 8), 10, np.uint16); expected[1, 1] = 15
        np.testing.assert_array_equal(snapshot.dataset.numpy('TCZYX'), expected)
        assert output.workflow == p['table']
        assert snapshot.metadata['extensions'][ROWS]['rows'][0]['name'] == 'Workflow-authored point'
        source = ij.load_session(str(Path(r['directory']) / 'authoring001' / 'portable-input.ijx'))
        np.testing.assert_array_equal(source.documents[0].snapshot().dataset.numpy('TCZYX'), np.full_like(expected, 10))



def test_path_free_prepared_image_has_no_staged_files_or_apply_permission(reports):
    for r in reports:
        p = r['viewport']['path_free']
        assert p['ready'] and p['preview_only'] and not p['program_ready']
        assert p['files'] == [] and not p['controls']['apply']['enabled']
        assert all(e['confirmed'] for e in p['entries'])
        assert p['entries'][2]['path'] == ''


def test_prepared_viewport_has_exact_xy_xz_yz_pixels_on_private_owner(reports):
    for r in reports:
        v = r['viewport']; p = v['initial']; planes = p['planes']
        assert [plane['plane'] for plane in planes] == ['XY', 'XZ', 'YZ']
        assert p['undo'] == 0 and p['visible']
        assert p['document_id'] != v['live_document_id']
        assert all(plane['source_document_id'] == p['document_id'] for plane in planes)
        base = np.asarray(v['source_values']).reshape(2, 3, 2)[0, 0] + 5
        for plane, width, height in zip(planes, [8, 8, 6], [6, 2, 2]):
            data = plane['readback']['source']
            assert plane['visible'] and data['shape'] == [height, width, 1]
            actual = np.asarray(data['pixels']).reshape(height, width)
            expected = np.full((height, width), base[0]) if plane['plane'] == 'XY' else np.repeat(base[:, None], width, axis=1)
            np.testing.assert_array_equal(actual, expected)


def test_prepared_viewport_draws_geometry_text_and_embedded_rgba_without_editing(reports):
    for r in reports:
        planes = r['viewport']['initial']['planes']
        image = planes[0]['display_annotations'][0]
        assert image['kind'] == 'image' and not image['error']
        assert image['rgba_prefix'] == [255, 0, 0, 160, 0, 255, 0, 160, 0, 0, 255, 160, 255, 255, 0, 160]
        text = planes[1]['display_annotations'][0]
        assert text['kind'] == 'text' and text['name'] == 'Workflow text' and not text['error']
        geometric = planes[2]['planar_annotations']
        assert geometric['controls'] == {} and not geometric['handles']
        assert geometric['layers'][0]['geometry']['points'] == [[1.250000000001, 0.500000000001]]
        assert geometric['layers'][0]['plane'] == 'YZ'


def test_prepared_viewport_private_time_channel_slice_zoom_and_live_immutability(reports):
    for r in reports:
        v = r['viewport']; nav = v['navigated']; sliced = v['sliced']; zoomed = v['zoomed']
        assert nav['display']['t'] == 1 and nav['display']['c'] == 2
        source = np.asarray(v['source_values']).reshape(2, 3, 2)[1, 2]
        np.testing.assert_array_equal(nav['planes'][0]['readback']['source']['pixels'], np.full(48, source[0]))
        assert sliced['display']['crosshair'][2] == 1
        np.testing.assert_array_equal(sliced['planes'][0]['readback']['source']['pixels'], np.full(48, source[1]))
        assert sliced['planes'][0]['display_annotations'] == []
        assert zoomed['display']['zoom'] > 0
        assert zoomed['planes'][0]['scale'][0] > sliced['planes'][0]['scale'][0]
        assert v['live_before'] == v['live_after'] and zoomed['undo'] == 0


def test_prepared_viewport_cancel_reprepare_retarget_and_stale_source_invalidation(reports):
    for r in reports:
        v = r['viewport']
        assert not v['canceled']['ready'] and v['canceled']['viewport'] == {}
        assert v['reprepared']['display'] == v['live_display']
        assert v['reprepared']['document_id'] != v['initial']['document_id']
        assert not v['retargeted']['ready'] and v['retargeted']['viewport'] == {}
        assert not v['retargeted']['entries'][1]['confirmed']
        p = v['retargeted_view']['planes'][1]['readback']['source']
        actual = np.asarray(p['pixels']).reshape(2, 8)
        base = np.asarray(v['source_values']).reshape(2, 3, 2)[0, 0]
        np.testing.assert_array_equal(actual[0], np.full(8, base[0] + 5))
        np.testing.assert_array_equal(actual[1], np.full(8, base[1]))
        assert not v['stale']['visible'] and v['stale']['viewport'] == {}


def test_prepared_viewport_bounded_admission_and_real_gl_embedded_pixels(reports):
    for r in reports:
        v = r['viewport']; reads = v['admission_reads']
        assert 'render memory' in v['memory_refusal']
        assert reads['before_plane'] == reads['after_plane'] and reads['before_region'] == reads['after_region']
        rgb = png_rgb(Path(r['directory']) / 'authoring001' / 'image-preview.png').astype(np.int16)
        assert rgb.shape == (820, 1250, 3)
        red = (rgb[:, :, 0] - rgb[:, :, 1] > 40) & (rgb[:, :, 0] - rgb[:, :, 2] > 40)
        assert red.sum() > 200
        # Restrict to the YZ image canvas, excluding UI blue highlights.
        yz = r['viewport']['initial']['planes'][2]
        x, y = [int(v) for v in yz['origin']]
        width = int(yz['width'] * yz['scale'][0]); height = int(yz['height'] * yz['scale'][1])
        canvas = rgb[max(0,y):y+height, max(0,x):x+width]
        cyan = (canvas[:, :, 1] - canvas[:, :, 0] > 60) & (canvas[:, :, 2] - canvas[:, :, 0] > 60)
        assert cyan.sum() > 5



def parameter_source(report):
    return ij.load_session(str(Path(report['directory']) / 'authoring001' / 'parameter-input.ijx')).documents[0].snapshot()


def test_parameter_invalid_keep_downstream_confirmation_and_discard(reports):
    for r in reports:
        p = r['parameters']
        assert p['invalid']['editor']['parameters'] and p['invalid']['editor']['error']
        assert all(not e['confirmed'] for e in p['kept']['entries'])
        assert p['kept']['editor'] == {} and not p['kept']['ready']
        for name in ['reopened', 'discarded']:
            value = next(f for f in p[name]['fields'] if f['name'] == 'value')
            assert value['text'] == '7' and value['kind'] == 'int64'
        root = p['registry']; assert root['version'] == 1
        assert root['steps'][0]['original']['parameters'] == {'value': 5}
        assert root['steps'][0]['parameters']['value'] == {'kind': 'int64', 'value': 7}
        source = parameter_source(r); expected = source.dataset.numpy('TCZYX').copy()
        expected[source.display.t, source.display.c] = (expected[source.display.t, source.display.c] + 7) * 2
        prepared = ij.prepare_native_workflow(source, ij.NativeWorkflowProgram.from_record(root).steps)
        np.testing.assert_array_equal(prepared.preview.dataset.numpy('TCZYX'), expected)


def test_parameter_real_input_retains_exact_signed_and_unsigned_limits(reports):
    for r in reports:
        p = r['parameters']
        for name, kind, value in [('unsigned', 'uint64', 2**64-1), ('signed', 'int64', -2**63)]:
            root = p[name]; actual = root['steps'][0]['parameters']['value']
            assert actual == {'kind': kind, 'value': value} and type(actual['value']) is int
            saved = Path(r['directory']) / 'authoring001' / ('parameters-' + name + '.json')
            assert ij.NativeWorkflowProgram.from_json(saved.read_text(encoding='utf-8')).record() == root
            assert root['steps'][0]['original']['parameters'] == {'value': 5}


def test_parameter_array_and_boolean_edits_execute_typed_convolution(reports):
    for r in reports:
        root = r['parameters']['array']; values = root['steps'][0]['parameters']
        assert values['kernel'] == {'kind': 'array', 'value': {'shape': [3, 3], 'values': [0,0,0,0,2,0,0,0,0]}}
        assert values['normalize'] == {'kind': 'bool', 'value': False}
        assert root['steps'][0]['original']['parameters']['normalize'] is True
        source = parameter_source(r); expected = source.dataset.numpy('TCZYX').copy()
        expected[source.display.t, source.display.c] *= 2
        result = ij.prepare_native_workflow(source, ij.NativeWorkflowProgram.from_record(root).steps)
        np.testing.assert_array_equal(result.preview.dataset.numpy('TCZYX'), expected)


def test_parameter_image_payload_and_text_choice_execute_without_owner_or_path_binding(reports):
    for r in reports:
        root = r['parameters']['image']; values = root['steps'][0]['parameters']
        assert root['steps'][0]['original'] is None
        assert values['operation'] == {'kind': 'text', 'value': 'subtract'}
        assert values['other']['kind'] == 'image'
        image = values['other']['value']
        assert (image['width'], image['height'], image['channels'], image['type']) == (8, 6, 1, 1)
        assert image['values'] == [2] * 48
        source = parameter_source(r); expected = source.dataset.numpy('TCZYX').copy()
        expected[source.display.t, source.display.c] -= 2
        result = ij.prepare_native_workflow(source, ij.NativeWorkflowProgram.from_record(root).steps)
        np.testing.assert_array_equal(result.preview.dataset.numpy('TCZYX'), expected)


def test_parameter_gaussian_and_rank_raw_options_roundtrip_full_originals(reports):
    for r in reports:
        p = r['parameters']
        for name, options in [('gaussian', {'kind': 'gaussian', 'sigma': 0.0, 'scaled_units': False}),
                              ('rank', {'kind': 'rank', 'method': 'mean', 'radius': 0.0})]:
            root = p[name]; step = root['steps'][0]
            assert root['version'] == 2 and step['filter_options'] == options and step['parameters'] == {}
            assert step['original'] == p[name + '_original'] and p[name + '_loaded'] == root
            file = Path(r['directory']) / 'authoring001' / ('parameters-' + name + '.json')
            assert ij.NativeWorkflowProgram.from_json(file.read_text(encoding='utf-8')).record() == root
            source = parameter_source(r)
            result = ij.prepare_native_workflow(source, ij.NativeWorkflowProgram.from_record(root).steps)
            np.testing.assert_array_equal(result.preview.dataset.numpy('TCZYX'), source.dataset.numpy('TCZYX'))
        assert p['gaussian_original']['options']['sigma'] == 2.0
        assert p['rank_original']['options']['method'] == 'median' and p['rank_original']['options']['radius'] == 2.0
        rgb = png_rgb(Path(r['directory']) / 'authoring001' / 'parameter-review.png')
        assert rgb.shape == (820, 1250, 3) and rgb.std() > 10


def test_parameter_cancel_stale_settings_and_live_source_are_guarded(reports):
    for r in reports:
        p = r['parameters']
        assert p['canceled']['editor'] == {} and not p['canceled']['prefix_ready']
        for name in ['stale', 'settings']:
            assert not p[name]['visible'] and p[name]['editor'] == {}
        assert p['live_before'] == p['live_after']


def test_parameter_v2_native_session_batch_publishes_without_running_embedded_workflows(reports):
    for r in reports:
        p = r['parameters']; ready = p['batch_prepared']; done = p['batch_published']
        assert ready['ready'] and all(not f['published'] for f in ready['files'])
        assert [i['state'] for i in done['items']] == ['Committed']
        session = next(f['path'] for f in done['files'] if f['session'])
        actual = ij.load_session(session).documents[0].snapshot().dataset.numpy('TCZYX')
        np.testing.assert_array_equal(actual, parameter_source(r).dataset.numpy('TCZYX'))
        assert all(f['published'] for f in done['files'])



def order_input(report):
    return ij.load_session(str(Path(report['directory']) / 'authoring001' / 'order-input.ijx'))


def test_order_v3_retains_full_disabled_steps_originals_tokens_and_roundtrip(reports):
    for r in reports:
        p = r['order']; root = p['edited']
        assert root == p['loaded'] and root['version'] == 3 and len(root['steps']) == 6
        assert [s['enabled'] for s in root['steps']] == [True, True, True, False, False, True]
        assert [s['kind'] for s in root['steps']] == ['import', 'operation', 'operation', 'export', 'import', 'export']
        assert [root['steps'][i]['operation'] for i in [1,2]] == ['multiply', 'add']
        for index, old_index in enumerate([0,3,1,2,4,5]):
            old = p['initial']['steps'][old_index]
            current = {k:v for k,v in root['steps'][index].items() if k not in ['enabled', 'filter_options']}
            assert current == old
        assert root['steps'][3]['name'] == 'before.zip' and root['steps'][5]['name'] == 'after.zip'
        file = Path(r['directory']) / 'authoring001' / 'order-edited.json'
        assert ij.NativeWorkflowProgram.from_json(file.read_text(encoding='utf-8')).record() == root


def test_order_disabled_export_resets_paths_confirmations_consent_and_child_capabilities(reports):
    for r in reports:
        p = r['order']; before = p['before_disable']; after = p['disabled_export']
        assert before['file_consent'] and before['entries'][2]['path']
        assert not after['file_consent'] and all(not e['confirmed'] and e['path'] == '' for e in after['entries'])
        assert not after['entries'][2]['enabled']
        assert not {'path','browse','file_consent'} & set(after['controls'])
        assert after['generation'] > before['generation']
        guard = p['child_guard']
        assert guard['editor']['parameters']
        assert all(not guard['controls'][key]['enabled'] for key in ['move_up','move_down','enabled'])
        assert not (Path(r['directory']) / 'authoring001' / 'order-disabled.zip').exists()
        rgb = png_rgb(Path(r['directory']) / 'authoring001' / 'order-review.png')
        assert rgb.shape == (820,1250,3) and rgb.std() > 10


def test_order_private_prefix_counts_only_enabled_predecessors(reports):
    for r in reports:
        p = r['order']['disabled_prefix']
        assert p['prefix_steps'] == 5 and p['prefix_completed_steps'] == 3
        assert p['editor']['draft_mode'] and len(p['editor']['sources']) == 2
        assert not p['editor']['controls'].get('export')


def test_order_preview_and_apply_use_native_order_and_skip_disabled_output(reports):
    for r in reports:
        p = r['order']; source = order_input(r).documents[0].snapshot()
        prepared = ij.prepare_native_workflow(source, ij.NativeWorkflowProgram.from_record(p['edited']).steps)
        expected = source.dataset.numpy('TCZYX').copy()
        expected[source.display.t,source.display.c] = expected[source.display.t,source.display.c] * 2 + 5
        np.testing.assert_array_equal(prepared.preview.dataset.numpy('TCZYX'),expected)
        assert prepared.completed_steps == 4 and [o['step_index'] for o in prepared.outputs] == [5]
        preview = p['preview']; assert preview['completed_steps'] == 4 and preview['files'] == []
        xy = preview['viewport']['planes'][0]['readback']['source']
        np.testing.assert_array_equal(np.asarray(xy['pixels']).reshape(6,8), expected[source.display.t,source.display.c,source.display.crosshair[2]])
        assert p['prepared']['completed_steps'] == 4 and len(p['prepared']['files']) == 1
        assert p['published']['files'][0]['published'] and p['applied']['undo'] == p['noop_before']['undo'] + 1
        assert len(p['applied']['metadata'][SOURCES]['entries']) == 2
        assert len(p['applied']['metadata'][ROWS]['rows']) == 1


def test_order_active_outputs_and_incompatible_moves_require_normal_preparation(reports):
    for r in reports:
        p = r['order']
        assert not p['missing_active_path']['ready'] and p['missing_active_path']['error']
        assert not p['incompatible']['ready'] and p['incompatible']['error']
        assert p['incompatible']['files'] == []
        assert not (Path(r['directory']) / 'authoring001' / 'order-incompatible.zip').exists()
        assert p['restored_before'] == p['restored_after']


def test_order_all_disabled_apply_is_a_noop_with_retained_v3_program(reports):
    for r in reports:
        p = r['order']; root = p['all-disabled']
        assert root['version'] == 3 and len(root['steps']) == 6 and not any(s['enabled'] for s in root['steps'])
        assert p['all_disabled_prepared']['completed_steps'] == 0 and p['all_disabled_prepared']['files'] == []
        assert p['all_disabled_applied'] == p['noop_before']
        assert p['all_disabled_published']['message'] == 'No enabled steps; image and files unchanged'


def test_order_all_disabled_batch_emits_only_a_confirmed_session_copy(reports):
    for r in reports:
        p = r['order']
        assert not p['batch_without_consent']['controls']['prepare']['enabled']
        assert len(p['batch_prepared']['files']) == 1 and p['batch_prepared']['files'][0]['session']
        done = p['batch_published']; assert done['files'][0]['published']
        actual = ij.load_session(done['files'][0]['path']); original = order_input(r)
        np.testing.assert_array_equal(actual.documents[0].snapshot().dataset.numpy('TCZYX'),original.documents[0].snapshot().dataset.numpy('TCZYX'))
        assert actual.workflow == original.workflow and actual.workflow[0]['version'] == 1



def test_rgb_stack_workflow_actual_anchor_widgets_portable_program_and_paired_apply(reports):
    for r in reports:
        s=r["rgb_stack"];program=s["table"][0];step=program["steps"][0]
        assert step["operation"]=="rgb_contrast_stack" and step["target"]["positions"]==[[0,0,1]] and step["original"]==s["original"]
        assert s["original"]["capture"]["reset_anchor"]==[0,0,0] and len(s["original"]["stages"])==2
        assert s["preview"]["preview_only"] and not s["preview"]["controls"]["apply"]["enabled"]
        expected=np.tile(np.array([85,30,40]),(8,8,1));expected[1]=[255,170,255]
        np.testing.assert_array_equal(np.asarray(s["after"]).reshape(8,8,3),expected)
        assert s["navigation_after"]==s["navigation_before"]=={"t":1,"c":1,"crosshair":[1,1,1],"zoom":1.5}
        assert s["history_after"]==s["history_before"]+1 and s["undone"]==s["before"] and s["redone"]==s["after"]
        assert s["display_after"]==s["display_redone"]=={"display_range":[0.,255.]} and s["display_undone"]==s["display_before"]


def test_rgb_session_actual_visit_inspection_anchor_translation_and_paired_apply(reports):
    for r in reports:
        s=r["rgb_session"];operation=s["table"][0]["steps"][0]
        assert operation["operation"]=="rgb_contrast_session" and operation["target"]["positions"]==[[1,1,0]]
        assert operation["original"]==s["original"] and len(s["original"]["visits"])==3
        assert [v["position"] for v in s["original"]["visits"]]==[[0,0,0],[0,0,1],[0,0,0]]
        assert s["preview"]["preview_only"] and not s["preview"]["controls"]["apply"]["enabled"]
        expected=np.tile(np.array([20,30,30]),(8,8,1));expected[:,:2,2]=170
        expected[6,:2]=[85,30,175];expected[7,:2]=[20,170,170]
        np.testing.assert_array_equal(np.asarray(s["after"]).reshape(8,8,3),expected)
        assert s["navigation_after"]==s["navigation_before"]=={"t":1,"c":1,"crosshair":[1,1,1],"zoom":1.5}
        assert s["history_after"]==s["history_before"]+1 and s["undone"]==s["before"] and s["redone"]==s["after"]
        assert s["display_after"]==s["display_redone"]=={"display_range":[0.,255.]} and s["display_undone"]==s["display_before"]
