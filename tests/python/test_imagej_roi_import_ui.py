"""Independent expectations over actual shared-ImGui physical input cases."""
import base64
import json
import os
from pathlib import Path
import pytest
import imagejxx as ij
from imagejxx import _desktop

@pytest.fixture(scope='module')
def reports(tmp_path_factory):
    cpp=Path(os.environ['IMAGEJXX_ROI_IMPORT_CPP_REPORT'])
    native=json.loads(cpp.read_text(encoding='utf-8'))
    directory=tmp_path_factory.mktemp('roi-import-ui')/'capture001'
    editor=_desktop.Editor(str(directory.parent/'preferences.json'))
    python=json.loads(editor._roi_import_ui_case(str(directory)))
    assert json.loads((directory/'case.json').read_text(encoding='utf-8'))==python
    return [native,python]

def rows(state):return state['metadata'].get('imagejxx.planar_annotations',{}).get('rows',[])
def sources(state):return state['metadata'].get('imagejxx.imagej_roi_import_sources')

def test_real_input_and_complete_review_without_publication(reports):
    for report,context in zip(reports,['imgui_cpp','imgui_python']):
        assert report['context']==context and report['state']=='observed' and report['error']==''
        assert report['contract']=='p06q.roi-import.ui.002'
        assert report['unmapped']['error']=='Choose a plane for every standard ROI'
        assert not report['unmapped']['ready'] and not report['unmapped']['controls']['apply']['enabled']
        assert 'Scientific Z cannot' in report['z_policy_refusal']['error']
        assert report['review']['ready'] and report['review']['error']==''
        assert [r['plane'] for r in report['review']['review']]==[1,2]
        assert [r['coordinate'] for r in report['review']['review']]==[1,4]
        assert not report['invalidated']['ready'] and not report['invalidated']['controls']['apply']['enabled']
        for key in ['before','unmapped','z_policy_refusal','review']:
            assert report['states'][key]['undo']==0 and rows(report['states'][key])==[]
            assert sources(report['states'][key]) is None and report['states'][key]['workflow']==[]
        names=[r['name'] for r in report['controls']]
        for key in ['menu.file.import_rois','path','load','plane.2','z_policy.1','validate','apply','cancel replacement','map_all']:
            assert key in names
        assert all(c['control']['enabled'] and c['control']['id'] for c in report['controls'])
        for frame in report['frames']:
            assert frame['events']==frame['rendered']['input_frame']['raw_inputs']
        assert any(f['rendered']['roi_import']['worker_active'] for f in report['frames'])

def test_native_geometry_positions_provenance_pixels_and_workflow(reports):
    expected=[[10000*t+1000*c+100*z+10*y+x for y in range(9) for x in range(13)]
              for t in range(2) for c in range(2) for z in range(6)]
    for report in reports:
        state=report['states']['applied'];data=rows(state)
        assert state['shape']==[2,2,6,9,13,1] and state['pixels']==expected
        assert len(data)==2 and state['undo']==1 and len(state['workflow'])==1
        assert state['selection'] is None and state['legacy_roi_count']==0
        assert [(r['name'],r['plane'],r['coordinate'],r['t'],r['c']) for r in data]==[
            ('bare','XZ',1,0,1),('native','YZ',4,None,None)]
        assert data[1]['attributes']=={'opaque':'kept'}
        archive=sources(state);assert [r['name'] for r in archive['entries']]==['bare.roi','native.roi']
        first,second=archive['entries'];assert first['mapping']['plane']=='xz' and first['mapping']['z_position']==1
        assert first['mapping']['coordinate'] is None and second['mapping'] is None
        record=ij.decode_imagej_roi(base64.b64decode(first['data']));assert record.group==7 and record.properties=='source: λ\n'
        native=ij.decode_imagej_roi_envelope(base64.b64decode(second['data']))
        assert native.annotation.selection.points==[[1.250000000001,1.500000000001],[3.750000000001,3.500000000001]]
        assert native.source_record.group==7 and native.source_record.properties=='source: λ\n'
        ij.validate_imagej_roi_import_record(state['workflow'][0])
        assert state['workflow'][0]['after_sources']==archive
        assert report['applied']['cancel_reason']=='accepted' and not report['applied']['visible']
        for other in report['states'].values():assert other['pixels']==expected

def test_cancel_stale_invalid_last_member_and_session_roundtrip(reports):
    for report in reports:
        applied=report['states']['applied']
        assert rows(report['states']['undone'])==[] and sources(report['states']['undone']) is None
        assert rows(report['states']['redone'])==rows(applied) and sources(report['states']['redone'])==sources(applied)
        assert report['replacement_review']['ready'] and report['replacement_review']['mode']==1
        assert rows(report['states']['canceled'])==rows(applied) and sources(report['states']['canceled'])==sources(applied)
        assert report['states']['canceled']['workflow']==applied['workflow']
        assert report['stale']['cancel_reason']=='source_changed' and not report['stale']['visible']
        assert report['states']['stale']['title']=='concurrent'
        assert not report['bad_member']['ready'] and report['bad_member']['error']
        for key in ['stale','bad_member','final','reopened']:
            assert rows(report['states'][key])==rows(applied)
            assert sources(report['states'][key])==sources(applied)
            assert report['states'][key]['workflow']==applied['workflow']
        assert not report['final_panel']['worker_active'] and not report['final_panel']['visible']

def test_matching_host_semantics(reports):
    cpp,python=reports
    for key in cpp['states']:
        for field in ['shape','pixels','metadata','selection','legacy_roi_count']:
            assert cpp['states'][key][field]==python['states'][key][field]

