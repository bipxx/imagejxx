"""Independent assertions over real shared ImGui workflow review input."""
import base64
import json
import os
from pathlib import Path
import pytest
import imagejxx as ij
from imagejxx import _desktop

@pytest.fixture(scope='module')
def reports(tmp_path_factory):
    cpp=json.loads(Path(os.environ['IMAGEJXX_ROI_REBIND_CPP_REPORT']).read_text(encoding='utf-8'))
    directory=tmp_path_factory.mktemp('roi-rebind-ui')/'capture001'
    editor=_desktop.Editor(str(directory.parent/'preferences.json'))
    python=json.loads(editor._roi_import_rebind_ui_case(str(directory)))
    assert python==json.loads((directory/'case.json').read_text(encoding='utf-8'))
    return [cpp,python]

def rows(state):return state['metadata'].get('imagejxx.planar_annotations',{}).get('rows',[])
def sources(state):return state['metadata'].get('imagejxx.imagej_roi_import_sources')

def test_workflow_widgets_confirmation_and_complete_original_validation(reports):
    for report,context in zip(reports,['imgui_cpp','imgui_python']):
        assert report['contract']=='p06q.roi-import-rebind.ui.001'
        assert report['context']==context and report['state']=='observed' and report['error']==''
        assert report['loaded']['workflow_rebind'] and not report['loaded']['destination_confirmed']
        assert len(report['loaded']['entries'])==2
        assert 'Confirm the reviewed destination' in report['unconfirmed']['error']
        assert not report['unconfirmed']['ready'] and not report['unconfirmed']['controls']['apply']['enabled']
        assert report['review']['ready'] and report['review']['destination_confirmed']
        assert [r['plane'] for r in report['review']['review']]==[1,2]
        assert report['invalid_original']['error'] and not report['invalid_original']['ready']
        assert report['invalid_original']['entries']==[]
        assert not report['invalid_original']['controls']['apply']['enabled']
        assert all(f['events']==f['rendered']['input_frame']['raw_inputs'] for f in report['frames'])
        assert all(c['control']['id'] and c['control']['enabled'] for c in report['controls'])
        assert {'workflow review','confirm destination','plane.2','z_policy.1','apply'} <= {c['name'] for c in report['controls']}
        assert len(report['frames'])<300 and report['source_unchanged'] and report['source_history']==1

def test_atomic_rebound_publication_preserves_original_and_transport(reports):
    for report in reports:
        states=report['states'];applied=states['applied'];original=report['original']
        assert applied['undo']==1 and len(rows(applied))==2 and len(applied['workflow'])==2
        assert applied['workflow'][0]==original
        new=applied['workflow'][1];ij.validate_imagej_roi_import_record(new)
        assert new['source']['document_id']==applied['id']!=original['source']['document_id']
        assert [(r['name'],r['plane'],r['coordinate']) for r in rows(applied)]==[('bare','XZ',1),('native','YZ',4)]
        assert rows(applied)[1]['attributes']=={'opaque':'kept'}
        assert new['after_sources']==sources(applied)
        old_entries=original['after_sources']['entries'];entries=sources(applied)['entries']
        assert [e['data'] for e in entries]==[e['data'] for e in old_entries]
        assert entries[0]['mapping']['plane']=='xz' and entries[1]['mapping'] is None
        record=ij.decode_imagej_roi(base64.b64decode(entries[0]['data']))
        assert record.group==7 and record.properties=='source: λ\n'
        for name in ['before','unconfirmed','review']:assert states[name]['undo']==0 and rows(states[name])==[]
        assert report['append_review']['ready'] and len(report['append_review']['review'])==2
        for name in ['canceled','invalid_original']:
            assert rows(states[name])==rows(applied) and sources(states[name])==sources(applied) and states[name]['undo']==1
        assert report['destination_history']==1 and report['destination_metadata']==applied['metadata']
        expected=[[20000+10000*t+1000*c+100*z+10*y+x for y in range(9) for x in range(13)]
                  for t in range(2) for c in range(2) for z in range(6)]
        for name in ['before','unconfirmed','review','applied','canceled','invalid_original']:assert states[name]['pixels']==expected

def test_envelope_extent_refusal_and_explicit_bare_extent_rebind(reports):
    for report in reports:
        states=report['states'];before=states['different_before'];refused=states['extent_refusal'];applied=states['different_applied']
        assert report['extent_refusal']['error'] and not report['extent_refusal']['ready']
        assert rows(before)==rows(refused)==[] and sources(refused) is None and refused['undo']==0
        assert report['bare_review']['ready'] and applied['undo']==1 and len(rows(applied))==1
        assert applied['workflow'][0]==report['bare_original'] and len(applied['workflow'])==2
        ij.validate_imagej_roi_import_record(applied['workflow'][1])
        entry=sources(applied)['entries'][0]
        assert entry['mapping']['extent']==[2,2,5,8,12,1]
        assert entry['data']==report['bare_original']['after_sources']['entries'][-1]['data']
        assert [(r['plane'],r['coordinate'],r['t'],r['c']) for r in rows(applied)]==[('XY',1,0,1)]
        assert before['pixels']==refused['pixels']==applied['pixels']
