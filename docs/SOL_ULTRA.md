# Execution guide

This is the short operating guide. [PLAN.md](PLAN.md) owns current scope, task
order, completion states, validation policy and waste decisions. Historical
instructions/results are preserved in [SOL_ULTRA_HISTORY.md](SOL_ULTRA_HISTORY.md).
Do not grow this file with packet histories or duplicate the roadmap.

## Resume card

Read the current gap summary and near-term queue in PLAN. Numerical analysis
kernels have recorded Windows/Linux verification; the later Windows CI consumer now passes its 77 original cases.
T00 still tracks publication and the full remote CI matrix. The last recorded checkout base is
8941468. Preserve pre-existing unrelated changes; inspect current state before
acting. Do not infer running processes or agents from old receipts or role names.

## Near-term delivery order

T00 current packet closeout, T01 conversion waste, T03 read-only analysis
preparation, T04 actual analysis UI. T02 removes demonstrated verification/build
waste alongside a delivery. Use the complete T05-T15 horizon in PLAN for the rest.
Expand only the next two executable tasks into detailed specifications.

## Bounded packet format and operating loop

For each task record: affected requirement IDs, user-visible outcome, API/UI/
settings/persistence/replay behavior, dependencies, file ownership, relevant
checks, storage needs and concrete exit. Reuse existing modules, native jobs,
CI/ownership helpers and reference inputs. Do not create another worker,
controller, codec or verification framework without a demonstrated gap.

Implement and validate the actual outcome, then apply
[task closeout and waste review](PLAN.md#task-closeout-and-waste-review).
Keep one current compiler cache/runtime/installed SDK per platform, fresh owned
scratch and bounded necessary evidence under AGENTS.md. A new failed run retains
its own diagnosis; a passed receipt requires successful required retention too.

## Select gates for the actual change

Use [the validation matrix](PLAN.md#validation-proportional-to-the-change).
Focused checks during development; broaden for changed shared behavior or an
integration/release boundary. SDK/platform rebuilds follow affected public,
dependency or packaging contracts. Prose alone does not trigger compilation.

## Recording and handoff

Update task states in PLAN with precise scope, a concise evidence link and the
waste decision. Preserve historical fingerprints, failed attempts and unavailable
artifact limitations. No speed claim from language choice or build timings.
[Three-role ownership](THREE_AGENT_EXECUTION.md) is a coordination template;
agent execution requires authorization under current instructions.

## Historical link compatibility

These older anchors forward to preserved historical sections. Current decisions
come from PLAN and the current sections above.

<a id="4-record-once-and-refresh-archives-after-metadata-changes"></a>
[Historical section: 4-record-once-and-refresh-archives-after-metadata-changes](SOL_ULTRA_HISTORY.md#4-record-once-and-refresh-archives-after-metadata-changes)

<a id="current-checkpoint"></a>
[Historical section: current-checkpoint](SOL_ULTRA_HISTORY.md#current-checkpoint)

<a id="execution-queue"></a>
[Historical section: execution-queue](SOL_ULTRA_HISTORY.md#execution-queue)

<a id="p01-option-schemas-and-real-certification-validators"></a>
[Historical section: p01-option-schemas-and-real-certification-validators](SOL_ULTRA_HISTORY.md#p01-option-schemas-and-real-certification-validators)

<a id="windows-validation-runbook"></a>
[Historical section: windows-validation-runbook](SOL_ULTRA_HISTORY.md#windows-validation-runbook)
