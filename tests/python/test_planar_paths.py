"""V3 planar path geometry, complete command equations and physical Z remaps."""
import copy
import json
import numpy as np
import pytest
import imagejxx as ij
from test_z_subset import fixture

KEY='imagejxx.planar_annotations'
COMMANDS=[0,-4,-3,1,8,-3,1,8,8,1,-4,8,4,0,2,2,1,5,2,1,5,5,1,2,5,4,
          0,10,-2,2,12,7,14,-2,4,0,-2,10,3,0,16,5,4,7,10,4]
STYLE={'opaque':[True,2**64-1,'retained']}


def path(commands=COMMANDS):
    return ij.SelectionEditor(ij.Selection.from_path(ij.ImageJCompositePath(commands)),(5,7)).properties(2.5,STYLE)


def row(plane='xz',area=None,coordinate=0):
    return ij.PlanarAnnotation('curves',path() if area is None else area,plane,coordinate,1,1,{'row':{'kept':True}})


def attach(owner,annotation):
    command=ij.resolve_planar_annotation_command(owner.snapshot(),ij.PlanarAnnotationRequest('store',annotation=annotation))
    ij.validate_planar_annotation_command_record(command.record())
    owner.commit(ij.prepare_planar_annotation(ij.planar_annotation_command_from_record(owner.snapshot(),command.record())))
    return command


def mapped_commands(commands,first,step):
    result=list(commands);at=0
    while at<len(result):
        op=int(result[at]);at+=1;n={0:2,1:2,2:4,3:6,4:0}[op]
        for i in range(1,n,2):result[at+i]=result[at+i]/step+.5-(first+.5)/step
        at+=n
    return result


def control_points(commands):
    at=0
    while at<len(commands):
        op=int(commands[at]);at+=1;n={0:2,1:2,2:4,3:6,4:0}[op]
        for i in range(0,n,2):yield commands[at+i:at+i+2]
        at+=n


def opcodes(commands):
    at=0;result=[]
    while at<len(commands):
        op=commands[at];result.append(op);at+=1+{0:2,1:2,2:4,3:6,4:0}[op]
    return result


@pytest.mark.parametrize('plane',['xy','xz','yz'])
@pytest.mark.parametrize('operation',['store','update','delete'])
def test_v3_equations_and_full_path_carriers(plane,operation):
    owner,_,_,_,reads=fixture()
    if operation!='store':attach(owner,row(plane))
    original=owner.snapshot();area=ij.SelectionEditor(path(),(5,7)).moved([.25,-.75])
    request=ij.PlanarAnnotationRequest(operation,None if operation=='store' else 0,None if operation=='delete' else row(plane,area))
    command=ij.resolve_planar_annotation_command(original,request);wire=json.loads(json.dumps(command.record()))
    assert wire['version']==3
    assert wire['before']['version']==(1 if operation=='store' else 3)
    assert wire['after']['version']==(1 if operation=='delete' else 3)
    ij.validate_planar_annotation_command_record(wire)
    replay=ij.planar_annotation_command_from_record(original,wire)
    owner.commit(ij.prepare_planar_annotation(replay))
    rows=ij.planar_annotations(owner.snapshot());assert len(rows)==(0 if operation=='delete' else 1)
    if rows:assert rows[0].selection==area and rows[0].attributes==row().attributes
    assert owner.snapshot().selection==original.selection and reads.counts().plane_reads==reads.counts().region_reads==0
    owner.undo();assert owner.snapshot().shares_data(original)
    owner.redo();assert len(ij.planar_annotations(owner.snapshot()))==len(rows)


