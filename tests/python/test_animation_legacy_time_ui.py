"""Ordinary Z time-series controls with literal sample/position/time oracles."""
import json
import os
from pathlib import Path
import pytest
from test_animation_ui import samples

EXPECTED={
 'cal-legacy':[2,3,4,0,1,2,3,4],
 'cal-legacy-step':[0,2,4,0,2,4,0,2],
 'cal-legacy-reverse':[0,4,3,2,1,0,4,3],
 'cal-legacy-ping':[2,3,4,3,2,1,0,1],
 'cal-legacy-edit':[2,3,4],
 'cal-legacy-unit':[2,3,4,0,1,2,3,4],
}

@pytest.mark.parametrize('context',['imgui_cpp','imgui_python'])
@pytest.mark.parametrize('case',list(EXPECTED))
def test_actual_legacy_time_controls(case,context,tmp_path):
 if context=='imgui_cpp':
  root=os.environ.get('IMAGEJXX_ANIMATION_CPP_ROOT');assert root
  report=json.loads((Path(root)/case/'case.json').read_bytes())
 else:
  from imagejxx import _desktop
  report=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._animation_ui_case(case,str(tmp_path/'capture')))
  assert report==json.loads((tmp_path/'capture/case.json').read_bytes())
 assert report['contract_id']=='p09s.animation-ui.001'
 assert (report['case_id'],report['context'])==(case,context)
 assert report['observation_state']=='observed' and report['acceptance_claim'] is False
 assert report['frame_count']==len(report['frames'])<=180 and 0<report['elapsed_seconds']<=15
 assert not report['lifecycle']['open'] and report['shape']==[1,1,5,5,7,1]
 before,after=report['before'],report['after']
 assert before['samples']==after['samples']==samples('legacy',report['shape'])
 draft=report['options_before_submit'];assert draft['open']
 unknown=case=='cal-legacy-unit';edited=case=='cal-legacy-edit'
 assert draft['values']['timing']==('FPS' if unknown else 'Calibrated time')
 controls={x['name'] for x in report['controls']}
 assert {'menu.action.show_animation_options','dialog.apply'}<=controls
 if not unknown:assert {'dialog.timing','dialog.timing.choice.Calibrated time'}<=controls
 mode='reverse' if case=='cal-legacy-reverse' else 'ping_pong' if case=='cal-legacy-ping' else 'forward'
 assert draft['values']['mode']=={'reverse':'Reverse','ping_pong':'Ping-pong','forward':'Forward'}[mode]
 interval=1/60 if unknown else .05
 previous=before;ticks=0;last_time=None;trace=[]
 for frame in report['frames']:
  rendered,source=frame['rendered'],frame['source'];animation=rendered['animation']
  assert frame['events']==rendered['input_frame']['raw_inputs'] and not rendered['window_error']
  assert animation['fps']==60
  delta=animation['ticks']-ticks;assert delta in {0,1}
  if animation['playing'] or ticks:
   assert animation['timing']==('fps' if unknown else 'calibrated_time')
   assert animation['interval_seconds']==pytest.approx(interval)
   assert animation['mode']==mode and animation['axis']=='z'
  if delta:
   if last_time is not None:assert animation['last_tick']-last_time>=interval-1e-9
   last_time=animation['last_tick'];trace.append(source['position'][2])
  else:assert source['position']==previous['position']
  assert source['display_revision']==previous['display_revision']+delta
  assert source['position'][:2]==[0,0] and source['crosshair'][:2]==[3,2]
  assert source['workflow']==source['results']==[] and source['redo']==0
  assert source['undo']==(1 if edited and source['metadata_revision'] else 0)
  assert source['pixels_revision']==source['selection_revision']==0
  for key in ['selection','previous','roi','spacing','origin','zoom','plane_zoom','linked_zoom','frame_interval','time_unit']:
   assert source[key]==before[key]
  if not edited:assert source['shares_original'] and not source['dirty'] and source['metadata_revision']==0
  previous,ticks=source,animation['ticks']
 assert trace==EXPECTED[case]
 assert not report['animation']['playing'] and report['animation']['source_id'] is None
 if edited:assert report['animation']['reason']=='source_edited'
 if unknown:
  assert len(report['api'])==1
  api=report['api'][0];assert api['label']=='unknown_unit' and not api['accepted'] and api['error'] and api['before']==api['after']


@pytest.mark.parametrize('nt,nc',[(1,1),(1,3),(2,1),(2,3)])
@pytest.mark.parametrize('axis',['z','c','t'])
def test_calibration_axis_admission(nt,nc,axis,tmp_path):
 import numpy as np
 import imagejxx as ij
 from imagejxx.ui.app import Editor
 from imagejxx.ui.persistence import Preferences
 editor=Editor(preferences=Preferences(tmp_path/'prefs.json'))
 calibration=ij.SpatialCalibration(frame_interval=50,time_unit='ms')
 source=ij.Document(ij.Dataset(np.zeros((nt,nc,5,5,7,1),np.uint16),axes='TCZYXS',calibration=calibration),'Axis admission')
 editor.add_native_document(source)
 editor.action('animation',axis=axis,playing=False)
 before=source.snapshot();state=editor.render_headless()['animation']
 allowed=axis=='t' or (axis=='z' and nt==nc==1)
 if allowed:
  editor.action('animation',timing='calibrated_time',playing=False)
  result=editor.render_headless()['animation'];assert result['timing']=='calibrated_time' and result['interval_seconds']==pytest.approx(.05)
 else:
  with pytest.raises(ValueError,match='Calibrated playback requires'):
   editor.action('animation',timing='calibrated_time',playing=False)
  assert editor.render_headless()['animation']==state
 assert source.snapshot().shares_data(before) and source.undo_steps==source.redo_steps==0
 assert editor.workflow==editor.results==[]
