"""Partial parity schemas and lossless migration; these do not certify parity.

P01a validates structure and identities. Evidence, dependency, artifact and
complete-baseline certification remain the responsibility of P01b-P01d.
"""

from __future__ import annotations

from copy import deepcopy
import json
import math
import os
from pathlib import Path, PurePosixPath
import re
import tempfile

COLLECTIONS = {
    "formats.json": "formats",
    "fixtures.json": "fixtures",
    "benchmarks.json": "benchmarks",
    "platforms.json": "platforms",
}
REFERENCE_FILES = ("references.json", "fiji-components.json", "commands.json")
MILESTONES = {f"M{i}" for i in range(14)}
FAMILIES = {f"G{i:03d}" for i in range(1, 100)}


def fail(where, message):
    raise ValueError(f"{where}: {message}")


def obj(value, where):
    if not isinstance(value, dict):
        fail(where, "requires an object")
    return value


def array(value, where):
    if not isinstance(value, list):
        fail(where, "requires an array")
    return value


def text(value, where, empty=False):
    if not isinstance(value, str) or (not empty and not value.strip()):
        fail(where, "requires a nonempty string" if not empty else "requires a string")
    return value


def texts(value, where, nonempty=False):
    values = array(value, where)
    for item in values:
        text(item, where)
    if nonempty and not values:
        fail(where, "requires a nonempty array")
    if len(set(values)) != len(values):
        fail(where, "duplicate values")
    return values


def choice(value, allowed, where):
    if not isinstance(value, str) or value not in allowed:
        fail(where, "unknown value")


def version(value, where):
    if type(value) is not int or value != 1:
        fail(where, "unsupported schema version")


def identity(value, where):
    value = text(value, where)
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", value):
        fail(where, "invalid stable ID")
    return value


def rows(value, where, semantic=True):
    values = array(value, where)
    found = set()
    for row in values:
        obj(row, where)
        item = (
            identity(row.get("id"), where) if semantic else text(row.get("id"), where)
        )
        if item in found:
            fail(where, "duplicate stable ID")
        found.add(item)
    return values


def sha(value, where):
    if not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{64}", value):
        fail(where, "requires a lower-case SHA256")


def path(value, where):
    value = text(value, where)
    parts = value.split("/")
    if (
        "\\" in value
        or ":" in value
        or PurePosixPath(value).is_absolute()
        or any(part in {"", ".", ".."} for part in parts)
    ):
        fail(where, "requires a safe relative path")


def number(value, where, integer=False):
    if type(value) not in ({int} if integer else {int, float}):
        fail(where, "requires an integer" if integer else "requires a finite number")
    if not integer:
        try:
            finite = math.isfinite(value)
        except OverflowError:
            finite = False
        if not finite:
            fail(where, "requires a finite number")


def parameter(value, where):
    value = obj(value, where)
    identity(value.get("name"), where)
    kind = value.get("type")
    choice(kind, {"boolean", "integer", "number", "string"}, where)
    if "default" not in value:
        fail(where, "missing default")

    def typed(item):
        if kind == "boolean" and type(item) is not bool:
            fail(where, "boolean default/choice requires bool")
        if kind == "integer":
            number(item, where, integer=True)
        if kind == "number":
            number(item, where)
        if kind == "string":
            text(item, where, empty=True)

    typed(value["default"])
    text(value.get("units"), where, empty=True)
    text(value.get("enabled_when"), where)
    if "choices" in value:
        options = array(value["choices"], where)
        if not options:
            fail(where, "choices must be nonempty when supplied")
        for option in options:
            typed(option)
        if len({json.dumps(option, sort_keys=True) for option in options}) != len(
            options
        ):
            fail(where, "duplicate parameter choices")
        if value["default"] not in options:
            fail(where, "default is outside choices")
    for bound in ("minimum", "maximum"):
        if bound in value:
            if kind not in {"integer", "number"}:
                fail(where, "bounds require numeric parameters")
            number(value[bound], where, integer=kind == "integer")
    if (
        "minimum" in value
        and "maximum" in value
        and value["minimum"] > value["maximum"]
    ):
        fail(where, "inverted parameter bounds")
    for candidate in [value["default"], *value.get("choices", [])]:
        if (
            "minimum" in value
            and candidate < value["minimum"]
            or "maximum" in value
            and candidate > value["maximum"]
        ):
            fail(where, "default or choice is outside bounds")


