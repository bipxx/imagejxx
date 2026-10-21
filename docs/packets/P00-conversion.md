# P00: configurable conversion and uint16 inversion

Status: **verified increment**, run `m2-m6-conversion-options-windows-2026-10-10`, after the user's explicit resume on 2026-10-10. This packet verifies and repairs the staged increment; it does not certify complete configuration or image-type parity.

## Requirements and outcome

G007 Conversion defaults and G060 Image types, M2/M6, retain their full inventory requirements. Deliver configurable grayscale conversion and uint16 inversion in the C++ core, native operation registry, shared Dear ImGui panels and Python API. Preserve legacy `convert`/`grayscale` defaults, settings, workflows and sessions.

The slice includes scale on/off; RGB/RGBA weights; observed, dtype, explicit and display ranges; unsigned 16-bit ranges and observed/full-range inversion; whole-dataset type conversion; consumed defaults and frozen recorded parameters. Density/intensity-calibrated conversion, full plugin/LUT behavior and remaining type/configuration options remain outstanding.

## Historical entry and ownership

Accepted baseline: `m2-m6-binary-options-windows-2026-10-10`, registry 84, family states 34 in progress/65 open/0 complete. Existing conversion working code has registry 86 and successful native/wheel builds. The 390-case fixture is captured, not yet matched.

Root owns conversion production code, bindings, target/execution/settings/recording integration, package installation, catalog export and evidence records. The panel worker owns `tests/native/test_desktop.cpp`; the independent reviewer reads conversion code/fixtures only. Workers preserve other edits and do not run competing builds or installs.

## Contracts and verification

Use the headless contracts in `include/imagejxx/conversion.hpp`. Validate options and input before mutation, detach values before GIL release, and return isolated output. Conversion covers every C/Z/T position, preserves active selection, and never applies a type change to only part of a dataset. All relevant published work uses native revisions/transactions and one-step history. Previews, cancellation, invalid/stale targets and failures preserve source/history. Settings consumers and recorded actions freeze effective parameters.

Reference: standalone ImageJ 1.54r commit `62db90a269cccc1ac79f658e4602043214ed055b`; `tests/fixtures/conversion-imagej154r.json`, owned capture driver and archive hashes. The direct processor corpus does not certify calibrated, signed/LUT, stack-plugin or GUI behavior. Check exact dtype/pixel matches; native extensions have separate analytic tests.

The completed verification followed [the Windows production runbook](../SOL_ULTRA.md#windows-validation-runbook): native checks, installed 86-operation bootstrap, source catalog export, rebuild/install, focused conversion tests, actual native/Python panel inputs, full hidden-render/executable-interchange regression, installed SDK/desktop, licenses and final source/wheel/installed byte/hash checks. Reports under `build/plan-implementation/conversion-verify-001` are retained accepted evidence. Do not recreate that directory or replay its recording. Later repairs get fresh packet/report identities and preserve the original failures and accepted artifacts.

## Progress and next action

- Re-read current guide and build/reference logs; implementation resume is authorized.
- Native-panel tests and independent conversion review are delegated with separate ownership.
- Repaired half-up ShortProcessor limits, Float32 range normalization/zero reset and overflowing subnormal Float64 scales. Preserved all original 390 cases and appended 72 boundaries; all 462 match exactly.
- 521 focused and 4,132 full Python cases pass with no skips; 16 native groups and 12 installed SDK consumers pass, including actual shared-panel and synchronous/queued/held-preview cancellation checks.
- Installed standalone render, shipped-dependency review and Ruff F pass. Final recorded source/wheel/installed byte checks passed; tested extension hashes and final archives are retained in the report directory.
- The accepted registry/source catalog has 86 operations. G007/G060 are partial; family counts are 36 in progress/63 open/0 complete.

The unique recording helper has executed once and must never be replayed. Retained initial reference and focused failures identify the repaired numerical mismatch and detached-object test comparison. Next: P01a schemas/migration, preserving every family requirement and failing strict certification while gaps remain.
