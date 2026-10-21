"""Typed non-processing display metadata; rendering/interchange integration next."""
import base64
import copy
import json
import numpy as np
import pytest
import imagejxx as ij
from test_planar_annotations import fixture

KEY='imagejxx.planar_display_annotations'

def text_payload(**kw):
    args=dict(bounds=[-1.250000000001,2.5,6.25,3.75],text='label λ😀\nsecond line',
              font_name='SansSerif',font_size=13.500000000001,font_style=3,
              justification=2,angle=17.250000000001,draw_string_mode=True,
              color=0x80223344,background=0x20445566)
    args.update(kw);return ij.TextAnnotationPayload(**args)

def image_payload(**kw):
    args=dict(bounds=[1.250000000001,-.500000000001,4.5,2.25],width=2,height=1,channels=4,
              pixels=bytes([255,0,0,128,0,0,0,255]),opacity=123,zero_transparent=True)
    args.update(kw);return ij.ImageAnnotationPayload(**args)

def row(kind='text',plane='xz',coordinate=0,t=0,c=2):
    return ij.PlanarDisplayAnnotation('label λ😀##',text_payload() if kind=='text' else image_payload(),
        plane,coordinate,t,c,attributes={'opaque':[None,True,2**64-1,{'λ':'😀'}]})

def command(doc,annotation=None,operation='store',index=None):
    return ij.resolve_planar_display_annotation_command(doc.snapshot(),
        ij.PlanarDisplayAnnotationRequest(operation,index,annotation))

def stored(doc):return doc.snapshot().metadata['extensions'][KEY]

@pytest.mark.parametrize('plane',['xy','xz','yz'])
@pytest.mark.parametrize('kind',['text','image'])
def test_payload_precision_plane_visibility_session_undo_and_no_selection(kind,plane):
    doc,reads=fixture();before=doc.snapshot();a=row(kind,plane);cmd=command(doc,a)
    ij.validate_planar_display_annotation_command_record(cmd.record());doc.commit(ij.prepare_planar_display_annotation(cmd))
    saved=ij.planar_display_annotations(doc.snapshot())[0]
    assert (saved.plane,saved.coordinate,saved.t,saved.c)==(plane,0,0,2)
    assert saved.payload.bounds==a.payload.bounds and saved.attributes==a.attributes
    assert ij.planar_display_annotation_visible(saved,plane,0,0,2)
    assert not ij.planar_display_annotation_visible(saved,plane,0,1,2)
    assert not ij.planar_display_annotation_visible(saved,plane,1,0,2)
    if kind=='text':
        assert saved.payload.text==a.payload.text and saved.payload.font_size==a.payload.font_size
        assert saved.payload.angle==a.payload.angle and saved.payload.color==0x80223344 and saved.payload.background==0x20445566
    else:assert saved.payload.pixels==a.payload.pixels and saved.payload.opacity==123 and saved.payload.zero_transparent
    assert not doc.snapshot().rois and doc.snapshot().selection is None and ij.planar_annotations(doc.snapshot())==[]
    assert reads.counts().plane_reads==reads.counts().region_reads==0 and doc.snapshot().dataset.shares_pixels(before.dataset)
    capture=ij.SessionCapture([doc],workflow=[cmd.record()]);restored=ij.decode_session(ij.encode_session(capture))
    assert restored.workflow==[cmd.record()] and stored(restored.documents[0])==stored(doc)
    p=ij.planar_display_annotations(restored.documents[0].snapshot())[0].payload
    assert p.bounds==a.payload.bounds
    if kind=='image':assert p.pixels==a.payload.pixels
    doc.undo();assert ij.planar_display_annotations(doc.snapshot())==[]
    doc.redo();assert stored(doc)==stored(restored.documents[0])

def test_mixed_rows_update_delete_noop_and_independent_selection_annotations():
    doc,reads=fixture();before=doc.snapshot()
    selection=ij.PlanarAnnotation('selection',ij.Selection('point',[(1,2)]),'xy',0)
    doc.commit(ij.prepare_store_planar_annotation(doc.snapshot(),selection))
    for kind in ['text','image']:doc.commit(ij.prepare_store_planar_display_annotation(doc.snapshot(),row(kind)))
    initial=stored(doc);steps=doc.undo_steps
    doc.commit(ij.prepare_update_planar_display_annotation(doc.snapshot(),0,row()))
    assert doc.undo_steps==steps
    update=row('image','yz',1,t=None,c=None);doc.commit(ij.prepare_update_planar_display_annotation(doc.snapshot(),0,update))
    assert isinstance(ij.planar_display_annotations(doc.snapshot())[0].payload,ij.ImageAnnotationPayload)
    doc.commit(ij.prepare_delete_planar_display_annotation(doc.snapshot(),1));doc.undo();doc.undo();assert stored(doc)==initial
    assert ij.planar_annotations(doc.snapshot())[0].selection.points==[[1,2]]
    assert doc.snapshot().selection is None and doc.snapshot().dataset.shares_pixels(before.dataset)
    assert reads.counts().plane_reads==reads.counts().region_reads==0
    doc.commit(ij.prepare_delete_planar_display_annotation(doc.snapshot(),1));doc.commit(ij.prepare_delete_planar_display_annotation(doc.snapshot(),0))
    assert KEY not in doc.snapshot().metadata['extensions']