def reference(value, refs, components, where):
    value = obj(value, where)
    if value.get("reference_id") != refs["reference_id"]:
        fail(where, "different pinned reference")
    component = value.get("component_id")
    if component not in components | {"imagej-core"}:
        fail(where, "orphaned component")
    expected = refs["imagej"] if component == "imagej-core" else refs["fiji"]
    if value.get("archive_sha256") != expected["sha256"]:
        fail(where, "different pinned archive")
    revision = text(value.get("revision"), where)
    if component == "imagej-core" and revision != refs["imagej"]["commit"]:
        fail(where, "different pinned ImageJ revision")
    text(value.get("scope"), where)
    members = array(value.get("source_members"), where)
    if not members:
        fail(where, "missing source member identity")
    names = set()
    for member in members:
        obj(member, where)
        path(member.get("path"), where)
        sha(member.get("sha256"), where)
        if member["path"] in names:
            fail(where, "duplicate source member")
        names.add(member["path"])


def status(value, where):
    choice(value, {"open", "partial", "complete"}, where)


def envelope(value, reference_id, where):
    obj(value, where)
    version(value.get("schema_version"), where)
    if value.get("reference_id") != reference_id:
        fail(where, "different pinned reference")
    choice(value.get("enumeration_status"), {"pending", "partial", "frozen"}, where)
    texts(value.get("remaining"), where)


