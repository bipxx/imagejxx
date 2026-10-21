"""Global indexed datasets, palette metadata, paired publication and reconstruction."""
import copy
import json
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij

REFERENCE=json.loads((Path(__file__).parents[1]/'fixtures/indexed-color-imagej154r.json').read_bytes())


def fixture(row=None):
    row=row or REFERENCE['cases'][-1];h,w=row['shape'];pixels=np.array(row['input'],np.uint8).reshape(1,1,-1,h,w,3)
    reads=ij.DatasetReadObserver();cal=ij.SpatialCalibration(spacing=[2,3,5],origin=[10,20,30],unit='um',frame_interval=2,time_origin=7)
    owner=ij.Document(ij.Dataset(pixels,axes='TCZYXS',calibration=cal).observe_reads(reads));edit=owner.begin_edit();edit.set_title('original RGB');edit.set_property('opaque',{'kept':[True,None]});edit.set_extension('future',{'version':9});edit.select(ij.Selection('rectangle',[(0,0),(1,1)]));owner.commit(edit)
    view=owner.snapshot().display;view.crosshair=[1,0,pixels.shape[2]-1];view.zoom=2;view.orthogonal=True;view.properties={'lut':'Inverted','labels':True,'display_range':[0,255]};owner.set_display(view)
    owner=ij.Document(owner.snapshot());return owner,pixels,reads


@pytest.mark.parametrize('destination',[ij.OutputDestination.NEW_DOCUMENT,ij.OutputDestination.IN_PLACE])
@pytest.mark.parametrize('row',REFERENCE['cases'],ids=lambda r:f"{r['kind']}-{r['pattern']}-{r['max_colors']}")
def test_reference_dataset_palette_pixels_and_paired_history(row,destination):
    owner,pixels,reads=fixture(row);source=owner.snapshot();command=ij.resolve_indexed_dataset_command(source,max_colors=row['max_colors'],destination=destination);record=command.record()
    ij.validate_indexed_dataset_command_record(record);loaded=ij.indexed_dataset_command_from_record(source,json.loads(json.dumps(record)));assert loaded.record()==record and reads.counts().plane_reads==0
    progress=[];prepared=ij.prepare_indexed_dataset(loaded,progress=lambda a,b:progress.append((a,b)));result=prepared.preview;palette=ij.indexed_palette(result);count=len(row['palette'])
    assert result.dataset.shape==(*pixels.shape[:-1],1) and prepared.begin_edit().dependencies==ij.Change.ALL
    np.testing.assert_array_equal(result.dataset.numpy('TCZYX').reshape(-1,pixels.shape[3]*pixels.shape[4]),row['indices'])
    np.testing.assert_array_equal(palette['palette'][:count],row['palette']);assert palette['color_count']==count and not np.array(palette['palette'][count:]).any()
    assert result.dataset.calibration.spacing==[2,3,5] and result.dataset.calibration.origin==[10,20,30] and result.dataset.calibration.time_origin==7
    assert result.display.crosshair==source.display.crosshair and result.display.zoom==2 and result.display.orthogonal
    assert result.display.properties['custom_lut']==palette['palette'] and 'labels' not in result.display.properties and result.display.properties['display_range']==[0.,255.]
    assert 'lut' not in result.display.properties and 'channel_display' not in result.display.properties
    assert result.metadata['extensions']['future']=={'version':9} and result.metadata['properties']['opaque']=={'kept':[True,None]}
    assert result.metadata['extensions']['imagejxx.indexed_color_source']['command']==record
    assert reads.counts().plane_reads==pixels.shape[2] and owner.undo_steps==0
    assert progress and all(a/b<=c/d for (a,b),(c,d) in zip(progress,progress[1:]))
    if destination==ij.OutputDestination.IN_PLACE:
        owner.commit(prepared.begin_edit());assert owner.undo_steps==1
        owner.undo();np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYXS'),pixels);assert owner.snapshot().display.properties==source.display.properties
        owner.redo();assert ij.indexed_palette(owner.snapshot())==palette
    else:
        app=ij.Application();app.adopt(owner);published=app.publish(prepared);assert published.document.id!=owner.id
        assert published.document.undo_steps==0 and owner.undo_steps==0
    rgb=ij.prepare_rgb_dataset(ij.resolve_rgb_dataset_command(result)).preview
    expected=np.array(palette['palette'],np.uint8)[result.dataset.numpy('TCZYX')]
    np.testing.assert_array_equal(rgb.dataset.numpy('TCZYXS'),expected)


