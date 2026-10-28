# Global calibration: observed semantics and implementation path

Status: original API behavior observed; native global calibration remains open.
The thirteen-case [reference](../parity/reviews/G034-global-calibration-reference002.json)
comes from actual pinned ImageJ1.54r ImagePlus/Calibration execution. It does not
invoke the private Calibrator dialog or qualify all calibration functions.
Reference001 is retained unchanged as encoding-limited evidence: the JVM stdout
encoding replaced the normalized micro-sign spatial unit with ?. Reference002
escapes JSON Unicode before printing and preserves the actual U+00B5 unit; use002
for global-calibration comparisons. Numeric/density fields agree in both runs.

## Observed contract

ImagePlus keeps global and local calibration separately. setGlobalCalibration
copies the supplied calibration; effective getCalibration copies it again and
calls setImage for the destination. Mutating the supplied object or a returned
effective copy does not change the global value. Existing and newly created
images see the global calibration. A per-image ignore flag returns local
calibration. Clearing global restores the unchanged local state.

For an8-bit global straight line10+2*x, spatial pixel width0.5 and value unit OD,
the observed effective result is:

| Destination | Width | Density function | Value unit | Value at raw1 |
| --- | ---: | --- | --- | ---: |
| UInt8 | 0.5 | straight line | OD | 12 |
| Unsigned UInt16 | 0.5 | none | Gray Value | 1 |
| Float32 | 0.5 | none | OD | 1 |
| RGB | 0.5 | none | Gray Value | 1 |
| Locally signed16 UInt16 | 0.5 | signed16 line | Gray Value | -32767 |

Ignoring the global setting on the original UInt8 returns its local width2;
clearing returns local widths2 and3 for the two existing integer images. Spatial
and density adaptation are separate. Do not approximate global with a metadata
edit applied to every open document, or merely add a checkbox to the current
per-document intensity dialog.

## Native core implementation order

1. Add a typed, immutable effective-calibration resolver beside existing
   SpatialCalibration/ImageJIntensityCalibration. Inputs are the original
   DocumentSnapshot, optional application global value, its source bit depth,
   destination ignore policy and calibration generation. Outputs are spatial
   and intensity values plus generation; no plane reads or dataset materializing.
   Preserve the source bit depth even for a None density record: dtype changes
   can affect its value unit. Reuse current typed function/lookup/none codecs.
   projected_imagej_intensity_calibration is explicitly a projection-output
   policy, not a generic dtype/global adapter; do not route global through it.
2. Own global value and generation in Application, with explicit copy/set/clear
   and local/effective read APIs exposed through existing Python bindings.
   Independent Application/editor sessions must not share a process-static
   C++ singleton. Document local calibration, pixel data and undo remain intact.
3. Capture the effective value once in target/operation requests. Retain the
   original owner and revisions independently; do not manufacture a snapshot
   whose overlaid metadata can be accidentally committed to local history.
   Application publication validates the captured calibration generation along
   with source/activation guards. Global changes invalidate stale prepared work.
4. Wire existing consumers before exposing a functional UI toggle: display and
   XYZ geometry, calibrated selection distances, measurements/profiles,
   histograms/thresholds, projection/derived output, fitting/plot captions and
   scientific I/O/session workflows. Every consumer must deliberately choose
   local or effective state. Reuse the shared resolver rather than per-panel
   fallback rules. Explicit standalone snapshot APIs keep their local contract.
5. Expose the Global calibration option through the shared ImGui calibration
   controls and Python facade. Editing local state while global is active,
   disabling global, source closure, new documents and per-document bypass
   require original-reference comparisons. Keep modeless jobs and stored
   snapshots bound to their captured generation and immutable values.

## API reuse map and completion gates

| Concern | Easiest existing path | Required proof |
| --- | --- | --- |
| Spatial values | SpatialCalibration in dataset.hpp | Axis/origin/unit/time/direction mapping; ImageJ pixel origins converted to world-space native origins |
| Density | ImageJIntensityCalibration codecs/functions | Original dtype adaptation, signed16 precedence, Float32 unit retention and lookup/function cases |
| Ownership | Application + existing document revision/activation guards | Set/clear never bulk-edits local documents; no source-provider retention |
| Commands | ResolvedTarget/prepared command paths | One effective capture, generation mismatch refusal, correct derived calibration |
| UI/Python | Existing Window/controller actions and bindings | Both actual hosts toggle/use/clear, new/open documents, cancellation and bypass |
| Persistence/replay | Existing session/workflow codecs | Explicit schema/migration and local/effective provenance; no silent local-state overwrite |

Extend the initial observer before claiming complete global semantics: a16-bit
source global applied to8/16-bit targets, custom tables/nonlinear/None/unit-only
records, signed16 variants, local edits while active, ignore/clear transitions,
spatial/time fields and original Calibrator/Set Scale UI. Match downstream
pixels/metadata/measurements, source/history invariants and stale-job behavior.
Full supported-platform/reference and comparative performance gates remain open.
This is a concrete next-packet contract, not implemented native global support.
