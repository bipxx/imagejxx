"""Literal movie pixels from actual controls in both shared ImGui hosts."""
import json
import os
import struct
from pathlib import Path
import numpy as np
import pytest
from test_avi_movie import parse_avi

CASES=['movie-z','movie-t','movie-c','movie-reverse','movie-ping','movie-rgb',
       'movie-composite','movie-existing','movie-stale','movie-cancel','movie-invalid',
       'movie-f32','movie-ieee','movie-gray8','movie-gray-refuse','movie-draft']

@pytest.mark.parametrize('context',['imgui_cpp','imgui_python'])
@pytest.mark.parametrize('case',CASES)
def test_movie_export_controls_and_exact_avi(case,context,tmp_path):
 if context=='imgui_cpp':
  configured=os.environ.get('IMAGEJXX_MOVIE_CPP_ROOT');assert configured
  report=json.loads((Path(configured)/case/'case.json').read_bytes())
 else:
  from imagejxx import _desktop
  report=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._animation_ui_case(case,str(tmp_path/'capture')))
  assert report==json.loads((tmp_path/'capture/case.json').read_bytes())
 assert report['contract_id']=='p09y.movie-export-ui.001' and report['case_id']==case and report['context']==context
 assert report['observation_state']=='observed' and report['acceptance_claim'] is False
 assert 0<report['elapsed_seconds']<=15 and report['frame_count']==len(report['frames'])<=550
 assert not report['lifecycle']['open']
 before,after=report['before'],report['after'];rgb=case=='movie-rgb';shape=[1,1,4,5,7,3] if rgb else [3,3,4,5,7,1]
 assert report['shape']==shape
 samples=[60*z+7*y+3*x+s if rgb else 40*t+10*c+3*z+y+x
          for t in range(shape[0]) for c in range(shape[1]) for z in range(4)
          for y in range(5) for x in range(7) for s in range(shape[5])]
 if case=='movie-f32':samples=[struct.unpack('<I',struct.pack('<f',x))[0] for x in samples]
 if case=='movie-ieee':samples=[(0x7f800001,0xffc01234,0x80000000,0x7f800000,0xff800000,0x437f0000)[(t+c+z+y+x)%6] for t in range(3) for c in range(3) for z in range(4) for y in range(5) for x in range(7)]
 assert before['samples']==after['samples']==samples
 assert after['position']==before['position']==([0,0,1] if rgb else [1,1,1])
 for key in ['crosshair','pixels_revision','display_revision','selection_revision','selection','spacing','frame_interval','time_unit','workflow','results','redo']:
  assert after[key]==before[key]
 assert after['workflow']==after['results']==[]
 assert after['undo']==(1 if case=='movie-stale' else 0) and after['metadata_revision']==(1 if case=='movie-stale' else 0)
 if case!='movie-stale':assert after['shares_original'] and not after['dirty']
 controls={x['name'] for x in report['controls']}
 assert {'menu.file.movie_export','movie.path','movie.export'}<=controls
 for frame in report['frames']:
  assert frame['events']==frame['rendered']['input_frame']['raw_inputs']
  assert not frame['rendered']['window_error']
 final=report['final'];assert not final['working']
 if case=='movie-gray-refuse':
  assert not report['output_exists'] and report['files_before_report']==[]
  assert 'Gray8 requires a grayscale display' in final['error']
  return
 if case in {'movie-stale','movie-cancel'}:
  assert report['worker_gate']['entered'] and report['worker_gate']['role']
  assert not report['output_exists'] and report['files_before_report']==[]
  if case=='movie-stale':assert 'changed' in final['error']
  else:assert 'cancel' in final['message'].lower() and 'movie.cancel' in controls
  return
 assert report['output_exists'] and not final['error'] and final['message']=='Movie exported'
 path=Path(report['output_path']);assert report['files_before_report']==[path.name]
 movie=parse_avi(path);assert (movie['width'],movie['height'])==(7,5)
 assert movie['fps']==(4 if case=='movie-t' else 8)
 selected=[0,2] if case=='movie-t' else list(range(3)) if case in {'movie-c','movie-draft'} else [3,2,1,0] if case=='movie-reverse' else [0,1,2,3,2,1] if case=='movie-ping' else [0,1,2,3]
 expected=[]
 for p in selected:
  frame=[]
  for y in range(5):
   row=[]
   for x in range(7):
    if rgb:pixel=[60*p+7*y+3*x+s for s in range(3)]
    elif case=='movie-composite':pixel=[40+10*s+3*p+y+x for s in range(3)]
    elif case=='movie-ieee':pixel=[(0,0,0,255,0,255)[(1+1+p+y+x)%6]]*3
    else:
     t,c,z=(p,1,1) if case=='movie-t' else (1,p,1) if case in {'movie-c','movie-draft'} else (1,1,p)
     pixel=[40*t+10*c+3*z+y+x]*(1 if case=='movie-gray8' else 3)
    row.append(pixel)
   frame.append(row)
  expected.append(frame)
 np.testing.assert_array_equal(movie['pixels'],np.asarray(expected,np.uint8))
 assert movie['frames']==len(selected) and final['file_bytes']==path.stat().st_size
 if case=='movie-existing':
  assert len(report['refusals'])==1 and 'exists' in report['refusals'][0]['error']
  assert final['overwrite'] is True and 'movie.overwrite' in controls
 if case=='movie-invalid':
  assert len(report['refusals'])==3 and all(x['error'] and not x['working'] for x in report['refusals'])

 if case=='movie-draft':
  assert len(report['refusals'])==3
  assert all(x['error'] and not x['working'] for x in report['refusals'])
  assert [x['text']['first'] for x in report['refusals'][:2]]==['-','-']
  assert report['refusals'][1]['axis']=='c' and report['refusals'][2]['text']['fps']=='8x'