def validate_documents(documents, family_ids=None):
    """Validate additive partial structures; legacy candidate manifests remain valid."""
    refs = obj(documents["references.json"], "references.json")
    reference_id = text(refs.get("reference_id"), "references.json")
    for name in REFERENCE_FILES:
        value = obj(documents[name], name)
        version(value.get("schema_version"), name)
        if value.get("reference_id") != reference_id:
            fail(name, "different pinned reference")
    for name in ("imagej", "fiji"):
        sha(obj(refs.get(name), "references.json " + name).get("sha256"), name)
    if not re.fullmatch(r"[0-9a-f]{40}", text(refs["imagej"].get("commit"), "imagej")):
        fail("imagej", "requires an immutable commit pin")
    if (
        documents["fiji-components.json"].get("archive_sha256")
        != refs["fiji"]["sha256"]
    ):
        fail("fiji-components.json", "different pinned archive")
    commands = documents["commands.json"]
    candidates = rows(commands.get("candidates"), "commands.json candidates")
    candidate_ids = {row["id"] for row in candidates}
    components = {
        row["id"]
        for row in rows(
            documents["fiji-components.json"].get("files"),
            "fiji-components.json files",
            False,
        )
    }
    for row in candidates:
        if row.get("component_id") not in components | {"imagej-core"}:
            fail(row["id"], "orphaned candidate component")
        if not row.get("evidence"):
            fail(row["id"], "missing candidate provenance")
    family_ids = FAMILIES if family_ids is None else set(family_ids)
    additions, requirements = [], []
    extension = {
        "requirements_schema_version",
        "requirements",
        "added_capabilities",
        "enumeration_status",
        "remaining",
    }
    if extension & commands.keys():
        if not extension <= commands.keys():
            fail("commands.json", "incomplete requirements extension")
        version(commands["requirements_schema_version"], "commands.json requirements")
        envelope(commands, reference_id, "commands.json requirements")
        additions = rows(commands["added_capabilities"], "added capabilities")
        for row in additions:
            if not row["id"].startswith("A."):
                fail(row["id"], "addition IDs must start with A.")
            for key in ("title", "required_work"):
                text(row.get(key), row["id"])
            if not set(texts(row.get("milestones"), row["id"], True)) <= MILESTONES:
                fail(row["id"], "unknown milestone")
            texts(row.get("remaining"), row["id"])
        parents = family_ids | {row["id"] for row in additions}
        requirements = rows(commands["requirements"], "child requirements")
        for row in requirements:
            where = row["id"]
            parent = row.get("parent_id")
            if parent not in parents or not where.startswith(parent + "."):
                fail(where, "orphaned or mismatched parent")
            if not set(texts(row.get("milestones"), where, True)) <= MILESTONES:
                fail(where, "unknown milestone")
            choice(
                row.get("kind"),
                {"option", "command", "interaction", "setting", "target"},
                where,
            )
            for key in ("title", "required_work"):
                text(row.get(key), where)
            links = texts(row.get("candidate_ids"), where)
            if not set(links) <= candidate_ids:
                fail(where, "orphaned candidate link")
            if not links:
                text(row.get("candidate_link_reason"), where)
            reference(row.get("reference"), refs, components, where)
            contract = obj(row.get("contract"), where)
            parameters = array(contract.get("parameters"), where)
            names = set()
            for value in parameters:
                parameter(value, where)
                if value["name"] in names:
                    fail(where, "duplicate parameter")
                names.add(value["name"])
            for key in ("pixel_types", "samples", "axes", "targets", "roi", "border"):
                texts(contract.get(key), where, True)
            for key in ("nonfinite", "geometry"):
                text(contract.get(key), where)
            interfaces = obj(row.get("interfaces"), where)
            for name in ("cpp", "python", "imgui"):
                surface = obj(interfaces.get(name), where)
                if type(surface.get("applicable")) is not bool:
                    fail(where, "interface applicability requires bool")
                texts(
                    surface.get("entry_points"), where, nonempty=surface["applicable"]
                )
                texts(surface.get("evidence"), where)
                if not surface["applicable"]:
                    text(surface.get("inapplicable_reason"), where)
            for consumer in array(row.get("settings_consumers"), where):
                obj(consumer, where)
                text(consumer.get("key"), where)
                text(consumer.get("consumer"), where)
            for key in ("fixture_ids", "verification_run_ids", "remaining"):
                texts(row.get(key), where)
            status(row.get("implementation_status"), where)
            choice(
                row.get("verification_status"),
                {"unverified", "partial", "verified"},
                where,
            )
            commercial = obj(row.get("commercial"), where)
            choice(
                commercial.get("decision"), {"pending", "eligible", "excluded"}, where
            )
            text(commercial.get("reason"), where)
            texts(
                commercial.get("evidence"),
                where,
                nonempty=commercial["decision"] != "pending",
            )
            provenance = obj(row.get("provenance"), where)
            choice(
                provenance.get("implementation"),
                {"owned_native_equivalent", "reused_upstream"},
                where,
            )
            incorporated = texts(provenance.get("incorporated_components"), where)
            if not set(incorporated) <= components | {"imagej-core"}:
                fail(where, "orphaned incorporated component")
    counts = {"requirements": len(requirements), "additions": len(additions)}
    for name, collection in COLLECTIONS.items():
        if name not in documents:
            continue  # Legacy partial inputs need not already have new manifests.
        manifest = documents[name]
        envelope(manifest, reference_id, name)
        entries = rows(manifest.get(collection), name)
        counts[collection] = len(entries)
        for row in entries:
            where = row["id"]
            texts(row.get("remaining"), where)
            if collection == "formats":
                text(row.get("parent_id"), where)
                if row["parent_id"] not in family_ids | {a["id"] for a in additions}:
                    fail(where, "orphaned format parent")
                for key in ("name", "variant"):
                    text(row.get(key), where)
                for key in (
                    "directions",
                    "axes",
                    "metadata",
                    "dependency_ids",
                    "fixture_ids",
                ):
                    texts(row.get(key), where)
                if not row["directions"] or not set(row["directions"]) <= {
                    "read",
                    "write",
                }:
                    fail(where, "invalid format directions")
                status(row.get("status"), where)
            elif collection == "fixtures":
                texts(row.get("requirement_ids"), where, True)
                path(row.get("path"), where)
                sha(row.get("sha256"), where)
                number(row.get("case_count"), where, integer=True)
                if row["case_count"] <= 0:
                    fail(where, "fixture case count must be positive")
                reference(row.get("reference"), refs, components, where)
                generator = obj(row.get("generator"), where)
                path(generator.get("path"), where)
                sha(generator.get("sha256"), where)
                text(row.get("scope"), where)
                comparison = obj(row.get("comparison"), where)
                choice(
                    comparison.get("method"),
                    {
                        "exact",
                        "float_tolerance",
                        "geometry",
                        "statistical",
                        "interaction",
                    },
                    where,
                )
                for key in ("atol", "rtol"):
                    number(comparison.get(key), where)
                    if comparison[key] < 0:
                        fail(where, "comparison tolerances must be nonnegative")
                if comparison["method"] == "exact" and (
                    comparison["atol"] or comparison["rtol"]
                ):
                    fail(where, "exact comparison requires zero tolerances")
                choice(row.get("capture_status"), {"captured", "generated"}, where)
                if row["capture_status"] == "generated":
                    if (row.get("origin") != "owned_deterministic"
                            or row.get("reference_execution") is not False):
                        fail(where, "generated fixture requires owned deterministic provenance and no reference execution")
                choice(
                    row.get("match_status"), {"unverified", "partial", "matched"}, where
                )
                texts(row.get("verification_run_ids"), where)
                license_row = obj(row.get("license"), where)
                choice(
                    license_row.get("decision"),
                    {"pending", "eligible", "excluded"},
                    where,
                )
                text(license_row.get("reason"), where)
                texts(
                    license_row.get("evidence"),
                    where,
                    nonempty=license_row["decision"] != "pending",
                )
            elif collection == "benchmarks":
                texts(row.get("requirement_ids"), where, True)
                texts(row.get("fixture_ids"), where)
                for key in ("workload", "hardware", "build"):
                    text(row.get(key), where)
                metrics = array(row.get("metrics"), where)
                if not metrics:
                    fail(where, "missing benchmark metrics")
                for metric in metrics:
                    obj(metric, where)
                    for key in ("name", "unit"):
                        text(metric.get(key), where)
                    number(metric.get("budget"), where)
                    if metric["budget"] < 0:
                        fail(where, "negative metric budget")
                    choice(metric.get("budget_status"), {"proposed", "ratified"}, where)
                texts(row.get("verification_run_ids"), where)
                status(row.get("status"), where)
            else:
                for key in ("os", "architecture", "compiler", "graphics"):
                    text(row.get(key), where)
                for key in (
                    "python_abis",
                    "products",
                    "checks",
                    "verification_run_ids",
                ):
                    texts(row.get(key), where)
                if not row["products"]:
                    fail(where, "platform must identify product variants")
                status(row.get("status"), where)
    return counts


