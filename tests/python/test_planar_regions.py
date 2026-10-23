"""Versioned intrinsic planar geometry and complete global Z lattice transport."""
import base64
import copy
import json
import os
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij
from test_z_subset import fixture

KEY='imagejxx.planar_annotations'
STYLE={'opaque':{'values':[True,None,2**64-1]},'label':'regional'}


def pattern(width=3,height=9,origin=(-2,-2)):
    y,x=np.indices((height,width));mask=(x==y%width).astype(np.uint8)
    area=ij.SelectionRegion(ij.Selection.from_mask(mask),mask.shape,origin).as_selection()
    return ij.SelectionEditor(area,(5,7)).properties(2.5,STYLE)


def row(plane='xz',area=None,coordinate=0):
    return ij.PlanarAnnotation('full '+plane,area or pattern(),plane,coordinate,0,None,{'row':['kept',2**64-1]})


def attach(owner,annotation):
    command=ij.resolve_planar_annotation_command(owner.snapshot(),ij.PlanarAnnotationRequest('store',annotation=annotation))
    assert command.record()['version']==2 and command.record()['after']['version']==2
    ij.validate_planar_annotation_command_record(command.record())
    replay=ij.planar_annotation_command_from_record(owner.snapshot(),json.loads(json.dumps(command.record())))
    owner.commit(ij.prepare_planar_annotation(replay))
    return command


def local(area):
    value=ij.SelectionRegion(area,(5,7));return value.local_selection.mask(value.shape)!=0