@pytest.mark.parametrize('case',['bounds-nan','bounds-zero','font-zero','font-huge','angle-infinite','font-style','justify','text-nul','font-empty','text-cap','font-cap','color-bool','color-overflow','flag-int'])
def test_text_payload_invalid_inputs(case):
    kw={
      'bounds-nan':{'bounds':[float('nan'),0,1,1]},'bounds-zero':{'bounds':[0,0,0,1]},
      'font-zero':{'font_size':0},'font-huge':{'font_size':16385},'angle-infinite':{'angle':float('inf')},
      'font-style':{'font_style':4},'justify':{'justification':3},'text-nul':{'text':'a\0b'},
      'font-empty':{'font_name':''},'text-cap':{'text':'x'*(1024*1024+1)},'font-cap':{'font_name':'x'*4097},
      'color-bool':{'color':True},'color-overflow':{'color':2**32},'flag-int':{'draw_string_mode':1},
    }[case]
    with pytest.raises((ValueError,TypeError,OverflowError)):text_payload(**kw)

@pytest.mark.parametrize('kw',[
    {'width':0},{'height':-1},{'width':True},{'channels':1},{'pixels':b'a'},
    {'pixels':bytearray(8)},{'pixels':'abcdefgh'},{'opacity':True},{'opacity':256},
    {'zero_transparent':0},{'bounds':[0,0,1,float('inf')]},{'width':16385},
    {'width':1024,'height':1025},
])
def test_image_payload_strict_bytes_dimensions_and_flags(kw):
    with pytest.raises((ValueError,TypeError,OverflowError)):image_payload(**kw)

@pytest.mark.parametrize('field,value',[('plane','zz'),('coordinate',True),('coordinate',None),('coordinate',-1),('t',True),('c',-1)])
def test_row_axes_strict(field,value):
    args=dict(name='x',payload=text_payload(),plane='xy',coordinate=0,t=None,c=None);args[field]=value
    with pytest.raises((ValueError,TypeError,OverflowError)):ij.PlanarDisplayAnnotation(**args)

@pytest.mark.parametrize('plane,fixed,t,c',[('xz',3,0,0),('yz',7,0,0),('xy',5,0,0),('xy',0,2,0),('xy',0,0,3)])
def test_row_extent_refusal_is_atomic(plane,fixed,t,c):
    doc,reads=fixture();before=doc.snapshot();steps=doc.undo_steps
    with pytest.raises(ValueError):ij.prepare_store_planar_display_annotation(before,row('text',plane,fixed,t,c))
    assert doc.undo_steps==steps and ij.planar_display_annotations(doc.snapshot())==[] and doc.snapshot().shares_data(before)
    assert reads.counts().plane_reads==reads.counts().region_reads==0

@pytest.mark.parametrize('mutate',[
    lambda r:r.update(extra=0),lambda r:r.update(version=True),lambda r:r.update(version=2),
    lambda r:r.update(destination='new'),lambda r:r.update(operation='unknown'),
    lambda r:r.update(index=0),lambda r:r.update(annotation=None),
    lambda r:r['after']['rows'][0]['payload'].update(text='forged'),
    lambda r:r['annotation']['rows'][0]['payload'].update(extra=0),
    lambda r:r['before'].update(extent=[2,3,5,3,7,3]),
    lambda r:r['after'].update(extent=[2.,3,5,3,7,1]),
])
def test_closed_command_equation_and_full_original_admission(mutate):
    doc,reads=fixture();cmd=command(doc,row());r=copy.deepcopy(cmd.record());mutate(r)
    target,_=fixture();request=ij.PlanarDisplayAnnotationRequest('store',annotation=row('image'))
    with pytest.raises((ValueError,IndexError,KeyError)):ij.validate_planar_display_annotation_command_record(r)
    with pytest.raises((ValueError,IndexError,KeyError)):ij.rebind_planar_display_annotation_command(target.snapshot(),r,request)
    assert ij.planar_display_annotations(target.snapshot())==[] and reads.counts().plane_reads==0

