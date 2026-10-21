"""Independent evidence from actual pairing/export controls in both ImGui hosts."""
import base64
import json
import os
from pathlib import Path
import pytest
import imagejxx as ij
from imagejxx import _desktop


@pytest.fixture(scope='module')
def reports(tmp_path_factory):
    cpp=json.loads(Path(os.environ['IMAGEJXX_ROI_EXPORT_CPP_REPORT']).read_text(encoding='utf-8'))
    directory=tmp_path_factory.mktemp('roi-export-ui')/'capture001'
    editor=_desktop.Editor(str(directory.parent/'preferences.json'))
    python=json.loads(editor._roi_export_ui_case(str(directory)))
    assert python==json.loads((directory/'case.json').read_text(encoding='utf-8'))
    return [cpp,python]


def test_explicit_pair_projection_and_actual_preview_controls(reports):
    for r,context in zip(reports,['imgui_cpp','imgui_python']):
        assert r['contract']=='p06q.roi-export.ui.002' and r['state']=='observed' and r['context']==context and r['error']==''
        assert 0 < len(r['frames']) <= 550 and r['worker_waits']
        for wait in r['worker_waits']:
            assert wait['poll_milliseconds'] == 20 and wait['deadline_seconds'] == 10
            assert type(wait['polls']) is int and wait['polls'] >= 0
            assert wait['polls'] * .020 - 1e-6 <= wait['elapsed_seconds'] <= 10
        assert r['loaded']['source_index']==r['loaded']['annotation_index']==r['loaded']['metadata_choice']==-1
        assert len(r['loaded']['sources'])==2 and [s['name'] for s in r['loaded']['sources']]==['duplicate##.roi']*2
        assert 'Choose both' in r['unselected']['error'] and not r['unselected']['ready']
        assert 'Confirm' in r['unconfirmed']['error']
        assert 'Choose fresh' in r['metadata_required']['error'] and 'Group' in r['invalid_group']['error']
        assert 'source type' in r['sidecar_refusal']['error'] and not r['sidecar_refusal']['ready']
        assert r['review']['ready'] and r['review']['projection_group']==9
        assert r['fresh_review']['ready'] and r['fresh_review']['projection_group']==0 and r['fresh_review']['metadata_choice']==0
        assert (r['review']['source_index'],r['review']['annotation_index'])==(0,1)
        assert r['after_browse_cancel']['ready'] and r['after_browse_cancel']['path'].endswith('.roi')
        xz=next(p for p in r['preview_render'] if p['plane']=='XZ')
        assert xz['import_preview']['active'] and len(xz['import_preview']['layers'])==1
        assert xz['import_preview']['layers'][0]['geometry']['points']==[[7.250000000001,.500000000001]]
        assert all(f['events']==f['rendered']['input_frame']['raw_inputs'] for f in r['frames'])
        assert all(c['control']['id'] and c['control']['enabled'] for c in r['controls'])
        assert {'source.0','annotation.1','metadata.2','export new file','confirm overwrite','Cancel Browse'}<={c['name'] for c in r['controls']}


def test_export_wire_original_provenance_native_precision_and_projection(reports):
    for r in reports:
        wire=Path(r['output_file']).read_bytes();assert wire==Path(r['existing_file']).read_bytes()
        mapped=ij.decode_imagej_roi_native_envelope(wire);row=mapped.annotation
        assert row.name=='edited λ😀' and (row.plane,row.coordinate,row.t,row.c)==('xz',4,2,1)
        assert row.selection.points==[[7.250000000001,.500000000001]]
        assert row.selection.attributes=={'future-selection':'native'} and row.attributes=={'opaque':'kept'}
        assert mapped.source_record.group==7 and mapped.source_record.properties=='source: λ😀\n'
        outer=ij.decode_imagej_roi(wire);assert outer.group==9
        payload=base64.b64decode(outer.properties.split(': ',1)[1]);offset=4
        def blob():
            nonlocal offset
            n=int.from_bytes(payload[offset:offset+4],'big');offset+=4
            value=payload[offset:offset+n];offset+=n;return value
        original=blob();offset+=24;native=json.loads(blob());projection=blob();assert offset==len(payload)
        assert native['rows'][0]['selection']['points']==row.selection.points
        assert ij.decode_imagej_roi(projection).properties=='project: λ😀\n'
        source=ij.decode_imagej_roi_envelope(original)
        archive=r['states']['before']['metadata']['imagejxx.imagej_roi_import_sources']
        assert ij.encode_imagej_roi(source.source_record)==base64.b64decode(archive['entries'][0]['data'])


