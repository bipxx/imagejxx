"""Affine display intent, closed versions, physical alignment and actual draws."""
import copy
import json
import os
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij
from test_z_subset import fixture
from test_planar_display_annotations import text_payload,image_payload

KEY='imagejxx.planar_display_annotations'
IDENTITY=[1,0,0,0,1,0]
MATRICES=[IDENTITY,[1,.25,2,.5,1,-2],[-1,.5,3,0,2,1]]


def annotation(kind='text',plane='xz',transform=IDENTITY,coordinate=0):
    payload=text_payload() if kind=='text' else image_payload()
    return ij.PlanarDisplayAnnotation('retained '+kind,payload,plane,coordinate,0,None,
        attributes={'opaque':[True,None,2**64-1]},transform=transform)


def store(owner,row):
    command=ij.resolve_planar_display_annotation_command(owner.snapshot(),ij.PlanarDisplayAnnotationRequest('store',annotation=row))
    ij.validate_planar_display_annotation_command_record(command.record())
    owner.commit(ij.prepare_planar_display_annotation(ij.planar_display_annotation_command_from_record(owner.snapshot(),command.record())))
    return command


@pytest.mark.parametrize('kind',['text','image'])
@pytest.mark.parametrize('plane',['xy','xz','yz'])
@pytest.mark.parametrize('matrix',MATRICES)
@pytest.mark.parametrize('step',[1,2,3])
@pytest.mark.parametrize('destination',[ij.OutputDestination.IN_PLACE,ij.OutputDestination.NEW_DOCUMENT])
def test_full_payload_preserving_subset_alignment(kind,plane,matrix,step,destination):
    owner,app,pixels,_,reads=fixture();row=annotation(kind,plane,matrix,1 if plane=='xy' else 0)
    store(owner,row);before=owner.snapshot();history=owner.undo_steps
    command=ij.resolve_z_subset_command(before,ij.ZSubsetRequest(1,4,step,destination))
    prepared=ij.prepare_z_subset(ij.z_subset_command_from_record(before,command.record()))
    assert owner.snapshot().shares_data(before) and reads.counts().plane_reads==reads.counts().region_reads==0
    output=app.publish(prepared).document;saved=ij.planar_display_annotations(output.snapshot())[0]
    expected=list(matrix)
    if plane!='xy':expected[3:]=[matrix[3]/step,matrix[4]/step,matrix[5]/step+.5-1.5/step]
    assert saved.transform==pytest.approx(expected,abs=1e-15)
    assert saved.attributes==row.attributes and saved.t==0 and saved.c is None
    assert saved.payload.bounds==row.payload.bounds
    if kind=='text':
        for key in ['text','font_name','font_size','font_style','justification','angle','draw_string_mode','color','background']:
            assert getattr(saved.payload,key)==getattr(row.payload,key)
    else:
        for key in ['width','height','channels','pixels','opacity','zero_transparent']:
            assert getattr(saved.payload,key)==getattr(row.payload,key)
    # Every payload point, including off-volume points, keeps its physical position.
    for local in [(-3,-4),(1.25,2.75),(8,12)]:
        a=row.map_point(local);b=saved.map_point(local)
        old_voxel=([a[0]-.5,a[1]-.5,1] if plane=='xy' else
                   [a[0]-.5,0,a[1]-.5] if plane=='xz' else [0,a[0]-.5,a[1]-.5])
        new_voxel=([b[0]-.5,b[1]-.5,0] if plane=='xy' else
                   [b[0]-.5,0,b[1]-.5] if plane=='xz' else [0,b[0]-.5,b[1]-.5])
        assert output.snapshot().dataset.calibration.voxel_to_world(new_voxel)==pytest.approx(before.dataset.calibration.voxel_to_world(old_voxel))
    np.testing.assert_array_equal(output.snapshot().dataset.numpy('TCZYX'),pixels[:,:,1::step])
    restored=ij.decode_session(ij.encode_session(ij.SessionCapture([output.snapshot()]))).documents[0]
    assert ij.planar_display_annotations(restored.snapshot())[0].transform==saved.transform
    if destination==ij.OutputDestination.IN_PLACE:
        assert output.undo_steps==history+1
        output.undo();assert output.snapshot().shares_data(before)
        output.redo();assert ij.planar_display_annotations(output.snapshot())[0].transform==saved.transform
    else:assert owner.snapshot().shares_data(before) and output.undo_steps==0


