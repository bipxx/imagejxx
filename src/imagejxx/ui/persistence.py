"""Portable session archives, ROI files and preferences; no pickled objects."""

from __future__ import annotations
import base64
from collections.abc import MutableMapping
import json
import math
import os
from pathlib import Path
import struct
import tempfile
import threading
import zlib

import numpy as np
import imagejxx as ij
from imagejxx import PreferencesDocument, Settings
from .model import Document, NamedROI, _sample_policy_from_metadata


DEFAULTS = {**Settings().to_dict(), "recent": []}
_SESSION_PUBLICATION_LOCK = threading.RLock()


def default_preferences_path():
    directory = os.environ.get("IMAGEJXX_CONFIG_DIR")
    if directory:
        return Path(directory) / "preferences.json"
    directory = os.environ.get("LOCALAPPDATA" if os.name == "nt" else "XDG_CONFIG_HOME")
    if directory:
        return Path(directory) / "imagejxx" / "preferences.json"
    if os.name != "nt" and os.environ.get("HOME"):
        return Path(os.environ["HOME"]) / ".config" / "imagejxx" / "preferences.json"
    return Path("imagejxx-preferences.json")


def _reject_constant(value):
    raise ValueError(f"Nonfinite JSON value: {value}")


def _validate_session_json_numbers(value):
    # Pixel NaNs live only in the opaque typed binary payload. Numeric metadata,
    # results and workflow fields never use JSON's NaN/null compatibility path.
    pending = [value]
    while pending:
        item = pending.pop()
        if isinstance(item, float) and not math.isfinite(item):
            raise ValueError("Session JSON numbers must be finite")
        if isinstance(item, dict):
            pending.extend(item.keys())
            pending.extend(item.values())
        elif isinstance(item, (list, tuple)):
            pending.extend(item)


def _preferences_compartments(content):
    if not isinstance(content, dict):
        raise ValueError("Preferences must contain an object")
    legacy = "format" not in content
    if legacy:
        return content, {}, {}, True
    if (
        content.get("format") != "imagejxx-preferences"
        or type(content.get("version")) is not int
        or content["version"] != Settings.schema_version
    ):
        raise ValueError(
            "Unsupported preferences format/version; original file preserved"
        )
    values, extras = content.get("values"), content.get("extras", {})
    if not isinstance(values, dict) or not isinstance(extras, dict):
        raise ValueError("Preferences values/extras must be objects")
    fields = {
        k: v
        for k, v in content.items()
        if k not in {"format", "version", "values", "extras"}
    }
    return values, extras, fields, False


def _needs_python_representation(value):
    """Detect opaque JSON that the native codec cannot retain losslessly."""
    pending = [value]
    while pending:
        item = pending.pop()
        if type(item) is int and not -(2**63) <= item <= 2**64 - 1:
            return True
        if isinstance(item, float) and not math.isfinite(item):
            return True
        if isinstance(item, str) and any(0xD800 <= ord(c) <= 0xDFFF for c in item):
            return True
        if isinstance(item, dict):
            pending.extend(item.keys())
            pending.extend(item.values())
        elif isinstance(item, (list, tuple)):
            pending.extend(item)
    return False


