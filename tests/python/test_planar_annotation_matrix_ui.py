"""Independent expectations for actual shared ImGui planar gestures."""
import base64,json,math,os
from pathlib import Path
import pytest

OPS=("rectangle","ellipse","line","point","freehand","polygon","multipoint","escape","focus","context","source","wheel","zoom","preferences","resize","vertex","rotate","mask_move","mask_hole")
OPS+=('busy','scientific_busy','modal','time','channel','owner','style','mask_rotate')+tuple(f'resize_{h}_{m}' for h in range(8) for m in ('none','shift','alt','both'))
OPS+=tuple(f'cross_{h}_{m}' for h in range(8) for m in ('none','shift','alt','both'))
OPS+=tuple('collapse_'+m for m in ('none','shift','alt','both'))
OPS+=tuple('invalid_'+name for name in ('width_zero','width_negative','width_nan','width_inf','width_overflow','width_partial','height_zero','center_nan','angle_inf','fixed_negative','time_zero','channel_over'))
OPS+=tuple(f'profile_{profile}_cross_{h}_{m}' for profile in ('ellipse','rbox','rellipse') for h in range(8) for m in ('none','shift','alt','both'))
OPS+=tuple('profile_ellipse_collapse_'+m for m in ('none','shift','alt','both'))
CASES=[plane+"."+op for plane in ("XY","XZ","YZ") for op in OPS]
KEY="imagejxx.planar_annotations"
@pytest.mark.parametrize("case",CASES)
@pytest.mark.parametrize("context",["imgui_cpp","imgui_python"])
def test_planar_input_matrix(case,context,tmp_path):
    if context=="imgui_cpp":
        report=json.loads((Path(os.environ["IMAGEJXX_PLANAR_MATRIX_CPP_ROOT"])/case/"case.json").read_bytes())
    else:
        from imagejxx import _desktop
        editor=_desktop.Editor(str(tmp_path/"preferences.json"))
        report=json.loads(editor._planar_annotation_matrix_ui_case(case,str(tmp_path/"capture")))
    assert report["case"]==case and report["context"]==context and report["contract"]=="p06p.planar-matrix.ui.001"
    assert not report["error"] and not report["acceptance_claim"]
    plane,op=case.split(".");view=("XY","XZ","YZ").index(plane)
    profile=None
    if op.startswith('profile_'):
        _,profile,op=op.split('_',2)
        assert report['profile']==profile
    states=report["states"];before=states["before"];after=states["after"]
    for f in report["frames"]:assert f["events"]==f["rendered"]["input_frame"]["raw_inputs"]
    for state in states.values():
        assert state["pixels"]==before["pixels"] and state["selection"] is None
        expected_tc=(1,2) if op=='scientific_busy' else (0,0) if op=='busy' else (0,1) if op=='time' and state is after else (1,0) if op=='channel' and state is after else (1,1)
        if op in ('time','channel') and state is states['closed']:expected_tc=(0,1) if op=='time' else (1,0)
        assert (state['t'],state['c'])==expected_tc
        assert state["revisions"]["pixels"]==before["revisions"]["pixels"]
    assert not report["after_panel"]["active"]
    if profile and op.startswith('cross_'):
        _,h,modifier=op.split('_');h=int(h)
        assert states['during']==before and report['collapsed_state']==before
        assert report['collapsed_panel']['active'] and report['draft']['active']
        assert not report['collapsed_panel']['error'] and not report['after_panel']['error']
        assert after['undo']==1 and len(after['workflow'])==1 and states['closed']==after
        row=after['metadata'][KEY]['rows'][0];selection=row['selection']
        assert (row['plane'],row['coordinate'],row['t'],row['c'])==(plane,(2,3,4)[view],1,1)
        assert row['attributes']=={'tag':'row'} and selection['attributes']['opaque']=='retained'
        angle=0 if profile=='ellipse' else math.pi/6;c,s=math.cos(angle),math.sin(angle)
        def local(point):
            x,y=point[0]-6,point[1]-4
            return [c*x+s*y,-s*x+c*y]
        frame=report['frames'][report['end_frame']]['rendered'];canvas=frame['orthogonal_planes'][view]
        mouse=frame['input_frame']['mouse_position']
        end=local([(mouse[i]-canvas['origin'][i])/canvas['scale'][i] for i in range(2)])
        if profile=='ellipse':
            assert selection['kind']=='ellipse'
            low,high=map(local,selection['points'])
            center=[(a+b)/2 for a,b in zip(low,high)];size=[b-a for a,b in zip(low,high)]
        else:
            assert selection['kind']==('rotated_rectangle' if profile=='rbox' else 'rotated_ellipse')
            center=local(selection['points'][0]);p,u,v=selection['points']
            du=[u[i]-p[i] for i in range(2)];dv=[v[i]-p[i] for i in range(2)]
            size=[2*math.hypot(*du),2*math.hypot(*dv)]
            assert du==pytest.approx([c*size[0]/2,s*size[0]/2])
            assert dv==pytest.approx([-s*size[1]/2,c*size[1]/2])
        assert all(v>0 for v in size)
        directions=((-1,-1),(0,-1),(1,-1),(1,0),(1,1),(0,1),(-1,1),(-1,0))[h]
        centered=modifier in ('alt','both');aspect=modifier in ('shift','both');requested=[]
        for axis,(direction,original) in enumerate(zip(directions,(6,4))):
            anchor=0 if centered or not direction else -direction*original/2
            requested.append(original if not direction else abs(end[axis]-anchor)*(2 if centered else 1))
            if centered or not direction:assert center[axis]==pytest.approx(0,abs=1e-12)
            else:
                assert min(abs(center[axis]-size[axis]/2-anchor),abs(center[axis]+size[axis]/2-anchor))<1e-10
                assert (center[axis]-anchor)*(end[axis]-anchor)>0
        if aspect:
            scale=max(requested[i]/(6,4)[i] for i,d in enumerate(directions) if d)
            assert size==pytest.approx([6*scale,4*scale])
        else:assert size==pytest.approx(requested)
        assert [p['builds'] for p in report['after_render']]==[p['builds'] for p in report['before_render']]
        return
    if op.startswith('collapse_'):
        assert report['draft']['active'] and report['draft']['input']['phase']=='collapsed'
        assert not report['draft']['error'] and not report['after_panel']['error']
        assert states['during']==before and after==before and states['closed']==before
        assert not any(layer['preview'] for layer in report['draft']['layers'])
        assert [p['builds'] for p in report['after_render']]==[p['builds'] for p in report['before_render']]
        return
    if op.startswith('invalid_'):
        assert states['during']==before and report['refused']==before and report['repaired']==before
        assert report['refusal_panel']['error'] and not report['refusal_panel']['active']
        assert not report['repair_panel']['error'] and not report['after_panel']['error']
        assert after['undo']==1 and len(after['workflow'])==1 and states['closed']==after
        row=after['metadata'][KEY]['rows'][0]
        assert (row['plane'],row['coordinate'],row['t'],row['c'])==(plane,(2,3,4)[view],1,1)
        assert row['selection']['points']==[[1,2],[7,4]]
        assert row['selection']['attributes']['opaque']=='retained' and row['attributes']=={'tag':'row'}
        assert [p['builds'] for p in report['after_render']]==[p['builds'] for p in report['before_render']]
        return
    if op not in ("polygon","multipoint"):
        assert states["during"]==before
    canceled=op in ("escape","focus","context","source","wheel","zoom","preferences","busy","scientific_busy","modal","time","channel","owner")
    if canceled:
        assert report["draft"]["active"]
        assert after["metadata"]==before["metadata"] and after["workflow"]==[]
        assert after["undo"]==(1 if op=="source" else 0)
        if op=="focus":assert report["focus_lost"]["app_focus_lost"]
        if op not in ("context","owner"):assert report["after_panel"]["last_cancel"]
        if op in ('busy','scientific_busy'):
            assert report['busy']['job_id']>0 and report['busy']['state']==1 and report['busy']['window_busy']
            assert not report['busy_panel']['active'] and not report['busy_after']
        if op=='scientific_busy':
            assert states['busy']==before and after==before
            assert report['worker']['source']==before['owner']
            assert report['worker']['operation']=='gaussian_blur_imagej'
            assert report['worker']['stage']=='ImageJ Gaussian source copy'
            assert 0<report['worker']['completed']<=report['worker']['total']
            target=report['scientific_target']['target']
            assert target['document_id']==before['owner'] and target['source_shape']==[2,3,9,13,17,1]
            assert target['scope']=='positions' and target['positions']==[[1,2,2],[0,1,7]]
            assert target['execution_mode']=='planes' and target['destination']=='in_place'
            assert target['roi_policy']=='whole_plane'
            for initial,final in zip(report['before_render'],report['after_render']):
                assert final['builds']==initial['builds']
                assert final['source_document_id']==initial['source_document_id']==before['owner']
            assert report['cancel_accepted'] and report['terminal']==dict(state=6,cancel_requested=True,reserved_bytes=0)
            assert report['recovery_draft']['active'] and not report['recovery_panel']['active']
            recovered=report['recovered']
            assert recovered['owner']==before['owner'] and recovered['pixels']==before['pixels']
            assert recovered['revisions']['pixels']==before['revisions']['pixels']
            assert recovered['undo']==1 and len(recovered['workflow'])==1 and states['closed']==recovered
            rows=recovered['metadata'][KEY]['rows'];assert len(rows)==1
            row=rows[0]
            assert (row['plane'],row['coordinate'],row['t'],row['c'])==(plane,(2,3,4)[view],1,2)
            assert row['selection']['kind']=='rectangle'
            expected=[]
            for key in ('recovery_start_frame','recovery_end_frame'):
                frame=report['frames'][report[key]]['rendered'];canvas=frame['orthogonal_planes'][view]
                mouse=frame['input_frame']['mouse_position']
                expected.append([(mouse[i]-canvas['origin'][i])/canvas['scale'][i] for i in range(2)])
            assert row['selection']['points']==[[2,2],[6,6]]
            # Rectangle creation snaps to integer cell boundaries. Delivered
            # screen positions may be fractional after canvas rasterization.
            for actual,wanted in zip(row['selection']['points'],expected):
                assert all(abs(a-b)<=.5 for a,b in zip(actual,wanted))
        if op=='modal':assert report['modal']['open']
        if op=='time':assert after['t']==0
        if op=='channel':assert after['c']==0
        return
    if op=="mask_hole":
        assert not report["draft"]["active"] and after==before
        return
    assert (report["draft"]["active"] or op in ('style','mask_rotate')) and after["undo"]==1 and len(after["workflow"])==1
    row=after["metadata"][KEY]["rows"][0]
    assert (row["plane"],row["coordinate"],row["t"],row["c"])==(plane,(2,3,4)[view],1,1)
    selection=row["selection"]
    def location(index):
        frame=report["frames"][index]["rendered"];p=frame["orthogonal_planes"][view];m=frame["input_frame"]["mouse_position"]
        return [(m[i]-p["origin"][i])/p["scale"][i] for i in range(2)]
    if op in ("rectangle","ellipse"):
        assert selection["kind"]==op and selection["points"]==[[1,1],[5,5]]
    elif op=="polygon":assert selection["points"]==[[1,1],[5,1],[3,5]] and selection["kind"]==op
    elif op=="multipoint":assert selection["points"]==[[1,1],[5,1]] and selection["kind"]==op
    elif op in ("line","point","freehand"):
        expected=[location(8)]
        if op=="line":expected.append(location(9))
        if op=="freehand":expected.extend(location(i) for i in (9,10,11))
        assert selection["kind"]==op
        assert len(selection["points"])==len(expected)
        for p,q in zip(selection["points"],expected):assert p==pytest.approx(q)
    elif op.startswith(('resize_','cross_')):
        _,h,modifier=op.split('_');h=int(h)
        assert selection['kind']=='rectangle'
        left,top=selection['points'][0];right,bottom=selection['points'][1]
        assert right>left and bottom>top
        end=location(report['end_frame']);direction=((-1,-1),(0,-1),(1,-1),(1,0),(1,1),(0,1),(-1,1),(-1,0))[h]
        centered=modifier in ('alt','both');aspect=modifier in ('shift','both')
        crossing=op.startswith('cross_')
        if crossing:
            assert report['collapsed_state']==before and report['collapsed_panel']['active']
            assert not report['collapsed_panel']['error'] and not report['after_panel']['error']
        # Geometric invariants: fixed opposite edges or center; aspect mode
        # chooses the smallest original-ratio box satisfying the moved handle.
        if centered:
            assert (left+right)/2==pytest.approx(6) and (top+bottom)/2==pytest.approx(4)
        else:
            if direction[0]<0:assert (left if crossing else right)==pytest.approx(9)
            elif direction[0]>0:assert (right if crossing else left)==pytest.approx(3)
            else:assert (left+right)/2==pytest.approx(6)
            if direction[1]<0:assert (top if crossing else bottom)==pytest.approx(6)
            elif direction[1]>0:assert (bottom if crossing else top)==pytest.approx(2)
            else:assert (top+bottom)/2==pytest.approx(4)
        extents=[]
        for axis,(original,center,low,high) in enumerate(((6,6,3,9),(4,4,2,6))):
            d=direction[axis]
            anchor=center if centered or not d else high if d<0 else low
            extents.append(original if not d else abs(end[axis]-anchor)*(2 if centered else 1))
        if aspect:
            active_scales=[extents[i]/(6,4)[i] for i,d in enumerate(direction) if d]
            assert (right-left)/(bottom-top)==pytest.approx(1.5)
            assert (right-left)/6==pytest.approx(max(active_scales))
        else:
            assert right-left==pytest.approx(extents[0]) and bottom-top==pytest.approx(extents[1])
    elif op=='style':
        assert row['name']=='Styled' and selection['points']==[[2,2],[6,4]]
        style=selection['attributes']['imagejxx.roi_style']
        assert style['stroke_color']==[17,34,51,255] and style['fill_color']==[85,102,119,128] and style['stroke_width']==0
    elif op=="resize":
        assert selection["kind"]=="rectangle" and selection["points"][0]==[1,1]
        assert selection["points"][1]==pytest.approx(location(9))
    elif op=="vertex":
        assert selection["kind"]=="polygon" and selection["points"][0]==[1,1] and selection["points"][2]==[3,5]
        assert selection["points"][1]==pytest.approx(location(9))
    elif op=="rotate":
        a,b=location(16),location(17)
        a=[a[0]-4,a[1]-3];b=[b[0]-4,b[1]-3]
        angle=math.atan2(a[0]*b[1]-a[1]*b[0],a[0]*b[0]+a[1]*b[1])
        assert selection["kind"]=="rotated_rectangle"
        expected=[[4,3],[4+2*math.cos(angle),3+2*math.sin(angle)],[4-math.sin(angle),3+math.cos(angle)]]
        for p,q in zip(selection["points"],expected):assert p==pytest.approx(q)
    elif op in ("mask_move","mask_rotate"):
        width,height=((17,13),(17,9),(13,9))[view]
        bits=[0]*(width*height)
        for y in range(2,7):
            for x in range(2,7):
                if (x,y)!=(4,4):
                    dx,dy=(x+1,y) if op=='mask_move' else (10-y,x-2)
                    bits[dy*width+dx]=1
        dx,dy=(10,2) if op=='mask_move' else (8,7)
        bits[dy*width+dx]=1
        packed=[0]*((width*height+7)//8)
        for i,bit in enumerate(bits):packed[i//8]|=bit<<(7-i%8)
        assert selection["kind"]=="mask"
        # Native selection codec encodes full-canvas, MSB-first packed bytes.
        assert selection["shape"]==[height,width]
        assert list(base64.b64decode(selection["mask"],validate=True))==packed
    if op in ("resize","vertex","rotate","mask_move","mask_rotate","style") or op.startswith("resize_"):
        assert row["attributes"]=={"tag":"row"} and selection["attributes"]["opaque"]=="retained"
    if op not in ("wheel","zoom"):
        assert [p["builds"] for p in report["after_render"]]==[p["builds"] for p in report["before_render"]]


def test_annotation_cap_refusal_recovers_without_losing_source(tmp_path):
    import imagejxx as ij
    from imagejxx import _desktop
    import numpy as np
    owner=ij.Document(ij.Dataset(np.zeros((257,257),dtype=np.uint8),axes="YX"))
    owner.commit(ij.prepare_store_planar_annotation(owner.snapshot(),ij.PlanarAnnotation("large canvas",ij.Selection("rectangle",[(1,1),(4,4)]),"xy",0)))
    view=owner.snapshot().display;view.orthogonal=True;owner.set_display(view)
    editor=_desktop.Editor(str(tmp_path/"preferences.json"));editor.add_native_document(owner)
    before=owner.snapshot();history=owner.undo_steps
    editor.update_settings(json.dumps({"processing_memory_mb":1}),False)
    rendered=json.loads(editor.render_headless(frames=3))
    assert not rendered["window_error"]
    assert "memory cap" in rendered["orthogonal_planes"][0]["planar_annotations"]["error"]
    assert rendered["orthogonal_planes"][0]["planar_annotations"]["cached_visuals"]==0
    assert owner.snapshot().shares_data(before) and owner.undo_steps==history
    editor.update_settings(json.dumps({"processing_memory_mb":16}),False)
    recovered=json.loads(editor.render_headless(frames=3))
    assert not recovered["window_error"] and not recovered["orthogonal_planes"][0]["planar_annotations"]["error"]
    assert owner.snapshot().shares_data(before) and owner.undo_steps==history
    assert recovered["orthogonal_planes"][0]["planar_annotations"]["cached_visuals"]==1
