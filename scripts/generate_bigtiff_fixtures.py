"""Independent BigTIFF structures, row transforms and bounded codec carriers."""
from pathlib import Path
import hashlib,importlib.util,json,struct,zlib
ROOT=Path(__file__).resolve().parents[1];DEST=ROOT/'tests/fixtures/scientific-bigtiff'
spec=importlib.util.spec_from_file_location('owned_codec',ROOT/'scripts/generate_packbits_lzw_tile_fixtures.py');codec=importlib.util.module_from_spec(spec);spec.loader.exec_module(codec)
PROFILES=json.loads((ROOT/'tests/fixtures/bigtiff-profiles.json').read_bytes())['profiles']

def make(profile,endian,shape,tile,pattern='finite',bad=None,table_type=16):
 t,c,z,y,x=shape;tw,th=tile;size=4 if profile['dtype']=='float32' else 2 if profile['dtype']=='uint16' else 1;s=profile['components'];compression=profile['compression'];predictor=profile['predictor'];tiled=profile['storage']=='tiled';mask=(1<<(size*8))-1
 pool=[0,0x80000000,0x3f800000,0xbf800000,1,0x80000001,0x007fffff,0x7f7fffff,0xff7fffff]
 if pattern!='finite':pool += [0x7fc01234,0xffc01234]
 if pattern=='ieee':pool += [0x7f800000,0xff800000,0x7f800001,0xff800001]
 def sample(tt,cc,zz,yy,xx,ss):
  n=tt*31+cc*17+zz*7+yy*5+xx+ss*13
  return pool[n%len(pool)] if size==4 else (n*7919+yy*257+xx*131+ss*89)&mask
 expected=b''.join(sample(tt,cc,zz,yy,xx,ss).to_bytes(size,'little') for tt in range(t) for cc in range(c) for zz in range(z) for yy in range(y) for xx in range(x) for ss in range(s))
 pack=lambda f,*v:struct.pack(endian+f,*v);out=bytearray((b'II' if endian=='<' else b'MM')+pack('HHHQ',43,8,0,0));payloads=[];directories=[];padding=0x7f800001 if size==4 else 0xa5
 def block(raw):
  out.extend(bytes((-len(out))%8));offset=len(out);out.extend(raw);return offset
 def encode(raw,width):
  result=bytearray();row_bytes=width*s*size
  for at in range(0,len(raw),row_bytes):
   words=[int.from_bytes(raw[i:i+size],'little' if endian=='<' else 'big') for i in range(at,at+row_bytes,size)]
   if predictor==3:
    shuffled=[(v>>shift)&255 for shift in [24,16,8,0] for v in words];result.extend((v-(shuffled[i-1] if i else 0))&255 for i,v in enumerate(shuffled))
   elif predictor==2:result.extend(b''.join(((v-(words[i-s] if i>=s else 0))&mask).to_bytes(size,'little' if endian=='<' else 'big') for i,v in enumerate(words)))
   else:result.extend(raw[at:at+row_bytes])
  raw=bytes(result)
  return zlib.compress(raw) if compression in [8,32946] else codec.lzw_encode(raw) if compression==5 else codec.literal(raw,row_bytes) if compression==32773 else raw
 description=(f'ImageJ=1.54r\nimages={t*c*z}\nchannels={c}\nslices={z}\nframes={t}\nhyperstack=true\nunit=um\nspacing=2.75\nfinterval=0.125\ntunit=ms\n').encode()+b'\0'
 rows=min(32,y)
 for tt in range(t):
  for zz in range(z):
   for cc in range(c):
    offsets=[];counts=[]
    for top in range(0,y,th if tiled else rows):
     for left in (range(0,x,tw) if tiled else [0]):
      width=tw if tiled else x;height=th if tiled else min(rows,y-top)
      raw=b''.join((sample(tt,cc,zz,top+dy,left+dx,ss) if top+dy<y and left+dx<x else padding).to_bytes(size,'little' if endian=='<' else 'big') for dy in range(height) for dx in range(width) for ss in range(s))
      encoded=encode(raw,width);offsets.append(block(encoded));counts.append(len(encoded));payloads.append((offsets[-1],len(encoded)))
    fields={}
    def field(tag,kind,values):
     raw=values if isinstance(values,bytes) else pack({3:'H',4:'I',5:'II',16:'Q'}[kind]*len(values),*([n for pair in values for n in pair] if kind==5 else values))
     count=len(raw)//{2:1,3:2,4:4,5:8,16:8}[kind];fields[tag]=[kind,count,raw]
    for tag,v in [(254,0),(256,x),(257,y)]:field(tag,4,[v])
    for tag,v in [(258,[size*8]*s),(259,[compression]),(262,[2 if s==3 else 1]),(274,[1]),(277,[s]),(284,[1]),(296,[1]),(339,[3 if size==4 else 1]*s)]:field(tag,3,v)
    if predictor!=1:field(317,3,[predictor])
    if tiled:
     field(322,4,[tw]);field(323,4,[th])
    else:field(278,4,[rows])
    # Mix legal SHORT/LONG/LONG8 tables; all generated compact offsets fit SHORT.
    field(324 if tiled else 273,table_type,offsets);field(325 if tiled else 279,table_type,counts)
    field(282,5,[(2,1)]);field(283,5,[(4,5)])
    if not directories:field(270,2,description);field(269,2,b'inline\0')
    directories.append(fields)
 first=directories[0]
 if bad=='offset-size':out[4:6]=pack('H',16)
 elif bad=='reserved':out[6:8]=pack('H',1)
 elif bad=='version':out[2:4]=pack('H',44)
 elif bad=='missing-offsets':first.pop(324 if tiled else 273)
 elif bad=='missing-counts':first.pop(325 if tiled else 279)
 elif bad=='unknown-tag':first[65000]=[3,1,pack('H',1)]
 elif bad=='signed-table':first[325 if tiled else 279][0]=17
 elif bad=='ifd8-table':first[324 if tiled else 273][0]=18
 elif bad=='zero-count':first[258][1]=0
 elif bad=='huge-count':first[258][1]=(1<<64)-1
 elif bad=='zero-denominator':first[282][2]=pack('II',2,0)
 elif bad=='mixed-compression':directories[1][259][2]=pack('H',1 if compression!=1 else 5)
 elif bad=='payload-outside':first[324 if tiled else 273]=[16,1,pack('Q',(1<<64)-8)]
 elif bad=='payload-overlap':
  key=324 if tiled else 273;first[key][2]=pack('Q',payloads[1][0])+first[key][2][8:]
 elif bad=='oversized-block':first[325 if tiled else 279]=[16,first[325 if tiled else 279][1],pack('Q',1<<40)*first[325 if tiled else 279][1]]
 elif bad=='wrong-predictor':first[317]=[3,1,pack('H',2 if size==4 else 3)]
 elif bad=='unknown-predictor':first[317]=[3,1,pack('H',4)]
 elif bad=='mixed-dimensions':directories[1][256][2]=pack('I',x+1)
 # Build directories after payloads; values up to eight bytes are always inline.
 ifds=[];external=[]
 for fields in directories:
  out.extend(bytes((-len(out))%8));ifds.append(len(out));out.extend(bytes(16+20*len(fields)))
 out[8:16]=pack('Q',ifds[0])
 for p,(offset,fields) in enumerate(zip(ifds,directories)):
  out[offset:offset+8]=pack('Q',(1<<64)-1 if bad=='huge-ifd-count' and not p else len(fields))
  for i,(tag,(kind,count,raw)) in enumerate(sorted(fields.items())):
   at=offset+8+20*i;value=raw.ljust(8,b'\0') if len(raw)<=8 else pack('Q',block(raw));external.append((at+12,len(raw),value))
   out[at:at+20]=pack('HHQ',tag,kind,count)+value
  next_ifd=ifds[p+1] if p+1<len(ifds) else ifds[0] if bad=='ifd-loop' else 0
  out[offset+8+20*len(fields):offset+16+20*len(fields)]=pack('Q',next_ifd)
 if bad=='outside-ifd':out[8:16]=pack('Q',(1<<64)-8)
 elif bad=='header-ifd':out[8:16]=pack('Q',8)
 elif bad=='duplicate-tag':out[ifds[0]+28:ifds[0]+30]=out[ifds[0]+8:ifds[0]+10]
 elif bad=='truncated-ifd':out=out[:ifds[-1]+10]
 elif bad=='external-overflow':
  at,n,value=next(r for r in external if r[1]>8);out[at:at+8]=pack('Q',(1<<64)-8)
 return bytes(out),dict(profile_ordinal=profile['ordinal'],format_id=profile['format_id'].replace('F.G080.tiff-','F.G080.bigtiff-',1),dtype='f32' if size==4 else 'rgb8' if s==3 else 'u16' if size==2 else 'u8',shape=[t,c,z,y,x,s],tile=list(tile),storage=profile['storage'],rows=rows,compression=compression,predictor=predictor,table_type=table_type,endian='little' if endian=='<' else 'big',pattern=pattern,padding=padding,hex=expected.hex(),spacing=[.5,1.25,2.75],sample_policy='preserve_ieee_float32' if pattern=='ieee' else 'preserve_quiet_nan_float32' if pattern=='quiet' else 'finite_only')

