"""Native path selection identity, continuous editing and closed V5 sessions."""
import copy
import json
import math
import struct
import zlib
import numpy as np
import pytest
import imagejxx as ij


def rect(x, y, right, bottom):
    return [0,x,y,1,right,y,1,right,bottom,1,x,bottom,4]


COMMANDS = rect(-4,-3,8,8)+rect(2,2,5,5)+[0,10,0,2,12,6,14,0,4]


def selection(commands=COMMANDS):
    return ij.Selection.from_path(ij.ImageJCompositePath(commands)).with_attributes({'opaque': [True, 2**64-1, 'retained']})


def owner():
    return ij.Document(ij.Dataset(np.arange(3*16*18,dtype=np.uint16).reshape(3,16,18),axes='ZYX'))


def session_limits(**values):
    result=ij.SessionLimits()
    for key,value in values.items():setattr(result,key,value)
    return result


def unpack(blob):
    raw=zlib.decompress(blob[18:])
    assert len(raw)==struct.unpack('<Q',blob[10:18])[0]
    return json.loads(raw)


def pack(root, version=5):
    raw=json.dumps(root,separators=(',',':'),allow_nan=False).encode()
    return f'IMAGEJXX{version}\n'.encode()+struct.pack('<Q',len(raw))+zlib.compress(raw)


def test_identity_storage_style_and_legacy_refusal():
    area=selection()
    assert area.kind=='path' and area.is_path and not area.is_region
    assert area.points==[] and area.path.commands==COMMANDS
    path=area.path
    detached=path.commands;detached[1]=100
    assert area.path.commands==COMMANDS
    with pytest.raises(ValueError,match='V5'):area.to_dict()
    with pytest.raises(ValueError,match='V5'):
        ij.Selection.from_dict(dict(kind='path',points=[],width=1,path=COMMANDS))
    assert area.with_attributes({'path': COMMANDS}).is_path
    with pytest.raises(ValueError):owner().select(area.with_attributes({'path': COMMANDS}))
    with pytest.raises(ValueError):owner().select(ij.Selection('path'))


@pytest.mark.parametrize('kind',['quadratic','cubic','s-cubic'])
@pytest.mark.parametrize('transform',[[1,0,0,0,1,0],[-1,.5,10,.25,1,-3],[0,-1,7,1,0,-2]])
def test_continuous_membership_against_polynomial(kind,transform):
    if kind=='quadratic':
        path=ij.ImageJCompositePath([0,0,0,2,2,4,4,0,4]);right=4
        height=lambda x:2*x-.5*x*x
    elif kind=='cubic':
        path=ij.ImageJCompositePath([0,0,0,3,1,3,2,3,3,0,4]);right=3
        height=lambda x:3*x-x*x
    else:
        path=ij.ImageJCompositePath([0,0,0,3,1,3,2,-3,3,0,4]);right=3
        height=lambda x:3*x-3*x*x+(2/3)*x*x*x
    moved=path.transformed(transform)
    editor=ij.SelectionEditor(ij.Selection.from_path(moved),(16,18))
    a=np.array(transform).reshape(2,3)
    for x in np.linspace(-.1,right+.1,19):
        for y in np.linspace(-1.13,3.17,23):
            expected=0<x<right and min(0,height(x))<y<max(0,height(x))
            point=(a[:,:2]@np.array([x,y])+a[:,2]).tolist()
            assert moved.contains(point)==expected
            # The editor uses the same continuous coordinates, including outside the canvas.
            hit=editor.hit_test(point,(1e-8,1e-8))
            assert (hit is not None)==expected


@pytest.mark.parametrize('delta',[(0,0),(.25,-.75),(40,-30),(-100,50)])
def test_translation_and_return_keep_complete_geometry(delta):
    original=selection();editor=ij.SelectionEditor(original,(16,18))
    moved=editor.moved(delta)
    assert moved.is_path and moved.attributes==original.attributes
    restored=ij.SelectionEditor(moved,(16,18)).moved([-v for v in delta])
    assert restored==original
    assert restored.path.commands==COMMANDS
    assert editor.bounds==pytest.approx([-4,-3,14,8])
    assert np.array_equal(restored.mask((16,18)),original.mask((16,18)))


