"""Inventory pinned reference archives without installing or executing their plugins.

Only candidate metadata is generated. Embedded license declarations are evidence
to review, not commercial approval. Reference bytes stay outside distribution
packages. This tool uses Python's standard library, not the native editor runtime.
"""
from __future__ import annotations

import argparse
import csv
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import re
import tempfile
import xml.etree.ElementTree as ET
import zipfile

def _stream_sha256(stream):
    """Hash remaining binary stream bytes with bounded memory on Python 3.10+."""
    digest = hashlib.sha256()
    for block in iter(lambda: stream.read(1024 * 1024), b""):
        digest.update(block)
    return digest.hexdigest()


ROOT = Path(__file__).resolve().parents[1]
METADATA_LIMIT = 32 * 1024**2
JAR_LIMIT = 256 * 1024**2
MANIFEST_VOLATILE = {
    b"ARCHIVER-VERSION", b"BUILT-BY", b"BUILD-JDK", b"CLASS-PATH",
    b"CREATED-BY", b"IMPLEMENTATION-BUILD",
}


def sha256_file(path):
    with Path(path).open("rb") as stream:
        return _stream_sha256(stream)


def verify_archive(path, expected):
    if not isinstance(expected, str) or not re.fullmatch(r"[0-9a-f]{64}", expected):
        raise ValueError("A concrete lower-case SHA256 is required for each archive")
    if sha256_file(path) != expected:
        raise ValueError(f"Reference archive checksum mismatch: {Path(path).name}")


def zip_members(archive, nested=False):
    members = archive.infolist()
    seen = set()
    for info in members:
        name = info.orig_filename
        parts = name.rstrip("/").split("/")
        if (name != info.filename or not name or name.startswith("/") or "\\" in name or "\0" in name or
                any(p in {"", ".", ".."} or (":" in p and not nested) for p in parts)):
            raise ValueError(f"Unsafe reference member: {name!r}")
        if name in seen and not nested:
            raise ValueError(f"Duplicate reference member: {name}")
        seen.add(name)
    return sorted(members, key=lambda member: member.filename)


def limited_read(archive, name, limit=METADATA_LIMIT):
    info = name if isinstance(name, zipfile.ZipInfo) else archive.getinfo(name)
    if info.file_size > limit:
        raise ValueError(f"Reference metadata exceeds its size limit: {info.filename}")
    return archive.read(name)


def json_objects(payload):
    """SciJava indexes contain concatenated JSON objects, not a JSON array."""
    text = payload.decode("utf-8-sig")
    decoder = json.JSONDecoder()
    position = 0
    while position < len(text):
        if text[position].isspace():
            position += 1
            continue
        item, position = decoder.raw_decode(text, position)
        if not isinstance(item, dict) or not isinstance(item.get("class"), str):
            raise ValueError("SciJava index must contain class descriptors")
        yield item


def updater_properties(payload):
    """Preserve the pinned stream filter's blank-line/empty-comment behavior."""
    result, position, line_start = bytearray(), 0, True
    while position < len(payload):
        value = payload[position]
        if line_start:
            if value == ord("#"):
                end = payload.find(b"\n", position + 1)
                if end < 0:
                    break
                result.append(ord("\n"))
                line_start = bool(payload[position + 1:end].strip(b"#"))
                position = end + 1
                continue
            line_start = False
        elif value == ord("\n"):
            line_start = True
        result.append(value)
        position += 1
    return bytes(result)


