"""Palette overrides retain frozen or deferred native channel-renderer intent."""
import copy
import numpy as np
import pytest
import imagejxx as ij


def palette():
    return np.stack([np.arange(256,dtype=np.uint8),np.zeros(256,np.uint8),np.arange(255,-1,-1,dtype=np.uint8)],axis=-1)


@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float32])
@pytest.mark.parametrize('channels',[1,2])
@pytest.mark.parametrize('captured',[False,True])
def test_palette_defaults_or_captured_range_are_metadata_only_and_replayable(dtype,channels,captured):
    pixels=np.zeros((2,channels,2,1,3),dtype)
    pixels[:]=[0,128,255]
    if channels==2:pixels[:,1]=[0,64,128]
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(pixels,axes='TCZYX').observe_reads(reads))
    if captured:
        view=owner.snapshot().display;view.properties={'display_range':[0.,255.]};owner.set_display(view)
    source=owner.snapshot();table=palette()
    command=ij.resolve_rgb_dataset_command(source,mode=ij.RgbDatasetMode.COMPOSITE,lut=table)
    assert reads.counts().plane_reads==0
    record=command.record();ij.validate_rgb_dataset_command_record(record)
    assert (record['request']['lut'] is None)==captured
    restored=ij.rgb_dataset_command_from_record(source,copy.deepcopy(record))
    table[:]=0
    result=ij.prepare_rgb_dataset(restored).preview.dataset.numpy('TCZYXS')
    expected=np.zeros((2,1,2,1,3,3),np.uint8)
    expected[...,0]=[0,128,255] if channels==1 else [0,192,255]
    expected[...,2]=[255,127,0] if channels==1 else [255,255,127]
    np.testing.assert_array_equal(result,expected)
    assert owner.undo_steps==0 and source.shares_data(owner.snapshot())


@pytest.mark.parametrize('mode',['composite','color','grayscale'])
def test_override_preserves_visibility_projection_and_active_channel(mode):
    pixels=np.zeros((1,2,1,1,3),np.uint8);pixels[:,0]=[0,128,255];pixels[:,1]=[0,64,128]
    owner=ij.Document(ij.Dataset(pixels,axes='TCZYX'));view=owner.snapshot().display;view.c=1
    state={'schema':'imagejxx.channel-display','version':2,'mode':mode,'projection':('max' if mode=='composite' else 'sum'),'channels':[
        {'visible':False,'range':[0,255],'lut':{'name':'Green'}}, {'visible':True,'range':[0,255],'lut':{'name':'Green'}}]}
    view.properties={'channel_display':state};owner.set_display(view)
    command=ij.resolve_rgb_dataset_command(owner.snapshot(),mode=ij.RgbDatasetMode.COMPOSITE,lut=palette())
    encoded=command.record()['request']['channels']
    assert encoded['channels'][0]['visible'] is False and encoded['channels'][1]['range']==[0.,255.]
    assert encoded['mode']==('color' if mode=='grayscale' else mode)
    result=ij.prepare_rgb_dataset(command).preview.dataset.numpy('TCZYXS')
    expected=np.zeros((1,1,1,1,3,3),np.uint8);expected[...,0]=[0,64,128];expected[...,2]=[255,191,127]
    np.testing.assert_array_equal(result,expected)


def test_explicit_channel_state_and_palette_override_are_captured_by_value():
    owner=ij.Document(ij.Dataset(np.array([[0,128,255]],np.uint8),axes='YX'))
    state={'schema':'imagejxx.channel-display','version':1,'mode':'composite','channels':[
        {'visible':True,'range':[0,255],'lut':{'name':'Green'}}]}
    command=ij.resolve_rgb_dataset_command(owner.snapshot(),mode=ij.RgbDatasetMode.COMPOSITE,channels=state,lut=palette())
    state['channels'][0]['range']=[0,1]
    request=command.record()['request'];assert request['lut'] is None and request['channels']['mode']=='color'
    np.testing.assert_array_equal(ij.prepare_rgb_dataset(command).preview.dataset.numpy('YXS'),[[[0,0,255],[128,0,127],[255,0,0]]])


def test_deferred_palette_budget_and_cancellation_leave_no_publication():
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(np.arange(6,dtype=np.uint16).reshape(2,3),axes='YX').observe_reads(reads))
    command=ij.resolve_rgb_dataset_command(owner.snapshot(),mode=ij.RgbDatasetMode.COMPOSITE,lut=palette())
    with pytest.raises(ValueError):ij.resolve_rgb_dataset_command(owner.snapshot(),mode=ij.RgbDatasetMode.COMPOSITE,lut=palette(),max_working_bytes=65536)
    assert reads.counts().plane_reads==0
    checkpoints=[]
    with pytest.raises(ValueError):ij.prepare_rgb_dataset(command,execution_max_working_bytes=command.layout.working_bytes-1,checkpoint=lambda:checkpoints.append(True))
    assert checkpoints==[] and reads.counts().plane_reads==0
    token=ij.CancellationToken();token.cancel()
    with pytest.raises(RuntimeError):ij.prepare_rgb_dataset(command,cancellation=token)
    assert reads.counts().plane_reads==0 and owner.undo_steps==0


def test_deferred_range_is_taken_from_captured_tz_not_later_display():
    pixels=np.array([0,128,255,0,64,128],np.uint16).reshape(2,1,1,1,3)
    owner=ij.Document(ij.Dataset(pixels,axes='TCZYX'));view=owner.snapshot().display;view.t=1;owner.set_display(view)
    command=ij.resolve_rgb_dataset_command(owner.snapshot(),mode=ij.RgbDatasetMode.COMPOSITE,lut=palette())
    view=owner.snapshot().display;view.t=0;owner.set_display(view)
    result=ij.prepare_rgb_dataset(command).preview.dataset.numpy('TCZYXS')
    expected=np.zeros((2,1,1,1,3,3),np.uint8)
    expected[0,...,0]=[0,254,255];expected[1,...,0]=[0,127,254]
    expected[...,2]=255-expected[...,0]
    np.testing.assert_array_equal(result,expected)


def test_per_plane_state_and_global_palette_remain_exclusive():
    owner=ij.Document(ij.Dataset(np.zeros((1,2),np.uint8),axes='YX'))
    state={'schema':'imagejxx.channel-display','version':1,'mode':'composite','channels':[
        {'visible':True,'range':[0,255],'lut':{'name':'Green'}}]}
    with pytest.raises(ValueError):ij.resolve_rgb_dataset_command(owner.snapshot(),channels=state,lut=palette())


def test_palette_expansion_refuses_record_limit_before_source_reads():
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(np.zeros((1,129,1,1,1),np.uint8),axes='TCZYX').observe_reads(reads))
    view=owner.snapshot().display;view.properties={'channel_display':{'schema':'imagejxx.channel-display','version':1,'mode':'color',
        'channels':[{'visible':True,'range':[0,255],'lut':{'name':'Red'}} for _ in range(129)]}}
    owner.set_display(view)
    with pytest.raises(ValueError):ij.resolve_rgb_dataset_command(owner.snapshot(),mode=ij.RgbDatasetMode.COMPOSITE,lut=palette())
    assert reads.counts().plane_reads==0 and owner.undo_steps==0
