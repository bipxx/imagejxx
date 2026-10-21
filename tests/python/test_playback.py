"""Literal sequence oracles for the public native playback iterator."""
import pytest
import imagejxx as ij


@pytest.mark.parametrize('mode,expected', [
    ('forward', [4,7,1,4,7,1]),
    ('reverse', [7,4,1,7,4,1]),
    ('ping_pong', [4,7,4,1,4,7]),
])
def test_strided_inclusive_playback(mode, expected):
    sequence = ij.PlaybackSequence(10, first=1, last=8, step=3, mode=mode)
    assert (sequence.extent,sequence.first,sequence.last,sequence.step,sequence.count,sequence.last_selected,sequence.mode) == (10,1,8,3,3,7,mode)
    position, reverse = 1, False
    actual = []
    for _ in expected:
        position, reverse = sequence.advance(position, reverse)
        assert type(position) is int and type(reverse) is bool
        actual.append(position)
    assert actual == expected


@pytest.mark.parametrize('mode,reverse,expected', [
    ('forward', False, 1), ('forward', True, 1),
    ('reverse', False, 7), ('reverse', True, 7),
    ('ping_pong', False, 1), ('ping_pong', True, 7),
])
@pytest.mark.parametrize('position',[0,2,8,9])
def test_off_grid_entry(mode, reverse, expected, position):
    sequence = ij.PlaybackSequence(10,1,8,3,mode)
    assert sequence.advance(position,reverse)[0] == expected


def test_defaults_singleton_and_integer_limits():
    sequence = ij.PlaybackSequence(1)
    assert sequence.count == 1 and sequence.last_selected == 0
    assert sequence.advance(0) == (0,False)
    sequence = ij.PlaybackSequence(2**31-1,0,2**31-2,2**31-2)
    assert sequence.count == 2 and sequence.last_selected == 2**31-2
    assert sequence.advance(0) == (2**31-2,False)
    assert sequence.advance(2**31-2) == (0,False)
    sequence = ij.PlaybackSequence(4,1,3,2,'ping_pong')
    assert sequence.advance(1) == (3,False)
    assert sequence.advance(3) == (1,True)
    assert sequence.advance(1,True) == (3,False)


@pytest.mark.parametrize('arguments',[
    dict(extent=0),dict(extent=-1),dict(extent=4,first=-1),dict(extent=4,first=3,last=2),
    dict(extent=4,last=4),dict(extent=4,last=-1),dict(extent=4,step=0),
    dict(extent=4,step=-1),dict(extent=4,mode='unknown'),dict(extent=2**31),
])
def test_invalid_ranges(arguments):
    with pytest.raises(ValueError):ij.PlaybackSequence(**arguments)


@pytest.mark.parametrize('field',['extent','first','last','step'])
@pytest.mark.parametrize('value',[True,False,1.,'1'])
def test_exact_integer_contract(field,value):
    arguments=dict(extent=4);arguments[field]=value
    with pytest.raises(TypeError):ij.PlaybackSequence(**arguments)


@pytest.mark.parametrize('position',[-1,4])
def test_invalid_current_position(position):
    with pytest.raises(IndexError):ij.PlaybackSequence(4).advance(position)


@pytest.mark.parametrize('position',[True,1.,'1'])
def test_current_position_does_not_coerce(position):
    with pytest.raises(TypeError):ij.PlaybackSequence(4).advance(position)


def test_direction_is_boolean():
    with pytest.raises(TypeError):ij.PlaybackSequence(4).advance(0,1)