def updater_jar_digests(archive):
    """Reconcile current and three historical updater fingerprint schemes.

    These are distinct from SHA1 of ZIP bytes. The pinned 2.0.3 implementation's
    current manifest filter emits no bytes; retaining only Main-Class would not
    reproduce its fingerprints. Entries (including directories) still contribute
    their names. Property comment text disappears while its newline remains.
    """
    states = {name: hashlib.sha1() for name in ("current", "legacy_manifest", "legacy_properties", "legacy_raw")}
    for info in sorted(archive.infolist(), key=lambda i: i.filename.encode("utf-16-be", "surrogatepass")):
        name = info.filename
        # Java ZipFile resolves streams by entry name. For repeated names it
        # reads the last occurrence even while enumerating an earlier entry.
        # Discovery below still retains each occurrence's original bytes.
        with archive.open(name) as stream:
            # Large ordinary entries can be streamed to all schemes.
            encoded = name.encode("ascii", "replace")
            for digest in states.values():
                digest.update(encoded)
            if name.endswith(".properties") or name == "META-INF/MANIFEST.MF":
                payload = stream.read(METADATA_LIMIT + 1)
                if len(payload) > METADATA_LIMIT:
                    raise ValueError(f"Oversized JAR metadata: {name}")
                normalized = updater_properties(payload) if name.endswith(".properties") else payload
                if name == "META-INF/MANIFEST.MF":
                    stable = b"".join(line + b"\n" for line in payload.splitlines()
                                      if line.split(b":", 1)[0].upper() not in MANIFEST_VOLATILE)
                    values = (b"", stable, payload, payload)
                else:
                    values = (normalized, normalized, normalized, payload)
                for digest, value in zip(states.values(), values):
                    digest.update(value)
            else:
                while chunk := stream.read(1024 * 1024):
                    for digest in states.values():
                        digest.update(chunk)
    return {name: digest.hexdigest() for name, digest in states.items()}


def properties(payload):
    # Maven-generated coordinate files use simple ASCII key=value records.
    result = {}
    for line in payload.decode("utf-8-sig").splitlines():
        if line.strip() and not line.lstrip().startswith(("#", "!")):
            key, separator, value = line.partition("=")
            if not separator:
                raise ValueError("Unsupported Maven coordinate property record")
            result[key.strip()] = value.strip()
    return result


def pom_metadata(payload):
    root = ET.fromstring(payload)
    for element in root.iter():
        element.tag = element.tag.rsplit("}", 1)[-1]
    def text(parent, name):
        return parent.findtext(name) if parent is not None else None
    parent = root.find("parent")
    return {
        "coordinates": {key: text(root, key) for key in ("groupId", "artifactId", "version")},
        "parent": {key: text(parent, key) for key in ("groupId", "artifactId", "version")},
        "declared_licenses": [{key: text(item, key) for key in ("name", "url", "distribution")}
                              for item in root.findall("licenses/license")],
        "source": {key: root.findtext("scm/" + key) for key in ("url", "connection", "tag")},
        "declared_dependencies": [{key: text(item, key) for key in (
            "groupId", "artifactId", "version", "classifier", "type", "scope", "optional")}
                                  for item in root.findall("dependencies/dependency")],
        "properties": {item.tag: item.text for item in root.findall("properties/*")},
        "resolution_status": "parent/BOM/properties/transitive review pending",
    }


def candidate(component, kind, key, data, evidence):
    identity = json.dumps([component, kind, evidence.get("jar_member", evidence.get("archive_member")),
                          evidence.get("entry_occurrence"), key],
                          ensure_ascii=True, separators=(",", ":"))
    return {
        "id": "R-" + hashlib.sha256(identity.encode()).hexdigest()[:24],
        "component_id": component, "kind": kind, "source_key": key,
        "descriptor": data, "evidence": evidence,
        "status": "candidate", "assignment": None, "option_inventory": None,
        "runtime_reconciliation": "pending", "commercial_review": "pending",
    }