@pytest.mark.parametrize('plane',['xy','xz','yz'])
@pytest.mark.parametrize('first',[0,1])
@pytest.mark.parametrize('step',[1,2,3])
@pytest.mark.parametrize('destination',[ij.OutputDestination.IN_PLACE,ij.OutputDestination.NEW_DOCUMENT])
def test_z_subset_maps_every_curve_control_and_world_landmark(plane,first,step,destination):
    owner,app,pixels,_,reads=fixture();annotation=row(plane,coordinate=3 if plane=='xy' else 0)
    attach(owner,annotation);before=owner.snapshot();history=owner.undo_steps
    cmd=ij.resolve_z_subset_command(before,ij.ZSubsetRequest(first,4,step,destination))
    prepared=ij.prepare_z_subset(ij.z_subset_command_from_record(before,cmd.record()))
    assert owner.snapshot().shares_data(before) and reads.counts().plane_reads==reads.counts().region_reads==0
    published=app.publish(prepared).document;rows=ij.planar_annotations(published.snapshot())
    retained=list(range(first,5,step));keep=plane!='xy' or 3 in retained
    assert len(rows)==int(keep)
    if keep:
        mapped=rows[0]
        expected=COMMANDS if plane=='xy' else mapped_commands(COMMANDS,first,step)
        actual=mapped.selection.path.commands
        assert len(actual)==len(expected)
        assert opcodes(actual)==opcodes(COMMANDS)
        if plane=='xy':assert actual==expected
        else:assert actual==pytest.approx(expected,rel=1e-14,abs=1e-12)
        assert mapped.selection.attributes==STYLE and mapped.selection.width==2.5 and mapped.attributes==annotation.attributes
        if plane=='xy':assert mapped.coordinate==retained.index(3)
        for source_point,target_point in zip(control_points(COMMANDS),control_points(expected)):
            assert ij.planar_annotation_world(before.dataset.calibration,annotation,source_point)==pytest.approx(
                ij.planar_annotation_world(published.snapshot().dataset.calibration,mapped,target_point),abs=1e-9)
    else:assert KEY not in published.snapshot().metadata['extensions']
    assert published.snapshot().selection==before.selection and published.snapshot().previous_selection==before.previous_selection
    # Native extension transport does not change the ordinary session frame solely for a metadata extension.
    blob=ij.encode_session(ij.SessionCapture([published.snapshot()]));assert blob[:10]==b'IMAGEJXX3\n'
    restored=ij.decode_session(blob).documents[0].snapshot();restored_rows=ij.planar_annotations(restored)
    assert len(restored_rows)==len(rows)
    if rows:assert restored_rows[0].selection==rows[0].selection
    np.testing.assert_array_equal(restored.dataset.numpy('TCZYX'),pixels[:,:,retained])
    identity=first==0 and step==1
    if destination==ij.OutputDestination.IN_PLACE:
        assert published.undo_steps==history+(not identity)
        if not identity:published.undo();assert published.snapshot().shares_data(before)
    else:assert owner.snapshot().shares_data(before)


@pytest.mark.parametrize('legacy',['rectangle','region'])
def test_v3_mixed_sets_and_downgrade_after_last_path_removed(legacy):
    owner,_,_,_,_=fixture()
    area=(ij.Selection('rectangle',[(1,1),(3,3)]) if legacy=='rectangle' else
          ij.SelectionRegion(ij.Selection.from_mask(np.ones((3,3),np.uint8)),(3,3),[-2,-2]).as_selection())
    attach(owner,row(area=area));expected=1 if legacy=='rectangle' else 2
    assert owner.snapshot().metadata['extensions'][KEY]['version']==expected
    attach(owner,row('yz'));assert owner.snapshot().metadata['extensions'][KEY]['version']==3
    cmd=ij.resolve_planar_annotation_command(owner.snapshot(),ij.PlanarAnnotationRequest('delete',1))
    assert cmd.record()['version']==3 and cmd.record()['after']['version']==expected
    ij.validate_planar_annotation_command_record(cmd.record());owner.commit(ij.prepare_planar_annotation(cmd))
    assert owner.snapshot().metadata['extensions'][KEY]['version']==expected


