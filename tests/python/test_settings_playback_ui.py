"""C5 installed actual ImGui observations and fresh-process persistence.

Required profiles set IMAGEJXX_REQUIRE_RENDER_TESTS=1 and
IMAGEJXX_SETTINGS_PLAYBACK_UI_CASE_ROOT. This entry authenticates existing
captures; it never imports imagejxx, launches a product or substitutes a kernel.
"""
from functools import lru_cache
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import stat
import sys

import pytest


def _source_comparators():
    """Execute the two current comparator sources, never timestamp-valid pyc."""
    names = (("observation_transport", "actual_observation_transport"),
             ("reader_core", "settings_playback_reader"))
    absent = object()
    previous = {name: sys.modules.get(name, absent) for _, name in names}
    loaded = {}
    try:
        for role, name in names:
            path = Path(__file__).resolve().with_name(name + ".py")
            before = path.read_bytes()
            pin = {"path": str(path), "bytes": len(before),
                   "sha256": hashlib.sha256(before).hexdigest()}
            source = path.read_bytes()
            if (len(source), hashlib.sha256(source).hexdigest()) != (pin["bytes"], pin["sha256"]):
                raise AssertionError("Comparator source changed while reading: " + name)
            spec = importlib.util.spec_from_file_location(name, path)
            module = importlib.util.module_from_spec(spec)
            # Preserve normal module metadata/imports while bypassing .pyc.
            sys.modules[name] = module
            exec(compile(source, spec.origin, "exec", dont_inherit=True), module.__dict__)
            after = path.read_bytes()
            if (len(after), hashlib.sha256(after).hexdigest()) != (pin["bytes"], pin["sha256"]):
                raise AssertionError("Comparator source changed while loading: " + name)
            loaded[role] = (module, pin)
    finally:
        # Other collected readers keep their own module identities and imports.
        for name, module in previous.items():
            if module is absent:
                sys.modules.pop(name, None)
            else:
                sys.modules[name] = module
    return loaded


_COMPARATORS = _source_comparators()
check_pair = _COMPARATORS["reader_core"][0].check_pair
check_report = _COMPARATORS["reader_core"][0].check_report


CASES = ("playback-persist", "playback-reopen")
CONTEXTS = ("imgui_cpp", "imgui_python")
PARAMETERS = tuple((case, context) for case in CASES for context in CONTEXTS)
EXPECTED_COLLECTION_IDS = tuple("test_installed_settings_playback[" + case + "-" + context + "]"
                                for case, context in PARAMETERS)
EXPECTED_COLLECTION_COUNT = 4
SOURCE_ROLES = {
    "producer": "src/desktop/settings_playback_ui_probe.cpp",
    "producer_header": "src/desktop/settings_playback_ui_probe.hpp",
    "reader": "tests/python/test_settings_playback_ui.py",
    "reader_core": "tests/python/settings_playback_reader.py",
    "observation_transport": "tests/python/actual_observation_transport.py",
    "observation_transport_header": "src/desktop/actual_observation_transport.hpp",
    "window": "src/desktop/window.cpp",
    "headless_header": "src/desktop/headless_session_internal.hpp",
    "settings_panel": "src/desktop/settings_panel.cpp",
}


def read(path, limit=2 * 1048576):
    raw = path.read_bytes()
    assert 0 < len(raw) <= limit

    def pairs(items):
        result = {}
        for key, value in items:
            assert key not in result, "duplicate observation key"
            result[key] = value
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
    assert type(pin["bytes"]) is int and 0 < info.st_size == pin["bytes"] <= 256 * 1048576
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1048576), b""):
            digest.update(block)
    assert digest.hexdigest() == pin["sha256"]
    after = path.stat()
    assert (info.st_size, info.st_mtime_ns) == (after.st_size, after.st_mtime_ns)
    return path