class Preferences:
    def __init__(self, path=None):
        self.path = Path(path) if path else default_preferences_path()
        self.values = {**Settings().to_dict(), "recent": []}
        self.warnings = []
        self._unknown_values, self._unknown_fields = {}, {}
        self._shadowed_extras = {}
        self._recovered = False
        source = self.path
        legacy = Path.home() / ".imagejxx" / "preferences.json"
        if (
            path is None
            and not os.environ.get("IMAGEJXX_CONFIG_DIR")
            and not source.exists()
            and legacy.exists()
        ):
            source = legacy
        if not source.exists():
            return
        try:
            # Read once, retaining Python's established JSON representation for
            # the explicit opaque compatibility boundary. Native parse sees
            # these same decoded bytes rather than reopening a changing file.
            content = json.loads(
                source.read_text(encoding="utf-8"), parse_constant=_reject_constant
            )
            values, extras, fields, legacy = _preferences_compartments(content)
            known = {d["name"] for d in Settings.schema()}
            unknown = {k: v for k, v in values.items() if k not in known}
            if _needs_python_representation((unknown, extras, fields)):
                self._load_compatibility(values, extras, fields, legacy, known)
            else:
                migration_warnings = []
                recognized = {k: v for k, v in values.items() if k in known}
                if _needs_python_representation(recognized):
                    # Invalid known values must still migrate through Settings.
                    # They cannot force an opaque fallback or become floats.
                    settings, migration_warnings = Settings.migrate(
                        recognized, legacy=legacy
                    )
                    normalized = {**unknown, **settings.to_dict()}
                    content = (
                        normalized if legacy else {**content, "values": normalized}
                    )
                document = PreferencesDocument.parse(
                    json.dumps(content, allow_nan=False), recover=True
                )
                self._adopt_native(document, known)
                self.warnings = [*migration_warnings, *self.warnings]
                self._recovered = bool(self.warnings) or document.write_blocked
        except (OSError, ValueError, TypeError, OverflowError) as error:
            self.values = {**Settings().to_dict(), "recent": []}
            self.warnings = [f"Could not load preferences: {error}"]
            self._recovered = True

    def _adopt_native(self, document, known):
        extras = json.loads(document.extras_json)
        self._unknown_values = json.loads(document.unknown_values_json)
        self._unknown_fields = json.loads(document.unknown_fields_json)
        self._shadowed_extras = {k: v for k, v in extras.items() if k in known}
        self.values = {**extras, **document.settings.to_dict()}
        self.warnings = list(document.warnings)

    def _load_compatibility(self, values, extras, fields, legacy, known):
        """Retain unsupported opaque JSON with the existing Python contract."""
        self._unknown_fields = fields
        extras = dict(extras)
        self._shadowed_extras = {k: v for k, v in extras.items() if k in known}
        settings, self.warnings = Settings.migrate(
            {k: v for k, v in values.items() if k in known}, legacy=legacy
        )
        unknown = {k: v for k, v in values.items() if k not in known}
        if legacy:
            extras.update(unknown)
        else:
            self._unknown_values = unknown
        self.values = {**extras, **settings.to_dict()}
        recent = extras.get("recent", [])
        if (
            not isinstance(recent, list)
            or len(recent) > 15
            or not all(isinstance(p, str) for p in recent)
        ):
            recent = []
            self.warnings.append("Invalid recent file list; using an empty list")
        self.values["recent"] = recent
        if "layout" in self.values:
            try:
                ij._native.validate_preferences_layout(self.values["layout"])
            except (ValueError, TypeError, UnicodeError):
                self.values.pop("layout")
                self.warnings.append(
                    "Invalid saved window layout; using the default layout"
                )
        self._recovered = bool(self.warnings)

    def save(self):
        known = {d["name"] for d in Settings.schema()}
        settings = Settings({k: v for k, v in self.values.items() if k in known})
        extras = {
            **self._shadowed_extras,
            **{k: v for k, v in self.values.items() if k not in known},
        }
        recent = extras.get("recent", [])
        if (
            not isinstance(recent, list)
            or len(recent) > 15
            or not all(isinstance(p, str) for p in recent)
        ):
            raise ValueError("Invalid recent file list")
        if "layout" in extras:
            ij._native.validate_preferences_layout(extras["layout"])
        content = {
            **self._unknown_fields,
            "format": "imagejxx-preferences",
            "version": Settings.schema_version,
            "values": {**self._unknown_values, **settings.to_dict()},
            "extras": extras,
        }
        # Always validate known Settings first, including opaque compatibility
        # saves. Representation support alone chooses the fallback; native
        # validation and publication errors propagate without retrying in Python.
        text = json.dumps(content, indent=2, allow_nan=False)
        if _needs_python_representation(
            (extras, self._unknown_values, self._unknown_fields)
        ):
            self._save_compatibility(text)
        else:
            document = PreferencesDocument()
            document.settings = settings
            document.extras_json = json.dumps(extras, allow_nan=False)
            document.unknown_values_json = json.dumps(
                self._unknown_values, allow_nan=False
            )
            document.unknown_fields_json = json.dumps(
                self._unknown_fields, allow_nan=False
            )
            document.write_blocked = self._recovered
            if not document.save(self.path, explicit_save=self._recovered):
                return
        self.warnings = []
        self._recovered = False

    def _save_compatibility(self, text):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        if self._recovered and self.path.exists():
            original = self.path.read_bytes()
            backup = Path(str(self.path) + ".bak")
            suffix = 1
            while True:
                try:
                    stream = backup.open("xb")
                except FileExistsError:
                    backup = Path(str(self.path) + f".bak.{suffix}")
                    suffix += 1
                    continue
                with stream:
                    stream.write(original)
                break
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(
                mode="w",
                encoding="utf-8",
                dir=self.path.parent,
                prefix=self.path.name + ".tmp.",
                delete=False,
            ) as stream:
                temporary = Path(stream.name)
                stream.write(text)
            temporary.replace(self.path)
        finally:
            if temporary is not None and temporary.exists():
                temporary.unlink()

    def recent(self, path):
        filename = str(Path(path).resolve())
        self.values["recent"] = [
            filename,
            *(p for p in self.values.get("recent", []) if p != filename),
        ][:15]


