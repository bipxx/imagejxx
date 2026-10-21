"""Bit-level source/transport contract; IEEE processing/display is a later gate."""
import base64,copy,json,struct,zlib
from pathlib import Path
import imagejxx as ij
import numpy as np
import pytest
FIX=Path(__file__).parents[1]/'fixtures'
GOLD=json.loads((FIX/'ieee-float32-source001.json').read_bytes())
WORDS=np.frombuffer(bytes.fromhex(GOLD['binary32_le']),dtype='<u4').copy()
F64=bytes.fromhex(GOLD['binary64_le'])
IEEE=ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32
QUIET=ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32

def pixels():return WORDS.view(np.float32).reshape(2,2,2,16,32)
def native(a=None,axes='TCZYX'):return ij.Dataset(pixels() if a is None else a,axes=axes,sample_policy=IEEE)
def raw(a):return a.tobytes(order='C')
def frame(document):
    encoded=json.dumps(document,separators=(',',':'),allow_nan=False).encode()
    return b'IMAGEJXX3\n'+struct.pack('<Q',len(encoded))+zlib.compress(encoded)

@pytest.mark.parametrize('value',GOLD['special_patterns'])
def test_literal_scalar_patterns(value):
    word=int(value,16);i=GOLD['special_patterns'].index(value);a=np.array([word],np.uint32).view(np.float32)[0]
    assert ij.float32_source_sample_bits(a,sample_policy=IEEE)==word
    represented=ij.float32_source_sample(word,sample_policy=IEEE)
    assert struct.pack('<d',represented)==F64[i*8:i*8+8]
    assert ij.float32_source_sample_bits(np.frombuffer(F64[i*8:i*8+8],dtype='<f8')[0],sample_policy=IEEE)==word
    if word&0x7f800000==0x7f800000:
        with pytest.raises(ValueError):ij.float32_source_sample(word)
        if not word&0x400000:
            with pytest.raises(ValueError):ij.float32_source_sample(word,sample_policy=QUIET)

@pytest.mark.parametrize('layout',['contiguous','transpose','reverse','fortran','big_endian','unaligned','readonly'])
def test_source_buffer_layouts(layout):
    words=WORDS.reshape(64,64)
    if layout=='transpose':words=words.T
    elif layout=='reverse':words=words[::-1,::2]
    elif layout=='fortran':words=np.asfortranarray(words)
    elif layout=='big_endian':words=words.astype('>u4')
    elif layout=='unaligned':
        backing=bytearray(1+words.nbytes);backing[1:]=words.tobytes();words=np.ndarray(words.shape,dtype='<u4',buffer=backing,offset=1)
    a=words.view(np.dtype(words.dtype.str.replace('u','f')))
    if layout=='readonly':a.setflags(write=False)
    expected=words.astype('<u4').tobytes();before=a.tobytes()
    data=native(a,'YX');out=data.numpy('YX')
    assert out.dtype==np.float32 and out.tobytes()==expected and a.tobytes()==before
    canonical=ij.canonical_float32_source(a,sample_policy=IEEE)
    assert canonical.tobytes()==expected and not np.shares_memory(canonical,a)

def test_all_patterns_multiaxis_regions_and_planes():
    a=pixels();data=native();assert data.sample_policy==IEEE and raw(data.numpy('TCZYX'))==raw(a)
    assert raw(data.numpy('CZTXY'))==raw(a.transpose(1,2,0,4,3))
    for t in range(2):
        for c in range(2):
            for z in range(2):assert raw(data.plane('xy',z,t=t,c=c))==raw(a[t,c,z])
            assert raw(data.plane('xz',3,t=t,c=c))==raw(a[t,c,:,3,:])
            assert raw(data.plane('yz',4,t=t,c=c))==raw(a[t,c,:,:,4])
    assert raw(data.region(2,3,5,4,t=1,c=1,z=1))==raw(a[1,1,1,3:7,2:7])

@pytest.mark.parametrize('operation',['dataset','transaction'])
def test_replacement_and_history_keep_signaling_bits(operation):
    a=pixels();owner=ij.Document(native());owner.mark_saved();replacement=a[0,0,0].copy();replacement.view(np.uint32)[0,0]=0xff800001
    if operation=='dataset':
        replaced=owner.snapshot().dataset.with_plane(replacement);edit=owner.begin_edit();edit.replace_dataset(replaced)
    else:edit=owner.begin_edit();edit.replace_plane(replacement)
    owner.commit(edit);expected=a.copy();expected[0,0,0]=replacement
    assert owner.undo_steps==1 and owner.snapshot().dataset.sample_policy==IEEE
    assert raw(owner.snapshot().dataset.numpy('TCZYX'))==raw(expected)
    owner.undo();assert raw(owner.snapshot().dataset.numpy('TCZYX'))==raw(a) and not owner.snapshot().dirty
    owner.redo();assert raw(owner.snapshot().dataset.numpy('TCZYX'))==raw(expected)

