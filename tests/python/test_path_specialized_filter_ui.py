"""Independent pinned sample/payload/history checks for specialized path UI."""
import gzip
import json
import math
import os
from pathlib import Path
import struct
import pytest
from test_path_filter_ui import little_words, retained, PATH

ROOT = Path(__file__).resolve().parents[2]
REFERENCE = json.loads(gzip.decompress((ROOT/'tests/fixtures/path-rank-targets-imagej154r.json.gz').read_bytes()))
ROWS = {row['id']: row for row in REFERENCE['cases']}
DISPLAY_REFERENCE = json.loads(gzip.decompress((ROOT/'tests/fixtures/path-top-hat-display-imagej154r.json.gz').read_bytes()))
CASES = [('specialized-despeckle','rank-068-despeckle-f32-triangle')]
CASES += [(f'specialized-outliers-{which}-{lut}',f'rank-{index:03}-outliers-f32-triangle')
          for (which,lut),index in zip([(w,l) for w in ('bright','dark') for l in ('grays','inverted')],(116,122,128,134))]
CASES += [(f'specialized-top-hat-{background}-{subtract}-{lut}',f'rank-{index:03}-top_hat-f32-triangle')
          for (background,subtract,lut),index in zip([(b,s,l) for b in ('dark','light')
                    for s in ('subtract','no-subtract') for l in ('grays','inverted')],
                    (142,151,160,169,178,187,196,205))]
CASES += [(f'specialized-remove-nans-{policy}',f'rank-{index:03}-remove_nans-f32-triangle')
          for policy,index in [('finite',212),('quiet',220),('ieee',228)]]
assert len(CASES) == len({case for case,_ in CASES}) == 16


def double_storage(words):
    result = []
    for text in words:
        word = int(text,16)
        if word & 0x7f800000 == 0x7f800000:
            bits = ((word & 0x80000000)<<32) | 0x7ff0000000000000 | ((word & 0x007fffff)<<29)
            result.append(bits.to_bytes(8,'little').hex())
        else:
            result.append(struct.pack('<d',struct.unpack('<f',word.to_bytes(4,'little'))[0]).hex())
    return ''.join(result)


def samples(state, words):
    assert state['shape'] == [1,1,1,17,23,1]
    assert len(state['planes']) == 1 and state['planes'][0]['position'] == [0,0,0]
    plane = state['planes'][0]
    assert plane['declared_f32_hex'] == little_words(words)
    assert plane['storage_f64_hex'] == double_storage(words)