def test_live_owner_revisions_display_replay_and_fresh_explicit_rebind():
    doc,reads=fixture();cmd=command(doc,row());record=cmd.record();before=doc.snapshot()
    state=before.display;state.c=1;state.t=0;doc.set_display(state)
    assert ij.planar_display_annotation_command_from_record(doc.snapshot(),record).record()==record
    other,_=fixture();request=ij.PlanarDisplayAnnotationRequest('store',annotation=row('image'))
    with pytest.raises(ValueError,match='another source'):ij.planar_display_annotation_command_from_record(other.snapshot(),record)
    rebound=ij.rebind_planar_display_annotation_command(other.snapshot(),record,request);other.commit(ij.prepare_planar_display_annotation(rebound))
    assert isinstance(ij.planar_display_annotations(other.snapshot())[0].payload,ij.ImageAnnotationPayload)
    doc.commit(ij.prepare_store_planar_display_annotation(before,row('image')))
    with pytest.raises((ValueError,RuntimeError)):ij.planar_display_annotation_command_from_record(doc.snapshot(),record)
    assert reads.counts().plane_reads==reads.counts().region_reads==0

def test_payload_request_record_and_attribute_ownership():
    doc,_=fixture();a=row('image');request=ij.PlanarDisplayAnnotationRequest('store',annotation=a);cmd=ij.resolve_planar_display_annotation_command(doc.snapshot(),request)
    attributes=a.attributes;attributes['new']='changed';record=cmd.record();record['annotation']['rows'][0]['name']='changed'
    assert 'new' not in a.attributes and cmd.record()['annotation']['rows'][0]['name']==a.name
    with pytest.raises(AttributeError):a.payload.opacity=0
    with pytest.raises(AttributeError):a.payload.pixels=b''
    with pytest.raises(AttributeError):request.index=0
    assert request.annotation.payload.pixels==a.payload.pixels

@pytest.mark.parametrize('case',['base64','pixels','extra','kind','bool-size','float-size','size-cap','row-count','metadata-cap','extent-samples'])
def test_document_boundary_rejects_malformed_display_extension(case):
    doc,reads=fixture();cmd=command(doc,row('image'));value=copy.deepcopy(cmd.record()['after']);p=value['rows'][0]['payload']
    if case=='base64':p['pixels']='!!!!!!!!!!!!'
    elif case=='pixels':p['pixels']=base64.b64encode(b'1234567').decode()
    elif case=='extra':p['extra']=0
    elif case=='kind':p['kind']='selection'
    elif case=='bool-size':p['width']=True
    elif case=='float-size':p['width']=2.0
    elif case=='size-cap':p['width']=1024;p['height']=1025
    elif case=='row-count':value['rows']*=4097
    elif case=='metadata-cap':value['rows'][0]['attributes']={'huge':'x'*(16*1024*1024)}
    else:value['extent'][-1]=3
    edit=doc.begin_edit();edit.set_extension(KEY,value)
    with pytest.raises((ValueError,RuntimeError)):doc.commit(edit)
    assert ij.planar_display_annotations(doc.snapshot())==[] and reads.counts().plane_reads==reads.counts().region_reads==0

@pytest.mark.parametrize('operation,index,annotation',[
    ('store',0,True),('store',None,False),('update',None,True),('update',0,False),('delete',0,True),('delete',None,False)
])
def test_request_grammar_refuses_without_mutation(operation,index,annotation):
    doc,_=fixture()
    request=ij.PlanarDisplayAnnotationRequest(operation,index,row() if annotation else None)
    with pytest.raises((ValueError,IndexError)):ij.resolve_planar_display_annotation_command(doc.snapshot(),request)
    assert doc.undo_steps==0


@pytest.mark.parametrize('channels',[3,4])
def test_maximum_image_pixel_count_and_roundtrip(channels):
    doc,reads=fixture();pixels=bytes([0,7,255,128][:channels])*(1024*1024)
    payload=image_payload(width=1024,height=1024,channels=channels,pixels=pixels)
    annotation=ij.PlanarDisplayAnnotation('large',payload,'yz',0)
    doc.commit(ij.prepare_store_planar_display_annotation(doc.snapshot(),annotation))
    saved=ij.planar_display_annotations(doc.snapshot())[0].payload
    assert saved.pixels==pixels and saved.channels==channels and saved.width==saved.height==1024
    assert reads.counts().plane_reads==reads.counts().region_reads==0


def test_aggregate_image_retention_limit_is_atomic():
    doc,reads=fixture();payload=image_payload(width=1024,height=1024,pixels=b'\0'*(4*1024*1024))
    annotation=ij.PlanarDisplayAnnotation('large',payload,'xy',0)
    for _ in range(2):doc.commit(ij.prepare_store_planar_display_annotation(doc.snapshot(),annotation))
    before=doc.snapshot();steps=doc.undo_steps;initial=stored(doc)
    with pytest.raises(ValueError,match='byte cap'):
        ij.prepare_store_planar_display_annotation(before,annotation)
    assert stored(doc)==initial and doc.snapshot().shares_data(before) and doc.undo_steps==steps
    assert len(ij.planar_display_annotations(doc.snapshot()))==2
    assert reads.counts().plane_reads==reads.counts().region_reads==0