@pytest.mark.parametrize('angle',[0,90,-90,180,270,360])
@pytest.mark.parametrize('pivot',[None,[0,0],[3,-2]])
def test_rotation_of_every_control_point(angle,pivot):
    original=selection();editor=ij.SelectionEditor(original,(16,18))
    moved=editor.rotated(angle,pivot)
    center=pivot or [5,2.5]
    radians=math.radians(angle);c=round(math.cos(radians));s=round(math.sin(radians))
    expected=[];at=0
    while at<len(COMMANDS):
        op=COMMANDS[at];at+=1;expected.append(op)
        n={0:2,1:2,2:4,3:6,4:0}[op]
        for i in range(0,n,2):
            x,y=COMMANDS[at+i]-center[0],COMMANDS[at+i+1]-center[1]
            expected.extend([center[0]+c*x-s*y,center[1]+s*x+c*y])
        at+=n
    assert moved.path.commands==expected
    assert moved.attributes==original.attributes
    assert ij.SelectionEditor(moved,(16,18)).rotated(-angle,pivot).path.commands==COMMANDS


def test_control_handles_and_vertex_edits_preserve_command_topology():
    original=selection([0,1,1,3,2,7,5,-2,8,1,4,0,11,3,2,12,6,14,3,4])
    editor=ij.SelectionEditor(original,(16,18));handles=editor.handles()
    assert len(handles)==7
    assert [h.index for h in handles]==list(range(7))
    for handle in handles:
        moved=editor.moved_vertex(handle.index,[handle.position[0]+.25,handle.position[1]-.5])
        before=original.path.commands;after=moved.path.commands
        assert len(before)==len(after) and sum(a!=b for a,b in zip(before,after))==2
        assert moved.is_path and moved.attributes==original.attributes
    for action in [lambda:editor.moved_vertex(7,[0,0]),lambda:editor.insertion_index([1,1]),
                   lambda:editor.inserted_vertex(0,[1,1]),lambda:editor.removed_vertex(0),
                   lambda:editor.resized(0,[1,1])]:
        with pytest.raises(ValueError):action()


@pytest.mark.parametrize('carrier',['active','previous','saved'])
def test_v5_native_roundtrip_all_selection_carriers(carrier):
    doc=owner();area=selection();edit=doc.begin_edit()
    if carrier=='active':edit.select(area)
    elif carrier=='previous':edit.set_previous_selection(area)
    else:edit.set_rois([ij.ROI('contours',area,z=1,attributes={'future':'row'})])
    doc.commit(edit);capture=ij.SessionCapture([doc.snapshot()])
    blob=ij.encode_session(capture)
    assert blob[:10]==b'IMAGEJXX5\n' and unpack(blob)['version']==5
    decoded=ij.decode_session(blob);assert decoded.source_version==5
    snapshot=decoded.documents[0].snapshot()
    result=snapshot.selection if carrier=='active' else snapshot.previous_selection if carrier=='previous' else snapshot.rois[0].selection
    assert result==area and result.path.commands==COMMANDS
    assert snapshot.dataset.numpy().tobytes()==doc.snapshot().dataset.numpy().tobytes()
    assert ij.encode_session(ij.SessionCapture([snapshot]))==blob
    if carrier=='saved':assert snapshot.rois[0].attributes=={'future':'row'}


@pytest.mark.parametrize('version',[3,4])
def test_historical_session_versions_remain_closed_to_paths(version):
    doc=owner();doc.select(selection());root=unpack(ij.encode_session(ij.SessionCapture([doc.snapshot()])))
    root['version']=version
    with pytest.raises(ValueError,match='version 5'):ij.decode_session(pack(root,version))


@pytest.mark.parametrize('mutation',['missing','extra','points','opcode','truncated','bool','version-only'])
def test_v5_structural_and_geometry_forgery_refuses(mutation):
    doc=owner();doc.select(selection());root=unpack(ij.encode_session(ij.SessionCapture([doc.snapshot()])))
    item=root['documents'][0]['selection']
    if mutation=='missing':del item['path']
    elif mutation=='extra':item['future']=1
    elif mutation=='points':item['points']=[[1,2]]
    elif mutation=='opcode':item['path'][0]=1
    elif mutation=='truncated':item['path']=[0,1]
    elif mutation=='bool':item['path'][1]=True
    else:root['documents'][0]['selection']=None
    with pytest.raises(ValueError):ij.decode_session(pack(root))


def test_document_history_retains_continuous_paths():
    doc=owner();original=selection();doc.select(original)
    moved=ij.SelectionEditor(original,(16,18)).moved([.25,-.75])
    edit=doc.begin_edit();edit.select(moved);edit.set_title('Path edit');doc.commit(edit)
    assert doc.snapshot().selection==moved and doc.snapshot().previous_selection==original
    doc.undo();assert doc.snapshot().selection==original
    doc.redo();assert doc.snapshot().selection==moved
    loaded=ij.decode_session(ij.encode_session(ij.SessionCapture([doc.snapshot()])))
    assert loaded.documents[0].snapshot().previous_selection==original