def legacy_commands(component, payload, evidence):
    result = []
    for number, line in enumerate(payload.decode("utf-8-sig").splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        cells = next(csv.reader([line], skipinitialspace=True))
        if len(cells) >= 3:
            data = {"menu": cells[0], "label": cells[1], "invocation": ",".join(cells[2:]), "raw": line}
            kind = "legacy_menu"
        else:
            data, kind = {"raw": line}, "unparsed_legacy_descriptor"
        result.append(candidate(component, kind, str(number), data, evidence))
    return result


def imagej_commands(payload, evidence):
    result, context = [], None
    for number, line in enumerate(payload.decode("utf-8-sig").splitlines(), 1):
        if line.lstrip().startswith("#"):
            if "submenu" in line or "menu" in line:
                context = line.lstrip(" #")
            continue
        key, separator, value = line.partition("=")
        if not separator or not key.strip():
            continue
        data = {"property": key.strip(), "value": value, "menu_context": context}
        result.append(candidate("imagej-core", "imagej_property", key.strip(), data,
                                {**evidence, "line": number}))
    return result


def jar_metadata(payload, component, path):
    candidates, poms, coordinates, licenses, resources = [], [], [], [], []
    with zipfile.ZipFile(io.BytesIO(payload)) as jar:
        # JAR resource names may contain colons and shaded archives may repeat
        # metadata members. Preserve each occurrence; nothing is extracted.
        entries = zip_members(jar, nested=True)
        digests = updater_jar_digests(jar)
        occurrences = {}
        for info in entries:
            name = info.filename
            occurrence = occurrences.get(name, 0)
            occurrences[name] = occurrence + 1
            evidence = {"archive_member": path, "jar_member": name, "entry_occurrence": occurrence}
            if not info.is_dir() and not name.endswith(".class"):
                with jar.open(info) as stream:
                    digest = _stream_sha256(stream)
                resources.append({"member": name, "entry_occurrence": occurrence,
                                  "bytes": info.file_size, "sha256": digest,
                                  "license_review": "pending"})
            if name.endswith("/pom.xml") and name.startswith("META-INF/maven/"):
                data = limited_read(jar, info)
                poms.append({"member": name, "entry_occurrence": occurrence,
                             "sha256": hashlib.sha256(data).hexdigest(), **pom_metadata(data)})
            elif name.endswith("/pom.properties") and name.startswith("META-INF/maven/"):
                coordinates.append({"member": name, "entry_occurrence": occurrence, **properties(limited_read(jar, info))})
            elif name.endswith("org.scijava.plugin.Plugin") and name.startswith("META-INF/json/"):
                for index, descriptor in enumerate(json_objects(limited_read(jar, info))):
                    candidates.append(candidate(component, "scijava_plugin", str(index), descriptor, evidence))
            elif name == "plugins.config" or name.endswith("/plugins.config"):
                candidates.extend(legacy_commands(component, limited_read(jar, info), evidence))
            elif not info.is_dir() and re.search(r"(?:^|/)(?:LICENSE|LICENCE|COPYING|NOTICE|COPYRIGHT)(?:[._-].*)?$", name, re.I):
                data = limited_read(jar, info)
                licenses.append({"member": name, "entry_occurrence": occurrence,
                                 "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)})
    return {"maven_coordinates": coordinates, "poms": poms, "license_evidence": licenses,
            "updater_digests": digests, "resources": resources,
            "duplicate_jar_members": {name: count for name, count in occurrences.items() if count > 1}}, candidates


def updater_database(payload):
    with gzip.GzipFile(fileobj=io.BytesIO(payload)) as stream:
        xml = stream.read(METADATA_LIMIT + 1)
    if len(xml) > METADATA_LIMIT:
        raise ValueError("Oversized reference updater database")
    root = ET.fromstring(xml)
    if root.tag != "pluginRecords":
        raise ValueError("Unsupported updater database root")
    sites, disabled, records = [], [], []
    historical_count = 0
    for element in root:
        if element.tag in {"update-site", "disabled-update-site"}:
            (sites if element.tag == "update-site" else disabled).append(dict(element.attrib))
        elif element.tag == "plugin":
            historical_count += len(element.findall("previous-version"))
            version = element.find("version")
            if version is not None:
                records.append({**element.attrib, "version": dict(version.attrib),
                                "dependencies": [dict(item.attrib) for item in version.findall("dependency")],
                                "platforms": [item.text for item in version.findall("platform")]})
        else:
            raise ValueError(f"Uninventoried updater record: {element.tag}")
    return {"sha256": hashlib.sha256(payload).hexdigest(), "xml_sha256": hashlib.sha256(xml).hexdigest(),
            "enabled_sites": sites, "disabled_sites": disabled, "current_records": records,
            "previous_version_count": historical_count}


def build_inventory(fiji_archive, imagej_archive, pins):
    verify_archive(fiji_archive, pins["fiji"]["sha256"])
    verify_archive(imagej_archive, pins["imagej"]["sha256"])
    files, candidates = [], []
    with zipfile.ZipFile(imagej_archive) as imagej:
        entries = zip_members(imagej)
        prefix = "ImageJ-" + pins["imagej"]["commit"] + "/"
        if any(not info.filename.startswith(prefix) for info in entries):
            raise ValueError("ImageJ source archive root disagrees with its commit pin")
        name = prefix + "IJ_Props.txt"
        data = limited_read(imagej, name)
        props = {"archive_member": name, "sha256": hashlib.sha256(data).hexdigest()}
        candidates.extend(imagej_commands(data, props))
    with zipfile.ZipFile(fiji_archive) as fiji:
        entries = zip_members(fiji)
        prefix = pins["fiji"]["root"] + "/"
        if any(not info.filename.startswith(prefix) for info in entries):
            raise ValueError("Fiji archive has unexpected roots")
        database_member = prefix + "db.xml.gz"
        db = updater_database(limited_read(fiji, database_member))
        updater = {row["filename"]: row for row in db["current_records"]}
        for info in entries:
            if info.is_dir():
                continue
            name = info.filename
            relative = name.removeprefix(prefix)
            with fiji.open(info) as stream:
                digest = _stream_sha256(stream)
            record = {"id": "fiji:" + relative, "path": relative, "bytes": info.file_size,
                      "sha256": digest, "kind": "file", "updater_record": updater.get(relative),
                      "commercial_review": {"decision": "pending", "code": "pending",
                                            "dependencies": "pending", "models_assets": "pending",
                                            "incorporation": "blocked_until_review"}}
            if name.endswith(".jar"):
                payload = limited_read(fiji, name, JAR_LIMIT)
                metadata, discovered = jar_metadata(payload, record["id"], name)
                record.update(metadata)
                record["kind"] = "jar"
                candidates.extend(discovered)
                digests = metadata["updater_digests"]
            else:
                with fiji.open(info) as stream:
                    h = hashlib.sha1(relative.encode("ascii", "replace"))
                    while chunk := stream.read(1024 * 1024):
                        h.update(chunk)
                digests = {"current": h.hexdigest()}
                if name.endswith((".ijm", ".py", ".groovy", ".bsh", ".js")):
                    record["kind"] = "script"
                    candidates.append(candidate(record["id"], "script", relative,
                                                {"path": relative}, {"archive_member": name, "sha256": digest}))
            if record["updater_record"]:
                version = record["updater_record"]["version"]
                matching = [kind for kind, checksum in digests.items() if checksum == version.get("checksum")]
                record["updater_reconciliation"] = {"digest_schemes": matching,
                    "size_matches": info.file_size == int(version.get("filesize", "-1")),
                    "status": ("matched" if info.file_size == int(version.get("filesize", "-1"))
                               else "digest_matches_size_differs") if matching else "mismatch"}
            files.append(record)
    present = {file["path"] for file in files}
    for row in db["current_records"]:
        row["bundled"] = row["filename"] in present
    reference_id = pins["reference_id"]
    reference = {"schema_version": 1, "reference_id": reference_id,
                 "status": "archive_pins_verified_inventory_partial", "acquired": pins["acquired"],
                 "imagej": {**pins["imagej"], "properties": props},
                 "fiji": {**pins["fiji"], "bundled_imagej_is_separate_from_core_pin": True,
                          "database_member": database_member, "database": db},
                 "incorporation_policy": "Reference acquisition does not approve code/models/assets for incorporation.",
                 "remaining": ["Runtime menus/tools/dialogs and all option/default/target inventories.",
                               "Exact source/parent/BOM/transitive licenses and every model/asset decision.",
                               "Formats, upstream fixtures, performance and platform manifests."]}
    components = {"schema_version": 1, "reference_id": reference_id,
                  "archive_sha256": pins["fiji"]["sha256"], "status": "candidates_pending_review",
                  "files": files, "dependency_graph": "Bundled updater and declared POM edges; alias/parent/BOM/transitive resolution pending."}
    commands = {"schema_version": 1, "reference_id": reference_id,
                "status": "static_candidates_pending_runtime_and_option_review",
                "scope": "ImageJ properties, legacy menus, every SciJava plugin descriptor and shipped scripts; service/Op/provider descriptors are retained for classification, not counted as editor commands.",
                "candidates": sorted(candidates, key=lambda row: row["id"])}
    if len({row["id"] for row in commands["candidates"]}) != len(commands["candidates"]):
        raise ValueError("Reference candidate ID collision")
    return {"references.json": reference, "fiji-components.json": components, "commands.json": commands}


def encoded_manifest(payload):
    return (json.dumps(payload, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")


def retained_extensions(previous, discovered):
    """Detect additions discovery cannot regenerate, including nested metadata."""
    if isinstance(previous, dict):
        if not isinstance(discovered, dict) or not previous.keys() <= discovered.keys():
            return True
        return any(retained_extensions(value, discovered[key])
                   for key, value in previous.items())
    if isinstance(previous, list):
        if not isinstance(discovered, list) or len(previous) > len(discovered):
            return True
        return any(retained_extensions(old, new)
                   for old, new in zip(previous, discovered))
    # Pending discovery placeholders may gain review metadata. Existing concrete
    # metadata must not be replaced by regenerated defaults or other values.
    if previous is None or (isinstance(previous, str) and previous in {
            "pending", "candidate", "blocked_until_review"}):
        return False
    return previous != discovered


def write_inventory(destination, documents, check=False):
    destination = Path(destination)
    if check:
        for name, payload in documents.items():
            if (destination / name).read_bytes() != encoded_manifest(payload):
                raise ValueError(f"Reference manifest is not reproducible: {name}")
        return
    # Discovery must not silently erase a later human license/option decision.
    # Reviewed records need a deliberate merge; --check never changes them.
    for name, payload in documents.items():
        target = destination / name
        if not target.is_file():
            continue
        previous = json.loads(target.read_text(encoding="utf-8"))
        if retained_extensions(previous, payload):
            raise ValueError(f"Enriched reference records require an explicit merge: {name}")
        if name == "fiji-components.json":
            reviewed = any(row["commercial_review"]["decision"] != "pending"
                           or row["commercial_review"].get("evidence")
                           or any(resource.get("license_review") != "pending"
                                  or resource.get("license_evidence")
                                  for resource in row.get("resources", []))
                           for row in previous["files"])
        elif name == "commands.json":
            reviewed = ("requirements_schema_version" in previous or bool(previous.get("requirements"))
                        or any(row["status"] != "candidate" or row["assignment"] is not None or
                           row["option_inventory"] is not None or row["runtime_reconciliation"] != "pending" or
                           row["commercial_review"] != "pending" for row in previous["candidates"]))
        else:
            reviewed = False
        if reviewed:
            raise ValueError(f"Reviewed reference records require an explicit merge: {name}")
    destination.mkdir(parents=True, exist_ok=True)
    staged = []
    try:
        for name, payload in documents.items():
            fd, temporary = tempfile.mkstemp(prefix=".reference-", dir=destination)
            staged.append((Path(temporary), destination / name))
            with os.fdopen(fd, "wb") as stream:
                stream.write(encoded_manifest(payload))
        for temporary, target in staged:
            os.replace(temporary, target)
    finally:
        for temporary, _ in staged:
            temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pins", type=Path, required=True)
    parser.add_argument("--fiji-archive", type=Path, required=True)
    parser.add_argument("--imagej-archive", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "docs/parity")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    pins = json.loads(args.pins.read_text(encoding="utf-8"))
    documents = build_inventory(args.fiji_archive, args.imagej_archive, pins)
    write_inventory(args.output, documents, args.check)
    files = documents["fiji-components.json"]["files"]
    print(f"Verified archive pins; inventoried {len(files)} files and {len(documents['commands.json']['candidates'])} static candidates.")
    print("Commercial, runtime, option and implementation reviews remain pending.")


if __name__ == "__main__":
    main()