def save_rois(path, rois):
    Path(path).write_text(
        json.dumps(
            {
                "format": "imagejxx-rois",
                "version": 1,
                "rois": [r.to_dict() for r in rois],
            },
            indent=2,
        ),
        encoding="utf-8",
    )


def load_rois(path, shape):
    content = json.loads(Path(path).read_text(encoding="utf-8"))
    if content.get("format") != "imagejxx-rois" or content.get("version") != 1:
        raise ValueError("Not an imagejxx ROI file")
    rois = [NamedROI.from_dict(value) for value in content["rois"]]
    for roi in rois:
        roi.selection.mask(shape)
    return rois


# Native marker state belongs to the workspace root. These helpers operate on
# detached JSON values and never encode or inspect image sample buffers.
_SESSION_TRANSPORT_FIELDS = frozenset({
    "pixels", "dtype", "shape", "spectrum", "fft_shape", "spectrum_shape",
    "pixel_sample_policy",
})
_SESSION_DISPLAY_FIELDS = ("display_range", "lut", "custom_lut")
_SESSION_METADATA_FIELDS = frozenset({
    "title", "properties", "extensions", "record_layout",
})


def _session_json_equal(left, right):
    """Compare JSON values without numeric coercion or loss of signed zero."""
    pending = [(left, right)]
    while pending:
        first, second = pending.pop()
        if type(first) is not type(second):
            return False
        if type(first) is float:
            if struct.pack(">d", first) != struct.pack(">d", second):
                return False
        elif type(first) is dict:
            if first.keys() != second.keys():
                return False
            pending.extend((value, second[key]) for key, value in first.items())
        elif type(first) is list:
            if len(first) != len(second):
                return False
            pending.extend(zip(first, second))
        elif first != second:
            return False
    return True


def _validate_marked_json(value):
    """Require lossless native JSON values and reject active reference cycles."""
    pending = [(value, False)]
    active = set()
    while pending:
        item, leaving = pending.pop()
        if leaving:
            active.remove(id(item))
            continue
        kind = type(item)
        if item is None or kind is bool:
            continue
        if kind is int:
            if not -(2**63) <= item <= 2**64 - 1:
                raise ValueError("Marked session JSON integer exceeds the native domain")
        elif kind is float:
            if not math.isfinite(item):
                raise ValueError("Session JSON numbers must be finite")
        elif kind is str:
            if not item.isascii():
                try:
                    item.encode("utf-8")
                except UnicodeEncodeError as error:
                    raise ValueError("Marked session strings must be valid UTF-8") from error
        elif kind in (dict, list, tuple):
            identity = id(item)
            if identity in active:
                raise ValueError("Marked session JSON contains a reference cycle")
            active.add(identity)
            pending.append((item, True))
            if kind is dict:
                if any(type(key) is not str for key in item):
                    raise ValueError("Marked session JSON object keys must be strings")
                pending.extend((key, False) for key in item)
                pending.extend((child, False) for child in item.values())
            else:
                # Established user table tuples encode as arrays; protocol
                # comparisons operate on parsed lists with exact JSON types.
                pending.extend((child, False) for child in item)
        else:
            raise ValueError("Marked session contains an unsupported JSON value")


