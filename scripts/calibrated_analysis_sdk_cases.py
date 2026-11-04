"""Convert retained original observations to a small stdlib-only SDK carrier."""
from pathlib import Path
import gzip
import hashlib
import json


def write_cases(path):
    root = Path(__file__).resolve().parents[1]
    fixtures = [root / f"docs/parity/reviews/G034-calibrated-analysis-reference{i:03}.jsonl.gz" for i in (1, 2, 3)]
    lines = ["imagejxx-calibrated-analysis-1", "77"]
    inputs = []
    fields = ("pixel_count", "area", "mean", "raw_mean", "stddev", "minimum", "maximum", "median", "mode_index", "mode_value", "integrated_density")
    for batch, source in enumerate(fixtures, 1):
        raw = source.read_bytes()
        inputs.append({"path": str(source), "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()})
        for row in map(json.loads, gzip.decompress(raw).splitlines()):
            kind = "function" if batch == 3 else row.get("kind", "statistics")
            membership = row.get("membership", "roi" if row.get("roi", False) else "full")
            density = row["mode"]
            ignored = row.get("ignore", False)
            if ignored or row["type"] == "float32":
                density = "raw"
            width, height = (3, 4) if ignored else (.5, 1.25)
            lines.append(" ".join(map(str, [row["id"], kind, row["type"], density, membership, width, height,
                int(row.get("interpolate", False)), int(row.get("diagonal", False))])))
            if kind == "profile":
                values = row["values"]
                lines.append(" ".join(map(str, [len(values), *values])))
            elif kind == "function":
                lines.append(" ".join(str(row[k]) for k in ("mean", "stddev", "minimum", "maximum", "median")))
            else:
                lines.append(" ".join(str(row[k]) for k in fields))
                for key in ("histogram", "histogram16"):
                    pairs = row[key] or []
                    lines.append(" ".join(map(str, [len(pairs), *[v for pair in pairs for v in pair]])))
    payload = ("\n".join(lines) + "\n").encode("ascii")
    path = Path(path)
    with path.open("xb") as stream:
        stream.write(payload)
    return {"inputs": inputs, "cases": 77, "bytes": len(payload), "sha256": hashlib.sha256(payload).hexdigest()}
