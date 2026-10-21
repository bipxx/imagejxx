"""Independent assertions over actual ROI Browse/Unicode ImGui input."""
import base64
import json
import os
from pathlib import Path
import pytest
import imagejxx as ij
from imagejxx import _desktop

@pytest.fixture(scope='module')
def reports(tmp_path_factory):
    cpp=json.loads(Path(os.environ['IMAGEJXX_ROI_BROWSE_CPP_REPORT']).read_text(encoding='utf-8'))
    directory=tmp_path_factory.mktemp('roi-browse-ui')/'capture001'
    editor=_desktop.Editor(str(directory.parent/'preferences.json'))
    python=json.loads(editor._roi_import_browse_ui_case(str(directory)))
    assert python==json.loads((directory/'case.json').read_text(encoding='utf-8'))
    return [cpp,python]

def rows(state):return state['metadata'].get('imagejxx.planar_annotations',{}).get('rows',[])
def sources(state):return state['metadata'].get('imagejxx.imagej_roi_import_sources')

def test_actual_browser_controls_type_filter_and_literal_hash_names(reports):
    for r,context in zip(reports,['imgui_cpp','imgui_python']):
        assert r['context']==context and r['contract']=='p06q.roi-import-browse.ui.001'
        assert r['state']=='observed' and r['error']=='' and not r['acceptance_claim']
        assert all(f['events']==f['rendered']['input_frame']['raw_inputs'] for f in r['frames'])
        assert all(c['control']['id'] and c['control']['enabled'] for c in r['controls'])
        listing=r['listing'];assert listing['open'] and listing['extensions']==['.roi','.zip']
        assert '.roi' in listing['prompt'] and '.zip' in listing['prompt']
        entries={e['name']:e for e in listing['entries']}
        assert set(entries)=={'child λ','shape##λ.ROi','archive 图.zip','left###same.roi','right###same.roi'}
        assert entries['child λ']['directory'] and not entries['shape##λ.ROi']['directory']
        assert entries['shape##λ.ROi']['label']=='shape##λ.ROi'
        assert entries['left###same.roi']['label']=='left###same.roi'
        assert len({e['control']['id'] for e in entries.values()})==len(entries)
        after={e['name']:e for e in r['listing_after_add']['entries']}
        assert set(after)==set(entries)|{'aaa-added.roi'}
        assert all(after[name]['control']['id']==e['control']['id'] for name,e in entries.items())
        assert {e['name'] for e in r['filtered']['entries']}=={'child λ','shape##λ.ROi'}
        assert {'choose Unicode ROI row','Open Unicode ROI','choose Unicode ZIP double click','Cancel Browse','Up'} <= {c['name'] for c in r['controls']}

def test_unicode_file_choice_and_cancel_preserve_prepared_draft(reports):
    for r in reports:
        assert r['chosen_roi']['path']==r['paths']['roi']
        assert not r['chosen_roi']['ready'] and r['chosen_roi']['entries']==[]
        before=r['before_browse_cancel'];after=r['after_browse_cancel']
        for key in ['path','entries','ready','review','job_id','source_document_id','job_cancel_requested']:
            assert before[key]==after[key]
        assert before['ready'] and not before['job_cancel_requested']
        assert before['review'][0]['name']=='单个 λ'
        assert r['chosen_zip']['path']==r['paths']['zip'] and not r['chosen_zip']['ready']
        assert r['chosen_zip']['entries']==[] and r['chosen_zip']['job_cancel_requested']
        for name in ['before','chosen_roi','browse_canceled','chosen_zip']:
            s=r['states'][name];assert rows(s)==[] and sources(s) is None and s['undo']==0 and s['workflow']==[]

def test_unicode_archive_import_exact_sources_workflow_and_pixels(reports):
    for r in reports:
        s=r['states']['applied'];data=rows(s)
        assert len(data)==2 and [v['name'] for v in data]==['单个 λ','second']
        assert [v['plane'] for v in data]==['XY','XY'] and [v['coordinate'] for v in data]==[0,0]
        assert s['undo']==1 and len(s['workflow'])==1
        ij.validate_imagej_roi_import_record(s['workflow'][0])
        archive=sources(s);assert archive==s['workflow'][0]['after_sources']
        assert [e['name'] for e in archive['entries']]==['路径😀.roi','second.roi']
        wire=Path(r['paths']['roi']).read_bytes()
        assert base64.b64decode(archive['entries'][0]['data'])==wire
        decoded=ij.decode_imagej_roi(wire);assert decoded.group==7 and decoded.properties=='source: λ😀\n'
        second=ij.decode_imagej_roi(base64.b64decode(archive['entries'][1]['data']))
        assert second.group==11 and second.roi.selection.kind=='point'
        assert all(e['mapping']['extent']==[1,1,1,9,13,1] for e in archive['entries'])
        expected=[10*y+x for y in range(9) for x in range(13)]
        for name,snapshot in r['states'].items():
            assert snapshot['pixels']==([100+v for v in expected] if name=='other' else expected)

def test_navigation_unsupported_file_and_generic_browser_compatibility(reports):
    for r in reports:
        assert r['child_listing']['path']==r['paths']['child']
        assert [e['name'] for e in r['child_listing']['entries']]==['nested.roi']
        assert r['parent_listing']['path']==r['paths']['inputs']
        assert r['unsupported']['path']==r['paths']['ignored']
        assert not r['unsupported']['controls']['submit']['enabled']
        assert not any(e['name']=='ignore.png' for e in r['unsupported']['entries'])
        assert rows(r['states']['unsupported'])==rows(r['states']['applied'])
        generic=r['generic_browser'];assert generic['open'] and generic['extensions']==[] and not generic['expired']
        assert 'ignore.png' in {e['name'] for e in generic['entries']}
        assert r['states']['final']==r['states']['metadata_changed']

def test_source_revision_and_draft_generation_expire_browser_without_retargeting(reports):
    for r in reports:
        for name in ['owner_changed','metadata_changed','draft_reopened']:
            observation=r[name]
            assert observation['browser']['expired'] and not observation['browser']['open']
            assert observation['input']['popup_stack']==[]
        assert not r['owner_changed']['panel']['visible'] and not r['metadata_changed']['panel']['visible']
        assert rows(r['states']['other'])==[] and sources(r['states']['other']) is None and r['states']['other']['undo']==0
        new=r['draft_reopened']['panel'];assert new['visible'] and new['path']=='' and new['entries']==[] and not new['ready']
        applied=r['states']['applied'];changed=r['states']['metadata_changed']
        assert rows(changed)==rows(applied) and sources(changed)==sources(applied)
        assert changed['title']=='concurrent Browse title' and changed['undo']==2
        assert changed['workflow']==applied['workflow']

def test_unicode_import_undo_redo_retains_exact_archive(reports):
    for r in reports:
        assert rows(r['states']['undone'])==[] and sources(r['states']['undone']) is None
        assert rows(r['states']['redone'])==rows(r['states']['applied'])
        assert sources(r['states']['redone'])==sources(r['states']['applied'])
        assert r['states']['redone']['workflow']==r['states']['applied']['workflow']
