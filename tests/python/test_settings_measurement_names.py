"""Installed native name validation, typed particle fields and strict files."""
import json

import imagejxx as ij
import numpy as np
import pytest

NAMES = ['min', 'max', 'bbox', 'pixels']


@pytest.mark.parametrize('value', [[''], [' \t\r\n'], ['min', 'min'], ['a'*257],
                                  ['μ'*129], [str(i) for i in range(1025)],
                                  'min', [1], True])
def test_invalid_measurement_candidate_does_not_publish_any_setting(value):
    settings = ij.Settings()
    settings.update({'measurements': NAMES, 'measurement_precision': 3})
    before = settings.to_dict()
    with pytest.raises((ValueError, TypeError)):
        settings.update({'measurements': value, 'theme': 'light', 'measurement_precision': 7})
    assert settings.to_dict() == before


@pytest.mark.parametrize('names', [[], NAMES, ['min', 'MIN', ' min ', '\v', '\f', 'custom##literal'],
                                  ['a'*256], ['μ'*128], [str(i) for i in range(1024)]])
def test_order_and_exact_name_bytes_are_preserved(names):
    settings = ij.Settings()
    settings['measurements'] = names
    assert settings['measurements'] == names
    detached = settings['measurements']
    detached.append('detached')
    assert settings['measurements'] == names


def test_actual_native_particle_fields_selected_by_persisted_names(tmp_path):
    preferences = ij.PreferencesDocument()
    preferences.settings['measurements'] = NAMES
    path = tmp_path/'preferences.json'
    assert preferences.save(str(path))
    saved = path.read_bytes()
    loaded = ij.PreferencesDocument.load(str(path), False)
    assert loaded.settings['measurements'] == NAMES
    pixels = np.array([[0,0,0,0,0,0], [0,2,3,4,0,0], [0,5,6,7,0,0],
                       [0,0,0,0,0,0], [0,0,0,0,0,0]], np.uint8)
    before = pixels.tobytes()
    labels, count = ij.label(pixels, connectivity=8, return_num=True)
    assert count == 1
    expected = np.array([[0,0,0,0,0,0], [0,1,1,1,0,0], [0,1,1,1,0,0],
                         [0,0,0,0,0,0], [0,0,0,0,0,0]], np.int32)
    np.testing.assert_array_equal(labels, expected)
    rows = ij.region_properties(labels, intensity=pixels, spacing=(1, 1))
    assert len(rows) == 1
    assert [rows[0][key] for key in loaded.settings['measurements']] == [2., 7., (1,1,3,2), 6]
    assert rows[0]['mean'] == 4.5 and rows[0]['integrated_density'] == 27.
    assert pixels.tobytes() == before and path.read_bytes() == saved
    assert loaded.settings['measurements'] == NAMES


def test_strict_file_keeps_custom_names_and_opaque_compartments(tmp_path):
    names = ['min', 'MIN', ' min ', 'custom##literal', '\v', '\f', 'μ'*128]
    wire = {'format': 'imagejxx-preferences', 'version': 1,
            'values': {'measurements': names, 'cfg3_future': {'word': 2**64-1, 'text': 'λ'}},
            'extras': {'recent': [], 'cfg3_extra': [True, None, 'μ']}, 'cfg3_root': {'keep': 17}}
    preferences = ij.PreferencesDocument.parse(json.dumps(wire, ensure_ascii=False), False)
    path = tmp_path/'preferences.json'
    assert preferences.save(str(path))
    loaded = ij.PreferencesDocument.load(str(path), False)
    assert loaded.settings['measurements'] == names
    assert json.loads(loaded.unknown_values_json) == {'cfg3_future': wire['values']['cfg3_future']}
    assert json.loads(loaded.extras_json) == wire['extras']
    assert json.loads(loaded.unknown_fields_json) == {'cfg3_root': wire['cfg3_root']}
