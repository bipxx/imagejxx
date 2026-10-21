"""Literal unit conversions and public calibration pacing contract."""
import math
import pytest
import imagejxx as ij

@pytest.mark.parametrize('unit,interval,expected',[
 ('s',.05,.1),('sec',.05,.1),('second',.05,.1),('seconds',.05,.1),
 ('ms',25,.05),('millisecond',25,.05),('milliseconds',25,.05),
 ('us',25000,.05),('µs',25000,.05),('μs',25000,.05),
 ('microsecond',25000,.05),('microseconds',25000,.05),
 ('ns',25000000,.05),('nanosecond',25000000,.05),('nanoseconds',25000000,.05),
 ('min',2,240),('minute',2,240),('minutes',2,240),
 ('h',2,14400),('hr',2,14400),('hour',2,14400),('hours',2,14400),
 ('d',2,345600),('day',2,345600),('days',2,345600),
])
def test_calibrated_unit_conversion(unit,interval,expected):
 assert ij.playback_interval_seconds(60,timing='calibrated_time',frame_interval=interval,time_unit=unit,step=2)==pytest.approx(expected)

@pytest.mark.parametrize('fps',[1,8,15,60,1000])
def test_fps_ignores_calibration_and_stride(fps):
 assert ij.playback_interval_seconds(fps,frame_interval=math.nan,time_unit='frame',step=7)==1/fps

@pytest.mark.parametrize('kwargs',[
 dict(fps=0),dict(fps=-1),dict(fps=math.inf),dict(fps=math.nan),
 dict(timing='unknown'),dict(step=0),dict(step=-1),
 dict(timing='calibrated_time',time_unit='frame'),
 dict(timing='calibrated_time',frame_interval=0),
 dict(timing='calibrated_time',frame_interval=-1),
 dict(timing='calibrated_time',frame_interval=math.inf),
 dict(timing='calibrated_time',frame_interval=math.nan),
 dict(timing='calibrated_time',frame_interval=1e308,time_unit='days'),
 dict(timing='calibrated_time',frame_interval=5e-324,time_unit='ns'),
])
def test_invalid_timing_refused(kwargs):
 with pytest.raises(ValueError):ij.playback_interval_seconds(**(dict(fps=60)|kwargs))

@pytest.mark.parametrize('step',[True,False,1.0,'2',None])
def test_step_requires_exact_integer(step):
 with pytest.raises(TypeError):ij.playback_interval_seconds(60,step=step)

def test_small_interval_is_scaled_before_multiplying():
 value=ij.playback_interval_seconds(60,timing='calibrated_time',frame_interval=1e-320,time_unit='ns',step=2147483647)
 assert value>0 and math.isfinite(value)
