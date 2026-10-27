# Pinned ImageJ projection observations

The gzip contains the exact stdout from the project-owned Java observer running
the existing approved ImageJ1.54r classes and reviewed Zulu21 runtime. It contains
324 ordered cases:300 successful API results and24 explicit multi-RGB-channel
hyperstack rejections. Two fresh observer runs produced identical raw and gzip
hashes. Neither ImageJ nor its JDK was rebuilt or copied into another runtime.

This is independent reference data, not imagejxx numerical, document or GUI
acceptance. No Java GUI runs. Grouped API output and the additional public
calibration calls reproduced from the plugin source are separate snapshots;
the latter is not an observation of the GUI command being executed.

`contract.json` pins the raw/gzip bytes, observer/tool source, ordered cases and
reference input identities. All468 cached ImageJ classes still match the original
compile manifest, all581 JDK members match the reviewed runtime manifest, and
352 extracted Java source files match the pinned source archive. Inputs remain
unchanged after capture. All observed source image states remain unchanged.
Historical missing Gaussian/XYZ/range inputs and their required hashes remain
untouched; this corpus cannot replace those inputs.

| Input | Average | Max/min | Sum | SD | Median |
| --- | --- | --- | --- | --- | --- |
| UInt8 | UInt8 | UInt8 | Float32 | Float32 | UInt8 |
| UInt16 | UInt16 | UInt16 | Float32 | Float32 | Float32 |
| Float32 | Float32 | Float32 | Float32 | Float32 | Float32 |
| RGB | RGB | RGB | RGB | RGB | RGB |

The observations lock several distinctions that the existing native_finite
profile does not implement: integer average rounding; float32 sum accumulation
versus the float average path; sample SD; all-NaN min/max sentinels and zero SD;
signed16 sum/median calibration behavior; RGB clipping/normalization; one-Z time
projection dispatch; channel LUT/range handling; positioned/wildcard overlays;
and calibration copying/group postprocessing. A cast of the native Float64
result is insufficient. Tests check selected analytic results and metadata
directly without importing the processing implementation.

No unsupported source behavior is silently treated as success. The24 rejection
rows retain their exact exception type/message and ImageJ stack frames. Valid
single-RGB-channel hyperstacks are additionally observed. Other unexpected
exceptions fail capture. Native multiple-RGB-channel support remains an owned
extension; these reference rejections do not remove it.

Read the data without Java:

```python
import gzip, json
with gzip.open("docs/parity/reviews/G034-imagej-z-projection-reference001/observations.jsonl.gz", "rt", encoding="utf-8") as stream:
    rows = [json.loads(line) for line in stream]
```

The offline gate is `python -I -B tests/tools/test_z_projection_reference.py`.
Recapture uses `scripts/capture_z_projection_reference.py` with explicit existing
reference root, class/runtime receipts and a fresh owned output. The tool admits
only the pinned class/runtime manifests, compiles only the observer into scratch,
checks every capture and lossless transport, and retires scratch after terminal
process proof. It never overwrites this historical corpus.

Next implement a separately qualified ImageJ profile and metadata policy.
Preserve the existing native_finite outputs/records and all original acceptance
requirements. Supplement missing reference regimes with new artifacts; do not
repin this corpus or infer full method/nonfinite/metadata/UI coverage from it.