@pytest.mark.parametrize('location',['version','annotation','before','after'])
@pytest.mark.parametrize('version',[1,2])
def test_historical_command_and_set_versions_remain_closed(location,version):
    owner,_,_,_,_=fixture();attach(owner,row())
    wire=ij.resolve_planar_annotation_command(owner.snapshot(),ij.PlanarAnnotationRequest('update',0,row())).record()
    if location=='version':wire['version']=version
    else:wire[location]['version']=version
    with pytest.raises((ValueError,RuntimeError)):ij.validate_planar_annotation_command_record(wire)


@pytest.mark.parametrize('fault',['missing','extra','points','opcode','truncated','bool','range','empty','attributes','equation'])
def test_malformed_path_or_equation_is_rejected_before_rebind(fault):
    owner,_,_,_,reads=fixture();before=owner.snapshot()
    wire=ij.resolve_planar_annotation_command(before,ij.PlanarAnnotationRequest('store',annotation=row())).record()
    at=wire['after']['rows'][0]['selection']
    if fault=='missing':del at['path']
    elif fault=='extra':at['mask']=''
    elif fault=='points':at['points']=[[0,0]]
    elif fault=='opcode':at['path'][0]=5
    elif fault=='truncated':at['path']=[0,1,1,2,3]
    elif fault=='bool':at['path'][1]=True
    elif fault=='range':at['path'][1]=1e9+1
    elif fault=='empty':at['path']=[]
    elif fault=='attributes':at['attributes']['path']=[]
    else:at['path'][1]+=.25
    for call in [lambda:ij.validate_planar_annotation_command_record(wire),
                 lambda:ij.planar_annotation_command_from_record(before,wire),
                 lambda:ij.rebind_planar_annotation_command(before,wire,ij.PlanarAnnotationRequest('store',annotation=row()))]:
        with pytest.raises((ValueError,RuntimeError)):call()
    assert owner.snapshot().shares_data(before) and reads.counts().plane_reads==0


def test_version_only_upgrade_and_extent_change_refuse():
    owner,_,_,_,_=fixture();attach(owner,row(area=ij.Selection('rectangle',[(1,1),(3,3)])))
    value=owner.snapshot().metadata['extensions'][KEY];value['version']=3
    edit=owner.begin_edit();edit.set_extension(KEY,value)
    with pytest.raises(ValueError):owner.commit(edit)
    value=ij.resolve_planar_annotation_command(owner.snapshot(),ij.PlanarAnnotationRequest('store',annotation=row())).record()['after']
    value['extent'][2]-=1;edit=owner.begin_edit();edit.set_extension(KEY,value)
    with pytest.raises(ValueError):owner.commit(edit)


def test_mapped_domain_is_admitted_before_callbacks():
    owner,_,_,_,reads=fixture();attach(owner,row(area=path([0,0,-1e9,2,2,-1e9,4,-1e9,4])))
    before=owner.snapshot();called=[]
    command=ij.resolve_z_subset_command(before,ij.ZSubsetRequest(1,4,1))
    with pytest.raises(ValueError,match='coordinate range'):
        ij.prepare_z_subset(command,checkpoint=lambda:called.append(True))
    assert not called and owner.snapshot().shares_data(before) and reads.counts().plane_reads==0


def test_desktop_refuses_planar_paths_before_publication(tmp_path):
    from imagejxx.ui.app import Editor
    from imagejxx.ui.model import Document
    from imagejxx.ui.persistence import Preferences
    editor=Editor(preferences=Preferences(tmp_path/'preferences.json'))
    editor.add_document(Document(np.zeros((5,5,7),np.uint8),'legacy',is_stack=True))
    live=editor.live_document();before=live.native_snapshot();info=live.info
    annotation=ij.PlanarAnnotation('curves',path(),'xz',0,0,0)
    command=ij.resolve_planar_annotation_command(before,ij.PlanarAnnotationRequest('store',annotation=annotation))
    edit=ij.prepare_planar_annotation(command)
    with pytest.raises(ValueError,match='planar path'):live.commit(edit)
    assert edit.active and live.native_snapshot().shares_data(before) and live.info==info
