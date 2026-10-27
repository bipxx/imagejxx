"""Offline independent checks of captured ImageJ data; no native/Java imports."""
import copy
import gzip
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("z_projection_reference_contract", ROOT/"scripts/capture_z_projection_reference.py")
capture = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(capture)
DATA = ROOT/"docs/parity/reviews/G034-imagej-z-projection-reference001"


def values(image):
    raw = bytes.fromhex(image["planes"][0]["raw_le_hex"])
    code = {8: "B", 16: "H", 24: "I", 32: "f"}[image["bit_depth"]]
    return struct.unpack("<"+str(len(raw)//struct.calcsize(code))+code, raw)


def words(image):
    raw = bytes.fromhex(image["planes"][0]["raw_le_hex"])
    return struct.unpack("<"+str(len(raw)//4)+"I", raw)


class ProjectionReference(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.contract = json.loads((DATA/"contract.json").read_bytes())
        compressed = (DATA/"observations.jsonl.gz").read_bytes()
        assert hashlib.sha256(compressed).hexdigest() == "a22ae856cac15d0f956f935dbe371136da455e428907590b3040d7c7996f7073"
        with gzip.open(DATA/"observations.jsonl.gz", "rb") as stream:
            cls.raw = stream.read(capture.LIMIT+1)
        assert hashlib.sha256(cls.raw).hexdigest() == "25e33266b96ec7932123345de8c60dc152adccc6f65a7ea617b763d697d9d8d0"
        cls.rows = capture.decode_rows(cls.raw)
        cls.by_id = {r["case_id"]: r for r in cls.rows}

    def output(self, case):
        return self.by_id[case]["output_api"]

    def test_exact_data_and_capture_source_identities(self):
        self.assertEqual(len(self.raw), self.contract["raw_bytes"])
        self.assertEqual(self.contract["raw_sha256"], self.contract["repeat_raw_sha256"])
        self.assertEqual([r["case_id"] for r in self.rows], self.contract["exact_ordered_case_ids"])
        for field in ("observer", "capture_tool"):
            item = self.contract[field]
            self.assertEqual(hashlib.sha256((ROOT/item["path"]).read_bytes()).hexdigest(), item["sha256"])
        self.assertEqual(self.contract["reference_inputs"]["class_count"], 468)
        self.assertEqual(self.contract["reference_inputs"]["runtime_count"], 581)
        self.assertIs(self.contract["acceptance_claim"], False)

    def test_source_state_and_explicit_negative_coverage(self):
        self.assertEqual(len(self.rows), 324)
        self.assertEqual(sum(r["status"] == "ok" for r in self.rows), 300)
        self.assertEqual(sum(r["status"] == "exception" for r in self.rows), 24)
        for row in self.rows:
            self.assertEqual(row["before"], row["after"], row["case_id"])
            self.assertIs(row["ui_executed"], False)
        self.assertEqual(self.output("hyper_rgb_c1.z4.alltrue.avg")["bit_depth"], 24)

    def test_scalar_output_types_and_integer_rounding(self):
        for kind, depth in (("u8", 8), ("u16", 16), ("f32", 32), ("rgb", 24)):
            for method in ("avg", "max", "min", "sum", "sd", "median"):
                expected = 32 if kind != "rgb" and (method in ("sum", "sd") or kind == "u16" and method == "median") else depth
                self.assertEqual(self.output(f"scalar.{kind}.full.{method}")["bit_depth"], expected)
        self.assertEqual(values(self.output("scalar.u8.range_even.avg")), (24, 26, 28, 30))
        self.assertEqual(values(self.output("scalar.u16.range_even.avg")), (7503, 7504, 7507, 7512))
        self.assertEqual(values(self.output("scalar.u16.signed16.sum"))[0], -1)
        self.assertEqual(values(self.output("scalar.u16.signed16.median"))[0], 0)
        self.assertEqual(self.output("scalar.u16.signed16.avg")["calibration"]["density_function"], 0)
        for method in ("sum", "sd", "median"):
            self.assertEqual(self.output("scalar.u16.signed16."+method)["calibration"]["density_function"], 20)

    def test_sample_sd_and_float32_accumulation_are_not_native_casts(self):
        sd = struct.unpack("<f", struct.pack("<f", math.sqrt(391.5)))[0]
        self.assertEqual(values(self.output("scalar.u8.full.sd")), (sd,)*4)
        self.assertEqual(words(self.output("scalar.f32.rounding.sum"))[0:2], (0, 0x7f800000))
        self.assertEqual(words(self.output("scalar.f32.rounding.avg"))[0], 0x3eaaaaab)
        self.assertEqual(values(self.output("scalar.f32.range_even.median"))[0], -.75)

    def test_nonfinite_results_are_recorded_as_words(self):
        self.assertEqual(words(self.output("scalar.f32.all_nan.max")), (0xff7fffff,)*4)
        self.assertEqual(words(self.output("scalar.f32.all_nan.min")), (0x7f7fffff,)*4)
        self.assertEqual(words(self.output("scalar.f32.all_nan.sd")), (0,)*4)
        self.assertEqual(words(self.output("scalar.f32.nonfinite.max"))[1:3], (0x7f800000, 0xff7fffff))
        self.assertEqual(words(self.output("scalar.f32.nonfinite.min"))[1:3], (0x7f7fffff, 0xff800000))

    def test_hyperstack_time_dispatch_and_calibration_copy(self):
        self.assertEqual(self.output("hyper.f32.z4.allfalse.avg")["dimensions_xyczt"], [2, 2, 2, 1, 1])
        self.assertEqual(self.output("hyper.f32.z4.alltrue.avg")["dimensions_xyczt"], [2, 2, 2, 1, 3])
        self.assertEqual(self.output("hyper.f32.z1.alltrue.avg")["dimensions_xyczt"], [2, 2, 2, 1, 1])
        self.assertEqual(values(self.output("hyper.f32.z1.alltrue.avg"))[0], 125)
        row = self.by_id["scalar.u8.range_even.avg"]
        for key in ("spacing_f64", "origin_indices_f64", "unit", "frame_interval_f64", "time_unit"):
            self.assertEqual(row["before"]["calibration"][key], row["output_api"]["calibration"][key])
        self.assertIsNone(row["output_api"]["selection"])
        self.assertIsNone(row["output_api"]["note"])

    def test_channel_luts_ranges_and_positioned_overlays(self):
        row = self.by_id["hyper.f32.z4.alltrue.avg"]
        red = hashlib.sha256(bytes(v for i in range(256) for v in (i, 0, 0))).hexdigest()
        green = hashlib.sha256(bytes(v for i in range(256) for v in (0, i, 0))).hexdigest()
        self.assertEqual([v["rgb256_sha256"] for v in row["output_api"]["luts"]], [red, green])
        self.assertEqual(row["output_api"]["luts"], row["before"]["luts"])
        summed = self.output("hyper.f32.z4.alltrue.sum")
        self.assertEqual([v["rgb256_sha256"] for v in summed["luts"]], [red, green])
        self.assertEqual(summed["luts"][0]["min_f64"], "401e000000000000")
        self.assertEqual(summed["luts"][0]["max_f64"], "4021400000000000")
        self.assertEqual([v["name"] for v in row["output_api"]["overlay"]], ["global", "in-t1", "in-t2", "in-t3", "wild-z"])
        self.assertEqual(row["output_api"]["overlay"][2]["czt"], [2, 1, 2])
        current = self.output("hyper.f32.z4.allfalse.avg")
        self.assertEqual([v["name"] for v in current["overlay"]], ["global", "in-t2", "wild-c-out-z"])

    def test_group_api_and_explicit_plugin_calibration_steps_are_distinct(self):
        z = self.by_id["group.u8.z.g3.avg"]
        t = self.by_id["group.u8.time.g3.avg"]
        self.assertEqual(z["output_api"]["dimensions_xyczt"], [4, 1, 1, 2, 1])
        self.assertEqual(t["output_api"]["dimensions_xyczt"], [4, 1, 1, 1, 2])
        self.assertEqual(z["output_api"]["calibration"]["spacing_f64"], ["3ff0000000000000"]*3)
        post = z["output_after_plugin_calibration_steps"]["calibration"]
        self.assertEqual(post["spacing_f64"][2], "4022000000000000")
        self.assertEqual(post["origin_indices_f64"], z["before"]["calibration"]["origin_indices_f64"])
        self.assertEqual(t["output_after_plugin_calibration_steps"]["calibration"]["frame_interval_f64"], "4004000000000000")
        self.assertIs(z["calibration_steps_reproduced_from_source"], True)

    def test_reader_refuses_missing_cases_unexpected_errors_and_forged_rejections(self):
        for mutation in ("missing", "exception", "rejection", "scope", "words"):
            rows = copy.deepcopy(self.rows)
            if mutation == "missing": rows.pop()
            elif mutation == "exception": rows[0]["status"] = "exception"
            elif mutation == "rejection": next(r for r in rows if r["status"] == "exception")["message"] = "different failure"
            elif mutation == "scope": rows[0]["ui_executed"] = True
            else: rows[0]["output_api"]["planes"][0]["raw_le_hex"] = "00"
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                capture.decode_rows(b"\n".join(json.dumps(r).encode() for r in rows))
        with self.assertRaises(ValueError): capture.decode_rows(b"x"*(capture.LIMIT+1))
        with self.assertRaises(ValueError): capture.decode_rows(self.raw.replace(b'{"case_id":', b'{"case_id":"duplicate","case_id":', 1))

    def test_lossless_retention_uses_fresh_targets(self):
        with tempfile.TemporaryDirectory(prefix="imagejxx-projection-retention-") as directory:
            source = Path(directory)/"source"; source.write_bytes(bytes(range(256))*40)
            target = Path(directory)/"capture.gz"
            result = capture.compress_checked(source, target)
            self.assertEqual(gzip.decompress(target.read_bytes()), source.read_bytes())
            self.assertEqual(result["raw"]["sha256"], hashlib.sha256(source.read_bytes()).hexdigest())
            with self.assertRaises(FileExistsError): capture.compress_checked(source, target)


if __name__ == "__main__":
    unittest.main()