@pytest.mark.parametrize('case_id,reference_id',CASES,ids=[case for case,_ in CASES])
@pytest.mark.parametrize('context',['imgui_cpp','imgui_python'])
def test_actual_specialized_path_filter_options_payloads_and_history(case_id,reference_id,context,tmp_path):
    if context == 'imgui_cpp':
        path=Path(json.loads(os.environ['IMAGEJXX_PATH_FILTER_CPP_REPORTS'])[case_id])
        assert path.stat().st_size < 16*1024*1024
        row=json.loads(path.read_bytes())
    else:
        from imagejxx import _desktop
        editor=_desktop.Editor(preferences_path=str(tmp_path/'preferences.json'))
        row=editor._path_filter_ui_case(case_id,str(tmp_path/'capture'))
        if isinstance(row,str):row=json.loads(row)
    assert row['contract']=='p13i.path-filter.ui.001' and row['state']=='observed'
    assert row['error']=='' and row['acceptance_claim'] is False
    assert row['case_id']==case_id and row['context']==context
    reference=ROWS[reference_id]
    request=reference['request']; observed=reference['observation']
    original=observed['input']['sample_words']; filtered=observed['after_mask_reset']['sample_words']
    despeckle=case_id=='specialized-despeckle'; noop=case_id=='specialized-remove-nans-finite'
    outliers='outliers' in case_id; top='top-hat' in case_id
    pattern='ieee' if case_id.endswith('-ieee') else 'quiet' if case_id.endswith('-quiet') else 'finite'
    operation='despeckle_imagej' if despeckle else 'remove_outliers_imagej' if outliers else 'top_hat_imagej' if top else 'remove_nans_imagej'
    preset='despeckle' if despeckle else 'remove_outliers' if outliers else 'top_hat' if top else 'remove_nans'
    inverted=request['inverted_lut']
    policy={'finite':'FiniteOnly','quiet':'PreserveQuietNaNFloat32','ieee':'PreserveIEEEFloat32'}[pattern]
    expected_recipe={'operation':operation,'preset':preset,'radius':1 if despeckle else 2,
        'which_outliers':('bright' if request['which_outliers']==0 else 'dark') if outliers else None,
        'threshold':3.5 if outliers else None,'light_background':request['light_background'] if top else None,
        'dont_subtract':request['dont_subtract'] if top else None,'inverted_lut':inverted,
        'input_pattern':pattern,'sample_policy':policy,'destination':'in_place','roi_policy':'active_selection'}
    assert row['recipe']==expected_recipe
    required={'before','source_saved','drawn','lut_selected','applied','filtered_saved','filtered_reopened'}
    if not despeckle:required|={'configured','preview'}
    if not noop:required|={'undone','redone'}
    states,obs=row['states'],row['observations']
    assert set(states)==set(obs)==required
    assert row['source_document_id']==row['output_document_id']==states['before']['document_id']
    baseline=states['lut_selected']
    assert states['drawn']['selection']['path']==PATH
    for name,state in states.items():
        samples(state,filtered if name in {'applied','redone','filtered_saved','filtered_reopened'} else original)
        assert retained(state)==retained(states['before']),name
        if name not in {'before','source_saved'}:
            assert state['selection']==states['drawn']['selection'],name
            assert state['previous']==states['drawn']['previous'],name
    if not despeckle:
        for name in ('configured','preview'):
            assert states[name]['data_identity']==baseline['data_identity']
            assert states[name]['undo']==baseline['undo']
            assert states[name]['display']==baseline['display']
        samples(obs['preview']['preview'],filtered)
        assert obs['configured']['rank_panel']['captured_inverted_lut']==(inverted if outliers or top else None)
        assert obs['configured']['rank_panel']['radius']==2
        if top:
            for phase,name in [('preview','preview'),('apply','applied')]:
                reference_display=next(item['observation'] for item in DISPLAY_REFERENCE['cases']
                    if item['request']['phase']==phase and all(item['request'][key]==request[key]
                       for key in ['light_background','dont_subtract','inverted_lut']))
                assert obs[name]['ordinary_pixels']['range']==reference_display['final_range'],(phase,case_id)
                pixels=obs[name]['ordinary_pixels']
                # The signed-zero input at (0,0) is outside P and unrelated
                # overlays. Its RGB verifies the range reached the renderer,
                # independently of diagnostics or changed filter pixels.
                image=pixels['readback']['rendered_rgb']
                assert image['dtype']=='uint8' and image['shape']==[17,23,3]
                low,high=[struct.unpack('<f',struct.pack('<f',value))[0] for value in reference_display['final_range']]
                index=max(0,min(255,math.floor((-low)*(255.0/(high-low))+.5)))
                if inverted:index=255-index
                assert image['pixels'][:3]==[index,index,index],(phase,case_id)
                transient=phase=='preview' and not request['dont_subtract']
                assert pixels['contrast_source']==('top_hat_preview' if transient else 'live')
                if transient:
                    effects=pixels['preview_display_effects']
                    assert len(effects)==1 and effects[0]['position']==[0,0,0]
                    assert effects[0]['range']==reference_display['final_range']
                    assert effects[0]['generation']==obs[name]['preview']['request_generation']
                else:assert pixels['preview_display_effects']==[]


    if noop:
        for name in ('configured','preview','applied'):
            for field in ('data_identity','revisions','dirty','undo','redo','workflow','history','display'):
                assert states[name][field]==baseline[field],(name,field)
    else:
        assert states['applied']['undo']==baseline['undo']+1
        assert states['undone']['undo']==baseline['undo']
        assert states['redone']['undo']==baseline['undo']+1
        record=states['applied']['workflow'][-1]
        assert len(states['applied']['workflow'])==len(baseline['workflow'])+1
        assert record['operation']==('rank_filter_imagej' if despeckle else operation)
        options={'method':'median','radius':1.0} if despeckle else {'radius':2.0}
        if outliers:options.update(which=expected_recipe['which_outliers'],threshold=3.5,inverted_lut=inverted)
        if top:options.update(light_background=request['light_background'],dont_subtract=request['dont_subtract'],inverted_lut=inverted)
        assert record['options']==options
        assert record['target']['roi_policy']=='active_selection'
        assert record['target']['destination']=='in_place'
        for restored,expected in [('undone',baseline),('redone',states['applied'])]:
            actual_display=dict(states[restored]['display']);expected_display=dict(expected['display'])
            actual_revision=actual_display.pop('revision');expected_revision=expected_display.pop('revision')
            assert actual_display==expected_display
            assert actual_revision >= expected_revision
        assert states['redone']['display']['revision'] >= states['undone']['display']['revision']
    selectors={control['selector'] for control in row['controls']}
    assert {'host.menu.Image','host.menu.Image.Lookup_table',
        'host.menu.Image.Lookup_table.'+('Inverted' if inverted else 'Grays')} <= selectors
    if despeckle:
        assert 'host.menu.action.despeckle_imagej' in selectors
        assert 'rank.preview' not in selectors and 'rank.radius' not in selectors
    else:
        assert {'host.menu.command.'+operation,'rank.radius','rank.preview','rank.ok'} <= selectors
    if outliers:assert {'rank.threshold','rank.which','rank.outlier.'+expected_recipe['which_outliers']} <= selectors
    if top:
        assert ('rank.light_background' in selectors)==request['light_background']
        assert ('rank.dont_subtract' in selectors)==request['dont_subtract']
    assert set(row['files'])=={'source','filtered'}
    for file in row['files'].values():assert len(bytes.fromhex(file['hex']))==file['bytes']
    assert row['final_usage']['reserved_bytes']==0
    assert 0 < len(row['frames']) <= 750
    for index,frame in enumerate(row['frames'],1):
        assert frame['ordinal']==index and frame['events']==frame['rendered']['input_frame']['raw_inputs']
    for control in row['controls']:
        assert control['hovered_id']==control['item']['id'] != 0
        assert control['item']['enabled'] is True
    assert [hit['image'] for hit in row['canvas_hits']]==[[2.2,1.2],[9.2,1.2],[2.2,8.2]]