@pytest.mark.parametrize('matrix',MATRICES)
@pytest.mark.parametrize('point',[(-3,-7),(0,0),(1.5,2.25),(4,9)])
def test_public_forward_inverse_and_detached_coefficients(matrix,point):
    row=annotation(transform=matrix)
    expected=[matrix[0]*point[0]+matrix[1]*point[1]+matrix[2],matrix[3]*point[0]+matrix[4]*point[1]+matrix[5]]
    assert row.map_point(point)==expected and row.unmap_point(expected)==pytest.approx(point)
    values=row.transform;values[0]=19;assert row.transform==matrix
    with pytest.raises(AttributeError):row.transform=matrix


@pytest.mark.parametrize('matrix',[[1,2,3,2,4,5],[1,0,0,0,0,0],[float('nan'),0,0,0,1,0],
    [1,0,0,0,float('inf'),0],[1e10,0,0,0,1,0],[True,0,0,0,1,0],['1',0,0,0,1,0],[1,0]])
def test_invalid_affine_refused_before_publication(matrix):
    with pytest.raises((ValueError,TypeError,RuntimeError)):annotation(transform=matrix)


@pytest.mark.parametrize('kind',['text','image'])
def test_repeated_subsets_compose_without_rewriting_payload(kind):
    owner,_,_,_,_=fixture();row=annotation(kind,transform=MATRICES[1]);store(owner,row)
    first=ij.prepare_z_subset(ij.resolve_z_subset_command(owner.snapshot(),ij.ZSubsetRequest(1,4,2)))
    app=ij.Application();app.adopt(owner);middle=app.publish(first).document
    second=ij.prepare_z_subset(ij.resolve_z_subset_command(middle.snapshot(),ij.ZSubsetRequest(0,1,2)))
    direct=ij.prepare_z_subset(ij.resolve_z_subset_command(owner.snapshot(),ij.ZSubsetRequest(1,4,4)))
    a=second.preview.metadata['extensions'][KEY];b=direct.preview.metadata['extensions'][KEY]
    assert a==b and a['rows'][0]['payload']==owner.snapshot().metadata['extensions'][KEY]['rows'][0]['payload']


def test_v2_update_delete_and_v1_version_closure():
    owner,_,_,_,_=fixture();store(owner,annotation(transform=MATRICES[1]))
    replace=ij.resolve_planar_display_annotation_command(owner.snapshot(),ij.PlanarDisplayAnnotationRequest('update',0,annotation()))
    assert replace.record()['version']==2 and replace.record()['after']['version']==1
    owner.commit(ij.prepare_planar_display_annotation(replace))
    legacy=ij.resolve_planar_display_annotation_command(owner.snapshot(),ij.PlanarDisplayAnnotationRequest('delete',0))
    assert legacy.record()['version']==1
    for version in [2,True,3]:
        forged=copy.deepcopy(legacy.record());forged['version']=version
        with pytest.raises(ValueError):ij.validate_planar_display_annotation_command_record(forged)
    store(owner,annotation(transform=MATRICES[2]));delete=ij.resolve_planar_display_annotation_command(owner.snapshot(),ij.PlanarDisplayAnnotationRequest('delete',1))
    assert delete.record()['version']==2 and delete.record()['after']['version']==1
    ij.validate_planar_display_annotation_command_record(delete.record())


@pytest.mark.parametrize('bad',['version','missing','unknown','singular','bool','length','identity_v2'])
def test_closed_affine_extension_claims_refuse_atomically(bad):
    owner,_,_,_,_=fixture();store(owner,annotation(transform=MATRICES[1]));before=owner.snapshot()
    record=copy.deepcopy(before.metadata['extensions'][KEY]);r=record['rows'][0]
    if bad=='version':record['version']=1
    elif bad=='missing':del r['transform']
    elif bad=='unknown':r['scale']=1
    elif bad=='singular':r['transform']=[1,2,3,2,4,5]
    elif bad=='bool':r['transform'][0]=True
    elif bad=='length':r['transform'].append(0)
    else:r['transform']=IDENTITY
    edit=owner.begin_edit();edit.set_extension(KEY,record)
    with pytest.raises((ValueError,RuntimeError)):owner.commit(edit)
    assert owner.snapshot().shares_data(before)