@pytest.mark.parametrize('plane',['xy','xz','yz'])
@pytest.mark.parametrize('destination',[ij.OutputDestination.IN_PLACE,ij.OutputDestination.NEW_DOCUMENT])
@pytest.mark.parametrize('origin',[(-2,-2),(3,0),(-7,5)])
def test_regional_planar_subset_full_frame_and_publication(plane,destination,origin):
    owner,app,pixels,_,reads=fixture();area=pattern(origin=origin)
    attach(owner,row(plane,area,3 if plane=='xy' else 0))
    before=owner.snapshot();history=owner.undo_steps
    command=ij.resolve_z_subset_command(before,ij.ZSubsetRequest(1,4,2,destination))
    ij.validate_z_subset_command_record(command.record())
    prepared=ij.prepare_z_subset(ij.z_subset_command_from_record(before,command.record()))
    assert owner.snapshot().shares_data(before)
    assert reads.counts().plane_reads==reads.counts().region_reads==0
    result=app.publish(prepared).document
    mapped=ij.planar_annotations(result.snapshot())[0]
    assert mapped.attributes==row(plane).attributes and mapped.selection.attributes==STYLE and mapped.selection.width==2.5
    if plane=='xy':
        assert mapped.coordinate==1 and mapped.selection==area
    else:
        ys=[y for y in range(origin[1],origin[1]+9) if (y-1)%2==0]
        assert mapped.selection.mask_origin==[origin[0],(ys[0]-1)//2]
        np.testing.assert_array_equal(local(mapped.selection),local(area)[[y-origin[1] for y in ys]])
        # Complete rows include cells outside the retained volume's Z extent.
        assert mapped.selection.to_dict()['shape']==[len(ys),3]
    np.testing.assert_array_equal(result.snapshot().dataset.numpy('TCZYX'),pixels[:,:,[1,3]])
    reopened=ij.decode_session(ij.encode_session(ij.SessionCapture([result.snapshot()]))).documents[0]
    assert ij.planar_annotations(reopened.snapshot())[0].selection==mapped.selection
    if destination==ij.OutputDestination.IN_PLACE:
        assert result.undo_steps==history+1
        result.undo();assert result.snapshot().shares_data(before)
        result.redo();assert ij.planar_annotations(result.snapshot())[0].selection==mapped.selection
    else:assert owner.snapshot().shares_data(before) and result.undo_steps==0


@pytest.mark.parametrize('origin',range(-5,6))
@pytest.mark.parametrize('step',[1,2,3,5])
def test_signed_lattice_rounding_and_empty_sampled_frame(origin,step):
    owner,_,_,_,_=fixture();area=pattern(3,1,[-2,origin]);attach(owner,row(area=area))
    prepared=ij.prepare_z_subset(ij.resolve_z_subset_command(owner.snapshot(),ij.ZSubsetRequest(1,4,step)))
    mapped=ij.planar_annotations(prepared.preview)[0].selection
    if (origin-1)%step==0:
        assert mapped.mask_origin==[-2,(origin-1)//step]
        np.testing.assert_array_equal(local(mapped),[[True,False,False]])
    else:
        assert mapped.mask_origin==[0,0] and mapped.to_dict()['shape']==[1,1] and not local(mapped).any()
    assert mapped.width==area.width and mapped.attributes==area.attributes


@pytest.mark.parametrize('plane',['xy','xz','yz'])
def test_v2_crud_replay_delete_and_legacy_v1_preserved(plane):
    owner,_,_,_,_=fixture();command=attach(owner,row(plane))
    original=owner.snapshot();area=pattern(origin=(-8,4))
    update=ij.resolve_planar_annotation_command(original,ij.PlanarAnnotationRequest('update',0,row(plane,area)))
    owner.commit(ij.prepare_planar_annotation(update));assert ij.planar_annotations(owner.snapshot())[0].selection==area
    owner.undo();assert owner.snapshot().shares_data(original)
    delete=ij.resolve_planar_annotation_command(owner.snapshot(),ij.PlanarAnnotationRequest('delete',0))
    record=delete.record();assert record['version']==2 and record['after']['version']==1
    ij.validate_planar_annotation_command_record(record)
    owner.commit(ij.prepare_planar_annotation(ij.planar_annotation_command_from_record(owner.snapshot(),record)))
    assert KEY not in owner.snapshot().metadata['extensions']
    legacy=ij.resolve_planar_annotation_command(owner.snapshot(),ij.PlanarAnnotationRequest('store',annotation=row(plane,ij.Selection('rectangle',[(0,0),(2,2)]))))
    assert legacy.record()['version']==legacy.record()['after']['version']==1


@pytest.mark.parametrize('plane',['xz','yz'])
def test_off_volume_lattice_landmarks_retain_world_coordinates(plane):
    owner,_,_,_,_=fixture();area=pattern();annotation=row(plane,area);attach(owner,annotation)
    source=owner.snapshot()
    prepared=ij.prepare_z_subset(ij.resolve_z_subset_command(source,ij.ZSubsetRequest(1,4,2)))
    mapped=ij.planar_annotations(prepared.preview)[0]
    # Retained lattice continues below and above the visible volume.
    for output_z in [-1,0,1,2]:
        source_z=1+2*output_z
        expected=ij.planar_annotation_world(source.dataset.calibration,annotation,[-1.5,source_z+.5])
        actual=ij.planar_annotation_world(prepared.preview.dataset.calibration,mapped,[-1.5,output_z+.5])
        assert actual==expected


def test_identity_subset_retains_exact_regional_extension_and_history():
    owner,app,_,_,reads=fixture();attach(owner,row());before=owner.snapshot();history=owner.undo_steps
    prepared=ij.prepare_z_subset(ij.resolve_z_subset_command(before,ij.ZSubsetRequest(0,4,1,ij.OutputDestination.IN_PLACE)))
    app.publish(prepared)
    assert owner.snapshot().shares_data(before) and owner.undo_steps==history
    assert reads.counts().plane_reads==reads.counts().region_reads==0


@pytest.mark.parametrize('bad',['downgrade','upgrade_without_region','bad_origin','tail','points','shape','unknown'])
def test_closed_versioned_region_records_refuse_atomically(bad):
    owner,_,_,_,_=fixture();attach(owner,row());before=owner.snapshot()
    record=copy.deepcopy(before.metadata['extensions'][KEY]);s=record['rows'][0]['selection']
    if bad=='downgrade':record['version']=1
    elif bad=='upgrade_without_region':s.clear();s.update(kind='rectangle',points=[[0,0],[1,1]],width=1,attributes={})
    elif bad=='bad_origin':s['origin'][0]=True
    elif bad=='tail':s['mask']=base64.b64encode(bytes([255]*4)).decode()
    elif bad=='points':s['points']=[[1,1]]
    elif bad=='shape':s['shape']=[0,3]
    else:s['future']=True
    edit=owner.begin_edit();edit.set_extension(KEY,record)
    with pytest.raises((ValueError,RuntimeError)):owner.commit(edit)
    assert owner.snapshot().shares_data(before)


@pytest.mark.parametrize('field',['version','before','annotation','after'])
def test_command_cannot_downgrade_nested_regional_claims(field):
    owner,_,_,_,_=fixture()
    cmd=ij.resolve_planar_annotation_command(owner.snapshot(),ij.PlanarAnnotationRequest('store',annotation=row()))
    value=copy.deepcopy(cmd.record())
    if field=='version':value['version']=1
    elif field=='before':value['before']['version']=2
    else:value[field]['version']=1
    with pytest.raises(ValueError):ij.validate_planar_annotation_command_record(value)


@pytest.mark.parametrize('context',['imgui_cpp','imgui_python'])
def test_actual_regional_subset_ui_apply_undo_rebind_reopen(context,tmp_path):
    if context=='imgui_cpp':report=json.loads(Path(os.environ['IMAGEJXX_PLANAR_REGION_CPP_REPORT']).read_bytes())
    else:
        from imagejxx import _desktop
        editor=_desktop.Editor(str(tmp_path/'prefs.json'))
        report=json.loads(editor._regional_z_subset_ui_case(str(tmp_path/'capture')))
    assert report['contract']=='p12t.planar-region-ui.001' and report['state']=='observed' and not report['error']
    states=report['states'];before=states['before']
    assert before['planar']['version']==2 and len(before['planar']['rows'])==4
    for phase in ['applied','redone','rebound','reopened']:
        value=states[phase];rows=value['planar']['rows'];assert len(rows)==3
        assert [(r['name'],r['coordinate']) for r in rows]==[('xz',2),('yz',3),('xy kept',1)]
        for i,(origin,shape,source_rows,width) in enumerate([([-2,-1],[4,3],[1,3,5,7],3),([-1,0],[3,4],[1,3,5],4)]):
            s=rows[i]['selection'];assert s['origin']==origin and s['shape']==shape
            bits=np.unpackbits(np.frombuffer(base64.b64decode(s['mask']),np.uint8))[:shape[0]*shape[1]].reshape(shape)
            expected=np.zeros(shape,np.uint8)
            for y,sy in enumerate(source_rows):expected[y,sy%width]=1
            np.testing.assert_array_equal(bits,expected)
            assert s['attributes']==before['planar']['rows'][i]['selection']['attributes'] and s['width']==2.5
        assert rows[2]['selection']==before['planar']['rows'][2]['selection']
        assert value['selection']==before['selection'] and value['previous_selection']==before['previous_selection']
    assert states['undone']['planar']==before['planar']
    assert {v['name'] for v in report['controls']} >= {'apply','undo','redo','workflow.rebind','rebind.apply'}