def migrate_documents(documents, requirements=(), fixtures=(), additions=()):
    """Return a validated copy; existing/enriched values always survive seeding."""
    validate_documents(documents)
    proposed = deepcopy(documents)
    commands = proposed["commands.json"]
    commands.setdefault("requirements_schema_version", 1)
    commands.setdefault("enumeration_status", "partial")
    commands.setdefault("requirements", [])
    commands.setdefault("added_capabilities", [])
    commands.setdefault(
        "remaining",
        [
            "Full runtime/option enumeration and P01b-P01d certification remain required."
        ],
    )
    for name, collection in COLLECTIONS.items():
        proposed.setdefault(
            name,
            {
                "schema_version": 1,
                "reference_id": commands["reference_id"],
                "enumeration_status": "pending",
                collection: [],
                "remaining": [
                    f"Complete {collection} enumeration and the owning certification gates."
                ],
            },
        )
    # Validate even seeds whose IDs already exist. A later human-reviewed value
    # survives seeding, but invalid incoming records are never silently ignored.
    seed_view = deepcopy(proposed)
    seed_view["commands.json"]["requirements"] = list(deepcopy(requirements))
    by_id = {row["id"]: row for row in seed_view["commands.json"]["added_capabilities"]}
    addition_seeds = rows(list(deepcopy(additions)), "migration additions")
    by_id.update({row["id"]: row for row in addition_seeds})
    seed_view["commands.json"]["added_capabilities"] = list(by_id.values())
    seed_view["fixtures.json"]["fixtures"] = list(deepcopy(fixtures))
    validate_documents(seed_view)

    def merge(collection, seeds):
        seeds = rows(list(deepcopy(seeds)), "migration seeds")
        existing = {row["id"]: row for row in collection}
        for seed in seeds:
            if seed["id"] in existing:
                current = existing[seed["id"]]
                if (
                    "parent_id" in seed
                    and current.get("parent_id") != seed["parent_id"]
                ):
                    fail(seed["id"], "migration cannot replace stable parent identity")
                if "reference" in seed:
                    for key in (
                        "reference_id",
                        "component_id",
                        "archive_sha256",
                        "revision",
                    ):
                        if seed["reference"][key] != current["reference"][key]:
                            fail(
                                seed["id"],
                                "migration cannot replace stable reference identity",
                            )
                    member_keys = lambda value: [
                        (m["path"], m["sha256"]) for m in value["source_members"]
                    ]
                    if member_keys(seed["reference"]) != member_keys(
                        current["reference"]
                    ):
                        fail(
                            seed["id"],
                            "migration cannot replace stable parent/reference identity",
                        )
                # Seeding is not an update to later human review or unknown fields.
                continue
            collection.append(seed)
        collection.sort(key=lambda row: row["id"])

    merge(commands["added_capabilities"], additions)
    merge(commands["requirements"], requirements)
    merge(proposed["fixtures.json"]["fixtures"], fixtures)
    if fixtures and proposed["fixtures.json"]["enumeration_status"] == "pending":
        proposed["fixtures.json"]["enumeration_status"] = "partial"
    validate_documents(proposed)
    return proposed


