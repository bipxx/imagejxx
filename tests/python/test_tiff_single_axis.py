"""Narrow acceptance of untouched channel/time-only FileSaver axes."""
from pathlib import Path
import hashlib,json
import numpy as np
import pytest
from imagejxx import scientific_io as io

FIXTURES=Path(__file__).parents[1]/'fixtures/scientific-tiff-single-axis'
CASES=json.loads((FIXTURES/'SOURCE.json').read_bytes())['files']
@pytest.mark.parametrize('case',CASES,ids=lambda c:c['file'])
def test_single_axis_omission_and_mixed_or_false_refusal(case):
    path=FIXTURES/case['file'];assert hashlib.sha256(path.read_bytes()).hexdigest()==case['sha256']
    if not case['valid']:
        events=[]
        with pytest.raises(ValueError):io.read_image_series(path,progress=lambda *v:events.append(v),checkpoint=lambda:events.append('checkpoint'))
        assert not events
        return
    result=io.read_image_series(path);assert result.dataset.shape==tuple(case['shape'])
    t,c,z,y,x=np.indices(case['shape'][:-1]);np.testing.assert_array_equal(result.dataset.numpy('TCZYX'),10000*t+1000*c+100*z+10*y+x)
    assert result.metadata['defaulted_fields']==case['defaulted_fields']
    assert 'hyperstack=' not in result.metadata['source_description']
    assert tuple(result.dataset.calibration.spacing)==(.5,1.25,2.5)
