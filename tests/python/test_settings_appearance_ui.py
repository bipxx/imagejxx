"""C2 installed actual GL observations, including fresh-process persistence.

Required qualification sets IMAGEJXX_REQUIRE_RENDER_TESTS=1 and
IMAGEJXX_SETTINGS_APPEARANCE_UI_CASE_ROOT. This reader never imports imagejxx.
"""
from functools import lru_cache
import hashlib
import json
import os
from pathlib import Path
import stat
import sys

import pytest

from settings_appearance_reader import CASES, CONTEXTS, check_pair

EXPECTED_COLLECTION_IDS = tuple("test_installed_settings_appearance[" + case + "-" + context + "]"
                                for case in CASES for context in CONTEXTS)
EXPECTED_COLLECTION_COUNT = 4


def read(path, limit=2*1048576):
    raw = path.read_bytes()
    assert 0 < len(raw) <= limit

    def pairs(items):
        result = {}
        for k, v in items:
            assert k not in result, "duplicate observation key"
            result[k] = v
        return result

    return json.loads(raw, object_pairs_hook=pairs,
                      parse_constant=lambda value: (_ for _ in ()).throw(AssertionError(value)))


def verify(pin, root=None):
    path = Path(pin["path"])
    assert path.is_absolute() and path.resolve(strict=True) == path
    if root is not None:
        assert path.is_relative_to(root)
    info = path.lstat()
    assert stat.S_ISREG(info.st_mode) and not path.is_symlink() and not getattr(info, "st_file_attributes", 0) & 0x400
    assert type(pin["bytes"]) is int and 0 < info.st_size == pin["bytes"] <= 256*1048576
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1048576), b""):
            digest.update(block)
    assert digest.hexdigest() == pin["sha256"]
    assert (info.st_size, info.st_mtime_ns) == (path.stat().st_size, path.stat().st_mtime_ns)
    return path