def test_imported_annotations_reach_the_correct_rendered_planes(reports):
    for report in reports:
        views=report['applied_render']
        assert [(v['plane'],v['coordinate'],v['width'],v['height']) for v in views]==[
            ('XY',2,13,9),('XZ',1,13,6),('YZ',4,9,6)]
        layers=views[0]['planar_annotations']['layers']
        assert [(layer['plane'],layer['coordinate'],layer['row']) for layer in layers]==[('XZ',1,0),('YZ',4,1)]
        assert all(not layer['preview'] and layer['canvas_id'] for layer in layers)
        assert layers[0]['canvas_id']!=layers[1]['canvas_id']
        assert layers[0]['geometry']['points']==[[1.25,1.5],[3.75,3.5]]
        assert layers[1]['geometry']['points']==[[1.250000000001,1.500000000001],[3.750000000001,3.500000000001]]
        assert any(frame['rendered']['vertices']>0 and frame['rendered']['indices']>0 for frame in report['frames'])

def pending(views):return [layer for view in views for layer in view['import_preview']['layers']]

def test_graphical_preview_without_publication_and_cache_reuse(reports):
    for report in reports:
        assert report['compact_review']['image_review'] and report['compact_review']['ready']
        layers=pending(report['compact_render'])
        assert [(r['plane'],r['coordinate'],r['row']) for r in layers]==[('XZ',1,0),('YZ',4,1)]
        assert all(r['preview'] and r['canvas_id'] for r in layers)
        assert layers[0]['geometry']['points']==[[1.25,1.5],[3.75,3.5]]
        assert layers[1]['geometry']['points']==[[1.250000000001,1.500000000001],[3.750000000001,3.500000000001]]
        assert pending(report['hidden_render'])==[]
        assert [(r['plane'],r['coordinate']) for r in pending(report['shown_render'])]==[('XZ',1),('YZ',4)]
        before=pending(report['idle_preview_before']);after=pending(report['idle_preview_after'])
        assert [r['screen_builds'] for r in before]==[r['screen_builds'] for r in after]
        assert pending(report['invalidated_render'])==[] and pending(report['stale_render'])==[]
        assert rows(report['states']['review'])==[] and report['states']['review']['workflow']==[]
        baseline=[p['builds'] for p in report['before_render']]
        for name in ['compact_render','hidden_render','shown_render','invalidated_render','applied_render']:
            assert [p['builds'] for p in report[name]]==baseline

def test_replacement_preview_comparison_restores_committed_layers(reports):
    for report in reports:
        replacement=report['replacement_render']
        assert all(v['import_preview']['replace'] for v in replacement)
        assert replacement[0]['planar_annotations']['layers']==[]
        assert [(l['plane'],l['coordinate']) for l in pending(replacement)]==[('XY',2)]
        assert report['channel_filtered_panel']['ready'] and report['slice_filtered_panel']['ready']
        assert pending(report['channel_filtered_render'])==[] and pending(report['slice_filtered_render'])==[]
        assert [(l['plane'],l['coordinate']) for l in pending(report['navigation_restored_render'])]==[('XY',2)]
        old=report['replacement_hidden_render']
        assert pending(old)==[] and not any(v['import_preview']['replace'] for v in old)
        assert [(l['plane'],l['coordinate']) for l in old[0]['planar_annotations']['layers']]==[('XZ',1),('YZ',4)]
        shown=report['replacement_shown_render']
        assert [(l['plane'],l['coordinate']) for l in pending(shown)]==[('XY',2)]
        assert shown[0]['planar_annotations']['layers']==[]
        canceled=report['canceled_render']
        assert pending(canceled)==[]
        assert [(l['plane'],l['coordinate']) for l in canceled[0]['planar_annotations']['layers']]==[('XZ',1),('YZ',4)]
        assert sources(report['states']['canceled'])==sources(report['states']['applied'])

def test_ordinary_xy_preview_committed_rendering_and_legacy_preservation(reports):
    for report in reports:
        ordinary=report['ordinary'];states=ordinary['states']
        review=ordinary['review_render']
        assert review['visible'] and review['import_preview']['active']
        staged=review['import_preview']['layers']
        assert len(staged)==1 and (staged[0]['plane'],staged[0]['coordinate'])==('XY',0)
        assert staged[0]['geometry']['points']==[[1.25,1.5],[3.75,3.5]]
        assert ordinary['hidden_render']['import_preview']['layers']==[]
        assert rows(states['review'])==[] and sources(states['review']) is None and states['review']['undo']==0
        applied=ordinary['applied_render']
        assert not applied['import_preview']['active'] and applied['import_preview']['layers']==[]
        live=applied['native_planar_annotations']['layers']
        assert len(live)==1 and (live[0]['plane'],live[0]['coordinate'])==('XY',0)
        assert live[0]['geometry']==staged[0]['geometry']
        assert len(rows(states['applied']))==1 and states['applied']['undo']==1
        assert len(states['applied']['workflow'])==2
        assert rows(states['undone'])==[] and sources(states['undone']) is None
        assert ordinary['undone_render']['native_planar_annotations']['layers']==[]
        assert rows(states['redone'])==rows(states['applied'])
        for state in states.values():
            assert state['shape']==[1,1,1,9,13,1]
            assert state['pixels']==[[10*y+x for y in range(9) for x in range(13)]]
            assert state['legacy_roi_count']==1 and state['selection'] is None
        assert not ordinary['final_panel']['visible'] and not ordinary['final_panel']['worker_active']
