# Calibration fitting-point exchange

## Editor-session standards and unit defaults

Accepted fit submissions now remember their original measured/known points for
both fit dialogs in the same Window. Reuse survives source closure and takes
precedence over per-document fit-point prefill; the function/settings still come
from the existing document logic. Unit defaults are remembered after accepted
fit submission or successful manual/function Apply, and used only when the next
source unit is the default Gray Value. An explicit document unit takes precedence.

These are immutable numeric points and one string, with no document/provider
owner. Admission bounds limit numeric values to2*4096 doubles and the unit to
1MiB; this is bounded UI session state, not render-memory accounting or RSS.
Copy candidates are prepared before job admission/publication and installed by
no-throw swaps. Synchronous refusal, imported/unsubmitted drafts and dialog
Cancel do not replace defaults. Cancellation or failure after a fit has already
been admitted retains that submitted standard set, matching the accepted-OK
lifecycle rather than a successful-solver-only cache. State is not a new saved
preference/session-file schema and does not leak into another editor instance.

The pinned Calibrator source uses static xText/yText/unitSaved updated on dialog
OK. These native editor admission semantics do not claim all original rejected
OK, raw-text stripping or measured-mean autofill behavior. Measured means,
Calibrate consolidation, durable replay and native global calibration remain
open. [Global calibration contract](G034-global-calibration-contract.md).


This packet follows the pinned ImageJ1.54r Calibrator.open/save workflow. It
exchanges measured/known fitting standards. It does not replace a density
lookup table or apply calibration merely because a file was opened.

## Contract

Both polynomial and nonlinear fitting dialogs offer Open points and Save points.
One-column input replaces known values and preserves measured values; two-column
input replaces both. Comma/tab/ASCII whitespace separators, blank lines and an
optional all-nonnumeric first header row follow the inspected TextReader path.
Imported values pass through Float32 storage, including negative zero, before
being promoted into the existing fitting draft. Decimal exponent, signed and
hexadecimal floating-point spellings use locale-independent C++ conversion.

The native fitting boundary requires finite numbers, at least two data rows,
at most4096 rows and at most1MiB input. Complete parsing and preparation precede
draft swaps. Unequal row widths refuse. ImageJ TextReader can silently discard
a partial final row without a newline; that malformed-input compatibility is
not claimed here. Nonfinite original-reader values refuse before draft changes.

Save emits tab-separated measured/known columns. An absent column remains
empty; a shorter nonempty column is padded with zero, as in Calibrator.save.
The existing numeric JSON draft is serialized using round-trip double spelling.
This does not reproduce Calibrator.getData's lossy character stripping or claim
Java byte-for-byte numeric formatting. Lookup records and session persistence
retain their existing contracts.

The direct native editor actions are import_intensity_points(path),
export_intensity_points(path), and browse_intensity_points(save=Boolean).
Optional document_id must identify the active captured owner. These require an
open polynomial/nonlinear draft, idle host and unchanged all-revision binding.
No provider reads, document mutation, job or undo step occurs during point I/O.
OK submits the imported draft through the existing prepared fitting worker.

The file browser temporarily suspends the same draft; cancellation resumes it
unchanged. Opening another draft or changing source invalidates deferred file
callbacks. Save captures the current point text before browsing. Existing file
browser replacement confirmation and atomic writeBytes path are reused.

## Evidence and remaining acceptance

ObserveCalibrationPointFiles.java executes the actual pinned TextReader used
by Calibrator.open in a headless JVM. The retained twelve-case reference covers
one/two columns, headers, blanks, Float32 rounding, hexadecimal input, signed
zero, underflow, overflow, NaN, malformed input and insufficient rows. It does
not invoke private Calibrator GUI/file-chooser methods or qualify Save/global
calibration. Exact raw Float32 word comparisons consume that reference.

The dedicated persistent HeadlessInputSession workflow is capped at160 frames
and8MiB report, separate from the older606/608 calibration trace. Both hosts
physically cancel Open, load two-column and then one-column files, save through
a Unicode path, and press OK to fit; independent readers inspect saved bytes,
coefficients, source pixels, history and actual delivered controls.

Full Calibrate dialog consolidation, remembered/session standards and measured
means, global calibration, calibration replay/macro/downstream behavior and
remaining original/platform/performance gates stay open. No whole-family or
comparative speed acceptance follows from this packet.