def _session_scalar_wire(item):
    if type(item) is not dict:
        raise ValueError("Session document must be an object")
    return {
        key: value for key, value in item.items()
        if key not in _SESSION_TRANSPORT_FIELDS
    }


def _session_display_map(item):
    return {key: item[key] for key in _SESSION_DISPLAY_FIELDS if key in item}


def _validate_session_native_metadata(metadata):
    if (
        type(metadata) is not dict
        or metadata.keys() != _SESSION_METADATA_FIELDS
        or type(metadata["title"]) is not str
        or any(type(metadata[key]) is not dict for key in (
            "properties", "extensions", "record_layout",
        ))
    ):
        raise ValueError("Invalid native session document metadata")
    _validate_marked_json(metadata)
    if any(not key for key in metadata["properties"]):
        raise ValueError("Metadata property names must be nonempty")
    if _SESSION_TRANSPORT_FIELDS.intersection(metadata["extensions"]):
        raise ValueError("Native session metadata cannot shadow transport fields")


def _session_dimensions(item):
    shape = item.get("shape")
    stack = item.get("is_stack")
    if (
        type(stack) is not bool or type(shape) is not list
        or any(type(n) is not int or not 0 < n <= 2**31 - 1 for n in shape)
        or type(item.get("dtype")) is not str
        or item["dtype"] not in ("uint8", "uint16", "float32", "float64")
        or type(item.get("pixels")) is not str
    ):
        raise ValueError("Invalid marked session pixel transport")
    offset = 1 if stack else 0
    if len(shape) not in (offset + 2, offset + 3):
        raise ValueError("Invalid marked session pixel shape")
    channels = shape[-1] if len(shape) == offset + 3 else 1
    if channels not in (1, 3, 4) or math.prod(shape) * 8 > 1024**3:
        raise ValueError("Invalid marked session pixel dimensions")
    return shape[offset + 1], shape[offset], shape[0] if stack else 1


def _session_spectrum_shape(item):
    if "spectrum" not in item:
        if "fft_shape" in item or item.get("spectrum_shape") is not None:
            raise ValueError("Marked session spectrum shape requires spectrum data")
        return None
    shape = item.get("fft_shape")
    if (
        type(item["spectrum"]) is not str or not item["spectrum"]
        or type(shape) is not list or len(shape) != 2
        or any(type(n) is not int or not 0 < n <= 2**31 - 1 for n in shape)
        or math.prod(shape) * 16 > 1024**3
    ):
        raise ValueError("Invalid marked session Fourier spectrum")
    original = item.get("spectrum_shape")
    if original is not None and (
        type(original) is not list or len(original) != 2
        or any(type(n) is not int or not 0 < n <= shape[i]
               for i, n in enumerate(original))
    ):
        raise ValueError("Invalid marked session original spectrum shape")
    return None if original is None else tuple(original)


def _session_legacy_state(item, dimensions):
    from imagejxx import _native

    wire = _session_scalar_wire(item)
    _validate_marked_json(wire)
    state = json.loads(_native._legacy_session_state_json(
        json.dumps(wire, allow_nan=False), dimensions[2],
    ))
    _validate_session_native_metadata(state["metadata"])
    return state


def _session_project_wire(metadata, wire, dimensions):
    from imagejxx import _native

    _validate_session_native_metadata(metadata)
    _validate_marked_json(wire)
    projected = json.loads(_native._session_wire_metadata_json(
        json.dumps(metadata, allow_nan=False), json.dumps(wire, allow_nan=False),
        *dimensions,
    ))
    if type(projected) is not dict or _SESSION_TRANSPORT_FIELDS.intersection(projected):
        raise ValueError("Invalid native scalar session projection")
    _validate_marked_json(projected)
    return projected


