"""Actual installed shortcut API carriers; two isolated, renderer-free modes.

No expected contract values are read. Read attempts come from the public native
DatasetReadObserver, with an independent positive-control owner. Capped native
shortcut errors expose their actual native category on the ValueError instance.
Run headless and editor in separate processes with explicit installed-origin
envelopes and fresh owned directories.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import sysconfig
import time


MIB = 1048576
MAX_RAW = 16 * MIB
CONTRACTS = {
    "registry": ("12187d16f509e75030879784c1c68d77993956a2f64dacec0cd44458e32600ed", (
        "registry.version", "registry.command_count", "registry.ordered_ids", "registry.defaults",
        "registry.labels", "registry.scopes", "default.bound_count", "default.unbound_count", "default.measure.chord")),
    "chords": ("7168b98bdc3a2d391bc7edc1fc7933a5d8ec057771d76de2876e78b43c530ce9", (
        "alias.canonical", "resolved.windows", "resolved.linux", "resolved.macos", "exact.extra_shift.reason",
        "exact.extra_shift.command_id", "grammar.repeated_modifier.refused", "grammar.modifier_only.refused")),
    "storage_conflicts": ("2becbcf6a92a3bece20369a453a50d34425adf6390cf401a56ad0c97464ab3cd", (
        "assigned.measure.windows_chord", "assigned.measure.macos_chord", "clear.measure.chord", "reset.measure.chord",
        "inactive.row", "inactive.count", "conflict.command_ids", "conflict.platform_count", "conflict.source.measure.chord",
        "cap.working.refusal_type")),
    "reservations": ("8789ebf77c5a6dd4486ba80a7556c7edde3ca00d2b30a908f52466ffefc2bf13", (
        "ctrl_alt_k.kind", "ctrl_alt_k.platforms", "primary_q.kind", "primary_q.platforms", "bare_escape.kind",
        "reserved.profile.measure.chord")),
    "focus_dispatch": ("302849f8f0bb9c6e8af2d02e0fd2f29f62246b2d57ed57211bc1e7bb1a4e200b", (
        "canvas.primary_z.command_id", "text.reason", "modal.reason", "capture.reason", "gesture.reason",
        "roi.primary_a.reason", "roi.primary_a.disposition", "local_claim.reason", "unfocused.reason", "rearming.reason",
        "repeat.reason", "ambiguous.reason", "missing_availability.reason")),
    "editor_atomicity": ("c981a9f558b789312203174f4096dc329583bb912d586dff47ce8d44b1439d30", (
        "assign.source.measure.chord", "assign.result.measure.windows_chord", "clear.result.measure.chord",
        "reset.result.measure.chord", "immutable.getter_mutation.measure.chord", "config.pixel_revision_delta", "config.workflow_delta")),
    "persistence_startup": ("8d285d313e311f6e7683640f1f04d638e185ac57b6ac3510ca13fe633e19f099", (
        "preferences.format", "preferences.version", "reopened.measure.windows_chord", "reopened.inactive.row",
        "recovery.measure.chord", "recovery.write_blocked", "failed_import.measure.chord", "failed_import.file_unchanged")),
    "native_public_contract": ("57c146ba026315db5b4024c82de0c58fdd7d821bad800797a4ad87c0e8abc28f", (
        "default.profile.measure.chord", "detached.getter.measure.chord", "detached.caller_input.measure.windows_chord",
        "native.provider_plane_reads", "native.provider_region_reads", "native.document_revision_delta")),
}
MISSING = {}
EDITOR_IDS = {("editor_atomicity", "config.pixel_revision_delta"), ("editor_atomicity", "config.workflow_delta")}


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def canonical_path(value):
    require(type(value) is str and value, "Explicit path string required")
    path = Path(value)
    require(path.is_absolute() and not path.is_symlink(), "Canonical absolute non-link path required")
    result = path.resolve(strict=True)
    require(str(result) == value, "Noncanonical path refused")
    return result


def read_bounded(path, maximum):
    require(path.is_file() and not path.is_symlink(), "Regular owned/pinned file required")
    size = path.stat().st_size
    require(0 < size <= maximum, "File size is outside the declared bound")
    with path.open("rb") as stream:
        data = stream.read(size + 1)
    require(len(data) == size, "File changed during bounded read")
    return data


def pin(path, maximum=64 * MIB):
    data = read_bounded(path, maximum)
    return {"path": str(path), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def checked_pin(value):
    require(type(value) is dict and set(value) == {"path", "bytes", "sha256"}, "Exact three-field pin required")
    require(type(value["bytes"]) is int and 0 < value["bytes"] <= 64 * MIB, "Strict bounded pin size required")
    digest = value["sha256"]
    require(type(digest) is str and len(digest) == 64 and all(c in "0123456789abcdef" for c in digest),
            "Canonical SHA256 required")
    actual = pin(canonical_path(value["path"]))
    require(actual == value, "Pinned installed bytes differ")
    return actual


def strict_json(data):
    def pairs(items):
        result = {}
        for name, value in items:
            require(name not in result, "Duplicate JSON key")
            result[name] = value
        return result

    def nonfinite(value):
        raise ValueError("Nonfinite JSON token: " + value)

    return json.loads(data, object_pairs_hook=pairs, parse_constant=nonfinite)


def origins(path, mode, *, loaded):
    from importlib.machinery import EXTENSION_SUFFIXES

    raw = read_bounded(path, MIB)
    value = strict_json(raw)
    keys = {"schema_version", "kind", "package_root", "modules", "numpy"}
    if mode == "editor":
        keys.add("editor_modules")
    require(type(value) is dict and set(value) == keys, "Exact origin envelope fields required")
    require(type(value["schema_version"]) is int and value["schema_version"] == 1, "Origin schema must be integer1")
    kind = "p06h_shortcuts_python_runtime_origins_v1" if mode == "headless" else "p06h_shortcuts_editor_runtime_origins_v1"
    require(value["kind"] == kind, "Wrong installed-origin context")
    package = canonical_path(value["package_root"])
    source_root = Path(__file__).resolve().parents[2] / "src"
    require(package.is_dir() and package.name == "imagejxx" and not package.is_relative_to(source_root),
            "Installed package required; source bootstrap refused")
    require(type(value["modules"]) is dict and set(value["modules"]) == {"imagejxx", "_native"}, "Exact core origins required")
    modules = {name: checked_pin(item) for name, item in value["modules"].items()}
    require(Path(modules["imagejxx"]["path"]) == package / "__init__.py", "Package initializer origin differs")
    native = Path(modules["_native"]["path"])
    require(native.parent == package and any(native.name == "_native" + suffix for suffix in EXTENSION_SUFFIXES),
            "Native extension origin differs")
    numpy = value["numpy"]
    require(type(numpy) is dict and set(numpy) == {"site_packages", "version", "module"}, "Exact NumPy origins required")
    require(numpy["version"] == "2.2.6", "Approved NumPy version required")
    numpy_site = canonical_path(numpy["site_packages"])
    require(numpy_site.is_dir() and not numpy_site.is_relative_to(source_root), "Approved installed NumPy site required")
    numpy_module = checked_pin(numpy["module"])
    require(Path(numpy_module["path"]) == numpy_site / "numpy" / "__init__.py", "NumPy site/module mismatch")
    editor_modules = {}
    if mode == "editor":
        require(type(value["editor_modules"]) is dict and set(value["editor_modules"]) == {
            "_desktop", "imagejxx.ui.app", "imagejxx.ui.persistence"}, "Exact Editor origins required")
        editor_modules = {name: checked_pin(item) for name, item in value["editor_modules"].items()}
        desktop = Path(editor_modules["_desktop"]["path"])
        require(desktop.parent == package and any(desktop.name == "_desktop" + suffix for suffix in EXTENSION_SUFFIXES),
                "Desktop extension origin differs")
        for role, filename in (("imagejxx.ui.app", "app.py"), ("imagejxx.ui.persistence", "persistence.py")):
            require(Path(editor_modules[role]["path"]) == package / "ui" / filename, "Installed UI wrapper origin differs")
    if loaded:
        names = {"imagejxx": "imagejxx", "_native": "imagejxx._native", "numpy": "numpy"}
        expected = {**modules, "numpy": numpy_module}
        if mode == "editor":
            names.update({"_desktop": "imagejxx._desktop", "imagejxx.ui.app": "imagejxx.ui.app",
                          "imagejxx.ui.persistence": "imagejxx.ui.persistence"})
            expected.update(editor_modules)
        for role, module_name in names.items():
            require(module_name in sys.modules, "Required installed module was not loaded: " + module_name)
            actual = Path(sys.modules[module_name].__file__).resolve(strict=True)
            require(str(actual) == expected[role]["path"], "Actual imported module origin differs: " + role)
        require(sys.modules["numpy"].__version__ == numpy["version"], "Actual NumPy version differs")
    if mode == "headless":
        require(not any(name == "imagejxx._desktop" or name == "imagejxx.ui" or name.startswith("imagejxx.ui.")
                        for name in sys.modules), "Headless carrier imported desktop/UI")
    require(not any(name == "imgui_bundle" or name.startswith("imgui_bundle.") for name in sys.modules),
            "Renderer-free API carrier imported Python ImGui")
    return {"envelope": pin(path, MIB), "package_root": str(package), "modules": modules,
            "numpy": {"site_packages": str(numpy_site), "version": numpy["version"], "module": numpy_module},
            "editor_modules": editor_modules}


def select_installed_sites(approved):
    require(not any(name == "imagejxx" or name.startswith("imagejxx.") for name in sys.modules),
            "Select explicit installed sites before any product import")
    base = Path(sys.base_prefix).resolve(strict=True)
    stdlib = Path(sysconfig.get_path("stdlib")).resolve(strict=True)
    platstdlib = Path(sysconfig.get_path("platstdlib")).resolve(strict=True)
    archive = "python%d%d.zip" % sys.version_info[:2]
    allowed = {base, base / "DLLs", base / archive, stdlib, platstdlib,
               stdlib / "lib-dynload", platstdlib / "lib-dynload", stdlib.parent / archive}
    explicit = [str(Path(approved["package_root"]).parent), approved["numpy"]["site_packages"]]
    sys.path[:] = list(dict.fromkeys(explicit + [item for item in sys.path if item and Path(item).resolve() in allowed]))


class Actual:
    def __init__(self, mode):
        self.mode = mode
        self.rows = []
        self.auxiliary = []
        self.identities = set()

    def emit(self, group, name, value):
        identity = group, name
        require(name in CONTRACTS[group][1] and identity not in self.identities, "Unknown/duplicate frozen assertion")
        require(identity not in MISSING and ((identity in EDITOR_IDS) == (self.mode == "editor")), "Wrong carrier scope")
        encoding = "canonical_json" if type(value) in (list, dict) else "scalar"
        require(type(value) in (list, dict, str, int, bool) or value is None, "Detached typed actual required")
        detached = strict_json(json.dumps(value, ensure_ascii=False, allow_nan=False))
        self.rows.append({"contract_id": "SC.G012.shortcuts." + group,
                          "requirement_id": "G012.shortcuts." + group,
                          "id": name, "encoding": encoding, "value": detached})
        self.identities.add(identity)

    def finish(self):
        available = {(group, name) for group, (_, names) in CONTRACTS.items() for name in names} - set(MISSING)
        expected = EDITOR_IDS if self.mode == "editor" else available - EDITOR_IDS
        require(self.identities == expected, "Available actual assertion membership did not finish")


def chord(ij, profile, platform):
    value = profile.chord("analysis.measure", platform)
    return None if value is None else ij.format_shortcut_chord(value)


def refusal(error_type, action):
    try:
        action()
    except error_type as error:
        return error
    raise AssertionError("Required actual refusal did not occur")


def preferences_payload(lines):
    return {"format": "imagejxx-preferences", "version": 1, "values": {"shortcut_bindings": lines},
            "extras": {"recent": []}}


def read_counts(observer):
    counts = observer.counts()
    require(type(counts.plane_reads) is int and type(counts.region_reads) is int and
            type(counts.overflowed) is bool and counts.plane_reads >= 0 and counts.region_reads >= 0,
            "Strict native read-count snapshot required")
    return {"plane_reads": counts.plane_reads, "region_reads": counts.region_reads,
            "overflowed": counts.overflowed}


def read_observer_positive_control(ij, np, actual):
    # This separate owner is read before creating the fresh no-I/O fixture.
    # Its counters are never reused or reset to populate the frozen zero rows.
    observer = ij.DatasetReadObserver()
    pixels = np.array([[0, 1, 2], [3, 4, 5]], dtype=np.uint8)
    owner = ij.Document(ij.Dataset(pixels, axes="YX").observe_reads(observer),
                        "Shortcut observer positive control")
    before = read_counts(observer)
    plane = owner.snapshot().dataset.plane("xy", 0)
    after_plane = read_counts(observer)
    region = owner.snapshot().dataset.region(1, 0, 2, 2)
    after_region = read_counts(observer)
    require(np.array_equal(plane, pixels) and np.array_equal(region, pixels[:, 1:]),
            "Positive-control observer read did not return the actual native samples")
    require(before == {"plane_reads": 0, "region_reads": 0, "overflowed": False} and
            after_plane == {"plane_reads": 1, "region_reads": 0, "overflowed": False} and
            after_region == {"plane_reads": 1, "region_reads": 1, "overflowed": False},
            "Positive-control public read observer did not count genuine native entries")
    actual.auxiliary.append({"id": "native.read_observer.positive_control", "document_id": owner.id,
                             "before": before, "after_plane": after_plane, "after_region": after_region,
                             "plane_samples": plane.tolist(), "region_samples": region.tolist(),
                             "scope": "separate public observer/native owner; no fixture counter reset"})


def headless(ij, np, output, actual):
    windows, linux, macos = ij.ShortcutPlatform.Windows, ij.ShortcutPlatform.Linux, ij.ShortcutPlatform.MacOS
    registry = ij.shortcut_commands()
    default = ij.ShortcutProfile()
    emit = actual.emit
    emit("registry", "registry.version", ij.shortcut_registry_version)
    emit("registry", "registry.command_count", len(registry))
    emit("registry", "registry.ordered_ids", [item.id for item in registry])
    emit("registry", "registry.defaults", {item.id: None if item.default_chord is None else ij.format_shortcut_chord(item.default_chord)
                                          for item in registry})
    emit("registry", "registry.labels", {item.id: item.label for item in registry})
    emit("registry", "registry.scopes", {item.id: item.scope.name.lower() for item in registry})
    emit("registry", "default.bound_count", sum(item.default_chord is not None for item in registry))
    emit("registry", "default.unbound_count", sum(item.default_chord is None for item in registry))
    emit("registry", "default.measure.chord", chord(ij, default, windows))
    parsed = ij.parse_shortcut_chord(" \tCtrlOrCmd+shift+LeftBracket\r\n")
    emit("chords", "alias.canonical", ij.format_shortcut_chord(parsed))
    for name, platform in (("windows", windows), ("linux", linux), ("macos", macos)):
        emit("chords", "resolved." + name, ij.format_shortcut_chord(ij.resolve_shortcut_chord(parsed, platform)))

    def context(focus=None, **flags):
        return ij.ShortcutContext(ij.ShortcutFocus.Canvas if focus is None else focus,
                                  availability={item.id: True for item in registry}, **flags)

    def edge(key, **flags):
        return ij.ShortcutEvent(key, down_edge=True, **flags)

    shifted = ij.evaluate_shortcut(default, windows, edge(ij.ShortcutKey.M, shift=True), context())
    emit("chords", "exact.extra_shift.reason", shifted.reason.name)
    emit("chords", "exact.extra_shift.command_id", shifted.command_id)
    for name, text in (("repeated_modifier", "Ctrl+Ctrl+K"), ("modifier_only", "Ctrl+")):
        error = refusal(ValueError, lambda text=text: ij.parse_shortcut_chord(text))
        emit("chords", "grammar." + name + ".refused", isinstance(error, ValueError))

    source = ij.ShortcutProfile()
    assigned = ij.assign_shortcut(source, "analysis.measure", "Primary+K").profile
    cleared = ij.clear_shortcut(assigned, "analysis.measure").profile
    reset = ij.reset_shortcut(cleared, "analysis.measure").profile
    emit("storage_conflicts", "assigned.measure.windows_chord", chord(ij, assigned, windows))
    emit("storage_conflicts", "assigned.measure.macos_chord", chord(ij, assigned, macos))
    emit("storage_conflicts", "clear.measure.chord", chord(ij, cleared, windows))
    emit("storage_conflicts", "reset.measure.chord", chord(ij, reset, windows))
    future = ij.compile_shortcuts(["analysis.measure=Primary+K", "future.tool=opaque future v1"])
    inactive = future.inactive_rows()
    require(len(inactive) == 1, "Literal future-row recipe did not yield one row")
    emit("storage_conflicts", "inactive.row", inactive[0])
    emit("storage_conflicts", "inactive.count", len(inactive))
    conflict = refusal(ij.ShortcutProfileError, lambda: ij.assign_shortcut(source, "analysis.measure", "Primary+S"))
    require(len(conflict.diagnostics) == 1, "Conflict recipe did not yield one structured diagnostic")
    diagnostic = conflict.diagnostics[0]
    emit("storage_conflicts", "conflict.command_ids", "|".join(sorted((diagnostic.command_id, diagnostic.other_command_id))))
    emit("storage_conflicts", "conflict.platform_count", len(diagnostic.platforms))
    emit("storage_conflicts", "conflict.source.measure.chord", chord(ij, source, windows))
    cap_error = refusal(ValueError, lambda: ij.compile_shortcuts(
        ["analysis.measure=Primary+K"], ij.ShortcutLimits(max_native_working_bytes=1)))
    native_category = getattr(cap_error, "native_category", None)
    require(type(native_category) is str and native_category == "length_error",
            "Capped shortcut refusal did not retain its actual native length_error category")
    emit("storage_conflicts", "cap.working.refusal_type", native_category)
    actual.auxiliary.append({"id": "cap.working.python_refusal", "python_type": type(cap_error).__name__,
                             "message": str(cap_error), "native_exception_category_observed": hasattr(cap_error, "native_category")})
    for name, text in (("ctrl_alt_k", "Ctrl+Alt+K"), ("primary_q", "Primary+Q"), ("bare_escape", "Escape")):
        error = refusal(ij.ShortcutProfileError, lambda text=text: ij.assign_shortcut(source, "app.jobs", text))
        require(len(error.diagnostics) == 1, "Reservation recipe did not yield one diagnostic")
        diagnostic = error.diagnostics[0]
        emit("reservations", name + ".kind", diagnostic.kind.name)
        if name != "bare_escape":
            emit("reservations", name + ".platforms", "|".join(platform.name for platform in diagnostic.platforms))
    emit("reservations", "reserved.profile.measure.chord", chord(ij, source, windows))

    undo = ij.evaluate_shortcut(default, windows, edge(ij.ShortcutKey.Z, ctrl=True), context())
    emit("focus_dispatch", "canvas.primary_z.command_id", undo.command_id)
    measure = edge(ij.ShortcutKey.M)
    scenarios = (("text", context(ij.ShortcutFocus.Text)), ("modal", context(ij.ShortcutFocus.Modal)),
                 ("capture", context(ij.ShortcutFocus.Capture)), ("gesture", context(gesture_active=True)),
                 ("local_claim", context(local_key_claimed=True)), ("unfocused", context(window_focused=False)),
                 ("rearming", context(rearmed=False)))
    for name, owner in scenarios:
        emit("focus_dispatch", name + ".reason", ij.evaluate_shortcut(default, windows, measure, owner).reason.name)
    roi = ij.evaluate_shortcut(default, windows, edge(ij.ShortcutKey.A, ctrl=True), context(ij.ShortcutFocus.ROIManager))
    emit("focus_dispatch", "roi.primary_a.reason", roi.reason.name)
    emit("focus_dispatch", "roi.primary_a.disposition", roi.disposition.name)
    repeat = ij.evaluate_shortcut(default, windows, edge(ij.ShortcutKey.M, repeat=True), context())
    emit("focus_dispatch", "repeat.reason", repeat.reason.name)
    ambiguous = ij.evaluate_shortcuts(default, windows, [measure, edge(ij.ShortcutKey.O, ctrl=True)], context())
    emit("focus_dispatch", "ambiguous.reason", ambiguous.reason.name)
    missing = context()
    missing.availability = {}
    emit("focus_dispatch", "missing_availability.reason", ij.evaluate_shortcut(default, windows, measure, missing).reason.name)

    emit("editor_atomicity", "assign.source.measure.chord", chord(ij, source, windows))
    emit("editor_atomicity", "assign.result.measure.windows_chord", chord(ij, assigned, windows))
    emit("editor_atomicity", "clear.result.measure.chord", chord(ij, cleared, windows))
    emit("editor_atomicity", "reset.result.measure.chord", chord(ij, reset, windows))
    getter = source.chord("analysis.measure", windows)
    getter.key = ij.ShortcutKey.K
    commands = ij.shortcut_commands()
    next(item for item in commands if item.id == "analysis.measure").default_chord.key = ij.ShortcutKey.K
    source.override_lines().append("analysis.measure=Primary+K")
    emit("editor_atomicity", "immutable.getter_mutation.measure.chord", chord(ij, source, windows))

    preferences = ij.PreferencesDocument.parse(json.dumps(preferences_payload(
        ["analysis.measure=Primary+K", "future.tool=opaque future v1"])), recover=False)
    preference_path = output / "preferences.json"
    require(preferences.save(preference_path), "Owned Preferences save failed")
    saved = strict_json(read_bounded(preference_path, MIB))
    reopened = ij.PreferencesDocument.load(preference_path, recover=False)
    reopened_profile = ij.compile_shortcuts(reopened.settings["shortcut_bindings"])
    emit("persistence_startup", "preferences.format", saved["format"])
    emit("persistence_startup", "preferences.version", saved["version"])
    emit("persistence_startup", "reopened.measure.windows_chord", chord(ij, reopened_profile, windows))
    reopened_rows = reopened_profile.inactive_rows()
    require(len(reopened_rows) == 1, "Fresh reopen lost literal inactive-row membership")
    emit("persistence_startup", "reopened.inactive.row", reopened_rows[0])
    invalid_path = output / "recovery.json"
    invalid = preferences_payload(["analysis.measure=Primary+S"])
    invalid["values"]["theme"] = "light"
    with invalid_path.open("x", encoding="utf-8") as stream:
        json.dump(invalid, stream)
    recovered = ij.PreferencesDocument.load(invalid_path, recover=True)
    emit("persistence_startup", "recovery.measure.chord", chord(ij, ij.compile_shortcuts(recovered.settings["shortcut_bindings"]), windows))
    emit("persistence_startup", "recovery.write_blocked", recovered.write_blocked)
    default_path = output / "failed-import-default.json"
    default_preferences = ij.PreferencesDocument()
    require(default_preferences.save(default_path), "Owned default Preferences save failed")
    before_file = read_bounded(default_path, MIB)
    refusal(ValueError, lambda: ij.PreferencesDocument.parse(json.dumps(invalid), recover=False))
    after_default = ij.PreferencesDocument.load(default_path, recover=False)
    emit("persistence_startup", "failed_import.measure.chord", chord(ij, ij.compile_shortcuts(after_default.settings["shortcut_bindings"]), windows))
    emit("persistence_startup", "failed_import.file_unchanged", read_bounded(default_path, MIB) == before_file)

    native_default = ij.ShortcutProfile()
    emit("native_public_contract", "default.profile.measure.chord", chord(ij, native_default, windows))
    detached = native_default.chord("analysis.measure", windows)
    detached.key = ij.ShortcutKey.K
    bindings = native_default.bindings(windows)
    next(item for item in bindings if item.command_id == "analysis.measure").chord.key = ij.ShortcutKey.K
    emit("native_public_contract", "detached.getter.measure.chord", chord(ij, native_default, windows))
    incoming = ["analysis.measure=Primary+K"]
    copied = ij.compile_shortcuts(incoming)
    incoming.clear()
    emit("native_public_contract", "detached.caller_input.measure.windows_chord", chord(ij, copied, windows))
    read_observer_positive_control(ij, np, actual)
    observer = ij.DatasetReadObserver()
    observed = ij.Dataset(np.array([[1, 8, 3], [4, 9, 6], [7, 2, 5]], dtype=np.uint8), axes="YX").observe_reads(observer)
    owner = ij.Document(observed,
                        "Shortcut native dependency")
    before = owner.snapshot()
    before_revisions = {name: getattr(before.revisions, name) for name in ("pixels", "metadata", "selection", "display")}
    before_reads = read_counts(observer)
    require(before_reads == {"plane_reads": 0, "region_reads": 0, "overflowed": False},
            "Fresh observed no-I/O fixture already acquired source samples")
    settings = ij.Settings()
    settings["shortcut_bindings"] = ["analysis.measure=Primary+K"]
    native_profile = ij.compile_shortcuts(settings["shortcut_bindings"])
    refusal(ij.ShortcutProfileError, lambda: ij.assign_shortcut(native_profile, "analysis.measure", "Primary+S"))
    after = owner.snapshot()
    after_revisions = {name: getattr(after.revisions, name) for name in before_revisions}
    after_reads = read_counts(observer)
    require(not after_reads["overflowed"], "Native observer overflow cannot supply exact configuration read values")
    emit("native_public_contract", "native.provider_plane_reads", after_reads["plane_reads"])
    emit("native_public_contract", "native.provider_region_reads", after_reads["region_reads"])
    emit("native_public_contract", "native.document_revision_delta", sum(after_revisions[name] - value for name, value in before_revisions.items()))
    actual.auxiliary.append({"id": "native.document.before_after", "document_id": owner.id,
                             "before_revisions": before_revisions, "after_revisions": after_revisions,
                             "shares_data": after.shares_data(before), "dirty_before": before.dirty, "dirty_after": after.dirty,
                             "undo_steps": owner.undo_steps, "redo_steps": owner.redo_steps})
    actual.auxiliary.append({"id": "native.read_observer.before_after", "document_id": owner.id,
                             "before": before_reads, "after": after_reads,
                             "shares_observed_dataset": after.dataset.shares_pixels(observed),
                             "scope": "actual public observer retained by native owner; no pixel acquisition in configuration"})
    actual.auxiliary.append({"id": "owned.preference_files", "files": [pin(path, MIB) for path in
                             (preference_path, invalid_path, default_path)]})


def editor(ij, np, output, actual):
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences

    owner = ij.Document(ij.Dataset(np.array([[1, 8, 3], [4, 9, 6], [7, 2, 5]], dtype=np.uint8), axes="YX"),
                        "Shortcut live configuration")
    application = Editor(preferences=Preferences(output / "preferences.json"))
    live = application.add_native_document(owner)
    application.recording = True
    application.apply_operation("add", {"value": 1}, asynchronous=False, document_id=live.id)
    require(not application.busy and not application.error, "Real renderer-free Add did not complete")
    recorded = application.workflow
    require(recorded and any(entry.get("operation") == "add" for entry in recorded), "Real Add did not create its workflow record")
    before_pixels = live.native_snapshot().revisions.pixels
    before_workflow = application.workflow
    before_settings = application.settings.to_dict()
    application.update_settings(shortcut_bindings=["analysis.measure=Primary+K"], persist=False)
    changed_settings = application.settings.to_dict()
    changed_profile = application.shortcut_profile.override_lines()
    conflict = refusal(ij.ShortcutProfileError, lambda: application.update_settings(
        {"brush_width": 21, "history_limit": 2, "shortcut_bindings": ["analysis.measure=Primary+S"]}, persist=False))
    require(application.settings.to_dict() == changed_settings, "Refused live Settings patch changed its owner")
    detached = application.shortcut_profile
    detached.override_lines().clear()
    detached.chord("analysis.measure", ij.ShortcutPlatform.Windows).key = ij.ShortcutKey.M
    require(application.shortcut_profile.override_lines() == changed_profile, "Detached profile mutation changed live state")
    after_pixels = live.native_snapshot().revisions.pixels
    after_workflow = application.workflow
    require(after_workflow == before_workflow, "Shortcut configuration changed recorded operation contents")
    actual.emit("editor_atomicity", "config.pixel_revision_delta", after_pixels - before_pixels)
    actual.emit("editor_atomicity", "config.workflow_delta", len(after_workflow) - len(before_workflow))
    actual.auxiliary.append({"id": "live.configuration.before_after", "document_id": live.id,
                             "native_owner_id": live.native_owner.id, "recording": application.recording,
                             "pixel_revision_before": before_pixels, "pixel_revision_after": after_pixels,
                             "workflow_before": before_workflow, "workflow_after": after_workflow,
                             "settings_before": before_settings, "settings_after": application.settings.to_dict(),
                             "profile_after": application.shortcut_profile.override_lines(),
                             "conflict_python_type": type(conflict).__name__,
                             "preferences_file_exists": (output / "preferences.json").exists(),
                             "scope": "actual renderer-free public Editor API; no ImGui or OS input"})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("headless", "editor"), required=True)
    parser.add_argument("--origins", required=True)
    parser.add_argument("--directory", required=True)
    args = parser.parse_args()
    require(sys.flags.isolated and sys.dont_write_bytecode, "Launch the actual interpreter with -I -B")
    started = time.monotonic()
    origin_path = canonical_path(args.origins)
    source = Path(__file__).resolve(strict=True)
    source_before = pin(source, MIB)
    approved = origins(origin_path, args.mode, loaded=False)
    output = Path(args.directory)
    require(output.is_absolute() and str(output.resolve()) == args.directory and not output.exists(), "Fresh absent canonical owned directory required")
    canonical_path(str(output.parent))
    output.mkdir()
    actual = Actual(args.mode)
    runtime_before = None
    failure = None
    try:
        select_installed_sites(approved)
        import imagejxx as ij
        import numpy as np
        if args.mode == "editor":
            import imagejxx.ui.app  # noqa: F401 - required actual installed origin.
            import imagejxx.ui.persistence  # noqa: F401 - required actual installed origin.
            from imagejxx import _desktop  # noqa: F401 - required actual origin.
        runtime_before = origins(origin_path, args.mode, loaded=True)
        require(runtime_before == approved, "Origin bytes changed before execution")
        (headless if args.mode == "headless" else editor)(ij, np, output, actual)
        actual.finish()
        runtime_after = origins(origin_path, args.mode, loaded=True)
        require(runtime_after == runtime_before and pin(source, MIB) == source_before, "Source/runtime changed during execution")
        require(time.monotonic() - started < 30, "Finite producer exceeded30s; parent must impose hard deadline")
    except BaseException as error:
        failure = error
        runtime_after = None
    missing = [{"contract_id": "SC.G012.shortcuts." + group, "id": name, "reason": reason}
               for (group, name), reason in MISSING.items()]
    raw = {"schema_version": 1, "producer_id": "p06h.shortcuts.python-api.raw.001." + args.mode,
           "context": "python_api", "mode": args.mode, "complete": failure is None, "acceptance_claim": False,
           "scope": "public core API" if args.mode == "headless" else "renderer-free public Editor API supplement",
           "source": source_before, "runtime_before": runtime_before, "runtime_after": runtime_after,
           "python": {"executable": str(Path(sys.executable).resolve(strict=True)), "version": sys.version,
                      "process_id": os.getpid()},
           "required_assertions_total": sum(len(value[1]) for value in CONTRACTS.values()),
           "required_contracts": [{"id": "SC.G012.shortcuts." + group, "sha256": digest,
                                   "assertion_ids": list(names)} for group, (digest, names) in CONTRACTS.items()],
           "observations": actual.rows, "auxiliary": actual.auxiliary, "missing_public_observers": missing,
           "other_process_assertion_ids": [{"contract_id": "SC.G012.shortcuts." + group, "id": name}
               for group, (_, names) in CONTRACTS.items() for name in names
               if (group, name) not in MISSING and (((group, name) in EDITOR_IDS) != (args.mode == "editor"))],
           "elapsed_seconds": time.monotonic() - started,
           "error": None if failure is None else {"type": type(failure).__name__, "message": str(failure)}}
    data = json.dumps(raw, ensure_ascii=False, indent=2, allow_nan=False).encode("utf-8") + b"\n"
    require(len(data) <= MAX_RAW, "Actual raw output exceeds16MiB")
    with (output / "shortcut-api-observations.json").open("xb") as stream:
        stream.write(data)
    if failure is not None:
        raise failure
    print(json.dumps({"mode": args.mode, "actual_rows": len(actual.rows), "required_total": 67,
                      "missing_public_observers": len(missing), "output": str(output / "shortcut-api-observations.json")}))


if __name__ == "__main__":
    main()
