"""Remove NaNs quiet-NaN Float32 profile; no editor-admission claim."""
import json
from pathlib import Path
import imagejxx as ij
import numpy as np
import pytest
CORPUS=json.loads((Path(__file__).parents[1]/'fixtures/remove-nans-imagej154r.json').read_bytes())
@pytest.mark.parametrize('row',CORPUS['cases'],ids=lambda r:f"s{r['signal']}-r{r['radius']}-m{r['mode']}-a{r['selection']}")
def test_runner_bits(row):
    source=np.stack([np.frombuffer(bytes.fromhex(v),dtype='<f4').reshape(20,25) for v in row['input_hex']])
    before=source.tobytes()
    # Finite dummy document supplies selection geometry only. Commands are a
    # separate pending packet; rank resolution still rejects NaN-containing data.
    owner=ij.Document(ij.Dataset(np.zeros((20,25),dtype=np.float32),axes='YX'))
    if row['selection']:
        edit=owner.begin_edit()
        edit.select(ij.Selection('rectangle',[[2,3],[21,16]]) if row['selection']==1 else ij.Selection('polygon',[[2,3],[20,2],[18,17],[5,15]]))
        owner.commit(edit)
    region=ij.rank_command_region(ij.resolve_rank_command(owner.snapshot()).target)
    result=source.copy()
    for z in range(3):
        if row['mode']==0 and z!=1:continue
        filtered=ij.remove_nans_imagej(source[z],row['radius'],extent=region['extent'])
        assert filtered.dtype==np.float32 and filtered.shape==(20,25)
        assert not np.shares_memory(filtered,source)
        if region['mask'] is not None:filtered[~region['mask'].astype(bool)]=source[z][~region['mask'].astype(bool)]
        result[z]=filtered
    assert source.tobytes()==before
    assert result.tobytes()==b''.join(bytes.fromhex(v) for v in row['output_hex'])
@pytest.mark.parametrize('radius',[-1,100.1,np.inf,np.nan,True,'2'])
def test_radius_rejected(radius):
    with pytest.raises((ValueError,TypeError)):ij.remove_nans_imagej(np.zeros((3,3),dtype=np.float32),radius)
@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float64])
def test_type_rejected(dtype):
    with pytest.raises(ValueError):ij.remove_nans_imagej(np.zeros((3,3),dtype=dtype))
@pytest.mark.parametrize('bits',[0x7f800000,0xff800000,0x7f800001,0xff800001])
def test_unsupported_nonfinite(bits):
    a=np.zeros((3,3),dtype=np.float32);a.view(np.uint32)[1,1]=bits
    before=a.tobytes()
    expected=a.copy()
    if bits & 0x7fffffff > 0x7f800000:expected[1,1]=0
    assert ij.remove_nans_imagej(a).tobytes()==expected.tobytes()
    assert ij.remove_nans_imagej(a,extent=[0,0,0,0]).tobytes()==before
    assert a.tobytes()==before
@pytest.mark.parametrize('layout',['transpose','reverse','big_endian'])
def test_bit_transport_and_layout(layout):
    a=np.arange(25,dtype=np.float32).reshape(5,5);a.view(np.uint32)[2,2]=0xffc12345;a[0,0]=-0.
    if layout=='transpose':a=a.T
    elif layout=='reverse':a=a[::-1,::-1]
    else:a=a.astype('>f4')
    before=a.tobytes();out=ij.remove_nans_imagej(a,1,extent=[0,0,0,0])
    assert out.tobytes()==a.astype(np.float32).tobytes()
    assert a.tobytes()==before