def test_atomic_export_refusals_cancel_stale_and_unchanged_document(reports):
    for r in reports:
        assert 'File exists' in r['overwrite_refusal']['error']
        assert r['overwrite_success']['message'].startswith('Exported ')
        assert not r['ready_canceled']['ready'] and 'canceled' in r['ready_canceled']['error']
        assert r['destination_race']['error'] and r['destination_race']['ready']
        assert 'raced.roi' in r['destination_race']['error'] and '😀' in r['destination_race']['error']
        assert not r['stale']['visible'] and r['stale']['cancel_reason']=='source_changed'
        states=r['states']
        assert states['before']==states['review']
        for name,count in [('exported',1),('overwritten',2)]:
            assert states[name]['metadata']==states['before']['metadata'] and states[name]['undo']==3
            assert len(states[name]['workflow'])==count
            for record in states[name]['workflow']:
                ij.validate_imagej_roi_export_record(record)
                assert base64.b64decode(record['output'])==Path(r['output_file']).read_bytes()
        assert states['before']['undo']==3 and states['before']['workflow']==[]
        assert states['stale']['undo']==4 and states['stale']['metadata']==states['before']['metadata']
        assert states['stale']['workflow']==r['workflow_before_cancel']
        assert r['source_data_shared_after_exports']
        # The viewer reads live pixels. Pair preparation/file workers acquire
        # none, measured while the UI frame pump is held at their boundary.
        assert {v['stage'] for v in r['worker_read_checks']}=={'prepare','stage-file'}
        assert any(v['stage']=='prepare' and v['phase']==2 for v in r['worker_read_checks'])
        assert any(v['stage']=='stage-file' and v['phase']==3 for v in r['worker_read_checks'])
        assert all(v['before_plane']==v['after_plane'] and v['before_region']==v['after_region'] for v in r['worker_read_checks'])
        parent=Path(r['output_file']).parent
        assert (parent/'raced.roi').read_bytes()==b'race sentinel'
        assert not (parent/'canceled.roi').exists() and not (parent/'stale.roi').exists()
        assert r['temporary_files']==[] and not list(parent.glob('*.tmp.*'))


def test_captured_export_review_rebind_and_publication_reservation(reports):
    for r in reports:
        loaded=r['captured_loaded']
        assert loaded['workflow_review'] and not loaded['destination_confirmed']
        assert loaded['source_index']==loaded['annotation_index']==loaded['metadata_choice']==-1
        assert loaded['path']=='' and not loaded['overwrite'] and not loaded['ready']
        assert 'Confirm the reviewed pairing' in r['captured_destination_refusal']['error']
        assert r['captured_review']['ready'] and r['captured_review']['metadata_choice']==2
        assert Path(r['replay_file']).read_bytes()==Path(r['output_file']).read_bytes()
        states=r['states'];original=r['captured_original']
        assert len(states['replayed']['workflow'])==3 and states['replayed']['workflow'][0]==original
        rebound=states['rebound']['workflow'][-1];ij.validate_imagej_roi_export_record(rebound)
        assert (rebound['pairing']['source_index'],rebound['pairing']['annotation_index'])==(1,0)
        assert rebound['pairing']['source']['document_id']!=original['pairing']['source']['document_id']
        mapped=ij.decode_imagej_roi_native_envelope(Path(r['rebound_file']).read_bytes())
        assert mapped.source_record.group==8 and mapped.annotation.selection.kind=='rectangle'
        xy=next(p for p in r['rebound_preview_render'] if p['plane']=='XY')
        assert xy['coordinate']==0 and len(xy['import_preview']['layers'])==1
        assert 'Workflow must be an array' in r['workflow_refusal']['error']
        assert states['recording_off']==states['rebound'] and len(states['rebound']['workflow'])==4
        assert Path(r['recording_off_file']).read_bytes()==Path(r['rebound_file']).read_bytes()
        for refusal in ['invalid_original','invalid_retry']:
            assert r[refusal]['error'] and not r[refusal]['ready'] and r[refusal]['sources']==[]
        assert r['captured_unchanged']
