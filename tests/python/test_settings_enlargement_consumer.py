"""Paired enlargement defaults at installed Python/native boundaries."""
import json

import imagejxx as ij
import numpy as np
import pytest


@pytest.mark.parametrize('distance', [-1., 1000000.5, float('nan'), float('inf'),
                                     -float('inf'), True, '2', None])
def test_invalid_distance_does_not_publish_either_setting(distance):
    settings = ij.Settings()
    settings.update({'selection_enlargement_distance': 3., 'selection_enlargement_units': 'calibrated'})
    before = settings.to_dict()
    with pytest.raises((ValueError, TypeError, OverflowError)):
        settings.update({'selection_enlargement_distance': distance, 'selection_enlargement_units': 'pixels'})
    assert settings.to_dict() == before


@pytest.mark.parametrize('units', ['unknown', '', True, ['pixels']])
def test_invalid_units_does_not_publish_distance(units):
    settings = ij.Settings()
    before = settings.to_dict()
    with pytest.raises((ValueError, TypeError)):
        settings.update({'selection_enlargement_distance': 2., 'selection_enlargement_units': units})
    assert settings.to_dict() == before


@pytest.mark.parametrize('scientific', [False, True])
@pytest.mark.parametrize('magnitude', [2., 3.])
@pytest.mark.parametrize('units', ['pixels', 'calibrated'])
@pytest.mark.parametrize('sign', [-1, 1])
def test_stored_pixel_magnitude_consumes_exact_unit_intent(scientific, magnitude, units, sign):
    shape = (2, 2, 2, 17, 17, 1) if scientific else (1, 1, 1, 17, 17, 1)
    pixels = np.fromfunction(lambda t, c, z, y, x, s: (19*t+31*c+47*z+3*y+x) % 256,
                            shape, dtype=int).astype(np.uint8)
    calibration = ij.SpatialCalibration(spacing=(.5, 1.5, 2), unit='um')
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(pixels, axes='TCZYXS', calibration=calibration).observe_reads(reads))
    initial = ij.Selection('rectangle', [(4, 4), (12, 11)])
    owner.select(initial)
    display = owner.snapshot().display
    display.t = display.c = int(scientific)
    display.crosshair = [8, 8, int(scientific)]
    owner.set_display(display)
    settings = ij.Settings()
    settings.update({'selection_enlargement_distance': magnitude, 'selection_enlargement_units': units})
    stored = settings.to_dict()
    distance = sign*settings['selection_enlargement_distance']*(.5 if units == 'calibrated' else 1.)
    request = ij.SelectionEnlargementRequest(distance, getattr(ij.SelectionEnlargementUnits, units.upper()),
                                            geometry=ij.SelectionEnlargementGeometry.FULL_REGION_V2)
    before = owner.snapshot()
    command = ij.resolve_selection_enlargement_command(before, request)
    assert command.estimate.pixel_distance == sign*magnitude
    delta = int(sign*magnitude)
    expected = np.zeros((17, 17), np.uint8)
    expected[4-delta:11+delta, 4-delta:12+delta] = 255
    np.testing.assert_array_equal(command.result.mask((17, 17)), expected)
    assert reads.counts().plane_reads == reads.counts().region_reads == 0
    owner.commit(ij.prepare_selection_enlargement(command))
    after = owner.snapshot()
    np.testing.assert_array_equal(after.selection.mask((17, 17)), expected)
    assert after.previous_selection == initial and after.dataset.shares_pixels(before.dataset)
    assert after.dirty == before.dirty and owner.undo_steps == owner.redo_steps == 0
    assert (after.display.t, after.display.c, after.display.crosshair) == (display.t, display.c, display.crosshair)
    assert settings.to_dict() == stored
    assert reads.counts().plane_reads == reads.counts().region_reads == 0
    np.testing.assert_array_equal(after.dataset.numpy('TCZYXS'), pixels)


def test_strict_native_preferences_preserve_opaque_data_and_pixel_magnitude(tmp_path):
    wire = {'format': 'imagejxx-preferences', 'version': 1,
            'values': {'selection_enlargement_distance': 3., 'selection_enlargement_units': 'calibrated',
                       'cfg2_future': {'word': 2**64-1, 'text': 'λ'}},
            'extras': {'recent': [], 'cfg2_extra': [True, None, 'μ']}, 'cfg2_root': {'keep': 17}}
    preferences = ij.PreferencesDocument.parse(json.dumps(wire, ensure_ascii=False), False)
    path = tmp_path/'preferences.json'
    assert preferences.save(str(path))
    saved = path.read_bytes()
    loaded = ij.PreferencesDocument.load(str(path), False)
    assert loaded.settings['selection_enlargement_distance'] == 3.
    assert loaded.settings['selection_enlargement_units'] == 'calibrated'
    assert json.loads(loaded.unknown_values_json) == {'cfg2_future': wire['values']['cfg2_future']}
    assert json.loads(loaded.extras_json) == wire['extras']
    assert json.loads(loaded.unknown_fields_json) == {'cfg2_root': wire['cfg2_root']}
    assert path.read_bytes() == saved
