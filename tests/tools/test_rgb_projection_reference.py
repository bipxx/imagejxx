"""Offline checks of supplemental ImageJ observations; no JVM or target imports."""
import gzip
import json
from pathlib import Path
import runpy
import unittest

ROOT=Path(__file__).resolve().parents[2]
FIXTURE=runpy.run_path(str(ROOT/'scripts/generate_imagej_rgb_projection_fixture.py'))
CAPTURE=runpy.run_path(str(ROOT/'scripts/capture_rgb_projection_reference.py'))


def words(row):
    result=[]
    for plane in row['output_api']['planes']:
        raw=bytes.fromhex(plane['raw_le_hex'])
        result.extend(int.from_bytes(raw[i:i+4],'little') for i in range(0,len(raw),4))
    return result


class RGBReference(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rows=FIXTURE['supplemental_rows']()
        cls.by_id={r['case_id']:r for r in cls.rows}

    def test_exact_matrix_and_source_preservation(self):
        self.assertEqual([r['case_id'] for r in self.rows],CAPTURE['expected_ids']())
        self.assertEqual(len(self.rows),124)
        for row in self.rows:
            self.assertEqual(row['before'],row['after'])
            self.assertFalse(row['ui_executed'])

    def test_zero_and_stationary_sd(self):
        for row in self.rows:
            if row['profile']=='zero' or (row['profile']=='stationary' and row['method']=='sd'):
                self.assertEqual(set(words(row)),{0xff000000},row['case_id'])

    def test_constant_conversion_rules(self):
        for row in self.rows:
            if row['profile']!='constant':
                continue
            if row['method']=='sd' or (row['method']=='sum' and row['before']['clip_when_summing']!='true'):
                expected=0xff000000
            elif row['method']=='sum':
                n=row['bounds_one_based'][1]-row['bounds_one_based'][0]+1
                r,g,b=(min(255,n*v) for v in (17,81,203))
                expected=0xff000000|(r<<16)|(g<<8)|b
            else:
                expected=0xff1151cb
            self.assertEqual(set(words(row)),{expected},row['case_id'])

    def test_clip_only_affects_sum(self):
        for row in self.rows:
            if row['method']=='sum' or not row['case_id'].endswith('clipfalse'):
                continue
            other=self.by_id[row['case_id'][:-5]+'true']
            self.assertEqual(words(row),words(other),row['case_id'])

    def test_varied_results_exercise_independent_channels(self):
        for method in ('sum','sd'):
            row=self.by_id[f'rgb-extra.varied.full.{method}.clipfalse']
            self.assertGreater(len(set(words(row))),20)
            self.assertTrue(any(((v>>16)&255)!=((v>>8)&255) for v in words(row)))

    def test_immutable_fixture_transcription(self):
        self.assertEqual(FIXTURE['TARGET'].read_bytes(),FIXTURE['render']())

    def test_modified_order_and_source_rejected(self):
        raw=gzip.decompress((FIXTURE['SUPPLEMENT']/'observations.jsonl.gz').read_bytes())
        rows=[json.loads(line) for line in raw.splitlines()]
        def encoded():return ('\n'.join(json.dumps(r) for r in rows)+'\n').encode()
        rows[0]['case_id']='different'
        with self.assertRaises(ValueError):CAPTURE['decode_rows'](encoded())
        rows[0]['case_id']=self.rows[0]['case_id']
        rows[0]['after']['title']='changed'
        with self.assertRaises(ValueError):CAPTURE['decode_rows'](encoded())


if __name__=='__main__':
    unittest.main()
