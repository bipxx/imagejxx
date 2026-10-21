"""Actual shared editor input, draw geometry and native OpenGL output."""
import base64
import json
import os
from pathlib import Path
import struct
import zlib
import numpy as np
import pytest
import imagejxx as ij
from imagejxx import _desktop

KEY='imagejxx.planar_display_annotations'

@pytest.fixture(scope='module')
def reports(tmp_path_factory):
    cpp=json.loads(Path(os.environ['IMAGEJXX_DISPLAY_CPP_REPORT']).read_text(encoding='utf-8'))
    directory=tmp_path_factory.mktemp('display-annotations-ui')/'capture001'
    editor=_desktop.Editor(str(directory.parent/'preferences.json'))
    python=json.loads(editor._display_annotation_ui_case(str(directory)))
    assert python==json.loads((directory/'case.json').read_text(encoding='utf-8'))
    return cpp,python

def rows(state):return state['metadata'][KEY]['rows']
def layers(panes):return [v for p in panes for v in p['display_annotations']]

def test_shared_controls_and_exact_native_text_edits(reports):
    for r in reports:
        assert r['contract']=='p06q.display-annotations.ui.001' and r['state']=='observed' and r['error']==''
        assert not r['acceptance_claim'] and all(f['events']==f['rendered']['input_frame']['raw_inputs'] for f in r['frames'])
        assert all(c['control']['id'] and c['control']['enabled'] for c in r['controls'])
        s=r['states'];assert s['stored_text']==s['noop']==s['invalid']
        assert r['invalid']['error'] and s['stored_text']['undo']==3 and len(s['stored_text']['workflow'])==1
        p=rows(s['styled_text'])[2]['payload']
        assert rows(s['styled_text'])[2]['name']=='New λ😀##'
        assert p['text']=='First λ😀\nSecond' and p['font_size']==2.500000000001
        assert p['bounds']==[1.250000000001,2.5,10,6] and p['angle']==23.500000000001
        assert p['font_style']==3 and p['justification']==1 and p['draw_string_mode']
        assert p['color']==0x80223344 and p['background']==0x20445566
        assert s['styled_text']['undo']==4 and len(s['styled_text']['workflow'])==2

def test_typed_text_layout_font_diagnostics_and_plane_membership(reports):
    for r in reports:
        initial=layers(r['initial_layers']);assert [(v['kind'],v['plane'],v['coordinate']) for v in initial]==[('image','XY',0)]
        text=next(v for v in layers(r['text_layers']) if v['row']==2)
        layout=text['layout'];assert layout['lines']==2 and layout['glyphs']>0 and layout['missing_glyphs']>=1
        assert layout['rotation_origin']=='bounds_center' and layout['angle']==23.500000000001
        assert layout['font_style']==3 and layout['justification']==1 and layout['baseline_mode']
        assert any('Font is not mapped' in v['error'] for v in layers(r['unmapped_layers']))
        mapped=next(v for v in layers(r['mapped_layers']) if v['row']==1)
        assert mapped['error']=='' and mapped['plane']=='XZ' and mapped['layout']['font_alias']=='SansSerif'
        assert mapped['layout']['resolved_font']

def test_image_rgba_opacity_zero_transparency_and_explicit_capture(reports):
    for r in reports:
        image=next(v for v in layers(r['image_layers']) if v['row']==0)
        assert image['kind']=='image' and image['rgba_prefix']==[255,0,0,62,0,0,0,0] and image['rgba_bytes']==8
        saved=rows(r['states']['image_edit'])[0]
        assert saved['attributes']=={'opaque':'kept'} and saved['payload']['opacity']==123 and saved['payload']['zero_transparent']
        assert base64.b64decode(saved['payload']['pixels'])==bytes([255,0,0,128,0,0,0,255])
        captured=rows(r['states']['captured'])[3]
        assert (captured['plane'],captured['coordinate'],captured['t'],captured['c'])==('YZ',3,1,1)
        p=captured['payload'];assert (p['width'],p['height'],p['channels'])==(12,4,3)
        rgb=np.frombuffer(base64.b64decode(p['pixels']),dtype=np.uint8).reshape(4,12,3)
        assert np.all(rgb[:,:,0]>0) and np.all(rgb[:,:,1:]==0)
        assert any(v['row']==3 and v['kind']=='image' and v['plane']=='YZ' for v in layers(r['captured_layers']))
        counts=r['explicit_capture_reads'];assert counts['after_plane']+counts['after_region']>counts['before_plane']+counts['before_region']

