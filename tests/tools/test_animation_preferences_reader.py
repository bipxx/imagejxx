"""Synthetic protocol controls; these fixtures are not playback evidence."""
from copy import deepcopy
import importlib.util
from pathlib import Path
import struct
import unittest

path = Path(__file__).resolve().parents[1] / "python/animation_preferences_reader.py"
spec = importlib.util.spec_from_file_location("loaded_fps_reader", path)
reader = importlib.util.module_from_spec(spec)
exec(compile(path.read_bytes(), str(path), "exec"), reader.__dict__)


def fixture(case="fps-loaded", fps=5, context="imgui_cpp"):
    calibrated = case == "cal-loaded"
    count, axis, extent = (8, 0, 5) if calibrated else (7, 2, 4)
    interval = .05 if calibrated else 1/fps
    source = dict(source_id=1, data_identity=1, title="synthetic", properties={},
                  selection={}, previous={}, roi={}, crosshair=[3, 2, 1], zoom=1.25,
                  plane_zoom=[1.25, 1.75, 2.25], linked_zoom=False, orthogonal=True,
                  spacing=[.5, 1.25, 2.5], origin=[11, -7, 19], frame_interval=.05,
                  time_unit="s", position=[1, 1, 1], display_revision=1,
                  shares_original=True, dirty=False, undo=0, redo=0,
                  pixels_revision=0, metadata_revision=0, selection_revision=0,
                  workflow=[], results=[])
    words = []
    for t in range(5 if calibrated else 2):
        for c in range(3):
            for z in range(4):
                words.extend(struct.unpack("<I", struct.pack("<f", value))[0]
                             for value in [1000*t+100*c+10*z+y+x for y in range(5) for x in range(7)])
    before = dict(deepcopy(source), samples=words)
    frames = []
    for tick in range(count+1):
        source["position"][axis] = (1+tick) % extent
        if not calibrated:
            source["crosshair"][2] = source["position"][2]
        source["display_revision"] = 1+tick
        frames.append(dict(events=[], source=deepcopy(source), rendered=dict(
            input_frame={"raw_inputs": []}, window_error="", orthogonal_planes=[
                {"plane": plane, "current_tcz": list(source["position"])} for plane in ("XY", "XZ", "YZ")],
            animation=dict(fps=fps, clock="imgui_monotonic_seconds", ticks=tick,
                           playing=True, axis="t" if calibrated else "z",
                           timing="calibrated_time" if calibrated else "fps",
                           interval_seconds=interval, last_tick=tick*interval))))
    return dict(contract_id="p09s.animation-ui.001", case_id=case, context=context,
                observation_state="observed", acceptance_claim=False, loaded_fps=fps,
                shape=[5 if calibrated else 2, 3, 4, 5, 7, 1], frame_count=len(frames),
                elapsed_seconds=1., lifecycle={"open": False}, api=[], before=before,
                after=dict(deepcopy(source), samples=words), frames=frames,
                animation={"playing": False, "source_id": None})


class LoadedFpsControls(unittest.TestCase):
    def test_menu_activation_can_precede_clock_arm_but_not_a_tick(self):
        row = fixture()
        activation = deepcopy(row["frames"][0])
        activation["rendered"]["animation"]["last_tick"] = -1
        row["frames"].insert(0, activation);row["frame_count"] += 1
        reader.check_loaded_fps(row, 5, "imgui_cpp")
        row["frames"][2]["rendered"]["animation"]["last_tick"] = -1
        with self.assertRaises(AssertionError):reader.check_loaded_fps(row, 5, "imgui_cpp")

    def test_both_rates_modes_and_contexts(self):
        for case in ("fps-loaded", "cal-loaded"):
            for fps in (5, 20):
                for context in ("imgui_cpp", "imgui_python"):
                    with self.subTest(case=case, fps=fps, context=context):
                        reader.check_loaded_fps(fixture(case, fps, context), fps, context)

    def test_wrong_loaded_rate_and_changed_interval_refuse(self):
        for mutation in (lambda r: r.update(loaded_fps=60),
                         lambda r: r["frames"][2]["rendered"]["animation"].update(fps=60),
                         lambda r: r["frames"][2]["rendered"]["animation"].update(interval_seconds=.1),
                         lambda r: r["frames"][2]["rendered"]["animation"].update(last_tick=.3)):
            row = fixture();mutation(row)
            with self.assertRaises(AssertionError):reader.check_loaded_fps(row, 5, "imgui_cpp")

    def test_source_bits_owner_history_and_axis_refuse(self):
        for mutation in (lambda r: r["after"]["samples"].__setitem__(0, 1),
                         lambda r: r["frames"][2]["source"].update(data_identity=2),
                         lambda r: r["frames"][2]["source"].update(undo=1),
                         lambda r: r["frames"][2]["source"].update(display_revision=99),
                         lambda r: r["frames"][2]["rendered"]["orthogonal_planes"].pop(),
                         lambda r: r["frames"][2]["source"]["position"].__setitem__(1, 2)):
            row = fixture();mutation(row)
            with self.assertRaises(AssertionError):reader.check_loaded_fps(row, 5, "imgui_cpp")

    def test_api_substitution_truncation_and_unclosed_lifetime_refuse(self):
        for mutation in (lambda r: r["api"].append({"label": "fps"}),
                         lambda r: r["frames"].pop(),
                         lambda r: r["lifecycle"].update(open=True)):
            row = fixture();mutation(row)
            with self.assertRaises(AssertionError):reader.check_loaded_fps(row, 5, "imgui_cpp")


if __name__ == "__main__":
    unittest.main()