@lru_cache(maxsize=1)
def captured():
    configured = os.environ.get("IMAGEJXX_SETTINGS_PLAYBACK_UI_CASE_ROOT")
    if not configured:
        if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") == "1":
            pytest.fail("C5 requires actual installed paired playback captures")
        pytest.skip("C5 actual installed playback captures not configured")
    root = Path(configured).resolve(strict=True)
    capture = read(root / "capture.json")
    provenance_path = verify(capture["provenance"], root)
    assert provenance_path == root / "provenance.json"
    provenance = read(provenance_path)
    assert capture["kind_selection"] == "playback" and capture["state"] == "passed" and capture["acceptance_claim"] is False
    assert provenance["kind"] == "c5.settings-playback-ui-reader-inputs.001"
    assert set(provenance["contexts"]) == set(CONTEXTS)
    assert capture["cases"] == provenance["cases"] and len(capture["processes"]) == 4
    sources = provenance["sources"]
    canonical = [{key: row[key] for key in ("role", "logical_path", "bytes", "sha256")}
                 for row in sorted(sources, key=lambda row: row["role"])]
    source_set = hashlib.sha256(json.dumps(canonical, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    assert source_set == provenance["source_set_sha256"]
    assert len({row["role"] for row in sources}) == len(sources)
    for row in sources:
        verify(row, root / "source")
    roles = {row["role"]: row["logical_path"] for row in sources}
    assert SOURCE_ROLES.items() <= roles.items()
    source_roles = {row["role"]: row for row in sources}
    raw = Path(__file__).read_bytes()
    assert (len(raw), hashlib.sha256(raw).hexdigest()) == (source_roles["reader"]["bytes"], source_roles["reader"]["sha256"]), "executing entry differs from captured source role"
    for role, (module, executed_pin) in _COMPARATORS.items():
        assert verify(executed_pin) == Path(module.__file__)
        assert (executed_pin["bytes"], executed_pin["sha256"]) == (source_roles[role]["bytes"], source_roles[role]["sha256"]), "executed comparator source differs from captured source role"
    descriptors = {(row["id"], row["context"]): row for row in provenance["cases"]}
    assert len(provenance["cases"]) == len(descriptors) == len(PARAMETERS)
    assert set(descriptors) == set(PARAMETERS)
    reports = {}
    for (case, context), process in zip(PARAMETERS, capture["processes"]):
        binding = provenance["contexts"][context]
        assert binding["source_set_sha256"] == source_set
        proof = read(verify(binding["build_receipt"], root))
        assert proof["kind"] == "c5.settings-playback-source-product-binding.001"
        assert proof["source_set_sha256"] == source_set and proof["sources"] == sources and proof["acceptance_claim"] is False
        build = read(verify(proof["passed_build_receipt"]))
        assert build["state"] == "passed" and build["source_freeze"] == proof["source_freeze"]
        explicit = read(verify(proof["explicit_product_bindings"]))
        assert explicit["kind"] == "c5.installed-ui-capture-bindings.001" and explicit["kind_selection"] == "playback"
        assert explicit["built_products"] == build["built_products"] and explicit["products"] == proof["products"]
        cpp, python = explicit["products"]["imgui_cpp"], explicit["products"]["imgui_python"]
        assert cpp["executable"] == cpp["producer_artifact"] == cpp["native_artifact"]
        assert python["executable"] == explicit["python_engine"]
        assert python["producer_artifact"] == python["native_artifact"] == explicit["installed_modules"]["imagejxx._desktop"]
        frozen = read(verify(proof["source_freeze"]))
        frozen_rows = {row["logical_path"]: row for row in frozen["sources"]}
        assert len(frozen_rows) == len(frozen["sources"])
        for source in sources:
            original = frozen_rows[source["logical_path"]]
            assert (source["bytes"], source["sha256"]) == (original["bytes"], original["sha256"])
        for selected, compiled in ((cpp["executable"], build["built_products"]["cpp"]),
                                   (explicit["installed_modules"]["imagejxx._desktop"], build["built_products"]["_desktop"]),
                                   (explicit["installed_modules"]["imagejxx._native"], build["built_products"]["_native"])):
            verify(selected)
            assert (selected["bytes"], selected["sha256"]) == (compiled["bytes"], compiled["sha256"])
        for module, source_role in (("imagejxx", "python_package"), ("imagejxx.ui.app", "python_wrapper"),
                                    ("imagejxx.ui.persistence", "python_preferences")):
            selected, source = explicit["installed_modules"][module], source_roles[source_role]
            verify(selected)
            assert (selected["bytes"], selected["sha256"]) == (source["bytes"], source["sha256"])
        for role in ("executable", "producer_artifact", "native_artifact"):
            verify(binding[role])
            assert binding[role] == proof["products"][context][role]
        descriptor = descriptors[case, context]
        report = read(verify(descriptor["raw"], root), 16 * 1048576 - 1)
        terminal = read(verify(process["owned_process_receipt"], root))
        assert process["state"] == "passed" and process["pid"] == terminal["target_pid"]
        assert terminal["state"] == "exited" and terminal["exit_code"] == 0 and terminal["target_completed"] is True
        assert terminal["cleanup_complete"] is True and terminal["timed_out"] is False
        assert report["runtime_before"] == report["runtime_after"]
        assert report["runtime_before"]["process_id"] == process["pid"]
        assert (report["case_id"], report["context"]) == (case, context)
        for role in ("executable", "producer_artifact", "native_artifact"):
            assert report["runtime_before"][role] == binding[role]
        if context == "imgui_python":
            origin = read(verify(descriptor["launcher_origin"], root))
            assert origin["observation_state"] == "observed" and origin["process_id"] == process["pid"]
            assert origin["source_set_sha256"] == source_set
            assert origin["before"] == origin["after"] == {key: binding[key] for key in ("executable", "producer_artifact", "native_artifact")}
            assert origin["modules_before"] == origin["modules_after"] == explicit["installed_modules"]
        reports[case, context] = check_report(report, context, case)
    for context in CONTEXTS:
        check_pair(reports[CASES[0], context], reports[CASES[1], context])
    return reports


@pytest.mark.parametrize("case,context", PARAMETERS,
                         ids=[case + "-" + context for case, context in PARAMETERS])
def test_installed_settings_playback(case, context):
    reports = captured()
    assert (reports[case, context]["case_id"], reports[case, context]["context"]) == (case, context)
