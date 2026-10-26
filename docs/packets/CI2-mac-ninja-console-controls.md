# CI2: bounded Darwin Ninja console experiment

This is a test-only prerequisite for the native build ownership decision. It
does not enable a product console profile or qualify generated CMake graphs.
The existing history build wrapper still refuses Darwin before starting a build.

## Frozen experiment

Reuse the existing Darwin broker, group anchors, acknowledged kernel exit
witnesses and unrelated live control. The optional launcher seam preserves the
original eight lifecycle test bodies and their default Python routes. The new
class reuses fixture helpers without inheriting those test methods.

The mandatory `test_ci_mac_ninja_process.py --require-darwin` selector requires
the actual Darwin kernel, an explicit absolute executable in
`IMAGEJXX_NINJA_CONSOLE_CONTROL_EXE`, an owned successful `--version` result of
`1.13.2`, and an unchanged executable hash. It runs exactly six named cases:

| Case | Required observation |
| --- | --- |
| Normal console exit | Actual Ninja argv; payload is its direct child with the pinned group/session; terminal success and complete cleanup |
| Nonzero console exit | Payload exits 7; actual Ninja exits 1; complete owned cleanup |
| Surviving leaf | Acknowledged payload/leaf identities and kernel witnesses; Ninja exits successfully; remaining owned leaf is observed and drained |
| Timeout | The same acknowledged console route times out; kernel exits and group drain complete; unrelated live control survives |
| Owner death | Retained direct owner is killed; broker watch detects closure; actual Ninja/payload/leaf exit; unrelated control survives |
| Nonconsole refusal | Modified graph refuses before the graph owner call; no graph receipt or payload output. The separate setup version check is allowed |

The graph is one exact literal edge with `pool = console` and explicit shell
`exec`. It is not a parser or admission policy for arbitrary Ninja files.
Normal/nonzero cases check actual payload parent, PGID and SID; the remaining
executing cases also use acknowledged independent lifetime witnesses.

## Local evidence and hosted entry

Existing Windows/Linux runtimes pass the 21 original portable protocol/socket
methods and two new graph/native-entry metadata methods. Windows explicitly
skips four POSIX socket methods; Linux skips none. These runs do not execute the
six native cases. Receipts and unchanged source pins remain under
`.tmp/manual-workflow-verification/mac-ninja-portable-{windows,linux}001/`.
Both hosts also pass seven workflow argument/ordering controls; Linux explicitly
skips the Windows shell method. Those separate receipts use
`mac-ninja-workflow-{windows,linux}001/`.

The source-profile workflow installs the already reviewed Ninja tool at
`1.13.2` on macOS, resolves its absolute path, and requires the native selector
before the full pip build. An always-upload step retains the bounded control
log under a unique profile/Python artifact name. Existing eight-test Darwin,
macOS11 observation, installed IO/UI, direct SDK and complete-suite gates remain.
No local compiler, product, SDK or runtime was built or installed for this packet.

## Decision after actual native execution

Actual hosted results are still required. Even six passing cases establish only
this single-edge experiment. Broker death, failed bootstrap, generated root and
try_compile graphs, compiler/launcher routes, and complete product build/install
ownership remain open. Nonconsole containment remains unqualified.

If the experiment passes, evaluate one explicit Apple/Ninja profile using
console compile/link settings before `project()`, propagation into try_compile,
and terminal custom commands. Audit every reachable command edge and reject
uncovered edges or overrides before admitting a real build. Retain exact
configuration/tool/graph identities. Measure serialization cost against the
existing job time limits. Do not replace the broker with session enumeration or
claim a product pass from these metadata controls.

Original reference availability is a separate requirement; follow the
[seven-file recovery contract](CI2-original-reference-recovery.md).