def indexed_fixture():
    owner,pixels,reads=fixture();prepared=ij.prepare_indexed_dataset(ij.resolve_indexed_dataset_command(owner.snapshot(),max_colors=16,destination=ij.OutputDestination.IN_PLACE));owner.commit(prepared.begin_edit());return owner,pixels,reads


@pytest.mark.parametrize('mutation',['extra','version','algorithm','policy','source','pose','colors_bool','colors_range','cap','layout','metadata'])
def test_bad_command_record_refuses_without_samples(mutation):
    owner,_,reads=fixture();record=ij.resolve_indexed_dataset_command(owner.snapshot()).record()
    if mutation=='extra':record['extra']=True
    elif mutation=='version':record['version']=True
    elif mutation=='algorithm':record['algorithm']='future'
    elif mutation=='policy':record['policy']='other'
    elif mutation=='source':record['source']['shape'][-1]=1
    elif mutation=='pose':record['view_position'][2]=999
    elif mutation=='colors_bool':record['request']['max_colors']=True
    elif mutation=='colors_range':record['request']['max_colors']=257
    elif mutation=='cap':record['request']['max_working_bytes']=1
    elif mutation=='layout':record['layout']['working_bytes']+=1
    else:record['metadata_bytes']=-1
    with pytest.raises(ValueError):ij.validate_indexed_dataset_command_record(record)
    assert reads.counts().plane_reads==0


@pytest.mark.parametrize('kind',['pixels','metadata','selection','display'])
def test_all_revision_publication_guard_and_explicit_rebind(kind):
    owner,pixels,_=fixture();app=ij.Application();app.adopt(owner);command=ij.resolve_indexed_dataset_command(owner.snapshot());prepared=ij.prepare_indexed_dataset(command)
    if kind=='display':view=owner.snapshot().display;view.zoom=3;owner.set_display(view)
    else:
        edit=owner.begin_edit()
        if kind=='pixels':edit.replace_plane(np.zeros(pixels.shape[3:],np.uint8))
        elif kind=='metadata':edit.set_title('new title')
        else:edit.select(ij.Selection('rectangle',[(0,0),(2,2)]))
        owner.commit(edit)
    with pytest.raises(ij.RevisionConflict):app.publish(prepared)
    with pytest.raises(ij.RevisionConflict):ij.indexed_dataset_command_from_record(owner.snapshot(),command.record())
    rebound=ij.rebind_indexed_dataset_command(owner.snapshot(),command.record(),max_colors=7)
    assert rebound.record()['request']['max_colors']==7


@pytest.mark.parametrize('mutation',['extra','version','policy','count_bool','count_low','count_high','rows','width','float','bool','negative','large','padding'])
def test_palette_codec_and_document_validation_are_strict(mutation):
    owner,_,_=indexed_fixture();record=ij.indexed_palette(owner.snapshot());bad=copy.deepcopy(record)
    if mutation=='extra':bad['extra']=1
    elif mutation=='version':bad['version']=2
    elif mutation=='policy':bad['policy']='future'
    elif mutation=='count_bool':bad['color_count']=True
    elif mutation=='count_low':bad['color_count']=0
    elif mutation=='count_high':bad['color_count']=257
    elif mutation=='rows':bad['palette'].pop()
    elif mutation=='width':bad['palette'][0].pop()
    elif mutation=='float':bad['palette'][0][0]=1.
    elif mutation=='bool':bad['palette'][0][0]=True
    elif mutation=='negative':bad['palette'][0][0]=-1
    elif mutation=='large':bad['palette'][0][0]=256
    else:bad['palette'][-1]=[1,2,3]
    with pytest.raises((ValueError,TypeError,OverflowError)):ij.validate_indexed_palette_record(bad)
    before=owner.snapshot()
    if mutation=='version':
        edit=owner.begin_edit();edit.set_extension('imagejxx.indexed_color',bad);owner.commit(edit)
        with pytest.raises(ValueError):ij.indexed_palette(owner.snapshot())
    else:
        with pytest.raises(ValueError):
            edit=owner.begin_edit();edit.set_extension('imagejxx.indexed_color',bad);owner.commit(edit)
        assert ij.indexed_palette(owner.snapshot())==record


