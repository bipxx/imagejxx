"""Synthetic reader refusal controls, never product or playback evidence."""
from copy import deepcopy
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
modules = {}
for name in ("actual_observation_transport", "settings_playback_reader"):
    path = ROOT / "tests/python" / (name + ".py")
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    previous = sys.modules.get(name)
    sys.modules[name] = module
    exec(compile(path.read_bytes(), str(path), "exec"), module.__dict__)
    modules[name] = module
    if previous is not None:
        sys.modules[name] = previous
reader = modules["settings_playback_reader"]


class PlaybackReaderControls(unittest.TestCase):
    def test_draft_reset_and_applied_readback_are_separate(self):
        state = {"settings":{"animation_fps":8}, "preferences_panel":{"open":True,
                 "fields":{"animation_fps":{"draft":8,"applied":8}}}}
        reader.check_draft(state,8)
        for mutation in (lambda s:s["preferences_panel"].update(open=False),
                         lambda s:s["preferences_panel"]["fields"]["animation_fps"].update(draft=5),
                         lambda s:s["preferences_panel"]["fields"]["animation_fps"].update(applied=5)):
            row=deepcopy(state);mutation(row)
            with self.assertRaises(AssertionError):reader.check_draft(row,8)

    def test_unknown_compartments_and_only_empty_reserved_recent(self):
        seed = {"format":"imagejxx-preferences", "version":1,
                "values":{"animation_fps":20, "future":{"raw":[1,"two",True]}},
                "future_root":[1], "extras":{"future_extra":"keep"}}
        expected = reader.opaque(seed, {"animation_fps"})
        empty = deepcopy(seed);empty["extras"]["recent"] = []
        self.assertEqual(reader.opaque(empty, {"animation_fps"}), expected)
        for recent in (False, None, {}, "", ["path"]):
            row = deepcopy(seed);row["extras"]["recent"] = recent
            with self.subTest(recent=recent), self.assertRaises(AssertionError):
                reader.opaque(row, {"animation_fps"})
        changed = deepcopy(seed);changed["extras"]["future_extra"] = "changed"
        self.assertNotEqual(reader.opaque(changed, {"animation_fps"}), expected)

    def test_file_bytes_and_hash_are_independently_checked(self):
        text = json.dumps({"values":{"animation_fps":20}})
        row = {"utf8":text, "bytes":len(text.encode()), "sha256":hashlib.sha256(text.encode()).hexdigest()}
        self.assertEqual(reader.payload(row)["values"]["animation_fps"], 20)
        for key, value in (("bytes",0), ("sha256","0"*64), ("utf8",text+" ")):
            with self.subTest(key=key), self.assertRaises(AssertionError):reader.payload(dict(row, **{key:value}))

    def baseline(self):
        return {"document_id":1, "samples":[0,1065353216], "shares_original":True,
                "dirty":False, "undo":0, "redo":0, "metadata":{"future":"retain"},
                "revisions":{"pixels":0,"metadata":0,"selection":0,"display":1},
                "display":{"t":1,"c":1,"crosshair":[2,1,1],"revision":1,"zoom":8}}

    def test_source_words_identity_history_and_camera_refuse(self):
        baseline = self.baseline();reader.check_source(baseline, baseline)
        for mutation in (lambda r:r["samples"].__setitem__(0,1),lambda r:r.update(document_id=2),
                         lambda r:r.update(undo=1),lambda r:r.update(shares_original=False),
                         lambda r:r["display"].update(zoom=9),lambda r:r["display"]["crosshair"].__setitem__(0,3)):
            row = deepcopy(baseline);mutation(row)
            with self.assertRaises(AssertionError):reader.check_source(row, baseline)

    def test_clock_rejects_an_early_display_advance(self):
        source = self.baseline()
        old = {"source":source,"fps":5,"clock_seconds":0.,
               "animation":{"playing":True,"ticks":0,"last_tick":0.,"axis":"z","timing":"fps"}}
        current = deepcopy(old);current["clock_seconds"] = 1/60;current["delta_seconds"] = 1/60
        current["animation"].update(clock="imgui_monotonic_seconds",fps=5,interval_seconds=.2,ticks=1,last_tick=1/60)
        current["source"]["display"]["crosshair"][2] = 2
        current["source"]["display"]["revision"] = 2;current["source"]["revisions"]["display"] = 2
        report = {"frames":[{"report":old},{"report":current}],"states":{"initial":{"frame":0}}}
        with self.assertRaisesRegex(AssertionError,"due/wrap"):
            reader.check_clock(report, source)


if __name__ == "__main__":
    unittest.main()
