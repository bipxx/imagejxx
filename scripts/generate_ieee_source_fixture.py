"""Deterministic binary32 transport goldens; independent of product code."""
from pathlib import Path
import base64,json,struct,zlib,hashlib
ROOT=Path(__file__).resolve().parents[1]
def build():
 special=[0x00000000,0x80000000,0x00000001,0x80000001,0x007fffff,0x807fffff,0x00800000,0x80800000,0x3f800000,0xbf800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7f800001,0xff800001,0x7fbfffff,0xffbfffff,0x7fc00000,0xffc00000,0x7fc00001,0xffc12345,0x7fffffff,0xffffffff,0x3eaaaaab,0xbeaaaaab,0x4b000001,0xcb000001,0x00800001,0x80800001,0x00400000,0x80400000]
 words=list(special);seed=0x154ffeed
 while len(words)<4096:
  seed=(1664525*seed+1013904223)&0xffffffff
  word=seed if len(words)<2048 else (seed&0x80000000)|0x7f800000|(seed&0x7fffff)
  words.append(word)
 raw32=struct.pack('<4096I',*words);raw64=bytearray()
 for word in words:
  exponent=(word>>23)&255
  if exponent==255:
   # The format's sign moves to bit63 and its 23 fraction bits occupy
   # bits51..29. No floating conversion is used for nonfinite patterns.
   raw64+=struct.pack('<Q',((word>>31)<<63)|(2047<<52)|((word&0x7fffff)<<29))
  else:raw64+=struct.pack('<d',struct.unpack('<f',struct.pack('<I',word))[0])
 cal=dict(spacing=[1.,1.,1.],origin=[0.,0.,0.],direction=[1.,0.,0.,0.,1.,0.,0.,0.,1.],unit='px',time_unit='s',frame_interval=1.,time_origin=0.)
 display=dict(t=0,c=0,crosshair=[0,0,0],orthogonal=False,linked_zoom=True,zoom=1.,plane_zoom=[1.,1.,1.],pan=[[0.,0.],[0.,0.],[0.,0.]],properties={})
 document=dict(axes='TCZYXS',shape=[2,2,2,16,32,1],pixel_type='float32',sample_policy='preserve_ieee_float32',storage='Float64LE',planes=[base64.b64encode(raw64[i:i+4096]).decode() for i in range(0,len(raw64),4096)],calibration=cal,metadata=dict(title='Owned IEEE binary32 transport',properties={'fixture':'literal binary32/binary64 layout'},extensions={},record_layout={}),display=display,selection=None,previous_selection=None,rois=[],overlays=[],fourier=None)
 envelope=dict(format='imagejxx-session',version=3,documents=[document],results=[],workflow=[],extensions={'owned_fixture':1})
 text=json.dumps(envelope,separators=(',',':'),allow_nan=False).encode();frame=b'IMAGEJXX3\n'+struct.pack('<Q',len(text))+zlib.compress(text)
 payload=dict(schema_version=1,scope='Source-bit storage/transport goldens, not ImageJ processing claims',shape_tczyxs=[2,2,2,16,32,1],special_patterns=[f'{x:08x}' for x in special],pattern_count=4096,binary32_le=raw32.hex(),binary64_le=raw64.hex(),session_frame_sha256=hashlib.sha256(frame).hexdigest(),generator='scripts/generate_ieee_source_fixture.py')
 return payload,frame
if __name__=='__main__':
 payload,frame=build();target=ROOT/'tests/fixtures/ieee-float32-source001.json';archive=ROOT/'tests/fixtures/ieee-float32-source001.ijx'
 assert not target.exists() and not archive.exists();target.write_text(json.dumps(payload,indent=2)+'\n');archive.write_bytes(frame)
 print('Generated 4096 source patterns and independently framed V3 session')
