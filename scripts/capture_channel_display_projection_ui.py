"""Bounded raw projection observations from the pinned installed C++ desktop.

Max M01--M10 remains the default. --projection min selects N01--N10.

Uses the existing --channel-display-ui-case entry and shared subprocess
supervisor. It checks transport/identity/resource bounds only; independent
pytest readers own all behavioral assertions. No build or installation occurs.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import time

from capture_roi_boolean_reference import ROOT, _run


CONTRACT = "p06c2.channel-display-projection-ui.001"
CASES = tuple(f"M{i:02d}" for i in range(1, 11))
MIN_CONTRACT = "p06c3.channel-display-min-ui.001"
MIN_CASES = tuple(f"N{i:02d}" for i in range(1, 11))
PARENT_SECONDS = 60
CASE_SECONDS = 15
MAX_REPORT = 16 * 1024**2
MAX_ARCHIVE = 32 * 1024**2
LIMITS = {"max_frames":180, "max_seconds":15, "max_report_bytes":MAX_REPORT,
          "max_archive_bytes":MAX_ARCHIVE, "max_documents":16,
          "max_shape_tczyxs":[2,9,4,3,5,4]}


def _reject_links(path: Path) -> None:
    for member in (path, *path.parents):
        if member.is_symlink():
            raise ValueError(f"Symbolic links are not capture inputs/outputs: {member}")


def _sha(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def _pin(path: Path) -> dict:
    return {"path":str(path), "bytes":path.stat().st_size, "sha256":_sha(path)}


def _write(path: Path, value: dict) -> None:
    # Exclusive creation preserves each terminal attempt and its start receipt.
    raw = (json.dumps(value, sort_keys=True, indent=2, allow_nan=False)+"\n").encode("utf-8")
    with path.open("xb") as stream:
        stream.write(raw)
        stream.flush()
        os.fsync(stream.fileno())


def _pairs(values):
    result = {}
    for key,value in values:
        if key in result:
            raise ValueError(f"Duplicate report field: {key}")
        result[key] = value
    return result


def _nonfinite(value):
    raise ValueError(f"Nonfinite JSON token: {value}")


def _number(value, high):
    return type(value) in {int,float} and math.isfinite(value) and 0 <= value <= high


def _observe_table_pool(row: dict) -> None:
    """Check only the N transport grammar; never infer a LUT or RGB value."""
    pool = row.get("observed_lut_tables")
    if row.get("table_encoding") != "observed-lut-table-refs.001" or not isinstance(pool,list):
        raise ValueError("Missing lossless observed LUT table transport")
    unique = set()
    for table in pool:
        if (not isinstance(table,list) or len(table) != 256 or
                any(not isinstance(rgb,list) or len(rgb) != 3 or
                    any(type(value) is not int or not 0 <= value <= 255 for value in rgb) for rgb in table)):
            raise ValueError("Observed LUT pool requires256x3 integerbytearrays")
        unique.add(bytes(value for rgb in table for value in rgb))
    if len(unique) != len(pool):
        raise ValueError("Repeated observed LUT pool entry")
    used = set()

    def visit(node,lut=False):
        if isinstance(node,dict):
            if "observed_table_ref" in node:
                raise ValueError("Observed LUT reference outside a LUT table")
            if lut and "table" in node:
                value = node["table"]
                if (set(node) != {"table"} or not isinstance(value,dict) or set(value) != {"observed_table_ref"} or
                        type(value["observed_table_ref"]) is not int or not 0 <= value["observed_table_ref"] < len(pool)):
                    raise ValueError("Malformed observed LUT table reference")
                used.add(value["observed_table_ref"])
            else:
                for key,value in node.items():
                    if key != "observed_lut_tables":
                        visit(value,key == "lut")
        elif isinstance(node,list):
            for child in node:
                visit(child)

    visit(row)
    if used != set(range(len(pool))):
        raise ValueError("Unused observed LUT pool entry")


def _observe(path: Path, case: str, contract: str = CONTRACT) -> dict:
    """Validate the raw envelope, never decide pixel/state/input parity."""
    _reject_links(path)
    if not path.is_file() or not 0 < path.stat().st_size <= MAX_REPORT:
        raise ValueError(f"Missing or oversized raw report: {path}")
    raw = path.read_bytes()
    if len(raw) > MAX_REPORT:
        raise ValueError("Raw report grew beyond16MiB")
    row = json.loads(raw, object_pairs_hook=_pairs, parse_constant=_nonfinite)
    if not isinstance(row,dict) or type(row.get("schema_version")) is not int or any(row.get(key) != value for key,value in (
            ("schema_version",1),("contract_id",contract),("case_id",case),
            ("context","imgui_cpp"),("observation_state","observed"))):
        raise ValueError(f"Unexpected raw report identity/envelope: {case}")
    if row.get("acceptance_claim") is not False or row.get("limits") != LIMITS:
        raise ValueError(f"Unexpected acceptance or fixed limits: {case}")
    if contract == MIN_CONTRACT:
        _observe_table_pool(row)
    count = row.get("frame_count")
    if type(count) is not int or not 1 <= count <= 180 or not isinstance(row.get("frames"),list) or count != len(row["frames"]):
        raise ValueError(f"Unexpected frame bound: {case}")
    if not _number(row.get("elapsed_seconds"),CASE_SECONDS):
        raise ValueError(f"Unexpected case elapsed bound: {case}")
    states = row.get("states")
    if not isinstance(states,dict) or not states:
        raise ValueError(f"Missing raw owner states: {case}")
    for state in states.values():
        if not isinstance(state,dict) or not isinstance(state.get("document_ids"),list) or len(state["document_ids"]) > 16:
            raise ValueError(f"Unexpected native document bound: {case}")
        if len(state["document_ids"]) != len(set(state["document_ids"])):
            raise ValueError(f"Repeated owner identity: {case}")
    artifacts = row.get("files")
    if not isinstance(artifacts,dict):
        raise ValueError(f"Missing raw artifact descriptors: {case}")
    archive_bytes = 0
    for descriptor in artifacts.values():
        if not isinstance(descriptor,dict) or set(descriptor) != {"member","byte_count","hex"}:
            raise ValueError(f"Invalid raw artifact descriptor: {case}")
        name,size = descriptor["member"],descriptor["byte_count"]
        if (not isinstance(name,str) or not name or name in {".",".."} or
                "/" in name or "\\" in name or ":" in name or Path(name).name != name or
                type(size) is not int or not 0 < size <= MAX_ARCHIVE):
            raise ValueError(f"Unsafe/oversized raw artifact: {case}")
        archive_bytes += size
        if archive_bytes > MAX_ARCHIVE:
            raise ValueError(f"Retained archives exceed32MiB: {case}")
        member = path.parent/name
        _reject_links(member)
        if not member.is_file() or member.stat().st_size != size:
            raise ValueError(f"Missing or changed raw artifact: {case}/{name}")
        if not isinstance(descriptor["hex"],str) or len(descriptor["hex"]) != 2*size or member.read_bytes().hex() != descriptor["hex"]:
            raise ValueError(f"Raw artifact byte identity differs: {case}/{name}")
    return {"case_id":case, "id":case, "byte_count":len(raw), "sha256":hashlib.sha256(raw).hexdigest()}


def capture(stage: Path, executable: Path, expected_sha: str, projection: str = "max") -> Path:
    if projection not in {"max","min"}:
        raise ValueError("Projection must be max or min")
    case_ids = CASES if projection == "max" else MIN_CASES
    contract = CONTRACT if projection == "max" else MIN_CONTRACT
    started = time.monotonic()
    if re.fullmatch(r"[0-9a-fA-F]{64}",expected_sha or "") is None:
        raise ValueError("A complete installed IMAGEJXX_CPP_EXE_SHA256 pin is required")
    expected_sha = expected_sha.lower()
    _reject_links(executable)
    executable = executable.resolve(strict=True)
    if executable.parent.name.lower() != "bin" or executable.name.lower() not in {"imagejxx","imagejxx.exe"}:
        raise ValueError("Use the installed bin/imagejxx executable")
    if not executable.is_file() or not 0 < executable.stat().st_size <= 512*1024**2:
        raise ValueError("Installed executable is missing or exceeds its512MiB input bound")
    _reject_links(stage)
    stage = stage.resolve()
    build = (ROOT/"build").resolve(strict=True)
    if stage == build or not stage.is_relative_to(build) or stage.exists():
        raise ValueError("Require a fresh stage below this workspace's build directory")
    # Include the longest protocol filename and supervisor log suffix in this
    # conservative Windows path guard; do not enlarge the frozen viewport.
    longest = str(stage/"10"/"observed"/"forbidden-v2.ijx")
    if len(longest.encode("utf-16-le"))//2 > 240:
        raise ValueError("Capture stage leaves insufficient Windows path headroom")
    stage.mkdir(parents=True,exist_ok=False)
    receipt = {"schema_version":1,"kind":"channel_display_projection_ui_capture" if projection == "max" else "channel_display_min_ui_capture",
        "contract_id":contract,"context":"imgui_cpp","acceptance_claim":False,
        "ordered_case_ids":list(case_ids),"case_count":10,"parent_timeout_seconds":PARENT_SECONDS,
        "case_limits":LIMITS,"executable":_pin(executable),"executable_expected_sha256":expected_sha,"stage":str(stage),
        "tool":_pin(Path(__file__).resolve()),
        "supervisor":_pin(ROOT/"scripts/capture_roi_boolean_reference.py"),"runs":[]}
    _write(stage/"receipt-start.json",{**receipt,"observation_state":"started"})
    cases = []
    try:
        if receipt["executable"]["sha256"] != expected_sha:
            raise ValueError("Installed executable bytes do not match the explicit SHA256 pin")
        for ordinal,case in enumerate(case_ids,1):
            if _sha(executable) != expected_sha:
                raise ValueError("Installed executable changed during this attempt")
            remaining = PARENT_SECONDS-(time.monotonic()-started)
            # The shared supervisor allows up to10s process wait plus10s
            # reader cleanup. Reserve that existing bound inside the parent.
            timeout = min(CASE_SECONDS,remaining-20)
            if timeout <= 0:
                raise TimeoutError("No parent time remains for another safely supervised case")
            directory = stage/f"{ordinal:02d}"
            directory.mkdir()
            observed = directory/"observed"
            command = [str(executable),"--channel-display-ui-case",case,str(observed)]
            receipt["current_run"] = {"case_id":case,"argv":command,"timeout_seconds":timeout,
                "stdout":str(directory/"capture.stdout"),"stderr":str(directory/"capture.stderr")}
            run = _run(command,directory,"capture",timeout)
            receipt["runs"].append({"case_id":case,**run})
            path = observed/"case.json"
            entry = _observe(path,case,contract)
            entry["relative_path"] = path.relative_to(stage).as_posix()
            cases.append(entry)
            receipt.pop("current_run")
            if time.monotonic()-started > PARENT_SECONDS:
                raise TimeoutError("Raw ten-case parent exceeded60seconds")
        if _sha(executable) != expected_sha:
            raise ValueError("Installed executable changed before final observation")
        elapsed = time.monotonic()-started
        if elapsed > PARENT_SECONDS:
            raise TimeoutError("Raw ten-case parent exceeded60seconds")
        index = {"contract_id":contract,"context":"imgui_cpp","case_count":10,
            "observation_state":"observed","acceptance_claim":False,"elapsed_seconds":elapsed,"cases":cases}
        index_path = stage/"index.json"
        _write(index_path,index)
        _write(stage/"receipt-final.json",{**receipt,"observation_state":"observed",
            "elapsed_seconds":time.monotonic()-started,"index":_pin(index_path)})
        return index_path
    except BaseException as error:
        _write(stage/"receipt-failed.json",{**receipt,"observation_state":"failed",
            "elapsed_seconds":time.monotonic()-started,"observed_case_ids":[row["case_id"] for row in cases],
            "error_type":type(error).__name__,"error":str(error),
            "logs":"Original capture.stdout/stderr and case-failed.json remain in each numbered directory"})
        raise


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--projection",choices=("max","min"),default="max",help="max=M01--M10 (default), min=N01--N10")
    parser.add_argument("--stage",type=Path,required=True,help="fresh excluded build/... attempt directory")
    parser.add_argument("--executable",type=Path,default=os.environ.get("IMAGEJXX_CPP_EXE"))
    parser.add_argument("--sha256",default=os.environ.get("IMAGEJXX_CPP_EXE_SHA256"))
    args = parser.parse_args()
    if args.executable is None or args.sha256 is None:
        parser.error("Set --executable/--sha256 or IMAGEJXX_CPP_EXE/IMAGEJXX_CPP_EXE_SHA256")
    print(capture(args.stage,args.executable,args.sha256,args.projection))


if __name__ == "__main__":
    main()
