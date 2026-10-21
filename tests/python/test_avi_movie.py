"""Independent RIFF/DIB/index parser and exact AVI transport assertions."""
import struct
import numpy as np
import pytest
import imagejxx as ij


def parse_avi(path):
 raw=path.read_bytes();assert raw[:4]==b'RIFF' and raw[8:12]==b'AVI '
 u32=lambda n:struct.unpack_from('<I',raw,n)[0]
 assert u32(4)==len(raw)-8
 def chunks(start,end):
  while start<end:
   kind=raw[start:start+4];size=u32(start+4);assert start+8+size<=end
   yield kind,start+8,size
   start+=8+size+(size&1)
  assert start==end
 top=list(chunks(12,len(raw)));assert [k for k,_,_ in top]==[b'LIST',b'LIST',b'idx1']
 hdrl,movi,index=top;assert raw[hdrl[1]:hdrl[1]+4]==b'hdrl' and raw[movi[1]:movi[1]+4]==b'movi'
 h=list(chunks(hdrl[1]+4,hdrl[1]+hdrl[2]));assert [k for k,_,_ in h]==[b'avih',b'LIST']
 avih=h[0][1];assert h[0][2]==56 and u32(avih+24)==1 and u32(avih+12)&16
 count=u32(avih+16);w,hgt=u32(avih+32),u32(avih+36)
 sl=h[1];assert raw[sl[1]:sl[1]+4]==b'strl'
 stream=list(chunks(sl[1]+4,sl[1]+sl[2]));assert [k for k,_,_ in stream]==[b'strh',b'strf']
 strh,strf=stream;assert strh[2]==56 and raw[strh[1]:strh[1]+8]==b'vidsDIB '
 scale,rate=u32(strh[1]+20),u32(strh[1]+24);assert scale>0 and rate>0
 assert u32(strh[1]+32)==count
 assert struct.unpack_from('<hhhh',raw,strh[1]+48)==(0,0,w,hgt)
 fmt=strf[1];assert u32(fmt)==40 and struct.unpack_from('<iiHHI',raw,fmt+4)==(w,hgt,1,struct.unpack_from('<H',raw,fmt+14)[0],0)
 bits=struct.unpack_from('<H',raw,fmt+14)[0];assert bits in (8,24)
 channels=bits//8;row=(w*channels+3)//4*4;size=row*hgt;assert u32(fmt+20)==size
 if bits==8:
  assert strf[2]==1064 and u32(fmt+32)==256
  assert raw[fmt+40:fmt+1064]==b''.join(bytes((i,i,i,0)) for i in range(256))
 else:assert strf[2]==40 and u32(fmt+32)==0
 frames=list(chunks(movi[1]+4,movi[1]+movi[2]));assert len(frames)==count
 assert index[2]==count*16
 pixels=[]
 for i,(kind,start,n) in enumerate(frames):
  assert kind==b'00db' and n==size
  tag,flags,offset,length=struct.unpack_from('<4sIII',raw,index[1]+16*i)
  assert (tag,flags,offset,length)==(b'00db',16,start-8-movi[1],size)
  rows=[]
  for y in range(hgt):
   line=raw[start+y*row:start+(y+1)*row]
   assert line[w*channels:]==b'\0'*(row-w*channels)
   values=np.frombuffer(line[:w*channels],np.uint8).reshape(w,channels)
   rows.append(values[:,::-1] if channels==3 else values)
  pixels.append(np.stack(rows[::-1]))
 return dict(width=w,height=hgt,frames=count,fps=rate/scale,rate=rate,scale=scale,
             microseconds=u32(avih),pixels=np.stack(pixels),bytes=len(raw))


@pytest.mark.parametrize('width',[1,2,3,4,7])
@pytest.mark.parametrize('channels',[1,3])
def test_avi_exact_rows_palette_and_index(width,channels,tmp_path):
 pixels=(np.arange(3*2*width*channels,dtype=np.uint8).reshape(3,2,width,channels)*3)%251
 source=pixels.copy();path=tmp_path/'movie.avi'
 ij.write_avi(path,pixels[...,0] if channels==1 else pixels,fps=7.5)
 movie=parse_avi(path)
 assert (movie['width'],movie['height'],movie['frames'],movie['rate'],movie['scale'])==(width,2,3,15,2)
 assert movie['microseconds']==133333
 np.testing.assert_array_equal(movie['pixels'],source);np.testing.assert_array_equal(pixels,source)


@pytest.mark.parametrize('fps',[.001,.1,1,8,24,29.97,60,1000])
def test_avi_rates(fps,tmp_path):
 path=tmp_path/'rate.avi';ij.write_avi(path,np.zeros((1,2,3),np.uint8),fps=fps)
 movie=parse_avi(path);assert movie['fps']==pytest.approx(fps,abs=1e-6)
 assert abs(movie['microseconds']-1e6/movie['fps'])<=.5