def test_session_estimate_admits_exact_path_json_budget():
    doc=owner();doc.select(selection());capture=ij.SessionCapture([doc.snapshot()])
    estimate=ij.estimate_session(capture)
    limits=session_limits(max_json_bytes=estimate.json_bytes)
    assert ij.decode_session(ij.encode_session(capture,limits),limits).source_version==5
    with pytest.raises((ValueError,OverflowError)):
        ij.encode_session(capture,session_limits(max_json_bytes=estimate.json_bytes-1))


def test_invalid_previous_path_rejected_before_staging():
    doc=owner();before=doc.snapshot();edit=doc.begin_edit()
    with pytest.raises(ValueError):edit.set_previous_selection(ij.Selection('path'))
    assert edit.active and doc.snapshot().previous_selection==before.previous_selection
    with pytest.raises(ValueError):
        edit.set_previous_selection(selection().with_attributes({'path':COMMANDS}))


def test_desktop_refuses_path_before_owner_or_history_changes(tmp_path):
    from imagejxx.ui.app import Editor
    from imagejxx.ui.model import Document
    from imagejxx.ui.persistence import Preferences
    editor=Editor(preferences=Preferences(tmp_path/'preferences.json'))
    editor.add_document(Document(np.zeros((16,18),np.uint8),'legacy'))
    live=editor.live_document();before=live.native_snapshot();info=live.info
    edit=live.begin_edit();edit.select(selection())
    with pytest.raises(ValueError,match='(?i)path'):live.commit(edit)
    assert edit.active and live.native_snapshot().selection==before.selection
    assert live.info==info


def test_native_v5_atomic_save_load_and_binary64_path_precision(tmp_path):
    commands=[0,-0.,0,2,1.0000000000000002,4.000000000000001,4,0,4]
    doc=owner();doc.select(selection(commands));capture=ij.SessionCapture([doc.snapshot()])
    path=tmp_path/'continuous.ijx';ij.save_session(str(path),capture)
    original=path.read_bytes();assert original[:10]==b'IMAGEJXX5\n'
    loaded=ij.load_session(str(path)).documents[0].snapshot().selection
    assert struct.pack('<9d',*loaded.path.commands)==struct.pack('<9d',*commands)
    with pytest.raises((ValueError,OverflowError)):
        ij.save_session(str(path),capture,session_limits(max_samples=1))
    assert path.read_bytes()==original and doc.snapshot().selection==capture.documents[0].selection


@pytest.mark.parametrize('kind',['u8','u16','float','quiet','ieee','rgb'])
def test_v5_keeps_all_typed_source_words_and_scientific_axes(kind):
    from test_region_storage import owner as typed_owner
    doc,reads,values=typed_owner(kind);doc.select(selection())
    ij.estimate_session(ij.SessionCapture([doc.snapshot()]))
    assert reads.counts().plane_reads==reads.counts().region_reads==0
    blob=ij.encode_session(ij.SessionCapture([doc.snapshot()]))
    loaded=ij.decode_session(blob).documents[0].snapshot()
    assert loaded.dataset.numpy('TCZYXS').tobytes()==values.tobytes()
    assert loaded.selection.path.commands==COMMANDS


def test_older_creation_command_refuses_path_base_before_capture():
    doc=owner();doc.select(selection());before=doc.snapshot()
    request=ij.SelectionCreationRequest(ij.Selection('rectangle',[(1,1),(3,3)]),ij.SelectionCreationOperation.REPLACE)
    with pytest.raises(ValueError,match='(?i)path'):
        ij.resolve_selection_creation_command(before,request)
    assert doc.snapshot().selection==before.selection and doc.undo_steps==0


def test_mixed_v5_capture_retains_regions_and_legacy_geometry():
    path_owner=owner();path_owner.select(selection())
    regional_owner=owner()
    region=ij.SelectionRegion(ij.Selection.from_mask(np.ones((3,4),np.uint8)),(3,4),[-3,-2]).as_selection()
    edit=regional_owner.begin_edit();edit.set_previous_selection(region);regional_owner.commit(edit)
    legacy_owner=owner();legacy=ij.Selection('rectangle',[(1,2),(5,7)]);legacy_owner.select(legacy)
    blob=ij.encode_session(ij.SessionCapture([d.snapshot() for d in [path_owner,regional_owner,legacy_owner]]))
    decoded=ij.decode_session(blob)
    assert decoded.source_version==5 and len(decoded.documents)==3
    assert decoded.documents[0].snapshot().selection.path.commands==COMMANDS
    assert decoded.documents[1].snapshot().previous_selection==region
    assert decoded.documents[2].snapshot().selection==legacy