def test_padding_defines_unused_indices_and_current_display_overrides_palette():
    owner,_,_=indexed_fixture();palette=ij.indexed_palette(owner.snapshot());indices=owner.snapshot().dataset.numpy('TCZYX');edit=owner.begin_edit();plane=indices[0,0,0].copy();plane[0,0]=255;edit.replace_plane(plane);owner.commit(edit)
    result=ij.prepare_rgb_dataset(ij.resolve_rgb_dataset_command(owner.snapshot())).preview.dataset.numpy('TCZYXS');np.testing.assert_array_equal(result[0,0,0,0,0],[0,0,0])
    view=owner.snapshot().display;view.properties={'lut':'Green','display_range':[0,255]};owner.set_display(view)
    rgb=ij.prepare_rgb_dataset(ij.resolve_rgb_dataset_command(owner.snapshot())).preview.dataset.numpy('TCZYXS');assert rgb[0,0,0,0,0].tolist()==[0,255,0]
    view=owner.snapshot().display;view.properties={};owner.set_display(view)
    fallback=ij.resolve_rgb_dataset_command(owner.snapshot()).record();assert fallback['request']['lut']==palette['palette']


def test_single_channel_composite_uses_indexed_palette():
    owner,_,_=indexed_fixture();result=ij.prepare_rgb_dataset(ij.resolve_rgb_dataset_command(owner.snapshot(),mode=ij.RgbDatasetMode.COMPOSITE)).preview
    palette=ij.indexed_palette(owner.snapshot());np.testing.assert_array_equal(result.dataset.numpy('TCZYXS'),np.array(palette['palette'],np.uint8)[owner.snapshot().dataset.numpy('TCZYX')])


@pytest.mark.parametrize('transport',['file','bytes'])
def test_session_retains_palette_display_and_reconstructs_rgb(tmp_path,transport):
    owner,_,_=indexed_fixture();source=owner.snapshot();capture=ij.SessionCapture([source]);capture.workflow=[source.metadata['extensions']['imagejxx.indexed_color_source']['command']];capture.results=[{'kept':True}];capture.extensions={'future':[3,None]}
    path=tmp_path/'indexed.ijx'
    if transport=='file':ij.save_session(str(path),capture);loaded=ij.load_session(str(path))
    else:loaded=ij.decode_session(ij.encode_session(capture))
    restored=loaded.documents[0].snapshot()
    assert ij.indexed_palette(restored)==ij.indexed_palette(source) and restored.display.properties==source.display.properties
    assert loaded.workflow==capture.workflow and loaded.results==capture.results and loaded.extensions==capture.extensions
    np.testing.assert_array_equal(ij.prepare_rgb_dataset(ij.resolve_rgb_dataset_command(restored)).preview.dataset.numpy('TCZYXS'),np.array(ij.indexed_palette(restored)['palette'],np.uint8)[restored.dataset.numpy('TCZYX')])


def test_execution_cap_precancel_midcancel_and_palette_encoder():
    owner,_,reads=fixture();command=ij.resolve_indexed_dataset_command(owner.snapshot());token=ij.CancellationToken();token.cancel()
    with pytest.raises(ij.OperationCancelled):ij.prepare_indexed_dataset(command,cancellation=token)
    with pytest.raises(ValueError):ij.prepare_indexed_dataset(command,execution_max_working_bytes=command.layout.working_bytes-1)
    assert reads.counts().plane_reads==0
    token=ij.CancellationToken()
    with pytest.raises(ij.OperationCancelled):ij.prepare_indexed_dataset(command,cancellation=token,progress=lambda a,b:token.cancel())
    assert owner.undo_steps==0
    result=ij.quantize_indexed_color(np.array(REFERENCE['cases'][0]['input'],np.uint8).reshape(1,17,3));encoded=ij.encode_indexed_palette(result['palette'],result['color_count']);ij.validate_indexed_palette_record(encoded)