def encoded(value):
    # Existing raw discovery descriptors can contain Infinity (SciJava priority).
    # Preserve those facts losslessly; typed options and budgets reject nonfinite
    # values in validation rather than altering unrelated candidate objects.
    return (
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True, allow_nan=True)
        + "\n"
    ).encode("utf-8")


def write_documents(directory, documents, check=False):
    """Validate and stage all inputs before writing; unchanged files stay untouched."""
    validate_documents(documents)
    payloads = {name: encoded(value) for name, value in documents.items()}
    if not set(payloads) <= set(REFERENCE_FILES) | set(COLLECTIONS):
        fail("migration", "unknown manifest filename")
    directory = Path(directory)
    changed = {
        name: value
        for name, value in payloads.items()
        if not (directory / name).is_file() or (directory / name).read_bytes() != value
    }
    if check:
        if changed:
            fail("migration", "manifests need migration: " + ", ".join(sorted(changed)))
        return []
    if not changed:
        return []
    directory.mkdir(parents=True, exist_ok=True)
    staged = []
    try:
        for name, value in changed.items():
            fd, temporary = tempfile.mkstemp(prefix=".parity-", dir=directory)
            staged.append((Path(temporary), directory / name))
            with os.fdopen(fd, "wb") as stream:
                stream.write(value)
        for temporary, destination in staged:
            os.replace(temporary, destination)
    finally:
        for temporary, _ in staged:
            temporary.unlink(missing_ok=True)
    return sorted(changed)
