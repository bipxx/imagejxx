"""Actual timing-choice input and independent monotonic-interval assertions."""
import json
import os
from pathlib import Path
import pytest
from test_animation_ui import samples

CASES=['cal-s','cal-ms','cal-us','cal-step','cal-fps','cal-invalid','cal-edit','cal-unit']

@pytest.mark.parametrize('context',['imgui_cpp','imgui_python'])
@pytest.mark.parametrize('case',CASES)
def test_actual_calibrated_time_controls(case,context,tmp_path):
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
 assert not report['lifecycle']['open']
 assert report['shape']==[5,3,4,5,7,1]
 before,after=report['before'],report['after']
 assert before['samples']==after['samples']==samples(case,report['shape'])
 draft=report['options_before_submit'];assert draft['open']
 assert draft['values']['timing']==('FPS' if case=='cal-unit' else 'Calibrated time')
 controls={x['name'] for x in report['controls']}
 assert {'menu.action.show_animation_options','dialog.apply'}<=controls
 if case!='cal-unit':assert {'dialog.timing','dialog.timing.choice.Calibrated time'}<=controls
 interval=1/60 if case=='cal-unit' else .05
 previous=before;ticks=0;last_time=None;trace=[]
 for frame in report['frames']:
  rendered,source=frame['rendered'],frame['source'];animation=rendered['animation']
  assert frame['events']==rendered['input_frame']['raw_inputs'] and not rendered['window_error']
  delta=animation['ticks']-ticks;assert delta in {0,1}
  if animation['playing'] or ticks:
   assert animation['timing']==('fps' if case=='cal-unit' else 'calibrated_time')
   assert animation['interval_seconds']==pytest.approx(interval)
  if delta:
   if last_time is not None:assert animation['last_tick']-last_time>=interval-1e-9
   last_time=animation['last_tick'];trace.append(source['position'][0])
  else:assert source['position']==previous['position']
  assert source['display_revision']==previous['display_revision']+delta
  assert source['position'][1:]==[1,1]
  assert source['workflow']==source['results']==[] and source['undo']==(1 if case=='cal-edit' and source['metadata_revision'] else 0)
  assert source['pixels_revision']==source['selection_revision']==0
  for key in ['selection','previous','roi','spacing','origin','zoom','plane_zoom','linked_zoom','frame_interval','time_unit']:
   assert source[key]==before[key]
  if case!='cal-edit':assert source['shares_original'] and not source['dirty'] and source['metadata_revision']==0
  if animation['playing']:
   assert animation['axis']=='t'
   for pane in rendered['orthogonal_planes']:assert pane['current_tcz']==source['position']
  previous,ticks=source,animation['ticks']
 expected=[0,2,4,0,2,4,0,2] if case=='cal-step' else [2,3,4,0,1,2,3,4]
 assert trace==expected[:3] if case=='cal-edit' else trace==expected
 assert not report['animation']['playing'] and report['animation']['source_id'] is None
 if case=='cal-edit':assert report['animation']['reason']=='source_edited'
 for api in report['api']:
  if api['label'] in {'unknown_unit','timing_unknown','timing_type','timing_axis'}:
   assert not api['accepted'] and api['error'] and api['before']==api['after']
 if case=='cal-fps':assert report['animation']['fps']==15