def test_tcz_xyz_annotations_and_opaque_metadata_are_retained():
    row=REFERENCE['cases'][-1];pixels=np.array(row['input'],np.uint8).reshape(2,2,2,4,8,3);owner=ij.Document(ij.Dataset(pixels,axes='TCZYXS',calibration=ij.SpatialCalibration(spacing=[2,3,5],unit='um')))
    selection=ij.Selection('rectangle',[(0,0),(1,1)]);edit=owner.begin_edit();edit.set_rois([ij.ROI('positioned',selection,t=1,c=1,z=1)]);owner.commit(edit)
    for plane in ['xy','xz','yz']:owner.commit(ij.prepare_store_planar_display_annotation(owner.snapshot(),ij.PlanarDisplayAnnotation(plane,ij.TextAnnotationPayload(text='kept',bounds=[0,0,1,1]),plane,0,t=1,c=1)))
    owner=ij.Document(owner.snapshot());source=owner.snapshot();prepared=ij.prepare_indexed_dataset(ij.resolve_indexed_dataset_command(source,destination=ij.OutputDestination.IN_PLACE));owner.commit(prepared.begin_edit())
    result=owner.snapshot();assert result.dataset.shape==(2,2,2,4,8,1) and len(ij.planar_display_annotations(result))==3 and len(result.rois)==1
    indices=np.array(row['indices'],np.uint8).reshape(2,2,2,4,8)
    np.testing.assert_array_equal(result.dataset.numpy('TCZYX'),indices)
    np.testing.assert_array_equal(result.dataset.plane('xz',1,t=1,c=1),indices[1,1,:,1,:])
    np.testing.assert_array_equal(result.dataset.plane('yz',2,t=1,c=1),indices[1,1,:,:,2])
    expanded=np.array(ij.indexed_palette(result)['palette'],np.uint8)[indices]
    composite=ij.prepare_rgb_dataset(ij.resolve_rgb_dataset_command(result,mode=ij.RgbDatasetMode.COMPOSITE)).preview.dataset.numpy('TCZYXS')
    np.testing.assert_array_equal(composite,np.clip(expanded.astype(np.int16).sum(axis=1,keepdims=True),0,255).astype(np.uint8))
    assert result.rois[0].t==result.rois[0].c==result.rois[0].z==1 and result.dataset.calibration.spacing==[2,3,5]
    owner.undo();assert owner.snapshot().dataset.shape[-1]==3 and len(ij.planar_display_annotations(owner.snapshot()))==3


@pytest.mark.parametrize('name',['Grays','Red','Inverted','Fire','Ice','Spectrum'])
def test_native_current_palette_matches_existing_shared_rgb_review(tmp_path,name):
    from imagejxx.ui.app import Editor
    owner,_,_=indexed_fixture();view=owner.snapshot().display;view.properties={'lut':name,'display_range':[0,255]};owner.set_display(view)
    native=ij.resolve_rgb_dataset_command(owner.snapshot()).record()
    editor=Editor(preferences=tmp_path/'prefs.json');editor.add_native_document(owner);editor.action('show_rgb_dataset')
    observed=editor.render_headless(frames=3)['rgb_dataset']['record']
    assert native['request']['lut']==observed['request']['lut'] and native['request']['display_range']==observed['request']['display_range']


def test_indexed_result_can_be_viewed_by_the_existing_editor(tmp_path):
    from imagejxx.ui.app import Editor
    owner,_,_=fixture();prepared=ij.prepare_indexed_dataset(ij.resolve_indexed_dataset_command(owner.snapshot()))
    visible=ij.Document(prepared.preview);editor=Editor(preferences=tmp_path/'prefs.json');editor.add_native_document(visible)
    editor.render_headless(frames=3)
    assert not editor.error and ij.indexed_palette(editor.live_document().native_snapshot())==ij.indexed_palette(prepared.preview)