@pytest.mark.parametrize('kind',['u8','u16','rgb','quiet','ieee'])
def test_sample_policies_source_words_and_identity_are_preserved(kind):
    from test_creation_workflow import source
    owner,reads,pixels=source(kind);store(owner,annotation('image'))
    before=owner.snapshot();history=owner.undo_steps;app=ij.Application();app.adopt(owner)
    identity=ij.prepare_z_subset(ij.resolve_z_subset_command(before,ij.ZSubsetRequest(0,1,1,ij.OutputDestination.IN_PLACE)))
    app.publish(identity);assert owner.snapshot().shares_data(before) and owner.undo_steps==history
    command=ij.resolve_z_subset_command(before,ij.ZSubsetRequest(0,1,2))
    ij.validate_z_subset_command_record(command.record())
    result=ij.prepare_z_subset(ij.z_subset_command_from_record(before,json.loads(json.dumps(command.record()))))
    assert reads.counts().plane_reads==reads.counts().region_reads==0
    assert result.preview.dataset.numpy('TCZYXS').tobytes()==pixels[:,:,::2].tobytes()


@pytest.mark.parametrize('kind',['text','image'])
def test_affine_imagej_export_refuses_instead_of_dropping_transform(kind):
    from test_imagej_display_roi import source,projection,EXTENT
    old,_=source(kind);a=old.annotation
    row=ij.PlanarDisplayAnnotation(a.name,a.payload,a.plane,a.coordinate,a.t,a.c,attributes=a.attributes,transform=[1,0,0,0,.5,-.25])
    mapped=ij.rebind_imagej_display_roi_mapping(old,EXTENT,row)
    with pytest.raises(ValueError,match='Affine display annotations'):
        ij.encode_imagej_display_roi_envelope(mapped,projection(mapped))


def layers(panes):return {v['row']:v for p in panes for v in p['display_annotations']}


@pytest.mark.parametrize('context',['imgui_cpp','imgui_python'])
def test_actual_subset_numeric_transform_and_observed_draw_vertices(context,tmp_path):
    if context=='imgui_cpp':r=json.loads(Path(os.environ['IMAGEJXX_DISPLAY_REMAP_CPP_REPORT']).read_bytes())
    else:
        from imagejxx import _desktop
        editor=_desktop.Editor(str(tmp_path/'prefs.json'))
        r=json.loads(editor._display_z_subset_ui_case(str(tmp_path/'capture')))
    assert r['contract']=='p12u.display-remap-ui.001' and r['state']=='observed' and not r['error']
    before=r['states']['before']['display_annotations'];assert before['version']==1 and len(before['rows'])==4
    for phase in ['applied','redone','rebound','reopened','display_restored']:
        current=r['states'][phase]['display_annotations'];assert current['version']==2 and len(current['rows'])==3
        assert current['rows'][0]['transform']==current['rows'][1]['transform']==[1,0,0,0,.5,-.25]
        assert current['rows'][2]['transform']==IDENTITY and current['rows'][2]['coordinate']==1
        for i in range(3):assert current['rows'][i]['payload']==before['rows'][i]['payload']
    assert r['states']['undone']['display_annotations']==before
    assert r['states']['display_changed']['display_annotations']['rows'][0]['transform'][5]==-.5
    for record in r['states']['display_restored']['workflow'][2:]:ij.validate_planar_display_annotation_command_record(record)
    source=layers(r['before_layers']);output=layers(r['applied_layers'])
    for i in [0,1]:
        a,b=source[i],output[i];assert a['error']==b['error']=='' and len(a['screen_vertices'])==len(b['screen_vertices'])==4
        for av,bv in zip(a['screen_vertices'],b['screen_vertices']):
            x,y=[(av['position'][k]-a['canvas_origin'][k])/a['canvas_scale'][k] for k in [0,1]]
            expected=[b['canvas_origin'][0]+b['canvas_scale'][0]*x,b['canvas_origin'][1]+b['canvas_scale'][1]*(.5*y-.25)]
            assert bv['position']==pytest.approx(expected,abs=.002)
            assert bv['uv']==av['uv'] and bv['color']==av['color']
    assert output[1]['rgba_prefix']==source[1]['rgba_prefix']
    from test_display_annotations_ui import png_rgb
    rgb=png_rgb(r['screenshot']).astype(np.int16)
    assert rgb.shape==(820,1250,3)
    red=(rgb[:,:,0]-rgb[:,:,1]>40)&(rgb[:,:,0]-rgb[:,:,2]>40)
    assert np.count_nonzero(red)>100

    assert {'display.transform.section','display.transform.5','display.apply','undo','redo','rebind.apply'}<={v['name'] for v in r['controls']}