def test_publication_workflow_undo_noop_stale_and_independent_selection(reports):
    for r in reports:
        s=r['states'];assert 'Workflow must be an array' in r['workflow_refusal']['error']
        assert s['workflow_refusal']==s['image_edit']
        assert s['recording_off']['undo']==6 and s['recording_off']['workflow']==s['image_edit']['workflow']
        assert s['image_restored']['undo']==7 and len(s['image_restored']['workflow'])==4
        assert len(rows(s['deleted']))==2 and s['deleted']['undo']==8 and len(s['deleted']['workflow'])==5
        assert len(rows(s['undone']))==3 and s['undone']['undo']==7
        assert not r['undo_cancel']['visible'] and r['undo_cancel']['cancel_reason']=='source_changed'
        assert not r['stale']['visible'] and r['stale']['cancel_reason']=='source_changed'
        assert s['captured']['undo']==s['final']['undo']==8 and len(s['final']['workflow'])==7
        assert s['final']['metadata']==s['captured']['metadata']
        assert r['shared_pixel_storage'] and r['processing_selection_absent']
        for record in s['final']['workflow']:ij.validate_planar_display_annotation_command_record(record)

def png_rgb(path):
    raw=Path(path).read_bytes();assert raw[:8]==b'\x89PNG\r\n\x1a\n';offset=8;compressed=bytearray()
    while offset<len(raw):
        size=struct.unpack_from('>I',raw,offset)[0];kind=raw[offset+4:offset+8];data=raw[offset+8:offset+8+size]
        if kind==b'IHDR':
            width,height,depth,color,_,_,interlace=struct.unpack('>IIBBBBB',data);assert depth==8 and color==2 and interlace==0
        if kind==b'IDAT':compressed.extend(data)
        offset+=size+12
    data=zlib.decompress(compressed);stride=width*3;pixels=bytearray();previous=bytearray(stride)
    def paeth(a,b,c):
        p=a+b-c;values=[abs(p-a),abs(p-b),abs(p-c)];return (a,b,c)[values.index(min(values))]
    for y in range(height):
        start=y*(stride+1);mode=data[start];row=bytearray(data[start+1:start+1+stride])
        for x in range(stride):
            a=row[x-3] if x>=3 else 0;b=previous[x];c=previous[x-3] if x>=3 else 0
            row[x]=(row[x]+(0,a,b,(a+b)//2,paeth(a,b,c))[mode])&255
        pixels.extend(row);previous=row
    return np.frombuffer(pixels,dtype=np.uint8).reshape(height,width,3)

def test_actual_hidden_opengl_renders_embedded_rgba(reports):
    for r in reports:
        rgb=png_rgb(r['screenshot']).astype(np.int16)
        assert rgb.shape==(820,1250,3)
        # The canvas source is grayscale. A large red area is the embedded
        # RGBA row after the native shader's alpha blend, not a UI wireframe.
        red=(rgb[:,:,0]-rgb[:,:,1]>40)&(rgb[:,:,0]-rgb[:,:,2]>40)
        assert red.sum()>2000


def test_typed_editor_replay_rebind_original_guard_and_zero_source_reads(tmp_path):
    from test_planar_display_annotations import fixture,row,command
    doc,reads=fixture();editor=_desktop.Editor(str(tmp_path/'preferences.json'))
    editor.add_native_document(doc);editor.recording=True
    original=command(doc,row()).record()
    editor.action('replay_display_annotation',json.dumps({'record':original}))
    assert doc.undo_steps==1 and json.loads(editor.workflow)==[original]
    with pytest.raises((ValueError,RuntimeError)):
        editor.action('replay_display_annotation',json.dumps({'record':original}))
    replacement=command(doc,row('image'),'update',0).record()
    request={key:replacement[key] for key in ('operation','index','annotation')}
    editor.action('replay_display_annotation',json.dumps({'record':original,'rebind_request':request}))
    assert doc.undo_steps==2 and isinstance(ij.planar_display_annotations(doc.snapshot())[0].payload,ij.ImageAnnotationPayload)
    held=json.loads(editor.workflow);assert len(held)==2 and held[0]==original
    bad=dict(original,version=2);before=doc.snapshot()
    with pytest.raises(ValueError,match='Unsupported planar annotation command'):
        editor.action('replay_display_annotation',json.dumps({'record':bad,'rebind_request':{'operation':'invalid'}}))
    assert doc.snapshot().shares_data(before) and json.loads(editor.workflow)==held
    editor.recording=False
    replacement=command(doc,row(),'update',0).record()
    request={key:replacement[key] for key in ('operation','index','annotation')}
    editor.action('replay_display_annotation',json.dumps({'record':original,'rebind_request':request}))
    assert doc.undo_steps==3 and json.loads(editor.workflow)==held
    assert reads.counts().plane_reads==reads.counts().region_reads==0