def _session_saved_native_records(owners, items):
    retained = [owner._session_native_state() for owner in owners]
    if not any(state is not None for state in retained):
        return items, None
    inferred = []
    dimensions = []
    needs_marker = False
    for item, state in zip(items, retained):
        size = _session_dimensions(item)
        _session_spectrum_shape(item)
        dimensions.append(size)
        legacy = _session_legacy_state(item, size)
        inferred.append(legacy)
        if state is not None:
            if (
                type(state) is not dict
                or state.keys() != {"metadata", "display_properties"}
                or type(state["display_properties"]) is not dict
            ):
                raise ValueError("Invalid retained native session state")
            _validate_marked_json(state)
            _validate_session_native_metadata(state["metadata"])
            if not _session_json_equal(
                state["display_properties"], _session_display_map(item),
            ):
                raise ValueError("Native display state differs from its serialized document")
            needs_marker |= not _session_json_equal(state, legacy)
    if not needs_marker:
        return items, None
    output, records = [], []
    for item, state, legacy, size in zip(items, retained, inferred, dimensions):
        metadata = (legacy if state is None else state)["metadata"]
        wire = _session_project_wire(metadata, _session_scalar_wire(item), size)
        # The core canonicalizes scalar metadata for every owner of a marked
        # workspace. Transport values remain the single existing encoding.
        regenerated = _session_project_wire(metadata, wire, size)
        if not _session_json_equal(regenerated, wire):
            raise ValueError("Generated native session metadata is not reloadable")
        final = {
            **wire,
            **{key: value for key, value in item.items()
               if key in _SESSION_TRANSPORT_FIELDS},
        }
        if _session_dimensions(final) != size:
            raise ValueError("Generated native session geometry differs from its transport")
        _session_spectrum_shape(final)
        output.append(final)
        records.append({
            "display_properties": [key for key in _SESSION_DISPLAY_FIELDS if key in wire],
            "metadata": metadata,
        })
    return output, records


def _session_loaded_native_records(manifest):
    marker = manifest["native_document_state"]
    items = manifest.get("documents")
    if (
        type(marker) is not dict or marker.keys() != {"version", "documents"}
        or type(marker["version"]) is not int or marker["version"] != 1
        or type(marker["documents"]) is not list or type(items) is not list
        or len(marker["documents"]) != len(items)
    ):
        raise ValueError("Invalid native session document state")
    if type(manifest.get("results")) is not list or type(manifest.get("workflow")) is not list:
        raise ValueError("Invalid marked session tables")
    records = marker["documents"]
    # Validate every declaration before constructing any detached owner.
    for item, entry in zip(items, records):
        if (
            type(entry) is not dict or entry.keys() != {"display_properties", "metadata"}
            or type(entry["display_properties"]) is not list
            or any(type(key) is not str for key in entry["display_properties"])
        ):
            raise ValueError("Invalid native session document-state entry")
        wire = _session_scalar_wire(item)
        expected = [key for key in _SESSION_DISPLAY_FIELDS if key in wire]
        if entry["display_properties"] != expected:
            raise ValueError("Display-property presence differs from serialized document")
        _validate_session_native_metadata(entry["metadata"])
        size = _session_dimensions(item)
        _session_spectrum_shape(item)
        generated = _session_project_wire(entry["metadata"], wire, size)
        if not _session_json_equal(generated, wire):
            raise ValueError("Native session metadata/view contradicts its serialized document")
    return records


def save_session(path, documents, *, results=(), workflow=()):
    destination = Path(path)
    manifest = {
        "format": "imagejxx-session",
        "version": 2,
        "documents": [],
        "results": list(results),
        "workflow": list(workflow),
    }
    owners = list(documents)
    items = [serialize_document(document) for document in owners]
    manifest["documents"], records = _session_saved_native_records(owners, items)
    if records is not None:
        manifest["native_document_state"] = {"version": 1, "documents": records}
        _validate_marked_json(manifest)
    content = json.dumps(manifest, allow_nan=False).encode("utf-8")
    if len(content) > 1024**3:
        raise ValueError("Session exceeds the 1 GiB interchange limit")
    encoded = b"IMAGEJXX2\n" + struct.pack(">I", len(content)) + zlib.compress(content)
    regional = any(
        (owner.selection is not None and owner.selection.kind == "region")
        or (owner.previous_selection is not None and owner.previous_selection.kind == "region")
        or any(roi.selection.kind == "region" for roi in owner.rois)
        for owner in owners
    )
    if regional:
        from imagejxx import _native
        encoded = _native._upgrade_regional_legacy_session(encoded)
    stream = None
    temporary = None
    try:
        stream = tempfile.NamedTemporaryFile(
            mode="wb", dir=destination.parent, prefix=".ijxs-", suffix=".tmp",
            delete=False,
        )
        temporary = Path(stream.name)
        if stream.write(encoded) != len(encoded):
            raise OSError("Incomplete session temporary write")
        stream.flush()
        stream.close()
        stream = None
        with _SESSION_PUBLICATION_LOCK:
            temporary.replace(destination)
    except BaseException as primary:
        cleanup_error = None
        if stream is not None:
            try:
                stream.close()
            except BaseException as error:
                cleanup_error = error
        if temporary is not None:
            try:
                temporary.unlink(missing_ok=True)
            except BaseException as error:
                cleanup_error = error
        if cleanup_error is not None:
            raise primary from cleanup_error
        raise