@pytest.mark.parametrize('fps',[0,-1,float('inf'),float('nan'),.0001,1001])
def test_avi_invalid_rate_preserves_destination(fps,tmp_path):
 path=tmp_path/'keep.avi';path.write_bytes(b'owned sentinel')
 with pytest.raises(ValueError):ij.write_avi(path,np.zeros((2,2,3),np.uint8),fps=fps,replace_existing=True)
 assert path.read_bytes()==b'owned sentinel' and list(tmp_path.iterdir())==[path]


@pytest.mark.parametrize('shape,dtype',[((2,3),np.uint8),((2,3,4,4),np.uint8),((2,3,4),np.uint16),((2,3,4),np.float32)])
def test_avi_rejects_ambiguous_or_unrendered_source(shape,dtype,tmp_path):
 path=tmp_path/'absent.avi'
 with pytest.raises(ValueError):ij.write_avi(path,np.zeros(shape,dtype))
 assert list(tmp_path.iterdir())==[]


def test_avi_limits_overwrite_unicode_and_immutable_dataset(tmp_path):
 pixels=np.arange(2*3*5,dtype=np.uint8).reshape(2,3,5)
 source=ij.Dataset(pixels,axes='ZYX');path=tmp_path/'µ-日本.avi'
 ij.write_avi(path,source);encoded=path.read_bytes();movie=parse_avi(path)
 with pytest.raises(Exception):ij.write_avi(path,source)
 assert path.read_bytes()==encoded
 for kwargs in [dict(max_file_bytes=movie['bytes']-1),dict(max_working_bytes=1)]:
  with pytest.raises(ValueError):ij.write_avi(path,source,replace_existing=True,**kwargs)
  assert path.read_bytes()==encoded
 ij.write_avi(path,source,replace_existing=True);assert path.read_bytes()==encoded
 np.testing.assert_array_equal(source.numpy('ZYX'),pixels)
 assert list(tmp_path.iterdir())==[path]


def test_avi_scientific_dataset_requires_explicit_axis_selection(tmp_path):
 source=ij.Dataset(np.zeros((2,3,4,5,7,1),np.uint8),axes='TCZYXS')
 with pytest.raises(ValueError):ij.write_avi(tmp_path/'absent.avi',source)
 assert list(tmp_path.iterdir())==[]


def test_avi_progress_and_checkpoint_are_before_publication(tmp_path):
 path=tmp_path/'progress.avi';progress=[];checks=[]
 ij.write_avi(path,np.zeros((3,2,3),np.uint8),
              progress=lambda done,total:progress.append((done,total,path.exists())),
              checkpoint=lambda:checks.append(path.exists()))
 assert progress==[(0,3,False),(1,3,False),(2,3,False),(3,3,False)]
 assert checks and not any(checks) and path.exists()


@pytest.mark.parametrize('where',['before','progress','checkpoint'])
def test_avi_python_cancellation_keeps_existing_file(where,tmp_path):
 path=tmp_path/'keep.avi';path.write_bytes(b'previous movie')
 token=ij.CancellationToken();count=0
 if where=='before':token.cancel()
 def checkpoint():
  nonlocal count
  count+=1
  if where=='checkpoint' and count==10:token.cancel()
 def progress(done,total):
  if where=='progress' and done==1:token.cancel()
 with pytest.raises(ij.OperationCancelled):
  ij.write_avi(path,np.zeros((3,2,3),np.uint8),replace_existing=True,
               cancellation=token,progress=progress,checkpoint=checkpoint)
 assert path.read_bytes()==b'previous movie' and list(tmp_path.iterdir())==[path]


def test_avi_python_callback_failure_discards_the_stage(tmp_path):
 path=tmp_path/'keep.avi';path.write_bytes(b'previous movie')
 def progress(done,total):
  if done==1:raise RuntimeError('owned callback failure')
 with pytest.raises(RuntimeError,match='owned callback failure'):
  ij.write_avi(path,np.zeros((3,2,3),np.uint8),replace_existing=True,progress=progress)
 assert path.read_bytes()==b'previous movie' and list(tmp_path.iterdir())==[path]


def test_avi_relative_destination_is_frozen_before_callbacks(tmp_path,monkeypatch):
 monkeypatch.chdir(tmp_path);other=tmp_path/'other';other.mkdir();moved=False
 def checkpoint():
  nonlocal moved
  if not moved:monkeypatch.chdir(other);moved=True
 ij.write_avi('relative.avi',np.zeros((2,2,3),np.uint8),checkpoint=checkpoint)
 assert (tmp_path/'relative.avi').is_file() and list(other.iterdir())==[]


@pytest.mark.parametrize('path',[None,7,object(),b'\xff'])
def test_avi_path_requires_filesystem_text(path,tmp_path,monkeypatch):
 monkeypatch.chdir(tmp_path)
 with pytest.raises((TypeError,UnicodeError)):
  ij.write_avi(path,np.zeros((1,2,3),np.uint8))
 assert list(tmp_path.iterdir())==[]


def test_avi_small_frames_still_reserve_palette_header_scratch(tmp_path):
 path=tmp_path/'absent.avi'
 with pytest.raises(ValueError):
  ij.write_avi(path,np.zeros((1,1,1),np.uint8),max_working_bytes=8192)
 assert list(tmp_path.iterdir())==[]
