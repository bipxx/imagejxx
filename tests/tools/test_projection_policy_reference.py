"""Offline source/data and policy checks; no JVM or imagejxx execution."""
import gzip
import json
from pathlib import Path
import runpy
import struct
import unittest

ROOT=Path(__file__).resolve().parents[2]
FIXTURE=runpy.run_path(str(ROOT/'scripts/generate_projection_policy_fixture.py'))
CAPTURE=runpy.run_path(str(ROOT/'scripts/capture_projection_policy_reference.py'))


class PolicyReference(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rows=FIXTURE['rows']()
        cls.by_id={r['case_id']:r for r in cls.rows}

    def test_exact_case_set_and_source_preservation(self):
        self.assertEqual([r['case_id'] for r in self.rows],CAPTURE['expected_ids']())
        self.assertEqual(len(self.rows),252)
        for row in self.rows:
            self.assertFalse(row['ui_executed'])
            self.assertEqual(row['before'],row['after'])
            self.assertEqual(row['table_entries_used'],row['after_table_entries_used'])

    def test_channel_only_output_axes(self):
        for row in self.rows:
            if row['family']!='channels':continue
            x,y,c,z,t=row['before']['dimensions_xyczt']
            self.assertEqual((c,z,t),(6,1,1))
            first,last=row['bounds_one_based'];count=last-first+1
            output_c=count//(row['group_size'] or count)
            self.assertEqual(row['output_api']['dimensions_xyczt'],[x,y,output_c,1,1])

    def test_density_output_types_and_intent(self):
        for row in self.rows:
            if row['family']!='density':continue
            before,out=row['before'],row['output_api']
            floating=row['method'] in ('sum','sd') or (row['kind']=='u16' and row['method']=='median')
            self.assertEqual(out['bit_depth'],32 if floating else before['bit_depth'])
            self.assertEqual(out['calibration']['density_function'],20 if floating else before['calibration']['density_function'])
            self.assertEqual(out['calibration']['value_unit'],before['calibration']['value_unit'])

    def test_nonmedian_uses_raw_codes(self):
        observed={}
        for row in self.rows:
            if row['family']!='density' or row['method']=='median':continue
            key=(row['kind'],tuple(row['bounds_one_based']),row['method'])
            raw=[p['raw_le_hex'] for p in row['output_api']['planes']]
            if key in observed:self.assertEqual(raw,observed[key],row['case_id'])
            observed[key]=raw

    def test_calibrated_median_analytic(self):
        row=self.by_id['policy.density.u16.linear.full.median']
        raw=bytes.fromhex(row['output_api']['planes'][0]['raw_le_hex'])
        self.assertEqual(struct.unpack('<f',raw[:4])[0],352.0)
        custom=self.by_id['policy.density.u16.custom_signed.full.sum']
        self.assertFalse(custom['signed16'])
        self.assertTrue(custom['table_entries_used'])
        byte=self.by_id['policy.density.u8.custom_range.full.median']
        self.assertEqual(byte['output_api']['planes'][0]['raw_le_hex'],'00fffff8')

    def test_exact_fixture_transcription(self):
        self.assertEqual(FIXTURE['TARGET'].read_bytes(),FIXTURE['render']())

    def test_changed_membership_and_table_rejected(self):
        raw=gzip.decompress((FIXTURE['BASE']/'observations.jsonl.gz').read_bytes())
        rows=[json.loads(line) for line in raw.splitlines()]
        rows[0]['status']='failed'
        with self.assertRaises(ValueError):CAPTURE['decode_rows'](('\n'.join(json.dumps(r) for r in rows)+'\n').encode())
        rows[0]['status']='ok'
        row=next(r for r in rows if r['table_entries_used'])
        row['after_table_entries_used'][0]['bits']='00000001'
        with self.assertRaises(ValueError):CAPTURE['decode_rows'](('\n'.join(json.dumps(r) for r in rows)+'\n').encode())


if __name__=='__main__':unittest.main()