def load_session(path, *, extensions=None):
    """Load detached documents, results and workflow from a session archive.

    Native v3/v4 archives use the bounded C++ codec. Their opaque root extensions
    require an explicit mutable mapping destination, replaced after every
    document has been validated and converted to the legacy Python model.
    """
    if extensions is not None and not isinstance(extensions, MutableMapping):
        raise TypeError("Session extensions destination must be a mutable mapping")
    source = Path(path)
    with source.open("rb") as stream:
        header = stream.read(10)
    if header in (b"IMAGEJXX3\n", b"IMAGEJXX4\n"):
        from imagejxx import _native

        decoded = ij.load_session(str(source))
        next_extensions = decoded.extensions
        if next_extensions and extensions is None:
            raise ValueError("A session extensions destination is required")
        documents = []
        for owner in decoded.documents:
            record = json.loads(_native._session_legacy_record_json(owner.snapshot()))
            documents.append(_deserialize_document(
                record["document"], native_state=record["native_state"],
            ))
        results, workflow = decoded.results, decoded.workflow
        if extensions is not None:
            extensions.clear()
            extensions.update(next_extensions)
        return documents, results, workflow
    documents = []
    content = source.read_bytes()
    if content[:10] != b"IMAGEJXX2\n" or len(content) < 15:
        raise ValueError("Not an imagejxx session")
    size = struct.unpack(">I", content[10:14])[0]
    if size > 1024**3:
        raise ValueError("Session exceeds the 1 GiB interchange limit")
    decoder = zlib.decompressobj()
    decoded = decoder.decompress(content[14:], size + 1)
    if len(decoded) != size or not decoder.eof or decoder.unused_data:
        raise ValueError("Invalid compressed session")
    duplicate_keys = False

    def object_pairs(pairs):
        nonlocal duplicate_keys
        result = {}
        for key, value in pairs:
            if key in result:
                duplicate_keys = True
            result[key] = value
        return result

    manifest = json.loads(
        decoded, parse_constant=_reject_constant, object_pairs_hook=object_pairs,
    )
    marked = type(manifest) is dict and "native_document_state" in manifest
    if marked:
        if b"\x00" in decoded:
            raise ValueError("Marked session JSON must use UTF-8")
        if duplicate_keys:
            raise ValueError("Marked session JSON contains duplicate object keys")
        _validate_marked_json(manifest)
    _validate_session_json_numbers(manifest)
    if manifest.get("format") != "imagejxx-session" or manifest.get("version") != 2:
        raise ValueError("Unsupported session version")
    if marked:
        records = _session_loaded_native_records(manifest)
        documents = [
            _deserialize_document(item, native_state=entry)
            for item, entry in zip(manifest["documents"], records)
        ]
    else:
        documents = [deserialize_document(item) for item in manifest["documents"]]
    return documents, manifest.get("results", []), manifest.get("workflow", [])


