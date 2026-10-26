"""Controls for the independent C2 sampler oracle; no renderer or product."""
import importlib.util
from pathlib import Path
import unittest


SOURCE = Path(__file__).resolve().parents[1] / "python/settings_appearance_expectations.py"
SPEC = importlib.util.spec_from_file_location("appearance_expectations_under_test", SOURCE)
oracle = importlib.util.module_from_spec(SPEC)
exec(compile(SOURCE.read_bytes(), str(SOURCE), "exec", dont_inherit=True), oracle.__dict__)


class LiteralSampling(unittest.TestCase):
    def test_repeat_edges_are_distinct_from_clamp_and_interior_is_identical(self):
        for u in (0, 1):
            self.assertEqual(oracle.expected_gl_sample(oracle.STEP_WORDS, 2, 4, u, .5,
                                                      "linear", "repeat", "repeat"), 64)
        self.assertEqual(oracle.expected_gl_sample(oracle.STEP_WORDS, 2, 4, 1, .5,
                                                  "nearest", "repeat", "repeat"), 0)
        for u in (.25, .375, .625, .75):
            for mode in ("nearest", "linear"):
                self.assertEqual(oracle.expected_gl_sample(oracle.STEP_WORDS, 2, 4, u, .5, mode),
                                 oracle.expected_gl_sample(oracle.STEP_WORDS, 2, 4, u, .5,
                                                           mode, "repeat", "repeat"))

    def test_step_endpoints_clamp_and_interior_distinguish_filters(self):
        for mode in ("nearest", "linear"):
            self.assertEqual(oracle.expected_gl_sample(oracle.STEP_WORDS, 2, 4, 0, .5, mode), 0)
            self.assertEqual(oracle.expected_gl_sample(oracle.STEP_WORDS, 2, 4, 1, .5, mode), 128)
        for u, nearest, linear in ((.25, 0, 0), (.375, 0, 32), (.625, 128, 96), (.75, 128, 128)):
            self.assertEqual(oracle.expected_gl_sample(oracle.STEP_WORDS, 2, 4, u, .5, "nearest"), nearest)
            self.assertEqual(oracle.expected_gl_sample(oracle.STEP_WORDS, 2, 4, u, .5, "linear"), linear)

    def test_checker_slice_parity_and_bilinear_mixture_are_literal(self):
        self.assertEqual(oracle.checker_plane(0), (0, 128, 128, 0))
        self.assertEqual(oracle.checker_plane(1), (128, 0, 0, 128))
        self.assertEqual(oracle.expected_gl_sample(oracle.checker_plane(0), 2, 2, .375, .375, "linear"), 48)
        self.assertEqual(oracle.expected_gl_sample(oracle.checker_plane(1), 2, 2, .375, .375, "linear"), 80)

    def test_pixel_center_is_used_instead_of_integer_corner(self):
        self.assertEqual(oracle.framebuffer_uv((23, 7), (0, 0, 64, 32)), (23.5 / 64, 7.5 / 32))
        u, v = oracle.framebuffer_uv((23, 7), (0, 0, 64, 32))
        self.assertEqual(oracle.expected_gl_sample(oracle.STEP_WORDS, 2, 4, u, v, "linear"), 30)
        with self.assertRaises(AssertionError):
            oracle.framebuffer_uv((64, 7), (0, 0, 64, 32))

    def test_invalid_geometry_or_nonfinite_uv_cannot_supply_an_oracle(self):
        for u in (float("nan"), float("inf"), -0.1, 1.1):
            with self.assertRaises(AssertionError):
                oracle.expected_gl_sample(oracle.STEP_WORDS, 2, 4, u, .5, "linear")
        with self.assertRaises(AssertionError):
            oracle.framebuffer_uv((1, 1), (0, 0, 0, 8))
        with self.assertRaises(AssertionError):
            oracle.expected_gl_sample((0,), 2, 2, .5, .5, "linear")


if __name__ == "__main__":
    unittest.main()