@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float64])
def test_non_float32_source_rejected(dtype):
    with pytest.raises(ValueError):native(np.zeros((2,3),dtype),'YX')

def test_rgb_and_unrepresentable_binary64_rejected():
    with pytest.raises(ValueError):native(np.zeros((2,3,3),np.float32),'YXS')
    for word in [0x7ff8000000000001,0x7ff0000000000001]:
        value=np.array([word],np.uint64).view(np.float64)[0]
        with pytest.raises(ValueError):ij.float32_source_sample_bits(value,sample_policy=IEEE)
    with pytest.raises(ValueError):ij.float32_source_sample_bits(.1,sample_policy=IEEE)

def test_independent_native_v3_golden_and_encode_bytes():
    decoded=ij.load_session(str(FIX/'ieee-float32-source001.ijx'));owner=decoded.documents[0]
    assert decoded.source_version==3 and owner.undo_steps==0 and not owner.snapshot().dirty
    assert owner.snapshot().dataset.sample_policy==IEEE and raw(owner.snapshot().dataset.numpy('TCZYX'))==raw(pixels())
    encoded=ij.encode_session(ij.SessionCapture([owner.snapshot()]));root=json.loads(zlib.decompress(encoded[18:]))
    document=root['documents'][0];assert document['sample_policy']=='preserve_ieee_float32'
    assert b''.join(base64.b64decode(p) for p in document['planes'])==F64
    restored=ij.decode_session(encoded).documents[0].snapshot()
    assert restored.dataset.sample_policy==IEEE and raw(restored.dataset.numpy('TCZYX'))==raw(pixels())

@pytest.mark.parametrize('policy',['finite_only','preserve_quiet_nan_float32','preserve_ieee_float64',False,2])
def test_serialized_policy_never_silently_widens(policy):
    original=(FIX/'ieee-float32-source001.ijx').read_bytes();root=json.loads(zlib.decompress(original[18:]));root['documents'][0]['sample_policy']=policy
    with pytest.raises(ValueError):ij.decode_session(frame(root))

def test_nonbinary32_session_payload_is_rejected():
    original=(FIX/'ieee-float32-source001.ijx').read_bytes();root=json.loads(zlib.decompress(original[18:]));doc=root['documents'][0]
    data=bytearray(base64.b64decode(doc['planes'][0]));data[:8]=struct.pack('<Q',0x7ff8000000000001);doc['planes'][0]=base64.b64encode(data).decode()
    with pytest.raises(ValueError):ij.decode_session(frame(root))

@pytest.mark.parametrize('stack',[False,True])
def test_detached_model_and_v2_native_bridge(tmp_path,stack):
    from imagejxx.ui.model import Document
    from imagejxx.ui.persistence import serialize_document,deserialize_document,save_session,load_session
    a=WORDS.view(np.float32).reshape(8,16,32) if stack else WORDS.view(np.float32).reshape(64,64)
    document=Document(a,is_stack=stack,sample_policy=IEEE);assert document.sample_policy==IEEE and raw(document.data)==raw(a)
    payload=serialize_document(document);assert payload['pixel_sample_policy']=='preserve_ieee_float32'
    assert base64.b64decode(payload['pixels'])==F64
    restored=deserialize_document(payload);assert restored.sample_policy==IEEE and raw(restored.data)==raw(a)
    path=tmp_path/'ieee-v2.ijx';save_session(path,[document]);opened,_,_=load_session(path)
    assert opened[0].sample_policy==IEEE and raw(opened[0].data)==raw(a)
    native_loaded=ij.load_session(str(path)).documents[0].snapshot();axes='ZYX' if stack else 'YX'
    assert native_loaded.dataset.sample_policy==IEEE and raw(native_loaded.dataset.numpy(axes))==raw(a)
    native_path=tmp_path/'ieee-v3.ijx';ij.save_session(str(native_path),ij.SessionCapture([native_loaded]));detached,_,_=load_session(native_path)
    assert detached[0].sample_policy==IEEE and raw(detached[0].data)==raw(a)
    changed=a.copy();changed.view(np.uint32).reshape(-1)[0]=0xff800001;document.mark_saved();document.commit(changed)
    document.undo();assert raw(document.data)==raw(a)
    document.redo();assert raw(document.data)==raw(changed)

@pytest.mark.parametrize('operation',['remove_nans_imagej','rank_filter_imagej','invert'])
def test_unqualified_processing_refused_before_pixel_read(operation):
    reads=ij.DatasetReadObserver();owner=ij.Document(native().observe_reads(reads));target=ij.resolve_target(owner.snapshot(),operation=operation)
    if operation!='remove_nans_imagej':
        with pytest.raises(ValueError,match='qualified operation profile'):ij.prepare_operation(operation,target)
    ij.resolve_remove_nans_command(owner.snapshot())
    assert reads.counts().plane_reads==0 and owner.undo_steps==0