@lru_cache(maxsize=1)
def captured():
    configured = os.environ.get("IMAGEJXX_SETTINGS_APPEARANCE_UI_CASE_ROOT")
    if not configured:
        if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") == "1":
            pytest.fail("C2 requires actual installed paired GL captures")
        pytest.skip("C2 actual installed GL captures not configured")
    root = Path(configured).resolve(strict=True)
    provenance = read(root / "provenance.json")
    capture = read(root / "capture.json")
    assert capture["kind_selection"] == "appearance" and capture["state"] == "passed" and capture["acceptance_claim"] is False
    assert provenance["kind"] == "c2.settings-appearance-ui-reader-inputs.001"
    assert set(provenance["contexts"]) == set(CONTEXTS)
    assert capture["cases"] == provenance["cases"] and len(capture["processes"]) == 4
    verify(capture["provenance"], root)
    sources = provenance["sources"]
    canonical = [{k: row[k] for k in ("role", "logical_path", "bytes", "sha256")}
                 for row in sorted(sources, key=lambda row: row["role"])]
    source_set = hashlib.sha256(json.dumps(canonical, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    assert source_set == provenance["source_set_sha256"]
    assert len({r["role"] for r in sources}) == len(sources)
    for row in sources:
        verify(row, root / "source")
    roles = {r["role"]: r["logical_path"] for r in sources}
    assert {"producer": "src/desktop/settings_appearance_ui_probe.cpp",
            "reader": "tests/python/test_settings_appearance_ui.py",
            "reader_core": "tests/python/settings_appearance_reader.py",
            "expectations": "tests/python/settings_appearance_expectations.py",
            "observation_transport": "tests/python/actual_observation_transport.py",
            "window": "src/desktop/window.cpp", "settings_panel": "src/desktop/settings_panel.cpp"}.items() <= roles.items()
    source_roles = {r["role"]: r for r in sources}
    for role, module in (("reader", sys.modules[__name__]),
                         ("reader_core", sys.modules["settings_appearance_reader"]),
                         ("expectations", sys.modules["settings_appearance_expectations"]),
                         ("observation_transport", sys.modules["actual_observation_transport"])):
        raw = Path(module.__file__).read_bytes()
        assert (len(raw), hashlib.sha256(raw).hexdigest()) == (source_roles[role]["bytes"], source_roles[role]["sha256"]), "executing comparator differs from captured source role"
    descriptors = {(r["id"], r["context"]): r for r in provenance["cases"]}
    assert set(descriptors) == {(case, context) for case in CASES for context in CONTEXTS}
    reports = {}
    processes = iter(capture["processes"])
    for case in CASES:
        for context in CONTEXTS:
            binding = provenance["contexts"][context]
            assert binding["source_set_sha256"] == source_set
            proof = read(verify(binding["build_receipt"], root))
            assert proof["kind"] == "c2.settings-appearance-source-product-binding.001"
            assert proof["source_set_sha256"] == source_set and proof["sources"] == sources and proof["acceptance_claim"] is False
            build = read(verify(proof["passed_build_receipt"]))
            assert build["state"] == "passed" and build["source_freeze"] == proof["source_freeze"]
            explicit = read(verify(proof["explicit_product_bindings"]))
            assert explicit["kind"] == "c2.installed-ui-capture-bindings.001" and explicit["kind_selection"] == "appearance"
            assert explicit["built_products"] == build["built_products"] and explicit["products"] == proof["products"]
            cpp = explicit["products"]["imgui_cpp"]
            python = explicit["products"]["imgui_python"]
            assert cpp["executable"] == cpp["producer_artifact"] == cpp["native_artifact"]
            assert python["executable"] == explicit["python_engine"]
            assert python["producer_artifact"] == python["native_artifact"] == explicit["installed_modules"]["imagejxx._desktop"]
            frozen = read(verify(proof["source_freeze"]))
            frozen_rows = {r["logical_path"]: r for r in frozen["sources"]}
            assert len(frozen_rows) == len(frozen["sources"])
            for source in sources:
                original = frozen_rows[source["logical_path"]]
                assert (source["bytes"], source["sha256"]) == (original["bytes"], original["sha256"])
            for selected, compiled in ((explicit["products"]["imgui_cpp"]["executable"], build["built_products"]["cpp"]),
                                       (explicit["installed_modules"]["imagejxx._desktop"], build["built_products"]["_desktop"]),
                                       (explicit["installed_modules"]["imagejxx._native"], build["built_products"]["_native"])):
                verify(selected)
                assert (selected["bytes"], selected["sha256"]) == (compiled["bytes"], compiled["sha256"])
            for module, source_role in (("imagejxx", "python_package"), ("imagejxx.ui.app", "python_wrapper"), ("imagejxx.ui.persistence", "python_preferences")):
                selected, source = explicit["installed_modules"][module], source_roles[source_role]
                verify(selected)
                assert (selected["bytes"], selected["sha256"]) == (source["bytes"], source["sha256"])
            for role in ("executable", "producer_artifact", "native_artifact"):
                verify(binding[role])
                assert binding[role] == proof["products"][context][role]
            descriptor = descriptors[case, context]
            report = read(verify(descriptor["raw"], root), 16*1048576)
            process = next(processes)
            terminal = read(verify(process["owned_process_receipt"], root))
            assert process["state"] == "passed" and process["pid"] == terminal["target_pid"]
            assert terminal["state"] == "exited" and terminal["exit_code"] == 0 and terminal["target_completed"] is True
            assert terminal["cleanup_complete"] is True and terminal["timed_out"] is False
            assert report["runtime_before"]["process_id"] == process["pid"]
            assert (report["case_id"], report["context"]) == (case, context)
            for role in ("executable", "producer_artifact", "native_artifact"):
                assert report["runtime_before"][role] == report["runtime_after"][role] == binding[role]
            if context == "imgui_python":
                origin = read(verify(descriptor["launcher_origin"], root))
                assert origin["observation_state"] == "observed" and origin["process_id"] == process["pid"]
                assert origin["source_set_sha256"] == source_set
                assert origin["before"] == origin["after"] == {k: binding[k] for k in ("executable", "producer_artifact", "native_artifact")}
                assert origin["modules_before"] == origin["modules_after"] == explicit["installed_modules"]
            reports[case, context] = report
    return reports


@pytest.mark.parametrize("case,context", [(c, x) for c in CASES for x in CONTEXTS],
                         ids=[c + "-" + x for c in CASES for x in CONTEXTS])
def test_installed_settings_appearance(case, context):
    reports = captured()
    pair = check_pair(reports[CASES[0], context], reports[CASES[1], context])
    assert pair[CASES.index(case)]["case_id"] == case