def generate():
 DEST.mkdir(exist_ok=True);records=[]
 def save(name,data,meta,valid,defect=None):
  (DEST/name).write_bytes(data);records.append(dict(file=name,valid=valid,defect=defect,bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),**meta))
 for profile in PROFILES:
  for geometry,shape,tile in [('one',(1,1,1,1,1),(16,16)),('edges',(2,2,2,17,35),(16,16)),('wide',(1,1,1,65,67),(32,16))]:
   for endian,label in [('<','le'),('>','be')]:
    for pattern in ['finite','quiet','ieee'] if profile['dtype']=='float32' else ['finite']:
     table_type=16 if geometry=='wide' else 4 if geometry=='edges' else 3
     data,meta=make(profile,endian,shape,tile,pattern,table_type=table_type);save(f"p{profile['ordinal']:02}-{geometry}-{label}-{pattern}.tif",data,meta,True)
 defects=['offset-size','reserved','version','missing-offsets','missing-counts','unknown-tag','signed-table','ifd8-table','zero-count','huge-count','zero-denominator','mixed-compression','payload-outside','payload-overlap','oversized-block','wrong-predictor','unknown-predictor','mixed-dimensions','huge-ifd-count','outside-ifd','header-ifd','duplicate-tag','truncated-ifd','external-overflow','ifd-loop']
 for ordinal in [0,1,2,3,40,44,42,47]:
  for defect in defects:
   data,meta=make(PROFILES[ordinal],'<',(2,2,2,17,35),(16,16),bad=defect);save(f'p{ordinal:02}-{defect}.tif',data,meta,False,defect)
 (DEST/'SOURCE.json').write_text(json.dumps(dict(generator='scripts/generate_bigtiff_fixtures.py',generator_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),codec_generator_sha256=hashlib.sha256((ROOT/'scripts/generate_packbits_lzw_tile_fixtures.py').read_bytes()).hexdigest(),specification='https://libtiff.gitlab.io/libtiff/specification/bigtiff.html',files=records),indent=2)+'\n')
 print(json.dumps(dict(valid=sum(r['valid'] for r in records),invalid=sum(not r['valid'] for r in records))))
if __name__=='__main__':generate()