def serialize_document(document):
    """Encode a document using the shared native/Python interchange representation."""
    item = document.metadata()
    if "channel_display" in item:
        raise ValueError("Channel display cannot be exported through detached V2 sessions")
    policy = _sample_policy_from_metadata(item)
    if policy != document.sample_policy:
        raise ValueError("Document metadata differs from its sample policy")
    _validate_session_json_numbers(item)
    if policy in (ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32, ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32):
        pixels = document.data
        if pixels.dtype != np.dtype(np.float32) or pixels.ndim != (
            3 if document.is_stack else 2
        ):
            raise ValueError("Quiet-NaN session policy requires grayscale Float32")
        # Both native helpers inspect IEEE fields. Do not float-cast a NaN
        # source to build the old opaque binary64 wire representation.
        raw = b"".join(
            struct.pack(
                "<d", ij.float32_source_sample(
                    ij.float32_source_sample_bits(sample, sample_policy=policy),
                    sample_policy=policy,
                ),
            )
            for sample in pixels.reshape(-1)
        )
    else:
        # Preserve exactly the established finite v2 byte encoding.
        raw = document.data.astype("<f8").tobytes()
    item.update(
        dtype=document.data.dtype.name,
        shape=list(document.data.shape),
        pixels=base64.b64encode(raw).decode("ascii"),
    )
    if document.spectrum is not None:
        if document.spectrum.ndim != 2 or not np.isfinite(document.spectrum).all():
            raise ValueError("Invalid stored Fourier spectrum")
        item["spectrum"] = base64.b64encode(
            document.spectrum.astype("<c16").tobytes()
        ).decode("ascii")
        item["fft_shape"] = list(document.spectrum.shape)
    return item


def deserialize_document(item):
    """Decode a detached document snapshot; no native pointers or executable data."""
    return _deserialize_document(item)


def _deserialize_document(item, *, native_state=None):
    if not isinstance(item, dict):
        raise ValueError("Session document must be an object")
    if "channel_display" in item:
        raise ValueError("Channel display requires native V3 sessions")
    _validate_session_json_numbers(item)
    policy = _sample_policy_from_metadata(item)
    if policy in (ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32, ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32) and (
        type(item.get("is_stack")) is not bool
        or not isinstance(item.get("shape"), (list, tuple))
        or any(type(n) is not int or n <= 0 or n > 2**31 - 1 for n in item["shape"])
    ):
        raise ValueError("Invalid quiet-NaN session shape")
    shape = tuple(int(n) for n in item["shape"])
    if (
        len(shape) not in (2, 3, 4)
        or min(shape) <= 0
        or item["dtype"] not in ("uint8", "uint16", "float32", "float64")
    ):
        raise ValueError("Invalid session pixels")
    is_stack = bool(item["is_stack"])
    if policy in (ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32, ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32) and (
        item["dtype"] != "float32" or len(shape) != (3 if is_stack else 2)
    ):
        raise ValueError("Quiet-NaN session policy requires grayscale Float32")
    raw = base64.b64decode(item["pixels"], validate=True)
    if len(raw) != int(np.prod(shape, dtype=object)) * 8:
        raise ValueError("Truncated session pixels")
    represented = np.frombuffer(raw, "<f8")
    if policy in (ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32, ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32):
        # Validate the original represented binary64 sample before narrowing.
        # NumPy scalar storage reaches the native encoder without NaN arithmetic.
        bits = np.fromiter(
            (ij.float32_source_sample_bits(sample, sample_policy=policy)
             for sample in represented),
            dtype=np.uint32, count=represented.size,
        )
        data = bits.view(np.float32).reshape(shape)
    else:
        if not np.isfinite(represented).all():
            raise ValueError("Finite-only session pixels must be finite")
        if native_state is None:
            data = represented.reshape(shape).astype(item["dtype"])
        else:
            with np.errstate(over="ignore", invalid="ignore"):
                data = represented.reshape(shape).astype(item["dtype"])
            canonical = data.astype("<f8").reshape(-1).view(np.uint64)
            if not np.array_equal(represented.view(np.uint64), canonical):
                raise ValueError("Marked session pixels are not canonically represented")
    document = Document(
        data, item.get("title", "Untitled"), is_stack=is_stack, sample_policy=policy
    )
    if native_state is None:
        document.load_metadata(item)
    else:
        document._adopt_session_native_state(
            native_state["metadata"], _session_scalar_wire(item),
        )
        document.spectrum_shape = _session_spectrum_shape(item)
    if item.get("spectrum"):
        spectrum = (
            np.frombuffer(base64.b64decode(item["spectrum"], validate=True), "<c16")
            .reshape(item["fft_shape"])
            .copy()
        )
        if spectrum.ndim != 2 or not np.isfinite(spectrum).all():
            raise ValueError("Invalid stored Fourier spectrum")
        document.spectrum = spectrum
    return document
